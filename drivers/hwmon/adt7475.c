// SPDX-License-Identifier: GPL-2.0-only
/*
 * adt7475 - Thermal sensor driver for the ADT7475 chip and derivatives
 * Copyright (C) 2007-2008, Advanced Micro Devices, Inc.
 * Copyright (C) 2008 Jordan Crouse <jordan@cosmicpenguin.net>
 * Copyright (C) 2008 Hans de Goede <hdegoede@redhat.com>
 * Copyright (C) 2009 Jean Delvare <jdelvare@suse.de>
 *
 * Derived from the lm83 driver by Jean Delvare
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/util_macros.h>

/* Indexes for the sysfs hooks */

#define INPUT		0
#define MIN		1
#define MAX		2
#define CONTROL		3
#define OFFSET		3
#define AUTOMIN		4
#define THERM		5
#define HYSTERSIS	6

/*
 * These are unique identifiers for the sysfs functions - unlike the
 * numbers above, these are not also indexes into an array
 */

#define ALARM		9
#define FAULT		10

/* 7475 Common Registers */

#define REG_DEVREV2		0x12	/* ADT7490 only */

#define REG_VTT			0x1E	/* ADT7490 only */
#define REG_EXTEND3		0x1F	/* ADT7490 only */

#define REG_VOLTAGE_BASE	0x20
#define REG_TEMP_BASE		0x25
#define REG_TACH_BASE		0x28
#define REG_PWM_BASE		0x30
#define REG_PWM_MAX_BASE	0x38

#define REG_DEVID		0x3D
#define REG_VENDID		0x3E
#define REG_DEVID2		0x3F

#define REG_CONFIG1		0x40

#define REG_STATUS1		0x41
#define REG_STATUS2		0x42

#define REG_VID			0x43	/* ADT7476 only */

#define REG_VOLTAGE_MIN_BASE	0x44
#define REG_VOLTAGE_MAX_BASE	0x45

#define REG_TEMP_MIN_BASE	0x4E
#define REG_TEMP_MAX_BASE	0x4F

#define REG_TACH_MIN_BASE	0x54

#define REG_PWM_CONFIG_BASE	0x5C

#define REG_TEMP_TRANGE_BASE	0x5F

#define REG_ENHANCE_ACOUSTICS1	0x62
#define REG_ENHANCE_ACOUSTICS2	0x63

#define REG_PWM_MIN_BASE	0x64

#define REG_TEMP_TMIN_BASE	0x67
#define REG_TEMP_THERM_BASE	0x6A

#define REG_REMOTE1_HYSTERSIS	0x6D
#define REG_REMOTE2_HYSTERSIS	0x6E

#define REG_TEMP_OFFSET_BASE	0x70

#define REG_CONFIG2		0x73

#define REG_EXTEND1		0x76
#define REG_EXTEND2		0x77

#define REG_CONFIG3		0x78
#define REG_CONFIG5		0x7C
#define REG_CONFIG4		0x7D

#define REG_STATUS4		0x81	/* ADT7490 only */

#define REG_VTT_MIN		0x84	/* ADT7490 only */
#define REG_VTT_MAX		0x86	/* ADT7490 only */

#define VID_VIDSEL		0x80	/* ADT7476 only */

#define CONFIG2_ATTN		0x20

#define CONFIG3_SMBALERT	0x01
#define CONFIG3_THERM		0x02

#define CONFIG4_PINFUNC		0x03
#define CONFIG4_THERM		0x01
#define CONFIG4_SMBALERT	0x02
#define CONFIG4_MAXDUTY		0x08
#define CONFIG4_ATTN_IN10	0x30
#define CONFIG4_ATTN_IN43	0xC0

#define CONFIG5_TWOSCOMP	0x01
#define CONFIG5_TEMPOFFSET	0x02
#define CONFIG5_VIDGPIO		0x10	/* ADT7476 only */

/* ADT7475 Settings */

#define ADT7475_VOLTAGE_COUNT	5	/* Not counting Vtt */
#define ADT7475_TEMP_COUNT	3
#define ADT7475_TACH_COUNT	4
#define ADT7475_PWM_COUNT	3

/* Macro to read the registers */

#define adt7475_read(reg) i2c_smbus_read_byte_data(client, (reg))

/* Macros to easily index the registers */

#define TACH_REG(idx) (REG_TACH_BASE + ((idx) * 2))
#define TACH_MIN_REG(idx) (REG_TACH_MIN_BASE + ((idx) * 2))

#define PWM_REG(idx) (REG_PWM_BASE + (idx))
#define PWM_MAX_REG(idx) (REG_PWM_MAX_BASE + (idx))
#define PWM_MIN_REG(idx) (REG_PWM_MIN_BASE + (idx))
#define PWM_CONFIG_REG(idx) (REG_PWM_CONFIG_BASE + (idx))

#define VOLTAGE_REG(idx) (REG_VOLTAGE_BASE + (idx))
#define VOLTAGE_MIN_REG(idx) (REG_VOLTAGE_MIN_BASE + ((idx) * 2))
#define VOLTAGE_MAX_REG(idx) (REG_VOLTAGE_MAX_BASE + ((idx) * 2))

#define TEMP_REG(idx) (REG_TEMP_BASE + (idx))
#define TEMP_MIN_REG(idx) (REG_TEMP_MIN_BASE + ((idx) * 2))
#define TEMP_MAX_REG(idx) (REG_TEMP_MAX_BASE + ((idx) * 2))
#define TEMP_TMIN_REG(idx) (REG_TEMP_TMIN_BASE + (idx))
#define TEMP_THERM_REG(idx) (REG_TEMP_THERM_BASE + (idx))
#define TEMP_OFFSET_REG(idx) (REG_TEMP_OFFSET_BASE + (idx))
#define TEMP_TRANGE_REG(idx) (REG_TEMP_TRANGE_BASE + (idx))

static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

enum chips { adt7473, adt7475, adt7476, adt7490 };

static const struct i2c_device_id adt7475_id[] = {
	{ "adt7473", adt7473 },
	{ "adt7475", adt7475 },
	{ "adt7476", adt7476 },
	{ "adt7490", adt7490 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adt7475_id);

static const struct of_device_id __maybe_unused adt7475_of_match[] = {
	{
		.compatible = "adi,adt7473",
		.data = (void *)adt7473
	},
	{
		.compatible = "adi,adt7475",
		.data = (void *)adt7475
	},
	{
		.compatible = "adi,adt7476",
		.data = (void *)adt7476
	},
	{
		.compatible = "adi,adt7490",
		.data = (void *)adt7490
	},
	{ },
};
MODULE_DEVICE_TABLE(of, adt7475_of_match);

struct adt7475_data {
	struct i2c_client *client;
	struct mutex lock;

	unsigned long measure_updated;
	bool valid;

	u8 config2;
	u8 config4;
	u8 config5;
	u8 has_voltage;
	u8 bypass_attn;		/* Bypass voltage attenuator */
	u8 has_pwm2:1;
	u8 has_fan4:1;
	u8 has_vid:1;
	u32 alarms;
	u16 voltage[3][6];
	u16 temp[7][3];
	u16 tach[2][4];
	u8 pwm[4][3];
	u8 range[3];
	u8 pwmctl[3];
	u8 pwmchan[3];
	u8 enh_acoustics[2];

	u8 vid;
	u8 vrm;
	const struct attribute_group *groups[9];
};

static struct i2c_driver adt7475_driver;
static struct adt7475_data *adt7475_update_device(struct device *dev);
static void adt7475_read_hystersis(struct i2c_client *client);
static void adt7475_read_pwm(struct i2c_client *client, int index);

/* Given a temp value, convert it to register value */

static inline u16 temp2reg(struct adt7475_data *data, long val)
{
	u16 ret;

	if (!(data->config5 & CONFIG5_TWOSCOMP)) {
		val = clamp_val(val, -64000, 191000);
		ret = (val + 64500) / 1000;
	} else {
		val = clamp_val(val, -128000, 127000);
		if (val < -500)
			ret = (256500 + val) / 1000;
		else
			ret = (val + 500) / 1000;
	}

	return ret << 2;
}

/* Given a register value, convert it to a real temp value */

static inline int reg2temp(struct adt7475_data *data, u16 reg)
{
	if (data->config5 & CONFIG5_TWOSCOMP) {
		if (reg >= 512)
			return (reg - 1024) * 250;
		else
			return reg * 250;
	} else
		return (reg - 256) * 250;
}

static inline int tach2rpm(u16 tach)
{
	if (tach == 0 || tach == 0xFFFF)
		return 0;

	return (90000 * 60) / tach;
}

static inline u16 rpm2tach(unsigned long rpm)
{
	if (rpm == 0)
		return 0;

	return clamp_val((90000 * 60) / rpm, 1, 0xFFFF);
}

/* Scaling factors for voltage inputs, taken from the ADT7490 datasheet */
static const int adt7473_in_scaling[ADT7475_VOLTAGE_COUNT + 1][2] = {
	{ 45, 94 },	/* +2.5V */
	{ 175, 525 },	/* Vccp */
	{ 68, 71 },	/* Vcc */
	{ 93, 47 },	/* +5V */
	{ 120, 20 },	/* +12V */
	{ 45, 45 },	/* Vtt */
};

static inline int reg2volt(int channel, u16 reg, u8 bypass_attn)
{
	const int *r = adt7473_in_scaling[channel];

	if (bypass_attn & (1 << channel))
		return DIV_ROUND_CLOSEST(reg * 2250, 1024);
	return DIV_ROUND_CLOSEST(reg * (r[0] + r[1]) * 2250, r[1] * 1024);
}

static inline u16 volt2reg(int channel, long volt, u8 bypass_attn)
{
	const int *r = adt7473_in_scaling[channel];
	long reg;

	if (bypass_attn & (1 << channel))
		reg = DIV_ROUND_CLOSEST(volt * 1024, 2250);
	else
		reg = DIV_ROUND_CLOSEST(volt * r[1] * 1024,
					(r[0] + r[1]) * 2250);
	return clamp_val(reg, 0, 1023) & (0xff << 2);
}

static int adt7475_read_word(struct i2c_client *client, int reg)
{
	int val1, val2;

	val1 = i2c_smbus_read_byte_data(client, reg);
	if (val1 < 0)
		return val1;
	val2 = i2c_smbus_read_byte_data(client, reg + 1);
	if (val2 < 0)
		return val2;

	return val1 | (val2 << 8);
}

static void adt7475_write_word(struct i2c_client *client, int reg, u16 val)
{
	i2c_smbus_write_byte_data(client, reg + 1, val >> 8);
	i2c_smbus_write_byte_data(client, reg, val & 0xFF);
}

static ssize_t voltage_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	unsigned short val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (sattr->nr) {
	case ALARM:
		return sprintf(buf, "%d\n",
			       (data->alarms >> sattr->index) & 1);
	default:
		val = data->voltage[sattr->nr][sattr->index];
		return sprintf(buf, "%d\n",
			       reg2volt(sattr->index, val, data->bypass_attn));
	}
}

static ssize_t voltage_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{

	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned char reg;
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	data->voltage[sattr->nr][sattr->index] =
				volt2reg(sattr->index, val, data->bypass_attn);

	if (sattr->index < ADT7475_VOLTAGE_COUNT) {
		if (sattr->nr == MIN)
			reg = VOLTAGE_MIN_REG(sattr->index);
		else
			reg = VOLTAGE_MAX_REG(sattr->index);
	} else {
		if (sattr->nr == MIN)
			reg = REG_VTT_MIN;
		else
			reg = REG_VTT_MAX;
	}

	i2c_smbus_write_byte_data(client, reg,
				  data->voltage[sattr->nr][sattr->index] >> 2);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t temp_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int out;

	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (sattr->nr) {
	case HYSTERSIS:
		mutex_lock(&data->lock);
		out = data->temp[sattr->nr][sattr->index];
		if (sattr->index != 1)
			out = (out >> 4) & 0xF;
		else
			out = (out & 0xF);
		/*
		 * Show the value as an absolute number tied to
		 * THERM
		 */
		out = reg2temp(data, data->temp[THERM][sattr->index]) -
			out * 1000;
		mutex_unlock(&data->lock);
		break;

	case OFFSET:
		/*
		 * Offset is always 2's complement, regardless of the
		 * setting in CONFIG5
		 */
		mutex_lock(&data->lock);
		out = (s8)data->temp[sattr->nr][sattr->index];
		if (data->config5 & CONFIG5_TEMPOFFSET)
			out *= 1000;
		else
			out *= 500;
		mutex_unlock(&data->lock);
		break;

	case ALARM:
		out = (data->alarms >> (sattr->index + 4)) & 1;
		break;

	case FAULT:
		/* Note - only for remote1 and remote2 */
		out = !!(data->alarms & (sattr->index ? 0x8000 : 0x4000));
		break;

	default:
		/* All other temp values are in the configured format */
		out = reg2temp(data, data->temp[sattr->nr][sattr->index]);
	}

	return sprintf(buf, "%d\n", out);
}

static ssize_t temp_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned char reg = 0;
	u8 out;
	int temp;
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	/* We need the config register in all cases for temp <-> reg conv. */
	data->config5 = adt7475_read(REG_CONFIG5);

	switch (sattr->nr) {
	case OFFSET:
		if (data->config5 & CONFIG5_TEMPOFFSET) {
			val = clamp_val(val, -63000, 127000);
			out = data->temp[OFFSET][sattr->index] = val / 1000;
		} else {
			val = clamp_val(val, -63000, 64000);
			out = data->temp[OFFSET][sattr->index] = val / 500;
		}
		break;

	case HYSTERSIS:
		/*
		 * The value will be given as an absolute value, turn it
		 * into an offset based on THERM
		 */

		/* Read fresh THERM and HYSTERSIS values from the chip */
		data->temp[THERM][sattr->index] =
			adt7475_read(TEMP_THERM_REG(sattr->index)) << 2;
		adt7475_read_hystersis(client);

		temp = reg2temp(data, data->temp[THERM][sattr->index]);
		val = clamp_val(val, temp - 15000, temp);
		val = (temp - val) / 1000;

		if (sattr->index != 1) {
			data->temp[HYSTERSIS][sattr->index] &= 0xF0;
			data->temp[HYSTERSIS][sattr->index] |= (val & 0xF) << 4;
		} else {
			data->temp[HYSTERSIS][sattr->index] &= 0x0F;
			data->temp[HYSTERSIS][sattr->index] |= (val & 0xF);
		}

		out = data->temp[HYSTERSIS][sattr->index];
		break;

	default:
		data->temp[sattr->nr][sattr->index] = temp2reg(data, val);

		/*
		 * We maintain an extra 2 digits of precision for simplicity
		 * - shift those back off before writing the value
		 */
		out = (u8) (data->temp[sattr->nr][sattr->index] >> 2);
	}

	switch (sattr->nr) {
	case MIN:
		reg = TEMP_MIN_REG(sattr->index);
		break;
	case MAX:
		reg = TEMP_MAX_REG(sattr->index);
		break;
	case OFFSET:
		reg = TEMP_OFFSET_REG(sattr->index);
		break;
	case AUTOMIN:
		reg = TEMP_TMIN_REG(sattr->index);
		break;
	case THERM:
		reg = TEMP_THERM_REG(sattr->index);
		break;
	case HYSTERSIS:
		if (sattr->index != 2)
			reg = REG_REMOTE1_HYSTERSIS;
		else
			reg = REG_REMOTE2_HYSTERSIS;

		break;
	}

	i2c_smbus_write_byte_data(client, reg, out);

	mutex_unlock(&data->lock);
	return count;
}

/* Assuming CONFIG6[SLOW] is 0 */
static const int ad7475_st_map[] = {
	37500, 18800, 12500, 7500, 4700, 3100, 1600, 800,
};

static ssize_t temp_st_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	long val;

	switch (sattr->index) {
	case 0:
		val = data->enh_acoustics[0] & 0xf;
		break;
	case 1:
		val = (data->enh_acoustics[1] >> 4) & 0xf;
		break;
	case 2:
	default:
		val = data->enh_acoustics[1] & 0xf;
		break;
	}

	if (val & 0x8)
		return sprintf(buf, "%d\n", ad7475_st_map[val & 0x7]);
	else
		return sprintf(buf, "0\n");
}

static ssize_t temp_st_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned char reg;
	int shift, idx;
	ulong val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	switch (sattr->index) {
	case 0:
		reg = REG_ENHANCE_ACOUSTICS1;
		shift = 0;
		idx = 0;
		break;
	case 1:
		reg = REG_ENHANCE_ACOUSTICS2;
		shift = 0;
		idx = 1;
		break;
	case 2:
	default:
		reg = REG_ENHANCE_ACOUSTICS2;
		shift = 4;
		idx = 1;
		break;
	}

	if (val > 0) {
		val = find_closest_descending(val, ad7475_st_map,
					      ARRAY_SIZE(ad7475_st_map));
		val |= 0x8;
	}

	mutex_lock(&data->lock);

	data->enh_acoustics[idx] &= ~(0xf << shift);
	data->enh_acoustics[idx] |= (val << shift);

	i2c_smbus_write_byte_data(client, reg, data->enh_acoustics[idx]);

	mutex_unlock(&data->lock);

	return count;
}

/*
 * Table of autorange values - the user will write the value in millidegrees,
 * and we'll convert it
 */
static const int autorange_table[] = {
	2000, 2500, 3330, 4000, 5000, 6670, 8000,
	10000, 13330, 16000, 20000, 26670, 32000, 40000,
	53330, 80000
};

static ssize_t point2_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int out, val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->lock);
	out = (data->range[sattr->index] >> 4) & 0x0F;
	val = reg2temp(data, data->temp[AUTOMIN][sattr->index]);
	mutex_unlock(&data->lock);

	return sprintf(buf, "%d\n", val + autorange_table[out]);
}

static ssize_t point2_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int temp;
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	/* Get a fresh copy of the needed registers */
	data->config5 = adt7475_read(REG_CONFIG5);
	data->temp[AUTOMIN][sattr->index] =
		adt7475_read(TEMP_TMIN_REG(sattr->index)) << 2;
	data->range[sattr->index] =
		adt7475_read(TEMP_TRANGE_REG(sattr->index));

	/*
	 * The user will write an absolute value, so subtract the start point
	 * to figure the range
	 */
	temp = reg2temp(data, data->temp[AUTOMIN][sattr->index]);
	val = clamp_val(val, temp + autorange_table[0],
		temp + autorange_table[ARRAY_SIZE(autorange_table) - 1]);
	val -= temp;

	/* Find the nearest table entry to what the user wrote */
	val = find_closest(val, autorange_table, ARRAY_SIZE(autorange_table));

	data->range[sattr->index] &= ~0xF0;
	data->range[sattr->index] |= val << 4;

	i2c_smbus_write_byte_data(client, TEMP_TRANGE_REG(sattr->index),
				  data->range[sattr->index]);

	mutex_unlock(&data->lock);
	return count;
}

static ssize_t tach_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int out;

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (sattr->nr == ALARM)
		out = (data->alarms >> (sattr->index + 10)) & 1;
	else
		out = tach2rpm(data->tach[sattr->nr][sattr->index]);

	return sprintf(buf, "%d\n", out);
}

static ssize_t tach_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{

	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	data->tach[MIN][sattr->index] = rpm2tach(val);

	adt7475_write_word(client, TACH_MIN_REG(sattr->index),
			   data->tach[MIN][sattr->index]);

	mutex_unlock(&data->lock);
	return count;
}

static ssize_t pwm_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->pwm[sattr->nr][sattr->index]);
}

static ssize_t pwmchan_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->pwmchan[sattr->index]);
}

static ssize_t pwmctrl_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->pwmctl[sattr->index]);
}

static ssize_t pwm_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{

	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned char reg = 0;
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	switch (sattr->nr) {
	case INPUT:
		/* Get a fresh value for CONTROL */
		data->pwm[CONTROL][sattr->index] =
			adt7475_read(PWM_CONFIG_REG(sattr->index));

		/*
		 * If we are not in manual mode, then we shouldn't allow
		 * the user to set the pwm speed
		 */
		if (((data->pwm[CONTROL][sattr->index] >> 5) & 7) != 7) {
			mutex_unlock(&data->lock);
			return count;
		}

		reg = PWM_REG(sattr->index);
		break;

	case MIN:
		reg = PWM_MIN_REG(sattr->index);
		break;

	case MAX:
		reg = PWM_MAX_REG(sattr->index);
		break;
	}

	data->pwm[sattr->nr][sattr->index] = clamp_val(val, 0, 0xFF);
	i2c_smbus_write_byte_data(client, reg,
				  data->pwm[sattr->nr][sattr->index]);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t stall_disable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);

	u8 mask = BIT(5 + sattr->index);

	return sprintf(buf, "%d\n", !!(data->enh_acoustics[0] & mask));
}

static ssize_t stall_disable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	u8 mask = BIT(5 + sattr->index);

	if (kstrtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	data->enh_acoustics[0] &= ~mask;
	if (val)
		data->enh_acoustics[0] |= mask;

	i2c_smbus_write_byte_data(client, REG_ENHANCE_ACOUSTICS1,
				  data->enh_acoustics[0]);

	mutex_unlock(&data->lock);

	return count;
}

/* Called by set_pwmctrl and set_pwmchan */

static int hw_set_pwm(struct i2c_client *client, int index,
		      unsigned int pwmctl, unsigned int pwmchan)
{
	struct adt7475_data *data = i2c_get_clientdata(client);
	long val = 0;

	switch (pwmctl) {
	case 0:
		val = 0x03;	/* Run at full speed */
		break;
	case 1:
		val = 0x07;	/* Manual mode */
		break;
	case 2:
		switch (pwmchan) {
		case 1:
			/* Remote1 controls PWM */
			val = 0x00;
			break;
		case 2:
			/* local controls PWM */
			val = 0x01;
			break;
		case 4:
			/* remote2 controls PWM */
			val = 0x02;
			break;
		case 6:
			/* local/remote2 control PWM */
			val = 0x05;
			break;
		case 7:
			/* All three control PWM */
			val = 0x06;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	data->pwmctl[index] = pwmctl;
	data->pwmchan[index] = pwmchan;

	data->pwm[CONTROL][index] &= ~0xE0;
	data->pwm[CONTROL][index] |= (val & 7) << 5;

	i2c_smbus_write_byte_data(client, PWM_CONFIG_REG(index),
				  data->pwm[CONTROL][index]);

	return 0;
}

static ssize_t pwmchan_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int r;
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);
	/* Read Modify Write PWM values */
	adt7475_read_pwm(client, sattr->index);
	r = hw_set_pwm(client, sattr->index, data->pwmctl[sattr->index], val);
	if (r)
		count = r;
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t pwmctrl_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int r;
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);
	/* Read Modify Write PWM values */
	adt7475_read_pwm(client, sattr->index);
	r = hw_set_pwm(client, sattr->index, val, data->pwmchan[sattr->index]);
	if (r)
		count = r;
	mutex_unlock(&data->lock);

	return count;
}

/* List of frequencies for the PWM */
static const int pwmfreq_table[] = {
	11, 14, 22, 29, 35, 44, 58, 88, 22500
};

static ssize_t pwmfreq_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int idx;

	if (IS_ERR(data))
		return PTR_ERR(data);
	idx = clamp_val(data->range[sattr->index] & 0xf, 0,
			ARRAY_SIZE(pwmfreq_table) - 1);

	return sprintf(buf, "%d\n", pwmfreq_table[idx]);
}

static ssize_t pwmfreq_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int out;
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;

	out = find_closest(val, pwmfreq_table, ARRAY_SIZE(pwmfreq_table));

	mutex_lock(&data->lock);

	data->range[sattr->index] =
		adt7475_read(TEMP_TRANGE_REG(sattr->index));
	data->range[sattr->index] &= ~0xf;
	data->range[sattr->index] |= out;

	i2c_smbus_write_byte_data(client, TEMP_TRANGE_REG(sattr->index),
				  data->range[sattr->index]);

	mutex_unlock(&data->lock);
	return count;
}

static ssize_t pwm_use_point2_pwm_at_crit_show(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", !!(data->config4 & CONFIG4_MAXDUTY));
}

static ssize_t pwm_use_point2_pwm_at_crit_store(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&data->lock);
	data->config4 = i2c_smbus_read_byte_data(client, REG_CONFIG4);
	if (val)
		data->config4 |= CONFIG4_MAXDUTY;
	else
		data->config4 &= ~CONFIG4_MAXDUTY;
	i2c_smbus_write_byte_data(client, REG_CONFIG4, data->config4);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t vrm_show(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct adt7475_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", (int)data->vrm);
}

static ssize_t vrm_store(struct device *dev, struct device_attribute *devattr,
			 const char *buf, size_t count)
{
	struct adt7475_data *data = dev_get_drvdata(dev);
	long val;

	if (kstrtol(buf, 10, &val))
		return -EINVAL;
	if (val < 0 || val > 255)
		return -EINVAL;
	data->vrm = val;

	return count;
}

static ssize_t cpu0_vid_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}

static SENSOR_DEVICE_ATTR_2_RO(in0_input, voltage, INPUT, 0);
static SENSOR_DEVICE_ATTR_2_RW(in0_max, voltage, MAX, 0);
static SENSOR_DEVICE_ATTR_2_RW(in0_min, voltage, MIN, 0);
static SENSOR_DEVICE_ATTR_2_RO(in0_alarm, voltage, ALARM, 0);
static SENSOR_DEVICE_ATTR_2_RO(in1_input, voltage, INPUT, 1);
static SENSOR_DEVICE_ATTR_2_RW(in1_max, voltage, MAX, 1);
static SENSOR_DEVICE_ATTR_2_RW(in1_min, voltage, MIN, 1);
static SENSOR_DEVICE_ATTR_2_RO(in1_alarm, voltage, ALARM, 1);
static SENSOR_DEVICE_ATTR_2_RO(in2_input, voltage, INPUT, 2);
static SENSOR_DEVICE_ATTR_2_RW(in2_max, voltage, MAX, 2);
static SENSOR_DEVICE_ATTR_2_RW(in2_min, voltage, MIN, 2);
static SENSOR_DEVICE_ATTR_2_RO(in2_alarm, voltage, ALARM, 2);
static SENSOR_DEVICE_ATTR_2_RO(in3_input, voltage, INPUT, 3);
static SENSOR_DEVICE_ATTR_2_RW(in3_max, voltage, MAX, 3);
static SENSOR_DEVICE_ATTR_2_RW(in3_min, voltage, MIN, 3);
static SENSOR_DEVICE_ATTR_2_RO(in3_alarm, voltage, ALARM, 3);
static SENSOR_DEVICE_ATTR_2_RO(in4_input, voltage, INPUT, 4);
static SENSOR_DEVICE_ATTR_2_RW(in4_max, voltage, MAX, 4);
static SENSOR_DEVICE_ATTR_2_RW(in4_min, voltage, MIN, 4);
static SENSOR_DEVICE_ATTR_2_RO(in4_alarm, voltage, ALARM, 8);
static SENSOR_DEVICE_ATTR_2_RO(in5_input, voltage, INPUT, 5);
static SENSOR_DEVICE_ATTR_2_RW(in5_max, voltage, MAX, 5);
static SENSOR_DEVICE_ATTR_2_RW(in5_min, voltage, MIN, 5);
static SENSOR_DEVICE_ATTR_2_RO(in5_alarm, voltage, ALARM, 31);
static SENSOR_DEVICE_ATTR_2_RO(temp1_input, temp, INPUT, 0);
static SENSOR_DEVICE_ATTR_2_RO(temp1_alarm, temp, ALARM, 0);
static SENSOR_DEVICE_ATTR_2_RO(temp1_fault, temp, FAULT, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_max, temp, MAX, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_min, temp, MIN, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_offset, temp, OFFSET, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_point1_temp, temp, AUTOMIN, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_point2_temp, point2, 0, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_crit, temp, THERM, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_crit_hyst, temp, HYSTERSIS, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_smoothing, temp_st, 0, 0);
static SENSOR_DEVICE_ATTR_2_RO(temp2_input, temp, INPUT, 1);
static SENSOR_DEVICE_ATTR_2_RO(temp2_alarm, temp, ALARM, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_max, temp, MAX, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_min, temp, MIN, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_offset, temp, OFFSET, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_point1_temp, temp, AUTOMIN, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_point2_temp, point2, 0, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_crit, temp, THERM, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_crit_hyst, temp, HYSTERSIS, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_smoothing, temp_st, 0, 1);
static SENSOR_DEVICE_ATTR_2_RO(temp3_input, temp, INPUT, 2);
static SENSOR_DEVICE_ATTR_2_RO(temp3_alarm, temp, ALARM, 2);
static SENSOR_DEVICE_ATTR_2_RO(temp3_fault, temp, FAULT, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_max, temp, MAX, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_min, temp, MIN, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_offset, temp, OFFSET, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_point1_temp, temp, AUTOMIN, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_point2_temp, point2, 0, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_crit, temp, THERM, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_crit_hyst, temp, HYSTERSIS, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_smoothing, temp_st, 0, 2);
static SENSOR_DEVICE_ATTR_2_RO(fan1_input, tach, INPUT, 0);
static SENSOR_DEVICE_ATTR_2_RW(fan1_min, tach, MIN, 0);
static SENSOR_DEVICE_ATTR_2_RO(fan1_alarm, tach, ALARM, 0);
static SENSOR_DEVICE_ATTR_2_RO(fan2_input, tach, INPUT, 1);
static SENSOR_DEVICE_ATTR_2_RW(fan2_min, tach, MIN, 1);
static SENSOR_DEVICE_ATTR_2_RO(fan2_alarm, tach, ALARM, 1);
static SENSOR_DEVICE_ATTR_2_RO(fan3_input, tach, INPUT, 2);
static SENSOR_DEVICE_ATTR_2_RW(fan3_min, tach, MIN, 2);
static SENSOR_DEVICE_ATTR_2_RO(fan3_alarm, tach, ALARM, 2);
static SENSOR_DEVICE_ATTR_2_RO(fan4_input, tach, INPUT, 3);
static SENSOR_DEVICE_ATTR_2_RW(fan4_min, tach, MIN, 3);
static SENSOR_DEVICE_ATTR_2_RO(fan4_alarm, tach, ALARM, 3);
static SENSOR_DEVICE_ATTR_2_RW(pwm1, pwm, INPUT, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_freq, pwmfreq, INPUT, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_enable, pwmctrl, INPUT, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_channels_temp, pwmchan, INPUT, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point1_pwm, pwm, MIN, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_auto_point2_pwm, pwm, MAX, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm1_stall_disable, stall_disable, 0, 0);
static SENSOR_DEVICE_ATTR_2_RW(pwm2, pwm, INPUT, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_freq, pwmfreq, INPUT, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_enable, pwmctrl, INPUT, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_channels_temp, pwmchan, INPUT, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point1_pwm, pwm, MIN, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_auto_point2_pwm, pwm, MAX, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm2_stall_disable, stall_disable, 0, 1);
static SENSOR_DEVICE_ATTR_2_RW(pwm3, pwm, INPUT, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_freq, pwmfreq, INPUT, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_enable, pwmctrl, INPUT, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_channels_temp, pwmchan, INPUT, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point1_pwm, pwm, MIN, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_auto_point2_pwm, pwm, MAX, 2);
static SENSOR_DEVICE_ATTR_2_RW(pwm3_stall_disable, stall_disable, 0, 2);

/* Non-standard name, might need revisiting */
static DEVICE_ATTR_RW(pwm_use_point2_pwm_at_crit);

static DEVICE_ATTR_RW(vrm);
static DEVICE_ATTR_RO(cpu0_vid);

static struct attribute *adt7475_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_fault.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_offset.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_smoothing.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_offset.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_smoothing.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_offset.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_smoothing.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_freq.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_channels_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_stall_disable.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm3_freq.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_channels_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_stall_disable.dev_attr.attr,
	&dev_attr_pwm_use_point2_pwm_at_crit.attr,
	NULL,
};

static struct attribute *fan4_attrs[] = {
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan4_min.dev_attr.attr,
	&sensor_dev_attr_fan4_alarm.dev_attr.attr,
	NULL
};

static struct attribute *pwm2_attrs[] = {
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm2_freq.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_channels_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_stall_disable.dev_attr.attr,
	NULL
};

static struct attribute *in0_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	NULL
};

static struct attribute *in3_attrs[] = {
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	NULL
};

static struct attribute *in4_attrs[] = {
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	NULL
};

static struct attribute *in5_attrs[] = {
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	NULL
};

static struct attribute *vid_attrs[] = {
	&dev_attr_cpu0_vid.attr,
	&dev_attr_vrm.attr,
	NULL
};

static const struct attribute_group adt7475_attr_group = { .attrs = adt7475_attrs };
static const struct attribute_group fan4_attr_group = { .attrs = fan4_attrs };
static const struct attribute_group pwm2_attr_group = { .attrs = pwm2_attrs };
static const struct attribute_group in0_attr_group = { .attrs = in0_attrs };
static const struct attribute_group in3_attr_group = { .attrs = in3_attrs };
static const struct attribute_group in4_attr_group = { .attrs = in4_attrs };
static const struct attribute_group in5_attr_group = { .attrs = in5_attrs };
static const struct attribute_group vid_attr_group = { .attrs = vid_attrs };

static int adt7475_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int vendid, devid, devid2;
	const char *name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	vendid = adt7475_read(REG_VENDID);
	devid2 = adt7475_read(REG_DEVID2);
	if (vendid != 0x41 ||		/* Analog Devices */
	    (devid2 & 0xf8) != 0x68)
		return -ENODEV;

	devid = adt7475_read(REG_DEVID);
	if (devid == 0x73)
		name = "adt7473";
	else if (devid == 0x75 && client->addr == 0x2e)
		name = "adt7475";
	else if (devid == 0x76)
		name = "adt7476";
	else if ((devid2 & 0xfc) == 0x6c)
		name = "adt7490";
	else {
		dev_dbg(&adapter->dev,
			"Couldn't detect an ADT7473/75/76/90 part at "
			"0x%02x\n", (unsigned int)client->addr);
		return -ENODEV;
	}

	strlcpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static int adt7475_update_limits(struct i2c_client *client)
{
	struct adt7475_data *data = i2c_get_clientdata(client);
	int i;
	int ret;

	ret = adt7475_read(REG_CONFIG4);
	if (ret < 0)
		return ret;
	data->config4 = ret;

	ret = adt7475_read(REG_CONFIG5);
	if (ret < 0)
		return ret;
	data->config5 = ret;

	for (i = 0; i < ADT7475_VOLTAGE_COUNT; i++) {
		if (!(data->has_voltage & (1 << i)))
			continue;
		/* Adjust values so they match the input precision */
		ret = adt7475_read(VOLTAGE_MIN_REG(i));
		if (ret < 0)
			return ret;
		data->voltage[MIN][i] = ret << 2;

		ret = adt7475_read(VOLTAGE_MAX_REG(i));
		if (ret < 0)
			return ret;
		data->voltage[MAX][i] = ret << 2;
	}

	if (data->has_voltage & (1 << 5)) {
		ret = adt7475_read(REG_VTT_MIN);
		if (ret < 0)
			return ret;
		data->voltage[MIN][5] = ret << 2;

		ret = adt7475_read(REG_VTT_MAX);
		if (ret < 0)
			return ret;
		data->voltage[MAX][5] = ret << 2;
	}

	for (i = 0; i < ADT7475_TEMP_COUNT; i++) {
		/* Adjust values so they match the input precision */
		ret = adt7475_read(TEMP_MIN_REG(i));
		if (ret < 0)
			return ret;
		data->temp[MIN][i] = ret << 2;

		ret = adt7475_read(TEMP_MAX_REG(i));
		if (ret < 0)
			return ret;
		data->temp[MAX][i] = ret << 2;

		ret = adt7475_read(TEMP_TMIN_REG(i));
		if (ret < 0)
			return ret;
		data->temp[AUTOMIN][i] = ret << 2;

		ret = adt7475_read(TEMP_THERM_REG(i));
		if (ret < 0)
			return ret;
		data->temp[THERM][i] = ret << 2;

		ret = adt7475_read(TEMP_OFFSET_REG(i));
		if (ret < 0)
			return ret;
		data->temp[OFFSET][i] = ret;
	}
	adt7475_read_hystersis(client);

	for (i = 0; i < ADT7475_TACH_COUNT; i++) {
		if (i == 3 && !data->has_fan4)
			continue;
		ret = adt7475_read_word(client, TACH_MIN_REG(i));
		if (ret < 0)
			return ret;
		data->tach[MIN][i] = ret;
	}

	for (i = 0; i < ADT7475_PWM_COUNT; i++) {
		if (i == 1 && !data->has_pwm2)
			continue;
		ret = adt7475_read(PWM_MAX_REG(i));
		if (ret < 0)
			return ret;
		data->pwm[MAX][i] = ret;

		ret = adt7475_read(PWM_MIN_REG(i));
		if (ret < 0)
			return ret;
		data->pwm[MIN][i] = ret;
		/* Set the channel and control information */
		adt7475_read_pwm(client, i);
	}

	ret = adt7475_read(TEMP_TRANGE_REG(0));
	if (ret < 0)
		return ret;
	data->range[0] = ret;

	ret = adt7475_read(TEMP_TRANGE_REG(1));
	if (ret < 0)
		return ret;
	data->range[1] = ret;

	ret = adt7475_read(TEMP_TRANGE_REG(2));
	if (ret < 0)
		return ret;
	data->range[2] = ret;

	return 0;
}

static int load_config3(const struct i2c_client *client, const char *propname)
{
	const char *function;
	u8 config3;
	int ret;

	ret = of_property_read_string(client->dev.of_node, propname, &function);
	if (!ret) {
		ret = adt7475_read(REG_CONFIG3);
		if (ret < 0)
			return ret;

		config3 = ret & ~CONFIG3_SMBALERT;
		if (!strcmp("pwm2", function))
			;
		else if (!strcmp("smbalert#", function))
			config3 |= CONFIG3_SMBALERT;
		else
			return -EINVAL;

		return i2c_smbus_write_byte_data(client, REG_CONFIG3, config3);
	}

	return 0;
}

static int load_config4(const struct i2c_client *client, const char *propname)
{
	const char *function;
	u8 config4;
	int ret;

	ret = of_property_read_string(client->dev.of_node, propname, &function);
	if (!ret) {
		ret = adt7475_read(REG_CONFIG4);
		if (ret < 0)
			return ret;

		config4 = ret & ~CONFIG4_PINFUNC;

		if (!strcmp("tach4", function))
			;
		else if (!strcmp("therm#", function))
			config4 |= CONFIG4_THERM;
		else if (!strcmp("smbalert#", function))
			config4 |= CONFIG4_SMBALERT;
		else if (!strcmp("gpio", function))
			config4 |= CONFIG4_PINFUNC;
		else
			return -EINVAL;

		return i2c_smbus_write_byte_data(client, REG_CONFIG4, config4);
	}

	return 0;
}

static int load_config(const struct i2c_client *client, enum chips chip)
{
	int err;
	const char *prop1, *prop2;

	switch (chip) {
	case adt7473:
	case adt7475:
		prop1 = "adi,pin5-function";
		prop2 = "adi,pin9-function";
		break;
	case adt7476:
	case adt7490:
		prop1 = "adi,pin10-function";
		prop2 = "adi,pin14-function";
		break;
	}

	err = load_config3(client, prop1);
	if (err) {
		dev_err(&client->dev, "failed to configure %s\n", prop1);
		return err;
	}

	err = load_config4(client, prop2);
	if (err) {
		dev_err(&client->dev, "failed to configure %s\n", prop2);
		return err;
	}

	return 0;
}

static int set_property_bit(const struct i2c_client *client, char *property,
			    u8 *config, u8 bit_index)
{
	u32 prop_value = 0;
	int ret = of_property_read_u32(client->dev.of_node, property,
					&prop_value);

	if (!ret) {
		if (prop_value)
			*config |= (1 << bit_index);
		else
			*config &= ~(1 << bit_index);
	}

	return ret;
}

static int load_attenuators(const struct i2c_client *client, enum chips chip,
			    struct adt7475_data *data)
{
	switch (chip) {
	case adt7476:
	case adt7490:
		set_property_bit(client, "adi,bypass-attenuator-in0",
				 &data->config4, 4);
		set_property_bit(client, "adi,bypass-attenuator-in1",
				 &data->config4, 5);
		set_property_bit(client, "adi,bypass-attenuator-in3",
				 &data->config4, 6);
		set_property_bit(client, "adi,bypass-attenuator-in4",
				 &data->config4, 7);

		return i2c_smbus_write_byte_data(client, REG_CONFIG4,
						 data->config4);
	case adt7473:
	case adt7475:
		set_property_bit(client, "adi,bypass-attenuator-in1",
				 &data->config2, 5);

		return i2c_smbus_write_byte_data(client, REG_CONFIG2,
						 data->config2);
	}

	return 0;
}

static int adt7475_set_pwm_polarity(struct i2c_client *client)
{
	u32 states[ADT7475_PWM_COUNT];
	int ret, i;
	u8 val;

	ret = of_property_read_u32_array(client->dev.of_node,
					 "adi,pwm-active-state", states,
					 ARRAY_SIZE(states));
	if (ret)
		return ret;

	for (i = 0; i < ADT7475_PWM_COUNT; i++) {
		ret = adt7475_read(PWM_CONFIG_REG(i));
		if (ret < 0)
			return ret;
		val = ret;
		if (states[i])
			val &= ~BIT(4);
		else
			val |= BIT(4);

		ret = i2c_smbus_write_byte_data(client, PWM_CONFIG_REG(i), val);
		if (ret)
			return ret;
	}

	return 0;
}

static int adt7475_probe(struct i2c_client *client)
{
	enum chips chip;
	static const char * const names[] = {
		[adt7473] = "ADT7473",
		[adt7475] = "ADT7475",
		[adt7476] = "ADT7476",
		[adt7490] = "ADT7490",
	};

	struct adt7475_data *data;
	struct device *hwmon_dev;
	int i, ret = 0, revision, group_num = 0;
	u8 config3;
	const struct i2c_device_id *id = i2c_match_id(adt7475_id, client);

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	mutex_init(&data->lock);
	data->client = client;
	i2c_set_clientdata(client, data);

	if (client->dev.of_node)
		chip = (enum chips)of_device_get_match_data(&client->dev);
	else
		chip = id->driver_data;

	/* Initialize device-specific values */
	switch (chip) {
	case adt7476:
		data->has_voltage = 0x0e;	/* in1 to in3 */
		revision = adt7475_read(REG_DEVID2) & 0x07;
		break;
	case adt7490:
		data->has_voltage = 0x3e;	/* in1 to in5 */
		revision = adt7475_read(REG_DEVID2) & 0x03;
		if (revision == 0x03)
			revision += adt7475_read(REG_DEVREV2);
		break;
	default:
		data->has_voltage = 0x06;	/* in1, in2 */
		revision = adt7475_read(REG_DEVID2) & 0x07;
	}

	ret = load_config(client, chip);
	if (ret)
		return ret;

	config3 = adt7475_read(REG_CONFIG3);
	/* Pin PWM2 may alternatively be used for ALERT output */
	if (!(config3 & CONFIG3_SMBALERT))
		data->has_pwm2 = 1;
	/* Meaning of this bit is inverted for the ADT7473-1 */
	if (id->driver_data == adt7473 && revision >= 1)
		data->has_pwm2 = !data->has_pwm2;

	data->config4 = adt7475_read(REG_CONFIG4);
	/* Pin TACH4 may alternatively be used for THERM */
	if ((data->config4 & CONFIG4_PINFUNC) == 0x0)
		data->has_fan4 = 1;

	/*
	 * THERM configuration is more complex on the ADT7476 and ADT7490,
	 * because 2 different pins (TACH4 and +2.5 Vin) can be used for
	 * this function
	 */
	if (id->driver_data == adt7490) {
		if ((data->config4 & CONFIG4_PINFUNC) == 0x1 &&
		    !(config3 & CONFIG3_THERM))
			data->has_fan4 = 1;
	}
	if (id->driver_data == adt7476 || id->driver_data == adt7490) {
		if (!(config3 & CONFIG3_THERM) ||
		    (data->config4 & CONFIG4_PINFUNC) == 0x1)
			data->has_voltage |= (1 << 0);		/* in0 */
	}

	/*
	 * On the ADT7476, the +12V input pin may instead be used as VID5,
	 * and VID pins may alternatively be used as GPIO
	 */
	if (id->driver_data == adt7476) {
		u8 vid = adt7475_read(REG_VID);
		if (!(vid & VID_VIDSEL))
			data->has_voltage |= (1 << 4);		/* in4 */

		data->has_vid = !(adt7475_read(REG_CONFIG5) & CONFIG5_VIDGPIO);
	}

	/* Voltage attenuators can be bypassed, globally or individually */
	data->config2 = adt7475_read(REG_CONFIG2);
	ret = load_attenuators(client, chip, data);
	if (ret)
		dev_warn(&client->dev, "Error configuring attenuator bypass\n");

	if (data->config2 & CONFIG2_ATTN) {
		data->bypass_attn = (0x3 << 3) | 0x3;
	} else {
		data->bypass_attn = ((data->config4 & CONFIG4_ATTN_IN10) >> 4) |
				    ((data->config4 & CONFIG4_ATTN_IN43) >> 3);
	}
	data->bypass_attn &= data->has_voltage;

	/*
	 * Call adt7475_read_pwm for all pwm's as this will reprogram any
	 * pwm's which are disabled to manual mode with 0% duty cycle
	 */
	for (i = 0; i < ADT7475_PWM_COUNT; i++)
		adt7475_read_pwm(client, i);

	ret = adt7475_set_pwm_polarity(client);
	if (ret && ret != -EINVAL)
		dev_warn(&client->dev, "Error configuring pwm polarity\n");

	/* Start monitoring */
	switch (chip) {
	case adt7475:
	case adt7476:
		i2c_smbus_write_byte_data(client, REG_CONFIG1,
					  adt7475_read(REG_CONFIG1) | 0x01);
		break;
	default:
		break;
	}

	data->groups[group_num++] = &adt7475_attr_group;

	/* Features that can be disabled individually */
	if (data->has_fan4) {
		data->groups[group_num++] = &fan4_attr_group;
	}
	if (data->has_pwm2) {
		data->groups[group_num++] = &pwm2_attr_group;
	}
	if (data->has_voltage & (1 << 0)) {
		data->groups[group_num++] = &in0_attr_group;
	}
	if (data->has_voltage & (1 << 3)) {
		data->groups[group_num++] = &in3_attr_group;
	}
	if (data->has_voltage & (1 << 4)) {
		data->groups[group_num++] = &in4_attr_group;
	}
	if (data->has_voltage & (1 << 5)) {
		data->groups[group_num++] = &in5_attr_group;
	}
	if (data->has_vid) {
		data->vrm = vid_which_vrm();
		data->groups[group_num] = &vid_attr_group;
	}

	/* register device with all the acquired attributes */
	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev,
							   client->name, data,
							   data->groups);

	if (IS_ERR(hwmon_dev)) {
		ret = PTR_ERR(hwmon_dev);
		return ret;
	}

	dev_info(&client->dev, "%s device, revision %d\n",
		 names[id->driver_data], revision);
	if ((data->has_voltage & 0x11) || data->has_fan4 || data->has_pwm2)
		dev_info(&client->dev, "Optional features:%s%s%s%s%s\n",
			 (data->has_voltage & (1 << 0)) ? " in0" : "",
			 (data->has_voltage & (1 << 4)) ? " in4" : "",
			 data->has_fan4 ? " fan4" : "",
			 data->has_pwm2 ? " pwm2" : "",
			 data->has_vid ? " vid" : "");
	if (data->bypass_attn)
		dev_info(&client->dev, "Bypassing attenuators on:%s%s%s%s\n",
			 (data->bypass_attn & (1 << 0)) ? " in0" : "",
			 (data->bypass_attn & (1 << 1)) ? " in1" : "",
			 (data->bypass_attn & (1 << 3)) ? " in3" : "",
			 (data->bypass_attn & (1 << 4)) ? " in4" : "");

	/* Limits and settings, should never change update more than once */
	ret = adt7475_update_limits(client);
	if (ret)
		return ret;

	return 0;
}

static struct i2c_driver adt7475_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adt7475",
		.of_match_table = of_match_ptr(adt7475_of_match),
	},
	.probe_new	= adt7475_probe,
	.id_table	= adt7475_id,
	.detect		= adt7475_detect,
	.address_list	= normal_i2c,
};

static void adt7475_read_hystersis(struct i2c_client *client)
{
	struct adt7475_data *data = i2c_get_clientdata(client);

	data->temp[HYSTERSIS][0] = (u16) adt7475_read(REG_REMOTE1_HYSTERSIS);
	data->temp[HYSTERSIS][1] = data->temp[HYSTERSIS][0];
	data->temp[HYSTERSIS][2] = (u16) adt7475_read(REG_REMOTE2_HYSTERSIS);
}

static void adt7475_read_pwm(struct i2c_client *client, int index)
{
	struct adt7475_data *data = i2c_get_clientdata(client);
	unsigned int v;

	data->pwm[CONTROL][index] = adt7475_read(PWM_CONFIG_REG(index));

	/*
	 * Figure out the internal value for pwmctrl and pwmchan
	 * based on the current settings
	 */
	v = (data->pwm[CONTROL][index] >> 5) & 7;

	if (v == 3)
		data->pwmctl[index] = 0;
	else if (v == 7)
		data->pwmctl[index] = 1;
	else if (v == 4) {
		/*
		 * The fan is disabled - we don't want to
		 * support that, so change to manual mode and
		 * set the duty cycle to 0 instead
		 */
		data->pwm[INPUT][index] = 0;
		data->pwm[CONTROL][index] &= ~0xE0;
		data->pwm[CONTROL][index] |= (7 << 5);

		i2c_smbus_write_byte_data(client, PWM_CONFIG_REG(index),
					  data->pwm[INPUT][index]);

		i2c_smbus_write_byte_data(client, PWM_CONFIG_REG(index),
					  data->pwm[CONTROL][index]);

		data->pwmctl[index] = 1;
	} else {
		data->pwmctl[index] = 2;

		switch (v) {
		case 0:
			data->pwmchan[index] = 1;
			break;
		case 1:
			data->pwmchan[index] = 2;
			break;
		case 2:
			data->pwmchan[index] = 4;
			break;
		case 5:
			data->pwmchan[index] = 6;
			break;
		case 6:
			data->pwmchan[index] = 7;
			break;
		}
	}
}

static int adt7475_update_measure(struct device *dev)
{
	struct adt7475_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u16 ext;
	int i;
	int ret;

	ret = adt7475_read(REG_STATUS2);
	if (ret < 0)
		return ret;
	data->alarms = ret << 8;

	ret = adt7475_read(REG_STATUS1);
	if (ret < 0)
		return ret;
	data->alarms |= ret;

	ret = adt7475_read(REG_EXTEND2);
	if (ret < 0)
		return ret;

	ext = (ret << 8);

	ret = adt7475_read(REG_EXTEND1);
	if (ret < 0)
		return ret;

	ext |= ret;

	for (i = 0; i < ADT7475_VOLTAGE_COUNT; i++) {
		if (!(data->has_voltage & (1 << i)))
			continue;
		ret = adt7475_read(VOLTAGE_REG(i));
		if (ret < 0)
			return ret;
		data->voltage[INPUT][i] =
			(ret << 2) |
			((ext >> (i * 2)) & 3);
	}

	for (i = 0; i < ADT7475_TEMP_COUNT; i++) {
		ret = adt7475_read(TEMP_REG(i));
		if (ret < 0)
			return ret;
		data->temp[INPUT][i] =
			(ret << 2) |
			((ext >> ((i + 5) * 2)) & 3);
	}

	if (data->has_voltage & (1 << 5)) {
		ret = adt7475_read(REG_STATUS4);
		if (ret < 0)
			return ret;
		data->alarms |= ret << 24;

		ret = adt7475_read(REG_EXTEND3);
		if (ret < 0)
			return ret;
		ext = ret;

		ret = adt7475_read(REG_VTT);
		if (ret < 0)
			return ret;
		data->voltage[INPUT][5] = ret << 2 |
			((ext >> 4) & 3);
	}

	for (i = 0; i < ADT7475_TACH_COUNT; i++) {
		if (i == 3 && !data->has_fan4)
			continue;
		ret = adt7475_read_word(client, TACH_REG(i));
		if (ret < 0)
			return ret;
		data->tach[INPUT][i] = ret;
	}

	/* Updated by hw when in auto mode */
	for (i = 0; i < ADT7475_PWM_COUNT; i++) {
		if (i == 1 && !data->has_pwm2)
			continue;
		ret = adt7475_read(PWM_REG(i));
		if (ret < 0)
			return ret;
		data->pwm[INPUT][i] = ret;
	}

	if (data->has_vid) {
		ret = adt7475_read(REG_VID);
		if (ret < 0)
			return ret;
		data->vid = ret & 0x3f;
	}

	return 0;
}

static struct adt7475_data *adt7475_update_device(struct device *dev)
{
	struct adt7475_data *data = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&data->lock);

	/* Measurement values update every 2 seconds */
	if (time_after(jiffies, data->measure_updated + HZ * 2) ||
	    !data->valid) {
		ret = adt7475_update_measure(dev);
		if (ret) {
			data->valid = false;
			mutex_unlock(&data->lock);
			return ERR_PTR(ret);
		}
		data->measure_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->lock);

	return data;
}

module_i2c_driver(adt7475_driver);

MODULE_AUTHOR("Advanced Micro Devices, Inc");
MODULE_DESCRIPTION("adt7475 driver");
MODULE_LICENSE("GPL");
