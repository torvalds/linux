// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>
#include <linux/iio/consumer.h>

#define MBG_TEMP_MON2_FAULT_STATUS	0x50

#define MON_FAULT_STATUS_MASK		GENMASK(7, 4)
#define MON_FAULT_LVL1_UPR		0x5

#define MON2_LVL1_UP_THRESH		0x59

#define MBG_TEMP_MON2_MISC_CFG		0x5f
#define MON2_UP_THRESH_EN		BIT(1)

#define MBG_TEMP_STEP_MV		8
#define MBG_TEMP_DEFAULT_TEMP_MV	600
#define MBG_TEMP_CONSTANT		1000
#define MBG_MIN_TRIP_TEMP		25000
#define MBG_MAX_SUPPORTED_TEMP		160000

/**
 * struct mbg_tm_chip - MBG thermal monitor device data.
 * @map: regmap for accessing MBG thermal registers.
 * @dev: mbg_tm_chip device.
 * @tz_dev: thermal zone device registered with the thermal framework.
 * @lock: mbg_tm_chip lock for set trip temperature.
 * @base: base register offset for this MBG instance
 * @irq: interrupt line used to signal threshold events
 * @last_temp: last measured temperature.
 * @last_thres_crossed: indicates whether the last interrupt crossed a threshold
 * @adc: IIO ADC channel used for temperature sensing
 */
struct mbg_tm_chip {
	struct regmap			*map;
	struct device			*dev;
	struct thermal_zone_device	*tz_dev;
	struct mutex                    lock;
	unsigned int			base;
	int				irq;
	int				last_temp;
	bool				last_thres_crossed;
	struct iio_channel		*adc;
};

/**
 * struct mbg_map_table - temperature to voltage mapping entry
 * @min_temp: minimum temperature supported by this mapping entry
 * @vtemp0: reference voltage or ADC code corresponding to the temperature
 * @tc: temperature coefficient used for conversion calculations
 */
struct mbg_map_table {
	int min_temp;
	int vtemp0;
	int tc;
};

static const struct mbg_map_table map_table[] = {
	{ -60000, 4337, 1967 },
	{ -40000, 4731, 1964 },
	{ -20000, 5124, 1957 },
	{ 0,      5515, 1949 },
	{ 20000,  5905, 1940 },
	{ 40000,  6293, 1930 },
	{ 60000,  6679, 1921 },
	{ 80000,  7064, 1910 },
	{ 100000, 7446, 1896 },
	{ 120000, 7825, 1878 },
	{ 140000, 8201, 1859 },
};

static int mbg_tm_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct mbg_tm_chip *chip = thermal_zone_device_priv(tz);
	int ret, milli_celsius;

	scoped_guard(mutex, &chip->lock) {
		if (chip->last_thres_crossed) {
			dev_dbg(chip->dev, "last_temp: %d\n", chip->last_temp);
			chip->last_thres_crossed = false;
			*temp = chip->last_temp;
			return 0;
		}
	}

	ret = iio_read_channel_processed(chip->adc, &milli_celsius);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read iio channel with %d\n", ret);
		return ret;
	}

	*temp = milli_celsius;

	return 0;
}

static int temp_to_vtemp_mv(int temp)
{
	int idx, vtemp, tc = 0, t0 = 0, vtemp0 = 0;

	for (idx = 0; idx < ARRAY_SIZE(map_table); idx++)
		if (temp >= map_table[idx].min_temp &&
		    temp < (map_table[idx].min_temp + 20000)) {
			tc = map_table[idx].tc;
			t0 = map_table[idx].min_temp;
			vtemp0 = map_table[idx].vtemp0;
			break;
		}

	/*
	 * Formula to calculate vtemp(mV) from a given temp
	 * vtemp = (temp - minT) * tc + vtemp0
	 * tc, t0 and vtemp0 values are mentioned in the map_table array.
	 */
	vtemp = ((temp - t0) * tc + vtemp0 * 100000) / 1000000;

	/* step size is 8mV */
	return abs(vtemp - MBG_TEMP_DEFAULT_TEMP_MV) / MBG_TEMP_STEP_MV;
}

static int mbg_tm_set_trip_temp(struct thermal_zone_device *tz, int low_temp,
				int temp)
{
	struct mbg_tm_chip *chip = thermal_zone_device_priv(tz);
	int ret = 0;

	guard(mutex)(&chip->lock);

	/* The HW has a limitation that the trip set must be above 25C */
	if (temp > MBG_MIN_TRIP_TEMP && temp < MBG_MAX_SUPPORTED_TEMP) {
		ret = regmap_write(chip->map, chip->base + MON2_LVL1_UP_THRESH,
				   temp_to_vtemp_mv(temp));
		if (ret < 0)
			return ret;

		ret = regmap_set_bits(chip->map, chip->base + MBG_TEMP_MON2_MISC_CFG,
				      MON2_UP_THRESH_EN);
		if (ret < 0)
			return ret;
	} else {
		dev_err(chip->dev, "Set trip b/w 25C and 160C\n");
		ret = regmap_clear_bits(chip->map, chip->base + MBG_TEMP_MON2_MISC_CFG,
					MON2_UP_THRESH_EN);
		return -ERANGE;
	}

	/*
	 * Configure the last_temp one degree higher, to ensure the
	 * violated temp is returned to thermal framework when it reads
	 * temperature for the first time after the violation happens.
	 * This is needed to account for the inaccuracy in the conversion
	 * formula used which leads to the thermal framework setting back
	 * the same thresholds in case the temperature it reads does not
	 * show violation.
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
	int ret, val;

	scoped_guard(mutex, &chip->lock) {
		ret = regmap_read(chip->map, chip->base + MBG_TEMP_MON2_FAULT_STATUS, &val);
		if (ret < 0)
			return IRQ_HANDLED;
		if (FIELD_GET(MON_FAULT_STATUS_MASK, val) == MON_FAULT_LVL1_UPR)
			chip->last_thres_crossed = true;
	}

	if (FIELD_GET(MON_FAULT_STATUS_MASK, val) == MON_FAULT_LVL1_UPR) {
		dev_dbg(chip->dev, "Notifying Thermal, fault status=%d\n", val);
		thermal_zone_device_update(chip->tz_dev, THERMAL_TRIP_VIOLATED);
	} else {
		dev_dbg(chip->dev, "Lvl1 upper threshold not violated, ignoring interrupt\n");
	}

	return IRQ_HANDLED;
}

static int mbg_tm_probe(struct platform_device *pdev)
{
	struct mbg_tm_chip *chip;
	struct device_node *node = pdev->dev.of_node;
	u32 res;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	mutex_init(&chip->lock);

	chip->map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->map)
		return -ENXIO;

	ret = device_property_read_u32(chip->dev, "reg", &res);
	if (ret < 0)
		return dev_err_probe(chip->dev, ret, "Couldn't read reg property\n");

	chip->base = res;

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0)
		return dev_err_probe(chip->dev, chip->irq, "Failed to get irq\n");

	chip->adc = devm_iio_channel_get(&pdev->dev, "thermal");
	if (IS_ERR(chip->adc))
		return dev_err_probe(chip->dev, PTR_ERR(chip->adc), "Failed to get adc channel\n");

	chip->tz_dev = devm_thermal_of_zone_register(chip->dev, 0, chip, &mbg_tm_ops);
	if (IS_ERR(chip->tz_dev))
		return dev_err_probe(chip->dev, PTR_ERR(chip->tz_dev),
				     "Failed to register sensor\n");

	return devm_request_threaded_irq(&pdev->dev, chip->irq, NULL, mbg_tm_isr, IRQF_ONESHOT,
					 node->name, chip);
}

static const struct of_device_id mbg_tm_match_table[] = {
	{ .compatible = "qcom,pm8775-mbg-tm" },
	{ }
};
MODULE_DEVICE_TABLE(of, mbg_tm_match_table);

static struct platform_driver mbg_tm_driver = {
	.driver = {
		.name = "qcom-spmi-mbg-tm",
		.of_match_table = mbg_tm_match_table,
	},
	.probe = mbg_tm_probe,
};
module_platform_driver(mbg_tm_driver);

MODULE_DESCRIPTION("PMIC MBG Temperature monitor driver");
MODULE_LICENSE("GPL");
