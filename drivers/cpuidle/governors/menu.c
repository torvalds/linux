/*
 * menu.c - the menu idle governor
 *
 * Copyright (C) 2006-2007 Adam Belay <abelay@novell.com>
 *
 * This code is licenced under the GPL.
 */

#include <linux/kernel.h>
#include <linux/cpuidle.h>
#include <linux/pm_qos_params.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>

#define BREAK_FUZZ	4	/* 4 us */

struct menu_device {
	int		last_state_idx;

	unsigned int	expected_us;
	unsigned int	predicted_us;
	unsigned int	last_measured_us;
	unsigned int	elapsed_us;
};

static DEFINE_PER_CPU(struct menu_device, menu_devices);

/**
 * menu_select - selects the next idle state to enter
 * @dev: the CPU
 */
static int menu_select(struct cpuidle_device *dev)
{
	struct menu_device *data = &__get_cpu_var(menu_devices);
	int i;

	/* determine the expected residency time */
	data->expected_us =
		(u32) ktime_to_ns(tick_nohz_get_sleep_length()) / 1000;

	/* find the deepest idle state that satisfies our constraints */
	for (i = 1; i < dev->state_count; i++) {
		struct cpuidle_state *s = &dev->states[i];

		if (s->target_residency > data->expected_us)
			break;
		if (s->target_residency > data->predicted_us)
			break;
		if (s->exit_latency > pm_qos_requirement(PM_QOS_CPU_DMA_LATENCY))
			break;
	}

	data->last_state_idx = i - 1;
	return i - 1;
}

/**
 * menu_reflect - attempts to guess what happened after entry
 * @dev: the CPU
 *
 * NOTE: it's important to be fast here because this operation will add to
 *       the overall exit latency.
 */
static void menu_reflect(struct cpuidle_device *dev)
{
	struct menu_device *data = &__get_cpu_var(menu_devices);
	int last_idx = data->last_state_idx;
	unsigned int measured_us =
		cpuidle_get_last_residency(dev) + data->elapsed_us;
	struct cpuidle_state *target = &dev->states[last_idx];

	/*
	 * Ugh, this idle state doesn't support residency measurements, so we
	 * are basically lost in the dark.  As a compromise, assume we slept
	 * for one full standard timer tick.  However, be aware that this
	 * could potentially result in a suboptimal state transition.
	 */
	if (!(target->flags & CPUIDLE_FLAG_TIME_VALID))
		measured_us = USEC_PER_SEC / HZ;

	/* Predict time remaining until next break event */
	if (measured_us + BREAK_FUZZ < data->expected_us - target->exit_latency) {
		data->predicted_us = max(measured_us, data->last_measured_us);
		data->last_measured_us = measured_us;
		data->elapsed_us = 0;
	} else {
		if (data->elapsed_us < data->elapsed_us + measured_us)
			data->elapsed_us = measured_us;
		else
			data->elapsed_us = -1;
		data->predicted_us = max(measured_us, data->last_measured_us);
	}
}

/**
 * menu_enable_device - scans a CPU's states and does setup
 * @dev: the CPU
 */
static int menu_enable_device(struct cpuidle_device *dev)
{
	struct menu_device *data = &per_cpu(menu_devices, dev->cpu);

	memset(data, 0, sizeof(struct menu_device));

	return 0;
}

static struct cpuidle_governor menu_governor = {
	.name =		"menu",
	.rating =	20,
	.enable =	menu_enable_device,
	.select =	menu_select,
	.reflect =	menu_reflect,
	.owner =	THIS_MODULE,
};

/**
 * init_menu - initializes the governor
 */
static int __init init_menu(void)
{
	return cpuidle_register_governor(&menu_governor);
}

/**
 * exit_menu - exits the governor
 */
static void __exit exit_menu(void)
{
	cpuidle_unregister_governor(&menu_governor);
}

MODULE_LICENSE("GPL");
module_init(init_menu);
module_exit(exit_menu);
