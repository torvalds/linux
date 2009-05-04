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

#define INTC_BASE	0xffd00000
#define INTC_ICR0	(INTC_BASE+0x0)
#define INTC_ICR1	(INTC_BASE+0x1c)
#define INTC_INTPRI	(INTC_BASE+0x10)
#define INTC_INTREQ	(INTC_BASE+0x24)
#define INTC_INTMSK0	(INTC_BASE+0x44)
#define INTC_INTMSK1	(INTC_BASE+0x48)
#define INTC_INTMSK2	(INTC_BASE+0x40080)
#define INTC_INTMSKCLR0	(INTC_BASE+0x64)
#define INTC_INTMSKCLR1	(INTC_BASE+0x68)
#define INTC_INTMSKCLR2	(INTC_BASE+0x40084)
#define INTC_INT2MSKR	(INTC_BASE+0x40038)
#define INTC_INT2MSKCR	(INTC_BASE+0x4003c)

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space.
 *
 * Note that the platform specific initialization (BSC registers, and memory
 * space mapping) will be called via the platform defined function
 * pcibios_init_platform().
 */
static int __init sh7780_pci_init(void)
{
	unsigned int id;
	int ret, match = 0;

	pr_debug("PCI: Starting intialization.\n");

	ctrl_outl(0x00000001, SH7780_PCI_VCR2); /* Enable PCIC */

	/* check for SH7780/SH7780R hardware */
	id = pci_read_reg(SH7780_PCIVID);
	if ((id & 0xffff) == SH7780_VENDOR_ID) {
		switch ((id >> 16) & 0xffff) {
		case SH7763_DEVICE_ID:
		case SH7780_DEVICE_ID:
		case SH7781_DEVICE_ID:
		case SH7785_DEVICE_ID:
			match = 1;
			break;
		}
	}

	if (unlikely(!match)) {
		printk(KERN_ERR "PCI: This is not an SH7780 (%x)\n", id);
		return -ENODEV;
	}

	/* Setup the INTC */
	if (mach_is_7780se()) {
		/* ICR0: IRL=use separately */
		ctrl_outl(0x00C00020, INTC_ICR0);
		/* ICR1: detect low level(for 2ndcut) */
		ctrl_outl(0xAAAA0000, INTC_ICR1);
		/* INTPRI: priority=3(all) */
		ctrl_outl(0x33333333, INTC_INTPRI);
	}

	if ((ret = sh4_pci_check_direct()) != 0)
		return ret;

	return pcibios_init_platform();
}
core_initcall(sh7780_pci_init);

int __init sh7780_pcic_init(struct sh4_pci_address_map *map)
{
	u32 word;

	/*
	 * This code is unused for some boards as it is done in the
	 * bootloader and doing it here means the MAC addresses loaded
	 * by the bootloader get lost.
	 */
	if (!(map->flags & SH4_PCIC_NO_RESET)) {
		/* toggle PCI reset pin */
		word = SH4_PCICR_PREFIX | SH4_PCICR_PRST;
		pci_write_reg(word, SH4_PCICR);
		/* Wait for a long time... not 1 sec. but long enough */
		mdelay(100);
		word = SH4_PCICR_PREFIX;
		pci_write_reg(word, SH4_PCICR);
	}

	/* set the command/status bits to:
	 * Wait Cycle Control + Parity Enable + Bus Master +
	 * Mem space enable
	 */
	pci_write_reg(0x00000046, SH7780_PCICMD);

	/* define this host as the host bridge */
	word = PCI_BASE_CLASS_BRIDGE << 24;
	pci_write_reg(word, SH7780_PCIRID);

	/* Set IO and Mem windows to local address
	 * Make PCI and local address the same for easy 1 to 1 mapping
	 */
	pci_write_reg(map->window0.size - 0xfffff, SH4_PCILSR0);
	pci_write_reg(map->window1.size - 0xfffff, SH4_PCILSR1);
	/* Set the values on window 0 PCI config registers */
	pci_write_reg(map->window0.base, SH4_PCILAR0);
	pci_write_reg(map->window0.base, SH7780_PCIMBAR0);
	/* Set the values on window 1 PCI config registers */
	pci_write_reg(map->window1.base, SH4_PCILAR1);
	pci_write_reg(map->window1.base, SH7780_PCIMBAR1);

	/* Map IO space into PCI IO window
	 * The IO window is 64K-PCIBIOS_MIN_IO in size
	 * IO addresses will be translated to the
	 * PCI IO window base address
	 */
	pr_debug("PCI: Mapping IO address 0x%x - 0x%x to base 0x%x\n",
		 PCIBIOS_MIN_IO, (64 << 10),
		 SH7780_PCI_IO_BASE + PCIBIOS_MIN_IO);

	/* NOTE: I'm ignoring the PCI error IRQs for now..
	 * TODO: add support for the internal error interrupts and
	 * DMA interrupts...
	 */

	/* Apply any last-minute PCIC fixups */
	pci_fixup_pcic();

	/* SH7780 init done, set central function init complete */
	/* use round robin mode to stop a device starving/overruning */
	word = SH4_PCICR_PREFIX | SH4_PCICR_CFIN | SH4_PCICR_FTO;
	pci_write_reg(word, SH4_PCICR);

	return 1;
}
