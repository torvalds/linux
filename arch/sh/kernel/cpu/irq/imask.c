/*
 * arch/sh/kernel/cpu/irq/imask.c
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
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/irq.h>
#include <linux/bitmap.h>
#include <asm/irq.h>

/* Bitmap of IRQ masked */
#define IMASK_PRIORITY	15

static DECLARE_BITMAP(imask_mask, IMASK_PRIORITY);
static int interrupt_priority;

static inline void set_interrupt_registers(int ip)
{
	unsigned long __dummy;

	asm volatile(
#ifdef CONFIG_CPU_HAS_SR_RB
		     "ldc	%2, r6_bank\n\t"
#endif
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

static void mask_imask_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;

	clear_bit(irq, imask_mask);
	if (interrupt_priority < IMASK_PRIORITY - irq)
		interrupt_priority = IMASK_PRIORITY - irq;
	set_interrupt_registers(interrupt_priority);
}

static void unmask_imask_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;

	set_bit(irq, imask_mask);
	interrupt_priority = IMASK_PRIORITY -
		find_first_zero_bit(imask_mask, IMASK_PRIORITY);
	set_interrupt_registers(interrupt_priority);
}

static struct irq_chip imask_irq_chip = {
	.name		= "SR.IMASK",
	.irq_mask	= mask_imask_irq,
	.irq_unmask	= unmask_imask_irq,
	.irq_mask_ack	= mask_imask_irq,
};

void make_imask_irq(unsigned int irq)
{
	irq_set_chip_and_handler_name(irq, &imask_irq_chip, handle_level_irq,
				      "level");
}
