// SPDX-License-Identifier: GPL-2.0-only
/*
 * PSCI CPU idle driver.
 *
 * Copyright (C) 2019 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 */

#define pr_fmt(fmt) "CPUidle PSCI: " fmt

#include <linux/cpuhotplug.h>
#include <linux/cpu_cooling.h>
#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/device/faux.h>
#include <linux/psci.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/syscore_ops.h>

#include <asm/cpuidle.h>
#include <trace/events/power.h>

#include "cpuidle-psci.h"
#include "dt_idle_states.h"
#include "dt_idle_genpd.h"

struct psci_cpuidle_data {
	u32 *psci_states;
	struct device *dev;
};

struct psci_cpuidle_domain_state {
	struct generic_pm_domain *pd;
	unsigned int state_idx;
	u32 state;
};

static DEFINE_PER_CPU_READ_MOSTLY(struct psci_cpuidle_data, psci_cpuidle_data);
static DEFINE_PER_CPU(struct psci_cpuidle_domain_state, psci_domain_state);
static bool psci_cpuidle_use_syscore;

void psci_set_domain_state(struct generic_pm_domain *pd, unsigned int state_idx,
			   u32 state)
{
	struct psci_cpuidle_domain_state *ds = this_cpu_ptr(&psci_domain_state);

	ds->pd = pd;
	ds->state_idx = state_idx;
	ds->state = state;
}

static inline void psci_clear_domain_state(void)
{
	__this_cpu_write(psci_domain_state.state, 0);
}

static __cpuidle int __psci_enter_domain_idle_state(struct cpuidle_device *dev,
						    struct cpuidle_driver *drv, int idx,
						    bool s2idle)
{
	struct psci_cpuidle_data *data = this_cpu_ptr(&psci_cpuidle_data);
	u32 *states = data->psci_states;
	struct device *pd_dev = data->dev;
	struct psci_cpuidle_domain_state *ds;
	u32 state = states[idx];
	int ret;

	ret = cpu_pm_enter();
	if (ret)
		return -1;

	/* Do runtime PM to manage a hierarchical CPU toplogy. */
	if (s2idle)
		dev_pm_genpd_suspend(pd_dev);
	else
		pm_runtime_put_sync_suspend(pd_dev);

	ds = this_cpu_ptr(&psci_domain_state);
	if (ds->state)
		state = ds->state;

	trace_psci_domain_idle_enter(dev->cpu, state, s2idle);
	ret = psci_cpu_suspend_enter(state) ? -1 : idx;
	trace_psci_domain_idle_exit(dev->cpu, state, s2idle);

	if (s2idle)
		dev_pm_genpd_resume(pd_dev);
	else
		pm_runtime_get_sync(pd_dev);

	cpu_pm_exit();

	/* Correct domain-idlestate statistics if we failed to enter. */
	if (ret == -1 && ds->state)
		pm_genpd_inc_rejected(ds->pd, ds->state_idx);

	/* Clear the domain state to start fresh when back from idle. */
	psci_clear_domain_state();
	return ret;
}

static int psci_enter_domain_idle_state(struct cpuidle_device *dev,
					struct cpuidle_driver *drv, int idx)
{
	return __psci_enter_domain_idle_state(dev, drv, idx, false);
}

static int psci_enter_s2idle_domain_idle_state(struct cpuidle_device *dev,
					       struct cpuidle_driver *drv,
					       int idx)
{
	return __psci_enter_domain_idle_state(dev, drv, idx, true);
}

static int psci_idle_cpuhp_up(unsigned int cpu)
{
	struct device *pd_dev = __this_cpu_read(psci_cpuidle_data.dev);

	if (pd_dev) {
		if (!IS_ENABLED(CONFIG_PREEMPT_RT))
			pm_runtime_get_sync(pd_dev);
		else
			dev_pm_genpd_resume(pd_dev);
	}

	return 0;
}

static int psci_idle_cpuhp_down(unsigned int cpu)
{
	struct device *pd_dev = __this_cpu_read(psci_cpuidle_data.dev);

	if (pd_dev) {
		if (!IS_ENABLED(CONFIG_PREEMPT_RT))
			pm_runtime_put_sync(pd_dev);
		else
			dev_pm_genpd_suspend(pd_dev);

		/* Clear domain state to start fresh at next online. */
		psci_clear_domain_state();
	}

	return 0;
}

static void psci_idle_syscore_switch(bool suspend)
{
	bool cleared = false;
	struct device *dev;
	int cpu;

	for_each_possible_cpu(cpu) {
		dev = per_cpu_ptr(&psci_cpuidle_data, cpu)->dev;

		if (dev && suspend) {
			dev_pm_genpd_suspend(dev);
		} else if (dev) {
			dev_pm_genpd_resume(dev);

			/* Account for userspace having offlined a CPU. */
			if (pm_runtime_status_suspended(dev))
				pm_runtime_set_active(dev);

			/* Clear domain state to re-start fresh. */
			if (!cleared) {
				psci_clear_domain_state();
				cleared = true;
			}
		}
	}
}

static int psci_idle_syscore_suspend(void)
{
	psci_idle_syscore_switch(true);
	return 0;
}

static void psci_idle_syscore_resume(void)
{
	psci_idle_syscore_switch(false);
}

static struct syscore_ops psci_idle_syscore_ops = {
	.suspend = psci_idle_syscore_suspend,
	.resume = psci_idle_syscore_resume,
};

static void psci_idle_init_syscore(void)
{
	if (psci_cpuidle_use_syscore)
		register_syscore_ops(&psci_idle_syscore_ops);
}

static void psci_idle_init_cpuhp(void)
{
	int err;

	err = cpuhp_setup_state_nocalls(CPUHP_AP_CPU_PM_STARTING,
					"cpuidle/psci:online",
					psci_idle_cpuhp_up,
					psci_idle_cpuhp_down);
	if (err)
		pr_warn("Failed %d while setup cpuhp state\n", err);
}

static __cpuidle int psci_enter_idle_state(struct cpuidle_device *dev,
					   struct cpuidle_driver *drv, int idx)
{
	u32 *state = __this_cpu_read(psci_cpuidle_data.psci_states);

	return CPU_PM_CPU_IDLE_ENTER_PARAM_RCU(psci_cpu_suspend_enter, idx, state[idx]);
}

static const struct of_device_id psci_idle_state_match[] = {
	{ .compatible = "arm,idle-state",
	  .data = psci_enter_idle_state },
	{ },
};

int psci_dt_parse_state_node(struct device_node *np, u32 *state)
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

static int psci_dt_cpu_init_topology(struct cpuidle_driver *drv,
				     struct psci_cpuidle_data *data,
				     unsigned int state_count, int cpu)
{
	/* Currently limit the hierarchical topology to be used in OSI mode. */
	if (!psci_has_osi_support())
		return 0;

	data->dev = dt_idle_attach_cpu(cpu, "psci");
	if (IS_ERR_OR_NULL(data->dev))
		return PTR_ERR_OR_ZERO(data->dev);

	psci_cpuidle_use_syscore = true;

	/*
	 * Using the deepest state for the CPU to trigger a potential selection
	 * of a shared state for the domain, assumes the domain states are all
	 * deeper states. On PREEMPT_RT the hierarchical topology is limited to
	 * s2ram and s2idle.
	 */
	drv->states[state_count - 1].enter_s2idle = psci_enter_s2idle_domain_idle_state;
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		drv->states[state_count - 1].enter = psci_enter_domain_idle_state;

	return 0;
}

static int psci_dt_cpu_init_idle(struct device *dev, struct cpuidle_driver *drv,
				 struct device_node *cpu_node,
				 unsigned int state_count, int cpu)
{
	int i, ret = 0;
	u32 *psci_states;
	struct device_node *state_node;
	struct psci_cpuidle_data *data = per_cpu_ptr(&psci_cpuidle_data, cpu);

	state_count++; /* Add WFI state too */
	psci_states = devm_kcalloc(dev, state_count, sizeof(*psci_states),
				   GFP_KERNEL);
	if (!psci_states)
		return -ENOMEM;

	for (i = 1; i < state_count; i++) {
		state_node = of_get_cpu_state_node(cpu_node, i - 1);
		if (!state_node)
			break;

		ret = psci_dt_parse_state_node(state_node, &psci_states[i]);
		of_node_put(state_node);

		if (ret)
			return ret;

		pr_debug("psci-power-state %#x index %d\n", psci_states[i], i);
	}

	if (i != state_count)
		return -ENODEV;

	/* Initialize optional data, used for the hierarchical topology. */
	ret = psci_dt_cpu_init_topology(drv, data, state_count, cpu);
	if (ret < 0)
		return ret;

	/* Idle states parsed correctly, store them in the per-cpu struct. */
	data->psci_states = psci_states;
	return 0;
}

static int psci_cpu_init_idle(struct device *dev, struct cpuidle_driver *drv,
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

	ret = psci_dt_cpu_init_idle(dev, drv, cpu_node, state_count, cpu);

	of_node_put(cpu_node);

	return ret;
}

static void psci_cpu_deinit_idle(int cpu)
{
	struct psci_cpuidle_data *data = per_cpu_ptr(&psci_cpuidle_data, cpu);

	dt_idle_detach_cpu(data->dev);
	psci_cpuidle_use_syscore = false;
}

static int psci_idle_init_cpu(struct device *dev, int cpu)
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

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->name = "psci_idle";
	drv->owner = THIS_MODULE;
	drv->cpumask = (struct cpumask *)cpumask_of(cpu);

	/*
	 * PSCI idle states relies on architectural WFI to be represented as
	 * state index 0.
	 */
	drv->states[0].enter = psci_enter_idle_state;
	drv->states[0].exit_latency = 1;
	drv->states[0].target_residency = 1;
	drv->states[0].power_usage = UINT_MAX;
	strcpy(drv->states[0].name, "WFI");
	strcpy(drv->states[0].desc, "ARM WFI");

	/*
	 * If no DT idle states are detected (ret == 0) let the driver
	 * initialization fail accordingly since there is no reason to
	 * initialize the idle driver if only wfi is supported, the
	 * default archictectural back-end already executes wfi
	 * on idle entry.
	 */
	ret = dt_init_idle_driver(drv, psci_idle_state_match, 1);
	if (ret <= 0)
		return ret ? : -ENODEV;

	/*
	 * Initialize PSCI idle states.
	 */
	ret = psci_cpu_init_idle(dev, drv, cpu, ret);
	if (ret) {
		pr_err("CPU %d failed to PSCI idle\n", cpu);
		return ret;
	}

	ret = cpuidle_register(drv, NULL);
	if (ret)
		goto deinit;

	cpuidle_cooling_register(drv);

	return 0;
deinit:
	psci_cpu_deinit_idle(cpu);
	return ret;
}

/*
 * psci_idle_probe - Initializes PSCI cpuidle driver
 *
 * Initializes PSCI cpuidle driver for all present CPUs, if any CPU fails
 * to register cpuidle driver then rollback to cancel all CPUs
 * registration.
 */
static int psci_cpuidle_probe(struct faux_device *fdev)
{
	int cpu, ret;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;

	for_each_present_cpu(cpu) {
		ret = psci_idle_init_cpu(&fdev->dev, cpu);
		if (ret)
			goto out_fail;
	}

	psci_idle_init_syscore();
	psci_idle_init_cpuhp();
	return 0;

out_fail:
	while (--cpu >= 0) {
		dev = per_cpu(cpuidle_devices, cpu);
		drv = cpuidle_get_cpu_driver(dev);
		cpuidle_unregister(drv);
		psci_cpu_deinit_idle(cpu);
	}

	return ret;
}

static struct faux_device_ops psci_cpuidle_ops = {
	.probe = psci_cpuidle_probe,
};

static bool __init dt_idle_state_present(void)
{
	struct device_node *cpu_node __free(device_node) =
			of_cpu_device_node_get(cpumask_first(cpu_possible_mask));
	if (!cpu_node)
		return false;

	struct device_node *state_node __free(device_node) =
			of_get_cpu_state_node(cpu_node, 0);
	if (!state_node)
		return false;

	return !!of_match_node(psci_idle_state_match, state_node);
}

static int __init psci_idle_init(void)
{
	struct faux_device *fdev;

	if (!dt_idle_state_present())
		return 0;

	fdev = faux_device_create("psci-cpuidle", NULL, &psci_cpuidle_ops);
	if (!fdev) {
		pr_err("Failed to create psci-cpuidle device\n");
		return -ENODEV;
	}

	return 0;
}
device_initcall(psci_idle_init);
