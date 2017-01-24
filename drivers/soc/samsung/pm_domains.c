/*
 * Exynos Generic power domain support.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Implementation of Exynos specific power domain control which is used in
 * conjunction with runtime-pm. Support for both device-tree and non-device-tree
 * based power domain support is included.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/sched.h>

#define MAX_CLK_PER_DOMAIN	4

struct exynos_pm_domain_config {
	/* Value for LOCAL_PWR_CFG and STATUS fields for each domain */
	u32 local_pwr_cfg;
};

/*
 * Exynos specific wrapper around the generic power domain
 */
struct exynos_pm_domain {
	void __iomem *base;
	char const *name;
	bool is_off;
	struct generic_pm_domain pd;
	struct clk *oscclk;
	struct clk *clk[MAX_CLK_PER_DOMAIN];
	struct clk *pclk[MAX_CLK_PER_DOMAIN];
	struct clk *asb_clk[MAX_CLK_PER_DOMAIN];
	u32 local_pwr_cfg;
};

static int exynos_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct exynos_pm_domain *pd;
	void __iomem *base;
	u32 timeout, pwr;
	char *op;
	int i;

	pd = container_of(domain, struct exynos_pm_domain, pd);
	base = pd->base;

	for (i = 0; i < MAX_CLK_PER_DOMAIN; i++) {
		if (IS_ERR(pd->asb_clk[i]))
			break;
		clk_prepare_enable(pd->asb_clk[i]);
	}

	/* Set oscclk before powering off a domain*/
	if (!power_on) {
		for (i = 0; i < MAX_CLK_PER_DOMAIN; i++) {
			if (IS_ERR(pd->clk[i]))
				break;
			pd->pclk[i] = clk_get_parent(pd->clk[i]);
			if (clk_set_parent(pd->clk[i], pd->oscclk))
				pr_err("%s: error setting oscclk as parent to clock %d\n",
						pd->name, i);
		}
	}

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

	/* Restore clocks after powering on a domain*/
	if (power_on) {
		for (i = 0; i < MAX_CLK_PER_DOMAIN; i++) {
			if (IS_ERR(pd->clk[i]))
				break;

			if (IS_ERR(pd->pclk[i]))
				continue; /* Skip on first power up */
			if (clk_set_parent(pd->clk[i], pd->pclk[i]))
				pr_err("%s: error setting parent to clock%d\n",
						pd->name, i);
		}
	}

	for (i = 0; i < MAX_CLK_PER_DOMAIN; i++) {
		if (IS_ERR(pd->asb_clk[i]))
			break;
		clk_disable_unprepare(pd->asb_clk[i]);
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

static const struct of_device_id exynos_pm_domain_of_match[] __initconst = {
	{
		.compatible = "samsung,exynos4210-pd",
		.data = &exynos4210_cfg,
	},
	{ },
};

static __init int exynos4_pm_init_power_domain(void)
{
	struct device_node *np;
	const struct of_device_id *match;

	for_each_matching_node_and_match(np, exynos_pm_domain_of_match, &match) {
		const struct exynos_pm_domain_config *pm_domain_cfg;
		struct exynos_pm_domain *pd;
		int on, i;

		pm_domain_cfg = match->data;

		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd) {
			pr_err("%s: failed to allocate memory for domain\n",
					__func__);
			of_node_put(np);
			return -ENOMEM;
		}
		pd->pd.name = kstrdup_const(strrchr(np->full_name, '/') + 1,
					    GFP_KERNEL);
		if (!pd->pd.name) {
			kfree(pd);
			of_node_put(np);
			return -ENOMEM;
		}

		pd->name = pd->pd.name;
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

		for (i = 0; i < MAX_CLK_PER_DOMAIN; i++) {
			char clk_name[8];

			snprintf(clk_name, sizeof(clk_name), "asb%d", i);
			pd->asb_clk[i] = of_clk_get_by_name(np, clk_name);
			if (IS_ERR(pd->asb_clk[i]))
				break;
		}

		pd->oscclk = of_clk_get_by_name(np, "oscclk");
		if (IS_ERR(pd->oscclk))
			goto no_clk;

		for (i = 0; i < MAX_CLK_PER_DOMAIN; i++) {
			char clk_name[8];

			snprintf(clk_name, sizeof(clk_name), "clk%d", i);
			pd->clk[i] = of_clk_get_by_name(np, clk_name);
			if (IS_ERR(pd->clk[i]))
				break;
			/*
			 * Skip setting parent on first power up.
			 * The parent at this time may not be useful at all.
			 */
			pd->pclk[i] = ERR_PTR(-EINVAL);
		}

		if (IS_ERR(pd->clk[0]))
			clk_put(pd->oscclk);

no_clk:
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
			pr_warn("%s failed to add subdomain: %s\n",
				parent.np->name, child.np->name);
		else
			pr_info("%s has as child subdomain: %s.\n",
				parent.np->name, child.np->name);
	}

	return 0;
}
core_initcall(exynos4_pm_init_power_domain);
