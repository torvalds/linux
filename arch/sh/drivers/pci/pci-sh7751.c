// SPDX-License-Identifier: GPL-2.0
/*
 * Low-Level PCI Support for the SH7751
 *
 *  Copyright (C) 2003 - 2009  Paul Mundt
 *  Copyright (C) 2001  Dustin McIntire
 *
 *  With cleanup by Paul van Gool <pvangool@mimotech.com>, 2003.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/io.h>
#include "pci-sh4.h"
#include <asm/addrspace.h>
#include <asm/sizes.h>

static int __init __area_sdram_check(struct pci_channel *chan,
				     unsigned int area)
{
	unsigned long word;

	word = __raw_readl(SH7751_BCR1);
	/* check BCR for SDRAM in area */
	if (((word >> area) & 1) == 0) {
		printk("PCI: Area %d is not configured for SDRAM. BCR1=0x%lx\n",
		       area, word);
		return 0;
	}
	pci_write_reg(chan, word, SH4_PCIBCR1);

	word = __raw_readw(SH7751_BCR2);
	/* check BCR2 for 32bit SDRAM interface*/
	if (((word >> (area << 1)) & 0x3) != 0x3) {
		printk("PCI: Area %d is not 32 bit SDRAM. BCR2=0x%lx\n",
		       area, word);
		return 0;
	}
	pci_write_reg(chan, word, SH4_PCIBCR2);

	return 1;
}

static struct resource sh7751_pci_resources[] = {
	{
		.name	= "SH7751_IO",
		.start	= 0x1000,
		.end	= SZ_4M - 1,
		.flags	= IORESOURCE_IO
	}, {
		.name	= "SH7751_mem",
		.start	= SH7751_PCI_MEMORY_BASE,
		.end	= SH7751_PCI_MEMORY_BASE + SH7751_PCI_MEM_SIZE - 1,
		.flags	= IORESOURCE_MEM
	},
};

static struct pci_channel sh7751_pci_controller = {
	.pci_ops	= &sh4_pci_ops,
	.resources	= sh7751_pci_resources,
	.nr_resources	= ARRAY_SIZE(sh7751_pci_resources),
	.mem_offset	= 0x00000000,
	.io_offset	= 0x00000000,
	.io_map_base	= SH7751_PCI_IO_BASE,
};

static struct sh4_pci_address_map sh7751_pci_map = {
	.window0	= {
		.base	= SH7751_CS3_BASE_ADDR,
		.size	= 0x04000000,
	},
};

static int __init sh7751_pci_init(void)
{
	struct pci_channel *chan = &sh7751_pci_controller;
	unsigned int id;
	u32 word, reg;

	printk(KERN_NOTICE "PCI: Starting initialization.\n");

	chan->reg_base = 0xfe200000;

	/* check for SH7751/SH7751R hardware */
	id = pci_read_reg(chan, SH7751_PCICONF0);
	if (id != ((SH7751_DEVICE_ID << 16) | SH7751_VENDOR_ID) &&
	    id != ((SH7751R_DEVICE_ID << 16) | SH7751_VENDOR_ID)) {
		pr_debug("PCI: This is not an SH7751(R) (%x)\n", id);
		return -ENODEV;
	}

	/* Set the BCR's to enable PCI access */
	reg = __raw_readl(SH7751_BCR1);
	reg |= 0x80000;
	__raw_writel(reg, SH7751_BCR1);

	/* Turn the clocks back on (not done in reset)*/
	pci_write_reg(chan, 0, SH4_PCICLKR);
	/* Clear Powerdown IRQ's (not done in reset) */
	word = SH4_PCIPINT_D3 | SH4_PCIPINT_D0;
	pci_write_reg(chan, word, SH4_PCIPINT);

	/* set the command/status bits to:
	 * Wait Cycle Control + Parity Enable + Bus Master +
	 * Mem space enable
	 */
	word = SH7751_PCICONF1_WCC | SH7751_PCICONF1_PER |
	       SH7751_PCICONF1_BUM | SH7751_PCICONF1_MES;
	pci_write_reg(chan, word, SH7751_PCICONF1);

	/* define this host as the host bridge */
	word = PCI_BASE_CLASS_BRIDGE << 24;
	pci_write_reg(chan, word, SH7751_PCICONF2);

	/* Set IO and Mem windows to local address
	 * Make PCI and local address the same for easy 1 to 1 mapping
	 */
	word = sh7751_pci_map.window0.size - 1;
	pci_write_reg(chan, word, SH4_PCILSR0);
	/* Set the values on window 0 PCI config registers */
	word = P2SEGADDR(sh7751_pci_map.window0.base);
	pci_write_reg(chan, word, SH4_PCILAR0);
	pci_write_reg(chan, word, SH7751_PCICONF5);

	/* Set the local 16MB PCI memory space window to
	 * the lowest PCI mapped address
	 */
	word = chan->resources[1].start & SH4_PCIMBR_MASK;
	pr_debug("PCI: Setting upper bits of Memory window to 0x%x\n", word);
	pci_write_reg(chan, word , SH4_PCIMBR);

	/* Make sure the MSB's of IO window are set to access PCI space
	 * correctly */
	word = chan->resources[0].start & SH4_PCIIOBR_MASK;
	pr_debug("PCI: Setting upper bits of IO window to 0x%x\n", word);
	pci_write_reg(chan, word, SH4_PCIIOBR);

	/* Set PCI WCRx, BCRx's, copy from BSC locations */

	/* check BCR for SDRAM in specified area */
	switch (sh7751_pci_map.window0.base) {
	case SH7751_CS0_BASE_ADDR: word = __area_sdram_check(chan, 0); break;
	case SH7751_CS1_BASE_ADDR: word = __area_sdram_check(chan, 1); break;
	case SH7751_CS2_BASE_ADDR: word = __area_sdram_check(chan, 2); break;
	case SH7751_CS3_BASE_ADDR: word = __area_sdram_check(chan, 3); break;
	case SH7751_CS4_BASE_ADDR: word = __area_sdram_check(chan, 4); break;
	case SH7751_CS5_BASE_ADDR: word = __area_sdram_check(chan, 5); break;
	case SH7751_CS6_BASE_ADDR: word = __area_sdram_check(chan, 6); break;
	}

	if (!word)
		return -1;

	/* configure the wait control registers */
	word = __raw_readl(SH7751_WCR1);
	pci_write_reg(chan, word, SH4_PCIWCR1);
	word = __raw_readl(SH7751_WCR2);
	pci_write_reg(chan, word, SH4_PCIWCR2);
	word = __raw_readl(SH7751_WCR3);
	pci_write_reg(chan, word, SH4_PCIWCR3);
	word = __raw_readl(SH7751_MCR);
	pci_write_reg(chan, word, SH4_PCIMCR);

	/* NOTE: I'm ignoring the PCI error IRQs for now..
	 * TODO: add support for the internal error interrupts and
	 * DMA interrupts...
	 */

	pci_fixup_pcic(chan);

	/* SH7751 init done, set central function init complete */
	/* use round robin mode to stop a device starving/overruning */
	word = SH4_PCICR_PREFIX | SH4_PCICR_CFIN | SH4_PCICR_ARBM;
	pci_write_reg(chan, word, SH4_PCICR);

	return register_pci_controller(chan);
}
arch_initcall(sh7751_pci_init);
