/*
 * Copyright (C) 2000, 2001 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * These are routines to set up and handle interrupts from the
 * sb1250 general purpose timer 0.  We're using the timer as a
 * system clock, so we set it up to run at 100 Hz.  On every
 * interrupt, we update our idea of what the time of day is,
 * then call do_timer() in the architecture-independent kernel
 * code to do general bookkeeping (e.g. update jiffies, run
 * bottom halves, etc.)
 */
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>

#include <asm/irq.h>
#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/io.h>

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>
#include <asm/sibyte/sb1250_scd.h>


#define IMR_IP2_VAL	K_INT_MAP_I0
#define IMR_IP3_VAL	K_INT_MAP_I1
#define IMR_IP4_VAL	K_INT_MAP_I2

#define SB1250_HPT_NUM		3
#define SB1250_HPT_VALUE	M_SCD_TIMER_CNT /* max value */


extern int sb1250_steal_irq(int irq);

static cycle_t sb1250_hpt_read(void);

void __init sb1250_hpt_setup(void)
{
	int cpu = smp_processor_id();

	if (!cpu) {
		/* Setup hpt using timer #3 but do not enable irq for it */
		__raw_writeq(0, IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM, R_SCD_TIMER_CFG)));
		__raw_writeq(SB1250_HPT_VALUE,
			     IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM, R_SCD_TIMER_INIT)));
		__raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS,
			     IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM, R_SCD_TIMER_CFG)));

		mips_hpt_frequency = V_SCD_TIMER_FREQ;
		clocksource_mips.read = sb1250_hpt_read;
		clocksource_mips.mask = M_SCD_TIMER_INIT;
	}
}

/*
 * The general purpose timer ticks at 1 Mhz independent if
 * the rest of the system
 */
static void sibyte_set_mode(enum clock_event_mode mode,
                           struct clock_event_device *evt)
{
	unsigned int cpu = smp_processor_id();
	void __iomem *timer_cfg, *timer_init;

	timer_cfg = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG));
	timer_init = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT));

	switch(mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		__raw_writeq(0, timer_cfg);
		__raw_writeq((V_SCD_TIMER_FREQ / HZ) - 1, timer_init);
		__raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS,
			     timer_cfg);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* Stop the timer until we actually program a shot */
	case CLOCK_EVT_MODE_SHUTDOWN:
		__raw_writeq(0, timer_cfg);
		break;

	case CLOCK_EVT_MODE_UNUSED:	/* shuddup gcc */
	case CLOCK_EVT_MODE_RESUME:
		;
	}
}

static int
sibyte_next_event(unsigned long delta, struct clock_event_device *evt)
{
	unsigned int cpu = smp_processor_id();
	void __iomem *timer_cfg, *timer_init;

	timer_cfg = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG));
	timer_init = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT));

	__raw_writeq(0, timer_cfg);
	__raw_writeq(delta, timer_init);
	__raw_writeq(M_SCD_TIMER_ENABLE, timer_cfg);

	return 0;
}

struct clock_event_device sibyte_hpt_clockevent = {
	.name		= "sb1250-counter",
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.set_mode	= sibyte_set_mode,
	.set_next_event	= sibyte_next_event,
	.shift		= 32,
	.irq		= 0,
};

static irqreturn_t sibyte_counter_handler(int irq, void *dev_id)
{
	struct clock_event_device *cd = &sibyte_hpt_clockevent;

	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static struct irqaction sibyte_irqaction = {
	.handler	= sibyte_counter_handler,
	.flags		= IRQF_DISABLED | IRQF_PERCPU,
	.name		= "timer",
};

void __cpuinit sb1250_clockevent_init(void)
{
	struct clock_event_device *cd = &sibyte_hpt_clockevent;
	unsigned int cpu = smp_processor_id();
	int irq = K_INT_TIMER_0 + cpu;

	/* Only have 4 general purpose timers, and we use last one as hpt */
	BUG_ON(cpu > 2);

	sb1250_mask_irq(cpu, irq);

	/* Map the timer interrupt to ip[4] of this cpu */
	__raw_writeq(IMR_IP4_VAL,
		     IOADDR(A_IMR_REGISTER(cpu, R_IMR_INTERRUPT_MAP_BASE) +
			    (irq << 3)));
	cd->cpumask = cpumask_of_cpu(0);

	sb1250_unmask_irq(cpu, irq);
	sb1250_steal_irq(irq);

	/*
	 * This interrupt is "special" in that it doesn't use the request_irq
	 * way to hook the irq line.  The timer interrupt is initialized early
	 * enough to make this a major pain, and it's also firing enough to
	 * warrant a bit of special case code.  sb1250_timer_interrupt is
	 * called directly from irq_handler.S when IP[4] is set during an
	 * interrupt
	 */
	setup_irq(irq, &sibyte_irqaction);

	clockevents_register_device(cd);
}

/*
 * The HPT is free running from SB1250_HPT_VALUE down to 0 then starts over
 * again.
 */
static cycle_t sb1250_hpt_read(void)
{
	unsigned int count;

	count = G_SCD_TIMER_CNT(__raw_readq(IOADDR(A_SCD_TIMER_REGISTER(SB1250_HPT_NUM, R_SCD_TIMER_CNT))));

	return SB1250_HPT_VALUE - count;
}

struct clocksource bcm1250_clocksource = {
	.name	= "MIPS",
	.rating	= 200,
	.read	= sb1250_hpt_read,
	.mask	= CLOCKSOURCE_MASK(32),
	.shift	= 32,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

void __init sb1250_clocksource_init(void)
{
	struct clocksource *cs = &bcm1250_clocksource;

	clocksource_set_clock(cs, V_SCD_TIMER_FREQ);
	clocksource_register(cs);
}

void __init plat_time_init(void)
{
	sb1250_clocksource_init();
	sb1250_clockevent_init();
}
