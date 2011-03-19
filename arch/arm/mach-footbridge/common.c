/*
 *  linux/arch/arm/mach-footbridge/common.c
 *
 *  Copyright (C) 1998-2000 Russell King, Dave Gilbert.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/spinlock.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/hardware/dec21285.h>

#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include "common.h"

unsigned int mem_fclk_21285 = 50000000;

EXPORT_SYMBOL(mem_fclk_21285);

static int __init early_fclk(char *arg)
{
	mem_fclk_21285 = simple_strtoul(arg, NULL, 0);
	return 0;
}

early_param("mem_fclk_21285", early_fclk);

static int __init parse_tag_memclk(const struct tag *tag)
{
	mem_fclk_21285 = tag->u.memclk.fmemclk;
	return 0;
}

__tagtable(ATAG_MEMCLK, parse_tag_memclk);

/*
 * Footbridge IRQ translation table
 *  Converts from our IRQ numbers into FootBridge masks
 */
static const int fb_irq_mask[] = {
	IRQ_MASK_UART_RX,	/*  0 */
	IRQ_MASK_UART_TX,	/*  1 */
	IRQ_MASK_TIMER1,	/*  2 */
	IRQ_MASK_TIMER2,	/*  3 */
	IRQ_MASK_TIMER3,	/*  4 */
	IRQ_MASK_IN0,		/*  5 */
	IRQ_MASK_IN1,		/*  6 */
	IRQ_MASK_IN2,		/*  7 */
	IRQ_MASK_IN3,		/*  8 */
	IRQ_MASK_DOORBELLHOST,	/*  9 */
	IRQ_MASK_DMA1,		/* 10 */
	IRQ_MASK_DMA2,		/* 11 */
	IRQ_MASK_PCI,		/* 12 */
	IRQ_MASK_SDRAMPARITY,	/* 13 */
	IRQ_MASK_I2OINPOST,	/* 14 */
	IRQ_MASK_PCI_ABORT,	/* 15 */
	IRQ_MASK_PCI_SERR,	/* 16 */
	IRQ_MASK_DISCARD_TIMER,	/* 17 */
	IRQ_MASK_PCI_DPERR,	/* 18 */
	IRQ_MASK_PCI_PERR,	/* 19 */
};

static void fb_mask_irq(struct irq_data *d)
{
	*CSR_IRQ_DISABLE = fb_irq_mask[_DC21285_INR(d->irq)];
}

static void fb_unmask_irq(struct irq_data *d)
{
	*CSR_IRQ_ENABLE = fb_irq_mask[_DC21285_INR(d->irq)];
}

static struct irq_chip fb_chip = {
	.irq_ack	= fb_mask_irq,
	.irq_mask	= fb_mask_irq,
	.irq_unmask	= fb_unmask_irq,
};

static void __init __fb_init_irq(void)
{
	unsigned int irq;

	/*
	 * setup DC21285 IRQs
	 */
	*CSR_IRQ_DISABLE = -1;
	*CSR_FIQ_DISABLE = -1;

	for (irq = _DC21285_IRQ(0); irq < _DC21285_IRQ(20); irq++) {
		set_irq_chip(irq, &fb_chip);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
}

void __init footbridge_init_irq(void)
{
	__fb_init_irq();

	if (!footbridge_cfn_mode())
		return;

	if (machine_is_ebsa285())
		/* The following is dependent on which slot
		 * you plug the Southbridge card into.  We
		 * currently assume that you plug it into
		 * the right-hand most slot.
		 */
		isa_init_irq(IRQ_PCI);

	if (machine_is_cats())
		isa_init_irq(IRQ_IN2);

	if (machine_is_netwinder())
		isa_init_irq(IRQ_IN3);
}

/*
 * Common mapping for all systems.  Note that the outbound write flush is
 * commented out since there is a "No Fix" problem with it.  Not mapping
 * it means that we have extra bullet protection on our feet.
 */
static struct map_desc fb_common_io_desc[] __initdata = {
	{
		.virtual	= ARMCSR_BASE,
		.pfn		= __phys_to_pfn(DC21285_ARMCSR_BASE),
		.length		= ARMCSR_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= XBUS_BASE,
		.pfn		= __phys_to_pfn(0x40000000),
		.length		= XBUS_SIZE,
		.type		= MT_DEVICE,
	}
};

/*
 * The mapping when the footbridge is in host mode.  We don't map any of
 * this when we are in add-in mode.
 */
static struct map_desc ebsa285_host_io_desc[] __initdata = {
#if defined(CONFIG_ARCH_FOOTBRIDGE) && defined(CONFIG_FOOTBRIDGE_HOST)
	{
		.virtual	= PCIMEM_BASE,
		.pfn		= __phys_to_pfn(DC21285_PCI_MEM),
		.length		= PCIMEM_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= PCICFG0_BASE,
		.pfn		= __phys_to_pfn(DC21285_PCI_TYPE_0_CONFIG),
		.length		= PCICFG0_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= PCICFG1_BASE,
		.pfn		= __phys_to_pfn(DC21285_PCI_TYPE_1_CONFIG),
		.length		= PCICFG1_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= PCIIACK_BASE,
		.pfn		= __phys_to_pfn(DC21285_PCI_IACK),
		.length		= PCIIACK_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= PCIO_BASE,
		.pfn		= __phys_to_pfn(DC21285_PCI_IO),
		.length		= PCIO_SIZE,
		.type		= MT_DEVICE,
	},
#endif
};

void __init footbridge_map_io(void)
{
	/*
	 * Set up the common mapping first; we need this to
	 * determine whether we're in host mode or not.
	 */
	iotable_init(fb_common_io_desc, ARRAY_SIZE(fb_common_io_desc));

	/*
	 * Now, work out what we've got to map in addition on this
	 * platform.
	 */
	if (footbridge_cfn_mode())
		iotable_init(ebsa285_host_io_desc, ARRAY_SIZE(ebsa285_host_io_desc));
}

#ifdef CONFIG_FOOTBRIDGE_ADDIN

static inline unsigned long fb_bus_sdram_offset(void)
{
	return *CSR_PCISDRAMBASE & 0xfffffff0;
}

/*
 * These two functions convert virtual addresses to PCI addresses and PCI
 * addresses to virtual addresses.  Note that it is only legal to use these
 * on memory obtained via get_zeroed_page or kmalloc.
 */
unsigned long __virt_to_bus(unsigned long res)
{
	WARN_ON(res < PAGE_OFFSET || res >= (unsigned long)high_memory);

	return res + (fb_bus_sdram_offset() - PAGE_OFFSET);
}
EXPORT_SYMBOL(__virt_to_bus);

unsigned long __bus_to_virt(unsigned long res)
{
	res = res - (fb_bus_sdram_offset() - PAGE_OFFSET);

	WARN_ON(res < PAGE_OFFSET || res >= (unsigned long)high_memory);

	return res;
}
EXPORT_SYMBOL(__bus_to_virt);

unsigned long __pfn_to_bus(unsigned long pfn)
{
	return __pfn_to_phys(pfn) + (fb_bus_sdram_offset() - PHYS_OFFSET);
}
EXPORT_SYMBOL(__pfn_to_bus);

unsigned long __bus_to_pfn(unsigned long bus)
{
	return __phys_to_pfn(bus - (fb_bus_sdram_offset() - PHYS_OFFSET));
}
EXPORT_SYMBOL(__bus_to_pfn);

#endif
