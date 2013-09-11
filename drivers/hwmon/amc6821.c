/*
 * amc6821.c - Part of lm_sensors, Linux kernel modules for hardware
 *	       monitoring
 * Copyright (C) 2009 T. Mertelj <tomaz.mertelj@guest.arnes.si>
 *
 * Based on max6650.c:
 * Copyright (C) 2007 Hans J. Koch <hjk@hansjkoch.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>


/*
 * Addresses to scan.
 */

static const unsigned short normal_i2c[] = {0x18, 0x19, 0x1a, 0x2c, 0x2d, 0x2e,
	0x4c, 0x4d, 0x4e, I2C_CLIENT_END};



/*
 * Insmod parameters
 */

static int pwminv;	/*Inverted PWM output. */
module_param(pwminv, int, S_IRUGO);

static int init = 1; /*Power-on initialization.*/
module_param(init, int, S_IRUGO);


enum chips { amc6821 };

#define AMC6821_REG_DEV_ID 0x3D
#define AMC6821_REG_COMP_ID 0x3E
#define AMC6821_REG_CONF1 0x00
#define AMC6821_REG_CONF2 0x01
#define AMC6821_REG_CONF3 0x3F
#define AMC6821_REG_CONF4 0x04
#define AMC6821_REG_STAT1 0x02
#define AMC6821_REG_STAT2 0x03
#define AMC6821_REG_TDATA_LOW 0x08
#define AMC6821_REG_TDATA_HI 0x09
#define AMC6821_REG_LTEMP_HI 0x0A
#define AMC6821_REG_RTEMP_HI 0x0B
#define AMC6821_REG_LTEMP_LIMIT_MIN 0x15
#define AMC6821_REG_LTEMP_LIMIT_MAX 0x14
#define AMC6821_REG_RTEMP_LIMIT_MIN 0x19
#define AMC6821_REG_RTEMP_LIMIT_MAX 0x18
#define AMC6821_REG_LTEMP_CRIT 0x1B
#define AMC6821_REG_RTEMP_CRIT 0x1D
#define AMC6821_REG_PSV_TEMP 0x1C
#define AMC6821_REG_DCY 0x22
#define AMC6821_REG_LTEMP_FAN_CTRL 0x24
#define AMC6821_REG_RTEMP_FAN_CTRL 0x25
#define AMC6821_REG_DCY_LOW_TEMP 0x21

#define AMC6821_REG_TACH_LLIMITL 0x10
#define AMC6821_REG_TACH_LLIMITH 0x11
#define AMC6821_REG_TACH_HLIMITL 0x12
#define AMC6821_REG_TACH_HLIMITH 0x13

#define AMC6821_CONF1_START 0x01
#define AMC6821_CONF1_FAN_INT_EN 0x02
#define AMC6821_CONF1_FANIE 0x04
#define AMC6821_CONF1_PWMINV 0x08
#define AMC6821_CONF1_FAN_FAULT_EN 0x10
#define AMC6821_CONF1_FDRC0 0x20
#define AMC6821_CONF1_FDRC1 0x40
#define AMC6821_CONF1_THERMOVIE 0x80

#define AMC6821_CONF2_PWM_EN 0x01
#define AMC6821_CONF2_TACH_MODE 0x02
#define AMC6821_CONF2_TACH_EN 0x04
#define AMC6821_CONF2_RTFIE 0x08
#define AMC6821_CONF2_LTOIE 0x10
#define AMC6821_CONF2_RTOIE 0x20
#define AMC6821_CONF2_PSVIE 0x40
#define AMC6821_CONF2_RST 0x80

#define AMC6821_CONF3_THERM_FAN_EN 0x80
#define AMC6821_CONF3_REV_MASK 0x0F

#define AMC6821_CONF4_OVREN 0x10
#define AMC6821_CONF4_TACH_FAST 0x20
#define AMC6821_CONF4_PSPR 0x40
#define AMC6821_CONF4_MODE 0x80

#define AMC6821_STAT1_RPM_ALARM 0x01
#define AMC6821_STAT1_FANS 0x02
#define AMC6821_STAT1_RTH 0x04
#define AMC6821_STAT1_RTL 0x08
#define AMC6821_STAT1_R_THERM 0x10
#define AMC6821_STAT1_RTF 0x20
#define AMC6821_STAT1_LTH 0x40
#define AMC6821_STAT1_LTL 0x80

#define AMC6821_STAT2_RTC 0x08
#define AMC6821_STAT2_LTC 0x10
#define AMC6821_STAT2_LPSV 0x20
#define AMC6821_STAT2_L_THERM 0x40
#define AMC6821_STAT2_THERM_IN 0x80

enum {IDX_TEMP1_INPUT = 0, IDX_TEMP1_MIN, IDX_TEMP1_MAX,
	IDX_TEMP1_CRIT, IDX_TEMP2_INPUT, IDX_TEMP2_MIN,
	IDX_TEMP2_MAX, IDX_TEMP2_CRIT,
	TEMP_IDX_LEN, };

static const u8 temp_reg[] = {AMC6821_REG_LTEMP_HI,
			AMC6821_REG_LTEMP_LIMIT_MIN,
			AMC6821_REG_LTEMP_LIMIT_MAX,
			AMC6821_REG_LTEMP_CRIT,
			AMC6821_REG_RTEMP_HI,
			AMC6821_REG_RTEMP_LIMIT_MIN,
			AMC6821_REG_RTEMP_LIMIT_MAX,
			AMC6821_REG_RTEMP_CRIT, };

enum {IDX_FAN1_INPUT = 0, IDX_FAN1_MIN, IDX_FAN1_MAX,
	FAN1_IDX_LEN, };

static const u8 fan_reg_low[] = {AMC6821_REG_TDATA_LOW,
			AMC6821_REG_TACH_LLIMITL,
			AMC6821_REG_TACH_HLIMITL, };


static const u8 fan_reg_hi[] = {AMC6821_REG_TDATA_HI,
			AMC6821_REG_TACH_LLIMITH,
			AMC6821_REG_TACH_HLIMITH, };

static int amc6821_probe(
		struct i2c_client *client,
		const struct i2c_device_id *id);
static int amc6821_detect(
		struct i2c_client *client,
		struct i2c_board_info *info);
static int amc6821_init_client(struct i2c_client *client);
static int amc6821_remove(struct i2c_client *client);
static struct amc6821_data *amc6821_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id amc6821_id[] = {
	{ "amc6821", amc6821 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, amc6821_id);

static struct i2c_driver amc6821_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name	= "amc6821",
	},
	.probe = amc6821_probe,
	.remove = amc6821_remove,
	.id_table = amc6821_id,
	.detect = amc6821_detect,
	.address_list = normal_i2c,
};


/*
 * Client data (each client gets its own)
 */

struct amc6821_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* register values */
	int temp[TEMP_IDX_LEN];

	u16 fan[FAN1_IDX_LEN];
	u8 fan1_div;

	u8 pwm1;
	u8 temp1_auto_point_temp[3];
	u8 temp2_auto_point_temp[3];
	u8 pwm1_auto_point_pwm[3];
	u8 pwm1_enable;
	u8 pwm1_auto_channels_temp;

	u8 stat1;
	u8 stat2;
};


static ssize_t get_temp(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	struct amc6821_data *data = amc6821_update_device(dev);
	int ix = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%d\n", data->temp[ix] * 1000);
}



static ssize_t set_temp(
		struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct amc6821_data *data = i2c_get_clientdata(client);
	int ix = to_sensor_dev_attr(attr)->index;
	long val;

	int ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;
	val = clamp_val(val / 1000, -128, 127);

	mutex_lock(&data->update_lock);
	data->temp[ix] = val;
	if (i2c_smbus_write_byte_data(client, temp_reg[ix], data->temp[ix])) {
		dev_err(&client->dev, "Register write error, aborting.\n");
		count = -EIO;
	}
	mutex_unlock(&data->update_lock);
	return count;
}




static ssize_t get_temp_alarm(
	struct device *dev,
	struct device_attribute *devattr,
	char *buf)
{
	struct amc6821_data *data = amc6821_update_device(dev);
	int ix = to_sensor_dev_attr(devattr)->index;
	u8 flag;

	switch (ix) {
	case IDX_TEMP1_MIN:
		flag = data->stat1 & AMC6821_STAT1_LTL;
		break;
	case IDX_TEMP1_MAX:
		flag = data->stat1 & AMC6821_STAT1_LTH;
		break;
	case IDX_TEMP1_CRIT:
		flag = data->stat2 & AMC6821_STAT2_LTC;
		break;
	case IDX_TEMP2_MIN:
		flag = data->stat1 & AMC6821_STAT1_RTL;
		break;
	case IDX_TEMP2_MAX:
		flag = data->stat1 & AMC6821_STAT1_RTH;
		break;
	case IDX_TEMP2_CRIT:
		flag = data->stat2 & AMC6821_STAT2_RTC;
		break;
	default:
		dev_dbg(dev, "Unknown attr->index (%d).\n", ix);
		return -EINVAL;
	}
	if (flag)
		return sprintf(buf, "1");
	else
		return sprintf(buf, "0");
}




static ssize_t get_temp2_fault(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	struct amc6821_data *data = amc6821_update_device(dev);
	if (data->stat1 & AMC6821_STAT1_RTF)
		return sprintf(buf, "1");
	else
		return sprintf(buf, "0");
}

static ssize_t get_pwm1(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	struct amc6821_data *data = amc6821_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm1);
}

static ssize_t set_pwm1(
		struct device *dev,
		struct device_attribute *devattr,
		const char *buf,
		size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct amc6821_data *data = i2c_get_clientdata(client);
	long val;
	int ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);
	data->pwm1 = clamp_val(val , 0, 255);
	i2c_smbus_write_byte_data(client, AMC6821_REG_DCY, data->pwm1);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t get_pwm1_enable(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	struct amc6821_data *data = amc6821_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm1_enable);
}

static ssize_t set_pwm1_enable(
		struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct amc6821_data *data = i2c_get_clientdata(client);
	long val;
	int config = kstrtol(buf, 10, &val);
	if (config)
		return config;

	config = i2c_smbus_read_byte_data(client, AMC6821_REG_CONF1);
	if (config < 0) {
			dev_err(&client->dev,
			"Error reading configuration register, aborting.\n");
			return config;
	}

	switch (val) {
	case 1:
		config &= ~AMC6821_CONF1_FDRC0;
		config &= ~AMC6821_CONF1_FDRC1;
		break;
	case 2:
		config &= ~AMC6821_CONF1_FDRC0;
		config |= AMC6821_CONF1_FDRC1;
		break;
	case 3:
		config |= AMC6821_CONF1_FDRC0;
		config |= AMC6821_CONF1_FDRC1;
		break;
	default:
		return -EINVAL;
	}
	mutex_lock(&data->update_lock);
	if (i2c_smbus_write_byte_data(client, AMC6821_REG_CONF1, config)) {
			dev_err(&client->dev,
			"Configuration register write error, aborting.\n");
			count = -EIO;
	}
	mutex_unlock(&data->update_lock);
	return count;
}


static ssize_t get_pwm1_auto_channels_temp(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	struct amc6821_data *data = amc6821_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm1_auto_channels_temp);
}


static ssize_t get_temp_auto_point_temp(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	int ix = to_sensor_dev_attr_2(devattr)->index;
	int nr = to_sensor_dev_attr_2(devattr)->nr;
	struct amc6821_data *data = amc6821_update_device(dev);
	switch (nr) {
	case 1:
		return sprintf(buf, "%d\n",
			data->temp1_auto_point_temp[ix] * 1000);
		break;
	case 2:
		return sprintf(buf, "%d\n",
			data->temp2_auto_point_temp[ix] * 1000);
		break;
	default:
		dev_dbg(dev, "Unknown attr->nr (%d).\n", nr);
		return -EINVAL;
	}
}


static ssize_t get_pwm1_auto_point_pwm(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	int ix = to_sensor_dev_attr(devattr)->index;
	struct amc6821_data *data = amc6821_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm1_auto_point_pwm[ix]);
}


static inline ssize_t set_slope_register(struct i2c_client *client,
		u8 reg,
		u8 dpwm,
		u8 *ptemp)
{
	int dt;
	u8 tmp;

	dt = ptemp[2]-ptemp[1];
	for (tmp = 4; tmp > 0; tmp--) {
		if (dt * (0x20 >> tmp) >= dpwm)
			break;
	}
	tmp |= (ptemp[1] & 0x7C) << 1;
	if (i2c_smbus_write_byte_data(client,
			reg, tmp)) {
		dev_err(&client->dev, "Register write error, aborting.\n");
		return -EIO;
	}
	return 0;
}



static ssize_t set_temp_auto_point_temp(
		struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct amc6821_data *data = amc6821_update_device(dev);
	int ix = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;
	u8 *ptemp;
	u8 reg;
	int dpwm;
	long val;
	int ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	switch (nr) {
	case 1:
		ptemp = data->temp1_auto_point_temp;
		reg = AMC6821_REG_LTEMP_FAN_CTRL;
		break;
	case 2:
		ptemp = data->temp2_auto_point_temp;
		reg = AMC6821_REG_RTEMP_FAN_CTRL;
		break;
	default:
		dev_dbg(dev, "Unknown attr->nr (%d).\n", nr);
		return -EINVAL;
	}

	data->valid = 0;
	mutex_lock(&data->update_lock);
	switch (ix) {
	case 0:
		ptemp[0] = clamp_val(val / 1000, 0,
				     data->temp1_auto_point_temp[1]);
		ptemp[0] = clamp_val(ptemp[0], 0,
				     data->temp2_auto_point_temp[1]);
		ptemp[0] = clamp_val(ptemp[0], 0, 63);
		if (i2c_smbus_write_byte_data(
					client,
					AMC6821_REG_PSV_TEMP,
					ptemp[0])) {
				dev_err(&client->dev,
					"Register write error, aborting.\n");
				count = -EIO;
		}
		goto EXIT;
		break;
	case 1:
		ptemp[1] = clamp_val(val / 1000, (ptemp[0] & 0x7C) + 4, 124);
		ptemp[1] &= 0x7C;
		ptemp[2] = clamp_val(ptemp[2], ptemp[1] + 1, 255);
		break;
	case 2:
		ptemp[2] = clamp_val(val / 1000, ptemp[1]+1, 255);
		break;
	default:
		dev_dbg(dev, "Unknown attr->index (%d).\n", ix);
		count = -EINVAL;
		goto EXIT;
	}
	dpwm = data->pwm1_auto_point_pwm[2] - data->pwm1_auto_point_pwm[1];
	if (set_slope_register(client, reg, dpwm, ptemp))
		count = -EIO;

EXIT:
	mutex_unlock(&data->update_lock);
	return count;
}



static ssize_t set_pwm1_auto_point_pwm(
		struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct amc6821_data *data = i2c_get_clientdata(client);
	int dpwm;
	long val;
	int ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);
	data->pwm1_auto_point_pwm[1] = clamp_val(val, 0, 254);
	if (i2c_smbus_write_byte_data(client, AMC6821_REG_DCY_LOW_TEMP,
			data->pwm1_auto_point_pwm[1])) {
		dev_err(&client->dev, "Register write error, aborting.\n");
		count = -EIO;
		goto EXIT;
	}
	dpwm = data->pwm1_auto_point_pwm[2] - data->pwm1_auto_point_pwm[1];
	if (set_slope_register(client, AMC6821_REG_LTEMP_FAN_CTRL, dpwm,
			data->temp1_auto_point_temp)) {
		count = -EIO;
		goto EXIT;
	}
	if (set_slope_register(client, AMC6821_REG_RTEMP_FAN_CTRL, dpwm,
			data->temp2_auto_point_temp)) {
		count = -EIO;
		goto EXIT;
	}

EXIT:
	data->valid = 0;
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t get_fan(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	struct amc6821_data *data = amc6821_update_device(dev);
	int ix = to_sensor_dev_attr(devattr)->index;
	if (0 == data->fan[ix])
		return sprintf(buf, "0");
	return sprintf(buf, "%d\n", (int)(6000000 / data->fan[ix]));
}



static ssize_t get_fan1_fault(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	struct amc6821_data *data = amc6821_update_device(dev);
	if (data->stat1 & AMC6821_STAT1_FANS)
		return sprintf(buf, "1");
	else
		return sprintf(buf, "0");
}



static ssize_t set_fan(
		struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct amc6821_data *data = i2c_get_clientdata(client);
	long val;
	int ix = to_sensor_dev_attr(attr)->index;
	int ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;
	val = 1 > val ? 0xFFFF : 6000000/val;

	mutex_lock(&data->update_lock);
	data->fan[ix] = (u16) clamp_val(val, 1, 0xFFFF);
	if (i2c_smbus_write_byte_data(client, fan_reg_low[ix],
			data->fan[ix] & 0xFF)) {
		dev_err(&client->dev, "Register write error, aborting.\n");
		count = -EIO;
		goto EXIT;
	}
	if (i2c_smbus_write_byte_data(client,
			fan_reg_hi[ix], data->fan[ix] >> 8)) {
		dev_err(&client->dev, "Register write error, aborting.\n");
		count = -EIO;
	}
EXIT:
	mutex_unlock(&data->update_lock);
	return count;
}



static ssize_t get_fan1_div(
		struct device *dev,
		struct device_attribute *devattr,
		char *buf)
{
	struct amc6821_data *data = amc6821_update_device(dev);
	return sprintf(buf, "%d\n", data->fan1_div);
}

static ssize_t set_fan1_div(
		struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct amc6821_data *data = i2c_get_clientdata(client);
	long val;
	int config = kstrtol(buf, 10, &val);
	if (config)
		return config;

	config = i2c_smbus_read_byte_data(client, AMC6821_REG_CONF4);
	if (config < 0) {
		dev_err(&client->dev,
			"Error reading configuration register, aborting.\n");
		return config;
	}
	mutex_lock(&data->update_lock);
	switch (val) {
	case 2:
		config &= ~AMC6821_CONF4_PSPR;
		data->fan1_div = 2;
		break;
	case 4:
		config |= AMC6821_CONF4_PSPR;
		data->fan1_div = 4;
		break;
	default:
		count = -EINVAL;
		goto EXIT;
	}
	if (i2c_smbus_write_byte_data(client, AMC6821_REG_CONF4, config)) {
		dev_err(&client->dev,
			"Configuration register write error, aborting.\n");
		count = -EIO;
	}
EXIT:
	mutex_unlock(&data->update_lock);
	return count;
}



static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO,
	get_temp, NULL, IDX_TEMP1_INPUT);
static SENSOR_DEVICE_ATTR(temp1_min, S_IRUGO | S_IWUSR, get_temp,
	set_temp, IDX_TEMP1_MIN);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO | S_IWUSR, get_temp,
	set_temp, IDX_TEMP1_MAX);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO | S_IWUSR, get_temp,
	set_temp, IDX_TEMP1_CRIT);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO,
	get_temp_alarm, NULL, IDX_TEMP1_MIN);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO,
	get_temp_alarm, NULL, IDX_TEMP1_MAX);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO,
	get_temp_alarm, NULL, IDX_TEMP1_CRIT);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO | S_IWUSR,
	get_temp, NULL, IDX_TEMP2_INPUT);
static SENSOR_DEVICE_ATTR(temp2_min, S_IRUGO | S_IWUSR, get_temp,
	set_temp, IDX_TEMP2_MIN);
static SENSOR_DEVICE_ATTR(temp2_max, S_IRUGO | S_IWUSR, get_temp,
	set_temp, IDX_TEMP2_MAX);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IRUGO | S_IWUSR, get_temp,
	set_temp, IDX_TEMP2_CRIT);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO,
	get_temp2_fault, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO,
	get_temp_alarm, NULL, IDX_TEMP2_MIN);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO,
	get_temp_alarm, NULL, IDX_TEMP2_MAX);
static SENSOR_DEVICE_ATTR(temp2_crit_alarm, S_IRUGO,
	get_temp_alarm, NULL, IDX_TEMP2_CRIT);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, get_fan, NULL, IDX_FAN1_INPUT);
static SENSOR_DEVICE_ATTR(fan1_min, S_IRUGO | S_IWUSR,
	get_fan, set_fan, IDX_FAN1_MIN);
static SENSOR_DEVICE_ATTR(fan1_max, S_IRUGO | S_IWUSR,
	get_fan, set_fan, IDX_FAN1_MAX);
static SENSOR_DEVICE_ATTR(fan1_fault, S_IRUGO, get_fan1_fault, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_div, S_IRUGO | S_IWUSR,
	get_fan1_div, set_fan1_div, 0);

static SENSOR_DEVICE_ATTR(pwm1, S_IWUSR | S_IRUGO, get_pwm1, set_pwm1, 0);
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IWUSR | S_IRUGO,
	get_pwm1_enable, set_pwm1_enable, 0);
static SENSOR_DEVICE_ATTR(pwm1_auto_point1_pwm, S_IRUGO,
	get_pwm1_auto_point_pwm, NULL, 0);
static SENSOR_DEVICE_ATTR(pwm1_auto_point2_pwm, S_IWUSR | S_IRUGO,
	get_pwm1_auto_point_pwm, set_pwm1_auto_point_pwm, 1);
static SENSOR_DEVICE_ATTR(pwm1_auto_point3_pwm, S_IRUGO,
	get_pwm1_auto_point_pwm, NULL, 2);
static SENSOR_DEVICE_ATTR(pwm1_auto_channels_temp, S_IRUGO,
	get_pwm1_auto_channels_temp, NULL, 0);
static SENSOR_DEVICE_ATTR_2(temp1_auto_point1_temp, S_IRUGO,
	get_temp_auto_point_temp, NULL, 1, 0);
static SENSOR_DEVICE_ATTR_2(temp1_auto_point2_temp, S_IWUSR | S_IRUGO,
	get_temp_auto_point_temp, set_temp_auto_point_temp, 1, 1);
static SENSOR_DEVICE_ATTR_2(temp1_auto_point3_temp, S_IWUSR | S_IRUGO,
	get_temp_auto_point_temp, set_temp_auto_point_temp, 1, 2);

static SENSOR_DEVICE_ATTR_2(temp2_auto_point1_temp, S_IWUSR | S_IRUGO,
	get_temp_auto_point_temp, set_temp_auto_point_temp, 2, 0);
static SENSOR_DEVICE_ATTR_2(temp2_auto_point2_temp, S_IWUSR | S_IRUGO,
	get_temp_auto_point_temp, set_temp_auto_point_temp, 2, 1);
static SENSOR_DEVICE_ATTR_2(temp2_auto_point3_temp, S_IWUSR | S_IRUGO,
	get_temp_auto_point_temp, set_temp_auto_point_temp, 2, 2);



static struct attribute *amc6821_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_max.dev_attr.attr,
	&sensor_dev_attr_fan1_fault.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_channels_temp.dev_attr.attr,
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

static struct attribute_group amc6821_attr_grp = {
	.attrs = amc6821_attrs,
};



/* Return 0 if detection is successful, -ENODEV otherwise */
static int amc6821_detect(
		struct i2c_client *client,
		struct i2c_board_info *info)
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
	strlcpy(info->type, "amc6821", I2C_NAME_SIZE);

	return 0;
}

static int amc6821_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct amc6821_data *data;
	int err;

	data = devm_kzalloc(&client->dev, sizeof(struct amc6821_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/*
	 * Initialize the amc6821 chip
	 */
	err = amc6821_init_client(client);
	if (err)
		return err;

	err = sysfs_create_group(&client->dev.kobj, &amc6821_attr_grp);
	if (err)
		return err;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (!IS_ERR(data->hwmon_dev))
		return 0;

	err = PTR_ERR(data->hwmon_dev);
	dev_err(&client->dev, "error registering hwmon device.\n");
	sysfs_remove_group(&client->dev.kobj, &amc6821_attr_grp);
	return err;
}

static int amc6821_remove(struct i2c_client *client)
{
	struct amc6821_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &amc6821_attr_grp);

	return 0;
}


static int amc6821_init_client(struct i2c_client *client)
{
	int config;
	int err = -EIO;

	if (init) {
		config = i2c_smbus_read_byte_data(client, AMC6821_REG_CONF4);

		if (config < 0) {
				dev_err(&client->dev,
			"Error reading configuration register, aborting.\n");
				return err;
		}

		config |= AMC6821_CONF4_MODE;

		if (i2c_smbus_write_byte_data(client, AMC6821_REG_CONF4,
				config)) {
			dev_err(&client->dev,
			"Configuration register write error, aborting.\n");
			return err;
		}

		config = i2c_smbus_read_byte_data(client, AMC6821_REG_CONF3);

		if (config < 0) {
			dev_err(&client->dev,
			"Error reading configuration register, aborting.\n");
			return err;
		}

		dev_info(&client->dev, "Revision %d\n", config & 0x0f);

		config &= ~AMC6821_CONF3_THERM_FAN_EN;

		if (i2c_smbus_write_byte_data(client, AMC6821_REG_CONF3,
				config)) {
			dev_err(&client->dev,
			"Configuration register write error, aborting.\n");
			return err;
		}

		config = i2c_smbus_read_byte_data(client, AMC6821_REG_CONF2);

		if (config < 0) {
			dev_err(&client->dev,
			"Error reading configuration register, aborting.\n");
			return err;
		}

		config &= ~AMC6821_CONF2_RTFIE;
		config &= ~AMC6821_CONF2_LTOIE;
		config &= ~AMC6821_CONF2_RTOIE;
		if (i2c_smbus_write_byte_data(client,
				AMC6821_REG_CONF2, config)) {
			dev_err(&client->dev,
			"Configuration register write error, aborting.\n");
			return err;
		}

		config = i2c_smbus_read_byte_data(client, AMC6821_REG_CONF1);

		if (config < 0) {
			dev_err(&client->dev,
			"Error reading configuration register, aborting.\n");
			return err;
		}

		config &= ~AMC6821_CONF1_THERMOVIE;
		config &= ~AMC6821_CONF1_FANIE;
		config |= AMC6821_CONF1_START;
		if (pwminv)
			config |= AMC6821_CONF1_PWMINV;
		else
			config &= ~AMC6821_CONF1_PWMINV;

		if (i2c_smbus_write_byte_data(
				client, AMC6821_REG_CONF1, config)) {
			dev_err(&client->dev,
			"Configuration register write error, aborting.\n");
			return err;
		}
	}
	return 0;
}


static struct amc6821_data *amc6821_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct amc6821_data *data = i2c_get_clientdata(client);
	int timeout = HZ;
	u8 reg;
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + timeout) ||
			!data->valid) {

		for (i = 0; i < TEMP_IDX_LEN; i++)
			data->temp[i] = i2c_smbus_read_byte_data(client,
				temp_reg[i]);

		data->stat1 = i2c_smbus_read_byte_data(client,
			AMC6821_REG_STAT1);
		data->stat2 = i2c_smbus_read_byte_data(client,
			AMC6821_REG_STAT2);

		data->pwm1 = i2c_smbus_read_byte_data(client,
			AMC6821_REG_DCY);
		for (i = 0; i < FAN1_IDX_LEN; i++) {
			data->fan[i] = i2c_smbus_read_byte_data(
					client,
					fan_reg_low[i]);
			data->fan[i] += i2c_smbus_read_byte_data(
					client,
					fan_reg_hi[i]) << 8;
		}
		data->fan1_div = i2c_smbus_read_byte_data(client,
			AMC6821_REG_CONF4);
		data->fan1_div = data->fan1_div & AMC6821_CONF4_PSPR ? 4 : 2;

		data->pwm1_auto_point_pwm[0] = 0;
		data->pwm1_auto_point_pwm[2] = 255;
		data->pwm1_auto_point_pwm[1] = i2c_smbus_read_byte_data(client,
			AMC6821_REG_DCY_LOW_TEMP);

		data->temp1_auto_point_temp[0] =
			i2c_smbus_read_byte_data(client,
					AMC6821_REG_PSV_TEMP);
		data->temp2_auto_point_temp[0] =
				data->temp1_auto_point_temp[0];
		reg = i2c_smbus_read_byte_data(client,
			AMC6821_REG_LTEMP_FAN_CTRL);
		data->temp1_auto_point_temp[1] = (reg & 0xF8) >> 1;
		reg &= 0x07;
		reg = 0x20 >> reg;
		if (reg > 0)
			data->temp1_auto_point_temp[2] =
				data->temp1_auto_point_temp[1] +
				(data->pwm1_auto_point_pwm[2] -
				data->pwm1_auto_point_pwm[1]) / reg;
		else
			data->temp1_auto_point_temp[2] = 255;

		reg = i2c_smbus_read_byte_data(client,
			AMC6821_REG_RTEMP_FAN_CTRL);
		data->temp2_auto_point_temp[1] = (reg & 0xF8) >> 1;
		reg &= 0x07;
		reg = 0x20 >> reg;
		if (reg > 0)
			data->temp2_auto_point_temp[2] =
				data->temp2_auto_point_temp[1] +
				(data->pwm1_auto_point_pwm[2] -
				data->pwm1_auto_point_pwm[1]) / reg;
		else
			data->temp2_auto_point_temp[2] = 255;

		reg = i2c_smbus_read_byte_data(client, AMC6821_REG_CONF1);
		reg = (reg >> 5) & 0x3;
		switch (reg) {
		case 0: /*open loop: software sets pwm1*/
			data->pwm1_auto_channels_temp = 0;
			data->pwm1_enable = 1;
			break;
		case 2: /*closed loop: remote T (temp2)*/
			data->pwm1_auto_channels_temp = 2;
			data->pwm1_enable = 2;
			break;
		case 3: /*closed loop: local and remote T (temp2)*/
			data->pwm1_auto_channels_temp = 3;
			data->pwm1_enable = 3;
			break;
		case 1: /*
			 * semi-open loop: software sets rpm, chip controls
			 * pwm1, currently not implemented
			 */
			data->pwm1_auto_channels_temp = 0;
			data->pwm1_enable = 0;
			break;
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}
	mutex_unlock(&data->update_lock);
	return data;
}

module_i2c_driver(amc6821_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("T. Mertelj <tomaz.mertelj@guest.arnes.si>");
MODULE_DESCRIPTION("Texas Instruments amc6821 hwmon driver");
