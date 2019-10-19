// SPDX-License-Identifier: GPL-2.0
//
// Exynos Generic power domain support.
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
//
// Implementation of Exynos specific power domain control which is used in
// conjunction with runtime-pm. Support for both device-tree and non-device-tree
// based power domain support is included.

#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/sched.h>

struct exynos_pm_domain_config {
	/* Value for LOCAL_PWR_CFG and STATUS fields for each domain */
	u32 local_pwr_cfg;
};

/*
 * Exynos specific wrapper around the generic power domain
 */
struct exynos_pm_domain {
	void __iomem *base;
	bool is_off;
	struct generic_pm_domain pd;
	u32 local_pwr_cfg;
};

static int exynos_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct exynos_pm_domain *pd;
	void __iomem *base;
	u32 timeout, pwr;
	char *op;

	pd = container_of(domain, struct exynos_pm_domain, pd);
	base = pd->base;

	pwr = power_on ? pd->local_pwr_cfg : 0;
	writel_relaxed(pwr, base);

	/* Wait max 1ms */
	timeout = 10;

	while ((readl_relaxed(base + 0x4) & pd->local_pwr_cfg) != pwr) {
		if (!timeout) {
			op = (power_on) ? "enable" : "disable";
			pr_err("Power domain %s %s failed\n", domain->name, op);
			return -ETIMEDOUT;
		}
		timeout--;
		cpu_relax();
		usleep_range(80, 100);
	}

	return 0;
}

static int exynos_pd_power_on(struct generic_pm_domain *domain)
{
	return exynos_pd_power(domain, true);
}

static int exynos_pd_power_off(struct generic_pm_domain *domain)
{
	return exynos_pd_power(domain, false);
}

static const struct exynos_pm_domain_config exynos4210_cfg __initconst = {
	.local_pwr_cfg		= 0x7,
};

static const struct exynos_pm_domain_config exynos5433_cfg __initconst = {
	.local_pwr_cfg		= 0xf,
};

static const struct of_device_id exynos_pm_domain_of_match[] __initconst = {
	{
		.compatible = "samsung,exynos4210-pd",
		.data = &exynos4210_cfg,
	}, {
		.compatible = "samsung,exynos5433-pd",
		.data = &exynos5433_cfg,
	},
	{ },
};

static __init const char *exynos_get_domain_name(struct device_node *node)
{
	const char *name;

	if (of_property_read_string(node, "label", &name) < 0)
		name = kbasename(node->full_name);
	return kstrdup_const(name, GFP_KERNEL);
}

static __init int exynos4_pm_init_power_domain(void)
{
	struct device_node *np;
	const struct of_device_id *match;

	for_each_matching_node_and_match(np, exynos_pm_domain_of_match, &match) {
		const struct exynos_pm_domain_config *pm_domain_cfg;
		struct exynos_pm_domain *pd;
		int on;

		pm_domain_cfg = match->data;

		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd) {
			of_node_put(np);
			return -ENOMEM;
		}
		pd->pd.name = exynos_get_domain_name(np);
		if (!pd->pd.name) {
			kfree(pd);
			of_node_put(np);
			return -ENOMEM;
		}

		pd->base = of_iomap(np, 0);
		if (!pd->base) {
			pr_warn("%s: failed to map memory\n", __func__);
			kfree_const(pd->pd.name);
			kfree(pd);
			continue;
		}

		pd->pd.power_off = exynos_pd_power_off;
		pd->pd.power_on = exynos_pd_power_on;
		pd->local_pwr_cfg = pm_domain_cfg->local_pwr_cfg;

		on = readl_relaxed(pd->base + 0x4) & pd->local_pwr_cfg;

		pm_genpd_init(&pd->pd, NULL, !on);
		of_genpd_add_provider_simple(np, &pd->pd);
	}

	/* Assign the child power domains to their parents */
	for_each_matching_node(np, exynos_pm_domain_of_match) {
		struct of_phandle_args child, parent;

		child.np = np;
		child.args_count = 0;

		if (of_parse_phandle_with_args(np, "power-domains",
					       "#power-domain-cells", 0,
					       &parent) != 0)
			continue;

		if (of_genpd_add_subdomain(&parent, &child))
			pr_warn("%pOF failed to add subdomain: %pOF\n",
				parent.np, child.np);
		else
			pr_info("%pOF has as child subdomain: %pOF.\n",
				parent.np, child.np);
	}

	return 0;
}
core_initcall(exynos4_pm_init_power_domain);
