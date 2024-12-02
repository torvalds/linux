// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI SCI Generic Power Domain Driver
 *
 * Copyright (C) 2015-2017 Texas Instruments Incorporated - http://www.ti.com/
 *	J Keerthy <j-keerthy@ti.com>
 *	Dave Gerlach <d-gerlach@ti.com>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/soc/ti/ti_sci_protocol.h>
#include <dt-bindings/soc/ti,sci_pm_domain.h>

/**
 * struct ti_sci_genpd_provider: holds common TI SCI genpd provider data
 * @ti_sci: handle to TI SCI protocol driver that provides ops to
 *	    communicate with system control processor.
 * @dev: pointer to dev for the driver for devm allocs
 * @pd_list: list of all the power domains on the device
 * @data: onecell data for genpd core
 */
struct ti_sci_genpd_provider {
	const struct ti_sci_handle *ti_sci;
	struct device *dev;
	struct list_head pd_list;
	struct genpd_onecell_data data;
};

/**
 * struct ti_sci_pm_domain: TI specific data needed for power domain
 * @idx: index of the device that identifies it with the system
 *	 control processor.
 * @exclusive: Permissions for exclusive request or shared request of the
 *	       device.
 * @pd: generic_pm_domain for use with the genpd framework
 * @node: link for the genpd list
 * @parent: link to the parent TI SCI genpd provider
 */
struct ti_sci_pm_domain {
	int idx;
	u8 exclusive;
	struct generic_pm_domain pd;
	struct list_head node;
	struct ti_sci_genpd_provider *parent;
};

#define genpd_to_ti_sci_pd(gpd) container_of(gpd, struct ti_sci_pm_domain, pd)

/*
 * ti_sci_pd_power_off(): genpd power down hook
 * @domain: pointer to the powerdomain to power off
 */
static int ti_sci_pd_power_off(struct generic_pm_domain *domain)
{
	struct ti_sci_pm_domain *pd = genpd_to_ti_sci_pd(domain);
	const struct ti_sci_handle *ti_sci = pd->parent->ti_sci;

	return ti_sci->ops.dev_ops.put_device(ti_sci, pd->idx);
}

/*
 * ti_sci_pd_power_on(): genpd power up hook
 * @domain: pointer to the powerdomain to power on
 */
static int ti_sci_pd_power_on(struct generic_pm_domain *domain)
{
	struct ti_sci_pm_domain *pd = genpd_to_ti_sci_pd(domain);
	const struct ti_sci_handle *ti_sci = pd->parent->ti_sci;

	if (pd->exclusive)
		return ti_sci->ops.dev_ops.get_device_exclusive(ti_sci,
								pd->idx);
	else
		return ti_sci->ops.dev_ops.get_device(ti_sci, pd->idx);
}

/*
 * ti_sci_pd_xlate(): translation service for TI SCI genpds
 * @genpdspec: DT identification data for the genpd
 * @data: genpd core data for all the powerdomains on the device
 */
static struct generic_pm_domain *ti_sci_pd_xlate(
					struct of_phandle_args *genpdspec,
					void *data)
{
	struct genpd_onecell_data *genpd_data = data;
	unsigned int idx = genpdspec->args[0];

	if (genpdspec->args_count != 1 && genpdspec->args_count != 2)
		return ERR_PTR(-EINVAL);

	if (idx >= genpd_data->num_domains) {
		pr_err("%s: invalid domain index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	if (!genpd_data->domains[idx])
		return ERR_PTR(-ENOENT);

	genpd_to_ti_sci_pd(genpd_data->domains[idx])->exclusive =
		genpdspec->args[1];

	return genpd_data->domains[idx];
}

static const struct of_device_id ti_sci_pm_domain_matches[] = {
	{ .compatible = "ti,sci-pm-domain", },
	{ },
};
MODULE_DEVICE_TABLE(of, ti_sci_pm_domain_matches);

static bool ti_sci_pm_idx_exists(struct ti_sci_genpd_provider *pd_provider, u32 idx)
{
	struct ti_sci_pm_domain *pd;

	list_for_each_entry(pd, &pd_provider->pd_list, node) {
		if (pd->idx == idx)
			return true;
	}

	return false;
}

static int ti_sci_pm_domain_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ti_sci_genpd_provider *pd_provider;
	struct ti_sci_pm_domain *pd;
	struct device_node *np = NULL;
	struct of_phandle_args args;
	int ret;
	u32 max_id = 0;
	int index;

	pd_provider = devm_kzalloc(dev, sizeof(*pd_provider), GFP_KERNEL);
	if (!pd_provider)
		return -ENOMEM;

	pd_provider->ti_sci = devm_ti_sci_get_handle(dev);
	if (IS_ERR(pd_provider->ti_sci))
		return PTR_ERR(pd_provider->ti_sci);

	pd_provider->dev = dev;

	INIT_LIST_HEAD(&pd_provider->pd_list);

	/* Find highest device ID used for power domains */
	while (1) {
		np = of_find_node_with_property(np, "power-domains");
		if (!np)
			break;

		index = 0;

		while (1) {
			ret = of_parse_phandle_with_args(np, "power-domains",
							 "#power-domain-cells",
							 index, &args);
			if (ret)
				break;

			if (args.args_count >= 1 && args.np == dev->of_node) {
				if (args.args[0] > max_id) {
					max_id = args.args[0];
				} else {
					if (ti_sci_pm_idx_exists(pd_provider, args.args[0])) {
						index++;
						continue;
					}
				}

				pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
				if (!pd)
					return -ENOMEM;

				pd->pd.name = devm_kasprintf(dev, GFP_KERNEL,
							     "pd:%d",
							     args.args[0]);
				if (!pd->pd.name)
					return -ENOMEM;

				pd->pd.power_off = ti_sci_pd_power_off;
				pd->pd.power_on = ti_sci_pd_power_on;
				pd->idx = args.args[0];
				pd->parent = pd_provider;

				pm_genpd_init(&pd->pd, NULL, true);

				list_add(&pd->node, &pd_provider->pd_list);
			}
			index++;
		}
	}

	pd_provider->data.domains =
		devm_kcalloc(dev, max_id + 1,
			     sizeof(*pd_provider->data.domains),
			     GFP_KERNEL);
	if (!pd_provider->data.domains)
		return -ENOMEM;

	pd_provider->data.num_domains = max_id + 1;
	pd_provider->data.xlate = ti_sci_pd_xlate;

	list_for_each_entry(pd, &pd_provider->pd_list, node)
		pd_provider->data.domains[pd->idx] = &pd->pd;

	return of_genpd_add_provider_onecell(dev->of_node, &pd_provider->data);
}

static struct platform_driver ti_sci_pm_domains_driver = {
	.probe = ti_sci_pm_domain_probe,
	.driver = {
		.name = "ti_sci_pm_domains",
		.of_match_table = ti_sci_pm_domain_matches,
	},
};
module_platform_driver(ti_sci_pm_domains_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI System Control Interface (SCI) Power Domain driver");
MODULE_AUTHOR("Dave Gerlach");
