/*
 * poll_state.c - Polling idle state
 *
 * This file is released under the GPLv2.
 */

#include <linux/cpuidle.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/sched/idle.h>

#define POLL_IDLE_TIME_LIMIT	(TICK_NSEC / 16)

static int __cpuidle poll_idle(struct cpuidle_device *dev,
			       struct cpuidle_driver *drv, int index)
{
	u64 time_start = local_clock();

	local_irq_enable();
	if (!current_set_polling_and_test()) {
		while (!need_resched()) {
			cpu_relax();

			if (local_clock() - time_start > POLL_IDLE_TIME_LIMIT)
				break;
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
