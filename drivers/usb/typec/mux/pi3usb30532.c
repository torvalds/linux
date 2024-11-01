// SPDX-License-Identifier: GPL-2.0+
/*
 * Pericom PI3USB30532 Type-C cross switch / mux driver
 *
 * Copyright (c) 2017-2018 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>

#define PI3USB30532_CONF			0x00

#define PI3USB30532_CONF_OPEN			0x00
#define PI3USB30532_CONF_SWAP			0x01
#define PI3USB30532_CONF_4LANE_DP		0x02
#define PI3USB30532_CONF_USB3			0x04
#define PI3USB30532_CONF_USB3_AND_2LANE_DP	0x06

struct pi3usb30532 {
	struct i2c_client *client;
	struct mutex lock; /* protects the cached conf register */
	struct typec_switch_dev *sw;
	struct typec_mux_dev *mux;
	u8 conf;
};

static int pi3usb30532_set_conf(struct pi3usb30532 *pi, u8 new_conf)
{
	int ret = 0;

	if (pi->conf == new_conf)
		return 0;

	ret = i2c_smbus_write_byte_data(pi->client, PI3USB30532_CONF, new_conf);
	if (ret) {
		dev_err(&pi->client->dev, "Error writing conf: %d\n", ret);
		return ret;
	}

	pi->conf = new_conf;
	return 0;
}

static int pi3usb30532_sw_set(struct typec_switch_dev *sw,
			      enum typec_orientation orientation)
{
	struct pi3usb30532 *pi = typec_switch_get_drvdata(sw);
	u8 new_conf;
	int ret;

	mutex_lock(&pi->lock);
	new_conf = pi->conf;

	switch (orientation) {
	case TYPEC_ORIENTATION_NONE:
		new_conf = PI3USB30532_CONF_OPEN;
		break;
	case TYPEC_ORIENTATION_NORMAL:
		new_conf &= ~PI3USB30532_CONF_SWAP;
		break;
	case TYPEC_ORIENTATION_REVERSE:
		new_conf |= PI3USB30532_CONF_SWAP;
		break;
	}

	ret = pi3usb30532_set_conf(pi, new_conf);
	mutex_unlock(&pi->lock);

	return ret;
}

static int
pi3usb30532_mux_set(struct typec_mux_dev *mux, struct typec_mux_state *state)
{
	struct pi3usb30532 *pi = typec_mux_get_drvdata(mux);
	u8 new_conf;
	int ret;

	mutex_lock(&pi->lock);
	new_conf = pi->conf;

	switch (state->mode) {
	case TYPEC_STATE_SAFE:
		new_conf = (new_conf & PI3USB30532_CONF_SWAP) |
			   PI3USB30532_CONF_OPEN;
		break;
	case TYPEC_STATE_USB:
		new_conf = (new_conf & PI3USB30532_CONF_SWAP) |
			   PI3USB30532_CONF_USB3;
		break;
	case TYPEC_DP_STATE_C:
	case TYPEC_DP_STATE_E:
		new_conf = (new_conf & PI3USB30532_CONF_SWAP) |
			   PI3USB30532_CONF_4LANE_DP;
		break;
	case TYPEC_DP_STATE_D:
		new_conf = (new_conf & PI3USB30532_CONF_SWAP) |
			   PI3USB30532_CONF_USB3_AND_2LANE_DP;
		break;
	default:
		break;
	}

	ret = pi3usb30532_set_conf(pi, new_conf);
	mutex_unlock(&pi->lock);

	return ret;
}

static int pi3usb30532_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct typec_switch_desc sw_desc = { };
	struct typec_mux_desc mux_desc = { };
	struct pi3usb30532 *pi;
	int ret;

	pi = devm_kzalloc(dev, sizeof(*pi), GFP_KERNEL);
	if (!pi)
		return -ENOMEM;

	pi->client = client;
	mutex_init(&pi->lock);

	ret = i2c_smbus_read_byte_data(client, PI3USB30532_CONF);
	if (ret < 0) {
		dev_err(dev, "Error reading config register %d\n", ret);
		return ret;
	}
	pi->conf = ret;

	sw_desc.drvdata = pi;
	sw_desc.fwnode = dev->fwnode;
	sw_desc.set = pi3usb30532_sw_set;

	pi->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(pi->sw)) {
		dev_err(dev, "Error registering typec switch: %ld\n",
			PTR_ERR(pi->sw));
		return PTR_ERR(pi->sw);
	}

	mux_desc.drvdata = pi;
	mux_desc.fwnode = dev->fwnode;
	mux_desc.set = pi3usb30532_mux_set;

	pi->mux = typec_mux_register(dev, &mux_desc);
	if (IS_ERR(pi->mux)) {
		typec_switch_unregister(pi->sw);
		dev_err(dev, "Error registering typec mux: %ld\n",
			PTR_ERR(pi->mux));
		return PTR_ERR(pi->mux);
	}

	i2c_set_clientdata(client, pi);
	return 0;
}

static void pi3usb30532_remove(struct i2c_client *client)
{
	struct pi3usb30532 *pi = i2c_get_clientdata(client);

	typec_mux_unregister(pi->mux);
	typec_switch_unregister(pi->sw);
}

static const struct i2c_device_id pi3usb30532_table[] = {
	{ "pi3usb30532" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pi3usb30532_table);

static struct i2c_driver pi3usb30532_driver = {
	.driver = {
		.name = "pi3usb30532",
	},
	.probe_new	= pi3usb30532_probe,
	.remove		= pi3usb30532_remove,
	.id_table	= pi3usb30532_table,
};

module_i2c_driver(pi3usb30532_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Pericom PI3USB30532 Type-C mux driver");
MODULE_LICENSE("GPL");
