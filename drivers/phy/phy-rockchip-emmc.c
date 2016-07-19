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
#include <linux/io.h>
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
#define GRF_EMMCPHY_CON0		0x0
#define GRF_EMMCPHY_CON1		0x4
#define GRF_EMMCPHY_CON2		0x8
#define GRF_EMMCPHY_CON3		0xc
#define GRF_EMMCPHY_CON4		0x10
#define GRF_EMMCPHY_CON5		0x14
#define GRF_EMMCPHY_CON6		0x18
#define GRF_EMMCPHY_STATUS		0x20
#define CTRL_OFFSET			0x2c

#define CTRL_INTER_CLKEN		0x1
#define CTRL_INTER_CLKRDY		0x2
#define CTRL_INTER_CLKOUT		0x4
#define PHYCTRL_PDB_MASK		0x1
#define PHYCTRL_PDB_SHIFT		0x0
#define PHYCTRL_PDB_PWR_ON		0x1
#define PHYCTRL_PDB_PWR_OFF		0x0
#define PHYCTRL_ENDLL_MASK		0x1
#define PHYCTRL_ENDLL_SHIFT		0x1
#define PHYCTRL_ENDLL_ENABLE		0x1
#define PHYCTRL_ENDLL_DISABLE		0x0
#define PHYCTRL_CALDONE_MASK		0x1
#define PHYCTRL_CALDONE_SHIFT		0x6
#define PHYCTRL_CALDONE_DONE		0x1
#define PHYCTRL_CALDONE_GOING		0x0
#define PHYCTRL_DLLRDY_MASK		0x1
#define PHYCTRL_DLLRDY_SHIFT		0x5
#define PHYCTRL_DLLRDY_DONE		0x1
#define PHYCTRL_DLLRDY_GOING		0x0
#define PHYCTRL_FREQSEL_200M		0x0
#define PHYCTRL_FREQSEL_50M		0x1
#define PHYCTRL_FREQSEL_100M		0x2
#define PHYCTRL_FREQSEL_150M		0x3
#define PHYCTRL_FREQSEL_MASK		0x3
#define PHYCTRL_FREQSEL_SHIFT		0xc
#define PHYCTRL_DR_MASK			0x7
#define PHYCTRL_DR_SHIFT		0x4
#define PHYCTRL_DR_50OHM		0x0
#define PHYCTRL_DR_33OHM		0x1
#define PHYCTRL_DR_66OHM		0x2
#define PHYCTRL_DR_100OHM		0x3
#define PHYCTRL_DR_40OHM		0x4
#define PHYCTRL_OTAPDLYENA		0x1
#define PHYCTRL_OTAPDLYENA_MASK		0x1
#define PHYCTRL_OTAPDLYENA_SHIFT	11
#define PHYCTRL_OTAPDLYSEL_MASK		0xf
#define PHYCTRL_OTAPDLYSEL_SHIFT	7
#define PHYCTRL_REN_STRB_ENABLE		0x1
#define PHYCTRL_REN_STRB_MASK		0x1
#define PHYCTRL_REN_STRB_SHIFT		9

struct rockchip_emmc_phy {
	unsigned int	reg_offset;
	struct regmap	*reg_base;
	void __iomem *ctrl_base;
	u32	freq_sel;
	u32	dr_sel;
	u32	opdelay;
};

static int rockchip_emmc_phy_power(struct rockchip_emmc_phy *rk_phy,
				   bool on_off)
{
	unsigned int caldone;
	unsigned int dllrdy;
	u16 ctrl_val;
	unsigned long timeout;

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

	ctrl_val = readw(rk_phy->ctrl_base + CTRL_OFFSET);
	ctrl_val |= CTRL_INTER_CLKEN;
	writew(ctrl_val, rk_phy->ctrl_base + CTRL_OFFSET);
	/* Wait max 20 ms */
	while (!((ctrl_val = readw(rk_phy->ctrl_base + CTRL_OFFSET))
		& CTRL_INTER_CLKRDY)) {
		if (timeout == 0) {
			pr_err("rockchip_emmc_phy_power_on: inter_clk not rdy\n");
			return -EINVAL;
		}
		timeout--;
		mdelay(1);
	}
	ctrl_val |= CTRL_INTER_CLKOUT;
	writew(ctrl_val, rk_phy->ctrl_base + CTRL_OFFSET);

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
	 * for dll to be ready for work. But according to the test, we
	 * find some chips need more than 25us.
	 */
	udelay(30);
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

static int rockchip_emmc_phy_init(struct phy *phy)
{
	struct rockchip_emmc_phy *rk_phy = phy_get_drvdata(phy);

	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON0,
		     HIWORD_UPDATE(rk_phy->freq_sel,
				   PHYCTRL_FREQSEL_MASK,
				   PHYCTRL_FREQSEL_SHIFT));

	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON6,
		     HIWORD_UPDATE(rk_phy->dr_sel,
				   PHYCTRL_DR_MASK,
				   PHYCTRL_DR_SHIFT));

	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON0,
		     HIWORD_UPDATE(PHYCTRL_OTAPDLYENA,
				   PHYCTRL_OTAPDLYENA_MASK,
				   PHYCTRL_OTAPDLYENA_SHIFT));

	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON2,
		     HIWORD_UPDATE(PHYCTRL_REN_STRB_ENABLE,
				   PHYCTRL_REN_STRB_MASK,
				   PHYCTRL_REN_STRB_SHIFT));

	regmap_write(rk_phy->reg_base,
		     rk_phy->reg_offset + GRF_EMMCPHY_CON0,
		     HIWORD_UPDATE(rk_phy->opdelay,
				   PHYCTRL_OTAPDLYSEL_MASK,
				   PHYCTRL_OTAPDLYSEL_SHIFT));

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
	.init		= rockchip_emmc_phy_init,
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
	u32 freq_sel;
	u32 dr_sel;
	u32 opdelay;
	u32 ctrl_base;

	grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(dev, "Missing rockchip,grf property\n");
		return PTR_ERR(grf);
	}

	rk_phy = devm_kzalloc(dev, sizeof(*rk_phy), GFP_KERNEL);
	if (!rk_phy)
		return -ENOMEM;

	if (of_property_read_u32(dev->of_node, "reg-offset", &reg_offset)) {
		dev_err(dev, "missing reg property in node %s\n",
			dev->of_node->name);
		return -EINVAL;
	}

	if (of_property_read_u32(dev->of_node, "ctrl-base", &ctrl_base)) {
		dev_err(dev, "missing ctrl-base property in node %s\n",
			dev->of_node->name);
		return -EINVAL;
	}

	rk_phy->ctrl_base = ioremap(ctrl_base, SZ_1K);
	if (!rk_phy->ctrl_base) {
		dev_err(dev, "failed to remap ctrl_base!\n");
		return -ENOMEM;
	}

	rk_phy->freq_sel = 0x0;
	if (!of_property_read_u32(dev->of_node, "freq-sel", &freq_sel)) {
		switch (freq_sel) {
		case 50000000:
			rk_phy->freq_sel = PHYCTRL_FREQSEL_50M;
			break;
		case 100000000:
			rk_phy->freq_sel = PHYCTRL_FREQSEL_100M;
			break;
		case 150000000:
			rk_phy->freq_sel = PHYCTRL_FREQSEL_150M;
			break;
		case 200000000:
			rk_phy->freq_sel = PHYCTRL_FREQSEL_200M;
			break;
		default:
			dev_info(dev, "Not support freq_sel, default 200M\n");
			break;
		}
	}

	rk_phy->dr_sel = 0x0;
	if (!of_property_read_u32(dev->of_node, "dr-sel", &dr_sel)) {
		switch (dr_sel) {
		case 50:
			rk_phy->dr_sel = PHYCTRL_DR_50OHM;
			break;
		case 33:
			rk_phy->dr_sel = PHYCTRL_DR_33OHM;
			break;
		case 66:
			rk_phy->dr_sel = PHYCTRL_DR_66OHM;
			break;
		case 100:
			rk_phy->dr_sel = PHYCTRL_DR_100OHM;
			break;
		case 40:
			rk_phy->dr_sel = PHYCTRL_DR_40OHM;
			break;
		default:
			dev_info(dev, "Not support dr_sel, default 50OHM\n");
			break;
		}
	}

	rk_phy->opdelay = 0x4;
	if (!of_property_read_u32(dev->of_node, "opdelay", &opdelay)) {
		if (opdelay > 15)
			dev_info(dev, "opdelay shouldn't larger than 15\n");
		else
			rk_phy->opdelay = opdelay;
	}

	rk_phy->reg_offset = reg_offset;
	rk_phy->reg_base = grf;

	generic_phy = devm_phy_create(dev, dev->of_node, &ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		iounmap(rk_phy->ctrl_base);
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
