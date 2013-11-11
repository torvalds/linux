/*
 * Driver for
 *    Samsung S5H1420 and
 *    PnpNetwork PN1010 QPSK Demodulator
 *
 * Copyright (C) 2005 Andrew de Quincey <adq_dvb@lidskialf.net>
 * Copyright (C) 2005 Patrick Boettcher <pb@linuxtv.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675 Mass
 * Ave, Cambridge, MA 02139, USA.
 */
#ifndef S5H1420_PRIV
#define S5H1420_PRIV

#include <asm/types.h>

enum s5h1420_register {
	ID01      = 0x00,
	CON_0     = 0x01,
	CON_1     = 0x02,
	PLL01     = 0x03,
	PLL02     = 0x04,
	QPSK01    = 0x05,
	QPSK02    = 0x06,
	Pre01     = 0x07,
	Post01    = 0x08,
	Loop01    = 0x09,
	Loop02    = 0x0a,
	Loop03    = 0x0b,
	Loop04    = 0x0c,
	Loop05    = 0x0d,
	Pnco01    = 0x0e,
	Pnco02    = 0x0f,
	Pnco03    = 0x10,
	Tnco01    = 0x11,
	Tnco02    = 0x12,
	Tnco03    = 0x13,
	Monitor01 = 0x14,
	Monitor02 = 0x15,
	Monitor03 = 0x16,
	Monitor04 = 0x17,
	Monitor05 = 0x18,
	Monitor06 = 0x19,
	Monitor07 = 0x1a,
	Monitor12 = 0x1f,

	FEC01     = 0x22,
	Soft01    = 0x23,
	Soft02    = 0x24,
	Soft03    = 0x25,
	Soft04    = 0x26,
	Soft05    = 0x27,
	Soft06    = 0x28,
	Vit01     = 0x29,
	Vit02     = 0x2a,
	Vit03     = 0x2b,
	Vit04     = 0x2c,
	Vit05     = 0x2d,
	Vit06     = 0x2e,
	Vit07     = 0x2f,
	Vit08     = 0x30,
	Vit09     = 0x31,
	Vit10     = 0x32,
	Vit11     = 0x33,
	Vit12     = 0x34,
	Sync01    = 0x35,
	Sync02    = 0x36,
	Rs01      = 0x37,
	Mpeg01    = 0x38,
	Mpeg02    = 0x39,
	DiS01     = 0x3a,
	DiS02     = 0x3b,
	DiS03     = 0x3c,
	DiS04     = 0x3d,
	DiS05     = 0x3e,
	DiS06     = 0x3f,
	DiS07     = 0x40,
	DiS08     = 0x41,
	DiS09     = 0x42,
	DiS10     = 0x43,
	DiS11     = 0x44,
	Rf01      = 0x45,
	Err01     = 0x46,
	Err02     = 0x47,
	Err03     = 0x48,
	Err04     = 0x49,
};


#endif
