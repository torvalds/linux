/*
 * Copyright (C) 2000,2001,2004 Broadcom Corporation
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
 * bcm1480 general purpose timer 0.  We're using the timer as a
 * system clock, so we set it up to run at 100 Hz.  On every
 * interrupt, we update our idea of what the time of day is,
 * then call do_timer() in the architecture-independent kernel
 * code to do general bookkeeping (e.g. update jiffies, run
 * bottom halves, etc.)
 */
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>

#include <asm/irq.h>
#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/io.h>

#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/bcm1480_int.h>
#include <asm/sibyte/bcm1480_scd.h>

#include <asm/sibyte/sb1250.h>


#define IMR_IP2_VAL	K_BCM1480_INT_MAP_I0
#define IMR_IP3_VAL	K_BCM1480_INT_MAP_I1
#define IMR_IP4_VAL	K_BCM1480_INT_MAP_I2

#ifdef CONFIG_SIMULATION
#define BCM1480_HPT_VALUE	50000
#else
#define BCM1480_HPT_VALUE	1000000
#endif

extern int bcm1480_steal_irq(int irq);

void __init plat_time_init(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned int irq = K_BCM1480_INT_TIMER_0 + cpu;

	BUG_ON(cpu > 3);	/* Only have 4 general purpose timers */

	bcm1480_mask_irq(cpu, irq);

	/* Map the timer interrupt to ip[4] of this cpu */
	__raw_writeq(IMR_IP4_VAL, IOADDR(A_BCM1480_IMR_REGISTER(cpu, R_BCM1480_IMR_INTERRUPT_MAP_BASE_H)
	      + (irq<<3)));

	bcm1480_unmask_irq(cpu, irq);
	bcm1480_steal_irq(irq);
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

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		__raw_writeq(0, timer_cfg);
		__raw_writeq(BCM1480_HPT_VALUE / HZ - 1, timer_init);
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

static int sibyte_next_event(unsigned long delta, struct clock_event_device *cd)
{
	unsigned int cpu = smp_processor_id();
	void __iomem *timer_init;
	unsigned int cnt;
	int res;

	timer_init = IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_INIT));
	cnt = __raw_readq(timer_init);
	cnt += delta;
	__raw_writeq(cnt, timer_init);
	res = ((long)(__raw_readq(timer_init) - cnt ) > 0) ? -ETIME : 0;

	return res;
}

static DEFINE_PER_CPU(struct clock_event_device, sibyte_hpt_clockevent);

static irqreturn_t sibyte_counter_handler(int irq, void *dev_id)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd = &per_cpu(sibyte_hpt_clockevent, cpu);

	/* Reset the timer */
	__raw_writeq(M_SCD_TIMER_ENABLE | M_SCD_TIMER_MODE_CONTINUOUS,
	             IOADDR(A_SCD_TIMER_REGISTER(cpu, R_SCD_TIMER_CFG)));
	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static struct irqaction sibyte_counter_irqaction = {
	.handler	= sibyte_counter_handler,
	.flags		= IRQF_DISABLED | IRQF_PERCPU,
	.name		= "timer",
};

/*
 * This interrupt is "special" in that it doesn't use the request_irq
 * way to hook the irq line.  The timer interrupt is initialized early
 * enough to make this a major pain, and it's also firing enough to
 * warrant a bit of special case code.  bcm1480_timer_interrupt is
 * called directly from irq_handler.S when IP[4] is set during an
 * interrupt
 */
void __cpuinit sb1480_clockevent_init(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned int irq = K_BCM1480_INT_TIMER_0 + cpu;
	struct clock_event_device *cd = &per_cpu(sibyte_hpt_clockevent, cpu);

	cd->name		= "bcm1480-counter";
	cd->features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_MODE_ONESHOT;
	cd->set_next_event	= sibyte_next_event;
	cd->set_mode		= sibyte_set_mode;
	cd->irq			= irq;
	clockevent_set_clock(cd, BCM1480_HPT_VALUE);

	setup_irq(irq, &sibyte_counter_irqaction);
}

static cycle_t bcm1480_hpt_read(void)
{
	/* We assume this function is called xtime_lock held. */
	unsigned long count =
		__raw_readq(IOADDR(A_SCD_TIMER_REGISTER(0, R_SCD_TIMER_CNT)));
	return (jiffies + 1) * (BCM1480_HPT_VALUE / HZ) - count;
}

struct clocksource bcm1480_clocksource = {
	.name	= "MIPS",
	.rating	= 200,
	.read	= bcm1480_hpt_read,
	.mask	= CLOCKSOURCE_MASK(32),
	.shift	= 32,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

void __init sb1480_clocksource_init(void)
{
	struct clocksource *cs = &bcm1480_clocksource;

	clocksource_set_clock(cs, BCM1480_HPT_VALUE);
	clocksource_register(cs);
}

void __init bcm1480_hpt_setup(void)
{
	mips_hpt_frequency = BCM1480_HPT_VALUE;
	sb1480_clocksource_init();
	sb1480_clockevent_init();
}
