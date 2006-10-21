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

static struct irq_chip r4030_irq_type = {
	.typename = "R4030",
	.startup = startup_r4030_irq,
	.shutdown = shutdown_r4030_irq,
	.enable = enable_r4030_irq,
	.disable = disable_r4030_irq,
	.ack = mask_and_ack_r4030_irq,
	.end = end_r4030_irq,
};

void __init init_r4030_ints(void)
{
	int i;

	for (i = JAZZ_PARALLEL_IRQ; i <= JAZZ_TIMER_IRQ; i++) {
		irq_desc[i].status     = IRQ_DISABLED;
		irq_desc[i].action     = 0;
		irq_desc[i].depth      = 1;
		irq_desc[i].chip    = &r4030_irq_type;
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
	init_i8259_irqs();			/* Integrated i8259  */
	init_r4030_ints();

	change_c0_status(ST0_IM, IE_IRQ4 | IE_IRQ3 | IE_IRQ2 | IE_IRQ1);
}

static void loc_call(unsigned int irq, unsigned int mask)
{
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE,
	                  r4030_read_reg16(JAZZ_IO_IRQ_ENABLE) & mask);
	do_IRQ(irq);
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE,
	                  r4030_read_reg16(JAZZ_IO_IRQ_ENABLE) | mask);
}

static void ll_local_dev(void)
{
	switch (r4030_read_reg32(JAZZ_IO_IRQ_SOURCE)) {
	case 0:
		panic("Unimplemented loc_no_irq handler");
		break;
	case 4:
		loc_call(JAZZ_PARALLEL_IRQ, JAZZ_IE_PARALLEL);
		break;
	case 8:
		loc_call(JAZZ_PARALLEL_IRQ, JAZZ_IE_FLOPPY);
		break;
	case 12:
		panic("Unimplemented loc_sound handler");
		break;
	case 16:
		panic("Unimplemented loc_video handler");
		break;
	case 20:
		loc_call(JAZZ_ETHERNET_IRQ, JAZZ_IE_ETHERNET);
		break;
	case 24:
		loc_call(JAZZ_SCSI_IRQ, JAZZ_IE_SCSI);
		break;
	case 28:
		loc_call(JAZZ_KEYBOARD_IRQ, JAZZ_IE_KEYBOARD);
		break;
	case 32:
		loc_call(JAZZ_MOUSE_IRQ, JAZZ_IE_MOUSE);
		break;
	case 36:
		loc_call(JAZZ_SERIAL1_IRQ, JAZZ_IE_SERIAL1);
		break;
	case 40:
		loc_call(JAZZ_SERIAL2_IRQ, JAZZ_IE_SERIAL2);
		break;
	}
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;

	if (pending & IE_IRQ5)
		write_c0_compare(0);
	else if (pending & IE_IRQ4) {
		r4030_read_reg32(JAZZ_TIMER_REGISTER);
		do_IRQ(JAZZ_TIMER_IRQ);
	} else if (pending & IE_IRQ3)
		panic("Unimplemented ISA NMI handler");
	else if (pending & IE_IRQ2)
		do_IRQ(r4030_read_reg32(JAZZ_EISA_IRQ_ACK));
	else if (pending & IE_IRQ1) {
		ll_local_dev();
	} else if (unlikely(pending & IE_IRQ0))
		panic("Unimplemented local_dma handler");
	else if (pending & IE_SW1) {
		clear_c0_cause(IE_SW1);
		panic("Unimplemented sw1 handler");
	} else if (pending & IE_SW0) {
		clear_c0_cause(IE_SW0);
		panic("Unimplemented sw0 handler");
	}
}
