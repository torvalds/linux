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
#include <linux/workqueue.h>

#define HD3SS3220_REG_CN_STAT		0x08
#define HD3SS3220_REG_CN_STAT_CTRL	0x09
#define HD3SS3220_REG_GEN_CTRL		0x0A
#define HD3SS3220_REG_DEV_REV		0xA0

/* Register HD3SS3220_REG_CN_STAT */
#define HD3SS3220_REG_CN_STAT_CURRENT_MODE_MASK		(BIT(7) | BIT(6))
#define HD3SS3220_REG_CN_STAT_CURRENT_MODE_DEFAULT	0x00
#define HD3SS3220_REG_CN_STAT_CURRENT_MODE_MID		BIT(6)
#define HD3SS3220_REG_CN_STAT_CURRENT_MODE_HIGH		BIT(7)

/* Register HD3SS3220_REG_CN_STAT_CTRL*/
#define HD3SS3220_REG_CN_STAT_CTRL_ATTACHED_STATE_MASK	(BIT(7) | BIT(6))
#define HD3SS3220_REG_CN_STAT_CTRL_AS_DFP		BIT(6)
#define HD3SS3220_REG_CN_STAT_CTRL_AS_UFP		BIT(7)
#define HD3SS3220_REG_CN_STAT_CTRL_TO_ACCESSORY		(BIT(7) | BIT(6))
#define HD3SS3220_REG_CN_STAT_CTRL_INT_STATUS		BIT(4)

/* Register HD3SS3220_REG_GEN_CTRL*/
#define HD3SS3220_REG_GEN_CTRL_DISABLE_TERM		BIT(0)
#define HD3SS3220_REG_GEN_CTRL_SRC_PREF_MASK		(BIT(2) | BIT(1))
#define HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_DEFAULT	0x00
#define HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_TRY_SNK	BIT(1)
#define HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_TRY_SRC	(BIT(2) | BIT(1))
#define HD3SS3220_REG_GEN_CTRL_MODE_SELECT_MASK		(BIT(5) | BIT(4))
#define HD3SS3220_REG_GEN_CTRL_MODE_SELECT_DEFAULT	0x00
#define HD3SS3220_REG_GEN_CTRL_MODE_SELECT_DFP		BIT(5)
#define HD3SS3220_REG_GEN_CTRL_MODE_SELECT_UFP		BIT(4)
#define HD3SS3220_REG_GEN_CTRL_MODE_SELECT_DRP		(BIT(5) | BIT(4))

struct hd3ss3220 {
	struct device *dev;
	struct regmap *regmap;
	struct usb_role_switch	*role_sw;
	struct typec_port *port;
	struct delayed_work output_poll_work;
	enum usb_role role_state;
	bool poll;
};

static int hd3ss3220_set_power_opmode(struct hd3ss3220 *hd3ss3220, int power_opmode)
{
	int current_mode;

	switch (power_opmode) {
	case TYPEC_PWR_MODE_USB:
		current_mode = HD3SS3220_REG_CN_STAT_CURRENT_MODE_DEFAULT;
		break;
	case TYPEC_PWR_MODE_1_5A:
		current_mode = HD3SS3220_REG_CN_STAT_CURRENT_MODE_MID;
		break;
	case TYPEC_PWR_MODE_3_0A:
		current_mode = HD3SS3220_REG_CN_STAT_CURRENT_MODE_HIGH;
		break;
	case TYPEC_PWR_MODE_PD: /* Power delivery not supported */
	default:
		dev_err(hd3ss3220->dev, "bad power operation mode: %d\n", power_opmode);
		return -EINVAL;
	}

	return regmap_update_bits(hd3ss3220->regmap, HD3SS3220_REG_CN_STAT,
				  HD3SS3220_REG_CN_STAT_CURRENT_MODE_MASK,
				  current_mode);
}

static int hd3ss3220_set_port_type(struct hd3ss3220 *hd3ss3220, int type)
{
	int mode_select, err;

	switch (type) {
	case TYPEC_PORT_SRC:
		mode_select = HD3SS3220_REG_GEN_CTRL_MODE_SELECT_DFP;
		break;
	case TYPEC_PORT_SNK:
		mode_select = HD3SS3220_REG_GEN_CTRL_MODE_SELECT_UFP;
		break;
	case TYPEC_PORT_DRP:
		mode_select = HD3SS3220_REG_GEN_CTRL_MODE_SELECT_DRP;
		break;
	default:
		dev_err(hd3ss3220->dev, "bad port type: %d\n", type);
		return -EINVAL;
	}

	/* Disable termination before changing MODE_SELECT as required by datasheet */
	err = regmap_update_bits(hd3ss3220->regmap, HD3SS3220_REG_GEN_CTRL,
				 HD3SS3220_REG_GEN_CTRL_DISABLE_TERM,
				 HD3SS3220_REG_GEN_CTRL_DISABLE_TERM);
	if (err < 0) {
		dev_err(hd3ss3220->dev, "Failed to disable port for mode change: %d\n", err);
		return err;
	}

	err = regmap_update_bits(hd3ss3220->regmap, HD3SS3220_REG_GEN_CTRL,
				 HD3SS3220_REG_GEN_CTRL_MODE_SELECT_MASK,
				 mode_select);
	if (err < 0) {
		dev_err(hd3ss3220->dev, "Failed to change mode: %d\n", err);
		regmap_update_bits(hd3ss3220->regmap, HD3SS3220_REG_GEN_CTRL,
				   HD3SS3220_REG_GEN_CTRL_DISABLE_TERM, 0);
		return err;
	}

	err = regmap_update_bits(hd3ss3220->regmap, HD3SS3220_REG_GEN_CTRL,
				 HD3SS3220_REG_GEN_CTRL_DISABLE_TERM, 0);
	if (err < 0)
		dev_err(hd3ss3220->dev, "Failed to re-enable port after mode change: %d\n", err);

	return err;
}

static int hd3ss3220_set_source_pref(struct hd3ss3220 *hd3ss3220, int prefer_role)
{
	int src_pref;

	switch (prefer_role) {
	case TYPEC_NO_PREFERRED_ROLE:
		src_pref = HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_DEFAULT;
		break;
	case TYPEC_SINK:
		src_pref = HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_TRY_SNK;
		break;
	case TYPEC_SOURCE:
		src_pref = HD3SS3220_REG_GEN_CTRL_SRC_PREF_DRP_TRY_SRC;
		break;
	default:
		dev_err(hd3ss3220->dev, "bad role preference: %d\n", prefer_role);
		return -EINVAL;
	}

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

static int hd3ss3220_try_role(struct typec_port *port, int role)
{
	struct hd3ss3220 *hd3ss3220 = typec_get_drvdata(port);

	return hd3ss3220_set_source_pref(hd3ss3220, role);
}

static int hd3ss3220_port_type_set(struct typec_port *port, enum typec_port_type type)
{
	struct hd3ss3220 *hd3ss3220 = typec_get_drvdata(port);

	return hd3ss3220_set_port_type(hd3ss3220, type);
}

static const struct typec_operations hd3ss3220_ops = {
	.try_role = hd3ss3220_try_role,
	.port_type_set = hd3ss3220_port_type_set,
};

static void hd3ss3220_set_role(struct hd3ss3220 *hd3ss3220)
{
	enum usb_role role_state = hd3ss3220_get_attached_state(hd3ss3220);

	usb_role_switch_set_role(hd3ss3220->role_sw, role_state);

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

	hd3ss3220->role_state = role_state;
}

static void output_poll_execute(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct hd3ss3220 *hd3ss3220 = container_of(delayed_work,
						   struct hd3ss3220,
						   output_poll_work);
	enum usb_role role_state = hd3ss3220_get_attached_state(hd3ss3220);

	if (hd3ss3220->role_state != role_state)
		hd3ss3220_set_role(hd3ss3220);

	schedule_delayed_work(&hd3ss3220->output_poll_work, HZ);
}

static irqreturn_t hd3ss3220_irq(struct hd3ss3220 *hd3ss3220)
{
	int err;

	hd3ss3220_set_role(hd3ss3220);
	err = regmap_write_bits(hd3ss3220->regmap, HD3SS3220_REG_CN_STAT_CTRL,
				HD3SS3220_REG_CN_STAT_CTRL_INT_STATUS,
				HD3SS3220_REG_CN_STAT_CTRL_INT_STATUS);
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

static int hd3ss3220_configure_power_opmode(struct hd3ss3220 *hd3ss3220,
					    struct fwnode_handle *connector)
{
	/*
	 * Supported power operation mode can be configured through device tree
	 */
	const char *cap_str;
	int ret, power_opmode;

	ret = fwnode_property_read_string(connector, "typec-power-opmode", &cap_str);
	if (ret)
		return 0;

	power_opmode = typec_find_pwr_opmode(cap_str);
	return hd3ss3220_set_power_opmode(hd3ss3220, power_opmode);
}

static int hd3ss3220_configure_port_type(struct hd3ss3220 *hd3ss3220,
					 struct fwnode_handle *connector,
					 struct typec_capability *cap)
{
	/*
	 * Port type can be configured through device tree
	 */
	const char *cap_str;
	int ret;

	ret = fwnode_property_read_string(connector, "power-role", &cap_str);
	if (ret)
		return 0;

	ret = typec_find_port_power_role(cap_str);
	if (ret < 0)
		return ret;

	cap->type = ret;
	return hd3ss3220_set_port_type(hd3ss3220, cap->type);
}

static int hd3ss3220_configure_source_pref(struct hd3ss3220 *hd3ss3220,
					   struct fwnode_handle *connector,
					   struct typec_capability *cap)
{
	/*
	 * Preferred role can be configured through device tree
	 */
	const char *cap_str;
	int ret;

	ret = fwnode_property_read_string(connector, "try-power-role", &cap_str);
	if (ret)
		return 0;

	ret = typec_find_power_role(cap_str);
	if (ret < 0)
		return ret;

	cap->prefer_role = ret;
	return hd3ss3220_set_source_pref(hd3ss3220, cap->prefer_role);
}

static const struct regmap_config config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x0A,
};

static int hd3ss3220_probe(struct i2c_client *client)
{
	struct typec_capability typec_cap = { };
	struct hd3ss3220 *hd3ss3220;
	struct fwnode_handle *connector, *ep;
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

	/* For backward compatibility check the connector child node first */
	connector = device_get_named_child_node(hd3ss3220->dev, "connector");
	if (connector) {
		hd3ss3220->role_sw = fwnode_usb_role_switch_get(connector);
	} else {
		ep = fwnode_graph_get_next_endpoint(dev_fwnode(hd3ss3220->dev), NULL);
		if (!ep)
			return -ENODEV;
		connector = fwnode_graph_get_remote_port_parent(ep);
		fwnode_handle_put(ep);
		if (!connector)
			return -ENODEV;
		hd3ss3220->role_sw = usb_role_switch_get(hd3ss3220->dev);
	}

	if (IS_ERR(hd3ss3220->role_sw)) {
		ret = PTR_ERR(hd3ss3220->role_sw);
		goto err_put_fwnode;
	}

	typec_cap.prefer_role = TYPEC_NO_PREFERRED_ROLE;
	typec_cap.driver_data = hd3ss3220;
	typec_cap.type = TYPEC_PORT_DRP;
	typec_cap.data = TYPEC_PORT_DRD;
	typec_cap.ops = &hd3ss3220_ops;
	typec_cap.fwnode = connector;

	ret = hd3ss3220_configure_source_pref(hd3ss3220, connector, &typec_cap);
	if (ret < 0)
		goto err_put_role;

	ret = hd3ss3220_configure_port_type(hd3ss3220, connector, &typec_cap);
	if (ret < 0)
		goto err_put_role;

	hd3ss3220->port = typec_register_port(&client->dev, &typec_cap);
	if (IS_ERR(hd3ss3220->port)) {
		ret = PTR_ERR(hd3ss3220->port);
		goto err_put_role;
	}

	ret = hd3ss3220_configure_power_opmode(hd3ss3220, connector);
	if (ret < 0)
		goto err_unreg_port;

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
	} else {
		INIT_DELAYED_WORK(&hd3ss3220->output_poll_work, output_poll_execute);
		hd3ss3220->poll = true;
	}

	ret = i2c_smbus_read_byte_data(client, HD3SS3220_REG_DEV_REV);
	if (ret < 0)
		goto err_unreg_port;

	fwnode_handle_put(connector);

	if (hd3ss3220->poll)
		schedule_delayed_work(&hd3ss3220->output_poll_work, HZ);

	dev_info(&client->dev, "probed revision=0x%x\n", ret);

	return 0;
err_unreg_port:
	typec_unregister_port(hd3ss3220->port);
err_put_role:
	usb_role_switch_put(hd3ss3220->role_sw);
err_put_fwnode:
	fwnode_handle_put(connector);

	return ret;
}

static void hd3ss3220_remove(struct i2c_client *client)
{
	struct hd3ss3220 *hd3ss3220 = i2c_get_clientdata(client);

	if (hd3ss3220->poll)
		cancel_delayed_work_sync(&hd3ss3220->output_poll_work);

	typec_unregister_port(hd3ss3220->port);
	usb_role_switch_put(hd3ss3220->role_sw);
}

static const struct of_device_id dev_ids[] = {
	{ .compatible = "ti,hd3ss3220"},
	{}
};
MODULE_DEVICE_TABLE(of, dev_ids);

static struct i2c_driver hd3ss3220_driver = {
	.driver = {
		.name = "hd3ss3220",
		.of_match_table = dev_ids,
	},
	.probe = hd3ss3220_probe,
	.remove = hd3ss3220_remove,
};

module_i2c_driver(hd3ss3220_driver);

MODULE_AUTHOR("Biju Das <biju.das@bp.renesas.com>");
MODULE_DESCRIPTION("TI HD3SS3220 DRP Port Controller Driver");
MODULE_LICENSE("GPL");
