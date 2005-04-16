/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994 - 2001, 2003 Ralf Baechle
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

#include <asm/i8259.h>
#include <asm/io.h>
#include <asm/jazz.h>

extern asmlinkage void jazz_handle_int(void);

static DEFINE_SPINLOCK(r4030_lock);

static void enable_r4030_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - JAZZ_PARALLEL_IRQ);
	unsigned long flags;

	spin_lock_irqsave(&r4030_lock, flags);
	mask |= r4030_read_reg16(JAZZ_IO_IRQ_ENABLE);
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE, mask);
	spin_unlock_irqrestore(&r4030_lock, flags);
}

static unsigned int startup_r4030_irq(unsigned int irq)
{
	enable_r4030_irq(irq);
	return 0; /* never anything pending */
}

#define shutdown_r4030_irq	disable_r4030_irq

void disable_r4030_irq(unsigned int irq)
{
	unsigned int mask = ~(1 << (irq - JAZZ_PARALLEL_IRQ));
	unsigned long flags;

	spin_lock_irqsave(&r4030_lock, flags);
	mask &= r4030_read_reg16(JAZZ_IO_IRQ_ENABLE);
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE, mask);
	spin_unlock_irqrestore(&r4030_lock, flags);
}

#define mask_and_ack_r4030_irq disable_r4030_irq

static void end_r4030_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_r4030_irq(irq);
}

static struct hw_interrupt_type r4030_irq_type = {
	"R4030",
	startup_r4030_irq,
	shutdown_r4030_irq,
	enable_r4030_irq,
	disable_r4030_irq,
	mask_and_ack_r4030_irq,
	end_r4030_irq,
	NULL
};

void __init init_r4030_ints(void)
{
	int i;

	for (i = JAZZ_PARALLEL_IRQ; i <= JAZZ_TIMER_IRQ; i++) {
		irq_desc[i].status     = IRQ_DISABLED;
		irq_desc[i].action     = 0;
		irq_desc[i].depth      = 1;
		irq_desc[i].handler    = &r4030_irq_type;
	}

	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE, 0);
	r4030_read_reg16(JAZZ_IO_IRQ_SOURCE);		/* clear pending IRQs */
	r4030_read_reg32(JAZZ_R4030_INVAL_ADDR);	/* clear error bits */
}

/*
 * On systems with i8259-style interrupt controllers we assume for
 * driver compatibility reasons interrupts 0 - 15 to be the i8259
 * interrupts even if the hardware uses a different interrupt numbering.
 */
void __init arch_init_irq(void)
{
	set_except_vector(0, jazz_handle_int);

	init_i8259_irqs();			/* Integrated i8259  */
	init_r4030_ints();

	change_c0_status(ST0_IM, IE_IRQ4 | IE_IRQ3 | IE_IRQ2 | IE_IRQ1);
}
