/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/irq.h>

static unsigned int int_enable;
static unsigned int wake_enable;

static struct sirc_regs_t sirc_regs = {
	.int_enable       = SPSS_SIRC_INT_ENABLE,
	.int_enable_clear = SPSS_SIRC_INT_ENABLE_CLEAR,
	.int_enable_set   = SPSS_SIRC_INT_ENABLE_SET,
	.int_type         = SPSS_SIRC_INT_TYPE,
	.int_polarity     = SPSS_SIRC_INT_POLARITY,
	.int_clear        = SPSS_SIRC_INT_CLEAR,
};

static struct sirc_cascade_regs sirc_reg_table[] = {
	{
		.int_status  = SPSS_SIRC_IRQ_STATUS,
		.cascade_irq = INT_SIRC_0,
	}
};

static unsigned int save_type;
static unsigned int save_polarity;

/* Mask off the given interrupt. Keep the int_enable mask in sync with
   the enable reg, so it can be restored after power collapse. */
static void sirc_irq_mask(unsigned int irq)
{
	unsigned int mask;


	mask = 1 << (irq - FIRST_SIRC_IRQ);
	writel(mask, sirc_regs.int_enable_clear);
	int_enable &= ~mask;
	return;
}

/* Unmask the given interrupt. Keep the int_enable mask in sync with
   the enable reg, so it can be restored after power collapse. */
static void sirc_irq_unmask(unsigned int irq)
{
	unsigned int mask;

	mask = 1 << (irq - FIRST_SIRC_IRQ);
	writel(mask, sirc_regs.int_enable_set);
	int_enable |= mask;
	return;
}

static void sirc_irq_ack(unsigned int irq)
{
	unsigned int mask;

	mask = 1 << (irq - FIRST_SIRC_IRQ);
	writel(mask, sirc_regs.int_clear);
	return;
}

static int sirc_irq_set_wake(unsigned int irq, unsigned int on)
{
	unsigned int mask;

	/* Used to set the interrupt enable mask during power collapse. */
	mask = 1 << (irq - FIRST_SIRC_IRQ);
	if (on)
		wake_enable |= mask;
	else
		wake_enable &= ~mask;

	return 0;
}

static int sirc_irq_set_type(unsigned int irq, unsigned int flow_type)
{
	unsigned int mask;
	unsigned int val;

	mask = 1 << (irq - FIRST_SIRC_IRQ);
	val = readl(sirc_regs.int_polarity);

	if (flow_type & (IRQF_TRIGGER_LOW | IRQF_TRIGGER_FALLING))
		val |= mask;
	else
		val &= ~mask;

	writel(val, sirc_regs.int_polarity);

	val = readl(sirc_regs.int_type);
	if (flow_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		val |= mask;
		irq_desc[irq].handle_irq = handle_edge_irq;
	} else {
		val &= ~mask;
		irq_desc[irq].handle_irq = handle_level_irq;
	}

	writel(val, sirc_regs.int_type);

	return 0;
}

/* Finds the pending interrupt on the passed cascade irq and redrives it */
static void sirc_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned int reg = 0;
	unsigned int sirq;
	unsigned int status;

	while ((reg < ARRAY_SIZE(sirc_reg_table)) &&
		(sirc_reg_table[reg].cascade_irq != irq))
		reg++;

	status = readl(sirc_reg_table[reg].int_status);
	status &= SIRC_MASK;
	if (status == 0)
		return;

	for (sirq = 0;
	     (sirq < NR_SIRC_IRQS) && ((status & (1U << sirq)) == 0);
	     sirq++)
		;
	generic_handle_irq(sirq+FIRST_SIRC_IRQ);

	desc->chip->ack(irq);
}

static struct irq_chip sirc_irq_chip = {
	.name      = "sirc",
	.ack       = sirc_irq_ack,
	.mask      = sirc_irq_mask,
	.unmask    = sirc_irq_unmask,
	.set_wake  = sirc_irq_set_wake,
	.set_type  = sirc_irq_set_type,
};

void __init msm_init_sirc(void)
{
	int i;

	int_enable = 0;
	wake_enable = 0;

	for (i = FIRST_SIRC_IRQ; i < LAST_SIRC_IRQ; i++) {
		set_irq_chip(i, &sirc_irq_chip);
		set_irq_handler(i, handle_edge_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	for (i = 0; i < ARRAY_SIZE(sirc_reg_table); i++) {
		set_irq_chained_handler(sirc_reg_table[i].cascade_irq,
					sirc_irq_handler);
		set_irq_wake(sirc_reg_table[i].cascade_irq, 1);
	}
	return;
}

