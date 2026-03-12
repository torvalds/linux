// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * DOC: dwmac-rk.c - Rockchip RK3288 DWMAC specific glue layer
 *
 * Copyright (C) 2014 Chen-Zhi (Roger Chen)
 *
 * Chen-Zhi (Roger Chen)  <roger.chen@rock-chips.com>
 */

#include <linux/stmmac.h>
#include <linux/hw_bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/of_net.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include "stmmac_platform.h"

struct rk_priv_data;

struct rk_clock_fields {
	/* io_clksel_cru_mask - io_clksel bit in clock GRF register which,
	 * when set, selects the tx clock from CRU.
	 */
	u16 io_clksel_cru_mask;
	/* io_clksel_io_mask - io_clksel bit in clock GRF register which,
	 * when set, selects the tx clock from IO.
	 */
	u16 io_clksel_io_mask;
	u16 gmii_clk_sel_mask;
	u16 rmii_clk_sel_mask;
	u16 rmii_gate_en_mask;
	u16 rmii_mode_mask;
	u16 mac_speed_mask;
};

struct rk_gmac_ops {
	int (*init)(struct rk_priv_data *bsp_priv);
	void (*set_to_rgmii)(struct rk_priv_data *bsp_priv,
			     int tx_delay, int rx_delay);
	void (*set_to_rmii)(struct rk_priv_data *bsp_priv);
	int (*set_speed)(struct rk_priv_data *bsp_priv,
			 phy_interface_t interface, int speed);
	void (*integrated_phy_powerup)(struct rk_priv_data *bsp_priv);
	void (*integrated_phy_powerdown)(struct rk_priv_data *bsp_priv);

	u16 gmac_grf_reg;
	u16 gmac_phy_intf_sel_mask;
	u16 gmac_rmii_mode_mask;

	u16 clock_grf_reg;
	struct rk_clock_fields clock;

	bool gmac_grf_reg_in_php;
	bool clock_grf_reg_in_php;
	bool supports_rgmii;
	bool supports_rmii;
	bool php_grf_required;
	bool regs_valid;
	u32 regs[];
};

static const char * const rk_clocks[] = {
	"aclk_mac", "pclk_mac", "mac_clk_tx", "clk_mac_speed",
};

static const char * const rk_rmii_clocks[] = {
	"mac_clk_rx", "clk_mac_ref", "clk_mac_refout",
};

enum rk_clocks_index {
	RK_ACLK_MAC = 0,
	RK_PCLK_MAC,
	RK_MAC_CLK_TX,
	RK_CLK_MAC_SPEED,
	RK_MAC_CLK_RX,
	RK_CLK_MAC_REF,
	RK_CLK_MAC_REFOUT,
};

struct rk_priv_data {
	struct device *dev;
	phy_interface_t phy_iface;
	int id;
	struct regulator *regulator;
	const struct rk_gmac_ops *ops;

	bool clk_enabled;
	bool clock_input;
	bool integrated_phy;
	bool supports_rgmii;
	bool supports_rmii;

	struct clk_bulk_data *clks;
	int num_clks;
	struct clk *clk_phy;

	struct reset_control *phy_reset;

	int tx_delay;
	int rx_delay;

	struct regmap *grf;
	struct regmap *php_grf;

	u16 gmac_grf_reg;
	u16 gmac_phy_intf_sel_mask;
	u16 gmac_rmii_mode_mask;

	u16 clock_grf_reg;
	struct rk_clock_fields clock;
};

#define GMAC_CLK_DIV1_125M		0
#define GMAC_CLK_DIV50_2_5M		2
#define GMAC_CLK_DIV5_25M		3

static int rk_gmac_rgmii_clk_div(int speed)
{
	if (speed == SPEED_10)
		return GMAC_CLK_DIV50_2_5M;
	if (speed == SPEED_100)
		return GMAC_CLK_DIV5_25M;
	if (speed == SPEED_1000)
		return GMAC_CLK_DIV1_125M;
	return -EINVAL;
}

static int rk_get_phy_intf_sel(phy_interface_t interface)
{
	int ret = stmmac_get_phy_intf_sel(interface);

	/* Only RGMII and RMII are supported */
	if (ret != PHY_INTF_SEL_RGMII && ret != PHY_INTF_SEL_RMII)
		ret = -EINVAL;

	return ret;
}

static u32 rk_encode_wm16(u16 val, u16 mask)
{
	u32 reg_val = mask << 16;

	if (mask)
		reg_val |= mask & (val << (ffs(mask) - 1));

	return reg_val;
}

static int rk_write_gmac_grf_reg(struct rk_priv_data *bsp_priv, u32 val)
{
	struct regmap *regmap;

	if (bsp_priv->ops->gmac_grf_reg_in_php)
		regmap = bsp_priv->php_grf;
	else
		regmap = bsp_priv->grf;

	return regmap_write(regmap, bsp_priv->gmac_grf_reg, val);
}

static int rk_write_clock_grf_reg(struct rk_priv_data *bsp_priv, u32 val)
{
	struct regmap *regmap;

	if (bsp_priv->ops->clock_grf_reg_in_php)
		regmap = bsp_priv->php_grf;
	else
		regmap = bsp_priv->grf;

	return regmap_write(regmap, bsp_priv->clock_grf_reg, val);
}

static int rk_set_rmii_gate_en(struct rk_priv_data *bsp_priv, bool state)
{
	u32 val;

	if (!bsp_priv->clock.rmii_gate_en_mask)
		return 0;

	val = rk_encode_wm16(state, bsp_priv->clock.rmii_gate_en_mask);

	return rk_write_clock_grf_reg(bsp_priv, val);
}

static int rk_ungate_rmii_clock(struct rk_priv_data *bsp_priv)
{
	return rk_set_rmii_gate_en(bsp_priv, false);
}

static int rk_gate_rmii_clock(struct rk_priv_data *bsp_priv)
{
	return rk_set_rmii_gate_en(bsp_priv, true);
}

static int rk_configure_io_clksel(struct rk_priv_data *bsp_priv)
{
	bool io, cru;
	u32 val;

	if (!bsp_priv->clock.io_clksel_io_mask &&
	    !bsp_priv->clock.io_clksel_cru_mask)
		return 0;

	io = bsp_priv->clock_input;
	cru = !io;

	/* The io_clksel configuration can be either:
	 *  0=CRU, 1=IO (rk3506, rk3520, rk3576) or
	 *  0=IO, 1=CRU (rk3588)
	 * where CRU means the transmit clock comes from the CRU and IO
	 * means the transmit clock comes from IO.
	 *
	 * Handle this by having two masks.
	 */
	val = rk_encode_wm16(io, bsp_priv->clock.io_clksel_io_mask) |
	      rk_encode_wm16(cru, bsp_priv->clock.io_clksel_cru_mask);

	return rk_write_clock_grf_reg(bsp_priv, val);
}

static int rk_set_clk_mac_speed(struct rk_priv_data *bsp_priv,
				phy_interface_t interface, int speed)
{
	struct clk *clk_mac_speed = bsp_priv->clks[RK_CLK_MAC_SPEED].clk;
	long rate;

	rate = rgmii_clock(speed);
	if (rate < 0)
		return rate;

	return clk_set_rate(clk_mac_speed, rate);
}

#define GRF_FIELD(hi, lo, val)		\
	FIELD_PREP_WM16(GENMASK_U16(hi, lo), val)

#define GRF_BIT(nr)			(BIT(nr) | BIT(nr+16))
#define GRF_CLR_BIT(nr)			(BIT(nr+16))

#define DELAY_ENABLE(soc, tx, rx) \
	(((tx) ? soc##_GMAC_TXCLK_DLY_ENABLE : soc##_GMAC_TXCLK_DLY_DISABLE) | \
	 ((rx) ? soc##_GMAC_RXCLK_DLY_ENABLE : soc##_GMAC_RXCLK_DLY_DISABLE))

#define RK_GRF_MACPHY_CON0		0xb00
#define RK_MACPHY_ENABLE		GRF_BIT(0)
#define RK_MACPHY_DISABLE		GRF_CLR_BIT(0)
#define RK_MACPHY_CFG_CLK_50M		GRF_BIT(14)
#define RK_GMAC2PHY_RMII_MODE		GRF_FIELD(7, 6, 1)

#define RK_GRF_MACPHY_CON1		0xb04

#define RK_GRF_MACPHY_CON2		0xb08
#define RK_GRF_CON2_MACPHY_ID		GRF_FIELD(15, 0, 0x1234)

#define RK_GRF_MACPHY_CON3		0xb0c
#define RK_GRF_CON3_MACPHY_ID		GRF_FIELD(5, 0, 0x35)

static void rk_gmac_integrated_ephy_powerup(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_CFG_CLK_50M);
	regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_GMAC2PHY_RMII_MODE);

	regmap_write(priv->grf, RK_GRF_MACPHY_CON2, RK_GRF_CON2_MACPHY_ID);
	regmap_write(priv->grf, RK_GRF_MACPHY_CON3, RK_GRF_CON3_MACPHY_ID);

	if (priv->phy_reset) {
		/* PHY needs to be disabled before trying to reset it */
		regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_DISABLE);
		if (priv->phy_reset)
			reset_control_assert(priv->phy_reset);
		usleep_range(10, 20);
		if (priv->phy_reset)
			reset_control_deassert(priv->phy_reset);
		usleep_range(10, 20);
		regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_ENABLE);
		msleep(30);
	}
}

static void rk_gmac_integrated_ephy_powerdown(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_DISABLE);
	if (priv->phy_reset)
		reset_control_assert(priv->phy_reset);
}

#define RK_FEPHY_SHUTDOWN		GRF_BIT(1)
#define RK_FEPHY_POWERUP		GRF_CLR_BIT(1)
#define RK_FEPHY_INTERNAL_RMII_SEL	GRF_BIT(6)
#define RK_FEPHY_24M_CLK_SEL		GRF_FIELD(9, 8, 3)
#define RK_FEPHY_PHY_ID			GRF_BIT(11)

static void rk_gmac_integrated_fephy_powerup(struct rk_priv_data *priv,
					     unsigned int reg)
{
	reset_control_assert(priv->phy_reset);
	usleep_range(20, 30);

	regmap_write(priv->grf, reg,
		     RK_FEPHY_POWERUP |
		     RK_FEPHY_INTERNAL_RMII_SEL |
		     RK_FEPHY_24M_CLK_SEL |
		     RK_FEPHY_PHY_ID);
	usleep_range(10000, 12000);

	reset_control_deassert(priv->phy_reset);
	usleep_range(50000, 60000);
}

static void rk_gmac_integrated_fephy_powerdown(struct rk_priv_data *priv,
					       unsigned int reg)
{
	regmap_write(priv->grf, reg, RK_FEPHY_SHUTDOWN);
}

#define PX30_GRF_GMAC_CON1		0x0904

static const struct rk_gmac_ops px30_ops = {
	.set_speed = rk_set_clk_mac_speed,

	.gmac_grf_reg = PX30_GRF_GMAC_CON1,
	.gmac_phy_intf_sel_mask = GENMASK_U16(6, 4),

	.clock_grf_reg = PX30_GRF_GMAC_CON1,
	.clock.mac_speed_mask = BIT_U16(2),

	.supports_rmii = true,
};

#define RK3128_GRF_MAC_CON0	0x0168
#define RK3128_GRF_MAC_CON1	0x016c

/* RK3128_GRF_MAC_CON0 */
#define RK3128_GMAC_TXCLK_DLY_ENABLE   GRF_BIT(14)
#define RK3128_GMAC_TXCLK_DLY_DISABLE  GRF_CLR_BIT(14)
#define RK3128_GMAC_RXCLK_DLY_ENABLE   GRF_BIT(15)
#define RK3128_GMAC_RXCLK_DLY_DISABLE  GRF_CLR_BIT(15)
#define RK3128_GMAC_CLK_RX_DL_CFG(val) GRF_FIELD(13, 7, val)
#define RK3128_GMAC_CLK_TX_DL_CFG(val) GRF_FIELD(6, 0, val)

/* RK3128_GRF_MAC_CON1 */
#define RK3128_GMAC_FLOW_CTRL          GRF_BIT(9)
#define RK3128_GMAC_FLOW_CTRL_CLR      GRF_CLR_BIT(9)

static void rk3128_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3128_GRF_MAC_CON0,
		     DELAY_ENABLE(RK3128, tx_delay, rx_delay) |
		     RK3128_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3128_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static const struct rk_gmac_ops rk3128_ops = {
	.set_to_rgmii = rk3128_set_to_rgmii,

	.gmac_grf_reg = RK3128_GRF_MAC_CON1,
	.gmac_phy_intf_sel_mask = GENMASK_U16(8, 6),
	.gmac_rmii_mode_mask = BIT_U16(14),

	.clock_grf_reg = RK3128_GRF_MAC_CON1,
	.clock.gmii_clk_sel_mask = GENMASK_U16(13, 12),
	.clock.rmii_clk_sel_mask = BIT_U16(11),
	.clock.mac_speed_mask = BIT_U16(10),

	.supports_rmii = true,
};

#define RK3228_GRF_MAC_CON0	0x0900
#define RK3228_GRF_MAC_CON1	0x0904

#define RK3228_GRF_CON_MUX	0x50

/* RK3228_GRF_MAC_CON0 */
#define RK3228_GMAC_CLK_RX_DL_CFG(val)	GRF_FIELD(13, 7, val)
#define RK3228_GMAC_CLK_TX_DL_CFG(val)	GRF_FIELD(6, 0, val)

/* RK3228_GRF_MAC_CON1 */
#define RK3228_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RK3228_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RK3228_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(0)
#define RK3228_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(0)
#define RK3228_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(1)
#define RK3228_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(1)

/* RK3228_GRF_COM_MUX */
#define RK3228_GRF_CON_MUX_GMAC_INTEGRATED_PHY	GRF_BIT(15)

static void rk3228_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON1,
		     DELAY_ENABLE(RK3228, tx_delay, rx_delay));

	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON0,
		     RK3228_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3228_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3228_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	/* set MAC to RMII mode */
	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON1, GRF_BIT(11));
}

static void rk3228_integrated_phy_powerup(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK3228_GRF_CON_MUX,
		     RK3228_GRF_CON_MUX_GMAC_INTEGRATED_PHY);

	rk_gmac_integrated_ephy_powerup(priv);
}

static const struct rk_gmac_ops rk3228_ops = {
	.set_to_rgmii = rk3228_set_to_rgmii,
	.set_to_rmii = rk3228_set_to_rmii,
	.integrated_phy_powerup = rk3228_integrated_phy_powerup,
	.integrated_phy_powerdown = rk_gmac_integrated_ephy_powerdown,

	.gmac_grf_reg = RK3228_GRF_MAC_CON1,
	.gmac_phy_intf_sel_mask = GENMASK_U16(6, 4),
	.gmac_rmii_mode_mask = BIT_U16(10),

	.clock_grf_reg = RK3228_GRF_MAC_CON1,
	.clock.gmii_clk_sel_mask = GENMASK_U16(9, 8),
	.clock.rmii_clk_sel_mask = BIT_U16(7),
	.clock.mac_speed_mask = BIT_U16(2),
};

#define RK3288_GRF_SOC_CON1	0x0248
#define RK3288_GRF_SOC_CON3	0x0250

/*RK3288_GRF_SOC_CON1*/
#define RK3288_GMAC_FLOW_CTRL		GRF_BIT(9)
#define RK3288_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(9)

/*RK3288_GRF_SOC_CON3*/
#define RK3288_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(14)
#define RK3288_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(14)
#define RK3288_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3288_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3288_GMAC_CLK_RX_DL_CFG(val)	GRF_FIELD(13, 7, val)
#define RK3288_GMAC_CLK_TX_DL_CFG(val)	GRF_FIELD(6, 0, val)

static void rk3288_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON3,
		     DELAY_ENABLE(RK3288, tx_delay, rx_delay) |
		     RK3288_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3288_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static const struct rk_gmac_ops rk3288_ops = {
	.set_to_rgmii = rk3288_set_to_rgmii,

	.gmac_grf_reg = RK3288_GRF_SOC_CON1,
	.gmac_phy_intf_sel_mask = GENMASK_U16(8, 6),
	.gmac_rmii_mode_mask = BIT_U16(14),

	.clock_grf_reg = RK3288_GRF_SOC_CON1,
	.clock.gmii_clk_sel_mask = GENMASK_U16(13, 12),
	.clock.rmii_clk_sel_mask = BIT_U16(11),
	.clock.mac_speed_mask = BIT_U16(10),

	.supports_rmii = true,
};

#define RK3308_GRF_MAC_CON0		0x04a0

/* RK3308_GRF_MAC_CON0 */
#define RK3308_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RK3308_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)

static const struct rk_gmac_ops rk3308_ops = {
	.gmac_grf_reg = RK3308_GRF_MAC_CON0,
	.gmac_phy_intf_sel_mask = GENMASK_U16(4, 2),

	.clock_grf_reg = RK3308_GRF_MAC_CON0,
	.clock.mac_speed_mask = BIT_U16(0),

	.supports_rmii = true,
};

#define RK3328_GRF_MAC_CON0	0x0900
#define RK3328_GRF_MAC_CON1	0x0904
#define RK3328_GRF_MAC_CON2	0x0908
#define RK3328_GRF_MACPHY_CON1	0xb04

/* RK3328_GRF_MAC_CON0 */
#define RK3328_GMAC_CLK_RX_DL_CFG(val)	GRF_FIELD(13, 7, val)
#define RK3328_GMAC_CLK_TX_DL_CFG(val)	GRF_FIELD(6, 0, val)

/* RK3328_GRF_MAC_CON1 */
#define RK3328_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RK3328_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RK3328_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(0)
#define RK3328_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(1)

/* RK3328_GRF_MACPHY_CON1 */
#define RK3328_MACPHY_RMII_MODE		GRF_BIT(9)

static int rk3328_init(struct rk_priv_data *bsp_priv)
{
	switch (bsp_priv->id) {
	case 0: /* gmac2io */
		bsp_priv->gmac_grf_reg = RK3328_GRF_MAC_CON1;
		bsp_priv->clock_grf_reg = RK3328_GRF_MAC_CON1;
		bsp_priv->clock.gmii_clk_sel_mask = GENMASK_U16(12, 11);
		return 0;

	case 1: /* gmac2phy */
		bsp_priv->gmac_grf_reg = RK3328_GRF_MAC_CON2;
		bsp_priv->clock_grf_reg = RK3328_GRF_MAC_CON2;
		bsp_priv->supports_rgmii = false;
		return 0;

	default:
		return -EINVAL;
	}
}

static void rk3328_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3328_GRF_MAC_CON1,
		     RK3328_GMAC_RXCLK_DLY_ENABLE |
		     RK3328_GMAC_TXCLK_DLY_ENABLE);

	regmap_write(bsp_priv->grf, RK3328_GRF_MAC_CON0,
		     RK3328_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3328_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3328_integrated_phy_powerup(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK3328_GRF_MACPHY_CON1,
		     RK3328_MACPHY_RMII_MODE);

	rk_gmac_integrated_ephy_powerup(priv);
}

static const struct rk_gmac_ops rk3328_ops = {
	.init = rk3328_init,
	.set_to_rgmii = rk3328_set_to_rgmii,
	.integrated_phy_powerup = rk3328_integrated_phy_powerup,
	.integrated_phy_powerdown = rk_gmac_integrated_ephy_powerdown,

	.gmac_phy_intf_sel_mask = GENMASK_U16(6, 4),
	.gmac_rmii_mode_mask = BIT_U16(9),

	.clock.rmii_clk_sel_mask = BIT_U16(7),
	.clock.mac_speed_mask = BIT_U16(2),

	.supports_rmii = true,

	.regs_valid = true,
	.regs = {
		0xff540000, /* gmac2io */
		0xff550000, /* gmac2phy */
		0, /* sentinel */
	},
};

#define RK3366_GRF_SOC_CON6	0x0418
#define RK3366_GRF_SOC_CON7	0x041c

/* RK3366_GRF_SOC_CON6 */
#define RK3366_GMAC_FLOW_CTRL		GRF_BIT(8)
#define RK3366_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(8)

/* RK3366_GRF_SOC_CON7 */
#define RK3366_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(7)
#define RK3366_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(7)
#define RK3366_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3366_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3366_GMAC_CLK_RX_DL_CFG(val)	GRF_FIELD(14, 8, val)
#define RK3366_GMAC_CLK_TX_DL_CFG(val)	GRF_FIELD(6, 0, val)

static void rk3366_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3366_GRF_SOC_CON7,
		     DELAY_ENABLE(RK3366, tx_delay, rx_delay) |
		     RK3366_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3366_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static const struct rk_gmac_ops rk3366_ops = {
	.set_to_rgmii = rk3366_set_to_rgmii,

	.gmac_grf_reg = RK3366_GRF_SOC_CON6,
	.gmac_phy_intf_sel_mask = GENMASK_U16(11, 9),
	.gmac_rmii_mode_mask = BIT_U16(6),

	.clock_grf_reg = RK3366_GRF_SOC_CON6,
	.clock.gmii_clk_sel_mask = GENMASK_U16(5, 4),
	.clock.rmii_clk_sel_mask = BIT_U16(3),
	.clock.mac_speed_mask = BIT_U16(7),

	.supports_rmii = true,
};

#define RK3368_GRF_SOC_CON15	0x043c
#define RK3368_GRF_SOC_CON16	0x0440

/* RK3368_GRF_SOC_CON15 */
#define RK3368_GMAC_FLOW_CTRL		GRF_BIT(8)
#define RK3368_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(8)

/* RK3368_GRF_SOC_CON16 */
#define RK3368_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(7)
#define RK3368_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(7)
#define RK3368_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3368_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3368_GMAC_CLK_RX_DL_CFG(val)	GRF_FIELD(14, 8, val)
#define RK3368_GMAC_CLK_TX_DL_CFG(val)	GRF_FIELD(6, 0, val)

static void rk3368_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3368_GRF_SOC_CON16,
		     DELAY_ENABLE(RK3368, tx_delay, rx_delay) |
		     RK3368_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3368_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static const struct rk_gmac_ops rk3368_ops = {
	.set_to_rgmii = rk3368_set_to_rgmii,

	.gmac_grf_reg = RK3368_GRF_SOC_CON15,
	.gmac_phy_intf_sel_mask = GENMASK_U16(11, 9),
	.gmac_rmii_mode_mask = BIT_U16(6),

	.clock_grf_reg = RK3368_GRF_SOC_CON15,
	.clock.gmii_clk_sel_mask = GENMASK_U16(5, 4),
	.clock.rmii_clk_sel_mask = BIT_U16(3),
	.clock.mac_speed_mask = BIT_U16(7),

	.supports_rmii = true,
};

#define RK3399_GRF_SOC_CON5	0xc214
#define RK3399_GRF_SOC_CON6	0xc218

/* RK3399_GRF_SOC_CON5 */
#define RK3399_GMAC_FLOW_CTRL		GRF_BIT(8)
#define RK3399_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(8)

/* RK3399_GRF_SOC_CON6 */
#define RK3399_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(7)
#define RK3399_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(7)
#define RK3399_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3399_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3399_GMAC_CLK_RX_DL_CFG(val)	GRF_FIELD(14, 8, val)
#define RK3399_GMAC_CLK_TX_DL_CFG(val)	GRF_FIELD(6, 0, val)

static void rk3399_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3399_GRF_SOC_CON6,
		     DELAY_ENABLE(RK3399, tx_delay, rx_delay) |
		     RK3399_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3399_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static const struct rk_gmac_ops rk3399_ops = {
	.set_to_rgmii = rk3399_set_to_rgmii,

	.gmac_grf_reg = RK3399_GRF_SOC_CON5,
	.gmac_phy_intf_sel_mask = GENMASK_U16(11, 9),
	.gmac_rmii_mode_mask = BIT_U16(6),

	.clock_grf_reg = RK3399_GRF_SOC_CON5,
	.clock.gmii_clk_sel_mask = GENMASK_U16(5, 4),
	.clock.rmii_clk_sel_mask = BIT_U16(3),
	.clock.mac_speed_mask = BIT_U16(7),

	.supports_rmii = true,
};

#define RK3506_GRF_SOC_CON8		0x0020
#define RK3506_GRF_SOC_CON11		0x002c

#define RK3506_GMAC_RMII_MODE		GRF_BIT(1)

static int rk3506_init(struct rk_priv_data *bsp_priv)
{
	switch (bsp_priv->id) {
	case 0:
		bsp_priv->clock_grf_reg = RK3506_GRF_SOC_CON8;
		return 0;

	case 1:
		bsp_priv->clock_grf_reg = RK3506_GRF_SOC_CON11;
		return 0;

	default:
		return -EINVAL;
	}
}

static const struct rk_gmac_ops rk3506_ops = {
	.init = rk3506_init,

	.clock.io_clksel_io_mask = BIT_U16(5),
	.clock.rmii_clk_sel_mask = BIT_U16(3),
	.clock.rmii_gate_en_mask = BIT_U16(2),
	.clock.rmii_mode_mask = BIT_U16(1),

	.supports_rmii = true,

	.regs_valid = true,
	.regs = {
		0xff4c8000, /* gmac0 */
		0xff4d0000, /* gmac1 */
		0x0, /* sentinel */
	},
};

#define RK3528_VO_GRF_GMAC_CON		0x0018
#define RK3528_VO_GRF_MACPHY_CON0	0x001c
#define RK3528_VO_GRF_MACPHY_CON1	0x0020
#define RK3528_VPU_GRF_GMAC_CON5	0x0018
#define RK3528_VPU_GRF_GMAC_CON6	0x001c

#define RK3528_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3528_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3528_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(14)
#define RK3528_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(14)

#define RK3528_GMAC_CLK_RX_DL_CFG(val)	GRF_FIELD(15, 8, val)
#define RK3528_GMAC_CLK_TX_DL_CFG(val)	GRF_FIELD(7, 0, val)

static int rk3528_init(struct rk_priv_data *bsp_priv)
{
	switch (bsp_priv->id) {
	case 0:
		bsp_priv->clock_grf_reg = RK3528_VO_GRF_GMAC_CON;
		bsp_priv->clock.rmii_clk_sel_mask = BIT_U16(3);
		bsp_priv->clock.rmii_gate_en_mask = BIT_U16(2);
		bsp_priv->clock.rmii_mode_mask = BIT_U16(1);
		bsp_priv->supports_rgmii = false;
		return 0;

	case 1:
		bsp_priv->clock_grf_reg = RK3528_VPU_GRF_GMAC_CON5;
		bsp_priv->clock.io_clksel_io_mask = BIT_U16(12);
		bsp_priv->clock.gmii_clk_sel_mask = GENMASK_U16(11, 10);
		bsp_priv->clock.rmii_clk_sel_mask = BIT_U16(10);
		bsp_priv->clock.rmii_gate_en_mask = BIT_U16(9);
		bsp_priv->clock.rmii_mode_mask = BIT_U16(8);
		return 0;

	default:
		return -EINVAL;
	}
}

static void rk3528_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON5,
		     DELAY_ENABLE(RK3528, tx_delay, rx_delay));

	regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON6,
		     RK3528_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3528_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3528_integrated_phy_powerup(struct rk_priv_data *bsp_priv)
{
	rk_gmac_integrated_fephy_powerup(bsp_priv, RK3528_VO_GRF_MACPHY_CON0);
}

static void rk3528_integrated_phy_powerdown(struct rk_priv_data *bsp_priv)
{
	rk_gmac_integrated_fephy_powerdown(bsp_priv, RK3528_VO_GRF_MACPHY_CON0);
}

static const struct rk_gmac_ops rk3528_ops = {
	.init = rk3528_init,
	.set_to_rgmii = rk3528_set_to_rgmii,
	.integrated_phy_powerup = rk3528_integrated_phy_powerup,
	.integrated_phy_powerdown = rk3528_integrated_phy_powerdown,

	.supports_rmii = true,

	.regs_valid = true,
	.regs = {
		0xffbd0000, /* gmac0 */
		0xffbe0000, /* gmac1 */
		0x0, /* sentinel */
	},
};

#define RK3568_GRF_GMAC0_CON0		0x0380
#define RK3568_GRF_GMAC0_CON1		0x0384
#define RK3568_GRF_GMAC1_CON0		0x0388
#define RK3568_GRF_GMAC1_CON1		0x038c

/* RK3568_GRF_GMAC0_CON1 && RK3568_GRF_GMAC1_CON1 */
#define RK3568_GMAC_FLOW_CTRL			GRF_BIT(3)
#define RK3568_GMAC_FLOW_CTRL_CLR		GRF_CLR_BIT(3)
#define RK3568_GMAC_RXCLK_DLY_ENABLE		GRF_BIT(1)
#define RK3568_GMAC_RXCLK_DLY_DISABLE		GRF_CLR_BIT(1)
#define RK3568_GMAC_TXCLK_DLY_ENABLE		GRF_BIT(0)
#define RK3568_GMAC_TXCLK_DLY_DISABLE		GRF_CLR_BIT(0)

/* RK3568_GRF_GMAC0_CON0 && RK3568_GRF_GMAC1_CON0 */
#define RK3568_GMAC_CLK_RX_DL_CFG(val)	GRF_FIELD(14, 8, val)
#define RK3568_GMAC_CLK_TX_DL_CFG(val)	GRF_FIELD(6, 0, val)

static int rk3568_init(struct rk_priv_data *bsp_priv)
{
	switch (bsp_priv->id) {
	case 0:
		bsp_priv->gmac_grf_reg = RK3568_GRF_GMAC0_CON1;
		return 0;

	case 1:
		bsp_priv->gmac_grf_reg = RK3568_GRF_GMAC1_CON1;
		return 0;

	default:
		return -EINVAL;
	}
}

static void rk3568_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	u32 con0, con1;

	con0 = (bsp_priv->id == 1) ? RK3568_GRF_GMAC1_CON0 :
				     RK3568_GRF_GMAC0_CON0;
	con1 = (bsp_priv->id == 1) ? RK3568_GRF_GMAC1_CON1 :
				     RK3568_GRF_GMAC0_CON1;

	regmap_write(bsp_priv->grf, con0,
		     RK3568_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3568_GMAC_CLK_TX_DL_CFG(tx_delay));

	regmap_write(bsp_priv->grf, con1,
		     RK3568_GMAC_RXCLK_DLY_ENABLE |
		     RK3568_GMAC_TXCLK_DLY_ENABLE);
}

static const struct rk_gmac_ops rk3568_ops = {
	.init = rk3568_init,
	.set_to_rgmii = rk3568_set_to_rgmii,
	.set_speed = rk_set_clk_mac_speed,

	.gmac_phy_intf_sel_mask = GENMASK_U16(6, 4),

	.supports_rmii = true,

	.regs_valid = true,
	.regs = {
		0xfe2a0000, /* gmac0 */
		0xfe010000, /* gmac1 */
		0x0, /* sentinel */
	},
};

/* VCCIO0_1_3_IOC */
#define RK3576_VCCIO0_1_3_IOC_CON2		0X6408
#define RK3576_VCCIO0_1_3_IOC_CON3		0X640c
#define RK3576_VCCIO0_1_3_IOC_CON4		0X6410
#define RK3576_VCCIO0_1_3_IOC_CON5		0X6414

#define RK3576_GMAC_RXCLK_DLY_ENABLE		GRF_BIT(15)
#define RK3576_GMAC_RXCLK_DLY_DISABLE		GRF_CLR_BIT(15)
#define RK3576_GMAC_TXCLK_DLY_ENABLE		GRF_BIT(7)
#define RK3576_GMAC_TXCLK_DLY_DISABLE		GRF_CLR_BIT(7)

#define RK3576_GMAC_CLK_RX_DL_CFG(val)		GRF_FIELD(14, 8, val)
#define RK3576_GMAC_CLK_TX_DL_CFG(val)		GRF_FIELD(6, 0, val)

/* SDGMAC_GRF */
#define RK3576_GRF_GMAC_CON0			0X0020
#define RK3576_GRF_GMAC_CON1			0X0024

static int rk3576_init(struct rk_priv_data *bsp_priv)
{
	switch (bsp_priv->id) {
	case 0:
		bsp_priv->gmac_grf_reg = RK3576_GRF_GMAC_CON0;
		bsp_priv->clock_grf_reg = RK3576_GRF_GMAC_CON0;
		return 0;

	case 1:
		bsp_priv->gmac_grf_reg = RK3576_GRF_GMAC_CON1;
		bsp_priv->clock_grf_reg = RK3576_GRF_GMAC_CON1;
		return 0;

	default:
		return -EINVAL;
	}
}

static void rk3576_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	unsigned int offset_con;

	offset_con = bsp_priv->id == 1 ? RK3576_VCCIO0_1_3_IOC_CON4 :
					 RK3576_VCCIO0_1_3_IOC_CON2;

	/* m0 && m1 delay enabled */
	regmap_write(bsp_priv->php_grf, offset_con,
		     DELAY_ENABLE(RK3576, tx_delay, rx_delay));
	regmap_write(bsp_priv->php_grf, offset_con + 0x4,
		     DELAY_ENABLE(RK3576, tx_delay, rx_delay));

	/* m0 && m1 delay value */
	regmap_write(bsp_priv->php_grf, offset_con,
		     RK3576_GMAC_CLK_TX_DL_CFG(tx_delay) |
		     RK3576_GMAC_CLK_RX_DL_CFG(rx_delay));
	regmap_write(bsp_priv->php_grf, offset_con + 0x4,
		     RK3576_GMAC_CLK_TX_DL_CFG(tx_delay) |
		     RK3576_GMAC_CLK_RX_DL_CFG(rx_delay));
}

static const struct rk_gmac_ops rk3576_ops = {
	.init = rk3576_init,
	.set_to_rgmii = rk3576_set_to_rgmii,

	.gmac_rmii_mode_mask = BIT_U16(3),

	.clock.io_clksel_io_mask = BIT_U16(7),
	.clock.gmii_clk_sel_mask = GENMASK_U16(6, 5),
	.clock.rmii_clk_sel_mask = BIT_U16(5),
	.clock.rmii_gate_en_mask = BIT_U16(4),

	.supports_rmii = true,

	.php_grf_required = true,
	.regs_valid = true,
	.regs = {
		0x2a220000, /* gmac0 */
		0x2a230000, /* gmac1 */
		0x0, /* sentinel */
	},
};

/* sys_grf */
#define RK3588_GRF_GMAC_CON7			0X031c
#define RK3588_GRF_GMAC_CON8			0X0320
#define RK3588_GRF_GMAC_CON9			0X0324

#define RK3588_GMAC_RXCLK_DLY_ENABLE(id)	GRF_BIT(2 * (id) + 3)
#define RK3588_GMAC_RXCLK_DLY_DISABLE(id)	GRF_CLR_BIT(2 * (id) + 3)
#define RK3588_GMAC_TXCLK_DLY_ENABLE(id)	GRF_BIT(2 * (id) + 2)
#define RK3588_GMAC_TXCLK_DLY_DISABLE(id)	GRF_CLR_BIT(2 * (id) + 2)

#define RK3588_GMAC_CLK_RX_DL_CFG(val)		GRF_FIELD(15, 8, val)
#define RK3588_GMAC_CLK_TX_DL_CFG(val)		GRF_FIELD(7, 0, val)

/* php_grf */
#define RK3588_GRF_GMAC_CON0			0X0008
#define RK3588_GRF_CLK_CON1			0X0070

static int rk3588_init(struct rk_priv_data *bsp_priv)
{
	switch (bsp_priv->id) {
	case 0:
		bsp_priv->gmac_phy_intf_sel_mask = GENMASK_U16(5, 3);
		bsp_priv->clock.io_clksel_cru_mask = BIT_U16(4);
		bsp_priv->clock.gmii_clk_sel_mask = GENMASK_U16(3, 2);
		bsp_priv->clock.rmii_clk_sel_mask = BIT_U16(2);
		bsp_priv->clock.rmii_gate_en_mask = BIT_U16(1);
		bsp_priv->clock.rmii_mode_mask = BIT_U16(0);
		return 0;

	case 1:
		bsp_priv->gmac_phy_intf_sel_mask = GENMASK_U16(11, 9);
		bsp_priv->clock.io_clksel_cru_mask = BIT_U16(9);
		bsp_priv->clock.gmii_clk_sel_mask = GENMASK_U16(8, 7);
		bsp_priv->clock.rmii_clk_sel_mask = BIT_U16(7);
		bsp_priv->clock.rmii_gate_en_mask = BIT_U16(6);
		bsp_priv->clock.rmii_mode_mask = BIT_U16(5);
		return 0;

	default:
		return -EINVAL;
	}
}

static void rk3588_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	u32 offset_con, id = bsp_priv->id;

	offset_con = bsp_priv->id == 1 ? RK3588_GRF_GMAC_CON9 :
					 RK3588_GRF_GMAC_CON8;

	regmap_write(bsp_priv->grf, RK3588_GRF_GMAC_CON7,
		     RK3588_GMAC_RXCLK_DLY_ENABLE(id) |
		     RK3588_GMAC_TXCLK_DLY_ENABLE(id));

	regmap_write(bsp_priv->grf, offset_con,
		     RK3588_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3588_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static const struct rk_gmac_ops rk3588_ops = {
	.init = rk3588_init,
	.set_to_rgmii = rk3588_set_to_rgmii,

	.gmac_grf_reg_in_php = true,
	.gmac_grf_reg = RK3588_GRF_GMAC_CON0,

	.clock_grf_reg_in_php = true,
	.clock_grf_reg = RK3588_GRF_CLK_CON1,

	.supports_rmii = true,

	.php_grf_required = true,
	.regs_valid = true,
	.regs = {
		0xfe1b0000, /* gmac0 */
		0xfe1c0000, /* gmac1 */
		0x0, /* sentinel */
	},
};

#define RV1108_GRF_GMAC_CON0		0X0900

/* RV1108_GRF_GMAC_CON0 */
#define RV1108_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RV1108_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)

static const struct rk_gmac_ops rv1108_ops = {
	.gmac_grf_reg = RV1108_GRF_GMAC_CON0,
	.gmac_phy_intf_sel_mask = GENMASK_U16(6, 4),

	.clock_grf_reg = RV1108_GRF_GMAC_CON0,
	.clock.rmii_clk_sel_mask = BIT_U16(7),
	.clock.mac_speed_mask = BIT_U16(2),

	.supports_rmii = true,
};

#define RV1126_GRF_GMAC_CON0		0X0070
#define RV1126_GRF_GMAC_CON1		0X0074
#define RV1126_GRF_GMAC_CON2		0X0078

/* RV1126_GRF_GMAC_CON0 */
#define RV1126_GMAC_FLOW_CTRL			GRF_BIT(7)
#define RV1126_GMAC_FLOW_CTRL_CLR		GRF_CLR_BIT(7)
#define RV1126_GMAC_M0_RXCLK_DLY_ENABLE		GRF_BIT(1)
#define RV1126_GMAC_M0_RXCLK_DLY_DISABLE	GRF_CLR_BIT(1)
#define RV1126_GMAC_M0_TXCLK_DLY_ENABLE		GRF_BIT(0)
#define RV1126_GMAC_M0_TXCLK_DLY_DISABLE	GRF_CLR_BIT(0)
#define RV1126_GMAC_M1_RXCLK_DLY_ENABLE		GRF_BIT(3)
#define RV1126_GMAC_M1_RXCLK_DLY_DISABLE	GRF_CLR_BIT(3)
#define RV1126_GMAC_M1_TXCLK_DLY_ENABLE		GRF_BIT(2)
#define RV1126_GMAC_M1_TXCLK_DLY_DISABLE	GRF_CLR_BIT(2)

/* RV1126_GRF_GMAC_CON1 */
#define RV1126_GMAC_M0_CLK_RX_DL_CFG(val)	GRF_FIELD(14, 8, val)
#define RV1126_GMAC_M0_CLK_TX_DL_CFG(val)	GRF_FIELD(6, 0, val)
/* RV1126_GRF_GMAC_CON2 */
#define RV1126_GMAC_M1_CLK_RX_DL_CFG(val)	GRF_FIELD(14, 8, val)
#define RV1126_GMAC_M1_CLK_TX_DL_CFG(val)	GRF_FIELD(6, 0, val)

static void rv1126_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON0,
		     RV1126_GMAC_M0_RXCLK_DLY_ENABLE |
		     RV1126_GMAC_M0_TXCLK_DLY_ENABLE |
		     RV1126_GMAC_M1_RXCLK_DLY_ENABLE |
		     RV1126_GMAC_M1_TXCLK_DLY_ENABLE);

	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON1,
		     RV1126_GMAC_M0_CLK_RX_DL_CFG(rx_delay) |
		     RV1126_GMAC_M0_CLK_TX_DL_CFG(tx_delay));

	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON2,
		     RV1126_GMAC_M1_CLK_RX_DL_CFG(rx_delay) |
		     RV1126_GMAC_M1_CLK_TX_DL_CFG(tx_delay));
}

static const struct rk_gmac_ops rv1126_ops = {
	.set_to_rgmii = rv1126_set_to_rgmii,
	.set_speed = rk_set_clk_mac_speed,

	.gmac_grf_reg = RV1126_GRF_GMAC_CON0,
	.gmac_phy_intf_sel_mask = GENMASK_U16(6, 4),

	.supports_rmii = true,
};

static int rk_gmac_clk_init(struct plat_stmmacenet_data *plat)
{
	struct rk_priv_data *bsp_priv = plat->bsp_priv;
	int phy_iface = bsp_priv->phy_iface;
	struct device *dev = bsp_priv->dev;
	int i, j, ret;

	bsp_priv->clk_enabled = false;

	bsp_priv->num_clks = ARRAY_SIZE(rk_clocks);
	if (phy_iface == PHY_INTERFACE_MODE_RMII)
		bsp_priv->num_clks += ARRAY_SIZE(rk_rmii_clocks);

	bsp_priv->clks = devm_kcalloc(dev, bsp_priv->num_clks,
				      sizeof(*bsp_priv->clks), GFP_KERNEL);
	if (!bsp_priv->clks)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(rk_clocks); i++)
		bsp_priv->clks[i].id = rk_clocks[i];

	if (phy_iface == PHY_INTERFACE_MODE_RMII) {
		for (j = 0; j < ARRAY_SIZE(rk_rmii_clocks); j++)
			bsp_priv->clks[i++].id = rk_rmii_clocks[j];
	}

	ret = devm_clk_bulk_get_optional(dev, bsp_priv->num_clks,
					 bsp_priv->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get clocks\n");

	if (bsp_priv->clock_input) {
		dev_info(dev, "clock input from PHY\n");
	} else if (phy_iface == PHY_INTERFACE_MODE_RMII) {
		clk_set_rate(plat->stmmac_clk, 50000000);
	}

	if (plat->phy_node && bsp_priv->integrated_phy) {
		bsp_priv->clk_phy = of_clk_get(plat->phy_node, 0);
		ret = PTR_ERR_OR_ZERO(bsp_priv->clk_phy);
		if (ret)
			return dev_err_probe(dev, ret, "Cannot get PHY clock\n");
		clk_set_rate(bsp_priv->clk_phy, 50000000);
	}

	return 0;
}

static int gmac_clk_enable(struct rk_priv_data *bsp_priv, bool enable)
{
	int ret;

	if (enable) {
		if (!bsp_priv->clk_enabled) {
			ret = clk_bulk_prepare_enable(bsp_priv->num_clks,
						      bsp_priv->clks);
			if (ret)
				return ret;

			ret = clk_prepare_enable(bsp_priv->clk_phy);
			if (ret)
				return ret;

			rk_configure_io_clksel(bsp_priv);
			rk_ungate_rmii_clock(bsp_priv);

			mdelay(5);
			bsp_priv->clk_enabled = true;
		}
	} else {
		if (bsp_priv->clk_enabled) {
			rk_gate_rmii_clock(bsp_priv);

			clk_bulk_disable_unprepare(bsp_priv->num_clks,
						   bsp_priv->clks);
			clk_disable_unprepare(bsp_priv->clk_phy);

			bsp_priv->clk_enabled = false;
		}
	}

	return 0;
}

static int rk_phy_powerup(struct rk_priv_data *bsp_priv)
{
	struct regulator *ldo = bsp_priv->regulator;
	int ret;

	ret = regulator_enable(ldo);
	if (ret)
		dev_err(bsp_priv->dev, "fail to enable phy-supply\n");

	return ret;
}

static void rk_phy_powerdown(struct rk_priv_data *bsp_priv)
{
	struct regulator *ldo = bsp_priv->regulator;
	int ret;

	ret = regulator_disable(ldo);
	if (ret)
		dev_err(bsp_priv->dev, "fail to disable phy-supply\n");
}

static struct rk_priv_data *rk_gmac_setup(struct platform_device *pdev,
					  struct plat_stmmacenet_data *plat,
					  const struct rk_gmac_ops *ops)
{
	struct rk_priv_data *bsp_priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	const char *strings = NULL;
	int value;

	bsp_priv = devm_kzalloc(dev, sizeof(*bsp_priv), GFP_KERNEL);
	if (!bsp_priv)
		return ERR_PTR(-ENOMEM);

	bsp_priv->phy_iface = plat->phy_interface;
	bsp_priv->ops = ops;

	/* Some SoCs have multiple MAC controllers, which need
	 * to be distinguished.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res && ops->regs_valid) {
		int i = 0;

		while (ops->regs[i]) {
			if (ops->regs[i] == res->start) {
				bsp_priv->id = i;
				break;
			}
			i++;
		}
	}

	bsp_priv->regulator = devm_regulator_get(dev, "phy");
	if (IS_ERR(bsp_priv->regulator)) {
		ret = PTR_ERR(bsp_priv->regulator);
		dev_err_probe(dev, ret, "failed to get phy regulator\n");
		return ERR_PTR(ret);
	}

	ret = of_property_read_string(dev->of_node, "clock_in_out", &strings);
	if (ret) {
		dev_err(dev, "Can not read property: clock_in_out.\n");
		bsp_priv->clock_input = true;
	} else {
		dev_info(dev, "clock input or output? (%s).\n",
			 strings);
		if (!strcmp(strings, "input"))
			bsp_priv->clock_input = true;
		else
			bsp_priv->clock_input = false;
	}

	ret = of_property_read_u32(dev->of_node, "tx_delay", &value);
	if (ret) {
		bsp_priv->tx_delay = 0x30;
		dev_err(dev, "Can not read property: tx_delay.");
		dev_err(dev, "set tx_delay to 0x%x\n",
			bsp_priv->tx_delay);
	} else {
		dev_info(dev, "TX delay(0x%x).\n", value);
		bsp_priv->tx_delay = value;
	}

	ret = of_property_read_u32(dev->of_node, "rx_delay", &value);
	if (ret) {
		bsp_priv->rx_delay = 0x10;
		dev_err(dev, "Can not read property: rx_delay.");
		dev_err(dev, "set rx_delay to 0x%x\n",
			bsp_priv->rx_delay);
	} else {
		dev_info(dev, "RX delay(0x%x).\n", value);
		bsp_priv->rx_delay = value;
	}

	bsp_priv->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,grf");
	if (IS_ERR(bsp_priv->grf)) {
		dev_err_probe(dev, PTR_ERR(bsp_priv->grf),
			      "failed to lookup rockchip,grf\n");
		return ERR_CAST(bsp_priv->grf);
	}

	if (ops->php_grf_required) {
		bsp_priv->php_grf =
			syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,php-grf");
		if (IS_ERR(bsp_priv->php_grf)) {
			dev_err_probe(dev, PTR_ERR(bsp_priv->php_grf),
				      "failed to lookup rockchip,php-grf\n");
			return ERR_CAST(bsp_priv->php_grf);
		}
	}

	if (plat->phy_node) {
		bsp_priv->integrated_phy = of_property_read_bool(plat->phy_node,
								 "phy-is-integrated");
		if (bsp_priv->integrated_phy) {
			bsp_priv->phy_reset = of_reset_control_get(plat->phy_node, NULL);
			if (IS_ERR(bsp_priv->phy_reset)) {
				dev_err(&pdev->dev, "No PHY reset control found.\n");
				bsp_priv->phy_reset = NULL;
			}
		}
	}
	dev_info(dev, "integrated PHY? (%s).\n",
		 bsp_priv->integrated_phy ? "yes" : "no");

	bsp_priv->dev = dev;

	/* Set the default phy_intf_sel and RMII mode register parameters. */
	bsp_priv->gmac_grf_reg = ops->gmac_grf_reg;
	bsp_priv->gmac_phy_intf_sel_mask = ops->gmac_phy_intf_sel_mask;
	bsp_priv->gmac_rmii_mode_mask = ops->gmac_rmii_mode_mask;

	/* Set the default clock control register related parameters */
	bsp_priv->clock_grf_reg = ops->clock_grf_reg;
	bsp_priv->clock = ops->clock;

	bsp_priv->supports_rgmii = ops->supports_rgmii || !!ops->set_to_rgmii;
	bsp_priv->supports_rmii = ops->supports_rmii || !!ops->set_to_rmii;

	if (ops->init) {
		ret = ops->init(bsp_priv);
		if (ret) {
			reset_control_put(bsp_priv->phy_reset);
			dev_err_probe(dev, ret, "failed to init BSP\n");
			return ERR_PTR(ret);
		}
	}

	if (bsp_priv->clock.io_clksel_cru_mask &&
	    bsp_priv->clock.io_clksel_io_mask)
		dev_warn(dev, "both CRU and IO io_clksel masks should not be populated - driver may malfunction\n");

	return bsp_priv;
}

static int rk_gmac_check_ops(struct rk_priv_data *bsp_priv)
{
	switch (bsp_priv->phy_iface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		if (!bsp_priv->supports_rgmii)
			return -EINVAL;
		break;
	case PHY_INTERFACE_MODE_RMII:
		if (!bsp_priv->supports_rmii)
			return -EINVAL;
		break;
	default:
		dev_err(bsp_priv->dev,
			"unsupported interface %d", bsp_priv->phy_iface);
	}
	return 0;
}

static int rk_gmac_powerup(struct rk_priv_data *bsp_priv)
{
	struct device *dev = bsp_priv->dev;
	u32 val;
	int ret;
	u8 intf;

	ret = rk_gmac_check_ops(bsp_priv);
	if (ret)
		return ret;

	ret = rk_get_phy_intf_sel(bsp_priv->phy_iface);
	if (ret < 0)
		return ret;

	intf = ret;

	ret = gmac_clk_enable(bsp_priv, true);
	if (ret)
		return ret;

	if (bsp_priv->gmac_phy_intf_sel_mask ||
	    bsp_priv->gmac_rmii_mode_mask) {
		/* If defined, encode the phy_intf_sel value */
		val = rk_encode_wm16(intf, bsp_priv->gmac_phy_intf_sel_mask);

		/* If defined, encode the RMII mode mask setting. */
		val |= rk_encode_wm16(intf == PHY_INTF_SEL_RMII,
				      bsp_priv->gmac_rmii_mode_mask);

		ret = rk_write_gmac_grf_reg(bsp_priv, val);
		if (ret < 0) {
			gmac_clk_enable(bsp_priv, false);
			return ret;
		}
	}

	if (bsp_priv->clock.rmii_mode_mask) {
		val = rk_encode_wm16(intf == PHY_INTF_SEL_RMII,
				     bsp_priv->clock.rmii_mode_mask);

		ret = rk_write_clock_grf_reg(bsp_priv, val);
		if (ret < 0) {
			gmac_clk_enable(bsp_priv, false);
			return ret;
		}
	}

	/*rmii or rgmii*/
	switch (bsp_priv->phy_iface) {
	case PHY_INTERFACE_MODE_RGMII:
		dev_info(dev, "init for RGMII\n");
		if (bsp_priv->ops->set_to_rgmii)
			bsp_priv->ops->set_to_rgmii(bsp_priv,
						    bsp_priv->tx_delay,
						    bsp_priv->rx_delay);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		dev_info(dev, "init for RGMII_ID\n");
		if (bsp_priv->ops->set_to_rgmii)
			bsp_priv->ops->set_to_rgmii(bsp_priv, 0, 0);
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		dev_info(dev, "init for RGMII_RXID\n");
		if (bsp_priv->ops->set_to_rgmii)
			bsp_priv->ops->set_to_rgmii(bsp_priv,
						    bsp_priv->tx_delay, 0);
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		dev_info(dev, "init for RGMII_TXID\n");
		if (bsp_priv->ops->set_to_rgmii)
			bsp_priv->ops->set_to_rgmii(bsp_priv,
						    0, bsp_priv->rx_delay);
		break;
	case PHY_INTERFACE_MODE_RMII:
		dev_info(dev, "init for RMII\n");
		if (bsp_priv->ops->set_to_rmii)
			bsp_priv->ops->set_to_rmii(bsp_priv);
		break;
	default:
		dev_err(dev, "NO interface defined!\n");
	}

	ret = rk_phy_powerup(bsp_priv);
	if (ret) {
		gmac_clk_enable(bsp_priv, false);
		return ret;
	}

	pm_runtime_get_sync(dev);

	if (bsp_priv->integrated_phy && bsp_priv->ops->integrated_phy_powerup)
		bsp_priv->ops->integrated_phy_powerup(bsp_priv);

	return 0;
}

static void rk_gmac_powerdown(struct rk_priv_data *gmac)
{
	if (gmac->integrated_phy && gmac->ops->integrated_phy_powerdown)
		gmac->ops->integrated_phy_powerdown(gmac);

	pm_runtime_put_sync(gmac->dev);

	rk_phy_powerdown(gmac);
	gmac_clk_enable(gmac, false);
}

static void rk_get_interfaces(struct stmmac_priv *priv, void *bsp_priv,
			      unsigned long *interfaces)
{
	struct rk_priv_data *rk = bsp_priv;

	if (rk->supports_rgmii)
		phy_interface_set_rgmii(interfaces);

	if (rk->supports_rmii)
		__set_bit(PHY_INTERFACE_MODE_RMII, interfaces);
}

static int rk_set_clk_tx_rate(void *bsp_priv_, struct clk *clk_tx_i,
			      phy_interface_t interface, int speed)
{
	struct rk_priv_data *bsp_priv = bsp_priv_;
	int ret = -EINVAL;
	bool is_100m;
	u32 val;

	if (bsp_priv->ops->set_speed) {
		ret = bsp_priv->ops->set_speed(bsp_priv, interface, speed);
		if (ret < 0)
			return ret;
	}

	if (phy_interface_mode_is_rgmii(interface) &&
	    bsp_priv->clock.gmii_clk_sel_mask) {
		ret = rk_gmac_rgmii_clk_div(speed);
		if (ret < 0)
			return ret;

		val = rk_encode_wm16(ret, bsp_priv->clock.gmii_clk_sel_mask);

		ret = rk_write_clock_grf_reg(bsp_priv, val);
	} else if (interface == PHY_INTERFACE_MODE_RMII &&
		   (bsp_priv->clock.rmii_clk_sel_mask ||
		    bsp_priv->clock.mac_speed_mask)) {
		is_100m = speed == SPEED_100;
		val = rk_encode_wm16(is_100m, bsp_priv->clock.mac_speed_mask) |
		      rk_encode_wm16(is_100m,
				     bsp_priv->clock.rmii_clk_sel_mask);

		ret = rk_write_clock_grf_reg(bsp_priv, val);
	}

	return ret;
}

static int rk_gmac_suspend(struct device *dev, void *bsp_priv_)
{
	struct rk_priv_data *bsp_priv = bsp_priv_;

	/* Keep the PHY up if we use Wake-on-Lan. */
	if (!device_may_wakeup(dev))
		rk_gmac_powerdown(bsp_priv);

	return 0;
}

static int rk_gmac_resume(struct device *dev, void *bsp_priv_)
{
	struct rk_priv_data *bsp_priv = bsp_priv_;

	/* The PHY was up for Wake-on-Lan. */
	if (!device_may_wakeup(dev))
		rk_gmac_powerup(bsp_priv);

	return 0;
}

static int rk_gmac_init(struct device *dev, void *bsp_priv)
{
	return rk_gmac_powerup(bsp_priv);
}

static void rk_gmac_exit(struct device *dev, void *bsp_priv_)
{
	struct stmmac_priv *priv = netdev_priv(dev_get_drvdata(dev));
	struct rk_priv_data *bsp_priv = bsp_priv_;

	rk_gmac_powerdown(bsp_priv);

	if (priv->plat->phy_node && bsp_priv->integrated_phy)
		clk_put(bsp_priv->clk_phy);

	reset_control_put(bsp_priv->phy_reset);
}

static int rk_gmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	const struct rk_gmac_ops *data;
	int ret;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "no of match data provided\n");
		return -EINVAL;
	}

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	/* If the stmmac is not already selected as gmac4,
	 * then make sure we fallback to gmac.
	 */
	if (plat_dat->core_type != DWMAC_CORE_GMAC4) {
		plat_dat->core_type = DWMAC_CORE_GMAC;
		plat_dat->rx_fifo_size = 4096;
		plat_dat->tx_fifo_size = 2048;
	}

	plat_dat->get_interfaces = rk_get_interfaces;
	plat_dat->set_clk_tx_rate = rk_set_clk_tx_rate;
	plat_dat->init = rk_gmac_init;
	plat_dat->exit = rk_gmac_exit;
	plat_dat->suspend = rk_gmac_suspend;
	plat_dat->resume = rk_gmac_resume;

	plat_dat->bsp_priv = rk_gmac_setup(pdev, plat_dat, data);
	if (IS_ERR(plat_dat->bsp_priv))
		return PTR_ERR(plat_dat->bsp_priv);

	ret = rk_gmac_clk_init(plat_dat);
	if (ret)
		return ret;

	return devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
}

static const struct of_device_id rk_gmac_dwmac_match[] = {
	{ .compatible = "rockchip,px30-gmac",	.data = &px30_ops   },
	{ .compatible = "rockchip,rk3128-gmac", .data = &rk3128_ops },
	{ .compatible = "rockchip,rk3228-gmac", .data = &rk3228_ops },
	{ .compatible = "rockchip,rk3288-gmac", .data = &rk3288_ops },
	{ .compatible = "rockchip,rk3308-gmac", .data = &rk3308_ops },
	{ .compatible = "rockchip,rk3328-gmac", .data = &rk3328_ops },
	{ .compatible = "rockchip,rk3366-gmac", .data = &rk3366_ops },
	{ .compatible = "rockchip,rk3368-gmac", .data = &rk3368_ops },
	{ .compatible = "rockchip,rk3399-gmac", .data = &rk3399_ops },
	{ .compatible = "rockchip,rk3506-gmac", .data = &rk3506_ops },
	{ .compatible = "rockchip,rk3528-gmac", .data = &rk3528_ops },
	{ .compatible = "rockchip,rk3568-gmac", .data = &rk3568_ops },
	{ .compatible = "rockchip,rk3576-gmac", .data = &rk3576_ops },
	{ .compatible = "rockchip,rk3588-gmac", .data = &rk3588_ops },
	{ .compatible = "rockchip,rv1108-gmac", .data = &rv1108_ops },
	{ .compatible = "rockchip,rv1126-gmac", .data = &rv1126_ops },
	{ }
};
MODULE_DEVICE_TABLE(of, rk_gmac_dwmac_match);

static struct platform_driver rk_gmac_dwmac_driver = {
	.probe  = rk_gmac_probe,
	.driver = {
		.name           = "rk_gmac-dwmac",
		.pm		= &stmmac_simple_pm_ops,
		.of_match_table = rk_gmac_dwmac_match,
	},
};
module_platform_driver(rk_gmac_dwmac_driver);

MODULE_AUTHOR("Chen-Zhi (Roger Chen) <roger.chen@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK3288 DWMAC specific glue layer");
MODULE_LICENSE("GPL");
