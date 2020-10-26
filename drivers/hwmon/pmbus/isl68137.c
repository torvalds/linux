// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Renesas Digital Multiphase Voltage Regulators
 *
 * Copyright (c) 2017 Google Inc
 * Copyright (c) 2020 Renesas Electronics America
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
#define RAA_DMPVR2_READ_VMON	0xc8

enum chips {
	isl68137,
	isl68220,
	isl68221,
	isl68222,
	isl68223,
	isl68224,
	isl68225,
	isl68226,
	isl68227,
	isl68229,
	isl68233,
	isl68239,
	isl69222,
	isl69223,
	isl69224,
	isl69225,
	isl69227,
	isl69228,
	isl69234,
	isl69236,
	isl69239,
	isl69242,
	isl69243,
	isl69247,
	isl69248,
	isl69254,
	isl69255,
	isl69256,
	isl69259,
	isl69260,
	isl69268,
	isl69269,
	isl69298,
	raa228000,
	raa228004,
	raa228006,
	raa228228,
	raa229001,
	raa229004,
};

enum variants {
	raa_dmpvr1_2rail,
	raa_dmpvr2_1rail,
	raa_dmpvr2_2rail,
	raa_dmpvr2_2rail_nontc,
	raa_dmpvr2_3rail,
	raa_dmpvr2_hv,
};

static const struct i2c_device_id raa_dmpvr_id[];

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
		rc = pmbus_read_word_data(client, page, 0xff,
					  PMBUS_VOUT_COMMAND);
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

static const struct attribute_group *isl68137_attribute_groups[] = {
	&enable_group,
	NULL,
};

static int raa_dmpvr2_read_word_data(struct i2c_client *client, int page,
				     int phase, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VMON:
		ret = pmbus_read_word_data(client, page, phase,
					   RAA_DMPVR2_READ_VMON);
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static struct pmbus_driver_info raa_dmpvr_info = {
	.pages = 3,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 1,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 2,
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
	    | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT
		| PMBUS_HAVE_VMON,
	.func[1] = PMBUS_HAVE_IIN | PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT
	    | PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP3 | PMBUS_HAVE_STATUS_TEMP
	    | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_IOUT
	    | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT,
	.func[2] = PMBUS_HAVE_IIN | PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT
	    | PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP3 | PMBUS_HAVE_STATUS_TEMP
	    | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_IOUT
	    | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT,
};

static int isl68137_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	memcpy(info, &raa_dmpvr_info, sizeof(*info));

	switch (i2c_match_id(raa_dmpvr_id, client)->driver_data) {
	case raa_dmpvr1_2rail:
		info->pages = 2;
		info->R[PSC_VOLTAGE_IN] = 3;
		info->func[0] &= ~PMBUS_HAVE_VMON;
		info->func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		    | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
		    | PMBUS_HAVE_POUT;
		info->groups = isl68137_attribute_groups;
		break;
	case raa_dmpvr2_1rail:
		info->pages = 1;
		info->read_word_data = raa_dmpvr2_read_word_data;
		break;
	case raa_dmpvr2_2rail_nontc:
		info->func[0] &= ~PMBUS_HAVE_TEMP;
		info->func[1] &= ~PMBUS_HAVE_TEMP;
		fallthrough;
	case raa_dmpvr2_2rail:
		info->pages = 2;
		info->read_word_data = raa_dmpvr2_read_word_data;
		break;
	case raa_dmpvr2_3rail:
		info->read_word_data = raa_dmpvr2_read_word_data;
		break;
	case raa_dmpvr2_hv:
		info->pages = 1;
		info->R[PSC_VOLTAGE_IN] = 1;
		info->m[PSC_VOLTAGE_OUT] = 2;
		info->R[PSC_VOLTAGE_OUT] = 2;
		info->m[PSC_CURRENT_IN] = 2;
		info->m[PSC_POWER] = 2;
		info->R[PSC_POWER] = -1;
		info->read_word_data = raa_dmpvr2_read_word_data;
		break;
	default:
		return -ENODEV;
	}

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id raa_dmpvr_id[] = {
	{"isl68137", raa_dmpvr1_2rail},
	{"isl68220", raa_dmpvr2_2rail},
	{"isl68221", raa_dmpvr2_3rail},
	{"isl68222", raa_dmpvr2_2rail},
	{"isl68223", raa_dmpvr2_2rail},
	{"isl68224", raa_dmpvr2_3rail},
	{"isl68225", raa_dmpvr2_2rail},
	{"isl68226", raa_dmpvr2_3rail},
	{"isl68227", raa_dmpvr2_1rail},
	{"isl68229", raa_dmpvr2_3rail},
	{"isl68233", raa_dmpvr2_2rail},
	{"isl68239", raa_dmpvr2_3rail},

	{"isl69222", raa_dmpvr2_2rail},
	{"isl69223", raa_dmpvr2_3rail},
	{"isl69224", raa_dmpvr2_2rail},
	{"isl69225", raa_dmpvr2_2rail},
	{"isl69227", raa_dmpvr2_3rail},
	{"isl69228", raa_dmpvr2_3rail},
	{"isl69234", raa_dmpvr2_2rail},
	{"isl69236", raa_dmpvr2_2rail},
	{"isl69239", raa_dmpvr2_3rail},
	{"isl69242", raa_dmpvr2_2rail},
	{"isl69243", raa_dmpvr2_1rail},
	{"isl69247", raa_dmpvr2_2rail},
	{"isl69248", raa_dmpvr2_2rail},
	{"isl69254", raa_dmpvr2_2rail},
	{"isl69255", raa_dmpvr2_2rail},
	{"isl69256", raa_dmpvr2_2rail},
	{"isl69259", raa_dmpvr2_2rail},
	{"isl69260", raa_dmpvr2_2rail},
	{"isl69268", raa_dmpvr2_2rail},
	{"isl69269", raa_dmpvr2_3rail},
	{"isl69298", raa_dmpvr2_2rail},

	{"raa228000", raa_dmpvr2_hv},
	{"raa228004", raa_dmpvr2_hv},
	{"raa228006", raa_dmpvr2_hv},
	{"raa228228", raa_dmpvr2_2rail_nontc},
	{"raa229001", raa_dmpvr2_2rail},
	{"raa229004", raa_dmpvr2_2rail},
	{}
};

MODULE_DEVICE_TABLE(i2c, raa_dmpvr_id);

/* This is the driver that will be inserted */
static struct i2c_driver isl68137_driver = {
	.driver = {
		   .name = "isl68137",
		   },
	.probe_new = isl68137_probe,
	.id_table = raa_dmpvr_id,
};

module_i2c_driver(isl68137_driver);

MODULE_AUTHOR("Maxim Sloyko <maxims@google.com>");
MODULE_DESCRIPTION("PMBus driver for Renesas digital multiphase voltage regulators");
MODULE_LICENSE("GPL");
