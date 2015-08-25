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

#ifndef __SPAR_KEYBOARDCHANNEL_H__
#define __SPAR_KEYBOARDCHANNEL_H__

#include <linux/kernel.h>
#include <linux/uuid.h>

#include "channel.h"
#include "ultrainputreport.h"

/* {c73416d0-b0b8-44af-b304-9d2ae99f1b3d} */
#define SPAR_KEYBOARD_CHANNEL_PROTOCOL_UUID				\
	UUID_LE(0xc73416d0, 0xb0b8, 0x44af,				\
		0xb3, 0x4, 0x9d, 0x2a, 0xe9, 0x9f, 0x1b, 0x3d)
#define SPAR_KEYBOARD_CHANNEL_PROTOCOL_UUID_STR "c73416d0-b0b8-44af-b304-9d2ae99f1b3d"
#define SPAR_KEYBOARD_CHANNEL_PROTOCOL_VERSIONID 1
#define KEYBOARD_MAXINPUTREPORTS 50

#endif
