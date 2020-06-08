// SPDX-License-Identifier: GPL-2.0-only
/*
 * PSCI CPU idle driver.
 *
 * Copyright (C) 2019 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 */

#define pr_fmt(fmt) "CPUidle PSCI: " fmt

#include <linux/cpuhotplug.h>
#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/psci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <asm/cpuidle.h>

#include "cpuidle-psci.h"
#include "dt_idle_states.h"

struct psci_cpuidle_data {
	u32 *psci_states;
	struct device *dev;
};

static DEFINE_PER_CPU_READ_MOSTLY(struct psci_cpuidle_data, psci_cpuidle_data);
static DEFINE_PER_CPU(u32, domain_state);
static bool psci_cpuidle_use_cpuhp __initdata;

void psci_set_domain_state(u32 state)
{
	__this_cpu_write(domain_state, state);
}

static inline u32 psci_get_domain_state(void)
{
	return __this_cpu_read(domain_state);
}

static inline int psci_enter_state(int idx, u32 state)
{
	return CPU_PM_CPU_IDLE_ENTER_PARAM(psci_cpu_suspend_enter, idx, state);
}

static int psci_enter_domain_idle_state(struct cpuidle_device *dev,
					struct cpuidle_driver *drv, int idx)
{
	struct psci_cpuidle_data *data = this_cpu_ptr(&psci_cpuidle_data);
	u32 *states = data->psci_states;
	struct device *pd_dev = data->dev;
	u32 state;
	int ret;

	ret = cpu_pm_enter();
	if (ret)
		return -1;

	/* Do runtime PM to manage a hierarchical CPU toplogy. */
	pm_runtime_put_sync_suspend(pd_dev);

	state = psci_get_domain_state();
	if (!state)
		state = states[idx];

	ret = psci_cpu_suspend_enter(state) ? -1 : idx;

	pm_runtime_get_sync(pd_dev);

	cpu_pm_exit();

	/* Clear the domain state to start fresh when back from idle. */
	psci_set_domain_state(0);
	return ret;
}

static int psci_idle_cpuhp_up(unsigned int cpu)
{
	struct device *pd_dev = __this_cpu_read(psci_cpuidle_data.dev);

	if (pd_dev)
		pm_runtime_get_sync(pd_dev);

	return 0;
}

static int psci_idle_cpuhp_down(unsigned int cpu)
{
	struct device *pd_dev = __this_cpu_read(psci_cpuidle_data.dev);

	if (pd_dev) {
		pm_runtime_put_sync(pd_dev);
		/* Clear domain state to start fresh at next online. */
		psci_set_domain_state(0);
	}

	return 0;
}

static void __init psci_idle_init_cpuhp(void)
{
	int err;

	if (!psci_cpuidle_use_cpuhp)
		return;

	err = cpuhp_setup_state_nocalls(CPUHP_AP_CPU_PM_STARTING,
					"cpuidle/psci:online",
					psci_idle_cpuhp_up,
					psci_idle_cpuhp_down);
	if (err)
		pr_warn("Failed %d while setup cpuhp state\n", err);
}

static int psci_enter_idle_state(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx)
{
	u32 *state = __this_cpu_read(psci_cpuidle_data.psci_states);

	return psci_enter_state(idx, state[idx]);
}

static struct cpuidle_driver psci_idle_driver __initdata = {
	.name = "psci_idle",
	.owner = THIS_MODULE,
	/*
	 * PSCI idle states relies on architectural WFI to
	 * be represented as state index 0.
	 */
	.states[0] = {
		.enter                  = psci_enter_idle_state,
		.exit_latency           = 1,
		.target_residency       = 1,
		.power_usage		= UINT_MAX,
		.name                   = "WFI",
		.desc                   = "ARM WFI",
	}
};

static const struct of_device_id psci_idle_state_match[] __initconst = {
	{ .compatible = "arm,idle-state",
	  .data = psci_enter_idle_state },
	{ },
};

int __init psci_dt_parse_state_node(struct device_node *np, u32 *state)
{
	int err = of_property_read_u32(np, "arm,psci-suspend-param", state);

	if (err) {
		pr_warn("%pOF missing arm,psci-suspend-param property\n", np);
		return err;
	}

	if (!psci_power_state_is_valid(*state)) {
		pr_warn("Invalid PSCI power state %#x\n", *state);
		return -EINVAL;
	}

	return 0;
}

static int __init psci_dt_cpu_init_topology(struct cpuidle_driver *drv,
					    struct psci_cpuidle_data *data,
					    unsigned int state_count, int cpu)
{
	/* Currently limit the hierarchical topology to be used in OSI mode. */
	if (!psci_has_osi_support())
		return 0;

	data->dev = psci_dt_attach_cpu(cpu);
	if (IS_ERR_OR_NULL(data->dev))
		return PTR_ERR_OR_ZERO(data->dev);

	/*
	 * Using the deepest state for the CPU to trigger a potential selection
	 * of a shared state for the domain, assumes the domain states are all
	 * deeper states.
	 */
	drv->states[state_count - 1].enter = psci_enter_domain_idle_state;
	psci_cpuidle_use_cpuhp = true;

	return 0;
}

static int __init psci_dt_cpu_init_idle(struct cpuidle_driver *drv,
					struct device_node *cpu_node,
					unsigned int state_count, int cpu)
{
	int i, ret = 0;
	u32 *psci_states;
	struct device_node *state_node;
	struct psci_cpuidle_data *data = per_cpu_ptr(&psci_cpuidle_data, cpu);

	state_count++; /* Add WFI state too */
	psci_states = kcalloc(state_count, sizeof(*psci_states), GFP_KERNEL);
	if (!psci_states)
		return -ENOMEM;

	for (i = 1; i < state_count; i++) {
		state_node = of_get_cpu_state_node(cpu_node, i - 1);
		if (!state_node)
			break;

		ret = psci_dt_parse_state_node(state_node, &psci_states[i]);
		of_node_put(state_node);

		if (ret)
			goto free_mem;

		pr_debug("psci-power-state %#x index %d\n", psci_states[i], i);
	}

	if (i != state_count) {
		ret = -ENODEV;
		goto free_mem;
	}

	/* Initialize optional data, used for the hierarchical topology. */
	ret = psci_dt_cpu_init_topology(drv, data, state_count, cpu);
	if (ret < 0)
		goto free_mem;

	/* Idle states parsed correctly, store them in the per-cpu struct. */
	data->psci_states = psci_states;
	return 0;

free_mem:
	kfree(psci_states);
	return ret;
}

static __init int psci_cpu_init_idle(struct cpuidle_driver *drv,
				     unsigned int cpu, unsigned int state_count)
{
	struct device_node *cpu_node;
	int ret;

	/*
	 * If the PSCI cpu_suspend function hook has not been initialized
	 * idle states must not be enabled, so bail out
	 */
	if (!psci_ops.cpu_suspend)
		return -EOPNOTSUPP;

	cpu_node = of_cpu_device_node_get(cpu);
	if (!cpu_node)
		return -ENODEV;

	ret = psci_dt_cpu_init_idle(drv, cpu_node, state_count, cpu);

	of_node_put(cpu_node);

	return ret;
}

static int __init psci_idle_init_cpu(int cpu)
{
	struct cpuidle_driver *drv;
	struct device_node *cpu_node;
	const char *enable_method;
	int ret = 0;

	cpu_node = of_cpu_device_node_get(cpu);
	if (!cpu_node)
		return -ENODEV;

	/*
	 * Check whether the enable-method for the cpu is PSCI, fail
	 * if it is not.
	 */
	enable_method = of_get_property(cpu_node, "enable-method", NULL);
	if (!enable_method || (strcmp(enable_method, "psci")))
		ret = -ENODEV;

	of_node_put(cpu_node);
	if (ret)
		return ret;

	drv = kmemdup(&psci_idle_driver, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->cpumask = (struct cpumask *)cpumask_of(cpu);

	/*
	 * Initialize idle states data, starting at index 1, since
	 * by default idle state 0 is the quiescent state reached
	 * by the cpu by executing the wfi instruction.
	 *
	 * If no DT idle states are detected (ret == 0) let the driver
	 * initialization fail accordingly since there is no reason to
	 * initialize the idle driver if only wfi is supported, the
	 * default archictectural back-end already executes wfi
	 * on idle entry.
	 */
	ret = dt_init_idle_driver(drv, psci_idle_state_match, 1);
	if (ret <= 0) {
		ret = ret ? : -ENODEV;
		goto out_kfree_drv;
	}

	/*
	 * Initialize PSCI idle states.
	 */
	ret = psci_cpu_init_idle(drv, cpu, ret);
	if (ret) {
		pr_err("CPU %d failed to PSCI idle\n", cpu);
		goto out_kfree_drv;
	}

	ret = cpuidle_register(drv, NULL);
	if (ret)
		goto out_kfree_drv;

	return 0;

out_kfree_drv:
	kfree(drv);
	return ret;
}

/*
 * psci_idle_init - Initializes PSCI cpuidle driver
 *
 * Initializes PSCI cpuidle driver for all CPUs, if any CPU fails
 * to register cpuidle driver then rollback to cancel all CPUs
 * registration.
 */
static int __init psci_idle_init(void)
{
	int cpu, ret;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;

	for_each_possible_cpu(cpu) {
		ret = psci_idle_init_cpu(cpu);
		if (ret)
			goto out_fail;
	}

	psci_idle_init_cpuhp();
	return 0;

out_fail:
	while (--cpu >= 0) {
		dev = per_cpu(cpuidle_devices, cpu);
		drv = cpuidle_get_cpu_driver(dev);
		cpuidle_unregister(drv);
		kfree(drv);
	}

	return ret;
}
device_initcall(psci_idle_init);
