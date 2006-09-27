/*
 * arch/sh/drivers/pci/ops-titan.c
 *
 * Ported to new API by Paul Mundt <lethal@linux-sh.org>
 *
 * Modified from ops-snapgear.c written by  David McCullough
 * Highly leveraged from pci-bigsur.c, written by Dustin McIntire.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * PCI initialization for the Titan boards
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/titan.h>
#include "pci-sh7751.h"

int __init pcibios_map_platform_irq(u8 slot, u8 pin)
{
	int irq = -1;

	switch (slot) {
	case 0: irq = TITAN_IRQ_WAN;   break;	/* eth0 (WAN) */
	case 1: irq = TITAN_IRQ_LAN;   break;	/* eth1 (LAN) */
	case 2: irq = TITAN_IRQ_MPCIA; break;	/* mPCI A */
	case 3: irq = TITAN_IRQ_MPCIB; break;	/* mPCI B */
	case 4: irq = TITAN_IRQ_USB;   break;	/* USB */
	default:
		printk(KERN_INFO "PCI: Bad IRQ mapping request for slot %d\n", slot);
		return -1;
	}

	printk("PCI: Mapping TITAN IRQ for slot %d, pin %c to irq %d\n",
		slot, pin - 1 + 'A', irq);

	return irq;
}

static struct resource sh7751_io_resource = {
	.name	= "SH7751_IO",
	.start	= SH7751_PCI_IO_BASE,
	.end	= SH7751_PCI_IO_BASE + SH7751_PCI_IO_SIZE - 1,
	.flags	= IORESOURCE_IO
};

static struct resource sh7751_mem_resource = {
	.name	= "SH7751_mem",
	.start	= SH7751_PCI_MEMORY_BASE,
	.end	= SH7751_PCI_MEMORY_BASE + SH7751_PCI_MEM_SIZE - 1,
	.flags	= IORESOURCE_MEM
};

extern struct pci_ops sh7751_pci_ops;

struct pci_channel board_pci_channels[] = {
	{ &sh7751_pci_ops, &sh7751_io_resource, &sh7751_mem_resource, 0, 0xff },
	{ NULL, NULL, NULL, 0, 0 },
};
EXPORT_SYMBOL(board_pci_channels);

static struct sh7751_pci_address_map sh7751_pci_map = {
	.window0	= {
		.base	= SH7751_CS2_BASE_ADDR,
		.size	= SH7751_MEM_REGION_SIZE*2,	/* cs2 and cs3 */
	},

	.window1	= {
		.base	= SH7751_CS2_BASE_ADDR,
		.size	= SH7751_MEM_REGION_SIZE*2,
	},

	.flags	= SH7751_PCIC_NO_RESET,
};

int __init pcibios_init_platform(void)
{
	return sh7751_pcic_init(&sh7751_pci_map);
}
