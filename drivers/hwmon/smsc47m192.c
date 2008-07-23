/*
    smsc47m192.c - Support for hardware monitoring block of
                   SMSC LPC47M192 and compatible Super I/O chips

    Copyright (C) 2006  Hartmut Rick <linux@rick.claranet.de>

    Derived from lm78.c and other chip drivers.

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
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(smsc47m192);

/* SMSC47M192 registers */
#define SMSC47M192_REG_IN(nr)		((nr)<6 ? (0x20 + (nr)) : \
					(0x50 + (nr) - 6))
#define SMSC47M192_REG_IN_MAX(nr)	((nr)<6 ? (0x2b + (nr) * 2) : \
					(0x54 + (((nr) - 6) * 2)))
#define SMSC47M192_REG_IN_MIN(nr)	((nr)<6 ? (0x2c + (nr) * 2) : \
					(0x55 + (((nr) - 6) * 2)))
static u8 SMSC47M192_REG_TEMP[3] =	{ 0x27, 0x26, 0x52 };
static u8 SMSC47M192_REG_TEMP_MAX[3] =	{ 0x39, 0x37, 0x58 };
static u8 SMSC47M192_REG_TEMP_MIN[3] =	{ 0x3A, 0x38, 0x59 };
#define SMSC47M192_REG_TEMP_OFFSET(nr)	((nr)==2 ? 0x1e : 0x1f)
#define SMSC47M192_REG_ALARM1		0x41
#define SMSC47M192_REG_ALARM2		0x42
#define SMSC47M192_REG_VID		0x47
#define SMSC47M192_REG_VID4		0x49
#define SMSC47M192_REG_CONFIG		0x40
#define SMSC47M192_REG_SFR		0x4f
#define SMSC47M192_REG_COMPANY_ID	0x3e
#define SMSC47M192_REG_VERSION		0x3f

/* generalised scaling with integer rounding */
static inline int SCALE(long val, int mul, int div)
{
	if (val < 0)
		return (val * mul - div / 2) / div;
	else
		return (val * mul + div / 2) / div;
}

/* Conversions */

/* smsc47m192 internally scales voltage measurements */
static const u16 nom_mv[] = { 2500, 2250, 3300, 5000, 12000, 3300, 1500, 1800 };

static inline unsigned int IN_FROM_REG(u8 reg, int n)
{
	return SCALE(reg, nom_mv[n], 192);
}

static inline u8 IN_TO_REG(unsigned long val, int n)
{
	return SENSORS_LIMIT(SCALE(val, 192, nom_mv[n]), 0, 255);
}

/* TEMP: 0.001 degC units (-128C to +127C)
   REG: 1C/bit, two's complement */
static inline s8 TEMP_TO_REG(int val)
{
	return SENSORS_LIMIT(SCALE(val, 1, 1000), -128000, 127000);
}

static inline int TEMP_FROM_REG(s8 val)
{
	return val * 1000;
}

struct smsc47m192_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[8];		/* Register value */
	u8 in_max[8];		/* Register value */
	u8 in_min[8];		/* Register value */
	s8 temp[3];		/* Register value */
	s8 temp_max[3];		/* Register value */
	s8 temp_min[3];		/* Register value */
	s8 temp_offset[3];	/* Register value */
	u16 alarms;		/* Register encoding, combined */
	u8 vid;			/* Register encoding, combined */
	u8 vrm;
};

static int smsc47m192_probe(struct i2c_client *client,
			    const struct i2c_device_id *id);
static int smsc47m192_detect(struct i2c_client *client, int kind,
			     struct i2c_board_info *info);
static int smsc47m192_remove(struct i2c_client *client);
static struct smsc47m192_data *smsc47m192_update_device(struct device *dev);

static const struct i2c_device_id smsc47m192_id[] = {
	{ "smsc47m192", smsc47m192 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, smsc47m192_id);

static struct i2c_driver smsc47m192_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "smsc47m192",
	},
	.probe		= smsc47m192_probe,
	.remove		= smsc47m192_remove,
	.id_table	= smsc47m192_id,
	.detect		= smsc47m192_detect,
	.address_data	= &addr_data,
};

/* Voltages */
static ssize_t show_in(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in[nr], nr));
}

static ssize_t show_in_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_min[nr], nr));
}

static ssize_t show_in_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_max[nr], nr));
}

static ssize_t set_in_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m192_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->in_min[nr] = IN_TO_REG(val, nr);
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_IN_MIN(nr),
							data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_in_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m192_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->in_max[nr] = IN_TO_REG(val, nr);
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_IN_MAX(nr),
							data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define show_in_offset(offset)					\
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO,		\
		show_in, NULL, offset);				\
static SENSOR_DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR,	\
		show_in_min, set_in_min, offset);		\
static SENSOR_DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR,	\
		show_in_max, set_in_max, offset);

show_in_offset(0)
show_in_offset(1)
show_in_offset(2)
show_in_offset(3)
show_in_offset(4)
show_in_offset(5)
show_in_offset(6)
show_in_offset(7)

/* Temperatures */
static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr]));
}

static ssize_t show_temp_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_min[nr]));
}

static ssize_t show_temp_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[nr]));
}

static ssize_t set_temp_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m192_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_min[nr] = TEMP_TO_REG(val);
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_TEMP_MIN[nr],
						data->temp_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_temp_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m192_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_max[nr] = TEMP_TO_REG(val);
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_TEMP_MAX[nr],
						data->temp_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_temp_offset(struct device *dev, struct device_attribute
		*attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_offset[nr]));
}

static ssize_t set_temp_offset(struct device *dev, struct device_attribute
		*attr, const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m192_data *data = i2c_get_clientdata(client);
	u8 sfr = i2c_smbus_read_byte_data(client, SMSC47M192_REG_SFR);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_offset[nr] = TEMP_TO_REG(val);
	if (nr>1)
		i2c_smbus_write_byte_data(client,
			SMSC47M192_REG_TEMP_OFFSET(nr), data->temp_offset[nr]);
	else if (data->temp_offset[nr] != 0) {
		/* offset[0] and offset[1] share the same register,
			SFR bit 4 activates offset[0] */
		i2c_smbus_write_byte_data(client, SMSC47M192_REG_SFR,
					(sfr & 0xef) | (nr==0 ? 0x10 : 0));
		data->temp_offset[1-nr] = 0;
		i2c_smbus_write_byte_data(client,
			SMSC47M192_REG_TEMP_OFFSET(nr), data->temp_offset[nr]);
	} else if ((sfr & 0x10) == (nr==0 ? 0x10 : 0))
		i2c_smbus_write_byte_data(client,
					SMSC47M192_REG_TEMP_OFFSET(nr), 0);
	mutex_unlock(&data->update_lock);
	return count;
}

#define show_temp_index(index)						\
static SENSOR_DEVICE_ATTR(temp##index##_input, S_IRUGO,			\
		show_temp, NULL, index-1);				\
static SENSOR_DEVICE_ATTR(temp##index##_min, S_IRUGO | S_IWUSR,		\
		show_temp_min, set_temp_min, index-1);			\
static SENSOR_DEVICE_ATTR(temp##index##_max, S_IRUGO | S_IWUSR,		\
		show_temp_max, set_temp_max, index-1);			\
static SENSOR_DEVICE_ATTR(temp##index##_offset, S_IRUGO | S_IWUSR,	\
		show_temp_offset, set_temp_offset, index-1);

show_temp_index(1)
show_temp_index(2)
show_temp_index(3)

/* VID */
static ssize_t show_vid(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

static ssize_t show_vrm(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct smsc47m192_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->vrm);
}

static ssize_t set_vrm(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct smsc47m192_data *data = dev_get_drvdata(dev);
	data->vrm = simple_strtoul(buf, NULL, 10);
	return count;
}
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm, set_vrm);

/* Alarms */
static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms & nr) ? 1 : 0);
}

static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 0x0010);
static SENSOR_DEVICE_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL, 0x0020);
static SENSOR_DEVICE_ATTR(temp3_alarm, S_IRUGO, show_alarm, NULL, 0x0040);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL, 0x4000);
static SENSOR_DEVICE_ATTR(temp3_fault, S_IRUGO, show_alarm, NULL, 0x8000);
static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 0x0001);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 0x0002);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 0x0004);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 0x0008);
static SENSOR_DEVICE_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 0x0100);
static SENSOR_DEVICE_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 0x0200);
static SENSOR_DEVICE_ATTR(in6_alarm, S_IRUGO, show_alarm, NULL, 0x0400);
static SENSOR_DEVICE_ATTR(in7_alarm, S_IRUGO, show_alarm, NULL, 0x0800);

static struct attribute *smsc47m192_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in7_alarm.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_offset.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_offset.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_offset.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,

	&dev_attr_cpu0_vid.attr,
	&dev_attr_vrm.attr,
	NULL
};

static const struct attribute_group smsc47m192_group = {
	.attrs = smsc47m192_attributes,
};

static struct attribute *smsc47m192_attributes_in4[] = {
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group smsc47m192_group_in4 = {
	.attrs = smsc47m192_attributes_in4,
};

static void smsc47m192_init_client(struct i2c_client *client)
{
	int i;
	u8 config = i2c_smbus_read_byte_data(client, SMSC47M192_REG_CONFIG);
	u8 sfr = i2c_smbus_read_byte_data(client, SMSC47M192_REG_SFR);

	/* select cycle mode (pause 1 sec between updates) */
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_SFR,
						(sfr & 0xfd) | 0x02);
	if (!(config & 0x01)) {
		/* initialize alarm limits */
		for (i=0; i<8; i++) {
			i2c_smbus_write_byte_data(client,
				SMSC47M192_REG_IN_MIN(i), 0);
			i2c_smbus_write_byte_data(client,
				SMSC47M192_REG_IN_MAX(i), 0xff);
		}
		for (i=0; i<3; i++) {
			i2c_smbus_write_byte_data(client,
				SMSC47M192_REG_TEMP_MIN[i], 0x80);
			i2c_smbus_write_byte_data(client,
				SMSC47M192_REG_TEMP_MAX[i], 0x7f);
		}

		/* start monitoring */
		i2c_smbus_write_byte_data(client, SMSC47M192_REG_CONFIG,
						(config & 0xf7) | 0x01);
	}
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int smsc47m192_detect(struct i2c_client *client, int kind,
			     struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int version;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Detection criteria from sensors_detect script */
	if (kind < 0) {
		if (i2c_smbus_read_byte_data(client,
				SMSC47M192_REG_COMPANY_ID) == 0x55
		 && ((version = i2c_smbus_read_byte_data(client,
				SMSC47M192_REG_VERSION)) & 0xf0) == 0x20
		 && (i2c_smbus_read_byte_data(client,
				SMSC47M192_REG_VID) & 0x70) == 0x00
		 && (i2c_smbus_read_byte_data(client,
				SMSC47M192_REG_VID4) & 0xfe) == 0x80) {
			dev_info(&adapter->dev,
				 "found SMSC47M192 or compatible, "
				 "version 2, stepping A%d\n", version & 0x0f);
		} else {
			dev_dbg(&adapter->dev,
				"SMSC47M192 detection failed at 0x%02x\n",
				client->addr);
			return -ENODEV;
		}
	}

	strlcpy(info->type, "smsc47m192", I2C_NAME_SIZE);

	return 0;
}

static int smsc47m192_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct smsc47m192_data *data;
	int config;
	int err;

	data = kzalloc(sizeof(struct smsc47m192_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	data->vrm = vid_which_vrm();
	mutex_init(&data->update_lock);

	/* Initialize the SMSC47M192 chip */
	smsc47m192_init_client(client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&client->dev.kobj, &smsc47m192_group)))
		goto exit_free;

	/* Pin 110 is either in4 (+12V) or VID4 */
	config = i2c_smbus_read_byte_data(client, SMSC47M192_REG_CONFIG);
	if (!(config & 0x20)) {
		if ((err = sysfs_create_group(&client->dev.kobj,
					      &smsc47m192_group_in4)))
			goto exit_remove_files;
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&client->dev.kobj, &smsc47m192_group);
	sysfs_remove_group(&client->dev.kobj, &smsc47m192_group_in4);
exit_free:
	kfree(data);
exit:
	return err;
}

static int smsc47m192_remove(struct i2c_client *client)
{
	struct smsc47m192_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &smsc47m192_group);
	sysfs_remove_group(&client->dev.kobj, &smsc47m192_group_in4);

	kfree(data);

	return 0;
}

static struct smsc47m192_data *smsc47m192_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m192_data *data = i2c_get_clientdata(client);
	int i, config;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	 || !data->valid) {
		u8 sfr = i2c_smbus_read_byte_data(client, SMSC47M192_REG_SFR);

		dev_dbg(&client->dev, "Starting smsc47m192 update\n");

		for (i = 0; i <= 7; i++) {
			data->in[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_IN(i));
			data->in_min[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_IN_MIN(i));
			data->in_max[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_IN_MAX(i));
		}
		for (i = 0; i < 3; i++) {
			data->temp[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_TEMP[i]);
			data->temp_max[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_TEMP_MAX[i]);
			data->temp_min[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_TEMP_MIN[i]);
		}
		for (i = 1; i < 3; i++)
			data->temp_offset[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_TEMP_OFFSET(i));
		/* first offset is temp_offset[0] if SFR bit 4 is set,
					temp_offset[1] otherwise */
		if (sfr & 0x10) {
			data->temp_offset[0] = data->temp_offset[1];
			data->temp_offset[1] = 0;
		} else
			data->temp_offset[0] = 0;

		data->vid = i2c_smbus_read_byte_data(client, SMSC47M192_REG_VID)
			    & 0x0f;
		config = i2c_smbus_read_byte_data(client,
						  SMSC47M192_REG_CONFIG);
		if (config & 0x20)
			data->vid |= (i2c_smbus_read_byte_data(client,
					SMSC47M192_REG_VID4) & 0x01) << 4;
		data->alarms = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_ALARM1) |
			       (i2c_smbus_read_byte_data(client,
		       				SMSC47M192_REG_ALARM2) << 8);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init smsc47m192_init(void)
{
	return i2c_add_driver(&smsc47m192_driver);
}

static void __exit smsc47m192_exit(void)
{
	i2c_del_driver(&smsc47m192_driver);
}

MODULE_AUTHOR("Hartmut Rick <linux@rick.claranet.de>");
MODULE_DESCRIPTION("SMSC47M192 driver");
MODULE_LICENSE("GPL");

module_init(smsc47m192_init);
module_exit(smsc47m192_exit);
