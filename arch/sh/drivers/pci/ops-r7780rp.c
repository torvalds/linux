/*
 * Author:  Ian DaSilva (idasilva@mvista.com)
 *
 * Highly leveraged from pci-bigsur.c, written by Dustin McIntire.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * PCI initialization for the Renesas SH7780 Highlander R7780RP-1 board
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <mach/highlander.h>
#include <asm/io.h>
#include "pci-sh4.h"

static char irq_tab[] __initdata = {
	65, 66, 67, 68,
};

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
	return irq_tab[slot];
}
