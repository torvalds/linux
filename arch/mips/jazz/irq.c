/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994 - 2001, 2003, 07 Ralf Baechle
 */
#include <linux/clockchips.h>
#include <linux/i8253.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/pgtable.h>

#include <asm/irq_cpu.h>
#include <asm/i8259.h>
#include <asm/io.h>
#include <asm/jazz.h>
#include <asm/tlbmisc.h>

static DEFINE_RAW_SPINLOCK(r4030_lock);

static void enable_r4030_irq(struct irq_data *d)
{
	unsigned int mask = 1 << (d->irq - JAZZ_IRQ_START);
	unsigned long flags;

	raw_spin_lock_irqsave(&r4030_lock, flags);
	mask |= r4030_read_reg16(JAZZ_IO_IRQ_ENABLE);
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE, mask);
	raw_spin_unlock_irqrestore(&r4030_lock, flags);
}

void disable_r4030_irq(struct irq_data *d)
{
	unsigned int mask = ~(1 << (d->irq - JAZZ_IRQ_START));
	unsigned long flags;

	raw_spin_lock_irqsave(&r4030_lock, flags);
	mask &= r4030_read_reg16(JAZZ_IO_IRQ_ENABLE);
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE, mask);
	raw_spin_unlock_irqrestore(&r4030_lock, flags);
}

static struct irq_chip r4030_irq_type = {
	.name = "R4030",
	.irq_mask = disable_r4030_irq,
	.irq_unmask = enable_r4030_irq,
};

void __init init_r4030_ints(void)
{
	int i;

	for (i = JAZZ_IRQ_START; i <= JAZZ_IRQ_END; i++)
		irq_set_chip_and_handler(i, &r4030_irq_type, handle_level_irq);

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
	/*
	 * this is a hack to get back the still needed wired mapping
	 * killed by init_mm()
	 */

	/* Map 0xe0000000 -> 0x0:800005C0, 0xe0010000 -> 0x1:30000580 */
	add_wired_entry(0x02000017, 0x03c00017, 0xe0000000, PM_64K);
	/* Map 0xe2000000 -> 0x0:900005C0, 0xe3010000 -> 0x0:910005C0 */
	add_wired_entry(0x02400017, 0x02440017, 0xe2000000, PM_16M);
	/* Map 0xe4000000 -> 0x0:600005C0, 0xe4100000 -> 400005C0 */
	add_wired_entry(0x01800017, 0x01000017, 0xe4000000, PM_4M);

	init_i8259_irqs();			/* Integrated i8259  */
	mips_cpu_irq_init();
	init_r4030_ints();

	change_c0_status(ST0_IM, IE_IRQ2 | IE_IRQ1);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status();
	unsigned int irq;

	if (pending & IE_IRQ4) {
		r4030_read_reg32(JAZZ_TIMER_REGISTER);
		do_IRQ(JAZZ_TIMER_IRQ);
	} else if (pending & IE_IRQ2) {
		irq = *(volatile u8 *)JAZZ_EISA_IRQ_ACK;
		do_IRQ(irq);
	} else if (pending & IE_IRQ1) {
		irq = *(volatile u8 *)JAZZ_IO_IRQ_SOURCE >> 2;
		if (likely(irq > 0))
			do_IRQ(irq + JAZZ_IRQ_START - 1);
		else
			panic("Unimplemented loc_no_irq handler");
	}
}

struct clock_event_device r4030_clockevent = {
	.name		= "r4030",
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.rating		= 300,
	.irq		= JAZZ_TIMER_IRQ,
};

static irqreturn_t r4030_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *cd = dev_id;

	cd->event_handler(cd);
	return IRQ_HANDLED;
}

void __init plat_time_init(void)
{
	struct clock_event_device *cd = &r4030_clockevent;
	unsigned int cpu = smp_processor_id();

	BUG_ON(HZ != 100);

	cd->cpumask		= cpumask_of(cpu);
	clockevents_register_device(cd);
	if (request_irq(JAZZ_TIMER_IRQ, r4030_timer_interrupt, IRQF_TIMER,
			"R4030 timer", cd))
		pr_err("Failed to register R4030 timer interrupt\n");

	/*
	 * Set clock to 100Hz.
	 *
	 * The R4030 timer receives an input clock of 1kHz which is divieded by
	 * a programmable 4-bit divider.  This makes it fairly inflexible.
	 */
	r4030_write_reg32(JAZZ_TIMER_INTERVAL, 9);
	setup_pit_timer();
}
