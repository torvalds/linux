// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2018 Broadcom
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* we have up to 8 PAXB based RC. The 9th one is always PAXC */
#define SR_NR_PCIE_PHYS               9
#define SR_PAXC_PHY_IDX               (SR_NR_PCIE_PHYS - 1)

#define PCIE_PIPEMUX_CFG_OFFSET       0x10c
#define PCIE_PIPEMUX_SELECT_STRAP     0xf

#define CDRU_STRAP_DATA_LSW_OFFSET    0x5c
#define PCIE_PIPEMUX_SHIFT            19
#define PCIE_PIPEMUX_MASK             0xf

#define MHB_MEM_PW_PAXC_OFFSET        0x1c0
#define MHB_PWR_ARR_POWERON           0x8
#define MHB_PWR_ARR_POWEROK           0x4
#define MHB_PWR_POWERON               0x2
#define MHB_PWR_POWEROK               0x1
#define MHB_PWR_STATUS_MASK           (MHB_PWR_ARR_POWERON | \
				       MHB_PWR_ARR_POWEROK | \
				       MHB_PWR_POWERON | \
				       MHB_PWR_POWEROK)

struct sr_pcie_phy_core;

/**
 * struct sr_pcie_phy - Stingray PCIe PHY
 *
 * @core: pointer to the Stingray PCIe PHY core control
 * @index: PHY index
 * @phy: pointer to the kernel PHY device
 */
struct sr_pcie_phy {
	struct sr_pcie_phy_core *core;
	unsigned int index;
	struct phy *phy;
};

/**
 * struct sr_pcie_phy_core - Stingray PCIe PHY core control
 *
 * @dev: pointer to device
 * @base: base register of PCIe SS
 * @cdru: regmap to the CDRU device
 * @mhb: regmap to the MHB device
 * @pipemux: pipemuex strap
 * @phys: array of PCIe PHYs
 */
struct sr_pcie_phy_core {
	struct device *dev;
	void __iomem *base;
	struct regmap *cdru;
	struct regmap *mhb;
	u32 pipemux;
	struct sr_pcie_phy phys[SR_NR_PCIE_PHYS];
};

/*
 * PCIe PIPEMUX lookup table
 *
 * Each array index represents a PIPEMUX strap setting
 * The array element represents a bitmap where a set bit means the PCIe
 * core and associated serdes has been enabled as RC and is available for use
 */
static const u8 pipemux_table[] = {
	/* PIPEMUX = 0, EP 1x16 */
	0x00,
	/* PIPEMUX = 1, EP 1x8 + RC 1x8, core 7 */
	0x80,
	/* PIPEMUX = 2, EP 4x4 */
	0x00,
	/* PIPEMUX = 3, RC 2x8, cores 0, 7 */
	0x81,
	/* PIPEMUX = 4, RC 4x4, cores 0, 1, 6, 7 */
	0xc3,
	/* PIPEMUX = 5, RC 8x2, all 8 cores */
	0xff,
	/* PIPEMUX = 6, RC 3x4 + 2x2, cores 0, 2, 3, 6, 7 */
	0xcd,
	/* PIPEMUX = 7, RC 1x4 + 6x2, cores 0, 2, 3, 4, 5, 6, 7 */
	0xfd,
	/* PIPEMUX = 8, EP 1x8 + RC 4x2, cores 4, 5, 6, 7 */
	0xf0,
	/* PIPEMUX = 9, EP 1x8 + RC 2x4, cores 6, 7 */
	0xc0,
	/* PIPEMUX = 10, EP 2x4 + RC 2x4, cores 1, 6 */
	0x42,
	/* PIPEMUX = 11, EP 2x4 + RC 4x2, cores 2, 3, 4, 5 */
	0x3c,
	/* PIPEMUX = 12, EP 1x4 + RC 6x2, cores 2, 3, 4, 5, 6, 7 */
	0xfc,
	/* PIPEMUX = 13, RC 2x4 + RC 1x4 + 2x2, cores 2, 3, 6 */
	0x4c,
};

/*
 * Return true if the strap setting is valid
 */
static bool pipemux_strap_is_valid(u32 pipemux)
{
	return !!(pipemux < ARRAY_SIZE(pipemux_table));
}

/*
 * Read the PCIe PIPEMUX from strap
 */
static u32 pipemux_strap_read(struct sr_pcie_phy_core *core)
{
	u32 pipemux;

	/*
	 * Read PIPEMUX configuration register to determine the pipemux setting
	 *
	 * In the case when the value indicates using HW strap, fall back to
	 * use HW strap
	 */
	pipemux = readl(core->base + PCIE_PIPEMUX_CFG_OFFSET);
	pipemux &= PCIE_PIPEMUX_MASK;
	if (pipemux == PCIE_PIPEMUX_SELECT_STRAP) {
		regmap_read(core->cdru, CDRU_STRAP_DATA_LSW_OFFSET, &pipemux);
		pipemux >>= PCIE_PIPEMUX_SHIFT;
		pipemux &= PCIE_PIPEMUX_MASK;
	}

	return pipemux;
}

/*
 * Given a PIPEMUX strap and PCIe core index, this function returns true if the
 * PCIe core needs to be enabled
 */
static bool pcie_core_is_for_rc(struct sr_pcie_phy *phy)
{
	struct sr_pcie_phy_core *core = phy->core;
	unsigned int core_idx = phy->index;

	return !!((pipemux_table[core->pipemux] >> core_idx) & 0x1);
}

static int sr_pcie_phy_init(struct phy *p)
{
	struct sr_pcie_phy *phy = phy_get_drvdata(p);

	/*
	 * Check whether this PHY is for root complex or not. If yes, return
	 * zero so the host driver can proceed to enumeration. If not, return
	 * an error and that will force the host driver to bail out
	 */
	if (pcie_core_is_for_rc(phy))
		return 0;

	return -ENODEV;
}

static int sr_paxc_phy_init(struct phy *p)
{
	struct sr_pcie_phy *phy = phy_get_drvdata(p);
	struct sr_pcie_phy_core *core = phy->core;
	unsigned int core_idx = phy->index;
	u32 val;

	if (core_idx != SR_PAXC_PHY_IDX)
		return -EINVAL;

	regmap_read(core->mhb, MHB_MEM_PW_PAXC_OFFSET, &val);
	if ((val & MHB_PWR_STATUS_MASK) != MHB_PWR_STATUS_MASK) {
		dev_err(core->dev, "PAXC is not powered up\n");
		return -ENODEV;
	}

	return 0;
}

static const struct phy_ops sr_pcie_phy_ops = {
	.init = sr_pcie_phy_init,
	.owner = THIS_MODULE,
};

static const struct phy_ops sr_paxc_phy_ops = {
	.init = sr_paxc_phy_init,
	.owner = THIS_MODULE,
};

static struct phy *sr_pcie_phy_xlate(struct device *dev,
				     const struct of_phandle_args *args)
{
	struct sr_pcie_phy_core *core;
	int phy_idx;

	core = dev_get_drvdata(dev);
	if (!core)
		return ERR_PTR(-EINVAL);

	phy_idx = args->args[0];

	if (WARN_ON(phy_idx >= SR_NR_PCIE_PHYS))
		return ERR_PTR(-ENODEV);

	return core->phys[phy_idx].phy;
}

static int sr_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct sr_pcie_phy_core *core;
	struct phy_provider *provider;
	unsigned int phy_idx = 0;

	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->dev = dev;
	core->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(core->base))
		return PTR_ERR(core->base);

	core->cdru = syscon_regmap_lookup_by_phandle(node, "brcm,sr-cdru");
	if (IS_ERR(core->cdru)) {
		dev_err(core->dev, "unable to find CDRU device\n");
		return PTR_ERR(core->cdru);
	}

	core->mhb = syscon_regmap_lookup_by_phandle(node, "brcm,sr-mhb");
	if (IS_ERR(core->mhb)) {
		dev_err(core->dev, "unable to find MHB device\n");
		return PTR_ERR(core->mhb);
	}

	/* read the PCIe PIPEMUX strap setting */
	core->pipemux = pipemux_strap_read(core);
	if (!pipemux_strap_is_valid(core->pipemux)) {
		dev_err(core->dev, "invalid PCIe PIPEMUX strap %u\n",
			core->pipemux);
		return -EIO;
	}

	for (phy_idx = 0; phy_idx < SR_NR_PCIE_PHYS; phy_idx++) {
		struct sr_pcie_phy *p = &core->phys[phy_idx];
		const struct phy_ops *ops;

		if (phy_idx == SR_PAXC_PHY_IDX)
			ops = &sr_paxc_phy_ops;
		else
			ops = &sr_pcie_phy_ops;

		p->phy = devm_phy_create(dev, NULL, ops);
		if (IS_ERR(p->phy)) {
			dev_err(dev, "failed to create PCIe PHY\n");
			return PTR_ERR(p->phy);
		}

		p->core = core;
		p->index = phy_idx;
		phy_set_drvdata(p->phy, p);
	}

	dev_set_drvdata(dev, core);

	provider = devm_of_phy_provider_register(dev, sr_pcie_phy_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "failed to register PHY provider\n");
		return PTR_ERR(provider);
	}

	return 0;
}

static const struct of_device_id sr_pcie_phy_match_table[] = {
	{ .compatible = "brcm,sr-pcie-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, sr_pcie_phy_match_table);

static struct platform_driver sr_pcie_phy_driver = {
	.driver = {
		.name		= "sr-pcie-phy",
		.of_match_table	= sr_pcie_phy_match_table,
	},
	.probe	= sr_pcie_phy_probe,
};
module_platform_driver(sr_pcie_phy_driver);

MODULE_AUTHOR("Ray Jui <ray.jui@broadcom.com>");
MODULE_DESCRIPTION("Broadcom Stingray PCIe PHY driver");
MODULE_LICENSE("GPL v2");
