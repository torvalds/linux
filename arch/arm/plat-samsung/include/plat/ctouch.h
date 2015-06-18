/*
 * include/mach/ctouch.h
 *
 * Copyright (C) 2015 FriendlyARM (www.arm9.net)
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __PLAT_CTOUCH_H__
#define __PLAT_CTOUCH_H__

enum {
	CTP_NONE = 0,
	CTP_GT80X,
	CTP_FT5X06,
	CTP_AUTO,
	CTP_MAX
};

extern unsigned int mini2451_get_ctp(void);

#endif	// __PLAT_CTOUCH_H__

