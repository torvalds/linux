/* arch/arm/plat-s3c64xx/irq.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - Interrupt handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/serial_core.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/vic.h>

#include <mach/map.h>
#include <plat/irq-vic-timer.h>
#include <plat/regs-serial.h>
#include <plat/cpu.h>

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
		.regs		= S3C_VA_UART0,
		.base_irq	= IRQ_S3CUART_BASE0,
		.parent_irq	= IRQ_UART0,
	},
	[1] = {
		.regs		= S3C_VA_UART1,
		.base_irq	= IRQ_S3CUART_BASE1,
		.parent_irq	= IRQ_UART1,
	},
	[2] = {
		.regs		= S3C_VA_UART2,
		.base_irq	= IRQ_S3CUART_BASE2,
		.parent_irq	= IRQ_UART2,
	},
	[3] = {
		.regs		= S3C_VA_UART3,
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

static void __init s3c64xx_uart_irq(struct uart_irq *uirq)
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

void __init s3c64xx_init_irq(u32 vic0_valid, u32 vic1_valid)
{
	int uart;

	printk(KERN_DEBUG "%s: initialising interrupts\n", __func__);

	/* initialise the pair of VICs */
	vic_init(S3C_VA_VIC0, S3C_VIC0_BASE, vic0_valid, 0);
	vic_init(S3C_VA_VIC1, S3C_VIC1_BASE, vic1_valid, 0);

	/* add the timer sub-irqs */

	s3c_init_vic_timer_irq(IRQ_TIMER0_VIC, IRQ_TIMER0);
	s3c_init_vic_timer_irq(IRQ_TIMER1_VIC, IRQ_TIMER1);
	s3c_init_vic_timer_irq(IRQ_TIMER2_VIC, IRQ_TIMER2);
	s3c_init_vic_timer_irq(IRQ_TIMER3_VIC, IRQ_TIMER3);
	s3c_init_vic_timer_irq(IRQ_TIMER4_VIC, IRQ_TIMER4);

	for (uart = 0; uart < ARRAY_SIZE(uart_irqs); uart++)
		s3c64xx_uart_irq(&uart_irqs[uart]);
}
