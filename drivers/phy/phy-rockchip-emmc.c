/*
 * Rockchip emmc PHY driver
 *
 * Copyright (C) 2016 Shawn Lin <shawn.lin@rock-chips.com>
 * Copyright (C) 2016 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/*
 * The higher 16-bit of this register is used for write protection
 * only if BIT(x + 16) set to 1 the BIT(x) can be written.
 */
#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

/* Register definition */
#define GRF_EMMCPHY_CON0	0x0
#define GRF_EMMCPHY_CON1	0x4
#define GRF_EMMCPHY_CON2	0x8
#define GRF_EMMCPHY_CON3	0xc
#define GRF_EMMCPHY_CON4	0x10
#define GRF_EMMCPHY_CON5	0x14
#define GRF_EMMCPHY_CON6	0x18
#define GRF_EMMCPHY_STATUS	0x20

#define PHYCTRL_PDB_MASK	0x1
#define PHYCTRL_PDB_SHIFT	0x0
#define PHYCTRL_PDB_PWR_ON	0x1
#define PHYCTRL_PDB_PWR_OFF	0x0
#define PHYCTRL_ENDLL_MASK	0x1
#define PHYCTRL_ENDLL_SHIFT     0x1
#define PHYCTRL_ENDLL_ENABLE	0x1
#define PHYCTRL_ENDLL_DISABLE	0x0
#define PHYCTRL_CALDONE_MASK	0x1
#define PHYCTRL_CALDONE_SHIFT   0x6
#define PHYCTRL_CALDONE_DONE	0x1
#define PHYCTRL_CALDONE_GOING	0x0
#define PHYCTRL_DLLRDY_MASK	0x1
#define PHYCTRL_DLLRDY_SHIFT	0x5
#define PHYCTRL_DLLRDY_DONE	0x1
#define PHYCTRL_DLLRDY_GOING	0x0

struct rockchip_emmc_phy {
	unsigned int	reg_offset;
	struct regmap	*reg_base;
};

static int rockchip_emmc_phy_power(struct rockchip_emmc_phy *rk_phy,
				   bool on_off)
{
	unsigned int caldone;
	unsigned int dllrdy;

	/*
	 * Keep phyctrl_pdb and phyctrl_endll low to allow
	 * initialization of CALIO state M/C DFFs
	 */
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(PHYCTRL_PDB_PWR_OFF,
				   PHYCTRL_PDB_MASK,
				   PHYCTRL_PDB_SHIFT));
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(PHYCTRL_ENDLL_DISABLE,
				   PHYCTRL_ENDLL_MASK,
				   PHYCTRL_ENDLL_SHIFT));

	/* Already finish power_off above */
	if (on_off == PHYCTRL_PDB_PWR_OFF)
		return 0;

	/*
	 * According to the user manual, calpad calibration
	 * cycle takes more than 2us without the minimal recommended
	 * value, so we may need a little margin here
	 */
	udelay(3);
	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(PHYCTRL_PDB_PWR_ON,
				   PHYCTRL_PDB_MASK,
				   PHYCTRL_PDB_SHIFT));

	/*
	 * According to the user manual, it asks driver to
	 * wait 5us for calpad busy trimming
	 */
	udelay(5);
	regmap_read(rk_phy->reg_base,
		    rk_phy->reg_offset + GRF_EMMCPHY_STATUS,
		    &caldone);
	caldone = (caldone >> PHYCTRL_CALDONE_SHIFT) & PHYCTRL_CALDONE_MASK;
	if (caldone != PHYCTRL_CALDONE_DONE) {
		pr_err("rockchip_emmc_phy_power: caldone timeout.\n");
		return -ETIMEDOUT;
	}

	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(PHYCTRL_ENDLL_ENABLE,
				   PHYCTRL_ENDLL_MASK,
				   PHYCTRL_ENDLL_SHIFT));
	/*
	 * After enable analog DLL circuits, we need extra 10.2us
	 * for dll to be ready for work.
	 */
	udelay(11);
	regmap_read(rk_phy->reg_base,
		    rk_phy->reg_offset + GRF_EMMCPHY_STATUS,
		    &dllrdy);
	dllrdy = (dllrdy >> PHYCTRL_DLLRDY_SHIFT) & PHYCTRL_DLLRDY_MASK;
	if (dllrdy != PHYCTRL_DLLRDY_DONE) {
		pr_err("rockchip_emmc_phy_power: dllrdy timeout.\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int rockchip_emmc_phy_power_off(struct phy *phy)
{
	struct rockchip_emmc_phy *rk_phy = phy_get_drvdata(phy);
	int ret = 0;

	/* Power down emmc phy analog blocks */
	ret = rockchip_emmc_phy_power(rk_phy, PHYCTRL_PDB_PWR_OFF);
	if (ret)
		return ret;

	return 0;
}

static int rockchip_emmc_phy_power_on(struct phy *phy)
{
	struct rockchip_emmc_phy *rk_phy = phy_get_drvdata(phy);
	int ret = 0;

	/* Power up emmc phy analog blocks */
	ret = rockchip_emmc_phy_power(rk_phy, PHYCTRL_PDB_PWR_ON);
	if (ret)
		return ret;

	return 0;
}

static const struct phy_ops ops = {
	.power_on	= rockchip_emmc_phy_power_on,
	.power_off	= rockchip_emmc_phy_power_off,
	.owner		= THIS_MODULE,
};

static int rockchip_emmc_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_emmc_phy *rk_phy;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct regmap *grf;
	unsigned int reg_offset;

	grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(dev, "Missing rockchip,grf property\n");
		return PTR_ERR(grf);
	}

	rk_phy = devm_kzalloc(dev, sizeof(*rk_phy), GFP_KERNEL);
	if (!rk_phy)
		return -ENOMEM;

	if (of_property_read_u32(dev->of_node, "reg", &reg_offset)) {
		dev_err(dev, "missing reg property in node %s\n",
			dev->of_node->name);
		return -EINVAL;
	}

	rk_phy->reg_offset = reg_offset;
	rk_phy->reg_base = grf;

	generic_phy = devm_phy_create(dev, dev->of_node, &ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, rk_phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id rockchip_emmc_phy_dt_ids[] = {
	{ .compatible = "rockchip,rk3399-emmc-phy" },
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_emmc_phy_dt_ids);

static struct platform_driver rockchip_emmc_driver = {
	.probe		= rockchip_emmc_phy_probe,
	.driver		= {
		.name	= "rockchip-emmc-phy",
		.of_match_table = rockchip_emmc_phy_dt_ids,
	},
};

module_platform_driver(rockchip_emmc_driver);

MODULE_AUTHOR("Shawn Lin <shawn.lin@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip EMMC PHY driver");
MODULE_LICENSE("GPL v2");
