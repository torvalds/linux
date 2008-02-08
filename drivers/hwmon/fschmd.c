/* fschmd.c
 *
 * Copyright (C) 2007 Hans de Goede <j.w.r.degoede@hhs.nl>
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
 *  Scylla, Heracles and Heimdall chips
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

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x73, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_5(fscpos, fscher, fscscy, fschrc, fschmd);

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

#define FSCHMD_CONTROL_ALERT_LED_MASK	0x01

/* watchdog (support to be implemented) */
#define FSCHMD_REG_WDOG_PRESET		0x28
#define FSCHMD_REG_WDOG_STATE		0x23
#define FSCHMD_REG_WDOG_CONTROL		0x21

/* voltages, weird order is to keep the same order as the old drivers */
static const u8 FSCHMD_REG_VOLT[3] = { 0x45, 0x42, 0x48 };

/* minimum pwm at which the fan is driven (pwm can by increased depending on
   the temp. Notice that for the scy some fans share there minimum speed.
   Also notice that with the scy the sensor order is different then with the
   other chips, this order was in the 2.4 driver and kept for consistency. */
static const u8 FSCHMD_REG_FAN_MIN[5][6] = {
	{ 0x55, 0x65 },					/* pos */
	{ 0x55, 0x65, 0xb5 },				/* her */
	{ 0x65, 0x65, 0x55, 0xa5, 0x55, 0xa5 },		/* scy */
	{ 0x55, 0x65, 0xa5, 0xb5 },			/* hrc */
	{ 0x55, 0x65, 0xa5, 0xb5, 0xc5 },		/* hmd */
};

/* actual fan speed */
static const u8 FSCHMD_REG_FAN_ACT[5][6] = {
	{ 0x0e, 0x6b, 0xab },				/* pos */
	{ 0x0e, 0x6b, 0xbb },				/* her */
	{ 0x6b, 0x6c, 0x0e, 0xab, 0x5c, 0xbb },		/* scy */
	{ 0x0e, 0x6b, 0xab, 0xbb },			/* hrc */
	{ 0x5b, 0x6b, 0xab, 0xbb, 0xcb },		/* hmd */
};

/* fan status registers */
static const u8 FSCHMD_REG_FAN_STATE[5][6] = {
	{ 0x0d, 0x62, 0xa2 },				/* pos */
	{ 0x0d, 0x62, 0xb2 },				/* her */
	{ 0x62, 0x61, 0x0d, 0xa2, 0x52, 0xb2 },		/* scy */
	{ 0x0d, 0x62, 0xa2, 0xb2 },			/* hrc */
	{ 0x52, 0x62, 0xa2, 0xb2, 0xc2 },		/* hmd */
};

/* fan ripple / divider registers */
static const u8 FSCHMD_REG_FAN_RIPPLE[5][6] = {
	{ 0x0f, 0x6f, 0xaf },				/* pos */
	{ 0x0f, 0x6f, 0xbf },				/* her */
	{ 0x6f, 0x6f, 0x0f, 0xaf, 0x0f, 0xbf },		/* scy */
	{ 0x0f, 0x6f, 0xaf, 0xbf },			/* hrc */
	{ 0x5f, 0x6f, 0xaf, 0xbf, 0xcf },		/* hmd */
};

static const int FSCHMD_NO_FAN_SENSORS[5] = { 3, 3, 6, 4, 5 };

/* Fan status register bitmasks */
#define FSCHMD_FAN_ALARM_MASK		0x04 /* called fault by FSC! */
#define FSCHMD_FAN_NOT_PRESENT_MASK	0x08 /* not documented */


/* actual temperature registers */
static const u8 FSCHMD_REG_TEMP_ACT[5][5] = {
	{ 0x64, 0x32, 0x35 },				/* pos */
	{ 0x64, 0x32, 0x35 },				/* her */
	{ 0x64, 0xD0, 0x32, 0x35 },			/* scy */
	{ 0x64, 0x32, 0x35 },				/* hrc */
	{ 0x70, 0x80, 0x90, 0xd0, 0xe0 },		/* hmd */
};

/* temperature state registers */
static const u8 FSCHMD_REG_TEMP_STATE[5][5] = {
	{ 0x71, 0x81, 0x91 },				/* pos */
	{ 0x71, 0x81, 0x91 },				/* her */
	{ 0x71, 0xd1, 0x81, 0x91 },			/* scy */
	{ 0x71, 0x81, 0x91 },				/* hrc */
	{ 0x71, 0x81, 0x91, 0xd1, 0xe1 },		/* hmd */
};

/* temperature high limit registers, FSC does not document these. Proven to be
   there with field testing on the fscher and fschrc, already supported / used
   in the fscscy 2.4 driver. FSC has confirmed that the fschmd has registers
   at these addresses, but doesn't want to confirm they are the same as with
   the fscher?? */
static const u8 FSCHMD_REG_TEMP_LIMIT[5][5] = {
	{ 0, 0, 0 },					/* pos */
	{ 0x76, 0x86, 0x96 },				/* her */
	{ 0x76, 0xd6, 0x86, 0x96 },			/* scy */
	{ 0x76, 0x86, 0x96 },				/* hrc */
	{ 0x76, 0x86, 0x96, 0xd6, 0xe6 },		/* hmd */
};

/* These were found through experimenting with an fscher, currently they are
   not used, but we keep them around for future reference.
static const u8 FSCHER_REG_TEMP_AUTOP1[] =	{ 0x73, 0x83, 0x93 };
static const u8 FSCHER_REG_TEMP_AUTOP2[] =	{ 0x75, 0x85, 0x95 }; */

static const int FSCHMD_NO_TEMP_SENSORS[5] = { 3, 3, 4, 3, 5 };

/* temp status register bitmasks */
#define FSCHMD_TEMP_WORKING_MASK	0x01
#define FSCHMD_TEMP_ALERT_MASK		0x02
/* there only really is an alarm if the sensor is working and alert == 1 */
#define FSCHMD_TEMP_ALARM_MASK \
	(FSCHMD_TEMP_WORKING_MASK | FSCHMD_TEMP_ALERT_MASK)

/* our driver name */
#define FSCHMD_NAME "fschmd"

/*
 * Functions declarations
 */

static int fschmd_attach_adapter(struct i2c_adapter *adapter);
static int fschmd_detach_client(struct i2c_client *client);
static struct fschmd_data *fschmd_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

static struct i2c_driver fschmd_driver = {
	.driver = {
		.name	= FSCHMD_NAME,
	},
	.attach_adapter	= fschmd_attach_adapter,
	.detach_client	= fschmd_detach_client,
};

/*
 * Client data (each client gets its own)
 */

struct fschmd_data {
	struct i2c_client client;
	struct device *hwmon_dev;
	struct mutex update_lock;
	int kind;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* register values */
	u8 global_control;	/* global control register */
	u8 volt[3];		/* 12, 5, battery voltage */
	u8 temp_act[5];		/* temperature */
	u8 temp_status[5];	/* status of sensor */
	u8 temp_max[5];		/* high temp limit, notice: undocumented! */
	u8 fan_act[6];		/* fans revolutions per second */
	u8 fan_status[6];	/* fan status */
	u8 fan_min[6];		/* fan min value for rps */
	u8 fan_ripple[6];	/* divider for rps */
};

/* Global variables to hold information read from special DMI tables, which are
   available on FSC machines with an fscher or later chip. */
static int dmi_mult[3] = { 490, 200, 100 };
static int dmi_offset[3] = { 0, 0, 0 };
static int dmi_vref = -1;


/*
 * Sysfs attr show / store functions
 */

static ssize_t show_in_value(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	const int max_reading[3] = { 14200, 6600, 3300 };
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	/* fscher / fschrc - 1 as data->kind is an array index, not a chips */
	if (data->kind == (fscher - 1) || data->kind >= (fschrc - 1))
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
	i2c_smbus_write_byte_data(&data->client,
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
	if (data->temp_status[index] & FSCHMD_TEMP_WORKING_MASK)
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

	reg = i2c_smbus_read_byte_data(&data->client,
		FSCHMD_REG_FAN_RIPPLE[data->kind][index]);

	/* bits 2..7 reserved => mask with 0x03 */
	reg &= ~0x03;
	reg |= v;

	i2c_smbus_write_byte_data(&data->client,
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

	if (data->fan_status[index] & FSCHMD_FAN_ALARM_MASK)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_fan_fault(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = fschmd_update_device(dev);

	if (data->fan_status[index] & FSCHMD_FAN_NOT_PRESENT_MASK)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}


static ssize_t show_pwm_auto_point1_pwm(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	int val = fschmd_update_device(dev)->fan_min[index];

	/* 0 = allow turning off, 1-255 = 50-100% */
	if (val)
		val = val / 2 + 128;

	return sprintf(buf, "%d\n", val);
}

static ssize_t store_pwm_auto_point1_pwm(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct fschmd_data *data = dev_get_drvdata(dev);
	unsigned long v = simple_strtoul(buf, NULL, 10);

	/* register: 0 = allow turning off, 1-255 = 50-100% */
	if (v) {
		v = SENSORS_LIMIT(v, 128, 255);
		v = (v - 128) * 2 + 1;
	}

	mutex_lock(&data->update_lock);

	i2c_smbus_write_byte_data(&data->client,
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

	if (data->global_control & FSCHMD_CONTROL_ALERT_LED_MASK)
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

	reg = i2c_smbus_read_byte_data(&data->client, FSCHMD_REG_CONTROL);

	if (v)
		reg |= FSCHMD_CONTROL_ALERT_LED_MASK;
	else
		reg &= ~FSCHMD_CONTROL_ALERT_LED_MASK;

	i2c_smbus_write_byte_data(&data->client, FSCHMD_REG_CONTROL, reg);

	data->global_control = reg;

	mutex_unlock(&data->update_lock);

	return count;
}

static struct sensor_device_attribute fschmd_attr[] = {
	SENSOR_ATTR(in0_input, 0444, show_in_value, NULL, 0),
	SENSOR_ATTR(in1_input, 0444, show_in_value, NULL, 1),
	SENSOR_ATTR(in2_input, 0444, show_in_value, NULL, 2),
	SENSOR_ATTR(alert_led, 0644, show_alert_led, store_alert_led, 0),
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
};


/*
 * Real code
 */

/* DMI decode routine to read voltage scaling factors from special DMI tables,
   which are available on FSC machines with an fscher or later chip. */
static void fschmd_dmi_decode(const struct dmi_header *header)
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
		dmi_vref = vref;
	}
}

static int fschmd_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct fschmd_data *data;
	u8 revision;
	const char * const names[5] = { "Poseidon", "Hermes", "Scylla",
					"Heracles", "Heimdall" };
	const char * const client_names[5] = { "fscpos", "fscher", "fscscy",
						"fschrc", "fschmd" };
	int i, err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	/* OK. For now, we presume we have a valid client. We now create the
	 * client structure, even though we cannot fill it completely yet.
	 * But it allows us to access i2c_smbus_read_byte_data. */
	if (!(data = kzalloc(sizeof(struct fschmd_data), GFP_KERNEL)))
		return -ENOMEM;

	client = &data->client;
	i2c_set_clientdata(client, data);
	client->addr = address;
	client->adapter = adapter;
	client->driver = &fschmd_driver;
	mutex_init(&data->update_lock);

	/* Detect & Identify the chip */
	if (kind <= 0) {
		char id[4];

		id[0] = i2c_smbus_read_byte_data(client,
				FSCHMD_REG_IDENT_0);
		id[1] = i2c_smbus_read_byte_data(client,
				FSCHMD_REG_IDENT_1);
		id[2] = i2c_smbus_read_byte_data(client,
				FSCHMD_REG_IDENT_2);
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
		else
			goto exit_free;
	}

	if (kind == fscpos) {
		/* The Poseidon has hardwired temp limits, fill these
		   in for the alarm resetting code */
		data->temp_max[0] = 70 + 128;
		data->temp_max[1] = 50 + 128;
		data->temp_max[2] = 50 + 128;
	}

	/* Read the special DMI table for fscher and newer chips */
	if (kind == fscher || kind >= fschrc) {
		dmi_walk(fschmd_dmi_decode);
		if (dmi_vref == -1) {
			printk(KERN_WARNING FSCHMD_NAME
				": Couldn't get voltage scaling factors from "
				"BIOS DMI table, using builtin defaults\n");
			dmi_vref = 33;
		}
	}

	/* i2c kind goes from 1-5, we want from 0-4 to address arrays */
	data->kind = kind - 1;
	strlcpy(client->name, client_names[data->kind], I2C_NAME_SIZE);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(client)))
		goto exit_free;

	for (i = 0; i < ARRAY_SIZE(fschmd_attr); i++) {
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

	revision = i2c_smbus_read_byte_data(client, FSCHMD_REG_REVISION);
	printk(KERN_INFO FSCHMD_NAME ": Detected FSC %s chip, revision: %d\n",
		names[data->kind], (int) revision);

	return 0;

exit_detach:
	fschmd_detach_client(client); /* will also free data for us */
	return err;

exit_free:
	kfree(data);
	return err;
}

static int fschmd_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, fschmd_detect);
}

static int fschmd_detach_client(struct i2c_client *client)
{
	struct fschmd_data *data = i2c_get_clientdata(client);
	int i, err;

	/* Check if registered in case we're called from fschmd_detect
	   to cleanup after an error */
	if (data->hwmon_dev)
		hwmon_device_unregister(data->hwmon_dev);

	for (i = 0; i < ARRAY_SIZE(fschmd_attr); i++)
		device_remove_file(&client->dev, &fschmd_attr[i].dev_attr);
	for (i = 0; i < (FSCHMD_NO_TEMP_SENSORS[data->kind] * 4); i++)
		device_remove_file(&client->dev,
					&fschmd_temp_attr[i].dev_attr);
	for (i = 0; i < (FSCHMD_NO_FAN_SENSORS[data->kind] * 5); i++)
		device_remove_file(&client->dev,
					&fschmd_fan_attr[i].dev_attr);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);
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
					FSCHMD_TEMP_ALERT_MASK);
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
			if ((data->fan_status[i] & FSCHMD_FAN_ALARM_MASK) &&
					data->fan_act[i])
				i2c_smbus_write_byte_data(client,
					FSCHMD_REG_FAN_STATE[data->kind][i],
					FSCHMD_FAN_ALARM_MASK);
		}

		for (i = 0; i < 3; i++)
			data->volt[i] = i2c_smbus_read_byte_data(client,
						FSCHMD_REG_VOLT[i]);

		data->global_control = i2c_smbus_read_byte_data(client,
						FSCHMD_REG_CONTROL);

		/* To be implemented in the future
		data->watchdog[0] = i2c_smbus_read_byte_data(client,
						FSCHMD_REG_WDOG_PRESET);
		data->watchdog[1] = i2c_smbus_read_byte_data(client,
						FSCHMD_REG_WDOG_STATE);
		data->watchdog[2] = i2c_smbus_read_byte_data(client,
						FSCHMD_REG_WDOG_CONTROL); */

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

MODULE_AUTHOR("Hans de Goede <j.w.r.degoede@hhs.nl>");
MODULE_DESCRIPTION("FSC Poseidon, Hermes, Scylla, Heracles and "
			"Heimdall driver");
MODULE_LICENSE("GPL");

module_init(fschmd_init);
module_exit(fschmd_exit);
