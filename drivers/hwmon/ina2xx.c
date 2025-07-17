// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Texas Instruments INA219, INA226 power monitor chips
 *
 * INA219:
 * Zero Drift Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: https://www.ti.com/product/ina219
 *
 * INA220:
 * Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: https://www.ti.com/product/ina220
 *
 * INA226:
 * Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: https://www.ti.com/product/ina226
 *
 * INA230:
 * Bi-directional Current/Power Monitor with I2C Interface
 * Datasheet: https://www.ti.com/product/ina230
 *
 * Copyright (C) 2012 Lothar Felten <lothar.felten@gmail.com>
 * Thanks to Jan Volkering
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/util_macros.h>

/* common register definitions */
#define INA2XX_CONFIG			0x00
#define INA2XX_SHUNT_VOLTAGE		0x01 /* readonly */
#define INA2XX_BUS_VOLTAGE		0x02 /* readonly */
#define INA2XX_POWER			0x03 /* readonly */
#define INA2XX_CURRENT			0x04 /* readonly */
#define INA2XX_CALIBRATION		0x05

/* INA226 register definitions */
#define INA226_MASK_ENABLE		0x06
#define INA226_ALERT_LIMIT		0x07
#define INA226_DIE_ID			0xFF

/* SY24655 register definitions */
#define SY24655_EIN				0x0A
#define SY24655_ACCUM_CONFIG	0x0D
#define INA2XX_MAX_REGISTERS		0x0D

/* settings - depend on use case */
#define INA219_CONFIG_DEFAULT		0x399F	/* PGA=8 */
#define INA226_CONFIG_DEFAULT		0x4527	/* averages=16 */
#define INA260_CONFIG_DEFAULT		0x6527	/* averages=16 */
#define SY24655_CONFIG_DEFAULT		0x4527	/* averages=16 */

/* (only for sy24655) */
#define SY24655_ACCUM_CONFIG_DEFAULT	0x044C	/* continuous mode, clear after read*/

/* worst case is 68.10 ms (~14.6Hz, ina219) */
#define INA2XX_CONVERSION_RATE		15
#define INA2XX_MAX_DELAY		69 /* worst case delay in ms */

#define INA2XX_RSHUNT_DEFAULT		10000
#define INA260_RSHUNT			2000

/* bit mask for reading the averaging setting in the configuration register */
#define INA226_AVG_RD_MASK		GENMASK(11, 9)

#define INA226_READ_AVG(reg)		FIELD_GET(INA226_AVG_RD_MASK, reg)

#define INA226_ALERT_LATCH_ENABLE	BIT(0)
#define INA226_ALERT_POLARITY		BIT(1)

/* bit number of alert functions in Mask/Enable Register */
#define INA226_SHUNT_OVER_VOLTAGE_MASK	BIT(15)
#define INA226_SHUNT_UNDER_VOLTAGE_MASK	BIT(14)
#define INA226_BUS_OVER_VOLTAGE_MASK	BIT(13)
#define INA226_BUS_UNDER_VOLTAGE_MASK	BIT(12)
#define INA226_POWER_OVER_LIMIT_MASK	BIT(11)

/* bit mask for alert config bits of Mask/Enable Register */
#define INA226_ALERT_CONFIG_MASK	GENMASK(15, 10)
#define INA226_ALERT_FUNCTION_FLAG	BIT(4)

/*
 * Both bus voltage and shunt voltage conversion times for ina226 are set
 * to 0b0100 on POR, which translates to 2200 microseconds in total.
 */
#define INA226_TOTAL_CONV_TIME_DEFAULT	2200

static bool ina2xx_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case INA2XX_CONFIG:
	case INA2XX_CALIBRATION:
	case INA226_MASK_ENABLE:
	case INA226_ALERT_LIMIT:
	case SY24655_ACCUM_CONFIG:
		return true;
	default:
		return false;
	}
}

static bool ina2xx_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case INA2XX_SHUNT_VOLTAGE:
	case INA2XX_BUS_VOLTAGE:
	case INA2XX_POWER:
	case INA2XX_CURRENT:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config ina2xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.use_single_write = true,
	.use_single_read = true,
	.max_register = INA2XX_MAX_REGISTERS,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = ina2xx_volatile_reg,
	.writeable_reg = ina2xx_writeable_reg,
};

enum ina2xx_ids { ina219, ina226, ina260, sy24655 };

struct ina2xx_config {
	u16 config_default;
	bool has_alerts;	/* chip supports alerts and limits */
	bool has_ishunt;	/* chip has internal shunt resistor */
	bool has_power_average;	/* chip has internal shunt resistor */
	int calibration_value;
	int shunt_div;
	int bus_voltage_shift;
	int bus_voltage_lsb;	/* uV */
	int power_lsb_factor;
};

struct ina2xx_data {
	const struct ina2xx_config *config;
	enum ina2xx_ids chip;

	long rshunt;
	long current_lsb_uA;
	long power_lsb_uW;
	struct mutex config_lock;
	struct regmap *regmap;
	struct i2c_client *client;
};

static const struct ina2xx_config ina2xx_config[] = {
	[ina219] = {
		.config_default = INA219_CONFIG_DEFAULT,
		.calibration_value = 4096,
		.shunt_div = 100,
		.bus_voltage_shift = 3,
		.bus_voltage_lsb = 4000,
		.power_lsb_factor = 20,
		.has_alerts = false,
		.has_ishunt = false,
		.has_power_average = false,
	},
	[ina226] = {
		.config_default = INA226_CONFIG_DEFAULT,
		.calibration_value = 2048,
		.shunt_div = 400,
		.bus_voltage_shift = 0,
		.bus_voltage_lsb = 1250,
		.power_lsb_factor = 25,
		.has_alerts = true,
		.has_ishunt = false,
		.has_power_average = false,
	},
	[ina260] = {
		.config_default = INA260_CONFIG_DEFAULT,
		.shunt_div = 400,
		.bus_voltage_shift = 0,
		.bus_voltage_lsb = 1250,
		.power_lsb_factor = 8,
		.has_alerts = true,
		.has_ishunt = true,
		.has_power_average = false,
	},
	[sy24655] = {
		.config_default = SY24655_CONFIG_DEFAULT,
		.calibration_value = 4096,
		.shunt_div = 400,
		.bus_voltage_shift = 0,
		.bus_voltage_lsb = 1250,
		.power_lsb_factor = 25,
		.has_alerts = true,
		.has_ishunt = false,
		.has_power_average = true,
	},
};

/*
 * Available averaging rates for ina226. The indices correspond with
 * the bit values expected by the chip (according to the ina226 datasheet,
 * table 3 AVG bit settings, found at
 * https://www.ti.com/lit/ds/symlink/ina226.pdf.
 */
static const int ina226_avg_tab[] = { 1, 4, 16, 64, 128, 256, 512, 1024 };

static int ina226_reg_to_interval(u16 config)
{
	int avg = ina226_avg_tab[INA226_READ_AVG(config)];

	/*
	 * Multiply the total conversion time by the number of averages.
	 * Return the result in milliseconds.
	 */
	return DIV_ROUND_CLOSEST(avg * INA226_TOTAL_CONV_TIME_DEFAULT, 1000);
}

/*
 * Return the new, shifted AVG field value of CONFIG register,
 * to use with regmap_update_bits
 */
static u16 ina226_interval_to_reg(long interval)
{
	int avg, avg_bits;

	/*
	 * The maximum supported interval is 1,024 * (2 * 8.244ms) ~= 16.8s.
	 * Clamp to 32 seconds before calculations to avoid overflows.
	 */
	interval = clamp_val(interval, 0, 32000);

	avg = DIV_ROUND_CLOSEST(interval * 1000,
				INA226_TOTAL_CONV_TIME_DEFAULT);
	avg_bits = find_closest(avg, ina226_avg_tab,
				ARRAY_SIZE(ina226_avg_tab));

	return FIELD_PREP(INA226_AVG_RD_MASK, avg_bits);
}

static int ina2xx_get_value(struct ina2xx_data *data, u8 reg,
			    unsigned int regval)
{
	int val;

	switch (reg) {
	case INA2XX_SHUNT_VOLTAGE:
		/* signed register */
		val = DIV_ROUND_CLOSEST((s16)regval, data->config->shunt_div);
		break;
	case INA2XX_BUS_VOLTAGE:
		val = (regval >> data->config->bus_voltage_shift) *
		  data->config->bus_voltage_lsb;
		val = DIV_ROUND_CLOSEST(val, 1000);
		break;
	case INA2XX_POWER:
		val = regval * data->power_lsb_uW;
		break;
	case INA2XX_CURRENT:
		/* signed register, result in mA */
		val = (s16)regval * data->current_lsb_uA;
		val = DIV_ROUND_CLOSEST(val, 1000);
		break;
	case INA2XX_CALIBRATION:
		val = regval;
		break;
	default:
		/* programmer goofed */
		WARN_ON_ONCE(1);
		val = 0;
		break;
	}

	return val;
}

/*
 * Read and convert register value from chip. If the register value is 0,
 * check if the chip has been power cycled or reset. If so, re-initialize it.
 */
static int ina2xx_read_init(struct device *dev, int reg, long *val)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int ret, retry;

	if (data->config->has_ishunt) {
		/* No calibration needed */
		ret = regmap_read(regmap, reg, &regval);
		if (ret < 0)
			return ret;
		*val = ina2xx_get_value(data, reg, regval);
		return 0;
	}

	for (retry = 5; retry; retry--) {
		ret = regmap_read(regmap, reg, &regval);
		if (ret < 0)
			return ret;

		/*
		 * If the current value in the calibration register is 0, the
		 * power and current registers will also remain at 0. In case
		 * the chip has been reset let's check the calibration
		 * register and reinitialize if needed.
		 * We do that extra read of the calibration register if there
		 * is some hint of a chip reset.
		 */
		if (regval == 0) {
			unsigned int cal;

			ret = regmap_read_bypassed(regmap, INA2XX_CALIBRATION, &cal);
			if (ret < 0)
				return ret;

			if (cal == 0) {
				dev_warn(dev, "chip not calibrated, reinitializing\n");

				regcache_mark_dirty(regmap);
				regcache_sync(regmap);

				/*
				 * Let's make sure the power and current
				 * registers have been updated before trying
				 * again.
				 */
				msleep(INA2XX_MAX_DELAY);
				continue;
			}
		}
		*val = ina2xx_get_value(data, reg, regval);
		return 0;
	}

	/*
	 * If we're here then although all write operations succeeded, the
	 * chip still returns 0 in the calibration register. Nothing more we
	 * can do here.
	 */
	dev_err(dev, "unable to reinitialize the chip\n");
	return -ENODEV;
}

/*
 * Turns alert limit values into register values.
 * Opposite of the formula in ina2xx_get_value().
 */
static u16 ina226_alert_to_reg(struct ina2xx_data *data, int reg, long val)
{
	switch (reg) {
	case INA2XX_SHUNT_VOLTAGE:
		val = clamp_val(val, 0, SHRT_MAX * data->config->shunt_div);
		val *= data->config->shunt_div;
		return clamp_val(val, 0, SHRT_MAX);
	case INA2XX_BUS_VOLTAGE:
		val = clamp_val(val, 0, 200000);
		val = (val * 1000) << data->config->bus_voltage_shift;
		val = DIV_ROUND_CLOSEST(val, data->config->bus_voltage_lsb);
		return clamp_val(val, 0, USHRT_MAX);
	case INA2XX_POWER:
		val = clamp_val(val, 0, UINT_MAX - data->power_lsb_uW);
		val = DIV_ROUND_CLOSEST(val, data->power_lsb_uW);
		return clamp_val(val, 0, USHRT_MAX);
	case INA2XX_CURRENT:
		val = clamp_val(val, INT_MIN / 1000, INT_MAX / 1000);
		/* signed register, result in mA */
		val = DIV_ROUND_CLOSEST(val * 1000, data->current_lsb_uA);
		return clamp_val(val, SHRT_MIN, SHRT_MAX);
	default:
		/* programmer goofed */
		WARN_ON_ONCE(1);
		return 0;
	}
}

static int ina226_alert_limit_read(struct ina2xx_data *data, u32 mask, int reg, long *val)
{
	struct regmap *regmap = data->regmap;
	int regval;
	int ret;

	mutex_lock(&data->config_lock);
	ret = regmap_read(regmap, INA226_MASK_ENABLE, &regval);
	if (ret)
		goto abort;

	if (regval & mask) {
		ret = regmap_read(regmap, INA226_ALERT_LIMIT, &regval);
		if (ret)
			goto abort;
		*val = ina2xx_get_value(data, reg, regval);
	} else {
		*val = 0;
	}
abort:
	mutex_unlock(&data->config_lock);
	return ret;
}

static int ina226_alert_limit_write(struct ina2xx_data *data, u32 mask, int reg, long val)
{
	struct regmap *regmap = data->regmap;
	int ret;

	if (val < 0)
		return -EINVAL;

	/*
	 * Clear all alerts first to avoid accidentally triggering ALERT pin
	 * due to register write sequence. Then, only enable the alert
	 * if the value is non-zero.
	 */
	mutex_lock(&data->config_lock);
	ret = regmap_update_bits(regmap, INA226_MASK_ENABLE,
				 INA226_ALERT_CONFIG_MASK, 0);
	if (ret < 0)
		goto abort;

	ret = regmap_write(regmap, INA226_ALERT_LIMIT,
			   ina226_alert_to_reg(data, reg, val));
	if (ret < 0)
		goto abort;

	if (val)
		ret = regmap_update_bits(regmap, INA226_MASK_ENABLE,
					 INA226_ALERT_CONFIG_MASK, mask);
abort:
	mutex_unlock(&data->config_lock);
	return ret;
}

static int ina2xx_chip_read(struct device *dev, u32 attr, long *val)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	u32 regval;
	int ret;

	switch (attr) {
	case hwmon_chip_update_interval:
		ret = regmap_read(data->regmap, INA2XX_CONFIG, &regval);
		if (ret)
			return ret;

		*val = ina226_reg_to_interval(regval);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ina226_alert_read(struct regmap *regmap, u32 mask, long *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read_bypassed(regmap, INA226_MASK_ENABLE, &regval);
	if (ret)
		return ret;

	*val = (regval & mask) && (regval & INA226_ALERT_FUNCTION_FLAG);

	return 0;
}

static int ina2xx_in_read(struct device *dev, u32 attr, int channel, long *val)
{
	int voltage_reg = channel ? INA2XX_BUS_VOLTAGE : INA2XX_SHUNT_VOLTAGE;
	u32 under_voltage_mask = channel ? INA226_BUS_UNDER_VOLTAGE_MASK
					 : INA226_SHUNT_UNDER_VOLTAGE_MASK;
	u32 over_voltage_mask = channel ? INA226_BUS_OVER_VOLTAGE_MASK
					: INA226_SHUNT_OVER_VOLTAGE_MASK;
	struct ina2xx_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int ret;

	switch (attr) {
	case hwmon_in_input:
		ret = regmap_read(regmap, voltage_reg, &regval);
		if (ret)
			return ret;
		*val = ina2xx_get_value(data, voltage_reg, regval);
		break;
	case hwmon_in_lcrit:
		return ina226_alert_limit_read(data, under_voltage_mask,
					       voltage_reg, val);
	case hwmon_in_crit:
		return ina226_alert_limit_read(data, over_voltage_mask,
					       voltage_reg, val);
	case hwmon_in_lcrit_alarm:
		return ina226_alert_read(regmap, under_voltage_mask, val);
	case hwmon_in_crit_alarm:
		return ina226_alert_read(regmap, over_voltage_mask, val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

/*
 * Configuring the READ_EIN (bit 10) of the ACCUM_CONFIG register to 1
 * can clear accumulator and sample_count after reading the EIN register.
 * This way, the average power between the last read and the current
 * read can be obtained. By combining with accurate time data from
 * outside, the energy consumption during that period can be calculated.
 */
static int sy24655_average_power_read(struct ina2xx_data *data, u8 reg, long *val)
{
	u8 template[6];
	int ret;
	long accumulator_24, sample_count;

	/* 48-bit register read */
	ret = i2c_smbus_read_i2c_block_data(data->client, reg, 6, template);
	if (ret < 0)
		return ret;
	if (ret != 6)
		return -EIO;
	accumulator_24 = ((template[3] << 16) |
				(template[4] << 8) |
				template[5]);
	sample_count = ((template[0] << 16) |
				(template[1] << 8) |
				template[2]);
	if (sample_count <= 0) {
		*val = 0;
		return 0;
	}

	*val = DIV_ROUND_CLOSEST(accumulator_24, sample_count) * data->power_lsb_uW;

	return 0;
}

static int ina2xx_power_read(struct device *dev, u32 attr, long *val)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_power_input:
		return ina2xx_read_init(dev, INA2XX_POWER, val);
	case hwmon_power_average:
		return sy24655_average_power_read(data, SY24655_EIN, val);
	case hwmon_power_crit:
		return ina226_alert_limit_read(data, INA226_POWER_OVER_LIMIT_MASK,
					       INA2XX_POWER, val);
	case hwmon_power_crit_alarm:
		return ina226_alert_read(data->regmap, INA226_POWER_OVER_LIMIT_MASK, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int ina2xx_curr_read(struct device *dev, u32 attr, long *val)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int ret;

	/*
	 * While the chips supported by this driver do not directly support
	 * current limits, they do support setting shunt voltage limits.
	 * The shunt voltage divided by the shunt resistor value is the current.
	 * On top of that, calibration values are set such that in the shunt
	 * voltage register and the current register report the same values.
	 * That means we can report and configure current limits based on shunt
	 * voltage limits.
	 */
	switch (attr) {
	case hwmon_curr_input:
		/*
		 * Since the shunt voltage and the current register report the
		 * same values when the chip is calibrated, we can calculate
		 * the current directly from the shunt voltage without relying
		 * on chip calibration.
		 */
		ret = regmap_read(regmap, INA2XX_SHUNT_VOLTAGE, &regval);
		if (ret)
			return ret;
		*val = ina2xx_get_value(data, INA2XX_CURRENT, regval);
		return 0;
	case hwmon_curr_lcrit:
		return ina226_alert_limit_read(data, INA226_SHUNT_UNDER_VOLTAGE_MASK,
					       INA2XX_CURRENT, val);
	case hwmon_curr_crit:
		return ina226_alert_limit_read(data, INA226_SHUNT_OVER_VOLTAGE_MASK,
					       INA2XX_CURRENT, val);
	case hwmon_curr_lcrit_alarm:
		return ina226_alert_read(regmap, INA226_SHUNT_UNDER_VOLTAGE_MASK, val);
	case hwmon_curr_crit_alarm:
		return ina226_alert_read(regmap, INA226_SHUNT_OVER_VOLTAGE_MASK, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int ina2xx_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return ina2xx_chip_read(dev, attr, val);
	case hwmon_in:
		return ina2xx_in_read(dev, attr, channel, val);
	case hwmon_power:
		return ina2xx_power_read(dev, attr, val);
	case hwmon_curr:
		return ina2xx_curr_read(dev, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int ina2xx_chip_write(struct device *dev, u32 attr, long val)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_chip_update_interval:
		return regmap_update_bits(data->regmap, INA2XX_CONFIG,
					  INA226_AVG_RD_MASK,
					  ina226_interval_to_reg(val));
	default:
		return -EOPNOTSUPP;
	}
}

static int ina2xx_in_write(struct device *dev, u32 attr, int channel, long val)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_in_lcrit:
		return ina226_alert_limit_write(data,
			channel ? INA226_BUS_UNDER_VOLTAGE_MASK : INA226_SHUNT_UNDER_VOLTAGE_MASK,
			channel ? INA2XX_BUS_VOLTAGE : INA2XX_SHUNT_VOLTAGE,
			val);
	case hwmon_in_crit:
		return ina226_alert_limit_write(data,
			channel ? INA226_BUS_OVER_VOLTAGE_MASK : INA226_SHUNT_OVER_VOLTAGE_MASK,
			channel ? INA2XX_BUS_VOLTAGE : INA2XX_SHUNT_VOLTAGE,
			val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ina2xx_power_write(struct device *dev, u32 attr, long val)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_power_crit:
		return ina226_alert_limit_write(data, INA226_POWER_OVER_LIMIT_MASK,
						INA2XX_POWER, val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ina2xx_curr_write(struct device *dev, u32 attr, long val)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_curr_lcrit:
		return ina226_alert_limit_write(data, INA226_SHUNT_UNDER_VOLTAGE_MASK,
						INA2XX_CURRENT, val);
	case hwmon_curr_crit:
		return ina226_alert_limit_write(data, INA226_SHUNT_OVER_VOLTAGE_MASK,
						INA2XX_CURRENT, val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ina2xx_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_chip:
		return ina2xx_chip_write(dev, attr, val);
	case hwmon_in:
		return ina2xx_in_write(dev, attr, channel, val);
	case hwmon_power:
		return ina2xx_power_write(dev, attr, val);
	case hwmon_curr:
		return ina2xx_curr_write(dev, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t ina2xx_is_visible(const void *_data, enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	const struct ina2xx_data *data = _data;
	bool has_alerts = data->config->has_alerts;
	bool has_power_average = data->config->has_power_average;
	enum ina2xx_ids chip = data->chip;

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			return 0444;
		case hwmon_in_lcrit:
		case hwmon_in_crit:
			if (has_alerts)
				return 0644;
			break;
		case hwmon_in_lcrit_alarm:
		case hwmon_in_crit_alarm:
			if (has_alerts)
				return 0444;
			break;
		default:
			break;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			return 0444;
		case hwmon_curr_lcrit:
		case hwmon_curr_crit:
			if (has_alerts)
				return 0644;
			break;
		case hwmon_curr_lcrit_alarm:
		case hwmon_curr_crit_alarm:
			if (has_alerts)
				return 0444;
			break;
		default:
			break;
		}
		break;
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
			return 0444;
		case hwmon_power_crit:
			if (has_alerts)
				return 0644;
			break;
		case hwmon_power_crit_alarm:
			if (has_alerts)
				return 0444;
			break;
		case hwmon_power_average:
			if (has_power_average)
				return 0444;
			break;
		default:
			break;
		}
		break;
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			if (chip == ina226 || chip == ina260)
				return 0644;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info * const ina2xx_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_CRIT | HWMON_I_CRIT_ALARM |
			   HWMON_I_LCRIT | HWMON_I_LCRIT_ALARM,
			   HWMON_I_INPUT | HWMON_I_CRIT | HWMON_I_CRIT_ALARM |
			   HWMON_I_LCRIT | HWMON_I_LCRIT_ALARM
			   ),
	HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT | HWMON_C_CRIT | HWMON_C_CRIT_ALARM |
			   HWMON_C_LCRIT | HWMON_C_LCRIT_ALARM),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_CRIT | HWMON_P_CRIT_ALARM |
			   HWMON_P_AVERAGE),
	NULL
};

static const struct hwmon_ops ina2xx_hwmon_ops = {
	.is_visible = ina2xx_is_visible,
	.read = ina2xx_read,
	.write = ina2xx_write,
};

static const struct hwmon_chip_info ina2xx_chip_info = {
	.ops = &ina2xx_hwmon_ops,
	.info = ina2xx_info,
};

/* shunt resistance */

/*
 * In order to keep calibration register value fixed, the product
 * of current_lsb and shunt_resistor should also be fixed and equal
 * to shunt_voltage_lsb = 1 / shunt_div multiplied by 10^9 in order
 * to keep the scale.
 */
static int ina2xx_set_shunt(struct ina2xx_data *data, unsigned long val)
{
	unsigned int dividend = DIV_ROUND_CLOSEST(1000000000,
						  data->config->shunt_div);
	if (!val || val > dividend)
		return -EINVAL;

	data->rshunt = val;
	data->current_lsb_uA = DIV_ROUND_CLOSEST(dividend, val);
	data->power_lsb_uW = data->config->power_lsb_factor *
			     data->current_lsb_uA;

	return 0;
}

static ssize_t shunt_resistor_show(struct device *dev,
				   struct device_attribute *da, char *buf)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%li\n", data->rshunt);
}

static ssize_t shunt_resistor_store(struct device *dev,
				    struct device_attribute *da,
				    const char *buf, size_t count)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int status;

	status = kstrtoul(buf, 10, &val);
	if (status < 0)
		return status;

	mutex_lock(&data->config_lock);
	status = ina2xx_set_shunt(data, val);
	mutex_unlock(&data->config_lock);
	if (status < 0)
		return status;
	return count;
}

static DEVICE_ATTR_RW(shunt_resistor);

/* pointers to created device attributes */
static struct attribute *ina2xx_attrs[] = {
	&dev_attr_shunt_resistor.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ina2xx);

/*
 * Initialize chip
 */
static int ina2xx_init(struct device *dev, struct ina2xx_data *data)
{
	struct regmap *regmap = data->regmap;
	u32 shunt;
	int ret;

	if (data->config->has_ishunt)
		shunt = INA260_RSHUNT;
	else if (device_property_read_u32(dev, "shunt-resistor", &shunt) < 0)
		shunt = INA2XX_RSHUNT_DEFAULT;

	ret = ina2xx_set_shunt(data, shunt);
	if (ret < 0)
		return ret;

	ret = regmap_write(regmap, INA2XX_CONFIG, data->config->config_default);
	if (ret < 0)
		return ret;

	if (data->config->has_alerts) {
		bool active_high = device_property_read_bool(dev, "ti,alert-polarity-active-high");

		regmap_update_bits(regmap, INA226_MASK_ENABLE,
				   INA226_ALERT_LATCH_ENABLE | INA226_ALERT_POLARITY,
				   INA226_ALERT_LATCH_ENABLE |
						FIELD_PREP(INA226_ALERT_POLARITY, active_high));
	}
	if (data->config->has_power_average) {
		if (data->chip == sy24655) {
			/*
			 * Initialize the power accumulation method to continuous
			 * mode and clear the EIN register after each read of the
			 * EIN register
			 */
			ret = regmap_write(regmap, SY24655_ACCUM_CONFIG,
					   SY24655_ACCUM_CONFIG_DEFAULT);
			if (ret < 0)
				return ret;
		}
	}

	if (data->config->has_ishunt)
		return 0;

	/*
	 * Calibration register is set to the best value, which eliminates
	 * truncation errors on calculating current register in hardware.
	 * According to datasheet (eq. 3) the best values are 2048 for
	 * ina226 and 4096 for ina219. They are hardcoded as calibration_value.
	 */
	return regmap_write(regmap, INA2XX_CALIBRATION,
			    data->config->calibration_value);
}

static int ina2xx_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ina2xx_data *data;
	struct device *hwmon_dev;
	enum ina2xx_ids chip;
	int ret;

	chip = (uintptr_t)i2c_get_match_data(client);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* set the device type */
	data->client = client;
	data->config = &ina2xx_config[chip];
	data->chip = chip;
	mutex_init(&data->config_lock);

	data->regmap = devm_regmap_init_i2c(client, &ina2xx_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	/*
	 * Regulator core returns -ENODEV if the 'vs' is not available.
	 * Hence the check for -ENODEV return code is necessary.
	 */
	ret = devm_regulator_get_enable_optional(dev, "vs");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to enable vs regulator\n");

	ret = ina2xx_init(dev, data);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to configure device\n");

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data, &ina2xx_chip_info,
							 data->config->has_ishunt ?
								NULL : ina2xx_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "power monitor %s (Rshunt = %li uOhm)\n",
		 client->name, data->rshunt);

	return 0;
}

static const struct i2c_device_id ina2xx_id[] = {
	{ "ina219", ina219 },
	{ "ina220", ina219 },
	{ "ina226", ina226 },
	{ "ina230", ina226 },
	{ "ina231", ina226 },
	{ "ina260", ina260 },
	{ "sy24655", sy24655 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ina2xx_id);

static const struct of_device_id __maybe_unused ina2xx_of_match[] = {
	{
		.compatible = "silergy,sy24655",
		.data = (void *)sy24655
	},
	{
		.compatible = "ti,ina219",
		.data = (void *)ina219
	},
	{
		.compatible = "ti,ina220",
		.data = (void *)ina219
	},
	{
		.compatible = "ti,ina226",
		.data = (void *)ina226
	},
	{
		.compatible = "ti,ina230",
		.data = (void *)ina226
	},
	{
		.compatible = "ti,ina231",
		.data = (void *)ina226
	},
	{
		.compatible = "ti,ina260",
		.data = (void *)ina260
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ina2xx_of_match);

static struct i2c_driver ina2xx_driver = {
	.driver = {
		.name	= "ina2xx",
		.of_match_table = of_match_ptr(ina2xx_of_match),
	},
	.probe		= ina2xx_probe,
	.id_table	= ina2xx_id,
};

module_i2c_driver(ina2xx_driver);

MODULE_AUTHOR("Lothar Felten <l-felten@ti.com>");
MODULE_DESCRIPTION("ina2xx driver");
MODULE_LICENSE("GPL");
