/*
 * linux/arch/sh/kernel/irq_maskreg.c
 *
 * Copyright (C) 2001 A&D Co., Ltd. <http://www.aandd.co.jp>
 *
 * This file may be copied or modified under the terms of the GNU
 * General Public License.  See linux/COPYING for more information.
 *
 * Interrupt handling for Simple external interrupt mask register
 *
 * This is for the machine which have single 16 bit register
 * for masking external IRQ individually.
 * Each bit of the register is for masking each interrupt.  
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/machvec.h>

/* address of external interrupt mask register
 * address must be set prior to use these (maybe in init_XXX_irq())
 * XXX : is it better to use .config than specifying it in code? */
unsigned short *irq_mask_register = 0;

/* forward declaration */
static unsigned int startup_maskreg_irq(unsigned int irq);
static void shutdown_maskreg_irq(unsigned int irq);
static void enable_maskreg_irq(unsigned int irq);
static void disable_maskreg_irq(unsigned int irq);
static void mask_and_ack_maskreg(unsigned int);
static void end_maskreg_irq(unsigned int irq);

/* hw_interrupt_type */
static struct hw_interrupt_type maskreg_irq_type = {
	" Mask Register",
	startup_maskreg_irq,
	shutdown_maskreg_irq,
	enable_maskreg_irq,
	disable_maskreg_irq,
	mask_and_ack_maskreg,
	end_maskreg_irq
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
	if (irq_mask_register) {
		unsigned long flags;
		unsigned short val, mask = 0x01 << irq;

		/* Set "irq"th bit */
		local_irq_save(flags);
		val = ctrl_inw((unsigned long)irq_mask_register);
		val |= mask;
		ctrl_outw(val, (unsigned long)irq_mask_register);
		local_irq_restore(flags);
	}
}

static void enable_maskreg_irq(unsigned int irq)
{
	if (irq_mask_register) {
		unsigned long flags;
		unsigned short val, mask = ~(0x01 << irq);

		/* Clear "irq"th bit */
		local_irq_save(flags);
		val = ctrl_inw((unsigned long)irq_mask_register);
		val &= mask;
		ctrl_outw(val, (unsigned long)irq_mask_register);
		local_irq_restore(flags);
	}
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
