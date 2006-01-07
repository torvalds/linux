/*
 *  pc87360.c - Part of lm_sensors, Linux kernel modules
 *              for hardware monitoring
 *  Copyright (C) 2004 Jean Delvare <khali@linux-fr.org>
 *
 *  Copied from smsc47m1.c:
 *  Copyright (C) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Supports the following chips:
 *
 *  Chip        #vin    #fan    #pwm    #temp   devid
 *  PC87360     -       2       2       -       0xE1
 *  PC87363     -       2       2       -       0xE8
 *  PC87364     -       3       3       -       0xE4
 *  PC87365     11      3       3       2       0xE5
 *  PC87366     11      3       3       3-4     0xE9
 *
 *  This driver assumes that no more than one chip is present, and one of
 *  the standard Super-I/O addresses is used (0x2E/0x2F or 0x4E/0x4F).
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-isa.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <asm/io.h>

static u8 devid;
static unsigned short address;
static unsigned short extra_isa[3];
static u8 confreg[4];

enum chips { any_chip, pc87360, pc87363, pc87364, pc87365, pc87366 };

static int init = 1;
module_param(init, int, 0);
MODULE_PARM_DESC(init,
 "Chip initialization level:\n"
 " 0: None\n"
 "*1: Forcibly enable internal voltage and temperature channels, except in9\n"
 " 2: Forcibly enable all voltage and temperature channels, except in9\n"
 " 3: Forcibly enable all voltage and temperature channels, including in9");

/*
 * Super-I/O registers and operations
 */

#define DEV	0x07	/* Register: Logical device select */
#define DEVID	0x20	/* Register: Device ID */
#define ACT	0x30	/* Register: Device activation */
#define BASE	0x60	/* Register: Base address */

#define FSCM	0x09	/* Logical device: fans */
#define VLM	0x0d	/* Logical device: voltages */
#define TMS	0x0e	/* Logical device: temperatures */
static const u8 logdev[3] = { FSCM, VLM, TMS };

#define LD_FAN		0
#define LD_IN		1
#define LD_TEMP		2

static inline void superio_outb(int sioaddr, int reg, int val)
{
	outb(reg, sioaddr);
	outb(val, sioaddr+1);
}

static inline int superio_inb(int sioaddr, int reg)
{
	outb(reg, sioaddr);
	return inb(sioaddr+1);
}

static inline void superio_exit(int sioaddr)
{
	outb(0x02, sioaddr);
	outb(0x02, sioaddr+1);
}

/*
 * Logical devices
 */

#define PC87360_EXTENT		0x10
#define PC87365_REG_BANK	0x09
#define NO_BANK			0xff

/*
 * Fan registers and conversions
 */

/* nr has to be 0 or 1 (PC87360/87363) or 2 (PC87364/87365/87366) */
#define PC87360_REG_PRESCALE(nr)	(0x00 + 2 * (nr))
#define PC87360_REG_PWM(nr)		(0x01 + 2 * (nr))
#define PC87360_REG_FAN_MIN(nr)		(0x06 + 3 * (nr))
#define PC87360_REG_FAN(nr)		(0x07 + 3 * (nr))
#define PC87360_REG_FAN_STATUS(nr)	(0x08 + 3 * (nr))

#define FAN_FROM_REG(val,div)		((val) == 0 ? 0: \
					 480000 / ((val)*(div)))
#define FAN_TO_REG(val,div)		((val) <= 100 ? 0 : \
					 480000 / ((val)*(div)))
#define FAN_DIV_FROM_REG(val)		(1 << ((val >> 5) & 0x03))
#define FAN_STATUS_FROM_REG(val)	((val) & 0x07)

#define FAN_CONFIG_MONITOR(val,nr)	(((val) >> (2 + nr * 3)) & 1)
#define FAN_CONFIG_CONTROL(val,nr)	(((val) >> (3 + nr * 3)) & 1)
#define FAN_CONFIG_INVERT(val,nr)	(((val) >> (4 + nr * 3)) & 1)

#define PWM_FROM_REG(val,inv)		((inv) ? 255 - (val) : (val))
static inline u8 PWM_TO_REG(int val, int inv)
{
	if (inv)
		val = 255 - val;
	if (val < 0)
		return 0;
	if (val > 255)
		return 255;
	return val;
}

/*
 * Voltage registers and conversions
 */

#define PC87365_REG_IN_CONVRATE		0x07
#define PC87365_REG_IN_CONFIG		0x08
#define PC87365_REG_IN			0x0B
#define PC87365_REG_IN_MIN		0x0D
#define PC87365_REG_IN_MAX		0x0C
#define PC87365_REG_IN_STATUS		0x0A
#define PC87365_REG_IN_ALARMS1		0x00
#define PC87365_REG_IN_ALARMS2		0x01
#define PC87365_REG_VID			0x06

#define IN_FROM_REG(val,ref)		(((val) * (ref) + 128) / 256)
#define IN_TO_REG(val,ref)		((val) < 0 ? 0 : \
					 (val)*256 >= (ref)*255 ? 255: \
					 ((val) * 256 + (ref)/2) / (ref))

/*
 * Temperature registers and conversions
 */

#define PC87365_REG_TEMP_CONFIG		0x08
#define PC87365_REG_TEMP		0x0B
#define PC87365_REG_TEMP_MIN		0x0D
#define PC87365_REG_TEMP_MAX		0x0C
#define PC87365_REG_TEMP_CRIT		0x0E
#define PC87365_REG_TEMP_STATUS		0x0A
#define PC87365_REG_TEMP_ALARMS		0x00

#define TEMP_FROM_REG(val)		((val) * 1000)
#define TEMP_TO_REG(val)		((val) < -55000 ? -55 : \
					 (val) > 127000 ? 127 : \
					 (val) < 0 ? ((val) - 500) / 1000 : \
					 ((val) + 500) / 1000)

/*
 * Client data (each client gets its own)
 */

struct pc87360_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore lock;
	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	int address[3];

	u8 fannr, innr, tempnr;

	u8 fan[3];		/* Register value */
	u8 fan_min[3];		/* Register value */
	u8 fan_status[3];	/* Register value */
	u8 pwm[3];		/* Register value */
	u16 fan_conf;		/* Configuration register values, combined */

	u16 in_vref;		/* 1 mV/bit */
	u8 in[14];		/* Register value */
	u8 in_min[14];		/* Register value */
	u8 in_max[14];		/* Register value */
	u8 in_crit[3];		/* Register value */
	u8 in_status[14];	/* Register value */
	u16 in_alarms;		/* Register values, combined, masked */
	u8 vid_conf;		/* Configuration register value */
	u8 vrm;
	u8 vid;			/* Register value */

	s8 temp[3];		/* Register value */
	s8 temp_min[3];		/* Register value */
	s8 temp_max[3];		/* Register value */
	s8 temp_crit[3];	/* Register value */
	u8 temp_status[3];	/* Register value */
	u8 temp_alarms;		/* Register value, masked */
};

/*
 * Functions declaration
 */

static int pc87360_detect(struct i2c_adapter *adapter);
static int pc87360_detach_client(struct i2c_client *client);

static int pc87360_read_value(struct pc87360_data *data, u8 ldi, u8 bank,
			      u8 reg);
static void pc87360_write_value(struct pc87360_data *data, u8 ldi, u8 bank,
				u8 reg, u8 value);
static void pc87360_init_client(struct i2c_client *client, int use_thermistors);
static struct pc87360_data *pc87360_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

static struct i2c_driver pc87360_driver = {
	.driver = {
		.name	= "pc87360",
	},
	.attach_adapter	= pc87360_detect,
	.detach_client	= pc87360_detach_client,
};

/*
 * Sysfs stuff
 */

static ssize_t show_fan_input(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", FAN_FROM_REG(data->fan[attr->index],
		       FAN_DIV_FROM_REG(data->fan_status[attr->index])));
}
static ssize_t show_fan_min(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", FAN_FROM_REG(data->fan_min[attr->index],
		       FAN_DIV_FROM_REG(data->fan_status[attr->index])));
}
static ssize_t show_fan_div(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n",
		       FAN_DIV_FROM_REG(data->fan_status[attr->index]));
}
static ssize_t show_fan_status(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n",
		       FAN_STATUS_FROM_REG(data->fan_status[attr->index]));
}
static ssize_t set_fan_min(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long fan_min = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	fan_min = FAN_TO_REG(fan_min, FAN_DIV_FROM_REG(data->fan_status[attr->index]));

	/* If it wouldn't fit, change clock divisor */
	while (fan_min > 255
	    && (data->fan_status[attr->index] & 0x60) != 0x60) {
		fan_min >>= 1;
		data->fan[attr->index] >>= 1;
		data->fan_status[attr->index] += 0x20;
	}
	data->fan_min[attr->index] = fan_min > 255 ? 255 : fan_min;
	pc87360_write_value(data, LD_FAN, NO_BANK, PC87360_REG_FAN_MIN(attr->index),
			    data->fan_min[attr->index]);

	/* Write new divider, preserve alarm bits */
	pc87360_write_value(data, LD_FAN, NO_BANK, PC87360_REG_FAN_STATUS(attr->index),
			    data->fan_status[attr->index] & 0xF9);
	up(&data->update_lock);

	return count;
}

#define show_and_set_fan(offset) \
static SENSOR_DEVICE_ATTR(fan##offset##_input, S_IRUGO, \
	show_fan_input, NULL, offset-1); \
static SENSOR_DEVICE_ATTR(fan##offset##_min, S_IWUSR | S_IRUGO, \
	show_fan_min, set_fan_min, offset-1); \
static SENSOR_DEVICE_ATTR(fan##offset##_div, S_IRUGO, \
	show_fan_div, NULL, offset-1); \
static SENSOR_DEVICE_ATTR(fan##offset##_status, S_IRUGO, \
	show_fan_status, NULL, offset-1);
show_and_set_fan(1)
show_and_set_fan(2)
show_and_set_fan(3)

static ssize_t show_pwm(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n",
		       PWM_FROM_REG(data->pwm[attr->index],
				    FAN_CONFIG_INVERT(data->fan_conf,
						      attr->index)));
}
static ssize_t set_pwm(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->pwm[attr->index] = PWM_TO_REG(val,
			      FAN_CONFIG_INVERT(data->fan_conf, attr->index));
	pc87360_write_value(data, LD_FAN, NO_BANK, PC87360_REG_PWM(attr->index),
			    data->pwm[attr->index]);
	up(&data->update_lock);
	return count;
}

#define show_and_set_pwm(offset) \
static SENSOR_DEVICE_ATTR(pwm##offset, S_IWUSR | S_IRUGO, \
	show_pwm, set_pwm, offset-1);
show_and_set_pwm(1)
show_and_set_pwm(2)
show_and_set_pwm(3)

static ssize_t show_in_input(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in[attr->index],
		       data->in_vref));
}
static ssize_t show_in_min(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_min[attr->index],
		       data->in_vref));
}
static ssize_t show_in_max(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_max[attr->index],
		       data->in_vref));
}
static ssize_t show_in_status(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", data->in_status[attr->index]);
}
static ssize_t set_in_min(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->in_min[attr->index] = IN_TO_REG(val, data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_IN_MIN,
			    data->in_min[attr->index]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_in_max(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->in_max[attr->index] = IN_TO_REG(val,
			       data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_IN_MAX,
			    data->in_max[attr->index]);
	up(&data->update_lock);
	return count;
}

#define show_and_set_in(offset) \
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO, \
	show_in_input, NULL, offset); \
static SENSOR_DEVICE_ATTR(in##offset##_min, S_IWUSR | S_IRUGO, \
	show_in_min, set_in_min, offset); \
static SENSOR_DEVICE_ATTR(in##offset##_max, S_IWUSR | S_IRUGO, \
	show_in_max, set_in_max, offset); \
static SENSOR_DEVICE_ATTR(in##offset##_status, S_IRUGO, \
	show_in_status, NULL, offset);
show_and_set_in(0)
show_and_set_in(1)
show_and_set_in(2)
show_and_set_in(3)
show_and_set_in(4)
show_and_set_in(5)
show_and_set_in(6)
show_and_set_in(7)
show_and_set_in(8)
show_and_set_in(9)
show_and_set_in(10)

static ssize_t show_therm_input(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in[attr->index],
		       data->in_vref));
}
static ssize_t show_therm_min(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_min[attr->index],
		       data->in_vref));
}
static ssize_t show_therm_max(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_max[attr->index],
		       data->in_vref));
}
static ssize_t show_therm_crit(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_crit[attr->index-11],
		       data->in_vref));
}
static ssize_t show_therm_status(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", data->in_status[attr->index]);
}
static ssize_t set_therm_min(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->in_min[attr->index] = IN_TO_REG(val, data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_TEMP_MIN,
			    data->in_min[attr->index]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_therm_max(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->in_max[attr->index] = IN_TO_REG(val, data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_TEMP_MAX,
			    data->in_max[attr->index]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_therm_crit(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->in_crit[attr->index-11] = IN_TO_REG(val, data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_TEMP_CRIT,
			    data->in_crit[attr->index-11]);
	up(&data->update_lock);
	return count;
}

#define show_and_set_therm(offset) \
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO, \
	show_therm_input, NULL, 11+offset-4); \
static SENSOR_DEVICE_ATTR(temp##offset##_min, S_IWUSR | S_IRUGO, \
	show_therm_min, set_therm_min, 11+offset-4); \
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IWUSR | S_IRUGO, \
	show_therm_max, set_therm_max, 11+offset-4); \
static SENSOR_DEVICE_ATTR(temp##offset##_crit, S_IWUSR | S_IRUGO, \
	show_therm_crit, set_therm_crit, 11+offset-4); \
static SENSOR_DEVICE_ATTR(temp##offset##_status, S_IRUGO, \
	show_therm_status, NULL, 11+offset-4);
show_and_set_therm(4)
show_and_set_therm(5)
show_and_set_therm(6)

static ssize_t show_vid(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

static ssize_t show_vrm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", data->vrm);
}
static ssize_t set_vrm(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	data->vrm = simple_strtoul(buf, NULL, 10);
	return count;
}
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm, set_vrm);

static ssize_t show_in_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", data->in_alarms);
}
static DEVICE_ATTR(alarms_in, S_IRUGO, show_in_alarms, NULL);

static ssize_t show_temp_input(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index]));
}
static ssize_t show_temp_min(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_min[attr->index]));
}
static ssize_t show_temp_max(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[attr->index]));
}
static ssize_t show_temp_crit(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_crit[attr->index]));
}
static ssize_t show_temp_status(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_status[attr->index]);
}
static ssize_t set_temp_min(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->temp_min[attr->index] = TEMP_TO_REG(val);
	pc87360_write_value(data, LD_TEMP, attr->index, PC87365_REG_TEMP_MIN,
			    data->temp_min[attr->index]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_temp_max(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->temp_max[attr->index] = TEMP_TO_REG(val);
	pc87360_write_value(data, LD_TEMP, attr->index, PC87365_REG_TEMP_MAX,
			    data->temp_max[attr->index]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_temp_crit(struct device *dev, struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->temp_crit[attr->index] = TEMP_TO_REG(val);
	pc87360_write_value(data, LD_TEMP, attr->index, PC87365_REG_TEMP_CRIT,
			    data->temp_crit[attr->index]);
	up(&data->update_lock);
	return count;
}

#define show_and_set_temp(offset) \
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO, \
	show_temp_input, NULL, offset-1); \
static SENSOR_DEVICE_ATTR(temp##offset##_min, S_IWUSR | S_IRUGO, \
	show_temp_min, set_temp_min, offset-1); \
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IWUSR | S_IRUGO, \
	show_temp_max, set_temp_max, offset-1); \
static SENSOR_DEVICE_ATTR(temp##offset##_crit, S_IWUSR | S_IRUGO, \
	show_temp_crit, set_temp_crit, offset-1); \
static SENSOR_DEVICE_ATTR(temp##offset##_status, S_IRUGO, \
	show_temp_status, NULL, offset-1);
show_and_set_temp(1)
show_and_set_temp(2)
show_and_set_temp(3)

static ssize_t show_temp_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", data->temp_alarms);
}
static DEVICE_ATTR(alarms_temp, S_IRUGO, show_temp_alarms, NULL);

/*
 * Device detection, registration and update
 */

static int __init pc87360_find(int sioaddr, u8 *devid, unsigned short *addresses)
{
	u16 val;
	int i;
	int nrdev; /* logical device count */

	/* No superio_enter */

	/* Identify device */
	val = superio_inb(sioaddr, DEVID);
	switch (val) {
	case 0xE1: /* PC87360 */
	case 0xE8: /* PC87363 */
	case 0xE4: /* PC87364 */
		nrdev = 1;
		break;
	case 0xE5: /* PC87365 */
	case 0xE9: /* PC87366 */
		nrdev = 3;
		break;
	default:
		superio_exit(sioaddr);
		return -ENODEV;
	}
	/* Remember the device id */
	*devid = val;

	for (i = 0; i < nrdev; i++) {
		/* select logical device */
		superio_outb(sioaddr, DEV, logdev[i]);

		val = superio_inb(sioaddr, ACT);
		if (!(val & 0x01)) {
			printk(KERN_INFO "pc87360: Device 0x%02x not "
			       "activated\n", logdev[i]);
			continue;
		}

		val = (superio_inb(sioaddr, BASE) << 8)
		    | superio_inb(sioaddr, BASE + 1);
		if (!val) {
			printk(KERN_INFO "pc87360: Base address not set for "
			       "device 0x%02x\n", logdev[i]);
			continue;
		}

		addresses[i] = val;

		if (i==0) { /* Fans */
			confreg[0] = superio_inb(sioaddr, 0xF0);
			confreg[1] = superio_inb(sioaddr, 0xF1);

#ifdef DEBUG
			printk(KERN_DEBUG "pc87360: Fan 1: mon=%d "
			       "ctrl=%d inv=%d\n", (confreg[0]>>2)&1,
			       (confreg[0]>>3)&1, (confreg[0]>>4)&1);
			printk(KERN_DEBUG "pc87360: Fan 2: mon=%d "
			       "ctrl=%d inv=%d\n", (confreg[0]>>5)&1,
			       (confreg[0]>>6)&1, (confreg[0]>>7)&1);
			printk(KERN_DEBUG "pc87360: Fan 3: mon=%d "
			       "ctrl=%d inv=%d\n", confreg[1]&1,
			       (confreg[1]>>1)&1, (confreg[1]>>2)&1);
#endif
		} else if (i==1) { /* Voltages */
			/* Are we using thermistors? */
			if (*devid == 0xE9) { /* PC87366 */
				/* These registers are not logical-device
				   specific, just that we won't need them if
				   we don't use the VLM device */
				confreg[2] = superio_inb(sioaddr, 0x2B);
				confreg[3] = superio_inb(sioaddr, 0x25);

				if (confreg[2] & 0x40) {
					printk(KERN_INFO "pc87360: Using "
					       "thermistors for temperature "
					       "monitoring\n");
				}
				if (confreg[3] & 0xE0) {
					printk(KERN_INFO "pc87360: VID "
					       "inputs routed (mode %u)\n",
					       confreg[3] >> 5);
				}
			}
		}
	}

	superio_exit(sioaddr);
	return 0;
}

static int pc87360_detect(struct i2c_adapter *adapter)
{
	int i;
	struct i2c_client *new_client;
	struct pc87360_data *data;
	int err = 0;
	const char *name = "pc87360";
	int use_thermistors = 0;

	if (!(data = kzalloc(sizeof(struct pc87360_data), GFP_KERNEL)))
		return -ENOMEM;

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	init_MUTEX(&data->lock);
	new_client->adapter = adapter;
	new_client->driver = &pc87360_driver;
	new_client->flags = 0;

	data->fannr = 2;
	data->innr = 0;
	data->tempnr = 0;

	switch (devid) {
	case 0xe8:
		name = "pc87363";
		break;
	case 0xe4:
		name = "pc87364";
		data->fannr = 3;
		break;
	case 0xe5:
		name = "pc87365";
		data->fannr = extra_isa[0] ? 3 : 0;
		data->innr = extra_isa[1] ? 11 : 0;
		data->tempnr = extra_isa[2] ? 2 : 0;
		break;
	case 0xe9:
		name = "pc87366";
		data->fannr = extra_isa[0] ? 3 : 0;
		data->innr = extra_isa[1] ? 14 : 0;
		data->tempnr = extra_isa[2] ? 3 : 0;
		break;
	}

	strcpy(new_client->name, name);
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	for (i = 0; i < 3; i++) {
		if (((data->address[i] = extra_isa[i]))
		 && !request_region(extra_isa[i], PC87360_EXTENT,
		 		    pc87360_driver.driver.name)) {
			dev_err(&new_client->dev, "Region 0x%x-0x%x already "
				"in use!\n", extra_isa[i],
				extra_isa[i]+PC87360_EXTENT-1);
			for (i--; i >= 0; i--)
				release_region(extra_isa[i], PC87360_EXTENT);
			err = -EBUSY;
			goto ERROR1;
		}
	}

	/* Retrieve the fans configuration from Super-I/O space */
	if (data->fannr)
		data->fan_conf = confreg[0] | (confreg[1] << 8);

	if ((err = i2c_attach_client(new_client)))
		goto ERROR2;

	/* Use the correct reference voltage
	   Unless both the VLM and the TMS logical devices agree to
	   use an external Vref, the internal one is used. */
	if (data->innr) {
		i = pc87360_read_value(data, LD_IN, NO_BANK,
				       PC87365_REG_IN_CONFIG);
		if (data->tempnr) {
			i &= pc87360_read_value(data, LD_TEMP, NO_BANK,
						PC87365_REG_TEMP_CONFIG);
		}
		data->in_vref = (i&0x02) ? 3025 : 2966;
		dev_dbg(&new_client->dev, "Using %s reference voltage\n",
			(i&0x02) ? "external" : "internal");

		data->vid_conf = confreg[3];
		data->vrm = 90;
	}

	/* Fan clock dividers may be needed before any data is read */
	for (i = 0; i < data->fannr; i++) {
		if (FAN_CONFIG_MONITOR(data->fan_conf, i))
			data->fan_status[i] = pc87360_read_value(data,
					      LD_FAN, NO_BANK,
					      PC87360_REG_FAN_STATUS(i));
	}

	if (init > 0) {
		if (devid == 0xe9 && data->address[1]) /* PC87366 */
			use_thermistors = confreg[2] & 0x40;

		pc87360_init_client(new_client, use_thermistors);
	}

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR3;
	}

	if (data->innr) {
		device_create_file(&new_client->dev, &sensor_dev_attr_in0_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in1_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in2_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in3_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in4_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in5_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in6_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in7_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in8_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in9_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in10_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in0_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in1_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in2_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in3_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in4_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in5_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in6_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in7_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in8_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in9_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in10_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in0_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in1_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in2_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in3_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in4_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in5_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in6_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in7_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in8_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in9_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in10_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in0_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in1_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in2_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in3_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in4_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in5_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in6_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in7_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in8_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in9_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_in10_status.dev_attr);

		device_create_file(&new_client->dev, &dev_attr_cpu0_vid);
		device_create_file(&new_client->dev, &dev_attr_vrm);
		device_create_file(&new_client->dev, &dev_attr_alarms_in);
	}

	if (data->tempnr) {
		device_create_file(&new_client->dev, &sensor_dev_attr_temp1_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp2_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp1_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp2_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp1_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp2_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp1_crit.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp2_crit.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp1_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp2_status.dev_attr);

		device_create_file(&new_client->dev, &dev_attr_alarms_temp);
	}
	if (data->tempnr == 3) {
		device_create_file(&new_client->dev, &sensor_dev_attr_temp3_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp3_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp3_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp3_crit.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp3_status.dev_attr);
	}
	if (data->innr == 14) {
		device_create_file(&new_client->dev, &sensor_dev_attr_temp4_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp5_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp6_input.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp4_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp5_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp6_min.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp4_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp5_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp6_max.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp4_crit.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp5_crit.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp6_crit.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp4_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp5_status.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_temp6_status.dev_attr);
	}

	if (data->fannr) {
		if (FAN_CONFIG_MONITOR(data->fan_conf, 0)) {
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan1_input.dev_attr);
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan1_min.dev_attr);
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan1_div.dev_attr);
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan1_status.dev_attr);
		}

		if (FAN_CONFIG_MONITOR(data->fan_conf, 1)) {
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan2_input.dev_attr);
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan2_min.dev_attr);
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan2_div.dev_attr);
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan2_status.dev_attr);
		}

		if (FAN_CONFIG_CONTROL(data->fan_conf, 0))
			device_create_file(&new_client->dev, &sensor_dev_attr_pwm1.dev_attr);
		if (FAN_CONFIG_CONTROL(data->fan_conf, 1))
			device_create_file(&new_client->dev, &sensor_dev_attr_pwm2.dev_attr);
	}
	if (data->fannr == 3) {
		if (FAN_CONFIG_MONITOR(data->fan_conf, 2)) {
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan3_input.dev_attr);
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan3_min.dev_attr);
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan3_div.dev_attr);
			device_create_file(&new_client->dev,
					   &sensor_dev_attr_fan3_status.dev_attr);
		}

		if (FAN_CONFIG_CONTROL(data->fan_conf, 2))
			device_create_file(&new_client->dev, &sensor_dev_attr_pwm3.dev_attr);
	}

	return 0;

ERROR3:
	i2c_detach_client(new_client);
ERROR2:
	for (i = 0; i < 3; i++) {
		if (data->address[i]) {
			release_region(data->address[i], PC87360_EXTENT);
		}
	}
ERROR1:
	kfree(data);
	return err;
}

static int pc87360_detach_client(struct i2c_client *client)
{
	struct pc87360_data *data = i2c_get_clientdata(client);
	int i;

	hwmon_device_unregister(data->class_dev);

	if ((i = i2c_detach_client(client)))
		return i;

	for (i = 0; i < 3; i++) {
		if (data->address[i]) {
			release_region(data->address[i], PC87360_EXTENT);
		}
	}
	kfree(data);

	return 0;
}

/* ldi is the logical device index
   bank is for voltages and temperatures only */
static int pc87360_read_value(struct pc87360_data *data, u8 ldi, u8 bank,
			      u8 reg)
{
	int res;

	down(&(data->lock));
	if (bank != NO_BANK)
		outb_p(bank, data->address[ldi] + PC87365_REG_BANK);
	res = inb_p(data->address[ldi] + reg);
	up(&(data->lock));

	return res;
}

static void pc87360_write_value(struct pc87360_data *data, u8 ldi, u8 bank,
				u8 reg, u8 value)
{
	down(&(data->lock));
	if (bank != NO_BANK)
		outb_p(bank, data->address[ldi] + PC87365_REG_BANK);
	outb_p(value, data->address[ldi] + reg);
	up(&(data->lock));
}

static void pc87360_init_client(struct i2c_client *client, int use_thermistors)
{
	struct pc87360_data *data = i2c_get_clientdata(client);
	int i, nr;
	const u8 init_in[14] = { 2, 2, 2, 2, 2, 2, 2, 1, 1, 3, 1, 2, 2, 2 };
	const u8 init_temp[3] = { 2, 2, 1 };
	u8 reg;

	if (init >= 2 && data->innr) {
		reg = pc87360_read_value(data, LD_IN, NO_BANK,
					 PC87365_REG_IN_CONVRATE);
		dev_info(&client->dev, "VLM conversion set to "
			 "1s period, 160us delay\n");
		pc87360_write_value(data, LD_IN, NO_BANK,
				    PC87365_REG_IN_CONVRATE,
				    (reg & 0xC0) | 0x11);
	}

	nr = data->innr < 11 ? data->innr : 11;
	for (i=0; i<nr; i++) {
		if (init >= init_in[i]) {
			/* Forcibly enable voltage channel */
			reg = pc87360_read_value(data, LD_IN, i,
						 PC87365_REG_IN_STATUS);
			if (!(reg & 0x01)) {
				dev_dbg(&client->dev, "Forcibly "
					"enabling in%d\n", i);
				pc87360_write_value(data, LD_IN, i,
						    PC87365_REG_IN_STATUS,
						    (reg & 0x68) | 0x87);
			}
		}
	}

	/* We can't blindly trust the Super-I/O space configuration bit,
	   most BIOS won't set it properly */
	for (i=11; i<data->innr; i++) {
		reg = pc87360_read_value(data, LD_IN, i,
					 PC87365_REG_TEMP_STATUS);
		use_thermistors = use_thermistors || (reg & 0x01);
	}

	i = use_thermistors ? 2 : 0;
	for (; i<data->tempnr; i++) {
		if (init >= init_temp[i]) {
			/* Forcibly enable temperature channel */
			reg = pc87360_read_value(data, LD_TEMP, i,
						 PC87365_REG_TEMP_STATUS);
			if (!(reg & 0x01)) {
				dev_dbg(&client->dev, "Forcibly "
					"enabling temp%d\n", i+1);
				pc87360_write_value(data, LD_TEMP, i,
						    PC87365_REG_TEMP_STATUS,
						    0xCF);
			}
		}
	}

	if (use_thermistors) {
		for (i=11; i<data->innr; i++) {
			if (init >= init_in[i]) {
				/* The pin may already be used by thermal
				   diodes */
				reg = pc87360_read_value(data, LD_TEMP,
				      (i-11)/2, PC87365_REG_TEMP_STATUS);
				if (reg & 0x01) {
					dev_dbg(&client->dev, "Skipping "
						"temp%d, pin already in use "
						"by temp%d\n", i-7, (i-11)/2);
					continue;
				}

				/* Forcibly enable thermistor channel */
				reg = pc87360_read_value(data, LD_IN, i,
							 PC87365_REG_IN_STATUS);
				if (!(reg & 0x01)) {
					dev_dbg(&client->dev, "Forcibly "
						"enabling temp%d\n", i-7);
					pc87360_write_value(data, LD_IN, i,
						PC87365_REG_TEMP_STATUS,
						(reg & 0x60) | 0x8F);
				}
			}
		}
	}

	if (data->innr) {
		reg = pc87360_read_value(data, LD_IN, NO_BANK,
					 PC87365_REG_IN_CONFIG);
		if (reg & 0x01) {
			dev_dbg(&client->dev, "Forcibly "
				"enabling monitoring (VLM)\n");
			pc87360_write_value(data, LD_IN, NO_BANK,
					    PC87365_REG_IN_CONFIG,
					    reg & 0xFE);
		}
	}

	if (data->tempnr) {
		reg = pc87360_read_value(data, LD_TEMP, NO_BANK,
					 PC87365_REG_TEMP_CONFIG);
		if (reg & 0x01) {
			dev_dbg(&client->dev, "Forcibly enabling "
				"monitoring (TMS)\n");
			pc87360_write_value(data, LD_TEMP, NO_BANK,
					    PC87365_REG_TEMP_CONFIG,
					    reg & 0xFE);
		}

		if (init >= 2) {
			/* Chip config as documented by National Semi. */
			pc87360_write_value(data, LD_TEMP, 0xF, 0xA, 0x08);
			/* We voluntarily omit the bank here, in case the
			   sequence itself matters. It shouldn't be a problem,
			   since nobody else is supposed to access the
			   device at that point. */
			pc87360_write_value(data, LD_TEMP, NO_BANK, 0xB, 0x04);
			pc87360_write_value(data, LD_TEMP, NO_BANK, 0xC, 0x35);
			pc87360_write_value(data, LD_TEMP, NO_BANK, 0xD, 0x05);
			pc87360_write_value(data, LD_TEMP, NO_BANK, 0xE, 0x05);
		}
	}
}

static void pc87360_autodiv(struct i2c_client *client, int nr)
{
	struct pc87360_data *data = i2c_get_clientdata(client);
	u8 old_min = data->fan_min[nr];

	/* Increase clock divider if needed and possible */
	if ((data->fan_status[nr] & 0x04) /* overflow flag */
	 || (data->fan[nr] >= 224)) { /* next to overflow */
		if ((data->fan_status[nr] & 0x60) != 0x60) {
			data->fan_status[nr] += 0x20;
			data->fan_min[nr] >>= 1;
			data->fan[nr] >>= 1;
			dev_dbg(&client->dev, "Increasing "
				"clock divider to %d for fan %d\n",
				FAN_DIV_FROM_REG(data->fan_status[nr]), nr+1);
		}
	} else {
		/* Decrease clock divider if possible */
		while (!(data->fan_min[nr] & 0x80) /* min "nails" divider */
		 && data->fan[nr] < 85 /* bad accuracy */
		 && (data->fan_status[nr] & 0x60) != 0x00) {
			data->fan_status[nr] -= 0x20;
			data->fan_min[nr] <<= 1;
			data->fan[nr] <<= 1;
			dev_dbg(&client->dev, "Decreasing "
				"clock divider to %d for fan %d\n",
				FAN_DIV_FROM_REG(data->fan_status[nr]),
				nr+1);
		}
	}

	/* Write new fan min if it changed */
	if (old_min != data->fan_min[nr]) {
		pc87360_write_value(data, LD_FAN, NO_BANK,
				    PC87360_REG_FAN_MIN(nr),
				    data->fan_min[nr]);
	}
}

static struct pc87360_data *pc87360_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pc87360_data *data = i2c_get_clientdata(client);
	u8 i;

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		dev_dbg(&client->dev, "Data update\n");

		/* Fans */
		for (i = 0; i < data->fannr; i++) {
			if (FAN_CONFIG_MONITOR(data->fan_conf, i)) {
				data->fan_status[i] =
					pc87360_read_value(data, LD_FAN,
					NO_BANK, PC87360_REG_FAN_STATUS(i));
				data->fan[i] = pc87360_read_value(data, LD_FAN,
					       NO_BANK, PC87360_REG_FAN(i));
				data->fan_min[i] = pc87360_read_value(data,
						   LD_FAN, NO_BANK,
						   PC87360_REG_FAN_MIN(i));
				/* Change clock divider if needed */
				pc87360_autodiv(client, i);
				/* Clear bits and write new divider */
				pc87360_write_value(data, LD_FAN, NO_BANK,
						    PC87360_REG_FAN_STATUS(i),
						    data->fan_status[i]);
			}
			if (FAN_CONFIG_CONTROL(data->fan_conf, i))
				data->pwm[i] = pc87360_read_value(data, LD_FAN,
					       NO_BANK, PC87360_REG_PWM(i));
		}

		/* Voltages */
		for (i = 0; i < data->innr; i++) {
			data->in_status[i] = pc87360_read_value(data, LD_IN, i,
					     PC87365_REG_IN_STATUS);
			/* Clear bits */
			pc87360_write_value(data, LD_IN, i,
					    PC87365_REG_IN_STATUS,
					    data->in_status[i]);
			if ((data->in_status[i] & 0x81) == 0x81) {
				data->in[i] = pc87360_read_value(data, LD_IN,
					      i, PC87365_REG_IN);
			}
			if (data->in_status[i] & 0x01) {
				data->in_min[i] = pc87360_read_value(data,
						  LD_IN, i,
						  PC87365_REG_IN_MIN);
				data->in_max[i] = pc87360_read_value(data,
						  LD_IN, i,
						  PC87365_REG_IN_MAX);
				if (i >= 11)
					data->in_crit[i-11] =
						pc87360_read_value(data, LD_IN,
						i, PC87365_REG_TEMP_CRIT);
			}
		}
		if (data->innr) {
			data->in_alarms = pc87360_read_value(data, LD_IN,
					  NO_BANK, PC87365_REG_IN_ALARMS1)
					| ((pc87360_read_value(data, LD_IN,
					    NO_BANK, PC87365_REG_IN_ALARMS2)
					    & 0x07) << 8);
			data->vid = (data->vid_conf & 0xE0) ?
				    pc87360_read_value(data, LD_IN,
				    NO_BANK, PC87365_REG_VID) : 0x1F;
		}

		/* Temperatures */
		for (i = 0; i < data->tempnr; i++) {
			data->temp_status[i] = pc87360_read_value(data,
					       LD_TEMP, i,
					       PC87365_REG_TEMP_STATUS);
			/* Clear bits */
			pc87360_write_value(data, LD_TEMP, i,
					    PC87365_REG_TEMP_STATUS,
					    data->temp_status[i]);
			if ((data->temp_status[i] & 0x81) == 0x81) {
				data->temp[i] = pc87360_read_value(data,
						LD_TEMP, i,
						PC87365_REG_TEMP);
			}
			if (data->temp_status[i] & 0x01) {
				data->temp_min[i] = pc87360_read_value(data,
						    LD_TEMP, i,
						    PC87365_REG_TEMP_MIN);
				data->temp_max[i] = pc87360_read_value(data,
						    LD_TEMP, i,
						    PC87365_REG_TEMP_MAX);
				data->temp_crit[i] = pc87360_read_value(data,
						     LD_TEMP, i,
						     PC87365_REG_TEMP_CRIT);
			}
		}
		if (data->tempnr) {
			data->temp_alarms = pc87360_read_value(data, LD_TEMP,
					    NO_BANK, PC87365_REG_TEMP_ALARMS)
					    & 0x3F;
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init pc87360_init(void)
{
	int i;

	if (pc87360_find(0x2e, &devid, extra_isa)
	 && pc87360_find(0x4e, &devid, extra_isa)) {
		printk(KERN_WARNING "pc87360: PC8736x not detected, "
		       "module not inserted.\n");
		return -ENODEV;
	}

	/* Arbitrarily pick one of the addresses */
	for (i = 0; i < 3; i++) {
		if (extra_isa[i] != 0x0000) {
			address = extra_isa[i];
			break;
		}
	}

	if (address == 0x0000) {
		printk(KERN_WARNING "pc87360: No active logical device, "
		       "module not inserted.\n");
		return -ENODEV;
	}

	return i2c_isa_add_driver(&pc87360_driver);
}

static void __exit pc87360_exit(void)
{
	i2c_isa_del_driver(&pc87360_driver);
}


MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("PC8736x hardware monitor");
MODULE_LICENSE("GPL");

module_init(pc87360_init);
module_exit(pc87360_exit);
