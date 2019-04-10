// SPDX-License-Identifier: GPL-2.0
/*
 * Real Time Clock driver for AB-RTCMC-32.768kHz-EOZ9 chip.
 * Copyright (C) 2019 Orolia
 *
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#define ABEOZ9_REG_CTRL1		0x00
#define ABEOZ9_REG_CTRL1_MASK		GENMASK(7, 0)
#define ABEOZ9_REG_CTRL1_WE		BIT(0)
#define ABEOZ9_REG_CTRL1_TE		BIT(1)
#define ABEOZ9_REG_CTRL1_TAR		BIT(2)
#define ABEOZ9_REG_CTRL1_EERE		BIT(3)
#define ABEOZ9_REG_CTRL1_SRON		BIT(4)
#define ABEOZ9_REG_CTRL1_TD0		BIT(5)
#define ABEOZ9_REG_CTRL1_TD1		BIT(6)
#define ABEOZ9_REG_CTRL1_CLKINT		BIT(7)

#define ABEOZ9_REG_CTRL_INT		0x01
#define ABEOZ9_REG_CTRL_INT_AIE		BIT(0)
#define ABEOZ9_REG_CTRL_INT_TIE		BIT(1)
#define ABEOZ9_REG_CTRL_INT_V1IE	BIT(2)
#define ABEOZ9_REG_CTRL_INT_V2IE	BIT(3)
#define ABEOZ9_REG_CTRL_INT_SRIE	BIT(4)

#define ABEOZ9_REG_CTRL_INT_FLAG	0x02
#define ABEOZ9_REG_CTRL_INT_FLAG_AF	BIT(0)
#define ABEOZ9_REG_CTRL_INT_FLAG_TF	BIT(1)
#define ABEOZ9_REG_CTRL_INT_FLAG_V1IF	BIT(2)
#define ABEOZ9_REG_CTRL_INT_FLAG_V2IF	BIT(3)
#define ABEOZ9_REG_CTRL_INT_FLAG_SRF	BIT(4)

#define ABEOZ9_REG_CTRL_STATUS		0x03
#define ABEOZ9_REG_CTRL_STATUS_V1F	BIT(2)
#define ABEOZ9_REG_CTRL_STATUS_V2F	BIT(3)
#define ABEOZ9_REG_CTRL_STATUS_SR	BIT(4)
#define ABEOZ9_REG_CTRL_STATUS_PON	BIT(5)
#define ABEOZ9_REG_CTRL_STATUS_EEBUSY	BIT(7)

#define ABEOZ9_REG_SEC			0x08
#define ABEOZ9_REG_MIN			0x09
#define ABEOZ9_REG_HOURS		0x0A
#define ABEOZ9_HOURS_PM			BIT(6)
#define ABEOZ9_REG_DAYS			0x0B
#define ABEOZ9_REG_WEEKDAYS		0x0C
#define ABEOZ9_REG_MONTHS		0x0D
#define ABEOZ9_REG_YEARS		0x0E

#define ABEOZ9_SEC_LEN			7

#define ABEOZ9_REG_REG_TEMP		0x20
#define ABEOZ953_TEMP_MAX		120
#define ABEOZ953_TEMP_MIN		-60

#define ABEOZ9_REG_EEPROM		0x30
#define ABEOZ9_REG_EEPROM_MASK		GENMASK(8, 0)
#define ABEOZ9_REG_EEPROM_THP		BIT(0)
#define ABEOZ9_REG_EEPROM_THE		BIT(1)
#define ABEOZ9_REG_EEPROM_FD0		BIT(2)
#define ABEOZ9_REG_EEPROM_FD1		BIT(3)
#define ABEOZ9_REG_EEPROM_R1K		BIT(4)
#define ABEOZ9_REG_EEPROM_R5K		BIT(5)
#define ABEOZ9_REG_EEPROM_R20K		BIT(6)
#define ABEOZ9_REG_EEPROM_R80K		BIT(7)

struct abeoz9_rtc_data {
	struct rtc_device *rtc;
	struct regmap *regmap;
	struct device *hwmon_dev;
};

static int abeoz9_check_validity(struct device *dev)
{
	struct abeoz9_rtc_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int ret;
	int val;

	ret = regmap_read(regmap, ABEOZ9_REG_CTRL_STATUS, &val);
	if (ret < 0) {
		dev_err(dev,
			"unable to get CTRL_STATUS register (%d)\n", ret);
		return ret;
	}

	if (val & ABEOZ9_REG_CTRL_STATUS_PON) {
		dev_warn(dev, "power-on reset detected, date is invalid\n");
		return -EINVAL;
	}

	if (val & ABEOZ9_REG_CTRL_STATUS_V1F) {
		dev_warn(dev,
			 "voltage drops below VLOW1 threshold, date is invalid\n");
		return -EINVAL;
	}

	if ((val & ABEOZ9_REG_CTRL_STATUS_V2F)) {
		dev_warn(dev,
			 "voltage drops below VLOW2 threshold, date is invalid\n");
		return -EINVAL;
	}

	return 0;
}

static int abeoz9_reset_validity(struct regmap *regmap)
{
	return regmap_update_bits(regmap, ABEOZ9_REG_CTRL_STATUS,
				  ABEOZ9_REG_CTRL_STATUS_V1F |
				  ABEOZ9_REG_CTRL_STATUS_V2F |
				  ABEOZ9_REG_CTRL_STATUS_PON,
				  0);
}

static int abeoz9_rtc_get_time(struct device *dev, struct rtc_time *tm)
{
	struct abeoz9_rtc_data *data = dev_get_drvdata(dev);
	u8 regs[ABEOZ9_SEC_LEN];
	int ret;

	ret = abeoz9_check_validity(dev);
	if (ret)
		return ret;

	ret = regmap_bulk_read(data->regmap, ABEOZ9_REG_SEC,
			       regs,
			       sizeof(regs));
	if (ret) {
		dev_err(dev, "reading RTC time failed (%d)\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(regs[ABEOZ9_REG_SEC - ABEOZ9_REG_SEC] & 0x7F);
	tm->tm_min = bcd2bin(regs[ABEOZ9_REG_MIN - ABEOZ9_REG_SEC] & 0x7F);

	if (regs[ABEOZ9_REG_HOURS - ABEOZ9_REG_SEC] & ABEOZ9_HOURS_PM) {
		tm->tm_hour =
			bcd2bin(regs[ABEOZ9_REG_HOURS - ABEOZ9_REG_SEC] & 0x1f);
		if (regs[ABEOZ9_REG_HOURS - ABEOZ9_REG_SEC] & ABEOZ9_HOURS_PM)
			tm->tm_hour += 12;
	} else {
		tm->tm_hour = bcd2bin(regs[ABEOZ9_REG_HOURS - ABEOZ9_REG_SEC]);
	}

	tm->tm_mday = bcd2bin(regs[ABEOZ9_REG_DAYS - ABEOZ9_REG_SEC]);
	tm->tm_wday = bcd2bin(regs[ABEOZ9_REG_WEEKDAYS - ABEOZ9_REG_SEC]);
	tm->tm_mon  = bcd2bin(regs[ABEOZ9_REG_MONTHS - ABEOZ9_REG_SEC]) - 1;
	tm->tm_year = bcd2bin(regs[ABEOZ9_REG_YEARS - ABEOZ9_REG_SEC]) + 100;

	return ret;
}

static int abeoz9_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct abeoz9_rtc_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u8 regs[ABEOZ9_SEC_LEN];
	int ret;

	regs[ABEOZ9_REG_SEC - ABEOZ9_REG_SEC] = bin2bcd(tm->tm_sec);
	regs[ABEOZ9_REG_MIN - ABEOZ9_REG_SEC] = bin2bcd(tm->tm_min);
	regs[ABEOZ9_REG_HOURS - ABEOZ9_REG_SEC] = bin2bcd(tm->tm_hour);
	regs[ABEOZ9_REG_DAYS - ABEOZ9_REG_SEC] = bin2bcd(tm->tm_mday);
	regs[ABEOZ9_REG_WEEKDAYS - ABEOZ9_REG_SEC] = bin2bcd(tm->tm_wday);
	regs[ABEOZ9_REG_MONTHS - ABEOZ9_REG_SEC] = bin2bcd(tm->tm_mon + 1);
	regs[ABEOZ9_REG_YEARS - ABEOZ9_REG_SEC] = bin2bcd(tm->tm_year - 100);

	ret = regmap_bulk_write(data->regmap, ABEOZ9_REG_SEC,
				regs,
				sizeof(regs));

	if (ret) {
		dev_err(dev, "set RTC time failed (%d)\n", ret);
		return ret;
	}

	return abeoz9_reset_validity(regmap);
}

static int abeoz9_trickle_parse_dt(struct device_node *node)
{
	u32 ohms = 0;

	if (of_property_read_u32(node, "trickle-resistor-ohms", &ohms))
		return 0;

	switch (ohms) {
	case 1000:
		return ABEOZ9_REG_EEPROM_R1K;
	case 5000:
		return ABEOZ9_REG_EEPROM_R5K;
	case 20000:
		return ABEOZ9_REG_EEPROM_R20K;
	case 80000:
		return ABEOZ9_REG_EEPROM_R80K;
	default:
		return 0;
	}
}

static int abeoz9_rtc_setup(struct device *dev, struct device_node *node)
{
	struct abeoz9_rtc_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int ret;

	/* Enable Self Recovery, Clock for Watch and EEPROM refresh functions */
	ret = regmap_update_bits(regmap, ABEOZ9_REG_CTRL1,
				 ABEOZ9_REG_CTRL1_MASK,
				 ABEOZ9_REG_CTRL1_WE |
				 ABEOZ9_REG_CTRL1_EERE |
				 ABEOZ9_REG_CTRL1_SRON);
	if (ret < 0) {
		dev_err(dev, "unable to set CTRL_1 register (%d)\n", ret);
		return ret;
	}

	ret = regmap_write(regmap, ABEOZ9_REG_CTRL_INT, 0);
	if (ret < 0) {
		dev_err(dev,
			"unable to set control CTRL_INT register (%d)\n",
			ret);
		return ret;
	}

	ret = regmap_write(regmap, ABEOZ9_REG_CTRL_INT_FLAG, 0);
	if (ret < 0) {
		dev_err(dev,
			"unable to set control CTRL_INT_FLAG register (%d)\n",
			ret);
		return ret;
	}

	ret = abeoz9_trickle_parse_dt(node);

	/* Enable built-in termometer */
	ret |= ABEOZ9_REG_EEPROM_THE;

	ret = regmap_update_bits(regmap, ABEOZ9_REG_EEPROM,
				 ABEOZ9_REG_EEPROM_MASK,
				 ret);
	if (ret < 0) {
		dev_err(dev, "unable to set EEPROM register (%d)\n", ret);
		return ret;
	}

	return ret;
}

static const struct rtc_class_ops rtc_ops = {
	.read_time = abeoz9_rtc_get_time,
	.set_time  = abeoz9_rtc_set_time,
};

static const struct regmap_config abeoz9_rtc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

#if IS_REACHABLE(CONFIG_HWMON)

static int abeoz9z3_temp_read(struct device *dev,
			      enum hwmon_sensor_types type,
			      u32 attr, int channel, long *temp)
{
	struct abeoz9_rtc_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int ret;
	unsigned int val;

	ret = regmap_read(regmap, ABEOZ9_REG_CTRL_STATUS, &val);
	if (ret < 0)
		return ret;

	if ((val & ABEOZ9_REG_CTRL_STATUS_V1F) ||
	    (val & ABEOZ9_REG_CTRL_STATUS_V2F)) {
		dev_err(dev,
			"thermometer might be disabled due to low voltage\n");
		return -EINVAL;
	}

	switch (attr) {
	case hwmon_temp_input:
		ret = regmap_read(regmap, ABEOZ9_REG_REG_TEMP, &val);
		if (ret < 0)
			return ret;
		*temp = 1000 * (val + ABEOZ953_TEMP_MIN);
		return 0;
	case hwmon_temp_max:
		*temp = 1000 * ABEOZ953_TEMP_MAX;
		return 0;
	case hwmon_temp_min:
		*temp = 1000 * ABEOZ953_TEMP_MIN;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t abeoz9_is_visible(const void *data,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_max:
	case hwmon_temp_min:
		return 0444;
	default:
		return 0;
	}
}

static const u32 abeoz9_chip_config[] = {
	HWMON_C_REGISTER_TZ,
	0
};

static const struct hwmon_channel_info abeoz9_chip = {
	.type = hwmon_chip,
	.config = abeoz9_chip_config,
};

static const u32 abeoz9_temp_config[] = {
	HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MIN,
	0
};

static const struct hwmon_channel_info abeoz9_temp = {
	.type = hwmon_temp,
	.config = abeoz9_temp_config,
};

static const struct hwmon_channel_info *abeoz9_info[] = {
	&abeoz9_chip,
	&abeoz9_temp,
	NULL
};

static const struct hwmon_ops abeoz9_hwmon_ops = {
	.is_visible = abeoz9_is_visible,
	.read = abeoz9z3_temp_read,
};

static const struct hwmon_chip_info abeoz9_chip_info = {
	.ops = &abeoz9_hwmon_ops,
	.info = abeoz9_info,
};

static void abeoz9_hwmon_register(struct device *dev,
				  struct abeoz9_rtc_data *data)
{
	data->hwmon_dev =
		devm_hwmon_device_register_with_info(dev,
						     "abeoz9",
						     data,
						     &abeoz9_chip_info,
						     NULL);
	if (IS_ERR(data->hwmon_dev)) {
		dev_warn(dev, "unable to register hwmon device %ld\n",
			 PTR_ERR(data->hwmon_dev));
	}
}

#else

static void abeoz9_hwmon_register(struct device *dev,
				  struct abeoz9_rtc_data *data)
{
}

#endif

static int abeoz9_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct abeoz9_rtc_data *data = NULL;
	struct device *dev = &client->dev;
	struct regmap *regmap;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_I2C_BLOCK)) {
		ret = -ENODEV;
		goto err;
	}

	regmap = devm_regmap_init_i2c(client, &abeoz9_rtc_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "regmap allocation failed: %d\n", ret);
		goto err;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err;
	}

	data->regmap = regmap;
	dev_set_drvdata(dev, data);

	ret = abeoz9_rtc_setup(dev, client->dev.of_node);
	if (ret)
		goto err;

	data->rtc = devm_rtc_allocate_device(dev);
	ret = PTR_ERR_OR_ZERO(data->rtc);
	if (ret)
		goto err;

	data->rtc->ops = &rtc_ops;
	data->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	data->rtc->range_max = RTC_TIMESTAMP_END_2099;

	ret = rtc_register_device(data->rtc);
	if (ret)
		goto err;

	abeoz9_hwmon_register(dev, data);
	return 0;

err:
	dev_err(dev, "unable to register RTC device (%d)\n", ret);
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id abeoz9_dt_match[] = {
	{ .compatible = "abracon,abeoz9" },
	{ },
};
MODULE_DEVICE_TABLE(of, abeoz9_dt_match);
#endif

static const struct i2c_device_id abeoz9_id[] = {
	{ "abeoz9", 0 },
	{ }
};

static struct i2c_driver abeoz9_driver = {
	.driver = {
		.name = "rtc-ab-eoz9",
		.of_match_table = of_match_ptr(abeoz9_dt_match),
	},
	.probe	  = abeoz9_probe,
	.id_table = abeoz9_id,
};

module_i2c_driver(abeoz9_driver);

MODULE_AUTHOR("Artem Panfilov <panfilov.artyom@gmail.com>");
MODULE_DESCRIPTION("Abracon AB-RTCMC-32.768kHz-EOZ9 RTC driver");
MODULE_LICENSE("GPL");
