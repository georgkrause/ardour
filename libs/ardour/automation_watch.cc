/*
    Copyright (C) 2012 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <iostream>

#include "pbd/compose.h"

#include "ardour/automation_control.h"
#include "ardour/automation_watch.h"
#include "ardour/debug.h"
#include "ardour/session.h"

using namespace ARDOUR;
using namespace PBD;
using std::cerr;
using std::endl;

AutomationWatch* AutomationWatch::_instance = 0;

AutomationWatch&
AutomationWatch::instance ()
{
	if (_instance == 0) {
		_instance = new AutomationWatch;
	}
	return *_instance;
}

AutomationWatch::AutomationWatch ()
	: _thread (0)
	, _run_thread (false)
{
	
}

AutomationWatch::~AutomationWatch ()
{
	if (_thread) {
		_run_thread = false;
		_thread->join ();
		_thread = 0;
	}

	Glib::Mutex::Lock lm (automation_watch_lock);
	automation_watches.clear ();
}

void
AutomationWatch::add_automation_watch (boost::shared_ptr<AutomationControl> ac)
{
	Glib::Mutex::Lock lm (automation_watch_lock);
	DEBUG_TRACE (DEBUG::Automation, string_compose ("now watching control %1 for automation\n", ac->name()));
	automation_watches.push_back (ac);

	/* we can't store shared_ptr<Destructible> in connections because it
	 * creates reference cycles. we don't need to make the weak_ptr<>
	 * explicit here, but it helps to remind us what is going on.
	 */
	
	boost::weak_ptr<AutomationControl> wac (ac);
	ac->DropReferences.connect_same_thread (*this, boost::bind (&AutomationWatch::remove_weak_automation_watch, this, wac));
}

void
AutomationWatch::remove_weak_automation_watch (boost::weak_ptr<AutomationControl> wac)
{
	boost::shared_ptr<AutomationControl> ac = wac.lock();

	if (!ac) {
		return;
	}

	remove_automation_watch (ac);
}

void
AutomationWatch::remove_automation_watch (boost::shared_ptr<AutomationControl> ac)
{
	Glib::Mutex::Lock lm (automation_watch_lock);
	DEBUG_TRACE (DEBUG::Automation, string_compose ("remove control %1 from automation watch\n", ac->name()));
	automation_watches.remove (ac);
}

gint
AutomationWatch::timer ()
{
	if (!_session || !_session->transport_rolling()) {
		return TRUE;
	}

	{
		Glib::Mutex::Lock lm (automation_watch_lock);
		
		framepos_t time = _session->audible_frame ();

		for (AutomationWatches::iterator aw = automation_watches.begin(); aw != automation_watches.end(); ++aw) {
			(*aw)->list()->add (time, (*aw)->user_double(), true);
		}
	}

	return TRUE;
}

void
AutomationWatch::thread ()
{
	while (_run_thread) {
		usleep (100000); // Config->get_automation_interval() * 10);
		timer ();
	}
}

void
AutomationWatch::set_session (Session* s)
{
	if (_thread) {
		_run_thread = false;
		_thread->join ();
		_thread = 0;
	}

	SessionHandlePtr::set_session (s);

	if (_session) {
		_run_thread = true;
		_thread = Glib::Thread::create (boost::bind (&AutomationWatch::thread, this),
						500000, true, true, Glib::THREAD_PRIORITY_NORMAL);
		
	}
}