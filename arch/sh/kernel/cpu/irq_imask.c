/* $Id: irq_imask.c,v 1.1.2.1 2002/11/17 10:53:43 mrbrown Exp $
 *
 * linux/arch/sh/kernel/irq_imask.c
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka
 *
 * Simple interrupt handling using IMASK of SR register.
 *
 */

/* NOTE: Will not work on level 15 */


#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/irq.h>

#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/irq.h>

/* Bitmap of IRQ masked */
static unsigned long imask_mask = 0x7fff;
static int interrupt_priority = 0;

static void enable_imask_irq(unsigned int irq);
static void disable_imask_irq(unsigned int irq);
static void shutdown_imask_irq(unsigned int irq);
static void mask_and_ack_imask(unsigned int);
static void end_imask_irq(unsigned int irq);

#define IMASK_PRIORITY	15

static unsigned int startup_imask_irq(unsigned int irq)
{ 
	/* Nothing to do */
	return 0; /* never anything pending */
}

static struct hw_interrupt_type imask_irq_type = {
	"SR.IMASK",
	startup_imask_irq,
	shutdown_imask_irq,
	enable_imask_irq,
	disable_imask_irq,
	mask_and_ack_imask,
	end_imask_irq
};

void static inline set_interrupt_registers(int ip)
{
	unsigned long __dummy;

	asm volatile("ldc	%2, r6_bank\n\t"
		     "stc	sr, %0\n\t"
		     "and	#0xf0, %0\n\t"
		     "shlr2	%0\n\t"
		     "cmp/eq	#0x3c, %0\n\t"
		     "bt/s	1f	! CLI-ed\n\t"
		     " stc	sr, %0\n\t"
		     "and	%1, %0\n\t"
		     "or	%2, %0\n\t"
		     "ldc	%0, sr\n"
		     "1:"
		     : "=&z" (__dummy)
		     : "r" (~0xf0), "r" (ip << 4)
		     : "t");
}

static void disable_imask_irq(unsigned int irq)
{
	clear_bit(irq, &imask_mask);
	if (interrupt_priority < IMASK_PRIORITY - irq)
		interrupt_priority = IMASK_PRIORITY - irq;

	set_interrupt_registers(interrupt_priority);
}

static void enable_imask_irq(unsigned int irq)
{
	set_bit(irq, &imask_mask);
	interrupt_priority = IMASK_PRIORITY - ffz(imask_mask);

	set_interrupt_registers(interrupt_priority);
}

static void mask_and_ack_imask(unsigned int irq)
{
	disable_imask_irq(irq);
}

static void end_imask_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_imask_irq(irq);
}

static void shutdown_imask_irq(unsigned int irq)
{
	/* Nothing to do */
}

void make_imask_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &imask_irq_type;
	enable_irq(irq);
}
