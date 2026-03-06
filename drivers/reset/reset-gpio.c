// SPDX-License-Identifier: GPL-2.0

#include <linux/auxiliary_bus.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/reset-controller.h>

struct reset_gpio_priv {
	struct reset_controller_dev rc;
	struct gpio_desc *reset;
};

static inline struct reset_gpio_priv
*rc_to_reset_gpio(struct reset_controller_dev *rc)
{
	return container_of(rc, struct reset_gpio_priv, rc);
}

static int reset_gpio_assert(struct reset_controller_dev *rc, unsigned long id)
{
	struct reset_gpio_priv *priv = rc_to_reset_gpio(rc);

	return gpiod_set_value_cansleep(priv->reset, 1);
}

static int reset_gpio_deassert(struct reset_controller_dev *rc,
			       unsigned long id)
{
	struct reset_gpio_priv *priv = rc_to_reset_gpio(rc);

	return gpiod_set_value_cansleep(priv->reset, 0);
}

static int reset_gpio_status(struct reset_controller_dev *rc, unsigned long id)
{
	struct reset_gpio_priv *priv = rc_to_reset_gpio(rc);

	return gpiod_get_value_cansleep(priv->reset);
}

static const struct reset_control_ops reset_gpio_ops = {
	.assert = reset_gpio_assert,
	.deassert = reset_gpio_deassert,
	.status = reset_gpio_status,
};

static int reset_gpio_fwnode_xlate(struct reset_controller_dev *rcdev,
				   const struct fwnode_reference_args *reset_spec)
{
	return reset_spec->args[0];
}

static int reset_gpio_probe(struct auxiliary_device *adev,
			    const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	struct reset_gpio_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset))
		return dev_err_probe(dev, PTR_ERR(priv->reset),
				     "Could not get reset gpios\n");

	priv->rc.ops = &reset_gpio_ops;
	priv->rc.owner = THIS_MODULE;
	priv->rc.dev = dev;

	/* Cells to match GPIO specifier, but it's not really used */
	priv->rc.fwnode_reset_n_cells = 2;
	priv->rc.fwnode_xlate = reset_gpio_fwnode_xlate;
	priv->rc.nr_resets = 1;

	return devm_reset_controller_register(dev, &priv->rc);
}

static const struct auxiliary_device_id reset_gpio_ids[] = {
	{ .name = "reset.gpio" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, reset_gpio_ids);

static struct auxiliary_driver reset_gpio_driver = {
	.probe		= reset_gpio_probe,
	.id_table	= reset_gpio_ids,
	.driver	= {
		.name = "reset-gpio",
		.suppress_bind_attrs = true,
	},
};
module_auxiliary_driver(reset_gpio_driver);

MODULE_AUTHOR("Krzysztof Kozlowski <krzysztof.kozlowski@linaro.org>");
MODULE_DESCRIPTION("Generic GPIO reset driver");
MODULE_LICENSE("GPL");
