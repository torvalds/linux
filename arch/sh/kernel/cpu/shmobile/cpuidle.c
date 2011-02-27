/*
 * arch/sh/kernel/cpu/shmobile/cpuidle.c
 *
 * Cpuidle support code for SuperH Mobile
 *
 *  Copyright (C) 2009 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/cpuidle.h>
#include <asm/suspend.h>
#include <asm/uaccess.h>
#include <asm/hwblk.h>

static unsigned long cpuidle_mode[] = {
	SUSP_SH_SLEEP, /* regular sleep mode */
	SUSP_SH_SLEEP | SUSP_SH_SF, /* sleep mode + self refresh */
	SUSP_SH_STANDBY | SUSP_SH_SF, /* software standby mode + self refresh */
};

static int cpuidle_sleep_enter(struct cpuidle_device *dev,
			       struct cpuidle_state *state)
{
	unsigned long allowed_mode = arch_hwblk_sleep_mode();
	ktime_t before, after;
	int requested_state = state - &dev->states[0];
	int allowed_state;
	int k;

	/* convert allowed mode to allowed state */
	for (k = ARRAY_SIZE(cpuidle_mode) - 1; k > 0; k--)
		if (cpuidle_mode[k] == allowed_mode)
			break;

	allowed_state = k;

	/* take the following into account for sleep mode selection:
	 * - allowed_state: best mode allowed by hardware (clock deps)
	 * - requested_state: best mode allowed by software (latencies)
	 */
	k = min_t(int, allowed_state, requested_state);

	dev->last_state = &dev->states[k];
	before = ktime_get();
	sh_mobile_call_standby(cpuidle_mode[k]);
	after = ktime_get();
	return ktime_to_ns(ktime_sub(after, before)) >> 10;
}

static struct cpuidle_device cpuidle_dev;
static struct cpuidle_driver cpuidle_driver = {
	.name =		"sh_idle",
	.owner =	THIS_MODULE,
};

void sh_mobile_setup_cpuidle(void)
{
	struct cpuidle_device *dev = &cpuidle_dev;
	struct cpuidle_state *state;
	int i;

	cpuidle_register_driver(&cpuidle_driver);

	for (i = 0; i < CPUIDLE_STATE_MAX; i++) {
		dev->states[i].name[0] = '\0';
		dev->states[i].desc[0] = '\0';
	}

	i = CPUIDLE_DRIVER_STATE_START;

	state = &dev->states[i++];
	snprintf(state->name, CPUIDLE_NAME_LEN, "C1");
	strncpy(state->desc, "SuperH Sleep Mode", CPUIDLE_DESC_LEN);
	state->exit_latency = 1;
	state->target_residency = 1 * 2;
	state->power_usage = 3;
	state->flags = 0;
	state->flags |= CPUIDLE_FLAG_TIME_VALID;
	state->enter = cpuidle_sleep_enter;

	dev->safe_state = state;

	if (sh_mobile_sleep_supported & SUSP_SH_SF) {
		state = &dev->states[i++];
		snprintf(state->name, CPUIDLE_NAME_LEN, "C2");
		strncpy(state->desc, "SuperH Sleep Mode [SF]",
			CPUIDLE_DESC_LEN);
		state->exit_latency = 100;
		state->target_residency = 1 * 2;
		state->power_usage = 1;
		state->flags = 0;
		state->flags |= CPUIDLE_FLAG_TIME_VALID;
		state->enter = cpuidle_sleep_enter;
	}

	if (sh_mobile_sleep_supported & SUSP_SH_STANDBY) {
		state = &dev->states[i++];
		snprintf(state->name, CPUIDLE_NAME_LEN, "C3");
		strncpy(state->desc, "SuperH Mobile Standby Mode [SF]",
			CPUIDLE_DESC_LEN);
		state->exit_latency = 2300;
		state->target_residency = 1 * 2;
		state->power_usage = 1;
		state->flags = 0;
		state->flags |= CPUIDLE_FLAG_TIME_VALID;
		state->enter = cpuidle_sleep_enter;
	}

	dev->state_count = i;

	cpuidle_register_device(dev);
}
