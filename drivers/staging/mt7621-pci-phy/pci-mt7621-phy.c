// SPDX-License-Identifier: GPL-2.0+
/*
 * Mediatek MT7621 PCI PHY Driver
 * Author: Sergio Paracuellos <sergio.paracuellos@gmail.com>
 */

#include <dt-bindings/phy/phy.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sys_soc.h>
#include <mt7621.h>
#include <ralink_regs.h>

#define RALINK_CLKCFG1				0x30

#define PCIE_PORT_CLK_EN(x)			BIT(24 + (x))

#define RG_PE1_PIPE_REG				0x02c
#define RG_PE1_PIPE_RST				BIT(12)
#define RG_PE1_PIPE_CMD_FRC			BIT(4)

#define RG_P0_TO_P1_WIDTH			0x100
#define RG_PE1_H_LCDDS_REG			0x49c
#define RG_PE1_H_LCDDS_PCW			GENMASK(30, 0)
#define RG_PE1_H_LCDDS_PCW_VAL(x)		((0x7fffffff & (x)) << 0)

#define RG_PE1_FRC_H_XTAL_REG			0x400
#define RG_PE1_FRC_H_XTAL_TYPE			BIT(8)
#define RG_PE1_H_XTAL_TYPE			GENMASK(10, 9)
#define RG_PE1_H_XTAL_TYPE_VAL(x)		((0x3 & (x)) << 9)

#define RG_PE1_FRC_PHY_REG			0x000
#define RG_PE1_FRC_PHY_EN			BIT(4)
#define RG_PE1_PHY_EN				BIT(5)

#define RG_PE1_H_PLL_REG			0x490
#define RG_PE1_H_PLL_BC				GENMASK(23, 22)
#define RG_PE1_H_PLL_BC_VAL(x)			((0x3 & (x)) << 22)
#define RG_PE1_H_PLL_BP				GENMASK(21, 18)
#define RG_PE1_H_PLL_BP_VAL(x)			((0xf & (x)) << 18)
#define RG_PE1_H_PLL_IR				GENMASK(15, 12)
#define RG_PE1_H_PLL_IR_VAL(x)			((0xf & (x)) << 12)
#define RG_PE1_H_PLL_IC				GENMASK(11, 8)
#define RG_PE1_H_PLL_IC_VAL(x)			((0xf & (x)) << 8)
#define RG_PE1_H_PLL_PREDIV			GENMASK(7, 6)
#define RG_PE1_H_PLL_PREDIV_VAL(x)		((0x3 & (x)) << 6)
#define RG_PE1_PLL_DIVEN			GENMASK(3, 1)
#define RG_PE1_PLL_DIVEN_VAL(x)			((0x7 & (x)) << 1)

#define RG_PE1_H_PLL_FBKSEL_REG			0x4bc
#define RG_PE1_H_PLL_FBKSEL			GENMASK(5, 4)
#define RG_PE1_H_PLL_FBKSEL_VAL(x)		((0x3 & (x)) << 4)

#define	RG_PE1_H_LCDDS_SSC_PRD_REG		0x4a4
#define RG_PE1_H_LCDDS_SSC_PRD			GENMASK(15, 0)
#define RG_PE1_H_LCDDS_SSC_PRD_VAL(x)		((0xffff & (x)) << 0)

#define RG_PE1_H_LCDDS_SSC_DELTA_REG		0x4a8
#define RG_PE1_H_LCDDS_SSC_DELTA		GENMASK(11, 0)
#define RG_PE1_H_LCDDS_SSC_DELTA_VAL(x)		((0xfff & (x)) << 0)
#define RG_PE1_H_LCDDS_SSC_DELTA1		GENMASK(27, 16)
#define RG_PE1_H_LCDDS_SSC_DELTA1_VAL(x)	((0xff & (x)) << 16)

#define RG_PE1_LCDDS_CLK_PH_INV_REG		0x4a0
#define RG_PE1_LCDDS_CLK_PH_INV			BIT(5)

#define RG_PE1_H_PLL_BR_REG			0x4ac
#define RG_PE1_H_PLL_BR				GENMASK(18, 16)
#define RG_PE1_H_PLL_BR_VAL(x)			((0x7 & (x)) << 16)

#define	RG_PE1_MSTCKDIV_REG			0x414
#define RG_PE1_MSTCKDIV				GENMASK(7, 6)
#define RG_PE1_MSTCKDIV_VAL(x)			((0x3 & (x)) << 6)

#define RG_PE1_FRC_MSTCKDIV			BIT(5)

#define MAX_PHYS	2

/**
 * struct mt7621_pci_phy_instance - Mt7621 Pcie PHY device
 * @phy: pointer to the kernel PHY device
 * @port_base: base register
 * @index: internal ID to identify the Mt7621 PCIe PHY
 */
struct mt7621_pci_phy_instance {
	struct phy *phy;
	void __iomem *port_base;
	u32 index;
};

/**
 * struct mt7621_pci_phy - Mt7621 Pcie PHY core
 * @dev: pointer to device
 * @regmap: kernel regmap pointer
 * @phys: pointer to Mt7621 PHY device
 * @nphys: number of PHY devices for this core
 * @bypass_pipe_rst: mark if 'mt7621_bypass_pipe_rst'
 * needs to be executed. Depends on chip revision.
 */
struct mt7621_pci_phy {
	struct device *dev;
	struct regmap *regmap;
	struct mt7621_pci_phy_instance **phys;
	int nphys;
	bool bypass_pipe_rst;
};

static inline u32 phy_read(struct mt7621_pci_phy *phy, u32 reg)
{
	u32 val;

	regmap_read(phy->regmap, reg, &val);

	return val;
}

static inline void phy_write(struct mt7621_pci_phy *phy, u32 val, u32 reg)
{
	regmap_write(phy->regmap, reg, val);
}

static void mt7621_bypass_pipe_rst(struct mt7621_pci_phy *phy,
				   struct mt7621_pci_phy_instance *instance)
{
	u32 offset = (instance->index != 1) ?
		RG_PE1_PIPE_REG : RG_PE1_PIPE_REG + RG_P0_TO_P1_WIDTH;
	u32 reg;

	reg = phy_read(phy, offset);
	reg &= ~(RG_PE1_PIPE_RST | RG_PE1_PIPE_CMD_FRC);
	reg |= (RG_PE1_PIPE_RST | RG_PE1_PIPE_CMD_FRC);
	phy_write(phy, reg, offset);
}

static void mt7621_set_phy_for_ssc(struct mt7621_pci_phy *phy,
				   struct mt7621_pci_phy_instance *instance)
{
	struct device *dev = phy->dev;
	u32 reg = rt_sysc_r32(SYSC_REG_SYSTEM_CONFIG0);
	u32 offset;
	u32 val;

	reg = (reg >> 6) & 0x7;
	/* Set PCIe Port PHY to disable SSC */
	/* Debug Xtal Type */
	val = phy_read(phy, RG_PE1_FRC_H_XTAL_REG);
	val &= ~(RG_PE1_FRC_H_XTAL_TYPE | RG_PE1_H_XTAL_TYPE);
	val |= RG_PE1_FRC_H_XTAL_TYPE;
	val |= RG_PE1_H_XTAL_TYPE_VAL(0x00);
	phy_write(phy, val, RG_PE1_FRC_H_XTAL_REG);

	/* disable port */
	offset = (instance->index != 1) ?
		RG_PE1_FRC_PHY_REG : RG_PE1_FRC_PHY_REG + RG_P0_TO_P1_WIDTH;
	val = phy_read(phy, offset);
	val &= ~(RG_PE1_FRC_PHY_EN | RG_PE1_PHY_EN);
	val |= RG_PE1_FRC_PHY_EN;
	phy_write(phy, val, offset);

	/* Set Pre-divider ratio (for host mode) */
	val = phy_read(phy, RG_PE1_H_PLL_REG);
	val &= ~(RG_PE1_H_PLL_PREDIV);

	if (reg <= 5 && reg >= 3) { /* 40MHz Xtal */
		val |= RG_PE1_H_PLL_PREDIV_VAL(0x01);
		phy_write(phy, val, RG_PE1_H_PLL_REG);
		dev_info(dev, "Xtal is 40MHz\n");
	} else { /* 25MHz | 20MHz Xtal */
		val |= RG_PE1_H_PLL_PREDIV_VAL(0x00);
		phy_write(phy, val, RG_PE1_H_PLL_REG);
		if (reg >= 6) {
			dev_info(dev, "Xtal is 25MHz\n");

			/* Select feedback clock */
			val = phy_read(phy, RG_PE1_H_PLL_FBKSEL_REG);
			val &= ~(RG_PE1_H_PLL_FBKSEL);
			val |= RG_PE1_H_PLL_FBKSEL_VAL(0x01);
			phy_write(phy, val, RG_PE1_H_PLL_FBKSEL_REG);

			/* DDS NCPO PCW (for host mode) */
			val = phy_read(phy, RG_PE1_H_LCDDS_SSC_PRD_REG);
			val &= ~(RG_PE1_H_LCDDS_SSC_PRD);
			val |= RG_PE1_H_LCDDS_SSC_PRD_VAL(0x18000000);
			phy_write(phy, val, RG_PE1_H_LCDDS_SSC_PRD_REG);

			/* DDS SSC dither period control */
			val = phy_read(phy, RG_PE1_H_LCDDS_SSC_PRD_REG);
			val &= ~(RG_PE1_H_LCDDS_SSC_PRD);
			val |= RG_PE1_H_LCDDS_SSC_PRD_VAL(0x18d);
			phy_write(phy, val, RG_PE1_H_LCDDS_SSC_PRD_REG);

			/* DDS SSC dither amplitude control */
			val = phy_read(phy, RG_PE1_H_LCDDS_SSC_DELTA_REG);
			val &= ~(RG_PE1_H_LCDDS_SSC_DELTA |
				 RG_PE1_H_LCDDS_SSC_DELTA1);
			val |= RG_PE1_H_LCDDS_SSC_DELTA_VAL(0x4a);
			val |= RG_PE1_H_LCDDS_SSC_DELTA1_VAL(0x4a);
			phy_write(phy, val, RG_PE1_H_LCDDS_SSC_DELTA_REG);
		} else {
			dev_info(dev, "Xtal is 20MHz\n");
		}
	}

	/* DDS clock inversion */
	val = phy_read(phy, RG_PE1_LCDDS_CLK_PH_INV_REG);
	val &= ~(RG_PE1_LCDDS_CLK_PH_INV);
	val |= RG_PE1_LCDDS_CLK_PH_INV;
	phy_write(phy, val, RG_PE1_LCDDS_CLK_PH_INV_REG);

	/* Set PLL bits */
	val = phy_read(phy, RG_PE1_H_PLL_REG);
	val &= ~(RG_PE1_H_PLL_BC | RG_PE1_H_PLL_BP | RG_PE1_H_PLL_IR |
		 RG_PE1_H_PLL_IC | RG_PE1_PLL_DIVEN);
	val |= RG_PE1_H_PLL_BC_VAL(0x02);
	val |= RG_PE1_H_PLL_BP_VAL(0x06);
	val |= RG_PE1_H_PLL_IR_VAL(0x02);
	val |= RG_PE1_H_PLL_IC_VAL(0x01);
	val |= RG_PE1_PLL_DIVEN_VAL(0x02);
	phy_write(phy, val, RG_PE1_H_PLL_REG);

	val = phy_read(phy, RG_PE1_H_PLL_BR_REG);
	val &= ~(RG_PE1_H_PLL_BR);
	val |= RG_PE1_H_PLL_BR_VAL(0x00);
	phy_write(phy, val, RG_PE1_H_PLL_BR_REG);

	if (reg <= 5 && reg >= 3) { /* 40MHz Xtal */
		/* set force mode enable of da_pe1_mstckdiv */
		val = phy_read(phy, RG_PE1_MSTCKDIV_REG);
		val &= ~(RG_PE1_MSTCKDIV | RG_PE1_FRC_MSTCKDIV);
		val |= (RG_PE1_MSTCKDIV_VAL(0x01) | RG_PE1_FRC_MSTCKDIV);
		phy_write(phy, val, RG_PE1_MSTCKDIV_REG);
	}
}

static int mt7621_pci_phy_init(struct phy *phy)
{
	struct mt7621_pci_phy_instance *instance = phy_get_drvdata(phy);
	struct mt7621_pci_phy *mphy = dev_get_drvdata(phy->dev.parent);

	if (mphy->bypass_pipe_rst)
		mt7621_bypass_pipe_rst(mphy, instance);

	mt7621_set_phy_for_ssc(mphy, instance);

	return 0;
}

static int mt7621_pci_phy_power_on(struct phy *phy)
{
	struct mt7621_pci_phy_instance *instance = phy_get_drvdata(phy);
	struct mt7621_pci_phy *mphy = dev_get_drvdata(phy->dev.parent);
	u32 offset = (instance->index != 1) ?
		RG_PE1_FRC_PHY_REG : RG_PE1_FRC_PHY_REG + RG_P0_TO_P1_WIDTH;
	u32 val;

	/* Enable PHY and disable force mode */
	val = phy_read(mphy, offset);
	val &= ~(RG_PE1_FRC_PHY_EN | RG_PE1_PHY_EN);
	val |= (RG_PE1_FRC_PHY_EN | RG_PE1_PHY_EN);
	phy_write(mphy, val, offset);

	return 0;
}

static int mt7621_pci_phy_power_off(struct phy *phy)
{
	struct mt7621_pci_phy_instance *instance = phy_get_drvdata(phy);
	struct mt7621_pci_phy *mphy = dev_get_drvdata(phy->dev.parent);
	u32 offset = (instance->index != 1) ?
		RG_PE1_FRC_PHY_REG : RG_PE1_FRC_PHY_REG + RG_P0_TO_P1_WIDTH;
	u32 val;

	/* Disable PHY */
	val = phy_read(mphy, offset);
	val &= ~(RG_PE1_FRC_PHY_EN | RG_PE1_PHY_EN);
	val |= RG_PE1_FRC_PHY_EN;
	phy_write(mphy, val, offset);

	return 0;
}

static int mt7621_pci_phy_exit(struct phy *phy)
{
	struct mt7621_pci_phy_instance *instance = phy_get_drvdata(phy);

	rt_sysc_m32(PCIE_PORT_CLK_EN(instance->index), 0, RALINK_CLKCFG1);

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

	if (args->args_count == 0)
		return mt7621_phy->phys[0]->phy;

	if (WARN_ON(args->args[0] >= MAX_PHYS))
		return ERR_PTR(-ENODEV);

	return mt7621_phy->phys[args->args[0]]->phy;
}

static const struct soc_device_attribute mt7621_pci_quirks_match[] = {
	{ .soc_id = "mt7621", .revision = "E2" }
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
	struct resource *res;
	int port;
	void __iomem *port_base;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->nphys = MAX_PHYS;
	phy->phys = devm_kcalloc(dev, phy->nphys,
				 sizeof(*phy->phys), GFP_KERNEL);
	if (!phy->phys)
		return -ENOMEM;

	attr = soc_device_match(mt7621_pci_quirks_match);
	if (attr)
		phy->bypass_pipe_rst = true;

	phy->dev = dev;
	platform_set_drvdata(pdev, phy);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get address resource\n");
		return -ENXIO;
	}

	port_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(port_base)) {
		dev_err(dev, "failed to remap phy regs\n");
		return PTR_ERR(port_base);
	}

	phy->regmap = devm_regmap_init_mmio(phy->dev, port_base,
					    &mt7621_pci_phy_regmap_config);
	if (IS_ERR(phy->regmap))
		return PTR_ERR(phy->regmap);

	for (port = 0; port < MAX_PHYS; port++) {
		struct mt7621_pci_phy_instance *instance;
		struct phy *pphy;

		instance = devm_kzalloc(dev, sizeof(*instance), GFP_KERNEL);
		if (!instance)
			return -ENOMEM;

		phy->phys[port] = instance;

		pphy = devm_phy_create(dev, dev->of_node, &mt7621_pci_phy_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create phy\n");
			return PTR_ERR(phy);
		}

		instance->port_base = port_base;
		instance->phy = pphy;
		instance->index = port;
		phy_set_drvdata(pphy, instance);
	}

	provider = devm_of_phy_provider_register(dev, mt7621_pcie_phy_of_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id mt7621_pci_phy_ids[] = {
	{ .compatible = "mediatek,mt7621-pci-phy" },
	{},
};
MODULE_DEVICE_TABLE(of, mt7621_pci_ids);

static struct platform_driver mt7621_pci_phy_driver = {
	.probe = mt7621_pci_phy_probe,
	.driver = {
		.name = "mt7621-pci-phy",
		.of_match_table = of_match_ptr(mt7621_pci_phy_ids),
	},
};

static int __init mt7621_pci_phy_drv_init(void)
{
	return platform_driver_register(&mt7621_pci_phy_driver);
}

module_init(mt7621_pci_phy_drv_init);

MODULE_AUTHOR("Sergio Paracuellos <sergio.paracuellos@gmail.com>");
MODULE_DESCRIPTION("MediaTek MT7621 PCIe PHY driver");
MODULE_LICENSE("GPL v2");
