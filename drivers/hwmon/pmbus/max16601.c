// SPDX-License-Identifier: GPL-2.0
/*
 * Hardware monitoring driver for Maxim MAX16601
 *
 * Implementation notes:
 *
 * Ths chip supports two rails, VCORE and VSA. Telemetry information for the
 * two rails is reported in two subsequent I2C addresses. The driver
 * instantiates a dummy I2C client at the second I2C address to report
 * information for the VSA rail in a single instance of the driver.
 * Telemetry for the VSA rail is reported to the PMBus core in PMBus page 2.
 *
 * The chip reports input current using two separate methods. The input current
 * reported with the standard READ_IIN command is derived from the output
 * current. The first method is reported to the PMBus core with PMBus page 0,
 * the second method is reported with PMBus page 1.
 *
 * The chip supports reading per-phase temperatures and per-phase input/output
 * currents for VCORE. Telemetry is reported in vendor specific registers.
 * The driver translates the vendor specific register values to PMBus standard
 * register values and reports per-phase information in PMBus page 0.
 *
 * Copyright 2019, 2020 Google LLC.
 */

#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "pmbus.h"

#define REG_SETPT_DVID		0xd1
#define  DAC_10MV_MODE		BIT(4)
#define REG_IOUT_AVG_PK		0xee
#define REG_IIN_SENSOR		0xf1
#define REG_TOTAL_INPUT_POWER	0xf2
#define REG_PHASE_ID		0xf3
#define  CORE_RAIL_INDICATOR	BIT(7)
#define REG_PHASE_REPORTING	0xf4

struct max16601_data {
	struct pmbus_driver_info info;
	struct i2c_client *vsa;
	int iout_avg_pkg;
};

#define to_max16601_data(x) container_of(x, struct max16601_data, info)

static int max16601_read_byte(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max16601_data *data = to_max16601_data(info);

	if (page > 0) {
		if (page == 2)	/* VSA */
			return i2c_smbus_read_byte_data(data->vsa, reg);
		return -EOPNOTSUPP;
	}
	return -ENODATA;
}

static int max16601_read_word(struct i2c_client *client, int page, int phase,
			      int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max16601_data *data = to_max16601_data(info);
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1];
	int ret;

	switch (page) {
	case 0:		/* VCORE */
		if (phase == 0xff)
			return -ENODATA;
		switch (reg) {
		case PMBUS_READ_IIN:
		case PMBUS_READ_IOUT:
		case PMBUS_READ_TEMPERATURE_1:
			ret = i2c_smbus_write_byte_data(client, REG_PHASE_ID,
							phase);
			if (ret)
				return ret;
			ret = i2c_smbus_read_block_data(client,
							REG_PHASE_REPORTING,
							buf);
			if (ret < 0)
				return ret;
			if (ret < 6)
				return -EIO;
			switch (reg) {
			case PMBUS_READ_TEMPERATURE_1:
				return buf[1] << 8 | buf[0];
			case PMBUS_READ_IOUT:
				return buf[3] << 8 | buf[2];
			case PMBUS_READ_IIN:
				return buf[5] << 8 | buf[4];
			default:
				break;
			}
		}
		return -EOPNOTSUPP;
	case 1:		/* VCORE, read IIN/PIN from sensor element */
		switch (reg) {
		case PMBUS_READ_IIN:
			return i2c_smbus_read_word_data(client, REG_IIN_SENSOR);
		case PMBUS_READ_PIN:
			return i2c_smbus_read_word_data(client,
							REG_TOTAL_INPUT_POWER);
		default:
			break;
		}
		return -EOPNOTSUPP;
	case 2:		/* VSA */
		switch (reg) {
		case PMBUS_VIRT_READ_IOUT_MAX:
			ret = i2c_smbus_read_word_data(data->vsa,
						       REG_IOUT_AVG_PK);
			if (ret < 0)
				return ret;
			if (sign_extend32(ret, 10) >
			    sign_extend32(data->iout_avg_pkg, 10))
				data->iout_avg_pkg = ret;
			return data->iout_avg_pkg;
		case PMBUS_VIRT_RESET_IOUT_HISTORY:
			return 0;
		case PMBUS_IOUT_OC_FAULT_LIMIT:
		case PMBUS_IOUT_OC_WARN_LIMIT:
		case PMBUS_OT_FAULT_LIMIT:
		case PMBUS_OT_WARN_LIMIT:
		case PMBUS_READ_IIN:
		case PMBUS_READ_IOUT:
		case PMBUS_READ_TEMPERATURE_1:
		case PMBUS_STATUS_WORD:
			return i2c_smbus_read_word_data(data->vsa, reg);
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static int max16601_write_byte(struct i2c_client *client, int page, u8 reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max16601_data *data = to_max16601_data(info);

	if (page == 2) {
		if (reg == PMBUS_CLEAR_FAULTS)
			return i2c_smbus_write_byte(data->vsa, reg);
		return -EOPNOTSUPP;
	}
	return -ENODATA;
}

static int max16601_write_word(struct i2c_client *client, int page, int reg,
			       u16 value)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct max16601_data *data = to_max16601_data(info);

	switch (page) {
	case 0:		/* VCORE */
		return -ENODATA;
	case 1:		/* VCORE IIN/PIN from sensor element */
	default:
		return -EOPNOTSUPP;
	case 2:		/* VSA */
		switch (reg) {
		case PMBUS_VIRT_RESET_IOUT_HISTORY:
			data->iout_avg_pkg = 0xfc00;
			return 0;
		case PMBUS_IOUT_OC_FAULT_LIMIT:
		case PMBUS_IOUT_OC_WARN_LIMIT:
		case PMBUS_OT_FAULT_LIMIT:
		case PMBUS_OT_WARN_LIMIT:
			return i2c_smbus_write_word_data(data->vsa, reg, value);
		default:
			return -EOPNOTSUPP;
		}
	}
}

static int max16601_identify(struct i2c_client *client,
			     struct pmbus_driver_info *info)
{
	int reg;

	reg = i2c_smbus_read_byte_data(client, REG_SETPT_DVID);
	if (reg < 0)
		return reg;
	if (reg & DAC_10MV_MODE)
		info->vrm_version[0] = vr13;
	else
		info->vrm_version[0] = vr12;

	return 0;
}

static struct pmbus_driver_info max16601_info = {
	.pages = 3,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = vid,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_POWER] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN | PMBUS_HAVE_PIN |
		PMBUS_HAVE_STATUS_INPUT |
		PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_POUT | PMBUS_PAGE_VIRTUAL | PMBUS_PHASE_VIRTUAL,
	.func[1] = PMBUS_HAVE_IIN | PMBUS_HAVE_PIN | PMBUS_PAGE_VIRTUAL,
	.func[2] = PMBUS_HAVE_IIN | PMBUS_HAVE_STATUS_INPUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP | PMBUS_PAGE_VIRTUAL,
	.phases[0] = 8,
	.pfunc[0] = PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_TEMP,
	.pfunc[1] = PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT,
	.pfunc[2] = PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_TEMP,
	.pfunc[3] = PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT,
	.pfunc[4] = PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_TEMP,
	.pfunc[5] = PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT,
	.pfunc[6] = PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_TEMP,
	.pfunc[7] = PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT,
	.identify = max16601_identify,
	.read_byte_data = max16601_read_byte,
	.read_word_data = max16601_read_word,
	.write_byte = max16601_write_byte,
	.write_word_data = max16601_write_word,
};

static void max16601_remove(void *_data)
{
	struct max16601_data *data = _data;

	i2c_unregister_device(data->vsa);
}

static int max16601_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1];
	struct max16601_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA |
				     I2C_FUNC_SMBUS_READ_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, PMBUS_IC_DEVICE_ID, buf);
	if (ret < 0)
		return -ENODEV;

	/* PMBUS_IC_DEVICE_ID is expected to return "MAX16601y.xx" */
	if (ret < 11 || strncmp(buf, "MAX16601", 8)) {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported chip '%s'\n", buf);
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(client, REG_PHASE_ID);
	if (ret < 0)
		return ret;
	if (!(ret & CORE_RAIL_INDICATOR)) {
		dev_err(dev,
			"Driver must be instantiated on CORE rail I2C address\n");
		return -ENODEV;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->iout_avg_pkg = 0xfc00;
	data->vsa = i2c_new_dummy_device(client->adapter, client->addr + 1);
	if (IS_ERR(data->vsa)) {
		dev_err(dev, "Failed to register VSA client\n");
		return PTR_ERR(data->vsa);
	}
	ret = devm_add_action_or_reset(dev, max16601_remove, data);
	if (ret)
		return ret;

	data->info = max16601_info;

	return pmbus_do_probe(client, id, &data->info);
}

static const struct i2c_device_id max16601_id[] = {
	{"max16601", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, max16601_id);

static struct i2c_driver max16601_driver = {
	.driver = {
		   .name = "max16601",
		   },
	.probe = max16601_probe,
	.remove = pmbus_do_remove,
	.id_table = max16601_id,
};

module_i2c_driver(max16601_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("PMBus driver for Maxim MAX16601");
MODULE_LICENSE("GPL v2");
