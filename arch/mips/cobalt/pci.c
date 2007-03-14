/*
 * Register PCI controller.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 2004, 05 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2001, 2002, 2003 by Liam Davies (ldavies@agile.tv)
 *
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/gt64120.h>

extern struct pci_ops gt64xxx_pci0_ops;

static struct resource cobalt_mem_resource = {
	.start	= GT_DEF_PCI0_MEM0_BASE,
	.end	= GT_DEF_PCI0_MEM0_BASE + GT_DEF_PCI0_MEM0_SIZE - 1,
	.name	= "PCI memory",
	.flags	= IORESOURCE_MEM,
};

static struct resource cobalt_io_resource = {
	.start	= 0x1000,
	.end	= GT_DEF_PCI0_IO_SIZE - 1,
	.name	= "PCI I/O",
	.flags	= IORESOURCE_IO,
};

static struct pci_controller cobalt_pci_controller = {
	.pci_ops	= &gt64xxx_pci0_ops,
	.mem_resource	= &cobalt_mem_resource,
	.io_resource	= &cobalt_io_resource,
	.io_offset	= 0 - GT_DEF_PCI0_IO_BASE,
};

static int __init cobalt_pci_init(void)
{
	register_pci_controller(&cobalt_pci_controller);

	return 0;
}

arch_initcall(cobalt_pci_init);
