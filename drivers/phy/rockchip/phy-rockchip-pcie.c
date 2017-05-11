/*
 * Rockchip PCIe PHY driver
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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

/*
 * The higher 16-bit of this register is used for write protection
 * only if BIT(x + 16) set to 1 the BIT(x) can be written.
 */
#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

#define PHY_MAX_LANE_NUM      4
#define PHY_CFG_DATA_SHIFT    7
#define PHY_CFG_ADDR_SHIFT    1
#define PHY_CFG_DATA_MASK     0xf
#define PHY_CFG_ADDR_MASK     0x3f
#define PHY_CFG_RD_MASK       0x3ff
#define PHY_CFG_WR_ENABLE     1
#define PHY_CFG_WR_DISABLE    1
#define PHY_CFG_WR_SHIFT      0
#define PHY_CFG_WR_MASK       1
#define PHY_CFG_PLL_LOCK      0x10
#define PHY_CFG_CLK_TEST      0x10
#define PHY_CFG_CLK_SCC       0x12
#define PHY_CFG_SEPE_RATE     BIT(3)
#define PHY_CFG_PLL_100M      BIT(3)
#define PHY_PLL_LOCKED        BIT(9)
#define PHY_PLL_OUTPUT        BIT(10)
#define PHY_LANE_A_STATUS     0x30
#define PHY_LANE_B_STATUS     0x31
#define PHY_LANE_C_STATUS     0x32
#define PHY_LANE_D_STATUS     0x33
#define PHY_LANE_RX_DET_SHIFT 11
#define PHY_LANE_RX_DET_TH    0x1
#define PHY_LANE_IDLE_OFF     0x1
#define PHY_LANE_IDLE_MASK    0x1
#define PHY_LANE_IDLE_A_SHIFT 3
#define PHY_LANE_IDLE_B_SHIFT 4
#define PHY_LANE_IDLE_C_SHIFT 5
#define PHY_LANE_IDLE_D_SHIFT 6

struct rockchip_pcie_data {
	unsigned int pcie_conf;
	unsigned int pcie_status;
	unsigned int pcie_laneoff;
};

struct rockchip_pcie_phy {
	struct rockchip_pcie_data *phy_data;
	struct regmap *reg_base;
	struct reset_control *phy_rst;
	struct clk *clk_pciephy_ref;
};

static inline void phy_wr_cfg(struct rockchip_pcie_phy *rk_phy,
			      u32 addr, u32 data)
{
	regmap_write(rk_phy->reg_base, rk_phy->phy_data->pcie_conf,
		     HIWORD_UPDATE(data,
				   PHY_CFG_DATA_MASK,
				   PHY_CFG_DATA_SHIFT) |
		     HIWORD_UPDATE(addr,
				   PHY_CFG_ADDR_MASK,
				   PHY_CFG_ADDR_SHIFT));
	udelay(1);
	regmap_write(rk_phy->reg_base, rk_phy->phy_data->pcie_conf,
		     HIWORD_UPDATE(PHY_CFG_WR_ENABLE,
				   PHY_CFG_WR_MASK,
				   PHY_CFG_WR_SHIFT));
	udelay(1);
	regmap_write(rk_phy->reg_base, rk_phy->phy_data->pcie_conf,
		     HIWORD_UPDATE(PHY_CFG_WR_DISABLE,
				   PHY_CFG_WR_MASK,
				   PHY_CFG_WR_SHIFT));
}

static inline u32 phy_rd_cfg(struct rockchip_pcie_phy *rk_phy,
			     u32 addr)
{
	u32 val;

	regmap_write(rk_phy->reg_base, rk_phy->phy_data->pcie_conf,
		     HIWORD_UPDATE(addr,
				   PHY_CFG_RD_MASK,
				   PHY_CFG_ADDR_SHIFT));
	regmap_read(rk_phy->reg_base,
		    rk_phy->phy_data->pcie_status,
		    &val);
	return val;
}

static int rockchip_pcie_phy_power_off(struct phy *phy)
{
	struct rockchip_pcie_phy *rk_phy = phy_get_drvdata(phy);
	int err = 0;

	err = reset_control_assert(rk_phy->phy_rst);
	if (err) {
		pr_err("assert phy_rst err %d\n", err);
		return err;
	}

	return 0;
}

static int rockchip_pcie_phy_power_on(struct phy *phy)
{
	struct rockchip_pcie_phy *rk_phy = phy_get_drvdata(phy);
	int err = 0;
	u32 status;
	unsigned long timeout;

	err = reset_control_deassert(rk_phy->phy_rst);
	if (err) {
		pr_err("deassert phy_rst err %d\n", err);
		return err;
	}

	regmap_write(rk_phy->reg_base, rk_phy->phy_data->pcie_conf,
		     HIWORD_UPDATE(PHY_CFG_PLL_LOCK,
				   PHY_CFG_ADDR_MASK,
				   PHY_CFG_ADDR_SHIFT));

	/*
	 * No documented timeout value for phy operation below,
	 * so we make it large enough here. And we use loop-break
	 * method which should not be harmful.
	 */
	timeout = jiffies + msecs_to_jiffies(1000);

	err = -EINVAL;
	while (time_before(jiffies, timeout)) {
		regmap_read(rk_phy->reg_base,
			    rk_phy->phy_data->pcie_status,
			    &status);
		if (status & PHY_PLL_LOCKED) {
			pr_debug("pll locked!\n");
			err = 0;
			break;
		}
		msleep(20);
	}

	if (err) {
		pr_err("pll lock timeout!\n");
		goto err_pll_lock;
	}

	phy_wr_cfg(rk_phy, PHY_CFG_CLK_TEST, PHY_CFG_SEPE_RATE);
	phy_wr_cfg(rk_phy, PHY_CFG_CLK_SCC, PHY_CFG_PLL_100M);

	err = -ETIMEDOUT;
	while (time_before(jiffies, timeout)) {
		regmap_read(rk_phy->reg_base,
			    rk_phy->phy_data->pcie_status,
			    &status);
		if (!(status & PHY_PLL_OUTPUT)) {
			pr_debug("pll output enable done!\n");
			err = 0;
			break;
		}
		msleep(20);
	}

	if (err) {
		pr_err("pll output enable timeout!\n");
		goto err_pll_lock;
	}

	regmap_write(rk_phy->reg_base, rk_phy->phy_data->pcie_conf,
		     HIWORD_UPDATE(PHY_CFG_PLL_LOCK,
				   PHY_CFG_ADDR_MASK,
				   PHY_CFG_ADDR_SHIFT));
	err = -EINVAL;
	while (time_before(jiffies, timeout)) {
		regmap_read(rk_phy->reg_base,
			    rk_phy->phy_data->pcie_status,
			    &status);
		if (status & PHY_PLL_LOCKED) {
			pr_debug("pll relocked!\n");
			err = 0;
			break;
		}
		msleep(20);
	}

	if (err) {
		pr_err("pll relock timeout!\n");
		goto err_pll_lock;
	}

	return 0;

err_pll_lock:
	reset_control_assert(rk_phy->phy_rst);
	return err;
}

static int rockchip_pcie_phy_init(struct phy *phy)
{
	struct rockchip_pcie_phy *rk_phy = phy_get_drvdata(phy);
	int err = 0;

	err = clk_prepare_enable(rk_phy->clk_pciephy_ref);
	if (err) {
		pr_err("Fail to enable pcie ref clock.\n");
		goto err_refclk;
	}

	err = reset_control_assert(rk_phy->phy_rst);
	if (err) {
		pr_err("assert phy_rst err %d\n", err);
		goto err_reset;
	}

	return err;

err_reset:
	clk_disable_unprepare(rk_phy->clk_pciephy_ref);
err_refclk:
	return err;
}

static int rockchip_pcie_phy_exit(struct phy *phy)
{
	struct rockchip_pcie_phy *rk_phy = phy_get_drvdata(phy);
	int err = 0;

	clk_disable_unprepare(rk_phy->clk_pciephy_ref);

	err = reset_control_deassert(rk_phy->phy_rst);
	if (err) {
		pr_err("deassert phy_rst err %d\n", err);
		goto err_reset;
	}

	return err;

err_reset:
	clk_prepare_enable(rk_phy->clk_pciephy_ref);
	return err;
}

static const struct phy_ops ops = {
	.init		= rockchip_pcie_phy_init,
	.exit		= rockchip_pcie_phy_exit,
	.power_on	= rockchip_pcie_phy_power_on,
	.power_off	= rockchip_pcie_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct rockchip_pcie_data rk3399_pcie_data = {
	.pcie_conf = 0xe220,
	.pcie_status = 0xe2a4,
	.pcie_laneoff = 0xe214,
};

static const struct of_device_id rockchip_pcie_phy_dt_ids[] = {
	{
		.compatible = "rockchip,rk3399-pcie-phy",
		.data = &rk3399_pcie_data,
	},
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_pcie_phy_dt_ids);

static int rockchip_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_pcie_phy *rk_phy;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct regmap *grf;
	const struct of_device_id *of_id;

	grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(dev, "Missing rockchip,grf property\n");
		return PTR_ERR(grf);
	}

	rk_phy = devm_kzalloc(dev, sizeof(*rk_phy), GFP_KERNEL);
	if (!rk_phy)
		return -ENOMEM;

	of_id = of_match_device(rockchip_pcie_phy_dt_ids, &pdev->dev);
	if (!of_id)
		return -EINVAL;

	rk_phy->phy_data = (struct rockchip_pcie_data *)of_id->data;
	rk_phy->reg_base = grf;

	rk_phy->phy_rst = devm_reset_control_get(dev, "phy");
	if (IS_ERR(rk_phy->phy_rst)) {
		if (PTR_ERR(rk_phy->phy_rst) != -EPROBE_DEFER)
			dev_err(dev,
				"missing phy property for reset controller\n");
		return PTR_ERR(rk_phy->phy_rst);
	}

	rk_phy->clk_pciephy_ref = devm_clk_get(dev, "refclk");
	if (IS_ERR(rk_phy->clk_pciephy_ref)) {
		dev_err(dev, "refclk not found.\n");
		return PTR_ERR(rk_phy->clk_pciephy_ref);
	}

	generic_phy = devm_phy_create(dev, dev->of_node, &ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, rk_phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver rockchip_pcie_driver = {
	.probe		= rockchip_pcie_phy_probe,
	.driver		= {
		.name	= "rockchip-pcie-phy",
		.of_match_table = rockchip_pcie_phy_dt_ids,
	},
};

module_platform_driver(rockchip_pcie_driver);

MODULE_AUTHOR("Shawn Lin <shawn.lin@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip PCIe PHY driver");
MODULE_LICENSE("GPL v2");
