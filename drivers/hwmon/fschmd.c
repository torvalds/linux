/* fschmd.c
 *
 * Copyright (C) 2007 - 2009 Hans de Goede <hdegoede@redhat.com>
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

/*
 *  Merged Fujitsu Siemens hwmon driver, supporting the Poseidon, Hermes,
 *  Scylla, Heracles, Heimdall, Hades and Syleus chips
 *
 *  Based on the original 2.4 fscscy, 2.6 fscpos, 2.6 fscher and 2.6
 *  (candidate) fschmd drivers:
 *  Copyright (C) 2006 Thilo Cestonaro
 *			<thilo.cestonaro.external@fujitsu-siemens.com>
 *  Copyright (C) 2004, 2005 Stefan Ott <stefan@desire.ch>
 *  Copyright (C) 2003, 2004 Reinhard Nissl <rnissl@gmx.de>
 *  Copyright (c) 2001 Martin Knoblauch <mkn@teraport.de, knobi@knobisoft.de>
 *  Copyright (C) 2000 Hermann Jung <hej@odn.de>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/dmi.h>
#include <linux/fs.h>
#include <linux/watchdog.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/kref.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x73, I2C_CLIENT_END };

/* Insmod parameters */
static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

enum chips { fscpos, fscher, fscscy, fschrc, fschmd, fschds, fscsyl };

/*
 * The FSCHMD registers and other defines
 */

/* chip identification */
#define FSCHMD_REG_IDENT_0		0x00
#define FSCHMD_REG_IDENT_1		0x01
#define FSCHMD_REG_IDENT_2		0x02
#define FSCHMD_REG_REVISION		0x03

/* global control and status */
#define FSCHMD_REG_EVENT_STATE		0x04
#define FSCHMD_REG_CONTROL		0x05

#define FSCHMD_CONTROL_ALERT_LED	0x01

/* watchdog */
static const u8 FSCHMD_REG_WDOG_CONTROL[7] =
	{ 0x21, 0x21, 0x21, 0x21, 0x21, 0x28, 0x28 };
static const u8 FSCHMD_REG_WDOG_STATE[7] =
	{ 0x23, 0x23, 0x23, 0x23, 0x23, 0x29, 0x29 };
static const u8 FSCHMD_REG_WDOG_PRESET[7] =
	{ 0x28, 0x28, 0x28, 0x28, 0x28, 0x2a, 0x2a };

#define FSCHMD_WDOG_CONTROL_TRIGGER	0x10
#define FSCHMD_WDOG_CONTROL_STARTED	0x10 /* the same as trigger */
#define FSCHMD_WDOG_CONTROL_STOP	0x20
#define FSCHMD_WDOG_CONTROL_RESOLUTION	0x40

#define FSCHMD_WDOG_STATE_CARDRESET	0x02

/* voltages, weird order is to keep the same order as the old drivers */
static const u8 FSCHMD_REG_VOLT[7][6] = {
	{ 0x45, 0x42, 0x48 },				/* pos */
	{ 0x45, 0x42, 0x48 },				/* her */
	{ 0x45, 0x42, 0x48 },				/* scy */
	{ 0x45, 0x42, 0x48 },				/* hrc */
	{ 0x45, 0x42, 0x48 },				/* hmd */
	{ 0x21, 0x20, 0x22 },				/* hds */
	{ 0x21, 0x20, 0x22, 0x23, 0x24, 0x25 },		/* syl */
};

static const int FSCHMD_NO_VOLT_SENSORS[7] = { 3, 3, 3, 3, 3, 3, 6 };

/* minimum pwm at which the fan is driven (pwm can by increased depending on
   the temp. Notice that for the scy some fans share there minimum speed.
   Also notice that with the scy the sensor order is different than with the
   other chips, this order was in the 2.4 driver and kept for consistency. */
static const u8 FSCHMD_REG_FAN_MIN[7][7] = {
	{ 0x55, 0x65 },					/* pos */
	{ 0x55, 0x65, 0xb5 },				/* her */
	{ 0x65, 0x65, 0x55, 0xa5, 0x55, 0xa5 },		/* scy */
	{ 0x55, 0x65, 0xa5, 0xb5 },			/* hrc */
	{ 0x55, 0x65, 0xa5, 0xb5, 0xc5 },		/* hmd */
	{ 0x55, 0x65, 0xa5, 0xb5, 0xc5 },		/* hds */
	{ 0x54, 0x64, 0x74, 0x84, 0x94, 0xa4, 0xb4 },	/* syl */
};

/* actual fan speed */
static const u8 FSCHMD_REG_FAN_ACT[7][7] = {
	{ 0x0e, 0x6b, 0xab },				/* pos */
	{ 0x0e, 0x6b, 0xbb },				/* her */
	{ 0x6b, 0x6c, 0x0e, 0xab, 0x5c, 0xbb },		/* scy */
	{ 0x0e, 0x6b, 0xab, 0xbb },			/* hrc */
	{ 0x5b, 0x6b, 0xab, 0xbb, 0xcb },		/* hmd */
	{ 0x5b, 0x6b, 0xab, 0xbb, 0xcb },		/* hds */
	{ 0x57, 0x67, 0x77, 0x87, 0x97, 0xa7, 0xb7 },	/* syl */
};

/* fan status registers */
static const u8 FSCHMD_REG_FAN_STATE[7][7] = {
	{ 0x0d, 0x62, 0xa2 },				/* pos */
	{ 0x0d, 0x62, 0xb2 },				/* her */
	{ 0x62, 0x61, 0x0d, 0xa2, 0x52, 0xb2 },		/* scy */
	{ 0x0d, 0x62, 0xa2, 0xb2 },			/* hrc */
	{ 0x52, 0x62, 0xa2, 0xb2, 0xc2 },		/* hmd */
	{ 0x52, 0x62, 0xa2, 0xb2, 0xc2 },		/* hds */
	{ 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0 },	/* syl */
};

/* fan ripple / divider registers */
static const u8 FSCHMD_REG_FAN_RIPPLE[7][7] = {
	{ 0x0f, 0x6f, 0xaf },				/* pos */
	{ 0x0f, 0x6f, 0xbf },				/* her */
	{ 0x6f, 0x6f, 0x0f, 0xaf, 0x0f, 0xbf },		/* scy */
	{ 0x0f, 0x6f, 0xaf, 0xbf },			/* hrc */
	{ 0x5f, 0x6f, 0xaf, 0xbf, 0xcf },		/* hmd */
	{ 0x5f, 0x6f, 0xaf, 0xbf, 0xcf },		/* hds */
	{ 0x56, 0x66, 0x76, 0x86, 0x96, 0xa6, 0xb6 },	/* syl */
};

static const int FSCHMD_NO_FAN_SENSORS[7] = { 3, 3, 6, 4, 5, 5, 7 };

/* Fan status register bitmasks */
#define FSCHMD_FAN_ALARM	0x04 /* called fault by FSC! */
#define FSCHMD_FAN_NOT_PRESENT	0x08
#define FSCHMD_FAN_DISABLED	0x80


/* actual temperature registers */
static const u8 FSCHMD_REG_TEMP_ACT[7][11] = {
	{ 0x64, 0x32, 0x35 },				/* pos */
	{ 0x64, 0x32, 0x35 },				/* her */
	{ 0x64, 0xD0, 0x32, 0x35 },			/* scy */
	{ 0x64, 0x32, 0x35 },				/* hrc */
	{ 0x70, 0x80, 0x90, 0xd0, 0xe0 },		/* hmd */
	{ 0x70, 0x80, 0x90, 0xd0, 0xe0 },		/* hds */
	{ 0x58, 0x68, 0x78, 0x88, 0x98, 0xa8,		/* syl */
	  0xb8, 0xc8, 0xd8, 0xe8, 0xf8 },
};

/* temperature state registers */
static const u8 FSCHMD_REG_TEMP_STATE[7][11] = {
	{ 0x71, 0x81, 0x91 },				/* pos */
	{ 0x71, 0x81, 0x91 },				/* her */
	{ 0x71, 0xd1, 0x81, 0x91 },			/* scy */
	{ 0x71, 0x81, 0x91 },				/* hrc */
	{ 0x71, 0x81, 0x91, 0xd1, 0xe1 },		/* hmd */
	{ 0x71, 0x81, 0x91, 0xd1, 0xe1 },		/* hds */
	{ 0x59, 0x69, 0x79, 0x89, 0x99, 0xa9,		/* syl */
	  0xb9, 0xc9, 0xd9, 0xe9, 0xf9 },
};

/* temperature high limit registers, FSC does not document these. Proven to be
   there with field testing on the fscher and fschrc, already supported / used
   in the fscscy 2.4 driver. FSC has confirmed that the fschmd has registers
   at these addresses, but doesn't want to confirm they are the same as with
   the fscher?? */
static const u8 FSCHMD_REG_TEMP_LIMIT[7][11] = {
	{ 0, 0, 0 },					/* pos */
	{ 0x76, 0x86, 0x96 },				/* her */
	{ 0x76, 0xd6, 0x86, 0x96 },			/* scy */
	{ 0x76, 0x86, 0x96 },				/* hrc */
	{ 0x76, 0x86, 0x96, 0xd6, 0xe6 },		/* hmd */
	{ 0x76, 0x86, 0x96, 0xd6, 0xe6 },		/* hds */
	{ 0x5a, 0x6a, 0x7a, 0x8a, 0x9a, 0xaa,		/* syl */
	  0xba, 0xca, 0xda, 0xea, 0xfa },
};

/* These were found through experimenting with an fscher, currently they are
   not used, but we keep them around for future reference.
   On the fscsyl AUTOP1 lives at 0x#c (so 0x5c for fan1, 0x6c for fan2, etc),
   AUTOP2 lives at 0x#e, and 0x#1 is a bitmask defining which temps influence
   the fan speed.
static const u8 FSCHER_REG_TEMP_AUTOP1[] =	{ 0x73, 0x83, 0x93 };
static const u8 FSCHER_REG_TEMP_AUTOP2[] =	{ 0x75, 0x85, 0x95 }; */

static const int FSCHMD_NO_TEMP_SENSORS[7] = { 3, 3, 4, 3, 5, 5, 11 };

/* temp status register bitmasks */
#define FSCHMD_TEMP_WORKING	0x01
#define FSCHMD_TEMP_ALERT	0x02
#define FSCHMD_TEMP_DISABLED	0x80
/* there only really is an alarm if the sensor is working and alert == 1 */
#define FSCHMD_TEMP_ALARM_MASK \
	(FSCHMD_TEMP_WORKING | FSCHMD_TEMP_ALERT)

/*
 * Functions declarations
 */

static int fschmd_probe(struct i2c_client *client,
			const struct i2c_device_id *id);
static int fschmd_detect(struct i2c_client *client,
			 struct i2c_board_info *info);
static int fschmd_remove(struct i2c_client *client);
static struct fschmd_data *fschmd_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id fschmd_id[] = {
	{ "fscpos", fscpos },
	{ "fscher", fscher },
	{ "fscscy", fscscy },
	{ "fschrc", fschrc },
	{ "fschmd", fschmd },
	{ "fschds", fschds },
	{ "fscsyl", fscsyl },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fschmd_id);

static struct i2c_driver fschmd_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "fschmd",
	},
	.probe		= fschmd_probe,
	.remove		= fschmd_remove,
	.id_table	= fschmd_id,
	.detect		= fschmd_detect,
	.address_list	= normal_i2c,
};

/*
 * Client data (each client gets its own)
 */

struct fschmd_data {
	struct i2c_client *client;
	struct device *hwmon_dev;
	struct mutex update_lock;
	struct mutex watchdog_lock;
	struct list_head list; /* member of the watchdog_data_list */
	struct kref kref;
	struct miscdevice watchdog_miscdev;
	enum chips kind;
	unsigned long watchdog_is_open;
	char watchdog_expect_close;
	char watchdog_name[10]; /* must be unique to avoid sysfs conflict */
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* register values */
	u8 revision;            /* chip revision */
	u8 global_control;	/* global control register */
	u8 watchdog_control;    /* watchdog control register */
	u8 watchdog_state;      /* watchdog status register */
	u8 watchdog_preset;     /* watchdog counter preset on trigger val */
	u8 volt[6];		/* voltage */
	u8 temp_act[11];	/* temperature */
	u8 temp_status[11];	/* status of sensor */
	u8 temp_max[11];	/* high temp limit, notice: undocumented! */
	u8 fan_act[7];		/* fans revolutions per second */
	u8 fan_status[7];	/* fan status */
	u8 fan_min[7];		/* fan min value for rps */
	u8 fan_ripple[7];	/* divider for rps */
};

/* Global variables to hold information read from special DMI tables, which are
   available on FSC machines with an fscher or later chip. There is no need to
   protect these with a lock as they are only modified from our attach function
   which always gets called with the i2c-core lock held and never accessed
   before the attach function is done with them. */
static int dmi_mult[6] = { 490, 200, 100, 100, 200, 100 };
static int dmi_offset[6] = { 0, 0, 0, 0, 0, 0 };
static int dmi_vref = -1;

/* Somewhat ugly :( global data pointer list with all fschmd devices, so that
   we can find our device data as when using misc_register there is no other
   method to get to ones device data from the open fop. */
static LIST_HEAD(watchdog_data_list);
/* Note this lock not only protect list access, but also data.kref access */
static DEFINE_MUTEX(watchdog_data_mutex);

/* Release our data struct when we're detached from the i2c client *and* all
   references to our watchdog device are released */
static void fschmd_release_resources(struct kref *ref)
{
	struct fschmd_data *data = container_of(ref, struct fschmd_data, kref);
	kfree(data);
}

/*
 * Sysfs attr show / store functions
 */

static ssize_t show_in_value(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	const int max_reading[3] = { 14200, 6600, 3300 };
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	if (data->kind == fscher || data->kind >= fschrc)
		return sprintf(buf, "%d\n", (data->volt[index] * dmi_vref *
			dmi_mult[index]) / 255 + dmi_offset[index]);
	else
		return sprintf(buf, "%d\n", (data->volt[index] *
			max_reading[index] + 128) / 255);
}


#define TEMP_FROM_REG(val)	(((val) - 128) * 1000)

static ssize_t show_temp_value(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_act[index]));
}

static ssize_t show_temp_max(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[index]));
}

static ssize_t store_temp_max(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = dev_get_drvdata(dev);
	long v = simple_strtol(buf, NULL, 10) / 1000;

	v = SENSORS_LIMIT(v, -128, 127) + 128;

	mutex_lock(&data->update_lock);
	i2c_smbus_write_byte_data(to_i2c_client(dev),
		FSCHMD_REG_TEMP_LIMIT[data->kind][index], v);
	data->temp_max[index] = v;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp_fault(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	/* bit 0 set means sensor working ok, so no fault! */
	if (data->temp_status[index] & FSCHMD_TEMP_WORKING)
		return sprintf(buf, "0\n");
	else
		return sprintf(buf, "1\n");
}

static ssize_t show_temp_alarm(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	if ((data->temp_status[index] & FSCHMD_TEMP_ALARM_MASK) ==
			FSCHMD_TEMP_ALARM_MASK)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}


#define RPM_FROM_REG(val)	((val) * 60)

static ssize_t show_fan_value(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	return sprintf(buf, "%u\n", RPM_FROM_REG(data->fan_act[index]));
}

static ssize_t show_fan_div(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	/* bits 2..7 reserved => mask with 3 */
	return sprintf(buf, "%d\n", 1 << (data->fan_ripple[index] & 3));
}

static ssize_t store_fan_div(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	u8 reg;
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = dev_get_drvdata(dev);
	/* supported values: 2, 4, 8 */
	unsigned long v = simple_strtoul(buf, NULL, 10);

	switch (v) {
	case 2: v = 1; break;
	case 4: v = 2; break;
	case 8: v = 3; break;
	default:
		dev_err(dev, "fan_div value %lu not supported. "
			"Choose one of 2, 4 or 8!\n", v);
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);

	reg = i2c_smbus_read_byte_data(to_i2c_client(dev),
		FSCHMD_REG_FAN_RIPPLE[data->kind][index]);

	/* bits 2..7 reserved => mask with 0x03 */
	reg &= ~0x03;
	reg |= v;

	i2c_smbus_write_byte_data(to_i2c_client(dev),
		FSCHMD_REG_FAN_RIPPLE[data->kind][index], reg);

	data->fan_ripple[index] = reg;

	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_fan_alarm(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	if (data->fan_status[index] & FSCHMD_FAN_ALARM)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_fan_fault(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	if (data->fan_status[index] & FSCHMD_FAN_NOT_PRESENT)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}


static ssize_t show_pwm_auto_point1_pwm(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);
	int val = data->fan_min[index];

	/* 0 = allow turning off (except on the syl), 1-255 = 50-100% */
	if (val || data->kind == fscsyl)
		val = val / 2 + 128;

	return sprintf(buf, "%d\n", val);
}

static ssize_t store_pwm_auto_point1_pwm(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = dev_get_drvdata(dev);
	unsigned long v = simple_strtoul(buf, NULL, 10);

	/* reg: 0 = allow turning off (except on the syl), 1-255 = 50-100% */
	if (v || data->kind == fscsyl) {
		v = SENSORS_LIMIT(v, 128, 255);
		v = (v - 128) * 2 + 1;
	}

	mutex_lock(&data->update_lock);

	i2c_smbus_write_byte_data(to_i2c_client(dev),
		FSCHMD_REG_FAN_MIN[data->kind][index], v);
	data->fan_min[index] = v;

	mutex_unlock(&data->update_lock);

	return count;
}


/* The FSC hwmon family has the ability to force an attached alert led to flash
   from software, we export this as an alert_led sysfs attr */
static ssize_t show_alert_led(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct fschmd_data *data = fschmd_update_device(dev);

	if (data->global_control & FSCHMD_CONTROL_ALERT_LED)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t store_alert_led(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	u8 reg;
	struct fschmd_data *data = dev_get_drvdata(dev);
	unsigned long v = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);

	reg = i2c_smbus_read_byte_data(to_i2c_client(dev), FSCHMD_REG_CONTROL);

	if (v)
		reg |= FSCHMD_CONTROL_ALERT_LED;
	else
		reg &= ~FSCHMD_CONTROL_ALERT_LED;

	i2c_smbus_write_byte_data(to_i2c_client(dev), FSCHMD_REG_CONTROL, reg);

	data->global_control = reg;

	mutex_unlock(&data->update_lock);

	return count;
}

static DEVICE_ATTR(alert_led, 0644, show_alert_led, store_alert_led);

static struct sensor_device_attribute fschmd_attr[] = {
	SENSOR_ATTR(in0_input, 0444, show_in_value, NULL, 0),
	SENSOR_ATTR(in1_input, 0444, show_in_value, NULL, 1),
	SENSOR_ATTR(in2_input, 0444, show_in_value, NULL, 2),
	SENSOR_ATTR(in3_input, 0444, show_in_value, NULL, 3),
	SENSOR_ATTR(in4_input, 0444, show_in_value, NULL, 4),
	SENSOR_ATTR(in5_input, 0444, show_in_value, NULL, 5),
};

static struct sensor_device_attribute fschmd_temp_attr[] = {
	SENSOR_ATTR(temp1_input, 0444, show_temp_value, NULL, 0),
	SENSOR_ATTR(temp1_max,   0644, show_temp_max, store_temp_max, 0),
	SENSOR_ATTR(temp1_fault, 0444, show_temp_fault, NULL, 0),
	SENSOR_ATTR(temp1_alarm, 0444, show_temp_alarm, NULL, 0),
	SENSOR_ATTR(temp2_input, 0444, show_temp_value, NULL, 1),
	SENSOR_ATTR(temp2_max,   0644, show_temp_max, store_temp_max, 1),
	SENSOR_ATTR(temp2_fault, 0444, show_temp_fault, NULL, 1),
	SENSOR_ATTR(temp2_alarm, 0444, show_temp_alarm, NULL, 1),
	SENSOR_ATTR(temp3_input, 0444, show_temp_value, NULL, 2),
	SENSOR_ATTR(temp3_max,   0644, show_temp_max, store_temp_max, 2),
	SENSOR_ATTR(temp3_fault, 0444, show_temp_fault, NULL, 2),
	SENSOR_ATTR(temp3_alarm, 0444, show_temp_alarm, NULL, 2),
	SENSOR_ATTR(temp4_input, 0444, show_temp_value, NULL, 3),
	SENSOR_ATTR(temp4_max,   0644, show_temp_max, store_temp_max, 3),
	SENSOR_ATTR(temp4_fault, 0444, show_temp_fault, NULL, 3),
	SENSOR_ATTR(temp4_alarm, 0444, show_temp_alarm, NULL, 3),
	SENSOR_ATTR(temp5_input, 0444, show_temp_value, NULL, 4),
	SENSOR_ATTR(temp5_max,   0644, show_temp_max, store_temp_max, 4),
	SENSOR_ATTR(temp5_fault, 0444, show_temp_fault, NULL, 4),
	SENSOR_ATTR(temp5_alarm, 0444, show_temp_alarm, NULL, 4),
	SENSOR_ATTR(temp6_input, 0444, show_temp_value, NULL, 5),
	SENSOR_ATTR(temp6_max,   0644, show_temp_max, store_temp_max, 5),
	SENSOR_ATTR(temp6_fault, 0444, show_temp_fault, NULL, 5),
	SENSOR_ATTR(temp6_alarm, 0444, show_temp_alarm, NULL, 5),
	SENSOR_ATTR(temp7_input, 0444, show_temp_value, NULL, 6),
	SENSOR_ATTR(temp7_max,   0644, show_temp_max, store_temp_max, 6),
	SENSOR_ATTR(temp7_fault, 0444, show_temp_fault, NULL, 6),
	SENSOR_ATTR(temp7_alarm, 0444, show_temp_alarm, NULL, 6),
	SENSOR_ATTR(temp8_input, 0444, show_temp_value, NULL, 7),
	SENSOR_ATTR(temp8_max,   0644, show_temp_max, store_temp_max, 7),
	SENSOR_ATTR(temp8_fault, 0444, show_temp_fault, NULL, 7),
	SENSOR_ATTR(temp8_alarm, 0444, show_temp_alarm, NULL, 7),
	SENSOR_ATTR(temp9_input, 0444, show_temp_value, NULL, 8),
	SENSOR_ATTR(temp9_max,   0644, show_temp_max, store_temp_max, 8),
	SENSOR_ATTR(temp9_fault, 0444, show_temp_fault, NULL, 8),
	SENSOR_ATTR(temp9_alarm, 0444, show_temp_alarm, NULL, 8),
	SENSOR_ATTR(temp10_input, 0444, show_temp_value, NULL, 9),
	SENSOR_ATTR(temp10_max,   0644, show_temp_max, store_temp_max, 9),
	SENSOR_ATTR(temp10_fault, 0444, show_temp_fault, NULL, 9),
	SENSOR_ATTR(temp10_alarm, 0444, show_temp_alarm, NULL, 9),
	SENSOR_ATTR(temp11_input, 0444, show_temp_value, NULL, 10),
	SENSOR_ATTR(temp11_max,   0644, show_temp_max, store_temp_max, 10),
	SENSOR_ATTR(temp11_fault, 0444, show_temp_fault, NULL, 10),
	SENSOR_ATTR(temp11_alarm, 0444, show_temp_alarm, NULL, 10),
};

static struct sensor_device_attribute fschmd_fan_attr[] = {
	SENSOR_ATTR(fan1_input, 0444, show_fan_value, NULL, 0),
	SENSOR_ATTR(fan1_div,   0644, show_fan_div, store_fan_div, 0),
	SENSOR_ATTR(fan1_alarm, 0444, show_fan_alarm, NULL, 0),
	SENSOR_ATTR(fan1_fault, 0444, show_fan_fault, NULL, 0),
	SENSOR_ATTR(pwm1_auto_point1_pwm, 0644, show_pwm_auto_point1_pwm,
		store_pwm_auto_point1_pwm, 0),
	SENSOR_ATTR(fan2_input, 0444, show_fan_value, NULL, 1),
	SENSOR_ATTR(fan2_div,   0644, show_fan_div, store_fan_div, 1),
	SENSOR_ATTR(fan2_alarm, 0444, show_fan_alarm, NULL, 1),
	SENSOR_ATTR(fan2_fault, 0444, show_fan_fault, NULL, 1),
	SENSOR_ATTR(pwm2_auto_point1_pwm, 0644, show_pwm_auto_point1_pwm,
		store_pwm_auto_point1_pwm, 1),
	SENSOR_ATTR(fan3_input, 0444, show_fan_value, NULL, 2),
	SENSOR_ATTR(fan3_div,   0644, show_fan_div, store_fan_div, 2),
	SENSOR_ATTR(fan3_alarm, 0444, show_fan_alarm, NULL, 2),
	SENSOR_ATTR(fan3_fault, 0444, show_fan_fault, NULL, 2),
	SENSOR_ATTR(pwm3_auto_point1_pwm, 0644, show_pwm_auto_point1_pwm,
		store_pwm_auto_point1_pwm, 2),
	SENSOR_ATTR(fan4_input, 0444, show_fan_value, NULL, 3),
	SENSOR_ATTR(fan4_div,   0644, show_fan_div, store_fan_div, 3),
	SENSOR_ATTR(fan4_alarm, 0444, show_fan_alarm, NULL, 3),
	SENSOR_ATTR(fan4_fault, 0444, show_fan_fault, NULL, 3),
	SENSOR_ATTR(pwm4_auto_point1_pwm, 0644, show_pwm_auto_point1_pwm,
		store_pwm_auto_point1_pwm, 3),
	SENSOR_ATTR(fan5_input, 0444, show_fan_value, NULL, 4),
	SENSOR_ATTR(fan5_div,   0644, show_fan_div, store_fan_div, 4),
	SENSOR_ATTR(fan5_alarm, 0444, show_fan_alarm, NULL, 4),
	SENSOR_ATTR(fan5_fault, 0444, show_fan_fault, NULL, 4),
	SENSOR_ATTR(pwm5_auto_point1_pwm, 0644, show_pwm_auto_point1_pwm,
		store_pwm_auto_point1_pwm, 4),
	SENSOR_ATTR(fan6_input, 0444, show_fan_value, NULL, 5),
	SENSOR_ATTR(fan6_div,   0644, show_fan_div, store_fan_div, 5),
	SENSOR_ATTR(fan6_alarm, 0444, show_fan_alarm, NULL, 5),
	SENSOR_ATTR(fan6_fault, 0444, show_fan_fault, NULL, 5),
	SENSOR_ATTR(pwm6_auto_point1_pwm, 0644, show_pwm_auto_point1_pwm,
		store_pwm_auto_point1_pwm, 5),
	SENSOR_ATTR(fan7_input, 0444, show_fan_value, NULL, 6),
	SENSOR_ATTR(fan7_div,   0644, show_fan_div, store_fan_div, 6),
	SENSOR_ATTR(fan7_alarm, 0444, show_fan_alarm, NULL, 6),
	SENSOR_ATTR(fan7_fault, 0444, show_fan_fault, NULL, 6),
	SENSOR_ATTR(pwm7_auto_point1_pwm, 0644, show_pwm_auto_point1_pwm,
		store_pwm_auto_point1_pwm, 6),
};


/*
 * Watchdog routines
 */

static int watchdog_set_timeout(struct fschmd_data *data, int timeout)
{
	int ret, resolution;
	int kind = data->kind + 1; /* 0-x array index -> 1-x module param */

	/* 2 second or 60 second resolution? */
	if (timeout <= 510 || kind == fscpos || kind == fscscy)
		resolution = 2;
	else
		resolution = 60;

	if (timeout < resolution || timeout > (resolution * 255))
		return -EINVAL;

	mutex_lock(&data->watchdog_lock);
	if (!data->client) {
		ret = -ENODEV;
		goto leave;
	}

	if (resolution == 2)
		data->watchdog_control &= ~FSCHMD_WDOG_CONTROL_RESOLUTION;
	else
		data->watchdog_control |= FSCHMD_WDOG_CONTROL_RESOLUTION;

	data->watchdog_preset = DIV_ROUND_UP(timeout, resolution);

	/* Write new timeout value */
	i2c_smbus_write_byte_data(data->client,
		FSCHMD_REG_WDOG_PRESET[data->kind], data->watchdog_preset);
	/* Write new control register, do not trigger! */
	i2c_smbus_write_byte_data(data->client,
		FSCHMD_REG_WDOG_CONTROL[data->kind],
		data->watchdog_control & ~FSCHMD_WDOG_CONTROL_TRIGGER);

	ret = data->watchdog_preset * resolution;

leave:
	mutex_unlock(&data->watchdog_lock);
	return ret;
}

static int watchdog_get_timeout(struct fschmd_data *data)
{
	int timeout;

	mutex_lock(&data->watchdog_lock);
	if (data->watchdog_control & FSCHMD_WDOG_CONTROL_RESOLUTION)
		timeout = data->watchdog_preset * 60;
	else
		timeout = data->watchdog_preset * 2;
	mutex_unlock(&data->watchdog_lock);

	return timeout;
}

static int watchdog_trigger(struct fschmd_data *data)
{
	int ret = 0;

	mutex_lock(&data->watchdog_lock);
	if (!data->client) {
		ret = -ENODEV;
		goto leave;
	}

	data->watchdog_control |= FSCHMD_WDOG_CONTROL_TRIGGER;
	i2c_smbus_write_byte_data(data->client,
				  FSCHMD_REG_WDOG_CONTROL[data->kind],
				  data->watchdog_control);
leave:
	mutex_unlock(&data->watchdog_lock);
	return ret;
}

static int watchdog_stop(struct fschmd_data *data)
{
	int ret = 0;

	mutex_lock(&data->watchdog_lock);
	if (!data->client) {
		ret = -ENODEV;
		goto leave;
	}

	data->watchdog_control &= ~FSCHMD_WDOG_CONTROL_STARTED;
	/* Don't store the stop flag in our watchdog control register copy, as
	   its a write only bit (read always returns 0) */
	i2c_smbus_write_byte_data(data->client,
		FSCHMD_REG_WDOG_CONTROL[data->kind],
		data->watchdog_control | FSCHMD_WDOG_CONTROL_STOP);
leave:
	mutex_unlock(&data->watchdog_lock);
	return ret;
}

static int watchdog_open(struct inode *inode, struct file *filp)
{
	struct fschmd_data *pos, *data = NULL;
	int watchdog_is_open;

	/* We get called from drivers/char/misc.c with misc_mtx hold, and we
	   call misc_register() from fschmd_probe() with watchdog_data_mutex
	   hold, as misc_register() takes the misc_mtx lock, this is a possible
	   deadlock, so we use mutex_trylock here. */
	if (!mutex_trylock(&watchdog_data_mutex))
		return -ERESTARTSYS;
	list_for_each_entry(pos, &watchdog_data_list, list) {
		if (pos->watchdog_miscdev.minor == iminor(inode)) {
			data = pos;
			break;
		}
	}
	/* Note we can never not have found data, so we don't check for this */
	watchdog_is_open = test_and_set_bit(0, &data->watchdog_is_open);
	if (!watchdog_is_open)
		kref_get(&data->kref);
	mutex_unlock(&watchdog_data_mutex);

	if (watchdog_is_open)
		return -EBUSY;

	/* Start the watchdog */
	watchdog_trigger(data);
	filp->private_data = data;

	return nonseekable_open(inode, filp);
}

static int watchdog_release(struct inode *inode, struct file *filp)
{
	struct fschmd_data *data = filp->private_data;

	if (data->watchdog_expect_close) {
		watchdog_stop(data);
		data->watchdog_expect_close = 0;
	} else {
		watchdog_trigger(data);
		dev_crit(&data->client->dev,
			"unexpected close, not stopping watchdog!\n");
	}

	clear_bit(0, &data->watchdog_is_open);

	mutex_lock(&watchdog_data_mutex);
	kref_put(&data->kref, fschmd_release_resources);
	mutex_unlock(&watchdog_data_mutex);

	return 0;
}

static ssize_t watchdog_write(struct file *filp, const char __user *buf,
	size_t count, loff_t *offset)
{
	int ret;
	struct fschmd_data *data = filp->private_data;

	if (count) {
		if (!nowayout) {
			size_t i;

			/* Clear it in case it was set with a previous write */
			data->watchdog_expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					data->watchdog_expect_close = 1;
			}
		}
		ret = watchdog_trigger(data);
		if (ret < 0)
			return ret;
	}
	return count;
}

static int watchdog_ioctl(struct inode *inode, struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	static struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
				WDIOF_CARDRESET,
		.identity = "FSC watchdog"
	};
	int i, ret = 0;
	struct fschmd_data *data = filp->private_data;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ident.firmware_version = data->revision;
		if (!nowayout)
			ident.options |= WDIOF_MAGICCLOSE;
		if (copy_to_user((void __user *)arg, &ident, sizeof(ident)))
			ret = -EFAULT;
		break;

	case WDIOC_GETSTATUS:
		ret = put_user(0, (int __user *)arg);
		break;

	case WDIOC_GETBOOTSTATUS:
		if (data->watchdog_state & FSCHMD_WDOG_STATE_CARDRESET)
			ret = put_user(WDIOF_CARDRESET, (int __user *)arg);
		else
			ret = put_user(0, (int __user *)arg);
		break;

	case WDIOC_KEEPALIVE:
		ret = watchdog_trigger(data);
		break;

	case WDIOC_GETTIMEOUT:
		i = watchdog_get_timeout(data);
		ret = put_user(i, (int __user *)arg);
		break;

	case WDIOC_SETTIMEOUT:
		if (get_user(i, (int __user *)arg)) {
			ret = -EFAULT;
			break;
		}
		ret = watchdog_set_timeout(data, i);
		if (ret > 0)
			ret = put_user(ret, (int __user *)arg);
		break;

	case WDIOC_SETOPTIONS:
		if (get_user(i, (int __user *)arg)) {
			ret = -EFAULT;
			break;
		}

		if (i & WDIOS_DISABLECARD)
			ret = watchdog_stop(data);
		else if (i & WDIOS_ENABLECARD)
			ret = watchdog_trigger(data);
		else
			ret = -EINVAL;

		break;
	default:
		ret = -ENOTTY;
	}

	return ret;
}

static const struct file_operations watchdog_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = watchdog_open,
	.release = watchdog_release,
	.write = watchdog_write,
	.ioctl = watchdog_ioctl,
};


/*
 * Detect, register, unregister and update device functions
 */

/* DMI decode routine to read voltage scaling factors from special DMI tables,
   which are available on FSC machines with an fscher or later chip. */
static void fschmd_dmi_decode(const struct dmi_header *header, void *dummy)
{
	int i, mult[3] = { 0 }, offset[3] = { 0 }, vref = 0, found = 0;

	/* dmi code ugliness, we get passed the address of the contents of
	   a complete DMI record, but in the form of a dmi_header pointer, in
	   reality this address holds header->length bytes of which the header
	   are the first 4 bytes */
	u8 *dmi_data = (u8 *)header;

	/* We are looking for OEM-specific type 185 */
	if (header->type != 185)
		return;

	/* we are looking for what Siemens calls "subtype" 19, the subtype
	   is stored in byte 5 of the dmi block */
	if (header->length < 5 || dmi_data[4] != 19)
		return;

	/* After the subtype comes 1 unknown byte and then blocks of 5 bytes,
	   consisting of what Siemens calls an "Entity" number, followed by
	   2 16-bit words in LSB first order */
	for (i = 6; (i + 4) < header->length; i += 5) {
		/* entity 1 - 3: voltage multiplier and offset */
		if (dmi_data[i] >= 1 && dmi_data[i] <= 3) {
			/* Our in sensors order and the DMI order differ */
			const int shuffle[3] = { 1, 0, 2 };
			int in = shuffle[dmi_data[i] - 1];

			/* Check for twice the same entity */
			if (found & (1 << in))
				return;

			mult[in] = dmi_data[i + 1] | (dmi_data[i + 2] << 8);
			offset[in] = dmi_data[i + 3] | (dmi_data[i + 4] << 8);

			found |= 1 << in;
		}

		/* entity 7: reference voltage */
		if (dmi_data[i] == 7) {
			/* Check for twice the same entity */
			if (found & 0x08)
				return;

			vref = dmi_data[i + 1] | (dmi_data[i + 2] << 8);

			found |= 0x08;
		}
	}

	if (found == 0x0F) {
		for (i = 0; i < 3; i++) {
			dmi_mult[i] = mult[i] * 10;
			dmi_offset[i] = offset[i] * 10;
		}
		/* According to the docs there should be separate dmi entries
		   for the mult's and offsets of in3-5 of the syl, but on
		   my test machine these are not present */
		dmi_mult[3] = dmi_mult[2];
		dmi_mult[4] = dmi_mult[1];
		dmi_mult[5] = dmi_mult[2];
		dmi_offset[3] = dmi_offset[2];
		dmi_offset[4] = dmi_offset[1];
		dmi_offset[5] = dmi_offset[2];
		dmi_vref = vref;
	}
}

static int fschmd_detect(struct i2c_client *client,
			 struct i2c_board_info *info)
{
	enum chips kind;
	struct i2c_adapter *adapter = client->adapter;
	char id[4];

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Detect & Identify the chip */
	id[0] = i2c_smbus_read_byte_data(client, FSCHMD_REG_IDENT_0);
	id[1] = i2c_smbus_read_byte_data(client, FSCHMD_REG_IDENT_1);
	id[2] = i2c_smbus_read_byte_data(client, FSCHMD_REG_IDENT_2);
	id[3] = '\0';

	if (!strcmp(id, "PEG"))
		kind = fscpos;
	else if (!strcmp(id, "HER"))
		kind = fscher;
	else if (!strcmp(id, "SCY"))
		kind = fscscy;
	else if (!strcmp(id, "HRC"))
		kind = fschrc;
	else if (!strcmp(id, "HMD"))
		kind = fschmd;
	else if (!strcmp(id, "HDS"))
		kind = fschds;
	else if (!strcmp(id, "SYL"))
		kind = fscsyl;
	else
		return -ENODEV;

	strlcpy(info->type, fschmd_id[kind].name, I2C_NAME_SIZE);

	return 0;
}

static int fschmd_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct fschmd_data *data;
	const char * const names[7] = { "Poseidon", "Hermes", "Scylla",
				"Heracles", "Heimdall", "Hades", "Syleus" };
	const int watchdog_minors[] = { WATCHDOG_MINOR, 212, 213, 214, 215 };
	int i, err;
	enum chips kind = id->driver_data;

	data = kzalloc(sizeof(struct fschmd_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);
	mutex_init(&data->watchdog_lock);
	INIT_LIST_HEAD(&data->list);
	kref_init(&data->kref);
	/* Store client pointer in our data struct for watchdog usage
	   (where the client is found through a data ptr instead of the
	   otherway around) */
	data->client = client;
	data->kind = kind;

	if (kind == fscpos) {
		/* The Poseidon has hardwired temp limits, fill these
		   in for the alarm resetting code */
		data->temp_max[0] = 70 + 128;
		data->temp_max[1] = 50 + 128;
		data->temp_max[2] = 50 + 128;
	}

	/* Read the special DMI table for fscher and newer chips */
	if ((kind == fscher || kind >= fschrc) && dmi_vref == -1) {
		dmi_walk(fschmd_dmi_decode, NULL);
		if (dmi_vref == -1) {
			dev_warn(&client->dev,
				"Couldn't get voltage scaling factors from "
				"BIOS DMI table, using builtin defaults\n");
			dmi_vref = 33;
		}
	}

	/* Read in some never changing registers */
	data->revision = i2c_smbus_read_byte_data(client, FSCHMD_REG_REVISION);
	data->global_control = i2c_smbus_read_byte_data(client,
					FSCHMD_REG_CONTROL);
	data->watchdog_control = i2c_smbus_read_byte_data(client,
					FSCHMD_REG_WDOG_CONTROL[data->kind]);
	data->watchdog_state = i2c_smbus_read_byte_data(client,
					FSCHMD_REG_WDOG_STATE[data->kind]);
	data->watchdog_preset = i2c_smbus_read_byte_data(client,
					FSCHMD_REG_WDOG_PRESET[data->kind]);

	err = device_create_file(&client->dev, &dev_attr_alert_led);
	if (err)
		goto exit_detach;

	for (i = 0; i < FSCHMD_NO_VOLT_SENSORS[data->kind]; i++) {
		err = device_create_file(&client->dev,
					&fschmd_attr[i].dev_attr);
		if (err)
			goto exit_detach;
	}

	for (i = 0; i < (FSCHMD_NO_TEMP_SENSORS[data->kind] * 4); i++) {
		/* Poseidon doesn't have TEMP_LIMIT registers */
		if (kind == fscpos && fschmd_temp_attr[i].dev_attr.show ==
				show_temp_max)
			continue;

		if (kind == fscsyl) {
			if (i % 4 == 0)
				data->temp_status[i / 4] =
					i2c_smbus_read_byte_data(client,
						FSCHMD_REG_TEMP_STATE
						[data->kind][i / 4]);
			if (data->temp_status[i / 4] & FSCHMD_TEMP_DISABLED)
				continue;
		}

		err = device_create_file(&client->dev,
					&fschmd_temp_attr[i].dev_attr);
		if (err)
			goto exit_detach;
	}

	for (i = 0; i < (FSCHMD_NO_FAN_SENSORS[data->kind] * 5); i++) {
		/* Poseidon doesn't have a FAN_MIN register for its 3rd fan */
		if (kind == fscpos &&
				!strcmp(fschmd_fan_attr[i].dev_attr.attr.name,
					"pwm3_auto_point1_pwm"))
			continue;

		if (kind == fscsyl) {
			if (i % 5 == 0)
				data->fan_status[i / 5] =
					i2c_smbus_read_byte_data(client,
						FSCHMD_REG_FAN_STATE
						[data->kind][i / 5]);
			if (data->fan_status[i / 5] & FSCHMD_FAN_DISABLED)
				continue;
		}

		err = device_create_file(&client->dev,
					&fschmd_fan_attr[i].dev_attr);
		if (err)
			goto exit_detach;
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		data->hwmon_dev = NULL;
		goto exit_detach;
	}

	/* We take the data_mutex lock early so that watchdog_open() cannot
	   run when misc_register() has completed, but we've not yet added
	   our data to the watchdog_data_list (and set the default timeout) */
	mutex_lock(&watchdog_data_mutex);
	for (i = 0; i < ARRAY_SIZE(watchdog_minors); i++) {
		/* Register our watchdog part */
		snprintf(data->watchdog_name, sizeof(data->watchdog_name),
			"watchdog%c", (i == 0) ? '\0' : ('0' + i));
		data->watchdog_miscdev.name = data->watchdog_name;
		data->watchdog_miscdev.fops = &watchdog_fops;
		data->watchdog_miscdev.minor = watchdog_minors[i];
		err = misc_register(&data->watchdog_miscdev);
		if (err == -EBUSY)
			continue;
		if (err) {
			data->watchdog_miscdev.minor = 0;
			dev_err(&client->dev,
				"Registering watchdog chardev: %d\n", err);
			break;
		}

		list_add(&data->list, &watchdog_data_list);
		watchdog_set_timeout(data, 60);
		dev_info(&client->dev,
			"Registered watchdog chardev major 10, minor: %d\n",
			watchdog_minors[i]);
		break;
	}
	if (i == ARRAY_SIZE(watchdog_minors)) {
		data->watchdog_miscdev.minor = 0;
		dev_warn(&client->dev, "Couldn't register watchdog chardev "
			"(due to no free minor)\n");
	}
	mutex_unlock(&watchdog_data_mutex);

	dev_info(&client->dev, "Detected FSC %s chip, revision: %d\n",
		names[data->kind], (int) data->revision);

	return 0;

exit_detach:
	fschmd_remove(client); /* will also free data for us */
	return err;
}

static int fschmd_remove(struct i2c_client *client)
{
	struct fschmd_data *data = i2c_get_clientdata(client);
	int i;

	/* Unregister the watchdog (if registered) */
	if (data->watchdog_miscdev.minor) {
		misc_deregister(&data->watchdog_miscdev);
		if (data->watchdog_is_open) {
			dev_warn(&client->dev,
				"i2c client detached with watchdog open! "
				"Stopping watchdog.\n");
			watchdog_stop(data);
		}
		mutex_lock(&watchdog_data_mutex);
		list_del(&data->list);
		mutex_unlock(&watchdog_data_mutex);
		/* Tell the watchdog code the client is gone */
		mutex_lock(&data->watchdog_lock);
		data->client = NULL;
		mutex_unlock(&data->watchdog_lock);
	}

	/* Check if registered in case we're called from fschmd_detect
	   to cleanup after an error */
	if (data->hwmon_dev)
		hwmon_device_unregister(data->hwmon_dev);

	device_remove_file(&client->dev, &dev_attr_alert_led);
	for (i = 0; i < (FSCHMD_NO_VOLT_SENSORS[data->kind]); i++)
		device_remove_file(&client->dev, &fschmd_attr[i].dev_attr);
	for (i = 0; i < (FSCHMD_NO_TEMP_SENSORS[data->kind] * 4); i++)
		device_remove_file(&client->dev,
					&fschmd_temp_attr[i].dev_attr);
	for (i = 0; i < (FSCHMD_NO_FAN_SENSORS[data->kind] * 5); i++)
		device_remove_file(&client->dev,
					&fschmd_fan_attr[i].dev_attr);

	mutex_lock(&watchdog_data_mutex);
	kref_put(&data->kref, fschmd_release_resources);
	mutex_unlock(&watchdog_data_mutex);

	return 0;
}

static struct fschmd_data *fschmd_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fschmd_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + 2 * HZ) || !data->valid) {

		for (i = 0; i < FSCHMD_NO_TEMP_SENSORS[data->kind]; i++) {
			data->temp_act[i] = i2c_smbus_read_byte_data(client,
					FSCHMD_REG_TEMP_ACT[data->kind][i]);
			data->temp_status[i] = i2c_smbus_read_byte_data(client,
					FSCHMD_REG_TEMP_STATE[data->kind][i]);

			/* The fscpos doesn't have TEMP_LIMIT registers */
			if (FSCHMD_REG_TEMP_LIMIT[data->kind][i])
				data->temp_max[i] = i2c_smbus_read_byte_data(
					client,
					FSCHMD_REG_TEMP_LIMIT[data->kind][i]);

			/* reset alarm if the alarm condition is gone,
			   the chip doesn't do this itself */
			if ((data->temp_status[i] & FSCHMD_TEMP_ALARM_MASK) ==
					FSCHMD_TEMP_ALARM_MASK &&
					data->temp_act[i] < data->temp_max[i])
				i2c_smbus_write_byte_data(client,
					FSCHMD_REG_TEMP_STATE[data->kind][i],
					data->temp_status[i]);
		}

		for (i = 0; i < FSCHMD_NO_FAN_SENSORS[data->kind]; i++) {
			data->fan_act[i] = i2c_smbus_read_byte_data(client,
					FSCHMD_REG_FAN_ACT[data->kind][i]);
			data->fan_status[i] = i2c_smbus_read_byte_data(client,
					FSCHMD_REG_FAN_STATE[data->kind][i]);
			data->fan_ripple[i] = i2c_smbus_read_byte_data(client,
					FSCHMD_REG_FAN_RIPPLE[data->kind][i]);

			/* The fscpos third fan doesn't have a fan_min */
			if (FSCHMD_REG_FAN_MIN[data->kind][i])
				data->fan_min[i] = i2c_smbus_read_byte_data(
					client,
					FSCHMD_REG_FAN_MIN[data->kind][i]);

			/* reset fan status if speed is back to > 0 */
			if ((data->fan_status[i] & FSCHMD_FAN_ALARM) &&
					data->fan_act[i])
				i2c_smbus_write_byte_data(client,
					FSCHMD_REG_FAN_STATE[data->kind][i],
					data->fan_status[i]);
		}

		for (i = 0; i < FSCHMD_NO_VOLT_SENSORS[data->kind]; i++)
			data->volt[i] = i2c_smbus_read_byte_data(client,
					       FSCHMD_REG_VOLT[data->kind][i]);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init fschmd_init(void)
{
	return i2c_add_driver(&fschmd_driver);
}

static void __exit fschmd_exit(void)
{
	i2c_del_driver(&fschmd_driver);
}

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("FSC Poseidon, Hermes, Scylla, Heracles, Heimdall, Hades "
			"and Syleus driver");
MODULE_LICENSE("GPL");

module_init(fschmd_init);
module_exit(fschmd_exit);
