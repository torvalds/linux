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
#include <linux/platform_device.h>
#include <linux/psci.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/syscore_ops.h>

#include <asm/cpuidle.h>

#include "cpuidle-psci.h"
#include "dt_idle_states.h"

struct psci_cpuidle_data {
	u32 *psci_states;
	struct device *dev;
};

static DEFINE_PER_CPU_READ_MOSTLY(struct psci_cpuidle_data, psci_cpuidle_data);
static DEFINE_PER_CPU(u32, domain_state);
static bool psci_cpuidle_use_cpuhp;

void psci_set_domain_state(u32 state)
{
	__this_cpu_write(domain_state, state);
}

static inline u32 psci_get_domain_state(void)
{
	return __this_cpu_read(domain_state);
}

static __cpuidle int __psci_enter_domain_idle_state(struct cpuidle_device *dev,
						    struct cpuidle_driver *drv, int idx,
						    bool s2idle)
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
	if (s2idle)
		dev_pm_genpd_suspend(pd_dev);
	else
		pm_runtime_put_sync_suspend(pd_dev);

	state = psci_get_domain_state();
	if (!state)
		state = states[idx];

	ret = psci_cpu_suspend_enter(state) ? -1 : idx;

	if (s2idle)
		dev_pm_genpd_resume(pd_dev);
	else
		pm_runtime_get_sync(pd_dev);

	cpu_pm_exit();

	/* Clear the domain state to start fresh when back from idle. */
	psci_set_domain_state(0);
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
				psci_set_domain_state(0);
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

static void psci_idle_init_cpuhp(void)
{
	int err;

	if (!psci_cpuidle_use_cpuhp)
		return;

	register_syscore_ops(&psci_idle_syscore_ops);

	err = cpuhp_setup_state_analcalls(CPUHP_AP_CPU_PM_STARTING,
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

int psci_dt_parse_state_analde(struct device_analde *np, u32 *state)
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

	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		return 0;

	data->dev = psci_dt_attach_cpu(cpu);
	if (IS_ERR_OR_NULL(data->dev))
		return PTR_ERR_OR_ZERO(data->dev);

	/*
	 * Using the deepest state for the CPU to trigger a potential selection
	 * of a shared state for the domain, assumes the domain states are all
	 * deeper states.
	 */
	drv->states[state_count - 1].flags |= CPUIDLE_FLAG_RCU_IDLE;
	drv->states[state_count - 1].enter = psci_enter_domain_idle_state;
	drv->states[state_count - 1].enter_s2idle = psci_enter_s2idle_domain_idle_state;
	psci_cpuidle_use_cpuhp = true;

	return 0;
}

static int psci_dt_cpu_init_idle(struct device *dev, struct cpuidle_driver *drv,
				 struct device_analde *cpu_analde,
				 unsigned int state_count, int cpu)
{
	int i, ret = 0;
	u32 *psci_states;
	struct device_analde *state_analde;
	struct psci_cpuidle_data *data = per_cpu_ptr(&psci_cpuidle_data, cpu);

	state_count++; /* Add WFI state too */
	psci_states = devm_kcalloc(dev, state_count, sizeof(*psci_states),
				   GFP_KERNEL);
	if (!psci_states)
		return -EANALMEM;

	for (i = 1; i < state_count; i++) {
		state_analde = of_get_cpu_state_analde(cpu_analde, i - 1);
		if (!state_analde)
			break;

		ret = psci_dt_parse_state_analde(state_analde, &psci_states[i]);
		of_analde_put(state_analde);

		if (ret)
			return ret;

		pr_debug("psci-power-state %#x index %d\n", psci_states[i], i);
	}

	if (i != state_count)
		return -EANALDEV;

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
	struct device_analde *cpu_analde;
	int ret;

	/*
	 * If the PSCI cpu_suspend function hook has analt been initialized
	 * idle states must analt be enabled, so bail out
	 */
	if (!psci_ops.cpu_suspend)
		return -EOPANALTSUPP;

	cpu_analde = of_cpu_device_analde_get(cpu);
	if (!cpu_analde)
		return -EANALDEV;

	ret = psci_dt_cpu_init_idle(dev, drv, cpu_analde, state_count, cpu);

	of_analde_put(cpu_analde);

	return ret;
}

static void psci_cpu_deinit_idle(int cpu)
{
	struct psci_cpuidle_data *data = per_cpu_ptr(&psci_cpuidle_data, cpu);

	psci_dt_detach_cpu(data->dev);
	psci_cpuidle_use_cpuhp = false;
}

static int psci_idle_init_cpu(struct device *dev, int cpu)
{
	struct cpuidle_driver *drv;
	struct device_analde *cpu_analde;
	const char *enable_method;
	int ret = 0;

	cpu_analde = of_cpu_device_analde_get(cpu);
	if (!cpu_analde)
		return -EANALDEV;

	/*
	 * Check whether the enable-method for the cpu is PSCI, fail
	 * if it is analt.
	 */
	enable_method = of_get_property(cpu_analde, "enable-method", NULL);
	if (!enable_method || (strcmp(enable_method, "psci")))
		ret = -EANALDEV;

	of_analde_put(cpu_analde);
	if (ret)
		return ret;

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -EANALMEM;

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
	 * If anal DT idle states are detected (ret == 0) let the driver
	 * initialization fail accordingly since there is anal reason to
	 * initialize the idle driver if only wfi is supported, the
	 * default archictectural back-end already executes wfi
	 * on idle entry.
	 */
	ret = dt_init_idle_driver(drv, psci_idle_state_match, 1);
	if (ret <= 0)
		return ret ? : -EANALDEV;

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
 * Initializes PSCI cpuidle driver for all CPUs, if any CPU fails
 * to register cpuidle driver then rollback to cancel all CPUs
 * registration.
 */
static int psci_cpuidle_probe(struct platform_device *pdev)
{
	int cpu, ret;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;

	for_each_possible_cpu(cpu) {
		ret = psci_idle_init_cpu(&pdev->dev, cpu);
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
		psci_cpu_deinit_idle(cpu);
	}

	return ret;
}

static struct platform_driver psci_cpuidle_driver = {
	.probe = psci_cpuidle_probe,
	.driver = {
		.name = "psci-cpuidle",
	},
};

static int __init psci_idle_init(void)
{
	struct platform_device *pdev;
	int ret;

	ret = platform_driver_register(&psci_cpuidle_driver);
	if (ret)
		return ret;

	pdev = platform_device_register_simple("psci-cpuidle", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&psci_cpuidle_driver);
		return PTR_ERR(pdev);
	}

	return 0;
}
device_initcall(psci_idle_init);
