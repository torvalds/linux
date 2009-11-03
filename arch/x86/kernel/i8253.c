/*
 * 8253/PIT functions
 *
 */
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/timex.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/i8253.h>
#include <asm/hpet.h>
#include <asm/smp.h>

DEFINE_SPINLOCK(i8253_lock);
EXPORT_SYMBOL(i8253_lock);

/*
 * HPET replaces the PIT, when enabled. So we need to know, which of
 * the two timers is used
 */
struct clock_event_device *global_clock_event;

/*
 * Initialize the PIT timer.
 *
 * This is also called after resume to bring the PIT into operation again.
 */
static void init_pit_timer(enum clock_event_mode mode,
			   struct clock_event_device *evt)
{
	spin_lock(&i8253_lock);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* binary, mode 2, LSB/MSB, ch 0 */
		outb_pit(0x34, PIT_MODE);
		outb_pit(LATCH & 0xff , PIT_CH0);	/* LSB */
		outb_pit(LATCH >> 8 , PIT_CH0);		/* MSB */
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		if (evt->mode == CLOCK_EVT_MODE_PERIODIC ||
		    evt->mode == CLOCK_EVT_MODE_ONESHOT) {
			outb_pit(0x30, PIT_MODE);
			outb_pit(0, PIT_CH0);
			outb_pit(0, PIT_CH0);
		}
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* One shot setup */
		outb_pit(0x38, PIT_MODE);
		break;

	case CLOCK_EVT_MODE_RESUME:
		/* Nothing to do here */
		break;
	}
	spin_unlock(&i8253_lock);
}

/*
 * Program the next event in oneshot mode
 *
 * Delta is given in PIT ticks
 */
static int pit_next_event(unsigned long delta, struct clock_event_device *evt)
{
	spin_lock(&i8253_lock);
	outb_pit(delta & 0xff , PIT_CH0);	/* LSB */
	outb_pit(delta >> 8 , PIT_CH0);		/* MSB */
	spin_unlock(&i8253_lock);

	return 0;
}

/*
 * On UP the PIT can serve all of the possible timer functions. On SMP systems
 * it can be solely used for the global tick.
 *
 * The profiling and update capabilities are switched off once the local apic is
 * registered. This mechanism replaces the previous #ifdef LOCAL_APIC -
 * !using_apic_timer decisions in do_timer_interrupt_hook()
 */
static struct clock_event_device pit_ce = {
	.name		= "pit",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= init_pit_timer,
	.set_next_event = pit_next_event,
	.shift		= 32,
	.irq		= 0,
};

/*
 * Initialize the conversion factor and the min/max deltas of the clock event
 * structure and register the clock event source with the framework.
 */
void __init setup_pit_timer(void)
{
	/*
	 * Start pit with the boot cpu mask and make it global after the
	 * IO_APIC has been initialized.
	 */
	pit_ce.cpumask = cpumask_of(smp_processor_id());
	pit_ce.mult = div_sc(CLOCK_TICK_RATE, NSEC_PER_SEC, pit_ce.shift);
	pit_ce.max_delta_ns = clockevent_delta2ns(0x7FFF, &pit_ce);
	pit_ce.min_delta_ns = clockevent_delta2ns(0xF, &pit_ce);

	clockevents_register_device(&pit_ce);
	global_clock_event = &pit_ce;
}

#ifndef CONFIG_X86_64
/*
 * Since the PIT overflows every tick, its not very useful
 * to just read by itself. So use jiffies to emulate a free
 * running counter:
 */
static cycle_t pit_read(struct clocksource *cs)
{
	static int old_count;
	static u32 old_jifs;
	unsigned long flags;
	int count;
	u32 jifs;

	spin_lock_irqsave(&i8253_lock, flags);
	/*
	 * Although our caller may have the read side of xtime_lock,
	 * this is now a seqlock, and we are cheating in this routine
	 * by having side effects on state that we cannot undo if
	 * there is a collision on the seqlock and our caller has to
	 * retry.  (Namely, old_jifs and old_count.)  So we must treat
	 * jiffies as volatile despite the lock.  We read jiffies
	 * before latching the timer count to guarantee that although
	 * the jiffies value might be older than the count (that is,
	 * the counter may underflow between the last point where
	 * jiffies was incremented and the point where we latch the
	 * count), it cannot be newer.
	 */
	jifs = jiffies;
	outb_pit(0x00, PIT_MODE);	/* latch the count ASAP */
	count = inb_pit(PIT_CH0);	/* read the latched count */
	count |= inb_pit(PIT_CH0) << 8;

	/* VIA686a test code... reset the latch if count > max + 1 */
	if (count > LATCH) {
		outb_pit(0x34, PIT_MODE);
		outb_pit(LATCH & 0xff, PIT_CH0);
		outb_pit(LATCH >> 8, PIT_CH0);
		count = LATCH - 1;
	}

	/*
	 * It's possible for count to appear to go the wrong way for a
	 * couple of reasons:
	 *
	 *  1. The timer counter underflows, but we haven't handled the
	 *     resulting interrupt and incremented jiffies yet.
	 *  2. Hardware problem with the timer, not giving us continuous time,
	 *     the counter does small "jumps" upwards on some Pentium systems,
	 *     (see c't 95/10 page 335 for Neptun bug.)
	 *
	 * Previous attempts to handle these cases intelligently were
	 * buggy, so we just do the simple thing now.
	 */
	if (count > old_count && jifs == old_jifs)
		count = old_count;

	old_count = count;
	old_jifs = jifs;

	spin_unlock_irqrestore(&i8253_lock, flags);

	count = (LATCH - 1) - count;

	return (cycle_t)(jifs * LATCH) + count;
}

static struct clocksource pit_cs = {
	.name		= "pit",
	.rating		= 110,
	.read		= pit_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.mult		= 0,
	.shift		= 20,
};

static int __init init_pit_clocksource(void)
{
	 /*
	  * Several reasons not to register PIT as a clocksource:
	  *
	  * - On SMP PIT does not scale due to i8253_lock
	  * - when HPET is enabled
	  * - when local APIC timer is active (PIT is switched off)
	  */
	if (num_possible_cpus() > 1 || is_hpet_enabled() ||
	    pit_ce.mode != CLOCK_EVT_MODE_PERIODIC)
		return 0;

	pit_cs.mult = clocksource_hz2mult(CLOCK_TICK_RATE, pit_cs.shift);

	return clocksource_register(&pit_cs);
}
arch_initcall(init_pit_clocksource);

#endif /* !CONFIG_X86_64 */
