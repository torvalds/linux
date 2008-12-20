/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "global.h"
struct IODATA COMMON_INIT_TBL_VT1636[] = {
/*  Index, Mask, Value */
	/* Set panel power sequence timing */
	{0x10, 0xC0, 0x00},
	/* T1: VDD on - Data on. Each increment is 1 ms. (50ms = 031h) */
	{0x0B, 0xFF, 0x40},
	/* T2: Data on - Backlight on. Each increment is 2 ms. (210ms = 068h) */
	{0x0C, 0xFF, 0x31},
	/* T3: Backlight off -Data off. Each increment is 2 ms. (210ms = 068h)*/
	{0x0D, 0xFF, 0x31},
	/* T4: Data off - VDD off. Each increment is 1 ms. (50ms = 031h) */
	{0x0E, 0xFF, 0x68},
	/* T5: VDD off - VDD on. Each increment is 100 ms. (500ms = 04h) */
	{0x0F, 0xFF, 0x68},
	/* LVDS output power up */
	{0x09, 0xA0, 0xA0},
	/* turn on back light */
	{0x10, 0x33, 0x13}
};

struct IODATA DUAL_CHANNEL_ENABLE_TBL_VT1636[] = {
/*  Index, Mask, Value */
	{0x08, 0xF0, 0xE0}	/* Input Data Mode Select */
};

struct IODATA SINGLE_CHANNEL_ENABLE_TBL_VT1636[] = {
/*  Index, Mask, Value */
	{0x08, 0xF0, 0x00}	/* Input Data Mode Select */
};

struct IODATA DITHERING_ENABLE_TBL_VT1636[] = {
/*  Index, Mask, Value */
	{0x0A, 0x70, 0x50}
};

struct IODATA DITHERING_DISABLE_TBL_VT1636[] = {
/*  Index, Mask, Value */
	{0x0A, 0x70, 0x00}
};

struct IODATA VDD_ON_TBL_VT1636[] = {
/*  Index, Mask, Value */
	{0x10, 0x20, 0x20}
};

struct IODATA VDD_OFF_TBL_VT1636[] = {
/*  Index, Mask, Value */
	{0x10, 0x20, 0x00}
};
