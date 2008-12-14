/*
 * linux/arch/sh/boards/renesas/systemh/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SystemH Support.
 *
 * Modified for 7751 SystemH by
 * Jonathan Short.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <mach/systemh7751.h>
#include <asm/smc37c93x.h>

/* address of external interrupt mask register
 * address must be set prior to use these (maybe in init_XXX_irq())
 * XXX : is it better to use .config than specifying it in code? */
static unsigned long *systemh_irq_mask_register = (unsigned long *)0xB3F10004;
static unsigned long *systemh_irq_request_register = (unsigned long *)0xB3F10000;

/* forward declaration */
static void enable_systemh_irq(unsigned int irq);
static void disable_systemh_irq(unsigned int irq);
static void mask_and_ack_systemh(unsigned int);

static struct irq_chip systemh_irq_type = {
	.name = " SystemH Register",
	.unmask = enable_systemh_irq,
	.mask = disable_systemh_irq,
	.ack = mask_and_ack_systemh,
};

static void disable_systemh_irq(unsigned int irq)
{
	if (systemh_irq_mask_register) {
		unsigned long val, mask = 0x01 << 1;

		/* Clear the "irq"th bit in the mask and set it in the request */
		val = ctrl_inl((unsigned long)systemh_irq_mask_register);
		val &= ~mask;
		ctrl_outl(val, (unsigned long)systemh_irq_mask_register);

		val = ctrl_inl((unsigned long)systemh_irq_request_register);
		val |= mask;
		ctrl_outl(val, (unsigned long)systemh_irq_request_register);
	}
}

static void enable_systemh_irq(unsigned int irq)
{
	if (systemh_irq_mask_register) {
		unsigned long val, mask = 0x01 << 1;

		/* Set "irq"th bit in the mask register */
		val = ctrl_inl((unsigned long)systemh_irq_mask_register);
		val |= mask;
		ctrl_outl(val, (unsigned long)systemh_irq_mask_register);
	}
}

static void mask_and_ack_systemh(unsigned int irq)
{
	disable_systemh_irq(irq);
}

void make_systemh_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	set_irq_chip_and_handler(irq, &systemh_irq_type, handle_level_irq);
	disable_systemh_irq(irq);
}
