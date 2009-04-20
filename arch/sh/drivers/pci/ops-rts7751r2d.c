/*
 * linux/arch/sh/drivers/pci/ops-rts7751r2d.c
 *
 * Author:  Ian DaSilva (idasilva@mvista.com)
 *
 * Highly leveraged from pci-bigsur.c, written by Dustin McIntire.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * PCI initialization for the Renesas SH7751R RTS7751R2D board
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <mach/r2d.h>
#include "pci-sh4.h"

static u8 rts7751r2d_irq_tab[] __initdata = {
	IRQ_PCI_INTA,
	IRQ_PCI_INTB,
	IRQ_PCI_INTC,
	IRQ_PCI_INTD,
};

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
	return rts7751r2d_irq_tab[slot];
}
