/*
 *  pc87360.c - Part of lm_sensors, Linux kernel modules
 *              for hardware monitoring
 *  Copyright (C) 2004, 2007 Jean Delvare <jdelvare@suse.de>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/acpi.h>
#include <linux/io.h>

static u8 devid;
static struct platform_device *pdev;
static unsigned short extra_isa[3];
static u8 confreg[4];

static int init = 1;
module_param(init, int, 0);
MODULE_PARM_DESC(init,
"Chip initialization level:\n"
" 0: None\n"
"*1: Forcibly enable internal voltage and temperature channels, except in9\n"
" 2: Forcibly enable all voltage and temperature channels, except in9\n"
" 3: Forcibly enable all voltage and temperature channels, including in9");

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

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
#define LDNI_MAX 3
static const u8 logdev[LDNI_MAX] = { FSCM, VLM, TMS };

#define LD_FAN		0
#define LD_IN		1
#define LD_TEMP		2

static inline void superio_outb(int sioaddr, int reg, int val)
{
	outb(reg, sioaddr);
	outb(val, sioaddr + 1);
}

static inline int superio_inb(int sioaddr, int reg)
{
	outb(reg, sioaddr);
	return inb(sioaddr + 1);
}

static inline void superio_exit(int sioaddr)
{
	outb(0x02, sioaddr);
	outb(0x02, sioaddr + 1);
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

#define FAN_FROM_REG(val, div)		((val) == 0 ? 0 : \
					 480000 / ((val) * (div)))
#define FAN_TO_REG(val, div)		((val) <= 100 ? 0 : \
					 480000 / ((val) * (div)))
#define FAN_DIV_FROM_REG(val)		(1 << (((val) >> 5) & 0x03))
#define FAN_STATUS_FROM_REG(val)	((val) & 0x07)

#define FAN_CONFIG_MONITOR(val, nr)	(((val) >> (2 + (nr) * 3)) & 1)
#define FAN_CONFIG_CONTROL(val, nr)	(((val) >> (3 + (nr) * 3)) & 1)
#define FAN_CONFIG_INVERT(val, nr)	(((val) >> (4 + (nr) * 3)) & 1)

#define PWM_FROM_REG(val, inv)		((inv) ? 255 - (val) : (val))
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

#define IN_FROM_REG(val, ref)		(((val) * (ref) + 128) / 256)
#define IN_TO_REG(val, ref)		((val) < 0 ? 0 : \
					 (val) * 256 >= (ref) * 255 ? 255 : \
					 ((val) * 256 + (ref) / 2) / (ref))

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
 * Device data
 */

struct pc87360_data {
	const char *name;
	struct device *hwmon_dev;
	struct mutex lock;
	struct mutex update_lock;
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

static int pc87360_probe(struct platform_device *pdev);
static int pc87360_remove(struct platform_device *pdev);

static int pc87360_read_value(struct pc87360_data *data, u8 ldi, u8 bank,
			      u8 reg);
static void pc87360_write_value(struct pc87360_data *data, u8 ldi, u8 bank,
				u8 reg, u8 value);
static void pc87360_init_device(struct platform_device *pdev,
				int use_thermistors);
static struct pc87360_data *pc87360_update_device(struct device *dev);

/*
 * Driver data
 */

static struct platform_driver pc87360_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "pc87360",
	},
	.probe		= pc87360_probe,
	.remove		= pc87360_remove,
};

/*
 * Sysfs stuff
 */

static ssize_t show_fan_input(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", FAN_FROM_REG(data->fan[attr->index],
		       FAN_DIV_FROM_REG(data->fan_status[attr->index])));
}
static ssize_t show_fan_min(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", FAN_FROM_REG(data->fan_min[attr->index],
		       FAN_DIV_FROM_REG(data->fan_status[attr->index])));
}
static ssize_t show_fan_div(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n",
		       FAN_DIV_FROM_REG(data->fan_status[attr->index]));
}
static ssize_t show_fan_status(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n",
		       FAN_STATUS_FROM_REG(data->fan_status[attr->index]));
}
static ssize_t set_fan_min(struct device *dev,
			   struct device_attribute *devattr, const char *buf,
	size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long fan_min;
	int err;

	err = kstrtol(buf, 10, &fan_min);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	fan_min = FAN_TO_REG(fan_min,
			     FAN_DIV_FROM_REG(data->fan_status[attr->index]));

	/* If it wouldn't fit, change clock divisor */
	while (fan_min > 255
	    && (data->fan_status[attr->index] & 0x60) != 0x60) {
		fan_min >>= 1;
		data->fan[attr->index] >>= 1;
		data->fan_status[attr->index] += 0x20;
	}
	data->fan_min[attr->index] = fan_min > 255 ? 255 : fan_min;
	pc87360_write_value(data, LD_FAN, NO_BANK,
			    PC87360_REG_FAN_MIN(attr->index),
			    data->fan_min[attr->index]);

	/* Write new divider, preserve alarm bits */
	pc87360_write_value(data, LD_FAN, NO_BANK,
			    PC87360_REG_FAN_STATUS(attr->index),
			    data->fan_status[attr->index] & 0xF9);
	mutex_unlock(&data->update_lock);

	return count;
}

static struct sensor_device_attribute fan_input[] = {
	SENSOR_ATTR(fan1_input, S_IRUGO, show_fan_input, NULL, 0),
	SENSOR_ATTR(fan2_input, S_IRUGO, show_fan_input, NULL, 1),
	SENSOR_ATTR(fan3_input, S_IRUGO, show_fan_input, NULL, 2),
};
static struct sensor_device_attribute fan_status[] = {
	SENSOR_ATTR(fan1_status, S_IRUGO, show_fan_status, NULL, 0),
	SENSOR_ATTR(fan2_status, S_IRUGO, show_fan_status, NULL, 1),
	SENSOR_ATTR(fan3_status, S_IRUGO, show_fan_status, NULL, 2),
};
static struct sensor_device_attribute fan_div[] = {
	SENSOR_ATTR(fan1_div, S_IRUGO, show_fan_div, NULL, 0),
	SENSOR_ATTR(fan2_div, S_IRUGO, show_fan_div, NULL, 1),
	SENSOR_ATTR(fan3_div, S_IRUGO, show_fan_div, NULL, 2),
};
static struct sensor_device_attribute fan_min[] = {
	SENSOR_ATTR(fan1_min, S_IWUSR | S_IRUGO, show_fan_min, set_fan_min, 0),
	SENSOR_ATTR(fan2_min, S_IWUSR | S_IRUGO, show_fan_min, set_fan_min, 1),
	SENSOR_ATTR(fan3_min, S_IWUSR | S_IRUGO, show_fan_min, set_fan_min, 2),
};

#define FAN_UNIT_ATTRS(X)		\
{	&fan_input[X].dev_attr.attr,	\
	&fan_status[X].dev_attr.attr,	\
	&fan_div[X].dev_attr.attr,	\
	&fan_min[X].dev_attr.attr,	\
	NULL				\
}

static ssize_t show_pwm(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n",
		       PWM_FROM_REG(data->pwm[attr->index],
				    FAN_CONFIG_INVERT(data->fan_conf,
						      attr->index)));
}
static ssize_t set_pwm(struct device *dev, struct device_attribute *devattr,
		       const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->pwm[attr->index] = PWM_TO_REG(val,
			      FAN_CONFIG_INVERT(data->fan_conf, attr->index));
	pc87360_write_value(data, LD_FAN, NO_BANK, PC87360_REG_PWM(attr->index),
			    data->pwm[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static struct sensor_device_attribute pwm[] = {
	SENSOR_ATTR(pwm1, S_IWUSR | S_IRUGO, show_pwm, set_pwm, 0),
	SENSOR_ATTR(pwm2, S_IWUSR | S_IRUGO, show_pwm, set_pwm, 1),
	SENSOR_ATTR(pwm3, S_IWUSR | S_IRUGO, show_pwm, set_pwm, 2),
};

static struct attribute *pc8736x_fan_attr[][5] = {
	FAN_UNIT_ATTRS(0),
	FAN_UNIT_ATTRS(1),
	FAN_UNIT_ATTRS(2)
};

static const struct attribute_group pc8736x_fan_attr_group[] = {
	{ .attrs = pc8736x_fan_attr[0], },
	{ .attrs = pc8736x_fan_attr[1], },
	{ .attrs = pc8736x_fan_attr[2], },
};

static ssize_t show_in_input(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in[attr->index],
		       data->in_vref));
}
static ssize_t show_in_min(struct device *dev,
			   struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_min[attr->index],
		       data->in_vref));
}
static ssize_t show_in_max(struct device *dev,
			   struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_max[attr->index],
		       data->in_vref));
}
static ssize_t show_in_status(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", data->in_status[attr->index]);
}
static ssize_t set_in_min(struct device *dev, struct device_attribute *devattr,
			  const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[attr->index] = IN_TO_REG(val, data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_IN_MIN,
			    data->in_min[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_in_max(struct device *dev, struct device_attribute *devattr,
			  const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[attr->index] = IN_TO_REG(val,
			       data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_IN_MAX,
			    data->in_max[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static struct sensor_device_attribute in_input[] = {
	SENSOR_ATTR(in0_input, S_IRUGO, show_in_input, NULL, 0),
	SENSOR_ATTR(in1_input, S_IRUGO, show_in_input, NULL, 1),
	SENSOR_ATTR(in2_input, S_IRUGO, show_in_input, NULL, 2),
	SENSOR_ATTR(in3_input, S_IRUGO, show_in_input, NULL, 3),
	SENSOR_ATTR(in4_input, S_IRUGO, show_in_input, NULL, 4),
	SENSOR_ATTR(in5_input, S_IRUGO, show_in_input, NULL, 5),
	SENSOR_ATTR(in6_input, S_IRUGO, show_in_input, NULL, 6),
	SENSOR_ATTR(in7_input, S_IRUGO, show_in_input, NULL, 7),
	SENSOR_ATTR(in8_input, S_IRUGO, show_in_input, NULL, 8),
	SENSOR_ATTR(in9_input, S_IRUGO, show_in_input, NULL, 9),
	SENSOR_ATTR(in10_input, S_IRUGO, show_in_input, NULL, 10),
};
static struct sensor_device_attribute in_status[] = {
	SENSOR_ATTR(in0_status, S_IRUGO, show_in_status, NULL, 0),
	SENSOR_ATTR(in1_status, S_IRUGO, show_in_status, NULL, 1),
	SENSOR_ATTR(in2_status, S_IRUGO, show_in_status, NULL, 2),
	SENSOR_ATTR(in3_status, S_IRUGO, show_in_status, NULL, 3),
	SENSOR_ATTR(in4_status, S_IRUGO, show_in_status, NULL, 4),
	SENSOR_ATTR(in5_status, S_IRUGO, show_in_status, NULL, 5),
	SENSOR_ATTR(in6_status, S_IRUGO, show_in_status, NULL, 6),
	SENSOR_ATTR(in7_status, S_IRUGO, show_in_status, NULL, 7),
	SENSOR_ATTR(in8_status, S_IRUGO, show_in_status, NULL, 8),
	SENSOR_ATTR(in9_status, S_IRUGO, show_in_status, NULL, 9),
	SENSOR_ATTR(in10_status, S_IRUGO, show_in_status, NULL, 10),
};
static struct sensor_device_attribute in_min[] = {
	SENSOR_ATTR(in0_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 0),
	SENSOR_ATTR(in1_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 1),
	SENSOR_ATTR(in2_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 2),
	SENSOR_ATTR(in3_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 3),
	SENSOR_ATTR(in4_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 4),
	SENSOR_ATTR(in5_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 5),
	SENSOR_ATTR(in6_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 6),
	SENSOR_ATTR(in7_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 7),
	SENSOR_ATTR(in8_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 8),
	SENSOR_ATTR(in9_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 9),
	SENSOR_ATTR(in10_min, S_IWUSR | S_IRUGO, show_in_min, set_in_min, 10),
};
static struct sensor_device_attribute in_max[] = {
	SENSOR_ATTR(in0_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 0),
	SENSOR_ATTR(in1_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 1),
	SENSOR_ATTR(in2_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 2),
	SENSOR_ATTR(in3_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 3),
	SENSOR_ATTR(in4_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 4),
	SENSOR_ATTR(in5_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 5),
	SENSOR_ATTR(in6_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 6),
	SENSOR_ATTR(in7_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 7),
	SENSOR_ATTR(in8_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 8),
	SENSOR_ATTR(in9_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 9),
	SENSOR_ATTR(in10_max, S_IWUSR | S_IRUGO, show_in_max, set_in_max, 10),
};

/* (temp & vin) channel status register alarm bits (pdf sec.11.5.12) */
#define CHAN_ALM_MIN	0x02	/* min limit crossed */
#define CHAN_ALM_MAX	0x04	/* max limit exceeded */
#define TEMP_ALM_CRIT	0x08	/* temp crit exceeded (temp only) */

/*
 * show_in_min/max_alarm() reads data from the per-channel status
 * register (sec 11.5.12), not the vin event status registers (sec
 * 11.5.2) that (legacy) show_in_alarm() resds (via data->in_alarms)
 */

static ssize_t show_in_min_alarm(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	unsigned nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%u\n", !!(data->in_status[nr] & CHAN_ALM_MIN));
}
static ssize_t show_in_max_alarm(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	unsigned nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%u\n", !!(data->in_status[nr] & CHAN_ALM_MAX));
}

static struct sensor_device_attribute in_min_alarm[] = {
	SENSOR_ATTR(in0_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 0),
	SENSOR_ATTR(in1_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 1),
	SENSOR_ATTR(in2_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 2),
	SENSOR_ATTR(in3_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 3),
	SENSOR_ATTR(in4_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 4),
	SENSOR_ATTR(in5_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 5),
	SENSOR_ATTR(in6_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 6),
	SENSOR_ATTR(in7_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 7),
	SENSOR_ATTR(in8_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 8),
	SENSOR_ATTR(in9_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 9),
	SENSOR_ATTR(in10_min_alarm, S_IRUGO, show_in_min_alarm, NULL, 10),
};
static struct sensor_device_attribute in_max_alarm[] = {
	SENSOR_ATTR(in0_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 0),
	SENSOR_ATTR(in1_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 1),
	SENSOR_ATTR(in2_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 2),
	SENSOR_ATTR(in3_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 3),
	SENSOR_ATTR(in4_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 4),
	SENSOR_ATTR(in5_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 5),
	SENSOR_ATTR(in6_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 6),
	SENSOR_ATTR(in7_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 7),
	SENSOR_ATTR(in8_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 8),
	SENSOR_ATTR(in9_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 9),
	SENSOR_ATTR(in10_max_alarm, S_IRUGO, show_in_max_alarm, NULL, 10),
};

#define VIN_UNIT_ATTRS(X) \
	&in_input[X].dev_attr.attr,	\
	&in_status[X].dev_attr.attr,	\
	&in_min[X].dev_attr.attr,	\
	&in_max[X].dev_attr.attr,	\
	&in_min_alarm[X].dev_attr.attr,	\
	&in_max_alarm[X].dev_attr.attr

static ssize_t show_vid(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

static ssize_t show_vrm(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct pc87360_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", data->vrm);
}
static ssize_t set_vrm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct pc87360_data *data = dev_get_drvdata(dev);
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
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm, set_vrm);

static ssize_t show_in_alarms(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", data->in_alarms);
}
static DEVICE_ATTR(alarms_in, S_IRUGO, show_in_alarms, NULL);

static struct attribute *pc8736x_vin_attr_array[] = {
	VIN_UNIT_ATTRS(0),
	VIN_UNIT_ATTRS(1),
	VIN_UNIT_ATTRS(2),
	VIN_UNIT_ATTRS(3),
	VIN_UNIT_ATTRS(4),
	VIN_UNIT_ATTRS(5),
	VIN_UNIT_ATTRS(6),
	VIN_UNIT_ATTRS(7),
	VIN_UNIT_ATTRS(8),
	VIN_UNIT_ATTRS(9),
	VIN_UNIT_ATTRS(10),
	&dev_attr_cpu0_vid.attr,
	&dev_attr_vrm.attr,
	&dev_attr_alarms_in.attr,
	NULL
};
static const struct attribute_group pc8736x_vin_group = {
	.attrs = pc8736x_vin_attr_array,
};

static ssize_t show_therm_input(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in[attr->index],
		       data->in_vref));
}
static ssize_t show_therm_min(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_min[attr->index],
		       data->in_vref));
}
static ssize_t show_therm_max(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_max[attr->index],
		       data->in_vref));
}
static ssize_t show_therm_crit(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_crit[attr->index-11],
		       data->in_vref));
}
static ssize_t show_therm_status(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", data->in_status[attr->index]);
}

static ssize_t set_therm_min(struct device *dev,
			     struct device_attribute *devattr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[attr->index] = IN_TO_REG(val, data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_TEMP_MIN,
			    data->in_min[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_therm_max(struct device *dev,
			     struct device_attribute *devattr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[attr->index] = IN_TO_REG(val, data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_TEMP_MAX,
			    data->in_max[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_therm_crit(struct device *dev,
			      struct device_attribute *devattr,
			      const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_crit[attr->index-11] = IN_TO_REG(val, data->in_vref);
	pc87360_write_value(data, LD_IN, attr->index, PC87365_REG_TEMP_CRIT,
			    data->in_crit[attr->index-11]);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * the +11 term below reflects the fact that VLM units 11,12,13 are
 * used in the chip to measure voltage across the thermistors
 */
static struct sensor_device_attribute therm_input[] = {
	SENSOR_ATTR(temp4_input, S_IRUGO, show_therm_input, NULL, 0 + 11),
	SENSOR_ATTR(temp5_input, S_IRUGO, show_therm_input, NULL, 1 + 11),
	SENSOR_ATTR(temp6_input, S_IRUGO, show_therm_input, NULL, 2 + 11),
};
static struct sensor_device_attribute therm_status[] = {
	SENSOR_ATTR(temp4_status, S_IRUGO, show_therm_status, NULL, 0 + 11),
	SENSOR_ATTR(temp5_status, S_IRUGO, show_therm_status, NULL, 1 + 11),
	SENSOR_ATTR(temp6_status, S_IRUGO, show_therm_status, NULL, 2 + 11),
};
static struct sensor_device_attribute therm_min[] = {
	SENSOR_ATTR(temp4_min, S_IRUGO | S_IWUSR,
		    show_therm_min, set_therm_min, 0 + 11),
	SENSOR_ATTR(temp5_min, S_IRUGO | S_IWUSR,
		    show_therm_min, set_therm_min, 1 + 11),
	SENSOR_ATTR(temp6_min, S_IRUGO | S_IWUSR,
		    show_therm_min, set_therm_min, 2 + 11),
};
static struct sensor_device_attribute therm_max[] = {
	SENSOR_ATTR(temp4_max, S_IRUGO | S_IWUSR,
		    show_therm_max, set_therm_max, 0 + 11),
	SENSOR_ATTR(temp5_max, S_IRUGO | S_IWUSR,
		    show_therm_max, set_therm_max, 1 + 11),
	SENSOR_ATTR(temp6_max, S_IRUGO | S_IWUSR,
		    show_therm_max, set_therm_max, 2 + 11),
};
static struct sensor_device_attribute therm_crit[] = {
	SENSOR_ATTR(temp4_crit, S_IRUGO | S_IWUSR,
		    show_therm_crit, set_therm_crit, 0 + 11),
	SENSOR_ATTR(temp5_crit, S_IRUGO | S_IWUSR,
		    show_therm_crit, set_therm_crit, 1 + 11),
	SENSOR_ATTR(temp6_crit, S_IRUGO | S_IWUSR,
		    show_therm_crit, set_therm_crit, 2 + 11),
};

/*
 * show_therm_min/max_alarm() reads data from the per-channel voltage
 * status register (sec 11.5.12)
 */

static ssize_t show_therm_min_alarm(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	unsigned nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%u\n", !!(data->in_status[nr] & CHAN_ALM_MIN));
}
static ssize_t show_therm_max_alarm(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	unsigned nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%u\n", !!(data->in_status[nr] & CHAN_ALM_MAX));
}
static ssize_t show_therm_crit_alarm(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	unsigned nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%u\n", !!(data->in_status[nr] & TEMP_ALM_CRIT));
}

static struct sensor_device_attribute therm_min_alarm[] = {
	SENSOR_ATTR(temp4_min_alarm, S_IRUGO,
		    show_therm_min_alarm, NULL, 0 + 11),
	SENSOR_ATTR(temp5_min_alarm, S_IRUGO,
		    show_therm_min_alarm, NULL, 1 + 11),
	SENSOR_ATTR(temp6_min_alarm, S_IRUGO,
		    show_therm_min_alarm, NULL, 2 + 11),
};
static struct sensor_device_attribute therm_max_alarm[] = {
	SENSOR_ATTR(temp4_max_alarm, S_IRUGO,
		    show_therm_max_alarm, NULL, 0 + 11),
	SENSOR_ATTR(temp5_max_alarm, S_IRUGO,
		    show_therm_max_alarm, NULL, 1 + 11),
	SENSOR_ATTR(temp6_max_alarm, S_IRUGO,
		    show_therm_max_alarm, NULL, 2 + 11),
};
static struct sensor_device_attribute therm_crit_alarm[] = {
	SENSOR_ATTR(temp4_crit_alarm, S_IRUGO,
		    show_therm_crit_alarm, NULL, 0 + 11),
	SENSOR_ATTR(temp5_crit_alarm, S_IRUGO,
		    show_therm_crit_alarm, NULL, 1 + 11),
	SENSOR_ATTR(temp6_crit_alarm, S_IRUGO,
		    show_therm_crit_alarm, NULL, 2 + 11),
};

#define THERM_UNIT_ATTRS(X) \
	&therm_input[X].dev_attr.attr,	\
	&therm_status[X].dev_attr.attr,	\
	&therm_min[X].dev_attr.attr,	\
	&therm_max[X].dev_attr.attr,	\
	&therm_crit[X].dev_attr.attr,	\
	&therm_min_alarm[X].dev_attr.attr, \
	&therm_max_alarm[X].dev_attr.attr, \
	&therm_crit_alarm[X].dev_attr.attr

static struct attribute *pc8736x_therm_attr_array[] = {
	THERM_UNIT_ATTRS(0),
	THERM_UNIT_ATTRS(1),
	THERM_UNIT_ATTRS(2),
	NULL
};
static const struct attribute_group pc8736x_therm_group = {
	.attrs = pc8736x_therm_attr_array,
};

static ssize_t show_temp_input(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t show_temp_min(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_min[attr->index]));
}

static ssize_t show_temp_max(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[attr->index]));
}

static ssize_t show_temp_crit(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n",
		       TEMP_FROM_REG(data->temp_crit[attr->index]));
}

static ssize_t show_temp_status(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_status[attr->index]);
}

static ssize_t set_temp_min(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_min[attr->index] = TEMP_TO_REG(val);
	pc87360_write_value(data, LD_TEMP, attr->index, PC87365_REG_TEMP_MIN,
			    data->temp_min[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_temp_max(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_max[attr->index] = TEMP_TO_REG(val);
	pc87360_write_value(data, LD_TEMP, attr->index, PC87365_REG_TEMP_MAX,
			    data->temp_max[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_temp_crit(struct device *dev,
			     struct device_attribute *devattr, const char *buf,
			     size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct pc87360_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_crit[attr->index] = TEMP_TO_REG(val);
	pc87360_write_value(data, LD_TEMP, attr->index, PC87365_REG_TEMP_CRIT,
			    data->temp_crit[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static struct sensor_device_attribute temp_input[] = {
	SENSOR_ATTR(temp1_input, S_IRUGO, show_temp_input, NULL, 0),
	SENSOR_ATTR(temp2_input, S_IRUGO, show_temp_input, NULL, 1),
	SENSOR_ATTR(temp3_input, S_IRUGO, show_temp_input, NULL, 2),
};
static struct sensor_device_attribute temp_status[] = {
	SENSOR_ATTR(temp1_status, S_IRUGO, show_temp_status, NULL, 0),
	SENSOR_ATTR(temp2_status, S_IRUGO, show_temp_status, NULL, 1),
	SENSOR_ATTR(temp3_status, S_IRUGO, show_temp_status, NULL, 2),
};
static struct sensor_device_attribute temp_min[] = {
	SENSOR_ATTR(temp1_min, S_IRUGO | S_IWUSR,
		    show_temp_min, set_temp_min, 0),
	SENSOR_ATTR(temp2_min, S_IRUGO | S_IWUSR,
		    show_temp_min, set_temp_min, 1),
	SENSOR_ATTR(temp3_min, S_IRUGO | S_IWUSR,
		    show_temp_min, set_temp_min, 2),
};
static struct sensor_device_attribute temp_max[] = {
	SENSOR_ATTR(temp1_max, S_IRUGO | S_IWUSR,
		    show_temp_max, set_temp_max, 0),
	SENSOR_ATTR(temp2_max, S_IRUGO | S_IWUSR,
		    show_temp_max, set_temp_max, 1),
	SENSOR_ATTR(temp3_max, S_IRUGO | S_IWUSR,
		    show_temp_max, set_temp_max, 2),
};
static struct sensor_device_attribute temp_crit[] = {
	SENSOR_ATTR(temp1_crit, S_IRUGO | S_IWUSR,
		    show_temp_crit, set_temp_crit, 0),
	SENSOR_ATTR(temp2_crit, S_IRUGO | S_IWUSR,
		    show_temp_crit, set_temp_crit, 1),
	SENSOR_ATTR(temp3_crit, S_IRUGO | S_IWUSR,
		    show_temp_crit, set_temp_crit, 2),
};

static ssize_t show_temp_alarms(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	return sprintf(buf, "%u\n", data->temp_alarms);
}

static DEVICE_ATTR(alarms_temp, S_IRUGO, show_temp_alarms, NULL);

/*
 * show_temp_min/max_alarm() reads data from the per-channel status
 * register (sec 12.3.7), not the temp event status registers (sec
 * 12.3.2) that show_temp_alarm() reads (via data->temp_alarms)
 */

static ssize_t show_temp_min_alarm(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	unsigned nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%u\n", !!(data->temp_status[nr] & CHAN_ALM_MIN));
}

static ssize_t show_temp_max_alarm(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	unsigned nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%u\n", !!(data->temp_status[nr] & CHAN_ALM_MAX));
}

static ssize_t show_temp_crit_alarm(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	unsigned nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%u\n", !!(data->temp_status[nr] & TEMP_ALM_CRIT));
}

static struct sensor_device_attribute temp_min_alarm[] = {
	SENSOR_ATTR(temp1_min_alarm, S_IRUGO, show_temp_min_alarm, NULL, 0),
	SENSOR_ATTR(temp2_min_alarm, S_IRUGO, show_temp_min_alarm, NULL, 1),
	SENSOR_ATTR(temp3_min_alarm, S_IRUGO, show_temp_min_alarm, NULL, 2),
};

static struct sensor_device_attribute temp_max_alarm[] = {
	SENSOR_ATTR(temp1_max_alarm, S_IRUGO, show_temp_max_alarm, NULL, 0),
	SENSOR_ATTR(temp2_max_alarm, S_IRUGO, show_temp_max_alarm, NULL, 1),
	SENSOR_ATTR(temp3_max_alarm, S_IRUGO, show_temp_max_alarm, NULL, 2),
};

static struct sensor_device_attribute temp_crit_alarm[] = {
	SENSOR_ATTR(temp1_crit_alarm, S_IRUGO, show_temp_crit_alarm, NULL, 0),
	SENSOR_ATTR(temp2_crit_alarm, S_IRUGO, show_temp_crit_alarm, NULL, 1),
	SENSOR_ATTR(temp3_crit_alarm, S_IRUGO, show_temp_crit_alarm, NULL, 2),
};

#define TEMP_FAULT	0x40	/* open diode */
static ssize_t show_temp_fault(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = pc87360_update_device(dev);
	unsigned nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%u\n", !!(data->temp_status[nr] & TEMP_FAULT));
}
static struct sensor_device_attribute temp_fault[] = {
	SENSOR_ATTR(temp1_fault, S_IRUGO, show_temp_fault, NULL, 0),
	SENSOR_ATTR(temp2_fault, S_IRUGO, show_temp_fault, NULL, 1),
	SENSOR_ATTR(temp3_fault, S_IRUGO, show_temp_fault, NULL, 2),
};

#define TEMP_UNIT_ATTRS(X)			\
{	&temp_input[X].dev_attr.attr,		\
	&temp_status[X].dev_attr.attr,		\
	&temp_min[X].dev_attr.attr,		\
	&temp_max[X].dev_attr.attr,		\
	&temp_crit[X].dev_attr.attr,		\
	&temp_min_alarm[X].dev_attr.attr,	\
	&temp_max_alarm[X].dev_attr.attr,	\
	&temp_crit_alarm[X].dev_attr.attr,	\
	&temp_fault[X].dev_attr.attr,		\
	NULL					\
}

static struct attribute *pc8736x_temp_attr[][10] = {
	TEMP_UNIT_ATTRS(0),
	TEMP_UNIT_ATTRS(1),
	TEMP_UNIT_ATTRS(2)
};

static const struct attribute_group pc8736x_temp_attr_group[] = {
	{ .attrs = pc8736x_temp_attr[0] },
	{ .attrs = pc8736x_temp_attr[1] },
	{ .attrs = pc8736x_temp_attr[2] }
};

static ssize_t show_name(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct pc87360_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", data->name);
}

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

/*
 * Device detection, registration and update
 */

static int __init pc87360_find(int sioaddr, u8 *devid,
			       unsigned short *addresses)
{
	u16 val;
	int i;
	int nrdev; /* logical device count */

	/* No superio_enter */

	/* Identify device */
	val = force_id ? force_id : superio_inb(sioaddr, DEVID);
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
			pr_info("Device 0x%02x not activated\n", logdev[i]);
			continue;
		}

		val = (superio_inb(sioaddr, BASE) << 8)
		    | superio_inb(sioaddr, BASE + 1);
		if (!val) {
			pr_info("Base address not set for device 0x%02x\n",
				logdev[i]);
			continue;
		}

		addresses[i] = val;

		if (i == 0) { /* Fans */
			confreg[0] = superio_inb(sioaddr, 0xF0);
			confreg[1] = superio_inb(sioaddr, 0xF1);

			pr_debug("Fan %d: mon=%d ctrl=%d inv=%d\n", 1,
				 (confreg[0] >> 2) & 1, (confreg[0] >> 3) & 1,
				 (confreg[0] >> 4) & 1);
			pr_debug("Fan %d: mon=%d ctrl=%d inv=%d\n", 2,
				 (confreg[0] >> 5) & 1, (confreg[0] >> 6) & 1,
				 (confreg[0] >> 7) & 1);
			pr_debug("Fan %d: mon=%d ctrl=%d inv=%d\n", 3,
				 confreg[1] & 1, (confreg[1] >> 1) & 1,
				 (confreg[1] >> 2) & 1);
		} else if (i == 1) { /* Voltages */
			/* Are we using thermistors? */
			if (*devid == 0xE9) { /* PC87366 */
				/*
				 * These registers are not logical-device
				 * specific, just that we won't need them if
				 * we don't use the VLM device
				 */
				confreg[2] = superio_inb(sioaddr, 0x2B);
				confreg[3] = superio_inb(sioaddr, 0x25);

				if (confreg[2] & 0x40) {
					pr_info("Using thermistors for temperature monitoring\n");
				}
				if (confreg[3] & 0xE0) {
					pr_info("VID inputs routed (mode %u)\n",
						confreg[3] >> 5);
				}
			}
		}
	}

	superio_exit(sioaddr);
	return 0;
}

static void pc87360_remove_files(struct device *dev)
{
	int i;

	device_remove_file(dev, &dev_attr_name);
	device_remove_file(dev, &dev_attr_alarms_temp);
	for (i = 0; i < ARRAY_SIZE(pc8736x_temp_attr_group); i++)
		sysfs_remove_group(&dev->kobj, &pc8736x_temp_attr_group[i]);
	for (i = 0; i < ARRAY_SIZE(pc8736x_fan_attr_group); i++) {
		sysfs_remove_group(&pdev->dev.kobj, &pc8736x_fan_attr_group[i]);
		device_remove_file(dev, &pwm[i].dev_attr);
	}
	sysfs_remove_group(&dev->kobj, &pc8736x_therm_group);
	sysfs_remove_group(&dev->kobj, &pc8736x_vin_group);
}

static int pc87360_probe(struct platform_device *pdev)
{
	int i;
	struct pc87360_data *data;
	int err = 0;
	const char *name;
	int use_thermistors = 0;
	struct device *dev = &pdev->dev;

	data = devm_kzalloc(dev, sizeof(struct pc87360_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	switch (devid) {
	default:
		name = "pc87360";
		data->fannr = 2;
		break;
	case 0xe8:
		name = "pc87363";
		data->fannr = 2;
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

	data->name = name;
	mutex_init(&data->lock);
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	for (i = 0; i < LDNI_MAX; i++) {
		data->address[i] = extra_isa[i];
		if (data->address[i]
		 && !devm_request_region(dev, extra_isa[i], PC87360_EXTENT,
					 pc87360_driver.driver.name)) {
			dev_err(dev,
				"Region 0x%x-0x%x already in use!\n",
				extra_isa[i], extra_isa[i]+PC87360_EXTENT-1);
			return -EBUSY;
		}
	}

	/* Retrieve the fans configuration from Super-I/O space */
	if (data->fannr)
		data->fan_conf = confreg[0] | (confreg[1] << 8);

	/*
	 * Use the correct reference voltage
	 * Unless both the VLM and the TMS logical devices agree to
	 * use an external Vref, the internal one is used.
	 */
	if (data->innr) {
		i = pc87360_read_value(data, LD_IN, NO_BANK,
				       PC87365_REG_IN_CONFIG);
		if (data->tempnr) {
			i &= pc87360_read_value(data, LD_TEMP, NO_BANK,
						PC87365_REG_TEMP_CONFIG);
		}
		data->in_vref = (i&0x02) ? 3025 : 2966;
		dev_dbg(dev, "Using %s reference voltage\n",
			(i&0x02) ? "external" : "internal");

		data->vid_conf = confreg[3];
		data->vrm = vid_which_vrm();
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

		pc87360_init_device(pdev, use_thermistors);
	}

	/* Register all-or-nothing sysfs groups */

	if (data->innr) {
		err = sysfs_create_group(&dev->kobj, &pc8736x_vin_group);
		if (err)
			goto error;
	}

	if (data->innr == 14) {
		err = sysfs_create_group(&dev->kobj, &pc8736x_therm_group);
		if (err)
			goto error;
	}

	/* create device attr-files for varying sysfs groups */

	if (data->tempnr) {
		for (i = 0; i < data->tempnr; i++) {
			err = sysfs_create_group(&dev->kobj,
						 &pc8736x_temp_attr_group[i]);
			if (err)
				goto error;
		}
		err = device_create_file(dev, &dev_attr_alarms_temp);
		if (err)
			goto error;
	}

	for (i = 0; i < data->fannr; i++) {
		if (FAN_CONFIG_MONITOR(data->fan_conf, i)) {
			err = sysfs_create_group(&dev->kobj,
						 &pc8736x_fan_attr_group[i]);
			if (err)
				goto error;
		}
		if (FAN_CONFIG_CONTROL(data->fan_conf, i)) {
			err = device_create_file(dev, &pwm[i].dev_attr);
			if (err)
				goto error;
		}
	}

	err = device_create_file(dev, &dev_attr_name);
	if (err)
		goto error;

	data->hwmon_dev = hwmon_device_register(dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto error;
	}
	return 0;

error:
	pc87360_remove_files(dev);
	return err;
}

static int pc87360_remove(struct platform_device *pdev)
{
	struct pc87360_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	pc87360_remove_files(&pdev->dev);

	return 0;
}

/*
 * ldi is the logical device index
 * bank is for voltages and temperatures only
 */
static int pc87360_read_value(struct pc87360_data *data, u8 ldi, u8 bank,
			      u8 reg)
{
	int res;

	mutex_lock(&(data->lock));
	if (bank != NO_BANK)
		outb_p(bank, data->address[ldi] + PC87365_REG_BANK);
	res = inb_p(data->address[ldi] + reg);
	mutex_unlock(&(data->lock));

	return res;
}

static void pc87360_write_value(struct pc87360_data *data, u8 ldi, u8 bank,
				u8 reg, u8 value)
{
	mutex_lock(&(data->lock));
	if (bank != NO_BANK)
		outb_p(bank, data->address[ldi] + PC87365_REG_BANK);
	outb_p(value, data->address[ldi] + reg);
	mutex_unlock(&(data->lock));
}

/* (temp & vin) channel conversion status register flags (pdf sec.11.5.12) */
#define CHAN_CNVRTD	0x80	/* new data ready */
#define CHAN_ENA	0x01	/* enabled channel (temp or vin) */
#define CHAN_ALM_ENA	0x10	/* propagate to alarms-reg ?? (chk val!) */
#define CHAN_READY	(CHAN_ENA|CHAN_CNVRTD) /* sample ready mask */

#define TEMP_OTS_OE	0x20	/* OTS Output Enable */
#define VIN_RW1C_MASK	(CHAN_READY|CHAN_ALM_MAX|CHAN_ALM_MIN)   /* 0x87 */
#define TEMP_RW1C_MASK	(VIN_RW1C_MASK|TEMP_ALM_CRIT|TEMP_FAULT) /* 0xCF */

static void pc87360_init_device(struct platform_device *pdev,
				int use_thermistors)
{
	struct pc87360_data *data = platform_get_drvdata(pdev);
	int i, nr;
	const u8 init_in[14] = { 2, 2, 2, 2, 2, 2, 2, 1, 1, 3, 1, 2, 2, 2 };
	const u8 init_temp[3] = { 2, 2, 1 };
	u8 reg;

	if (init >= 2 && data->innr) {
		reg = pc87360_read_value(data, LD_IN, NO_BANK,
					 PC87365_REG_IN_CONVRATE);
		dev_info(&pdev->dev,
			 "VLM conversion set to 1s period, 160us delay\n");
		pc87360_write_value(data, LD_IN, NO_BANK,
				    PC87365_REG_IN_CONVRATE,
				    (reg & 0xC0) | 0x11);
	}

	nr = data->innr < 11 ? data->innr : 11;
	for (i = 0; i < nr; i++) {
		reg = pc87360_read_value(data, LD_IN, i,
					 PC87365_REG_IN_STATUS);
		dev_dbg(&pdev->dev, "bios in%d status:0x%02x\n", i, reg);
		if (init >= init_in[i]) {
			/* Forcibly enable voltage channel */
			if (!(reg & CHAN_ENA)) {
				dev_dbg(&pdev->dev, "Forcibly enabling in%d\n",
					i);
				pc87360_write_value(data, LD_IN, i,
						    PC87365_REG_IN_STATUS,
						    (reg & 0x68) | 0x87);
			}
		}
	}

	/*
	 * We can't blindly trust the Super-I/O space configuration bit,
	 * most BIOS won't set it properly
	 */
	dev_dbg(&pdev->dev, "bios thermistors:%d\n", use_thermistors);
	for (i = 11; i < data->innr; i++) {
		reg = pc87360_read_value(data, LD_IN, i,
					 PC87365_REG_TEMP_STATUS);
		use_thermistors = use_thermistors || (reg & CHAN_ENA);
		/* thermistors are temp[4-6], measured on vin[11-14] */
		dev_dbg(&pdev->dev, "bios temp%d_status:0x%02x\n", i-7, reg);
	}
	dev_dbg(&pdev->dev, "using thermistors:%d\n", use_thermistors);

	i = use_thermistors ? 2 : 0;
	for (; i < data->tempnr; i++) {
		reg = pc87360_read_value(data, LD_TEMP, i,
					 PC87365_REG_TEMP_STATUS);
		dev_dbg(&pdev->dev, "bios temp%d_status:0x%02x\n", i + 1, reg);
		if (init >= init_temp[i]) {
			/* Forcibly enable temperature channel */
			if (!(reg & CHAN_ENA)) {
				dev_dbg(&pdev->dev,
					"Forcibly enabling temp%d\n", i + 1);
				pc87360_write_value(data, LD_TEMP, i,
						    PC87365_REG_TEMP_STATUS,
						    0xCF);
			}
		}
	}

	if (use_thermistors) {
		for (i = 11; i < data->innr; i++) {
			if (init >= init_in[i]) {
				/*
				 * The pin may already be used by thermal
				 * diodes
				 */
				reg = pc87360_read_value(data, LD_TEMP,
				      (i - 11) / 2, PC87365_REG_TEMP_STATUS);
				if (reg & CHAN_ENA) {
					dev_dbg(&pdev->dev,
			"Skipping temp%d, pin already in use by temp%d\n",
						i - 7, (i - 11) / 2);
					continue;
				}

				/* Forcibly enable thermistor channel */
				reg = pc87360_read_value(data, LD_IN, i,
							 PC87365_REG_IN_STATUS);
				if (!(reg & CHAN_ENA)) {
					dev_dbg(&pdev->dev,
						"Forcibly enabling temp%d\n",
						i - 7);
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
		dev_dbg(&pdev->dev, "bios vin-cfg:0x%02x\n", reg);
		if (reg & CHAN_ENA) {
			dev_dbg(&pdev->dev,
				"Forcibly enabling monitoring (VLM)\n");
			pc87360_write_value(data, LD_IN, NO_BANK,
					    PC87365_REG_IN_CONFIG,
					    reg & 0xFE);
		}
	}

	if (data->tempnr) {
		reg = pc87360_read_value(data, LD_TEMP, NO_BANK,
					 PC87365_REG_TEMP_CONFIG);
		dev_dbg(&pdev->dev, "bios temp-cfg:0x%02x\n", reg);
		if (reg & CHAN_ENA) {
			dev_dbg(&pdev->dev,
				"Forcibly enabling monitoring (TMS)\n");
			pc87360_write_value(data, LD_TEMP, NO_BANK,
					    PC87365_REG_TEMP_CONFIG,
					    reg & 0xFE);
		}

		if (init >= 2) {
			/* Chip config as documented by National Semi. */
			pc87360_write_value(data, LD_TEMP, 0xF, 0xA, 0x08);
			/*
			 * We voluntarily omit the bank here, in case the
			 * sequence itself matters. It shouldn't be a problem,
			 * since nobody else is supposed to access the
			 * device at that point.
			 */
			pc87360_write_value(data, LD_TEMP, NO_BANK, 0xB, 0x04);
			pc87360_write_value(data, LD_TEMP, NO_BANK, 0xC, 0x35);
			pc87360_write_value(data, LD_TEMP, NO_BANK, 0xD, 0x05);
			pc87360_write_value(data, LD_TEMP, NO_BANK, 0xE, 0x05);
		}
	}
}

static void pc87360_autodiv(struct device *dev, int nr)
{
	struct pc87360_data *data = dev_get_drvdata(dev);
	u8 old_min = data->fan_min[nr];

	/* Increase clock divider if needed and possible */
	if ((data->fan_status[nr] & 0x04) /* overflow flag */
	 || (data->fan[nr] >= 224)) { /* next to overflow */
		if ((data->fan_status[nr] & 0x60) != 0x60) {
			data->fan_status[nr] += 0x20;
			data->fan_min[nr] >>= 1;
			data->fan[nr] >>= 1;
			dev_dbg(dev,
				"Increasing clock divider to %d for fan %d\n",
				FAN_DIV_FROM_REG(data->fan_status[nr]), nr + 1);
		}
	} else {
		/* Decrease clock divider if possible */
		while (!(data->fan_min[nr] & 0x80) /* min "nails" divider */
		 && data->fan[nr] < 85 /* bad accuracy */
		 && (data->fan_status[nr] & 0x60) != 0x00) {
			data->fan_status[nr] -= 0x20;
			data->fan_min[nr] <<= 1;
			data->fan[nr] <<= 1;
			dev_dbg(dev,
				"Decreasing clock divider to %d for fan %d\n",
				FAN_DIV_FROM_REG(data->fan_status[nr]),
				nr + 1);
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
	struct pc87360_data *data = dev_get_drvdata(dev);
	u8 i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		dev_dbg(dev, "Data update\n");

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
				pc87360_autodiv(dev, i);
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
			if ((data->in_status[i] & CHAN_READY) == CHAN_READY) {
				data->in[i] = pc87360_read_value(data, LD_IN,
					      i, PC87365_REG_IN);
			}
			if (data->in_status[i] & CHAN_ENA) {
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
			if ((data->temp_status[i] & CHAN_READY) == CHAN_READY) {
				data->temp[i] = pc87360_read_value(data,
						LD_TEMP, i,
						PC87365_REG_TEMP);
			}
			if (data->temp_status[i] & CHAN_ENA) {
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

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init pc87360_device_add(unsigned short address)
{
	struct resource res[3];
	int err, i, res_count;

	pdev = platform_device_alloc("pc87360", address);
	if (!pdev) {
		err = -ENOMEM;
		pr_err("Device allocation failed\n");
		goto exit;
	}

	memset(res, 0, 3 * sizeof(struct resource));
	res_count = 0;
	for (i = 0; i < 3; i++) {
		if (!extra_isa[i])
			continue;
		res[res_count].start = extra_isa[i];
		res[res_count].end = extra_isa[i] + PC87360_EXTENT - 1;
		res[res_count].name = "pc87360",
		res[res_count].flags = IORESOURCE_IO,

		err = acpi_check_resource_conflict(&res[res_count]);
		if (err)
			goto exit_device_put;

		res_count++;
	}

	err = platform_device_add_resources(pdev, res, res_count);
	if (err) {
		pr_err("Device resources addition failed (%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add(pdev);
	if (err) {
		pr_err("Device addition failed (%d)\n", err);
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev);
exit:
	return err;
}

static int __init pc87360_init(void)
{
	int err, i;
	unsigned short address = 0;

	if (pc87360_find(0x2e, &devid, extra_isa)
	 && pc87360_find(0x4e, &devid, extra_isa)) {
		pr_warn("PC8736x not detected, module not inserted\n");
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
		pr_warn("No active logical device, module not inserted\n");
		return -ENODEV;
	}

	err = platform_driver_register(&pc87360_driver);
	if (err)
		goto exit;

	/* Sets global pdev as a side effect */
	err = pc87360_device_add(address);
	if (err)
		goto exit_driver;

	return 0;

 exit_driver:
	platform_driver_unregister(&pc87360_driver);
 exit:
	return err;
}

static void __exit pc87360_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&pc87360_driver);
}


MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("PC8736x hardware monitor");
MODULE_LICENSE("GPL");

module_init(pc87360_init);
module_exit(pc87360_exit);
