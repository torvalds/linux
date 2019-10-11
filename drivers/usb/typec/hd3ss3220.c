// SPDX-License-Identifier: GPL-2.0+
/*
 * TI HD3SS3220 Type-C DRP Port Controller Driver
 *
 * Copyright (C) 2019 Renesas Electronics Corp.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/usb/role.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/usb/typec.h>
#include <linux/delay.h>

#define HD3SS3220_REG_CN_STAT_CTRL	0x09
#define HD3SS3220_REG_GEN_CTRL		0x0A
#define HD3SS3220_REG_DEV_REV		0xA0

/* Register HD3SS3220_REG_CN_STAT_CTRL*/
#define HD3SS3220_REG_CN_STAT_CTRL_ATTACHED_STATE_MASK	(BIT(7) | BIT(6))
#define HD3SS3220_REG_CN_STAT_CTRL_AS_DFP		BIT(6)
#define HD3SS3220_REG_CN_STAT_CTRL_AS_UFP		BIT(7)
#define HD3SS3220_REG_CN_STAT_CTRL_TO_ACCESSORY		(BIT(7) | BIT(6))
#define HD3SS3220_REG_CN_STAT_CTRL_INT_STATUS		BIT(4)

/* Register HD3SS3220_REG_GEN_CTRL*/
#define HD3SS3220_REG_GEN_CTRL_SRC_PREF_MASK		(BIT(2) | BIT(1))
#define HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_DEFAULT	0x00
#define HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_TRY_SNK	BIT(1)
#define HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_TRY_SRC	(BIT(2) | BIT(1))

struct hd3ss3220 {
	struct device *dev;
	struct regmap *regmap;
	struct usb_role_switch	*role_sw;
	struct typec_port *port;
	struct typec_capability typec_cap;
};

static int hd3ss3220_set_source_pref(struct hd3ss3220 *hd3ss3220, int src_pref)
{
	return regmap_update_bits(hd3ss3220->regmap, HD3SS3220_REG_GEN_CTRL,
				  HD3SS3220_REG_GEN_CTRL_SRC_PREF_MASK,
				  src_pref);
}

static enum usb_role hd3ss3220_get_attached_state(struct hd3ss3220 *hd3ss3220)
{
	unsigned int reg_val;
	enum usb_role attached_state;
	int ret;

	ret = regmap_read(hd3ss3220->regmap, HD3SS3220_REG_CN_STAT_CTRL,
			  &reg_val);
	if (ret < 0)
		return ret;

	switch (reg_val & HD3SS3220_REG_CN_STAT_CTRL_ATTACHED_STATE_MASK) {
	case HD3SS3220_REG_CN_STAT_CTRL_AS_DFP:
		attached_state = USB_ROLE_HOST;
		break;
	case HD3SS3220_REG_CN_STAT_CTRL_AS_UFP:
		attached_state = USB_ROLE_DEVICE;
		break;
	default:
		attached_state = USB_ROLE_NONE;
		break;
	}

	return attached_state;
}

static int hd3ss3220_dr_set(const struct typec_capability *cap,
			    enum typec_data_role role)
{
	struct hd3ss3220 *hd3ss3220 = container_of(cap, struct hd3ss3220,
						   typec_cap);
	enum usb_role role_val;
	int pref, ret = 0;

	if (role == TYPEC_HOST) {
		role_val = USB_ROLE_HOST;
		pref = HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_TRY_SRC;
	} else {
		role_val = USB_ROLE_DEVICE;
		pref = HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_TRY_SNK;
	}

	ret = hd3ss3220_set_source_pref(hd3ss3220, pref);
	usleep_range(10, 100);

	usb_role_switch_set_role(hd3ss3220->role_sw, role_val);
	typec_set_data_role(hd3ss3220->port, role);

	return ret;
}

static void hd3ss3220_set_role(struct hd3ss3220 *hd3ss3220)
{
	enum usb_role role_state = hd3ss3220_get_attached_state(hd3ss3220);

	usb_role_switch_set_role(hd3ss3220->role_sw, role_state);
	if (role_state == USB_ROLE_NONE)
		hd3ss3220_set_source_pref(hd3ss3220,
				HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_DEFAULT);

	switch (role_state) {
	case USB_ROLE_HOST:
		typec_set_data_role(hd3ss3220->port, TYPEC_HOST);
		break;
	case USB_ROLE_DEVICE:
		typec_set_data_role(hd3ss3220->port, TYPEC_DEVICE);
		break;
	default:
		break;
	}
}

static irqreturn_t hd3ss3220_irq(struct hd3ss3220 *hd3ss3220)
{
	int err;

	hd3ss3220_set_role(hd3ss3220);
	err = regmap_update_bits_base(hd3ss3220->regmap,
				      HD3SS3220_REG_CN_STAT_CTRL,
				      HD3SS3220_REG_CN_STAT_CTRL_INT_STATUS,
				      HD3SS3220_REG_CN_STAT_CTRL_INT_STATUS,
				      NULL, false, true);
	if (err < 0)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static irqreturn_t hd3ss3220_irq_handler(int irq, void *data)
{
	struct i2c_client *client = to_i2c_client(data);
	struct hd3ss3220 *hd3ss3220 = i2c_get_clientdata(client);

	return hd3ss3220_irq(hd3ss3220);
}

static const struct regmap_config config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x0A,
};

static int hd3ss3220_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct hd3ss3220 *hd3ss3220;
	struct fwnode_handle *connector;
	int ret;
	unsigned int data;

	hd3ss3220 = devm_kzalloc(&client->dev, sizeof(struct hd3ss3220),
				 GFP_KERNEL);
	if (!hd3ss3220)
		return -ENOMEM;

	i2c_set_clientdata(client, hd3ss3220);

	hd3ss3220->dev = &client->dev;
	hd3ss3220->regmap = devm_regmap_init_i2c(client, &config);
	if (IS_ERR(hd3ss3220->regmap))
		return PTR_ERR(hd3ss3220->regmap);

	hd3ss3220_set_source_pref(hd3ss3220,
				  HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_DEFAULT);
	connector = device_get_named_child_node(hd3ss3220->dev, "connector");
	if (!connector)
		return -ENODEV;

	hd3ss3220->role_sw = fwnode_usb_role_switch_get(connector);
	fwnode_handle_put(connector);
	if (IS_ERR(hd3ss3220->role_sw))
		return PTR_ERR(hd3ss3220->role_sw);

	hd3ss3220->typec_cap.prefer_role = TYPEC_NO_PREFERRED_ROLE;
	hd3ss3220->typec_cap.dr_set = hd3ss3220_dr_set;
	hd3ss3220->typec_cap.type = TYPEC_PORT_DRP;
	hd3ss3220->typec_cap.data = TYPEC_PORT_DRD;

	hd3ss3220->port = typec_register_port(&client->dev,
					      &hd3ss3220->typec_cap);
	if (IS_ERR(hd3ss3220->port)) {
		ret = PTR_ERR(hd3ss3220->port);
		goto err_put_role;
	}

	hd3ss3220_set_role(hd3ss3220);
	ret = regmap_read(hd3ss3220->regmap, HD3SS3220_REG_CN_STAT_CTRL, &data);
	if (ret < 0)
		goto err_unreg_port;

	if (data & HD3SS3220_REG_CN_STAT_CTRL_INT_STATUS) {
		ret = regmap_write(hd3ss3220->regmap,
				HD3SS3220_REG_CN_STAT_CTRL,
				data | HD3SS3220_REG_CN_STAT_CTRL_INT_STATUS);
		if (ret < 0)
			goto err_unreg_port;
	}

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					hd3ss3220_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"hd3ss3220", &client->dev);
		if (ret)
			goto err_unreg_port;
	}

	ret = i2c_smbus_read_byte_data(client, HD3SS3220_REG_DEV_REV);
	if (ret < 0)
		goto err_unreg_port;

	dev_info(&client->dev, "probed revision=0x%x\n", ret);

	return 0;
err_unreg_port:
	typec_unregister_port(hd3ss3220->port);
err_put_role:
	usb_role_switch_put(hd3ss3220->role_sw);

	return ret;
}

static int hd3ss3220_remove(struct i2c_client *client)
{
	struct hd3ss3220 *hd3ss3220 = i2c_get_clientdata(client);

	typec_unregister_port(hd3ss3220->port);
	usb_role_switch_put(hd3ss3220->role_sw);

	return 0;
}

static const struct of_device_id dev_ids[] = {
	{ .compatible = "ti,hd3ss3220"},
	{}
};
MODULE_DEVICE_TABLE(of, dev_ids);

static struct i2c_driver hd3ss3220_driver = {
	.driver = {
		.name = "hd3ss3220",
		.of_match_table = of_match_ptr(dev_ids),
	},
	.probe = hd3ss3220_probe,
	.remove =  hd3ss3220_remove,
};

module_i2c_driver(hd3ss3220_driver);

MODULE_AUTHOR("Biju Das <biju.das@bp.renesas.com>");
MODULE_DESCRIPTION("TI HD3SS3220 DRP Port Controller Driver");
MODULE_LICENSE("GPL");
