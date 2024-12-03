/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright            : (C) 2006 by Frank Mori Hess
 ***************************************************************************/

#ifndef _GPIB_STATE_MACHINES_H
#define _GPIB_STATE_MACHINES_H

enum talker_function_state {
	talker_idle,
	talker_addressed,
	talker_active,
	serial_poll_active
};

enum listener_function_state {
	listener_idle,
	listener_addressed,
	listener_active
};

#endif	// _GPIB_STATE_MACHINES_H
