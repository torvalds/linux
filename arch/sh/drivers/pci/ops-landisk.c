/*
 * arch/sh/drivers/pci/ops-landisk.c
 *
 * PCI initialization for the I-O DATA Device, Inc. LANDISK board
 *
 * Copyright (C) 2006 kogiidena
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include "pci-sh4.h"

static struct resource sh7751_io_resource = {
	.name = "SH7751 IO",
	.start = SH7751_PCI_IO_BASE,
	.end = SH7751_PCI_IO_BASE + SH7751_PCI_IO_SIZE - 1,
	.flags = IORESOURCE_IO
};

static struct resource sh7751_mem_resource = {
	.name = "SH7751 mem",
	.start = SH7751_PCI_MEMORY_BASE,
	.end = SH7751_PCI_MEMORY_BASE + SH7751_PCI_MEM_SIZE - 1,
	.flags = IORESOURCE_MEM
};

struct pci_channel board_pci_channels[] = {
	{ sh7751_pci_init, &sh4_pci_ops, &sh7751_io_resource, &sh7751_mem_resource, 0, 0x3ff},
	{NULL, NULL, NULL, 0, 0},
};

static struct sh4_pci_address_map sh7751_pci_map = {
	.window0 = {
		.base	= SH7751_CS3_BASE_ADDR,
		.size	= (64 << 20),	/* 64MB */
	},

	.flags = SH4_PCIC_NO_RESET,
};

int __init pcibios_init_platform(void)
{
	return sh7751_pcic_init(&board_pci_channels[0], &sh7751_pci_map);
}

int pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
	/*
	 * slot0: pin1-4 = irq5,6,7,8
	 * slot1: pin1-4 = irq6,7,8,5
	 * slot2: pin1-4 = irq7,8,5,6
	 * slot3: pin1-4 = irq8,5,6,7
	 */
	int irq = ((slot + pin - 1) & 0x3) + 5;

	if ((slot | (pin - 1)) > 0x3) {
		printk("PCI: Bad IRQ mapping request for slot %d pin %c\n",
		       slot, pin - 1 + 'A');
		return -1;
	}
	return irq;
}
