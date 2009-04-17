/*
 * Author:  Ian DaSilva (idasilva@mvista.com)
 *
 * Highly leveraged from pci-bigsur.c, written by Dustin McIntire.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * PCI initialization for the Renesas R0P7785LC0011RL board
 * Based on arch/sh/drivers/pci/ops-r7780rp.c
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include "pci-sh4.h"

static char irq_tab[] __initdata = {
	65, 66, 67, 68,
};

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
	return irq_tab[slot];
}

static struct sh4_pci_address_map sh7785_pci_map = {
	.window0	= {
#if defined(CONFIG_32BIT)
		.base	= SH7780_32BIT_DDR_BASE_ADDR,
		.size	= 0x40000000,
#else
		.base	= SH7780_CS0_BASE_ADDR,
		.size	= 0x20000000,
#endif
	},
};

int __init pcibios_init_platform(void)
{
	return sh7780_pcic_init(&sh7785_pci_map);
}
