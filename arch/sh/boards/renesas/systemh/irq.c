/*
 * linux/arch/sh/boards/systemh/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SystemH Support.
 *
 * Modified for 7751 SystemH by
 * Jonathan Short.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <linux/hdreg.h>
#include <linux/ide.h>
#include <asm/io.h>
#include <asm/mach/7751systemh.h>
#include <asm/smc37c93x.h>

/* address of external interrupt mask register
 * address must be set prior to use these (maybe in init_XXX_irq())
 * XXX : is it better to use .config than specifying it in code? */
static unsigned long *systemh_irq_mask_register = (unsigned long *)0xB3F10004;
static unsigned long *systemh_irq_request_register = (unsigned long *)0xB3F10000;

/* forward declaration */
static unsigned int startup_systemh_irq(unsigned int irq);
static void shutdown_systemh_irq(unsigned int irq);
static void enable_systemh_irq(unsigned int irq);
static void disable_systemh_irq(unsigned int irq);
static void mask_and_ack_systemh(unsigned int);
static void end_systemh_irq(unsigned int irq);

/* hw_interrupt_type */
static struct hw_interrupt_type systemh_irq_type = {
	.typename = " SystemH Register",
	.startup = startup_systemh_irq,
	.shutdown = shutdown_systemh_irq,
	.enable = enable_systemh_irq,
	.disable = disable_systemh_irq,
	.ack = mask_and_ack_systemh,
	.end = end_systemh_irq
};

static unsigned int startup_systemh_irq(unsigned int irq)
{
	enable_systemh_irq(irq);
	return 0; /* never anything pending */
}

static void shutdown_systemh_irq(unsigned int irq)
{
	disable_systemh_irq(irq);
}

static void disable_systemh_irq(unsigned int irq)
{
	if (systemh_irq_mask_register) {
		unsigned long flags;
		unsigned long val, mask = 0x01 << 1;

		/* Clear the "irq"th bit in the mask and set it in the request */
		local_irq_save(flags);

		val = ctrl_inl((unsigned long)systemh_irq_mask_register);
		val &= ~mask;
		ctrl_outl(val, (unsigned long)systemh_irq_mask_register);

		val = ctrl_inl((unsigned long)systemh_irq_request_register);
		val |= mask;
		ctrl_outl(val, (unsigned long)systemh_irq_request_register);

		local_irq_restore(flags);
	}
}

static void enable_systemh_irq(unsigned int irq)
{
	if (systemh_irq_mask_register) {
		unsigned long flags;
		unsigned long val, mask = 0x01 << 1;

		/* Set "irq"th bit in the mask register */
		local_irq_save(flags);
		val = ctrl_inl((unsigned long)systemh_irq_mask_register);
		val |= mask;
		ctrl_outl(val, (unsigned long)systemh_irq_mask_register);
		local_irq_restore(flags);
	}
}

static void mask_and_ack_systemh(unsigned int irq)
{
	disable_systemh_irq(irq);
}

static void end_systemh_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_systemh_irq(irq);
}

void make_systemh_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &systemh_irq_type;
	disable_systemh_irq(irq);
}

