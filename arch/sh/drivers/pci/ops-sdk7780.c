/*
 * linux/arch/sh/drivers/pci/ops-sdk7780.c
 *
 * Copyright (C) 2006  Nobuhiro Iwamatsu
 *
 * PCI initialization for the SDK7780SE03
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <asm/sdk7780.h>
#include <asm/io.h>
#include "pci-sh4.h"

/* IDSEL [16][17][18][19][20][21][22][23][24][25][26][27][28][29][30][31] */
static char sdk7780_irq_tab[4][16] __initdata = {
	/* INTA */
	{ 65, 68, 67, 68, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	/* INTB */
	{ 66, 65, -1, 65, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	/* INTC */
	{ 67, 66, -1, 66, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	/* INTD */
	{ 68, 67, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
};

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
       return sdk7780_irq_tab[pin-1][slot];
}

static struct resource sdk7780_io_resource = {
	.name	= "SH7780_IO",
	.start	= SH7780_PCI_IO_BASE,
	.end	= SH7780_PCI_IO_BASE + SH7780_PCI_IO_SIZE - 1,
	.flags	= IORESOURCE_IO
};

static struct resource sdk7780_mem_resource = {
	.name	= "SH7780_mem",
	.start	= SH7780_PCI_MEMORY_BASE,
	.end	= SH7780_PCI_MEMORY_BASE + SH7780_PCI_MEM_SIZE - 1,
	.flags	= IORESOURCE_MEM
};

struct pci_channel board_pci_channels[] = {
	{ &sh4_pci_ops, &sdk7780_io_resource, &sdk7780_mem_resource, 0, 0xff },
	{ NULL, NULL, NULL, 0, 0 },
};
EXPORT_SYMBOL(board_pci_channels);

static struct sh4_pci_address_map sdk7780_pci_map = {
	.window0	= {
		.base	= SH7780_CS2_BASE_ADDR,
		.size	= 0x04000000,
	},
	.window1	= {
		.base	= SH7780_CS3_BASE_ADDR,
		.size	= 0x04000000,
	},
	.flags	= SH4_PCIC_NO_RESET,
};

int __init pcibios_init_platform(void)
{
	printk(KERN_INFO "SH7780 PCI: Finished initializing PCI controller\n");
	return sh7780_pcic_init(&sdk7780_pci_map);
}
