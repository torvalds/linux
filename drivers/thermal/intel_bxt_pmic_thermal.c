/*
 * Intel Broxton PMIC thermal driver
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/mfd/intel_soc_pmic.h>

#define BXTWC_THRM0IRQ		0x4E04
#define BXTWC_THRM1IRQ		0x4E05
#define BXTWC_THRM2IRQ		0x4E06
#define BXTWC_MTHRM0IRQ		0x4E12
#define BXTWC_MTHRM1IRQ		0x4E13
#define BXTWC_MTHRM2IRQ		0x4E14
#define BXTWC_STHRM0IRQ		0x4F19
#define BXTWC_STHRM1IRQ		0x4F1A
#define BXTWC_STHRM2IRQ		0x4F1B

struct trip_config_map {
	u16 irq_reg;
	u16 irq_en;
	u16 evt_stat;
	u8 irq_mask;
	u8 irq_en_mask;
	u8 evt_mask;
	u8 trip_num;
};

struct thermal_irq_map {
	char handle[20];
	int num_trips;
	const struct trip_config_map *trip_config;
};

struct pmic_thermal_data {
	const struct thermal_irq_map *maps;
	int num_maps;
};

static const struct trip_config_map bxtwc_str0_trip_config[] = {
	{
		.irq_reg = BXTWC_THRM0IRQ,
		.irq_mask = 0x01,
		.irq_en = BXTWC_MTHRM0IRQ,
		.irq_en_mask = 0x01,
		.evt_stat = BXTWC_STHRM0IRQ,
		.evt_mask = 0x01,
		.trip_num = 0
	},
	{
		.irq_reg = BXTWC_THRM0IRQ,
		.irq_mask = 0x10,
		.irq_en = BXTWC_MTHRM0IRQ,
		.irq_en_mask = 0x10,
		.evt_stat = BXTWC_STHRM0IRQ,
		.evt_mask = 0x10,
		.trip_num = 1
	}
};

static const struct trip_config_map bxtwc_str1_trip_config[] = {
	{
		.irq_reg = BXTWC_THRM0IRQ,
		.irq_mask = 0x02,
		.irq_en = BXTWC_MTHRM0IRQ,
		.irq_en_mask = 0x02,
		.evt_stat = BXTWC_STHRM0IRQ,
		.evt_mask = 0x02,
		.trip_num = 0
	},
	{
		.irq_reg = BXTWC_THRM0IRQ,
		.irq_mask = 0x20,
		.irq_en = BXTWC_MTHRM0IRQ,
		.irq_en_mask = 0x20,
		.evt_stat = BXTWC_STHRM0IRQ,
		.evt_mask = 0x20,
		.trip_num = 1
	},
};

static const struct trip_config_map bxtwc_str2_trip_config[] = {
	{
		.irq_reg = BXTWC_THRM0IRQ,
		.irq_mask = 0x04,
		.irq_en = BXTWC_MTHRM0IRQ,
		.irq_en_mask = 0x04,
		.evt_stat = BXTWC_STHRM0IRQ,
		.evt_mask = 0x04,
		.trip_num = 0
	},
	{
		.irq_reg = BXTWC_THRM0IRQ,
		.irq_mask = 0x40,
		.irq_en = BXTWC_MTHRM0IRQ,
		.irq_en_mask = 0x40,
		.evt_stat = BXTWC_STHRM0IRQ,
		.evt_mask = 0x40,
		.trip_num = 1
	},
};

static const struct trip_config_map bxtwc_str3_trip_config[] = {
	{
		.irq_reg = BXTWC_THRM2IRQ,
		.irq_mask = 0x10,
		.irq_en = BXTWC_MTHRM2IRQ,
		.irq_en_mask = 0x10,
		.evt_stat = BXTWC_STHRM2IRQ,
		.evt_mask = 0x10,
		.trip_num = 0
	},
};

static const struct thermal_irq_map bxtwc_thermal_irq_map[] = {
	{
		.handle = "STR0",
		.trip_config = bxtwc_str0_trip_config,
		.num_trips = ARRAY_SIZE(bxtwc_str0_trip_config),
	},
	{
		.handle = "STR1",
		.trip_config = bxtwc_str1_trip_config,
		.num_trips = ARRAY_SIZE(bxtwc_str1_trip_config),
	},
	{
		.handle = "STR2",
		.trip_config = bxtwc_str2_trip_config,
		.num_trips = ARRAY_SIZE(bxtwc_str2_trip_config),
	},
	{
		.handle = "STR3",
		.trip_config = bxtwc_str3_trip_config,
		.num_trips = ARRAY_SIZE(bxtwc_str3_trip_config),
	},
};

static const struct pmic_thermal_data bxtwc_thermal_data = {
	.maps = bxtwc_thermal_irq_map,
	.num_maps = ARRAY_SIZE(bxtwc_thermal_irq_map),
};

static irqreturn_t pmic_thermal_irq_handler(int irq, void *data)
{
	struct platform_device *pdev = data;
	struct thermal_zone_device *tzd;
	struct pmic_thermal_data *td;
	struct intel_soc_pmic *pmic;
	struct regmap *regmap;
	u8 reg_val, mask, irq_stat;
	u16 reg, evt_stat_reg;
	int i, j, ret;

	pmic = dev_get_drvdata(pdev->dev.parent);
	regmap = pmic->regmap;
	td = (struct pmic_thermal_data *)
		platform_get_device_id(pdev)->driver_data;

	/* Resolve thermal irqs */
	for (i = 0; i < td->num_maps; i++) {
		for (j = 0; j < td->maps[i].num_trips; j++) {
			reg = td->maps[i].trip_config[j].irq_reg;
			mask = td->maps[i].trip_config[j].irq_mask;
			/*
			 * Read the irq register to resolve whether the
			 * interrupt was triggered for this sensor
			 */
			if (regmap_read(regmap, reg, &ret))
				return IRQ_HANDLED;

			reg_val = (u8)ret;
			irq_stat = ((u8)ret & mask);

			if (!irq_stat)
				continue;

			/*
			 * Read the status register to find out what
			 * event occurred i.e a high or a low
			 */
			evt_stat_reg = td->maps[i].trip_config[j].evt_stat;
			if (regmap_read(regmap, evt_stat_reg, &ret))
				return IRQ_HANDLED;

			tzd = thermal_zone_get_zone_by_name(td->maps[i].handle);
			if (!IS_ERR(tzd))
				thermal_zone_device_update(tzd,
						THERMAL_EVENT_UNSPECIFIED);

			/* Clear the appropriate irq */
			regmap_write(regmap, reg, reg_val & mask);
		}
	}

	return IRQ_HANDLED;
}

static int pmic_thermal_probe(struct platform_device *pdev)
{
	struct regmap_irq_chip_data *regmap_irq_chip;
	struct pmic_thermal_data *thermal_data;
	int ret, irq, virq, i, j, pmic_irq_count;
	struct intel_soc_pmic *pmic;
	struct regmap *regmap;
	struct device *dev;
	u16 reg;
	u8 mask;

	dev = &pdev->dev;
	pmic = dev_get_drvdata(pdev->dev.parent);
	if (!pmic) {
		dev_err(dev, "Failed to get struct intel_soc_pmic pointer\n");
		return -ENODEV;
	}

	thermal_data = (struct pmic_thermal_data *)
				platform_get_device_id(pdev)->driver_data;
	if (!thermal_data) {
		dev_err(dev, "No thermal data initialized!!\n");
		return -ENODEV;
	}

	regmap = pmic->regmap;
	regmap_irq_chip = pmic->irq_chip_data;

	pmic_irq_count = 0;
	while ((irq = platform_get_irq(pdev, pmic_irq_count)) != -ENXIO) {
		virq = regmap_irq_get_virq(regmap_irq_chip, irq);
		if (virq < 0) {
			dev_err(dev, "failed to get virq by irq %d\n", irq);
			return virq;
		}

		ret = devm_request_threaded_irq(&pdev->dev, virq,
				NULL, pmic_thermal_irq_handler,
				IRQF_ONESHOT, "pmic_thermal", pdev);

		if (ret) {
			dev_err(dev, "request irq(%d) failed: %d\n", virq, ret);
			return ret;
		}
		pmic_irq_count++;
	}

	/* Enable thermal interrupts */
	for (i = 0; i < thermal_data->num_maps; i++) {
		for (j = 0; j < thermal_data->maps[i].num_trips; j++) {
			reg = thermal_data->maps[i].trip_config[j].irq_en;
			mask = thermal_data->maps[i].trip_config[j].irq_en_mask;
			ret = regmap_update_bits(regmap, reg, mask, 0x00);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static const struct platform_device_id pmic_thermal_id_table[] = {
	{
		.name = "bxt_wcove_thermal",
		.driver_data = (kernel_ulong_t)&bxtwc_thermal_data,
	},
	{},
};

static struct platform_driver pmic_thermal_driver = {
	.probe = pmic_thermal_probe,
	.driver = {
		.name = "pmic_thermal",
	},
	.id_table = pmic_thermal_id_table,
};

MODULE_DEVICE_TABLE(platform, pmic_thermal_id_table);
module_platform_driver(pmic_thermal_driver);

MODULE_AUTHOR("Yegnesh S Iyer <yegnesh.s.iyer@intel.com>");
MODULE_DESCRIPTION("Intel Broxton PMIC Thermal Driver");
MODULE_LICENSE("GPL v2");
