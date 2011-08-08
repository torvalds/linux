/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Support the cycle counter clocksource and tile timer clock event device.
 */

#include <linux/time.h>
#include <linux/timex.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/hardirq.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <asm/irq_regs.h>
#include <asm/traps.h>
#include <hv/hypervisor.h>
#include <arch/interrupts.h>
#include <arch/spr_def.h>


/*
 * Define the cycle counter clock source.
 */

/* How many cycles per second we are running at. */
static cycles_t cycles_per_sec __write_once;

cycles_t get_clock_rate(void)
{
	return cycles_per_sec;
}

#if CHIP_HAS_SPLIT_CYCLE()
cycles_t get_cycles(void)
{
	unsigned int high = __insn_mfspr(SPR_CYCLE_HIGH);
	unsigned int low = __insn_mfspr(SPR_CYCLE_LOW);
	unsigned int high2 = __insn_mfspr(SPR_CYCLE_HIGH);

	while (unlikely(high != high2)) {
		low = __insn_mfspr(SPR_CYCLE_LOW);
		high = high2;
		high2 = __insn_mfspr(SPR_CYCLE_HIGH);
	}

	return (((cycles_t)high) << 32) | low;
}
EXPORT_SYMBOL(get_cycles);
#endif

/*
 * We use a relatively small shift value so that sched_clock()
 * won't wrap around very often.
 */
#define SCHED_CLOCK_SHIFT 10

static unsigned long sched_clock_mult __write_once;

static cycles_t clocksource_get_cycles(struct clocksource *cs)
{
	return get_cycles();
}

static struct clocksource cycle_counter_cs = {
	.name = "cycle counter",
	.rating = 300,
	.read = clocksource_get_cycles,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Called very early from setup_arch() to set cycles_per_sec.
 * We initialize it early so we can use it to set up loops_per_jiffy.
 */
void __init setup_clock(void)
{
	cycles_per_sec = hv_sysconf(HV_SYSCONF_CPU_SPEED);
	sched_clock_mult =
		clocksource_hz2mult(cycles_per_sec, SCHED_CLOCK_SHIFT);
}

void __init calibrate_delay(void)
{
	loops_per_jiffy = get_clock_rate() / HZ;
	pr_info("Clock rate yields %lu.%02lu BogoMIPS (lpj=%lu)\n",
		loops_per_jiffy/(500000/HZ),
		(loops_per_jiffy/(5000/HZ)) % 100, loops_per_jiffy);
}

/* Called fairly late in init/main.c, but before we go smp. */
void __init time_init(void)
{
	/* Initialize and register the clock source. */
	clocksource_register_hz(&cycle_counter_cs, cycles_per_sec);

	/* Start up the tile-timer interrupt source on the boot cpu. */
	setup_tile_timer();
}


/*
 * Define the tile timer clock event device.  The timer is driven by
 * the TILE_TIMER_CONTROL register, which consists of a 31-bit down
 * counter, plus bit 31, which signifies that the counter has wrapped
 * from zero to (2**31) - 1.  The INT_TILE_TIMER interrupt will be
 * raised as long as bit 31 is set.
 *
 * The TILE_MINSEC value represents the largest range of real-time
 * we can possibly cover with the timer, based on MAX_TICK combined
 * with the slowest reasonable clock rate we might run at.
 */

#define MAX_TICK 0x7fffffff   /* we have 31 bits of countdown timer */
#define TILE_MINSEC 5         /* timer covers no more than 5 seconds */

static int tile_timer_set_next_event(unsigned long ticks,
				     struct clock_event_device *evt)
{
	BUG_ON(ticks > MAX_TICK);
	__insn_mtspr(SPR_TILE_TIMER_CONTROL, ticks);
	arch_local_irq_unmask_now(INT_TILE_TIMER);
	return 0;
}

/*
 * Whenever anyone tries to change modes, we just mask interrupts
 * and wait for the next event to get set.
 */
static void tile_timer_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	arch_local_irq_mask_now(INT_TILE_TIMER);
}

/*
 * Set min_delta_ns to 1 microsecond, since it takes about
 * that long to fire the interrupt.
 */
static DEFINE_PER_CPU(struct clock_event_device, tile_timer) = {
	.name = "tile timer",
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.min_delta_ns = 1000,
	.rating = 100,
	.irq = -1,
	.set_next_event = tile_timer_set_next_event,
	.set_mode = tile_timer_set_mode,
};

void __cpuinit setup_tile_timer(void)
{
	struct clock_event_device *evt = &__get_cpu_var(tile_timer);

	/* Fill in fields that are speed-specific. */
	clockevents_calc_mult_shift(evt, cycles_per_sec, TILE_MINSEC);
	evt->max_delta_ns = clockevent_delta2ns(MAX_TICK, evt);

	/* Mark as being for this cpu only. */
	evt->cpumask = cpumask_of(smp_processor_id());

	/* Start out with timer not firing. */
	arch_local_irq_mask_now(INT_TILE_TIMER);

	/* Register tile timer. */
	clockevents_register_device(evt);
}

/* Called from the interrupt vector. */
void do_timer_interrupt(struct pt_regs *regs, int fault_num)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct clock_event_device *evt = &__get_cpu_var(tile_timer);

	/*
	 * Mask the timer interrupt here, since we are a oneshot timer
	 * and there are now by definition no events pending.
	 */
	arch_local_irq_mask(INT_TILE_TIMER);

	/* Track time spent here in an interrupt context */
	irq_enter();

	/* Track interrupt count. */
	__get_cpu_var(irq_stat).irq_timer_count++;

	/* Call the generic timer handler */
	evt->event_handler(evt);

	/*
	 * Track time spent against the current process again and
	 * process any softirqs if they are waiting.
	 */
	irq_exit();

	set_irq_regs(old_regs);
}

/*
 * Scheduler clock - returns current time in nanosec units.
 * Note that with LOCKDEP, this is called during lockdep_init(), and
 * we will claim that sched_clock() is zero for a little while, until
 * we run setup_clock(), above.
 */
unsigned long long sched_clock(void)
{
	return clocksource_cyc2ns(get_cycles(),
				  sched_clock_mult, SCHED_CLOCK_SHIFT);
}

int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

/*
 * Use the tile timer to convert nsecs to core clock cycles, relying
 * on it having the same frequency as SPR_CYCLE.
 */
cycles_t ns2cycles(unsigned long nsecs)
{
	struct clock_event_device *dev = &__get_cpu_var(tile_timer);
	return ((u64)nsecs * dev->mult) >> dev->shift;
}
