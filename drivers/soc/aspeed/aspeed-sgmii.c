// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */
#include <linux/sizes.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define FTGMAC100_SGMII_CFG		0x00
#define FTGMAC100_SGMII_PHY_PIPE_CTL	0x20
#define FTGMAC100_SGMII_MODE		0x30

#define FTGMAC100_SGMII_CFG_FIFO_MODE		BIT(0)
#define FTGMAC100_SGMII_CFG_SPEED_10M		0
#define FTGMAC100_SGMII_CFG_SPEED_100M		BIT(4)
#define FTGMAC100_SGMII_CFG_SPEED_1G		BIT(5)
#define FTGMAC100_SGMII_CFG_PWR_DOWN		BIT(11)
#define FTGMAC100_SGMII_CFG_AN_ENABLE		BIT(12)
#define FTGMAC100_SGMII_CFG_SW_RESET		BIT(15)
#define FTGMAC100_SGMII_PCTL_TX_DEEMPH_3_5DB	BIT(6)
#define FTGMAC100_SGMII_MODE_ENABLE		BIT(0)
#define FTGMAC100_SGMII_MODE_USE_LOCAL_CONFIG	BIT(2)

#define FTGMAC100_PLDA_CLK		0x268
#define FTGMAC100_PLDA_STATUS		0x300

struct ast2700_sgmii {
	struct device *dev;
	void __iomem *regs;
	struct regmap *plda_regmap;
};

static int ast2700_sgmii_probe(struct platform_device *pdev)
{
	struct ast2700_sgmii *sgmii;
	struct resource *res;
	struct device *dev;
	struct device_node *np;
	struct device_node *fixed_link_node;
	int i;
	u32 reg, speed;

	dev = &pdev->dev;

	sgmii = devm_kzalloc(dev, sizeof(*sgmii), GFP_KERNEL);
	if (!sgmii)
		return -ENOMEM;

	sgmii->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot get resource\n");
		return -ENODEV;
	}

	sgmii->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(sgmii->regs)) {
		dev_err(dev, "cannot map registers\n");
		return PTR_ERR(sgmii->regs);
	}

	np = pdev->dev.of_node;
	sgmii->plda_regmap = syscon_regmap_lookup_by_phandle(np, "aspeed,plda");
	if (IS_ERR(sgmii->plda_regmap)) {
		dev_err(sgmii->dev, "Unable to find plda regmap (%ld)\n",
			PTR_ERR(sgmii->plda_regmap));
		return PTR_ERR(sgmii->plda_regmap);
	}

	reg = 0x12b;
	regmap_read(sgmii->plda_regmap, FTGMAC100_PLDA_CLK, &reg);

	writel(0, sgmii->regs + FTGMAC100_SGMII_MODE);
	writel(FTGMAC100_SGMII_CFG_FIFO_MODE, sgmii->regs + FTGMAC100_SGMII_CFG);
	reg = FTGMAC100_SGMII_CFG_SW_RESET | FTGMAC100_SGMII_CFG_PWR_DOWN |
	      FTGMAC100_SGMII_CFG_FIFO_MODE;
	writel(reg, sgmii->regs + FTGMAC100_SGMII_CFG);

	reg = FTGMAC100_SGMII_CFG_FIFO_MODE;
	fixed_link_node = of_get_child_by_name(np, "fixed-link");
	if (fixed_link_node && of_property_read_u32(fixed_link_node, "speed", &speed) == 0) {
		if (speed == 10)
			reg |= FTGMAC100_SGMII_CFG_SPEED_10M;
		else if (speed == 100)
			reg |= FTGMAC100_SGMII_CFG_SPEED_100M;
		else
			reg |= FTGMAC100_SGMII_CFG_SPEED_1G;
	} else {
		reg |= FTGMAC100_SGMII_CFG_AN_ENABLE;
	}
	writel(reg, sgmii->regs + FTGMAC100_SGMII_CFG);

	writel(FTGMAC100_SGMII_PCTL_TX_DEEMPH_3_5DB, sgmii->regs + FTGMAC100_SGMII_PHY_PIPE_CTL);
	reg = FTGMAC100_SGMII_MODE_USE_LOCAL_CONFIG | FTGMAC100_SGMII_MODE_ENABLE;
	writel(reg, sgmii->regs + FTGMAC100_SGMII_MODE);

	for (i = 0; i < 200; i++) {
		regmap_read(sgmii->plda_regmap, FTGMAC100_PLDA_STATUS, &reg);
		if (reg & BIT(24))
			break;
		mdelay(1);
	}

	dev_set_drvdata(dev, sgmii);

	dev_info(dev, "module loaded\n");

	return 0;
}

static int ast2700_sgmii_remove(struct platform_device *pdev)
{
	struct ast2700_sgmii *sgmii;
	struct device *dev;

	dev = &pdev->dev;

	sgmii = (struct ast2700_sgmii *)dev_get_drvdata(dev);

	writel(0, sgmii->regs + FTGMAC100_SGMII_MODE);

	return 0;
}

static const struct of_device_id ast2700_sgmii_of_matches[] = {
	{ .compatible = "aspeed,ast2700-sgmii" },
	{ },
};

static struct platform_driver ast2700_sgmii_driver = {
	.driver = {
		.name = "ast2700-sgmii",
		.of_match_table = ast2700_sgmii_of_matches,
	},
	.probe = ast2700_sgmii_probe,
	.remove = ast2700_sgmii_remove,
};

module_platform_driver(ast2700_sgmii_driver);

MODULE_AUTHOR("Jacky Chou <jacky_chou@aspeedtech.com>");
MODULE_DESCRIPTION("Control of AST2700 SGMII Device");
MODULE_LICENSE("GPL");
