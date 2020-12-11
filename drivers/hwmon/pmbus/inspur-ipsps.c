// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2019 Inspur Corp.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include <linux/hwmon-sysfs.h>

#include "pmbus.h"

#define IPSPS_REG_VENDOR_ID	0x99
#define IPSPS_REG_MODEL		0x9A
#define IPSPS_REG_FW_VERSION	0x9B
#define IPSPS_REG_PN		0x9C
#define IPSPS_REG_SN		0x9E
#define IPSPS_REG_HW_VERSION	0xB0
#define IPSPS_REG_MODE		0xFC

#define MODE_ACTIVE		0x55
#define MODE_STANDBY		0x0E
#define MODE_REDUNDANCY		0x00

#define MODE_ACTIVE_STRING		"active"
#define MODE_STANDBY_STRING		"standby"
#define MODE_REDUNDANCY_STRING		"redundancy"

enum ipsps_index {
	vendor,
	model,
	fw_version,
	part_number,
	serial_number,
	hw_version,
	mode,
	num_regs,
};

static const u8 ipsps_regs[num_regs] = {
	[vendor] = IPSPS_REG_VENDOR_ID,
	[model] = IPSPS_REG_MODEL,
	[fw_version] = IPSPS_REG_FW_VERSION,
	[part_number] = IPSPS_REG_PN,
	[serial_number] = IPSPS_REG_SN,
	[hw_version] = IPSPS_REG_HW_VERSION,
	[mode] = IPSPS_REG_MODE,
};

static ssize_t ipsps_string_show(struct device *dev,
				 struct device_attribute *devattr,
				 char *buf)
{
	u8 reg;
	int rc;
	char *p;
	char data[I2C_SMBUS_BLOCK_MAX + 1];
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	reg = ipsps_regs[attr->index];
	rc = i2c_smbus_read_block_data(client, reg, data);
	if (rc < 0)
		return rc;

	/* filled with printable characters, ending with # */
	p = memscan(data, '#', rc);
	*p = '\0';

	return snprintf(buf, PAGE_SIZE, "%s\n", data);
}

static ssize_t ipsps_fw_version_show(struct device *dev,
				     struct device_attribute *devattr,
				     char *buf)
{
	u8 reg;
	int rc;
	u8 data[I2C_SMBUS_BLOCK_MAX] = { 0 };
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	reg = ipsps_regs[attr->index];
	rc = i2c_smbus_read_block_data(client, reg, data);
	if (rc < 0)
		return rc;

	if (rc != 6)
		return -EPROTO;

	return snprintf(buf, PAGE_SIZE, "%u.%02u%u-%u.%02u\n",
			data[1], data[2]/* < 100 */, data[3]/*< 10*/,
			data[4], data[5]/* < 100 */);
}

static ssize_t ipsps_mode_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	u8 reg;
	int rc;
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	reg = ipsps_regs[attr->index];
	rc = i2c_smbus_read_byte_data(client, reg);
	if (rc < 0)
		return rc;

	switch (rc) {
	case MODE_ACTIVE:
		return snprintf(buf, PAGE_SIZE, "[%s] %s %s\n",
				MODE_ACTIVE_STRING,
				MODE_STANDBY_STRING, MODE_REDUNDANCY_STRING);
	case MODE_STANDBY:
		return snprintf(buf, PAGE_SIZE, "%s [%s] %s\n",
				MODE_ACTIVE_STRING,
				MODE_STANDBY_STRING, MODE_REDUNDANCY_STRING);
	case MODE_REDUNDANCY:
		return snprintf(buf, PAGE_SIZE, "%s %s [%s]\n",
				MODE_ACTIVE_STRING,
				MODE_STANDBY_STRING, MODE_REDUNDANCY_STRING);
	default:
		return snprintf(buf, PAGE_SIZE, "unspecified\n");
	}
}

static ssize_t ipsps_mode_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	u8 reg;
	int rc;
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	reg = ipsps_regs[attr->index];
	if (sysfs_streq(MODE_STANDBY_STRING, buf)) {
		rc = i2c_smbus_write_byte_data(client, reg,
					       MODE_STANDBY);
		if (rc < 0)
			return rc;
		return count;
	} else if (sysfs_streq(MODE_ACTIVE_STRING, buf)) {
		rc = i2c_smbus_write_byte_data(client, reg,
					       MODE_ACTIVE);
		if (rc < 0)
			return rc;
		return count;
	}

	return -EINVAL;
}

static SENSOR_DEVICE_ATTR_RO(vendor, ipsps_string, vendor);
static SENSOR_DEVICE_ATTR_RO(model, ipsps_string, model);
static SENSOR_DEVICE_ATTR_RO(part_number, ipsps_string, part_number);
static SENSOR_DEVICE_ATTR_RO(serial_number, ipsps_string, serial_number);
static SENSOR_DEVICE_ATTR_RO(hw_version, ipsps_string, hw_version);
static SENSOR_DEVICE_ATTR_RO(fw_version, ipsps_fw_version, fw_version);
static SENSOR_DEVICE_ATTR_RW(mode, ipsps_mode, mode);

static struct attribute *ipsps_attrs[] = {
	&sensor_dev_attr_vendor.dev_attr.attr,
	&sensor_dev_attr_model.dev_attr.attr,
	&sensor_dev_attr_part_number.dev_attr.attr,
	&sensor_dev_attr_serial_number.dev_attr.attr,
	&sensor_dev_attr_hw_version.dev_attr.attr,
	&sensor_dev_attr_fw_version.dev_attr.attr,
	&sensor_dev_attr_mode.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(ipsps);

static struct pmbus_driver_info ipsps_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_IIN | PMBUS_HAVE_POUT | PMBUS_HAVE_PIN |
		PMBUS_HAVE_FAN12 | PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 |
		PMBUS_HAVE_TEMP3 | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_INPUT |
		PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_STATUS_FAN12,
	.groups = ipsps_groups,
};

static struct pmbus_platform_data ipsps_pdata = {
	.flags = PMBUS_SKIP_STATUS_CHECK,
};

static int ipsps_probe(struct i2c_client *client)
{
	client->dev.platform_data = &ipsps_pdata;
	return pmbus_do_probe(client, &ipsps_info);
}

static const struct i2c_device_id ipsps_id[] = {
	{ "ipsps1", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ipsps_id);

#ifdef CONFIG_OF
static const struct of_device_id ipsps_of_match[] = {
	{ .compatible = "inspur,ipsps1" },
	{}
};
MODULE_DEVICE_TABLE(of, ipsps_of_match);
#endif

static struct i2c_driver ipsps_driver = {
	.driver = {
		.name = "inspur-ipsps",
		.of_match_table = of_match_ptr(ipsps_of_match),
	},
	.probe_new = ipsps_probe,
	.remove = pmbus_do_remove,
	.id_table = ipsps_id,
};

module_i2c_driver(ipsps_driver);

MODULE_AUTHOR("John Wang");
MODULE_DESCRIPTION("PMBus driver for Inspur Power System power supplies");
MODULE_LICENSE("GPL");
