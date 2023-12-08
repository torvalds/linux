// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * altera-lpt.c
 *
 * altera FPGA driver
 *
 * Copyright (C) Altera Corporation 1998-2001
 * Copyright (C) 2010 NetUP Inc.
 * Copyright (C) 2010 Abylay Ospan <aospan@netup.ru>
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include "altera-exprt.h"

static int lpt_hardware_initialized;

static void byteblaster_write(int port, int data)
{
	outb((u8)data, (u16)(port + 0x378));
};

static int byteblaster_read(int port)
{
	int data = 0;
	data = inb((u16)(port + 0x378));
	return data & 0xff;
};

int netup_jtag_io_lpt(void *device, int tms, int tdi, int read_tdo)
{
	int data = 0;
	int tdo = 0;
	int initial_lpt_ctrl = 0;

	if (!lpt_hardware_initialized) {
		initial_lpt_ctrl = byteblaster_read(2);
		byteblaster_write(2, (initial_lpt_ctrl | 0x02) & 0xdf);
		lpt_hardware_initialized = 1;
	}

	data = ((tdi ? 0x40 : 0) | (tms ? 0x02 : 0));

	byteblaster_write(0, data);

	if (read_tdo) {
		tdo = byteblaster_read(1);
		tdo = ((tdo & 0x80) ? 0 : 1);
	}

	byteblaster_write(0, data | 0x01);

	byteblaster_write(0, data);

	return tdo;
}
