/*
 * linux/arch/sh/drivers/pci/ops-se7780.c
 *
 * Copyright (C) 2006  Nobuhiro Iwamatsu
 *
 * PCI initialization for the Hitachi UL Solution Engine 7780SE03
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <mach-se/mach/se7780.h>
#include <asm/io.h>
#include "pci-sh4.h"

/*
 * IDSEL = AD16  PCI slot
 * IDSEL = AD17  PCI slot
 * IDSEL = AD18  Serial ATA Controller (Silicon Image SiL3512A)
 * IDSEL = AD19  USB Host Controller (NEC uPD7210100A)
 */

/* IDSEL [16][17][18][19][20][21][22][23][24][25][26][27][28][29][30][31] */
static char se7780_irq_tab[4][16] __initdata = {
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
       return se7780_irq_tab[pin-1][slot];
}

static struct resource se7780_io_resource = {
	.name	= "SH7780_IO",
	.start	= SH7780_PCI_IO_BASE,
	.end	= SH7780_PCI_IO_BASE + SH7780_PCI_IO_SIZE - 1,
	.flags	= IORESOURCE_IO
};

static struct resource se7780_mem_resource = {
	.name	= "SH7780_mem",
	.start	= SH7780_PCI_MEMORY_BASE,
	.end	= SH7780_PCI_MEMORY_BASE + SH7780_PCI_MEM_SIZE - 1,
	.flags	= IORESOURCE_MEM
};

extern struct pci_ops se7780_pci_ops;

struct pci_channel board_pci_channels[] = {
	{ sh7780_pci_init, &sh4_pci_ops, &se7780_io_resource, &se7780_mem_resource, 0, 0xff },
	{ NULL, NULL, NULL, 0, 0 },
};
EXPORT_SYMBOL(board_pci_channels);

static struct sh4_pci_address_map se7780_pci_map = {
	.window0	= {
		.base	= SH7780_CS2_BASE_ADDR,
		.size	= 0x04000000,
	},
	.flags  = SH4_PCIC_NO_RESET,
};

int __init pcibios_init_platform(void)
{
	printk("SH7780 PCI: Finished initialization of the PCI controller\n");

	/*
	 * FPGA PCISEL register initialize
	 *
	 *  CPU  || SLOT1 | SLOT2 | S-ATA | USB
	 *  -------------------------------------
	 *  INTA || INTA  | INTD  |  --   | INTB
	 *  -------------------------------------
	 *  INTB || INTB  | INTA  |  --   | INTC
	 *  -------------------------------------
	 *  INTC || INTC  | INTB  | INTA  |  --
	 *  -------------------------------------
	 *  INTD || INTD  | INTC  |  --   | INTA
	 *  -------------------------------------
	 */
	ctrl_outw(0x0013, FPGA_PCI_INTSEL1);
	ctrl_outw(0xE402, FPGA_PCI_INTSEL2);

	return sh7780_pcic_init(&board_pci_channels[0], &se7780_pci_map);
}
