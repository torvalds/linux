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
#include <linux/of.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include "pmbus.h"

#define ISL68137_VOUT_AVS	0x30
#define RAA_DMPVR2_READ_VMON	0xc8
#define MAX_CHANNELS            4

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
	raa228244,
	raa228246,
	raa229001,
	raa229004,
	raa229621,
};

enum variants {
	raa_dmpvr1_2rail,
	raa_dmpvr2_1rail,
	raa_dmpvr2_2rail,
	raa_dmpvr2_2rail_nontc,
	raa_dmpvr2_3rail,
	raa_dmpvr2_hv,
};

struct isl68137_channel {
	u32 vout_voltage_divider[2];
};

struct isl68137_data {
	struct pmbus_driver_info info;
	struct isl68137_channel channel[MAX_CHANNELS];
};

#define to_isl68137_data(x)	container_of(x, struct isl68137_data, info)

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
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct isl68137_data *data = to_isl68137_data(info);
	int ret;
	u64 temp;

	switch (reg) {
	case PMBUS_VIRT_READ_VMON:
		ret = pmbus_read_word_data(client, page, phase,
					   RAA_DMPVR2_READ_VMON);
		break;
	case PMBUS_READ_POUT:
	case PMBUS_READ_VOUT:
		/*
		 * In cases where a voltage divider is attached to the target
		 * rail between Vout and the Vsense pin, both Vout and Pout
		 * should be scaled by the voltage divider scaling factor.
		 * I.e. Vout = Vsense * Rtotal / Rout
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret > 0) {
			temp = DIV_U64_ROUND_CLOSEST((u64)ret *
				data->channel[page].vout_voltage_divider[1],
				data->channel[page].vout_voltage_divider[0]);
			ret = clamp_val(temp, 0, 0xffff);
		}
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int raa_dmpvr2_write_word_data(struct i2c_client *client, int page,
				      int reg, u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct isl68137_data *data = to_isl68137_data(info);
	int ret;
	u64 temp;

	switch (reg) {
	case PMBUS_VOUT_MAX:
	case PMBUS_VOUT_MARGIN_HIGH:
	case PMBUS_VOUT_MARGIN_LOW:
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
	case PMBUS_VOUT_COMMAND:
		/*
		 * In cases where a voltage divider is attached to the target
		 * rail between Vout and the Vsense pin, Vout related PMBus
		 * commands should be scaled based on the expected voltage
		 * at the Vsense pin.
		 * I.e. Vsense = Vout * Rout / Rtotal
		 */
		temp = DIV_U64_ROUND_CLOSEST((u64)word *
				data->channel[page].vout_voltage_divider[0],
				data->channel[page].vout_voltage_divider[1]);
		ret = clamp_val(temp, 0, 0xffff);
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

static int isl68137_probe_child_from_dt(struct device *dev,
					struct device_node *child,
					struct isl68137_data *data)
{
	u32 channel, rout, rtotal;
	int err;

	err = of_property_read_u32(child, "reg", &channel);
	if (err) {
		dev_err(dev, "missing reg property of %pOFn\n", child);
		return err;
	}
	if (channel >= data->info.pages) {
		dev_err(dev, "invalid reg %d of %pOFn\n", channel, child);
		return -EINVAL;
	}

	err = of_property_read_u32_array(child, "vout-voltage-divider",
					 data->channel[channel].vout_voltage_divider,
					 ARRAY_SIZE(data->channel[channel].vout_voltage_divider));
	if (err && err != -EINVAL) {
		dev_err(dev,
			"malformed vout-voltage-divider value for channel %d\n",
			channel);
		return err;
	}

	rout = data->channel[channel].vout_voltage_divider[0];
	rtotal = data->channel[channel].vout_voltage_divider[1];
	if (rout == 0) {
		dev_err(dev,
			"Voltage divider output resistance must be greater than 0\n");
		return -EINVAL;
	}
	if (rtotal < rout) {
		dev_err(dev,
			"Voltage divider total resistance is less than output resistance\n");
		return -EINVAL;
	}

	return 0;
}

static int isl68137_probe_from_dt(struct device *dev,
				  struct isl68137_data *data)
{
	const struct device_node *np = dev->of_node;
	int err;

	for_each_child_of_node_scoped(np, child) {
		if (strcmp(child->name, "channel"))
			continue;

		err = isl68137_probe_child_from_dt(dev, child, data);
		if (err)
			return err;
	}

	return 0;
}

static int isl68137_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pmbus_driver_info *info;
	struct isl68137_data *data;
	int i, err;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/*
	 * Initialize all voltage dividers to Rout=1 and Rtotal=1 to simplify
	 * logic in PMBus word read/write functions
	 */
	for (i = 0; i < MAX_CHANNELS; i++)
		memset(data->channel[i].vout_voltage_divider,
		       1,
		       sizeof(data->channel[i].vout_voltage_divider));

	memcpy(&data->info, &raa_dmpvr_info, sizeof(data->info));
	info = &data->info;

	switch (i2c_match_id(raa_dmpvr_id, client)->driver_data) {
	case raa_dmpvr1_2rail:
		info->pages = 2;
		info->R[PSC_VOLTAGE_IN] = 3;
		info->func[0] &= ~PMBUS_HAVE_VMON;
		info->func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		    | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
		    | PMBUS_HAVE_POUT;
		info->read_word_data = raa_dmpvr2_read_word_data;
		info->write_word_data = raa_dmpvr2_write_word_data;
		info->groups = isl68137_attribute_groups;
		break;
	case raa_dmpvr2_1rail:
		info->pages = 1;
		info->read_word_data = raa_dmpvr2_read_word_data;
		info->write_word_data = raa_dmpvr2_write_word_data;
		break;
	case raa_dmpvr2_2rail_nontc:
		info->func[0] &= ~PMBUS_HAVE_TEMP3;
		info->func[1] &= ~PMBUS_HAVE_TEMP3;
		fallthrough;
	case raa_dmpvr2_2rail:
		info->pages = 2;
		info->read_word_data = raa_dmpvr2_read_word_data;
		info->write_word_data = raa_dmpvr2_write_word_data;
		break;
	case raa_dmpvr2_3rail:
		info->read_word_data = raa_dmpvr2_read_word_data;
		info->write_word_data = raa_dmpvr2_write_word_data;
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
		info->write_word_data = raa_dmpvr2_write_word_data;
		break;
	default:
		return -ENODEV;
	}

	err = isl68137_probe_from_dt(dev, data);
	if (err)
		return err;

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
	{"raa228244", raa_dmpvr2_2rail_nontc},
	{"raa228246", raa_dmpvr2_2rail_nontc},
	{"raa229001", raa_dmpvr2_2rail},
	{"raa229004", raa_dmpvr2_2rail},
	{"raa229621", raa_dmpvr2_2rail},
	{}
};

MODULE_DEVICE_TABLE(i2c, raa_dmpvr_id);

static const struct of_device_id isl68137_of_match[] = {
	{ .compatible = "isil,isl68137", .data = (void *)raa_dmpvr1_2rail },
	{ .compatible = "renesas,isl68220", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl68221", .data = (void *)raa_dmpvr2_3rail },
	{ .compatible = "renesas,isl68222", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl68223", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl68224", .data = (void *)raa_dmpvr2_3rail },
	{ .compatible = "renesas,isl68225", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl68226", .data = (void *)raa_dmpvr2_3rail },
	{ .compatible = "renesas,isl68227", .data = (void *)raa_dmpvr2_1rail },
	{ .compatible = "renesas,isl68229", .data = (void *)raa_dmpvr2_3rail },
	{ .compatible = "renesas,isl68233", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl68239", .data = (void *)raa_dmpvr2_3rail },

	{ .compatible = "renesas,isl69222", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69223", .data = (void *)raa_dmpvr2_3rail },
	{ .compatible = "renesas,isl69224", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69225", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69227", .data = (void *)raa_dmpvr2_3rail },
	{ .compatible = "renesas,isl69228", .data = (void *)raa_dmpvr2_3rail },
	{ .compatible = "renesas,isl69234", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69236", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69239", .data = (void *)raa_dmpvr2_3rail },
	{ .compatible = "renesas,isl69242", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69243", .data = (void *)raa_dmpvr2_1rail },
	{ .compatible = "renesas,isl69247", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69248", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69254", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69255", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69256", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69259", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "isil,isl69260", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,isl69268", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "isil,isl69269", .data = (void *)raa_dmpvr2_3rail },
	{ .compatible = "renesas,isl69298", .data = (void *)raa_dmpvr2_2rail },

	{ .compatible = "renesas,raa228000", .data = (void *)raa_dmpvr2_hv },
	{ .compatible = "renesas,raa228004", .data = (void *)raa_dmpvr2_hv },
	{ .compatible = "renesas,raa228006", .data = (void *)raa_dmpvr2_hv },
	{ .compatible = "renesas,raa228228", .data = (void *)raa_dmpvr2_2rail_nontc },
	{ .compatible = "renesas,raa228244", .data = (void *)raa_dmpvr2_2rail_nontc },
	{ .compatible = "renesas,raa228246", .data = (void *)raa_dmpvr2_2rail_nontc },
	{ .compatible = "renesas,raa229001", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,raa229004", .data = (void *)raa_dmpvr2_2rail },
	{ .compatible = "renesas,raa229621", .data = (void *)raa_dmpvr2_2rail },
	{ },
};

MODULE_DEVICE_TABLE(of, isl68137_of_match);

/* This is the driver that will be inserted */
static struct i2c_driver isl68137_driver = {
	.driver = {
		.name = "isl68137",
		.of_match_table = isl68137_of_match,
	},
	.probe = isl68137_probe,
	.id_table = raa_dmpvr_id,
};

module_i2c_driver(isl68137_driver);

MODULE_AUTHOR("Maxim Sloyko <maxims@google.com>");
MODULE_DESCRIPTION("PMBus driver for Renesas digital multiphase voltage regulators");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
