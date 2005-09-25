/*
 * linux/arch/arm/mach-h720x/common.c
 *
 * Copyright (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *               2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *               2004 Sascha Hauer    <s.hauer@pengutronix.de>
 *
 * common stuff for Hynix h720x processors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mman.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/arch/irqs.h>

#include <asm/mach/dma.h>

#if 0
#define IRQDBG(args...) printk(args)
#else
#define IRQDBG(args...) do {} while(0)
#endif

void __init arch_dma_init(dma_t *dma)
{
}

/*
 * Return usecs since last timer reload
 * (timercount * (usecs perjiffie)) / (ticks per jiffie)
 */
unsigned long h720x_gettimeoffset(void)
{
	return (CPU_REG (TIMER_VIRT, TM0_COUNT) * tick_usec) / LATCH;
}

/*
 * mask Global irq's
 */
static void mask_global_irq (unsigned int irq )
{
	CPU_REG (IRQC_VIRT, IRQC_IER) &= ~(1 << irq);
}

/*
 * unmask Global irq's
 */
static void unmask_global_irq (unsigned int irq )
{
	CPU_REG (IRQC_VIRT, IRQC_IER) |= (1 << irq);
}


/*
 * ack GPIO irq's
 * Ack only for edge triggered int's valid
 */
static void inline ack_gpio_irq(u32 irq)
{
	u32 reg_base = GPIO_VIRT(IRQ_TO_REGNO(irq));
	u32 bit = IRQ_TO_BIT(irq);
	if ( (CPU_REG (reg_base, GPIO_EDGE) & bit))
		CPU_REG (reg_base, GPIO_CLR) = bit;
}

/*
 * mask GPIO irq's
 */
static void inline mask_gpio_irq(u32 irq)
{
	u32 reg_base = GPIO_VIRT(IRQ_TO_REGNO(irq));
	u32 bit = IRQ_TO_BIT(irq);
	CPU_REG (reg_base, GPIO_MASK) &= ~bit;
}

/*
 * unmask GPIO irq's
 */
static void inline unmask_gpio_irq(u32 irq)
{
	u32 reg_base = GPIO_VIRT(IRQ_TO_REGNO(irq));
	u32 bit = IRQ_TO_BIT(irq);
	CPU_REG (reg_base, GPIO_MASK) |= bit;
}

static void
h720x_gpio_handler(unsigned int mask, unsigned int irq,
                 struct irqdesc *desc, struct pt_regs *regs)
{
	IRQDBG("%s irq: %d\n",__FUNCTION__,irq);
	desc = irq_desc + irq;
	while (mask) {
		if (mask & 1) {
			IRQDBG("handling irq %d\n", irq);
			desc_handle_irq(irq, desc, regs);
		}
		irq++;
		desc++;
		mask >>= 1;
	}
}

static void
h720x_gpioa_demux_handler(unsigned int irq_unused, struct irqdesc *desc,
			struct pt_regs *regs)
{
	unsigned int mask, irq;

	mask = CPU_REG(GPIO_A_VIRT,GPIO_STAT);
	irq = IRQ_CHAINED_GPIOA(0);
	IRQDBG("%s mask: 0x%08x irq: %d\n",__FUNCTION__,mask,irq);
	h720x_gpio_handler(mask, irq, desc, regs);
}

static void
h720x_gpiob_demux_handler(unsigned int irq_unused, struct irqdesc *desc,
			struct pt_regs *regs)
{
	unsigned int mask, irq;
	mask = CPU_REG(GPIO_B_VIRT,GPIO_STAT);
	irq = IRQ_CHAINED_GPIOB(0);
	IRQDBG("%s mask: 0x%08x irq: %d\n",__FUNCTION__,mask,irq);
	h720x_gpio_handler(mask, irq, desc, regs);
}

static void
h720x_gpioc_demux_handler(unsigned int irq_unused, struct irqdesc *desc,
			struct pt_regs *regs)
{
	unsigned int mask, irq;

	mask = CPU_REG(GPIO_C_VIRT,GPIO_STAT);
	irq = IRQ_CHAINED_GPIOC(0);
	IRQDBG("%s mask: 0x%08x irq: %d\n",__FUNCTION__,mask,irq);
	h720x_gpio_handler(mask, irq, desc, regs);
}

static void
h720x_gpiod_demux_handler(unsigned int irq_unused, struct irqdesc *desc,
			struct pt_regs *regs)
{
	unsigned int mask, irq;

	mask = CPU_REG(GPIO_D_VIRT,GPIO_STAT);
	irq = IRQ_CHAINED_GPIOD(0);
	IRQDBG("%s mask: 0x%08x irq: %d\n",__FUNCTION__,mask,irq);
	h720x_gpio_handler(mask, irq, desc, regs);
}

#ifdef CONFIG_CPU_H7202
static void
h720x_gpioe_demux_handler(unsigned int irq_unused, struct irqdesc *desc,
			struct pt_regs *regs)
{
	unsigned int mask, irq;

	mask = CPU_REG(GPIO_E_VIRT,GPIO_STAT);
	irq = IRQ_CHAINED_GPIOE(0);
	IRQDBG("%s mask: 0x%08x irq: %d\n",__FUNCTION__,mask,irq);
	h720x_gpio_handler(mask, irq, desc, regs);
}
#endif

static struct irqchip h720x_global_chip = {
	.ack = mask_global_irq,
	.mask = mask_global_irq,
	.unmask = unmask_global_irq,
};

static struct irqchip h720x_gpio_chip = {
	.ack = ack_gpio_irq,
	.mask = mask_gpio_irq,
	.unmask = unmask_gpio_irq,
};

/*
 * Initialize IRQ's, mask all, enable multiplexed irq's
 */
void __init h720x_init_irq (void)
{
	int 	irq;

	/* Mask global irq's */
	CPU_REG (IRQC_VIRT, IRQC_IER) = 0x0;

	/* Mask all multiplexed irq's */
	CPU_REG (GPIO_A_VIRT, GPIO_MASK) = 0x0;
	CPU_REG (GPIO_B_VIRT, GPIO_MASK) = 0x0;
	CPU_REG (GPIO_C_VIRT, GPIO_MASK) = 0x0;
	CPU_REG (GPIO_D_VIRT, GPIO_MASK) = 0x0;

	/* Initialize global IRQ's, fast path */
	for (irq = 0; irq < NR_GLBL_IRQS; irq++) {
		set_irq_chip(irq, &h720x_global_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	/* Initialize multiplexed IRQ's, slow path */
	for (irq = IRQ_CHAINED_GPIOA(0) ; irq <= IRQ_CHAINED_GPIOD(31); irq++) {
		set_irq_chip(irq, &h720x_gpio_chip);
		set_irq_handler(irq, do_edge_IRQ);
		set_irq_flags(irq, IRQF_VALID );
	}
	set_irq_chained_handler(IRQ_GPIOA, h720x_gpioa_demux_handler);
	set_irq_chained_handler(IRQ_GPIOB, h720x_gpiob_demux_handler);
	set_irq_chained_handler(IRQ_GPIOC, h720x_gpioc_demux_handler);
	set_irq_chained_handler(IRQ_GPIOD, h720x_gpiod_demux_handler);

#ifdef CONFIG_CPU_H7202
	for (irq = IRQ_CHAINED_GPIOE(0) ; irq <= IRQ_CHAINED_GPIOE(31); irq++) {
		set_irq_chip(irq, &h720x_gpio_chip);
		set_irq_handler(irq, do_edge_IRQ);
		set_irq_flags(irq, IRQF_VALID );
	}
	set_irq_chained_handler(IRQ_GPIOE, h720x_gpioe_demux_handler);
#endif

	/* Enable multiplexed irq's */
	CPU_REG (IRQC_VIRT, IRQC_IER) = IRQ_ENA_MUX;
}

static struct map_desc h720x_io_desc[] __initdata = {
	{ IO_VIRT, IO_PHYS, IO_SIZE, MT_DEVICE },
};

/* Initialize io tables */
void __init h720x_map_io(void)
{
	iotable_init(h720x_io_desc,ARRAY_SIZE(h720x_io_desc));
}
