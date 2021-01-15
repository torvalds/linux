// SPDX-License-Identifier: GPL-2.0
/**
 * drivers/extcon/extcon-tusb320.c - TUSB320 extcon driver
 *
 * Copyright (C) 2020 National Instruments Corporation
 * Author: Michael Auchter <michael.auchter@ni.com>
 */

#include <linux/extcon-provider.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define TUSB320_REG9				0x9
#define TUSB320_REG9_ATTACHED_STATE_SHIFT	6
#define TUSB320_REG9_ATTACHED_STATE_MASK	0x3
#define TUSB320_REG9_CABLE_DIRECTION		BIT(5)
#define TUSB320_REG9_INTERRUPT_STATUS		BIT(4)
#define TUSB320_ATTACHED_STATE_NONE		0x0
#define TUSB320_ATTACHED_STATE_DFP		0x1
#define TUSB320_ATTACHED_STATE_UFP		0x2
#define TUSB320_ATTACHED_STATE_ACC		0x3

struct tusb320_priv {
	struct device *dev;
	struct regmap *regmap;
	struct extcon_dev *edev;
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

static irqreturn_t tusb320_irq_handler(int irq, void *dev_id)
{
	struct tusb320_priv *priv = dev_id;
	int state, polarity;
	unsigned reg;

	if (regmap_read(priv->regmap, TUSB320_REG9, &reg)) {
		dev_err(priv->dev, "error during i2c read!\n");
		return IRQ_NONE;
	}

	if (!(reg & TUSB320_REG9_INTERRUPT_STATUS))
		return IRQ_NONE;

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

	regmap_write(priv->regmap, TUSB320_REG9, reg);

	return IRQ_HANDLED;
}

static const struct regmap_config tusb320_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int tusb320_extcon_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct tusb320_priv *priv;
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

	/* update initial state */
	tusb320_irq_handler(client->irq, priv);

	ret = devm_request_threaded_irq(priv->dev, client->irq, NULL,
					tusb320_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					client->name, priv);

	return ret;
}

static const struct of_device_id tusb320_extcon_dt_match[] = {
	{ .compatible = "ti,tusb320", },
	{ }
};
MODULE_DEVICE_TABLE(of, tusb320_extcon_dt_match);

static struct i2c_driver tusb320_extcon_driver = {
	.probe		= tusb320_extcon_probe,
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
