// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/extcon/extcon-tusb320.c - TUSB320 extcon driver
 *
 * Copyright (C) 2020 National Instruments Corporation
 * Author: Michael Auchter <michael.auchter@ni.com>
 */

#include <linux/bitfield.h>
#include <linux/extcon-provider.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/usb/typec.h>

#define TUSB320_REG8				0x8
#define TUSB320_REG8_CURRENT_MODE_ADVERTISE	GENMASK(7, 6)
#define TUSB320_REG8_CURRENT_MODE_ADVERTISE_USB	0x0
#define TUSB320_REG8_CURRENT_MODE_ADVERTISE_15A	0x1
#define TUSB320_REG8_CURRENT_MODE_ADVERTISE_30A	0x2
#define TUSB320_REG8_CURRENT_MODE_DETECT	GENMASK(5, 4)
#define TUSB320_REG8_CURRENT_MODE_DETECT_DEF	0x0
#define TUSB320_REG8_CURRENT_MODE_DETECT_MED	0x1
#define TUSB320_REG8_CURRENT_MODE_DETECT_ACC	0x2
#define TUSB320_REG8_CURRENT_MODE_DETECT_HI	0x3
#define TUSB320_REG8_ACCESSORY_CONNECTED	GENMASK(3, 2)
#define TUSB320_REG8_ACCESSORY_CONNECTED_NONE	0x0
#define TUSB320_REG8_ACCESSORY_CONNECTED_AUDIO	0x4
#define TUSB320_REG8_ACCESSORY_CONNECTED_ACC	0x5
#define TUSB320_REG8_ACCESSORY_CONNECTED_DEBUG	0x6
#define TUSB320_REG8_ACTIVE_CABLE_DETECTION	BIT(0)

#define TUSB320_REG9				0x9
#define TUSB320_REG9_ATTACHED_STATE_SHIFT	6
#define TUSB320_REG9_ATTACHED_STATE_MASK	0x3
#define TUSB320_REG9_CABLE_DIRECTION		BIT(5)
#define TUSB320_REG9_INTERRUPT_STATUS		BIT(4)

#define TUSB320_REGA				0xa
#define TUSB320L_REGA_DISABLE_TERM		BIT(0)
#define TUSB320_REGA_I2C_SOFT_RESET		BIT(3)
#define TUSB320_REGA_MODE_SELECT_SHIFT		4
#define TUSB320_REGA_MODE_SELECT_MASK		0x3

#define TUSB320L_REGA0_REVISION			0xa0

enum tusb320_attached_state {
	TUSB320_ATTACHED_STATE_NONE,
	TUSB320_ATTACHED_STATE_DFP,
	TUSB320_ATTACHED_STATE_UFP,
	TUSB320_ATTACHED_STATE_ACC,
};

enum tusb320_mode {
	TUSB320_MODE_PORT,
	TUSB320_MODE_UFP,
	TUSB320_MODE_DFP,
	TUSB320_MODE_DRP,
};

struct tusb320_priv;

struct tusb320_ops {
	int (*set_mode)(struct tusb320_priv *priv, enum tusb320_mode mode);
	int (*get_revision)(struct tusb320_priv *priv, unsigned int *revision);
};

struct tusb320_priv {
	struct device *dev;
	struct regmap *regmap;
	struct extcon_dev *edev;
	struct tusb320_ops *ops;
	enum tusb320_attached_state state;
	struct typec_port *port;
	struct typec_capability	cap;
	enum typec_port_type port_type;
	enum typec_pwr_opmode pwr_opmode;
};

static const char * const tusb_attached_states[] = {
	[TUSB320_ATTACHED_STATE_NONE] = "not attached",
	[TUSB320_ATTACHED_STATE_DFP]  = "downstream facing port",
	[TUSB320_ATTACHED_STATE_UFP]  = "upstream facing port",
	[TUSB320_ATTACHED_STATE_ACC]  = "accessory",
};

static const unsigned int tusb320_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static int tusb320_check_signature(struct tusb320_priv *priv)
{
	static const char sig[] = { '\0', 'T', 'U', 'S', 'B', '3', '2', '0' };
	unsigned val;
	int i, ret;

	for (i = 0; i < sizeof(sig); i++) {
		ret = regmap_read(priv->regmap, sizeof(sig) - 1 - i, &val);
		if (ret < 0)
			return ret;
		if (val != sig[i]) {
			dev_err(priv->dev, "signature mismatch!\n");
			return -ENODEV;
		}
	}

	return 0;
}

static int tusb320_set_mode(struct tusb320_priv *priv, enum tusb320_mode mode)
{
	int ret;

	/* Mode cannot be changed while cable is attached */
	if (priv->state != TUSB320_ATTACHED_STATE_NONE)
		return -EBUSY;

	/* Write mode */
	ret = regmap_write_bits(priv->regmap, TUSB320_REGA,
		TUSB320_REGA_MODE_SELECT_MASK << TUSB320_REGA_MODE_SELECT_SHIFT,
		mode << TUSB320_REGA_MODE_SELECT_SHIFT);
	if (ret) {
		dev_err(priv->dev, "failed to write mode: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tusb320l_set_mode(struct tusb320_priv *priv, enum tusb320_mode mode)
{
	int ret;

	/* Disable CC state machine */
	ret = regmap_write_bits(priv->regmap, TUSB320_REGA,
		TUSB320L_REGA_DISABLE_TERM, 1);
	if (ret) {
		dev_err(priv->dev,
			"failed to disable CC state machine: %d\n", ret);
		return ret;
	}

	/* Write mode */
	ret = regmap_write_bits(priv->regmap, TUSB320_REGA,
		TUSB320_REGA_MODE_SELECT_MASK << TUSB320_REGA_MODE_SELECT_SHIFT,
		mode << TUSB320_REGA_MODE_SELECT_SHIFT);
	if (ret) {
		dev_err(priv->dev, "failed to write mode: %d\n", ret);
		goto err;
	}

	msleep(5);
err:
	/* Re-enable CC state machine */
	ret = regmap_write_bits(priv->regmap, TUSB320_REGA,
		TUSB320L_REGA_DISABLE_TERM, 0);
	if (ret)
		dev_err(priv->dev,
			"failed to re-enable CC state machine: %d\n", ret);

	return ret;
}

static int tusb320_reset(struct tusb320_priv *priv)
{
	int ret;

	/* Set mode to default (follow PORT pin) */
	ret = priv->ops->set_mode(priv, TUSB320_MODE_PORT);
	if (ret && ret != -EBUSY) {
		dev_err(priv->dev,
			"failed to set mode to PORT: %d\n", ret);
		return ret;
	}

	/* Perform soft reset */
	ret = regmap_write_bits(priv->regmap, TUSB320_REGA,
			TUSB320_REGA_I2C_SOFT_RESET, 1);
	if (ret) {
		dev_err(priv->dev,
			"failed to write soft reset bit: %d\n", ret);
		return ret;
	}

	/* Wait for chip to go through reset */
	msleep(95);

	return 0;
}

static int tusb320l_get_revision(struct tusb320_priv *priv, unsigned int *revision)
{
	return regmap_read(priv->regmap, TUSB320L_REGA0_REVISION, revision);
}

static struct tusb320_ops tusb320_ops = {
	.set_mode = tusb320_set_mode,
};

static struct tusb320_ops tusb320l_ops = {
	.set_mode = tusb320l_set_mode,
	.get_revision = tusb320l_get_revision,
};

static int tusb320_set_adv_pwr_mode(struct tusb320_priv *priv)
{
	u8 mode;

	if (priv->pwr_opmode == TYPEC_PWR_MODE_USB)
		mode = TUSB320_REG8_CURRENT_MODE_ADVERTISE_USB;
	else if (priv->pwr_opmode == TYPEC_PWR_MODE_1_5A)
		mode = TUSB320_REG8_CURRENT_MODE_ADVERTISE_15A;
	else if (priv->pwr_opmode == TYPEC_PWR_MODE_3_0A)
		mode = TUSB320_REG8_CURRENT_MODE_ADVERTISE_30A;
	else	/* No other mode is supported. */
		return -EINVAL;

	return regmap_write_bits(priv->regmap, TUSB320_REG8,
				 TUSB320_REG8_CURRENT_MODE_ADVERTISE,
				 FIELD_PREP(TUSB320_REG8_CURRENT_MODE_ADVERTISE,
					    mode));
}

static int tusb320_port_type_set(struct typec_port *port,
				 enum typec_port_type type)
{
	struct tusb320_priv *priv = typec_get_drvdata(port);

	if (type == TYPEC_PORT_SRC)
		return priv->ops->set_mode(priv, TUSB320_MODE_DFP);
	else if (type == TYPEC_PORT_SNK)
		return priv->ops->set_mode(priv, TUSB320_MODE_UFP);
	else if (type == TYPEC_PORT_DRP)
		return priv->ops->set_mode(priv, TUSB320_MODE_DRP);
	else
		return priv->ops->set_mode(priv, TUSB320_MODE_PORT);
}

static const struct typec_operations tusb320_typec_ops = {
	.port_type_set	= tusb320_port_type_set,
};

static void tusb320_extcon_irq_handler(struct tusb320_priv *priv, u8 reg)
{
	int state, polarity;

	state = (reg >> TUSB320_REG9_ATTACHED_STATE_SHIFT) &
		TUSB320_REG9_ATTACHED_STATE_MASK;
	polarity = !!(reg & TUSB320_REG9_CABLE_DIRECTION);

	dev_dbg(priv->dev, "attached state: %s, polarity: %d\n",
		tusb_attached_states[state], polarity);

	extcon_set_state(priv->edev, EXTCON_USB,
			 state == TUSB320_ATTACHED_STATE_UFP);
	extcon_set_state(priv->edev, EXTCON_USB_HOST,
			 state == TUSB320_ATTACHED_STATE_DFP);
	extcon_set_property(priv->edev, EXTCON_USB,
			    EXTCON_PROP_USB_TYPEC_POLARITY,
			    (union extcon_property_value)polarity);
	extcon_set_property(priv->edev, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_TYPEC_POLARITY,
			    (union extcon_property_value)polarity);
	extcon_sync(priv->edev, EXTCON_USB);
	extcon_sync(priv->edev, EXTCON_USB_HOST);

	priv->state = state;
}

static void tusb320_typec_irq_handler(struct tusb320_priv *priv, u8 reg9)
{
	struct typec_port *port = priv->port;
	struct device *dev = priv->dev;
	u8 mode, role, state;
	int ret, reg8;
	bool ori;

	ori = reg9 & TUSB320_REG9_CABLE_DIRECTION;
	typec_set_orientation(port, ori ? TYPEC_ORIENTATION_REVERSE :
					  TYPEC_ORIENTATION_NORMAL);

	state = (reg9 >> TUSB320_REG9_ATTACHED_STATE_SHIFT) &
		TUSB320_REG9_ATTACHED_STATE_MASK;
	if (state == TUSB320_ATTACHED_STATE_DFP)
		role = TYPEC_SOURCE;
	else
		role = TYPEC_SINK;

	typec_set_vconn_role(port, role);
	typec_set_pwr_role(port, role);
	typec_set_data_role(port, role == TYPEC_SOURCE ?
				  TYPEC_HOST : TYPEC_DEVICE);

	ret = regmap_read(priv->regmap, TUSB320_REG8, &reg8);
	if (ret) {
		dev_err(dev, "error during reg8 i2c read, ret=%d!\n", ret);
		return;
	}

	mode = FIELD_GET(TUSB320_REG8_CURRENT_MODE_DETECT, reg8);
	if (mode == TUSB320_REG8_CURRENT_MODE_DETECT_DEF)
		typec_set_pwr_opmode(port, TYPEC_PWR_MODE_USB);
	else if (mode == TUSB320_REG8_CURRENT_MODE_DETECT_MED)
		typec_set_pwr_opmode(port, TYPEC_PWR_MODE_1_5A);
	else if (mode == TUSB320_REG8_CURRENT_MODE_DETECT_HI)
		typec_set_pwr_opmode(port, TYPEC_PWR_MODE_3_0A);
	else	/* Charge through accessory */
		typec_set_pwr_opmode(port, TYPEC_PWR_MODE_USB);
}

static irqreturn_t tusb320_state_update_handler(struct tusb320_priv *priv,
						bool force_update)
{
	unsigned int reg;

	if (regmap_read(priv->regmap, TUSB320_REG9, &reg)) {
		dev_err(priv->dev, "error during i2c read!\n");
		return IRQ_NONE;
	}

	if (!force_update && !(reg & TUSB320_REG9_INTERRUPT_STATUS))
		return IRQ_NONE;

	tusb320_extcon_irq_handler(priv, reg);

	/*
	 * Type-C support is optional. Only call the Type-C handler if a
	 * port had been registered previously.
	 */
	if (priv->port)
		tusb320_typec_irq_handler(priv, reg);

	regmap_write(priv->regmap, TUSB320_REG9, reg);

	return IRQ_HANDLED;
}

static irqreturn_t tusb320_irq_handler(int irq, void *dev_id)
{
	struct tusb320_priv *priv = dev_id;

	return tusb320_state_update_handler(priv, false);
}

static const struct regmap_config tusb320_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int tusb320_extcon_probe(struct tusb320_priv *priv)
{
	int ret;

	priv->edev = devm_extcon_dev_allocate(priv->dev, tusb320_extcon_cable);
	if (IS_ERR(priv->edev)) {
		dev_err(priv->dev, "failed to allocate extcon device\n");
		return PTR_ERR(priv->edev);
	}

	ret = devm_extcon_dev_register(priv->dev, priv->edev);
	if (ret < 0) {
		dev_err(priv->dev, "failed to register extcon device\n");
		return ret;
	}

	extcon_set_property_capability(priv->edev, EXTCON_USB,
				       EXTCON_PROP_USB_TYPEC_POLARITY);
	extcon_set_property_capability(priv->edev, EXTCON_USB_HOST,
				       EXTCON_PROP_USB_TYPEC_POLARITY);

	return 0;
}

static int tusb320_typec_probe(struct i2c_client *client,
			       struct tusb320_priv *priv)
{
	struct fwnode_handle *connector;
	const char *cap_str;
	int ret;

	/* The Type-C connector is optional, for backward compatibility. */
	connector = device_get_named_child_node(&client->dev, "connector");
	if (!connector)
		return 0;

	/* Type-C connector found. */
	ret = typec_get_fw_cap(&priv->cap, connector);
	if (ret)
		return ret;

	priv->port_type = priv->cap.type;

	/* This goes into register 0x8 field CURRENT_MODE_ADVERTISE */
	ret = fwnode_property_read_string(connector, "typec-power-opmode", &cap_str);
	if (ret)
		return ret;

	ret = typec_find_pwr_opmode(cap_str);
	if (ret < 0)
		return ret;
	if (ret == TYPEC_PWR_MODE_PD)
		return -EINVAL;

	priv->pwr_opmode = ret;

	/* Initialize the hardware with the devicetree settings. */
	ret = tusb320_set_adv_pwr_mode(priv);
	if (ret)
		return ret;

	priv->cap.revision		= USB_TYPEC_REV_1_1;
	priv->cap.accessory[0]		= TYPEC_ACCESSORY_AUDIO;
	priv->cap.accessory[1]		= TYPEC_ACCESSORY_DEBUG;
	priv->cap.orientation_aware	= true;
	priv->cap.driver_data		= priv;
	priv->cap.ops			= &tusb320_typec_ops;
	priv->cap.fwnode		= connector;

	priv->port = typec_register_port(&client->dev, &priv->cap);
	if (IS_ERR(priv->port))
		return PTR_ERR(priv->port);

	return 0;
}

static int tusb320_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct tusb320_priv *priv;
	const void *match_data;
	unsigned int revision;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &client->dev;

	priv->regmap = devm_regmap_init_i2c(client, &tusb320_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	ret = tusb320_check_signature(priv);
	if (ret)
		return ret;

	match_data = device_get_match_data(&client->dev);
	if (!match_data)
		return -EINVAL;

	priv->ops = (struct tusb320_ops*)match_data;

	if (priv->ops->get_revision) {
		ret = priv->ops->get_revision(priv, &revision);
		if (ret)
			dev_warn(priv->dev,
				"failed to read revision register: %d\n", ret);
		else
			dev_info(priv->dev, "chip revision %d\n", revision);
	}

	ret = tusb320_extcon_probe(priv);
	if (ret)
		return ret;

	ret = tusb320_typec_probe(client, priv);
	if (ret)
		return ret;

	/* update initial state */
	tusb320_state_update_handler(priv, true);

	/* Reset chip to its default state */
	ret = tusb320_reset(priv);
	if (ret)
		dev_warn(priv->dev, "failed to reset chip: %d\n", ret);
	else
		/*
		 * State and polarity might change after a reset, so update
		 * them again and make sure the interrupt status bit is cleared.
		 */
		tusb320_state_update_handler(priv, true);

	ret = devm_request_threaded_irq(priv->dev, client->irq, NULL,
					tusb320_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					client->name, priv);

	return ret;
}

static const struct of_device_id tusb320_extcon_dt_match[] = {
	{ .compatible = "ti,tusb320", .data = &tusb320_ops, },
	{ .compatible = "ti,tusb320l", .data = &tusb320l_ops, },
	{ }
};
MODULE_DEVICE_TABLE(of, tusb320_extcon_dt_match);

static struct i2c_driver tusb320_extcon_driver = {
	.probe		= tusb320_probe,
	.driver		= {
		.name	= "extcon-tusb320",
		.of_match_table = tusb320_extcon_dt_match,
	},
};

static int __init tusb320_init(void)
{
	return i2c_add_driver(&tusb320_extcon_driver);
}
subsys_initcall(tusb320_init);

static void __exit tusb320_exit(void)
{
	i2c_del_driver(&tusb320_extcon_driver);
}
module_exit(tusb320_exit);

MODULE_AUTHOR("Michael Auchter <michael.auchter@ni.com>");
MODULE_DESCRIPTION("TI TUSB320 extcon driver");
MODULE_LICENSE("GPL v2");
