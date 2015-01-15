/*
 * Generic Exynos Memory Bus Frequency driver with DEVFREQ Framework
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author : Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This driver is based on exynos4_bus.c, which was written
 * by MyungJoo Ham <myungjoo.ham@samsung.com>, Samsung Electronics.
 *
 * This driver support Exynos Memory Bus frequency feature by using in DEVFREQ
 * framework. This version supprots Exynos3250/Exynos4 series/Exynos5260 SoC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#define SAFEVOLT			50000

struct exynos_memory_bus_opp {
	unsigned long rate;
	unsigned long volt;
};

struct exynos_memory_bus_block {
	struct clk *clk;
	struct exynos_memory_bus_opp *freq_table;
};

struct exynos_memory_bus {
	/* devfreq device to monitor and control memory bus group */
	struct device *dev;
	struct devfreq *devfreq;

	struct exynos_memory_bus_opp *freq_table;
	unsigned int freq_count;
	struct regulator *regulator;
	struct mutex lock;
	int ratio;

	struct exynos_memory_bus_opp curr_opp;

	struct exynos_memory_bus_block *block;
	unsigned int block_count;

	/* devfreq-event device to get current state of memory bus group */
	struct devfreq_event_dev **edev;
	unsigned int edev_count;
};

/*
 * Initialize the memory bus group/block by parsing dt node in the devicetree
 */
static int of_init_memory_bus(struct device_node *np,
			      struct exynos_memory_bus *bus)
{
	struct device *dev = bus->dev;
	struct dev_pm_opp *opp;
	unsigned long rate, volt;
	int i, ret, count, size;

	/* Get the freq/voltage OPP table to scale memory bus frequency */
	ret = of_init_opp_table(dev);
	if (ret < 0) {
		dev_err(dev, "failed to get OPP table\n");
		return ret;
	}

	rcu_read_lock();

	bus->freq_count = dev_pm_opp_get_opp_count(dev);
	if (bus->freq_count <= 0) {
		dev_err(dev, "failed to get the count of OPP entry\n");
		rcu_read_unlock();
		return -EINVAL;
	}

	size = sizeof(*bus->freq_table) * bus->freq_count;
	bus->freq_table = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!bus->freq_table) {
		rcu_read_unlock();
		return -ENOMEM;
	}

	for (i = 0, rate = 0; i < bus->freq_count; i++, rate++) {
		opp = dev_pm_opp_find_freq_ceil(dev, &rate);
		if (IS_ERR(opp)) {
			dev_err(dev, "failed to find dev_pm_opp\n");
			rcu_read_unlock();
			return PTR_ERR(opp);
		}

		volt = dev_pm_opp_get_voltage(opp);

		bus->freq_table[i].rate = rate;
		bus->freq_table[i].volt = volt;

		dev_dbg(dev, "Level%d : freq(%ld), voltage(%ld)\n", i, rate, volt);
	}

	rcu_read_unlock();

	/* Get the regulator to provide memory bus group with the power */
	bus->regulator = devm_regulator_get(dev, "vdd-mem");
	if (IS_ERR(bus->regulator)) {
		dev_err(dev, "failed to get vdd-memory regulator\n");
		return PTR_ERR(bus->regulator);
	}

	ret = regulator_enable(bus->regulator);
	if (ret < 0) {
		dev_err(dev, "failed to enable vdd-memory regulator\n");
		return ret;
	}

	/* Get the saturation ratio according to Exynos SoC */
	if (of_property_read_u32(np, "exynos,saturation-ratio", &bus->ratio))
		bus->ratio = DEFAULT_SATURATION_RATIO;

	/*
	 * Get the devfreq-event devices to get the current state of
	 * memory bus group. This raw data will be used in devfreq governor.
	 */
	count = devfreq_event_get_edev_count(dev);
	if (count < 0) {
		dev_err(dev, "failed to get the count of devfreq-event dev\n");
		return count;
	}
	bus->edev_count = count;

	size = sizeof(*bus->edev) * count;
	bus->edev = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!bus->edev)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		bus->edev[i] = devfreq_event_get_edev_by_phandle(dev, i);
		if (IS_ERR(bus->edev[i])) {
			of_free_opp_table(dev);
			return -EPROBE_DEFER;
		}
	}

	return 0;
}

static int of_init_memory_bus_block(struct device_node *np,
				    struct exynos_memory_bus *bus)
{
	struct exynos_memory_bus_block *block;
	struct device *dev = bus->dev;
	struct device_node *buses_np, *node;
	int i, count;

	buses_np = of_get_child_by_name(np, "blocks");
	if (!buses_np) {
		dev_err(dev,
			"failed to get child node of memory bus\n");
		return -EINVAL;
	}

	count = of_get_child_count(buses_np);
	block = devm_kzalloc(dev, sizeof(*block) * count, GFP_KERNEL);
	if (!block)
		return -ENOMEM;
	bus->block = block;
	bus->block_count = count;

	/* Parse the busrmation of memory bus block */
	i = 0;
	for_each_child_of_node(buses_np, node) {
		const struct property *prop;
		const __be32 *val;
		int j, nr, size;

		block = &bus->block[i++];

		/* Get the frequency table of each memory bus block */
		prop = of_find_property(node, "frequency", NULL);
		if (!prop)
			return -ENODEV;
		if (!prop->value)
			return -ENODATA;

		nr = prop->length / sizeof(u32);
		if (!nr)
			return -EINVAL;

		if (nr != bus->freq_count) {
			dev_err(dev, "the size of frequency table is different \
					from OPP table\n");
			return -EINVAL;
		}

		size = sizeof(*block->freq_table) * nr;
		block->freq_table = devm_kzalloc(dev, size, GFP_KERNEL);
		if (!block->freq_table)
			return -ENOMEM;

		val = prop->value;
		for (j = nr - 1; j >= 0; j--)
			block->freq_table[j].rate = be32_to_cpup(val++) * 1000;

		for (j = 0; j < nr; j++)
			dev_dbg(dev, "%s: Level%d : freq(%ld)\n",
				node->name, j, block->freq_table[j].rate);

		/* Get the clock of each memory bus block */
		block->clk = of_clk_get_by_name(node, "memory-bus");
		if (IS_ERR(block->clk)) {
			dev_err(dev, "failed to get memory-bus clock in %s\n",
					node->name);
			return PTR_ERR(block->clk);
		}
		clk_prepare_enable(block->clk);

		of_node_put(node);
	}

	of_node_put(buses_np);

	return 0;
}

/*
 * Control the devfreq-event device to get the current state of memory bus
 */
static int exynos_bus_enable_edev(struct exynos_memory_bus *bus)
{
	int i, ret;

	for (i = 0; i < bus->edev_count; i++) {
		ret = devfreq_event_enable_edev(bus->edev[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int exynos_bus_disable_edev(struct exynos_memory_bus *bus)
{
	int i, ret;

	for (i = 0; i < bus->edev_count; i++) {
		ret = devfreq_event_disable_edev(bus->edev[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}


static int exynos_bus_set_event(struct exynos_memory_bus *bus)
{
	int i, ret;

	for (i = 0; i < bus->edev_count; i++) {
		ret = devfreq_event_set_event(bus->edev[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int exynos_bus_get_event(struct exynos_memory_bus *bus,
				struct devfreq_event_data *edata)
{
	struct devfreq_event_data event_data;
	unsigned long event = 0, total_event = 0;
	int i, ret = 0;

	for (i = 0; i < bus->edev_count; i++) {
		ret = devfreq_event_get_event(bus->edev[i], &event_data);
		if (ret < 0)
			return ret;

		if (i == 0 || event_data.event > event) {
			event = event_data.event;
			total_event = event_data.total_event;
		}
	}

	edata->event = event;
	edata->total_event = total_event;

	return ret;
}

/*
 * Must necessary function for devfreq governor
 */

static int exynos_bus_set_frequency(struct exynos_memory_bus *bus,
				    struct exynos_memory_bus_opp *new_opp)
{
	int i, j;

	for (i = 0; i < bus->freq_count; i++)
		if (new_opp->rate == bus->freq_table[i].rate)
			break;

	if (i == bus->freq_count)
		i = bus->freq_count - 1;

	for (j = 0; j < bus->block_count; j++)
		clk_set_rate(bus->block[j].clk,
				bus->block[j].freq_table[i].rate);

	return 0;
}

static int exynos_bus_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct exynos_memory_bus *bus = dev_get_drvdata(dev);
	struct exynos_memory_bus_opp new_opp;
	unsigned long new_freq, old_freq;
	struct dev_pm_opp *opp;
	int ret = 0;

	/* Get new opp-bus instance according to new bus clock */
	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR_OR_NULL(opp)) {
		dev_err(dev, "failed to get recommed opp instance\n");
		rcu_read_unlock();
		return PTR_ERR(opp);
	}
	new_opp.rate = dev_pm_opp_get_freq(opp);
	new_opp.volt = dev_pm_opp_get_voltage(opp);
	rcu_read_unlock();

	old_freq = bus->curr_opp.rate;
	new_freq = new_opp.rate;
	if (old_freq == new_freq)
		return 0;

	dev_dbg(dev, "Change the frequency of memory bus (%ld kHz -> %ld kHz)\n",
			old_freq / 1000, new_freq / 1000);

	/* Change voltage/clock according to new bus level */
	mutex_lock(&bus->lock);

	if (old_freq < new_freq) {
		ret = regulator_set_voltage(bus->regulator, new_opp.volt,
						new_opp.volt + SAFEVOLT);
		if (ret < 0) {
			dev_err(bus->dev, "failed to set voltage\n");
			regulator_set_voltage(bus->regulator,
					bus->curr_opp.rate,
					bus->curr_opp.rate + SAFEVOLT);
			goto out;
		}
	}

	ret = exynos_bus_set_frequency(bus, &new_opp);
	if (ret < 0) {
		dev_err(dev, "failed to change clock of memory bus\n");
		goto out;
	}

	if (old_freq > new_freq) {
		ret = regulator_set_voltage(bus->regulator, new_opp.volt,
						new_opp.volt + SAFEVOLT);
		if (ret < 0) {
			dev_err(bus->dev, "failed to set voltage\n");
			regulator_set_voltage(bus->regulator,
					bus->curr_opp.rate,
					bus->curr_opp.rate + SAFEVOLT);
			goto out;
		}
	}

	bus->curr_opp = new_opp;

out:
	mutex_unlock(&bus->lock);

	return ret;
}

static int exynos_bus_get_dev_status(struct device *dev,
				     struct devfreq_dev_status *stat)
{
	struct exynos_memory_bus *bus = dev_get_drvdata(dev);
	struct devfreq_event_data edata;
	int ret;

	stat->current_frequency = bus->curr_opp.rate;

	ret = exynos_bus_get_event(bus, &edata);
	if (ret < 0) {
		stat->total_time = stat->busy_time = 0;
		goto err;
	}

	stat->busy_time = (edata.event * 100) / bus->ratio;
	stat->total_time = edata.total_event;

	dev_dbg(dev, "Usage of devfreq-event : %ld/%ld\n", stat->busy_time,
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
	struct exynos_memory_bus *bus = dev_get_drvdata(dev);
	int i, ret;

	ret = exynos_bus_disable_edev(bus);
	if (ret < 0)
		dev_warn(dev, "failed to disable the devfreq-event devices\n");

	for (i = 0; i < bus->block_count; i++)
		clk_disable_unprepare(bus->block[i].clk);

	if (regulator_is_enabled(bus->regulator))
		regulator_disable(bus->regulator);

	of_free_opp_table(dev);
}

static struct devfreq_dev_profile exynos_memory_bus_profile = {
	.polling_ms	= 100,
	.target		= exynos_bus_target,
	.get_dev_status	= exynos_bus_get_dev_status,
	.exit		= exynos_bus_exit,
};

static struct devfreq_simple_ondemand_data exynos_memory_bus_ondemand_data = {
	.upthreshold		= 40,
	.downdifferential	= 5,
};

static int exynos_bus_probe(struct platform_device *pdev)
{
	struct exynos_memory_bus *bus;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

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

	/* Initialize */
	ret = of_init_memory_bus(np, bus);
	if (ret < 0) {
		dev_err(dev, "failed to initialize memory-bus\n");
		return ret;
	}

	ret = of_init_memory_bus_block(np, bus);
	if (ret < 0) {
		dev_err(dev, "failed to initialize memory-bus block\n");
		return ret;
	}

	/* Add devfreq device for DVFS of memory bus */
	bus->devfreq = devm_devfreq_add_device(dev,
					&exynos_memory_bus_profile,
					"simple_ondemand",
					&exynos_memory_bus_ondemand_data);
	if (IS_ERR_OR_NULL(bus->devfreq)) {
		dev_err(dev, "failed to add devfreq device\n");
		return  PTR_ERR(bus->devfreq);
	}

	/* Register opp_notifier to catch the change of OPP  */
	ret = devm_devfreq_register_opp_notifier(dev, bus->devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to register opp notifier\n");
		return ret;
	}

	/*
	 * Enable devfreq-event to get raw data which is used to determine
	 * current memory bus load.
	 */
	ret = exynos_bus_enable_edev(bus);
	if (ret < 0) {
		dev_err(dev, "failed to enable devfreq-event devices\n");
		return ret;
	}

	ret = exynos_bus_set_event(bus);
	if (ret < 0) {
		dev_err(dev, "failed to set event to devfreq-event devices\n");
		return ret;
	}

	return 0;
}

static int exynos_bus_remove(struct platform_device *pdev)
{
	/*
	 * devfreq_dev_profile.exit() have to free the resource of this
	 * device driver.
	 */

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_bus_resume(struct device *dev)
{
	struct exynos_memory_bus *bus = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(bus->regulator);
	if (ret < 0) {
		dev_err(dev, "failed to enable vdd-memory regulator\n");
		return ret;
	}

	ret = exynos_bus_enable_edev(bus);
	if (ret < 0) {
		dev_err(dev, "failed to enable the devfreq-event devices\n");
		return ret;
	}

	return 0;
}

static int exynos_bus_suspend(struct device *dev)
{
	struct exynos_memory_bus *bus = dev_get_drvdata(dev);
	int ret;

	ret = exynos_bus_disable_edev(bus);
	if (ret < 0) {
		dev_err(dev, "failed to disable the devfreq-event devices\n");
		return ret;
	}

	regulator_disable(bus->regulator);

	return 0;
}
#endif

static const struct dev_pm_ops exynos_bus_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(exynos_bus_suspend, exynos_bus_resume)
};

static const struct of_device_id exynos_bus_of_match[] = {
	{ .compatible = "samsung,exynos-memory-bus", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exynos_bus_of_match);

static struct platform_driver exynos_bus_platdrv = {
	.probe		= exynos_bus_probe,
	.remove		= exynos_bus_remove,
	.driver = {
		.name	= "exynos-memory-bus",
		.owner	= THIS_MODULE,
		.pm	= &exynos_bus_pm,
		.of_match_table = of_match_ptr(exynos_bus_of_match),
	},
};
module_platform_driver(exynos_bus_platdrv);

MODULE_DESCRIPTION("Generic Exynos Memory Bus Frequency driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL v2");
