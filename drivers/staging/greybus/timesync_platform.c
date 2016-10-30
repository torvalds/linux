/*
 * TimeSync API driver.
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 *
 * This code reads directly from an ARMv7 memory-mapped timer that lives in
 * MMIO space. Since this counter lives inside of MMIO space its shared between
 * cores and that means we don't have to worry about issues like TSC on x86
 * where each time-stamp-counter (TSC) is local to a particular core.
 *
 * Register-level access code is based on
 * drivers/clocksource/arm_arch_timer.c
 */
#include <linux/cpufreq.h>
#include <linux/of_platform.h>

#include "greybus.h"
#include "arche_platform.h"

#define DEFAULT_FRAMETIME_CLOCK_HZ 19200000

static u32 gb_timesync_clock_frequency;
int (*arche_platform_change_state_cb)(enum arche_platform_state state,
				      struct gb_timesync_svc *pdata);
EXPORT_SYMBOL_GPL(arche_platform_change_state_cb);

u64 gb_timesync_platform_get_counter(void)
{
	return (u64)get_cycles();
}

u32 gb_timesync_platform_get_clock_rate(void)
{
	if (unlikely(!gb_timesync_clock_frequency)) {
		gb_timesync_clock_frequency = cpufreq_get(0);
		if (!gb_timesync_clock_frequency)
			gb_timesync_clock_frequency = DEFAULT_FRAMETIME_CLOCK_HZ;
	}

	return gb_timesync_clock_frequency;
}

int gb_timesync_platform_lock_bus(struct gb_timesync_svc *pdata)
{
	return arche_platform_change_state_cb(ARCHE_PLATFORM_STATE_TIME_SYNC,
					      pdata);
}

void gb_timesync_platform_unlock_bus(void)
{
	arche_platform_change_state_cb(ARCHE_PLATFORM_STATE_ACTIVE, NULL);
}

static const struct of_device_id arch_timer_of_match[] = {
	{ .compatible   = "google,greybus-frame-time-counter", },
	{},
};

int __init gb_timesync_platform_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, arch_timer_of_match);
	if (!np) {
		/* Tolerate not finding to allow BBB etc to continue */
		pr_warn("Unable to find a compatible ARMv7 timer\n");
		return 0;
	}

	if (of_property_read_u32(np, "clock-frequency",
				 &gb_timesync_clock_frequency)) {
		pr_err("Unable to find timer clock-frequency\n");
		return -ENODEV;
	}

	return 0;
}

void gb_timesync_platform_exit(void) {}
