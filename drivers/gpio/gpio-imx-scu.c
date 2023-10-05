// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021~2022 NXP
 *
 * The driver exports a standard gpiochip interface
 * to control the PIN resources on SCU domain.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/firmware/imx/svc/rm.h>
#include <dt-bindings/firmware/imx/rsrc.h>

struct scu_gpio_priv {
	struct gpio_chip	chip;
	struct mutex		lock;
	struct device		*dev;
	struct imx_sc_ipc	*handle;
};

static unsigned int scu_rsrc_arr[] = {
	IMX_SC_R_BOARD_R0,
	IMX_SC_R_BOARD_R1,
	IMX_SC_R_BOARD_R2,
	IMX_SC_R_BOARD_R3,
	IMX_SC_R_BOARD_R4,
	IMX_SC_R_BOARD_R5,
	IMX_SC_R_BOARD_R6,
	IMX_SC_R_BOARD_R7,
};

static int imx_scu_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct scu_gpio_priv *priv = gpiochip_get_data(chip);
	int level;
	int err;

	if (offset >= chip->ngpio)
		return -EINVAL;

	mutex_lock(&priv->lock);

	/* to read PIN state via scu api */
	err = imx_sc_misc_get_control(priv->handle,
			scu_rsrc_arr[offset], 0, &level);
	mutex_unlock(&priv->lock);

	if (err) {
		dev_err(priv->dev, "SCU get failed: %d\n", err);
		return err;
	}

	return level;
}

static void imx_scu_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct scu_gpio_priv *priv = gpiochip_get_data(chip);
	int err;

	if (offset >= chip->ngpio)
		return;

	mutex_lock(&priv->lock);

	/* to set PIN output level via scu api */
	err = imx_sc_misc_set_control(priv->handle,
			scu_rsrc_arr[offset], 0, value);
	mutex_unlock(&priv->lock);

	if (err)
		dev_err(priv->dev, "SCU set (%d) failed: %d\n",
				scu_rsrc_arr[offset], err);
}

static int imx_scu_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	if (offset >= chip->ngpio)
		return -EINVAL;

	return GPIO_LINE_DIRECTION_OUT;
}

static int imx_scu_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct scu_gpio_priv *priv;
	struct gpio_chip *gc;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = imx_scu_get_handle(&priv->handle);
	if (ret)
		return ret;

	priv->dev = dev;
	mutex_init(&priv->lock);

	gc = &priv->chip;
	gc->base = -1;
	gc->parent = dev;
	gc->ngpio = ARRAY_SIZE(scu_rsrc_arr);
	gc->label = dev_name(dev);
	gc->get = imx_scu_gpio_get;
	gc->set = imx_scu_gpio_set;
	gc->get_direction = imx_scu_gpio_get_direction;

	platform_set_drvdata(pdev, priv);

	return devm_gpiochip_add_data(dev, gc, priv);
}

static const struct of_device_id imx_scu_gpio_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-sc-gpio" },
	{ /* sentinel */ }
};

static struct platform_driver imx_scu_gpio_driver = {
	.driver	= {
		.name = "gpio-imx-scu",
		.of_match_table = imx_scu_gpio_dt_ids,
	},
	.probe = imx_scu_gpio_probe,
};

static int __init _imx_scu_gpio_init(void)
{
	return platform_driver_register(&imx_scu_gpio_driver);
}

subsys_initcall_sync(_imx_scu_gpio_init);

MODULE_AUTHOR("Shenwei Wang <shenwei.wang@nxp.com>");
MODULE_DESCRIPTION("NXP GPIO over IMX SCU API");
