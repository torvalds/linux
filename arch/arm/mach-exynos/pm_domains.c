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

#define INT_LOCAL_PWR_EN	0x7
#define MAX_CLK_PER_DOMAIN	4

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
};

static int exynos_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct exynos_pm_domain *pd;
	void __iomem *base;
	u32 timeout, pwr;
	char *op;

	pd = container_of(domain, struct exynos_pm_domain, pd);
	base = pd->base;

	/* Set oscclk before powering off a domain*/
	if (!power_on) {
		int i;

		for (i = 0; i < MAX_CLK_PER_DOMAIN; i++) {
			if (IS_ERR(pd->clk[i]))
				break;
			if (clk_set_parent(pd->clk[i], pd->oscclk))
				pr_err("%s: error setting oscclk as parent to clock %d\n",
						pd->name, i);
		}
	}

	pwr = power_on ? INT_LOCAL_PWR_EN : 0;
	__raw_writel(pwr, base);

	/* Wait max 1ms */
	timeout = 10;

	while ((__raw_readl(base + 0x4) & INT_LOCAL_PWR_EN) != pwr) {
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
		int i;

		for (i = 0; i < MAX_CLK_PER_DOMAIN; i++) {
			if (IS_ERR(pd->clk[i]))
				break;
			if (clk_set_parent(pd->clk[i], pd->pclk[i]))
				pr_err("%s: error setting parent to clock%d\n",
						pd->name, i);
		}
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

static void exynos_add_device_to_domain(struct exynos_pm_domain *pd,
					 struct device *dev)
{
	int ret;

	dev_dbg(dev, "adding to power domain %s\n", pd->pd.name);

	while (1) {
		ret = pm_genpd_add_device(&pd->pd, dev);
		if (ret != -EAGAIN)
			break;
		cond_resched();
	}

	pm_genpd_dev_need_restore(dev, true);
}

static void exynos_remove_device_from_domain(struct device *dev)
{
	struct generic_pm_domain *genpd = dev_to_genpd(dev);
	int ret;

	dev_dbg(dev, "removing from power domain %s\n", genpd->name);

	while (1) {
		ret = pm_genpd_remove_device(genpd, dev);
		if (ret != -EAGAIN)
			break;
		cond_resched();
	}
}

static void exynos_read_domain_from_dt(struct device *dev)
{
	struct platform_device *pd_pdev;
	struct exynos_pm_domain *pd;
	struct device_node *node;

	node = of_parse_phandle(dev->of_node, "samsung,power-domain", 0);
	if (!node)
		return;
	pd_pdev = of_find_device_by_node(node);
	if (!pd_pdev)
		return;
	pd = platform_get_drvdata(pd_pdev);
	exynos_add_device_to_domain(pd, dev);
}

static int exynos_pm_notifier_call(struct notifier_block *nb,
				    unsigned long event, void *data)
{
	struct device *dev = data;

	switch (event) {
	case BUS_NOTIFY_BIND_DRIVER:
		if (dev->of_node)
			exynos_read_domain_from_dt(dev);

		break;

	case BUS_NOTIFY_UNBOUND_DRIVER:
		exynos_remove_device_from_domain(dev);

		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block platform_nb = {
	.notifier_call = exynos_pm_notifier_call,
};

static __init int exynos4_pm_init_power_domain(void)
{
	struct platform_device *pdev;
	struct device_node *np;

	for_each_compatible_node(np, NULL, "samsung,exynos4210-pd") {
		struct exynos_pm_domain *pd;
		int on, i;
		struct device *dev;

		pdev = of_find_device_by_node(np);
		dev = &pdev->dev;

		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd) {
			pr_err("%s: failed to allocate memory for domain\n",
					__func__);
			return -ENOMEM;
		}

		pd->pd.name = kstrdup(np->name, GFP_KERNEL);
		pd->name = pd->pd.name;
		pd->base = of_iomap(np, 0);
		pd->pd.power_off = exynos_pd_power_off;
		pd->pd.power_on = exynos_pd_power_on;
		pd->pd.of_node = np;

		pd->oscclk = clk_get(dev, "oscclk");
		if (IS_ERR(pd->oscclk))
			goto no_clk;

		for (i = 0; i < MAX_CLK_PER_DOMAIN; i++) {
			char clk_name[8];

			snprintf(clk_name, sizeof(clk_name), "clk%d", i);
			pd->clk[i] = clk_get(dev, clk_name);
			if (IS_ERR(pd->clk[i]))
				break;
			snprintf(clk_name, sizeof(clk_name), "pclk%d", i);
			pd->pclk[i] = clk_get(dev, clk_name);
			if (IS_ERR(pd->pclk[i])) {
				clk_put(pd->clk[i]);
				pd->clk[i] = ERR_PTR(-EINVAL);
				break;
			}
		}

		if (IS_ERR(pd->clk[0]))
			clk_put(pd->oscclk);

no_clk:
		platform_set_drvdata(pdev, pd);

		on = __raw_readl(pd->base + 0x4) & INT_LOCAL_PWR_EN;

		pm_genpd_init(&pd->pd, NULL, !on);
	}

	bus_register_notifier(&platform_bus_type, &platform_nb);

	return 0;
}
arch_initcall(exynos4_pm_init_power_domain);
