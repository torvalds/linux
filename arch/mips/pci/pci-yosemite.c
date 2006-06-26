/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <asm/titan_dep.h>

extern struct pci_ops titan_pci_ops;

static struct resource py_mem_resource = {
	.start	= 0xe0000000UL,
	.end	= 0xe3ffffffUL,
	.name	= "Titan PCI MEM",
	.flags	= IORESOURCE_MEM
};

/*
 * PMON really reserves 16MB of I/O port space but that's stupid, nothing
 * needs that much since allocations are limited to 256 bytes per device
 * anyway.  So we just claim 64kB here.
 */
#define TITAN_IO_SIZE	0x0000ffffUL
#define TITAN_IO_BASE	0xe8000000UL

static struct resource py_io_resource = {
	.start	= 0x00001000UL,
	.end	= TITAN_IO_SIZE - 1,
	.name	= "Titan IO MEM",
	.flags	= IORESOURCE_IO,
};

static struct pci_controller py_controller = {
	.pci_ops	= &titan_pci_ops,
	.mem_resource	= &py_mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_resource	= &py_io_resource,
	.io_offset	= 0x00000000UL
};

static char ioremap_failed[] __initdata = "Could not ioremap I/O port range";

static int __init pmc_yosemite_setup(void)
{
	unsigned long io_v_base;

	io_v_base = (unsigned long) ioremap(TITAN_IO_BASE, TITAN_IO_SIZE);
	if (!io_v_base)
		panic(ioremap_failed);

	set_io_port_base(io_v_base);
	TITAN_WRITE(RM9000x2_OCD_LKM7, TITAN_READ(RM9000x2_OCD_LKM7) | 1);

	ioport_resource.end = TITAN_IO_SIZE - 1;

	register_pci_controller(&py_controller);

	return 0;
}

arch_initcall(pmc_yosemite_setup);
