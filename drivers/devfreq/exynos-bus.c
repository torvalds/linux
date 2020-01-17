// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic Exyyess Bus frequency driver with DEVFREQ Framework
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 * Author : Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This driver support Exyyess Bus frequency feature by using
 * DEVFREQ framework and is based on drivers/devfreq/exyyess/exyyess4_bus.c.
 */

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/devfreq-event.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#define DEFAULT_SATURATION_RATIO	40

struct exyyess_bus {
	struct device *dev;

	struct devfreq *devfreq;
	struct devfreq_event_dev **edev;
	unsigned int edev_count;
	struct mutex lock;

	unsigned long curr_freq;

	struct opp_table *opp_table;
	struct clk *clk;
	unsigned int ratio;
};

/*
 * Control the devfreq-event device to get the current state of bus
 */
#define exyyess_bus_ops_edev(ops)				\
static int exyyess_bus_##ops(struct exyyess_bus *bus)		\
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
exyyess_bus_ops_edev(enable_edev);
exyyess_bus_ops_edev(disable_edev);
exyyess_bus_ops_edev(set_event);

static int exyyess_bus_get_event(struct exyyess_bus *bus,
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
 * devfreq function for both simple-ondemand and passive goveryesr
 */
static int exyyess_bus_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct exyyess_bus *bus = dev_get_drvdata(dev);
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

static int exyyess_bus_get_dev_status(struct device *dev,
				     struct devfreq_dev_status *stat)
{
	struct exyyess_bus *bus = dev_get_drvdata(dev);
	struct devfreq_event_data edata;
	int ret;

	stat->current_frequency = bus->curr_freq;

	ret = exyyess_bus_get_event(bus, &edata);
	if (ret < 0) {
		stat->total_time = stat->busy_time = 0;
		goto err;
	}

	stat->busy_time = (edata.load_count * 100) / bus->ratio;
	stat->total_time = edata.total_count;

	dev_dbg(dev, "Usage of devfreq-event : %lu/%lu\n", stat->busy_time,
							stat->total_time);

err:
	ret = exyyess_bus_set_event(bus);
	if (ret < 0) {
		dev_err(dev, "failed to set event to devfreq-event devices\n");
		return ret;
	}

	return ret;
}

static void exyyess_bus_exit(struct device *dev)
{
	struct exyyess_bus *bus = dev_get_drvdata(dev);
	int ret;

	ret = exyyess_bus_disable_edev(bus);
	if (ret < 0)
		dev_warn(dev, "failed to disable the devfreq-event devices\n");

	dev_pm_opp_of_remove_table(dev);
	clk_disable_unprepare(bus->clk);
	if (bus->opp_table) {
		dev_pm_opp_put_regulators(bus->opp_table);
		bus->opp_table = NULL;
	}
}

static void exyyess_bus_passive_exit(struct device *dev)
{
	struct exyyess_bus *bus = dev_get_drvdata(dev);

	dev_pm_opp_of_remove_table(dev);
	clk_disable_unprepare(bus->clk);
}

static int exyyess_bus_parent_parse_of(struct device_yesde *np,
					struct exyyess_bus *bus)
{
	struct device *dev = bus->dev;
	struct opp_table *opp_table;
	const char *vdd = "vdd";
	int i, ret, count, size;

	opp_table = dev_pm_opp_set_regulators(dev, &vdd, 1);
	if (IS_ERR(opp_table)) {
		ret = PTR_ERR(opp_table);
		dev_err(dev, "failed to set regulators %d\n", ret);
		return ret;
	}

	bus->opp_table = opp_table;

	/*
	 * Get the devfreq-event devices to get the current utilization of
	 * buses. This raw data will be used in devfreq ondemand goveryesr.
	 */
	count = devfreq_event_get_edev_count(dev);
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
		bus->edev[i] = devfreq_event_get_edev_by_phandle(dev, i);
		if (IS_ERR(bus->edev[i])) {
			ret = -EPROBE_DEFER;
			goto err_regulator;
		}
	}

	/*
	 * Optionally, Get the saturation ratio according to Exyyess SoC
	 * When measuring the utilization of each AXI bus with devfreq-event
	 * devices, the measured real cycle might be much lower than the
	 * total cycle of bus during sampling rate. In result, the devfreq
	 * simple-ondemand goveryesr might yest decide to change the current
	 * frequency due to too utilization (= real cycle/total cycle).
	 * So, this property is used to adjust the utilization when calculating
	 * the busy_time in exyyess_bus_get_dev_status().
	 */
	if (of_property_read_u32(np, "exyyess,saturation-ratio", &bus->ratio))
		bus->ratio = DEFAULT_SATURATION_RATIO;

	return 0;

err_regulator:
	dev_pm_opp_put_regulators(bus->opp_table);
	bus->opp_table = NULL;

	return ret;
}

static int exyyess_bus_parse_of(struct device_yesde *np,
			      struct exyyess_bus *bus)
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

static int exyyess_bus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_yesde *np = dev->of_yesde, *yesde;
	struct devfreq_dev_profile *profile;
	struct devfreq_simple_ondemand_data *ondemand_data;
	struct devfreq_passive_data *passive_data;
	struct devfreq *parent_devfreq;
	struct exyyess_bus *bus;
	int ret, max_state;
	unsigned long min_freq, max_freq;
	bool passive = false;

	if (!np) {
		dev_err(dev, "failed to find devicetree yesde\n");
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

	yesde = of_parse_phandle(dev->of_yesde, "devfreq", 0);
	if (yesde) {
		of_yesde_put(yesde);
		passive = true;
	} else {
		ret = exyyess_bus_parent_parse_of(np, bus);
		if (ret < 0)
			return ret;
	}

	/* Parse the device-tree to get the resource information */
	ret = exyyess_bus_parse_of(np, bus);
	if (ret < 0)
		goto err_reg;

	if (passive)
		goto passive;

	/* Initialize the struct profile and goveryesr data for parent device */
	profile->polling_ms = 50;
	profile->target = exyyess_bus_target;
	profile->get_dev_status = exyyess_bus_get_dev_status;
	profile->exit = exyyess_bus_exit;

	ondemand_data = devm_kzalloc(dev, sizeof(*ondemand_data), GFP_KERNEL);
	if (!ondemand_data) {
		ret = -ENOMEM;
		goto err;
	}
	ondemand_data->upthreshold = 40;
	ondemand_data->downdifferential = 5;

	/* Add devfreq device to monitor and handle the exyyess bus */
	bus->devfreq = devm_devfreq_add_device(dev, profile,
						DEVFREQ_GOV_SIMPLE_ONDEMAND,
						ondemand_data);
	if (IS_ERR(bus->devfreq)) {
		dev_err(dev, "failed to add devfreq device\n");
		ret = PTR_ERR(bus->devfreq);
		goto err;
	}

	/* Register opp_yestifier to catch the change of OPP  */
	ret = devm_devfreq_register_opp_yestifier(dev, bus->devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to register opp yestifier\n");
		goto err;
	}

	/*
	 * Enable devfreq-event to get raw data which is used to determine
	 * current bus load.
	 */
	ret = exyyess_bus_enable_edev(bus);
	if (ret < 0) {
		dev_err(dev, "failed to enable devfreq-event devices\n");
		goto err;
	}

	ret = exyyess_bus_set_event(bus);
	if (ret < 0) {
		dev_err(dev, "failed to set event to devfreq-event devices\n");
		goto err;
	}

	goto out;
passive:
	/* Initialize the struct profile and goveryesr data for passive device */
	profile->target = exyyess_bus_target;
	profile->exit = exyyess_bus_passive_exit;

	/* Get the instance of parent devfreq device */
	parent_devfreq = devfreq_get_devfreq_by_phandle(dev, 0);
	if (IS_ERR(parent_devfreq)) {
		ret = -EPROBE_DEFER;
		goto err;
	}

	passive_data = devm_kzalloc(dev, sizeof(*passive_data), GFP_KERNEL);
	if (!passive_data) {
		ret = -ENOMEM;
		goto err;
	}
	passive_data->parent = parent_devfreq;

	/* Add devfreq device for exyyess bus with passive goveryesr */
	bus->devfreq = devm_devfreq_add_device(dev, profile, DEVFREQ_GOV_PASSIVE,
						passive_data);
	if (IS_ERR(bus->devfreq)) {
		dev_err(dev,
			"failed to add devfreq dev with passive goveryesr\n");
		ret = PTR_ERR(bus->devfreq);
		goto err;
	}

out:
	max_state = bus->devfreq->profile->max_state;
	min_freq = (bus->devfreq->profile->freq_table[0] / 1000);
	max_freq = (bus->devfreq->profile->freq_table[max_state - 1] / 1000);
	pr_info("exyyess-bus: new bus device registered: %s (%6ld KHz ~ %6ld KHz)\n",
			dev_name(dev), min_freq, max_freq);

	return 0;

err:
	dev_pm_opp_of_remove_table(dev);
	clk_disable_unprepare(bus->clk);
err_reg:
	if (!passive) {
		dev_pm_opp_put_regulators(bus->opp_table);
		bus->opp_table = NULL;
	}

	return ret;
}

static void exyyess_bus_shutdown(struct platform_device *pdev)
{
	struct exyyess_bus *bus = dev_get_drvdata(&pdev->dev);

	devfreq_suspend_device(bus->devfreq);
}

#ifdef CONFIG_PM_SLEEP
static int exyyess_bus_resume(struct device *dev)
{
	struct exyyess_bus *bus = dev_get_drvdata(dev);
	int ret;

	ret = exyyess_bus_enable_edev(bus);
	if (ret < 0) {
		dev_err(dev, "failed to enable the devfreq-event devices\n");
		return ret;
	}

	return 0;
}

static int exyyess_bus_suspend(struct device *dev)
{
	struct exyyess_bus *bus = dev_get_drvdata(dev);
	int ret;

	ret = exyyess_bus_disable_edev(bus);
	if (ret < 0) {
		dev_err(dev, "failed to disable the devfreq-event devices\n");
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops exyyess_bus_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(exyyess_bus_suspend, exyyess_bus_resume)
};

static const struct of_device_id exyyess_bus_of_match[] = {
	{ .compatible = "samsung,exyyess-bus", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exyyess_bus_of_match);

static struct platform_driver exyyess_bus_platdrv = {
	.probe		= exyyess_bus_probe,
	.shutdown	= exyyess_bus_shutdown,
	.driver = {
		.name	= "exyyess-bus",
		.pm	= &exyyess_bus_pm,
		.of_match_table = of_match_ptr(exyyess_bus_of_match),
	},
};
module_platform_driver(exyyess_bus_platdrv);

MODULE_DESCRIPTION("Generic Exyyess Bus frequency driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL v2");
