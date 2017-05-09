/*
 * TI SCI Generic Power Domain Driver
 *
 * Copyright (C) 2015-2017 Texas Instruments Incorporated - http://www.ti.com/
 *	J Keerthy <j-keerthy@ti.com>
 *	Dave Gerlach <d-gerlach@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/soc/ti/ti_sci_protocol.h>

/**
 * struct ti_sci_genpd_dev_data: holds data needed for every device attached
 *				 to this genpd
 * @idx: index of the device that identifies it with the system
 *	 control processor.
 */
struct ti_sci_genpd_dev_data {
	int idx;
};

/**
 * struct ti_sci_pm_domain: TI specific data needed for power domain
 * @ti_sci: handle to TI SCI protocol driver that provides ops to
 *	    communicate with system control processor.
 * @dev: pointer to dev for the driver for devm allocs
 * @pd: generic_pm_domain for use with the genpd framework
 */
struct ti_sci_pm_domain {
	const struct ti_sci_handle *ti_sci;
	struct device *dev;
	struct generic_pm_domain pd;
};

#define genpd_to_ti_sci_pd(gpd) container_of(gpd, struct ti_sci_pm_domain, pd)

/**
 * ti_sci_dev_id(): get prepopulated ti_sci id from struct dev
 * @dev: pointer to device associated with this genpd
 *
 * Returns device_id stored from ti,sci_id property
 */
static int ti_sci_dev_id(struct device *dev)
{
	struct generic_pm_domain_data *genpd_data = dev_gpd_data(dev);
	struct ti_sci_genpd_dev_data *sci_dev_data = genpd_data->data;

	return sci_dev_data->idx;
}

/**
 * ti_sci_dev_to_sci_handle(): get pointer to ti_sci_handle
 * @dev: pointer to device associated with this genpd
 *
 * Returns ti_sci_handle to be used to communicate with system
 *	   control processor.
 */
static const struct ti_sci_handle *ti_sci_dev_to_sci_handle(struct device *dev)
{
	struct generic_pm_domain *pd = pd_to_genpd(dev->pm_domain);
	struct ti_sci_pm_domain *ti_sci_genpd = genpd_to_ti_sci_pd(pd);

	return ti_sci_genpd->ti_sci;
}

/**
 * ti_sci_dev_start(): genpd device start hook called to turn device on
 * @dev: pointer to device associated with this genpd to be powered on
 */
static int ti_sci_dev_start(struct device *dev)
{
	const struct ti_sci_handle *ti_sci = ti_sci_dev_to_sci_handle(dev);
	int idx = ti_sci_dev_id(dev);

	return ti_sci->ops.dev_ops.get_device(ti_sci, idx);
}

/**
 * ti_sci_dev_stop(): genpd device stop hook called to turn device off
 * @dev: pointer to device associated with this genpd to be powered off
 */
static int ti_sci_dev_stop(struct device *dev)
{
	const struct ti_sci_handle *ti_sci = ti_sci_dev_to_sci_handle(dev);
	int idx = ti_sci_dev_id(dev);

	return ti_sci->ops.dev_ops.put_device(ti_sci, idx);
}

static int ti_sci_pd_attach_dev(struct generic_pm_domain *domain,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args pd_args;
	struct ti_sci_pm_domain *ti_sci_genpd = genpd_to_ti_sci_pd(domain);
	const struct ti_sci_handle *ti_sci = ti_sci_genpd->ti_sci;
	struct ti_sci_genpd_dev_data *sci_dev_data;
	struct generic_pm_domain_data *genpd_data;
	int idx, ret = 0;

	ret = of_parse_phandle_with_args(np, "power-domains",
					 "#power-domain-cells", 0, &pd_args);
	if (ret < 0)
		return ret;

	if (pd_args.args_count != 1)
		return -EINVAL;

	idx = pd_args.args[0];

	/*
	 * Check the validity of the requested idx, if the index is not valid
	 * the PMMC will return a NAK here and we will not allocate it.
	 */
	ret = ti_sci->ops.dev_ops.is_valid(ti_sci, idx);
	if (ret)
		return -EINVAL;

	sci_dev_data = kzalloc(sizeof(*sci_dev_data), GFP_KERNEL);
	if (!sci_dev_data)
		return -ENOMEM;

	sci_dev_data->idx = idx;

	genpd_data = dev_gpd_data(dev);
	genpd_data->data = sci_dev_data;

	return 0;
}

static void ti_sci_pd_detach_dev(struct generic_pm_domain *domain,
				 struct device *dev)
{
	struct generic_pm_domain_data *genpd_data = dev_gpd_data(dev);
	struct ti_sci_genpd_dev_data *sci_dev_data = genpd_data->data;

	kfree(sci_dev_data);
	genpd_data->data = NULL;
}

static const struct of_device_id ti_sci_pm_domain_matches[] = {
	{ .compatible = "ti,sci-pm-domain", },
	{ },
};
MODULE_DEVICE_TABLE(of, ti_sci_pm_domain_matches);

static int ti_sci_pm_domain_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ti_sci_pm_domain *ti_sci_pd;
	int ret;

	ti_sci_pd = devm_kzalloc(dev, sizeof(*ti_sci_pd), GFP_KERNEL);
	if (!ti_sci_pd)
		return -ENOMEM;

	ti_sci_pd->ti_sci = devm_ti_sci_get_handle(dev);
	if (IS_ERR(ti_sci_pd->ti_sci))
		return PTR_ERR(ti_sci_pd->ti_sci);

	ti_sci_pd->dev = dev;

	ti_sci_pd->pd.attach_dev = ti_sci_pd_attach_dev;
	ti_sci_pd->pd.detach_dev = ti_sci_pd_detach_dev;

	ti_sci_pd->pd.dev_ops.start = ti_sci_dev_start;
	ti_sci_pd->pd.dev_ops.stop = ti_sci_dev_stop;

	pm_genpd_init(&ti_sci_pd->pd, NULL, true);

	ret = of_genpd_add_provider_simple(np, &ti_sci_pd->pd);

	return ret;
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
