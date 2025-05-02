// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V SBI CPU idle driver.
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "cpuidle-riscv-sbi: " fmt

#include <linux/cleanup.h>
#include <linux/cpuhotplug.h>
#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/cpu_cooling.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <asm/cpuidle.h>
#include <asm/sbi.h>
#include <asm/smp.h>
#include <asm/suspend.h>

#include "cpuidle.h"
#include "dt_idle_states.h"
#include "dt_idle_genpd.h"

struct sbi_cpuidle_data {
	u32 *states;
	struct device *dev;
};

struct sbi_domain_state {
	bool available;
	u32 state;
};

static DEFINE_PER_CPU_READ_MOSTLY(struct sbi_cpuidle_data, sbi_cpuidle_data);
static DEFINE_PER_CPU(struct sbi_domain_state, domain_state);
static bool sbi_cpuidle_use_osi;
static bool sbi_cpuidle_use_cpuhp;
static bool sbi_cpuidle_pd_allow_domain_state;

static inline void sbi_set_domain_state(u32 state)
{
	struct sbi_domain_state *data = this_cpu_ptr(&domain_state);

	data->available = true;
	data->state = state;
}

static inline u32 sbi_get_domain_state(void)
{
	struct sbi_domain_state *data = this_cpu_ptr(&domain_state);

	return data->state;
}

static inline void sbi_clear_domain_state(void)
{
	struct sbi_domain_state *data = this_cpu_ptr(&domain_state);

	data->available = false;
}

static inline bool sbi_is_domain_state_available(void)
{
	struct sbi_domain_state *data = this_cpu_ptr(&domain_state);

	return data->available;
}

static __cpuidle int sbi_cpuidle_enter_state(struct cpuidle_device *dev,
					     struct cpuidle_driver *drv, int idx)
{
	u32 *states = __this_cpu_read(sbi_cpuidle_data.states);
	u32 state = states[idx];

	if (state & SBI_HSM_SUSP_NON_RET_BIT)
		return CPU_PM_CPU_IDLE_ENTER_PARAM(riscv_sbi_hart_suspend, idx, state);
	else
		return CPU_PM_CPU_IDLE_ENTER_RETENTION_PARAM(riscv_sbi_hart_suspend,
							     idx, state);
}

static __cpuidle int __sbi_enter_domain_idle_state(struct cpuidle_device *dev,
						   struct cpuidle_driver *drv, int idx,
						   bool s2idle)
{
	struct sbi_cpuidle_data *data = this_cpu_ptr(&sbi_cpuidle_data);
	u32 *states = data->states;
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

	ct_cpuidle_enter();

	if (sbi_is_domain_state_available())
		state = sbi_get_domain_state();
	else
		state = states[idx];

	ret = riscv_sbi_hart_suspend(state) ? -1 : idx;

	ct_cpuidle_exit();

	if (s2idle)
		dev_pm_genpd_resume(pd_dev);
	else
		pm_runtime_get_sync(pd_dev);

	cpu_pm_exit();

	/* Clear the domain state to start fresh when back from idle. */
	sbi_clear_domain_state();
	return ret;
}

static int sbi_enter_domain_idle_state(struct cpuidle_device *dev,
				       struct cpuidle_driver *drv, int idx)
{
	return __sbi_enter_domain_idle_state(dev, drv, idx, false);
}

static int sbi_enter_s2idle_domain_idle_state(struct cpuidle_device *dev,
					      struct cpuidle_driver *drv,
					      int idx)
{
	return __sbi_enter_domain_idle_state(dev, drv, idx, true);
}

static int sbi_cpuidle_cpuhp_up(unsigned int cpu)
{
	struct device *pd_dev = __this_cpu_read(sbi_cpuidle_data.dev);

	if (pd_dev)
		pm_runtime_get_sync(pd_dev);

	return 0;
}

static int sbi_cpuidle_cpuhp_down(unsigned int cpu)
{
	struct device *pd_dev = __this_cpu_read(sbi_cpuidle_data.dev);

	if (pd_dev) {
		pm_runtime_put_sync(pd_dev);
		/* Clear domain state to start fresh at next online. */
		sbi_clear_domain_state();
	}

	return 0;
}

static void sbi_idle_init_cpuhp(void)
{
	int err;

	if (!sbi_cpuidle_use_cpuhp)
		return;

	err = cpuhp_setup_state_nocalls(CPUHP_AP_CPU_PM_STARTING,
					"cpuidle/sbi:online",
					sbi_cpuidle_cpuhp_up,
					sbi_cpuidle_cpuhp_down);
	if (err)
		pr_warn("Failed %d while setup cpuhp state\n", err);
}

static const struct of_device_id sbi_cpuidle_state_match[] = {
	{ .compatible = "riscv,idle-state",
	  .data = sbi_cpuidle_enter_state },
	{ },
};

static int sbi_dt_parse_state_node(struct device_node *np, u32 *state)
{
	int err = of_property_read_u32(np, "riscv,sbi-suspend-param", state);

	if (err) {
		pr_warn("%pOF missing riscv,sbi-suspend-param property\n", np);
		return err;
	}

	if (!riscv_sbi_suspend_state_is_valid(*state)) {
		pr_warn("Invalid SBI suspend state %#x\n", *state);
		return -EINVAL;
	}

	return 0;
}

static int sbi_dt_cpu_init_topology(struct cpuidle_driver *drv,
				     struct sbi_cpuidle_data *data,
				     unsigned int state_count, int cpu)
{
	/* Currently limit the hierarchical topology to be used in OSI mode. */
	if (!sbi_cpuidle_use_osi)
		return 0;

	data->dev = dt_idle_attach_cpu(cpu, "sbi");
	if (IS_ERR_OR_NULL(data->dev))
		return PTR_ERR_OR_ZERO(data->dev);

	/*
	 * Using the deepest state for the CPU to trigger a potential selection
	 * of a shared state for the domain, assumes the domain states are all
	 * deeper states.
	 */
	drv->states[state_count - 1].flags |= CPUIDLE_FLAG_RCU_IDLE;
	drv->states[state_count - 1].enter = sbi_enter_domain_idle_state;
	drv->states[state_count - 1].enter_s2idle =
					sbi_enter_s2idle_domain_idle_state;
	sbi_cpuidle_use_cpuhp = true;

	return 0;
}

static int sbi_cpuidle_dt_init_states(struct device *dev,
					struct cpuidle_driver *drv,
					unsigned int cpu,
					unsigned int state_count)
{
	struct sbi_cpuidle_data *data = per_cpu_ptr(&sbi_cpuidle_data, cpu);
	struct device_node *state_node;
	u32 *states;
	int i, ret;

	struct device_node *cpu_node __free(device_node) = of_cpu_device_node_get(cpu);
	if (!cpu_node)
		return -ENODEV;

	states = devm_kcalloc(dev, state_count, sizeof(*states), GFP_KERNEL);
	if (!states)
		return -ENOMEM;

	/* Parse SBI specific details from state DT nodes */
	for (i = 1; i < state_count; i++) {
		state_node = of_get_cpu_state_node(cpu_node, i - 1);
		if (!state_node)
			break;

		ret = sbi_dt_parse_state_node(state_node, &states[i]);
		of_node_put(state_node);

		if (ret)
			return ret;

		pr_debug("sbi-state %#x index %d\n", states[i], i);
	}
	if (i != state_count)
		return -ENODEV;

	/* Initialize optional data, used for the hierarchical topology. */
	ret = sbi_dt_cpu_init_topology(drv, data, state_count, cpu);
	if (ret < 0)
		return ret;

	/* Store states in the per-cpu struct. */
	data->states = states;

	return 0;
}

static void sbi_cpuidle_deinit_cpu(int cpu)
{
	struct sbi_cpuidle_data *data = per_cpu_ptr(&sbi_cpuidle_data, cpu);

	dt_idle_detach_cpu(data->dev);
	sbi_cpuidle_use_cpuhp = false;
}

static int sbi_cpuidle_init_cpu(struct device *dev, int cpu)
{
	struct cpuidle_driver *drv;
	unsigned int state_count = 0;
	int ret = 0;

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->name = "sbi_cpuidle";
	drv->owner = THIS_MODULE;
	drv->cpumask = (struct cpumask *)cpumask_of(cpu);

	/* RISC-V architectural WFI to be represented as state index 0. */
	drv->states[0].enter = sbi_cpuidle_enter_state;
	drv->states[0].exit_latency = 1;
	drv->states[0].target_residency = 1;
	drv->states[0].power_usage = UINT_MAX;
	strcpy(drv->states[0].name, "WFI");
	strcpy(drv->states[0].desc, "RISC-V WFI");

	/*
	 * If no DT idle states are detected (ret == 0) let the driver
	 * initialization fail accordingly since there is no reason to
	 * initialize the idle driver if only wfi is supported, the
	 * default archictectural back-end already executes wfi
	 * on idle entry.
	 */
	ret = dt_init_idle_driver(drv, sbi_cpuidle_state_match, 1);
	if (ret <= 0) {
		pr_debug("HART%ld: failed to parse DT idle states\n",
			 cpuid_to_hartid_map(cpu));
		return ret ? : -ENODEV;
	}
	state_count = ret + 1; /* Include WFI state as well */

	/* Initialize idle states from DT. */
	ret = sbi_cpuidle_dt_init_states(dev, drv, cpu, state_count);
	if (ret) {
		pr_err("HART%ld: failed to init idle states\n",
		       cpuid_to_hartid_map(cpu));
		return ret;
	}

	if (cpuidle_disabled())
		return 0;

	ret = cpuidle_register(drv, NULL);
	if (ret)
		goto deinit;

	cpuidle_cooling_register(drv);

	return 0;
deinit:
	sbi_cpuidle_deinit_cpu(cpu);
	return ret;
}

static void sbi_cpuidle_domain_sync_state(struct device *dev)
{
	/*
	 * All devices have now been attached/probed to the PM domain
	 * topology, hence it's fine to allow domain states to be picked.
	 */
	sbi_cpuidle_pd_allow_domain_state = true;
}

#ifdef CONFIG_DT_IDLE_GENPD

static int sbi_cpuidle_pd_power_off(struct generic_pm_domain *pd)
{
	struct genpd_power_state *state = &pd->states[pd->state_idx];
	u32 *pd_state;

	if (!state->data)
		return 0;

	if (!sbi_cpuidle_pd_allow_domain_state)
		return -EBUSY;

	/* OSI mode is enabled, set the corresponding domain state. */
	pd_state = state->data;
	sbi_set_domain_state(*pd_state);

	return 0;
}

struct sbi_pd_provider {
	struct list_head link;
	struct device_node *node;
};

static LIST_HEAD(sbi_pd_providers);

static int sbi_pd_init(struct device_node *np)
{
	struct generic_pm_domain *pd;
	struct sbi_pd_provider *pd_provider;
	struct dev_power_governor *pd_gov;
	int ret = -ENOMEM;

	pd = dt_idle_pd_alloc(np, sbi_dt_parse_state_node);
	if (!pd)
		goto out;

	pd_provider = kzalloc(sizeof(*pd_provider), GFP_KERNEL);
	if (!pd_provider)
		goto free_pd;

	pd->flags |= GENPD_FLAG_IRQ_SAFE | GENPD_FLAG_CPU_DOMAIN;

	/* Allow power off when OSI is available. */
	if (sbi_cpuidle_use_osi)
		pd->power_off = sbi_cpuidle_pd_power_off;
	else
		pd->flags |= GENPD_FLAG_ALWAYS_ON;

	/* Use governor for CPU PM domains if it has some states to manage. */
	pd_gov = pd->states ? &pm_domain_cpu_gov : NULL;

	ret = pm_genpd_init(pd, pd_gov, false);
	if (ret)
		goto free_pd_prov;

	ret = of_genpd_add_provider_simple(np, pd);
	if (ret)
		goto remove_pd;

	pd_provider->node = of_node_get(np);
	list_add(&pd_provider->link, &sbi_pd_providers);

	pr_debug("init PM domain %s\n", pd->name);
	return 0;

remove_pd:
	pm_genpd_remove(pd);
free_pd_prov:
	kfree(pd_provider);
free_pd:
	dt_idle_pd_free(pd);
out:
	pr_err("failed to init PM domain ret=%d %pOF\n", ret, np);
	return ret;
}

static void sbi_pd_remove(void)
{
	struct sbi_pd_provider *pd_provider, *it;
	struct generic_pm_domain *genpd;

	list_for_each_entry_safe(pd_provider, it, &sbi_pd_providers, link) {
		of_genpd_del_provider(pd_provider->node);

		genpd = of_genpd_remove_last(pd_provider->node);
		if (!IS_ERR(genpd))
			kfree(genpd);

		of_node_put(pd_provider->node);
		list_del(&pd_provider->link);
		kfree(pd_provider);
	}
}

static int sbi_genpd_probe(struct device_node *np)
{
	int ret = 0, pd_count = 0;

	if (!np)
		return -ENODEV;

	/*
	 * Parse child nodes for the "#power-domain-cells" property and
	 * initialize a genpd/genpd-of-provider pair when it's found.
	 */
	for_each_child_of_node_scoped(np, node) {
		if (!of_property_present(node, "#power-domain-cells"))
			continue;

		ret = sbi_pd_init(node);
		if (ret)
			goto remove_pd;

		pd_count++;
	}

	/* Bail out if not using the hierarchical CPU topology. */
	if (!pd_count)
		goto no_pd;

	/* Link genpd masters/subdomains to model the CPU topology. */
	ret = dt_idle_pd_init_topology(np);
	if (ret)
		goto remove_pd;

	return 0;

remove_pd:
	sbi_pd_remove();
	pr_err("failed to create CPU PM domains ret=%d\n", ret);
no_pd:
	return ret;
}

#else

static inline int sbi_genpd_probe(struct device_node *np)
{
	return 0;
}

#endif

static int sbi_cpuidle_probe(struct platform_device *pdev)
{
	int cpu, ret;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;
	struct device_node *pds_node;

	/* Detect OSI support based on CPU DT nodes */
	sbi_cpuidle_use_osi = true;
	for_each_possible_cpu(cpu) {
		struct device_node *np __free(device_node) = of_cpu_device_node_get(cpu);
		if (np &&
		    of_property_present(np, "power-domains") &&
		    of_property_present(np, "power-domain-names")) {
			continue;
		} else {
			sbi_cpuidle_use_osi = false;
			break;
		}
	}

	/* Populate generic power domains from DT nodes */
	pds_node = of_find_node_by_path("/cpus/power-domains");
	if (pds_node) {
		ret = sbi_genpd_probe(pds_node);
		of_node_put(pds_node);
		if (ret)
			return ret;
	}

	/* Initialize CPU idle driver for each present CPU */
	for_each_present_cpu(cpu) {
		ret = sbi_cpuidle_init_cpu(&pdev->dev, cpu);
		if (ret) {
			pr_debug("HART%ld: idle driver init failed\n",
				 cpuid_to_hartid_map(cpu));
			goto out_fail;
		}
	}

	/* Setup CPU hotplut notifiers */
	sbi_idle_init_cpuhp();

	if (cpuidle_disabled())
		pr_info("cpuidle is disabled\n");
	else
		pr_info("idle driver registered for all CPUs\n");

	return 0;

out_fail:
	while (--cpu >= 0) {
		dev = per_cpu(cpuidle_devices, cpu);
		drv = cpuidle_get_cpu_driver(dev);
		cpuidle_unregister(drv);
		sbi_cpuidle_deinit_cpu(cpu);
	}

	return ret;
}

static struct platform_driver sbi_cpuidle_driver = {
	.probe = sbi_cpuidle_probe,
	.driver = {
		.name = "sbi-cpuidle",
		.sync_state = sbi_cpuidle_domain_sync_state,
	},
};

static int __init sbi_cpuidle_init(void)
{
	int ret;
	struct platform_device *pdev;

	if (!riscv_sbi_hsm_is_supported())
		return 0;

	ret = platform_driver_register(&sbi_cpuidle_driver);
	if (ret)
		return ret;

	pdev = platform_device_register_simple("sbi-cpuidle",
						-1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&sbi_cpuidle_driver);
		return PTR_ERR(pdev);
	}

	return 0;
}
arch_initcall(sbi_cpuidle_init);
