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

#ifndef __SPAR_MOUSECHANNEL_H__
#define __SPAR_MOUSECHANNEL_H__

#include <linux/kernel.h>
#include <linux/uuid.h>

#include "channel.h"
#include "ultrainputreport.h"

/* {addf07d4-94a9-46e2-81c3-61abcdbdbd87} */
#define SPAR_MOUSE_CHANNEL_PROTOCOL_UUID  \
	UUID_LE(0xaddf07d4, 0x94a9, 0x46e2, \
		0x81, 0xc3, 0x61, 0xab, 0xcd, 0xbd, 0xbd, 0x87)
#define SPAR_MOUSE_CHANNEL_PROTOCOL_UUID_STR \
	"addf07d4-94a9-46e2-81c3-61abcdbdbd87"
#define SPAR_MOUSE_CHANNEL_PROTOCOL_VERSIONID 1
#define MOUSE_MAXINPUTREPORTS 50

#endif
