/**
 * uniphier_thermal.c - Socionext UniPhier thermal driver
 *
 * Copyright 2014      Panasonic Corporation
 * Copyright 2016-2017 Socionext Inc.
 * All rights reserved.
 *
 * Author:
 *	Kunihiko Hayashi <hayashi.kunihiko@socionext.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

#include "thermal_core.h"

/*
 * block registers
 * addresses are the offset from .block_base
 */
#define PVTCTLEN			0x0000
#define PVTCTLEN_EN			BIT(0)

#define PVTCTLMODE			0x0004
#define PVTCTLMODE_MASK			0xf
#define PVTCTLMODE_TEMPMON		0x5

#define EMONREPEAT			0x0040
#define EMONREPEAT_ENDLESS		BIT(24)
#define EMONREPEAT_PERIOD		GENMASK(3, 0)
#define EMONREPEAT_PERIOD_1000000	0x9

/*
 * common registers
 * addresses are the offset from .map_base
 */
#define PVTCTLSEL			0x0900
#define PVTCTLSEL_MASK			GENMASK(2, 0)
#define PVTCTLSEL_MONITOR		0

#define SETALERT0			0x0910
#define SETALERT1			0x0914
#define SETALERT2			0x0918
#define SETALERT_TEMP_OVF		(GENMASK(7, 0) << 16)
#define SETALERT_TEMP_OVF_VALUE(val)	(((val) & GENMASK(7, 0)) << 16)
#define SETALERT_EN			BIT(0)

#define PMALERTINTCTL			0x0920
#define PMALERTINTCTL_CLR(ch)		BIT(4 * (ch) + 2)
#define PMALERTINTCTL_SET(ch)		BIT(4 * (ch) + 1)
#define PMALERTINTCTL_EN(ch)		BIT(4 * (ch) + 0)
#define PMALERTINTCTL_MASK		(GENMASK(10, 8) | GENMASK(6, 4) | \
					 GENMASK(2, 0))

#define TMOD				0x0928
#define TMOD_WIDTH			9

#define TMODCOEF			0x0e5c

#define TMODSETUP0_EN			BIT(30)
#define TMODSETUP0_VAL(val)		(((val) & GENMASK(13, 0)) << 16)
#define TMODSETUP1_EN			BIT(15)
#define TMODSETUP1_VAL(val)		((val) & GENMASK(14, 0))

/* SoC critical temperature */
#define CRITICAL_TEMP_LIMIT		(120 * 1000)

/* Max # of alert channels */
#define ALERT_CH_NUM			3

/* SoC specific thermal sensor data */
struct uniphier_tm_soc_data {
	u32 map_base;
	u32 block_base;
	u32 tmod_setup_addr;
};

struct uniphier_tm_dev {
	struct regmap *regmap;
	struct device *dev;
	bool alert_en[ALERT_CH_NUM];
	struct thermal_zone_device *tz_dev;
	const struct uniphier_tm_soc_data *data;
};

static int uniphier_tm_initialize_sensor(struct uniphier_tm_dev *tdev)
{
	struct regmap *map = tdev->regmap;
	u32 val;
	u32 tmod_calib[2];
	int ret;

	/* stop PVT */
	regmap_write_bits(map, tdev->data->block_base + PVTCTLEN,
			  PVTCTLEN_EN, 0);

	/*
	 * Since SoC has a calibrated value that was set in advance,
	 * TMODCOEF shows non-zero and PVT refers the value internally.
	 *
	 * If TMODCOEF shows zero, the boards don't have the calibrated
	 * value, and the driver has to set default value from DT.
	 */
	ret = regmap_read(map, tdev->data->map_base + TMODCOEF, &val);
	if (ret)
		return ret;
	if (!val) {
		/* look for the default values in DT */
		ret = of_property_read_u32_array(tdev->dev->of_node,
						 "socionext,tmod-calibration",
						 tmod_calib,
						 ARRAY_SIZE(tmod_calib));
		if (ret)
			return ret;

		regmap_write(map, tdev->data->tmod_setup_addr,
			TMODSETUP0_EN | TMODSETUP0_VAL(tmod_calib[0]) |
			TMODSETUP1_EN | TMODSETUP1_VAL(tmod_calib[1]));
	}

	/* select temperature mode */
	regmap_write_bits(map, tdev->data->block_base + PVTCTLMODE,
			  PVTCTLMODE_MASK, PVTCTLMODE_TEMPMON);

	/* set monitoring period */
	regmap_write_bits(map, tdev->data->block_base + EMONREPEAT,
			  EMONREPEAT_ENDLESS | EMONREPEAT_PERIOD,
			  EMONREPEAT_ENDLESS | EMONREPEAT_PERIOD_1000000);

	/* set monitor mode */
	regmap_write_bits(map, tdev->data->map_base + PVTCTLSEL,
			  PVTCTLSEL_MASK, PVTCTLSEL_MONITOR);

	return 0;
}

static void uniphier_tm_set_alert(struct uniphier_tm_dev *tdev, u32 ch,
				  u32 temp)
{
	struct regmap *map = tdev->regmap;

	/* set alert temperature */
	regmap_write_bits(map, tdev->data->map_base + SETALERT0 + (ch << 2),
			  SETALERT_EN | SETALERT_TEMP_OVF,
			  SETALERT_EN |
			  SETALERT_TEMP_OVF_VALUE(temp / 1000));
}

static void uniphier_tm_enable_sensor(struct uniphier_tm_dev *tdev)
{
	struct regmap *map = tdev->regmap;
	int i;
	u32 bits = 0;

	for (i = 0; i < ALERT_CH_NUM; i++)
		if (tdev->alert_en[i])
			bits |= PMALERTINTCTL_EN(i);

	/* enable alert interrupt */
	regmap_write_bits(map, tdev->data->map_base + PMALERTINTCTL,
			  PMALERTINTCTL_MASK, bits);

	/* start PVT */
	regmap_write_bits(map, tdev->data->block_base + PVTCTLEN,
			  PVTCTLEN_EN, PVTCTLEN_EN);

	usleep_range(700, 1500);	/* The spec note says at least 700us */
}

static void uniphier_tm_disable_sensor(struct uniphier_tm_dev *tdev)
{
	struct regmap *map = tdev->regmap;

	/* disable alert interrupt */
	regmap_write_bits(map, tdev->data->map_base + PMALERTINTCTL,
			  PMALERTINTCTL_MASK, 0);

	/* stop PVT */
	regmap_write_bits(map, tdev->data->block_base + PVTCTLEN,
			  PVTCTLEN_EN, 0);

	usleep_range(1000, 2000);	/* The spec note says at least 1ms */
}

static int uniphier_tm_get_temp(void *data, int *out_temp)
{
	struct uniphier_tm_dev *tdev = data;
	struct regmap *map = tdev->regmap;
	int ret;
	u32 temp;

	ret = regmap_read(map, tdev->data->map_base + TMOD, &temp);
	if (ret)
		return ret;

	/* MSB of the TMOD field is a sign bit */
	*out_temp = sign_extend32(temp, TMOD_WIDTH - 1) * 1000;

	return 0;
}

static const struct thermal_zone_of_device_ops uniphier_of_thermal_ops = {
	.get_temp = uniphier_tm_get_temp,
};

static void uniphier_tm_irq_clear(struct uniphier_tm_dev *tdev)
{
	u32 mask = 0, bits = 0;
	int i;

	for (i = 0; i < ALERT_CH_NUM; i++) {
		mask |= (PMALERTINTCTL_CLR(i) | PMALERTINTCTL_SET(i));
		bits |= PMALERTINTCTL_CLR(i);
	}

	/* clear alert interrupt */
	regmap_write_bits(tdev->regmap,
			  tdev->data->map_base + PMALERTINTCTL, mask, bits);
}

static irqreturn_t uniphier_tm_alarm_irq(int irq, void *_tdev)
{
	struct uniphier_tm_dev *tdev = _tdev;

	disable_irq_nosync(irq);
	uniphier_tm_irq_clear(tdev);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t uniphier_tm_alarm_irq_thread(int irq, void *_tdev)
{
	struct uniphier_tm_dev *tdev = _tdev;

	thermal_zone_device_update(tdev->tz_dev, THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int uniphier_tm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct device_node *parent;
	struct uniphier_tm_dev *tdev;
	const struct thermal_trip *trips;
	int i, ret, irq, ntrips, crit_temp = INT_MAX;

	tdev = devm_kzalloc(dev, sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;
	tdev->dev = dev;

	tdev->data = of_device_get_match_data(dev);
	if (WARN_ON(!tdev->data))
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/* get regmap from syscon node */
	parent = of_get_parent(dev->of_node); /* parent should be syscon node */
	regmap = syscon_node_to_regmap(parent);
	of_node_put(parent);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to get regmap (error %ld)\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}
	tdev->regmap = regmap;

	ret = uniphier_tm_initialize_sensor(tdev);
	if (ret) {
		dev_err(dev, "failed to initialize sensor\n");
		return ret;
	}

	ret = devm_request_threaded_irq(dev, irq, uniphier_tm_alarm_irq,
					uniphier_tm_alarm_irq_thread,
					0, "thermal", tdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, tdev);

	tdev->tz_dev = devm_thermal_zone_of_sensor_register(dev, 0, tdev,
						&uniphier_of_thermal_ops);
	if (IS_ERR(tdev->tz_dev)) {
		dev_err(dev, "failed to register sensor device\n");
		return PTR_ERR(tdev->tz_dev);
	}

	/* get trip points */
	trips = of_thermal_get_trip_points(tdev->tz_dev);
	ntrips = of_thermal_get_ntrips(tdev->tz_dev);
	if (ntrips > ALERT_CH_NUM) {
		dev_err(dev, "thermal zone has too many trips\n");
		return -E2BIG;
	}

	/* set alert temperatures */
	for (i = 0; i < ntrips; i++) {
		if (trips[i].type == THERMAL_TRIP_CRITICAL &&
		    trips[i].temperature < crit_temp)
			crit_temp = trips[i].temperature;
		uniphier_tm_set_alert(tdev, i, trips[i].temperature);
		tdev->alert_en[i] = true;
	}
	if (crit_temp > CRITICAL_TEMP_LIMIT) {
		dev_err(dev, "critical trip is over limit(>%d), or not set\n",
			CRITICAL_TEMP_LIMIT);
		return -EINVAL;
	}

	uniphier_tm_enable_sensor(tdev);

	return 0;
}

static int uniphier_tm_remove(struct platform_device *pdev)
{
	struct uniphier_tm_dev *tdev = platform_get_drvdata(pdev);

	/* disable sensor */
	uniphier_tm_disable_sensor(tdev);

	return 0;
}

static const struct uniphier_tm_soc_data uniphier_pxs2_tm_data = {
	.map_base        = 0xe000,
	.block_base      = 0xe000,
	.tmod_setup_addr = 0xe904,
};

static const struct uniphier_tm_soc_data uniphier_ld20_tm_data = {
	.map_base        = 0xe000,
	.block_base      = 0xe800,
	.tmod_setup_addr = 0xe938,
};

static const struct of_device_id uniphier_tm_dt_ids[] = {
	{
		.compatible = "socionext,uniphier-pxs2-thermal",
		.data       = &uniphier_pxs2_tm_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-thermal",
		.data       = &uniphier_ld20_tm_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_tm_dt_ids);

static struct platform_driver uniphier_tm_driver = {
	.probe = uniphier_tm_probe,
	.remove = uniphier_tm_remove,
	.driver = {
		.name = "uniphier-thermal",
		.of_match_table = uniphier_tm_dt_ids,
	},
};
module_platform_driver(uniphier_tm_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("UniPhier thermal driver");
MODULE_LICENSE("GPL v2");
