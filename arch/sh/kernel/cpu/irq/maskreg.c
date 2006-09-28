/*
 * Interrupt handling for Simple external interrupt mask register
 *
 * Copyright (C) 2001 A&D Co., Ltd. <http://www.aandd.co.jp>
 *
 * This is for the machine which have single 16 bit register
 * for masking external IRQ individually.
 * Each bit of the register is for masking each interrupt.
 *
 * This file may be copied or modified under the terms of the GNU
 * General Public License.  See linux/COPYING for more information.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/system.h>
#include <asm/io.h>

/* address of external interrupt mask register */
unsigned long irq_mask_register;

/* forward declaration */
static unsigned int startup_maskreg_irq(unsigned int irq);
static void shutdown_maskreg_irq(unsigned int irq);
static void enable_maskreg_irq(unsigned int irq);
static void disable_maskreg_irq(unsigned int irq);
static void mask_and_ack_maskreg(unsigned int);
static void end_maskreg_irq(unsigned int irq);

/* hw_interrupt_type */
static struct hw_interrupt_type maskreg_irq_type = {
	.typename = "Mask Register",
	.startup = startup_maskreg_irq,
	.shutdown = shutdown_maskreg_irq,
	.enable = enable_maskreg_irq,
	.disable = disable_maskreg_irq,
	.ack = mask_and_ack_maskreg,
	.end = end_maskreg_irq
};

/* actual implementatin */
static unsigned int startup_maskreg_irq(unsigned int irq)
{
	enable_maskreg_irq(irq);
	return 0; /* never anything pending */
}

static void shutdown_maskreg_irq(unsigned int irq)
{
	disable_maskreg_irq(irq);
}

static void disable_maskreg_irq(unsigned int irq)
{
	unsigned short val, mask = 0x01 << irq;

	BUG_ON(!irq_mask_register);

	/* Set "irq"th bit */
	val = ctrl_inw(irq_mask_register);
	val |= mask;
	ctrl_outw(val, irq_mask_register);
}

static void enable_maskreg_irq(unsigned int irq)
{
	unsigned short val, mask = ~(0x01 << irq);

	BUG_ON(!irq_mask_register);

	/* Clear "irq"th bit */
	val = ctrl_inw(irq_mask_register);
	val &= mask;
	ctrl_outw(val, irq_mask_register);
}

static void mask_and_ack_maskreg(unsigned int irq)
{
	disable_maskreg_irq(irq);
}

static void end_maskreg_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_maskreg_irq(irq);
}

void make_maskreg_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &maskreg_irq_type;
	disable_maskreg_irq(irq);
}
