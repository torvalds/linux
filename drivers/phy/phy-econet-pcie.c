// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Caleb James DeLisle <cjd@cjdns.fr>
 *	   Ahmed Naseef <naseefkm@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/bits.h>
#include <linux/delay.h>

/* Rx detection timing for EN751221: 16*8 clock cycles  */
#define EN751221_RXDET_VAL		16

/* Rx detection timing when in power mode 3 */
#define EN75_RXDET_P3_REG		0xa28
#define EN75_RXDET_P3_MASK		GENMASK(17, 9)

/* Rx detection timing when in power mode 2 */
#define EN75_RXDET_P2_REG		0xa2c
#define EN75_RXDET_P2_MASK		GENMASK(8, 0)

/* Rx impedance */
#define EN75_RX_IMPEDANCE_REG		0xb2c
#define EN75_RX_IMPEDANCE_MASK		GENMASK(13, 12)
enum en75_rx_impedance {
	EN75_RX_IMPEDANCE_100_OHM	= 0,
	EN75_RX_IMPEDANCE_95_OHM	= 1,
	EN75_RX_IMPEDANCE_90_OHM	= 2,
};

/* PLL Invert clock */
#define EN75_PLL_PH_INV_REG		0x4a0
#define EN75_PLL_PH_INV_MASK		BIT(5)

struct en75_phy_op {
	u32 reg;
	u32 mask;
	u32 val;
};

struct en7528_pcie_phy {
	struct regmap *regmap;
	const struct en75_phy_op *data;
};

/* Port 0 PHY: set LCDDS_CLK_PH_INV for PLL operation */
static const struct en75_phy_op en7528_phy_gen1[] = {
	{
		.reg = EN75_PLL_PH_INV_REG,
		.mask = EN75_PLL_PH_INV_MASK,
		.val = 1,
	},
	{ /* sentinel */ }
};

/* EN7528 Port 1 PHY: Rx impedance tuning, target R -5 Ohm */
static const struct en75_phy_op en7528_phy_gen2[] = {
	{
		.reg = EN75_RX_IMPEDANCE_REG,
		.mask = EN75_RX_IMPEDANCE_MASK,
		.val = EN75_RX_IMPEDANCE_95_OHM,
	},
	{ /* sentinel */ }
};

/* EN751221 Port 1 PHY, set RX detect to 16*8 clock cycles */
static const struct en75_phy_op en751221_phy_gen2[] = {
	{
		.reg = EN75_RXDET_P3_REG,
		.mask = EN75_RXDET_P3_MASK,
		.val = EN751221_RXDET_VAL,
	},
	{
		.reg = EN75_RXDET_P2_REG,
		.mask = EN75_RXDET_P2_MASK,
		.val = EN751221_RXDET_VAL,
	},
	{ /* sentinel */ }
};

static int en75_pcie_phy_init(struct phy *phy)
{
	struct en7528_pcie_phy *ephy = phy_get_drvdata(phy);
	const struct en75_phy_op *data = ephy->data;
	int i, ret;
	u32 val;

	for (i = 0; data[i].mask || data[i].val; i++) {
		if (i)
			usleep_range(1000, 2000);

		val = field_prep(data[i].mask, data[i].val);

		ret = regmap_update_bits(ephy->regmap, data[i].reg,
					 data[i].mask, val);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct phy_ops en75_pcie_phy_ops = {
	.init	= en75_pcie_phy_init,
	.owner	= THIS_MODULE,
};

static int en75_pcie_phy_probe(struct platform_device *pdev)
{
	struct regmap_config regmap_config = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
	};
	struct device *dev = &pdev->dev;
	const struct en75_phy_op *data;
	struct phy_provider *provider;
	struct en7528_pcie_phy *ephy;
	void __iomem *base;
	struct phy *phy;
	int i;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	ephy = devm_kzalloc(dev, sizeof(*ephy), GFP_KERNEL);
	if (!ephy)
		return -ENOMEM;

	ephy->data = data;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* Set max_register to highest used register */
	for (i = 0; data[i].mask || data[i].val; i++)
		if (data[i].reg > regmap_config.max_register)
			regmap_config.max_register = data[i].reg;

	ephy->regmap = devm_regmap_init_mmio(dev, base, &regmap_config);
	if (IS_ERR(ephy->regmap))
		return PTR_ERR(ephy->regmap);

	phy = devm_phy_create(dev, dev->of_node, &en75_pcie_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, ephy);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id en75_pcie_phy_ids[] = {
	{ .compatible = "econet,en7528-pcie-gen1", .data = en7528_phy_gen1 },
	{ .compatible = "econet,en7528-pcie-gen2", .data = en7528_phy_gen2 },
	{ .compatible = "econet,en751221-pcie-gen1", .data = en7528_phy_gen1 },
	{ .compatible = "econet,en751221-pcie-gen2", .data = en751221_phy_gen2 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, en75_pcie_phy_ids);

static struct platform_driver en75_pcie_phy_driver = {
	.probe = en75_pcie_phy_probe,
	.driver = {
		.name = "econet-pcie-phy",
		.of_match_table = en75_pcie_phy_ids,
	},
};
module_platform_driver(en75_pcie_phy_driver);

MODULE_AUTHOR("Caleb James DeLisle <cjd@cjdns.fr>");
MODULE_DESCRIPTION("EcoNet PCIe PHY driver");
MODULE_LICENSE("GPL");
