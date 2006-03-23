/*
    asb100.c - Part of lm_sensors, Linux kernel modules for hardware
	        monitoring

    Copyright (C) 2004 Mark M. Hoffman <mhoffman@lightlink.com>

	(derived from w83781d.c)

    Copyright (C) 1998 - 2003  Frodo Looijaard <frodol@dds.nl>,
    Philip Edelbrock <phil@netroedge.com>, and
    Mark Studebaker <mdsxyz123@yahoo.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
    This driver supports the hardware sensor chips: Asus ASB100 and
    ASB100-A "BACH".

    ASB100-A supports pwm1, while plain ASB100 does not.  There is no known
    way for the driver to tell which one is there.

    Chip	#vin	#fanin	#pwm	#temp	wchipid	vendid	i2c	ISA
    asb100	7	3	1	4	0x31	0x0694	yes	no
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include "lm75.h"

/*
	HISTORY:
	2003-12-29	1.0.0	Ported from lm_sensors project for kernel 2.6
*/
#define ASB100_VERSION "1.0.0"

/* I2C addresses to scan */
static unsigned short normal_i2c[] = { 0x2d, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(asb100);
I2C_CLIENT_MODULE_PARM(force_subclients, "List of subclient addresses: "
	"{bus, clientaddr, subclientaddr1, subclientaddr2}");

/* Voltage IN registers 0-6 */
#define ASB100_REG_IN(nr)	(0x20 + (nr))
#define ASB100_REG_IN_MAX(nr)	(0x2b + (nr * 2))
#define ASB100_REG_IN_MIN(nr)	(0x2c + (nr * 2))

/* FAN IN registers 1-3 */
#define ASB100_REG_FAN(nr)	(0x28 + (nr))
#define ASB100_REG_FAN_MIN(nr)	(0x3b + (nr))

/* TEMPERATURE registers 1-4 */
static const u16 asb100_reg_temp[]	= {0, 0x27, 0x150, 0x250, 0x17};
static const u16 asb100_reg_temp_max[]	= {0, 0x39, 0x155, 0x255, 0x18};
static const u16 asb100_reg_temp_hyst[]	= {0, 0x3a, 0x153, 0x253, 0x19};

#define ASB100_REG_TEMP(nr) (asb100_reg_temp[nr])
#define ASB100_REG_TEMP_MAX(nr) (asb100_reg_temp_max[nr])
#define ASB100_REG_TEMP_HYST(nr) (asb100_reg_temp_hyst[nr])

#define ASB100_REG_TEMP2_CONFIG	0x0152
#define ASB100_REG_TEMP3_CONFIG	0x0252


#define ASB100_REG_CONFIG	0x40
#define ASB100_REG_ALARM1	0x41
#define ASB100_REG_ALARM2	0x42
#define ASB100_REG_SMIM1	0x43
#define ASB100_REG_SMIM2	0x44
#define ASB100_REG_VID_FANDIV	0x47
#define ASB100_REG_I2C_ADDR	0x48
#define ASB100_REG_CHIPID	0x49
#define ASB100_REG_I2C_SUBADDR	0x4a
#define ASB100_REG_PIN		0x4b
#define ASB100_REG_IRQ		0x4c
#define ASB100_REG_BANK		0x4e
#define ASB100_REG_CHIPMAN	0x4f

#define ASB100_REG_WCHIPID	0x58

/* bit 7 -> enable, bits 0-3 -> duty cycle */
#define ASB100_REG_PWM1		0x59

/* CONVERSIONS
   Rounding and limit checking is only done on the TO_REG variants. */

/* These constants are a guess, consistent w/ w83781d */
#define ASB100_IN_MIN (   0)
#define ASB100_IN_MAX (4080)

/* IN: 1/1000 V (0V to 4.08V)
   REG: 16mV/bit */
static u8 IN_TO_REG(unsigned val)
{
	unsigned nval = SENSORS_LIMIT(val, ASB100_IN_MIN, ASB100_IN_MAX);
	return (nval + 8) / 16;
}

static unsigned IN_FROM_REG(u8 reg)
{
	return reg * 16;
}

static u8 FAN_TO_REG(long rpm, int div)
{
	if (rpm == -1)
		return 0;
	if (rpm == 0)
		return 255;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1350000 + rpm * div / 2) / (rpm * div), 1, 254);
}

static int FAN_FROM_REG(u8 val, int div)
{
	return val==0 ? -1 : val==255 ? 0 : 1350000/(val*div);
}

/* These constants are a guess, consistent w/ w83781d */
#define ASB100_TEMP_MIN (-128000)
#define ASB100_TEMP_MAX ( 127000)

/* TEMP: 0.001C/bit (-128C to +127C)
   REG: 1C/bit, two's complement */
static u8 TEMP_TO_REG(int temp)
{
	int ntemp = SENSORS_LIMIT(temp, ASB100_TEMP_MIN, ASB100_TEMP_MAX);
	ntemp += (ntemp<0 ? -500 : 500);
	return (u8)(ntemp / 1000);
}

static int TEMP_FROM_REG(u8 reg)
{
	return (s8)reg * 1000;
}

/* PWM: 0 - 255 per sensors documentation
   REG: (6.25% duty cycle per bit) */
static u8 ASB100_PWM_TO_REG(int pwm)
{
	pwm = SENSORS_LIMIT(pwm, 0, 255);
	return (u8)(pwm / 16);
}

static int ASB100_PWM_FROM_REG(u8 reg)
{
	return reg * 16;
}

#define DIV_FROM_REG(val) (1 << (val))

/* FAN DIV: 1, 2, 4, or 8 (defaults to 2)
   REG: 0, 1, 2, or 3 (respectively) (defaults to 1) */
static u8 DIV_TO_REG(long val)
{
	return val==8 ? 3 : val==4 ? 2 : val==1 ? 0 : 1;
}

/* For each registered client, we need to keep some data in memory. That
   data is pointed to by client->data. The structure itself is
   dynamically allocated, at the same time the client itself is allocated. */
struct asb100_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct mutex lock;
	enum chips type;

	struct mutex update_lock;
	unsigned long last_updated;	/* In jiffies */

	/* array of 2 pointers to subclients */
	struct i2c_client *lm75[2];

	char valid;		/* !=0 if following fields are valid */
	u8 in[7];		/* Register value */
	u8 in_max[7];		/* Register value */
	u8 in_min[7];		/* Register value */
	u8 fan[3];		/* Register value */
	u8 fan_min[3];		/* Register value */
	u16 temp[4];		/* Register value (0 and 3 are u8 only) */
	u16 temp_max[4];	/* Register value (0 and 3 are u8 only) */
	u16 temp_hyst[4];	/* Register value (0 and 3 are u8 only) */
	u8 fan_div[3];		/* Register encoding, right justified */
	u8 pwm;			/* Register encoding */
	u8 vid;			/* Register encoding, combined */
	u32 alarms;		/* Register encoding, combined */
	u8 vrm;
};

static int asb100_read_value(struct i2c_client *client, u16 reg);
static void asb100_write_value(struct i2c_client *client, u16 reg, u16 val);

static int asb100_attach_adapter(struct i2c_adapter *adapter);
static int asb100_detect(struct i2c_adapter *adapter, int address, int kind);
static int asb100_detach_client(struct i2c_client *client);
static struct asb100_data *asb100_update_device(struct device *dev);
static void asb100_init_client(struct i2c_client *client);

static struct i2c_driver asb100_driver = {
	.driver = {
		.name	= "asb100",
	},
	.id		= I2C_DRIVERID_ASB100,
	.attach_adapter	= asb100_attach_adapter,
	.detach_client	= asb100_detach_client,
};

/* 7 Voltages */
#define show_in_reg(reg) \
static ssize_t show_##reg (struct device *dev, char *buf, int nr) \
{ \
	struct asb100_data *data = asb100_update_device(dev); \
	return sprintf(buf, "%d\n", IN_FROM_REG(data->reg[nr])); \
}

show_in_reg(in)
show_in_reg(in_min)
show_in_reg(in_max)

#define set_in_reg(REG, reg) \
static ssize_t set_in_##reg(struct device *dev, const char *buf, \
		size_t count, int nr) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct asb100_data *data = i2c_get_clientdata(client); \
	unsigned long val = simple_strtoul(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->in_##reg[nr] = IN_TO_REG(val); \
	asb100_write_value(client, ASB100_REG_IN_##REG(nr), \
		data->in_##reg[nr]); \
	mutex_unlock(&data->update_lock); \
	return count; \
}

set_in_reg(MIN, min)
set_in_reg(MAX, max)

#define sysfs_in(offset) \
static ssize_t \
	show_in##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_in(dev, buf, offset); \
} \
static DEVICE_ATTR(in##offset##_input, S_IRUGO, \
		show_in##offset, NULL); \
static ssize_t \
	show_in##offset##_min (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_in_min(dev, buf, offset); \
} \
static ssize_t \
	show_in##offset##_max (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_in_max(dev, buf, offset); \
} \
static ssize_t set_in##offset##_min (struct device *dev, struct device_attribute *attr, \
		const char *buf, size_t count) \
{ \
	return set_in_min(dev, buf, count, offset); \
} \
static ssize_t set_in##offset##_max (struct device *dev, struct device_attribute *attr, \
		const char *buf, size_t count) \
{ \
	return set_in_max(dev, buf, count, offset); \
} \
static DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR, \
		show_in##offset##_min, set_in##offset##_min); \
static DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR, \
		show_in##offset##_max, set_in##offset##_max);

sysfs_in(0);
sysfs_in(1);
sysfs_in(2);
sysfs_in(3);
sysfs_in(4);
sysfs_in(5);
sysfs_in(6);

#define device_create_file_in(client, offset) do { \
	device_create_file(&client->dev, &dev_attr_in##offset##_input); \
	device_create_file(&client->dev, &dev_attr_in##offset##_min); \
	device_create_file(&client->dev, &dev_attr_in##offset##_max); \
} while (0)

/* 3 Fans */
static ssize_t show_fan(struct device *dev, char *buf, int nr)
{
	struct asb100_data *data = asb100_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[nr],
		DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t show_fan_min(struct device *dev, char *buf, int nr)
{
	struct asb100_data *data = asb100_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[nr],
		DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t show_fan_div(struct device *dev, char *buf, int nr)
{
	struct asb100_data *data = asb100_update_device(dev);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[nr]));
}

static ssize_t set_fan_min(struct device *dev, const char *buf,
				size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct asb100_data *data = i2c_get_clientdata(client);
	u32 val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	asb100_write_value(client, ASB100_REG_FAN_MIN(nr), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

/* Note: we save and restore the fan minimum here, because its value is
   determined in part by the fan divisor.  This follows the principle of
   least suprise; the user doesn't expect the fan minimum to change just
   because the divisor changed. */
static ssize_t set_fan_div(struct device *dev, const char *buf,
				size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct asb100_data *data = i2c_get_clientdata(client);
	unsigned long min;
	unsigned long val = simple_strtoul(buf, NULL, 10);
	int reg;
	
	mutex_lock(&data->update_lock);

	min = FAN_FROM_REG(data->fan_min[nr],
			DIV_FROM_REG(data->fan_div[nr]));
	data->fan_div[nr] = DIV_TO_REG(val);

	switch(nr) {
	case 0:	/* fan 1 */
		reg = asb100_read_value(client, ASB100_REG_VID_FANDIV);
		reg = (reg & 0xcf) | (data->fan_div[0] << 4);
		asb100_write_value(client, ASB100_REG_VID_FANDIV, reg);
		break;

	case 1:	/* fan 2 */
		reg = asb100_read_value(client, ASB100_REG_VID_FANDIV);
		reg = (reg & 0x3f) | (data->fan_div[1] << 6);
		asb100_write_value(client, ASB100_REG_VID_FANDIV, reg);
		break;

	case 2:	/* fan 3 */
		reg = asb100_read_value(client, ASB100_REG_PIN);
		reg = (reg & 0x3f) | (data->fan_div[2] << 6);
		asb100_write_value(client, ASB100_REG_PIN, reg);
		break;
	}

	data->fan_min[nr] =
		FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	asb100_write_value(client, ASB100_REG_FAN_MIN(nr), data->fan_min[nr]);

	mutex_unlock(&data->update_lock);

	return count;
}

#define sysfs_fan(offset) \
static ssize_t show_fan##offset(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_fan(dev, buf, offset - 1); \
} \
static ssize_t show_fan##offset##_min(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_fan_min(dev, buf, offset - 1); \
} \
static ssize_t show_fan##offset##_div(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_fan_div(dev, buf, offset - 1); \
} \
static ssize_t set_fan##offset##_min(struct device *dev, struct device_attribute *attr, const char *buf, \
					size_t count) \
{ \
	return set_fan_min(dev, buf, count, offset - 1); \
} \
static ssize_t set_fan##offset##_div(struct device *dev, struct device_attribute *attr, const char *buf, \
					size_t count) \
{ \
	return set_fan_div(dev, buf, count, offset - 1); \
} \
static DEVICE_ATTR(fan##offset##_input, S_IRUGO, \
		show_fan##offset, NULL); \
static DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR, \
		show_fan##offset##_min, set_fan##offset##_min); \
static DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR, \
		show_fan##offset##_div, set_fan##offset##_div);

sysfs_fan(1);
sysfs_fan(2);
sysfs_fan(3);

#define device_create_file_fan(client, offset) do { \
	device_create_file(&client->dev, &dev_attr_fan##offset##_input); \
	device_create_file(&client->dev, &dev_attr_fan##offset##_min); \
	device_create_file(&client->dev, &dev_attr_fan##offset##_div); \
} while (0)

/* 4 Temp. Sensors */
static int sprintf_temp_from_reg(u16 reg, char *buf, int nr)
{
	int ret = 0;

	switch (nr) {
	case 1: case 2:
		ret = sprintf(buf, "%d\n", LM75_TEMP_FROM_REG(reg));
		break;
	case 0: case 3: default:
		ret = sprintf(buf, "%d\n", TEMP_FROM_REG(reg));
		break;
	}
	return ret;
}
		 	
#define show_temp_reg(reg) \
static ssize_t show_##reg(struct device *dev, char *buf, int nr) \
{ \
	struct asb100_data *data = asb100_update_device(dev); \
	return sprintf_temp_from_reg(data->reg[nr], buf, nr); \
}

show_temp_reg(temp);
show_temp_reg(temp_max);
show_temp_reg(temp_hyst);

#define set_temp_reg(REG, reg) \
static ssize_t set_##reg(struct device *dev, const char *buf, \
			size_t count, int nr) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct asb100_data *data = i2c_get_clientdata(client); \
	unsigned long val = simple_strtoul(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	switch (nr) { \
	case 1: case 2: \
		data->reg[nr] = LM75_TEMP_TO_REG(val); \
		break; \
	case 0: case 3: default: \
		data->reg[nr] = TEMP_TO_REG(val); \
		break; \
	} \
	asb100_write_value(client, ASB100_REG_TEMP_##REG(nr+1), \
			data->reg[nr]); \
	mutex_unlock(&data->update_lock); \
	return count; \
}

set_temp_reg(MAX, temp_max);
set_temp_reg(HYST, temp_hyst);

#define sysfs_temp(num) \
static ssize_t show_temp##num(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_temp(dev, buf, num-1); \
} \
static DEVICE_ATTR(temp##num##_input, S_IRUGO, show_temp##num, NULL); \
static ssize_t show_temp_max##num(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_temp_max(dev, buf, num-1); \
} \
static ssize_t set_temp_max##num(struct device *dev, struct device_attribute *attr, const char *buf, \
					size_t count) \
{ \
	return set_temp_max(dev, buf, count, num-1); \
} \
static DEVICE_ATTR(temp##num##_max, S_IRUGO | S_IWUSR, \
		show_temp_max##num, set_temp_max##num); \
static ssize_t show_temp_hyst##num(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_temp_hyst(dev, buf, num-1); \
} \
static ssize_t set_temp_hyst##num(struct device *dev, struct device_attribute *attr, const char *buf, \
					size_t count) \
{ \
	return set_temp_hyst(dev, buf, count, num-1); \
} \
static DEVICE_ATTR(temp##num##_max_hyst, S_IRUGO | S_IWUSR, \
		show_temp_hyst##num, set_temp_hyst##num);

sysfs_temp(1);
sysfs_temp(2);
sysfs_temp(3);
sysfs_temp(4);

/* VID */
#define device_create_file_temp(client, num) do { \
	device_create_file(&client->dev, &dev_attr_temp##num##_input); \
	device_create_file(&client->dev, &dev_attr_temp##num##_max); \
	device_create_file(&client->dev, &dev_attr_temp##num##_max_hyst); \
} while (0)

static ssize_t show_vid(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct asb100_data *data = asb100_update_device(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}

static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);
#define device_create_file_vid(client) \
device_create_file(&client->dev, &dev_attr_cpu0_vid)

/* VRM */
static ssize_t show_vrm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct asb100_data *data = asb100_update_device(dev);
	return sprintf(buf, "%d\n", data->vrm);
}

static ssize_t set_vrm(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct asb100_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	data->vrm = val;
	return count;
}

/* Alarms */
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm, set_vrm);
#define device_create_file_vrm(client) \
device_create_file(&client->dev, &dev_attr_vrm);

static ssize_t show_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct asb100_data *data = asb100_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);
#define device_create_file_alarms(client) \
device_create_file(&client->dev, &dev_attr_alarms)

/* 1 PWM */
static ssize_t show_pwm1(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct asb100_data *data = asb100_update_device(dev);
	return sprintf(buf, "%d\n", ASB100_PWM_FROM_REG(data->pwm & 0x0f));
}

static ssize_t set_pwm1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct asb100_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->pwm &= 0x80; /* keep the enable bit */
	data->pwm |= (0x0f & ASB100_PWM_TO_REG(val));
	asb100_write_value(client, ASB100_REG_PWM1, data->pwm);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_pwm_enable1(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct asb100_data *data = asb100_update_device(dev);
	return sprintf(buf, "%d\n", (data->pwm & 0x80) ? 1 : 0);
}

static ssize_t set_pwm_enable1(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct asb100_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->pwm &= 0x0f; /* keep the duty cycle bits */
	data->pwm |= (val ? 0x80 : 0x00);
	asb100_write_value(client, ASB100_REG_PWM1, data->pwm);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, show_pwm1, set_pwm1);
static DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR,
		show_pwm_enable1, set_pwm_enable1);
#define device_create_file_pwm1(client) do { \
	device_create_file(&new_client->dev, &dev_attr_pwm1); \
	device_create_file(&new_client->dev, &dev_attr_pwm1_enable); \
} while (0)

/* This function is called when:
	asb100_driver is inserted (when this module is loaded), for each
		available adapter
	when a new adapter is inserted (and asb100_driver is still present)
 */
static int asb100_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, asb100_detect);
}

static int asb100_detect_subclients(struct i2c_adapter *adapter, int address,
		int kind, struct i2c_client *new_client)
{
	int i, id, err;
	struct asb100_data *data = i2c_get_clientdata(new_client);

	data->lm75[0] = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!(data->lm75[0])) {
		err = -ENOMEM;
		goto ERROR_SC_0;
	}

	data->lm75[1] = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!(data->lm75[1])) {
		err = -ENOMEM;
		goto ERROR_SC_1;
	}

	id = i2c_adapter_id(adapter);

	if (force_subclients[0] == id && force_subclients[1] == address) {
		for (i = 2; i <= 3; i++) {
			if (force_subclients[i] < 0x48 ||
			    force_subclients[i] > 0x4f) {
				dev_err(&new_client->dev, "invalid subclient "
					"address %d; must be 0x48-0x4f\n",
					force_subclients[i]);
				err = -ENODEV;
				goto ERROR_SC_2;
			}
		}
		asb100_write_value(new_client, ASB100_REG_I2C_SUBADDR,
					(force_subclients[2] & 0x07) |
					((force_subclients[3] & 0x07) <<4));
		data->lm75[0]->addr = force_subclients[2];
		data->lm75[1]->addr = force_subclients[3];
	} else {
		int val = asb100_read_value(new_client, ASB100_REG_I2C_SUBADDR);
		data->lm75[0]->addr = 0x48 + (val & 0x07);
		data->lm75[1]->addr = 0x48 + ((val >> 4) & 0x07);
	}

	if(data->lm75[0]->addr == data->lm75[1]->addr) {
		dev_err(&new_client->dev, "duplicate addresses 0x%x "
				"for subclients\n", data->lm75[0]->addr);
		err = -ENODEV;
		goto ERROR_SC_2;
	}

	for (i = 0; i <= 1; i++) {
		i2c_set_clientdata(data->lm75[i], NULL);
		data->lm75[i]->adapter = adapter;
		data->lm75[i]->driver = &asb100_driver;
		data->lm75[i]->flags = 0;
		strlcpy(data->lm75[i]->name, "asb100 subclient", I2C_NAME_SIZE);
	}

	if ((err = i2c_attach_client(data->lm75[0]))) {
		dev_err(&new_client->dev, "subclient %d registration "
			"at address 0x%x failed.\n", i, data->lm75[0]->addr);
		goto ERROR_SC_2;
	}

	if ((err = i2c_attach_client(data->lm75[1]))) {
		dev_err(&new_client->dev, "subclient %d registration "
			"at address 0x%x failed.\n", i, data->lm75[1]->addr);
		goto ERROR_SC_3;
	}

	return 0;

/* Undo inits in case of errors */
ERROR_SC_3:
	i2c_detach_client(data->lm75[0]);
ERROR_SC_2:
	kfree(data->lm75[1]);
ERROR_SC_1:
	kfree(data->lm75[0]);
ERROR_SC_0:
	return err;
}

static int asb100_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int err;
	struct i2c_client *new_client;
	struct asb100_data *data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		pr_debug("asb100.o: detect failed, "
				"smbus byte data not supported!\n");
		err = -ENODEV;
		goto ERROR0;
	}

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access asb100_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct asb100_data), GFP_KERNEL))) {
		pr_debug("asb100.o: detect failed, kzalloc failed!\n");
		err = -ENOMEM;
		goto ERROR0;
	}

	new_client = &data->client;
	mutex_init(&data->lock);
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &asb100_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. */

	/* The chip may be stuck in some other bank than bank 0. This may
	   make reading other information impossible. Specify a force=... or
	   force_*=... parameter, and the chip will be reset to the right
	   bank. */
	if (kind < 0) {

		int val1 = asb100_read_value(new_client, ASB100_REG_BANK);
		int val2 = asb100_read_value(new_client, ASB100_REG_CHIPMAN);

		/* If we're in bank 0 */
		if ( (!(val1 & 0x07)) &&
				/* Check for ASB100 ID (low byte) */
				( ((!(val1 & 0x80)) && (val2 != 0x94)) ||
				/* Check for ASB100 ID (high byte ) */
				((val1 & 0x80) && (val2 != 0x06)) ) ) {
			pr_debug("asb100.o: detect failed, "
					"bad chip id 0x%02x!\n", val2);
			err = -ENODEV;
			goto ERROR1;
		}

	} /* kind < 0 */

	/* We have either had a force parameter, or we have already detected
	   Winbond. Put it now into bank 0 and Vendor ID High Byte */
	asb100_write_value(new_client, ASB100_REG_BANK,
		(asb100_read_value(new_client, ASB100_REG_BANK) & 0x78) | 0x80);

	/* Determine the chip type. */
	if (kind <= 0) {
		int val1 = asb100_read_value(new_client, ASB100_REG_WCHIPID);
		int val2 = asb100_read_value(new_client, ASB100_REG_CHIPMAN);

		if ((val1 == 0x31) && (val2 == 0x06))
			kind = asb100;
		else {
			if (kind == 0)
				dev_warn(&new_client->dev, "ignoring "
					"'force' parameter for unknown chip "
					"at adapter %d, address 0x%02x.\n",
					i2c_adapter_id(adapter), address);
			err = -ENODEV;
			goto ERROR1;
		}
	}

	/* Fill in remaining client fields and put it into the global list */
	strlcpy(new_client->name, "asb100", I2C_NAME_SIZE);
	data->type = kind;

	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR1;

	/* Attach secondary lm75 clients */
	if ((err = asb100_detect_subclients(adapter, address, kind,
			new_client)))
		goto ERROR2;

	/* Initialize the chip */
	asb100_init_client(new_client);

	/* A few vars need to be filled upon startup */
	data->fan_min[0] = asb100_read_value(new_client, ASB100_REG_FAN_MIN(0));
	data->fan_min[1] = asb100_read_value(new_client, ASB100_REG_FAN_MIN(1));
	data->fan_min[2] = asb100_read_value(new_client, ASB100_REG_FAN_MIN(2));

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR3;
	}

	device_create_file_in(new_client, 0);
	device_create_file_in(new_client, 1);
	device_create_file_in(new_client, 2);
	device_create_file_in(new_client, 3);
	device_create_file_in(new_client, 4);
	device_create_file_in(new_client, 5);
	device_create_file_in(new_client, 6);

	device_create_file_fan(new_client, 1);
	device_create_file_fan(new_client, 2);
	device_create_file_fan(new_client, 3);

	device_create_file_temp(new_client, 1);
	device_create_file_temp(new_client, 2);
	device_create_file_temp(new_client, 3);
	device_create_file_temp(new_client, 4);

	device_create_file_vid(new_client);
	device_create_file_vrm(new_client);

	device_create_file_alarms(new_client);

	device_create_file_pwm1(new_client);

	return 0;

ERROR3:
	i2c_detach_client(data->lm75[1]);
	i2c_detach_client(data->lm75[0]);
	kfree(data->lm75[1]);
	kfree(data->lm75[0]);
ERROR2:
	i2c_detach_client(new_client);
ERROR1:
	kfree(data);
ERROR0:
	return err;
}

static int asb100_detach_client(struct i2c_client *client)
{
	struct asb100_data *data = i2c_get_clientdata(client);
	int err;

	/* main client */
	if (data)
		hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	/* main client */
	if (data)
		kfree(data);

	/* subclient */
	else
		kfree(client);

	return 0;
}

/* The SMBus locks itself, usually, but nothing may access the chip between
   bank switches. */
static int asb100_read_value(struct i2c_client *client, u16 reg)
{
	struct asb100_data *data = i2c_get_clientdata(client);
	struct i2c_client *cl;
	int res, bank;

	mutex_lock(&data->lock);

	bank = (reg >> 8) & 0x0f;
	if (bank > 2)
		/* switch banks */
		i2c_smbus_write_byte_data(client, ASB100_REG_BANK, bank);

	if (bank == 0 || bank > 2) {
		res = i2c_smbus_read_byte_data(client, reg & 0xff);
	} else {
		/* switch to subclient */
		cl = data->lm75[bank - 1];

		/* convert from ISA to LM75 I2C addresses */
		switch (reg & 0xff) {
		case 0x50: /* TEMP */
			res = swab16(i2c_smbus_read_word_data (cl, 0));
			break;
		case 0x52: /* CONFIG */
			res = i2c_smbus_read_byte_data(cl, 1);
			break;
		case 0x53: /* HYST */
			res = swab16(i2c_smbus_read_word_data (cl, 2));
			break;
		case 0x55: /* MAX */
		default:
			res = swab16(i2c_smbus_read_word_data (cl, 3));
			break;
		}
	}

	if (bank > 2)
		i2c_smbus_write_byte_data(client, ASB100_REG_BANK, 0);

	mutex_unlock(&data->lock);

	return res;
}

static void asb100_write_value(struct i2c_client *client, u16 reg, u16 value)
{
	struct asb100_data *data = i2c_get_clientdata(client);
	struct i2c_client *cl;
	int bank;

	mutex_lock(&data->lock);

	bank = (reg >> 8) & 0x0f;
	if (bank > 2)
		/* switch banks */
		i2c_smbus_write_byte_data(client, ASB100_REG_BANK, bank);

	if (bank == 0 || bank > 2) {
		i2c_smbus_write_byte_data(client, reg & 0xff, value & 0xff);
	} else {
		/* switch to subclient */
		cl = data->lm75[bank - 1];

		/* convert from ISA to LM75 I2C addresses */
		switch (reg & 0xff) {
		case 0x52: /* CONFIG */
			i2c_smbus_write_byte_data(cl, 1, value & 0xff);
			break;
		case 0x53: /* HYST */
			i2c_smbus_write_word_data(cl, 2, swab16(value));
			break;
		case 0x55: /* MAX */
			i2c_smbus_write_word_data(cl, 3, swab16(value));
			break;
		}
	}

	if (bank > 2)
		i2c_smbus_write_byte_data(client, ASB100_REG_BANK, 0);

	mutex_unlock(&data->lock);
}

static void asb100_init_client(struct i2c_client *client)
{
	struct asb100_data *data = i2c_get_clientdata(client);
	int vid = 0;

	vid = asb100_read_value(client, ASB100_REG_VID_FANDIV) & 0x0f;
	vid |= (asb100_read_value(client, ASB100_REG_CHIPID) & 0x01) << 4;
	data->vrm = vid_which_vrm();
	vid = vid_from_reg(vid, data->vrm);

	/* Start monitoring */
	asb100_write_value(client, ASB100_REG_CONFIG, 
		(asb100_read_value(client, ASB100_REG_CONFIG) & 0xf7) | 0x01);
}

static struct asb100_data *asb100_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct asb100_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
		|| !data->valid) {

		dev_dbg(&client->dev, "starting device update...\n");

		/* 7 voltage inputs */
		for (i = 0; i < 7; i++) {
			data->in[i] = asb100_read_value(client,
				ASB100_REG_IN(i));
			data->in_min[i] = asb100_read_value(client,
				ASB100_REG_IN_MIN(i));
			data->in_max[i] = asb100_read_value(client,
				ASB100_REG_IN_MAX(i));
		}

		/* 3 fan inputs */
		for (i = 0; i < 3; i++) {
			data->fan[i] = asb100_read_value(client,
					ASB100_REG_FAN(i));
			data->fan_min[i] = asb100_read_value(client,
					ASB100_REG_FAN_MIN(i));
		}

		/* 4 temperature inputs */
		for (i = 1; i <= 4; i++) {
			data->temp[i-1] = asb100_read_value(client,
					ASB100_REG_TEMP(i));
			data->temp_max[i-1] = asb100_read_value(client,
					ASB100_REG_TEMP_MAX(i));
			data->temp_hyst[i-1] = asb100_read_value(client,
					ASB100_REG_TEMP_HYST(i));
		}

		/* VID and fan divisors */
		i = asb100_read_value(client, ASB100_REG_VID_FANDIV);
		data->vid = i & 0x0f;
		data->vid |= (asb100_read_value(client,
				ASB100_REG_CHIPID) & 0x01) << 4;
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = (i >> 6) & 0x03;
		data->fan_div[2] = (asb100_read_value(client,
				ASB100_REG_PIN) >> 6) & 0x03;

		/* PWM */
		data->pwm = asb100_read_value(client, ASB100_REG_PWM1);

		/* alarms */
		data->alarms = asb100_read_value(client, ASB100_REG_ALARM1) +
			(asb100_read_value(client, ASB100_REG_ALARM2) << 8);

		data->last_updated = jiffies;
		data->valid = 1;

		dev_dbg(&client->dev, "... device update complete\n");
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init asb100_init(void)
{
	return i2c_add_driver(&asb100_driver);
}

static void __exit asb100_exit(void)
{
	i2c_del_driver(&asb100_driver);
}

MODULE_AUTHOR("Mark M. Hoffman <mhoffman@lightlink.com>");
MODULE_DESCRIPTION("ASB100 Bach driver");
MODULE_LICENSE("GPL");

module_init(asb100_init);
module_exit(asb100_exit);

