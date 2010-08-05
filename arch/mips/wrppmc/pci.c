/*
 * pci.c: GT64120 PCI support.
 *
 * Copyright (C) 2006, Wind River System Inc. Rongkai.Zhan <rongkai.zhan@windriver.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/gt64120.h>

extern struct pci_ops gt64xxx_pci0_ops;

static struct resource pci0_io_resource = {
	.name  = "pci_0 io",
	.start = GT_PCI_IO_BASE,
	.end   = GT_PCI_IO_BASE + GT_PCI_IO_SIZE - 1,
	.flags = IORESOURCE_IO,
};

static struct resource pci0_mem_resource = {
	.name  = "pci_0 memory",
	.start = GT_PCI_MEM_BASE,
	.end   = GT_PCI_MEM_BASE + GT_PCI_MEM_SIZE - 1,
	.flags = IORESOURCE_MEM,
};

static struct pci_controller hose_0 = {
	.pci_ops	= &gt64xxx_pci0_ops,
	.io_resource	= &pci0_io_resource,
	.mem_resource	= &pci0_mem_resource,
};

static int __init gt64120_pci_init(void)
{
	u32 tmp;

	tmp = GT_READ(GT_PCI0_CMD_OFS);		/* Huh??? -- Ralf  */
	tmp = GT_READ(GT_PCI0_BARE_OFS);

	/* reset the whole PCI I/O space range */
	ioport_resource.start = GT_PCI_IO_BASE;
	ioport_resource.end = GT_PCI_IO_BASE + GT_PCI_IO_SIZE - 1;

	register_pci_controller(&hose_0);
	return 0;
}

arch_initcall(gt64120_pci_init);
