/*
 * adm1025.c
 *
 * Copyright (C) 2000       Chen-Yuan Wu <gwu@esoft.com>
 * Copyright (C) 2003-2009  Jean Delvare <jdelvare@suse.de>
 *
 * The ADM1025 is a sensor chip made by Analog Devices. It reports up to 6
 * voltages (including its own power source) and up to two temperatures
 * (its own plus up to one external one). Voltages are scaled internally
 * (which is not the common way) with ratios such that the nominal value
 * of each voltage correspond to a register value of 192 (which means a
 * resolution of about 0.5% of the nominal value). Temperature values are
 * reported with a 1 deg resolution and a 3 deg accuracy. Complete
 * datasheet can be obtained from Analog's website at:
 *   http://www.onsemi.com/PowerSolutions/product.do?id=ADM1025
 *
 * This driver also supports the ADM1025A, which differs from the ADM1025
 * only in that it has "open-drain VID inputs while the ADM1025 has
 * on-chip 100k pull-ups on the VID inputs". It doesn't make any
 * difference for us.
 *
 * This driver also supports the NE1619, a sensor chip made by Philips.
 * That chip is similar to the ADM1025A, with a few differences. The only
 * difference that matters to us is that the NE1619 has only two possible
 * addresses while the ADM1025A has a third one. Complete datasheet can be
 * obtained from Philips's website at:
 *   http://www.semiconductors.philips.com/pip/NE1619DS.html
 *
 * Since the ADM1025 was the first chipset supported by this driver, most
 * comments will refer to this chipset, but are actually general and
 * concern all supported chipsets, unless mentioned otherwise.
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>

/*
 * Addresses to scan
 * ADM1025 and ADM1025A have three possible addresses: 0x2c, 0x2d and 0x2e.
 * NE1619 has two possible addresses: 0x2c and 0x2d.
 */

static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

enum chips { adm1025, ne1619 };

/*
 * The ADM1025 registers
 */

#define ADM1025_REG_MAN_ID		0x3E
#define ADM1025_REG_CHIP_ID		0x3F
#define ADM1025_REG_CONFIG		0x40
#define ADM1025_REG_STATUS1		0x41
#define ADM1025_REG_STATUS2		0x42
#define ADM1025_REG_IN(nr)		(0x20 + (nr))
#define ADM1025_REG_IN_MAX(nr)		(0x2B + (nr) * 2)
#define ADM1025_REG_IN_MIN(nr)		(0x2C + (nr) * 2)
#define ADM1025_REG_TEMP(nr)		(0x26 + (nr))
#define ADM1025_REG_TEMP_HIGH(nr)	(0x37 + (nr) * 2)
#define ADM1025_REG_TEMP_LOW(nr)	(0x38 + (nr) * 2)
#define ADM1025_REG_VID			0x47
#define ADM1025_REG_VID4		0x49

/*
 * Conversions and various macros
 * The ADM1025 uses signed 8-bit values for temperatures.
 */

static const int in_scale[6] = { 2500, 2250, 3300, 5000, 12000, 3300 };

#define IN_FROM_REG(reg, scale)	(((reg) * (scale) + 96) / 192)
#define IN_TO_REG(val, scale)	((val) <= 0 ? 0 : \
				 (val) * 192 >= (scale) * 255 ? 255 : \
				 ((val) * 192 + (scale) / 2) / (scale))

#define TEMP_FROM_REG(reg)	((reg) * 1000)
#define TEMP_TO_REG(val)	((val) <= -127500 ? -128 : \
				 (val) >= 126500 ? 127 : \
				 (((val) < 0 ? (val) - 500 : \
				   (val) + 500) / 1000))

/*
 * Client data (each client gets its own)
 */

struct adm1025_data {
	struct i2c_client *client;
	const struct attribute_group *groups[3];
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	u8 in[6];		/* register value */
	u8 in_max[6];		/* register value */
	u8 in_min[6];		/* register value */
	s8 temp[2];		/* register value */
	s8 temp_min[2];		/* register value */
	s8 temp_max[2];		/* register value */
	u16 alarms;		/* register values, combined */
	u8 vid;			/* register values, combined */
	u8 vrm;
};

static struct adm1025_data *adm1025_update_device(struct device *dev)
{
	struct adm1025_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		int i;

		dev_dbg(&client->dev, "Updating data.\n");
		for (i = 0; i < 6; i++) {
			data->in[i] = i2c_smbus_read_byte_data(client,
				      ADM1025_REG_IN(i));
			data->in_min[i] = i2c_smbus_read_byte_data(client,
					  ADM1025_REG_IN_MIN(i));
			data->in_max[i] = i2c_smbus_read_byte_data(client,
					  ADM1025_REG_IN_MAX(i));
		}
		for (i = 0; i < 2; i++) {
			data->temp[i] = i2c_smbus_read_byte_data(client,
					ADM1025_REG_TEMP(i));
			data->temp_min[i] = i2c_smbus_read_byte_data(client,
					    ADM1025_REG_TEMP_LOW(i));
			data->temp_max[i] = i2c_smbus_read_byte_data(client,
					    ADM1025_REG_TEMP_HIGH(i));
		}
		data->alarms = i2c_smbus_read_byte_data(client,
			       ADM1025_REG_STATUS1)
			     | (i2c_smbus_read_byte_data(client,
				ADM1025_REG_STATUS2) << 8);
		data->vid = (i2c_smbus_read_byte_data(client,
			     ADM1025_REG_VID) & 0x0f)
			  | ((i2c_smbus_read_byte_data(client,
			      ADM1025_REG_VID4) & 0x01) << 4);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs stuff
 */

static ssize_t
show_in(struct device *dev, struct device_attribute *attr, char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in[index],
		       in_scale[index]));
}

static ssize_t
show_in_min(struct device *dev, struct device_attribute *attr, char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_min[index],
		       in_scale[index]));
}

static ssize_t
show_in_max(struct device *dev, struct device_attribute *attr, char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_max[index],
		       in_scale[index]));
}

static ssize_t
show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[index]));
}

static ssize_t
show_temp_min(struct device *dev, struct device_attribute *attr, char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_min[index]));
}

static ssize_t
show_temp_max(struct device *dev, struct device_attribute *attr, char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[index]));
}

static ssize_t set_in_min(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[index] = IN_TO_REG(val, in_scale[index]);
	i2c_smbus_write_byte_data(client, ADM1025_REG_IN_MIN(index),
				  data->in_min[index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_in_max(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[index] = IN_TO_REG(val, in_scale[index]);
	i2c_smbus_write_byte_data(client, ADM1025_REG_IN_MAX(index),
				  data->in_max[index]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define set_in(offset) \
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO, \
	show_in, NULL, offset); \
static SENSOR_DEVICE_ATTR(in##offset##_min, S_IWUSR | S_IRUGO, \
	show_in_min, set_in_min, offset); \
static SENSOR_DEVICE_ATTR(in##offset##_max, S_IWUSR | S_IRUGO, \
	show_in_max, set_in_max, offset)
set_in(0);
set_in(1);
set_in(2);
set_in(3);
set_in(4);
set_in(5);

static ssize_t set_temp_min(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_min[index] = TEMP_TO_REG(val);
	i2c_smbus_write_byte_data(client, ADM1025_REG_TEMP_LOW(index),
				  data->temp_min[index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_temp_max(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_max[index] = TEMP_TO_REG(val);
	i2c_smbus_write_byte_data(client, ADM1025_REG_TEMP_HIGH(index),
				  data->temp_max[index]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define set_temp(offset) \
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO, \
	show_temp, NULL, offset - 1); \
static SENSOR_DEVICE_ATTR(temp##offset##_min, S_IWUSR | S_IRUGO, \
	show_temp_min, set_temp_min, offset - 1); \
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IWUSR | S_IRUGO, \
	show_temp_max, set_temp_max, offset - 1)
set_temp(1);
set_temp(2);

static ssize_t
show_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static ssize_t
show_alarm(struct device *dev, struct device_attribute *attr, char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}
static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 9);
static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp1_fault, S_IRUGO, show_alarm, NULL, 14);

static ssize_t
show_vid(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%u\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

static ssize_t
show_vrm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adm1025_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", data->vrm);
}
static ssize_t set_vrm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct adm1025_data *data = dev_get_drvdata(dev);
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

/*
 * Real code
 */

static struct attribute *adm1025_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_fault.dev_attr.attr,
	&dev_attr_alarms.attr,
	&dev_attr_cpu0_vid.attr,
	&dev_attr_vrm.attr,
	NULL
};

static const struct attribute_group adm1025_group = {
	.attrs = adm1025_attributes,
};

static struct attribute *adm1025_attributes_in4[] = {
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group adm1025_group_in4 = {
	.attrs = adm1025_attributes_in4,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int adm1025_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	const char *name;
	u8 man_id, chip_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Check for unused bits */
	if ((i2c_smbus_read_byte_data(client, ADM1025_REG_CONFIG) & 0x80)
	 || (i2c_smbus_read_byte_data(client, ADM1025_REG_STATUS1) & 0xC0)
	 || (i2c_smbus_read_byte_data(client, ADM1025_REG_STATUS2) & 0xBC)) {
		dev_dbg(&adapter->dev, "ADM1025 detection failed at 0x%02x\n",
			client->addr);
		return -ENODEV;
	}

	/* Identification */
	chip_id = i2c_smbus_read_byte_data(client, ADM1025_REG_CHIP_ID);
	if ((chip_id & 0xF0) != 0x20)
		return -ENODEV;

	man_id = i2c_smbus_read_byte_data(client, ADM1025_REG_MAN_ID);
	if (man_id == 0x41)
		name = "adm1025";
	else if (man_id == 0xA1 && client->addr != 0x2E)
		name = "ne1619";
	else
		return -ENODEV;

	strlcpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static void adm1025_init_client(struct i2c_client *client)
{
	u8 reg;
	struct adm1025_data *data = i2c_get_clientdata(client);
	int i;

	data->vrm = vid_which_vrm();

	/*
	 * Set high limits
	 * Usually we avoid setting limits on driver init, but it happens
	 * that the ADM1025 comes with stupid default limits (all registers
	 * set to 0). In case the chip has not gone through any limit
	 * setting yet, we better set the high limits to the max so that
	 * no alarm triggers.
	 */
	for (i = 0; i < 6; i++) {
		reg = i2c_smbus_read_byte_data(client,
					       ADM1025_REG_IN_MAX(i));
		if (reg == 0)
			i2c_smbus_write_byte_data(client,
						  ADM1025_REG_IN_MAX(i),
						  0xFF);
	}
	for (i = 0; i < 2; i++) {
		reg = i2c_smbus_read_byte_data(client,
					       ADM1025_REG_TEMP_HIGH(i));
		if (reg == 0)
			i2c_smbus_write_byte_data(client,
						  ADM1025_REG_TEMP_HIGH(i),
						  0x7F);
	}

	/*
	 * Start the conversions
	 */
	reg = i2c_smbus_read_byte_data(client, ADM1025_REG_CONFIG);
	if (!(reg & 0x01))
		i2c_smbus_write_byte_data(client, ADM1025_REG_CONFIG,
					  (reg&0x7E)|0x01);
}

static int adm1025_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct adm1025_data *data;
	u8 config;

	data = devm_kzalloc(dev, sizeof(struct adm1025_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->update_lock);

	/* Initialize the ADM1025 chip */
	adm1025_init_client(client);

	/* sysfs hooks */
	data->groups[0] = &adm1025_group;
	/* Pin 11 is either in4 (+12V) or VID4 */
	config = i2c_smbus_read_byte_data(client, ADM1025_REG_CONFIG);
	if (!(config & 0x20))
		data->groups[1] = &adm1025_group_in4;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id adm1025_id[] = {
	{ "adm1025", adm1025 },
	{ "ne1619", ne1619 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm1025_id);

static struct i2c_driver adm1025_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adm1025",
	},
	.probe		= adm1025_probe,
	.id_table	= adm1025_id,
	.detect		= adm1025_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(adm1025_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("ADM1025 driver");
MODULE_LICENSE("GPL");
