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
#include <linux/usb/tcpm.h>
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
	struct typec_switch sw;
	struct typec_mux mux;
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

static int pi3usb30532_sw_set(struct typec_switch *sw,
			      enum typec_orientation orientation)
{
	struct pi3usb30532 *pi = container_of(sw, struct pi3usb30532, sw);
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

static int pi3usb30532_mux_set(struct typec_mux *mux, int state)
{
	struct pi3usb30532 *pi = container_of(mux, struct pi3usb30532, mux);
	u8 new_conf;
	int ret;

	mutex_lock(&pi->lock);
	new_conf = pi->conf;

	switch (state) {
	case TYPEC_MUX_NONE:
		new_conf = PI3USB30532_CONF_OPEN;
		break;
	case TYPEC_MUX_USB:
		new_conf = (new_conf & PI3USB30532_CONF_SWAP) |
			   PI3USB30532_CONF_USB3;
		break;
	case TYPEC_MUX_DP:
		new_conf = (new_conf & PI3USB30532_CONF_SWAP) |
			   PI3USB30532_CONF_4LANE_DP;
		break;
	case TYPEC_MUX_DOCK:
		new_conf = (new_conf & PI3USB30532_CONF_SWAP) |
			   PI3USB30532_CONF_USB3_AND_2LANE_DP;
		break;
	}

	ret = pi3usb30532_set_conf(pi, new_conf);
	mutex_unlock(&pi->lock);

	return ret;
}

static int pi3usb30532_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pi3usb30532 *pi;
	int ret;

	pi = devm_kzalloc(dev, sizeof(*pi), GFP_KERNEL);
	if (!pi)
		return -ENOMEM;

	pi->client = client;
	pi->sw.dev = dev;
	pi->sw.set = pi3usb30532_sw_set;
	pi->mux.dev = dev;
	pi->mux.set = pi3usb30532_mux_set;
	mutex_init(&pi->lock);

	ret = i2c_smbus_read_byte_data(client, PI3USB30532_CONF);
	if (ret < 0) {
		dev_err(dev, "Error reading config register %d\n", ret);
		return ret;
	}
	pi->conf = ret;

	ret = typec_switch_register(&pi->sw);
	if (ret) {
		dev_err(dev, "Error registering typec switch: %d\n", ret);
		return ret;
	}

	ret = typec_mux_register(&pi->mux);
	if (ret) {
		typec_switch_unregister(&pi->sw);
		dev_err(dev, "Error registering typec mux: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, pi);
	return 0;
}

static int pi3usb30532_remove(struct i2c_client *client)
{
	struct pi3usb30532 *pi = i2c_get_clientdata(client);

	typec_mux_unregister(&pi->mux);
	typec_switch_unregister(&pi->sw);
	return 0;
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
