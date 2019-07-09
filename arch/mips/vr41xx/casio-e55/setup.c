// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  setup.c, Setup for the CASIO CASSIOPEIA E-11/15/55/65.
 *
 *  Copyright (C) 2002-2006  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/io.h>

#define E55_ISA_IO_BASE		0x1400c000
#define E55_ISA_IO_SIZE		0x03ff4000
#define E55_ISA_IO_START	0
#define E55_ISA_IO_END		(E55_ISA_IO_SIZE - 1)
#define E55_IO_PORT_BASE	KSEG1ADDR(E55_ISA_IO_BASE)

static int __init casio_e55_setup(void)
{
	set_io_port_base(E55_IO_PORT_BASE);
	ioport_resource.start = E55_ISA_IO_START;
	ioport_resource.end = E55_ISA_IO_END;

	return 0;
}

arch_initcall(casio_e55_setup);
