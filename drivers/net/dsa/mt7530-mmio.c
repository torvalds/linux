// SPDX-License-Identifier: GPL-2.0-only

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <net/dsa.h>

#include "mt7530.h"

static const struct of_device_id mt7988_of_match[] = {
	{ .compatible = "mediatek,mt7988-switch", .data = &mt753x_table[ID_MT7988], },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mt7988_of_match);

static int
mt7988_probe(struct platform_device *pdev)
{
	static struct regmap_config *sw_regmap_config;
	struct mt7530_priv *priv;
	void __iomem *base_addr;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->bus = NULL;
	priv->dev = &pdev->dev;

	ret = mt7530_probe_common(priv);
	if (ret)
		return ret;

	priv->rstc = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(priv->rstc)) {
		dev_err(&pdev->dev, "Couldn't get our reset line\n");
		return PTR_ERR(priv->rstc);
	}

	base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base_addr)) {
		dev_err(&pdev->dev, "cannot request I/O memory space\n");
		return -ENXIO;
	}

	sw_regmap_config = devm_kzalloc(&pdev->dev, sizeof(*sw_regmap_config), GFP_KERNEL);
	if (!sw_regmap_config)
		return -ENOMEM;

	sw_regmap_config->name = "switch";
	sw_regmap_config->reg_bits = 16;
	sw_regmap_config->val_bits = 32;
	sw_regmap_config->reg_stride = 4;
	sw_regmap_config->max_register = MT7530_CREV;
	priv->regmap = devm_regmap_init_mmio(&pdev->dev, base_addr, sw_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	return dsa_register_switch(priv->ds);
}

static void mt7988_remove(struct platform_device *pdev)
{
	struct mt7530_priv *priv = platform_get_drvdata(pdev);

	if (priv)
		mt7530_remove_common(priv);
}

static void mt7988_shutdown(struct platform_device *pdev)
{
	struct mt7530_priv *priv = platform_get_drvdata(pdev);

	if (!priv)
		return;

	dsa_switch_shutdown(priv->ds);

	dev_set_drvdata(&pdev->dev, NULL);
}

static struct platform_driver mt7988_platform_driver = {
	.probe  = mt7988_probe,
	.remove_new = mt7988_remove,
	.shutdown = mt7988_shutdown,
	.driver = {
		.name = "mt7530-mmio",
		.of_match_table = mt7988_of_match,
	},
};
module_platform_driver(mt7988_platform_driver);

MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_DESCRIPTION("Driver for Mediatek MT7530 Switch (MMIO)");
MODULE_LICENSE("GPL");
