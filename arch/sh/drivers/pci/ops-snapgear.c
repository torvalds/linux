/*
 * arch/sh/drivers/pci/ops-snapgear.c
 *
 * Author:  David McCullough <davidm@snapgear.com>
 * 
 * Ported to new API by Paul Mundt <lethal@linux-sh.org>
 *
 * Highly leveraged from pci-bigsur.c, written by Dustin McIntire.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * PCI initialization for the SnapGear boards
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <asm/io.h>
#include "pci-sh7751.h"

#define SNAPGEAR_PCI_IO		0x4000
#define SNAPGEAR_PCI_MEM	0xfd000000

/* PCI: default LOCAL memory window sizes (seen from PCI bus) */
#define SNAPGEAR_LSR0_SIZE    (64*(1<<20)) //64MB
#define SNAPGEAR_LSR1_SIZE    (64*(1<<20)) //64MB

static struct resource sh7751_io_resource = {
	.name		= "SH7751 IO",
	.start		= SNAPGEAR_PCI_IO,
	.end		= SNAPGEAR_PCI_IO + (64*1024) - 1, /* 64KiB I/O */
	.flags		= IORESOURCE_IO,
};

static struct resource sh7751_mem_resource = {
	.name		= "SH7751 mem",
	.start		= SNAPGEAR_PCI_MEM,
	.end		= SNAPGEAR_PCI_MEM + (64*1024*1024) - 1, /* 64MiB mem */
	.flags		= IORESOURCE_MEM,
};

extern struct pci_ops sh7751_pci_ops;

struct pci_channel board_pci_channels[] = {
	{ &sh7751_pci_ops, &sh7751_io_resource, &sh7751_mem_resource, 0, 0xff },
	{ 0, }
};

static struct sh7751_pci_address_map sh7751_pci_map = {
	.window0	= {
		.base	= SH7751_CS2_BASE_ADDR,
		.size	= SNAPGEAR_LSR0_SIZE,
	},

	.window1	= {
		.base	= SH7751_CS2_BASE_ADDR,
		.size	= SNAPGEAR_LSR1_SIZE,
	},

	.flags	= SH7751_PCIC_NO_RESET,
};

/*
 * Initialize the SnapGear PCI interface 
 * Setup hardware to be Central Funtion
 * Copy the BSR regs to the PCI interface
 * Setup PCI windows into local RAM
 */
int __init pcibios_init_platform(void)
{
	return sh7751_pcic_init(&sh7751_pci_map);
}

int __init pcibios_map_platform_irq(u8 slot, u8 pin)
{
	int irq = -1;

	switch (slot) {
	case 8:  /* the PCI bridge */ break;
	case 11: irq = 8;  break; /* USB    */
	case 12: irq = 11; break; /* PCMCIA */
	case 13: irq = 5;  break; /* eth0   */
	case 14: irq = 8;  break; /* eth1   */
	case 15: irq = 11; break; /* safenet (unused) */
	}

	printk("PCI: Mapping SnapGear IRQ for slot %d, pin %c to irq %d\n",
	       slot, pin - 1 + 'A', irq);

	return irq;
}

void __init pcibios_fixup(void)
{
	/* Nothing to fixup .. */
}

