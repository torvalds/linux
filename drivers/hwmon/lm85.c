// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lm85.c - Part of lm_sensors, Linux kernel modules for hardware
 *	    monitoring
 * Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>
 * Copyright (c) 2002, 2003  Philip Pokorny <ppokorny@penguincomputing.com>
 * Copyright (c) 2003        Margit Schubert-While <margitsw@t-online.de>
 * Copyright (c) 2004        Justin Thiessen <jthiessen@penguincomputing.com>
 * Copyright (C) 2007--2014  Jean Delvare <jdelvare@suse.de>
 *
 * Chip details at	      <http://www.national.com/ds/LM/LM85.pdf>
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/util_macros.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

enum chips {
	lm85, lm96000,
	adm1027, adt7463, adt7468,
	emc6d100, emc6d102, emc6d103, emc6d103s
};

/* The LM85 registers */

#define LM85_REG_IN(nr)			(0x20 + (nr))
#define LM85_REG_IN_MIN(nr)		(0x44 + (nr) * 2)
#define LM85_REG_IN_MAX(nr)		(0x45 + (nr) * 2)

#define LM85_REG_TEMP(nr)		(0x25 + (nr))
#define LM85_REG_TEMP_MIN(nr)		(0x4e + (nr) * 2)
#define LM85_REG_TEMP_MAX(nr)		(0x4f + (nr) * 2)

/* Fan speeds are LSB, MSB (2 bytes) */
#define LM85_REG_FAN(nr)		(0x28 + (nr) * 2)
#define LM85_REG_FAN_MIN(nr)		(0x54 + (nr) * 2)

#define LM85_REG_PWM(nr)		(0x30 + (nr))

#define LM85_REG_COMPANY		0x3e
#define LM85_REG_VERSTEP		0x3f

#define ADT7468_REG_CFG5		0x7c
#define ADT7468_OFF64			(1 << 0)
#define ADT7468_HFPWM			(1 << 1)
#define IS_ADT7468_OFF64(data)		\
	((data)->type == adt7468 && !((data)->cfg5 & ADT7468_OFF64))
#define IS_ADT7468_HFPWM(data)		\
	((data)->type == adt7468 && !((data)->cfg5 & ADT7468_HFPWM))

/* These are the recognized values for the above regs */
#define LM85_COMPANY_NATIONAL		0x01
#define LM85_COMPANY_ANALOG_DEV		0x41
#define LM85_COMPANY_SMSC		0x5c
#define LM85_VERSTEP_LM85C		0x60
#define LM85_VERSTEP_LM85B		0x62
#define LM85_VERSTEP_LM96000_1		0x68
#define LM85_VERSTEP_LM96000_2		0x69
#define LM85_VERSTEP_ADM1027		0x60
#define LM85_VERSTEP_ADT7463		0x62
#define LM85_VERSTEP_ADT7463C		0x6A
#define LM85_VERSTEP_ADT7468_1		0x71
#define LM85_VERSTEP_ADT7468_2		0x72
#define LM85_VERSTEP_EMC6D100_A0        0x60
#define LM85_VERSTEP_EMC6D100_A1        0x61
#define LM85_VERSTEP_EMC6D102		0x65
#define LM85_VERSTEP_EMC6D103_A0	0x68
#define LM85_VERSTEP_EMC6D103_A1	0x69
#define LM85_VERSTEP_EMC6D103S		0x6A	/* Also known as EMC6D103:A2 */

#define LM85_REG_CONFIG			0x40

#define LM85_REG_ALARM1			0x41
#define LM85_REG_ALARM2			0x42

#define LM85_REG_VID			0x43

/* Automated FAN control */
#define LM85_REG_AFAN_CONFIG(nr)	(0x5c + (nr))
#define LM85_REG_AFAN_RANGE(nr)		(0x5f + (nr))
#define LM85_REG_AFAN_SPIKE1		0x62
#define LM85_REG_AFAN_MINPWM(nr)	(0x64 + (nr))
#define LM85_REG_AFAN_LIMIT(nr)		(0x67 + (nr))
#define LM85_REG_AFAN_CRITICAL(nr)	(0x6a + (nr))
#define LM85_REG_AFAN_HYST1		0x6d
#define LM85_REG_AFAN_HYST2		0x6e

#define ADM1027_REG_EXTEND_ADC1		0x76
#define ADM1027_REG_EXTEND_ADC2		0x77

#define EMC6D100_REG_ALARM3             0x7d
/* IN5, IN6 and IN7 */
#define EMC6D100_REG_IN(nr)             (0x70 + ((nr) - 5))
#define EMC6D100_REG_IN_MIN(nr)         (0x73 + ((nr) - 5) * 2)
#define EMC6D100_REG_IN_MAX(nr)         (0x74 + ((nr) - 5) * 2)
#define EMC6D102_REG_EXTEND_ADC1	0x85
#define EMC6D102_REG_EXTEND_ADC2	0x86
#define EMC6D102_REG_EXTEND_ADC3	0x87
#define EMC6D102_REG_EXTEND_ADC4	0x88

/*
 * Conversions. Rounding and limit checking is only done on the TO_REG
 * variants. Note that you should be a bit careful with which arguments
 * these macros are called: arguments may be evaluated more than once.
 */

/* IN are scaled according to built-in resistors */
static const int lm85_scaling[] = {  /* .001 Volts */
	2500, 2250, 3300, 5000, 12000,
	3300, 1500, 1800 /*EMC6D100*/
};
#define SCALE(val, from, to)	(((val) * (to) + ((from) / 2)) / (from))

#define INS_TO_REG(n, val)	\
		SCALE(clamp_val(val, 0, 255 * lm85_scaling[n] / 192), \
		      lm85_scaling[n], 192)

#define INSEXT_FROM_REG(n, val, ext)	\
		SCALE(((val) << 4) + (ext), 192 << 4, lm85_scaling[n])

#define INS_FROM_REG(n, val)	SCALE((val), 192, lm85_scaling[n])

/* FAN speed is measured using 90kHz clock */
static inline u16 FAN_TO_REG(unsigned long val)
{
	if (!val)
		return 0xffff;
	return clamp_val(5400000 / val, 1, 0xfffe);
}
#define FAN_FROM_REG(val)	((val) == 0 ? -1 : (val) == 0xffff ? 0 : \
				 5400000 / (val))

/* Temperature is reported in .001 degC increments */
#define TEMP_TO_REG(val)	\
		DIV_ROUND_CLOSEST(clamp_val((val), -127000, 127000), 1000)
#define TEMPEXT_FROM_REG(val, ext)	\
		SCALE(((val) << 4) + (ext), 16, 1000)
#define TEMP_FROM_REG(val)	((val) * 1000)

#define PWM_TO_REG(val)			clamp_val(val, 0, 255)
#define PWM_FROM_REG(val)		(val)

/*
 * ZONEs have the following parameters:
 *    Limit (low) temp,           1. degC
 *    Hysteresis (below limit),   1. degC (0-15)
 *    Range of speed control,     .1 degC (2-80)
 *    Critical (high) temp,       1. degC
 *
 * FAN PWMs have the following parameters:
 *    Reference Zone,                 1, 2, 3, etc.
 *    Spinup time,                    .05 sec
 *    PWM value at limit/low temp,    1 count
 *    PWM Frequency,                  1. Hz
 *    PWM is Min or OFF below limit,  flag
 *    Invert PWM output,              flag
 *
 * Some chips filter the temp, others the fan.
 *    Filter constant (or disabled)   .1 seconds
 */

/* These are the zone temperature range encodings in .001 degree C */
static const int lm85_range_map[] = {
	2000, 2500, 3300, 4000, 5000, 6600, 8000, 10000,
	13300, 16000, 20000, 26600, 32000, 40000, 53300, 80000
};

static int RANGE_TO_REG(long range)
{
	return find_closest(range, lm85_range_map, ARRAY_SIZE(lm85_range_map));
}
#define RANGE_FROM_REG(val)	lm85_range_map[(val) & 0x0f]

/* These are the PWM frequency encodings */
static const int lm85_freq_map[] = { /* 1 Hz */
	10, 15, 23, 30, 38, 47, 61, 94
};

static const int lm96000_freq_map[] = { /* 1 Hz */
	10, 15, 23, 30, 38, 47, 61, 94,
	22500, 24000, 25700, 25700, 27700, 27700, 30000, 30000
};

static const int adm1027_freq_map[] = { /* 1 Hz */
	11, 15, 22, 29, 35, 44, 59, 88
};

static int FREQ_TO_REG(const int *map,
		       unsigned int map_size, unsigned long freq)
{
	return find_closest(freq, map, map_size);
}

static int FREQ_FROM_REG(const int *map, unsigned int map_size, u8 reg)
{
	return map[reg % map_size];
}

/*
 * Since we can't use strings, I'm abusing these numbers
 *   to stand in for the following meanings:
 *      1 -- PWM responds to Zone 1
 *      2 -- PWM responds to Zone 2
 *      3 -- PWM responds to Zone 3
 *     23 -- PWM responds to the higher temp of Zone 2 or 3
 *    123 -- PWM responds to highest of Zone 1, 2, or 3
 *      0 -- PWM is always at 0% (ie, off)
 *     -1 -- PWM is always at 100%
 *     -2 -- PWM responds to manual control
 */

static const int lm85_zone_map[] = { 1, 2, 3, -1, 0, 23, 123, -2 };
#define ZONE_FROM_REG(val)	lm85_zone_map[(val) >> 5]

static int ZONE_TO_REG(int zone)
{
	int i;

	for (i = 0; i <= 7; ++i)
		if (zone == lm85_zone_map[i])
			break;
	if (i > 7)   /* Not found. */
		i = 3;  /* Always 100% */
	return i << 5;
}

#define HYST_TO_REG(val)	clamp_val(((val) + 500) / 1000, 0, 15)
#define HYST_FROM_REG(val)	((val) * 1000)

/*
 * Chip sampling rates
 *
 * Some sensors are not updated more frequently than once per second
 *    so it doesn't make sense to read them more often than that.
 *    We cache the results and return the saved data if the driver
 *    is called again before a second has elapsed.
 *
 * Also, there is significant configuration data for this chip
 *    given the automatic PWM fan control that is possible.  There
 *    are about 47 bytes of config data to only 22 bytes of actual
 *    readings.  So, we keep the config data up to date in the cache
 *    when it is written and only sample it once every 1 *minute*
 */
#define LM85_DATA_INTERVAL  (HZ + HZ / 2)
#define LM85_CONFIG_INTERVAL  (1 * 60 * HZ)

/*
 * LM85 can automatically adjust fan speeds based on temperature
 * This structure encapsulates an entire Zone config.  There are
 * three zones (one for each temperature input) on the lm85
 */
struct lm85_zone {
	s8 limit;	/* Low temp limit */
	u8 hyst;	/* Low limit hysteresis. (0-15) */
	u8 range;	/* Temp range, encoded */
	s8 critical;	/* "All fans ON" temp limit */
	u8 max_desired; /*
			 * Actual "max" temperature specified.  Preserved
			 * to prevent "drift" as other autofan control
			 * values change.
			 */
};

struct lm85_autofan {
	u8 config;	/* Register value */
	u8 min_pwm;	/* Minimum PWM value, encoded */
	u8 min_off;	/* Min PWM or OFF below "limit", flag */
};

/*
 * For each registered chip, we need to keep some data in memory.
 * The structure is dynamically allocated.
 */
struct lm85_data {
	struct i2c_client *client;
	const struct attribute_group *groups[6];
	const int *freq_map;
	unsigned int freq_map_size;

	enum chips type;

	bool has_vid5;	/* true if VID5 is configured for ADT7463 or ADT7468 */

	struct mutex update_lock;
	int valid;		/* !=0 if following fields are valid */
	unsigned long last_reading;	/* In jiffies */
	unsigned long last_config;	/* In jiffies */

	u8 in[8];		/* Register value */
	u8 in_max[8];		/* Register value */
	u8 in_min[8];		/* Register value */
	s8 temp[3];		/* Register value */
	s8 temp_min[3];		/* Register value */
	s8 temp_max[3];		/* Register value */
	u16 fan[4];		/* Register value */
	u16 fan_min[4];		/* Register value */
	u8 pwm[3];		/* Register value */
	u8 pwm_freq[3];		/* Register encoding */
	u8 temp_ext[3];		/* Decoded values */
	u8 in_ext[8];		/* Decoded values */
	u8 vid;			/* Register value */
	u8 vrm;			/* VRM version */
	u32 alarms;		/* Register encoding, combined */
	u8 cfg5;		/* Config Register 5 on ADT7468 */
	struct lm85_autofan autofan[3];
	struct lm85_zone zone[3];
};

static int lm85_read_value(struct i2c_client *client, u8 reg)
{
	int res;

	/* What size location is it? */
	switch (reg) {
	case LM85_REG_FAN(0):  /* Read WORD data */
	case LM85_REG_FAN(1):
	case LM85_REG_FAN(2):
	case LM85_REG_FAN(3):
	case LM85_REG_FAN_MIN(0):
	case LM85_REG_FAN_MIN(1):
	case LM85_REG_FAN_MIN(2):
	case LM85_REG_FAN_MIN(3):
	case LM85_REG_ALARM1:	/* Read both bytes at once */
		res = i2c_smbus_read_byte_data(client, reg) & 0xff;
		res |= i2c_smbus_read_byte_data(client, reg + 1) << 8;
		break;
	default:	/* Read BYTE data */
		res = i2c_smbus_read_byte_data(client, reg);
		break;
	}

	return res;
}

static void lm85_write_value(struct i2c_client *client, u8 reg, int value)
{
	switch (reg) {
	case LM85_REG_FAN(0):  /* Write WORD data */
	case LM85_REG_FAN(1):
	case LM85_REG_FAN(2):
	case LM85_REG_FAN(3):
	case LM85_REG_FAN_MIN(0):
	case LM85_REG_FAN_MIN(1):
	case LM85_REG_FAN_MIN(2):
	case LM85_REG_FAN_MIN(3):
	/* NOTE: ALARM is read only, so not included here */
		i2c_smbus_write_byte_data(client, reg, value & 0xff);
		i2c_smbus_write_byte_data(client, reg + 1, value >> 8);
		break;
	default:	/* Write BYTE data */
		i2c_smbus_write_byte_data(client, reg, value);
		break;
	}
}

static struct lm85_data *lm85_update_device(struct device *dev)
{
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i;

	mutex_lock(&data->update_lock);

	if (!data->valid ||
	     time_after(jiffies, data->last_reading + LM85_DATA_INTERVAL)) {
		/* Things that change quickly */
		dev_dbg(&client->dev, "Reading sensor values\n");

		/*
		 * Have to read extended bits first to "freeze" the
		 * more significant bits that are read later.
		 * There are 2 additional resolution bits per channel and we
		 * have room for 4, so we shift them to the left.
		 */
		if (data->type == adm1027 || data->type == adt7463 ||
		    data->type == adt7468) {
			int ext1 = lm85_read_value(client,
						   ADM1027_REG_EXTEND_ADC1);
			int ext2 =  lm85_read_value(client,
						    ADM1027_REG_EXTEND_ADC2);
			int val = (ext1 << 8) + ext2;

			for (i = 0; i <= 4; i++)
				data->in_ext[i] =
					((val >> (i * 2)) & 0x03) << 2;

			for (i = 0; i <= 2; i++)
				data->temp_ext[i] =
					(val >> ((i + 4) * 2)) & 0x0c;
		}

		data->vid = lm85_read_value(client, LM85_REG_VID);

		for (i = 0; i <= 3; ++i) {
			data->in[i] =
			    lm85_read_value(client, LM85_REG_IN(i));
			data->fan[i] =
			    lm85_read_value(client, LM85_REG_FAN(i));
		}

		if (!data->has_vid5)
			data->in[4] = lm85_read_value(client, LM85_REG_IN(4));

		if (data->type == adt7468)
			data->cfg5 = lm85_read_value(client, ADT7468_REG_CFG5);

		for (i = 0; i <= 2; ++i) {
			data->temp[i] =
			    lm85_read_value(client, LM85_REG_TEMP(i));
			data->pwm[i] =
			    lm85_read_value(client, LM85_REG_PWM(i));

			if (IS_ADT7468_OFF64(data))
				data->temp[i] -= 64;
		}

		data->alarms = lm85_read_value(client, LM85_REG_ALARM1);

		if (data->type == emc6d100) {
			/* Three more voltage sensors */
			for (i = 5; i <= 7; ++i) {
				data->in[i] = lm85_read_value(client,
							EMC6D100_REG_IN(i));
			}
			/* More alarm bits */
			data->alarms |= lm85_read_value(client,
						EMC6D100_REG_ALARM3) << 16;
		} else if (data->type == emc6d102 || data->type == emc6d103 ||
			   data->type == emc6d103s) {
			/*
			 * Have to read LSB bits after the MSB ones because
			 * the reading of the MSB bits has frozen the
			 * LSBs (backward from the ADM1027).
			 */
			int ext1 = lm85_read_value(client,
						   EMC6D102_REG_EXTEND_ADC1);
			int ext2 = lm85_read_value(client,
						   EMC6D102_REG_EXTEND_ADC2);
			int ext3 = lm85_read_value(client,
						   EMC6D102_REG_EXTEND_ADC3);
			int ext4 = lm85_read_value(client,
						   EMC6D102_REG_EXTEND_ADC4);
			data->in_ext[0] = ext3 & 0x0f;
			data->in_ext[1] = ext4 & 0x0f;
			data->in_ext[2] = ext4 >> 4;
			data->in_ext[3] = ext3 >> 4;
			data->in_ext[4] = ext2 >> 4;

			data->temp_ext[0] = ext1 & 0x0f;
			data->temp_ext[1] = ext2 & 0x0f;
			data->temp_ext[2] = ext1 >> 4;
		}

		data->last_reading = jiffies;
	}  /* last_reading */

	if (!data->valid ||
	     time_after(jiffies, data->last_config + LM85_CONFIG_INTERVAL)) {
		/* Things that don't change often */
		dev_dbg(&client->dev, "Reading config values\n");

		for (i = 0; i <= 3; ++i) {
			data->in_min[i] =
			    lm85_read_value(client, LM85_REG_IN_MIN(i));
			data->in_max[i] =
			    lm85_read_value(client, LM85_REG_IN_MAX(i));
			data->fan_min[i] =
			    lm85_read_value(client, LM85_REG_FAN_MIN(i));
		}

		if (!data->has_vid5)  {
			data->in_min[4] = lm85_read_value(client,
					  LM85_REG_IN_MIN(4));
			data->in_max[4] = lm85_read_value(client,
					  LM85_REG_IN_MAX(4));
		}

		if (data->type == emc6d100) {
			for (i = 5; i <= 7; ++i) {
				data->in_min[i] = lm85_read_value(client,
						EMC6D100_REG_IN_MIN(i));
				data->in_max[i] = lm85_read_value(client,
						EMC6D100_REG_IN_MAX(i));
			}
		}

		for (i = 0; i <= 2; ++i) {
			int val;

			data->temp_min[i] =
			    lm85_read_value(client, LM85_REG_TEMP_MIN(i));
			data->temp_max[i] =
			    lm85_read_value(client, LM85_REG_TEMP_MAX(i));

			data->autofan[i].config =
			    lm85_read_value(client, LM85_REG_AFAN_CONFIG(i));
			val = lm85_read_value(client, LM85_REG_AFAN_RANGE(i));
			data->pwm_freq[i] = val % data->freq_map_size;
			data->zone[i].range = val >> 4;
			data->autofan[i].min_pwm =
			    lm85_read_value(client, LM85_REG_AFAN_MINPWM(i));
			data->zone[i].limit =
			    lm85_read_value(client, LM85_REG_AFAN_LIMIT(i));
			data->zone[i].critical =
			    lm85_read_value(client, LM85_REG_AFAN_CRITICAL(i));

			if (IS_ADT7468_OFF64(data)) {
				data->temp_min[i] -= 64;
				data->temp_max[i] -= 64;
				data->zone[i].limit -= 64;
				data->zone[i].critical -= 64;
			}
		}

		if (data->type != emc6d103s) {
			i = lm85_read_value(client, LM85_REG_AFAN_SPIKE1);
			data->autofan[0].min_off = (i & 0x20) != 0;
			data->autofan[1].min_off = (i & 0x40) != 0;
			data->autofan[2].min_off = (i & 0x80) != 0;

			i = lm85_read_value(client, LM85_REG_AFAN_HYST1);
			data->zone[0].hyst = i >> 4;
			data->zone[1].hyst = i & 0x0f;

			i = lm85_read_value(client, LM85_REG_AFAN_HYST2);
			data->zone[2].hyst = i >> 4;
		}

		data->last_config = jiffies;
	}  /* last_config */

	data->valid = 1;

	mutex_unlock(&data->update_lock);

	return data;
}

/* 4 Fans */
static ssize_t fan_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[nr]));
}

static ssize_t fan_min_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[nr]));
}

static ssize_t fan_min_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val);
	lm85_write_value(client, LM85_REG_FAN_MIN(nr), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan, 0);
static SENSOR_DEVICE_ATTR_RW(fan1_min, fan_min, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan, 1);
static SENSOR_DEVICE_ATTR_RW(fan2_min, fan_min, 1);
static SENSOR_DEVICE_ATTR_RO(fan3_input, fan, 2);
static SENSOR_DEVICE_ATTR_RW(fan3_min, fan_min, 2);
static SENSOR_DEVICE_ATTR_RO(fan4_input, fan, 3);
static SENSOR_DEVICE_ATTR_RW(fan4_min, fan_min, 3);

/* vid, vrm, alarms */

static ssize_t cpu0_vid_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct lm85_data *data = lm85_update_device(dev);
	int vid;

	if (data->has_vid5) {
		/* 6-pin VID (VRM 10) */
		vid = vid_from_reg(data->vid & 0x3f, data->vrm);
	} else {
		/* 5-pin VID (VRM 9) */
		vid = vid_from_reg(data->vid & 0x1f, data->vrm);
	}

	return sprintf(buf, "%d\n", vid);
}

static DEVICE_ATTR_RO(cpu0_vid);

static ssize_t vrm_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct lm85_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%ld\n", (long) data->vrm);
}

static ssize_t vrm_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct lm85_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val > 255)
		return -EINVAL;

	data->vrm = val;
	return count;
}

static DEVICE_ATTR_RW(vrm);

static ssize_t alarms_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}

static DEVICE_ATTR_RO(alarms);

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms >> nr) & 1);
}

static SENSOR_DEVICE_ATTR_RO(in0_alarm, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(in1_alarm, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(in2_alarm, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(in3_alarm, alarm, 3);
static SENSOR_DEVICE_ATTR_RO(in4_alarm, alarm, 8);
static SENSOR_DEVICE_ATTR_RO(in5_alarm, alarm, 18);
static SENSOR_DEVICE_ATTR_RO(in6_alarm, alarm, 16);
static SENSOR_DEVICE_ATTR_RO(in7_alarm, alarm, 17);
static SENSOR_DEVICE_ATTR_RO(temp1_alarm, alarm, 4);
static SENSOR_DEVICE_ATTR_RO(temp1_fault, alarm, 14);
static SENSOR_DEVICE_ATTR_RO(temp2_alarm, alarm, 5);
static SENSOR_DEVICE_ATTR_RO(temp3_alarm, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(temp3_fault, alarm, 15);
static SENSOR_DEVICE_ATTR_RO(fan1_alarm, alarm, 10);
static SENSOR_DEVICE_ATTR_RO(fan2_alarm, alarm, 11);
static SENSOR_DEVICE_ATTR_RO(fan3_alarm, alarm, 12);
static SENSOR_DEVICE_ATTR_RO(fan4_alarm, alarm, 13);

/* pwm */

static ssize_t pwm_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", PWM_FROM_REG(data->pwm[nr]));
}

static ssize_t pwm_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->pwm[nr] = PWM_TO_REG(val);
	lm85_write_value(client, LM85_REG_PWM(nr), data->pwm[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t pwm_enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	int pwm_zone, enable;

	pwm_zone = ZONE_FROM_REG(data->autofan[nr].config);
	switch (pwm_zone) {
	case -1:	/* PWM is always at 100% */
		enable = 0;
		break;
	case 0:		/* PWM is always at 0% */
	case -2:	/* PWM responds to manual control */
		enable = 1;
		break;
	default:	/* PWM in automatic mode */
		enable = 2;
	}
	return sprintf(buf, "%d\n", enable);
}

static ssize_t pwm_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 config;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	switch (val) {
	case 0:
		config = 3;
		break;
	case 1:
		config = 7;
		break;
	case 2:
		/*
		 * Here we have to choose arbitrarily one of the 5 possible
		 * configurations; I go for the safest
		 */
		config = 6;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	data->autofan[nr].config = lm85_read_value(client,
		LM85_REG_AFAN_CONFIG(nr));
	data->autofan[nr].config = (data->autofan[nr].config & ~0xe0)
		| (config << 5);
	lm85_write_value(client, LM85_REG_AFAN_CONFIG(nr),
		data->autofan[nr].config);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t pwm_freq_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	int freq;

	if (IS_ADT7468_HFPWM(data))
		freq = 22500;
	else
		freq = FREQ_FROM_REG(data->freq_map, data->freq_map_size,
				     data->pwm_freq[nr]);

	return sprintf(buf, "%d\n", freq);
}

static ssize_t pwm_freq_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	/*
	 * The ADT7468 has a special high-frequency PWM output mode,
	 * where all PWM outputs are driven by a 22.5 kHz clock.
	 * This might confuse the user, but there's not much we can do.
	 */
	if (data->type == adt7468 && val >= 11300) {	/* High freq. mode */
		data->cfg5 &= ~ADT7468_HFPWM;
		lm85_write_value(client, ADT7468_REG_CFG5, data->cfg5);
	} else {					/* Low freq. mode */
		data->pwm_freq[nr] = FREQ_TO_REG(data->freq_map,
						 data->freq_map_size, val);
		lm85_write_value(client, LM85_REG_AFAN_RANGE(nr),
				 (data->zone[nr].range << 4)
				 | data->pwm_freq[nr]);
		if (data->type == adt7468) {
			data->cfg5 |= ADT7468_HFPWM;
			lm85_write_value(client, ADT7468_REG_CFG5, data->cfg5);
		}
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1, pwm, 0);
static SENSOR_DEVICE_ATTR_RW(pwm1_enable, pwm_enable, 0);
static SENSOR_DEVICE_ATTR_RW(pwm1_freq, pwm_freq, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2, pwm, 1);
static SENSOR_DEVICE_ATTR_RW(pwm2_enable, pwm_enable, 1);
static SENSOR_DEVICE_ATTR_RW(pwm2_freq, pwm_freq, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3, pwm, 2);
static SENSOR_DEVICE_ATTR_RW(pwm3_enable, pwm_enable, 2);
static SENSOR_DEVICE_ATTR_RW(pwm3_freq, pwm_freq, 2);

/* Voltages */

static ssize_t in_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", INSEXT_FROM_REG(nr, data->in[nr],
						    data->in_ext[nr]));
}

static ssize_t in_min_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", INS_FROM_REG(nr, data->in_min[nr]));
}

static ssize_t in_min_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[nr] = INS_TO_REG(nr, val);
	lm85_write_value(client, LM85_REG_IN_MIN(nr), data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t in_max_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", INS_FROM_REG(nr, data->in_max[nr]));
}

static ssize_t in_max_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[nr] = INS_TO_REG(nr, val);
	lm85_write_value(client, LM85_REG_IN_MAX(nr), data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(in0_input, in, 0);
static SENSOR_DEVICE_ATTR_RW(in0_min, in_min, 0);
static SENSOR_DEVICE_ATTR_RW(in0_max, in_max, 0);
static SENSOR_DEVICE_ATTR_RO(in1_input, in, 1);
static SENSOR_DEVICE_ATTR_RW(in1_min, in_min, 1);
static SENSOR_DEVICE_ATTR_RW(in1_max, in_max, 1);
static SENSOR_DEVICE_ATTR_RO(in2_input, in, 2);
static SENSOR_DEVICE_ATTR_RW(in2_min, in_min, 2);
static SENSOR_DEVICE_ATTR_RW(in2_max, in_max, 2);
static SENSOR_DEVICE_ATTR_RO(in3_input, in, 3);
static SENSOR_DEVICE_ATTR_RW(in3_min, in_min, 3);
static SENSOR_DEVICE_ATTR_RW(in3_max, in_max, 3);
static SENSOR_DEVICE_ATTR_RO(in4_input, in, 4);
static SENSOR_DEVICE_ATTR_RW(in4_min, in_min, 4);
static SENSOR_DEVICE_ATTR_RW(in4_max, in_max, 4);
static SENSOR_DEVICE_ATTR_RO(in5_input, in, 5);
static SENSOR_DEVICE_ATTR_RW(in5_min, in_min, 5);
static SENSOR_DEVICE_ATTR_RW(in5_max, in_max, 5);
static SENSOR_DEVICE_ATTR_RO(in6_input, in, 6);
static SENSOR_DEVICE_ATTR_RW(in6_min, in_min, 6);
static SENSOR_DEVICE_ATTR_RW(in6_max, in_max, 6);
static SENSOR_DEVICE_ATTR_RO(in7_input, in, 7);
static SENSOR_DEVICE_ATTR_RW(in7_min, in_min, 7);
static SENSOR_DEVICE_ATTR_RW(in7_max, in_max, 7);

/* Temps */

static ssize_t temp_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", TEMPEXT_FROM_REG(data->temp[nr],
						     data->temp_ext[nr]));
}

static ssize_t temp_min_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_min[nr]));
}

static ssize_t temp_min_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	if (IS_ADT7468_OFF64(data))
		val += 64;

	mutex_lock(&data->update_lock);
	data->temp_min[nr] = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_TEMP_MIN(nr), data->temp_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_max_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[nr]));
}

static ssize_t temp_max_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	if (IS_ADT7468_OFF64(data))
		val += 64;

	mutex_lock(&data->update_lock);
	data->temp_max[nr] = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_TEMP_MAX(nr), data->temp_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_min, temp_min, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp_max, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_min, temp_min, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp_max, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_min, temp_min, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_max, temp_max, 2);

/* Automatic PWM control */

static ssize_t pwm_auto_channels_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", ZONE_FROM_REG(data->autofan[nr].config));
}

static ssize_t pwm_auto_channels_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->autofan[nr].config = (data->autofan[nr].config & (~0xe0))
		| ZONE_TO_REG(val);
	lm85_write_value(client, LM85_REG_AFAN_CONFIG(nr),
		data->autofan[nr].config);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t pwm_auto_pwm_min_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", PWM_FROM_REG(data->autofan[nr].min_pwm));
}

static ssize_t pwm_auto_pwm_min_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->autofan[nr].min_pwm = PWM_TO_REG(val);
	lm85_write_value(client, LM85_REG_AFAN_MINPWM(nr),
		data->autofan[nr].min_pwm);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t pwm_auto_pwm_minctl_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", data->autofan[nr].min_off);
}

static ssize_t pwm_auto_pwm_minctl_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 tmp;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->autofan[nr].min_off = val;
	tmp = lm85_read_value(client, LM85_REG_AFAN_SPIKE1);
	tmp &= ~(0x20 << nr);
	if (data->autofan[nr].min_off)
		tmp |= 0x20 << nr;
	lm85_write_value(client, LM85_REG_AFAN_SPIKE1, tmp);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1_auto_channels, pwm_auto_channels, 0);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_pwm_min, pwm_auto_pwm_min, 0);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_pwm_minctl, pwm_auto_pwm_minctl, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_channels, pwm_auto_channels, 1);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_pwm_min, pwm_auto_pwm_min, 1);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_pwm_minctl, pwm_auto_pwm_minctl, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_channels, pwm_auto_channels, 2);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_pwm_min, pwm_auto_pwm_min, 2);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_pwm_minctl, pwm_auto_pwm_minctl, 2);

/* Temperature settings for automatic PWM control */

static ssize_t temp_auto_temp_off_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->zone[nr].limit) -
		HYST_FROM_REG(data->zone[nr].hyst));
}

static ssize_t temp_auto_temp_off_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int min;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	min = TEMP_FROM_REG(data->zone[nr].limit);
	data->zone[nr].hyst = HYST_TO_REG(min - val);
	if (nr == 0 || nr == 1) {
		lm85_write_value(client, LM85_REG_AFAN_HYST1,
			(data->zone[0].hyst << 4)
			| data->zone[1].hyst);
	} else {
		lm85_write_value(client, LM85_REG_AFAN_HYST2,
			(data->zone[2].hyst << 4));
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_auto_temp_min_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->zone[nr].limit));
}

static ssize_t temp_auto_temp_min_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->zone[nr].limit = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_AFAN_LIMIT(nr),
		data->zone[nr].limit);

/* Update temp_auto_max and temp_auto_range */
	data->zone[nr].range = RANGE_TO_REG(
		TEMP_FROM_REG(data->zone[nr].max_desired) -
		TEMP_FROM_REG(data->zone[nr].limit));
	lm85_write_value(client, LM85_REG_AFAN_RANGE(nr),
		((data->zone[nr].range & 0x0f) << 4)
		| data->pwm_freq[nr]);

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_auto_temp_max_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->zone[nr].limit) +
		RANGE_FROM_REG(data->zone[nr].range));
}

static ssize_t temp_auto_temp_max_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int min;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	min = TEMP_FROM_REG(data->zone[nr].limit);
	data->zone[nr].max_desired = TEMP_TO_REG(val);
	data->zone[nr].range = RANGE_TO_REG(
		val - min);
	lm85_write_value(client, LM85_REG_AFAN_RANGE(nr),
		((data->zone[nr].range & 0x0f) << 4)
		| data->pwm_freq[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_auto_temp_crit_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->zone[nr].critical));
}

static ssize_t temp_auto_temp_crit_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm85_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->zone[nr].critical = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_AFAN_CRITICAL(nr),
		data->zone[nr].critical);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_auto_temp_off, temp_auto_temp_off, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_auto_temp_min, temp_auto_temp_min, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_auto_temp_max, temp_auto_temp_max, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_auto_temp_crit, temp_auto_temp_crit, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_auto_temp_off, temp_auto_temp_off, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_auto_temp_min, temp_auto_temp_min, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_auto_temp_max, temp_auto_temp_max, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_auto_temp_crit, temp_auto_temp_crit, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_auto_temp_off, temp_auto_temp_off, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_auto_temp_min, temp_auto_temp_min, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_auto_temp_max, temp_auto_temp_max, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_auto_temp_crit, temp_auto_temp_crit, 2);

static struct attribute *lm85_attributes[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan4_min.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	&sensor_dev_attr_fan4_alarm.dev_attr.attr,

	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_freq.dev_attr.attr,
	&sensor_dev_attr_pwm2_freq.dev_attr.attr,
	&sensor_dev_attr_pwm3_freq.dev_attr.attr,

	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,

	&sensor_dev_attr_pwm1_auto_channels.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_channels.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_channels.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_pwm_min.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_pwm_min.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_pwm_min.dev_attr.attr,

	&sensor_dev_attr_temp1_auto_temp_min.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_temp_min.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_temp_min.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_temp_max.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_temp_max.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_temp_max.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_temp_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_temp_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_temp_crit.dev_attr.attr,

	&dev_attr_vrm.attr,
	&dev_attr_cpu0_vid.attr,
	&dev_attr_alarms.attr,
	NULL
};

static const struct attribute_group lm85_group = {
	.attrs = lm85_attributes,
};

static struct attribute *lm85_attributes_minctl[] = {
	&sensor_dev_attr_pwm1_auto_pwm_minctl.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_pwm_minctl.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_pwm_minctl.dev_attr.attr,
	NULL
};

static const struct attribute_group lm85_group_minctl = {
	.attrs = lm85_attributes_minctl,
};

static struct attribute *lm85_attributes_temp_off[] = {
	&sensor_dev_attr_temp1_auto_temp_off.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_temp_off.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_temp_off.dev_attr.attr,
	NULL
};

static const struct attribute_group lm85_group_temp_off = {
	.attrs = lm85_attributes_temp_off,
};

static struct attribute *lm85_attributes_in4[] = {
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm85_group_in4 = {
	.attrs = lm85_attributes_in4,
};

static struct attribute *lm85_attributes_in567[] = {
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,
	&sensor_dev_attr_in7_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm85_group_in567 = {
	.attrs = lm85_attributes_in567,
};

static void lm85_init_client(struct i2c_client *client)
{
	int value;

	/* Start monitoring if needed */
	value = lm85_read_value(client, LM85_REG_CONFIG);
	if (!(value & 0x01)) {
		dev_info(&client->dev, "Starting monitoring\n");
		lm85_write_value(client, LM85_REG_CONFIG, value | 0x01);
	}

	/* Warn about unusual configuration bits */
	if (value & 0x02)
		dev_warn(&client->dev, "Device configuration is locked\n");
	if (!(value & 0x04))
		dev_warn(&client->dev, "Device is not ready\n");
}

static int lm85_is_fake(struct i2c_client *client)
{
	/*
	 * Differenciate between real LM96000 and Winbond WPCD377I. The latter
	 * emulate the former except that it has no hardware monitoring function
	 * so the readings are always 0.
	 */
	int i;
	u8 in_temp, fan;

	for (i = 0; i < 8; i++) {
		in_temp = i2c_smbus_read_byte_data(client, 0x20 + i);
		fan = i2c_smbus_read_byte_data(client, 0x28 + i);
		if (in_temp != 0x00 || fan != 0xff)
			return 0;
	}

	return 1;
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm85_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int address = client->addr;
	const char *type_name = NULL;
	int company, verstep;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		/* We need to be able to do byte I/O */
		return -ENODEV;
	}

	/* Determine the chip type */
	company = lm85_read_value(client, LM85_REG_COMPANY);
	verstep = lm85_read_value(client, LM85_REG_VERSTEP);

	dev_dbg(&adapter->dev,
		"Detecting device at 0x%02x with COMPANY: 0x%02x and VERSTEP: 0x%02x\n",
		address, company, verstep);

	if (company == LM85_COMPANY_NATIONAL) {
		switch (verstep) {
		case LM85_VERSTEP_LM85C:
			type_name = "lm85c";
			break;
		case LM85_VERSTEP_LM85B:
			type_name = "lm85b";
			break;
		case LM85_VERSTEP_LM96000_1:
		case LM85_VERSTEP_LM96000_2:
			/* Check for Winbond WPCD377I */
			if (lm85_is_fake(client)) {
				dev_dbg(&adapter->dev,
					"Found Winbond WPCD377I, ignoring\n");
				return -ENODEV;
			}
			type_name = "lm96000";
			break;
		}
	} else if (company == LM85_COMPANY_ANALOG_DEV) {
		switch (verstep) {
		case LM85_VERSTEP_ADM1027:
			type_name = "adm1027";
			break;
		case LM85_VERSTEP_ADT7463:
		case LM85_VERSTEP_ADT7463C:
			type_name = "adt7463";
			break;
		case LM85_VERSTEP_ADT7468_1:
		case LM85_VERSTEP_ADT7468_2:
			type_name = "adt7468";
			break;
		}
	} else if (company == LM85_COMPANY_SMSC) {
		switch (verstep) {
		case LM85_VERSTEP_EMC6D100_A0:
		case LM85_VERSTEP_EMC6D100_A1:
			/* Note: we can't tell a '100 from a '101 */
			type_name = "emc6d100";
			break;
		case LM85_VERSTEP_EMC6D102:
			type_name = "emc6d102";
			break;
		case LM85_VERSTEP_EMC6D103_A0:
		case LM85_VERSTEP_EMC6D103_A1:
			type_name = "emc6d103";
			break;
		case LM85_VERSTEP_EMC6D103S:
			type_name = "emc6d103s";
			break;
		}
	}

	if (!type_name)
		return -ENODEV;

	strlcpy(info->type, type_name, I2C_NAME_SIZE);

	return 0;
}

static const struct i2c_device_id lm85_id[];

static int lm85_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct lm85_data *data;
	int idx = 0;

	data = devm_kzalloc(dev, sizeof(struct lm85_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	if (client->dev.of_node)
		data->type = (enum chips)of_device_get_match_data(&client->dev);
	else
		data->type = i2c_match_id(lm85_id, client)->driver_data;
	mutex_init(&data->update_lock);

	/* Fill in the chip specific driver values */
	switch (data->type) {
	case adm1027:
	case adt7463:
	case adt7468:
	case emc6d100:
	case emc6d102:
	case emc6d103:
	case emc6d103s:
		data->freq_map = adm1027_freq_map;
		data->freq_map_size = ARRAY_SIZE(adm1027_freq_map);
		break;
	case lm96000:
		data->freq_map = lm96000_freq_map;
		data->freq_map_size = ARRAY_SIZE(lm96000_freq_map);
		break;
	default:
		data->freq_map = lm85_freq_map;
		data->freq_map_size = ARRAY_SIZE(lm85_freq_map);
	}

	/* Set the VRM version */
	data->vrm = vid_which_vrm();

	/* Initialize the LM85 chip */
	lm85_init_client(client);

	/* sysfs hooks */
	data->groups[idx++] = &lm85_group;

	/* minctl and temp_off exist on all chips except emc6d103s */
	if (data->type != emc6d103s) {
		data->groups[idx++] = &lm85_group_minctl;
		data->groups[idx++] = &lm85_group_temp_off;
	}

	/*
	 * The ADT7463/68 have an optional VRM 10 mode where pin 21 is used
	 * as a sixth digital VID input rather than an analog input.
	 */
	if (data->type == adt7463 || data->type == adt7468) {
		u8 vid = lm85_read_value(client, LM85_REG_VID);
		if (vid & 0x80)
			data->has_vid5 = true;
	}

	if (!data->has_vid5)
		data->groups[idx++] = &lm85_group_in4;

	/* The EMC6D100 has 3 additional voltage inputs */
	if (data->type == emc6d100)
		data->groups[idx++] = &lm85_group_in567;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id lm85_id[] = {
	{ "adm1027", adm1027 },
	{ "adt7463", adt7463 },
	{ "adt7468", adt7468 },
	{ "lm85", lm85 },
	{ "lm85b", lm85 },
	{ "lm85c", lm85 },
	{ "lm96000", lm96000 },
	{ "emc6d100", emc6d100 },
	{ "emc6d101", emc6d100 },
	{ "emc6d102", emc6d102 },
	{ "emc6d103", emc6d103 },
	{ "emc6d103s", emc6d103s },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm85_id);

static const struct of_device_id __maybe_unused lm85_of_match[] = {
	{
		.compatible = "adi,adm1027",
		.data = (void *)adm1027
	},
	{
		.compatible = "adi,adt7463",
		.data = (void *)adt7463
	},
	{
		.compatible = "adi,adt7468",
		.data = (void *)adt7468
	},
	{
		.compatible = "national,lm85",
		.data = (void *)lm85
	},
	{
		.compatible = "national,lm85b",
		.data = (void *)lm85
	},
	{
		.compatible = "national,lm85c",
		.data = (void *)lm85
	},
	{
		.compatible = "ti,lm96000",
		.data = (void *)lm96000
	},
	{
		.compatible = "smsc,emc6d100",
		.data = (void *)emc6d100
	},
	{
		.compatible = "smsc,emc6d101",
		.data = (void *)emc6d100
	},
	{
		.compatible = "smsc,emc6d102",
		.data = (void *)emc6d102
	},
	{
		.compatible = "smsc,emc6d103",
		.data = (void *)emc6d103
	},
	{
		.compatible = "smsc,emc6d103s",
		.data = (void *)emc6d103s
	},
	{ },
};
MODULE_DEVICE_TABLE(of, lm85_of_match);

static struct i2c_driver lm85_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name   = "lm85",
		.of_match_table = of_match_ptr(lm85_of_match),
	},
	.probe_new	= lm85_probe,
	.id_table	= lm85_id,
	.detect		= lm85_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm85_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Philip Pokorny <ppokorny@penguincomputing.com>, "
	"Margit Schubert-While <margitsw@t-online.de>, "
	"Justin Thiessen <jthiessen@penguincomputing.com>");
MODULE_DESCRIPTION("LM85-B, LM85-C driver");
