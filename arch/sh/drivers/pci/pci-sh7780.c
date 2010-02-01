/*
 * Low-Level PCI Support for the SH7780
 *
 *  Copyright (C) 2005 - 2010  Paul Mundt
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
#include <linux/log2.h>
#include "pci-sh4.h"
#include <asm/mmu.h>
#include <asm/sizes.h>

static struct resource sh7785_io_resource = {
	.name	= "SH7785_IO",
	.start	= 0x1000,
	.end	= SH7780_PCI_IO_SIZE - 1,
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

static int __init sh7780_pci_init(void)
{
	struct pci_channel *chan = &sh7780_pci_controller;
	phys_addr_t memphys;
	size_t memsize;
	unsigned int id;
	const char *type;

	printk(KERN_NOTICE "PCI: Starting intialization.\n");

	chan->reg_base = 0xfe040000;

	/* Enable CPU access to the PCIC registers. */
	__raw_writel(PCIECR_ENBL, PCIECR);

	/* Reset */
	__raw_writel(SH4_PCICR_PREFIX | SH4_PCICR_PRST,
		     chan->reg_base + SH4_PCICR);

	/*
	 * Wait for it to come back up. The spec says to allow for up to
	 * 1 second after toggling the reset pin, but in practice 100ms
	 * is more than enough.
	 */
	mdelay(100);

	id = __raw_readw(chan->reg_base + PCI_VENDOR_ID);
	if (id != PCI_VENDOR_ID_RENESAS) {
		printk(KERN_ERR "PCI: Unknown vendor ID 0x%04x.\n", id);
		return -ENODEV;
	}

	id = __raw_readw(chan->reg_base + PCI_DEVICE_ID);
	type = (id == PCI_DEVICE_ID_RENESAS_SH7763) ? "SH7763" :
	       (id == PCI_DEVICE_ID_RENESAS_SH7780) ? "SH7780" :
	       (id == PCI_DEVICE_ID_RENESAS_SH7781) ? "SH7781" :
	       (id == PCI_DEVICE_ID_RENESAS_SH7785) ? "SH7785" :
					  NULL;
	if (unlikely(!type)) {
		printk(KERN_ERR "PCI: Found an unsupported Renesas host "
		       "controller, device id 0x%04x.\n", id);
		return -EINVAL;
	}

	printk(KERN_NOTICE "PCI: Found a Renesas %s host "
	       "controller, revision %d.\n", type,
	       __raw_readb(chan->reg_base + PCI_REVISION_ID));

	/*
	 * Now throw it in to register initialization mode and
	 * start the real work.
	 */
	__raw_writel(SH4_PCICR_PREFIX, chan->reg_base + SH4_PCICR);

	__raw_writel(0, chan->reg_base + PCI_BASE_ADDRESS_0);

	memphys = __pa(memory_start);
	memsize = roundup_pow_of_two(memory_end - memory_start);

	/*
	 * If there's more than 512MB of memory, we need to roll over to
	 * LAR1/LSR1.
	 */
	if (memsize > SZ_512M) {
		__raw_writel(memphys + SZ_512M, chan->reg_base + SH4_PCILAR1);
		__raw_writel((((memsize - SZ_512M) - SZ_1M) & 0x1ff00000) | 1,
			     chan->reg_base + SH4_PCILSR1);
		memsize = SZ_512M;
	} else {
		/*
		 * Otherwise just zero it out and disable it.
		 */
		__raw_writel(0, chan->reg_base + SH4_PCILAR1);
		__raw_writel(0, chan->reg_base + SH4_PCILSR1);
	}

	/*
	 * LAR0/LSR0 covers up to the first 512MB, which is enough to
	 * cover all of lowmem on most platforms.
	 */
	__raw_writel(memphys, chan->reg_base + SH4_PCILAR0);
	__raw_writel(((memsize - SZ_1M) & 0x1ff00000) | 1,
		     chan->reg_base + SH4_PCILSR0);

	/* Clear out PCI arbiter IRQs */
	__raw_writel(0, chan->reg_base + SH4_PCIAINT);

	/* Unmask all of the arbiter IRQs. */
	__raw_writel(SH4_PCIAINT_MBKN | SH4_PCIAINT_TBTO | SH4_PCIAINT_MBTO | \
		     SH4_PCIAINT_TABT | SH4_PCIAINT_MABT | SH4_PCIAINT_RDPE | \
		     SH4_PCIAINT_WDPE, chan->reg_base + SH4_PCIAINTM);

	/* Clear all error conditions */
	__raw_writew(PCI_STATUS_DETECTED_PARITY  | \
		     PCI_STATUS_SIG_SYSTEM_ERROR | \
		     PCI_STATUS_REC_MASTER_ABORT | \
		     PCI_STATUS_REC_TARGET_ABORT | \
		     PCI_STATUS_SIG_TARGET_ABORT | \
		     PCI_STATUS_PARITY, chan->reg_base + PCI_STATUS);

	__raw_writew(PCI_COMMAND_SERR | PCI_COMMAND_WAIT | \
		     PCI_COMMAND_PARITY | PCI_COMMAND_MASTER | \
		     PCI_COMMAND_MEMORY, chan->reg_base + PCI_COMMAND);

	/* Unmask all of the PCI IRQs */
	__raw_writel(SH4_PCIINTM_TTADIM  | SH4_PCIINTM_TMTOIM  | \
		     SH4_PCIINTM_MDEIM   | SH4_PCIINTM_APEDIM  | \
		     SH4_PCIINTM_SDIM    | SH4_PCIINTM_DPEITWM | \
		     SH4_PCIINTM_PEDITRM | SH4_PCIINTM_TADIMM  | \
		     SH4_PCIINTM_MADIMM  | SH4_PCIINTM_MWPDIM  | \
		     SH4_PCIINTM_MRDPEIM, chan->reg_base + SH4_PCIINTM);

	/*
	 * Disable the cache snoop controller for non-coherent DMA.
	 */
	__raw_writel(0, chan->reg_base + SH7780_PCICSCR0);
	__raw_writel(0, chan->reg_base + SH7780_PCICSAR0);
	__raw_writel(0, chan->reg_base + SH7780_PCICSCR1);
	__raw_writel(0, chan->reg_base + SH7780_PCICSAR1);

	__raw_writel(0xfd000000, chan->reg_base + SH7780_PCIMBR0);
	__raw_writel(0x00fc0000, chan->reg_base + SH7780_PCIMBMR0);

	__raw_writel(0, chan->reg_base + SH7780_PCIIOBR);
	__raw_writel(0, chan->reg_base + SH7780_PCIIOBMR);

	/*
	 * Initialization mode complete, release the control register and
	 * enable round robin mode to stop device overruns/starvation.
	 */
	__raw_writel(SH4_PCICR_PREFIX | SH4_PCICR_CFIN | SH4_PCICR_FTO,
		     chan->reg_base + SH4_PCICR);

	register_pci_controller(chan);

	return 0;
}
arch_initcall(sh7780_pci_init);
