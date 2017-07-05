/*
 * Actions Semi Owl Smart Power System (SPS)
 *
 * Copyright 2012 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * Copyright (c) 2017 Andreas Färber
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_domain.h>
#include <linux/soc/actions/owl-sps.h>
#include <dt-bindings/power/owl-s500-powergate.h>

struct owl_sps_domain_info {
	const char *name;
	int pwr_bit;
	int ack_bit;
	unsigned int genpd_flags;
};

struct owl_sps_info {
	unsigned num_domains;
	const struct owl_sps_domain_info *domains;
};

struct owl_sps {
	struct device *dev;
	const struct owl_sps_info *info;
	void __iomem *base;
	struct genpd_onecell_data genpd_data;
	struct generic_pm_domain *domains[];
};

#define to_owl_pd(gpd) container_of(gpd, struct owl_sps_domain, genpd)

struct owl_sps_domain {
	struct generic_pm_domain genpd;
	const struct owl_sps_domain_info *info;
	struct owl_sps *sps;
};

static int owl_sps_set_power(struct owl_sps_domain *pd, bool enable)
{
	u32 pwr_mask, ack_mask;

	ack_mask = BIT(pd->info->ack_bit);
	pwr_mask = BIT(pd->info->pwr_bit);

	return owl_sps_set_pg(pd->sps->base, pwr_mask, ack_mask, enable);
}

static int owl_sps_power_on(struct generic_pm_domain *domain)
{
	struct owl_sps_domain *pd = to_owl_pd(domain);

	dev_dbg(pd->sps->dev, "%s power on", pd->info->name);

	return owl_sps_set_power(pd, true);
}

static int owl_sps_power_off(struct generic_pm_domain *domain)
{
	struct owl_sps_domain *pd = to_owl_pd(domain);

	dev_dbg(pd->sps->dev, "%s power off", pd->info->name);

	return owl_sps_set_power(pd, false);
}

static int owl_sps_init_domain(struct owl_sps *sps, int index)
{
	struct owl_sps_domain *pd;

	pd = devm_kzalloc(sps->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->info = &sps->info->domains[index];
	pd->sps = sps;

	pd->genpd.name = pd->info->name;
	pd->genpd.power_on = owl_sps_power_on;
	pd->genpd.power_off = owl_sps_power_off;
	pd->genpd.flags = pd->info->genpd_flags;
	pm_genpd_init(&pd->genpd, NULL, false);

	sps->genpd_data.domains[index] = &pd->genpd;

	return 0;
}

static int owl_sps_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct owl_sps_info *sps_info;
	struct owl_sps *sps;
	int i, ret;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "no device node\n");
		return -ENODEV;
	}

	match = of_match_device(pdev->dev.driver->of_match_table, &pdev->dev);
	if (!match || !match->data) {
		dev_err(&pdev->dev, "unknown compatible or missing data\n");
		return -EINVAL;
	}

	sps_info = match->data;

	sps = devm_kzalloc(&pdev->dev, sizeof(*sps) +
			   sps_info->num_domains * sizeof(sps->domains[0]),
			   GFP_KERNEL);
	if (!sps)
		return -ENOMEM;

	sps->base = of_io_request_and_map(pdev->dev.of_node, 0, "owl-sps");
	if (IS_ERR(sps->base)) {
		dev_err(&pdev->dev, "failed to map sps registers\n");
		return PTR_ERR(sps->base);
	}

	sps->dev = &pdev->dev;
	sps->info = sps_info;
	sps->genpd_data.domains = sps->domains;
	sps->genpd_data.num_domains = sps_info->num_domains;

	for (i = 0; i < sps_info->num_domains; i++) {
		ret = owl_sps_init_domain(sps, i);
		if (ret)
			return ret;
	}

	ret = of_genpd_add_provider_onecell(pdev->dev.of_node, &sps->genpd_data);
	if (ret) {
		dev_err(&pdev->dev, "failed to add provider (%d)", ret);
		return ret;
	}

	return 0;
}

static const struct owl_sps_domain_info s500_sps_domains[] = {
	[S500_PD_VDE] = {
		.name = "VDE",
		.pwr_bit = 0,
		.ack_bit = 16,
	},
	[S500_PD_VCE_SI] = {
		.name = "VCE_SI",
		.pwr_bit = 1,
		.ack_bit = 17,
	},
	[S500_PD_USB2_1] = {
		.name = "USB2_1",
		.pwr_bit = 2,
		.ack_bit = 18,
	},
	[S500_PD_CPU2] = {
		.name = "CPU2",
		.pwr_bit = 5,
		.ack_bit = 21,
		.genpd_flags = GENPD_FLAG_ALWAYS_ON,
	},
	[S500_PD_CPU3] = {
		.name = "CPU3",
		.pwr_bit = 6,
		.ack_bit = 22,
		.genpd_flags = GENPD_FLAG_ALWAYS_ON,
	},
	[S500_PD_DMA] = {
		.name = "DMA",
		.pwr_bit = 8,
		.ack_bit = 12,
	},
	[S500_PD_DS] = {
		.name = "DS",
		.pwr_bit = 9,
		.ack_bit = 13,
	},
	[S500_PD_USB3] = {
		.name = "USB3",
		.pwr_bit = 10,
		.ack_bit = 14,
	},
	[S500_PD_USB2_0] = {
		.name = "USB2_0",
		.pwr_bit = 11,
		.ack_bit = 15,
	},
};

static const struct owl_sps_info s500_sps_info = {
	.num_domains = ARRAY_SIZE(s500_sps_domains),
	.domains = s500_sps_domains,
};

static const struct of_device_id owl_sps_of_matches[] = {
	{ .compatible = "actions,s500-sps", .data = &s500_sps_info },
	{ }
};

static struct platform_driver owl_sps_platform_driver = {
	.probe = owl_sps_probe,
	.driver = {
		.name = "owl-sps",
		.of_match_table = owl_sps_of_matches,
		.suppress_bind_attrs = true,
	},
};

static int __init owl_sps_init(void)
{
	return platform_driver_register(&owl_sps_platform_driver);
}
postcore_initcall(owl_sps_init);
