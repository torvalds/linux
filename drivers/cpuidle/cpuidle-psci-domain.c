// SPDX-License-Identifier: GPL-2.0
/*
 * PM domains for CPUs via genpd - managed by cpuidle-psci.
 *
 * Copyright (C) 2019 Linaro Ltd.
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 */

#define pr_fmt(fmt) "CPUidle PSCI: " fmt

#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/psci.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "cpuidle-psci.h"

struct psci_pd_provider {
	struct list_head link;
	struct device_node *node;
};

static LIST_HEAD(psci_pd_providers);
static bool psci_pd_allow_domain_state;

static int psci_pd_power_off(struct generic_pm_domain *pd)
{
	struct genpd_power_state *state = &pd->states[pd->state_idx];
	u32 *pd_state;

	if (!state->data)
		return 0;

	if (!psci_pd_allow_domain_state)
		return -EBUSY;

	/* OSI mode is enabled, set the corresponding domain state. */
	pd_state = state->data;
	psci_set_domain_state(*pd_state);

	return 0;
}

static int psci_pd_init(struct device_node *np, bool use_osi)
{
	struct generic_pm_domain *pd;
	struct psci_pd_provider *pd_provider;
	struct dev_power_governor *pd_gov;
	int ret = -ENOMEM;

	pd = dt_idle_pd_alloc(np, psci_dt_parse_state_node);
	if (!pd)
		goto out;

	pd_provider = kzalloc(sizeof(*pd_provider), GFP_KERNEL);
	if (!pd_provider)
		goto free_pd;

	pd->flags |= GENPD_FLAG_IRQ_SAFE | GENPD_FLAG_CPU_DOMAIN;

	/*
	 * Allow power off when OSI has been successfully enabled.
	 * PREEMPT_RT is not yet ready to enter domain idle states.
	 */
	if (use_osi && !IS_ENABLED(CONFIG_PREEMPT_RT))
		pd->power_off = psci_pd_power_off;
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
	list_add(&pd_provider->link, &psci_pd_providers);

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

static void psci_pd_remove(void)
{
	struct psci_pd_provider *pd_provider, *it;
	struct generic_pm_domain *genpd;

	list_for_each_entry_safe_reverse(pd_provider, it,
					 &psci_pd_providers, link) {
		of_genpd_del_provider(pd_provider->node);

		genpd = of_genpd_remove_last(pd_provider->node);
		if (!IS_ERR(genpd))
			kfree(genpd);

		of_node_put(pd_provider->node);
		list_del(&pd_provider->link);
		kfree(pd_provider);
	}
}

static void psci_cpuidle_domain_sync_state(struct device *dev)
{
	/*
	 * All devices have now been attached/probed to the PM domain topology,
	 * hence it's fine to allow domain states to be picked.
	 */
	psci_pd_allow_domain_state = true;
}

static const struct of_device_id psci_of_match[] = {
	{ .compatible = "arm,psci-1.0" },
	{}
};

static int psci_cpuidle_domain_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *node;
	bool use_osi = psci_has_osi_support();
	int ret = 0, pd_count = 0;

	if (!np)
		return -ENODEV;

	/*
	 * Parse child nodes for the "#power-domain-cells" property and
	 * initialize a genpd/genpd-of-provider pair when it's found.
	 */
	for_each_child_of_node(np, node) {
		if (!of_property_present(node, "#power-domain-cells"))
			continue;

		ret = psci_pd_init(node, use_osi);
		if (ret) {
			of_node_put(node);
			goto exit;
		}

		pd_count++;
	}

	/* Bail out if not using the hierarchical CPU topology. */
	if (!pd_count)
		return 0;

	/* Link genpd masters/subdomains to model the CPU topology. */
	ret = dt_idle_pd_init_topology(np);
	if (ret)
		goto remove_pd;

	/* let's try to enable OSI. */
	ret = psci_set_osi_mode(use_osi);
	if (ret)
		goto remove_pd;

	pr_info("Initialized CPU PM domain topology using %s mode\n",
		use_osi ? "OSI" : "PC");
	return 0;

remove_pd:
	dt_idle_pd_remove_topology(np);
	psci_pd_remove();
exit:
	pr_err("failed to create CPU PM domains ret=%d\n", ret);
	return ret;
}

static struct platform_driver psci_cpuidle_domain_driver = {
	.probe  = psci_cpuidle_domain_probe,
	.driver = {
		.name = "psci-cpuidle-domain",
		.of_match_table = psci_of_match,
		.sync_state = psci_cpuidle_domain_sync_state,
	},
};

static int __init psci_idle_init_domains(void)
{
	return platform_driver_register(&psci_cpuidle_domain_driver);
}
subsys_initcall(psci_idle_init_domains);
