// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022 NXP
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>

#define MIX_SLICE_SW_CTRL_OFF		0x20
#define SLICE_SW_CTRL_PSW_CTRL_OFF_MASK	BIT(4)
#define SLICE_SW_CTRL_PDN_SOFT_MASK	BIT(31)

#define MIX_FUNC_STAT_OFF		0xB4

#define FUNC_STAT_PSW_STAT_MASK		BIT(0)
#define FUNC_STAT_RST_STAT_MASK		BIT(2)
#define FUNC_STAT_ISO_STAT_MASK		BIT(4)

struct imx93_power_domain {
	struct generic_pm_domain genpd;
	struct device *dev;
	void __iomem *addr;
	struct clk_bulk_data *clks;
	int num_clks;
	bool init_off;
};

#define to_imx93_pd(_genpd) container_of(_genpd, struct imx93_power_domain, genpd)

static int imx93_pd_on(struct generic_pm_domain *genpd)
{
	struct imx93_power_domain *domain = to_imx93_pd(genpd);
	void __iomem *addr = domain->addr;
	u32 val;
	int ret;

	ret = clk_bulk_prepare_enable(domain->num_clks, domain->clks);
	if (ret) {
		dev_err(domain->dev, "failed to enable clocks for domain: %s\n", genpd->name);
		return ret;
	}

	val = readl(addr + MIX_SLICE_SW_CTRL_OFF);
	val &= ~SLICE_SW_CTRL_PDN_SOFT_MASK;
	writel(val, addr + MIX_SLICE_SW_CTRL_OFF);

	ret = readl_poll_timeout(addr + MIX_FUNC_STAT_OFF, val,
				 !(val & FUNC_STAT_ISO_STAT_MASK), 1, 10000);
	if (ret) {
		dev_err(domain->dev, "pd_on timeout: name: %s, stat: %x\n", genpd->name, val);
		return ret;
	}

	return 0;
}

static int imx93_pd_off(struct generic_pm_domain *genpd)
{
	struct imx93_power_domain *domain = to_imx93_pd(genpd);
	void __iomem *addr = domain->addr;
	int ret;
	u32 val;

	/* Power off MIX */
	val = readl(addr + MIX_SLICE_SW_CTRL_OFF);
	val |= SLICE_SW_CTRL_PDN_SOFT_MASK;
	writel(val, addr + MIX_SLICE_SW_CTRL_OFF);

	ret = readl_poll_timeout(addr + MIX_FUNC_STAT_OFF, val,
				 val & FUNC_STAT_PSW_STAT_MASK, 1, 1000);
	if (ret) {
		dev_err(domain->dev, "pd_off timeout: name: %s, stat: %x\n", genpd->name, val);
		return ret;
	}

	clk_bulk_disable_unprepare(domain->num_clks, domain->clks);

	return 0;
};

static int imx93_pd_remove(struct platform_device *pdev)
{
	struct imx93_power_domain *domain = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	if (!domain->init_off)
		clk_bulk_disable_unprepare(domain->num_clks, domain->clks);

	of_genpd_del_provider(np);
	pm_genpd_remove(&domain->genpd);

	return 0;
}

static int imx93_pd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct imx93_power_domain *domain;
	int ret;

	domain = devm_kzalloc(dev, sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return -ENOMEM;

	domain->addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(domain->addr))
		return PTR_ERR(domain->addr);

	domain->num_clks = devm_clk_bulk_get_all(dev, &domain->clks);
	if (domain->num_clks < 0)
		return dev_err_probe(dev, domain->num_clks, "Failed to get domain's clocks\n");

	domain->genpd.name = dev_name(dev);
	domain->genpd.power_off = imx93_pd_off;
	domain->genpd.power_on = imx93_pd_on;
	domain->dev = dev;

	domain->init_off = readl(domain->addr + MIX_FUNC_STAT_OFF) & FUNC_STAT_ISO_STAT_MASK;
	/* Just to sync the status of hardware */
	if (!domain->init_off) {
		ret = clk_bulk_prepare_enable(domain->num_clks, domain->clks);
		if (ret) {
			dev_err(domain->dev, "failed to enable clocks for domain: %s\n",
				domain->genpd.name);
			return ret;
		}
	}

	ret = pm_genpd_init(&domain->genpd, NULL, domain->init_off);
	if (ret)
		goto err_clk_unprepare;

	platform_set_drvdata(pdev, domain);

	ret = of_genpd_add_provider_simple(np, &domain->genpd);
	if (ret)
		goto err_genpd_remove;

	return 0;

err_genpd_remove:
	pm_genpd_remove(&domain->genpd);

err_clk_unprepare:
	if (!domain->init_off)
		clk_bulk_disable_unprepare(domain->num_clks, domain->clks);

	return ret;
}

static const struct of_device_id imx93_pd_ids[] = {
	{ .compatible = "fsl,imx93-src-slice" },
	{ }
};
MODULE_DEVICE_TABLE(of, imx93_pd_ids);

static struct platform_driver imx93_power_domain_driver = {
	.driver = {
		.name	= "imx93_power_domain",
		.owner	= THIS_MODULE,
		.of_match_table = imx93_pd_ids,
	},
	.probe = imx93_pd_probe,
	.remove = imx93_pd_remove,
};
module_platform_driver(imx93_power_domain_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX93 power domain driver");
MODULE_LICENSE("GPL");
