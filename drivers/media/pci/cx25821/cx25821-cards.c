/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *	Based on Steven Toth <stoth@linuxtv.org> cx23885 driver
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "cx25821.h"

/* board config info */

struct cx25821_board cx25821_boards[] = {
	[UNKNOWN_BOARD] = {
		.name = "UNKNOWN/GENERIC",
		/* Ensure safe default for unknown boards */
		.clk_freq = 0,
	},

	[CX25821_BOARD] = {
		.name = "CX25821",
		.portb = CX25821_RAW,
		.portc = CX25821_264,
	},

};

const unsigned int cx25821_bcount = ARRAY_SIZE(cx25821_boards);
