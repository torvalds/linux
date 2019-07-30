// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Linaro Ltd.
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * Implements PM domains using the generic PM domain for ux500.
 */
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/pm_domain.h>

#include <dt-bindings/arm/ux500_pm_domains.h>
#include "pm_domains.h"

static int pd_power_off(struct generic_pm_domain *domain)
{
	/*
	 * Handle the gating of the PM domain regulator here.
	 *
	 * Drivers/subsystems handling devices in the PM domain needs to perform
	 * register context save/restore from their respective runtime PM
	 * callbacks, to be able to enable PM domain gating/ungating.
	 */
	return 0;
}

static int pd_power_on(struct generic_pm_domain *domain)
{
	/*
	 * Handle the ungating of the PM domain regulator here.
	 *
	 * Drivers/subsystems handling devices in the PM domain needs to perform
	 * register context save/restore from their respective runtime PM
	 * callbacks, to be able to enable PM domain gating/ungating.
	 */
	return 0;
}

static struct generic_pm_domain ux500_pm_domain_vape = {
	.name = "VAPE",
	.power_off = pd_power_off,
	.power_on = pd_power_on,
};

static struct generic_pm_domain *ux500_pm_domains[NR_DOMAINS] = {
	[DOMAIN_VAPE] = &ux500_pm_domain_vape,
};

static const struct of_device_id ux500_pm_domain_matches[] __initconst = {
	{ .compatible = "stericsson,ux500-pm-domains", },
	{ },
};

int __init ux500_pm_domains_init(void)
{
	struct device_node *np;
	struct genpd_onecell_data *genpd_data;
	int i;

	np = of_find_matching_node(NULL, ux500_pm_domain_matches);
	if (!np)
		return -ENODEV;

	genpd_data = kzalloc(sizeof(*genpd_data), GFP_KERNEL);
	if (!genpd_data)
		return -ENOMEM;

	genpd_data->domains = ux500_pm_domains;
	genpd_data->num_domains = ARRAY_SIZE(ux500_pm_domains);

	for (i = 0; i < ARRAY_SIZE(ux500_pm_domains); ++i)
		pm_genpd_init(ux500_pm_domains[i], NULL, false);

	of_genpd_add_provider_onecell(np, genpd_data);
	return 0;
}
