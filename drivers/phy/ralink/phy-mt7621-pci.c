// SPDX-License-Identifier: GPL-2.0+
/*
 * Mediatek MT7621 PCI PHY Driver
 * Author: Sergio Paracuellos <sergio.paracuellos@gmail.com>
 */

#include <dt-bindings/phy/phy.h>
#include <linux/clk.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sys_soc.h>

#define RG_PE1_PIPE_REG				0x02c
#define RG_PE1_PIPE_RST				BIT(12)
#define RG_PE1_PIPE_CMD_FRC			BIT(4)

#define RG_P0_TO_P1_WIDTH			0x100
#define RG_PE1_H_LCDDS_REG			0x49c
#define RG_PE1_H_LCDDS_PCW			GENMASK(30, 0)

#define RG_PE1_FRC_H_XTAL_REG			0x400
#define RG_PE1_FRC_H_XTAL_TYPE			BIT(8)
#define RG_PE1_H_XTAL_TYPE			GENMASK(10, 9)

#define RG_PE1_FRC_PHY_REG			0x000
#define RG_PE1_FRC_PHY_EN			BIT(4)
#define RG_PE1_PHY_EN				BIT(5)

#define RG_PE1_H_PLL_REG			0x490
#define RG_PE1_H_PLL_BC				GENMASK(23, 22)
#define RG_PE1_H_PLL_BP				GENMASK(21, 18)
#define RG_PE1_H_PLL_IR				GENMASK(15, 12)
#define RG_PE1_H_PLL_IC				GENMASK(11, 8)
#define RG_PE1_H_PLL_PREDIV			GENMASK(7, 6)
#define RG_PE1_PLL_DIVEN			GENMASK(3, 1)

#define RG_PE1_H_PLL_FBKSEL_REG			0x4bc
#define RG_PE1_H_PLL_FBKSEL			GENMASK(5, 4)

#define	RG_PE1_H_LCDDS_SSC_PRD_REG		0x4a4
#define RG_PE1_H_LCDDS_SSC_PRD			GENMASK(15, 0)

#define RG_PE1_H_LCDDS_SSC_DELTA_REG		0x4a8
#define RG_PE1_H_LCDDS_SSC_DELTA		GENMASK(11, 0)
#define RG_PE1_H_LCDDS_SSC_DELTA1		GENMASK(27, 16)

#define RG_PE1_LCDDS_CLK_PH_INV_REG		0x4a0
#define RG_PE1_LCDDS_CLK_PH_INV			BIT(5)

#define RG_PE1_H_PLL_BR_REG			0x4ac
#define RG_PE1_H_PLL_BR				GENMASK(18, 16)

#define	RG_PE1_MSTCKDIV_REG			0x414
#define RG_PE1_MSTCKDIV				GENMASK(7, 6)

#define RG_PE1_FRC_MSTCKDIV			BIT(5)

#define MAX_PHYS	2

/**
 * struct mt7621_pci_phy - Mt7621 Pcie PHY core
 * @dev: pointer to device
 * @regmap: kernel regmap pointer
 * @phy: pointer to the kernel PHY device
 * @sys_clk: pointer to the system XTAL clock
 * @port_base: base register
 * @has_dual_port: if the phy has dual ports.
 * @bypass_pipe_rst: mark if 'mt7621_bypass_pipe_rst'
 * needs to be executed. Depends on chip revision.
 */
struct mt7621_pci_phy {
	struct device *dev;
	struct regmap *regmap;
	struct phy *phy;
	struct clk *sys_clk;
	void __iomem *port_base;
	bool has_dual_port;
	bool bypass_pipe_rst;
};

static inline void mt7621_phy_rmw(struct mt7621_pci_phy *phy,
				  u32 reg, u32 clr, u32 set)
{
	u32 val;

	/*
	 * We cannot use 'regmap_write_bits' here because internally
	 * 'set' is masked before is set to the value that will be
	 * written to the register. That way results in no reliable
	 * pci setup. Avoid to mask 'set' before set value to 'val'
	 * completely avoid the problem.
	 */
	regmap_read(phy->regmap, reg, &val);
	val &= ~clr;
	val |= set;
	regmap_write(phy->regmap, reg, val);
}

static void mt7621_bypass_pipe_rst(struct mt7621_pci_phy *phy)
{
	mt7621_phy_rmw(phy, RG_PE1_PIPE_REG, 0, RG_PE1_PIPE_RST);
	mt7621_phy_rmw(phy, RG_PE1_PIPE_REG, 0, RG_PE1_PIPE_CMD_FRC);

	if (phy->has_dual_port) {
		mt7621_phy_rmw(phy, RG_PE1_PIPE_REG + RG_P0_TO_P1_WIDTH,
			       0, RG_PE1_PIPE_RST);
		mt7621_phy_rmw(phy, RG_PE1_PIPE_REG + RG_P0_TO_P1_WIDTH,
			       0, RG_PE1_PIPE_CMD_FRC);
	}
}

static int mt7621_set_phy_for_ssc(struct mt7621_pci_phy *phy)
{
	struct device *dev = phy->dev;
	unsigned long clk_rate;

	clk_rate = clk_get_rate(phy->sys_clk);
	if (!clk_rate)
		return -EINVAL;

	/* Set PCIe Port PHY to disable SSC */
	/* Debug Xtal Type */
	mt7621_phy_rmw(phy, RG_PE1_FRC_H_XTAL_REG,
		       RG_PE1_FRC_H_XTAL_TYPE | RG_PE1_H_XTAL_TYPE,
		       RG_PE1_FRC_H_XTAL_TYPE |
		       FIELD_PREP(RG_PE1_H_XTAL_TYPE, 0x00));

	/* disable port */
	mt7621_phy_rmw(phy, RG_PE1_FRC_PHY_REG, RG_PE1_PHY_EN,
		       RG_PE1_FRC_PHY_EN);

	if (phy->has_dual_port) {
		mt7621_phy_rmw(phy, RG_PE1_FRC_PHY_REG + RG_P0_TO_P1_WIDTH,
			       RG_PE1_PHY_EN, RG_PE1_FRC_PHY_EN);
	}

	if (clk_rate == 40000000) { /* 40MHz Xtal */
		/* Set Pre-divider ratio (for host mode) */
		mt7621_phy_rmw(phy, RG_PE1_H_PLL_REG, RG_PE1_H_PLL_PREDIV,
			       FIELD_PREP(RG_PE1_H_PLL_PREDIV, 0x01));

		dev_dbg(dev, "Xtal is 40MHz\n");
	} else if (clk_rate == 25000000) { /* 25MHz Xal */
		mt7621_phy_rmw(phy, RG_PE1_H_PLL_REG, RG_PE1_H_PLL_PREDIV,
			       FIELD_PREP(RG_PE1_H_PLL_PREDIV, 0x00));

		/* Select feedback clock */
		mt7621_phy_rmw(phy, RG_PE1_H_PLL_FBKSEL_REG,
			       RG_PE1_H_PLL_FBKSEL,
			       FIELD_PREP(RG_PE1_H_PLL_FBKSEL, 0x01));

		/* DDS NCPO PCW (for host mode) */
		mt7621_phy_rmw(phy, RG_PE1_H_LCDDS_SSC_PRD_REG,
			       RG_PE1_H_LCDDS_SSC_PRD,
			       FIELD_PREP(RG_PE1_H_LCDDS_SSC_PRD, 0x00));

		/* DDS SSC dither period control */
		mt7621_phy_rmw(phy, RG_PE1_H_LCDDS_SSC_PRD_REG,
			       RG_PE1_H_LCDDS_SSC_PRD,
			       FIELD_PREP(RG_PE1_H_LCDDS_SSC_PRD, 0x18d));

		/* DDS SSC dither amplitude control */
		mt7621_phy_rmw(phy, RG_PE1_H_LCDDS_SSC_DELTA_REG,
			       RG_PE1_H_LCDDS_SSC_DELTA |
			       RG_PE1_H_LCDDS_SSC_DELTA1,
			       FIELD_PREP(RG_PE1_H_LCDDS_SSC_DELTA, 0x4a) |
			       FIELD_PREP(RG_PE1_H_LCDDS_SSC_DELTA1, 0x4a));

		dev_dbg(dev, "Xtal is 25MHz\n");
	} else { /* 20MHz Xtal */
		mt7621_phy_rmw(phy, RG_PE1_H_PLL_REG, RG_PE1_H_PLL_PREDIV,
			       FIELD_PREP(RG_PE1_H_PLL_PREDIV, 0x00));

		dev_dbg(dev, "Xtal is 20MHz\n");
	}

	/* DDS clock inversion */
	mt7621_phy_rmw(phy, RG_PE1_LCDDS_CLK_PH_INV_REG,
		       RG_PE1_LCDDS_CLK_PH_INV, RG_PE1_LCDDS_CLK_PH_INV);

	/* Set PLL bits */
	mt7621_phy_rmw(phy, RG_PE1_H_PLL_REG,
		       RG_PE1_H_PLL_BC | RG_PE1_H_PLL_BP | RG_PE1_H_PLL_IR |
		       RG_PE1_H_PLL_IC | RG_PE1_PLL_DIVEN,
		       FIELD_PREP(RG_PE1_H_PLL_BC, 0x02) |
		       FIELD_PREP(RG_PE1_H_PLL_BP, 0x06) |
		       FIELD_PREP(RG_PE1_H_PLL_IR, 0x02) |
		       FIELD_PREP(RG_PE1_H_PLL_IC, 0x01) |
		       FIELD_PREP(RG_PE1_PLL_DIVEN, 0x02));

	mt7621_phy_rmw(phy, RG_PE1_H_PLL_BR_REG, RG_PE1_H_PLL_BR,
		       FIELD_PREP(RG_PE1_H_PLL_BR, 0x00));

	if (clk_rate == 40000000) { /* 40MHz Xtal */
		/* set force mode enable of da_pe1_mstckdiv */
		mt7621_phy_rmw(phy, RG_PE1_MSTCKDIV_REG,
			       RG_PE1_MSTCKDIV | RG_PE1_FRC_MSTCKDIV,
			       FIELD_PREP(RG_PE1_MSTCKDIV, 0x01) |
			       RG_PE1_FRC_MSTCKDIV);
	}

	return 0;
}

static int mt7621_pci_phy_init(struct phy *phy)
{
	struct mt7621_pci_phy *mphy = phy_get_drvdata(phy);

	if (mphy->bypass_pipe_rst)
		mt7621_bypass_pipe_rst(mphy);

	return mt7621_set_phy_for_ssc(mphy);
}

static int mt7621_pci_phy_power_on(struct phy *phy)
{
	struct mt7621_pci_phy *mphy = phy_get_drvdata(phy);

	/* Enable PHY and disable force mode */
	mt7621_phy_rmw(mphy, RG_PE1_FRC_PHY_REG,
		       RG_PE1_FRC_PHY_EN, RG_PE1_PHY_EN);

	if (mphy->has_dual_port) {
		mt7621_phy_rmw(mphy, RG_PE1_FRC_PHY_REG + RG_P0_TO_P1_WIDTH,
			       RG_PE1_FRC_PHY_EN, RG_PE1_PHY_EN);
	}

	return 0;
}

static int mt7621_pci_phy_power_off(struct phy *phy)
{
	struct mt7621_pci_phy *mphy = phy_get_drvdata(phy);

	/* Disable PHY */
	mt7621_phy_rmw(mphy, RG_PE1_FRC_PHY_REG,
		       RG_PE1_PHY_EN, RG_PE1_FRC_PHY_EN);

	if (mphy->has_dual_port) {
		mt7621_phy_rmw(mphy, RG_PE1_FRC_PHY_REG + RG_P0_TO_P1_WIDTH,
			       RG_PE1_PHY_EN, RG_PE1_FRC_PHY_EN);
	}

	return 0;
}

static int mt7621_pci_phy_exit(struct phy *phy)
{
	return 0;
}

static const struct phy_ops mt7621_pci_phy_ops = {
	.init		= mt7621_pci_phy_init,
	.exit		= mt7621_pci_phy_exit,
	.power_on	= mt7621_pci_phy_power_on,
	.power_off	= mt7621_pci_phy_power_off,
	.owner		= THIS_MODULE,
};

static struct phy *mt7621_pcie_phy_of_xlate(struct device *dev,
					    struct of_phandle_args *args)
{
	struct mt7621_pci_phy *mt7621_phy = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] >= MAX_PHYS))
		return ERR_PTR(-ENODEV);

	mt7621_phy->has_dual_port = args->args[0];

	dev_dbg(dev, "PHY for 0x%px (dual port = %d)\n",
		mt7621_phy->port_base, mt7621_phy->has_dual_port);

	return mt7621_phy->phy;
}

static const struct soc_device_attribute mt7621_pci_quirks_match[] = {
	{ .soc_id = "mt7621", .revision = "E2" },
	{ /* sentinel */ }
};

static const struct regmap_config mt7621_pci_phy_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x700,
};

static int mt7621_pci_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct soc_device_attribute *attr;
	struct phy_provider *provider;
	struct mt7621_pci_phy *phy;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	attr = soc_device_match(mt7621_pci_quirks_match);
	if (attr)
		phy->bypass_pipe_rst = true;

	phy->dev = dev;
	platform_set_drvdata(pdev, phy);

	phy->port_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->port_base)) {
		dev_err(dev, "failed to remap phy regs\n");
		return PTR_ERR(phy->port_base);
	}

	phy->regmap = devm_regmap_init_mmio(phy->dev, phy->port_base,
					    &mt7621_pci_phy_regmap_config);
	if (IS_ERR(phy->regmap))
		return PTR_ERR(phy->regmap);

	phy->phy = devm_phy_create(dev, dev->of_node, &mt7621_pci_phy_ops);
	if (IS_ERR(phy->phy)) {
		dev_err(dev, "failed to create phy\n");
		return PTR_ERR(phy->phy);
	}

	phy->sys_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(phy->sys_clk)) {
		dev_err(dev, "failed to get phy clock\n");
		return PTR_ERR(phy->sys_clk);
	}

	phy_set_drvdata(phy->phy, phy);

	provider = devm_of_phy_provider_register(dev, mt7621_pcie_phy_of_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id mt7621_pci_phy_ids[] = {
	{ .compatible = "mediatek,mt7621-pci-phy" },
	{},
};
MODULE_DEVICE_TABLE(of, mt7621_pci_phy_ids);

static struct platform_driver mt7621_pci_phy_driver = {
	.probe = mt7621_pci_phy_probe,
	.driver = {
		.name = "mt7621-pci-phy",
		.of_match_table = mt7621_pci_phy_ids,
	},
};

builtin_platform_driver(mt7621_pci_phy_driver);

MODULE_AUTHOR("Sergio Paracuellos <sergio.paracuellos@gmail.com>");
MODULE_DESCRIPTION("MediaTek MT7621 PCIe PHY driver");
MODULE_LICENSE("GPL v2");
