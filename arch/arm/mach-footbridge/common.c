/*
 *  linux/arch/arm/mach-footbridge/common.c
 *
 *  Copyright (C) 1998-2000 Russell King, Dave Gilbert.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/init.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/hardware/dec21285.h>

#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include "common.h"

extern void __init isa_init_irq(unsigned int irq);

unsigned int mem_fclk_21285 = 50000000;

EXPORT_SYMBOL(mem_fclk_21285);

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

static void fb_mask_irq(unsigned int irq)
{
	*CSR_IRQ_DISABLE = fb_irq_mask[_DC21285_INR(irq)];
}

static void fb_unmask_irq(unsigned int irq)
{
	*CSR_IRQ_ENABLE = fb_irq_mask[_DC21285_INR(irq)];
}

static struct irqchip fb_chip = {
	.ack	= fb_mask_irq,
	.mask	= fb_mask_irq,
	.unmask = fb_unmask_irq,
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
		set_irq_handler(irq, do_level_IRQ);
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
 { ARMCSR_BASE,	 DC21285_ARMCSR_BASE,	    ARMCSR_SIZE,  MT_DEVICE },
 { XBUS_BASE,    0x40000000,		    XBUS_SIZE,    MT_DEVICE }
};

/*
 * The mapping when the footbridge is in host mode.  We don't map any of
 * this when we are in add-in mode.
 */
static struct map_desc ebsa285_host_io_desc[] __initdata = {
#if defined(CONFIG_ARCH_FOOTBRIDGE) && defined(CONFIG_FOOTBRIDGE_HOST)
 { PCIMEM_BASE,  DC21285_PCI_MEM,	    PCIMEM_SIZE,  MT_DEVICE },
 { PCICFG0_BASE, DC21285_PCI_TYPE_0_CONFIG, PCICFG0_SIZE, MT_DEVICE },
 { PCICFG1_BASE, DC21285_PCI_TYPE_1_CONFIG, PCICFG1_SIZE, MT_DEVICE },
 { PCIIACK_BASE, DC21285_PCI_IACK,	    PCIIACK_SIZE, MT_DEVICE },
 { PCIO_BASE,    DC21285_PCI_IO,	    PCIO_SIZE,	  MT_DEVICE }
#endif
};

/*
 * The CO-ebsa285 mapping.
 */
static struct map_desc co285_io_desc[] __initdata = {
#ifdef CONFIG_ARCH_CO285
 { PCIO_BASE,	 DC21285_PCI_IO,	    PCIO_SIZE,    MT_DEVICE },
 { PCIMEM_BASE,	 DC21285_PCI_MEM,	    PCIMEM_SIZE,  MT_DEVICE }
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
	if (machine_is_co285())
		iotable_init(co285_io_desc, ARRAY_SIZE(co285_io_desc));
	if (footbridge_cfn_mode())
		iotable_init(ebsa285_host_io_desc, ARRAY_SIZE(ebsa285_host_io_desc));
}

#ifdef CONFIG_FOOTBRIDGE_ADDIN

/*
 * These two functions convert virtual addresses to PCI addresses and PCI
 * addresses to virtual addresses.  Note that it is only legal to use these
 * on memory obtained via get_zeroed_page or kmalloc.
 */
unsigned long __virt_to_bus(unsigned long res)
{
	WARN_ON(res < PAGE_OFFSET || res >= (unsigned long)high_memory);

	return (res - PAGE_OFFSET) + (*CSR_PCISDRAMBASE & 0xfffffff0);
}
EXPORT_SYMBOL(__virt_to_bus);

unsigned long __bus_to_virt(unsigned long res)
{
	res -= (*CSR_PCISDRAMBASE & 0xfffffff0);
	res += PAGE_OFFSET;

	WARN_ON(res < PAGE_OFFSET || res >= (unsigned long)high_memory);

	return res;
}
EXPORT_SYMBOL(__bus_to_virt);

#endif
