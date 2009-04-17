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
#include <mach/sdk7780.h>
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
