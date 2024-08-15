// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic Exynos Bus frequency driver with DEVFREQ Framework
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 * Author : Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This driver support Exynos Bus frequency feature by using
 * DEVFREQ framework and is based on drivers/devfreq/exynos/exynos4_bus.c.
 */

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/devfreq-event.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define DEFAULT_SATURATION_RATIO	40

struct exynos_bus {
	struct device *dev;
	struct platform_device *icc_pdev;

	struct devfreq *devfreq;
	struct devfreq_event_dev **edev;
	unsigned int edev_count;
	struct mutex lock;

	unsigned long curr_freq;

	int opp_token;
	struct clk *clk;
	unsigned int ratio;
};

/*
 * Control the devfreq-event device to get the current state of bus
 */
#define exynos_bus_ops_edev(ops)				\
static int exynos_bus_##ops(struct exynos_bus *bus)		\
{								\
	int i, ret;						\
								\
	for (i = 0; i < bus->edev_count; i++) {			\
		if (!bus->edev[i])				\
			continue;				\
		ret = devfreq_event_##ops(bus->edev[i]);	\
		if (ret < 0)					\
			return ret;				\
	}							\
								\
	return 0;						\
}
exynos_bus_ops_edev(enable_edev);
exynos_bus_ops_edev(disable_edev);
exynos_bus_ops_edev(set_event);

static int exynos_bus_get_event(struct exynos_bus *bus,
				struct devfreq_event_data *edata)
{
	struct devfreq_event_data event_data;
	unsigned long load_count = 0, total_count = 0;
	int i, ret = 0;

	for (i = 0; i < bus->edev_count; i++) {
		if (!bus->edev[i])
			continue;

		ret = devfreq_event_get_event(bus->edev[i], &event_data);
		if (ret < 0)
			return ret;

		if (i == 0 || event_data.load_count > load_count) {
			load_count = event_data.load_count;
			total_count = event_data.total_count;
		}
	}

	edata->load_count = load_count;
	edata->total_count = total_count;

	return ret;
}

/*
 * devfreq function for both simple-ondemand and passive governor
 */
static int exynos_bus_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct exynos_bus *bus = dev_get_drvdata(dev);
	struct dev_pm_opp *new_opp;
	int ret = 0;

	/* Get correct frequency for bus. */
	new_opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(new_opp)) {
		dev_err(dev, "failed to get recommended opp instance\n");
		return PTR_ERR(new_opp);
	}

	dev_pm_opp_put(new_opp);

	/* Change voltage and frequency according to new OPP level */
	mutex_lock(&bus->lock);
	ret = dev_pm_opp_set_rate(dev, *freq);
	if (!ret)
		bus->curr_freq = *freq;

	mutex_unlock(&bus->lock);

	return ret;
}

static int exynos_bus_get_dev_status(struct device *dev,
				     struct devfreq_dev_status *stat)
{
	struct exynos_bus *bus = dev_get_drvdata(dev);
	struct devfreq_event_data edata;
	int ret;

	stat->current_frequency = bus->curr_freq;

	ret = exynos_bus_get_event(bus, &edata);
	if (ret < 0) {
		dev_err(dev, "failed to get event from devfreq-event devices\n");
		stat->total_time = stat->busy_time = 0;
		goto err;
	}

	stat->busy_time = (edata.load_count * 100) / bus->ratio;
	stat->total_time = edata.total_count;

	dev_dbg(dev, "Usage of devfreq-event : %lu/%lu\n", stat->busy_time,
							stat->total_time);

err:
	ret = exynos_bus_set_event(bus);
	if (ret < 0) {
		dev_err(dev, "failed to set event to devfreq-event devices\n");
		return ret;
	}

	return ret;
}

static void exynos_bus_exit(struct device *dev)
{
	struct exynos_bus *bus = dev_get_drvdata(dev);
	int ret;

	ret = exynos_bus_disable_edev(bus);
	if (ret < 0)
		dev_warn(dev, "failed to disable the devfreq-event devices\n");

	platform_device_unregister(bus->icc_pdev);

	dev_pm_opp_of_remove_table(dev);
	clk_disable_unprepare(bus->clk);
	dev_pm_opp_put_regulators(bus->opp_token);
}

static void exynos_bus_passive_exit(struct device *dev)
{
	struct exynos_bus *bus = dev_get_drvdata(dev);

	platform_device_unregister(bus->icc_pdev);

	dev_pm_opp_of_remove_table(dev);
	clk_disable_unprepare(bus->clk);
}

static int exynos_bus_parent_parse_of(struct device_node *np,
					struct exynos_bus *bus)
{
	struct device *dev = bus->dev;
	const char *supplies[] = { "vdd", NULL };
	int i, ret, count, size;

	ret = dev_pm_opp_set_regulators(dev, supplies);
	if (ret < 0) {
		dev_err(dev, "failed to set regulators %d\n", ret);
		return ret;
	}

	bus->opp_token = ret;

	/*
	 * Get the devfreq-event devices to get the current utilization of
	 * buses. This raw data will be used in devfreq ondemand governor.
	 */
	count = devfreq_event_get_edev_count(dev, "devfreq-events");
	if (count < 0) {
		dev_err(dev, "failed to get the count of devfreq-event dev\n");
		ret = count;
		goto err_regulator;
	}
	bus->edev_count = count;

	size = sizeof(*bus->edev) * count;
	bus->edev = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!bus->edev) {
		ret = -ENOMEM;
		goto err_regulator;
	}

	for (i = 0; i < count; i++) {
		bus->edev[i] = devfreq_event_get_edev_by_phandle(dev,
							"devfreq-events", i);
		if (IS_ERR(bus->edev[i])) {
			ret = -EPROBE_DEFER;
			goto err_regulator;
		}
	}

	/*
	 * Optionally, Get the saturation ratio according to Exynos SoC
	 * When measuring the utilization of each AXI bus with devfreq-event
	 * devices, the measured real cycle might be much lower than the
	 * total cycle of bus during sampling rate. In result, the devfreq
	 * simple-ondemand governor might not decide to change the current
	 * frequency due to too utilization (= real cycle/total cycle).
	 * So, this property is used to adjust the utilization when calculating
	 * the busy_time in exynos_bus_get_dev_status().
	 */
	if (of_property_read_u32(np, "exynos,saturation-ratio", &bus->ratio))
		bus->ratio = DEFAULT_SATURATION_RATIO;

	return 0;

err_regulator:
	dev_pm_opp_put_regulators(bus->opp_token);

	return ret;
}

static int exynos_bus_parse_of(struct device_node *np,
			      struct exynos_bus *bus)
{
	struct device *dev = bus->dev;
	struct dev_pm_opp *opp;
	unsigned long rate;
	int ret;

	/* Get the clock to provide each bus with source clock */
	bus->clk = devm_clk_get(dev, "bus");
	if (IS_ERR(bus->clk)) {
		dev_err(dev, "failed to get bus clock\n");
		return PTR_ERR(bus->clk);
	}

	ret = clk_prepare_enable(bus->clk);
	if (ret < 0) {
		dev_err(dev, "failed to get enable clock\n");
		return ret;
	}

	/* Get the freq and voltage from OPP table to scale the bus freq */
	ret = dev_pm_opp_of_add_table(dev);
	if (ret < 0) {
		dev_err(dev, "failed to get OPP table\n");
		goto err_clk;
	}

	rate = clk_get_rate(bus->clk);

	opp = devfreq_recommended_opp(dev, &rate, 0);
	if (IS_ERR(opp)) {
		dev_err(dev, "failed to find dev_pm_opp\n");
		ret = PTR_ERR(opp);
		goto err_opp;
	}
	bus->curr_freq = dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);

	return 0;

err_opp:
	dev_pm_opp_of_remove_table(dev);
err_clk:
	clk_disable_unprepare(bus->clk);

	return ret;
}

static int exynos_bus_profile_init(struct exynos_bus *bus,
				   struct devfreq_dev_profile *profile)
{
	struct device *dev = bus->dev;
	struct devfreq_simple_ondemand_data *ondemand_data;
	int ret;

	/* Initialize the struct profile and governor data for parent device */
	profile->polling_ms = 50;
	profile->target = exynos_bus_target;
	profile->get_dev_status = exynos_bus_get_dev_status;
	profile->exit = exynos_bus_exit;

	ondemand_data = devm_kzalloc(dev, sizeof(*ondemand_data), GFP_KERNEL);
	if (!ondemand_data)
		return -ENOMEM;

	ondemand_data->upthreshold = 40;
	ondemand_data->downdifferential = 5;

	/* Add devfreq device to monitor and handle the exynos bus */
	bus->devfreq = devm_devfreq_add_device(dev, profile,
						DEVFREQ_GOV_SIMPLE_ONDEMAND,
						ondemand_data);
	if (IS_ERR(bus->devfreq)) {
		dev_err(dev, "failed to add devfreq device\n");
		return PTR_ERR(bus->devfreq);
	}

	/* Register opp_notifier to catch the change of OPP  */
	ret = devm_devfreq_register_opp_notifier(dev, bus->devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to register opp notifier\n");
		return ret;
	}

	/*
	 * Enable devfreq-event to get raw data which is used to determine
	 * current bus load.
	 */
	ret = exynos_bus_enable_edev(bus);
	if (ret < 0) {
		dev_err(dev, "failed to enable devfreq-event devices\n");
		return ret;
	}

	ret = exynos_bus_set_event(bus);
	if (ret < 0) {
		dev_err(dev, "failed to set event to devfreq-event devices\n");
		goto err_edev;
	}

	return 0;

err_edev:
	if (exynos_bus_disable_edev(bus))
		dev_warn(dev, "failed to disable the devfreq-event devices\n");

	return ret;
}

static int exynos_bus_profile_init_passive(struct exynos_bus *bus,
					   struct devfreq_dev_profile *profile)
{
	struct device *dev = bus->dev;
	struct devfreq_passive_data *passive_data;
	struct devfreq *parent_devfreq;

	/* Initialize the struct profile and governor data for passive device */
	profile->target = exynos_bus_target;
	profile->exit = exynos_bus_passive_exit;

	/* Get the instance of parent devfreq device */
	parent_devfreq = devfreq_get_devfreq_by_phandle(dev, "devfreq", 0);
	if (IS_ERR(parent_devfreq))
		return -EPROBE_DEFER;

	passive_data = devm_kzalloc(dev, sizeof(*passive_data), GFP_KERNEL);
	if (!passive_data)
		return -ENOMEM;

	passive_data->parent = parent_devfreq;

	/* Add devfreq device for exynos bus with passive governor */
	bus->devfreq = devm_devfreq_add_device(dev, profile, DEVFREQ_GOV_PASSIVE,
						passive_data);
	if (IS_ERR(bus->devfreq)) {
		dev_err(dev,
			"failed to add devfreq dev with passive governor\n");
		return PTR_ERR(bus->devfreq);
	}

	return 0;
}

static int exynos_bus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *node;
	struct devfreq_dev_profile *profile;
	struct exynos_bus *bus;
	int ret, max_state;
	unsigned long min_freq, max_freq;
	bool passive = false;

	if (!np) {
		dev_err(dev, "failed to find devicetree node\n");
		return -EINVAL;
	}

	bus = devm_kzalloc(&pdev->dev, sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;
	mutex_init(&bus->lock);
	bus->dev = &pdev->dev;
	platform_set_drvdata(pdev, bus);

	profile = devm_kzalloc(dev, sizeof(*profile), GFP_KERNEL);
	if (!profile)
		return -ENOMEM;

	node = of_parse_phandle(dev->of_node, "devfreq", 0);
	if (node) {
		of_node_put(node);
		passive = true;
	} else {
		ret = exynos_bus_parent_parse_of(np, bus);
		if (ret < 0)
			return ret;
	}

	/* Parse the device-tree to get the resource information */
	ret = exynos_bus_parse_of(np, bus);
	if (ret < 0)
		goto err_reg;

	if (passive)
		ret = exynos_bus_profile_init_passive(bus, profile);
	else
		ret = exynos_bus_profile_init(bus, profile);

	if (ret < 0)
		goto err;

	/* Create child platform device for the interconnect provider */
	if (of_get_property(dev->of_node, "#interconnect-cells", NULL)) {
		bus->icc_pdev = platform_device_register_data(
						dev, "exynos-generic-icc",
						PLATFORM_DEVID_AUTO, NULL, 0);

		if (IS_ERR(bus->icc_pdev)) {
			ret = PTR_ERR(bus->icc_pdev);
			goto err;
		}
	}

	max_state = bus->devfreq->max_state;
	min_freq = (bus->devfreq->freq_table[0] / 1000);
	max_freq = (bus->devfreq->freq_table[max_state - 1] / 1000);
	pr_info("exynos-bus: new bus device registered: %s (%6ld KHz ~ %6ld KHz)\n",
			dev_name(dev), min_freq, max_freq);

	return 0;

err:
	dev_pm_opp_of_remove_table(dev);
	clk_disable_unprepare(bus->clk);
err_reg:
	dev_pm_opp_put_regulators(bus->opp_token);

	return ret;
}

static void exynos_bus_shutdown(struct platform_device *pdev)
{
	struct exynos_bus *bus = dev_get_drvdata(&pdev->dev);

	devfreq_suspend_device(bus->devfreq);
}

#ifdef CONFIG_PM_SLEEP
static int exynos_bus_resume(struct device *dev)
{
	struct exynos_bus *bus = dev_get_drvdata(dev);
	int ret;

	ret = exynos_bus_enable_edev(bus);
	if (ret < 0) {
		dev_err(dev, "failed to enable the devfreq-event devices\n");
		return ret;
	}

	return 0;
}

static int exynos_bus_suspend(struct device *dev)
{
	struct exynos_bus *bus = dev_get_drvdata(dev);
	int ret;

	ret = exynos_bus_disable_edev(bus);
	if (ret < 0) {
		dev_err(dev, "failed to disable the devfreq-event devices\n");
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops exynos_bus_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(exynos_bus_suspend, exynos_bus_resume)
};

static const struct of_device_id exynos_bus_of_match[] = {
	{ .compatible = "samsung,exynos-bus", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exynos_bus_of_match);

static struct platform_driver exynos_bus_platdrv = {
	.probe		= exynos_bus_probe,
	.shutdown	= exynos_bus_shutdown,
	.driver = {
		.name	= "exynos-bus",
		.pm	= &exynos_bus_pm,
		.of_match_table = of_match_ptr(exynos_bus_of_match),
	},
};
module_platform_driver(exynos_bus_platdrv);

MODULE_DESCRIPTION("Generic Exynos Bus frequency driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL v2");
