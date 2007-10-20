/*
    gl520sm.c - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>,
                              Kyösti Mälkki <kmalkki@cc.hut.fi>
    Copyright (c) 2005        Maarten Deprez <maartendeprez@users.sourceforge.net>

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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

/* Type of the extra sensor */
static unsigned short extra_sensor_type;
module_param(extra_sensor_type, ushort, 0);
MODULE_PARM_DESC(extra_sensor_type, "Type of extra sensor (0=autodetect, 1=temperature, 2=voltage)");

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x2c, 0x2d, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(gl520sm);

/* Many GL520 constants specified below 
One of the inputs can be configured as either temp or voltage.
That's why _TEMP2 and _IN4 access the same register 
*/

/* The GL520 registers */
#define GL520_REG_CHIP_ID		0x00
#define GL520_REG_REVISION		0x01
#define GL520_REG_CONF			0x03
#define GL520_REG_MASK			0x11

#define GL520_REG_VID_INPUT		0x02

#define GL520_REG_IN0_INPUT		0x15
#define GL520_REG_IN0_LIMIT		0x0c
#define GL520_REG_IN0_MIN		GL520_REG_IN0_LIMIT
#define GL520_REG_IN0_MAX		GL520_REG_IN0_LIMIT

#define GL520_REG_IN1_INPUT		0x14
#define GL520_REG_IN1_LIMIT		0x09
#define GL520_REG_IN1_MIN		GL520_REG_IN1_LIMIT
#define GL520_REG_IN1_MAX		GL520_REG_IN1_LIMIT

#define GL520_REG_IN2_INPUT		0x13
#define GL520_REG_IN2_LIMIT		0x0a
#define GL520_REG_IN2_MIN		GL520_REG_IN2_LIMIT
#define GL520_REG_IN2_MAX		GL520_REG_IN2_LIMIT

#define GL520_REG_IN3_INPUT		0x0d
#define GL520_REG_IN3_LIMIT		0x0b
#define GL520_REG_IN3_MIN		GL520_REG_IN3_LIMIT
#define GL520_REG_IN3_MAX		GL520_REG_IN3_LIMIT

#define GL520_REG_IN4_INPUT		0x0e
#define GL520_REG_IN4_MAX		0x17
#define GL520_REG_IN4_MIN		0x18

#define GL520_REG_TEMP1_INPUT		0x04
#define GL520_REG_TEMP1_MAX		0x05
#define GL520_REG_TEMP1_MAX_HYST	0x06

#define GL520_REG_TEMP2_INPUT		0x0e
#define GL520_REG_TEMP2_MAX		0x17
#define GL520_REG_TEMP2_MAX_HYST	0x18

#define GL520_REG_FAN_INPUT		0x07
#define GL520_REG_FAN_MIN		0x08
#define GL520_REG_FAN_DIV		0x0f
#define GL520_REG_FAN_OFF		GL520_REG_FAN_DIV

#define GL520_REG_ALARMS		0x12
#define GL520_REG_BEEP_MASK		0x10
#define GL520_REG_BEEP_ENABLE		GL520_REG_CONF

/*
 * Function declarations
 */

static int gl520_attach_adapter(struct i2c_adapter *adapter);
static int gl520_detect(struct i2c_adapter *adapter, int address, int kind);
static void gl520_init_client(struct i2c_client *client);
static int gl520_detach_client(struct i2c_client *client);
static int gl520_read_value(struct i2c_client *client, u8 reg);
static int gl520_write_value(struct i2c_client *client, u8 reg, u16 value);
static struct gl520_data *gl520_update_device(struct device *dev);

/* Driver data */
static struct i2c_driver gl520_driver = {
	.driver = {
		.name	= "gl520sm",
	},
	.id		= I2C_DRIVERID_GL520,
	.attach_adapter	= gl520_attach_adapter,
	.detach_client	= gl520_detach_client,
};

/* Client data */
struct gl520_data {
	struct i2c_client client;
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid;		/* zero until the following fields are valid */
	unsigned long last_updated;	/* in jiffies */

	u8 vid;
	u8 vrm;
	u8 in_input[5];		/* [0] = VVD */
	u8 in_min[5];		/* [0] = VDD */
	u8 in_max[5];		/* [0] = VDD */
	u8 fan_input[2];
	u8 fan_min[2];
	u8 fan_div[2];
	u8 fan_off;
	u8 temp_input[2];
	u8 temp_max[2];
	u8 temp_max_hyst[2];
	u8 alarms;
	u8 beep_enable;
	u8 beep_mask;
	u8 alarm_mask;
	u8 two_temps;
};

/*
 * Sysfs stuff
 */

#define sysfs_r(type, n, item, reg) \
static ssize_t get_##type##item (struct gl520_data *, char *, int); \
static ssize_t get_##type##n##item (struct device *, struct device_attribute *attr, char *); \
static ssize_t get_##type##n##item (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct gl520_data *data = gl520_update_device(dev); \
	return get_##type##item(data, buf, (n)); \
}

#define sysfs_w(type, n, item, reg) \
static ssize_t set_##type##item (struct i2c_client *, struct gl520_data *, const char *, size_t, int, int); \
static ssize_t set_##type##n##item (struct device *, struct device_attribute *attr, const char *, size_t); \
static ssize_t set_##type##n##item (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct gl520_data *data = i2c_get_clientdata(client); \
	return set_##type##item(client, data, buf, count, (n), reg); \
}

#define sysfs_rw_n(type, n, item, reg) \
sysfs_r(type, n, item, reg) \
sysfs_w(type, n, item, reg) \
static DEVICE_ATTR(type##n##item, S_IRUGO | S_IWUSR, get_##type##n##item, set_##type##n##item);

#define sysfs_ro_n(type, n, item, reg) \
sysfs_r(type, n, item, reg) \
static DEVICE_ATTR(type##n##item, S_IRUGO, get_##type##n##item, NULL);

#define sysfs_rw(type, item, reg) \
sysfs_r(type, 0, item, reg) \
sysfs_w(type, 0, item, reg) \
static DEVICE_ATTR(type##item, S_IRUGO | S_IWUSR, get_##type##0##item, set_##type##0##item);

#define sysfs_ro(type, item, reg) \
sysfs_r(type, 0, item, reg) \
static DEVICE_ATTR(type##item, S_IRUGO, get_##type##0##item, NULL);


#define sysfs_vid(n) \
sysfs_ro_n(cpu, n, _vid, GL520_REG_VID_INPUT)

#define sysfs_in(n) \
sysfs_ro_n(in, n, _input, GL520_REG_IN##n##INPUT) \
sysfs_rw_n(in, n, _min, GL520_REG_IN##n##_MIN) \
sysfs_rw_n(in, n, _max, GL520_REG_IN##n##_MAX) \

#define sysfs_fan(n) \
sysfs_ro_n(fan, n, _input, GL520_REG_FAN_INPUT) \
sysfs_rw_n(fan, n, _min, GL520_REG_FAN_MIN) \
sysfs_rw_n(fan, n, _div, GL520_REG_FAN_DIV)

#define sysfs_fan_off(n) \
sysfs_rw_n(fan, n, _off, GL520_REG_FAN_OFF) \

#define sysfs_temp(n) \
sysfs_ro_n(temp, n, _input, GL520_REG_TEMP##n##_INPUT) \
sysfs_rw_n(temp, n, _max, GL520_REG_TEMP##n##_MAX) \
sysfs_rw_n(temp, n, _max_hyst, GL520_REG_TEMP##n##_MAX_HYST)

#define sysfs_alarms() \
sysfs_ro(alarms, , GL520_REG_ALARMS) \
sysfs_rw(beep_enable, , GL520_REG_BEEP_ENABLE) \
sysfs_rw(beep_mask, , GL520_REG_BEEP_MASK)


sysfs_vid(0)

sysfs_in(0)
sysfs_in(1)
sysfs_in(2)
sysfs_in(3)
sysfs_in(4)

sysfs_fan(1)
sysfs_fan(2)
sysfs_fan_off(1)

sysfs_temp(1)
sysfs_temp(2)

sysfs_alarms()


static ssize_t get_cpu_vid(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%u\n", vid_from_reg(data->vid, data->vrm));
}

#define VDD_FROM_REG(val) (((val)*95+2)/4)
#define VDD_TO_REG(val) (SENSORS_LIMIT((((val)*4+47)/95),0,255))

#define IN_FROM_REG(val) ((val)*19)
#define IN_TO_REG(val) (SENSORS_LIMIT((((val)+9)/19),0,255))

static ssize_t get_in_input(struct gl520_data *data, char *buf, int n)
{
	u8 r = data->in_input[n];

	if (n == 0)
		return sprintf(buf, "%d\n", VDD_FROM_REG(r));
	else
		return sprintf(buf, "%d\n", IN_FROM_REG(r));
}

static ssize_t get_in_min(struct gl520_data *data, char *buf, int n)
{
	u8 r = data->in_min[n];

	if (n == 0)
		return sprintf(buf, "%d\n", VDD_FROM_REG(r));
	else
		return sprintf(buf, "%d\n", IN_FROM_REG(r));
}

static ssize_t get_in_max(struct gl520_data *data, char *buf, int n)
{
	u8 r = data->in_max[n];

	if (n == 0)
		return sprintf(buf, "%d\n", VDD_FROM_REG(r));
	else
		return sprintf(buf, "%d\n", IN_FROM_REG(r));
}

static ssize_t set_in_min(struct i2c_client *client, struct gl520_data *data, const char *buf, size_t count, int n, int reg)
{
	long v = simple_strtol(buf, NULL, 10);
	u8 r;

	mutex_lock(&data->update_lock);

	if (n == 0)
		r = VDD_TO_REG(v);
	else
		r = IN_TO_REG(v);

	data->in_min[n] = r;

	if (n < 4)
		gl520_write_value(client, reg, (gl520_read_value(client, reg) & ~0xff) | r);
	else
		gl520_write_value(client, reg, r);

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_in_max(struct i2c_client *client, struct gl520_data *data, const char *buf, size_t count, int n, int reg)
{
	long v = simple_strtol(buf, NULL, 10);
	u8 r;

	if (n == 0)
		r = VDD_TO_REG(v);
	else
		r = IN_TO_REG(v);

	mutex_lock(&data->update_lock);

	data->in_max[n] = r;

	if (n < 4)
		gl520_write_value(client, reg, (gl520_read_value(client, reg) & ~0xff00) | (r << 8));
	else
		gl520_write_value(client, reg, r);

	mutex_unlock(&data->update_lock);
	return count;
}

#define DIV_FROM_REG(val) (1 << (val))
#define FAN_FROM_REG(val,div) ((val)==0 ? 0 : (480000/((val) << (div))))
#define FAN_TO_REG(val,div) ((val)<=0?0:SENSORS_LIMIT((480000 + ((val) << ((div)-1))) / ((val) << (div)), 1, 255));

static ssize_t get_fan_input(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_input[n - 1], data->fan_div[n - 1]));
}

static ssize_t get_fan_min(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[n - 1], data->fan_div[n - 1]));
}

static ssize_t get_fan_div(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[n - 1]));
}

static ssize_t get_fan_off(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", data->fan_off);
}

static ssize_t set_fan_min(struct i2c_client *client, struct gl520_data *data, const char *buf, size_t count, int n, int reg)
{
	unsigned long v = simple_strtoul(buf, NULL, 10);
	u8 r;

	mutex_lock(&data->update_lock);
	r = FAN_TO_REG(v, data->fan_div[n - 1]);
	data->fan_min[n - 1] = r;

	if (n == 1)
		gl520_write_value(client, reg, (gl520_read_value(client, reg) & ~0xff00) | (r << 8));
	else
		gl520_write_value(client, reg, (gl520_read_value(client, reg) & ~0xff) | r);

	data->beep_mask = gl520_read_value(client, GL520_REG_BEEP_MASK);
	if (data->fan_min[n - 1] == 0)
		data->alarm_mask &= (n == 1) ? ~0x20 : ~0x40;
	else
		data->alarm_mask |= (n == 1) ? 0x20 : 0x40;
	data->beep_mask &= data->alarm_mask;
	gl520_write_value(client, GL520_REG_BEEP_MASK, data->beep_mask);

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_fan_div(struct i2c_client *client, struct gl520_data *data, const char *buf, size_t count, int n, int reg)
{
	unsigned long v = simple_strtoul(buf, NULL, 10);
	u8 r;

	switch (v) {
	case 1: r = 0; break;
	case 2: r = 1; break;
	case 4: r = 2; break;
	case 8: r = 3; break;
	default:
		dev_err(&client->dev, "fan_div value %ld not supported. Choose one of 1, 2, 4 or 8!\n", v);
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	data->fan_div[n - 1] = r;

	if (n == 1)
		gl520_write_value(client, reg, (gl520_read_value(client, reg) & ~0xc0) | (r << 6));
	else
		gl520_write_value(client, reg, (gl520_read_value(client, reg) & ~0x30) | (r << 4));

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_fan_off(struct i2c_client *client, struct gl520_data *data, const char *buf, size_t count, int n, int reg)
{
	u8 r = simple_strtoul(buf, NULL, 10)?1:0;

	mutex_lock(&data->update_lock);
	data->fan_off = r;
	gl520_write_value(client, reg, (gl520_read_value(client, reg) & ~0x0c) | (r << 2));
	mutex_unlock(&data->update_lock);
	return count;
}

#define TEMP_FROM_REG(val) (((val) - 130) * 1000)
#define TEMP_TO_REG(val) (SENSORS_LIMIT(((((val)<0?(val)-500:(val)+500) / 1000)+130),0,255))

static ssize_t get_temp_input(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_input[n - 1]));
}

static ssize_t get_temp_max(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[n - 1]));
}

static ssize_t get_temp_max_hyst(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max_hyst[n - 1]));
}

static ssize_t set_temp_max(struct i2c_client *client, struct gl520_data *data, const char *buf, size_t count, int n, int reg)
{
	long v = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_max[n - 1] = TEMP_TO_REG(v);
	gl520_write_value(client, reg, data->temp_max[n - 1]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_temp_max_hyst(struct i2c_client *client, struct gl520_data *data, const char *buf, size_t count, int n, int reg)
{
	long v = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_max_hyst[n - 1] = TEMP_TO_REG(v);
	gl520_write_value(client, reg, data->temp_max_hyst[n - 1]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t get_alarms(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", data->alarms);
}

static ssize_t get_beep_enable(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", data->beep_enable);
}

static ssize_t get_beep_mask(struct gl520_data *data, char *buf, int n)
{
	return sprintf(buf, "%d\n", data->beep_mask);
}

static ssize_t set_beep_enable(struct i2c_client *client, struct gl520_data *data, const char *buf, size_t count, int n, int reg)
{
	u8 r = simple_strtoul(buf, NULL, 10)?0:1;

	mutex_lock(&data->update_lock);
	data->beep_enable = !r;
	gl520_write_value(client, reg, (gl520_read_value(client, reg) & ~0x04) | (r << 2));
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_beep_mask(struct i2c_client *client, struct gl520_data *data, const char *buf, size_t count, int n, int reg)
{
	u8 r = simple_strtoul(buf, NULL, 10);
	
	mutex_lock(&data->update_lock);
	r &= data->alarm_mask;
	data->beep_mask = r;
	gl520_write_value(client, reg, r);
	mutex_unlock(&data->update_lock);
	return count;
}

static struct attribute *gl520_attributes[] = {
	&dev_attr_cpu0_vid.attr,

	&dev_attr_in0_input.attr,
	&dev_attr_in0_min.attr,
	&dev_attr_in0_max.attr,
	&dev_attr_in1_input.attr,
	&dev_attr_in1_min.attr,
	&dev_attr_in1_max.attr,
	&dev_attr_in2_input.attr,
	&dev_attr_in2_min.attr,
	&dev_attr_in2_max.attr,
	&dev_attr_in3_input.attr,
	&dev_attr_in3_min.attr,
	&dev_attr_in3_max.attr,

	&dev_attr_fan1_input.attr,
	&dev_attr_fan1_min.attr,
	&dev_attr_fan1_div.attr,
	&dev_attr_fan1_off.attr,
	&dev_attr_fan2_input.attr,
	&dev_attr_fan2_min.attr,
	&dev_attr_fan2_div.attr,

	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_max.attr,
	&dev_attr_temp1_max_hyst.attr,

	&dev_attr_alarms.attr,
	&dev_attr_beep_enable.attr,
	&dev_attr_beep_mask.attr,
	NULL
};

static const struct attribute_group gl520_group = {
	.attrs = gl520_attributes,
};

static struct attribute *gl520_attributes_opt[] = {
	&dev_attr_in4_input.attr,
	&dev_attr_in4_min.attr,
	&dev_attr_in4_max.attr,

	&dev_attr_temp2_input.attr,
	&dev_attr_temp2_max.attr,
	&dev_attr_temp2_max_hyst.attr,
	NULL
};

static const struct attribute_group gl520_group_opt = {
	.attrs = gl520_attributes_opt,
};


/*
 * Real code
 */

static int gl520_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, gl520_detect);
}

static int gl520_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct gl520_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		goto exit;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access gl520_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct gl520_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &gl520_driver;
	new_client->flags = 0;

	/* Determine the chip type. */
	if (kind < 0) {
		if ((gl520_read_value(new_client, GL520_REG_CHIP_ID) != 0x20) ||
		    ((gl520_read_value(new_client, GL520_REG_REVISION) & 0x7f) != 0x00) ||
		    ((gl520_read_value(new_client, GL520_REG_CONF) & 0x80) != 0x00)) {
			dev_dbg(&new_client->dev, "Unknown chip type, skipping\n");
			goto exit_free;
		}
	}

	/* Fill in the remaining client fields */
	strlcpy(new_client->name, "gl520sm", I2C_NAME_SIZE);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;

	/* Initialize the GL520SM chip */
	gl520_init_client(new_client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&new_client->dev.kobj, &gl520_group)))
		goto exit_detach;

	if (data->two_temps) {
		if ((err = device_create_file(&new_client->dev,
					      &dev_attr_temp2_input))
		 || (err = device_create_file(&new_client->dev,
					      &dev_attr_temp2_max))
		 || (err = device_create_file(&new_client->dev,
					      &dev_attr_temp2_max_hyst)))
			goto exit_remove_files;
	} else {
		if ((err = device_create_file(&new_client->dev,
					      &dev_attr_in4_input))
		 || (err = device_create_file(&new_client->dev,
					      &dev_attr_in4_min))
		 || (err = device_create_file(&new_client->dev,
					      &dev_attr_in4_max)))
			goto exit_remove_files;
	}


	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&new_client->dev.kobj, &gl520_group);
	sysfs_remove_group(&new_client->dev.kobj, &gl520_group_opt);
exit_detach:
	i2c_detach_client(new_client);
exit_free:
	kfree(data);
exit:
	return err;
}


/* Called when we have found a new GL520SM. */
static void gl520_init_client(struct i2c_client *client)
{
	struct gl520_data *data = i2c_get_clientdata(client);
	u8 oldconf, conf;

	conf = oldconf = gl520_read_value(client, GL520_REG_CONF);

	data->alarm_mask = 0xff;
	data->vrm = vid_which_vrm();

	if (extra_sensor_type == 1)
		conf &= ~0x10;
	else if (extra_sensor_type == 2)
		conf |= 0x10;
	data->two_temps = !(conf & 0x10);

	/* If IRQ# is disabled, we can safely force comparator mode */
	if (!(conf & 0x20))
		conf &= 0xf7;

	/* Enable monitoring if needed */
	conf |= 0x40;

	if (conf != oldconf)
		gl520_write_value(client, GL520_REG_CONF, conf);

	gl520_update_device(&(client->dev));

	if (data->fan_min[0] == 0)
		data->alarm_mask &= ~0x20;
	if (data->fan_min[1] == 0)
		data->alarm_mask &= ~0x40;

	data->beep_mask &= data->alarm_mask;
	gl520_write_value(client, GL520_REG_BEEP_MASK, data->beep_mask);
}

static int gl520_detach_client(struct i2c_client *client)
{
	struct gl520_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &gl520_group);
	sysfs_remove_group(&client->dev.kobj, &gl520_group_opt);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);
	return 0;
}


/* Registers 0x07 to 0x0c are word-sized, others are byte-sized 
   GL520 uses a high-byte first convention */
static int gl520_read_value(struct i2c_client *client, u8 reg)
{
	if ((reg >= 0x07) && (reg <= 0x0c))
		return swab16(i2c_smbus_read_word_data(client, reg));
	else
		return i2c_smbus_read_byte_data(client, reg);
}

static int gl520_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	if ((reg >= 0x07) && (reg <= 0x0c))
		return i2c_smbus_write_word_data(client, reg, swab16(value));
	else
		return i2c_smbus_write_byte_data(client, reg, value);
}


static struct gl520_data *gl520_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct gl520_data *data = i2c_get_clientdata(client);
	int val;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + 2 * HZ) || !data->valid) {

		dev_dbg(&client->dev, "Starting gl520sm update\n");

		data->alarms = gl520_read_value(client, GL520_REG_ALARMS);
		data->beep_mask = gl520_read_value(client, GL520_REG_BEEP_MASK);
		data->vid = gl520_read_value(client, GL520_REG_VID_INPUT) & 0x1f;

		val = gl520_read_value(client, GL520_REG_IN0_LIMIT);
		data->in_min[0] = val & 0xff;
		data->in_max[0] = (val >> 8) & 0xff;
		val = gl520_read_value(client, GL520_REG_IN1_LIMIT);
		data->in_min[1] = val & 0xff;
		data->in_max[1] = (val >> 8) & 0xff;
		val = gl520_read_value(client, GL520_REG_IN2_LIMIT);
		data->in_min[2] = val & 0xff;
		data->in_max[2] = (val >> 8) & 0xff;
		val = gl520_read_value(client, GL520_REG_IN3_LIMIT);
		data->in_min[3] = val & 0xff;
		data->in_max[3] = (val >> 8) & 0xff;

		val = gl520_read_value(client, GL520_REG_FAN_INPUT);
		data->fan_input[0] = (val >> 8) & 0xff;
		data->fan_input[1] = val & 0xff;

		val = gl520_read_value(client, GL520_REG_FAN_MIN);
		data->fan_min[0] = (val >> 8) & 0xff;
		data->fan_min[1] = val & 0xff;

		data->temp_input[0] = gl520_read_value(client, GL520_REG_TEMP1_INPUT);
		data->temp_max[0] = gl520_read_value(client, GL520_REG_TEMP1_MAX);
		data->temp_max_hyst[0] = gl520_read_value(client, GL520_REG_TEMP1_MAX_HYST);

		val = gl520_read_value(client, GL520_REG_FAN_DIV);
		data->fan_div[0] = (val >> 6) & 0x03;
		data->fan_div[1] = (val >> 4) & 0x03;
		data->fan_off = (val >> 2) & 0x01;

		data->alarms &= data->alarm_mask;

		val = gl520_read_value(client, GL520_REG_CONF);
		data->beep_enable = !((val >> 2) & 1);

		data->in_input[0] = gl520_read_value(client, GL520_REG_IN0_INPUT);
		data->in_input[1] = gl520_read_value(client, GL520_REG_IN1_INPUT);
		data->in_input[2] = gl520_read_value(client, GL520_REG_IN2_INPUT);
		data->in_input[3] = gl520_read_value(client, GL520_REG_IN3_INPUT);

		/* Temp1 and Vin4 are the same input */
		if (data->two_temps) {
			data->temp_input[1] = gl520_read_value(client, GL520_REG_TEMP2_INPUT);
			data->temp_max[1] = gl520_read_value(client, GL520_REG_TEMP2_MAX);
			data->temp_max_hyst[1] = gl520_read_value(client, GL520_REG_TEMP2_MAX_HYST);
		} else {
			data->in_input[4] = gl520_read_value(client, GL520_REG_IN4_INPUT);
			data->in_min[4] = gl520_read_value(client, GL520_REG_IN4_MIN);
			data->in_max[4] = gl520_read_value(client, GL520_REG_IN4_MAX);
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}


static int __init sensors_gl520sm_init(void)
{
	return i2c_add_driver(&gl520_driver);
}

static void __exit sensors_gl520sm_exit(void)
{
	i2c_del_driver(&gl520_driver);
}


MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>, "
	"Kyösti Mälkki <kmalkki@cc.hut.fi>, "
	"Maarten Deprez <maartendeprez@users.sourceforge.net>");
MODULE_DESCRIPTION("GL520SM driver");
MODULE_LICENSE("GPL");

module_init(sensors_gl520sm_init);
module_exit(sensors_gl520sm_exit);
