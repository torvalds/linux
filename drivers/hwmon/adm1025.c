/*
 * adm1025.c
 *
 * Copyright (C) 2000       Chen-Yuan Wu <gwu@esoft.com>
 * Copyright (C) 2003-2004  Jean Delvare <khali@linux-fr.org>
 *
 * The ADM1025 is a sensor chip made by Analog Devices. It reports up to 6
 * voltages (including its own power source) and up to two temperatures
 * (its own plus up to one external one). Voltages are scaled internally
 * (which is not the common way) with ratios such that the nominal value
 * of each voltage correspond to a register value of 192 (which means a
 * resolution of about 0.5% of the nominal value). Temperature values are
 * reported with a 1 deg resolution and a 3 deg accuracy. Complete
 * datasheet can be obtained from Analog's website at:
 *   http://www.analog.com/Analog_Root/productPage/productHome/0,2121,ADM1025,00.html
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
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>

/*
 * Addresses to scan
 * ADM1025 and ADM1025A have three possible addresses: 0x2c, 0x2d and 0x2e.
 * NE1619 has two possible addresses: 0x2c and 0x2d.
 */

static unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

/*
 * Insmod parameters
 */

I2C_CLIENT_INSMOD_2(adm1025, ne1619);

/*
 * The ADM1025 registers
 */

#define ADM1025_REG_MAN_ID		0x3E
#define ADM1025_REG_CHIP_ID 		0x3F
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

static int in_scale[6] = { 2500, 2250, 3300, 5000, 12000, 3300 };

#define IN_FROM_REG(reg,scale)	(((reg) * (scale) + 96) / 192)
#define IN_TO_REG(val,scale)	((val) <= 0 ? 0 : \
				 (val) * 192 >= (scale) * 255 ? 255 : \
				 ((val) * 192 + (scale)/2) / (scale))

#define TEMP_FROM_REG(reg)	((reg) * 1000)
#define TEMP_TO_REG(val)	((val) <= -127500 ? -128 : \
				 (val) >= 126500 ? 127 : \
				 (((val) < 0 ? (val)-500 : (val)+500) / 1000))

/*
 * Functions declaration
 */

static int adm1025_attach_adapter(struct i2c_adapter *adapter);
static int adm1025_detect(struct i2c_adapter *adapter, int address, int kind);
static void adm1025_init_client(struct i2c_client *client);
static int adm1025_detach_client(struct i2c_client *client);
static struct adm1025_data *adm1025_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

static struct i2c_driver adm1025_driver = {
	.driver = {
		.name	= "adm1025",
	},
	.id		= I2C_DRIVERID_ADM1025,
	.attach_adapter	= adm1025_attach_adapter,
	.detach_client	= adm1025_detach_client,
};

/*
 * Client data (each client gets its own)
 */

struct adm1025_data {
	struct i2c_client client;
	struct device *hwmon_dev;
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

/*
 * Sysfs stuff
 */

#define show_in(offset) \
static ssize_t show_in##offset(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct adm1025_data *data = adm1025_update_device(dev); \
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in[offset], \
		       in_scale[offset])); \
} \
static ssize_t show_in##offset##_min(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct adm1025_data *data = adm1025_update_device(dev); \
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_min[offset], \
		       in_scale[offset])); \
} \
static ssize_t show_in##offset##_max(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct adm1025_data *data = adm1025_update_device(dev); \
	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_max[offset], \
		       in_scale[offset])); \
} \
static DEVICE_ATTR(in##offset##_input, S_IRUGO, show_in##offset, NULL);
show_in(0);
show_in(1);
show_in(2);
show_in(3);
show_in(4);
show_in(5);

#define show_temp(offset) \
static ssize_t show_temp##offset(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct adm1025_data *data = adm1025_update_device(dev); \
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[offset-1])); \
} \
static ssize_t show_temp##offset##_min(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct adm1025_data *data = adm1025_update_device(dev); \
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_min[offset-1])); \
} \
static ssize_t show_temp##offset##_max(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct adm1025_data *data = adm1025_update_device(dev); \
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[offset-1])); \
}\
static DEVICE_ATTR(temp##offset##_input, S_IRUGO, show_temp##offset, NULL);
show_temp(1);
show_temp(2);

#define set_in(offset) \
static ssize_t set_in##offset##_min(struct device *dev, struct device_attribute *attr, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct adm1025_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->in_min[offset] = IN_TO_REG(val, in_scale[offset]); \
	i2c_smbus_write_byte_data(client, ADM1025_REG_IN_MIN(offset), \
				  data->in_min[offset]); \
	mutex_unlock(&data->update_lock); \
	return count; \
} \
static ssize_t set_in##offset##_max(struct device *dev, struct device_attribute *attr, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct adm1025_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->in_max[offset] = IN_TO_REG(val, in_scale[offset]); \
	i2c_smbus_write_byte_data(client, ADM1025_REG_IN_MAX(offset), \
				  data->in_max[offset]); \
	mutex_unlock(&data->update_lock); \
	return count; \
} \
static DEVICE_ATTR(in##offset##_min, S_IWUSR | S_IRUGO, \
	show_in##offset##_min, set_in##offset##_min); \
static DEVICE_ATTR(in##offset##_max, S_IWUSR | S_IRUGO, \
	show_in##offset##_max, set_in##offset##_max);
set_in(0);
set_in(1);
set_in(2);
set_in(3);
set_in(4);
set_in(5);

#define set_temp(offset) \
static ssize_t set_temp##offset##_min(struct device *dev, struct device_attribute *attr, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct adm1025_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->temp_min[offset-1] = TEMP_TO_REG(val); \
	i2c_smbus_write_byte_data(client, ADM1025_REG_TEMP_LOW(offset-1), \
				  data->temp_min[offset-1]); \
	mutex_unlock(&data->update_lock); \
	return count; \
} \
static ssize_t set_temp##offset##_max(struct device *dev, struct device_attribute *attr, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct adm1025_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->temp_max[offset-1] = TEMP_TO_REG(val); \
	i2c_smbus_write_byte_data(client, ADM1025_REG_TEMP_HIGH(offset-1), \
				  data->temp_max[offset-1]); \
	mutex_unlock(&data->update_lock); \
	return count; \
} \
static DEVICE_ATTR(temp##offset##_min, S_IWUSR | S_IRUGO, \
	show_temp##offset##_min, set_temp##offset##_min); \
static DEVICE_ATTR(temp##offset##_max, S_IWUSR | S_IRUGO, \
	show_temp##offset##_max, set_temp##offset##_max);
set_temp(1);
set_temp(2);

static ssize_t show_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static ssize_t show_vid(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adm1025_data *data = adm1025_update_device(dev);
	return sprintf(buf, "%u\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

static ssize_t show_vrm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adm1025_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", data->vrm);
}
static ssize_t set_vrm(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1025_data *data = i2c_get_clientdata(client);
	data->vrm = simple_strtoul(buf, NULL, 10);
	return count;
}
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm, set_vrm);

/*
 * Real code
 */

static int adm1025_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, adm1025_detect);
}

static struct attribute *adm1025_attributes[] = {
	&dev_attr_in0_input.attr,
	&dev_attr_in1_input.attr,
	&dev_attr_in2_input.attr,
	&dev_attr_in3_input.attr,
	&dev_attr_in5_input.attr,
	&dev_attr_in0_min.attr,
	&dev_attr_in1_min.attr,
	&dev_attr_in2_min.attr,
	&dev_attr_in3_min.attr,
	&dev_attr_in5_min.attr,
	&dev_attr_in0_max.attr,
	&dev_attr_in1_max.attr,
	&dev_attr_in2_max.attr,
	&dev_attr_in3_max.attr,
	&dev_attr_in5_max.attr,
	&dev_attr_temp1_input.attr,
	&dev_attr_temp2_input.attr,
	&dev_attr_temp1_min.attr,
	&dev_attr_temp2_min.attr,
	&dev_attr_temp1_max.attr,
	&dev_attr_temp2_max.attr,
	&dev_attr_alarms.attr,
	&dev_attr_cpu0_vid.attr,
	&dev_attr_vrm.attr,
	NULL
};

static const struct attribute_group adm1025_group = {
	.attrs = adm1025_attributes,
};

static struct attribute *adm1025_attributes_opt[] = {
	&dev_attr_in4_input.attr,
	&dev_attr_in4_min.attr,
	&dev_attr_in4_max.attr,
	NULL
};

static const struct attribute_group adm1025_group_opt = {
	.attrs = adm1025_attributes_opt,
};

/*
 * The following function does more than just detection. If detection
 * succeeds, it also registers the new chip.
 */
static int adm1025_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct adm1025_data *data;
	int err = 0;
	const char *name = "";
	u8 config;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	if (!(data = kzalloc(sizeof(struct adm1025_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	/* The common I2C client data is placed right before the
	   ADM1025-specific data. */
	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &adm1025_driver;
	new_client->flags = 0;

	/*
	 * Now we do the remaining detection. A negative kind means that
	 * the driver was loaded with no force parameter (default), so we
	 * must both detect and identify the chip. A zero kind means that
	 * the driver was loaded with the force parameter, the detection
	 * step shall be skipped. A positive kind means that the driver
	 * was loaded with the force parameter and a given kind of chip is
	 * requested, so both the detection and the identification steps
	 * are skipped.
	 */
	config = i2c_smbus_read_byte_data(new_client, ADM1025_REG_CONFIG);
	if (kind < 0) { /* detection */
		if ((config & 0x80) != 0x00
		 || (i2c_smbus_read_byte_data(new_client,
		     ADM1025_REG_STATUS1) & 0xC0) != 0x00
		 || (i2c_smbus_read_byte_data(new_client,
		     ADM1025_REG_STATUS2) & 0xBC) != 0x00) {
			dev_dbg(&adapter->dev,
				"ADM1025 detection failed at 0x%02x.\n",
				address);
			goto exit_free;
		}
	}

	if (kind <= 0) { /* identification */
		u8 man_id, chip_id;

		man_id = i2c_smbus_read_byte_data(new_client,
			 ADM1025_REG_MAN_ID);
		chip_id = i2c_smbus_read_byte_data(new_client,
			  ADM1025_REG_CHIP_ID);
		
		if (man_id == 0x41) { /* Analog Devices */
			if ((chip_id & 0xF0) == 0x20) { /* ADM1025/ADM1025A */
				kind = adm1025;
			}
		} else
		if (man_id == 0xA1) { /* Philips */
			if (address != 0x2E
			 && (chip_id & 0xF0) == 0x20) { /* NE1619 */
				kind = ne1619;
			}
		}

		if (kind <= 0) { /* identification failed */
			dev_info(&adapter->dev,
			    "Unsupported chip (man_id=0x%02X, "
			    "chip_id=0x%02X).\n", man_id, chip_id);
			goto exit_free;
		}
	}

	if (kind == adm1025) {
		name = "adm1025";
	} else if (kind == ne1619) {
		name = "ne1619";
	}

	/* We can fill in the remaining client fields */
	strlcpy(new_client->name, name, I2C_NAME_SIZE);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;

	/* Initialize the ADM1025 chip */
	adm1025_init_client(new_client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&new_client->dev.kobj, &adm1025_group)))
		goto exit_detach;

	/* Pin 11 is either in4 (+12V) or VID4 */
	if (!(config & 0x20)) {
		if ((err = device_create_file(&new_client->dev,
					&dev_attr_in4_input))
		 || (err = device_create_file(&new_client->dev,
					&dev_attr_in4_min))
		 || (err = device_create_file(&new_client->dev,
					&dev_attr_in4_max)))
			goto exit_remove;
	}

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	sysfs_remove_group(&new_client->dev.kobj, &adm1025_group);
	sysfs_remove_group(&new_client->dev.kobj, &adm1025_group_opt);
exit_detach:
	i2c_detach_client(new_client);
exit_free:
	kfree(data);
exit:
	return err;
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
	for (i=0; i<6; i++) {
		reg = i2c_smbus_read_byte_data(client,
					       ADM1025_REG_IN_MAX(i));
		if (reg == 0)
			i2c_smbus_write_byte_data(client,
						  ADM1025_REG_IN_MAX(i),
						  0xFF);
	}
	for (i=0; i<2; i++) {
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

static int adm1025_detach_client(struct i2c_client *client)
{
	struct adm1025_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &adm1025_group);
	sysfs_remove_group(&client->dev.kobj, &adm1025_group_opt);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);
	return 0;
}

static struct adm1025_data *adm1025_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1025_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		int i;

		dev_dbg(&client->dev, "Updating data.\n");
		for (i=0; i<6; i++) {
			data->in[i] = i2c_smbus_read_byte_data(client,
				      ADM1025_REG_IN(i));
			data->in_min[i] = i2c_smbus_read_byte_data(client,
					  ADM1025_REG_IN_MIN(i));
			data->in_max[i] = i2c_smbus_read_byte_data(client,
					  ADM1025_REG_IN_MAX(i));
		}
		for (i=0; i<2; i++) {
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

static int __init sensors_adm1025_init(void)
{
	return i2c_add_driver(&adm1025_driver);
}

static void __exit sensors_adm1025_exit(void)
{
	i2c_del_driver(&adm1025_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("ADM1025 driver");
MODULE_LICENSE("GPL");

module_init(sensors_adm1025_init);
module_exit(sensors_adm1025_exit);
