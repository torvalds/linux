/* arch/arm/plat-samsung/irq-uart.c
 *	originally part of arch/arm/plat-s3c64xx/irq.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * Samsung- UART Interrupt handling
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

#include <mach/map.h>
#include <plat/irq-uart.h>
#include <plat/regs-serial.h>
#include <plat/cpu.h>

/* Note, we make use of the fact that the parent IRQs, IRQ_UART[0..3]
 * are consecutive when looking up the interrupt in the demux routines.
 */

static inline void __iomem *s3c_irq_uart_base(unsigned int irq)
{
	struct s3c_uart_irq *uirq = get_irq_chip_data(irq);
	return uirq->regs;
}

static inline unsigned int s3c_irq_uart_bit(unsigned int irq)
{
	return irq & 3;
}

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
	struct s3c_uart_irq *uirq = desc->handler_data;
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

static void __init s3c_init_uart_irq(struct s3c_uart_irq *uirq)
{
	struct irq_desc *desc = irq_to_desc(uirq->parent_irq);
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

	desc->handler_data = uirq;
	set_irq_chained_handler(uirq->parent_irq, s3c_irq_demux_uart);
}

/**
 * s3c_init_uart_irqs() - initialise UART IRQs and the necessary demuxing
 * @irq: The interrupt data for registering
 * @nr_irqs: The number of interrupt descriptions in @irq.
 *
 * Register the UART interrupts specified by @irq including the demuxing
 * routines. This supports the S3C6400 and newer style of devices.
 */
void __init s3c_init_uart_irqs(struct s3c_uart_irq *irq, unsigned int nr_irqs)
{
	for (; nr_irqs > 0; nr_irqs--, irq++)
		s3c_init_uart_irq(irq);
}
