// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2024 NXP
 */

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>

#define EARC			0x200
#define EARC_RESET_MASK		0x3

struct imx8mp_audiomix_reset {
	struct reset_controller_dev rcdev;
	spinlock_t lock; /* protect register read-modify-write cycle */
	void __iomem *base;
};

static struct imx8mp_audiomix_reset *to_imx8mp_audiomix_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct imx8mp_audiomix_reset, rcdev);
}

static int imx8mp_audiomix_reset_assert(struct reset_controller_dev *rcdev,
					unsigned long id)
{
	struct imx8mp_audiomix_reset *priv = to_imx8mp_audiomix_reset(rcdev);
	void __iomem *reg_addr = priv->base;
	unsigned int mask, reg;
	unsigned long flags;

	mask = BIT(id);
	spin_lock_irqsave(&priv->lock, flags);
	reg = readl(reg_addr + EARC);
	writel(reg & ~mask, reg_addr + EARC);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int imx8mp_audiomix_reset_deassert(struct reset_controller_dev *rcdev,
					  unsigned long id)
{
	struct imx8mp_audiomix_reset *priv = to_imx8mp_audiomix_reset(rcdev);
	void __iomem *reg_addr = priv->base;
	unsigned int mask, reg;
	unsigned long flags;

	mask = BIT(id);
	spin_lock_irqsave(&priv->lock, flags);
	reg = readl(reg_addr + EARC);
	writel(reg | mask, reg_addr + EARC);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static const struct reset_control_ops imx8mp_audiomix_reset_ops = {
	.assert   = imx8mp_audiomix_reset_assert,
	.deassert = imx8mp_audiomix_reset_deassert,
};

static int imx8mp_audiomix_reset_probe(struct auxiliary_device *adev,
				       const struct auxiliary_device_id *id)
{
	struct imx8mp_audiomix_reset *priv;
	struct device *dev = &adev->dev;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);

	priv->rcdev.owner     = THIS_MODULE;
	priv->rcdev.nr_resets = fls(EARC_RESET_MASK);
	priv->rcdev.ops       = &imx8mp_audiomix_reset_ops;
	priv->rcdev.of_node   = dev->parent->of_node;
	priv->rcdev.dev	      = dev;
	priv->rcdev.of_reset_n_cells = 1;
	priv->base            = of_iomap(dev->parent->of_node, 0);
	if (!priv->base)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	ret = devm_reset_controller_register(dev, &priv->rcdev);
	if (ret)
		goto out_unmap;

	return 0;

out_unmap:
	iounmap(priv->base);
	return ret;
}

static void imx8mp_audiomix_reset_remove(struct auxiliary_device *adev)
{
	struct imx8mp_audiomix_reset *priv = dev_get_drvdata(&adev->dev);

	iounmap(priv->base);
}

static const struct auxiliary_device_id imx8mp_audiomix_reset_ids[] = {
	{
		.name = "clk_imx8mp_audiomix.reset",
	},
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, imx8mp_audiomix_reset_ids);

static struct auxiliary_driver imx8mp_audiomix_reset_driver = {
	.probe		= imx8mp_audiomix_reset_probe,
	.remove		= imx8mp_audiomix_reset_remove,
	.id_table	= imx8mp_audiomix_reset_ids,
};

module_auxiliary_driver(imx8mp_audiomix_reset_driver);

MODULE_AUTHOR("Shengjiu Wang <shengjiu.wang@nxp.com>");
MODULE_DESCRIPTION("Freescale i.MX8MP Audio Block Controller reset driver");
MODULE_LICENSE("GPL");
