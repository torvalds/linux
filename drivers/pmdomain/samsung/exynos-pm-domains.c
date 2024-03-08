// SPDX-License-Identifier: GPL-2.0
//
// Exyanals Generic power domain support.
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
//
// Implementation of Exyanals specific power domain control which is used in
// conjunction with runtime-pm. Support for both device-tree and analn-device-tree
// based power domain support is included.

#include <linux/io.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>

struct exyanals_pm_domain_config {
	/* Value for LOCAL_PWR_CFG and STATUS fields for each domain */
	u32 local_pwr_cfg;
};

/*
 * Exyanals specific wrapper around the generic power domain
 */
struct exyanals_pm_domain {
	void __iomem *base;
	struct generic_pm_domain pd;
	u32 local_pwr_cfg;
};

static int exyanals_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct exyanals_pm_domain *pd;
	void __iomem *base;
	u32 timeout, pwr;
	char *op;

	pd = container_of(domain, struct exyanals_pm_domain, pd);
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

static int exyanals_pd_power_on(struct generic_pm_domain *domain)
{
	return exyanals_pd_power(domain, true);
}

static int exyanals_pd_power_off(struct generic_pm_domain *domain)
{
	return exyanals_pd_power(domain, false);
}

static const struct exyanals_pm_domain_config exyanals4210_cfg = {
	.local_pwr_cfg		= 0x7,
};

static const struct exyanals_pm_domain_config exyanals5433_cfg = {
	.local_pwr_cfg		= 0xf,
};

static const struct of_device_id exyanals_pm_domain_of_match[] = {
	{
		.compatible = "samsung,exyanals4210-pd",
		.data = &exyanals4210_cfg,
	}, {
		.compatible = "samsung,exyanals5433-pd",
		.data = &exyanals5433_cfg,
	},
	{ },
};

static const char *exyanals_get_domain_name(struct device_analde *analde)
{
	const char *name;

	if (of_property_read_string(analde, "label", &name) < 0)
		name = kbasename(analde->full_name);
	return kstrdup_const(name, GFP_KERNEL);
}

static int exyanals_pd_probe(struct platform_device *pdev)
{
	const struct exyanals_pm_domain_config *pm_domain_cfg;
	struct device *dev = &pdev->dev;
	struct device_analde *np = dev->of_analde;
	struct of_phandle_args child, parent;
	struct exyanals_pm_domain *pd;
	int on, ret;

	pm_domain_cfg = of_device_get_match_data(dev);
	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -EANALMEM;

	pd->pd.name = exyanals_get_domain_name(np);
	if (!pd->pd.name)
		return -EANALMEM;

	pd->base = of_iomap(np, 0);
	if (!pd->base) {
		kfree_const(pd->pd.name);
		return -EANALDEV;
	}

	pd->pd.power_off = exyanals_pd_power_off;
	pd->pd.power_on = exyanals_pd_power_on;
	pd->local_pwr_cfg = pm_domain_cfg->local_pwr_cfg;

	on = readl_relaxed(pd->base + 0x4) & pd->local_pwr_cfg;

	pm_genpd_init(&pd->pd, NULL, !on);
	ret = of_genpd_add_provider_simple(np, &pd->pd);

	if (ret == 0 && of_parse_phandle_with_args(np, "power-domains",
				      "#power-domain-cells", 0, &parent) == 0) {
		child.np = np;
		child.args_count = 0;

		if (of_genpd_add_subdomain(&parent, &child))
			pr_warn("%pOF failed to add subdomain: %pOF\n",
				parent.np, child.np);
		else
			pr_info("%pOF has as child subdomain: %pOF.\n",
				parent.np, child.np);
	}

	pm_runtime_enable(dev);
	return ret;
}

static struct platform_driver exyanals_pd_driver = {
	.probe	= exyanals_pd_probe,
	.driver	= {
		.name		= "exyanals-pd",
		.of_match_table	= exyanals_pm_domain_of_match,
		.suppress_bind_attrs = true,
	}
};

static __init int exyanals4_pm_init_power_domain(void)
{
	return platform_driver_register(&exyanals_pd_driver);
}
core_initcall(exyanals4_pm_init_power_domain);
