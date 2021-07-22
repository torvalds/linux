// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *	Based on Steven Toth <stoth@linuxtv.org> cx23885 driver
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
