// SPDX-License-Identifier: GPL-2.0
/*
 * Simple Power-Managed Bus Driver
 *
 * Copyright (C) 2014-2015 Glider bvba
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

struct simple_pm_bus {
	struct clk_bulk_data *clks;
	int num_clks;
};

static int simple_pm_bus_probe(struct platform_device *pdev)
{
	const struct device *dev = &pdev->dev;
	const struct of_dev_auxdata *lookup = dev_get_platdata(dev);
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;
	struct simple_pm_bus *bus;

	/*
	 * Allow user to use driver_override to bind this driver to a
	 * transparent bus device which has a different compatible string
	 * that's not listed in simple_pm_bus_of_match. We don't want to do any
	 * of the simple-pm-bus tasks for these devices, so return early.
	 */
	if (pdev->driver_override)
		return 0;

	match = of_match_device(dev->driver->of_match_table, dev);
	/*
	 * These are transparent bus devices (not simple-pm-bus matches) that
	 * have their child nodes populated automatically.  So, don't need to
	 * do anything more. We only match with the device if this driver is
	 * the most specific match because we don't want to incorrectly bind to
	 * a device that has a more specific driver.
	 */
	if (match && match->data) {
		if (of_property_match_string(np, "compatible", match->compatible) == 0)
			return 0;
		else
			return -ENODEV;
	}

	bus = devm_kzalloc(&pdev->dev, sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;

	bus->num_clks = devm_clk_bulk_get_all(&pdev->dev, &bus->clks);
	if (bus->num_clks < 0)
		return dev_err_probe(&pdev->dev, bus->num_clks, "failed to get clocks\n");

	dev_set_drvdata(&pdev->dev, bus);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	pm_runtime_enable(&pdev->dev);

	if (np)
		of_platform_populate(np, NULL, lookup, &pdev->dev);

	return 0;
}

static int simple_pm_bus_remove(struct platform_device *pdev)
{
	const void *data = of_device_get_match_data(&pdev->dev);

	if (pdev->driver_override || data)
		return 0;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int simple_pm_bus_runtime_suspend(struct device *dev)
{
	struct simple_pm_bus *bus = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(bus->num_clks, bus->clks);

	return 0;
}

static int simple_pm_bus_runtime_resume(struct device *dev)
{
	struct simple_pm_bus *bus = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(bus->num_clks, bus->clks);
	if (ret) {
		dev_err(dev, "failed to enable clocks: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops simple_pm_bus_pm_ops = {
	RUNTIME_PM_OPS(simple_pm_bus_runtime_suspend, simple_pm_bus_runtime_resume, NULL)
	NOIRQ_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

#define ONLY_BUS	((void *) 1) /* Match if the device is only a bus. */

static const struct of_device_id simple_pm_bus_of_match[] = {
	{ .compatible = "simple-pm-bus", },
	{ .compatible = "simple-bus",	.data = ONLY_BUS },
	{ .compatible = "simple-mfd",	.data = ONLY_BUS },
	{ .compatible = "isa",		.data = ONLY_BUS },
	{ .compatible = "arm,amba-bus",	.data = ONLY_BUS },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, simple_pm_bus_of_match);

static struct platform_driver simple_pm_bus_driver = {
	.probe = simple_pm_bus_probe,
	.remove = simple_pm_bus_remove,
	.driver = {
		.name = "simple-pm-bus",
		.of_match_table = simple_pm_bus_of_match,
		.pm = pm_ptr(&simple_pm_bus_pm_ops),
	},
};

module_platform_driver(simple_pm_bus_driver);

MODULE_DESCRIPTION("Simple Power-Managed Bus Driver");
MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
