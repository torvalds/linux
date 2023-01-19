// SPDX-License-Identifier: GPL-2.0
/*
 * NXP ISP1301 USB transceiver driver
 *
 * Copyright (C) 2012 Roland Stigge
 *
 * Author: Roland Stigge <stigge@antcom.de>
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/usb/phy.h>
#include <linux/usb/isp1301.h>

#define DRV_NAME		"isp1301"

struct isp1301 {
	struct usb_phy		phy;
	struct mutex		mutex;

	struct i2c_client	*client;
};

#define phy_to_isp(p)		(container_of((p), struct isp1301, phy))

static const struct i2c_device_id isp1301_id[] = {
	{ "isp1301", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isp1301_id);

static const struct of_device_id isp1301_of_match[] = {
	{.compatible = "nxp,isp1301" },
	{ },
};
MODULE_DEVICE_TABLE(of, isp1301_of_match);

static struct i2c_client *isp1301_i2c_client;

static int __isp1301_write(struct isp1301 *isp, u8 reg, u8 value, u8 clear)
{
	return i2c_smbus_write_byte_data(isp->client, reg | clear, value);
}

static int isp1301_write(struct isp1301 *isp, u8 reg, u8 value)
{
	return __isp1301_write(isp, reg, value, 0);
}

static int isp1301_clear(struct isp1301 *isp, u8 reg, u8 value)
{
	return __isp1301_write(isp, reg, value, ISP1301_I2C_REG_CLEAR_ADDR);
}

static int isp1301_phy_init(struct usb_phy *phy)
{
	struct isp1301 *isp = phy_to_isp(phy);

	/* Disable transparent UART mode first */
	isp1301_clear(isp, ISP1301_I2C_MODE_CONTROL_1, MC1_UART_EN);
	isp1301_clear(isp, ISP1301_I2C_MODE_CONTROL_1, ~MC1_SPEED_REG);
	isp1301_write(isp, ISP1301_I2C_MODE_CONTROL_1, MC1_SPEED_REG);
	isp1301_clear(isp, ISP1301_I2C_MODE_CONTROL_2, ~0);
	isp1301_write(isp, ISP1301_I2C_MODE_CONTROL_2, (MC2_BI_DI | MC2_PSW_EN
				| MC2_SPD_SUSP_CTRL));

	isp1301_clear(isp, ISP1301_I2C_OTG_CONTROL_1, ~0);
	isp1301_write(isp, ISP1301_I2C_MODE_CONTROL_1, MC1_DAT_SE0);
	isp1301_write(isp, ISP1301_I2C_OTG_CONTROL_1, (OTG1_DM_PULLDOWN
				| OTG1_DP_PULLDOWN));
	isp1301_clear(isp, ISP1301_I2C_OTG_CONTROL_1, (OTG1_DM_PULLUP
				| OTG1_DP_PULLUP));

	/* mask all interrupts */
	isp1301_clear(isp, ISP1301_I2C_INTERRUPT_LATCH, ~0);
	isp1301_clear(isp, ISP1301_I2C_INTERRUPT_FALLING, ~0);
	isp1301_clear(isp, ISP1301_I2C_INTERRUPT_RISING, ~0);

	return 0;
}

static int isp1301_phy_set_vbus(struct usb_phy *phy, int on)
{
	struct isp1301 *isp = phy_to_isp(phy);

	if (on)
		isp1301_write(isp, ISP1301_I2C_OTG_CONTROL_1, OTG1_VBUS_DRV);
	else
		isp1301_clear(isp, ISP1301_I2C_OTG_CONTROL_1, OTG1_VBUS_DRV);

	return 0;
}

static int isp1301_probe(struct i2c_client *client)
{
	struct isp1301 *isp;
	struct usb_phy *phy;

	isp = devm_kzalloc(&client->dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->client = client;
	mutex_init(&isp->mutex);

	phy = &isp->phy;
	phy->dev = &client->dev;
	phy->label = DRV_NAME;
	phy->init = isp1301_phy_init;
	phy->set_vbus = isp1301_phy_set_vbus;
	phy->type = USB_PHY_TYPE_USB2;

	i2c_set_clientdata(client, isp);
	usb_add_phy_dev(phy);

	isp1301_i2c_client = client;

	return 0;
}

static void isp1301_remove(struct i2c_client *client)
{
	struct isp1301 *isp = i2c_get_clientdata(client);

	usb_remove_phy(&isp->phy);
	isp1301_i2c_client = NULL;
}

static struct i2c_driver isp1301_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = isp1301_of_match,
	},
	.probe_new = isp1301_probe,
	.remove = isp1301_remove,
	.id_table = isp1301_id,
};

module_i2c_driver(isp1301_driver);

struct i2c_client *isp1301_get_client(struct device_node *node)
{
	struct i2c_client *client;

	/* reference of ISP1301 I2C node via DT */
	client = of_find_i2c_device_by_node(node);
	if (client)
		return client;

	/* non-DT: only one ISP1301 chip supported */
	return isp1301_i2c_client;
}
EXPORT_SYMBOL_GPL(isp1301_get_client);

MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("NXP ISP1301 USB transceiver driver");
MODULE_LICENSE("GPL");
