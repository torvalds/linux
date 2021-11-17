// SPDX-License-Identifier: GPL-2.0-only
/*
 * DT idle states parsing code.
 *
 * Copyright (C) 2014 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 */

#define pr_fmt(fmt) "DT idle-states: " fmt

#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "dt_idle_states.h"

static int init_state_node(struct cpuidle_state *idle_state,
			   const struct of_device_id *match_id,
			   struct device_node *state_node)
{
	int err;
	const char *desc;

	/*
	 * CPUidle drivers are expected to initialize the const void *data
	 * pointer of the passed in struct of_device_id array to the idle
	 * state enter function.
	 */
	idle_state->enter = match_id->data;
	/*
	 * Since this is not a "coupled" state, it's safe to assume interrupts
	 * won't be enabled when it exits allowing the tick to be frozen
	 * safely. So enter() can be also enter_s2idle() callback.
	 */
	idle_state->enter_s2idle = match_id->data;

	err = of_property_read_u32(state_node, "wakeup-latency-us",
				   &idle_state->exit_latency);
	if (err) {
		u32 entry_latency, exit_latency;

		err = of_property_read_u32(state_node, "entry-latency-us",
					   &entry_latency);
		if (err) {
			pr_debug(" * %pOF missing entry-latency-us property\n",
				 state_node);
			return -EINVAL;
		}

		err = of_property_read_u32(state_node, "exit-latency-us",
					   &exit_latency);
		if (err) {
			pr_debug(" * %pOF missing exit-latency-us property\n",
				 state_node);
			return -EINVAL;
		}
		/*
		 * If wakeup-latency-us is missing, default to entry+exit
		 * latencies as defined in idle states bindings
		 */
		idle_state->exit_latency = entry_latency + exit_latency;
	}

	err = of_property_read_u32(state_node, "min-residency-us",
				   &idle_state->target_residency);
	if (err) {
		pr_debug(" * %pOF missing min-residency-us property\n",
			     state_node);
		return -EINVAL;
	}

	err = of_property_read_string(state_node, "idle-state-name", &desc);
	if (err)
		desc = state_node->name;

	idle_state->flags = 0;
	if (of_property_read_bool(state_node, "local-timer-stop"))
		idle_state->flags |= CPUIDLE_FLAG_TIMER_STOP;
	/*
	 * TODO:
	 *	replace with kstrdup and pointer assignment when name
	 *	and desc become string pointers
	 */
	strncpy(idle_state->name, state_node->name, CPUIDLE_NAME_LEN - 1);
	strncpy(idle_state->desc, desc, CPUIDLE_DESC_LEN - 1);
	return 0;
}

/*
 * Check that the idle state is uniform across all CPUs in the CPUidle driver
 * cpumask
 */
static bool idle_state_valid(struct device_node *state_node, unsigned int idx,
			     const cpumask_t *cpumask)
{
	int cpu;
	struct device_node *cpu_node, *curr_state_node;
	bool valid = true;

	/*
	 * Compare idle state phandles for index idx on all CPUs in the
	 * CPUidle driver cpumask. Start from next logical cpu following
	 * cpumask_first(cpumask) since that's the CPU state_node was
	 * retrieved from. If a mismatch is found bail out straight
	 * away since we certainly hit a firmware misconfiguration.
	 */
	for (cpu = cpumask_next(cpumask_first(cpumask), cpumask);
	     cpu < nr_cpu_ids; cpu = cpumask_next(cpu, cpumask)) {
		cpu_node = of_cpu_device_node_get(cpu);
		curr_state_node = of_get_cpu_state_node(cpu_node, idx);
		if (state_node != curr_state_node)
			valid = false;

		of_node_put(curr_state_node);
		of_node_put(cpu_node);
		if (!valid)
			break;
	}

	return valid;
}

/**
 * dt_init_idle_driver() - Parse the DT idle states and initialize the
 *			   idle driver states array
 * @drv:	  Pointer to CPU idle driver to be initialized
 * @matches:	  Array of of_device_id match structures to search in for
 *		  compatible idle state nodes. The data pointer for each valid
 *		  struct of_device_id entry in the matches array must point to
 *		  a function with the following signature, that corresponds to
 *		  the CPUidle state enter function signature:
 *
 *		  int (*)(struct cpuidle_device *dev,
 *			  struct cpuidle_driver *drv,
 *			  int index);
 *
 * @start_idx:    First idle state index to be initialized
 *
 * If DT idle states are detected and are valid the state count and states
 * array entries in the cpuidle driver are initialized accordingly starting
 * from index start_idx.
 *
 * Return: number of valid DT idle states parsed, <0 on failure
 */
int dt_init_idle_driver(struct cpuidle_driver *drv,
			const struct of_device_id *matches,
			unsigned int start_idx)
{
	struct cpuidle_state *idle_state;
	struct device_node *state_node, *cpu_node;
	const struct of_device_id *match_id;
	int i, err = 0;
	const cpumask_t *cpumask;
	unsigned int state_idx = start_idx;

	if (state_idx >= CPUIDLE_STATE_MAX)
		return -EINVAL;
	/*
	 * We get the idle states for the first logical cpu in the
	 * driver mask (or cpu_possible_mask if the driver cpumask is not set)
	 * and we check through idle_state_valid() if they are uniform
	 * across CPUs, otherwise we hit a firmware misconfiguration.
	 */
	cpumask = drv->cpumask ? : cpu_possible_mask;
	cpu_node = of_cpu_device_node_get(cpumask_first(cpumask));

	for (i = 0; ; i++) {
		state_node = of_get_cpu_state_node(cpu_node, i);
		if (!state_node)
			break;

		match_id = of_match_node(matches, state_node);
		if (!match_id) {
			err = -ENODEV;
			break;
		}

		if (!of_device_is_available(state_node)) {
			of_node_put(state_node);
			continue;
		}

		if (!idle_state_valid(state_node, i, cpumask)) {
			pr_warn("%pOF idle state not valid, bailing out\n",
				state_node);
			err = -EINVAL;
			break;
		}

		if (state_idx == CPUIDLE_STATE_MAX) {
			pr_warn("State index reached static CPU idle driver states array size\n");
			break;
		}

		idle_state = &drv->states[state_idx++];
		err = init_state_node(idle_state, match_id, state_node);
		if (err) {
			pr_err("Parsing idle state node %pOF failed with err %d\n",
			       state_node, err);
			err = -EINVAL;
			break;
		}
		of_node_put(state_node);
	}

	of_node_put(state_node);
	of_node_put(cpu_node);
	if (err)
		return err;
	/*
	 * Update the driver state count only if some valid DT idle states
	 * were detected
	 */
	if (i)
		drv->state_count = state_idx;

	/*
	 * Return the number of present and valid DT idle states, which can
	 * also be 0 on platforms with missing DT idle states or legacy DT
	 * configuration predating the DT idle states bindings.
	 */
	return i;
}
EXPORT_SYMBOL_GPL(dt_init_idle_driver);
