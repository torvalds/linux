/*
 *	Low-Level PCI Support for the SH7751
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

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "pci-sh4.h"
#include <asm/addrspace.h>
#include <asm/io.h>

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space.
 *
 * Note that the platform specific initialization (BSC registers, and memory
 * space mapping) will be called via the platform defined function
 * pcibios_init_platform().
 */
int __init sh7751_pci_init(struct pci_channel *chan)
{
	unsigned int id;
	int ret;

	pr_debug("PCI: Starting intialization.\n");

	chan->reg_base = 0xfe200000;

	/* check for SH7751/SH7751R hardware */
	id = pci_read_reg(chan, SH7751_PCICONF0);
	if (id != ((SH7751_DEVICE_ID << 16) | SH7751_VENDOR_ID) &&
	    id != ((SH7751R_DEVICE_ID << 16) | SH7751_VENDOR_ID)) {
		pr_debug("PCI: This is not an SH7751(R) (%x)\n", id);
		return -ENODEV;
	}

	if ((ret = sh4_pci_check_direct(chan)) != 0)
		return ret;

	return pcibios_init_platform();
}

static int __init __area_sdram_check(struct pci_channel *chan,
				     unsigned int area)
{
	u32 word;

	word = ctrl_inl(SH7751_BCR1);
	/* check BCR for SDRAM in area */
	if (((word >> area) & 1) == 0) {
		printk("PCI: Area %d is not configured for SDRAM. BCR1=0x%x\n",
		       area, word);
		return 0;
	}
	pci_write_reg(chan, word, SH4_PCIBCR1);

	word = (u16)ctrl_inw(SH7751_BCR2);
	/* check BCR2 for 32bit SDRAM interface*/
	if (((word >> (area << 1)) & 0x3) != 0x3) {
		printk("PCI: Area %d is not 32 bit SDRAM. BCR2=0x%x\n",
		       area, word);
		return 0;
	}
	pci_write_reg(chan, word, SH4_PCIBCR2);

	return 1;
}

int __init sh7751_pcic_init(struct pci_channel *chan,
			    struct sh4_pci_address_map *map)
{
	u32 reg;
	u32 word;

	/* Set the BCR's to enable PCI access */
	reg = ctrl_inl(SH7751_BCR1);
	reg |= 0x80000;
	ctrl_outl(reg, SH7751_BCR1);

	/* Turn the clocks back on (not done in reset)*/
	pci_write_reg(chan, 0, SH4_PCICLKR);
	/* Clear Powerdown IRQ's (not done in reset) */
	word = SH4_PCIPINT_D3 | SH4_PCIPINT_D0;
	pci_write_reg(chan, word, SH4_PCIPINT);

	/*
	 * This code is unused for some boards as it is done in the
	 * bootloader and doing it here means the MAC addresses loaded
	 * by the bootloader get lost.
	 */
	if (!(map->flags & SH4_PCIC_NO_RESET)) {
		/* toggle PCI reset pin */
		word = SH4_PCICR_PREFIX | SH4_PCICR_PRST;
		pci_write_reg(chan, word, SH4_PCICR);
		/* Wait for a long time... not 1 sec. but long enough */
		mdelay(100);
		word = SH4_PCICR_PREFIX;
		pci_write_reg(chan, word, SH4_PCICR);
	}

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
	 * Window0 = map->window0.size @ non-cached area base = SDRAM
	 * Window1 = map->window1.size @ cached area base = SDRAM
	 */
	word = map->window0.size - 1;
	pci_write_reg(chan, word, SH4_PCILSR0);
	word = map->window1.size - 1;
	pci_write_reg(chan, word, SH4_PCILSR1);
	/* Set the values on window 0 PCI config registers */
	word = P2SEGADDR(map->window0.base);
	pci_write_reg(chan, word, SH4_PCILAR0);
	pci_write_reg(chan, word, SH7751_PCICONF5);
	/* Set the values on window 1 PCI config registers */
	word =  PHYSADDR(map->window1.base);
	pci_write_reg(chan, word, SH4_PCILAR1);
	pci_write_reg(chan, word, SH7751_PCICONF6);

	/* Set the local 16MB PCI memory space window to
	 * the lowest PCI mapped address
	 */
	word = chan->mem_resource->start & SH4_PCIMBR_MASK;
	pr_debug("PCI: Setting upper bits of Memory window to 0x%x\n", word);
	pci_write_reg(chan, word , SH4_PCIMBR);

	/* Map IO space into PCI IO window:
	 * IO addresses will be translated to the PCI IO window base address
	 */
	pr_debug("PCI: Mapping IO address 0x%x - 0x%x to base 0x%x\n",
		 chan->io_resource->start, chan->io_resource->end,
		 SH7751_PCI_IO_BASE + chan->io_resource->start);

	/* Make sure the MSB's of IO window are set to access PCI space
	 * correctly */
	word = chan->io_resource->start & SH4_PCIIOBR_MASK;
	pr_debug("PCI: Setting upper bits of IO window to 0x%x\n", word);
	pci_write_reg(chan, word, SH4_PCIIOBR);

	/* Set PCI WCRx, BCRx's, copy from BSC locations */

	/* check BCR for SDRAM in specified area */
	switch (map->window0.base) {
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
	word = ctrl_inl(SH7751_WCR1);
	pci_write_reg(chan, word, SH4_PCIWCR1);
	word = ctrl_inl(SH7751_WCR2);
	pci_write_reg(chan, word, SH4_PCIWCR2);
	word = ctrl_inl(SH7751_WCR3);
	pci_write_reg(chan, word, SH4_PCIWCR3);
	word = ctrl_inl(SH7751_MCR);
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

	return 0;
}
