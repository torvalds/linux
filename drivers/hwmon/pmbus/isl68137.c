// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Intersil ISL68137
 *
 * Copyright (c) 2017 Google Inc
 *
 */

#include <linux/err.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "pmbus.h"

#define ISL68137_VOUT_AVS	0x30

static ssize_t isl68137_avs_enable_show_page(struct i2c_client *client,
					     int page,
					     char *buf)
{
	int val = pmbus_read_byte_data(client, page, PMBUS_OPERATION);

	return sprintf(buf, "%d\n",
		       (val & ISL68137_VOUT_AVS) == ISL68137_VOUT_AVS ? 1 : 0);
}

static ssize_t isl68137_avs_enable_store_page(struct i2c_client *client,
					      int page,
					      const char *buf, size_t count)
{
	int rc, op_val;
	bool result;

	rc = kstrtobool(buf, &result);
	if (rc)
		return rc;

	op_val = result ? ISL68137_VOUT_AVS : 0;

	/*
	 * Writes to VOUT setpoint over AVSBus will persist after the VRM is
	 * switched to PMBus control. Switching back to AVSBus control
	 * restores this persisted setpoint rather than re-initializing to
	 * PMBus VOUT_COMMAND. Writing VOUT_COMMAND first over PMBus before
	 * enabling AVS control is the workaround.
	 */
	if (op_val == ISL68137_VOUT_AVS) {
		rc = pmbus_read_word_data(client, page, PMBUS_VOUT_COMMAND);
		if (rc < 0)
			return rc;

		rc = pmbus_write_word_data(client, page, PMBUS_VOUT_COMMAND,
					   rc);
		if (rc < 0)
			return rc;
	}

	rc = pmbus_update_byte_data(client, page, PMBUS_OPERATION,
				    ISL68137_VOUT_AVS, op_val);

	return (rc < 0) ? rc : count;
}

static ssize_t isl68137_avs_enable_show(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return isl68137_avs_enable_show_page(client, attr->index, buf);
}

static ssize_t isl68137_avs_enable_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return isl68137_avs_enable_store_page(client, attr->index, buf, count);
}

static SENSOR_DEVICE_ATTR_RW(avs0_enable, isl68137_avs_enable, 0);
static SENSOR_DEVICE_ATTR_RW(avs1_enable, isl68137_avs_enable, 1);

static struct attribute *enable_attrs[] = {
	&sensor_dev_attr_avs0_enable.dev_attr.attr,
	&sensor_dev_attr_avs1_enable.dev_attr.attr,
	NULL,
};

static const struct attribute_group enable_group = {
	.attrs = enable_attrs,
};

static const struct attribute_group *attribute_groups[] = {
	&enable_group,
	NULL,
};

static struct pmbus_driver_info isl68137_info = {
	.pages = 2,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 1,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 3,
	.m[PSC_VOLTAGE_OUT] = 1,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 3,
	.m[PSC_CURRENT_IN] = 1,
	.b[PSC_CURRENT_IN] = 0,
	.R[PSC_CURRENT_IN] = 2,
	.m[PSC_CURRENT_OUT] = 1,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = 1,
	.m[PSC_POWER] = 1,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = 0,
	.m[PSC_TEMPERATURE] = 1,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 0,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN | PMBUS_HAVE_PIN
	    | PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2
	    | PMBUS_HAVE_TEMP3 | PMBUS_HAVE_STATUS_TEMP
	    | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
	    | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT,
	.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
	    | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT,
	.groups = attribute_groups,
};

static int isl68137_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	return pmbus_do_probe(client, id, &isl68137_info);
}

static const struct i2c_device_id isl68137_id[] = {
	{"isl68137", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, isl68137_id);

/* This is the driver that will be inserted */
static struct i2c_driver isl68137_driver = {
	.driver = {
		   .name = "isl68137",
		   },
	.probe = isl68137_probe,
	.remove = pmbus_do_remove,
	.id_table = isl68137_id,
};

module_i2c_driver(isl68137_driver);

MODULE_AUTHOR("Maxim Sloyko <maxims@google.com>");
MODULE_DESCRIPTION("PMBus driver for Intersil ISL68137");
MODULE_LICENSE("GPL");
