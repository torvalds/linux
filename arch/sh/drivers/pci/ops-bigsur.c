/*
 * linux/arch/sh/drivers/pci/ops-bigsur.c
 *
 * By Dustin McIntire (dustin@sensoria.com) (c)2001
 *
 * Ported to new API by Paul Mundt <lethal@linux-sh.org>.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * PCI initialization for the Hitachi Big Sur Evaluation Board
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>
#include "pci-sh4.h"
#include <asm/bigsur/bigsur.h>

#define BIGSUR_PCI_IO	0x4000
#define BIGSUR_PCI_MEM	0xfd000000

static struct resource sh7751_io_resource = {
	.name		= "SH7751 IO",
	.start		= BIGSUR_PCI_IO,
	.end		= BIGSUR_PCI_IO + (64*1024) - 1,
	.flags		= IORESOURCE_IO,
};

static struct resource sh7751_mem_resource = {
	.name		= "SH7751 mem",
	.start		= BIGSUR_PCI_MEM,
	.end		= BIGSUR_PCI_MEM + (64*1024*1024) - 1,
	.flags		= IORESOURCE_MEM,
};

extern struct pci_ops sh7751_pci_ops;

struct pci_channel board_pci_channels[] = {
	{ &sh4_pci_ops, &sh7751_io_resource, &sh7751_mem_resource, 0, 0xff },
	{ 0, }
};

static struct sh4_pci_address_map sh7751_pci_map = {
	.window0	= {
		.base	= SH7751_CS3_BASE_ADDR,
		.size	= BIGSUR_LSR0_SIZE,
	},

	.window1	= {
		.base	= SH7751_CS3_BASE_ADDR,
		.size	= BIGSUR_LSR1_SIZE,
	},
};

/*
 * Initialize the Big Sur PCI interface
 * Setup hardware to be Central Funtion
 * Copy the BSR regs to the PCI interface
 * Setup PCI windows into local RAM
 */
int __init pcibios_init_platform(void)
{
	return sh7751_pcic_init(&sh7751_pci_map);
}

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
	/*
	 * The Big Sur can be used in a CPCI chassis, but the SH7751 PCI
	 * interface is on the wrong end of the board so that it can also
	 * support a V320 CPI interface chip...  Therefor the IRQ mapping is
	 * somewhat use dependent... I'l assume a linear map for now, i.e.
	 * INTA=slot0,pin0... INTD=slot3,pin0...
	 */
	int irq = (slot + pin-1) % 4 + BIGSUR_SH7751_PCI_IRQ_BASE;

	PCIDBG(2, "PCI: Mapping Big Sur IRQ for slot %d, pin %c to irq %d\n",
	       slot, pin-1+'A', irq);

	return irq;
}
