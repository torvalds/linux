// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  setup.c, Setup for the IBM WorkPad z50.
 *
 *  Copyright (C) 2002-2006  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/io.h>

#define WORKPAD_ISA_IO_BASE	0x15000000
#define WORKPAD_ISA_IO_SIZE	0x03000000
#define WORKPAD_ISA_IO_START	0
#define WORKPAD_ISA_IO_END	(WORKPAD_ISA_IO_SIZE - 1)
#define WORKPAD_IO_PORT_BASE	KSEG1ADDR(WORKPAD_ISA_IO_BASE)

static int __init ibm_workpad_setup(void)
{
	set_io_port_base(WORKPAD_IO_PORT_BASE);
	ioport_resource.start = WORKPAD_ISA_IO_START;
	ioport_resource.end = WORKPAD_ISA_IO_END;

	return 0;
}

arch_initcall(ibm_workpad_setup);
