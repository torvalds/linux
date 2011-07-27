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

DEFINE_RAW_SPINLOCK(i8253_lock);
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
	raw_spin_lock(&i8253_lock);

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
	raw_spin_unlock(&i8253_lock);
}

/*
 * Program the next event in oneshot mode
 *
 * Delta is given in PIT ticks
 */
static int pit_next_event(unsigned long delta, struct clock_event_device *evt)
{
	raw_spin_lock(&i8253_lock);
	outb_pit(delta & 0xff , PIT_CH0);	/* LSB */
	outb_pit(delta >> 8 , PIT_CH0);		/* MSB */
	raw_spin_unlock(&i8253_lock);

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

	clockevents_config_and_register(&pit_ce, CLOCK_TICK_RATE, 0xF, 0x7FFF);
	global_clock_event = &pit_ce;
}

#ifndef CONFIG_X86_64
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

	return clocksource_i8253_init();
}
arch_initcall(init_pit_clocksource);
#endif /* !CONFIG_X86_64 */
