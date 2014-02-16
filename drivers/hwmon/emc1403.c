/*
 * emc1403.c - SMSC Thermal Driver
 *
 * Copyright (C) 2008 Intel Corp
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * TODO
 *	-	cache alarm and critical limit registers
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>

#define THERMAL_PID_REG		0xfd
#define THERMAL_SMSC_ID_REG	0xfe
#define THERMAL_REVISION_REG	0xff

struct thermal_data {
	struct i2c_client *client;
	const struct attribute_group *groups[3];
	struct mutex mutex;
	/*
	 * Cache the hyst value so we don't keep re-reading it. In theory
	 * we could cache it forever as nobody else should be writing it.
	 */
	u8 cached_hyst;
	unsigned long hyst_valid;
};

static ssize_t show_temp(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);
	struct thermal_data *data = dev_get_drvdata(dev);
	int retval;

	retval = i2c_smbus_read_byte_data(data->client, sda->index);
	if (retval < 0)
		return retval;
	return sprintf(buf, "%d000\n", retval);
}

static ssize_t show_bit(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sda = to_sensor_dev_attr_2(attr);
	struct thermal_data *data = dev_get_drvdata(dev);
	int retval;

	retval = i2c_smbus_read_byte_data(data->client, sda->nr);
	if (retval < 0)
		return retval;
	return sprintf(buf, "%d\n", !!(retval & sda->index));
}

static ssize_t store_temp(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);
	struct thermal_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int retval;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;
	retval = i2c_smbus_write_byte_data(data->client, sda->index,
					DIV_ROUND_CLOSEST(val, 1000));
	if (retval < 0)
		return retval;
	return count;
}

static ssize_t store_bit(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sda = to_sensor_dev_attr_2(attr);
	struct thermal_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int retval;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->mutex);
	retval = i2c_smbus_read_byte_data(client, sda->nr);
	if (retval < 0)
		goto fail;

	retval &= ~sda->index;
	if (val)
		retval |= sda->index;

	retval = i2c_smbus_write_byte_data(client, sda->index, retval);
	if (retval == 0)
		retval = count;
fail:
	mutex_unlock(&data->mutex);
	return retval;
}

static ssize_t show_hyst(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);
	struct thermal_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int retval;
	int hyst;

	retval = i2c_smbus_read_byte_data(client, sda->index);
	if (retval < 0)
		return retval;

	if (time_after(jiffies, data->hyst_valid)) {
		hyst = i2c_smbus_read_byte_data(client, 0x21);
		if (hyst < 0)
			return retval;
		data->cached_hyst = hyst;
		data->hyst_valid = jiffies + HZ;
	}
	return sprintf(buf, "%d000\n", retval - data->cached_hyst);
}

static ssize_t store_hyst(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);
	struct thermal_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int retval;
	int hyst;
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->mutex);
	retval = i2c_smbus_read_byte_data(client, sda->index);
	if (retval < 0)
		goto fail;

	hyst = val - retval * 1000;
	hyst = DIV_ROUND_CLOSEST(hyst, 1000);
	if (hyst < 0 || hyst > 255) {
		retval = -ERANGE;
		goto fail;
	}

	retval = i2c_smbus_write_byte_data(client, 0x21, hyst);
	if (retval == 0) {
		retval = count;
		data->cached_hyst = hyst;
		data->hyst_valid = jiffies + HZ;
	}
fail:
	mutex_unlock(&data->mutex);
	return retval;
}

/*
 *	Sensors. We pass the actual i2c register to the methods.
 */

static SENSOR_DEVICE_ATTR(temp1_min, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x06);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x05);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x20);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0x00);
static SENSOR_DEVICE_ATTR_2(temp1_min_alarm, S_IRUGO,
	show_bit, NULL, 0x36, 0x01);
static SENSOR_DEVICE_ATTR_2(temp1_max_alarm, S_IRUGO,
	show_bit, NULL, 0x35, 0x01);
static SENSOR_DEVICE_ATTR_2(temp1_crit_alarm, S_IRUGO,
	show_bit, NULL, 0x37, 0x01);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO | S_IWUSR,
	show_hyst, store_hyst, 0x20);

static SENSOR_DEVICE_ATTR(temp2_min, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x08);
static SENSOR_DEVICE_ATTR(temp2_max, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x07);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x19);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 0x01);
static SENSOR_DEVICE_ATTR_2(temp2_min_alarm, S_IRUGO,
	show_bit, NULL, 0x36, 0x02);
static SENSOR_DEVICE_ATTR_2(temp2_max_alarm, S_IRUGO,
	show_bit, NULL, 0x35, 0x02);
static SENSOR_DEVICE_ATTR_2(temp2_crit_alarm, S_IRUGO,
	show_bit, NULL, 0x37, 0x02);
static SENSOR_DEVICE_ATTR(temp2_crit_hyst, S_IRUGO | S_IWUSR,
	show_hyst, store_hyst, 0x19);

static SENSOR_DEVICE_ATTR(temp3_min, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x16);
static SENSOR_DEVICE_ATTR(temp3_max, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x15);
static SENSOR_DEVICE_ATTR(temp3_crit, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x1A);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 0x23);
static SENSOR_DEVICE_ATTR_2(temp3_min_alarm, S_IRUGO,
	show_bit, NULL, 0x36, 0x04);
static SENSOR_DEVICE_ATTR_2(temp3_max_alarm, S_IRUGO,
	show_bit, NULL, 0x35, 0x04);
static SENSOR_DEVICE_ATTR_2(temp3_crit_alarm, S_IRUGO,
	show_bit, NULL, 0x37, 0x04);
static SENSOR_DEVICE_ATTR(temp3_crit_hyst, S_IRUGO | S_IWUSR,
	show_hyst, store_hyst, 0x1A);

static SENSOR_DEVICE_ATTR(temp4_min, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x2D);
static SENSOR_DEVICE_ATTR(temp4_max, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x2C);
static SENSOR_DEVICE_ATTR(temp4_crit, S_IRUGO | S_IWUSR,
	show_temp, store_temp, 0x30);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_temp, NULL, 0x2A);
static SENSOR_DEVICE_ATTR_2(temp4_min_alarm, S_IRUGO,
	show_bit, NULL, 0x36, 0x08);
static SENSOR_DEVICE_ATTR_2(temp4_max_alarm, S_IRUGO,
	show_bit, NULL, 0x35, 0x08);
static SENSOR_DEVICE_ATTR_2(temp4_crit_alarm, S_IRUGO,
	show_bit, NULL, 0x37, 0x08);
static SENSOR_DEVICE_ATTR(temp4_crit_hyst, S_IRUGO | S_IWUSR,
	show_hyst, store_hyst, 0x30);

static SENSOR_DEVICE_ATTR_2(power_state, S_IRUGO | S_IWUSR,
	show_bit, store_bit, 0x03, 0x40);

static struct attribute *emc1403_attrs[] = {
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_power_state.dev_attr.attr,
	NULL
};

static const struct attribute_group emc1403_group = {
	.attrs = emc1403_attrs,
};

static struct attribute *emc1404_attrs[] = {
	&sensor_dev_attr_temp4_min.dev_attr.attr,
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp4_crit.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp4_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_crit_hyst.dev_attr.attr,
	NULL
};

static const struct attribute_group emc1404_group = {
	.attrs = emc1404_attrs,
};

static int emc1403_detect(struct i2c_client *client,
			struct i2c_board_info *info)
{
	int id;
	/* Check if thermal chip is SMSC and EMC1403 or EMC1423 */

	id = i2c_smbus_read_byte_data(client, THERMAL_SMSC_ID_REG);
	if (id != 0x5d)
		return -ENODEV;

	id = i2c_smbus_read_byte_data(client, THERMAL_PID_REG);
	switch (id) {
	case 0x21:
		strlcpy(info->type, "emc1403", I2C_NAME_SIZE);
		break;
	case 0x23:
		strlcpy(info->type, "emc1423", I2C_NAME_SIZE);
		break;
	case 0x25:
		strlcpy(info->type, "emc1404", I2C_NAME_SIZE);
		break;
	case 0x27:
		strlcpy(info->type, "emc1424", I2C_NAME_SIZE);
		break;
	default:
		return -ENODEV;
	}

	id = i2c_smbus_read_byte_data(client, THERMAL_REVISION_REG);
	if (id != 0x01)
		return -ENODEV;

	return 0;
}

static int emc1403_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct thermal_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(&client->dev, sizeof(struct thermal_data),
			    GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->mutex);
	data->hyst_valid = jiffies - 1;		/* Expired */

	data->groups[0] = &emc1403_group;
	if (id->driver_data)
		data->groups[1] = &emc1404_group;

	hwmon_dev = hwmon_device_register_with_groups(&client->dev,
						      client->name, data,
						      data->groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(&client->dev, "%s Thermal chip found\n", id->name);
	return 0;
}

static const unsigned short emc1403_address_list[] = {
	0x18, 0x29, 0x4c, 0x4d, I2C_CLIENT_END
};

static const struct i2c_device_id emc1403_idtable[] = {
	{ "emc1403", 0 },
	{ "emc1404", 1 },
	{ "emc1423", 0 },
	{ "emc1424", 1 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, emc1403_idtable);

static struct i2c_driver sensor_emc1403 = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "emc1403",
	},
	.detect = emc1403_detect,
	.probe = emc1403_probe,
	.id_table = emc1403_idtable,
	.address_list = emc1403_address_list,
};

module_i2c_driver(sensor_emc1403);

MODULE_AUTHOR("Kalhan Trisal <kalhan.trisal@intel.com");
MODULE_DESCRIPTION("emc1403 Thermal Driver");
MODULE_LICENSE("GPL v2");
