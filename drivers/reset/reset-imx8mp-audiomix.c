// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2024 NXP
 */

#include <dt-bindings/reset/imx8mp-reset-audiomix.h>

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>

#define IMX8MP_AUDIOMIX_EARC_RESET_OFFSET	0x200
#define IMX8MP_AUDIOMIX_EARC_RESET_MASK		BIT(0)
#define IMX8MP_AUDIOMIX_EARC_PHY_RESET_MASK	BIT(1)

#define IMX8MP_AUDIOMIX_DSP_RUNSTALL_OFFSET	0x108
#define IMX8MP_AUDIOMIX_DSP_RUNSTALL_MASK	BIT(5)

struct imx8mp_reset_map {
	unsigned int offset;
	unsigned int mask;
	bool active_low;
};

static const struct imx8mp_reset_map reset_map[] = {
	[IMX8MP_AUDIOMIX_EARC_RESET] = {
		.offset	= IMX8MP_AUDIOMIX_EARC_RESET_OFFSET,
		.mask	= IMX8MP_AUDIOMIX_EARC_RESET_MASK,
		.active_low = true,
	},
	[IMX8MP_AUDIOMIX_EARC_PHY_RESET] = {
		.offset	= IMX8MP_AUDIOMIX_EARC_RESET_OFFSET,
		.mask	= IMX8MP_AUDIOMIX_EARC_PHY_RESET_MASK,
		.active_low = true,
	},
	[IMX8MP_AUDIOMIX_DSP_RUNSTALL] = {
		.offset	= IMX8MP_AUDIOMIX_DSP_RUNSTALL_OFFSET,
		.mask	= IMX8MP_AUDIOMIX_DSP_RUNSTALL_MASK,
		.active_low = false,
	},
};

struct imx8mp_audiomix_reset {
	struct reset_controller_dev rcdev;
	spinlock_t lock; /* protect register read-modify-write cycle */
	void __iomem *base;
};

static struct imx8mp_audiomix_reset *to_imx8mp_audiomix_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct imx8mp_audiomix_reset, rcdev);
}

static int imx8mp_audiomix_update(struct reset_controller_dev *rcdev,
				  unsigned long id, bool assert)
{
	struct imx8mp_audiomix_reset *priv = to_imx8mp_audiomix_reset(rcdev);
	void __iomem *reg_addr = priv->base;
	unsigned int mask, offset, active_low;
	unsigned long reg, flags;

	mask = reset_map[id].mask;
	offset = reset_map[id].offset;
	active_low = reset_map[id].active_low;

	spin_lock_irqsave(&priv->lock, flags);

	reg = readl(reg_addr + offset);
	if (active_low ^ assert)
		reg |= mask;
	else
		reg &= ~mask;
	writel(reg, reg_addr + offset);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int imx8mp_audiomix_reset_assert(struct reset_controller_dev *rcdev,
					unsigned long id)
{
	return imx8mp_audiomix_update(rcdev, id, true);
}

static int imx8mp_audiomix_reset_deassert(struct reset_controller_dev *rcdev,
					  unsigned long id)
{
	return imx8mp_audiomix_update(rcdev, id, false);
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
	priv->rcdev.nr_resets = ARRAY_SIZE(reset_map);
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
