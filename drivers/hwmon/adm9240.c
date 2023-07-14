// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * adm9240.c	Part of lm_sensors, Linux kernel modules for hardware
 *		monitoring
 *
 * Copyright (C) 1999	Frodo Looijaard <frodol@dds.nl>
 *			Philip Edelbrock <phil@netroedge.com>
 * Copyright (C) 2003	Michiel Rook <michiel@grendelproject.nl>
 * Copyright (C) 2005	Grant Coady <gcoady.lk@gmail.com> with valuable
 *				guidance from Jean Delvare
 *
 * Driver supports	Analog Devices		ADM9240
 *			Dallas Semiconductor	DS1780
 *			National Semiconductor	LM81
 *
 * ADM9240 is the reference, DS1780 and LM81 are register compatibles
 *
 * Voltage	Six inputs are scaled by chip, VID also reported
 * Temperature	Chip temperature to 0.5'C, maximum and max_hysteris
 * Fans		2 fans, low speed alarm, automatic fan clock divider
 * Alarms	16-bit map of active alarms
 * Analog Out	0..1250 mV output
 *
 * Chassis Intrusion: clear CI latch with 'echo 0 > intrusion0_alarm'
 *
 * Test hardware: Intel SE440BX-2 desktop motherboard --Grant
 *
 * LM81 extended temp reading not implemented
 */

#include <linux/bits.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, 0x2f,
					I2C_CLIENT_END };

enum chips { adm9240, ds1780, lm81 };

/* ADM9240 registers */
#define ADM9240_REG_MAN_ID		0x3e
#define ADM9240_REG_DIE_REV		0x3f
#define ADM9240_REG_CONFIG		0x40

#define ADM9240_REG_IN(nr)		(0x20 + (nr))   /* 0..5 */
#define ADM9240_REG_IN_MAX(nr)		(0x2b + (nr) * 2)
#define ADM9240_REG_IN_MIN(nr)		(0x2c + (nr) * 2)
#define ADM9240_REG_FAN(nr)		(0x28 + (nr))   /* 0..1 */
#define ADM9240_REG_FAN_MIN(nr)		(0x3b + (nr))
#define ADM9240_REG_INT(nr)		(0x41 + (nr))
#define ADM9240_REG_INT_MASK(nr)	(0x43 + (nr))
#define ADM9240_REG_TEMP		0x27
#define ADM9240_REG_TEMP_MAX(nr)	(0x39 + (nr)) /* 0, 1 = high, hyst */
#define ADM9240_REG_ANALOG_OUT		0x19
#define ADM9240_REG_CHASSIS_CLEAR	0x46
#define ADM9240_REG_VID_FAN_DIV		0x47
#define ADM9240_REG_I2C_ADDR		0x48
#define ADM9240_REG_VID4		0x49
#define ADM9240_REG_TEMP_CONF		0x4b

/* generalised scaling with integer rounding */
static inline int SCALE(long val, int mul, int div)
{
	if (val < 0)
		return (val * mul - div / 2) / div;
	else
		return (val * mul + div / 2) / div;
}

/* adm9240 internally scales voltage measurements */
static const u16 nom_mv[] = { 2500, 2700, 3300, 5000, 12000, 2700 };

static inline unsigned int IN_FROM_REG(u8 reg, int n)
{
	return SCALE(reg, nom_mv[n], 192);
}

static inline u8 IN_TO_REG(unsigned long val, int n)
{
	val = clamp_val(val, 0, nom_mv[n] * 255 / 192);
	return SCALE(val, 192, nom_mv[n]);
}

/* temperature range: -40..125, 127 disables temperature alarm */
static inline s8 TEMP_TO_REG(long val)
{
	val = clamp_val(val, -40000, 127000);
	return SCALE(val, 1, 1000);
}

/* two fans, each with low fan speed limit */
static inline unsigned int FAN_FROM_REG(u8 reg, u8 div)
{
	if (!reg) /* error */
		return -1;

	if (reg == 255)
		return 0;

	return SCALE(1350000, 1, reg * div);
}

/* analog out 0..1250mV */
static inline u8 AOUT_TO_REG(unsigned long val)
{
	val = clamp_val(val, 0, 1250);
	return SCALE(val, 255, 1250);
}

static inline unsigned int AOUT_FROM_REG(u8 reg)
{
	return SCALE(reg, 1250, 255);
}

/* per client data */
struct adm9240_data {
	struct device *dev;
	struct regmap *regmap;
	struct mutex update_lock;

	u8 fan_div[2];		/* rw	fan1_div, read-only accessor */
	u8 vrm;			/* --	vrm set on startup, no accessor */
};

/* write new fan div, callers must hold data->update_lock */
static int adm9240_write_fan_div(struct adm9240_data *data, int channel, u8 fan_div)
{
	unsigned int reg, old, shift = (channel + 2) * 2;
	int err;

	err = regmap_read(data->regmap, ADM9240_REG_VID_FAN_DIV, &reg);
	if (err < 0)
		return err;
	old = (reg >> shift) & 3;
	reg &= ~(3 << shift);
	reg |= (fan_div << shift);
	err = regmap_write(data->regmap, ADM9240_REG_VID_FAN_DIV, reg);
	if (err < 0)
		return err;
	dev_dbg(data->dev,
		"fan%d clock divider changed from %lu to %lu\n",
		channel + 1, BIT(old), BIT(fan_div));

	return 0;
}

/*
 * set fan speed low limit:
 *
 * - value is zero: disable fan speed low limit alarm
 *
 * - value is below fan speed measurement range: enable fan speed low
 *   limit alarm to be asserted while fan speed too slow to measure
 *
 * - otherwise: select fan clock divider to suit fan speed low limit,
 *   measurement code may adjust registers to ensure fan speed reading
 */
static int adm9240_fan_min_write(struct adm9240_data *data, int channel, long val)
{
	u8 new_div;
	u8 fan_min;
	int err;

	mutex_lock(&data->update_lock);

	if (!val) {
		fan_min = 255;
		new_div = data->fan_div[channel];

		dev_dbg(data->dev, "fan%u low limit set disabled\n", channel + 1);
	} else if (val < 1350000 / (8 * 254)) {
		new_div = 3;
		fan_min = 254;

		dev_dbg(data->dev, "fan%u low limit set minimum %u\n",
			channel + 1, FAN_FROM_REG(254, BIT(new_div)));
	} else {
		unsigned int new_min = 1350000 / val;

		new_div = 0;
		while (new_min > 192 && new_div < 3) {
			new_div++;
			new_min /= 2;
		}
		if (!new_min) /* keep > 0 */
			new_min++;

		fan_min = new_min;

		dev_dbg(data->dev, "fan%u low limit set fan speed %u\n",
			channel + 1, FAN_FROM_REG(new_min, BIT(new_div)));
	}

	if (new_div != data->fan_div[channel]) {
		data->fan_div[channel] = new_div;
		adm9240_write_fan_div(data, channel, new_div);
	}
	err = regmap_write(data->regmap, ADM9240_REG_FAN_MIN(channel), fan_min);

	mutex_unlock(&data->update_lock);

	return err;
}

static ssize_t cpu0_vid_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int err;
	u8 vid;

	err = regmap_read(data->regmap, ADM9240_REG_VID_FAN_DIV, &regval);
	if (err < 0)
		return err;
	vid = regval & 0x0f;
	err = regmap_read(data->regmap, ADM9240_REG_VID4, &regval);
	if (err < 0)
		return err;
	vid |= (regval & 1) << 4;
	return sprintf(buf, "%d\n", vid_from_reg(vid, data->vrm));
}
static DEVICE_ATTR_RO(cpu0_vid);

static ssize_t aout_output_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int err;

	err = regmap_read(data->regmap, ADM9240_REG_ANALOG_OUT, &regval);
	if (err)
		return err;

	return sprintf(buf, "%d\n", AOUT_FROM_REG(regval));
}

static ssize_t aout_output_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	err = regmap_write(data->regmap, ADM9240_REG_ANALOG_OUT, AOUT_TO_REG(val));
	return err < 0 ? err : count;
}
static DEVICE_ATTR_RW(aout_output);

static struct attribute *adm9240_attrs[] = {
	&dev_attr_aout_output.attr,
	&dev_attr_cpu0_vid.attr,
	NULL
};

ATTRIBUTE_GROUPS(adm9240);

/*** sensor chip detect and driver install ***/

/* Return 0 if detection is successful, -ENODEV otherwise */
static int adm9240_detect(struct i2c_client *new_client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	const char *name = "";
	int address = new_client->addr;
	u8 man_id, die_rev;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* verify chip: reg address should match i2c address */
	if (i2c_smbus_read_byte_data(new_client, ADM9240_REG_I2C_ADDR) != address)
		return -ENODEV;

	/* check known chip manufacturer */
	man_id = i2c_smbus_read_byte_data(new_client, ADM9240_REG_MAN_ID);
	if (man_id == 0x23)
		name = "adm9240";
	else if (man_id == 0xda)
		name = "ds1780";
	else if (man_id == 0x01)
		name = "lm81";
	else
		return -ENODEV;

	/* successful detect, print chip info */
	die_rev = i2c_smbus_read_byte_data(new_client, ADM9240_REG_DIE_REV);
	dev_info(&adapter->dev, "found %s revision %u\n",
		 man_id == 0x23 ? "ADM9240" :
		 man_id == 0xda ? "DS1780" : "LM81", die_rev);

	strscpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static int adm9240_init_client(struct adm9240_data *data)
{
	unsigned int regval;
	u8 conf, mode;
	int err;

	err = regmap_raw_read(data->regmap, ADM9240_REG_CONFIG, &conf, 1);
	if (err < 0)
		return err;
	err = regmap_raw_read(data->regmap, ADM9240_REG_TEMP_CONF, &mode, 1);
	if (err < 0)
		return err;
	mode &= 3;

	data->vrm = vid_which_vrm(); /* need this to report vid as mV */

	dev_info(data->dev, "Using VRM: %d.%d\n", data->vrm / 10,
		 data->vrm % 10);

	if (conf & 1) { /* measurement cycle running: report state */

		dev_info(data->dev, "status: config 0x%02x mode %u\n",
			 conf, mode);

	} else { /* cold start: open limits before starting chip */
		int i;

		for (i = 0; i < 6; i++) {
			err = regmap_write(data->regmap,
					   ADM9240_REG_IN_MIN(i), 0);
			if (err < 0)
				return err;
			err = regmap_write(data->regmap,
					   ADM9240_REG_IN_MAX(i), 255);
			if (err < 0)
				return err;
		}
		for (i = 0; i < 2; i++) {
			err = regmap_write(data->regmap,
					   ADM9240_REG_FAN_MIN(i), 255);
			if (err < 0)
				return err;
		}
		for (i = 0; i < 2; i++) {
			err = regmap_write(data->regmap,
					   ADM9240_REG_TEMP_MAX(i), 127);
			if (err < 0)
				return err;
		}

		/* start measurement cycle */
		err = regmap_write(data->regmap, ADM9240_REG_CONFIG, 1);
		if (err < 0)
			return err;

		dev_info(data->dev,
			 "cold start: config was 0x%02x mode %u\n", conf, mode);
	}

	/* read fan divs */
	err = regmap_read(data->regmap, ADM9240_REG_VID_FAN_DIV, &regval);
	if (err < 0)
		return err;
	data->fan_div[0] = (regval >> 4) & 3;
	data->fan_div[1] = (regval >> 6) & 3;
	return 0;
}

static int adm9240_chip_read(struct device *dev, u32 attr, long *val)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	u8 regs[2];
	int err;

	switch (attr) {
	case hwmon_chip_alarms:
		err = regmap_bulk_read(data->regmap, ADM9240_REG_INT(0), &regs, 2);
		if (err < 0)
			return err;
		*val = regs[0] | regs[1] << 8;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int adm9240_intrusion_read(struct device *dev, u32 attr, long *val)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int err;

	switch (attr) {
	case hwmon_intrusion_alarm:
		err = regmap_read(data->regmap, ADM9240_REG_INT(1), &regval);
		if (err < 0)
			return err;
		*val = !!(regval & BIT(4));
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int adm9240_intrusion_write(struct device *dev, u32 attr, long val)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	int err;

	switch (attr) {
	case hwmon_intrusion_alarm:
		if (val)
			return -EINVAL;
		err = regmap_write(data->regmap, ADM9240_REG_CHASSIS_CLEAR, 0x80);
		if (err < 0)
			return err;
		dev_dbg(data->dev, "chassis intrusion latch cleared\n");
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int adm9240_in_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int reg;
	int err;

	switch (attr) {
	case hwmon_in_input:
		reg = ADM9240_REG_IN(channel);
		break;
	case hwmon_in_min:
		reg = ADM9240_REG_IN_MIN(channel);
		break;
	case hwmon_in_max:
		reg = ADM9240_REG_IN_MAX(channel);
		break;
	case hwmon_in_alarm:
		if (channel < 4) {
			reg = ADM9240_REG_INT(0);
		} else {
			reg = ADM9240_REG_INT(1);
			channel -= 4;
		}
		err = regmap_read(data->regmap, reg, &regval);
		if (err < 0)
			return err;
		*val = !!(regval & BIT(channel));
		return 0;
	default:
		return -EOPNOTSUPP;
	}
	err = regmap_read(data->regmap, reg, &regval);
	if (err < 0)
		return err;
	*val = IN_FROM_REG(regval, channel);
	return 0;
}

static int adm9240_in_write(struct device *dev, u32 attr, int channel, long val)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	int reg;

	switch (attr) {
	case hwmon_in_min:
		reg = ADM9240_REG_IN_MIN(channel);
		break;
	case hwmon_in_max:
		reg = ADM9240_REG_IN_MAX(channel);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return regmap_write(data->regmap, reg, IN_TO_REG(val, channel));
}

static int adm9240_fan_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int err;

	switch (attr) {
	case hwmon_fan_input:
		mutex_lock(&data->update_lock);
		err = regmap_read(data->regmap, ADM9240_REG_FAN(channel), &regval);
		if (err < 0) {
			mutex_unlock(&data->update_lock);
			return err;
		}
		if (regval == 255 && data->fan_div[channel] < 3) {
			/* adjust fan clock divider on overflow */
			err = adm9240_write_fan_div(data, channel,
						    ++data->fan_div[channel]);
			if (err) {
				mutex_unlock(&data->update_lock);
				return err;
			}
		}
		*val = FAN_FROM_REG(regval, BIT(data->fan_div[channel]));
		mutex_unlock(&data->update_lock);
		break;
	case hwmon_fan_div:
		*val = BIT(data->fan_div[channel]);
		break;
	case hwmon_fan_min:
		err = regmap_read(data->regmap, ADM9240_REG_FAN_MIN(channel), &regval);
		if (err < 0)
			return err;
		*val = FAN_FROM_REG(regval, BIT(data->fan_div[channel]));
		break;
	case hwmon_fan_alarm:
		err = regmap_read(data->regmap, ADM9240_REG_INT(0), &regval);
		if (err < 0)
			return err;
		*val = !!(regval & BIT(channel + 6));
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int adm9240_fan_write(struct device *dev, u32 attr, int channel, long val)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	int err;

	switch (attr) {
	case hwmon_fan_min:
		err = adm9240_fan_min_write(data, channel, val);
		if (err < 0)
			return err;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int adm9240_temp_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int err, temp;

	switch (attr) {
	case hwmon_temp_input:
		err = regmap_read(data->regmap, ADM9240_REG_TEMP, &regval);
		if (err < 0)
			return err;
		temp = regval << 1;
		err = regmap_read(data->regmap, ADM9240_REG_TEMP_CONF, &regval);
		if (err < 0)
			return err;
		temp |= regval >> 7;
		*val = sign_extend32(temp, 8) * 500;
		break;
	case hwmon_temp_max:
		err = regmap_read(data->regmap, ADM9240_REG_TEMP_MAX(0), &regval);
		if (err < 0)
			return err;
		*val = (s8)regval * 1000;
		break;
	case hwmon_temp_max_hyst:
		err = regmap_read(data->regmap, ADM9240_REG_TEMP_MAX(1), &regval);
		if (err < 0)
			return err;
		*val = (s8)regval * 1000;
		break;
	case hwmon_temp_alarm:
		err = regmap_read(data->regmap, ADM9240_REG_INT(0), &regval);
		if (err < 0)
			return err;
		*val = !!(regval & BIT(4));
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int adm9240_temp_write(struct device *dev, u32 attr, int channel, long val)
{
	struct adm9240_data *data = dev_get_drvdata(dev);
	int reg;

	switch (attr) {
	case hwmon_temp_max:
		reg = ADM9240_REG_TEMP_MAX(0);
		break;
	case hwmon_temp_max_hyst:
		reg = ADM9240_REG_TEMP_MAX(1);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return regmap_write(data->regmap, reg, TEMP_TO_REG(val));
}

static int adm9240_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return adm9240_chip_read(dev, attr, val);
	case hwmon_intrusion:
		return adm9240_intrusion_read(dev, attr, val);
	case hwmon_in:
		return adm9240_in_read(dev, attr, channel, val);
	case hwmon_fan:
		return adm9240_fan_read(dev, attr, channel, val);
	case hwmon_temp:
		return adm9240_temp_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int adm9240_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			 int channel, long val)
{
	switch (type) {
	case hwmon_intrusion:
		return adm9240_intrusion_write(dev, attr, val);
	case hwmon_in:
		return adm9240_in_write(dev, attr, channel, val);
	case hwmon_fan:
		return adm9240_fan_write(dev, attr, channel, val);
	case hwmon_temp:
		return adm9240_temp_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t adm9240_is_visible(const void *_data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	umode_t mode = 0;

	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_alarms:
			mode = 0444;
			break;
		default:
			break;
		}
		break;
	case hwmon_intrusion:
		switch (attr) {
		case hwmon_intrusion_alarm:
			mode = 0644;
			break;
		default:
			break;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp:
		case hwmon_temp_alarm:
			mode = 0444;
			break;
		case hwmon_temp_max:
		case hwmon_temp_max_hyst:
			mode = 0644;
			break;
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
		case hwmon_fan_div:
		case hwmon_fan_alarm:
			mode = 0444;
			break;
		case hwmon_fan_min:
			mode = 0644;
			break;
		default:
			break;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_alarm:
			mode = 0444;
			break;
		case hwmon_in_min:
		case hwmon_in_max:
			mode = 0644;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return mode;
}

static const struct hwmon_ops adm9240_hwmon_ops = {
	.is_visible = adm9240_is_visible,
	.read = adm9240_read,
	.write = adm9240_write,
};

static const struct hwmon_channel_info * const adm9240_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_ALARMS),
	HWMON_CHANNEL_INFO(intrusion, HWMON_INTRUSION_ALARM),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST | HWMON_T_ALARM),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX | HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX | HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX | HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX | HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX | HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX | HWMON_I_ALARM),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_DIV | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_DIV | HWMON_F_ALARM),
	NULL
};

static const struct hwmon_chip_info adm9240_chip_info = {
	.ops = &adm9240_hwmon_ops,
	.info = adm9240_info,
};

static bool adm9240_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADM9240_REG_IN(0) ... ADM9240_REG_IN(5):
	case ADM9240_REG_FAN(0) ... ADM9240_REG_FAN(1):
	case ADM9240_REG_INT(0) ... ADM9240_REG_INT(1):
	case ADM9240_REG_TEMP:
	case ADM9240_REG_TEMP_CONF:
	case ADM9240_REG_VID_FAN_DIV:
	case ADM9240_REG_VID4:
	case ADM9240_REG_ANALOG_OUT:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config adm9240_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.use_single_read = true,
	.use_single_write = true,
	.volatile_reg = adm9240_volatile_reg,
};

static int adm9240_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct adm9240_data *data;
	int err;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	mutex_init(&data->update_lock);
	data->regmap = devm_regmap_init_i2c(client, &adm9240_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	err = adm9240_init_client(data);
	if (err < 0)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data,
							 &adm9240_chip_info,
							 adm9240_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id adm9240_id[] = {
	{ "adm9240", adm9240 },
	{ "ds1780", ds1780 },
	{ "lm81", lm81 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm9240_id);

static struct i2c_driver adm9240_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adm9240",
	},
	.probe		= adm9240_probe,
	.id_table	= adm9240_id,
	.detect		= adm9240_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(adm9240_driver);

MODULE_AUTHOR("Michiel Rook <michiel@grendelproject.nl>, "
		"Grant Coady <gcoady.lk@gmail.com> and others");
MODULE_DESCRIPTION("ADM9240/DS1780/LM81 driver");
MODULE_LICENSE("GPL");
