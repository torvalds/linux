/*
 *	Low-Level PCI Support for the SH7780
 *
 *  Dustin McIntire (dustin@sensoria.com)
 *	Derived from arch/i386/kernel/pci-*.c which bore the message:
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 *
 *  Ported to the new API by Paul Mundt <lethal@linux-sh.org>
 *  With cleanup by Paul van Gool <pvangool@mimotech.com>
 *
 *  May be copied or modified under the terms of the GNU General Public
 *  License.  See linux/COPYING for more information.
 *
 */
#undef DEBUG

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "pci-sh4.h"

static int __init sh7780_pci_init(struct pci_channel *chan)
{
	unsigned int id;
	const char *type = NULL;
	int ret;

	printk(KERN_NOTICE "PCI: Starting intialization.\n");

	chan->reg_base = 0xfe040000;
	chan->io_base = 0xfe200000;

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
	 * Platform specific initialization (BSC registers, and memory space
	 * mapping) will be called via the platform defined function
	 * pcibios_init_platform().
	 */
	return pcibios_init_platform();
}

extern u8 pci_cache_line_size;

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

struct pci_channel board_pci_channels[] = {
	{ sh7780_pci_init, &sh4_pci_ops, &sh7785_io_resource, &sh7785_mem_resource, 0, 0xff },
	{ NULL, NULL, NULL, 0, 0 },
};

int __init sh7780_pcic_init(struct sh4_pci_address_map *map)
{
	struct pci_channel *chan = &board_pci_channels[0];
	u32 word;

	/*
	 * Set the class and sub-class codes.
	 */
	__raw_writeb(PCI_CLASS_BRIDGE_HOST >> 8,
		     chan->reg_base + SH7780_PCIBCC);
	__raw_writeb(PCI_CLASS_BRIDGE_HOST & 0xff,
		     chan->reg_base + SH7780_PCISUB);

	pci_cache_line_size = pci_read_reg(chan, SH7780_PCICLS) / 4;

	/* set the command/status bits to:
	 * Wait Cycle Control + Parity Enable + Bus Master +
	 * Mem space enable
	 */
	pci_write_reg(chan, 0x00000046, SH7780_PCICMD);

	/* Set IO and Mem windows to local address
	 * Make PCI and local address the same for easy 1 to 1 mapping
	 */
	pci_write_reg(chan, map->window0.size - 0xfffff, SH4_PCILSR0);
	pci_write_reg(chan, map->window1.size - 0xfffff, SH4_PCILSR1);
	/* Set the values on window 0 PCI config registers */
	pci_write_reg(chan, map->window0.base, SH4_PCILAR0);
	pci_write_reg(chan, map->window0.base, SH7780_PCIMBAR0);
	/* Set the values on window 1 PCI config registers */
	pci_write_reg(chan, map->window1.base, SH4_PCILAR1);
	pci_write_reg(chan, map->window1.base, SH7780_PCIMBAR1);

	/* Apply any last-minute PCIC fixups */
	pci_fixup_pcic(chan);

	/* SH7780 init done, set central function init complete */
	/* use round robin mode to stop a device starving/overruning */
	word = SH4_PCICR_PREFIX | SH4_PCICR_CFIN | SH4_PCICR_FTO;
	pci_write_reg(chan, word, SH4_PCICR);

	__set_io_port_base(SH7780_PCI_IO_BASE);

	return 0;
}
