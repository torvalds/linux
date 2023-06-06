// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Renesas Electronics Corporation
 */

#include <linux/delay.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

#define PWC_PWCRST			0x00
#define PWC_PWCCKEN			0x04
#define PWC_PWCCTL			0x50
#define PWC_GPIO			0x80

#define PWC_PWCRST_RSTSOFTAX		0x1
#define PWC_PWCCKEN_ENGCKMAIN		0x1
#define PWC_PWCCTL_PWOFF		0x1

struct rzv2m_pwc_priv {
	void __iomem *base;
	struct device *dev;
	struct gpio_chip gp;
	DECLARE_BITMAP(ch_en_bits, 2);
};

static void rzv2m_pwc_gpio_set(struct gpio_chip *chip, unsigned int offset,
			       int value)
{
	struct rzv2m_pwc_priv *priv = gpiochip_get_data(chip);
	u32 reg;

	/* BIT 16 enables write to BIT 0, and BIT 17 enables write to BIT 1 */
	reg = BIT(offset + 16);
	if (value)
		reg |= BIT(offset);

	writel(reg, priv->base + PWC_GPIO);

	assign_bit(offset, priv->ch_en_bits, value);
}

static int rzv2m_pwc_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rzv2m_pwc_priv *priv = gpiochip_get_data(chip);

	return test_bit(offset, priv->ch_en_bits);
}

static int rzv2m_pwc_gpio_direction_output(struct gpio_chip *gc,
					   unsigned int nr, int value)
{
	if (nr > 1)
		return -EINVAL;

	rzv2m_pwc_gpio_set(gc, nr, value);

	return 0;
}

static const struct gpio_chip rzv2m_pwc_gc = {
	.label = "gpio_rzv2m_pwc",
	.owner = THIS_MODULE,
	.get = rzv2m_pwc_gpio_get,
	.set = rzv2m_pwc_gpio_set,
	.direction_output = rzv2m_pwc_gpio_direction_output,
	.can_sleep = false,
	.ngpio = 2,
	.base = -1,
};

static int rzv2m_pwc_poweroff(struct sys_off_data *data)
{
	struct rzv2m_pwc_priv *priv = data->cb_data;

	writel(PWC_PWCRST_RSTSOFTAX, priv->base + PWC_PWCRST);
	writel(PWC_PWCCKEN_ENGCKMAIN, priv->base + PWC_PWCCKEN);
	writel(PWC_PWCCTL_PWOFF, priv->base + PWC_PWCCTL);

	mdelay(150);

	dev_err(priv->dev, "Failed to power off the system");

	return NOTIFY_DONE;
}

static int rzv2m_pwc_probe(struct platform_device *pdev)
{
	struct rzv2m_pwc_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	/*
	 * The register used by this driver cannot be read, therefore set the
	 * outputs to their default values and initialize priv->ch_en_bits
	 * accordingly. BIT 16 enables write to BIT 0, BIT 17 enables write to
	 * BIT 1, and the default value of both BIT 0 and BIT 1 is 0.
	 */
	writel(BIT(17) | BIT(16), priv->base + PWC_GPIO);
	bitmap_zero(priv->ch_en_bits, 2);

	priv->gp = rzv2m_pwc_gc;
	priv->gp.parent = pdev->dev.parent;
	priv->gp.fwnode = dev_fwnode(&pdev->dev);

	ret = devm_gpiochip_add_data(&pdev->dev, &priv->gp, priv);
	if (ret)
		return ret;

	if (device_property_read_bool(&pdev->dev, "renesas,rzv2m-pwc-power"))
		ret = devm_register_power_off_handler(&pdev->dev,
						      rzv2m_pwc_poweroff, priv);

	return ret;
}

static const struct of_device_id rzv2m_pwc_of_match[] = {
	{ .compatible = "renesas,rzv2m-pwc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzv2m_pwc_of_match);

static struct platform_driver rzv2m_pwc_driver = {
	.probe = rzv2m_pwc_probe,
	.driver = {
		.name = "rzv2m_pwc",
		.of_match_table = rzv2m_pwc_of_match,
	},
};
module_platform_driver(rzv2m_pwc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fabrizio Castro <castro.fabrizio.jz@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/V2M PWC driver");
