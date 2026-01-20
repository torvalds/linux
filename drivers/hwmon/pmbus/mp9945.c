// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Single-phase Digital VR Controllers(MP9945)
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

#define MFR_VR_MULTI_CONFIG_R1	0x08
#define MFR_SVID_CFG_R1		0xBD

/* VOUT_MODE register values */
#define VOUT_MODE_LINEAR16	0x17
#define VOUT_MODE_VID		0x21
#define VOUT_MODE_DIRECT	0x40

#define MP9945_PAGE_NUM		1

#define MP9945_RAIL1_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | \
				PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | \
				PMBUS_HAVE_PIN | PMBUS_HAVE_POUT | \
				PMBUS_HAVE_TEMP | \
				PMBUS_HAVE_STATUS_VOUT | \
				PMBUS_HAVE_STATUS_IOUT | \
				PMBUS_HAVE_STATUS_TEMP | \
				PMBUS_HAVE_STATUS_INPUT)

enum mp9945_vout_mode {
	MP9945_VOUT_MODE_VID,
	MP9945_VOUT_MODE_DIRECT,
	MP9945_VOUT_MODE_LINEAR16,
};

struct mp9945_data {
	struct pmbus_driver_info info;
	enum mp9945_vout_mode vout_mode;
	int vid_resolution;
	int vid_offset;
};

#define to_mp9945_data(x) container_of(x, struct mp9945_data, info)

static int mp9945_read_vout(struct i2c_client *client, struct mp9945_data *data)
{
	int ret;

	ret = i2c_smbus_read_word_data(client, PMBUS_READ_VOUT);
	if (ret < 0)
		return ret;

	ret &= GENMASK(11, 0);

	switch (data->vout_mode) {
	case MP9945_VOUT_MODE_VID:
		if (ret > 0)
			ret = (ret + data->vid_offset) * data->vid_resolution;
		break;
	case MP9945_VOUT_MODE_DIRECT:
		break;
	case MP9945_VOUT_MODE_LINEAR16:
		/* LSB: 1000 * 2^-9 (mV) */
		ret = DIV_ROUND_CLOSEST(ret * 125, 64);
		break;
	default:
		return -ENODEV;
	}

	return ret;
}

static int mp9945_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		/*
		 * Override VOUT_MODE to DIRECT as the driver handles custom
		 * VOUT format conversions internally.
		 */
		return PB_VOUT_MODE_DIRECT;
	default:
		return -ENODATA;
	}
}

static int mp9945_read_word_data(struct i2c_client *client, int page, int phase,
				 int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp9945_data *data = to_mp9945_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	switch (reg) {
	case PMBUS_READ_VOUT:
		ret = mp9945_read_vout(client, data);
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = i2c_smbus_read_word_data(client, reg);
		if (ret < 0)
			return ret;

		/* LSB: 1.95 (mV) */
		ret = DIV_ROUND_CLOSEST((ret & GENMASK(11, 0)) * 39, 20);
		break;
	case PMBUS_VOUT_UV_WARN_LIMIT:
		ret = i2c_smbus_read_word_data(client, reg);
		if (ret < 0)
			return ret;

		ret &= GENMASK(9, 0);
		if (ret > 0)
			ret = (ret + data->vid_offset) * data->vid_resolution;
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int mp9945_identify(struct i2c_client *client,
			   struct pmbus_driver_info *info)
{
	struct mp9945_data *data = to_mp9945_data(info);
	int ret;

	ret = i2c_smbus_read_byte_data(client, PMBUS_VOUT_MODE);
	if (ret < 0)
		return ret;

	switch (ret) {
	case VOUT_MODE_LINEAR16:
		data->vout_mode = MP9945_VOUT_MODE_LINEAR16;
		break;
	case VOUT_MODE_VID:
		data->vout_mode = MP9945_VOUT_MODE_VID;
		break;
	case VOUT_MODE_DIRECT:
		data->vout_mode = MP9945_VOUT_MODE_DIRECT;
		break;
	default:
		return -ENODEV;
	}

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 3);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_VR_MULTI_CONFIG_R1);
	if (ret < 0)
		return ret;

	data->vid_resolution = (FIELD_GET(BIT(2), ret)) ? 5 : 10;

	ret = i2c_smbus_read_word_data(client, MFR_SVID_CFG_R1);
	if (ret < 0)
		return ret;

	data->vid_offset = (FIELD_GET(BIT(15), ret)) ? 19 : 49;

	return i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
}

static struct pmbus_driver_info mp9945_info = {
	.pages = MP9945_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.b[PSC_VOLTAGE_OUT] = 0,
	.func[0] = MP9945_RAIL1_FUNC,
	.read_word_data = mp9945_read_word_data,
	.read_byte_data = mp9945_read_byte_data,
	.identify = mp9945_identify,
};

static int mp9945_probe(struct i2c_client *client)
{
	struct mp9945_data *data;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mp9945_info, sizeof(mp9945_info));

	/*
	 * Set page 0 before probe. The core reads paged registers which are
	 * only on page 0 for this device.
	 */
	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	return pmbus_do_probe(client, &data->info);
}

static const struct i2c_device_id mp9945_id[] = {
	{"mp9945"},
	{}
};
MODULE_DEVICE_TABLE(i2c, mp9945_id);

static const struct of_device_id __maybe_unused mp9945_of_match[] = {
	{.compatible = "mps,mp9945"},
	{}
};
MODULE_DEVICE_TABLE(of, mp9945_of_match);

static struct i2c_driver mp9945_driver = {
	.driver = {
		.name = "mp9945",
		.of_match_table = of_match_ptr(mp9945_of_match),
	},
	.probe = mp9945_probe,
	.id_table = mp9945_id,
};

module_i2c_driver(mp9945_driver);

MODULE_AUTHOR("Cosmo Chou <chou.cosmo@gmail.com>");
MODULE_DESCRIPTION("PMBus driver for MPS MP9945");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
