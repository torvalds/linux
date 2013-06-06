/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/profile.h>
#include <linux/clocksource.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>

#define	TICK_SIZE		(tick_nsec / 1000)
#define NIOS2_TIMER_PERIOD	(timer_freq / HZ)

#define ALTERA_TIMER_STATUS_REG		0
#define ALTERA_TIMER_CONTROL_REG	4
#define ALTERA_TIMER_PERIODL_REG	8
#define ALTERA_TIMER_PERIODH_REG	12
#define ALTERA_TIMER_SNAPL_REG		16
#define ALTERA_TIMER_SNAPH_REG		20

#define ALTERA_TIMER_CONTROL_ITO_MSK	(0x1)
#define ALTERA_TIMER_CONTROL_CONT_MSK	(0x2)
#define ALTERA_TIMER_CONTROL_START_MSK	(0x4)
#define ALTERA_TIMER_CONTROL_STOP_MSK	(0x8)

static u32 nios2_timer_count;
static void __iomem *timer_membase;
static u32 timer_freq;

static inline unsigned long read_timersnapshot(void)
{
	unsigned long count;

	outw(0, timer_membase + ALTERA_TIMER_SNAPL_REG);
	count =
		inw(timer_membase + ALTERA_TIMER_SNAPH_REG) << 16 |
		inw(timer_membase + ALTERA_TIMER_SNAPL_REG);

	return count;
}

static inline void write_timerperiod(unsigned long period)
{
	outw(period, timer_membase + ALTERA_TIMER_PERIODL_REG);
	outw(period >> 16, timer_membase + ALTERA_TIMER_PERIODH_REG);
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "xtime_update()" routine every clocktick
 */
irqreturn_t timer_interrupt(int irq, void *dummy)
{
	/* Clear the interrupt condition */
	outw(0, timer_membase + ALTERA_TIMER_STATUS_REG);
	nios2_timer_count += NIOS2_TIMER_PERIOD;

	profile_tick(CPU_PROFILING);

	xtime_update(1);

	update_process_times(user_mode(get_irq_regs()));

	return IRQ_HANDLED;
}

static cycle_t nios2_timer_read(struct clocksource *cs)
{
	unsigned long flags;
	u32 cycles;
	u32 tcn;

	local_irq_save(flags);
	tcn = NIOS2_TIMER_PERIOD - 1 - read_timersnapshot();
	cycles = nios2_timer_count;
	local_irq_restore(flags);

	return cycles + tcn;
}

static struct clocksource nios2_timer = {
	.name	= "timer",
	.rating	= 250,
	.read	= nios2_timer_read,
	.shift	= 20,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct irqaction nios2_timer_irq = {
	.name		= "timer",
	.flags		= IRQF_TIMER,
	.handler	= timer_interrupt,
};

void __init nios2_late_time_init(void)
{
	u32 irq;
	unsigned int ctrl;
	struct device_node *timer =
		of_find_compatible_node(NULL, NULL, "ALTR,timer-1.0");

	BUG_ON(!timer);

	timer_membase = of_iomap(timer, 0);
	if (WARN_ON(!timer_membase))
		return;

	if (of_property_read_u32(timer, "clock-frequency", &timer_freq)) {
		pr_err("Can't get timer clock-frequency from device tree\n");
		return;
	}

	irq = irq_of_parse_and_map(timer, 0);
	if (irq < 0) {
		pr_err("Can't get timer interrupt\n");
		return;
	}
	setup_irq(irq, &nios2_timer_irq);

	write_timerperiod(NIOS2_TIMER_PERIOD - 1);

	/* clocksource initialize */
	nios2_timer.mult = clocksource_hz2mult(timer_freq, nios2_timer.shift);
	clocksource_register(&nios2_timer);

	/* interrupt enable + continuous + start */
	ctrl = ALTERA_TIMER_CONTROL_ITO_MSK | ALTERA_TIMER_CONTROL_CONT_MSK |
		ALTERA_TIMER_CONTROL_START_MSK;
	outw(ctrl, timer_membase + ALTERA_TIMER_CONTROL_REG);
}

void read_persistent_clock(struct timespec *ts)
{
	ts->tv_sec = mktime(2007, 1, 1, 0, 0, 0);
	ts->tv_nsec = 0;
}

void __init time_init(void)
{
	late_time_init = nios2_late_time_init;
}
