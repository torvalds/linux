/*
 *  linux/arch/arm/kernel/isa.c
 *
 *  Copyright (C) 1999 Phil Blundell
 *
 *  ISA shared memory and I/O port support
 */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* 
 * Nothing about this is actually ARM specific.  One day we could move
 * it into kernel/resource.c or some place like that.  
 */

#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/init.h>

static unsigned int isa_membase, isa_portbase, isa_portshift;

static ctl_table ctl_isa_vars[4] = {
	{BUS_ISA_MEM_BASE, "membase", &isa_membase, 
	 sizeof(isa_membase), 0444, NULL, &proc_dointvec},
	{BUS_ISA_PORT_BASE, "portbase", &isa_portbase, 
	 sizeof(isa_portbase), 0444, NULL, &proc_dointvec},
	{BUS_ISA_PORT_SHIFT, "portshift", &isa_portshift, 
	 sizeof(isa_portshift), 0444, NULL, &proc_dointvec},
	{0}
};

static struct ctl_table_header *isa_sysctl_header;

static ctl_table ctl_isa[2] = {{CTL_BUS_ISA, "isa", NULL, 0, 0555, ctl_isa_vars},
			       {0}};
static ctl_table ctl_bus[2] = {{CTL_BUS, "bus", NULL, 0, 0555, ctl_isa},
			       {0}};

void __init
register_isa_ports(unsigned int membase, unsigned int portbase, unsigned int portshift)
{
	isa_membase = membase;
	isa_portbase = portbase;
	isa_portshift = portshift;
	isa_sysctl_header = register_sysctl_table(ctl_bus, 0);
}
