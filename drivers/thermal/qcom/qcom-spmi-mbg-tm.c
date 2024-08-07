// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>
#include <linux/iio/consumer.h>

#include "../thermal_core.h"

#define MBG_TEMP_MON_MM_MON2_FAULT_STATUS 0x50

#define MON_FAULT_STATUS_MASK		GENMASK(7, 6)
#define MON_FAULT_POLARITY_STATUS_MASK	GENMASK(5, 4)

#define MON_FAULT_STATUS_LVL1		BIT(6)
#define MON_FAULT_POLARITY_STATUS_UPR	BIT(4)

#define MON2_LVL1_UP_THRESH		0x59

#define MBG_TEMP_MON_MM_MON2_MISC_CFG	0x5f
#define UP_THRESH_EN			BIT(1)

#define STEP_MV				8
#define MBG_DEFAULT_TEMP_MV		600
#define MBG_TEMP_CONSTANT		1000

struct mbg_tm_chip {
	struct regmap			*map;
	struct device			*dev;
	struct thermal_zone_device	*tz_dev;
	struct mutex                    lock;
	unsigned int			base;
	int				irq;
	int				last_temp;
	bool				last_temp_set;
	struct iio_channel		*adc;
};

struct mbg_map_table {
	int min_temp;
	int max_temp;
	int vtemp0;
	int tc;
	int t0;
};

static const struct mbg_map_table map_table[] = {
	/* minT   maxT   vtemp0   tc    t0 */
	{ -60000, -40000, 4337, 1967, -60000 },
	{ -40000, -20000, 4731, 1964, -40000 },
	{ -20000, 0, 5124, 1957, -20000  },
	{ 0, 20000, 5515, 1949, 0 },
	{ 20000, 40000, 5905, 1940, 20000 },
	{ 40000, 60000, 6293, 1930, 40000 },
	{ 60000, 80000, 6679, 1921, 60000 },
	{ 80000, 100000, 7064, 1910, 80000 },
	{ 100000, 120000, 7446, 1896, 100000 },
	{ 120000, 140000, 7825, 1878, 120000 },
	{ 140000, 160000, 8201, 1859, 140000 },
};

static int mbg_tm_read(struct mbg_tm_chip *chip, u16 addr, int *data)
{
	return regmap_read(chip->map, chip->base + addr, data);
}

static int mbg_tm_write(struct mbg_tm_chip *chip, u16 addr, int data)
{
	return regmap_write(chip->map, chip->base + addr, data);
}

static int mbg_tm_reg_update(struct mbg_tm_chip *chip, u16 addr, u8 mask, u8 val)
{
	return regmap_write_bits(chip->map, chip->base + addr, mask, val);
}

static int mbg_tm_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct mbg_tm_chip *chip = tz->devdata;
	int ret, milli_celsius;

	if (!temp)
		return -EINVAL;

	if (chip->last_temp_set) {
		pr_debug("last_temp: %d\n", chip->last_temp);
		chip->last_temp_set = false;
		*temp = chip->last_temp;
		return 0;
	}

	ret = iio_read_channel_processed(chip->adc, &milli_celsius);
	if (ret < 0) {
		dev_err(chip->dev, "failed to read iio channel %d\n", ret);
		return ret;
	}

	*temp = milli_celsius;

	return 0;
}

static int temp_to_vtemp(int temp)
{
	int idx, vtemp, tc = 0, t0 = 0, vtemp0 = 0;

	for (idx = 0; idx < ARRAY_SIZE(map_table); idx++)
		if (temp >= map_table[idx].min_temp &&
				temp < map_table[idx].max_temp) {
			tc = map_table[idx].tc;
			t0 = map_table[idx].t0;
			vtemp0 = map_table[idx].vtemp0;
			break;
		}

	/*
	 * Formula to calculate vtemp from a given temp
	 * vtemp = (t-t0) * tc + vtemp0
	 * tc, t0 and vtemp0 values are mentioned in the map_table array.
	 */
	vtemp = (temp - t0)/1000 * tc/1000 + vtemp0/10;

	return abs(vtemp - MBG_DEFAULT_TEMP_MV)/STEP_MV;

}

static int mbg_tm_set_trip_temp(struct thermal_zone_device *tz, int low_thresh, int temp)
{
	struct mbg_tm_chip *chip = tz->devdata;
	int ret = 0, vtemp = 0;

	mutex_lock(&chip->lock);

	if (temp != INT_MAX) {
		mbg_tm_reg_update(chip, MBG_TEMP_MON_MM_MON2_MISC_CFG,
					 UP_THRESH_EN, UP_THRESH_EN);
		vtemp = temp_to_vtemp(temp);
		ret = mbg_tm_write(chip, MON2_LVL1_UP_THRESH, vtemp);
		if (ret < 0) {
			mutex_unlock(&chip->lock);
			return ret;
		}
	} else {
		mbg_tm_reg_update(chip, MBG_TEMP_MON_MM_MON2_MISC_CFG,
					UP_THRESH_EN, 0);
	}

	mutex_unlock(&chip->lock);

	/*
	 * Configure the last_temp one degree higher, to ensure the
	 * violated temp is returned to thermal framework after the
	 * violation happens. This is needed to account for the
	 * inaccuracy in the conversion formula used which leads
	 * to the thermal framework setting back the same thresholds
	 * in case the temperature it reads does not show violation.
	 */
	chip->last_temp = temp + MBG_TEMP_CONSTANT;

	return ret;
}

static const struct thermal_zone_device_ops mbg_tm_ops = {
	.get_temp = mbg_tm_get_temp,
	.set_trips = mbg_tm_set_trip_temp,
};

static irqreturn_t mbg_tm_isr(int irq, void *data)
{
	struct mbg_tm_chip *chip = data;
	int ret;
	int val = 0;

	mutex_lock(&chip->lock);

	ret = mbg_tm_read(chip, MBG_TEMP_MON_MM_MON2_FAULT_STATUS, &val);

	mutex_unlock(&chip->lock);

	if (ret < 0)
		return IRQ_HANDLED;

	if ((val & MON_FAULT_STATUS_MASK) & MON_FAULT_STATUS_LVL1) {
		if ((val & MON_FAULT_POLARITY_STATUS_MASK) & MON_FAULT_POLARITY_STATUS_UPR) {
			chip->last_temp_set = true;
			thermal_zone_device_update(chip->tz_dev,
						THERMAL_TRIP_VIOLATED);
			pr_debug("Notifying Thermal, val=%d\n", val);
		} else {
			pr_debug("High trip not violated, ignoring IRQ\n");
		}
	}

	return IRQ_HANDLED;
}

static int mbg_tm_probe(struct platform_device *pdev)
{
	struct mbg_tm_chip *chip;
	struct device_node *node;
	u32 res;
	int ret = 0;

	node = pdev->dev.of_node;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	mutex_init(&chip->lock);

	chip->map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->map)
		return -ENXIO;

	ret = of_property_read_u32(node, "reg", &res);
	if (ret < 0)
		return ret;

	chip->base = res;

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0)
		return chip->irq;

	chip->adc = devm_iio_channel_get(&pdev->dev, "thermal");
	if (IS_ERR(chip->adc))
		return PTR_ERR(chip->adc);

	chip->tz_dev = devm_thermal_of_zone_register(&pdev->dev,
					0, chip, &mbg_tm_ops);
	if (IS_ERR(chip->tz_dev)) {
		dev_err(&pdev->dev, "failed to register sensor\n");
		return PTR_ERR(chip->tz_dev);
	}

	ret = devm_request_threaded_irq(&pdev->dev, chip->irq, NULL,
			mbg_tm_isr, IRQF_ONESHOT, node->name, chip);

	return ret;
}

static const struct of_device_id mbg_tm_match_table[] = {
	{ .compatible = "qcom,spmi-mgb-tm" },
	{ }
};
MODULE_DEVICE_TABLE(of, mbg_tm_match_table);

static struct platform_driver mbg_tm_driver = {
	.driver = {
		.name = "qcom-spmi-mbg-tm",
		.of_match_table = mbg_tm_match_table,
	},
	.probe  = mbg_tm_probe,
};
module_platform_driver(mbg_tm_driver);

MODULE_DESCRIPTION("PMIC MBG Temperature monitor driver");
MODULE_LICENSE("GPL");
