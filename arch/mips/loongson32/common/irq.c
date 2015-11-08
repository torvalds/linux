/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/irq_cpu.h>

#include <loongson1.h>
#include <irq.h>

#define LS1X_INTC_REG(n, x) \
		((void __iomem *)KSEG1ADDR(LS1X_INTC_BASE + (n * 0x18) + (x)))

#define LS1X_INTC_INTISR(n)		LS1X_INTC_REG(n, 0x0)
#define LS1X_INTC_INTIEN(n)		LS1X_INTC_REG(n, 0x4)
#define LS1X_INTC_INTSET(n)		LS1X_INTC_REG(n, 0x8)
#define LS1X_INTC_INTCLR(n)		LS1X_INTC_REG(n, 0xc)
#define LS1X_INTC_INTPOL(n)		LS1X_INTC_REG(n, 0x10)
#define LS1X_INTC_INTEDGE(n)		LS1X_INTC_REG(n, 0x14)

static void ls1x_irq_ack(struct irq_data *d)
{
	unsigned int bit = (d->irq - LS1X_IRQ_BASE) & 0x1f;
	unsigned int n = (d->irq - LS1X_IRQ_BASE) >> 5;

	__raw_writel(__raw_readl(LS1X_INTC_INTCLR(n))
			| (1 << bit), LS1X_INTC_INTCLR(n));
}

static void ls1x_irq_mask(struct irq_data *d)
{
	unsigned int bit = (d->irq - LS1X_IRQ_BASE) & 0x1f;
	unsigned int n = (d->irq - LS1X_IRQ_BASE) >> 5;

	__raw_writel(__raw_readl(LS1X_INTC_INTIEN(n))
			& ~(1 << bit), LS1X_INTC_INTIEN(n));
}

static void ls1x_irq_mask_ack(struct irq_data *d)
{
	unsigned int bit = (d->irq - LS1X_IRQ_BASE) & 0x1f;
	unsigned int n = (d->irq - LS1X_IRQ_BASE) >> 5;

	__raw_writel(__raw_readl(LS1X_INTC_INTIEN(n))
			& ~(1 << bit), LS1X_INTC_INTIEN(n));
	__raw_writel(__raw_readl(LS1X_INTC_INTCLR(n))
			| (1 << bit), LS1X_INTC_INTCLR(n));
}

static void ls1x_irq_unmask(struct irq_data *d)
{
	unsigned int bit = (d->irq - LS1X_IRQ_BASE) & 0x1f;
	unsigned int n = (d->irq - LS1X_IRQ_BASE) >> 5;

	__raw_writel(__raw_readl(LS1X_INTC_INTIEN(n))
			| (1 << bit), LS1X_INTC_INTIEN(n));
}

static struct irq_chip ls1x_irq_chip = {
	.name		= "LS1X-INTC",
	.irq_ack	= ls1x_irq_ack,
	.irq_mask	= ls1x_irq_mask,
	.irq_mask_ack	= ls1x_irq_mask_ack,
	.irq_unmask	= ls1x_irq_unmask,
};

static void ls1x_irq_dispatch(int n)
{
	u32 int_status, irq;

	/* Get pending sources, masked by current enables */
	int_status = __raw_readl(LS1X_INTC_INTISR(n)) &
			__raw_readl(LS1X_INTC_INTIEN(n));

	if (int_status) {
		irq = LS1X_IRQ(n, __ffs(int_status));
		do_IRQ(irq);
	}
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending;

	pending = read_c0_cause() & read_c0_status() & ST0_IM;

	if (pending & CAUSEF_IP7)
		do_IRQ(TIMER_IRQ);
	else if (pending & CAUSEF_IP2)
		ls1x_irq_dispatch(0); /* INT0 */
	else if (pending & CAUSEF_IP3)
		ls1x_irq_dispatch(1); /* INT1 */
	else if (pending & CAUSEF_IP4)
		ls1x_irq_dispatch(2); /* INT2 */
	else if (pending & CAUSEF_IP5)
		ls1x_irq_dispatch(3); /* INT3 */
	else if (pending & CAUSEF_IP6)
		ls1x_irq_dispatch(4); /* INT4 */
	else
		spurious_interrupt();

}

struct irqaction cascade_irqaction = {
	.handler = no_action,
	.name = "cascade",
	.flags = IRQF_NO_THREAD,
};

static void __init ls1x_irq_init(int base)
{
	int n;

	/* Disable interrupts and clear pending,
	 * setup all IRQs as high level triggered
	 */
	for (n = 0; n < 4; n++) {
		__raw_writel(0x0, LS1X_INTC_INTIEN(n));
		__raw_writel(0xffffffff, LS1X_INTC_INTCLR(n));
		__raw_writel(0xffffffff, LS1X_INTC_INTPOL(n));
		/* set DMA0, DMA1 and DMA2 to edge trigger */
		__raw_writel(n ? 0x0 : 0xe000, LS1X_INTC_INTEDGE(n));
	}


	for (n = base; n < LS1X_IRQS; n++) {
		irq_set_chip_and_handler(n, &ls1x_irq_chip,
					 handle_level_irq);
	}

	setup_irq(INT0_IRQ, &cascade_irqaction);
	setup_irq(INT1_IRQ, &cascade_irqaction);
	setup_irq(INT2_IRQ, &cascade_irqaction);
	setup_irq(INT3_IRQ, &cascade_irqaction);
}

void __init arch_init_irq(void)
{
	mips_cpu_irq_init();
	ls1x_irq_init(LS1X_IRQ_BASE);
}
