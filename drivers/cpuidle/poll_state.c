/*
 * poll_state.c - Polling idle state
 *
 * This file is released under the GPLv2.
 */

#include <linux/cpuidle.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/sched/idle.h>

#define POLL_IDLE_RELAX_COUNT	200

static int __cpuidle poll_idle(struct cpuidle_device *dev,
			       struct cpuidle_driver *drv, int index)
{
	u64 time_start = local_clock();

	dev->poll_time_limit = false;

	local_irq_enable();
	if (!current_set_polling_and_test()) {
		unsigned int loop_count = 0;
		u64 limit = TICK_USEC;
		int i;

		for (i = 1; i < drv->state_count; i++) {
			if (drv->states[i].disabled || dev->states_usage[i].disable)
				continue;

			limit = (u64)drv->states[i].target_residency * NSEC_PER_USEC;
			break;
		}

		while (!need_resched()) {
			cpu_relax();
			if (loop_count++ < POLL_IDLE_RELAX_COUNT)
				continue;

			loop_count = 0;
			if (local_clock() - time_start > limit) {
				dev->poll_time_limit = true;
				break;
			}
		}
	}
	current_clr_polling();

	return index;
}

void cpuidle_poll_state_init(struct cpuidle_driver *drv)
{
	struct cpuidle_state *state = &drv->states[0];

	snprintf(state->name, CPUIDLE_NAME_LEN, "POLL");
	snprintf(state->desc, CPUIDLE_DESC_LEN, "CPUIDLE CORE POLL IDLE");
	state->exit_latency = 0;
	state->target_residency = 0;
	state->power_usage = -1;
	state->enter = poll_idle;
	state->disabled = false;
	state->flags = CPUIDLE_FLAG_POLLING;
}
EXPORT_SYMBOL_GPL(cpuidle_poll_state_init);
