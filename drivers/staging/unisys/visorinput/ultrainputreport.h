/* Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __SPAR_ULTRAINPUTREPORT_H__
#define __SPAR_ULTRAINPUTREPORT_H__

#include <linux/types.h>

#include "ultrainputreport.h"

/* Identifies mouse and keyboard activity which is specified by the firmware to
 *  the host using the cmsimpleinput protocol.  @ingroup coretypes
 */
enum ultra_inputaction {
	inputaction_none = 0,
	inputaction_xy_motion = 1,	/* only motion; arg1=x, arg2=y */
	inputaction_mouse_button_down = 2, /* arg1: 1=left,2=center,3=right */
	inputaction_mouse_button_up = 3, /* arg1: 1=left,2=center,3=right */
	inputaction_mouse_button_click = 4, /* arg1: 1=left,2=center,3=right */
	inputaction_mouse_button_dclick = 5, /* arg1: 1=left,2=center,
					      * 3=right
					      */
	inputaction_wheel_rotate_away = 6, /* arg1: wheel rotation away from
					    * user
					    */
	inputaction_wheel_rotate_toward = 7, /* arg1: wheel rotation toward
					      * user
					      */
	inputaction_set_max_xy = 8,	/* set screen maxXY; arg1=x, arg2=y */
	inputaction_key_down = 64,	/* arg1: scancode, as follows:
					 * If arg1 <= 0xff, it's a 1-byte
					 * scancode and arg1 is that scancode.
					 * If arg1 > 0xff, it's a 2-byte
					 * scanecode, with the 1st byte in the
					 * low 8 bits, and the 2nd byte in the
					 * high 8 bits.  E.g., the right ALT key
					 * would appear as x'38e0'.
					 */
	inputaction_key_up = 65,	/* arg1: scancode (in same format as
					 * inputaction_keyDown)
					 */
	inputaction_set_locking_key_state = 66,
					/* arg1: scancode (in same format
					 *	 as inputaction_keyDown);
					 *	 MUST refer to one of the
					 *	 locking keys, like capslock,
					 *	 numlock, or scrolllock
					 * arg2: 1 iff locking key should be
					 *	 in the LOCKED position
					 *	 (e.g., light is ON)
					 */
	inputaction_key_down_up = 67,	/* arg1: scancode (in same format
					 *	 as inputaction_keyDown)
					 */
	inputaction_last
};

struct ultra_inputactivity {
	u16 action;
	u16 arg1;
	u16 arg2;
	u16 arg3;
} __packed;

struct ultra_inputreport {
	u64 seq_no;
	struct ultra_inputactivity activity;
} __packed;

#endif
