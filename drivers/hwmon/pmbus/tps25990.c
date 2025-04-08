// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2024 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "pmbus.h"

#define TPS25990_READ_VAUX		0xd0
#define TPS25990_READ_VIN_MIN		0xd1
#define TPS25990_READ_VIN_PEAK		0xd2
#define TPS25990_READ_IIN_PEAK		0xd4
#define TPS25990_READ_PIN_PEAK		0xd5
#define TPS25990_READ_TEMP_AVG		0xd6
#define TPS25990_READ_TEMP_PEAK		0xd7
#define TPS25990_READ_VOUT_MIN		0xda
#define TPS25990_READ_VIN_AVG		0xdc
#define TPS25990_READ_VOUT_AVG		0xdd
#define TPS25990_READ_IIN_AVG		0xde
#define TPS25990_READ_PIN_AVG		0xdf
#define TPS25990_VIREF			0xe0
#define TPS25990_PK_MIN_AVG		0xea
#define  PK_MIN_AVG_RST_PEAK		BIT(7)
#define  PK_MIN_AVG_RST_AVG		BIT(6)
#define  PK_MIN_AVG_RST_MIN		BIT(5)
#define  PK_MIN_AVG_AVG_CNT		GENMASK(2, 0)
#define TPS25990_MFR_WRITE_PROTECT	0xf8
#define  TPS25990_UNLOCKED		BIT(7)

#define TPS25990_8B_SHIFT		2
#define TPS25990_VIN_OVF_NUM		525100
#define TPS25990_VIN_OVF_DIV		10163
#define TPS25990_VIN_OVF_OFF		155
#define TPS25990_IIN_OCF_NUM		953800
#define TPS25990_IIN_OCF_DIV		129278
#define TPS25990_IIN_OCF_OFF		157

#define PK_MIN_AVG_RST_MASK		(PK_MIN_AVG_RST_PEAK | \
					 PK_MIN_AVG_RST_AVG  | \
					 PK_MIN_AVG_RST_MIN)

/*
 * Arbitrary default Rimon value: 1kOhm
 * This correspond to an overcurrent limit of 55A, close to the specified limit
 * of un-stacked TPS25990 and makes further calculation easier to setup in
 * sensor.conf, if necessary
 */
#define TPS25990_DEFAULT_RIMON		1000000000

static void tps25990_set_m(int *m, u32 rimon)
{
	u64 val = ((u64)*m) * rimon;

	/* Make sure m fits the s32 type */
	*m = DIV_ROUND_CLOSEST_ULL(val, 1000000);
}

static int tps25990_mfr_write_protect_set(struct i2c_client *client,
					  u8 protect)
{
	u8 val;

	switch (protect) {
	case 0:
		val = 0xa2;
		break;
	case PB_WP_ALL:
		val = 0x0;
		break;
	default:
		return -EINVAL;
	}

	return pmbus_write_byte_data(client, -1, TPS25990_MFR_WRITE_PROTECT,
				     val);
}

static int tps25990_mfr_write_protect_get(struct i2c_client *client)
{
	int ret = pmbus_read_byte_data(client, -1, TPS25990_MFR_WRITE_PROTECT);

	if (ret < 0)
		return ret;

	return (ret & TPS25990_UNLOCKED) ? 0 : PB_WP_ALL;
}

static int tps25990_read_word_data(struct i2c_client *client,
				   int page, int phase, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VIN_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_VIN_PEAK);
		break;

	case PMBUS_VIRT_READ_VIN_MIN:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_VIN_MIN);
		break;

	case PMBUS_VIRT_READ_VIN_AVG:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_VIN_AVG);
		break;

	case PMBUS_VIRT_READ_VOUT_MIN:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_VOUT_MIN);
		break;

	case PMBUS_VIRT_READ_VOUT_AVG:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_VOUT_AVG);
		break;

	case PMBUS_VIRT_READ_IIN_AVG:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_IIN_AVG);
		break;

	case PMBUS_VIRT_READ_IIN_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_IIN_PEAK);
		break;

	case PMBUS_VIRT_READ_TEMP_AVG:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_TEMP_AVG);
		break;

	case PMBUS_VIRT_READ_TEMP_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_TEMP_PEAK);
		break;

	case PMBUS_VIRT_READ_PIN_AVG:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_PIN_AVG);
		break;

	case PMBUS_VIRT_READ_PIN_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_PIN_PEAK);
		break;

	case PMBUS_VIRT_READ_VMON:
		ret = pmbus_read_word_data(client, page, phase,
					   TPS25990_READ_VAUX);
		break;

	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_IIN_OC_WARN_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_PIN_OP_WARN_LIMIT:
		/*
		 * These registers provide an 8 bits value instead of a
		 * 10bits one. Just shifting twice the register value is
		 * enough to make the sensor type conversion work, even
		 * if the datasheet provides different m, b and R for
		 * those.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			break;
		ret <<= TPS25990_8B_SHIFT;
		break;

	case PMBUS_VIN_OV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			break;
		ret = DIV_ROUND_CLOSEST(ret * TPS25990_VIN_OVF_NUM,
					TPS25990_VIN_OVF_DIV);
		ret += TPS25990_VIN_OVF_OFF;
		break;

	case PMBUS_IIN_OC_FAULT_LIMIT:
		/*
		 * VIREF directly sets the over-current limit at which the eFuse
		 * will turn the FET off and trigger a fault. Expose it through
		 * this generic property instead of a manufacturer specific one.
		 */
		ret = pmbus_read_byte_data(client, page, TPS25990_VIREF);
		if (ret < 0)
			break;
		ret = DIV_ROUND_CLOSEST(ret * TPS25990_IIN_OCF_NUM,
					TPS25990_IIN_OCF_DIV);
		ret += TPS25990_IIN_OCF_OFF;
		break;

	case PMBUS_VIRT_SAMPLES:
		ret = pmbus_read_byte_data(client, page, TPS25990_PK_MIN_AVG);
		if (ret < 0)
			break;
		ret = 1 << FIELD_GET(PK_MIN_AVG_AVG_CNT, ret);
		break;

	case PMBUS_VIRT_RESET_TEMP_HISTORY:
	case PMBUS_VIRT_RESET_VIN_HISTORY:
	case PMBUS_VIRT_RESET_IIN_HISTORY:
	case PMBUS_VIRT_RESET_PIN_HISTORY:
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		ret = 0;
		break;

	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int tps25990_write_word_data(struct i2c_client *client,
				    int page, int reg, u16 value)
{
	int ret;

	switch (reg) {
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_IIN_OC_WARN_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_PIN_OP_WARN_LIMIT:
		value >>= TPS25990_8B_SHIFT;
		value = clamp_val(value, 0, 0xff);
		ret = pmbus_write_word_data(client, page, reg, value);
		break;

	case PMBUS_VIN_OV_FAULT_LIMIT:
		value -= TPS25990_VIN_OVF_OFF;
		value = DIV_ROUND_CLOSEST(((unsigned int)value) * TPS25990_VIN_OVF_DIV,
					  TPS25990_VIN_OVF_NUM);
		value = clamp_val(value, 0, 0xf);
		ret = pmbus_write_word_data(client, page, reg, value);
		break;

	case PMBUS_IIN_OC_FAULT_LIMIT:
		value -= TPS25990_IIN_OCF_OFF;
		value = DIV_ROUND_CLOSEST(((unsigned int)value) * TPS25990_IIN_OCF_DIV,
					  TPS25990_IIN_OCF_NUM);
		value = clamp_val(value, 0, 0x3f);
		ret = pmbus_write_byte_data(client, page, TPS25990_VIREF, value);
		break;

	case PMBUS_VIRT_SAMPLES:
		value = clamp_val(value, 1, 1 << PK_MIN_AVG_AVG_CNT);
		value = ilog2(value);
		ret = pmbus_update_byte_data(client, page, TPS25990_PK_MIN_AVG,
					     PK_MIN_AVG_AVG_CNT,
					     FIELD_PREP(PK_MIN_AVG_AVG_CNT, value));
		break;

	case PMBUS_VIRT_RESET_TEMP_HISTORY:
	case PMBUS_VIRT_RESET_VIN_HISTORY:
	case PMBUS_VIRT_RESET_IIN_HISTORY:
	case PMBUS_VIRT_RESET_PIN_HISTORY:
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		/*
		 * TPS25990 has history resets based on MIN/AVG/PEAK instead of per
		 * sensor type. Exposing this quirk in hwmon is not desirable so
		 * reset MIN, AVG and PEAK together. Even is there effectively only
		 * one reset, which resets everything, expose the 5 entries so
		 * userspace is not required map a sensor type to another to trigger
		 * a reset
		 */
		ret = pmbus_update_byte_data(client, 0, TPS25990_PK_MIN_AVG,
					     PK_MIN_AVG_RST_MASK,
					     PK_MIN_AVG_RST_MASK);
		break;

	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int tps25990_read_byte_data(struct i2c_client *client,
				   int page, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_WRITE_PROTECT:
		ret = tps25990_mfr_write_protect_get(client);
		break;

	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int tps25990_write_byte_data(struct i2c_client *client,
				    int page, int reg, u8 byte)
{
	int ret;

	switch (reg) {
	case PMBUS_WRITE_PROTECT:
		ret = tps25990_mfr_write_protect_set(client, byte);
		break;

	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

#if IS_ENABLED(CONFIG_SENSORS_TPS25990_REGULATOR)
static const struct regulator_desc tps25990_reg_desc[] = {
	PMBUS_REGULATOR_ONE("vout"),
};
#endif

static const struct pmbus_driver_info tps25990_base_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.m[PSC_VOLTAGE_IN] = 5251,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = -2,
	.format[PSC_VOLTAGE_OUT] = direct,
	.m[PSC_VOLTAGE_OUT] = 5251,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = -2,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_TEMPERATURE] = 140,
	.b[PSC_TEMPERATURE] = 32100,
	.R[PSC_TEMPERATURE] = -2,
	/*
	 * Current and Power measurement depends on the ohm value
	 * of Rimon. m is multiplied by 1000 below to have an integer
	 * and -3 is added to R to compensate.
	 */
	.format[PSC_CURRENT_IN] = direct,
	.m[PSC_CURRENT_IN] = 9538,
	.b[PSC_CURRENT_IN] = 0,
	.R[PSC_CURRENT_IN] = -6,
	.format[PSC_POWER] = direct,
	.m[PSC_POWER] = 4901,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = -7,
	.func[0] = (PMBUS_HAVE_VIN |
		    PMBUS_HAVE_VOUT |
		    PMBUS_HAVE_VMON |
		    PMBUS_HAVE_IIN |
		    PMBUS_HAVE_PIN |
		    PMBUS_HAVE_TEMP |
		    PMBUS_HAVE_STATUS_VOUT |
		    PMBUS_HAVE_STATUS_IOUT |
		    PMBUS_HAVE_STATUS_INPUT |
		    PMBUS_HAVE_STATUS_TEMP |
		    PMBUS_HAVE_SAMPLES),
	.read_word_data = tps25990_read_word_data,
	.write_word_data = tps25990_write_word_data,
	.read_byte_data = tps25990_read_byte_data,
	.write_byte_data = tps25990_write_byte_data,

#if IS_ENABLED(CONFIG_SENSORS_TPS25990_REGULATOR)
	.reg_desc = tps25990_reg_desc,
	.num_regulators = ARRAY_SIZE(tps25990_reg_desc),
#endif
};

static const struct i2c_device_id tps25990_i2c_id[] = {
	{ "tps25990" },
	{}
};
MODULE_DEVICE_TABLE(i2c, tps25990_i2c_id);

static const struct of_device_id tps25990_of_match[] = {
	{ .compatible = "ti,tps25990" },
	{}
};
MODULE_DEVICE_TABLE(of, tps25990_of_match);

static int tps25990_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pmbus_driver_info *info;
	u32 rimon = TPS25990_DEFAULT_RIMON;
	int ret;

	ret = device_property_read_u32(dev, "ti,rimon-micro-ohms", &rimon);
	if (ret < 0 && ret != -EINVAL)
		return dev_err_probe(dev, ret, "failed to get rimon\n");

	info = devm_kmemdup(dev, &tps25990_base_info, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* Adapt the current and power scale for each instance */
	tps25990_set_m(&info->m[PSC_CURRENT_IN], rimon);
	tps25990_set_m(&info->m[PSC_POWER], rimon);

	return pmbus_do_probe(client, info);
}

static struct i2c_driver tps25990_driver = {
	.driver = {
		.name = "tps25990",
		.of_match_table = tps25990_of_match,
	},
	.probe = tps25990_probe,
	.id_table = tps25990_i2c_id,
};
module_i2c_driver(tps25990_driver);

MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_DESCRIPTION("PMBUS driver for TPS25990 eFuse");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
