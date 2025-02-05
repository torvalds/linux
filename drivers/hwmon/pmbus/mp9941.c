// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Multi-phase Digital VR Controllers(MP9941)
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

/*
 * Vender specific registers. The MFR_ICC_MAX(0x02) is used to
 * config the iin scale. The MFR_RESO_SET(0xC7) is used to
 * config the vout format. The MFR_VR_MULTI_CONFIG_R1(0x0D) is
 * used to identify the vout vid step.
 */
#define MFR_ICC_MAX	0x02
#define MFR_RESO_SET	0xC7
#define MFR_VR_MULTI_CONFIG_R1	0x0D

#define MP9941_VIN_LIMIT_UINT	1
#define MP9941_VIN_LIMIT_DIV	8
#define MP9941_READ_VIN_UINT	1
#define MP9941_READ_VIN_DIV	32

#define MP9941_PAGE_NUM	1

#define MP9941_RAIL1_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | \
							PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT | \
							PMBUS_HAVE_TEMP | PMBUS_HAVE_PIN | \
							PMBUS_HAVE_IIN | \
							PMBUS_HAVE_STATUS_VOUT | \
							PMBUS_HAVE_STATUS_IOUT | \
							PMBUS_HAVE_STATUS_TEMP | \
							PMBUS_HAVE_STATUS_INPUT)

struct mp9941_data {
	struct pmbus_driver_info info;
	int vid_resolution;
};

#define to_mp9941_data(x) container_of(x, struct mp9941_data, info)

static int mp9941_set_vout_format(struct i2c_client *client)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_RESO_SET);
	if (ret < 0)
		return ret;

	/*
	 * page = 0, MFR_RESO_SET[7:6] defines the vout format
	 * 2'b11 set the vout format as direct
	 */
	ret = (ret & ~GENMASK(7, 6)) | FIELD_PREP(GENMASK(7, 6), 3);

	return i2c_smbus_write_word_data(client, MFR_RESO_SET, ret);
}

static int
mp9941_identify_vid_resolution(struct i2c_client *client, struct pmbus_driver_info *info)
{
	struct mp9941_data *data = to_mp9941_data(info);
	int ret;

	/*
	 * page = 2, MFR_VR_MULTI_CONFIG_R1[4:4] defines rail1 vid step value
	 * 1'b0 represents the vid step value is 10mV
	 * 1'b1 represents the vid step value is 5mV
	 */
	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 2);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_VR_MULTI_CONFIG_R1);
	if (ret < 0)
		return ret;

	if (FIELD_GET(GENMASK(4, 4), ret))
		data->vid_resolution = 5;
	else
		data->vid_resolution = 10;

	return 0;
}

static int mp9941_identify_iin_scale(struct i2c_client *client)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_RESO_SET);
	if (ret < 0)
		return ret;

	ret = (ret & ~GENMASK(3, 2)) | FIELD_PREP(GENMASK(3, 2), 0);

	ret = i2c_smbus_write_word_data(client, MFR_RESO_SET, ret);
	if (ret < 0)
		return ret;

	/*
	 * page = 2, MFR_ICC_MAX[15:13] defines the iin scale
	 * 3'b000 set the iout scale as 0.5A/Lsb
	 */
	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 2);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_ICC_MAX);
	if (ret < 0)
		return ret;

	ret = (ret & ~GENMASK(15, 13)) | FIELD_PREP(GENMASK(15, 13), 0);

	return i2c_smbus_write_word_data(client, MFR_ICC_MAX, ret);
}

static int mp9941_identify(struct i2c_client *client, struct pmbus_driver_info *info)
{
	int ret;

	ret = mp9941_identify_iin_scale(client);
	if (ret < 0)
		return ret;

	ret = mp9941_identify_vid_resolution(client, info);
	if (ret < 0)
		return ret;

	return mp9941_set_vout_format(client);
}

static int mp9941_read_word_data(struct i2c_client *client, int page, int phase,
				 int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp9941_data *data = to_mp9941_data(info);
	int ret;

	switch (reg) {
	case PMBUS_READ_VIN:
		/* The MP9941 vin scale is (1/32V)/Lsb */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(9, 0)) * MP9941_READ_VIN_UINT,
					MP9941_READ_VIN_DIV);
		break;
	case PMBUS_READ_IIN:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = ret & GENMASK(10, 0);
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
		/* The MP9941 vin ov limit scale is (1/8V)/Lsb */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(7, 0)) * MP9941_VIN_LIMIT_UINT,
					MP9941_VIN_LIMIT_DIV);
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = ret & GENMASK(7, 0);
		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
	case PMBUS_MFR_VOUT_MIN:
	case PMBUS_MFR_VOUT_MAX:
		/*
		 * The vout scale is set to 1mV/Lsb(using r/m/b scale).
		 * But the vout uv limit and vout max/min scale is 1VID/Lsb,
		 * so the vout uv limit and vout max/min value should be
		 * multiplied by vid resolution.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = ret * data->vid_resolution;
		break;
	case PMBUS_READ_IOUT:
	case PMBUS_READ_POUT:
	case PMBUS_READ_TEMPERATURE_1:
	case PMBUS_READ_VOUT:
	case PMBUS_READ_PIN:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		ret = -ENODATA;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mp9941_write_word_data(struct i2c_client *client, int page, int reg,
				  u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp9941_data *data = to_mp9941_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIN_OV_FAULT_LIMIT:
		/* The MP9941 vin ov limit scale is (1/8V)/Lsb */
		ret = pmbus_write_word_data(client, page, reg,
					    DIV_ROUND_CLOSEST(word * MP9941_VIN_LIMIT_DIV,
							      MP9941_VIN_LIMIT_UINT));
		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
	case PMBUS_MFR_VOUT_MIN:
	case PMBUS_MFR_VOUT_MAX:
		ret = pmbus_write_word_data(client, page, reg,
					    DIV_ROUND_CLOSEST(word, data->vid_resolution));
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		ret = -ENODATA;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct pmbus_driver_info mp9941_info = {
	.pages = MP9941_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,

	.m[PSC_TEMPERATURE] = 1,
	.R[PSC_TEMPERATURE] = 0,
	.b[PSC_TEMPERATURE] = 0,

	.m[PSC_VOLTAGE_IN] = 1,
	.R[PSC_VOLTAGE_IN] = 0,
	.b[PSC_VOLTAGE_IN] = 0,

	.m[PSC_CURRENT_IN] = 2,
	.R[PSC_CURRENT_IN] = 0,
	.b[PSC_CURRENT_IN] = 0,

	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.b[PSC_VOLTAGE_OUT] = 0,

	.func[0] = MP9941_RAIL1_FUNC,
	.read_word_data = mp9941_read_word_data,
	.write_word_data = mp9941_write_word_data,
	.identify = mp9941_identify,
};

static int mp9941_probe(struct i2c_client *client)
{
	struct mp9941_data *data;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mp9941_info, sizeof(mp9941_info));

	return pmbus_do_probe(client, &data->info);
}

static const struct i2c_device_id mp9941_id[] = {
	{ "mp9941" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mp9941_id);

static const struct of_device_id __maybe_unused mp9941_of_match[] = {
	{.compatible = "mps,mp9941"},
	{}
};
MODULE_DEVICE_TABLE(of, mp9941_of_match);

static struct i2c_driver mp9941_driver = {
	.driver = {
		.name = "mp9941",
		.of_match_table = mp9941_of_match,
	},
	.probe = mp9941_probe,
	.id_table = mp9941_id,
};

module_i2c_driver(mp9941_driver);

MODULE_AUTHOR("Noah Wang <noahwang.wang@outlook.com>");
MODULE_DESCRIPTION("PMBus driver for MPS MP9941");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
