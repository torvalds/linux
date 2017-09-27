/*
 * Copyright (C) 2015 Linaro Ltd.
 *
 * Author: Jun Nie <jun.nie@linaro.org>
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>

#define PCU_DM_CLKEN        0x18
#define PCU_DM_RSTEN        0x1C
#define PCU_DM_ISOEN        0x20
#define PCU_DM_PWRDN        0x24
#define PCU_DM_ACK_SYNC     0x28

enum {
	PCU_DM_NEON0 = 0,
	PCU_DM_NEON1,
	PCU_DM_GPU,
	PCU_DM_DECPPU,
	PCU_DM_VOU,
	PCU_DM_R2D,
	PCU_DM_TOP,
};

static void __iomem *pcubase;

struct zx_pm_domain {
	struct generic_pm_domain dm;
	unsigned int bit;
};

static int normal_power_off(struct generic_pm_domain *domain)
{
	struct zx_pm_domain *zpd = (struct zx_pm_domain *)domain;
	unsigned long loop = 1000;
	u32 tmp;

	tmp = readl_relaxed(pcubase + PCU_DM_CLKEN);
	tmp &= ~BIT(zpd->bit);
	writel_relaxed(tmp, pcubase + PCU_DM_CLKEN);
	udelay(5);

	tmp = readl_relaxed(pcubase + PCU_DM_ISOEN);
	tmp &= ~BIT(zpd->bit);
	writel_relaxed(tmp | BIT(zpd->bit), pcubase + PCU_DM_ISOEN);
	udelay(5);

	tmp = readl_relaxed(pcubase + PCU_DM_RSTEN);
	tmp &= ~BIT(zpd->bit);
	writel_relaxed(tmp, pcubase + PCU_DM_RSTEN);
	udelay(5);

	tmp = readl_relaxed(pcubase + PCU_DM_PWRDN);
	tmp &= ~BIT(zpd->bit);
	writel_relaxed(tmp | BIT(zpd->bit), pcubase + PCU_DM_PWRDN);
	do {
		tmp = readl_relaxed(pcubase + PCU_DM_ACK_SYNC) & BIT(zpd->bit);
	} while (--loop && !tmp);

	if (!loop) {
		pr_err("Error: %s %s fail\n", __func__, domain->name);
		return -EIO;
	}

	return 0;
}

static int normal_power_on(struct generic_pm_domain *domain)
{
	struct zx_pm_domain *zpd = (struct zx_pm_domain *)domain;
	unsigned long loop = 10000;
	u32 tmp;

	tmp = readl_relaxed(pcubase + PCU_DM_PWRDN);
	tmp &= ~BIT(zpd->bit);
	writel_relaxed(tmp, pcubase + PCU_DM_PWRDN);
	do {
		tmp = readl_relaxed(pcubase + PCU_DM_ACK_SYNC) & BIT(zpd->bit);
	} while (--loop && tmp);

	if (!loop) {
		pr_err("Error: %s %s fail\n", __func__, domain->name);
		return -EIO;
	}

	tmp = readl_relaxed(pcubase + PCU_DM_RSTEN);
	tmp &= ~BIT(zpd->bit);
	writel_relaxed(tmp | BIT(zpd->bit), pcubase + PCU_DM_RSTEN);
	udelay(5);

	tmp = readl_relaxed(pcubase + PCU_DM_ISOEN);
	tmp &= ~BIT(zpd->bit);
	writel_relaxed(tmp, pcubase + PCU_DM_ISOEN);
	udelay(5);

	tmp = readl_relaxed(pcubase + PCU_DM_CLKEN);
	tmp &= ~BIT(zpd->bit);
	writel_relaxed(tmp | BIT(zpd->bit), pcubase + PCU_DM_CLKEN);
	udelay(5);
	return 0;
}

static struct zx_pm_domain gpu_domain = {
	.dm = {
		.name		= "gpu_domain",
		.power_off	= normal_power_off,
		.power_on	= normal_power_on,
	},
	.bit = PCU_DM_GPU,
};

static struct zx_pm_domain decppu_domain = {
	.dm = {
		.name		= "decppu_domain",
		.power_off	= normal_power_off,
		.power_on	= normal_power_on,
	},
	.bit = PCU_DM_DECPPU,
};

static struct zx_pm_domain vou_domain = {
	.dm = {
		.name		= "vou_domain",
		.power_off	= normal_power_off,
		.power_on	= normal_power_on,
	},
	.bit = PCU_DM_VOU,
};

static struct zx_pm_domain r2d_domain = {
	.dm = {
		.name		= "r2d_domain",
		.power_off	= normal_power_off,
		.power_on	= normal_power_on,
	},
	.bit = PCU_DM_R2D,
};

static struct generic_pm_domain *zx296702_pm_domains[] = {
	&vou_domain.dm,
	&gpu_domain.dm,
	&decppu_domain.dm,
	&r2d_domain.dm,
};

static int zx296702_pd_probe(struct platform_device *pdev)
{
	struct genpd_onecell_data *genpd_data;
	struct resource *res;
	int i;

	genpd_data = devm_kzalloc(&pdev->dev, sizeof(*genpd_data), GFP_KERNEL);
	if (!genpd_data)
		return -ENOMEM;

	genpd_data->domains = zx296702_pm_domains;
	genpd_data->num_domains = ARRAY_SIZE(zx296702_pm_domains);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}

	pcubase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pcubase)) {
		dev_err(&pdev->dev, "ioremap fail.\n");
		return -EIO;
	}

	for (i = 0; i < ARRAY_SIZE(zx296702_pm_domains); ++i)
		pm_genpd_init(zx296702_pm_domains[i], NULL, false);

	of_genpd_add_provider_onecell(pdev->dev.of_node, genpd_data);
	return 0;
}

static const struct of_device_id zx296702_pm_domain_matches[] __initconst = {
	{ .compatible = "zte,zx296702-pcu", },
	{ },
};

static struct platform_driver zx296702_pd_driver __initdata = {
	.driver = {
		.name = "zx-powerdomain",
		.owner = THIS_MODULE,
		.of_match_table = zx296702_pm_domain_matches,
	},
	.probe = zx296702_pd_probe,
};

static int __init zx296702_pd_init(void)
{
	return platform_driver_register(&zx296702_pd_driver);
}
subsys_initcall(zx296702_pd_init);
