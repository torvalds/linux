/*
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Mikko Perttunen <mperttunen@nvidia.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/thermal.h>

#include <dt-bindings/thermal/tegra124-soctherm.h>

#include "soctherm.h"

#define SENSOR_CONFIG0				0
#define SENSOR_CONFIG0_STOP			BIT(0)
#define SENSOR_CONFIG0_CPTR_OVER		BIT(2)
#define SENSOR_CONFIG0_OVER			BIT(3)
#define SENSOR_CONFIG0_TCALC_OVER		BIT(4)
#define SENSOR_CONFIG0_TALL_MASK		(0xfffff << 8)
#define SENSOR_CONFIG0_TALL_SHIFT		8

#define SENSOR_CONFIG1				4
#define SENSOR_CONFIG1_TSAMPLE_MASK		0x3ff
#define SENSOR_CONFIG1_TSAMPLE_SHIFT		0
#define SENSOR_CONFIG1_TIDDQ_EN_MASK		(0x3f << 15)
#define SENSOR_CONFIG1_TIDDQ_EN_SHIFT		15
#define SENSOR_CONFIG1_TEN_COUNT_MASK		(0x3f << 24)
#define SENSOR_CONFIG1_TEN_COUNT_SHIFT		24
#define SENSOR_CONFIG1_TEMP_ENABLE		BIT(31)

/*
 * SENSOR_CONFIG2 is defined in soctherm.h
 * because, it will be used by tegra_soctherm_fuse.c
 */

#define SENSOR_STATUS0				0xc
#define SENSOR_STATUS0_VALID_MASK		BIT(31)
#define SENSOR_STATUS0_CAPTURE_MASK		0xffff

#define SENSOR_STATUS1				0x10
#define SENSOR_STATUS1_TEMP_VALID_MASK		BIT(31)
#define SENSOR_STATUS1_TEMP_MASK		0xffff

#define READBACK_VALUE_MASK			0xff00
#define READBACK_VALUE_SHIFT			8
#define READBACK_ADD_HALF			BIT(7)
#define READBACK_NEGATE				BIT(0)

/* get val from register(r) mask bits(m) */
#define REG_GET_MASK(r, m)	(((r) & (m)) >> (ffs(m) - 1))
/* set val(v) to mask bits(m) of register(r) */
#define REG_SET_MASK(r, m, v)	(((r) & ~(m)) | \
				 (((v) & (m >> (ffs(m) - 1))) << (ffs(m) - 1)))

static const int min_low_temp = -127000;
static const int max_high_temp = 127000;

struct tegra_thermctl_zone {
	void __iomem *reg;
	struct device *dev;
	struct thermal_zone_device *tz;
	const struct tegra_tsensor_group *sg;
};

struct tegra_soctherm {
	struct reset_control *reset;
	struct clk *clock_tsensor;
	struct clk *clock_soctherm;
	void __iomem *regs;
	struct thermal_zone_device **thermctl_tzs;

	u32 *calib;
	struct tegra_soctherm_soc *soc;

	struct dentry *debugfs_dir;
};

static void enable_tsensor(struct tegra_soctherm *tegra, unsigned int i)
{
	const struct tegra_tsensor *sensor = &tegra->soc->tsensors[i];
	void __iomem *base = tegra->regs + sensor->base;
	unsigned int val;

	val = sensor->config->tall << SENSOR_CONFIG0_TALL_SHIFT;
	writel(val, base + SENSOR_CONFIG0);

	val  = (sensor->config->tsample - 1) << SENSOR_CONFIG1_TSAMPLE_SHIFT;
	val |= sensor->config->tiddq_en << SENSOR_CONFIG1_TIDDQ_EN_SHIFT;
	val |= sensor->config->ten_count << SENSOR_CONFIG1_TEN_COUNT_SHIFT;
	val |= SENSOR_CONFIG1_TEMP_ENABLE;
	writel(val, base + SENSOR_CONFIG1);

	writel(tegra->calib[i], base + SENSOR_CONFIG2);
}

/*
 * Translate from soctherm readback format to millicelsius.
 * The soctherm readback format in bits is as follows:
 *   TTTTTTTT H______N
 * where T's contain the temperature in Celsius,
 * H denotes an addition of 0.5 Celsius and N denotes negation
 * of the final value.
 */
static int translate_temp(u16 val)
{
	int t;

	t = ((val & READBACK_VALUE_MASK) >> READBACK_VALUE_SHIFT) * 1000;
	if (val & READBACK_ADD_HALF)
		t += 500;
	if (val & READBACK_NEGATE)
		t *= -1;

	return t;
}

static int tegra_thermctl_get_temp(void *data, int *out_temp)
{
	struct tegra_thermctl_zone *zone = data;
	u32 val;

	val = readl(zone->reg);
	val = REG_GET_MASK(val, zone->sg->sensor_temp_mask);
	*out_temp = translate_temp(val);

	return 0;
}

static int
thermtrip_program(struct device *dev, const struct tegra_tsensor_group *sg,
		  int trip_temp);

static int tegra_thermctl_set_trip_temp(void *data, int trip, int temp)
{
	struct tegra_thermctl_zone *zone = data;
	struct thermal_zone_device *tz = zone->tz;
	const struct tegra_tsensor_group *sg = zone->sg;
	struct device *dev = zone->dev;
	enum thermal_trip_type type;
	int ret;

	if (!tz)
		return -EINVAL;

	ret = tz->ops->get_trip_type(tz, trip, &type);
	if (ret)
		return ret;

	if (type != THERMAL_TRIP_CRITICAL)
		return 0;

	return thermtrip_program(dev, sg, temp);
}

static const struct thermal_zone_of_device_ops tegra_of_thermal_ops = {
	.get_temp = tegra_thermctl_get_temp,
	.set_trip_temp = tegra_thermctl_set_trip_temp,
};

/**
 * enforce_temp_range() - check and enforce temperature range [min, max]
 * @trip_temp: the trip temperature to check
 *
 * Checks and enforces the permitted temperature range that SOC_THERM
 * HW can support This is
 * done while taking care of precision.
 *
 * Return: The precision adjusted capped temperature in millicelsius.
 */
static int enforce_temp_range(struct device *dev, int trip_temp)
{
	int temp;

	temp = clamp_val(trip_temp, min_low_temp, max_high_temp);
	if (temp != trip_temp)
		dev_info(dev, "soctherm: trip temperature %d forced to %d\n",
			 trip_temp, temp);
	return temp;
}

/**
 * thermtrip_program() - Configures the hardware to shut down the
 * system if a given sensor group reaches a given temperature
 * @dev: ptr to the struct device for the SOC_THERM IP block
 * @sg: pointer to the sensor group to set the thermtrip temperature for
 * @trip_temp: the temperature in millicelsius to trigger the thermal trip at
 *
 * Sets the thermal trip threshold of the given sensor group to be the
 * @trip_temp.  If this threshold is crossed, the hardware will shut
 * down.
 *
 * Note that, although @trip_temp is specified in millicelsius, the
 * hardware is programmed in degrees Celsius.
 *
 * Return: 0 upon success, or %-EINVAL upon failure.
 */
static int thermtrip_program(struct device *dev,
			     const struct tegra_tsensor_group *sg,
			     int trip_temp)
{
	struct tegra_soctherm *ts = dev_get_drvdata(dev);
	int temp;
	u32 r;

	if (!sg || !sg->thermtrip_threshold_mask)
		return -EINVAL;

	temp = enforce_temp_range(dev, trip_temp) / ts->soc->thresh_grain;

	r = readl(ts->regs + THERMCTL_THERMTRIP_CTL);
	r = REG_SET_MASK(r, sg->thermtrip_threshold_mask, temp);
	r = REG_SET_MASK(r, sg->thermtrip_enable_mask, 1);
	r = REG_SET_MASK(r, sg->thermtrip_any_en_mask, 0);
	writel(r, ts->regs + THERMCTL_THERMTRIP_CTL);

	return 0;
}

/**
 * tegra_soctherm_set_hwtrips() - set HW trip point from DT data
 * @dev: struct device * of the SOC_THERM instance
 *
 * Configure the SOC_THERM HW trip points, setting "THERMTRIP"
 * trip points , using "critical" type trip_temp from thermal
 * zone.
 * After they have been configured, THERMTRIP will take action
 * when the configured SoC thermal sensor group reaches a
 * certain temperature.
 *
 * Return: 0 upon success, or a negative error code on failure.
 * "Success" does not mean that trips was enabled; it could also
 * mean that no node was found in DT.
 * THERMTRIP has been enabled successfully when a message similar to
 * this one appears on the serial console:
 * "thermtrip: will shut down when sensor group XXX reaches YYYYYY mC"
 */
static int tegra_soctherm_set_hwtrips(struct device *dev,
				      const struct tegra_tsensor_group *sg,
				      struct thermal_zone_device *tz)
{
	int temperature;
	int ret;

	ret = tz->ops->get_crit_temp(tz, &temperature);
	if (ret) {
		dev_warn(dev, "thermtrip: %s: missing critical temperature\n",
			 sg->name);
		return ret;
	}

	ret = thermtrip_program(dev, sg, temperature);
	if (ret) {
		dev_err(dev, "thermtrip: %s: error during enable\n",
			sg->name);
		return ret;
	}

	dev_info(dev,
		 "thermtrip: will shut down when %s reaches %d mC\n",
		 sg->name, temperature);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int regs_show(struct seq_file *s, void *data)
{
	struct platform_device *pdev = s->private;
	struct tegra_soctherm *ts = platform_get_drvdata(pdev);
	const struct tegra_tsensor *tsensors = ts->soc->tsensors;
	const struct tegra_tsensor_group **ttgs = ts->soc->ttgs;
	u32 r, state;
	int i;

	seq_puts(s, "-----TSENSE (convert HW)-----\n");

	for (i = 0; i < ts->soc->num_tsensors; i++) {
		r = readl(ts->regs + tsensors[i].base + SENSOR_CONFIG1);
		state = REG_GET_MASK(r, SENSOR_CONFIG1_TEMP_ENABLE);

		seq_printf(s, "%s: ", tsensors[i].name);
		seq_printf(s, "En(%d) ", state);

		if (!state) {
			seq_puts(s, "\n");
			continue;
		}

		state = REG_GET_MASK(r, SENSOR_CONFIG1_TIDDQ_EN_MASK);
		seq_printf(s, "tiddq(%d) ", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG1_TEN_COUNT_MASK);
		seq_printf(s, "ten_count(%d) ", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG1_TSAMPLE_MASK);
		seq_printf(s, "tsample(%d) ", state + 1);

		r = readl(ts->regs + tsensors[i].base + SENSOR_STATUS1);
		state = REG_GET_MASK(r, SENSOR_STATUS1_TEMP_VALID_MASK);
		seq_printf(s, "Temp(%d/", state);
		state = REG_GET_MASK(r, SENSOR_STATUS1_TEMP_MASK);
		seq_printf(s, "%d) ", translate_temp(state));

		r = readl(ts->regs + tsensors[i].base + SENSOR_STATUS0);
		state = REG_GET_MASK(r, SENSOR_STATUS0_VALID_MASK);
		seq_printf(s, "Capture(%d/", state);
		state = REG_GET_MASK(r, SENSOR_STATUS0_CAPTURE_MASK);
		seq_printf(s, "%d) ", state);

		r = readl(ts->regs + tsensors[i].base + SENSOR_CONFIG0);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_STOP);
		seq_printf(s, "Stop(%d) ", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_TALL_MASK);
		seq_printf(s, "Tall(%d) ", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_TCALC_OVER);
		seq_printf(s, "Over(%d/", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_OVER);
		seq_printf(s, "%d/", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_CPTR_OVER);
		seq_printf(s, "%d) ", state);

		r = readl(ts->regs + tsensors[i].base + SENSOR_CONFIG2);
		state = REG_GET_MASK(r, SENSOR_CONFIG2_THERMA_MASK);
		seq_printf(s, "Therm_A/B(%d/", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG2_THERMB_MASK);
		seq_printf(s, "%d)\n", (s16)state);
	}

	r = readl(ts->regs + SENSOR_PDIV);
	seq_printf(s, "PDIV: 0x%x\n", r);

	r = readl(ts->regs + SENSOR_HOTSPOT_OFF);
	seq_printf(s, "HOTSPOT: 0x%x\n", r);

	seq_puts(s, "\n");
	seq_puts(s, "-----SOC_THERM-----\n");

	r = readl(ts->regs + SENSOR_TEMP1);
	state = REG_GET_MASK(r, SENSOR_TEMP1_CPU_TEMP_MASK);
	seq_printf(s, "Temperatures: CPU(%d) ", translate_temp(state));
	state = REG_GET_MASK(r, SENSOR_TEMP1_GPU_TEMP_MASK);
	seq_printf(s, " GPU(%d) ", translate_temp(state));
	r = readl(ts->regs + SENSOR_TEMP2);
	state = REG_GET_MASK(r, SENSOR_TEMP2_PLLX_TEMP_MASK);
	seq_printf(s, " PLLX(%d) ", translate_temp(state));
	state = REG_GET_MASK(r, SENSOR_TEMP2_MEM_TEMP_MASK);
	seq_printf(s, " MEM(%d)\n", translate_temp(state));

	r = readl(ts->regs + THERMCTL_THERMTRIP_CTL);
	state = REG_GET_MASK(r, ttgs[0]->thermtrip_any_en_mask);
	seq_printf(s, "Thermtrip Any En(%d)\n", state);
	for (i = 0; i < ts->soc->num_ttgs; i++) {
		state = REG_GET_MASK(r, ttgs[i]->thermtrip_enable_mask);
		seq_printf(s, "     %s En(%d) ", ttgs[i]->name, state);
		state = REG_GET_MASK(r, ttgs[i]->thermtrip_threshold_mask);
		state *= ts->soc->thresh_grain;
		seq_printf(s, "Thresh(%d)\n", state);
	}

	return 0;
}

static int regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, regs_show, inode->i_private);
}

static const struct file_operations regs_fops = {
	.open		= regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void soctherm_debug_init(struct platform_device *pdev)
{
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);
	struct dentry *root, *file;

	root = debugfs_create_dir("soctherm", NULL);
	if (!root) {
		dev_err(&pdev->dev, "failed to create debugfs directory\n");
		return;
	}

	tegra->debugfs_dir = root;

	file = debugfs_create_file("reg_contents", 0644, root,
				   pdev, &regs_fops);
	if (!file) {
		dev_err(&pdev->dev, "failed to create debugfs file\n");
		debugfs_remove_recursive(tegra->debugfs_dir);
		tegra->debugfs_dir = NULL;
	}
}
#else
static inline void soctherm_debug_init(struct platform_device *pdev) {}
#endif

static int soctherm_clk_enable(struct platform_device *pdev, bool enable)
{
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);
	int err;

	if (!tegra->clock_soctherm || !tegra->clock_tsensor)
		return -EINVAL;

	reset_control_assert(tegra->reset);

	if (enable) {
		err = clk_prepare_enable(tegra->clock_soctherm);
		if (err) {
			reset_control_deassert(tegra->reset);
			return err;
		}

		err = clk_prepare_enable(tegra->clock_tsensor);
		if (err) {
			clk_disable_unprepare(tegra->clock_soctherm);
			reset_control_deassert(tegra->reset);
			return err;
		}
	} else {
		clk_disable_unprepare(tegra->clock_tsensor);
		clk_disable_unprepare(tegra->clock_soctherm);
	}

	reset_control_deassert(tegra->reset);

	return 0;
}

static void soctherm_init(struct platform_device *pdev)
{
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);
	const struct tegra_tsensor_group **ttgs = tegra->soc->ttgs;
	int i;
	u32 pdiv, hotspot;

	/* Initialize raw sensors */
	for (i = 0; i < tegra->soc->num_tsensors; ++i)
		enable_tsensor(tegra, i);

	/* program pdiv and hotspot offsets per THERM */
	pdiv = readl(tegra->regs + SENSOR_PDIV);
	hotspot = readl(tegra->regs + SENSOR_HOTSPOT_OFF);
	for (i = 0; i < tegra->soc->num_ttgs; ++i) {
		pdiv = REG_SET_MASK(pdiv, ttgs[i]->pdiv_mask,
				    ttgs[i]->pdiv);
		/* hotspot offset from PLLX, doesn't need to configure PLLX */
		if (ttgs[i]->id == TEGRA124_SOCTHERM_SENSOR_PLLX)
			continue;
		hotspot =  REG_SET_MASK(hotspot,
					ttgs[i]->pllx_hotspot_mask,
					ttgs[i]->pllx_hotspot_diff);
	}
	writel(pdiv, tegra->regs + SENSOR_PDIV);
	writel(hotspot, tegra->regs + SENSOR_HOTSPOT_OFF);
}

static const struct of_device_id tegra_soctherm_of_match[] = {
#ifdef CONFIG_ARCH_TEGRA_124_SOC
	{
		.compatible = "nvidia,tegra124-soctherm",
		.data = &tegra124_soctherm,
	},
#endif
#ifdef CONFIG_ARCH_TEGRA_210_SOC
	{
		.compatible = "nvidia,tegra210-soctherm",
		.data = &tegra210_soctherm,
	},
#endif
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_soctherm_of_match);

static int tegra_soctherm_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct tegra_soctherm *tegra;
	struct thermal_zone_device *z;
	struct tsensor_shared_calib shared_calib;
	struct resource *res;
	struct tegra_soctherm_soc *soc;
	unsigned int i;
	int err;

	match = of_match_node(tegra_soctherm_of_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	soc = (struct tegra_soctherm_soc *)match->data;
	if (soc->num_ttgs > TEGRA124_SOCTHERM_SENSOR_NUM)
		return -EINVAL;

	tegra = devm_kzalloc(&pdev->dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, tegra);

	tegra->soc = soc;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tegra->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tegra->regs))
		return PTR_ERR(tegra->regs);

	tegra->reset = devm_reset_control_get(&pdev->dev, "soctherm");
	if (IS_ERR(tegra->reset)) {
		dev_err(&pdev->dev, "can't get soctherm reset\n");
		return PTR_ERR(tegra->reset);
	}

	tegra->clock_tsensor = devm_clk_get(&pdev->dev, "tsensor");
	if (IS_ERR(tegra->clock_tsensor)) {
		dev_err(&pdev->dev, "can't get tsensor clock\n");
		return PTR_ERR(tegra->clock_tsensor);
	}

	tegra->clock_soctherm = devm_clk_get(&pdev->dev, "soctherm");
	if (IS_ERR(tegra->clock_soctherm)) {
		dev_err(&pdev->dev, "can't get soctherm clock\n");
		return PTR_ERR(tegra->clock_soctherm);
	}

	tegra->calib = devm_kzalloc(&pdev->dev,
				    sizeof(u32) * soc->num_tsensors,
				    GFP_KERNEL);
	if (!tegra->calib)
		return -ENOMEM;

	/* calculate shared calibration data */
	err = tegra_calc_shared_calib(soc->tfuse, &shared_calib);
	if (err)
		return err;

	/* calculate tsensor calibaration data */
	for (i = 0; i < soc->num_tsensors; ++i) {
		err = tegra_calc_tsensor_calib(&soc->tsensors[i],
					       &shared_calib,
					       &tegra->calib[i]);
		if (err)
			return err;
	}

	tegra->thermctl_tzs = devm_kzalloc(&pdev->dev,
					   sizeof(*z) * soc->num_ttgs,
					   GFP_KERNEL);
	if (!tegra->thermctl_tzs)
		return -ENOMEM;

	err = soctherm_clk_enable(pdev, true);
	if (err)
		return err;

	soctherm_init(pdev);

	for (i = 0; i < soc->num_ttgs; ++i) {
		struct tegra_thermctl_zone *zone =
			devm_kzalloc(&pdev->dev, sizeof(*zone), GFP_KERNEL);
		if (!zone) {
			err = -ENOMEM;
			goto disable_clocks;
		}

		zone->reg = tegra->regs + soc->ttgs[i]->sensor_temp_offset;
		zone->dev = &pdev->dev;
		zone->sg = soc->ttgs[i];

		z = devm_thermal_zone_of_sensor_register(&pdev->dev,
							 soc->ttgs[i]->id, zone,
							 &tegra_of_thermal_ops);
		if (IS_ERR(z)) {
			err = PTR_ERR(z);
			dev_err(&pdev->dev, "failed to register sensor: %d\n",
				err);
			goto disable_clocks;
		}

		zone->tz = z;
		tegra->thermctl_tzs[soc->ttgs[i]->id] = z;

		/* Configure hw trip points */
		tegra_soctherm_set_hwtrips(&pdev->dev, soc->ttgs[i], z);
	}

	soctherm_debug_init(pdev);

	return 0;

disable_clocks:
	soctherm_clk_enable(pdev, false);

	return err;
}

static int tegra_soctherm_remove(struct platform_device *pdev)
{
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);

	debugfs_remove_recursive(tegra->debugfs_dir);

	soctherm_clk_enable(pdev, false);

	return 0;
}

static int __maybe_unused soctherm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	soctherm_clk_enable(pdev, false);

	return 0;
}

static int __maybe_unused soctherm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);
	struct tegra_soctherm_soc *soc = tegra->soc;
	int err, i;

	err = soctherm_clk_enable(pdev, true);
	if (err) {
		dev_err(&pdev->dev,
			"Resume failed: enable clocks failed\n");
		return err;
	}

	soctherm_init(pdev);

	for (i = 0; i < soc->num_ttgs; ++i) {
		struct thermal_zone_device *tz;

		tz = tegra->thermctl_tzs[soc->ttgs[i]->id];
		tegra_soctherm_set_hwtrips(dev, soc->ttgs[i], tz);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(tegra_soctherm_pm, soctherm_suspend, soctherm_resume);

static struct platform_driver tegra_soctherm_driver = {
	.probe = tegra_soctherm_probe,
	.remove = tegra_soctherm_remove,
	.driver = {
		.name = "tegra_soctherm",
		.pm = &tegra_soctherm_pm,
		.of_match_table = tegra_soctherm_of_match,
	},
};
module_platform_driver(tegra_soctherm_driver);

MODULE_AUTHOR("Mikko Perttunen <mperttunen@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra SOCTHERM thermal management driver");
MODULE_LICENSE("GPL v2");
