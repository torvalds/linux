// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * amc6821.c - Part of lm_sensors, Linux kernel modules for hardware
 *	       monitoring
 * Copyright (C) 2009 T. Mertelj <tomaz.mertelj@guest.arnes.si>
 *
 * Based on max6650.c:
 * Copyright (C) 2007 Hans J. Koch <hjk@hansjkoch.de>
 *
 * Conversion to regmap and with_info API:
 * Copyright (C) 2024 Guenter Roeck <linux@roeck-us.net>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/*
 * Addresses to scan.
 */

static const unsigned short normal_i2c[] = {0x18, 0x19, 0x1a, 0x2c, 0x2d, 0x2e,
	0x4c, 0x4d, 0x4e, I2C_CLIENT_END};

/*
 * Insmod parameters
 */

static int pwminv;	/*Inverted PWM output. */
module_param(pwminv, int, 0444);

static int init = 1; /*Power-on initialization.*/
module_param(init, int, 0444);

#define AMC6821_REG_DEV_ID		0x3D
#define AMC6821_REG_COMP_ID		0x3E
#define AMC6821_REG_CONF1		0x00
#define AMC6821_REG_CONF2		0x01
#define AMC6821_REG_CONF3		0x3F
#define AMC6821_REG_CONF4		0x04
#define AMC6821_REG_STAT1		0x02
#define AMC6821_REG_STAT2		0x03
#define AMC6821_REG_TEMP_LO		0x06
#define AMC6821_REG_TDATA_LOW		0x08
#define AMC6821_REG_TDATA_HI		0x09
#define AMC6821_REG_LTEMP_HI		0x0A
#define AMC6821_REG_RTEMP_HI		0x0B
#define AMC6821_REG_LTEMP_LIMIT_MIN	0x15
#define AMC6821_REG_LTEMP_LIMIT_MAX	0x14
#define AMC6821_REG_RTEMP_LIMIT_MIN	0x19
#define AMC6821_REG_RTEMP_LIMIT_MAX	0x18
#define AMC6821_REG_LTEMP_CRIT		0x1B
#define AMC6821_REG_RTEMP_CRIT		0x1D
#define AMC6821_REG_PSV_TEMP		0x1C
#define AMC6821_REG_DCY			0x22
#define AMC6821_REG_LTEMP_FAN_CTRL	0x24
#define AMC6821_REG_RTEMP_FAN_CTRL	0x25
#define AMC6821_REG_DCY_LOW_TEMP	0x21

#define AMC6821_REG_TACH_LLIMITL	0x10
#define AMC6821_REG_TACH_HLIMITL	0x12
#define AMC6821_REG_TACH_SETTINGL	0x1e

#define AMC6821_CONF1_START		BIT(0)
#define AMC6821_CONF1_FAN_INT_EN	BIT(1)
#define AMC6821_CONF1_FANIE		BIT(2)
#define AMC6821_CONF1_PWMINV		BIT(3)
#define AMC6821_CONF1_FAN_FAULT_EN	BIT(4)
#define AMC6821_CONF1_FDRC0		BIT(5)
#define AMC6821_CONF1_FDRC1		BIT(6)
#define AMC6821_CONF1_THERMOVIE		BIT(7)

#define AMC6821_CONF2_PWM_EN		BIT(0)
#define AMC6821_CONF2_TACH_MODE		BIT(1)
#define AMC6821_CONF2_TACH_EN		BIT(2)
#define AMC6821_CONF2_RTFIE		BIT(3)
#define AMC6821_CONF2_LTOIE		BIT(4)
#define AMC6821_CONF2_RTOIE		BIT(5)
#define AMC6821_CONF2_PSVIE		BIT(6)
#define AMC6821_CONF2_RST		BIT(7)

#define AMC6821_CONF3_THERM_FAN_EN	BIT(7)
#define AMC6821_CONF3_REV_MASK		GENMASK(3, 0)

#define AMC6821_CONF4_OVREN		BIT(4)
#define AMC6821_CONF4_TACH_FAST		BIT(5)
#define AMC6821_CONF4_PSPR		BIT(6)
#define AMC6821_CONF4_MODE		BIT(7)

#define AMC6821_STAT1_RPM_ALARM		BIT(0)
#define AMC6821_STAT1_FANS		BIT(1)
#define AMC6821_STAT1_RTH		BIT(2)
#define AMC6821_STAT1_RTL		BIT(3)
#define AMC6821_STAT1_R_THERM		BIT(4)
#define AMC6821_STAT1_RTF		BIT(5)
#define AMC6821_STAT1_LTH		BIT(6)
#define AMC6821_STAT1_LTL		BIT(7)

#define AMC6821_STAT2_RTC		BIT(3)
#define AMC6821_STAT2_LTC		BIT(4)
#define AMC6821_STAT2_LPSV		BIT(5)
#define AMC6821_STAT2_L_THERM		BIT(6)
#define AMC6821_STAT2_THERM_IN		BIT(7)

#define AMC6821_TEMP_SLOPE_MASK		GENMASK(2, 0)
#define AMC6821_TEMP_LIMIT_MASK		GENMASK(7, 3)

/*
 * Client data (each client gets its own)
 */

struct amc6821_data {
	struct regmap *regmap;
	struct mutex update_lock;
};

/*
 * Return 0 on success or negative error code.
 *
 * temps returns set of three temperatures, in °C:
 * temps[0]: Passive cooling temperature, applies to both channels
 * temps[1]: Low temperature, start slope calculations
 * temps[2]: High temperature
 *
 * Channel 0: local, channel 1: remote.
 */
static int amc6821_get_auto_point_temps(struct regmap *regmap, int channel, u8 *temps)
{
	u32 regs[] = {
		AMC6821_REG_DCY_LOW_TEMP,
		AMC6821_REG_PSV_TEMP,
		channel ? AMC6821_REG_RTEMP_FAN_CTRL : AMC6821_REG_LTEMP_FAN_CTRL
	};
	u8 regvals[3];
	int slope;
	int err;

	err = regmap_multi_reg_read(regmap, regs, regvals, 3);
	if (err)
		return err;
	temps[0] = regvals[1];
	temps[1] = FIELD_GET(AMC6821_TEMP_LIMIT_MASK, regvals[2]) * 4;

	/* slope is 32 >> <slope bits> in °C */
	slope = 32 >> FIELD_GET(AMC6821_TEMP_SLOPE_MASK, regvals[2]);
	if (slope)
		temps[2] = temps[1] + DIV_ROUND_CLOSEST(255 - regvals[0], slope);
	else
		temps[2] = 255;

	return 0;
}

static int amc6821_temp_read_values(struct regmap *regmap, u32 attr, int channel, long *val)
{
	int reg, err;
	u32 regval;

	switch (attr) {
	case hwmon_temp_input:
		reg = channel ? AMC6821_REG_RTEMP_HI : AMC6821_REG_LTEMP_HI;
		break;
	case hwmon_temp_min:
		reg = channel ? AMC6821_REG_RTEMP_LIMIT_MIN : AMC6821_REG_LTEMP_LIMIT_MIN;
		break;
	case hwmon_temp_max:
		reg = channel ? AMC6821_REG_RTEMP_LIMIT_MAX : AMC6821_REG_LTEMP_LIMIT_MAX;
		break;
	case hwmon_temp_crit:
		reg = channel ? AMC6821_REG_RTEMP_CRIT : AMC6821_REG_LTEMP_CRIT;
		break;
	default:
		return -EOPNOTSUPP;
	}
	err = regmap_read(regmap, reg, &regval);
	if (err)
		return err;
	*val = sign_extend32(regval, 7) * 1000;
	return 0;
}

static int amc6821_read_alarms(struct regmap *regmap, enum hwmon_sensor_types type,
			       u32 attr, int channel, long *val)
{
	int reg, mask, err;
	u32 regval;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_min_alarm:
			reg = AMC6821_REG_STAT1;
			mask = channel ? AMC6821_STAT1_RTL : AMC6821_STAT1_LTL;
			break;
		case hwmon_temp_max_alarm:
			reg = AMC6821_REG_STAT1;
			mask = channel ? AMC6821_STAT1_RTH : AMC6821_STAT1_LTH;
			break;
		case hwmon_temp_crit_alarm:
			reg = AMC6821_REG_STAT2;
			mask = channel ? AMC6821_STAT2_RTC : AMC6821_STAT2_LTC;
			break;
		case hwmon_temp_fault:
			reg = AMC6821_REG_STAT1;
			mask = AMC6821_STAT1_RTF;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_fault:
			reg = AMC6821_REG_STAT1;
			mask = AMC6821_STAT1_FANS;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}
	err = regmap_read(regmap, reg, &regval);
	if (err)
		return err;
	*val = !!(regval & mask);
	return 0;
}

static int amc6821_temp_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct amc6821_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
		return amc6821_temp_read_values(data->regmap, attr, channel, val);
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_fault:
		return amc6821_read_alarms(data->regmap, hwmon_temp, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int amc6821_temp_write(struct device *dev, u32 attr, int channel, long val)
{
	struct amc6821_data *data = dev_get_drvdata(dev);
	int reg;

	val = DIV_ROUND_CLOSEST(clamp_val(val, -128000, 127000), 1000);

	switch (attr) {
	case hwmon_temp_min:
		reg = channel ? AMC6821_REG_RTEMP_LIMIT_MIN : AMC6821_REG_LTEMP_LIMIT_MIN;
		break;
	case hwmon_temp_max:
		reg = channel ? AMC6821_REG_RTEMP_LIMIT_MAX : AMC6821_REG_LTEMP_LIMIT_MAX;
		break;
	case hwmon_temp_crit:
		reg = channel ? AMC6821_REG_RTEMP_CRIT : AMC6821_REG_LTEMP_CRIT;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return regmap_write(data->regmap, reg, val);
}

static int amc6821_pwm_read(struct device *dev, u32 attr, long *val)
{
	struct amc6821_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u32 regval;
	int err;

	switch (attr) {
	case hwmon_pwm_enable:
		err = regmap_read(regmap, AMC6821_REG_CONF1, &regval);
		if (err)
			return err;
		switch (regval & (AMC6821_CONF1_FDRC0 | AMC6821_CONF1_FDRC1)) {
		case 0:
			*val = 1;	/* manual */
			break;
		case AMC6821_CONF1_FDRC0:
			*val = 4;	/* target rpm (fan1_target) controlled */
			break;
		case AMC6821_CONF1_FDRC1:
			*val = 2;	/* remote temp controlled */
			break;
		default:
			*val = 3;	/* max(local, remote) temp controlled */
			break;
		}
		return 0;
	case hwmon_pwm_mode:
		err = regmap_read(regmap, AMC6821_REG_CONF2, &regval);
		if (err)
			return err;
		*val = !!(regval & AMC6821_CONF2_TACH_MODE);
		return 0;
	case hwmon_pwm_auto_channels_temp:
		err = regmap_read(regmap, AMC6821_REG_CONF1, &regval);
		if (err)
			return err;
		switch (regval & (AMC6821_CONF1_FDRC0 | AMC6821_CONF1_FDRC1)) {
		case 0:
		case AMC6821_CONF1_FDRC0:
			*val = 0;	/* manual or target rpm controlled */
			break;
		case AMC6821_CONF1_FDRC1:
			*val = 2;	/* remote temp controlled */
			break;
		default:
			*val = 3;	/* max(local, remote) temp controlled */
			break;
		}
		return 0;
	case hwmon_pwm_input:
		err = regmap_read(regmap, AMC6821_REG_DCY, &regval);
		if (err)
			return err;
		*val = regval;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int amc6821_pwm_write(struct device *dev, u32 attr, long val)
{
	struct amc6821_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u32 mode;

	switch (attr) {
	case hwmon_pwm_enable:
		switch (val) {
		case 1:
			mode = 0;
			break;
		case 2:
			mode = AMC6821_CONF1_FDRC1;
			break;
		case 3:
			mode = AMC6821_CONF1_FDRC0 | AMC6821_CONF1_FDRC1;
			break;
		case 4:
			mode = AMC6821_CONF1_FDRC0;
			break;
		default:
			return -EINVAL;
		}
		return regmap_update_bits(regmap, AMC6821_REG_CONF1,
					  AMC6821_CONF1_FDRC0 | AMC6821_CONF1_FDRC1,
					  mode);
	case hwmon_pwm_mode:
		if (val < 0 || val > 1)
			return -EINVAL;
		return regmap_update_bits(regmap, AMC6821_REG_CONF2,
					  AMC6821_CONF2_TACH_MODE,
					  val ? AMC6821_CONF2_TACH_MODE : 0);
		break;
	case hwmon_pwm_input:
		if (val < 0 || val > 255)
			return -EINVAL;
		return regmap_write(regmap, AMC6821_REG_DCY, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int amc6821_fan_read_rpm(struct regmap *regmap, u32 attr, long *val)
{
	int reg, err;
	u8 regs[2];
	u32 regval;

	switch (attr) {
	case hwmon_fan_input:
		reg = AMC6821_REG_TDATA_LOW;
		break;
	case hwmon_fan_min:
		reg = AMC6821_REG_TACH_LLIMITL;
		break;
	case hwmon_fan_max:
		reg = AMC6821_REG_TACH_HLIMITL;
		break;
	case hwmon_fan_target:
		reg = AMC6821_REG_TACH_SETTINGL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = regmap_bulk_read(regmap, reg, regs, 2);
	if (err)
		return err;

	regval = (regs[1] << 8) | regs[0];
	*val = regval ? 6000000 / regval : 0;

	return 0;
}

static int amc6821_fan_read(struct device *dev, u32 attr, long *val)
{
	struct amc6821_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u32 regval;
	int err;

	switch (attr) {
	case hwmon_fan_input:
	case hwmon_fan_min:
	case hwmon_fan_max:
	case hwmon_fan_target:
		return amc6821_fan_read_rpm(regmap, attr, val);
	case hwmon_fan_fault:
		return amc6821_read_alarms(regmap, hwmon_fan, attr, 0, val);
	case hwmon_fan_pulses:
		err = regmap_read(regmap, AMC6821_REG_CONF4, &regval);
		if (err)
			return err;
		*val = (regval & AMC6821_CONF4_PSPR) ? 4 : 2;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int amc6821_fan_write(struct device *dev, u32 attr, long val)
{
	struct amc6821_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u8 regs[2];
	int reg;

	if (attr == hwmon_fan_pulses) {
		if (val != 2 && val != 4)
			return -EINVAL;
		return regmap_update_bits(regmap, AMC6821_REG_CONF4,
					 AMC6821_CONF4_PSPR,
					 val == 4 ? AMC6821_CONF4_PSPR : 0);
	}

	if (val < 0)
		return -EINVAL;

	switch (attr) {
	case hwmon_fan_min:
		if (!val)	/* no unlimited minimum speed */
			return -EINVAL;
		reg = AMC6821_REG_TACH_LLIMITL;
		break;
	case hwmon_fan_max:
		reg = AMC6821_REG_TACH_HLIMITL;
		break;
	case hwmon_fan_target:
		if (!val)	/* no unlimited target speed */
			return -EINVAL;
		reg = AMC6821_REG_TACH_SETTINGL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	val = val ? 6000000 / clamp_val(val, 1, 6000000) : 0;
	val = clamp_val(val, 0, 0xffff);

	regs[0] = val & 0xff;
	regs[1] = val >> 8;

	return regmap_bulk_write(data->regmap, reg, regs, 2);
}

static ssize_t temp_auto_point_temp_show(struct device *dev,
					 struct device_attribute *devattr,
					 char *buf)
{
	struct amc6821_data *data = dev_get_drvdata(dev);
	int ix = to_sensor_dev_attr_2(devattr)->index;
	int nr = to_sensor_dev_attr_2(devattr)->nr;
	u8 temps[3];
	int err;

	mutex_lock(&data->update_lock);
	err = amc6821_get_auto_point_temps(data->regmap, nr, temps);
	mutex_unlock(&data->update_lock);
	if (err)
		return err;

	return sysfs_emit(buf, "%d\n", temps[ix] * 1000);
}

static ssize_t pwm1_auto_point_pwm_show(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct amc6821_data *data = dev_get_drvdata(dev);
	int ix = to_sensor_dev_attr(devattr)->index;
	u32 val;
	int err;

	switch (ix) {
	case 0:
		val = 0;
		break;
	case 1:
		err = regmap_read(data->regmap, AMC6821_REG_DCY_LOW_TEMP, &val);
		if (err)
			return err;
		break;
	default:
		val = 255;
		break;
	}
	return sysfs_emit(buf, "%d\n", val);
}

/*
 * Set TEMP[0-4] (low temperature) and SLP[0-2] (slope) of local or remote
 * TEMP-FAN control register.
 *
 * Return 0 on success or negative error code.
 *
 * Channel 0: local, channel 1: remote
 */
static inline int set_slope_register(struct regmap *regmap, int channel, u8 *temps)
{
	u8 regval = FIELD_PREP(AMC6821_TEMP_LIMIT_MASK, temps[1] / 4);
	u8 tmp, dpwm;
	int err, dt;
	u32 pwm;

	err = regmap_read(regmap, AMC6821_REG_DCY_LOW_TEMP, &pwm);
	if (err)
		return err;

	dpwm = 255 - pwm;

	dt = temps[2] - temps[1];
	for (tmp = 4; tmp > 0; tmp--) {
		if (dt * (32 >> tmp) >= dpwm)
			break;
	}
	regval |= FIELD_PREP(AMC6821_TEMP_SLOPE_MASK, tmp);

	return regmap_write(regmap,
			    channel ? AMC6821_REG_RTEMP_FAN_CTRL : AMC6821_REG_LTEMP_FAN_CTRL,
			    regval);
}

static ssize_t temp_auto_point_temp_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct amc6821_data *data = dev_get_drvdata(dev);
	int ix = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;
	struct regmap *regmap = data->regmap;
	u8 temps[3], otemps[3];
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);

	ret = amc6821_get_auto_point_temps(data->regmap, nr, temps);
	if (ret)
		goto unlock;

	switch (ix) {
	case 0:
		/*
		 * Passive cooling temperature. Range limit against low limit
		 * of both channels.
		 */
		ret = amc6821_get_auto_point_temps(data->regmap, 1 - nr, otemps);
		if (ret)
			goto unlock;
		val = DIV_ROUND_CLOSEST(clamp_val(val, 0, 63000), 1000);
		val = clamp_val(val, 0, min(temps[1], otemps[1]));
		ret = regmap_write(regmap, AMC6821_REG_PSV_TEMP, val);
		break;
	case 1:
		/*
		 * Low limit; must be between passive and high limit,
		 * and not exceed 124. Step size is 4 degrees C.
		 */
		val = clamp_val(val, DIV_ROUND_UP(temps[0], 4) * 4000, 124000);
		temps[1] = DIV_ROUND_CLOSEST(val, 4000) * 4;
		val = temps[1] / 4;
		/* Auto-adjust high limit if necessary */
		temps[2] = clamp_val(temps[2], temps[1] + 1, 255);
		ret = set_slope_register(regmap, nr, temps);
		break;
	case 2:
		/* high limit, must be higher than low limit */
		val = clamp_val(val, (temps[1] + 1) * 1000, 255000);
		temps[2] = DIV_ROUND_CLOSEST(val, 1000);
		ret = set_slope_register(regmap, nr, temps);
		break;
	default:
		ret = -EINVAL;
		break;
	}
unlock:
	mutex_unlock(&data->update_lock);
	return ret ? : count;
}

static ssize_t pwm1_auto_point_pwm_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct amc6821_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int i, ret;
	u8 val;

	ret = kstrtou8(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);
	ret = regmap_write(regmap, AMC6821_REG_DCY_LOW_TEMP, val);
	if (ret)
		goto unlock;

	for (i = 0; i < 2; i++) {
		u8 temps[3];

		ret = amc6821_get_auto_point_temps(regmap, i, temps);
		if (ret)
			break;
		ret = set_slope_register(regmap, i, temps);
		if (ret)
			break;
	}
unlock:
	mutex_unlock(&data->update_lock);
	return ret ? : count;
}

static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point1_pwm, pwm1_auto_point_pwm, 0);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point2_pwm, pwm1_auto_point_pwm, 1);
static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point3_pwm, pwm1_auto_point_pwm, 2);
static SENSOR_DEVICE_ATTR_2_RO(temp1_auto_point1_temp, temp_auto_point_temp,
			       0, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_point2_temp, temp_auto_point_temp,
			       0, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_point3_temp, temp_auto_point_temp,
			       0, 2);

static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_point1_temp, temp_auto_point_temp,
			       1, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_point2_temp, temp_auto_point_temp,
			       1, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_point3_temp, temp_auto_point_temp,
			       1, 2);

static struct attribute *amc6821_attrs[] = {
	&sensor_dev_attr_pwm1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point3_temp.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(amc6821);

static int amc6821_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_temp:
		return amc6821_temp_read(dev, attr, channel, val);
	case hwmon_fan:
		return amc6821_fan_read(dev, attr, val);
	case hwmon_pwm:
		return amc6821_pwm_read(dev, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int amc6821_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_temp:
		return amc6821_temp_write(dev, attr, channel, val);
	case hwmon_fan:
		return amc6821_fan_write(dev, attr, val);
	case hwmon_pwm:
		return amc6821_pwm_write(dev, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t amc6821_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_min_alarm:
		case hwmon_temp_max_alarm:
		case hwmon_temp_crit_alarm:
		case hwmon_temp_fault:
			return 0444;
		case hwmon_temp_min:
		case hwmon_temp_max:
		case hwmon_temp_crit:
			return 0644;
		default:
			return 0;
		}
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
		case hwmon_fan_fault:
			return 0444;
		case hwmon_fan_pulses:
		case hwmon_fan_min:
		case hwmon_fan_max:
		case hwmon_fan_target:
			return 0644;
		default:
			return 0;
		}
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_mode:
		case hwmon_pwm_enable:
		case hwmon_pwm_input:
			return 0644;
		case hwmon_pwm_auto_channels_temp:
			return 0444;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static const struct hwmon_channel_info * const amc6821_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM |
			   HWMON_T_FAULT),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_MAX |
			   HWMON_F_TARGET | HWMON_F_PULSES | HWMON_F_FAULT),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE | HWMON_PWM_MODE |
			   HWMON_PWM_AUTO_CHANNELS_TEMP),
	NULL
};

static const struct hwmon_ops amc6821_hwmon_ops = {
	.is_visible = amc6821_is_visible,
	.read = amc6821_read,
	.write = amc6821_write,
};

static const struct hwmon_chip_info amc6821_chip_info = {
	.ops = &amc6821_hwmon_ops,
	.info = amc6821_info,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int amc6821_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int address = client->addr;
	int dev_id, comp_id;

	dev_dbg(&adapter->dev, "amc6821_detect called.\n");

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_dbg(&adapter->dev,
			"amc6821: I2C bus doesn't support byte mode, "
			"skipping.\n");
		return -ENODEV;
	}

	dev_id = i2c_smbus_read_byte_data(client, AMC6821_REG_DEV_ID);
	comp_id = i2c_smbus_read_byte_data(client, AMC6821_REG_COMP_ID);
	if (dev_id != 0x21 || comp_id != 0x49) {
		dev_dbg(&adapter->dev,
			"amc6821: detection failed at 0x%02x.\n",
			address);
		return -ENODEV;
	}

	/*
	 * Bit 7 of the address register is ignored, so we can check the
	 * ID registers again
	 */
	dev_id = i2c_smbus_read_byte_data(client, 0x80 | AMC6821_REG_DEV_ID);
	comp_id = i2c_smbus_read_byte_data(client, 0x80 | AMC6821_REG_COMP_ID);
	if (dev_id != 0x21 || comp_id != 0x49) {
		dev_dbg(&adapter->dev,
			"amc6821: detection failed at 0x%02x.\n",
			address);
		return -ENODEV;
	}

	dev_info(&adapter->dev, "amc6821: chip found at 0x%02x.\n", address);
	strscpy(info->type, "amc6821", I2C_NAME_SIZE);

	return 0;
}

static int amc6821_init_client(struct amc6821_data *data)
{
	struct regmap *regmap = data->regmap;
	int err;

	if (init) {
		err = regmap_set_bits(regmap, AMC6821_REG_CONF4, AMC6821_CONF4_MODE);
		if (err)
			return err;
		err = regmap_clear_bits(regmap, AMC6821_REG_CONF3, AMC6821_CONF3_THERM_FAN_EN);
		if (err)
			return err;
		err = regmap_clear_bits(regmap, AMC6821_REG_CONF2,
					AMC6821_CONF2_RTFIE |
					AMC6821_CONF2_LTOIE |
					AMC6821_CONF2_RTOIE);
		if (err)
			return err;

		err = regmap_update_bits(regmap, AMC6821_REG_CONF1,
					 AMC6821_CONF1_THERMOVIE | AMC6821_CONF1_FANIE |
					 AMC6821_CONF1_START | AMC6821_CONF1_PWMINV,
					 AMC6821_CONF1_START |
					 (pwminv ? AMC6821_CONF1_PWMINV : 0));
		if (err)
			return err;
	}
	return 0;
}

static bool amc6821_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AMC6821_REG_STAT1:
	case AMC6821_REG_STAT2:
	case AMC6821_REG_TEMP_LO:
	case AMC6821_REG_TDATA_LOW:
	case AMC6821_REG_LTEMP_HI:
	case AMC6821_REG_RTEMP_HI:
	case AMC6821_REG_TDATA_HI:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config amc6821_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AMC6821_REG_CONF3,
	.volatile_reg = amc6821_volatile_reg,
	.cache_type = REGCACHE_MAPLE,
};

static int amc6821_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct amc6821_data *data;
	struct device *hwmon_dev;
	struct regmap *regmap;
	int err;

	data = devm_kzalloc(dev, sizeof(struct amc6821_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &amc6821_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to initialize regmap\n");
	data->regmap = regmap;

	err = amc6821_init_client(data);
	if (err)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data, &amc6821_chip_info,
							 amc6821_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id amc6821_id[] = {
	{ "amc6821", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, amc6821_id);

static const struct of_device_id __maybe_unused amc6821_of_match[] = {
	{
		.compatible = "ti,amc6821",
	},
	{ }
};

MODULE_DEVICE_TABLE(of, amc6821_of_match);

static struct i2c_driver amc6821_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name	= "amc6821",
		.of_match_table = of_match_ptr(amc6821_of_match),
	},
	.probe = amc6821_probe,
	.id_table = amc6821_id,
	.detect = amc6821_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(amc6821_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("T. Mertelj <tomaz.mertelj@guest.arnes.si>");
MODULE_DESCRIPTION("Texas Instruments amc6821 hwmon driver");
