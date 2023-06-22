// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#if defined(_TRACE_HOOK_PM_DOMAIN_H)
#include <trace/hooks/pm_domain.h>
#endif

#define CREATE_TRACE_POINTS
#include "qcom-simple-lpm.h"

LIST_HEAD(cluster_dev_list);
u64 cluster_cur_div = 500;

static struct simple_lpm_cluster *to_cluster(struct generic_pm_domain *genpd)
{
	struct simple_lpm_cluster *cluster_simple_gov;

	list_for_each_entry(cluster_simple_gov, &cluster_dev_list, list)
		if (cluster_simple_gov->genpd == genpd)
			return cluster_simple_gov;

	return NULL;
}

void update_cluster_select(struct simple_lpm_cpu *cpu_gov)
{
}

#if defined(_TRACE_HOOK_PM_DOMAIN_H)
static void android_vh_allow_domain_state(void *unused,
					  struct generic_pm_domain *genpd,
					  uint32_t idx, bool *allow)
{
	struct simple_lpm_cluster *cluster_simple_gov = to_cluster(genpd);

	if (!cluster_simple_gov)
		return;

	*allow = cluster_simple_gov->state_allowed[idx];
}
#endif

static int simple_lpm_cluster_simple_gov_remove(struct platform_device *pdev)
{
	int i;
	struct generic_pm_domain *genpd = pd_to_genpd(pdev->dev.pm_domain);
	struct simple_lpm_cluster *cluster_simple_gov = to_cluster(genpd);

	if (!cluster_simple_gov)
		return -ENODEV;

	pm_runtime_disable(&pdev->dev);
	cluster_simple_gov->genpd->flags &= ~GENPD_FLAG_MIN_RESIDENCY;
	remove_simple_cluster_sysfs_nodes(cluster_simple_gov);

	for (i = 0; i < genpd->state_count; i++) {
		struct genpd_power_state *states = &genpd->states[i];

		states->residency_ns = states->residency_ns * cluster_cur_div;
		states->power_on_latency_ns = states->power_on_latency_ns * cluster_cur_div;
		states->power_off_latency_ns = states->power_off_latency_ns * cluster_cur_div;
	}

	list_del(&cluster_simple_gov->list);

	return 0;
}

static int simple_lpm_cluster_simple_gov_probe(struct platform_device *pdev)
{
	int i, ret;
	struct simple_lpm_cluster *cluster_simple_gov;

	cluster_simple_gov = devm_kzalloc(&pdev->dev,
				   sizeof(struct simple_lpm_cluster),
				   GFP_KERNEL);
	if (!cluster_simple_gov)
		return -ENOMEM;

	spin_lock_init(&cluster_simple_gov->lock);
	cluster_simple_gov->dev = &pdev->dev;
	pm_runtime_enable(&pdev->dev);
	cluster_simple_gov->genpd = pd_to_genpd(cluster_simple_gov->dev->pm_domain);
	dev_pm_genpd_set_next_wakeup(cluster_simple_gov->dev, KTIME_MAX - 1);
	cluster_simple_gov->genpd->flags |= GENPD_FLAG_MIN_RESIDENCY;

	ret = create_simple_cluster_sysfs_nodes(cluster_simple_gov);
	if (ret < 0) {
		pm_runtime_disable(&pdev->dev);
		cluster_simple_gov->genpd->flags &= ~GENPD_FLAG_MIN_RESIDENCY;
		return ret;
	}

	list_add_tail(&cluster_simple_gov->list, &cluster_dev_list);
	cluster_simple_gov->initialized = true;

	for (i = 0; i < cluster_simple_gov->genpd->state_count; i++) {
		struct generic_pm_domain *genpd = cluster_simple_gov->genpd;
		struct genpd_power_state *states = &genpd->states[i];

		do_div(states->residency_ns, cluster_cur_div);
		do_div(states->power_on_latency_ns, cluster_cur_div);
		do_div(states->power_off_latency_ns, cluster_cur_div);
		cluster_simple_gov->state_allowed[i] = true;
	}

	return 0;
}

static const struct of_device_id qcom_cluster_simple_lpm[] = {
	{ .compatible = "qcom,lpm-cluster-dev" },
	{ }
};

static struct platform_driver qcom_cluster_simple_lpm_driver = {
	.probe = simple_lpm_cluster_simple_gov_probe,
	.remove = simple_lpm_cluster_simple_gov_remove,
	.driver = {
		.name = "qcom-simple-gov",
		.of_match_table = qcom_cluster_simple_lpm,
		.suppress_bind_attrs = true,
	},
};

static void cluster_simple_gov_disable(void)
{
#if defined(_TRACE_HOOK_PM_DOMAIN_H)
	unregister_trace_android_vh_allow_domain_state(android_vh_allow_domain_state, NULL);
#endif
	platform_driver_unregister(&qcom_cluster_simple_lpm_driver);
}

static void cluster_simple_gov_enable(void)
{
#if defined(_TRACE_HOOK_PM_DOMAIN_H)
	register_trace_android_vh_allow_domain_state(android_vh_allow_domain_state, NULL);
#endif
	platform_driver_register(&qcom_cluster_simple_lpm_driver);
}

struct simple_cluster_governor gov_ops = {
	.select = update_cluster_select,
	.enable = cluster_simple_gov_enable,
	.disable = cluster_simple_gov_disable,
};

void qcom_cluster_lpm_simple_governor_deinit(void)
{
	unregister_cluster_simple_governor_ops(&gov_ops);
}

int qcom_cluster_lpm_simple_governor_init(void)
{
	register_cluster_simple_governor_ops(&gov_ops);

	return 0;
}
