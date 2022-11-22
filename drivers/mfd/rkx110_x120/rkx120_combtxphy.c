// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 */

#include <linux/kernel.h>
#include <linux/iopoll.h>
#include "rkx110_x120.h"
#include "rkx120_reg.h"
#include "serdes_combphy.h"

#define CLANE_PARA0		0x0000
#define CLANE_PARA1		0x0004
#define T_INITTIME_C(x)		UPDATE(x, 31, 0)
#define CLANE_PARA2		0x0008
#define T_CLKPREPARE_C(x)	UPDATE(x, 23, 16)
#define T_CLKZERO_C(x)		UPDATE(x, 15, 8)
#define T_CLKPRE_C(x)		UPDATE(x, 7, 0)
#define CLANE_PARA3		0x000c
#define T_CLKPOST_C_MASK	GENMASK(23, 16)
#define T_CLKPOST_C(x)		UPDATE(x, 23, 16)
#define T_CLKTRAIL_C_MASK	GENMASK(15, 8)
#define T_CLKTRAIL_C(x)		UPDATE(x, 15, 8)
#define T_HSEXIT_C_MASK		GENMASK(7, 0)
#define T_HSEXIT_C(x)		UPDATE(x, 7, 0)
#define DLANE0_PARA0		0x0010
#define T_RST2ENLPTX_D(x)	UPDATE(x, 15, 0)
#define DLANE0_PARA1		0x0014
#define T_INITTIME_D(x)		UPDATE(x, 31, 0)
#define DLANE0_PARA2		0x0018
#define T_HSPREPARE_D(x)	UPDATE(x, 31, 24)
#define T_HSZERO_D(x)		UPDATE(x, 23, 16)
#define T_HSTRAIL_D(x)		UPDATE(x, 15, 8)
#define T_HSEXIT_D(x)		UPDATE(x, 7, 0)
#define DLANE0_PARA3		0x001c
#define T_WAKEUP_D(x)		UPDATE(x, 31, 0)
#define DLANE0_PARA4		0x0020
#define T_TAGO_D0(x)		UPDATE(x, 23, 16)
#define T_TASURE_D0(x)		UPDATE(x, 15, 8)
#define T_TAGET_D0(x)		UPDATE(x, 7, 0)
#define DLANE_PARA0(x)		(0x0014 + (x * 0x0010))
#define DLANE_PARA1(x)		(0x0018 + (x * 0x0010))
#define DLANE_PARA2(x)		(0x001C + (x * 0x0010))
#define DLANE_PARA3(x)		(0x0020 + (x * 0x0010))
#define COMMON_PARA0		(0x0054)
#define T_LPX(x)		UPDATE(x, 7, 0)
#define CTRL_PARA0		0x0058
#define PWON_SEL		BIT(3)
#define PWON_DSI		BIT(1)
#define SU_IDDQ_EN		BIT(0)
#define PLL_CTRL_PARA0		0x005c
#define PLL_LOCK		BIT(27)
#define RATE_MASK		GENMASK(26, 24)
#define RATE(x)			UPDATE(x, 26, 24)
#define REFCLK_DIV_MASK		GENMASK(23, 19)
#define REFCLK_DIV(x)		UPDATE(x, 23, 19)
#define PLL_DIV_MASK		GENMASK(18, 4)
#define PLL_DIV(x)		UPDATE(x, 18, 4)
#define DSI_PIXELCLK_DIV(x)	UPDATE(x, 3, 0)
#define PLL_CTRL_PARA1		0x0060
#define PLL_CTRL(x)		UPDATE(x, 31, 0)
#define RCAL_CTRL		0x0064
#define RCAL_EN			BIT(13)
#define RCAL_TRIM(x)		UPDATE(x, 12, 9)
#define RCAL_DONE		BIT(0)
#define TRIM_PARA		0x0068
#define HSTX_AMP_TRIM(x)	UPDATE(x, 13, 11)
#define LPTX_SR_TRIM(x)		UPDATE(x, 10, 8)
#define LPRX_VREF_TRIM(x)	UPDATE(x, 7, 4)
#define LPCD_VREF_TRIM(x)	UPDATE(x, 3, 0)
#define TEST_PARA0		0x006c
#define FSET_EN			BIT(3)

#define CLANE_PARA4		0x0078
#define INTERFACE_PARA		0x007c
#define TXREADYESC_VLD(x)	UPDATE(x, 15, 8)
#define RXVALIDESC_VLD(x)	UPDATE(x, 7, 0)

#define GRF_MIPITX_CON0		0x0000
#define PHYSHUTDWN(x)		HIWORD_UPDATE(x, GENMASK(10, 10), 10)
#define LVDS_REFCLK_DIV(x)	HIWORD_UPDATE(x, GENMASK(9, 6), 6)
#define PHY_MODE(x)		HIWORD_UPDATE(x, GENMASK(4, 3), 3)
#define RATE_LVDS(x)		HIWORD_UPDATE(x, GENMASK(2, 1), 1)
#define GRF_MIPITX_CON1		0x0004
#define PWON_PLL(x)		HIWORD_UPDATE(x, GENMASK(15, 15), 15)
#define LVDS_PLL_DIV(x)		HIWORD_UPDATE(x, GENMASK(14, 0), 0)

#define GRF_MIPITX_CON5		0x0014
#define TXCLK_BUS_WIDTH(x)	HIWORD_UPDATE(x, GENMASK(14, 12), 12)
#define TX3_BUS_WIDTH(x)	HIWORD_UPDATE(x, GENMASK(11, 9), 9)
#define TX2_BUS_WIDTH(x)	HIWORD_UPDATE(x, GENMASK(8, 6), 6)
#define TX1_BUS_WIDTH(x)	HIWORD_UPDATE(x, GENMASK(5, 3), 3)
#define TX0_BUS_WIDTH(x)	HIWORD_UPDATE(x, GENMASK(2, 0), 0)
#define GRF_MIPITX_CON6		0x0018
#define TX0_CTL_LOW(x)		HIWORD_UPDATE(x, GENMASK(15, 0), 0)
#define GRF_MIPITX_CON7		0x001c
#define TX1_CTL_LOW(x)		HIWORD_UPDATE(x, GENMASK(15, 0), 0)
#define GRF_MIPITX_CON8		0x0020
#define TX2_CTL_LOW(x)		HIWORD_UPDATE(x, GENMASK(15, 0), 0)
#define GRF_MIPITX_CON9		0x0024
#define TX3_CTL_LOW(x)		HIWORD_UPDATE(x, GENMASK(15, 0), 0)
#define GRF_MIPITX_CON10	0x0028
#define TXCK_CTL_LOW(x)		HIWORD_UPDATE(x, GENMASK(15, 0), 0)

#define GRF_MIPITX_CON13	0x0034
#define TX_IDLE(x)		HIWORD_UPDATE(x, GENMASK(4, 0), 0)
#define GRF_MIPITX_CON14	0x0038
#define TX_PD(x)		HIWORD_UPDATE(x, GENMASK(14, 10), 10)

#define GRF_MIPI_STATUS		0x0080
#define PHYLOCK			BIT(0)

static void rkx120_combtxphy_dsi_timing_init(struct rk_serdes *des, u8 remote_id)
{
	struct rkx120_combtxphy *combtxphy = &des->combtxphy;
	const struct configure_opts_combphy cfg = combtxphy->mipi_dphy_cfg;
	u32 byte_clk = DIV_ROUND_CLOSEST_ULL(combtxphy->rate, 8);
	u32 esc_div = DIV_ROUND_UP(byte_clk, 20 * USEC_PER_SEC);
	u64 t_byte_clk = DIV_ROUND_CLOSEST_ULL(NSEC_PER_SEC, byte_clk);
	u32 t_clkprepare, t_clkzero, t_clkpre, t_clkpost, t_clktrail;
	u32 t_init, t_hsexit, t_hsprepare, t_hszero, t_wakeup, t_hstrail;
	u32 t_tago, t_tasure, t_taget;
	u32 base = RKX120_MIPI_LVDS_TX_PHY0_BASE;

	serdes_combphy_write(des, remote_id, base + INTERFACE_PARA,
			     TXREADYESC_VLD(esc_div - 2) |
			     RXVALIDESC_VLD(esc_div - 2));
	serdes_combphy_write(des, remote_id, base + COMMON_PARA0, esc_div);
	serdes_combphy_update_bits(des, remote_id, base + TEST_PARA0, FSET_EN, FSET_EN);

	t_init = DIV_ROUND_UP(cfg.init, t_byte_clk) - 1;
	serdes_combphy_write(des, remote_id, base + CLANE_PARA1, T_INITTIME_C(t_init));
	serdes_combphy_write(des, remote_id, base + DLANE0_PARA1, T_INITTIME_D(t_init));
	serdes_combphy_write(des, remote_id, base + DLANE_PARA1(1), T_INITTIME_D(t_init));
	serdes_combphy_write(des, remote_id, base + DLANE_PARA1(2), T_INITTIME_D(t_init));
	serdes_combphy_write(des, remote_id, base + DLANE_PARA1(3), T_INITTIME_D(t_init));

	t_clkprepare = DIV_ROUND_UP(cfg.clk_prepare, t_byte_clk) - 1;
	t_clkzero = DIV_ROUND_UP(cfg.clk_zero, t_byte_clk) - 1;
	t_clkpre = DIV_ROUND_UP(cfg.clk_pre, t_byte_clk) - 1;
	serdes_combphy_write(des, remote_id, base + CLANE_PARA2,
			     T_CLKPREPARE_C(t_clkprepare) |
			     T_CLKZERO_C(t_clkzero) | T_CLKPRE_C(t_clkpre));

	t_clkpost = DIV_ROUND_UP(cfg.clk_post, t_byte_clk) - 1;
	t_clktrail = DIV_ROUND_UP(cfg.clk_trail, t_byte_clk) - 1;
	t_hsexit = DIV_ROUND_UP(cfg.hs_exit, t_byte_clk) - 1;
	serdes_combphy_write(des, remote_id, base + CLANE_PARA3,
			     T_CLKPOST_C(t_clkpost) |
			     T_CLKTRAIL_C(t_clktrail) |
			     T_HSEXIT_C(t_hsexit));

	t_hsprepare = DIV_ROUND_UP(cfg.hs_prepare, t_byte_clk) - 1;
	t_hszero = DIV_ROUND_UP(cfg.hs_zero, t_byte_clk) - 1;
	t_hstrail = DIV_ROUND_UP(cfg.hs_trail, t_byte_clk) - 1;
	serdes_combphy_write(des, remote_id, base + DLANE0_PARA2,
			     T_HSPREPARE_D(t_hsprepare) |
			     T_HSZERO_D(t_hszero) |
			     T_HSTRAIL_D(t_hstrail) |
			     T_HSEXIT_D(t_hsexit));

	serdes_combphy_write(des, remote_id, base + DLANE_PARA2(1),
			     T_HSPREPARE_D(t_hsprepare) |
			     T_HSZERO_D(t_hszero) |
			     T_HSTRAIL_D(t_hstrail) |
			     T_HSEXIT_D(t_hsexit));

	serdes_combphy_write(des, remote_id, base + DLANE_PARA2(2),
			     T_HSPREPARE_D(t_hsprepare) |
			     T_HSZERO_D(t_hszero) |
			     T_HSTRAIL_D(t_hstrail) |
			     T_HSEXIT_D(t_hsexit));

	serdes_combphy_write(des, remote_id, base + DLANE_PARA2(3),
			     T_HSPREPARE_D(t_hsprepare) |
			     T_HSZERO_D(t_hszero) |
			     T_HSTRAIL_D(t_hstrail) |
			     T_HSEXIT_D(t_hsexit));

	t_wakeup = DIV_ROUND_UP(cfg.wakeup, t_byte_clk) - 1;
	serdes_combphy_write(des, remote_id, base + DLANE0_PARA3, T_WAKEUP_D(t_wakeup));
	serdes_combphy_write(des, remote_id, base + DLANE_PARA3(1), T_WAKEUP_D(t_wakeup));
	serdes_combphy_write(des, remote_id, base + DLANE_PARA3(2), T_WAKEUP_D(t_wakeup));
	serdes_combphy_write(des, remote_id, base + DLANE_PARA3(3), T_WAKEUP_D(t_wakeup));
	serdes_combphy_write(des, remote_id, base + CLANE_PARA4, T_WAKEUP_D(t_wakeup));

	t_tago = DIV_ROUND_UP(cfg.ta_go, t_byte_clk) - 1;
	t_tasure = DIV_ROUND_UP(cfg.ta_sure, t_byte_clk) - 1;
	t_taget = DIV_ROUND_UP(cfg.ta_get, t_byte_clk) - 1;
	serdes_combphy_write(des, remote_id, base + DLANE0_PARA4,
			     T_TAGO_D0(t_tago) |
			     T_TASURE_D0(t_tasure) |
			     T_TAGET_D0(t_taget));
}

static void rkx120_combtxphy_dsi_pll_set(struct rk_serdes *des, u8 remote_id)
{
	struct rkx120_combtxphy *combtxphy = &des->combtxphy;
	u32 base = RKX120_MIPI_LVDS_TX_PHY0_BASE;

	serdes_combphy_update_bits(des, remote_id, base + PLL_CTRL_PARA0,
				   RATE_MASK | REFCLK_DIV_MASK | PLL_DIV_MASK,
				   RATE(combtxphy->rate_factor) |
				   REFCLK_DIV(combtxphy->ref_div - 1) |
				   PLL_DIV(combtxphy->fb_div));
}

static void rkx120_combtxphy_dsi_power_on(struct rk_serdes *des, u8 remote_id)
{
	struct rkx120_combtxphy *combtxphy = &des->combtxphy;
	struct i2c_client *client = des->chip[remote_id].client;
	u32 grf_base = RKX120_GRF_MIPI0_BASE;
	u32 val;
	int ret;

	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON0,
			   PHY_MODE(COMBTX_PHY_MODE_VIDEO_MIPI));

	serdes_combphy_get_default_config(combtxphy->rate, &combtxphy->mipi_dphy_cfg);
	rkx120_combtxphy_dsi_timing_init(des, remote_id);
	rkx120_combtxphy_dsi_pll_set(des, remote_id);

	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON0, PHYSHUTDWN(1));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON1, PWON_PLL(1));

	ret = read_poll_timeout(des->i2c_read_reg, ret,
				!(ret < 0) && (val & PLL_LOCK),
				0, MSEC_PER_SEC, false, client,
				RKX120_MIPI_LVDS_TX_PHY0_BASE + PLL_CTRL_PARA0,
				&val);
	if (ret < 0)
		dev_err(des->dev, "PLL is not locked\n");
}

static void rkx120_combtxphy_dsi_power_off(struct rk_serdes *des, u8 remote_id)
{
	struct i2c_client *client = des->chip[remote_id].client;
	u32 grf_base = RKX120_GRF_MIPI0_BASE;

	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON0, PHYSHUTDWN(0));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON1, PWON_PLL(0));
}

static void rkx120_combtxphy_lvds_power_on(struct rk_serdes *des, u8 remote_id, u8 phy_id)
{
	struct rkx120_combtxphy *combtxphy = &des->combtxphy;
	struct i2c_client *client = des->chip[remote_id].client;
	u32 grf_base = (phy_id == 0) ?
			RKX120_GRF_MIPI0_BASE : RKX120_GRF_MIPI1_BASE;
	const struct {
		unsigned long max_lane_mbps;
		u8 rate_lvds;
		u8 refclk_div;
		u16 pll_div;
	} hsfreqrange_table[] = {
		{250, 2, 0, 0x3800},
		{500, 1, 1, 0x3800},
		{1000, 0, 3, 0x3800},
	};
	int ret;
	u32 val;
	u8 index;

	for (index = 0; index < ARRAY_SIZE(hsfreqrange_table); index++)
		if (combtxphy->rate < hsfreqrange_table[index].max_lane_mbps * USEC_PER_SEC)
			break;

	if (index == ARRAY_SIZE(hsfreqrange_table))
		--index;

	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON13, TX_IDLE(0x1f));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON14, TX_PD(0x1f));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON0,
			   LVDS_REFCLK_DIV(hsfreqrange_table[index].refclk_div) |
			   RATE_LVDS(hsfreqrange_table[index].rate_lvds));

	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON1,
			   LVDS_PLL_DIV(hsfreqrange_table[index].pll_div));

	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON5,
			   TXCLK_BUS_WIDTH(2) | TX3_BUS_WIDTH(2) |
			   TX2_BUS_WIDTH(2) | TX1_BUS_WIDTH(2) |
			   TX0_BUS_WIDTH(2));

	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON6, TX0_CTL_LOW(0x80));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON7, TX0_CTL_LOW(0x80));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON8, TX0_CTL_LOW(0x80));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON9, TX0_CTL_LOW(0x80));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON10, TX0_CTL_LOW(0x80));

	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON14, TX_PD(0));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON0, PHYSHUTDWN(1) |
			   PHY_MODE(COMBTX_PHY_MODE_VIDEO_LVDS));
	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON1, PWON_PLL(1));

	ret = read_poll_timeout(des->i2c_read_reg, ret,
				!(ret < 0) && (val & PHYLOCK),
				0, MSEC_PER_SEC, false, client,
				grf_base + GRF_MIPI_STATUS, &val);
	if (ret < 0)
		dev_err(des->dev, "PLL is not locked\n");

	des->i2c_write_reg(client, grf_base + GRF_MIPITX_CON13, TX_IDLE(0));
}

void rkx120_combtxphy_power_on(struct rk_serdes *des, u8 remote_id, u8 phy_id)
{
	struct rkx120_combtxphy *combtxphy = &des->combtxphy;

	switch (combtxphy->mode) {
	case COMBTX_PHY_MODE_VIDEO_MIPI:
		rkx120_combtxphy_dsi_power_on(des, remote_id);
		break;
	case COMBTX_PHY_MODE_VIDEO_LVDS:
		rkx120_combtxphy_lvds_power_on(des, remote_id, phy_id);
		break;
	default:
		break;
	}
}

void rkx120_combtxphy_power_off(struct rk_serdes *des, u8 remote_id)
{
	struct rkx120_combtxphy *combtxphy = &des->combtxphy;

	switch (combtxphy->mode) {
	case COMBTX_PHY_MODE_VIDEO_MIPI:
		rkx120_combtxphy_dsi_power_off(des, remote_id);
		break;
	case COMBTX_PHY_MODE_VIDEO_LVDS:
		break;
	case COMBTX_PHY_MODE_GPIO:
		break;
	default:
		break;
	}
}

static void rkx120_combtxphy_dsi_pll_calc_rate(struct rkx120_combtxphy *combtxphy, u64 rate)
{
	const struct {
		unsigned long max_lane_mbps;
		u8 refclk_div;
		u8 post_factor;
	} hsfreqrange_table[] = {
		{ 100, 1, 5 },
		{ 200, 1, 4 },
		{ 400, 1, 3 },
		{ 800, 1, 2},
		{ 1600, 1, 1},
	};
	u64 ref_clk = 24 * USEC_PER_SEC;
	u64 fvco;
	u16 fb_div;
	u8 ref_div, post_div, index;

	for (index = 0; index < ARRAY_SIZE(hsfreqrange_table); index++)
		if (rate <= hsfreqrange_table[index].max_lane_mbps * USEC_PER_SEC)
			break;

	if (index == ARRAY_SIZE(hsfreqrange_table))
		--index;

	/*
	 * FVCO = Fckref * 8 * （ PI_POST_DIV + PF_POST_DIV） / R
	 * data_rate = FVCO / post_div;
	 */

	ref_div = hsfreqrange_table[index].refclk_div;
	post_div = 1 << hsfreqrange_table[index].post_factor;
	fvco = rate * post_div;
	fb_div = DIV_ROUND_CLOSEST_ULL((fvco * ref_div) << 10, ref_clk * 8);

	rate = DIV_ROUND_CLOSEST_ULL(ref_clk * 8 * fb_div,
				     (u64)(ref_div * post_div) << 10);

	combtxphy->ref_div = ref_div;
	combtxphy->fb_div = fb_div;
	combtxphy->rate_factor = hsfreqrange_table[index].post_factor;
	combtxphy->rate = rate;
}

static void rkx120_combtxphy_lvds_pll_calc_rate(struct rkx120_combtxphy *combtxphy, u64 rate)
{
}

void rkx120_combtxphy_set_rate(struct rk_serdes *des, u64 rate)
{
	struct rkx120_combtxphy *combtxphy = &des->combtxphy;

	switch (combtxphy->mode) {
	case COMBTX_PHY_MODE_VIDEO_MIPI:
		rkx120_combtxphy_dsi_pll_calc_rate(combtxphy, rate);
		break;
	case COMBTX_PHY_MODE_VIDEO_LVDS:
		rkx120_combtxphy_lvds_pll_calc_rate(combtxphy, rate);
		break;
	default:
		break;
	}

	combtxphy->rate = rate;
}

u64 rkx120_combtxphy_get_rate(struct rk_serdes *des)
{
	return des->combtxphy.rate;
}

void rkx120_combtxphy_set_mode(struct rk_serdes *des, enum combtx_phy_mode mode)
{
	struct rkx120_combtxphy *combtxphy = &des->combtxphy;

	combtxphy->mode = mode;
}
