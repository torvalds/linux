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

#include <asm/mach/irq.h>

#include <mach/map.h>
#include <plat/irq-uart.h>
#include <plat/regs-serial.h>
#include <plat/cpu.h>

/* Note, we make use of the fact that the parent IRQs, IRQ_UART[0..3]
 * are consecutive when looking up the interrupt in the demux routines.
 */
static void s3c_irq_demux_uart(unsigned int irq, struct irq_desc *desc)
{
	struct s3c_uart_irq *uirq = desc->irq_data.handler_data;
	struct irq_chip *chip = irq_get_chip(irq);
	u32 pend = __raw_readl(uirq->regs + S3C64XX_UINTP);
	int base = uirq->base_irq;

	chained_irq_enter(chip, desc);

	if (pend & (1 << 0))
		generic_handle_irq(base);
	if (pend & (1 << 1))
		generic_handle_irq(base + 1);
	if (pend & (1 << 2))
		generic_handle_irq(base + 2);
	if (pend & (1 << 3))
		generic_handle_irq(base + 3);

	chained_irq_exit(chip, desc);
}

static void __init s3c_init_uart_irq(struct s3c_uart_irq *uirq)
{
	void __iomem *reg_base = uirq->regs;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	/* mask all interrupts at the start. */
	__raw_writel(0xf, reg_base + S3C64XX_UINTM);

	gc = irq_alloc_generic_chip("s3c-uart", 1, uirq->base_irq, reg_base,
				    handle_level_irq);

	if (!gc) {
		pr_err("%s: irq_alloc_generic_chip for IRQ %u failed\n",
		       __func__, uirq->base_irq);
		return;
	}

	ct = gc->chip_types;
	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->chip.irq_mask = irq_gc_mask_set_bit;
	ct->chip.irq_unmask = irq_gc_mask_clr_bit;
	ct->regs.ack = S3C64XX_UINTP;
	ct->regs.mask = S3C64XX_UINTM;
	irq_setup_generic_chip(gc, IRQ_MSK(4), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST | IRQ_NOPROBE, 0);

	irq_set_handler_data(uirq->parent_irq, uirq);
	irq_set_chained_handler(uirq->parent_irq, s3c_irq_demux_uart);
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
