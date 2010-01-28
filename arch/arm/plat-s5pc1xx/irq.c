/* arch/arm/plat-s5pc1xx/irq.c
 *
 * Copyright 2009 Samsung Electronics Co.
 *      Byungho Min <bhmin@samsung.com>
 *
 * S5PC1XX - Interrupt handling
 *
 * Based on plat-s3c64xx/irq.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/vic.h>

#include <mach/map.h>
#include <plat/regs-timer.h>
#include <plat/cpu.h>

/* Timer interrupt handling */

static void s3c_irq_demux_timer(unsigned int base_irq, unsigned int sub_irq)
{
	generic_handle_irq(sub_irq);
}

static void s3c_irq_demux_timer0(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER0);
}

static void s3c_irq_demux_timer1(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER1);
}

static void s3c_irq_demux_timer2(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER2);
}

static void s3c_irq_demux_timer3(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER3);
}

static void s3c_irq_demux_timer4(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER4);
}

/* We assume the IRQ_TIMER0..IRQ_TIMER4 range is continuous. */

static void s3c_irq_timer_mask(unsigned int irq)
{
	u32 reg = __raw_readl(S3C64XX_TINT_CSTAT);

	reg &= 0x1f;  /* mask out pending interrupts */
	reg &= ~(1 << (irq - IRQ_TIMER0));
	__raw_writel(reg, S3C64XX_TINT_CSTAT);
}

static void s3c_irq_timer_unmask(unsigned int irq)
{
	u32 reg = __raw_readl(S3C64XX_TINT_CSTAT);

	reg &= 0x1f;  /* mask out pending interrupts */
	reg |= 1 << (irq - IRQ_TIMER0);
	__raw_writel(reg, S3C64XX_TINT_CSTAT);
}

static void s3c_irq_timer_ack(unsigned int irq)
{
	u32 reg = __raw_readl(S3C64XX_TINT_CSTAT);

	reg &= 0x1f;  /* mask out pending interrupts */
	reg |= (1 << 5) << (irq - IRQ_TIMER0);
	__raw_writel(reg, S3C64XX_TINT_CSTAT);
}

static struct irq_chip s3c_irq_timer = {
	.name		= "s3c-timer",
	.mask		= s3c_irq_timer_mask,
	.unmask		= s3c_irq_timer_unmask,
	.ack		= s3c_irq_timer_ack,
};

struct uart_irq {
	void __iomem	*regs;
	unsigned int	 base_irq;
	unsigned int	 parent_irq;
};

/* Note, we make use of the fact that the parent IRQs, IRQ_UART[0..3]
 * are consecutive when looking up the interrupt in the demux routines.
 */
static struct uart_irq uart_irqs[] = {
	[0] = {
		.regs		= (void *)S3C_VA_UART0,
		.base_irq	= IRQ_S3CUART_BASE0,
		.parent_irq	= IRQ_UART0,
	},
	[1] = {
		.regs		= (void *)S3C_VA_UART1,
		.base_irq	= IRQ_S3CUART_BASE1,
		.parent_irq	= IRQ_UART1,
	},
	[2] = {
		.regs		= (void *)S3C_VA_UART2,
		.base_irq	= IRQ_S3CUART_BASE2,
		.parent_irq	= IRQ_UART2,
	},
	[3] = {
		.regs		= (void *)S3C_VA_UART3,
		.base_irq	= IRQ_S3CUART_BASE3,
		.parent_irq	= IRQ_UART3,
	},
};

static inline void __iomem *s3c_irq_uart_base(unsigned int irq)
{
	struct uart_irq *uirq = get_irq_chip_data(irq);
	return uirq->regs;
}

static inline unsigned int s3c_irq_uart_bit(unsigned int irq)
{
	return irq & 3;
}

/* UART interrupt registers, not worth adding to seperate include header */
#define S3C64XX_UINTP	0x30
#define S3C64XX_UINTSP	0x34
#define S3C64XX_UINTM	0x38

static void s3c_irq_uart_mask(unsigned int irq)
{
	void __iomem *regs = s3c_irq_uart_base(irq);
	unsigned int bit = s3c_irq_uart_bit(irq);
	u32 reg;

	reg = __raw_readl(regs + S3C64XX_UINTM);
	reg |= (1 << bit);
	__raw_writel(reg, regs + S3C64XX_UINTM);
}

static void s3c_irq_uart_maskack(unsigned int irq)
{
	void __iomem *regs = s3c_irq_uart_base(irq);
	unsigned int bit = s3c_irq_uart_bit(irq);
	u32 reg;

	reg = __raw_readl(regs + S3C64XX_UINTM);
	reg |= (1 << bit);
	__raw_writel(reg, regs + S3C64XX_UINTM);
	__raw_writel(1 << bit, regs + S3C64XX_UINTP);
}

static void s3c_irq_uart_unmask(unsigned int irq)
{
	void __iomem *regs = s3c_irq_uart_base(irq);
	unsigned int bit = s3c_irq_uart_bit(irq);
	u32 reg;

	reg = __raw_readl(regs + S3C64XX_UINTM);
	reg &= ~(1 << bit);
	__raw_writel(reg, regs + S3C64XX_UINTM);
}

static void s3c_irq_uart_ack(unsigned int irq)
{
	void __iomem *regs = s3c_irq_uart_base(irq);
	unsigned int bit = s3c_irq_uart_bit(irq);

	__raw_writel(1 << bit, regs + S3C64XX_UINTP);
}

static void s3c_irq_demux_uart(unsigned int irq, struct irq_desc *desc)
{
	struct uart_irq *uirq = &uart_irqs[irq - IRQ_UART0];
	u32 pend = __raw_readl(uirq->regs + S3C64XX_UINTP);
	int base = uirq->base_irq;

	if (pend & (1 << 0))
		generic_handle_irq(base);
	if (pend & (1 << 1))
		generic_handle_irq(base + 1);
	if (pend & (1 << 2))
		generic_handle_irq(base + 2);
	if (pend & (1 << 3))
		generic_handle_irq(base + 3);
}

static struct irq_chip s3c_irq_uart = {
	.name		= "s3c-uart",
	.mask		= s3c_irq_uart_mask,
	.unmask		= s3c_irq_uart_unmask,
	.mask_ack	= s3c_irq_uart_maskack,
	.ack		= s3c_irq_uart_ack,
};

static void __init s5pc1xx_uart_irq(struct uart_irq *uirq)
{
	void __iomem *reg_base = uirq->regs;
	unsigned int irq;
	int offs;

	/* mask all interrupts at the start. */
	__raw_writel(0xf, reg_base + S3C64XX_UINTM);

	for (offs = 0; offs < 3; offs++) {
		irq = uirq->base_irq + offs;

		set_irq_chip(irq, &s3c_irq_uart);
		set_irq_chip_data(irq, uirq);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	set_irq_chained_handler(uirq->parent_irq, s3c_irq_demux_uart);
}

void __init s5pc1xx_init_irq(u32 *vic_valid, int num)
{
	int i;
	int uart, irq;

	printk(KERN_DEBUG "%s: initialising interrupts\n", __func__);

	/* initialise the pair of VICs */
	for (i = 0; i < num; i++)
		vic_init((void *)S5PC1XX_VA_VIC(i), S3C_IRQ(i * S3C_IRQ_OFFSET),
				vic_valid[i], 0);

	/* add the timer sub-irqs */

	set_irq_chained_handler(IRQ_TIMER0, s3c_irq_demux_timer0);
	set_irq_chained_handler(IRQ_TIMER1, s3c_irq_demux_timer1);
	set_irq_chained_handler(IRQ_TIMER2, s3c_irq_demux_timer2);
	set_irq_chained_handler(IRQ_TIMER3, s3c_irq_demux_timer3);
	set_irq_chained_handler(IRQ_TIMER4, s3c_irq_demux_timer4);

	for (irq = IRQ_TIMER0; irq <= IRQ_TIMER4; irq++) {
		set_irq_chip(irq, &s3c_irq_timer);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	for (uart = 0; uart < ARRAY_SIZE(uart_irqs); uart++)
		s5pc1xx_uart_irq(&uart_irqs[uart]);
}


