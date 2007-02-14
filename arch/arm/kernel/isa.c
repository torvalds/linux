/*
 *  linux/arch/arm/kernel/isa.c
 *
 *  Copyright (C) 1999 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *  ISA shared memory and I/O port support, and is required to support
 *  iopl, inb, outb and friends in userspace via glibc emulation.
 */
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/init.h>

static unsigned int isa_membase, isa_portbase, isa_portshift;

static ctl_table ctl_isa_vars[4] = {
	{
		.ctl_name	= BUS_ISA_MEM_BASE,
		.procname	= "membase",
		.data		= &isa_membase, 
		.maxlen		= sizeof(isa_membase),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	}, {
		.ctl_name	= BUS_ISA_PORT_BASE,
		.procname	= "portbase",
		.data		= &isa_portbase, 
		.maxlen		= sizeof(isa_portbase),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	}, {
		.ctl_name	= BUS_ISA_PORT_SHIFT,
		.procname	= "portshift",
		.data		= &isa_portshift, 
		.maxlen		= sizeof(isa_portshift),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	}, {0}
};

static struct ctl_table_header *isa_sysctl_header;

static ctl_table ctl_isa[2] = {
	{
		.ctl_name	= CTL_BUS_ISA,
		.procname	= "isa",
		.mode		= 0555,
		.child		= ctl_isa_vars,
	}, {0}
};

static ctl_table ctl_bus[2] = {
	{
		.ctl_name	= CTL_BUS,
		.procname	= "bus",
		.mode		= 0555,
		.child		= ctl_isa,
	}, {0}
};

void __init
register_isa_ports(unsigned int membase, unsigned int portbase, unsigned int portshift)
{
	isa_membase = membase;
	isa_portbase = portbase;
	isa_portshift = portshift;
	isa_sysctl_header = register_sysctl_table(ctl_bus);
}
