// SPDX-License-Identifier: GPL-2.0
/*
 * phy-uniphier-usb2.c - PHY driver for UniPhier USB2 controller
 * Copyright 2015-2018 Socionext Inc.
 * Author:
 *      Kunihiko Hayashi <hayashi.kunihiko@socionext.com>
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define SG_USBPHY1CTRL		0x500
#define SG_USBPHY1CTRL2		0x504
#define SG_USBPHY2CTRL		0x508
#define SG_USBPHY2CTRL2		0x50c	/* LD11 */
#define SG_USBPHY12PLL		0x50c	/* Pro4 */
#define SG_USBPHY3CTRL		0x510
#define SG_USBPHY3CTRL2		0x514
#define SG_USBPHY4CTRL		0x518	/* Pro4 */
#define SG_USBPHY4CTRL2		0x51c	/* Pro4 */
#define SG_USBPHY34PLL		0x51c	/* Pro4 */

struct uniphier_u2phy_param {
	u32 offset;
	u32 value;
};

struct uniphier_u2phy_soc_data {
	struct uniphier_u2phy_param config0;
	struct uniphier_u2phy_param config1;
};

struct uniphier_u2phy_priv {
	struct regmap *regmap;
	struct phy *phy;
	struct regulator *vbus;
	const struct uniphier_u2phy_soc_data *data;
	struct uniphier_u2phy_priv *next;
};

static int uniphier_u2phy_power_on(struct phy *phy)
{
	struct uniphier_u2phy_priv *priv = phy_get_drvdata(phy);
	int ret = 0;

	if (priv->vbus)
		ret = regulator_enable(priv->vbus);

	return ret;
}

static int uniphier_u2phy_power_off(struct phy *phy)
{
	struct uniphier_u2phy_priv *priv = phy_get_drvdata(phy);

	if (priv->vbus)
		regulator_disable(priv->vbus);

	return 0;
}

static int uniphier_u2phy_init(struct phy *phy)
{
	struct uniphier_u2phy_priv *priv = phy_get_drvdata(phy);

	if (!priv->data)
		return 0;

	regmap_write(priv->regmap, priv->data->config0.offset,
		     priv->data->config0.value);
	regmap_write(priv->regmap, priv->data->config1.offset,
		     priv->data->config1.value);

	return 0;
}

static struct phy *uniphier_u2phy_xlate(struct device *dev,
					const struct of_phandle_args *args)
{
	struct uniphier_u2phy_priv *priv = dev_get_drvdata(dev);

	while (priv && args->np != priv->phy->dev.of_node)
		priv = priv->next;

	if (!priv) {
		dev_err(dev, "Failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	return priv->phy;
}

static const struct phy_ops uniphier_u2phy_ops = {
	.init      = uniphier_u2phy_init,
	.power_on  = uniphier_u2phy_power_on,
	.power_off = uniphier_u2phy_power_off,
	.owner = THIS_MODULE,
};

static int uniphier_u2phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *parent, *child;
	struct uniphier_u2phy_priv *priv = NULL, *next = NULL;
	struct phy_provider *phy_provider;
	struct regmap *regmap;
	const struct uniphier_u2phy_soc_data *data;
	int ret, data_idx, ndatas;

	data = of_device_get_match_data(dev);
	if (WARN_ON(!data))
		return -EINVAL;

	/* get number of data */
	for (ndatas = 0; data[ndatas].config0.offset; ndatas++)
		;

	parent = of_get_parent(dev->of_node);
	regmap = syscon_node_to_regmap(parent);
	of_node_put(parent);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to get regmap\n");
		return PTR_ERR(regmap);
	}

	for_each_child_of_node(dev->of_node, child) {
		priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
		if (!priv) {
			ret = -ENOMEM;
			goto out_put_child;
		}
		priv->regmap = regmap;

		priv->vbus = devm_regulator_get_optional(dev, "vbus");
		if (IS_ERR(priv->vbus)) {
			if (PTR_ERR(priv->vbus) == -EPROBE_DEFER) {
				ret = PTR_ERR(priv->vbus);
				goto out_put_child;
			}
			priv->vbus = NULL;
		}

		priv->phy = devm_phy_create(dev, child, &uniphier_u2phy_ops);
		if (IS_ERR(priv->phy)) {
			dev_err(dev, "Failed to create phy\n");
			ret = PTR_ERR(priv->phy);
			goto out_put_child;
		}

		ret = of_property_read_u32(child, "reg", &data_idx);
		if (ret) {
			dev_err(dev, "Failed to get reg property\n");
			goto out_put_child;
		}

		if (data_idx < ndatas)
			priv->data = &data[data_idx];
		else
			dev_warn(dev, "No phy configuration: %s\n",
				 child->full_name);

		phy_set_drvdata(priv->phy, priv);
		priv->next = next;
		next = priv;
	}

	dev_set_drvdata(dev, priv);
	phy_provider = devm_of_phy_provider_register(dev,
						     uniphier_u2phy_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);

out_put_child:
	of_node_put(child);

	return ret;
}

static const struct uniphier_u2phy_soc_data uniphier_pro4_data[] = {
	{
		.config0 = { SG_USBPHY1CTRL, 0x05142400 },
		.config1 = { SG_USBPHY12PLL, 0x00010010 },
	},
	{
		.config0 = { SG_USBPHY2CTRL, 0x05142400 },
		.config1 = { SG_USBPHY12PLL, 0x00010010 },
	},
	{
		.config0 = { SG_USBPHY3CTRL, 0x05142400 },
		.config1 = { SG_USBPHY34PLL, 0x00010010 },
	},
	{
		.config0 = { SG_USBPHY4CTRL, 0x05142400 },
		.config1 = { SG_USBPHY34PLL, 0x00010010 },
	},
	{ /* sentinel */ }
};

static const struct uniphier_u2phy_soc_data uniphier_ld11_data[] = {
	{
		.config0 = { SG_USBPHY1CTRL,  0x82280000 },
		.config1 = { SG_USBPHY1CTRL2, 0x00000106 },
	},
	{
		.config0 = { SG_USBPHY2CTRL,  0x82280000 },
		.config1 = { SG_USBPHY2CTRL2, 0x00000106 },
	},
	{
		.config0 = { SG_USBPHY3CTRL,  0x82280000 },
		.config1 = { SG_USBPHY3CTRL2, 0x00000106 },
	},
	{ /* sentinel */ }
};

static const struct of_device_id uniphier_u2phy_match[] = {
	{
		.compatible = "socionext,uniphier-pro4-usb2-phy",
		.data = &uniphier_pro4_data,
	},
	{
		.compatible = "socionext,uniphier-ld11-usb2-phy",
		.data = &uniphier_ld11_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_u2phy_match);

static struct platform_driver uniphier_u2phy_driver = {
	.probe = uniphier_u2phy_probe,
	.driver = {
		.name = "uniphier-usb2-phy",
		.of_match_table = uniphier_u2phy_match,
	},
};
module_platform_driver(uniphier_u2phy_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("UniPhier PHY driver for USB2 controller");
MODULE_LICENSE("GPL v2");
