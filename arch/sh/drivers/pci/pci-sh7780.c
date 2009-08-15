/*
 * Low-Level PCI Support for the SH7780
 *
 *  Copyright (C) 2005 - 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "pci-sh4.h"

static struct resource sh7785_io_resource = {
	.name	= "SH7785_IO",
	.start	= SH7780_PCI_IO_BASE,
	.end	= SH7780_PCI_IO_BASE + SH7780_PCI_IO_SIZE - 1,
	.flags	= IORESOURCE_IO
};

static struct resource sh7785_mem_resource = {
	.name	= "SH7785_mem",
	.start	= SH7780_PCI_MEMORY_BASE,
	.end	= SH7780_PCI_MEMORY_BASE + SH7780_PCI_MEM_SIZE - 1,
	.flags	= IORESOURCE_MEM
};

static struct pci_channel sh7780_pci_controller = {
	.pci_ops	= &sh4_pci_ops,
	.mem_resource	= &sh7785_mem_resource,
	.mem_offset	= 0x00000000,
	.io_resource	= &sh7785_io_resource,
	.io_offset	= 0x00000000,
	.io_map_base	= SH7780_PCI_IO_BASE,
};

static struct sh4_pci_address_map sh7780_pci_map = {
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

static int __init sh7780_pci_init(void)
{
	struct pci_channel *chan = &sh7780_pci_controller;
	unsigned int id;
	const char *type = NULL;
	int ret;
	u32 word;

	printk(KERN_NOTICE "PCI: Starting intialization.\n");

	chan->reg_base = 0xfe040000;

	/* Enable CPU access to the PCIC registers. */
	__raw_writel(PCIECR_ENBL, PCIECR);

	id = __raw_readw(chan->reg_base + SH7780_PCIVID);
	if (id != SH7780_VENDOR_ID) {
		printk(KERN_ERR "PCI: Unknown vendor ID 0x%04x.\n", id);
		return -ENODEV;
	}

	id = __raw_readw(chan->reg_base + SH7780_PCIDID);
	type = (id == SH7763_DEVICE_ID)	? "SH7763" :
	       (id == SH7780_DEVICE_ID) ? "SH7780" :
	       (id == SH7781_DEVICE_ID) ? "SH7781" :
	       (id == SH7785_DEVICE_ID) ? "SH7785" :
					  NULL;
	if (unlikely(!type)) {
		printk(KERN_ERR "PCI: Found an unsupported Renesas host "
		       "controller, device id 0x%04x.\n", id);
		return -EINVAL;
	}

	printk(KERN_NOTICE "PCI: Found a Renesas %s host "
	       "controller, revision %d.\n", type,
	       __raw_readb(chan->reg_base + SH7780_PCIRID));

	if ((ret = sh4_pci_check_direct(chan)) != 0)
		return ret;

	/*
	 * Set the class and sub-class codes.
	 */
	__raw_writeb(PCI_CLASS_BRIDGE_HOST >> 8,
		     chan->reg_base + SH7780_PCIBCC);
	__raw_writeb(PCI_CLASS_BRIDGE_HOST & 0xff,
		     chan->reg_base + SH7780_PCISUB);

	/*
	 * Set IO and Mem windows to local address
	 * Make PCI and local address the same for easy 1 to 1 mapping
	 */
	pci_write_reg(chan, sh7780_pci_map.window0.size - 0xfffff, SH4_PCILSR0);
	/* Set the values on window 0 PCI config registers */
	pci_write_reg(chan, sh7780_pci_map.window0.base, SH4_PCILAR0);
	pci_write_reg(chan, sh7780_pci_map.window0.base, SH7780_PCIMBAR0);

	pci_write_reg(chan, 0x0000380f, SH4_PCIAINTM);

	/* Set up standard PCI config registers */
	__raw_writew(0xFB00, chan->reg_base + SH7780_PCISTATUS);
	__raw_writew(0x0047, chan->reg_base + SH7780_PCICMD);
	__raw_writew(0x1912, chan->reg_base + SH7780_PCISVID);
	__raw_writew(0x0001, chan->reg_base + SH7780_PCISID);

	__raw_writeb(0x00, chan->reg_base + SH7780_PCIPIF);

	/* Apply any last-minute PCIC fixups */
	pci_fixup_pcic(chan);

	pci_write_reg(chan, 0xfd000000, SH7780_PCIMBR0);
	pci_write_reg(chan, 0x00fc0000, SH7780_PCIMBMR0);

#ifdef CONFIG_32BIT
	pci_write_reg(chan, 0xc0000000, SH7780_PCIMBR2);
	pci_write_reg(chan, 0x20000000 - SH7780_PCI_IO_SIZE, SH7780_PCIMBMR2);
#endif

	/* Set IOBR for windows containing area specified in pci.h */
	pci_write_reg(chan, chan->io_resource->start & ~(SH7780_PCI_IO_SIZE-1),
		      SH7780_PCIIOBR);
	pci_write_reg(chan, ((SH7780_PCI_IO_SIZE-1) & (7<<18)),
		      SH7780_PCIIOBMR);

	/* SH7780 init done, set central function init complete */
	/* use round robin mode to stop a device starving/overruning */
	word = SH4_PCICR_PREFIX | SH4_PCICR_CFIN | SH4_PCICR_FTO;
	pci_write_reg(chan, word, SH4_PCICR);

	register_pci_controller(chan);

	return 0;
}
arch_initcall(sh7780_pci_init);
