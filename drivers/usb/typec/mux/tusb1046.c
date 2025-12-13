// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the TUSB1046-DCI USB Type-C crosspoint switch
 *
 * Copyright (C) 2024 Bootlin
 */

#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_altmode.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/mutex.h>

#define TUSB1046_REG_GENERAL 0xa

/* General register bits */
#define TUSB1046_GENERAL_FLIPSEL BIT(2)
#define TUSB1046_GENERAL_CTLSEL  GENMASK(1, 0)

/* Mux modes */
#define TUSB1046_CTLSEL_DISABLED          0x0
#define TUSB1046_CTLSEL_USB3              0x1
#define TUSB1046_CTLSEL_4LANE_DP          0x2
#define TUSB1046_CTLSEL_USB3_AND_2LANE_DP 0x3

struct tusb1046_priv {
	struct i2c_client *client;
	struct typec_switch_dev *sw;
	struct typec_mux_dev *mux;

	/* Lock General register during accesses */
	struct mutex general_reg_lock;
};

static int tusb1046_mux_set(struct typec_mux_dev *mux,
			    struct typec_mux_state *state)
{
	struct tusb1046_priv *priv = typec_mux_get_drvdata(mux);
	struct i2c_client *client = priv->client;
	struct device *dev = &client->dev;
	int mode, val, ret = 0;

	if (state->mode >= TYPEC_STATE_MODAL &&
	    state->alt->svid != USB_TYPEC_DP_SID)
		return -EINVAL;

	dev_dbg(dev, "mux mode requested: %lu\n", state->mode);

	mutex_lock(&priv->general_reg_lock);

	val = i2c_smbus_read_byte_data(client, TUSB1046_REG_GENERAL);
	if (val < 0) {
		dev_err(dev, "failed to read ctlsel status, err %d\n", val);
		ret = val;
		goto out_unlock;
	}

	switch (state->mode) {
	case TYPEC_STATE_USB:
		mode = TUSB1046_CTLSEL_USB3;
		break;
	case TYPEC_DP_STATE_C:
	case TYPEC_DP_STATE_E:
		mode = TUSB1046_CTLSEL_4LANE_DP;
		break;
	case TYPEC_DP_STATE_D:
		mode = TUSB1046_CTLSEL_USB3_AND_2LANE_DP;
		break;
	case TYPEC_STATE_SAFE:
	default:
		mode = TUSB1046_CTLSEL_DISABLED;
		break;
	}

	val &= ~TUSB1046_GENERAL_CTLSEL;
	val |= mode;

	ret = i2c_smbus_write_byte_data(client, TUSB1046_REG_GENERAL, val);

out_unlock:
	mutex_unlock(&priv->general_reg_lock);
	return ret;
}

static int tusb1046_switch_set(struct typec_switch_dev *sw,
			       enum typec_orientation orientation)
{
	struct tusb1046_priv *priv = typec_switch_get_drvdata(sw);
	struct i2c_client *client = priv->client;
	struct device *dev = &client->dev;
	int val, ret = 0;

	dev_dbg(dev, "setting USB3.0 lane flip for orientation %d\n", orientation);

	mutex_lock(&priv->general_reg_lock);

	val = i2c_smbus_read_byte_data(client, TUSB1046_REG_GENERAL);
	if (val < 0) {
		dev_err(dev, "failed to read flipsel status, err %d\n", val);
		ret = val;
		goto out_unlock;
	}

	if (orientation == TYPEC_ORIENTATION_REVERSE)
		val |= TUSB1046_GENERAL_FLIPSEL;
	else
		val &= ~TUSB1046_GENERAL_FLIPSEL;

	ret = i2c_smbus_write_byte_data(client, TUSB1046_REG_GENERAL, val);

out_unlock:
	mutex_unlock(&priv->general_reg_lock);
	return ret;
}

static int tusb1046_i2c_probe(struct i2c_client *client)
{
	struct typec_switch_desc sw_desc = { };
	struct typec_mux_desc mux_desc = { };
	struct device *dev = &client->dev;
	struct tusb1046_priv *priv;
	int ret = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;

	mutex_init(&priv->general_reg_lock);

	sw_desc.drvdata = priv;
	sw_desc.fwnode = dev_fwnode(dev);
	sw_desc.set = tusb1046_switch_set;

	priv->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(priv->sw)) {
		ret = dev_err_probe(dev, PTR_ERR(priv->sw), "failed to register type-c switch\n");
		goto err_destroy_mutex;
	}

	mux_desc.drvdata = priv;
	mux_desc.fwnode = dev_fwnode(dev);
	mux_desc.set = tusb1046_mux_set;

	priv->mux = typec_mux_register(dev, &mux_desc);
	if (IS_ERR(priv->mux)) {
		ret = dev_err_probe(dev, PTR_ERR(priv->mux), "failed to register type-c mux\n");
		goto err_unregister_switch;
	}

	i2c_set_clientdata(client, priv);

	return 0;

err_unregister_switch:
	typec_switch_unregister(priv->sw);
err_destroy_mutex:
	mutex_destroy(&priv->general_reg_lock);
	return ret;
}

static void tusb1046_i2c_remove(struct i2c_client *client)
{
	struct tusb1046_priv *priv = i2c_get_clientdata(client);

	typec_switch_unregister(priv->sw);
	typec_mux_unregister(priv->mux);
	mutex_destroy(&priv->general_reg_lock);
}

static const struct of_device_id tusb1046_match_table[] = {
	{.compatible = "ti,tusb1046"},
	{},
};

static struct i2c_driver tusb1046_driver = {
	.driver = {
		.name = "tusb1046",
		.of_match_table = tusb1046_match_table,
	},
	.probe = tusb1046_i2c_probe,
	.remove = tusb1046_i2c_remove,
};

module_i2c_driver(tusb1046_driver);

MODULE_DESCRIPTION("TUSB1046 USB Type-C switch driver");
MODULE_AUTHOR("Romain Gantois <romain.gantois@bootlin.com>");
MODULE_LICENSE("GPL");
