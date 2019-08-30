// SPDX-License-Identifier: GPL-2.0
/*
 * Low-Level PCI Support for the SH7780
 *
 *  Copyright (C) 2005 - 2010  Paul Mundt
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include "pci-sh4.h"
#include <asm/mmu.h>
#include <linux/sizes.h>

#if defined(CONFIG_CPU_BIG_ENDIAN)
# define PCICR_ENDIANNESS SH4_PCICR_BSWP
#else
# define PCICR_ENDIANNESS 0
#endif


static struct resource sh7785_pci_resources[] = {
	{
		.name	= "PCI IO",
		.start	= 0x1000,
		.end	= SZ_4M - 1,
		.flags	= IORESOURCE_IO,
	}, {
		.name	= "PCI MEM 0",
		.start	= 0xfd000000,
		.end	= 0xfd000000 + SZ_16M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "PCI MEM 1",
		.start	= 0x10000000,
		.end	= 0x10000000 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		/*
		 * 32-bit only resources must be last.
		 */
		.name	= "PCI MEM 2",
		.start	= 0xc0000000,
		.end	= 0xc0000000 + SZ_512M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	},
};

static struct pci_channel sh7780_pci_controller = {
	.pci_ops	= &sh4_pci_ops,
	.resources	= sh7785_pci_resources,
	.nr_resources	= ARRAY_SIZE(sh7785_pci_resources),
	.io_offset	= 0,
	.mem_offset	= 0,
	.io_map_base	= 0xfe200000,
	.serr_irq	= evt2irq(0xa00),
	.err_irq	= evt2irq(0xaa0),
};

struct pci_errors {
	unsigned int	mask;
	const char	*str;
} pci_arbiter_errors[] = {
	{ SH4_PCIAINT_MBKN,	"master broken" },
	{ SH4_PCIAINT_TBTO,	"target bus time out" },
	{ SH4_PCIAINT_MBTO,	"master bus time out" },
	{ SH4_PCIAINT_TABT,	"target abort" },
	{ SH4_PCIAINT_MABT,	"master abort" },
	{ SH4_PCIAINT_RDPE,	"read data parity error" },
	{ SH4_PCIAINT_WDPE,	"write data parity error" },
}, pci_interrupt_errors[] = {
	{ SH4_PCIINT_MLCK,	"master lock error" },
	{ SH4_PCIINT_TABT,	"target-target abort" },
	{ SH4_PCIINT_TRET,	"target retry time out" },
	{ SH4_PCIINT_MFDE,	"master function disable error" },
	{ SH4_PCIINT_PRTY,	"address parity error" },
	{ SH4_PCIINT_SERR,	"SERR" },
	{ SH4_PCIINT_TWDP,	"data parity error for target write" },
	{ SH4_PCIINT_TRDP,	"PERR detected for target read" },
	{ SH4_PCIINT_MTABT,	"target abort for master" },
	{ SH4_PCIINT_MMABT,	"master abort for master" },
	{ SH4_PCIINT_MWPD,	"master write data parity error" },
	{ SH4_PCIINT_MRPD,	"master read data parity error" },
};

static irqreturn_t sh7780_pci_err_irq(int irq, void *dev_id)
{
	struct pci_channel *hose = dev_id;
	unsigned long addr;
	unsigned int status;
	unsigned int cmd;
	int i;

	addr = __raw_readl(hose->reg_base + SH4_PCIALR);

	/*
	 * Handle status errors.
	 */
	status = __raw_readw(hose->reg_base + PCI_STATUS);
	if (status & (PCI_STATUS_PARITY |
		      PCI_STATUS_DETECTED_PARITY |
		      PCI_STATUS_SIG_TARGET_ABORT |
		      PCI_STATUS_REC_TARGET_ABORT |
		      PCI_STATUS_REC_MASTER_ABORT)) {
		cmd = pcibios_handle_status_errors(addr, status, hose);
		if (likely(cmd))
			__raw_writew(cmd, hose->reg_base + PCI_STATUS);
	}

	/*
	 * Handle arbiter errors.
	 */
	status = __raw_readl(hose->reg_base + SH4_PCIAINT);
	for (i = cmd = 0; i < ARRAY_SIZE(pci_arbiter_errors); i++) {
		if (status & pci_arbiter_errors[i].mask) {
			printk(KERN_DEBUG "PCI: %s, addr=%08lx\n",
			       pci_arbiter_errors[i].str, addr);
			cmd |= pci_arbiter_errors[i].mask;
		}
	}
	__raw_writel(cmd, hose->reg_base + SH4_PCIAINT);

	/*
	 * Handle the remaining PCI errors.
	 */
	status = __raw_readl(hose->reg_base + SH4_PCIINT);
	for (i = cmd = 0; i < ARRAY_SIZE(pci_interrupt_errors); i++) {
		if (status & pci_interrupt_errors[i].mask) {
			printk(KERN_DEBUG "PCI: %s, addr=%08lx\n",
			       pci_interrupt_errors[i].str, addr);
			cmd |= pci_interrupt_errors[i].mask;
		}
	}
	__raw_writel(cmd, hose->reg_base + SH4_PCIINT);

	return IRQ_HANDLED;
}

static irqreturn_t sh7780_pci_serr_irq(int irq, void *dev_id)
{
	struct pci_channel *hose = dev_id;

	printk(KERN_DEBUG "PCI: system error received: ");
	pcibios_report_status(PCI_STATUS_SIG_SYSTEM_ERROR, 1);
	printk("\n");

	/* Deassert SERR */
	__raw_writel(SH4_PCIINTM_SDIM, hose->reg_base + SH4_PCIINTM);

	/* Back off the IRQ for awhile */
	disable_irq_nosync(irq);
	hose->serr_timer.expires = jiffies + HZ;
	add_timer(&hose->serr_timer);

	return IRQ_HANDLED;
}

static int __init sh7780_pci_setup_irqs(struct pci_channel *hose)
{
	int ret;

	/* Clear out PCI arbiter IRQs */
	__raw_writel(0, hose->reg_base + SH4_PCIAINT);

	/* Clear all error conditions */
	__raw_writew(PCI_STATUS_DETECTED_PARITY  | \
		     PCI_STATUS_SIG_SYSTEM_ERROR | \
		     PCI_STATUS_REC_MASTER_ABORT | \
		     PCI_STATUS_REC_TARGET_ABORT | \
		     PCI_STATUS_SIG_TARGET_ABORT | \
		     PCI_STATUS_PARITY, hose->reg_base + PCI_STATUS);

	ret = request_irq(hose->serr_irq, sh7780_pci_serr_irq, 0,
			  "PCI SERR interrupt", hose);
	if (unlikely(ret)) {
		printk(KERN_ERR "PCI: Failed hooking SERR IRQ\n");
		return ret;
	}

	/*
	 * The PCI ERR IRQ needs to be IRQF_SHARED since all of the power
	 * down IRQ vectors are routed through the ERR IRQ vector. We
	 * only request_irq() once as there is only a single masking
	 * source for multiple events.
	 */
	ret = request_irq(hose->err_irq, sh7780_pci_err_irq, IRQF_SHARED,
			  "PCI ERR interrupt", hose);
	if (unlikely(ret)) {
		free_irq(hose->serr_irq, hose);
		return ret;
	}

	/* Unmask all of the arbiter IRQs. */
	__raw_writel(SH4_PCIAINT_MBKN | SH4_PCIAINT_TBTO | SH4_PCIAINT_MBTO | \
		     SH4_PCIAINT_TABT | SH4_PCIAINT_MABT | SH4_PCIAINT_RDPE | \
		     SH4_PCIAINT_WDPE, hose->reg_base + SH4_PCIAINTM);

	/* Unmask all of the PCI IRQs */
	__raw_writel(SH4_PCIINTM_TTADIM  | SH4_PCIINTM_TMTOIM  | \
		     SH4_PCIINTM_MDEIM   | SH4_PCIINTM_APEDIM  | \
		     SH4_PCIINTM_SDIM    | SH4_PCIINTM_DPEITWM | \
		     SH4_PCIINTM_PEDITRM | SH4_PCIINTM_TADIMM  | \
		     SH4_PCIINTM_MADIMM  | SH4_PCIINTM_MWPDIM  | \
		     SH4_PCIINTM_MRDPEIM, hose->reg_base + SH4_PCIINTM);

	return ret;
}

static inline void __init sh7780_pci_teardown_irqs(struct pci_channel *hose)
{
	free_irq(hose->err_irq, hose);
	free_irq(hose->serr_irq, hose);
}

static void __init sh7780_pci66_init(struct pci_channel *hose)
{
	unsigned int tmp;

	if (!pci_is_66mhz_capable(hose, 0, 0))
		return;

	/* Enable register access */
	tmp = __raw_readl(hose->reg_base + SH4_PCICR);
	tmp |= SH4_PCICR_PREFIX;
	__raw_writel(tmp, hose->reg_base + SH4_PCICR);

	/* Enable 66MHz operation */
	tmp = __raw_readw(hose->reg_base + PCI_STATUS);
	tmp |= PCI_STATUS_66MHZ;
	__raw_writew(tmp, hose->reg_base + PCI_STATUS);

	/* Done */
	tmp = __raw_readl(hose->reg_base + SH4_PCICR);
	tmp |= SH4_PCICR_PREFIX | SH4_PCICR_CFIN;
	__raw_writel(tmp, hose->reg_base + SH4_PCICR);
}

static int __init sh7780_pci_init(void)
{
	struct pci_channel *chan = &sh7780_pci_controller;
	phys_addr_t memphys;
	size_t memsize;
	unsigned int id;
	const char *type;
	int ret, i;

	printk(KERN_NOTICE "PCI: Starting initialization.\n");

	chan->reg_base = 0xfe040000;

	/* Enable CPU access to the PCIC registers. */
	__raw_writel(PCIECR_ENBL, PCIECR);

	/* Reset */
	__raw_writel(SH4_PCICR_PREFIX | SH4_PCICR_PRST | PCICR_ENDIANNESS,
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
	__raw_writel(SH4_PCICR_PREFIX | PCICR_ENDIANNESS,
		     chan->reg_base + SH4_PCICR);

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

	/*
	 * Hook up the ERR and SERR IRQs.
	 */
	ret = sh7780_pci_setup_irqs(chan);
	if (unlikely(ret))
		return ret;

	/*
	 * Disable the cache snoop controller for non-coherent DMA.
	 */
	__raw_writel(0, chan->reg_base + SH7780_PCICSCR0);
	__raw_writel(0, chan->reg_base + SH7780_PCICSAR0);
	__raw_writel(0, chan->reg_base + SH7780_PCICSCR1);
	__raw_writel(0, chan->reg_base + SH7780_PCICSAR1);

	/*
	 * Setup the memory BARs
	 */
	for (i = 1; i < chan->nr_resources; i++) {
		struct resource *res = chan->resources + i;
		resource_size_t size;

		if (unlikely(res->flags & IORESOURCE_IO))
			continue;

		/*
		 * Make sure we're in the right physical addressing mode
		 * for dealing with the resource.
		 */
		if ((res->flags & IORESOURCE_MEM_32BIT) && __in_29bit_mode()) {
			chan->nr_resources--;
			continue;
		}

		size = resource_size(res);

		/*
		 * The MBMR mask is calculated in units of 256kB, which
		 * keeps things pretty simple.
		 */
		__raw_writel(((roundup_pow_of_two(size) / SZ_256K) - 1) << 18,
			     chan->reg_base + SH7780_PCIMBMR(i - 1));
		__raw_writel(res->start, chan->reg_base + SH7780_PCIMBR(i - 1));
	}

	/*
	 * And I/O.
	 */
	__raw_writel(0, chan->reg_base + PCI_BASE_ADDRESS_0);
	__raw_writel(0, chan->reg_base + SH7780_PCIIOBR);
	__raw_writel(0, chan->reg_base + SH7780_PCIIOBMR);

	__raw_writew(PCI_COMMAND_SERR   | PCI_COMMAND_WAIT   | \
		     PCI_COMMAND_PARITY | PCI_COMMAND_MASTER | \
		     PCI_COMMAND_MEMORY, chan->reg_base + PCI_COMMAND);

	/*
	 * Initialization mode complete, release the control register and
	 * enable round robin mode to stop device overruns/starvation.
	 */
	__raw_writel(SH4_PCICR_PREFIX | SH4_PCICR_CFIN | SH4_PCICR_FTO |
		     PCICR_ENDIANNESS,
		     chan->reg_base + SH4_PCICR);

	ret = register_pci_controller(chan);
	if (unlikely(ret))
		goto err;

	sh7780_pci66_init(chan);

	printk(KERN_NOTICE "PCI: Running at %dMHz.\n",
	       (__raw_readw(chan->reg_base + PCI_STATUS) & PCI_STATUS_66MHZ) ?
	       66 : 33);

	return 0;

err:
	sh7780_pci_teardown_irqs(chan);
	return ret;
}
arch_initcall(sh7780_pci_init);
