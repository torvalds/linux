// SPDX-License-Identifier: GPL-2.0
/*
 * 8253/PIT functions
 *
 */
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/i8253.h>

#include <asm/hypervisor.h>
#include <asm/apic.h>
#include <asm/hpet.h>
#include <asm/time.h>
#include <asm/smp.h>

/*
 * HPET replaces the PIT, when enabled. So we need to know, which of
 * the two timers is used
 */
struct clock_event_device *global_clock_event;

/*
 * Modern chipsets can disable the PIT clock which makes it unusable. It
 * would be possible to enable the clock but the registers are chipset
 * specific and not discoverable. Avoid the whack a mole game.
 *
 * These platforms have discoverable TSC/CPU frequencies but this also
 * requires to know the local APIC timer frequency as it normally is
 * calibrated against the PIT interrupt.
 */
static bool __init use_pit(void)
{
	if (!IS_ENABLED(CONFIG_X86_TSC) || !boot_cpu_has(X86_FEATURE_TSC))
		return true;

	/* This also returns true when APIC is disabled */
	return apic_needs_pit();
}

bool __init pit_timer_init(void)
{
	if (!use_pit()) {
		/*
		 * Don't just ignore the PIT. Ensure it's stopped, because
		 * VMMs otherwise steal CPU time just to pointlessly waggle
		 * the (masked) IRQ.
		 */
		clockevent_i8253_disable();
		return false;
	}
	clockevent_i8253_init(true);
	global_clock_event = &i8253_clockevent;
	return true;
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
	    !clockevent_state_periodic(&i8253_clockevent))
		return 0;

	return clocksource_i8253_init();
}
arch_initcall(init_pit_clocksource);
#endif /* !CONFIG_X86_64 */
