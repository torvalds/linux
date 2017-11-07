// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 */

#ifndef __SPAR_ULTRAINPUTREPORT_H__
#define __SPAR_ULTRAINPUTREPORT_H__

#include <linux/types.h>

/* These defines identify mouse and keyboard activity which is specified by the
 * firmware to the host using the cmsimpleinput protocol.  @ingroup coretypes
 */
	/* only motion; arg1=x, arg2=y */
#define INPUTACTION_XY_MOTION 1
/* arg1: 1=left,2=center,3=right */
#define INPUTACTION_MOUSE_BUTTON_DOWN 2
/* arg1: 1=left,2=center,3=right */
#define INPUTACTION_MOUSE_BUTTON_UP 3
/* arg1: 1=left,2=center,3=right */
#define INPUTACTION_MOUSE_BUTTON_CLICK 4
/* arg1: 1=left,2=center 3=right */
#define INPUTACTION_MOUSE_BUTTON_DCLICK 5
/* arg1: wheel rotation away from user */
#define INPUTACTION_WHEEL_ROTATE_AWAY 6
/* arg1: wheel rotation toward user */
#define INPUTACTION_WHEEL_ROTATE_TOWARD 7
/* arg1: scancode, as follows: If arg1 <= 0xff, it's a 1-byte scancode and arg1
 *	 is that scancode. If arg1 > 0xff, it's a 2-byte scanecode, with the 1st
 *	 byte in the low 8 bits, and the 2nd byte in the high 8 bits.
 *	 E.g., the right ALT key would appear as x'38e0'.
 */
#define INPUTACTION_KEY_DOWN 64
/* arg1: scancode (in same format as inputaction_keyDown) */
#define INPUTACTION_KEY_UP 65
/* arg1: scancode (in same format as inputaction_keyDown); MUST refer to one of
 *	 the locking keys, like capslock, numlock, or scrolllock.
 * arg2: 1 iff locking key should be in the LOCKED position (e.g., light is ON)
 */
#define INPUTACTION_SET_LOCKING_KEY_STATE 66
/* arg1: scancode (in same format as inputaction_keyDown */
#define INPUTACTION_KEY_DOWN_UP 67

struct visor_inputactivity {
	u16 action;
	u16 arg1;
	u16 arg2;
	u16 arg3;
} __packed;

struct visor_inputreport {
	u64 seq_no;
	struct visor_inputactivity activity;
} __packed;

#endif
