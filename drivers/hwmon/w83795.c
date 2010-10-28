/*
 *  w83795.c - Linux kernel driver for hardware monitoring
 *  Copyright (C) 2008 Nuvoton Technology Corp.
 *                Wei Song
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation - version 2.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301 USA.
 *
 *  Supports following chips:
 *
 *  Chip       #vin   #fanin #pwm #temp #dts wchipid  vendid  i2c  ISA
 *  w83795g     21     14     8     6     8    0x79   0x5ca3  yes   no
 *  w83795adg   18     14     2     6     8    0x79   0x5ca3  yes   no
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, 0x2f, I2C_CLIENT_END };


static int reset;
module_param(reset, bool, 0);
MODULE_PARM_DESC(reset, "Set to 1 to reset chip, not recommended");


#define W83795_REG_BANKSEL		0x00
#define W83795_REG_VENDORID		0xfd
#define W83795_REG_CHIPID		0xfe
#define W83795_REG_DEVICEID		0xfb
#define W83795_REG_DEVICEID_A		0xff

#define W83795_REG_I2C_ADDR		0xfc
#define W83795_REG_CONFIG		0x01
#define W83795_REG_CONFIG_CONFIG48	0x04

/* Multi-Function Pin Ctrl Registers */
#define W83795_REG_VOLT_CTRL1		0x02
#define W83795_REG_VOLT_CTRL2		0x03
#define W83795_REG_TEMP_CTRL1		0x04
#define W83795_REG_TEMP_CTRL2		0x05
#define W83795_REG_FANIN_CTRL1		0x06
#define W83795_REG_FANIN_CTRL2		0x07
#define W83795_REG_VMIGB_CTRL		0x08

#define TEMP_CTRL_DISABLE		0
#define TEMP_CTRL_TD			1
#define TEMP_CTRL_VSEN			2
#define TEMP_CTRL_TR			3
#define TEMP_CTRL_SHIFT			4
#define TEMP_CTRL_HASIN_SHIFT		5
/* temp mode may effect VSEN17-12 (in20-15) */
static u16 W83795_REG_TEMP_CTRL[][6] = {
	/* Disable, TD, VSEN, TR, register shift value, has_in shift num */
	{0x00, 0x01, 0x02, 0x03, 0, 17},	/* TR1 */
	{0x00, 0x04, 0x08, 0x0C, 2, 18},	/* TR2 */
	{0x00, 0x10, 0x20, 0x30, 4, 19},	/* TR3 */
	{0x00, 0x40, 0x80, 0xC0, 6, 20},	/* TR4 */
	{0x00, 0x00, 0x02, 0x03, 0, 15},	/* TR5 */
	{0x00, 0x00, 0x08, 0x0C, 2, 16},	/* TR6 */
};

#define TEMP_READ			0
#define TEMP_CRIT			1
#define TEMP_CRIT_HYST			2
#define TEMP_WARN			3
#define TEMP_WARN_HYST			4
/* only crit and crit_hyst affect real-time alarm status
 * current crit crit_hyst warn warn_hyst */
static u16 W83795_REG_TEMP[][5] = {
	{0x21, 0x96, 0x97, 0x98, 0x99},	/* TD1/TR1 */
	{0x22, 0x9a, 0x9b, 0x9c, 0x9d},	/* TD2/TR2 */
	{0x23, 0x9e, 0x9f, 0xa0, 0xa1},	/* TD3/TR3 */
	{0x24, 0xa2, 0xa3, 0xa4, 0xa5},	/* TD4/TR4 */
	{0x1f, 0xa6, 0xa7, 0xa8, 0xa9},	/* TR5 */
	{0x20, 0xaa, 0xab, 0xac, 0xad},	/* TR6 */
};

#define IN_READ				0
#define IN_MAX				1
#define IN_LOW				2
static const u16 W83795_REG_IN[][3] = {
	/* Current, HL, LL */
	{0x10, 0x70, 0x71},	/* VSEN1 */
	{0x11, 0x72, 0x73},	/* VSEN2 */
	{0x12, 0x74, 0x75},	/* VSEN3 */
	{0x13, 0x76, 0x77},	/* VSEN4 */
	{0x14, 0x78, 0x79},	/* VSEN5 */
	{0x15, 0x7a, 0x7b},	/* VSEN6 */
	{0x16, 0x7c, 0x7d},	/* VSEN7 */
	{0x17, 0x7e, 0x7f},	/* VSEN8 */
	{0x18, 0x80, 0x81},	/* VSEN9 */
	{0x19, 0x82, 0x83},	/* VSEN10 */
	{0x1A, 0x84, 0x85},	/* VSEN11 */
	{0x1B, 0x86, 0x87},	/* VTT */
	{0x1C, 0x88, 0x89},	/* 3VDD */
	{0x1D, 0x8a, 0x8b},	/* 3VSB */
	{0x1E, 0x8c, 0x8d},	/* VBAT */
	{0x1F, 0xa6, 0xa7},	/* VSEN12 */
	{0x20, 0xaa, 0xab},	/* VSEN13 */
	{0x21, 0x96, 0x97},	/* VSEN14 */
	{0x22, 0x9a, 0x9b},	/* VSEN15 */
	{0x23, 0x9e, 0x9f},	/* VSEN16 */
	{0x24, 0xa2, 0xa3},	/* VSEN17 */
};
#define W83795_REG_VRLSB		0x3C
#define VRLSB_SHIFT			6

static const u8 W83795_REG_IN_HL_LSB[] = {
	0x8e,	/* VSEN1-4 */
	0x90,	/* VSEN5-8 */
	0x92,	/* VSEN9-11 */
	0x94,	/* VTT, 3VDD, 3VSB, 3VBAT */
	0xa8,	/* VSEN12 */
	0xac,	/* VSEN13 */
	0x98,	/* VSEN14 */
	0x9c,	/* VSEN15 */
	0xa0,	/* VSEN16 */
	0xa4,	/* VSEN17 */
};

#define IN_LSB_REG(index, type) \
	(((type) == 1) ? W83795_REG_IN_HL_LSB[(index)] \
	: (W83795_REG_IN_HL_LSB[(index)] + 1))

#define IN_LSB_REG_NUM			10

#define IN_LSB_SHIFT			0
#define IN_LSB_IDX			1
static const u8 IN_LSB_SHIFT_IDX[][2] = {
	/* High/Low LSB shift, LSB No. */
	{0x00, 0x00},	/* VSEN1 */
	{0x02, 0x00},	/* VSEN2 */
	{0x04, 0x00},	/* VSEN3 */
	{0x06, 0x00},	/* VSEN4 */
	{0x00, 0x01},	/* VSEN5 */
	{0x02, 0x01},	/* VSEN6 */
	{0x04, 0x01},	/* VSEN7 */
	{0x06, 0x01},	/* VSEN8 */
	{0x00, 0x02},	/* VSEN9 */
	{0x02, 0x02},	/* VSEN10 */
	{0x04, 0x02},	/* VSEN11 */
	{0x00, 0x03},	/* VTT */
	{0x02, 0x03},	/* 3VDD */
	{0x04, 0x03},	/* 3VSB	*/
	{0x06, 0x03},	/* VBAT	*/
	{0x06, 0x04},	/* VSEN12 */
	{0x06, 0x05},	/* VSEN13 */
	{0x06, 0x06},	/* VSEN14 */
	{0x06, 0x07},	/* VSEN15 */
	{0x06, 0x08},	/* VSEN16 */
	{0x06, 0x09},	/* VSEN17 */
};


/* 3VDD, 3VSB, VBAT * 0.006 */
#define REST_VLT_BEGIN			12  /* the 13th volt to 15th */
#define REST_VLT_END			14  /* the 13th volt to 15th */

#define W83795_REG_FAN(index)		(0x2E + (index))
#define W83795_REG_FAN_MIN_HL(index)	(0xB6 + (index))
#define W83795_REG_FAN_MIN_LSB(index)	(0xC4 + (index) / 2)
#define W83795_REG_FAN_MIN_LSB_SHIFT(index) \
	(((index) % 1) ? 4 : 0)

#define W83795_REG_VID_CTRL		0x6A

#define ALARM_BEEP_REG_NUM		6
#define W83795_REG_ALARM(index)		(0x41 + (index))
#define W83795_REG_BEEP(index)		(0x50 + (index))

#define W83795_REG_CLR_CHASSIS		0x4D


#define W83795_REG_TEMP_NUM		6
#define W83795_REG_FCMS1		0x201
#define W83795_REG_FCMS2		0x208
#define W83795_REG_TFMR(index)		(0x202 + (index))
#define W83795_REG_FOMC			0x20F
#define W83795_REG_FOPFP(index)		(0x218 + (index))

#define W83795_REG_TSS(index)		(0x209 + (index))

#define PWM_OUTPUT			0
#define PWM_START			1
#define PWM_NONSTOP			2
#define PWM_STOP_TIME			3
#define PWM_DIV				4
#define W83795_REG_PWM(index, nr) \
	(((nr) == 0 ? 0x210 : \
	  (nr) == 1 ? 0x220 : \
	  (nr) == 2 ? 0x228 : \
	  (nr) == 3 ? 0x230 : 0x218) + (index))

#define W83795_REG_FOPFP_DIV(index) \
	(((index) < 8) ? ((index) + 1) : \
	 ((index) == 8) ? 12 : \
	 (16 << ((index) - 9)))

#define W83795_REG_FTSH(index)		(0x240 + (index) * 2)
#define W83795_REG_FTSL(index)		(0x241 + (index) * 2)
#define W83795_REG_TFTS			0x250

#define TEMP_PWM_TTTI			0
#define TEMP_PWM_CTFS			1
#define TEMP_PWM_HCT			2
#define TEMP_PWM_HOT			3
#define W83795_REG_TTTI(index)		(0x260 + (index))
#define W83795_REG_CTFS(index)		(0x268 + (index))
#define W83795_REG_HT(index)		(0x270 + (index))

#define SF4_TEMP			0
#define SF4_PWM				1
#define W83795_REG_SF4_TEMP(temp_num, index) \
	(0x280 + 0x10 * (temp_num) + (index))
#define W83795_REG_SF4_PWM(temp_num, index) \
	(0x288 + 0x10 * (temp_num) + (index))

#define W83795_REG_DTSC			0x301
#define W83795_REG_DTSE			0x302
#define W83795_REG_DTS(index)		(0x26 + (index))

#define DTS_CRIT			0
#define DTS_CRIT_HYST			1
#define DTS_WARN			2
#define DTS_WARN_HYST			3
#define W83795_REG_DTS_EXT(index)	(0xB2 + (index))

#define SETUP_PWM_DEFAULT		0
#define SETUP_PWM_UPTIME		1
#define SETUP_PWM_DOWNTIME		2
#define W83795_REG_SETUP_PWM(index)    (0x20C + (index))

static inline u16 in_from_reg(u8 index, u16 val)
{
	if ((index >= REST_VLT_BEGIN) && (index <= REST_VLT_END))
		return val * 6;
	else
		return val * 2;
}

static inline u16 in_to_reg(u8 index, u16 val)
{
	if ((index >= REST_VLT_BEGIN) && (index <= REST_VLT_END))
		return val / 6;
	else
		return val / 2;
}

static inline unsigned long fan_from_reg(u16 val)
{
	if ((val >= 0xff0) || (val == 0))
		return 0;
	return 1350000UL / val;
}

static inline u16 fan_to_reg(long rpm)
{
	if (rpm <= 0)
		return 0x0fff;
	return SENSORS_LIMIT((1350000 + (rpm >> 1)) / rpm, 1, 0xffe);
}

static inline unsigned long time_from_reg(u8 reg)
{
	return reg * 100;
}

static inline u8 time_to_reg(unsigned long val)
{
	return SENSORS_LIMIT((val + 50) / 100, 0, 0xff);
}

static inline long temp_from_reg(s8 reg)
{
	return reg * 1000;
}

static inline s8 temp_to_reg(long val, s8 min, s8 max)
{
	return SENSORS_LIMIT((val < 0 ? -val : val) / 1000, min, max);
}


enum chip_types {w83795g, w83795adg};

struct w83795_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	unsigned long last_updated;	/* In jiffies */
	enum chip_types chip_type;

	u8 bank;

	u32 has_in;		/* Enable monitor VIN or not */
	u16 in[21][3];		/* Register value, read/high/low */
	u8 in_lsb[10][3];	/* LSB Register value, high/low */
	u8 has_gain;		/* has gain: in17-20 * 8 */

	u16 has_fan;		/* Enable fan14-1 or not */
	u16 fan[14];		/* Register value combine */
	u16 fan_min[14];	/* Register value combine */

	u8 has_temp;		/* Enable monitor temp6-1 or not */
	u8 temp[6][5];		/* current, crit, crit_hyst, warn, warn_hyst */
	u8 temp_read_vrlsb[6];
	u8 temp_mode;		/* bit 0: TR mode, bit 1: TD mode */
	u8 temp_src[3];		/* Register value */

	u8 enable_dts;		/* Enable PECI and SB-TSI,
				 * bit 0: =1 enable, =0 disable,
				 * bit 1: =1 AMD SB-TSI, =0 Intel PECI */
	u8 has_dts;		/* Enable monitor DTS temp */
	u8 dts[8];		/* Register value */
	u8 dts_read_vrlsb[8];	/* Register value */
	u8 dts_ext[4];		/* Register value */

	u8 has_pwm;		/* 795g supports 8 pwm, 795adg only supports 2,
				 * no config register, only affected by chip
				 * type */
	u8 pwm[8][5];		/* Register value, output, start, non stop, stop
				 * time, div */
	u8 pwm_fcms[2];		/* Register value */
	u8 pwm_tfmr[6];		/* Register value */
	u8 pwm_fomc;		/* Register value */

	u16 target_speed[8];	/* Register value, target speed for speed
				 * cruise */
	u8 tol_speed;		/* tolerance of target speed */
	u8 pwm_temp[6][4];	/* TTTI, CTFS, HCT, HOT */
	u8 sf4_reg[6][2][7];	/* 6 temp, temp/dcpwm, 7 registers */

	u8 setup_pwm[3];	/* Register value */

	u8 alarms[6];		/* Register value */
	u8 beeps[6];		/* Register value */
	u8 beep_enable;

	char valid;
};

/*
 * Hardware access
 * We assume that nobdody can change the bank outside the driver.
 */

/* Must be called with data->update_lock held, except during initialization */
static int w83795_set_bank(struct i2c_client *client, u8 bank)
{
	struct w83795_data *data = i2c_get_clientdata(client);
	int err;

	/* If the same bank is already set, nothing to do */
	if ((data->bank & 0x07) == bank)
		return 0;

	/* Change to new bank, preserve all other bits */
	bank |= data->bank & ~0x07;
	err = i2c_smbus_write_byte_data(client, W83795_REG_BANKSEL, bank);
	if (err < 0) {
		dev_err(&client->dev,
			"Failed to set bank to %d, err %d\n",
			(int)bank, err);
		return err;
	}
	data->bank = bank;

	return 0;
}

/* Must be called with data->update_lock held, except during initialization */
static u8 w83795_read(struct i2c_client *client, u16 reg)
{
	int err;

	err = w83795_set_bank(client, reg >> 8);
	if (err < 0)
		return 0x00;	/* Arbitrary */

	err = i2c_smbus_read_byte_data(client, reg & 0xff);
	if (err < 0) {
		dev_err(&client->dev,
			"Failed to read from register 0x%03x, err %d\n",
			(int)reg, err);
		return 0x00;	/* Arbitrary */
	}
	return err;
}

/* Must be called with data->update_lock held, except during initialization */
static int w83795_write(struct i2c_client *client, u16 reg, u8 value)
{
	int err;

	err = w83795_set_bank(client, reg >> 8);
	if (err < 0)
		return err;

	err = i2c_smbus_write_byte_data(client, reg & 0xff, value);
	if (err < 0)
		dev_err(&client->dev,
			"Failed to write to register 0x%03x, err %d\n",
			(int)reg, err);
	return err;
}

static struct w83795_data *w83795_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	u16 tmp;
	int i;

	mutex_lock(&data->update_lock);

	if (!(time_after(jiffies, data->last_updated + HZ * 2)
	      || !data->valid))
		goto END;

	/* Update the voltages value */
	for (i = 0; i < ARRAY_SIZE(data->in); i++) {
		if (!(data->has_in & (1 << i)))
			continue;
		tmp = w83795_read(client, W83795_REG_IN[i][IN_READ]) << 2;
		tmp |= (w83795_read(client, W83795_REG_VRLSB)
			>> VRLSB_SHIFT) & 0x03;
		data->in[i][IN_READ] = tmp;
	}

	/* Update fan */
	for (i = 0; i < ARRAY_SIZE(data->fan); i++) {
		if (!(data->has_fan & (1 << i)))
			continue;
		data->fan[i] = w83795_read(client, W83795_REG_FAN(i)) << 4;
		data->fan[i] |=
		  (w83795_read(client, W83795_REG_VRLSB >> 4)) & 0x0F;
	}

	/* Update temperature */
	for (i = 0; i < ARRAY_SIZE(data->temp); i++) {
		/* even stop monitor, register still keep value, just read out
		 * it */
		if (!(data->has_temp & (1 << i))) {
			data->temp[i][TEMP_READ] = 0;
			data->temp_read_vrlsb[i] = 0;
			continue;
		}
		data->temp[i][TEMP_READ] =
			w83795_read(client, W83795_REG_TEMP[i][TEMP_READ]);
		data->temp_read_vrlsb[i] =
			w83795_read(client, W83795_REG_VRLSB);
	}

	/* Update dts temperature */
	if (data->enable_dts != 0) {
		for (i = 0; i < ARRAY_SIZE(data->dts); i++) {
			if (!(data->has_dts & (1 << i)))
				continue;
			data->dts[i] =
				w83795_read(client, W83795_REG_DTS(i));
			data->dts_read_vrlsb[i] =
				w83795_read(client, W83795_REG_VRLSB);
		}
	}

	/* Update pwm output */
	for (i = 0; i < data->has_pwm; i++) {
		data->pwm[i][PWM_OUTPUT] =
		    w83795_read(client, W83795_REG_PWM(i, PWM_OUTPUT));
	}

	/* update alarm */
	for (i = 0; i < ALARM_BEEP_REG_NUM; i++)
		data->alarms[i] = w83795_read(client, W83795_REG_ALARM(i));

	data->last_updated = jiffies;
	data->valid = 1;

END:
	mutex_unlock(&data->update_lock);
	return data;
}

/*
 * Sysfs attributes
 */

#define ALARM_STATUS      0
#define BEEP_ENABLE       1
static ssize_t
show_alarm_beep(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83795_data *data = w83795_update_device(dev);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index >> 3;
	int bit = sensor_attr->index & 0x07;
	u8 val;

	if (ALARM_STATUS == nr) {
		val = (data->alarms[index] >> (bit)) & 1;
	} else {		/* BEEP_ENABLE */
		val = (data->beeps[index] >> (bit)) & 1;
	}

	return sprintf(buf, "%u\n", val);
}

static ssize_t
store_beep(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index >> 3;
	int shift = sensor_attr->index & 0x07;
	u8 beep_bit = 1 << shift;
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->beeps[index] = w83795_read(client, W83795_REG_BEEP(index));
	data->beeps[index] &= ~beep_bit;
	data->beeps[index] |= val << shift;
	w83795_write(client, W83795_REG_BEEP(index), data->beeps[index]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_beep_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	return sprintf(buf, "%u\n", data->beep_enable);
}

static ssize_t
store_beep_enable(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	unsigned long val;
	u8 tmp;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->beep_enable = val;
	tmp = w83795_read(client, W83795_REG_BEEP(5));
	tmp &= 0x7f;
	tmp |= val << 7;
	w83795_write(client, W83795_REG_BEEP(5), tmp);
	mutex_unlock(&data->update_lock);

	return count;
}

/* Write any value to clear chassis alarm */
static ssize_t
store_chassis_clear(struct device *dev,
		    struct device_attribute *attr, const char *buf,
		    size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	u8 val;

	mutex_lock(&data->update_lock);
	val = w83795_read(client, W83795_REG_CLR_CHASSIS);
	val |= 0x80;
	w83795_write(client, W83795_REG_CLR_CHASSIS, val);
	mutex_unlock(&data->update_lock);
	return count;
}

#define FAN_INPUT     0
#define FAN_MIN       1
static ssize_t
show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83795_data *data = w83795_update_device(dev);
	u16 val;

	if (FAN_INPUT == nr)
		val = data->fan[index] & 0x0fff;
	else
		val = data->fan_min[index] & 0x0fff;

	return sprintf(buf, "%lu\n", fan_from_reg(val));
}

static ssize_t
store_fan_min(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;
	val = fan_to_reg(val);

	mutex_lock(&data->update_lock);
	data->fan_min[index] = val;
	w83795_write(client, W83795_REG_FAN_MIN_HL(index), (val >> 4) & 0xff);
	val &= 0x0f;
	if (index % 1) {
		val <<= 4;
		val |= w83795_read(client, W83795_REG_FAN_MIN_LSB(index))
		       & 0x0f;
	} else {
		val |= w83795_read(client, W83795_REG_FAN_MIN_LSB(index))
		       & 0xf0;
	}
	w83795_write(client, W83795_REG_FAN_MIN_LSB(index), val & 0xff);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83795_data *data = w83795_update_device(dev);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	u16 val;

	switch (nr) {
	case PWM_STOP_TIME:
		val = time_from_reg(data->pwm[index][nr]);
		break;
	case PWM_DIV:
		val = W83795_REG_FOPFP_DIV(data->pwm[index][nr] & 0x0f);
		break;
	default:
		val = data->pwm[index][nr];
		break;
	}

	return sprintf(buf, "%u\n", val);
}

static ssize_t
store_pwm(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	unsigned long val;
	int i;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	switch (nr) {
	case PWM_STOP_TIME:
		val = time_to_reg(val);
		break;
	case PWM_DIV:
		for (i = 0; i < 16; i++) {
			if (W83795_REG_FOPFP_DIV(i) == val) {
				val = i;
				break;
			}
		}
		if (i >= 16)
			goto err_end;
		val |= w83795_read(client, W83795_REG_PWM(index, nr)) & 0x80;
		break;
	default:
		val = SENSORS_LIMIT(val, 0, 0xff);
		break;
	}
	w83795_write(client, W83795_REG_PWM(index, nr), val);
	data->pwm[index][nr] = val & 0xff;
	mutex_unlock(&data->update_lock);
	return count;
err_end:
	mutex_unlock(&data->update_lock);
	return -EINVAL;
}

static ssize_t
show_pwm_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	int index = sensor_attr->index;
	u8 tmp;

	if (1 == (data->pwm_fcms[0] & (1 << index))) {
		tmp = 2;
		goto out;
	}
	for (tmp = 0; tmp < 6; tmp++) {
		if (data->pwm_tfmr[tmp] & (1 << index)) {
			tmp = 3;
			goto out;
		}
	}
	if (data->pwm_fomc & (1 << index))
		tmp = 0;
	else
		tmp = 1;

out:
	return sprintf(buf, "%u\n", tmp);
}

static ssize_t
store_pwm_enable(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	unsigned long val;
	int i;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;
	if (val > 2)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	switch (val) {
	case 0:
	case 1:
		data->pwm_fcms[0] &= ~(1 << index);
		w83795_write(client, W83795_REG_FCMS1, data->pwm_fcms[0]);
		for (i = 0; i < 6; i++) {
			data->pwm_tfmr[i] &= ~(1 << index);
			w83795_write(client, W83795_REG_TFMR(i),
				data->pwm_tfmr[i]);
		}
		data->pwm_fomc |= 1 << index;
		data->pwm_fomc ^= val << index;
		w83795_write(client, W83795_REG_FOMC, data->pwm_fomc);
		break;
	case 2:
		data->pwm_fcms[0] |= (1 << index);
		w83795_write(client, W83795_REG_FCMS1, data->pwm_fcms[0]);
		break;
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_temp_src(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	int index = sensor_attr->index;
	u8 val = index / 2;
	u8 tmp = data->temp_src[val];

	if (index % 1)
		val = 4;
	else
		val = 0;
	tmp >>= val;
	tmp &= 0x0f;

	return sprintf(buf, "%u\n", tmp);
}

static ssize_t
store_temp_src(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	unsigned long tmp;
	u8 val = index / 2;

	if (strict_strtoul(buf, 10, &tmp) < 0)
		return -EINVAL;
	tmp = SENSORS_LIMIT(tmp, 0, 15);

	mutex_lock(&data->update_lock);
	if (index % 1) {
		tmp <<= 4;
		data->temp_src[val] &= 0x0f;
	} else {
		data->temp_src[val] &= 0xf0;
	}
	data->temp_src[val] |= tmp;
	w83795_write(client, W83795_REG_TSS(val), data->temp_src[val]);
	mutex_unlock(&data->update_lock);

	return count;
}

#define TEMP_PWM_ENABLE   0
#define TEMP_PWM_FAN_MAP  1
static ssize_t
show_temp_pwm_enable(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	u8 tmp = 0xff;

	switch (nr) {
	case TEMP_PWM_ENABLE:
		tmp = (data->pwm_fcms[1] >> index) & 1;
		if (tmp)
			tmp = 4;
		else
			tmp = 3;
		break;
	case TEMP_PWM_FAN_MAP:
		tmp = data->pwm_tfmr[index];
		break;
	}

	return sprintf(buf, "%u\n", tmp);
}

static ssize_t
store_temp_pwm_enable(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	unsigned long tmp;

	if (strict_strtoul(buf, 10, &tmp) < 0)
		return -EINVAL;

	switch (nr) {
	case TEMP_PWM_ENABLE:
		if ((tmp != 3) && (tmp != 4))
			return -EINVAL;
		tmp -= 3;
		mutex_lock(&data->update_lock);
		data->pwm_fcms[1] &= ~(1 << index);
		data->pwm_fcms[1] |= tmp << index;
		w83795_write(client, W83795_REG_FCMS2, data->pwm_fcms[1]);
		mutex_unlock(&data->update_lock);
		break;
	case TEMP_PWM_FAN_MAP:
		mutex_lock(&data->update_lock);
		tmp = SENSORS_LIMIT(tmp, 0, 0xff);
		w83795_write(client, W83795_REG_TFMR(index), tmp);
		data->pwm_tfmr[index] = tmp;
		mutex_unlock(&data->update_lock);
		break;
	}
	return count;
}

#define FANIN_TARGET   0
#define FANIN_TOL      1
static ssize_t
show_fanin(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	u16 tmp = 0;

	switch (nr) {
	case FANIN_TARGET:
		tmp = fan_from_reg(data->target_speed[index]);
		break;
	case FANIN_TOL:
		tmp = data->tol_speed;
		break;
	}

	return sprintf(buf, "%u\n", tmp);
}

static ssize_t
store_fanin(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	switch (nr) {
	case FANIN_TARGET:
		val = fan_to_reg(SENSORS_LIMIT(val, 0, 0xfff));
		w83795_write(client, W83795_REG_FTSH(index), (val >> 4) & 0xff);
		w83795_write(client, W83795_REG_FTSL(index), (val << 4) & 0xf0);
		data->target_speed[index] = val;
		break;
	case FANIN_TOL:
		val = SENSORS_LIMIT(val, 0, 0x3f);
		w83795_write(client, W83795_REG_TFTS, val);
		data->tol_speed = val;
		break;
	}
	mutex_unlock(&data->update_lock);

	return count;
}


static ssize_t
show_temp_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	long tmp = temp_from_reg(data->pwm_temp[index][nr]);

	return sprintf(buf, "%ld\n", tmp);
}

static ssize_t
store_temp_pwm(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	unsigned long val;
	u8 tmp;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;
	val /= 1000;

	mutex_lock(&data->update_lock);
	switch (nr) {
	case TEMP_PWM_TTTI:
		val = SENSORS_LIMIT(val, 0, 0x7f);
		w83795_write(client, W83795_REG_TTTI(index), val);
		break;
	case TEMP_PWM_CTFS:
		val = SENSORS_LIMIT(val, 0, 0x7f);
		w83795_write(client, W83795_REG_CTFS(index), val);
		break;
	case TEMP_PWM_HCT:
		val = SENSORS_LIMIT(val, 0, 0x0f);
		tmp = w83795_read(client, W83795_REG_HT(index));
		tmp &= 0x0f;
		tmp |= (val << 4) & 0xf0;
		w83795_write(client, W83795_REG_HT(index), tmp);
		break;
	case TEMP_PWM_HOT:
		val = SENSORS_LIMIT(val, 0, 0x0f);
		tmp = w83795_read(client, W83795_REG_HT(index));
		tmp &= 0xf0;
		tmp |= val & 0x0f;
		w83795_write(client, W83795_REG_HT(index), tmp);
		break;
	}
	data->pwm_temp[index][nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_sf4_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;

	return sprintf(buf, "%u\n", data->sf4_reg[index][SF4_PWM][nr]);
}

static ssize_t
store_sf4_pwm(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	w83795_write(client, W83795_REG_SF4_PWM(index, nr), val);
	data->sf4_reg[index][SF4_PWM][nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_sf4_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;

	return sprintf(buf, "%u\n",
		(data->sf4_reg[index][SF4_TEMP][nr]) * 1000);
}

static ssize_t
store_sf4_temp(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;
	val /= 1000;

	mutex_lock(&data->update_lock);
	w83795_write(client, W83795_REG_SF4_TEMP(index, nr), val);
	data->sf4_reg[index][SF4_TEMP][nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}


static ssize_t
show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83795_data *data = w83795_update_device(dev);
	long temp = temp_from_reg(data->temp[index][nr] & 0x7f);

	if (TEMP_READ == nr)
		temp += ((data->temp_read_vrlsb[index] >> VRLSB_SHIFT) & 0x03)
			* 250;
	if (data->temp[index][nr] & 0x80)
		temp = -temp;
	return sprintf(buf, "%ld\n", temp);
}

static ssize_t
store_temp(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	long tmp;

	if (strict_strtol(buf, 10, &tmp) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->temp[index][nr] = temp_to_reg(tmp, -128, 127);
	w83795_write(client, W83795_REG_TEMP[index][nr], data->temp[index][nr]);
	mutex_unlock(&data->update_lock);
	return count;
}


static ssize_t
show_dts_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	u8 tmp;

	if (data->enable_dts == 0)
		return sprintf(buf, "%d\n", 0);

	if ((data->has_dts >> index) & 0x01) {
		if (data->enable_dts & 2)
			tmp = 5;
		else
			tmp = 6;
	} else {
		tmp = 0;
	}

	return sprintf(buf, "%d\n", tmp);
}

static ssize_t
show_dts(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	struct w83795_data *data = w83795_update_device(dev);
	long temp = temp_from_reg(data->dts[index] & 0x7f);

	temp += ((data->dts_read_vrlsb[index] >> VRLSB_SHIFT) & 0x03) * 250;
	if (data->dts[index] & 0x80)
		temp = -temp;
	return sprintf(buf, "%ld\n", temp);
}

static ssize_t
show_dts_ext(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	long temp = temp_from_reg(data->dts_ext[nr] & 0x7f);

	if (data->dts_ext[nr] & 0x80)
		temp = -temp;
	return sprintf(buf, "%ld\n", temp);
}

static ssize_t
store_dts_ext(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	long tmp;

	if (strict_strtol(buf, 10, &tmp) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->dts_ext[nr] = temp_to_reg(tmp, -128, 127);
	w83795_write(client, W83795_REG_DTS_EXT(nr), data->dts_ext[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}


/*
	Type 3:  Thermal diode
	Type 4:  Thermistor

	Temp5-6, default TR
	Temp1-4, default TD
*/

static ssize_t
show_temp_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	u8 tmp;

	if (data->has_temp >> index & 0x01) {
		if (data->temp_mode >> index & 0x01)
			tmp = 3;
		else
			tmp = 4;
	} else {
		tmp = 0;
	}

	return sprintf(buf, "%d\n", tmp);
}

static ssize_t
store_temp_mode(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	unsigned long val;
	u8 tmp;
	u32 mask;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;
	if ((val != 4) && (val != 3))
		return -EINVAL;
	if ((index > 3) && (val == 3))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (val == 3) {
		val = TEMP_CTRL_TD;
		data->has_temp |= 1 << index;
		data->temp_mode |= 1 << index;
	} else if (val == 4) {
		val = TEMP_CTRL_TR;
		data->has_temp |= 1 << index;
		tmp = 1 << index;
		data->temp_mode &= ~tmp;
	}

	if (index > 3)
		tmp = w83795_read(client, W83795_REG_TEMP_CTRL1);
	else
		tmp = w83795_read(client, W83795_REG_TEMP_CTRL2);

	mask = 0x03 << W83795_REG_TEMP_CTRL[index][TEMP_CTRL_SHIFT];
	tmp &= ~mask;
	tmp |= W83795_REG_TEMP_CTRL[index][val];

	mask = 1 << W83795_REG_TEMP_CTRL[index][TEMP_CTRL_HASIN_SHIFT];
	data->has_in &= ~mask;

	if (index > 3)
		w83795_write(client, W83795_REG_TEMP_CTRL1, tmp);
	else
		w83795_write(client, W83795_REG_TEMP_CTRL2, tmp);

	mutex_unlock(&data->update_lock);
	return count;
}


/* show/store VIN */
static ssize_t
show_in(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83795_data *data = w83795_update_device(dev);
	u16 val = data->in[index][nr];
	u8 lsb_idx;

	switch (nr) {
	case IN_READ:
		/* calculate this value again by sensors as sensors3.conf */
		if ((index >= 17) &&
		    ((data->has_gain >> (index - 17)) & 1))
			val *= 8;
		break;
	case IN_MAX:
	case IN_LOW:
		lsb_idx = IN_LSB_SHIFT_IDX[index][IN_LSB_IDX];
		val <<= 2;
		val |= (data->in_lsb[lsb_idx][nr] >>
			IN_LSB_SHIFT_IDX[lsb_idx][IN_LSB_SHIFT]) & 0x03;
		if ((index >= 17) &&
		    ((data->has_gain >> (index - 17)) & 1))
			val *= 8;
		break;
	}
	val = in_from_reg(index, val);

	return sprintf(buf, "%d\n", val);
}

static ssize_t
store_in(struct device *dev, struct device_attribute *attr,
	 const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	unsigned long val;
	u8 tmp;
	u8 lsb_idx;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;
	val = in_to_reg(index, val);

	if ((index >= 17) &&
	    ((data->has_gain >> (index - 17)) & 1))
		val /= 8;
	val = SENSORS_LIMIT(val, 0, 0x3FF);
	mutex_lock(&data->update_lock);

	lsb_idx = IN_LSB_SHIFT_IDX[index][IN_LSB_IDX];
	tmp = w83795_read(client, IN_LSB_REG(lsb_idx, nr));
	tmp &= ~(0x03 << IN_LSB_SHIFT_IDX[index][IN_LSB_SHIFT]);
	tmp |= (val & 0x03) << IN_LSB_SHIFT_IDX[index][IN_LSB_SHIFT];
	w83795_write(client, IN_LSB_REG(lsb_idx, nr), tmp);
	data->in_lsb[lsb_idx][nr] = tmp;

	tmp = (val >> 2) & 0xff;
	w83795_write(client, W83795_REG_IN[index][nr], tmp);
	data->in[index][nr] = tmp;

	mutex_unlock(&data->update_lock);
	return count;
}


static ssize_t
show_sf_setup(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	u16 val = data->setup_pwm[nr];

	switch (nr) {
	case SETUP_PWM_UPTIME:
	case SETUP_PWM_DOWNTIME:
		val = time_from_reg(val);
		break;
	}

	return sprintf(buf, "%d\n", val);
}

static ssize_t
store_sf_setup(struct device *dev, struct device_attribute *attr,
	 const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83795_data *data = i2c_get_clientdata(client);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	switch (nr) {
	case SETUP_PWM_DEFAULT:
		val = SENSORS_LIMIT(val, 0, 0xff);
		break;
	case SETUP_PWM_UPTIME:
	case SETUP_PWM_DOWNTIME:
		val = time_to_reg(val);
		if (val == 0)
			return -EINVAL;
		break;
	}

	mutex_lock(&data->update_lock);
	data->setup_pwm[nr] = val;
	w83795_write(client, W83795_REG_SETUP_PWM(nr), val);
	mutex_unlock(&data->update_lock);
	return count;
}


#define NOT_USED			-1

#define SENSOR_ATTR_IN(index)		\
	SENSOR_ATTR_2(in##index##_input, S_IRUGO, show_in, NULL,	\
		IN_READ, index), \
	SENSOR_ATTR_2(in##index##_max, S_IRUGO | S_IWUSR, show_in,	\
		store_in, IN_MAX, index),				\
	SENSOR_ATTR_2(in##index##_min, S_IRUGO | S_IWUSR, show_in,	\
		store_in, IN_LOW, index),				\
	SENSOR_ATTR_2(in##index##_alarm, S_IRUGO, show_alarm_beep,	\
		NULL, ALARM_STATUS, index + ((index > 14) ? 1 : 0)), \
	SENSOR_ATTR_2(in##index##_beep, S_IWUSR | S_IRUGO,		\
		show_alarm_beep, store_beep, BEEP_ENABLE,		\
		index + ((index > 14) ? 1 : 0))

#define SENSOR_ATTR_FAN(index)						\
	SENSOR_ATTR_2(fan##index##_input, S_IRUGO, show_fan,		\
		NULL, FAN_INPUT, index - 1), \
	SENSOR_ATTR_2(fan##index##_min, S_IWUSR | S_IRUGO,		\
		show_fan, store_fan_min, FAN_MIN, index - 1),	\
	SENSOR_ATTR_2(fan##index##_alarm, S_IRUGO, show_alarm_beep,	\
		NULL, ALARM_STATUS, index + 31),			\
	SENSOR_ATTR_2(fan##index##_beep, S_IWUSR | S_IRUGO,		\
		show_alarm_beep, store_beep, BEEP_ENABLE, index + 31)

#define SENSOR_ATTR_PWM(index)						\
	SENSOR_ATTR_2(pwm##index, S_IWUSR | S_IRUGO, show_pwm,		\
		store_pwm, PWM_OUTPUT, index - 1),			\
	SENSOR_ATTR_2(pwm##index##_nonstop, S_IWUSR | S_IRUGO,		\
		show_pwm, store_pwm, PWM_NONSTOP, index - 1),		\
	SENSOR_ATTR_2(pwm##index##_start, S_IWUSR | S_IRUGO,		\
		show_pwm, store_pwm, PWM_START, index - 1),		\
	SENSOR_ATTR_2(pwm##index##_stop_time, S_IWUSR | S_IRUGO,	\
		show_pwm, store_pwm, PWM_STOP_TIME, index - 1),	 \
	SENSOR_ATTR_2(fan##index##_div, S_IWUSR | S_IRUGO,	\
		show_pwm, store_pwm, PWM_DIV, index - 1),	 \
	SENSOR_ATTR_2(pwm##index##_enable, S_IWUSR | S_IRUGO,		\
		show_pwm_enable, store_pwm_enable, NOT_USED, index - 1)

#define SENSOR_ATTR_FANIN_TARGET(index)					\
	SENSOR_ATTR_2(speed_cruise##index##_target, S_IWUSR | S_IRUGO, \
		show_fanin, store_fanin, FANIN_TARGET, index - 1)

#define SENSOR_ATTR_DTS(index)						\
	SENSOR_ATTR_2(temp##index##_type, S_IRUGO ,		\
		show_dts_mode, NULL, NOT_USED, index - 7),	\
	SENSOR_ATTR_2(temp##index##_input, S_IRUGO, show_dts,		\
		NULL, NOT_USED, index - 7),				\
	SENSOR_ATTR_2(temp##index##_max, S_IRUGO | S_IWUSR, show_dts_ext, \
		store_dts_ext, DTS_CRIT, NOT_USED),			\
	SENSOR_ATTR_2(temp##index##_max_hyst, S_IRUGO | S_IWUSR,	\
		show_dts_ext, store_dts_ext, DTS_CRIT_HYST, NOT_USED),	\
	SENSOR_ATTR_2(temp##index##_warn, S_IRUGO | S_IWUSR, show_dts_ext, \
		store_dts_ext, DTS_WARN, NOT_USED),			\
	SENSOR_ATTR_2(temp##index##_warn_hyst, S_IRUGO | S_IWUSR,	\
		show_dts_ext, store_dts_ext, DTS_WARN_HYST, NOT_USED),	\
	SENSOR_ATTR_2(temp##index##_alarm, S_IRUGO,			\
		show_alarm_beep, NULL, ALARM_STATUS, index + 17),	\
	SENSOR_ATTR_2(temp##index##_beep, S_IWUSR | S_IRUGO,		\
		show_alarm_beep, store_beep, BEEP_ENABLE, index + 17)

#define SENSOR_ATTR_TEMP(index)						\
	SENSOR_ATTR_2(temp##index##_type, S_IRUGO | S_IWUSR,		\
		show_temp_mode, store_temp_mode, NOT_USED, index - 1),	\
	SENSOR_ATTR_2(temp##index##_input, S_IRUGO, show_temp,		\
		NULL, TEMP_READ, index - 1),				\
	SENSOR_ATTR_2(temp##index##_max, S_IRUGO | S_IWUSR, show_temp,	\
		store_temp, TEMP_CRIT, index - 1),			\
	SENSOR_ATTR_2(temp##index##_max_hyst, S_IRUGO | S_IWUSR,	\
		show_temp, store_temp, TEMP_CRIT_HYST, index - 1),	\
	SENSOR_ATTR_2(temp##index##_warn, S_IRUGO | S_IWUSR, show_temp,	\
		store_temp, TEMP_WARN, index - 1),			\
	SENSOR_ATTR_2(temp##index##_warn_hyst, S_IRUGO | S_IWUSR,	\
		show_temp, store_temp, TEMP_WARN_HYST, index - 1),	\
	SENSOR_ATTR_2(temp##index##_alarm, S_IRUGO,			\
		show_alarm_beep, NULL, ALARM_STATUS,			\
		index + (index > 4 ? 11 : 17)),				\
	SENSOR_ATTR_2(temp##index##_beep, S_IWUSR | S_IRUGO,		\
		show_alarm_beep, store_beep, BEEP_ENABLE,		\
		index + (index > 4 ? 11 : 17)),				\
	SENSOR_ATTR_2(temp##index##_source_sel, S_IWUSR | S_IRUGO,	\
		show_temp_src, store_temp_src, NOT_USED, index - 1),	\
	SENSOR_ATTR_2(temp##index##_pwm_enable, S_IWUSR | S_IRUGO,	\
		show_temp_pwm_enable, store_temp_pwm_enable,		\
		TEMP_PWM_ENABLE, index - 1),				\
	SENSOR_ATTR_2(temp##index##_auto_channels_pwm, S_IWUSR | S_IRUGO, \
		show_temp_pwm_enable, store_temp_pwm_enable,		\
		TEMP_PWM_FAN_MAP, index - 1),				\
	SENSOR_ATTR_2(thermal_cruise##index, S_IWUSR | S_IRUGO,		\
		show_temp_pwm, store_temp_pwm, TEMP_PWM_TTTI, index - 1), \
	SENSOR_ATTR_2(temp##index##_crit, S_IWUSR | S_IRUGO,		\
		show_temp_pwm, store_temp_pwm, TEMP_PWM_CTFS, index - 1), \
	SENSOR_ATTR_2(temp##index##_crit_hyst, S_IWUSR | S_IRUGO,	\
		show_temp_pwm, store_temp_pwm, TEMP_PWM_HCT, index - 1), \
	SENSOR_ATTR_2(temp##index##_operation_hyst, S_IWUSR | S_IRUGO,	\
		show_temp_pwm, store_temp_pwm, TEMP_PWM_HOT, index - 1), \
	SENSOR_ATTR_2(temp##index##_auto_point1_pwm, S_IRUGO | S_IWUSR, \
		show_sf4_pwm, store_sf4_pwm, 0, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point2_pwm, S_IRUGO | S_IWUSR, \
		show_sf4_pwm, store_sf4_pwm, 1, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point3_pwm, S_IRUGO | S_IWUSR, \
		show_sf4_pwm, store_sf4_pwm, 2, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point4_pwm, S_IRUGO | S_IWUSR, \
		show_sf4_pwm, store_sf4_pwm, 3, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point5_pwm, S_IRUGO | S_IWUSR, \
		show_sf4_pwm, store_sf4_pwm, 4, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point6_pwm, S_IRUGO | S_IWUSR, \
		show_sf4_pwm, store_sf4_pwm, 5, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point7_pwm, S_IRUGO | S_IWUSR, \
		show_sf4_pwm, store_sf4_pwm, 6, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point1_temp, S_IRUGO | S_IWUSR,\
		show_sf4_temp, store_sf4_temp, 0, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point2_temp, S_IRUGO | S_IWUSR,\
		show_sf4_temp, store_sf4_temp, 1, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point3_temp, S_IRUGO | S_IWUSR,\
		show_sf4_temp, store_sf4_temp, 2, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point4_temp, S_IRUGO | S_IWUSR,\
		show_sf4_temp, store_sf4_temp, 3, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point5_temp, S_IRUGO | S_IWUSR,\
		show_sf4_temp, store_sf4_temp, 4, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point6_temp, S_IRUGO | S_IWUSR,\
		show_sf4_temp, store_sf4_temp, 5, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point7_temp, S_IRUGO | S_IWUSR,\
		show_sf4_temp, store_sf4_temp, 6, index - 1)


static struct sensor_device_attribute_2 w83795_in[] = {
	SENSOR_ATTR_IN(0),
	SENSOR_ATTR_IN(1),
	SENSOR_ATTR_IN(2),
	SENSOR_ATTR_IN(3),
	SENSOR_ATTR_IN(4),
	SENSOR_ATTR_IN(5),
	SENSOR_ATTR_IN(6),
	SENSOR_ATTR_IN(7),
	SENSOR_ATTR_IN(8),
	SENSOR_ATTR_IN(9),
	SENSOR_ATTR_IN(10),
	SENSOR_ATTR_IN(11),
	SENSOR_ATTR_IN(12),
	SENSOR_ATTR_IN(13),
	SENSOR_ATTR_IN(14),
	SENSOR_ATTR_IN(15),
	SENSOR_ATTR_IN(16),
	SENSOR_ATTR_IN(17),
	SENSOR_ATTR_IN(18),
	SENSOR_ATTR_IN(19),
	SENSOR_ATTR_IN(20),
};

static struct sensor_device_attribute_2 w83795_fan[] = {
	SENSOR_ATTR_FAN(1),
	SENSOR_ATTR_FAN(2),
	SENSOR_ATTR_FAN(3),
	SENSOR_ATTR_FAN(4),
	SENSOR_ATTR_FAN(5),
	SENSOR_ATTR_FAN(6),
	SENSOR_ATTR_FAN(7),
	SENSOR_ATTR_FAN(8),
	SENSOR_ATTR_FAN(9),
	SENSOR_ATTR_FAN(10),
	SENSOR_ATTR_FAN(11),
	SENSOR_ATTR_FAN(12),
	SENSOR_ATTR_FAN(13),
	SENSOR_ATTR_FAN(14),
};

static struct sensor_device_attribute_2 w83795_temp[] = {
	SENSOR_ATTR_TEMP(1),
	SENSOR_ATTR_TEMP(2),
	SENSOR_ATTR_TEMP(3),
	SENSOR_ATTR_TEMP(4),
	SENSOR_ATTR_TEMP(5),
	SENSOR_ATTR_TEMP(6),
};

static struct sensor_device_attribute_2 w83795_dts[] = {
	SENSOR_ATTR_DTS(7),
	SENSOR_ATTR_DTS(8),
	SENSOR_ATTR_DTS(9),
	SENSOR_ATTR_DTS(10),
	SENSOR_ATTR_DTS(11),
	SENSOR_ATTR_DTS(12),
	SENSOR_ATTR_DTS(13),
	SENSOR_ATTR_DTS(14),
};

static struct sensor_device_attribute_2 w83795_static[] = {
	SENSOR_ATTR_FANIN_TARGET(1),
	SENSOR_ATTR_FANIN_TARGET(2),
	SENSOR_ATTR_FANIN_TARGET(3),
	SENSOR_ATTR_FANIN_TARGET(4),
	SENSOR_ATTR_FANIN_TARGET(5),
	SENSOR_ATTR_FANIN_TARGET(6),
	SENSOR_ATTR_FANIN_TARGET(7),
	SENSOR_ATTR_FANIN_TARGET(8),
	SENSOR_ATTR_PWM(1),
	SENSOR_ATTR_PWM(2),
};

/* all registers existed in 795g than 795adg,
 * like PWM3 - PWM8 */
static struct sensor_device_attribute_2 w83795_left_reg[] = {
	SENSOR_ATTR_PWM(3),
	SENSOR_ATTR_PWM(4),
	SENSOR_ATTR_PWM(5),
	SENSOR_ATTR_PWM(6),
	SENSOR_ATTR_PWM(7),
	SENSOR_ATTR_PWM(8),
};

static struct sensor_device_attribute_2 sda_single_files[] = {
	SENSOR_ATTR_2(chassis, S_IWUSR | S_IRUGO, show_alarm_beep,
		      store_chassis_clear, ALARM_STATUS, 46),
	SENSOR_ATTR_2(beep_enable, S_IWUSR | S_IRUGO, show_beep_enable,
		      store_beep_enable, NOT_USED, NOT_USED),
	SENSOR_ATTR_2(speed_cruise_tolerance, S_IWUSR | S_IRUGO, show_fanin,
		store_fanin, FANIN_TOL, NOT_USED),
	SENSOR_ATTR_2(pwm_default, S_IWUSR | S_IRUGO, show_sf_setup,
		      store_sf_setup, SETUP_PWM_DEFAULT, NOT_USED),
	SENSOR_ATTR_2(pwm_uptime, S_IWUSR | S_IRUGO, show_sf_setup,
		      store_sf_setup, SETUP_PWM_UPTIME, NOT_USED),
	SENSOR_ATTR_2(pwm_downtime, S_IWUSR | S_IRUGO, show_sf_setup,
		      store_sf_setup, SETUP_PWM_DOWNTIME, NOT_USED),
};

/*
 * Driver interface
 */

static void w83795_init_client(struct i2c_client *client)
{
	if (reset)
		w83795_write(client, W83795_REG_CONFIG, 0x80);

	/* Start monitoring */
	w83795_write(client, W83795_REG_CONFIG,
		     w83795_read(client, W83795_REG_CONFIG) | 0x01);
}

static int w83795_get_device_id(struct i2c_client *client)
{
	int device_id;

	device_id = i2c_smbus_read_byte_data(client, W83795_REG_DEVICEID);

	/* Special case for rev. A chips; can't be checked first because later
	   revisions emulate this for compatibility */
	if (device_id < 0 || (device_id & 0xf0) != 0x50) {
		int alt_id;

		alt_id = i2c_smbus_read_byte_data(client,
						  W83795_REG_DEVICEID_A);
		if (alt_id == 0x50)
			device_id = alt_id;
	}

	return device_id;
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int w83795_detect(struct i2c_client *client,
			 struct i2c_board_info *info)
{
	int bank, vendor_id, device_id, expected, i2c_addr, config;
	struct i2c_adapter *adapter = client->adapter;
	unsigned short address = client->addr;
	const char *chip_name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;
	bank = i2c_smbus_read_byte_data(client, W83795_REG_BANKSEL);
	if (bank < 0 || (bank & 0x7c)) {
		dev_dbg(&adapter->dev,
			"w83795: Detection failed at addr 0x%02hx, check %s\n",
			address, "bank");
		return -ENODEV;
	}

	/* Check Nuvoton vendor ID */
	vendor_id = i2c_smbus_read_byte_data(client, W83795_REG_VENDORID);
	expected = bank & 0x80 ? 0x5c : 0xa3;
	if (vendor_id != expected) {
		dev_dbg(&adapter->dev,
			"w83795: Detection failed at addr 0x%02hx, check %s\n",
			address, "vendor id");
		return -ENODEV;
	}

	/* Check device ID */
	device_id = w83795_get_device_id(client) |
		    (i2c_smbus_read_byte_data(client, W83795_REG_CHIPID) << 8);
	if ((device_id >> 4) != 0x795) {
		dev_dbg(&adapter->dev,
			"w83795: Detection failed at addr 0x%02hx, check %s\n",
			address, "device id\n");
		return -ENODEV;
	}

	/* If Nuvoton chip, address of chip and W83795_REG_I2C_ADDR
	   should match */
	if ((bank & 0x07) == 0) {
		i2c_addr = i2c_smbus_read_byte_data(client,
						    W83795_REG_I2C_ADDR);
		if ((i2c_addr & 0x7f) != address) {
			dev_dbg(&adapter->dev,
				"w83795: Detection failed at addr 0x%02hx, "
				"check %s\n", address, "i2c addr");
			return -ENODEV;
		}
	}

	/* Check 795 chip type: 795G or 795ADG
	   Usually we don't write to chips during detection, but here we don't
	   quite have the choice; hopefully it's OK, we are about to return
	   success anyway */
	if ((bank & 0x07) != 0)
		i2c_smbus_write_byte_data(client, W83795_REG_BANKSEL,
					  bank & ~0x07);
	config = i2c_smbus_read_byte_data(client, W83795_REG_CONFIG);
	if (config & W83795_REG_CONFIG_CONFIG48)
		chip_name = "w83795adg";
	else
		chip_name = "w83795g";

	strlcpy(info->type, chip_name, I2C_NAME_SIZE);
	dev_info(&adapter->dev, "Found %s rev. %c at 0x%02hx\n", chip_name,
		 'A' + (device_id & 0xf), address);

	return 0;
}

static int w83795_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int i;
	u8 tmp;
	struct device *dev = &client->dev;
	struct w83795_data *data;
	int err = 0;

	data = kzalloc(sizeof(struct w83795_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	data->chip_type = id->driver_data;
	data->bank = i2c_smbus_read_byte_data(client, W83795_REG_BANKSEL);
	mutex_init(&data->update_lock);

	/* Initialize the chip */
	w83795_init_client(client);

	data->has_in = w83795_read(client, W83795_REG_VOLT_CTRL1);
	data->has_in |= w83795_read(client, W83795_REG_VOLT_CTRL2) << 8;
	/* VSEN11-9 not for 795adg */
	if (data->chip_type == w83795adg)
		data->has_in &= 0xf8ff;
	data->has_fan = w83795_read(client, W83795_REG_FANIN_CTRL1);
	data->has_fan |= w83795_read(client, W83795_REG_FANIN_CTRL2) << 8;

	/* VDSEN12-17 and TR1-6, TD1-4 use same register */
	tmp = w83795_read(client, W83795_REG_TEMP_CTRL1);
	if (tmp & 0x20)
		data->enable_dts = 1;
	else
		data->enable_dts = 0;
	data->has_temp = 0;
	data->temp_mode = 0;
	if (tmp & 0x08) {
		if (tmp & 0x04)
			data->has_temp |= 0x20;
		else
			data->has_in |= 0x10000;
	}
	if (tmp & 0x02) {
		if (tmp & 0x01)
			data->has_temp |= 0x10;
		else
			data->has_in |= 0x8000;
	}
	tmp = w83795_read(client, W83795_REG_TEMP_CTRL2);
	if (tmp & 0x40) {
		data->has_temp |= 0x08;
		if (!(tmp & 0x80))
			data->temp_mode |= 0x08;
	} else if (tmp & 0x80) {
		data->has_in |= 0x100000;
	}
	if (tmp & 0x10) {
		data->has_temp |= 0x04;
		if (!(tmp & 0x20))
			data->temp_mode |= 0x04;
	} else if (tmp & 0x20) {
		data->has_in |= 0x80000;
	}
	if (tmp & 0x04) {
		data->has_temp |= 0x02;
		if (!(tmp & 0x08))
			data->temp_mode |= 0x02;
	} else if (tmp & 0x08) {
		data->has_in |= 0x40000;
	}
	if (tmp & 0x01) {
		data->has_temp |= 0x01;
		if (!(tmp & 0x02))
			data->temp_mode |= 0x01;
	} else if (tmp & 0x02) {
		data->has_in |= 0x20000;
	}

	/* Check DTS enable status */
	if (data->enable_dts == 0) {
		data->has_dts = 0;
	} else {
		if (1 & w83795_read(client, W83795_REG_DTSC))
			data->enable_dts |= 2;
		data->has_dts = w83795_read(client, W83795_REG_DTSE);
	}

	/* First update the voltages measured value and limits */
	for (i = 0; i < ARRAY_SIZE(data->in); i++) {
		if (!(data->has_in & (1 << i)))
			continue;
		data->in[i][IN_MAX] =
			w83795_read(client, W83795_REG_IN[i][IN_MAX]);
		data->in[i][IN_LOW] =
			w83795_read(client, W83795_REG_IN[i][IN_LOW]);
		tmp = w83795_read(client, W83795_REG_IN[i][IN_READ]) << 2;
		tmp |= (w83795_read(client, W83795_REG_VRLSB)
			>> VRLSB_SHIFT) & 0x03;
		data->in[i][IN_READ] = tmp;
	}
	for (i = 0; i < IN_LSB_REG_NUM; i++) {
		data->in_lsb[i][IN_MAX] =
			w83795_read(client, IN_LSB_REG(i, IN_MAX));
		data->in_lsb[i][IN_LOW] =
			w83795_read(client, IN_LSB_REG(i, IN_LOW));
	}
	data->has_gain = w83795_read(client, W83795_REG_VMIGB_CTRL) & 0x0f;

	/* First update fan and limits */
	for (i = 0; i < ARRAY_SIZE(data->fan); i++) {
		if (!(data->has_fan & (1 << i)))
			continue;
		data->fan_min[i] =
			w83795_read(client, W83795_REG_FAN_MIN_HL(i)) << 4;
		data->fan_min[i] |=
		  (w83795_read(client, W83795_REG_FAN_MIN_LSB(i) >>
			W83795_REG_FAN_MIN_LSB_SHIFT(i))) & 0x0F;
		data->fan[i] = w83795_read(client, W83795_REG_FAN(i)) << 4;
		data->fan[i] |=
		  (w83795_read(client, W83795_REG_VRLSB >> 4)) & 0x0F;
	}

	/* temperature and limits */
	for (i = 0; i < ARRAY_SIZE(data->temp); i++) {
		if (!(data->has_temp & (1 << i)))
			continue;
		data->temp[i][TEMP_CRIT] =
			w83795_read(client, W83795_REG_TEMP[i][TEMP_CRIT]);
		data->temp[i][TEMP_CRIT_HYST] =
			w83795_read(client, W83795_REG_TEMP[i][TEMP_CRIT_HYST]);
		data->temp[i][TEMP_WARN] =
			w83795_read(client, W83795_REG_TEMP[i][TEMP_WARN]);
		data->temp[i][TEMP_WARN_HYST] =
			w83795_read(client, W83795_REG_TEMP[i][TEMP_WARN_HYST]);
		data->temp[i][TEMP_READ] =
			w83795_read(client, W83795_REG_TEMP[i][TEMP_READ]);
		data->temp_read_vrlsb[i] =
			w83795_read(client, W83795_REG_VRLSB);
	}

	/* dts temperature and limits */
	if (data->enable_dts != 0) {
		data->dts_ext[DTS_CRIT] =
			w83795_read(client, W83795_REG_DTS_EXT(DTS_CRIT));
		data->dts_ext[DTS_CRIT_HYST] =
			w83795_read(client, W83795_REG_DTS_EXT(DTS_CRIT_HYST));
		data->dts_ext[DTS_WARN] =
			w83795_read(client, W83795_REG_DTS_EXT(DTS_WARN));
		data->dts_ext[DTS_WARN_HYST] =
			w83795_read(client, W83795_REG_DTS_EXT(DTS_WARN_HYST));
		for (i = 0; i < ARRAY_SIZE(data->dts); i++) {
			if (!(data->has_dts & (1 << i)))
				continue;
			data->dts[i] = w83795_read(client, W83795_REG_DTS(i));
			data->dts_read_vrlsb[i] =
				w83795_read(client, W83795_REG_VRLSB);
		}
	}

	/* First update temp source selction */
	for (i = 0; i < 3; i++)
		data->temp_src[i] = w83795_read(client, W83795_REG_TSS(i));

	/* pwm and smart fan */
	if (data->chip_type == w83795g)
		data->has_pwm = 8;
	else
		data->has_pwm = 2;
	data->pwm_fcms[0] = w83795_read(client, W83795_REG_FCMS1);
	data->pwm_fcms[1] = w83795_read(client, W83795_REG_FCMS2);
	/* w83795adg only support pwm2-0 */
	for (i = 0; i < W83795_REG_TEMP_NUM; i++)
		data->pwm_tfmr[i] = w83795_read(client, W83795_REG_TFMR(i));
	data->pwm_fomc = w83795_read(client, W83795_REG_FOMC);
	for (i = 0; i < data->has_pwm; i++) {
		for (tmp = 0; tmp < 5; tmp++) {
			data->pwm[i][tmp] =
				w83795_read(client, W83795_REG_PWM(i, tmp));
		}
	}
	for (i = 0; i < 8; i++) {
		data->target_speed[i] =
			w83795_read(client, W83795_REG_FTSH(i)) << 4;
		data->target_speed[i] |=
			w83795_read(client, W83795_REG_FTSL(i)) >> 4;
	}
	data->tol_speed = w83795_read(client, W83795_REG_TFTS) & 0x3f;

	for (i = 0; i < W83795_REG_TEMP_NUM; i++) {
		data->pwm_temp[i][TEMP_PWM_TTTI] =
			w83795_read(client, W83795_REG_TTTI(i)) & 0x7f;
		data->pwm_temp[i][TEMP_PWM_CTFS] =
			w83795_read(client, W83795_REG_CTFS(i));
		tmp = w83795_read(client, W83795_REG_HT(i));
		data->pwm_temp[i][TEMP_PWM_HCT] = (tmp >> 4) & 0x0f;
		data->pwm_temp[i][TEMP_PWM_HOT] = tmp & 0x0f;
	}
	for (i = 0; i < W83795_REG_TEMP_NUM; i++) {
		for (tmp = 0; tmp < 7; tmp++) {
			data->sf4_reg[i][SF4_TEMP][tmp] =
				w83795_read(client,
					    W83795_REG_SF4_TEMP(i, tmp));
			data->sf4_reg[i][SF4_PWM][tmp] =
				w83795_read(client, W83795_REG_SF4_PWM(i, tmp));
		}
	}

	/* Setup PWM Register */
	for (i = 0; i < 3; i++) {
		data->setup_pwm[i] =
			w83795_read(client, W83795_REG_SETUP_PWM(i));
	}

	/* alarm and beep */
	for (i = 0; i < ALARM_BEEP_REG_NUM; i++) {
		data->alarms[i] = w83795_read(client, W83795_REG_ALARM(i));
		data->beeps[i] = w83795_read(client, W83795_REG_BEEP(i));
	}
	data->beep_enable =
		(w83795_read(client, W83795_REG_BEEP(5)) >> 7) & 0x01;

	/* Register sysfs hooks */
	for (i = 0; i < ARRAY_SIZE(w83795_in); i++) {
		if (!(data->has_in & (1 << (i / 6))))
			continue;
		err = device_create_file(dev, &w83795_in[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	for (i = 0; i < ARRAY_SIZE(w83795_fan); i++) {
		if (!(data->has_fan & (1 << (i / 5))))
			continue;
		err = device_create_file(dev, &w83795_fan[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	for (i = 0; i < ARRAY_SIZE(sda_single_files); i++) {
		err = device_create_file(dev, &sda_single_files[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	for (i = 0; i < ARRAY_SIZE(w83795_temp); i++) {
		if (!(data->has_temp & (1 << (i / 29))))
			continue;
		err = device_create_file(dev, &w83795_temp[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	if (data->enable_dts != 0) {
		for (i = 0; i < ARRAY_SIZE(w83795_dts); i++) {
			if (!(data->has_dts & (1 << (i / 8))))
				continue;
			err = device_create_file(dev, &w83795_dts[i].dev_attr);
			if (err)
				goto exit_remove;
		}
	}

	if (data->chip_type == w83795g) {
		for (i = 0; i < ARRAY_SIZE(w83795_left_reg); i++) {
			err = device_create_file(dev,
						 &w83795_left_reg[i].dev_attr);
			if (err)
				goto exit_remove;
		}
	}

	for (i = 0; i < ARRAY_SIZE(w83795_static); i++) {
		err = device_create_file(dev, &w83795_static[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	data->hwmon_dev = hwmon_device_register(dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

	/* Unregister sysfs hooks */
exit_remove:
	for (i = 0; i < ARRAY_SIZE(w83795_in); i++)
		device_remove_file(dev, &w83795_in[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83795_fan); i++)
		device_remove_file(dev, &w83795_fan[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(sda_single_files); i++)
		device_remove_file(dev, &sda_single_files[i].dev_attr);

	if (data->chip_type == w83795g) {
		for (i = 0; i < ARRAY_SIZE(w83795_left_reg); i++)
			device_remove_file(dev, &w83795_left_reg[i].dev_attr);
	}

	for (i = 0; i < ARRAY_SIZE(w83795_temp); i++)
		device_remove_file(dev, &w83795_temp[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83795_dts); i++)
		device_remove_file(dev, &w83795_dts[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83795_static); i++)
		device_remove_file(dev, &w83795_static[i].dev_attr);

	kfree(data);
exit:
	return err;
}

static int w83795_remove(struct i2c_client *client)
{
	struct w83795_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int i;

	hwmon_device_unregister(data->hwmon_dev);

	for (i = 0; i < ARRAY_SIZE(w83795_in); i++)
		device_remove_file(dev, &w83795_in[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83795_fan); i++)
		device_remove_file(dev, &w83795_fan[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(sda_single_files); i++)
		device_remove_file(dev, &sda_single_files[i].dev_attr);

	if (data->chip_type == w83795g) {
		for (i = 0; i < ARRAY_SIZE(w83795_left_reg); i++)
			device_remove_file(dev, &w83795_left_reg[i].dev_attr);
	}

	for (i = 0; i < ARRAY_SIZE(w83795_temp); i++)
		device_remove_file(dev, &w83795_temp[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83795_dts); i++)
		device_remove_file(dev, &w83795_dts[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83795_static); i++)
		device_remove_file(dev, &w83795_static[i].dev_attr);

	kfree(data);

	return 0;
}


static const struct i2c_device_id w83795_id[] = {
	{ "w83795g", w83795g },
	{ "w83795adg", w83795adg },
	{ }
};
MODULE_DEVICE_TABLE(i2c, w83795_id);

static struct i2c_driver w83795_driver = {
	.driver = {
		   .name = "w83795",
	},
	.probe		= w83795_probe,
	.remove		= w83795_remove,
	.id_table	= w83795_id,

	.class		= I2C_CLASS_HWMON,
	.detect		= w83795_detect,
	.address_list	= normal_i2c,
};

static int __init sensors_w83795_init(void)
{
	return i2c_add_driver(&w83795_driver);
}

static void __exit sensors_w83795_exit(void)
{
	i2c_del_driver(&w83795_driver);
}

MODULE_AUTHOR("Wei Song");
MODULE_DESCRIPTION("W83795G/ADG hardware monitoring driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83795_init);
module_exit(sensors_w83795_exit);
