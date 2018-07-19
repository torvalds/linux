// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2016 Allwinnertech Co., Ltd.
 * Copyright (C) 2017-2018 Bootlin
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "sun6i_mipi_dsi.h"

#define SUN6I_DPHY_GCTL_REG		0x00
#define SUN6I_DPHY_GCTL_LANE_NUM(n)		((((n) - 1) & 3) << 4)
#define SUN6I_DPHY_GCTL_EN			BIT(0)

#define SUN6I_DPHY_TX_CTL_REG		0x04
#define SUN6I_DPHY_TX_CTL_HS_TX_CLK_CONT	BIT(28)

#define SUN6I_DPHY_TX_TIME0_REG		0x10
#define SUN6I_DPHY_TX_TIME0_HS_TRAIL(n)		(((n) & 0xff) << 24)
#define SUN6I_DPHY_TX_TIME0_HS_PREPARE(n)	(((n) & 0xff) << 16)
#define SUN6I_DPHY_TX_TIME0_LP_CLK_DIV(n)	((n) & 0xff)

#define SUN6I_DPHY_TX_TIME1_REG		0x14
#define SUN6I_DPHY_TX_TIME1_CLK_POST(n)		(((n) & 0xff) << 24)
#define SUN6I_DPHY_TX_TIME1_CLK_PRE(n)		(((n) & 0xff) << 16)
#define SUN6I_DPHY_TX_TIME1_CLK_ZERO(n)		(((n) & 0xff) << 8)
#define SUN6I_DPHY_TX_TIME1_CLK_PREPARE(n)	((n) & 0xff)

#define SUN6I_DPHY_TX_TIME2_REG		0x18
#define SUN6I_DPHY_TX_TIME2_CLK_TRAIL(n)	((n) & 0xff)

#define SUN6I_DPHY_TX_TIME3_REG		0x1c

#define SUN6I_DPHY_TX_TIME4_REG		0x20
#define SUN6I_DPHY_TX_TIME4_HS_TX_ANA1(n)	(((n) & 0xff) << 8)
#define SUN6I_DPHY_TX_TIME4_HS_TX_ANA0(n)	((n) & 0xff)

#define SUN6I_DPHY_ANA0_REG		0x4c
#define SUN6I_DPHY_ANA0_REG_PWS			BIT(31)
#define SUN6I_DPHY_ANA0_REG_DMPC		BIT(28)
#define SUN6I_DPHY_ANA0_REG_DMPD(n)		(((n) & 0xf) << 24)
#define SUN6I_DPHY_ANA0_REG_SLV(n)		(((n) & 7) << 12)
#define SUN6I_DPHY_ANA0_REG_DEN(n)		(((n) & 0xf) << 8)

#define SUN6I_DPHY_ANA1_REG		0x50
#define SUN6I_DPHY_ANA1_REG_VTTMODE		BIT(31)
#define SUN6I_DPHY_ANA1_REG_CSMPS(n)		(((n) & 3) << 28)
#define SUN6I_DPHY_ANA1_REG_SVTT(n)		(((n) & 0xf) << 24)

#define SUN6I_DPHY_ANA2_REG		0x54
#define SUN6I_DPHY_ANA2_EN_P2S_CPU(n)		(((n) & 0xf) << 24)
#define SUN6I_DPHY_ANA2_EN_P2S_CPU_MASK		GENMASK(27, 24)
#define SUN6I_DPHY_ANA2_EN_CK_CPU		BIT(4)
#define SUN6I_DPHY_ANA2_REG_ENIB		BIT(1)

#define SUN6I_DPHY_ANA3_REG		0x58
#define SUN6I_DPHY_ANA3_EN_VTTD(n)		(((n) & 0xf) << 28)
#define SUN6I_DPHY_ANA3_EN_VTTD_MASK		GENMASK(31, 28)
#define SUN6I_DPHY_ANA3_EN_VTTC			BIT(27)
#define SUN6I_DPHY_ANA3_EN_DIV			BIT(26)
#define SUN6I_DPHY_ANA3_EN_LDOC			BIT(25)
#define SUN6I_DPHY_ANA3_EN_LDOD			BIT(24)
#define SUN6I_DPHY_ANA3_EN_LDOR			BIT(18)

#define SUN6I_DPHY_ANA4_REG		0x5c
#define SUN6I_DPHY_ANA4_REG_DMPLVC		BIT(24)
#define SUN6I_DPHY_ANA4_REG_DMPLVD(n)		(((n) & 0xf) << 20)
#define SUN6I_DPHY_ANA4_REG_CKDV(n)		(((n) & 0x1f) << 12)
#define SUN6I_DPHY_ANA4_REG_TMSC(n)		(((n) & 3) << 10)
#define SUN6I_DPHY_ANA4_REG_TMSD(n)		(((n) & 3) << 8)
#define SUN6I_DPHY_ANA4_REG_TXDNSC(n)		(((n) & 3) << 6)
#define SUN6I_DPHY_ANA4_REG_TXDNSD(n)		(((n) & 3) << 4)
#define SUN6I_DPHY_ANA4_REG_TXPUSC(n)		(((n) & 3) << 2)
#define SUN6I_DPHY_ANA4_REG_TXPUSD(n)		((n) & 3)

#define SUN6I_DPHY_DBG5_REG		0xf4

int sun6i_dphy_init(struct sun6i_dphy *dphy, unsigned int lanes)
{
	reset_control_deassert(dphy->reset);
	clk_prepare_enable(dphy->mod_clk);
	clk_set_rate_exclusive(dphy->mod_clk, 150000000);

	regmap_write(dphy->regs, SUN6I_DPHY_TX_CTL_REG,
		     SUN6I_DPHY_TX_CTL_HS_TX_CLK_CONT);

	regmap_write(dphy->regs, SUN6I_DPHY_TX_TIME0_REG,
		     SUN6I_DPHY_TX_TIME0_LP_CLK_DIV(14) |
		     SUN6I_DPHY_TX_TIME0_HS_PREPARE(6) |
		     SUN6I_DPHY_TX_TIME0_HS_TRAIL(10));

	regmap_write(dphy->regs, SUN6I_DPHY_TX_TIME1_REG,
		     SUN6I_DPHY_TX_TIME1_CLK_PREPARE(7) |
		     SUN6I_DPHY_TX_TIME1_CLK_ZERO(50) |
		     SUN6I_DPHY_TX_TIME1_CLK_PRE(3) |
		     SUN6I_DPHY_TX_TIME1_CLK_POST(10));

	regmap_write(dphy->regs, SUN6I_DPHY_TX_TIME2_REG,
		     SUN6I_DPHY_TX_TIME2_CLK_TRAIL(30));

	regmap_write(dphy->regs, SUN6I_DPHY_TX_TIME3_REG, 0);

	regmap_write(dphy->regs, SUN6I_DPHY_TX_TIME4_REG,
		     SUN6I_DPHY_TX_TIME4_HS_TX_ANA0(3) |
		     SUN6I_DPHY_TX_TIME4_HS_TX_ANA1(3));

	regmap_write(dphy->regs, SUN6I_DPHY_GCTL_REG,
		     SUN6I_DPHY_GCTL_LANE_NUM(lanes) |
		     SUN6I_DPHY_GCTL_EN);

	return 0;
}

int sun6i_dphy_power_on(struct sun6i_dphy *dphy, unsigned int lanes)
{
	u8 lanes_mask = GENMASK(lanes - 1, 0);

	regmap_write(dphy->regs, SUN6I_DPHY_ANA0_REG,
		     SUN6I_DPHY_ANA0_REG_PWS |
		     SUN6I_DPHY_ANA0_REG_DMPC |
		     SUN6I_DPHY_ANA0_REG_SLV(7) |
		     SUN6I_DPHY_ANA0_REG_DMPD(lanes_mask) |
		     SUN6I_DPHY_ANA0_REG_DEN(lanes_mask));

	regmap_write(dphy->regs, SUN6I_DPHY_ANA1_REG,
		     SUN6I_DPHY_ANA1_REG_CSMPS(1) |
		     SUN6I_DPHY_ANA1_REG_SVTT(7));

	regmap_write(dphy->regs, SUN6I_DPHY_ANA4_REG,
		     SUN6I_DPHY_ANA4_REG_CKDV(1) |
		     SUN6I_DPHY_ANA4_REG_TMSC(1) |
		     SUN6I_DPHY_ANA4_REG_TMSD(1) |
		     SUN6I_DPHY_ANA4_REG_TXDNSC(1) |
		     SUN6I_DPHY_ANA4_REG_TXDNSD(1) |
		     SUN6I_DPHY_ANA4_REG_TXPUSC(1) |
		     SUN6I_DPHY_ANA4_REG_TXPUSD(1) |
		     SUN6I_DPHY_ANA4_REG_DMPLVC |
		     SUN6I_DPHY_ANA4_REG_DMPLVD(lanes_mask));

	regmap_write(dphy->regs, SUN6I_DPHY_ANA2_REG,
		     SUN6I_DPHY_ANA2_REG_ENIB);
	udelay(5);

	regmap_write(dphy->regs, SUN6I_DPHY_ANA3_REG,
		     SUN6I_DPHY_ANA3_EN_LDOR |
		     SUN6I_DPHY_ANA3_EN_LDOC |
		     SUN6I_DPHY_ANA3_EN_LDOD);
	udelay(1);

	regmap_update_bits(dphy->regs, SUN6I_DPHY_ANA3_REG,
			   SUN6I_DPHY_ANA3_EN_VTTC |
			   SUN6I_DPHY_ANA3_EN_VTTD_MASK,
			   SUN6I_DPHY_ANA3_EN_VTTC |
			   SUN6I_DPHY_ANA3_EN_VTTD(lanes_mask));
	udelay(1);

	regmap_update_bits(dphy->regs, SUN6I_DPHY_ANA3_REG,
			   SUN6I_DPHY_ANA3_EN_DIV,
			   SUN6I_DPHY_ANA3_EN_DIV);
	udelay(1);

	regmap_update_bits(dphy->regs, SUN6I_DPHY_ANA2_REG,
			   SUN6I_DPHY_ANA2_EN_CK_CPU,
			   SUN6I_DPHY_ANA2_EN_CK_CPU);
	udelay(1);

	regmap_update_bits(dphy->regs, SUN6I_DPHY_ANA1_REG,
			   SUN6I_DPHY_ANA1_REG_VTTMODE,
			   SUN6I_DPHY_ANA1_REG_VTTMODE);

	regmap_update_bits(dphy->regs, SUN6I_DPHY_ANA2_REG,
			   SUN6I_DPHY_ANA2_EN_P2S_CPU_MASK,
			   SUN6I_DPHY_ANA2_EN_P2S_CPU(lanes_mask));

	return 0;
}

int sun6i_dphy_power_off(struct sun6i_dphy *dphy)
{
	regmap_update_bits(dphy->regs, SUN6I_DPHY_ANA1_REG,
			   SUN6I_DPHY_ANA1_REG_VTTMODE, 0);

	return 0;
}

int sun6i_dphy_exit(struct sun6i_dphy *dphy)
{
	clk_rate_exclusive_put(dphy->mod_clk);
	clk_disable_unprepare(dphy->mod_clk);
	reset_control_assert(dphy->reset);

	return 0;
}

static struct regmap_config sun6i_dphy_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= SUN6I_DPHY_DBG5_REG,
	.name		= "mipi-dphy",
};

static const struct of_device_id sun6i_dphy_of_table[] = {
	{ .compatible = "allwinner,sun6i-a31-mipi-dphy" },
	{ }
};

int sun6i_dphy_probe(struct sun6i_dsi *dsi, struct device_node *node)
{
	struct sun6i_dphy *dphy;
	struct resource res;
	void __iomem *regs;
	int ret;

	if (!of_match_node(sun6i_dphy_of_table, node)) {
		dev_err(dsi->dev, "Incompatible D-PHY\n");
		return -EINVAL;
	}

	dphy = devm_kzalloc(dsi->dev, sizeof(*dphy), GFP_KERNEL);
	if (!dphy)
		return -ENOMEM;

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dsi->dev, "phy: Couldn't get our resources\n");
		return ret;
	}

	regs = devm_ioremap_resource(dsi->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(dsi->dev, "Couldn't map the DPHY encoder registers\n");
		return PTR_ERR(regs);
	}

	dphy->regs = devm_regmap_init_mmio(dsi->dev, regs,
					   &sun6i_dphy_regmap_config);
	if (IS_ERR(dphy->regs)) {
		dev_err(dsi->dev, "Couldn't create the DPHY encoder regmap\n");
		return PTR_ERR(dphy->regs);
	}

	dphy->reset = of_reset_control_get_shared(node, NULL);
	if (IS_ERR(dphy->reset)) {
		dev_err(dsi->dev, "Couldn't get our reset line\n");
		return PTR_ERR(dphy->reset);
	}

	dphy->bus_clk = of_clk_get_by_name(node, "bus");
	if (IS_ERR(dphy->bus_clk)) {
		dev_err(dsi->dev, "Couldn't get the DPHY bus clock\n");
		ret = PTR_ERR(dphy->bus_clk);
		goto err_free_reset;
	}
	regmap_mmio_attach_clk(dphy->regs, dphy->bus_clk);

	dphy->mod_clk = of_clk_get_by_name(node, "mod");
	if (IS_ERR(dphy->mod_clk)) {
		dev_err(dsi->dev, "Couldn't get the DPHY mod clock\n");
		ret = PTR_ERR(dphy->mod_clk);
		goto err_free_bus;
	}

	dsi->dphy = dphy;

	return 0;

err_free_bus:
	regmap_mmio_detach_clk(dphy->regs);
	clk_put(dphy->bus_clk);
err_free_reset:
	reset_control_put(dphy->reset);
	return ret;
}

int sun6i_dphy_remove(struct sun6i_dsi *dsi)
{
	struct sun6i_dphy *dphy = dsi->dphy;

	regmap_mmio_detach_clk(dphy->regs);
	clk_put(dphy->mod_clk);
	clk_put(dphy->bus_clk);
	reset_control_put(dphy->reset);

	return 0;
}
