// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * nct7802 - Driver for Nuvoton NCT7802Y
 *
 * Copyright (C) 2014  Guenter Roeck <linux@roeck-us.net>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define DRVNAME "nct7802"

static const u8 REG_VOLTAGE[5] = { 0x09, 0x0a, 0x0c, 0x0d, 0x0e };

static const u8 REG_VOLTAGE_LIMIT_LSB[2][5] = {
	{ 0x46, 0x00, 0x40, 0x42, 0x44 },
	{ 0x45, 0x00, 0x3f, 0x41, 0x43 },
};

static const u8 REG_VOLTAGE_LIMIT_MSB[5] = { 0x48, 0x00, 0x47, 0x47, 0x48 };

static const u8 REG_VOLTAGE_LIMIT_MSB_SHIFT[2][5] = {
	{ 0, 0, 4, 0, 4 },
	{ 2, 0, 6, 2, 6 },
};

#define REG_BANK		0x00
#define REG_TEMP_LSB		0x05
#define REG_TEMP_PECI_LSB	0x08
#define REG_VOLTAGE_LOW		0x0f
#define REG_FANCOUNT_LOW	0x13
#define REG_START		0x21
#define REG_MODE		0x22 /* 7.2.32 Mode Selection Register */
#define REG_PECI_ENABLE		0x23
#define REG_FAN_ENABLE		0x24
#define REG_VMON_ENABLE		0x25
#define REG_PWM(x)		(0x60 + (x))
#define REG_SMARTFAN_EN(x)      (0x64 + (x) / 2)
#define SMARTFAN_EN_SHIFT(x)    ((x) % 2 * 4)
#define REG_VENDOR_ID		0xfd
#define REG_CHIP_ID		0xfe
#define REG_VERSION_ID		0xff

/*
 * Resistance temperature detector (RTD) modes according to 7.2.32 Mode
 * Selection Register
 */
#define RTD_MODE_CURRENT	0x1
#define RTD_MODE_THERMISTOR	0x2
#define RTD_MODE_VOLTAGE	0x3

#define MODE_RTD_MASK		0x3
#define MODE_LTD_EN		0x40

/*
 * Bit offset for sensors modes in REG_MODE.
 * Valid for index 0..2, indicating RTD1..3.
 */
#define MODE_BIT_OFFSET_RTD(index) ((index) * 2)

/*
 * Data structures and manipulation thereof
 */

struct nct7802_data {
	struct regmap *regmap;
	struct mutex access_lock; /* for multi-byte read and write operations */
	u8 in_status;
	struct mutex in_alarm_lock;
};

static ssize_t temp_type_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct nct7802_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	unsigned int mode;
	int ret;

	ret = regmap_read(data->regmap, REG_MODE, &mode);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", (mode >> (2 * sattr->index) & 3) + 2);
}

static ssize_t temp_type_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct nct7802_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	unsigned int type;
	int err;

	err = kstrtouint(buf, 0, &type);
	if (err < 0)
		return err;
	if (sattr->index == 2 && type != 4) /* RD3 */
		return -EINVAL;
	if (type < 3 || type > 4)
		return -EINVAL;
	err = regmap_update_bits(data->regmap, REG_MODE,
			3 << 2 * sattr->index, (type - 2) << 2 * sattr->index);
	return err ? : count;
}

static ssize_t pwm_mode_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int ret;

	if (sattr->index > 1)
		return sprintf(buf, "1\n");

	ret = regmap_read(data->regmap, 0x5E, &regval);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", !(regval & (1 << sattr->index)));
}

static ssize_t pwm_show(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	if (!attr->index)
		return sprintf(buf, "255\n");

	ret = regmap_read(data->regmap, attr->index, &val);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", val);
}

static ssize_t pwm_store(struct device *dev, struct device_attribute *devattr,
			 const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	int err;
	u8 val;

	err = kstrtou8(buf, 0, &val);
	if (err < 0)
		return err;

	err = regmap_write(data->regmap, attr->index, val);
	return err ? : count;
}

static ssize_t pwm_enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct nct7802_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	unsigned int reg, enabled;
	int ret;

	ret = regmap_read(data->regmap, REG_SMARTFAN_EN(sattr->index), &reg);
	if (ret < 0)
		return ret;
	enabled = reg >> SMARTFAN_EN_SHIFT(sattr->index) & 1;
	return sprintf(buf, "%u\n", enabled + 1);
}

static ssize_t pwm_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct nct7802_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	u8 val;
	int ret;

	ret = kstrtou8(buf, 0, &val);
	if (ret < 0)
		return ret;
	if (val < 1 || val > 2)
		return -EINVAL;
	ret = regmap_update_bits(data->regmap, REG_SMARTFAN_EN(sattr->index),
				 1 << SMARTFAN_EN_SHIFT(sattr->index),
				 (val - 1) << SMARTFAN_EN_SHIFT(sattr->index));
	return ret ? : count;
}

static int nct7802_read_temp(struct nct7802_data *data,
			     u8 reg_temp, u8 reg_temp_low, int *temp)
{
	unsigned int t1, t2 = 0;
	int err;

	*temp = 0;

	mutex_lock(&data->access_lock);
	err = regmap_read(data->regmap, reg_temp, &t1);
	if (err < 0)
		goto abort;
	t1 <<= 8;
	if (reg_temp_low) {	/* 11 bit data */
		err = regmap_read(data->regmap, reg_temp_low, &t2);
		if (err < 0)
			goto abort;
	}
	t1 |= t2 & 0xe0;
	*temp = (s16)t1 / 32 * 125;
abort:
	mutex_unlock(&data->access_lock);
	return err;
}

static int nct7802_read_fan(struct nct7802_data *data, u8 reg_fan)
{
	unsigned int f1, f2;
	int ret;

	mutex_lock(&data->access_lock);
	ret = regmap_read(data->regmap, reg_fan, &f1);
	if (ret < 0)
		goto abort;
	ret = regmap_read(data->regmap, REG_FANCOUNT_LOW, &f2);
	if (ret < 0)
		goto abort;
	ret = (f1 << 5) | (f2 >> 3);
	/* convert fan count to rpm */
	if (ret == 0x1fff)	/* maximum value, assume fan is stopped */
		ret = 0;
	else if (ret)
		ret = DIV_ROUND_CLOSEST(1350000U, ret);
abort:
	mutex_unlock(&data->access_lock);
	return ret;
}

static int nct7802_read_fan_min(struct nct7802_data *data, u8 reg_fan_low,
				u8 reg_fan_high)
{
	unsigned int f1, f2;
	int ret;

	mutex_lock(&data->access_lock);
	ret = regmap_read(data->regmap, reg_fan_low, &f1);
	if (ret < 0)
		goto abort;
	ret = regmap_read(data->regmap, reg_fan_high, &f2);
	if (ret < 0)
		goto abort;
	ret = f1 | ((f2 & 0xf8) << 5);
	/* convert fan count to rpm */
	if (ret == 0x1fff)	/* maximum value, assume no limit */
		ret = 0;
	else if (ret)
		ret = DIV_ROUND_CLOSEST(1350000U, ret);
	else
		ret = 1350000U;
abort:
	mutex_unlock(&data->access_lock);
	return ret;
}

static int nct7802_write_fan_min(struct nct7802_data *data, u8 reg_fan_low,
				 u8 reg_fan_high, unsigned long limit)
{
	int err;

	if (limit)
		limit = DIV_ROUND_CLOSEST(1350000U, limit);
	else
		limit = 0x1fff;
	limit = clamp_val(limit, 0, 0x1fff);

	mutex_lock(&data->access_lock);
	err = regmap_write(data->regmap, reg_fan_low, limit & 0xff);
	if (err < 0)
		goto abort;

	err = regmap_write(data->regmap, reg_fan_high, (limit & 0x1f00) >> 5);
abort:
	mutex_unlock(&data->access_lock);
	return err;
}

static u8 nct7802_vmul[] = { 4, 2, 2, 2, 2 };

static int nct7802_read_voltage(struct nct7802_data *data, int nr, int index)
{
	unsigned int v1, v2;
	int ret;

	mutex_lock(&data->access_lock);
	if (index == 0) {	/* voltage */
		ret = regmap_read(data->regmap, REG_VOLTAGE[nr], &v1);
		if (ret < 0)
			goto abort;
		ret = regmap_read(data->regmap, REG_VOLTAGE_LOW, &v2);
		if (ret < 0)
			goto abort;
		ret = ((v1 << 2) | (v2 >> 6)) * nct7802_vmul[nr];
	}  else {	/* limit */
		int shift = 8 - REG_VOLTAGE_LIMIT_MSB_SHIFT[index - 1][nr];

		ret = regmap_read(data->regmap,
				  REG_VOLTAGE_LIMIT_LSB[index - 1][nr], &v1);
		if (ret < 0)
			goto abort;
		ret = regmap_read(data->regmap, REG_VOLTAGE_LIMIT_MSB[nr],
				  &v2);
		if (ret < 0)
			goto abort;
		ret = (v1 | ((v2 << shift) & 0x300)) * nct7802_vmul[nr];
	}
abort:
	mutex_unlock(&data->access_lock);
	return ret;
}

static int nct7802_write_voltage(struct nct7802_data *data, int nr, int index,
				 unsigned long voltage)
{
	int shift = 8 - REG_VOLTAGE_LIMIT_MSB_SHIFT[index - 1][nr];
	int err;

	voltage = clamp_val(voltage, 0, 0x3ff * nct7802_vmul[nr]);
	voltage = DIV_ROUND_CLOSEST(voltage, nct7802_vmul[nr]);

	mutex_lock(&data->access_lock);
	err = regmap_write(data->regmap,
			   REG_VOLTAGE_LIMIT_LSB[index - 1][nr],
			   voltage & 0xff);
	if (err < 0)
		goto abort;

	err = regmap_update_bits(data->regmap, REG_VOLTAGE_LIMIT_MSB[nr],
				 0x0300 >> shift, (voltage & 0x0300) >> shift);
abort:
	mutex_unlock(&data->access_lock);
	return err;
}

static ssize_t in_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	int voltage;

	voltage = nct7802_read_voltage(data, sattr->nr, sattr->index);
	if (voltage < 0)
		return voltage;

	return sprintf(buf, "%d\n", voltage);
}

static ssize_t in_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	int index = sattr->index;
	int nr = sattr->nr;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	err = nct7802_write_voltage(data, nr, index, val);
	return err ? : count;
}

static ssize_t in_alarm_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	int volt, min, max, ret;
	unsigned int val;

	mutex_lock(&data->in_alarm_lock);

	/*
	 * The SMI Voltage status register is the only register giving a status
	 * for voltages. A bit is set for each input crossing a threshold, in
	 * both direction, but the "inside" or "outside" limits info is not
	 * available. Also this register is cleared on read.
	 * Note: this is not explicitly spelled out in the datasheet, but
	 * from experiment.
	 * To deal with this we use a status cache with one validity bit and
	 * one status bit for each input. Validity is cleared at startup and
	 * each time the register reports a change, and the status is processed
	 * by software based on current input value and limits.
	 */
	ret = regmap_read(data->regmap, 0x1e, &val); /* SMI Voltage status */
	if (ret < 0)
		goto abort;

	/* invalidate cached status for all inputs crossing a threshold */
	data->in_status &= ~((val & 0x0f) << 4);

	/* if cached status for requested input is invalid, update it */
	if (!(data->in_status & (0x10 << sattr->index))) {
		ret = nct7802_read_voltage(data, sattr->nr, 0);
		if (ret < 0)
			goto abort;
		volt = ret;

		ret = nct7802_read_voltage(data, sattr->nr, 1);
		if (ret < 0)
			goto abort;
		min = ret;

		ret = nct7802_read_voltage(data, sattr->nr, 2);
		if (ret < 0)
			goto abort;
		max = ret;

		if (volt < min || volt > max)
			data->in_status |= (1 << sattr->index);
		else
			data->in_status &= ~(1 << sattr->index);

		data->in_status |= 0x10 << sattr->index;
	}

	ret = sprintf(buf, "%u\n", !!(data->in_status & (1 << sattr->index)));
abort:
	mutex_unlock(&data->in_alarm_lock);
	return ret;
}

static ssize_t temp_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct nct7802_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int err, temp;

	err = nct7802_read_temp(data, sattr->nr, sattr->index, &temp);
	if (err < 0)
		return err;

	return sprintf(buf, "%d\n", temp);
}

static ssize_t temp_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	int nr = sattr->nr;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	val = DIV_ROUND_CLOSEST(clamp_val(val, -128000, 127000), 1000);

	err = regmap_write(data->regmap, nr, val & 0xff);
	return err ? : count;
}

static ssize_t fan_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	int speed;

	speed = nct7802_read_fan(data, sattr->index);
	if (speed < 0)
		return speed;

	return sprintf(buf, "%d\n", speed);
}

static ssize_t fan_min_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	int speed;

	speed = nct7802_read_fan_min(data, sattr->nr, sattr->index);
	if (speed < 0)
		return speed;

	return sprintf(buf, "%d\n", speed);
}

static ssize_t fan_min_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	err = nct7802_write_fan_min(data, sattr->nr, sattr->index, val);
	return err ? : count;
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct nct7802_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int bit = sattr->index;
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, sattr->nr, &val);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", !!(val & (1 << bit)));
}

static ssize_t
beep_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int err;

	err = regmap_read(data->regmap, sattr->nr, &regval);
	if (err)
		return err;

	return sprintf(buf, "%u\n", !!(regval & (1 << sattr->index)));
}

static ssize_t
beep_store(struct device *dev, struct device_attribute *attr, const char *buf,
	   size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct7802_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	if (val > 1)
		return -EINVAL;

	err = regmap_update_bits(data->regmap, sattr->nr, 1 << sattr->index,
				 val ? 1 << sattr->index : 0);
	return err ? : count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_type, temp_type, 0);
static SENSOR_DEVICE_ATTR_2_RO(temp1_input, temp, 0x01, REG_TEMP_LSB);
static SENSOR_DEVICE_ATTR_2_RW(temp1_min, temp, 0x31, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_max, temp, 0x30, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_crit, temp, 0x3a, 0);

static SENSOR_DEVICE_ATTR_RW(temp2_type, temp_type, 1);
static SENSOR_DEVICE_ATTR_2_RO(temp2_input, temp, 0x02, REG_TEMP_LSB);
static SENSOR_DEVICE_ATTR_2_RW(temp2_min, temp, 0x33, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp2_max, temp, 0x32, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp2_crit, temp, 0x3b, 0);

static SENSOR_DEVICE_ATTR_RW(temp3_type, temp_type, 2);
static SENSOR_DEVICE_ATTR_2_RO(temp3_input, temp, 0x03, REG_TEMP_LSB);
static SENSOR_DEVICE_ATTR_2_RW(temp3_min, temp, 0x35, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp3_max, temp, 0x34, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp3_crit, temp, 0x3c, 0);

static SENSOR_DEVICE_ATTR_2_RO(temp4_input, temp, 0x04, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp4_min, temp, 0x37, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp4_max, temp, 0x36, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp4_crit, temp, 0x3d, 0);

static SENSOR_DEVICE_ATTR_2_RO(temp5_input, temp, 0x06, REG_TEMP_PECI_LSB);
static SENSOR_DEVICE_ATTR_2_RW(temp5_min, temp, 0x39, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp5_max, temp, 0x38, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp5_crit, temp, 0x3e, 0);

static SENSOR_DEVICE_ATTR_2_RO(temp6_input, temp, 0x07, REG_TEMP_PECI_LSB);

static SENSOR_DEVICE_ATTR_2_RO(temp1_min_alarm, alarm, 0x18, 0);
static SENSOR_DEVICE_ATTR_2_RO(temp2_min_alarm, alarm, 0x18, 1);
static SENSOR_DEVICE_ATTR_2_RO(temp3_min_alarm, alarm, 0x18, 2);
static SENSOR_DEVICE_ATTR_2_RO(temp4_min_alarm, alarm, 0x18, 3);
static SENSOR_DEVICE_ATTR_2_RO(temp5_min_alarm, alarm, 0x18, 4);

static SENSOR_DEVICE_ATTR_2_RO(temp1_max_alarm, alarm, 0x19, 0);
static SENSOR_DEVICE_ATTR_2_RO(temp2_max_alarm, alarm, 0x19, 1);
static SENSOR_DEVICE_ATTR_2_RO(temp3_max_alarm, alarm, 0x19, 2);
static SENSOR_DEVICE_ATTR_2_RO(temp4_max_alarm, alarm, 0x19, 3);
static SENSOR_DEVICE_ATTR_2_RO(temp5_max_alarm, alarm, 0x19, 4);

static SENSOR_DEVICE_ATTR_2_RO(temp1_crit_alarm, alarm, 0x1b, 0);
static SENSOR_DEVICE_ATTR_2_RO(temp2_crit_alarm, alarm, 0x1b, 1);
static SENSOR_DEVICE_ATTR_2_RO(temp3_crit_alarm, alarm, 0x1b, 2);
static SENSOR_DEVICE_ATTR_2_RO(temp4_crit_alarm, alarm, 0x1b, 3);
static SENSOR_DEVICE_ATTR_2_RO(temp5_crit_alarm, alarm, 0x1b, 4);

static SENSOR_DEVICE_ATTR_2_RO(temp1_fault, alarm, 0x17, 0);
static SENSOR_DEVICE_ATTR_2_RO(temp2_fault, alarm, 0x17, 1);
static SENSOR_DEVICE_ATTR_2_RO(temp3_fault, alarm, 0x17, 2);

static SENSOR_DEVICE_ATTR_2_RW(temp1_beep, beep, 0x5c, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp2_beep, beep, 0x5c, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp3_beep, beep, 0x5c, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp4_beep, beep, 0x5c, 3);
static SENSOR_DEVICE_ATTR_2_RW(temp5_beep, beep, 0x5c, 4);
static SENSOR_DEVICE_ATTR_2_RW(temp6_beep, beep, 0x5c, 5);

static struct attribute *nct7802_temp_attrs[] = {
	&sensor_dev_attr_temp1_type.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_fault.dev_attr.attr,
	&sensor_dev_attr_temp1_beep.dev_attr.attr,

	&sensor_dev_attr_temp2_type.dev_attr.attr,		/* 10 */
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_beep.dev_attr.attr,

	&sensor_dev_attr_temp3_type.dev_attr.attr,		/* 20 */
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_beep.dev_attr.attr,

	&sensor_dev_attr_temp4_input.dev_attr.attr,		/* 30 */
	&sensor_dev_attr_temp4_min.dev_attr.attr,
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp4_crit.dev_attr.attr,
	&sensor_dev_attr_temp4_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_beep.dev_attr.attr,

	&sensor_dev_attr_temp5_input.dev_attr.attr,		/* 38 */
	&sensor_dev_attr_temp5_min.dev_attr.attr,
	&sensor_dev_attr_temp5_max.dev_attr.attr,
	&sensor_dev_attr_temp5_crit.dev_attr.attr,
	&sensor_dev_attr_temp5_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_beep.dev_attr.attr,

	&sensor_dev_attr_temp6_input.dev_attr.attr,		/* 46 */
	&sensor_dev_attr_temp6_beep.dev_attr.attr,

	NULL
};

static umode_t nct7802_temp_is_visible(struct kobject *kobj,
				       struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct7802_data *data = dev_get_drvdata(dev);
	unsigned int reg;
	int err;

	err = regmap_read(data->regmap, REG_MODE, &reg);
	if (err < 0)
		return 0;

	if (index < 10 &&
	    (reg & 03) != 0x01 && (reg & 0x03) != 0x02)		/* RD1 */
		return 0;

	if (index >= 10 && index < 20 &&
	    (reg & 0x0c) != 0x04 && (reg & 0x0c) != 0x08)	/* RD2 */
		return 0;
	if (index >= 20 && index < 30 && (reg & 0x30) != 0x20)	/* RD3 */
		return 0;

	if (index >= 30 && index < 38)				/* local */
		return attr->mode;

	err = regmap_read(data->regmap, REG_PECI_ENABLE, &reg);
	if (err < 0)
		return 0;

	if (index >= 38 && index < 46 && !(reg & 0x01))		/* PECI 0 */
		return 0;

	if (index >= 0x46 && (!(reg & 0x02)))			/* PECI 1 */
		return 0;

	return attr->mode;
}

static const struct attribute_group nct7802_temp_group = {
	.attrs = nct7802_temp_attrs,
	.is_visible = nct7802_temp_is_visible,
};

static SENSOR_DEVICE_ATTR_2_RO(in0_input, in, 0, 0);
static SENSOR_DEVICE_ATTR_2_RW(in0_min, in, 0, 1);
static SENSOR_DEVICE_ATTR_2_RW(in0_max, in, 0, 2);
static SENSOR_DEVICE_ATTR_2_RO(in0_alarm, in_alarm, 0, 3);
static SENSOR_DEVICE_ATTR_2_RW(in0_beep, beep, 0x5a, 3);

static SENSOR_DEVICE_ATTR_2_RO(in1_input, in, 1, 0);

static SENSOR_DEVICE_ATTR_2_RO(in2_input, in, 2, 0);
static SENSOR_DEVICE_ATTR_2_RW(in2_min, in, 2, 1);
static SENSOR_DEVICE_ATTR_2_RW(in2_max, in, 2, 2);
static SENSOR_DEVICE_ATTR_2_RO(in2_alarm, in_alarm, 2, 0);
static SENSOR_DEVICE_ATTR_2_RW(in2_beep, beep, 0x5a, 0);

static SENSOR_DEVICE_ATTR_2_RO(in3_input, in, 3, 0);
static SENSOR_DEVICE_ATTR_2_RW(in3_min, in, 3, 1);
static SENSOR_DEVICE_ATTR_2_RW(in3_max, in, 3, 2);
static SENSOR_DEVICE_ATTR_2_RO(in3_alarm, in_alarm, 3, 1);
static SENSOR_DEVICE_ATTR_2_RW(in3_beep, beep, 0x5a, 1);

static SENSOR_DEVICE_ATTR_2_RO(in4_input, in, 4, 0);
static SENSOR_DEVICE_ATTR_2_RW(in4_min, in, 4, 1);
static SENSOR_DEVICE_ATTR_2_RW(in4_max, in, 4, 2);
static SENSOR_DEVICE_ATTR_2_RO(in4_alarm, in_alarm, 4, 2);
static SENSOR_DEVICE_ATTR_2_RW(in4_beep, beep, 0x5a, 2);

static struct attribute *nct7802_in_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in0_beep.dev_attr.attr,

	&sensor_dev_attr_in1_input.dev_attr.attr,	/* 5 */

	&sensor_dev_attr_in2_input.dev_attr.attr,	/* 6 */
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_beep.dev_attr.attr,

	&sensor_dev_attr_in3_input.dev_attr.attr,	/* 11 */
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_beep.dev_attr.attr,

	&sensor_dev_attr_in4_input.dev_attr.attr,	/* 16 */
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_beep.dev_attr.attr,

	NULL,
};

static umode_t nct7802_in_is_visible(struct kobject *kobj,
				     struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct7802_data *data = dev_get_drvdata(dev);
	unsigned int reg;
	int err;

	if (index < 6)						/* VCC, VCORE */
		return attr->mode;

	err = regmap_read(data->regmap, REG_MODE, &reg);
	if (err < 0)
		return 0;

	if (index >= 6 && index < 11 && (reg & 0x03) != 0x03)	/* VSEN1 */
		return 0;
	if (index >= 11 && index < 16 && (reg & 0x0c) != 0x0c)	/* VSEN2 */
		return 0;
	if (index >= 16 && (reg & 0x30) != 0x30)		/* VSEN3 */
		return 0;

	return attr->mode;
}

static const struct attribute_group nct7802_in_group = {
	.attrs = nct7802_in_attrs,
	.is_visible = nct7802_in_is_visible,
};

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan, 0x10);
static SENSOR_DEVICE_ATTR_2_RW(fan1_min, fan_min, 0x49, 0x4c);
static SENSOR_DEVICE_ATTR_2_RO(fan1_alarm, alarm, 0x1a, 0);
static SENSOR_DEVICE_ATTR_2_RW(fan1_beep, beep, 0x5b, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan, 0x11);
static SENSOR_DEVICE_ATTR_2_RW(fan2_min, fan_min, 0x4a, 0x4d);
static SENSOR_DEVICE_ATTR_2_RO(fan2_alarm, alarm, 0x1a, 1);
static SENSOR_DEVICE_ATTR_2_RW(fan2_beep, beep, 0x5b, 1);
static SENSOR_DEVICE_ATTR_RO(fan3_input, fan, 0x12);
static SENSOR_DEVICE_ATTR_2_RW(fan3_min, fan_min, 0x4b, 0x4e);
static SENSOR_DEVICE_ATTR_2_RO(fan3_alarm, alarm, 0x1a, 2);
static SENSOR_DEVICE_ATTR_2_RW(fan3_beep, beep, 0x5b, 2);

/* 7.2.89 Fan Control Output Type */
static SENSOR_DEVICE_ATTR_RO(pwm1_mode, pwm_mode, 0);
static SENSOR_DEVICE_ATTR_RO(pwm2_mode, pwm_mode, 1);
static SENSOR_DEVICE_ATTR_RO(pwm3_mode, pwm_mode, 2);

/* 7.2.91... Fan Control Output Value */
static SENSOR_DEVICE_ATTR_RW(pwm1, pwm, REG_PWM(0));
static SENSOR_DEVICE_ATTR_RW(pwm2, pwm, REG_PWM(1));
static SENSOR_DEVICE_ATTR_RW(pwm3, pwm, REG_PWM(2));

/* 7.2.95... Temperature to Fan mapping Relationships Register */
static SENSOR_DEVICE_ATTR_RW(pwm1_enable, pwm_enable, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_enable, pwm_enable, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_enable, pwm_enable, 2);

static struct attribute *nct7802_fan_attrs[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_beep.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_beep.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	&sensor_dev_attr_fan3_beep.dev_attr.attr,

	NULL
};

static umode_t nct7802_fan_is_visible(struct kobject *kobj,
				      struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct7802_data *data = dev_get_drvdata(dev);
	int fan = index / 4;	/* 4 attributes per fan */
	unsigned int reg;
	int err;

	err = regmap_read(data->regmap, REG_FAN_ENABLE, &reg);
	if (err < 0 || !(reg & (1 << fan)))
		return 0;

	return attr->mode;
}

static const struct attribute_group nct7802_fan_group = {
	.attrs = nct7802_fan_attrs,
	.is_visible = nct7802_fan_is_visible,
};

static struct attribute *nct7802_pwm_attrs[] = {
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_mode.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_mode.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_mode.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	NULL
};

static const struct attribute_group nct7802_pwm_group = {
	.attrs = nct7802_pwm_attrs,
};

/* 7.2.115... 0x80-0x83, 0x84 Temperature (X-axis) transition */
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point1_temp, temp, 0x80, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point2_temp, temp, 0x81, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point3_temp, temp, 0x82, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point4_temp, temp, 0x83, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point5_temp, temp, 0x84, 0);

/* 7.2.120... 0x85-0x88 PWM (Y-axis) transition */
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point1_pwm, pwm, 0x85);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point2_pwm, pwm, 0x86);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point3_pwm, pwm, 0x87);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point4_pwm, pwm, 0x88);
static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point5_pwm, pwm, 0);

/* 7.2.124 Table 2 X-axis Transition Point 1 Register */
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point1_temp, temp, 0x90, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point2_temp, temp, 0x91, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point3_temp, temp, 0x92, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point4_temp, temp, 0x93, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point5_temp, temp, 0x94, 0);

/* 7.2.129 Table 2 Y-axis Transition Point 1 Register */
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_point1_pwm, pwm, 0x95);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_point2_pwm, pwm, 0x96);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_point3_pwm, pwm, 0x97);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_point4_pwm, pwm, 0x98);
static SENSOR_DEVICE_ATTR_RO(pwm2_auto_point5_pwm, pwm, 0);

/* 7.2.133 Table 3 X-axis Transition Point 1 Register */
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point1_temp, temp, 0xA0, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point2_temp, temp, 0xA1, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point3_temp, temp, 0xA2, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point4_temp, temp, 0xA3, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point5_temp, temp, 0xA4, 0);

/* 7.2.138 Table 3 Y-axis Transition Point 1 Register */
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_point1_pwm, pwm, 0xA5);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_point2_pwm, pwm, 0xA6);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_point3_pwm, pwm, 0xA7);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_point4_pwm, pwm, 0xA8);
static SENSOR_DEVICE_ATTR_RO(pwm3_auto_point5_pwm, pwm, 0);

static struct attribute *nct7802_auto_point_attrs[] = {
	&sensor_dev_attr_pwm1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point5_temp.dev_attr.attr,

	&sensor_dev_attr_pwm1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point5_pwm.dev_attr.attr,

	&sensor_dev_attr_pwm2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point4_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point5_temp.dev_attr.attr,

	&sensor_dev_attr_pwm2_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point5_pwm.dev_attr.attr,

	&sensor_dev_attr_pwm3_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point4_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point5_temp.dev_attr.attr,

	&sensor_dev_attr_pwm3_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point5_pwm.dev_attr.attr,

	NULL
};

static const struct attribute_group nct7802_auto_point_group = {
	.attrs = nct7802_auto_point_attrs,
};

static const struct attribute_group *nct7802_groups[] = {
	&nct7802_temp_group,
	&nct7802_in_group,
	&nct7802_fan_group,
	&nct7802_pwm_group,
	&nct7802_auto_point_group,
	NULL
};

static int nct7802_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	int reg;

	/*
	 * Chip identification registers are only available in bank 0,
	 * so only attempt chip detection if bank 0 is selected
	 */
	reg = i2c_smbus_read_byte_data(client, REG_BANK);
	if (reg != 0x00)
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, REG_VENDOR_ID);
	if (reg != 0x50)
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, REG_CHIP_ID);
	if (reg != 0xc3)
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, REG_VERSION_ID);
	if (reg < 0 || (reg & 0xf0) != 0x20)
		return -ENODEV;

	/* Also validate lower bits of voltage and temperature registers */
	reg = i2c_smbus_read_byte_data(client, REG_TEMP_LSB);
	if (reg < 0 || (reg & 0x1f))
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, REG_TEMP_PECI_LSB);
	if (reg < 0 || (reg & 0x3f))
		return -ENODEV;

	reg = i2c_smbus_read_byte_data(client, REG_VOLTAGE_LOW);
	if (reg < 0 || (reg & 0x3f))
		return -ENODEV;

	strlcpy(info->type, "nct7802", I2C_NAME_SIZE);
	return 0;
}

static bool nct7802_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return (reg != REG_BANK && reg <= 0x20) ||
		(reg >= REG_PWM(0) && reg <= REG_PWM(2));
}

static const struct regmap_config nct7802_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = nct7802_regmap_is_volatile,
};

static int nct7802_get_channel_config(struct device *dev,
				      struct device_node *node, u8 *mode_mask,
				      u8 *mode_val)
{
	u32 reg;
	const char *type_str, *md_str;
	u8 md;

	if (!node->name || of_node_cmp(node->name, "channel"))
		return 0;

	if (of_property_read_u32(node, "reg", &reg)) {
		dev_err(dev, "Could not read reg value for '%s'\n",
			node->full_name);
		return -EINVAL;
	}

	if (reg > 3) {
		dev_err(dev, "Invalid reg (%u) in '%s'\n", reg,
			node->full_name);
		return -EINVAL;
	}

	if (reg == 0) {
		if (!of_device_is_available(node))
			*mode_val &= ~MODE_LTD_EN;
		else
			*mode_val |= MODE_LTD_EN;
		*mode_mask |= MODE_LTD_EN;
		return 0;
	}

	/* At this point we have reg >= 1 && reg <= 3 */

	if (!of_device_is_available(node)) {
		*mode_val &= ~(MODE_RTD_MASK << MODE_BIT_OFFSET_RTD(reg - 1));
		*mode_mask |= MODE_RTD_MASK << MODE_BIT_OFFSET_RTD(reg - 1);
		return 0;
	}

	if (of_property_read_string(node, "sensor-type", &type_str)) {
		dev_err(dev, "No type for '%s'\n", node->full_name);
		return -EINVAL;
	}

	if (!strcmp(type_str, "voltage")) {
		*mode_val |= (RTD_MODE_VOLTAGE & MODE_RTD_MASK)
			     << MODE_BIT_OFFSET_RTD(reg - 1);
		*mode_mask |= MODE_RTD_MASK << MODE_BIT_OFFSET_RTD(reg - 1);
		return 0;
	}

	if (strcmp(type_str, "temperature")) {
		dev_err(dev, "Invalid type '%s' for '%s'\n", type_str,
			node->full_name);
		return -EINVAL;
	}

	if (reg == 3) {
		/* RTD3 only supports thermistor mode */
		md = RTD_MODE_THERMISTOR;
	} else {
		if (of_property_read_string(node, "temperature-mode",
					    &md_str)) {
			dev_err(dev, "No mode for '%s'\n", node->full_name);
			return -EINVAL;
		}

		if (!strcmp(md_str, "thermal-diode"))
			md = RTD_MODE_CURRENT;
		else if (!strcmp(md_str, "thermistor"))
			md = RTD_MODE_THERMISTOR;
		else {
			dev_err(dev, "Invalid mode '%s' for '%s'\n", md_str,
				node->full_name);
			return -EINVAL;
		}
	}

	*mode_val |= (md & MODE_RTD_MASK) << MODE_BIT_OFFSET_RTD(reg - 1);
	*mode_mask |= MODE_RTD_MASK << MODE_BIT_OFFSET_RTD(reg - 1);

	return 0;
}

static int nct7802_configure_channels(struct device *dev,
				      struct nct7802_data *data)
{
	/* Enable local temperature sensor by default */
	u8 mode_mask = MODE_LTD_EN, mode_val = MODE_LTD_EN;
	struct device_node *node;
	int err;

	if (dev->of_node) {
		for_each_child_of_node(dev->of_node, node) {
			err = nct7802_get_channel_config(dev, node, &mode_mask,
							 &mode_val);
			if (err)
				return err;
		}
	}

	return regmap_update_bits(data->regmap, REG_MODE, mode_mask, mode_val);
}

static int nct7802_init_chip(struct device *dev, struct nct7802_data *data)
{
	int err;

	/* Enable ADC */
	err = regmap_update_bits(data->regmap, REG_START, 0x01, 0x01);
	if (err)
		return err;

	err = nct7802_configure_channels(dev, data);
	if (err)
		return err;

	/* Enable Vcore and VCC voltage monitoring */
	return regmap_update_bits(data->regmap, REG_VMON_ENABLE, 0x03, 0x03);
}

static int nct7802_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct nct7802_data *data;
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->regmap = devm_regmap_init_i2c(client, &nct7802_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	mutex_init(&data->access_lock);
	mutex_init(&data->in_alarm_lock);

	ret = nct7802_init_chip(dev, data);
	if (ret < 0)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   nct7802_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const unsigned short nct7802_address_list[] = {
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, I2C_CLIENT_END
};

static const struct i2c_device_id nct7802_idtable[] = {
	{ "nct7802", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nct7802_idtable);

static struct i2c_driver nct7802_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = DRVNAME,
	},
	.detect = nct7802_detect,
	.probe_new = nct7802_probe,
	.id_table = nct7802_idtable,
	.address_list = nct7802_address_list,
};

module_i2c_driver(nct7802_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("NCT7802Y Hardware Monitoring Driver");
MODULE_LICENSE("GPL v2");
