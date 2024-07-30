// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L USBPHY control driver
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>

#define RESET			0x000
#define VBENCTL			0x03c

#define RESET_SEL_PLLRESET	BIT(12)
#define RESET_PLLRESET		BIT(8)

#define RESET_SEL_P2RESET	BIT(5)
#define RESET_SEL_P1RESET	BIT(4)
#define RESET_PHYRST_2		BIT(1)
#define RESET_PHYRST_1		BIT(0)

#define PHY_RESET_PORT2		(RESET_SEL_P2RESET | RESET_PHYRST_2)
#define PHY_RESET_PORT1		(RESET_SEL_P1RESET | RESET_PHYRST_1)

#define NUM_PORTS		2

struct rzg2l_usbphy_ctrl_priv {
	struct reset_controller_dev rcdev;
	struct reset_control *rstc;
	void __iomem *base;
	struct platform_device *vdev;

	spinlock_t lock;
};

#define rcdev_to_priv(x)	container_of(x, struct rzg2l_usbphy_ctrl_priv, rcdev)

static int rzg2l_usbphy_ctrl_assert(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct rzg2l_usbphy_ctrl_priv *priv = rcdev_to_priv(rcdev);
	u32 port_mask = PHY_RESET_PORT1 | PHY_RESET_PORT2;
	void __iomem *base = priv->base;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&priv->lock, flags);
	val = readl(base + RESET);
	val |= id ? PHY_RESET_PORT2 : PHY_RESET_PORT1;
	if (port_mask == (val & port_mask))
		val |= RESET_PLLRESET;
	writel(val, base + RESET);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rzg2l_usbphy_ctrl_deassert(struct reset_controller_dev *rcdev,
				      unsigned long id)
{
	struct rzg2l_usbphy_ctrl_priv *priv = rcdev_to_priv(rcdev);
	void __iomem *base = priv->base;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&priv->lock, flags);
	val = readl(base + RESET);

	val |= RESET_SEL_PLLRESET;
	val &= ~(RESET_PLLRESET | (id ? PHY_RESET_PORT2 : PHY_RESET_PORT1));
	writel(val, base + RESET);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rzg2l_usbphy_ctrl_status(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct rzg2l_usbphy_ctrl_priv *priv = rcdev_to_priv(rcdev);
	u32 port_mask;

	port_mask = id ? PHY_RESET_PORT2 : PHY_RESET_PORT1;

	return !!(readl(priv->base + RESET) & port_mask);
}

static const struct of_device_id rzg2l_usbphy_ctrl_match_table[] = {
	{ .compatible = "renesas,rzg2l-usbphy-ctrl" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg2l_usbphy_ctrl_match_table);

static const struct reset_control_ops rzg2l_usbphy_ctrl_reset_ops = {
	.assert = rzg2l_usbphy_ctrl_assert,
	.deassert = rzg2l_usbphy_ctrl_deassert,
	.status = rzg2l_usbphy_ctrl_status,
};

static const struct regmap_config rzg2l_usb_regconf = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 1,
};

static int rzg2l_usbphy_ctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzg2l_usbphy_ctrl_priv *priv;
	struct platform_device *vdev;
	struct regmap *regmap;
	unsigned long flags;
	int error;
	u32 val;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	regmap = devm_regmap_init_mmio(dev, priv->base + VBENCTL, &rzg2l_usb_regconf);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	priv->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(priv->rstc))
		return dev_err_probe(dev, PTR_ERR(priv->rstc),
				     "failed to get reset\n");

	error = reset_control_deassert(priv->rstc);
	if (error)
		return error;

	spin_lock_init(&priv->lock);
	dev_set_drvdata(dev, priv);

	pm_runtime_enable(&pdev->dev);
	error = pm_runtime_resume_and_get(&pdev->dev);
	if (error < 0) {
		dev_err_probe(&pdev->dev, error, "pm_runtime_resume_and_get failed");
		goto err_pm_disable_reset_deassert;
	}

	/* put pll and phy into reset state */
	spin_lock_irqsave(&priv->lock, flags);
	val = readl(priv->base + RESET);
	val |= RESET_SEL_PLLRESET | RESET_PLLRESET | PHY_RESET_PORT2 | PHY_RESET_PORT1;
	writel(val, priv->base + RESET);
	spin_unlock_irqrestore(&priv->lock, flags);

	priv->rcdev.ops = &rzg2l_usbphy_ctrl_reset_ops;
	priv->rcdev.of_reset_n_cells = 1;
	priv->rcdev.nr_resets = NUM_PORTS;
	priv->rcdev.of_node = dev->of_node;
	priv->rcdev.dev = dev;

	error = devm_reset_controller_register(dev, &priv->rcdev);
	if (error)
		goto err_pm_runtime_put;

	vdev = platform_device_alloc("rzg2l-usb-vbus-regulator", pdev->id);
	if (!vdev) {
		error = -ENOMEM;
		goto err_pm_runtime_put;
	}
	vdev->dev.parent = dev;
	priv->vdev = vdev;

	error = platform_device_add(vdev);
	if (error)
		goto err_device_put;

	return 0;

err_device_put:
	platform_device_put(vdev);
err_pm_runtime_put:
	pm_runtime_put(&pdev->dev);
err_pm_disable_reset_deassert:
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(priv->rstc);
	return error;
}

static void rzg2l_usbphy_ctrl_remove(struct platform_device *pdev)
{
	struct rzg2l_usbphy_ctrl_priv *priv = dev_get_drvdata(&pdev->dev);

	platform_device_unregister(priv->vdev);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(priv->rstc);
}

static struct platform_driver rzg2l_usbphy_ctrl_driver = {
	.driver = {
		.name		= "rzg2l_usbphy_ctrl",
		.of_match_table	= rzg2l_usbphy_ctrl_match_table,
	},
	.probe	= rzg2l_usbphy_ctrl_probe,
	.remove_new = rzg2l_usbphy_ctrl_remove,
};
module_platform_driver(rzg2l_usbphy_ctrl_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas RZ/G2L USBPHY Control");
MODULE_AUTHOR("biju.das.jz@bp.renesas.com>");
