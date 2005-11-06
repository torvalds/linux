/*
	fscpos.c - Kernel module for hardware monitoring with FSC Poseidon chips
	Copyright (C) 2004, 2005 Stefan Ott <stefan@desire.ch>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
	fujitsu siemens poseidon chip,
	module based on the old fscpos module by Hermann Jung <hej@odn.de> and
	the fscher module by Reinhard Nissl <rnissl@gmx.de>

	original module based on lm80.c
	Copyright (C) 1998, 1999 Frodo Looijaard <frodol@dds.nl>
	and Philip Edelbrock <phil@netroedge.com>

	Thanks to Jean Delvare for reviewing my code and suggesting a lot of
	improvements.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/hwmon.h>
#include <linux/err.h>

/*
 * Addresses to scan
 */
static unsigned short normal_i2c[] = { 0x73, I2C_CLIENT_END };

/*
 * Insmod parameters
 */
I2C_CLIENT_INSMOD_1(fscpos);

/*
 * The FSCPOS registers
 */

/* chip identification */
#define FSCPOS_REG_IDENT_0		0x00
#define FSCPOS_REG_IDENT_1		0x01
#define FSCPOS_REG_IDENT_2		0x02
#define FSCPOS_REG_REVISION		0x03

/* global control and status */
#define FSCPOS_REG_EVENT_STATE		0x04
#define FSCPOS_REG_CONTROL		0x05

/* watchdog */
#define FSCPOS_REG_WDOG_PRESET		0x28
#define FSCPOS_REG_WDOG_STATE		0x23
#define FSCPOS_REG_WDOG_CONTROL		0x21

/* voltages */
#define FSCPOS_REG_VOLT_12		0x45
#define FSCPOS_REG_VOLT_5		0x42
#define FSCPOS_REG_VOLT_BATT		0x48

/* fans - the chip does not support minimum speed for fan2 */
static u8 FSCPOS_REG_PWM[] = { 0x55, 0x65 };
static u8 FSCPOS_REG_FAN_ACT[] = { 0x0e, 0x6b, 0xab };
static u8 FSCPOS_REG_FAN_STATE[] = { 0x0d, 0x62, 0xa2 };
static u8 FSCPOS_REG_FAN_RIPPLE[] = { 0x0f, 0x6f, 0xaf };

/* temperatures */
static u8 FSCPOS_REG_TEMP_ACT[] = { 0x64, 0x32, 0x35 };
static u8 FSCPOS_REG_TEMP_STATE[] = { 0x71, 0x81, 0x91 };

/*
 * Functions declaration
 */
static int fscpos_attach_adapter(struct i2c_adapter *adapter);
static int fscpos_detect(struct i2c_adapter *adapter, int address, int kind);
static int fscpos_detach_client(struct i2c_client *client);

static int fscpos_read_value(struct i2c_client *client, u8 register);
static int fscpos_write_value(struct i2c_client *client, u8 register, u8 value);
static struct fscpos_data *fscpos_update_device(struct device *dev);
static void fscpos_init_client(struct i2c_client *client);

static void reset_fan_alarm(struct i2c_client *client, int nr);

/*
 * Driver data (common to all clients)
 */
static struct i2c_driver fscpos_driver = {
	.owner		= THIS_MODULE,
	.name		= "fscpos",
	.id		= I2C_DRIVERID_FSCPOS,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= fscpos_attach_adapter,
	.detach_client	= fscpos_detach_client,
};

/*
 * Client data (each client gets its own)
 */
struct fscpos_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore update_lock;
	char valid; 		/* 0 until following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* register values */
	u8 revision;		/* revision of chip */
	u8 global_event;	/* global event status */
	u8 global_control;	/* global control register */
	u8 wdog_control;	/* watchdog control */
	u8 wdog_state;		/* watchdog status */
	u8 wdog_preset;		/* watchdog preset */
	u8 volt[3];		/* 12, 5, battery current */
	u8 temp_act[3];		/* temperature */
	u8 temp_status[3];	/* status of sensor */
	u8 fan_act[3];		/* fans revolutions per second */
	u8 fan_status[3];	/* fan status */
	u8 pwm[2];		/* fan min value for rps */
	u8 fan_ripple[3];	/* divider for rps */
};

/* Temperature */
#define TEMP_FROM_REG(val)	(((val) - 128) * 1000)

static ssize_t show_temp_input(struct fscpos_data *data, char *buf, int nr)
{
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_act[nr - 1]));
}

static ssize_t show_temp_status(struct fscpos_data *data, char *buf, int nr)
{
	/* bits 2..7 reserved => mask with 0x03 */
	return sprintf(buf, "%u\n", data->temp_status[nr - 1] & 0x03);
}

static ssize_t show_temp_reset(struct fscpos_data *data, char *buf, int nr)
{
	return sprintf(buf, "1\n");
}

static ssize_t set_temp_reset(struct i2c_client *client, struct fscpos_data
			*data, const char *buf,	size_t count, int nr, int reg)
{
	unsigned long v = simple_strtoul(buf, NULL, 10);
	if (v != 1) {
		dev_err(&client->dev, "temp_reset value %ld not supported. "
					"Use 1 to reset the alarm!\n", v);
		return -EINVAL;
	}

	dev_info(&client->dev, "You used the temp_reset feature which has not "
				"been proplerly tested. Please report your "
				"experience to the module author.\n");

	/* Supported value: 2 (clears the status) */
	fscpos_write_value(client, FSCPOS_REG_TEMP_STATE[nr - 1], 2);
	return count;
}

/* Fans */
#define RPM_FROM_REG(val)	((val) * 60)

static ssize_t show_fan_status(struct fscpos_data *data, char *buf, int nr)
{
	/* bits 0..1, 3..7 reserved => mask with 0x04 */
	return sprintf(buf, "%u\n", data->fan_status[nr - 1] & 0x04);
}

static ssize_t show_fan_input(struct fscpos_data *data, char *buf, int nr)
{
	return sprintf(buf, "%u\n", RPM_FROM_REG(data->fan_act[nr - 1]));
}

static ssize_t show_fan_ripple(struct fscpos_data *data, char *buf, int nr)
{
	/* bits 2..7 reserved => mask with 0x03 */
	return sprintf(buf, "%u\n", data->fan_ripple[nr - 1] & 0x03);
}

static ssize_t set_fan_ripple(struct i2c_client *client, struct fscpos_data
			*data, const char *buf,	size_t count, int nr, int reg)
{
	/* supported values: 2, 4, 8 */
	unsigned long v = simple_strtoul(buf, NULL, 10);

	switch (v) {
		case 2: v = 1; break;
		case 4: v = 2; break;
		case 8: v = 3; break;
	default:
		dev_err(&client->dev, "fan_ripple value %ld not supported. "
					"Must be one of 2, 4 or 8!\n", v);
		return -EINVAL;
	}
	
	down(&data->update_lock);
	/* bits 2..7 reserved => mask with 0x03 */
	data->fan_ripple[nr - 1] &= ~0x03;
	data->fan_ripple[nr - 1] |= v;
	
	fscpos_write_value(client, reg, data->fan_ripple[nr - 1]);
	up(&data->update_lock);
	return count;
}

static ssize_t show_pwm(struct fscpos_data *data, char *buf, int nr)
{
	return sprintf(buf, "%u\n", data->pwm[nr - 1]);
}

static ssize_t set_pwm(struct i2c_client *client, struct fscpos_data *data,
				const char *buf, size_t count, int nr, int reg)
{
	unsigned long v = simple_strtoul(buf, NULL, 10);

	/* Range: 0..255 */
	if (v < 0) v = 0;
	if (v > 255) v = 255;

	down(&data->update_lock);
	data->pwm[nr - 1] = v;
	fscpos_write_value(client, reg, data->pwm[nr - 1]);
	up(&data->update_lock);
	return count;
}

static void reset_fan_alarm(struct i2c_client *client, int nr)
{
	fscpos_write_value(client, FSCPOS_REG_FAN_STATE[nr], 4);
}

/* Volts */
#define VOLT_FROM_REG(val, mult)	((val) * (mult) / 255)

static ssize_t show_volt_12(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fscpos_data *data = fscpos_update_device(dev);
	return sprintf(buf, "%u\n", VOLT_FROM_REG(data->volt[0], 14200));
}

static ssize_t show_volt_5(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fscpos_data *data = fscpos_update_device(dev);
	return sprintf(buf, "%u\n", VOLT_FROM_REG(data->volt[1], 6600));
}

static ssize_t show_volt_batt(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fscpos_data *data = fscpos_update_device(dev);
	return sprintf(buf, "%u\n", VOLT_FROM_REG(data->volt[2], 3300));
}

/* Watchdog */
static ssize_t show_wdog_control(struct fscpos_data *data, char *buf)
{
	/* bits 0..3 reserved, bit 6 write only => mask with 0xb0 */
	return sprintf(buf, "%u\n", data->wdog_control & 0xb0);
}

static ssize_t set_wdog_control(struct i2c_client *client, struct fscpos_data
				*data, const char *buf,	size_t count, int reg)
{
	/* bits 0..3 reserved => mask with 0xf0 */
	unsigned long v = simple_strtoul(buf, NULL, 10) & 0xf0;

	down(&data->update_lock);
	data->wdog_control &= ~0xf0;
	data->wdog_control |= v;
	fscpos_write_value(client, reg, data->wdog_control);
	up(&data->update_lock);
	return count;
}

static ssize_t show_wdog_state(struct fscpos_data *data, char *buf)
{
	/* bits 0, 2..7 reserved => mask with 0x02 */
	return sprintf(buf, "%u\n", data->wdog_state & 0x02);
}

static ssize_t set_wdog_state(struct i2c_client *client, struct fscpos_data
				*data, const char *buf, size_t count, int reg)
{
	unsigned long v = simple_strtoul(buf, NULL, 10) & 0x02;

	/* Valid values: 2 (clear) */
	if (v != 2) {
		dev_err(&client->dev, "wdog_state value %ld not supported. "
					"Must be 2 to clear the state!\n", v);
		return -EINVAL;
	}

	down(&data->update_lock);
	data->wdog_state &= ~v;
	fscpos_write_value(client, reg, v);
	up(&data->update_lock);
	return count;
}

static ssize_t show_wdog_preset(struct fscpos_data *data, char *buf)
{
	return sprintf(buf, "%u\n", data->wdog_preset);
}

static ssize_t set_wdog_preset(struct i2c_client *client, struct fscpos_data
				*data, const char *buf,	size_t count, int reg)
{
	unsigned long v = simple_strtoul(buf, NULL, 10) & 0xff;

	down(&data->update_lock);
	data->wdog_preset = v;
	fscpos_write_value(client, reg, data->wdog_preset);
	up(&data->update_lock);
	return count;
}

/* Event */
static ssize_t show_event(struct device *dev, struct device_attribute *attr, char *buf)
{
	/* bits 5..7 reserved => mask with 0x1f */
	struct fscpos_data *data = fscpos_update_device(dev);
	return sprintf(buf, "%u\n", data->global_event & 0x9b);
}

/*
 * Sysfs stuff
 */
#define create_getter(kind, sub) \
	static ssize_t sysfs_show_##kind##sub(struct device *dev, struct device_attribute *attr, char *buf) \
	{ \
		struct fscpos_data *data = fscpos_update_device(dev); \
		return show_##kind##sub(data, buf); \
	}

#define create_getter_n(kind, offset, sub) \
	static ssize_t sysfs_show_##kind##offset##sub(struct device *dev, struct device_attribute *attr, char\
								 	*buf) \
	{ \
		struct fscpos_data *data = fscpos_update_device(dev); \
		return show_##kind##sub(data, buf, offset); \
	}

#define create_setter(kind, sub, reg) \
	static ssize_t sysfs_set_##kind##sub (struct device *dev, struct device_attribute *attr, const char \
							*buf, size_t count) \
	{ \
		struct i2c_client *client = to_i2c_client(dev); \
		struct fscpos_data *data = i2c_get_clientdata(client); \
		return set_##kind##sub(client, data, buf, count, reg); \
	}

#define create_setter_n(kind, offset, sub, reg) \
	static ssize_t sysfs_set_##kind##offset##sub (struct device *dev, struct device_attribute *attr, \
					const char *buf, size_t count) \
	{ \
		struct i2c_client *client = to_i2c_client(dev); \
		struct fscpos_data *data = i2c_get_clientdata(client); \
		return set_##kind##sub(client, data, buf, count, offset, reg);\
	}

#define create_sysfs_device_ro(kind, sub, offset) \
	static DEVICE_ATTR(kind##offset##sub, S_IRUGO, \
					sysfs_show_##kind##offset##sub, NULL);

#define create_sysfs_device_rw(kind, sub, offset) \
	static DEVICE_ATTR(kind##offset##sub, S_IRUGO | S_IWUSR, \
		sysfs_show_##kind##offset##sub, sysfs_set_##kind##offset##sub);

#define sysfs_ro_n(kind, sub, offset) \
	create_getter_n(kind, offset, sub); \
	create_sysfs_device_ro(kind, sub, offset);

#define sysfs_rw_n(kind, sub, offset, reg) \
	create_getter_n(kind, offset, sub); \
	create_setter_n(kind, offset, sub, reg); \
	create_sysfs_device_rw(kind, sub, offset);

#define sysfs_rw(kind, sub, reg) \
	create_getter(kind, sub); \
	create_setter(kind, sub, reg); \
	create_sysfs_device_rw(kind, sub,);

#define sysfs_fan_with_min(offset, reg_status, reg_ripple, reg_min) \
	sysfs_fan(offset, reg_status, reg_ripple); \
	sysfs_rw_n(pwm,, offset, reg_min);

#define sysfs_fan(offset, reg_status, reg_ripple) \
	sysfs_ro_n(fan, _input, offset); \
	sysfs_ro_n(fan, _status, offset); \
	sysfs_rw_n(fan, _ripple, offset, reg_ripple);

#define sysfs_temp(offset, reg_status) \
	sysfs_ro_n(temp, _input, offset); \
	sysfs_ro_n(temp, _status, offset); \
	sysfs_rw_n(temp, _reset, offset, reg_status);

#define sysfs_watchdog(reg_wdog_preset, reg_wdog_state, reg_wdog_control) \
	sysfs_rw(wdog, _control, reg_wdog_control); \
	sysfs_rw(wdog, _preset, reg_wdog_preset); \
	sysfs_rw(wdog, _state, reg_wdog_state);

sysfs_fan_with_min(1, FSCPOS_REG_FAN_STATE[0], FSCPOS_REG_FAN_RIPPLE[0],
							FSCPOS_REG_PWM[0]);
sysfs_fan_with_min(2, FSCPOS_REG_FAN_STATE[1], FSCPOS_REG_FAN_RIPPLE[1],
							FSCPOS_REG_PWM[1]);
sysfs_fan(3, FSCPOS_REG_FAN_STATE[2], FSCPOS_REG_FAN_RIPPLE[2]);

sysfs_temp(1, FSCPOS_REG_TEMP_STATE[0]);
sysfs_temp(2, FSCPOS_REG_TEMP_STATE[1]);
sysfs_temp(3, FSCPOS_REG_TEMP_STATE[2]);

sysfs_watchdog(FSCPOS_REG_WDOG_PRESET, FSCPOS_REG_WDOG_STATE,
						FSCPOS_REG_WDOG_CONTROL);

static DEVICE_ATTR(event, S_IRUGO, show_event, NULL);
static DEVICE_ATTR(in0_input, S_IRUGO, show_volt_12, NULL);
static DEVICE_ATTR(in1_input, S_IRUGO, show_volt_5, NULL);
static DEVICE_ATTR(in2_input, S_IRUGO, show_volt_batt, NULL);

static int fscpos_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, fscpos_detect);
}

static int fscpos_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct fscpos_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	/*
	 * OK. For now, we presume we have a valid client. We now create the
	 * client structure, even though we cannot fill it completely yet.
	 * But it allows us to access fscpos_{read,write}_value.
	 */

	if (!(data = kzalloc(sizeof(struct fscpos_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &fscpos_driver;
	new_client->flags = 0;

	/* Do the remaining detection unless force or force_fscpos parameter */
	if (kind < 0) {
		if ((fscpos_read_value(new_client, FSCPOS_REG_IDENT_0)
			!= 0x50) /* 'P' */
		|| (fscpos_read_value(new_client, FSCPOS_REG_IDENT_1)
			!= 0x45) /* 'E' */
		|| (fscpos_read_value(new_client, FSCPOS_REG_IDENT_2)
			!= 0x47))/* 'G' */
		{
			dev_dbg(&new_client->dev, "fscpos detection failed\n");
			goto exit_free;
		}
	}

	/* Fill in the remaining client fields and put it in the global list */
	strlcpy(new_client->name, "fscpos", I2C_NAME_SIZE);

	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;

	/* Inizialize the fscpos chip */
	fscpos_init_client(new_client);

	/* Announce that the chip was found */
	dev_info(&new_client->dev, "Found fscpos chip, rev %u\n", data->revision);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_detach;
	}

	device_create_file(&new_client->dev, &dev_attr_event);
	device_create_file(&new_client->dev, &dev_attr_in0_input);
	device_create_file(&new_client->dev, &dev_attr_in1_input);
	device_create_file(&new_client->dev, &dev_attr_in2_input);
	device_create_file(&new_client->dev, &dev_attr_wdog_control);
	device_create_file(&new_client->dev, &dev_attr_wdog_preset);
	device_create_file(&new_client->dev, &dev_attr_wdog_state);
	device_create_file(&new_client->dev, &dev_attr_temp1_input);
	device_create_file(&new_client->dev, &dev_attr_temp1_status);
	device_create_file(&new_client->dev, &dev_attr_temp1_reset);
	device_create_file(&new_client->dev, &dev_attr_temp2_input);
	device_create_file(&new_client->dev, &dev_attr_temp2_status);
	device_create_file(&new_client->dev, &dev_attr_temp2_reset);
	device_create_file(&new_client->dev, &dev_attr_temp3_input);
	device_create_file(&new_client->dev, &dev_attr_temp3_status);
	device_create_file(&new_client->dev, &dev_attr_temp3_reset);
	device_create_file(&new_client->dev, &dev_attr_fan1_input);
	device_create_file(&new_client->dev, &dev_attr_fan1_status);
	device_create_file(&new_client->dev, &dev_attr_fan1_ripple);
	device_create_file(&new_client->dev, &dev_attr_pwm1);
	device_create_file(&new_client->dev, &dev_attr_fan2_input);
	device_create_file(&new_client->dev, &dev_attr_fan2_status);
	device_create_file(&new_client->dev, &dev_attr_fan2_ripple);
	device_create_file(&new_client->dev, &dev_attr_pwm2);
	device_create_file(&new_client->dev, &dev_attr_fan3_input);
	device_create_file(&new_client->dev, &dev_attr_fan3_status);
	device_create_file(&new_client->dev, &dev_attr_fan3_ripple);

	return 0;

exit_detach:
	i2c_detach_client(new_client);
exit_free:
	kfree(data);
exit:
	return err;
}

static int fscpos_detach_client(struct i2c_client *client)
{
	struct fscpos_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;
	kfree(data);
	return 0;
}

static int fscpos_read_value(struct i2c_client *client, u8 reg)
{
	dev_dbg(&client->dev, "Read reg 0x%02x\n", reg);
	return i2c_smbus_read_byte_data(client, reg);
}

static int fscpos_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	dev_dbg(&client->dev, "Write reg 0x%02x, val 0x%02x\n", reg, value);
	return i2c_smbus_write_byte_data(client, reg, value);
}

/* Called when we have found a new FSCPOS chip */
static void fscpos_init_client(struct i2c_client *client)
{
	struct fscpos_data *data = i2c_get_clientdata(client);

	/* read revision from chip */
	data->revision = fscpos_read_value(client, FSCPOS_REG_REVISION);
}

static struct fscpos_data *fscpos_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct fscpos_data *data = i2c_get_clientdata(client);

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + 2 * HZ) || !data->valid) {
		int i;

		dev_dbg(&client->dev, "Starting fscpos update\n");

		for (i = 0; i < 3; i++) {
			data->temp_act[i] = fscpos_read_value(client,
						FSCPOS_REG_TEMP_ACT[i]);
			data->temp_status[i] = fscpos_read_value(client,
						FSCPOS_REG_TEMP_STATE[i]);
			data->fan_act[i] = fscpos_read_value(client,
						FSCPOS_REG_FAN_ACT[i]);
			data->fan_status[i] = fscpos_read_value(client,
						FSCPOS_REG_FAN_STATE[i]);
			data->fan_ripple[i] = fscpos_read_value(client,
						FSCPOS_REG_FAN_RIPPLE[i]);
			if (i < 2) {
				/* fan2_min is not supported by the chip */
				data->pwm[i] = fscpos_read_value(client,
							FSCPOS_REG_PWM[i]);
			}
			/* reset fan status if speed is back to > 0 */
			if (data->fan_status[i] != 0 && data->fan_act[i] > 0) {
				reset_fan_alarm(client, i);
			}
		}

		data->volt[0] = fscpos_read_value(client, FSCPOS_REG_VOLT_12);
		data->volt[1] = fscpos_read_value(client, FSCPOS_REG_VOLT_5);
		data->volt[2] = fscpos_read_value(client, FSCPOS_REG_VOLT_BATT);

		data->wdog_preset = fscpos_read_value(client,
							FSCPOS_REG_WDOG_PRESET);
		data->wdog_state = fscpos_read_value(client,
							FSCPOS_REG_WDOG_STATE);
		data->wdog_control = fscpos_read_value(client,
						FSCPOS_REG_WDOG_CONTROL);

		data->global_event = fscpos_read_value(client,
						FSCPOS_REG_EVENT_STATE);

		data->last_updated = jiffies;
		data->valid = 1;
	}
	up(&data->update_lock);
	return data;
}

static int __init sm_fscpos_init(void)
{
	return i2c_add_driver(&fscpos_driver);
}

static void __exit sm_fscpos_exit(void)
{
	i2c_del_driver(&fscpos_driver);
}

MODULE_AUTHOR("Stefan Ott <stefan@desire.ch> based on work from Hermann Jung "
				"<hej@odn.de>, Frodo Looijaard <frodol@dds.nl>"
				" and Philip Edelbrock <phil@netroedge.com>");
MODULE_DESCRIPTION("fujitsu siemens poseidon chip driver");
MODULE_LICENSE("GPL");

module_init(sm_fscpos_init);
module_exit(sm_fscpos_exit);
