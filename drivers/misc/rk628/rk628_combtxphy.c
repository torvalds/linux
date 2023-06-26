// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include "rk628.h"
#include "rk628_combtxphy.h"
#include "rk628_cru.h"

static void rk628_combtxphy_dsi_power_on(struct rk628 *rk628)
{
	struct rk628_combtxphy *combtxphy = &rk628->combtxphy;
	u32 val;
	int ret;

	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0, SW_BUS_WIDTH_MASK |
			      SW_GVI_LVDS_EN_MASK | SW_MIPI_DSI_EN_MASK,
			       SW_BUS_WIDTH_8BIT | SW_MIPI_DSI_EN);

	if (combtxphy->flags & COMBTXPHY_MODULEA_EN)
		rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
				      SW_MODULEA_EN_MASK, SW_MODULEA_EN);

	if (combtxphy->flags & COMBTXPHY_MODULEB_EN)
		rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
				      SW_MODULEB_EN_MASK, SW_MODULEB_EN);

	rk628_i2c_write(rk628, COMBTXPHY_CON5,
			SW_REF_DIV(combtxphy->ref_div - 1) |
			SW_PLL_FB_DIV(combtxphy->fb_div) |
			SW_PLL_FRAC_DIV(combtxphy->frac_div) |
			SW_RATE(combtxphy->rate_div / 2));

	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0, SW_PD_PLL, 0);

	ret = regmap_read_poll_timeout(rk628->regmap[RK628_DEV_GRF],
				       GRF_DPHY0_STATUS, val,
				       val & DPHY_PHYLOCK, 0, 1000);
	if (ret < 0)
		dev_err(rk628->dev, "phy is not lock\n");


	rk628_i2c_update_bits(rk628, COMBTXPHY_CON9,
			      SW_DSI_FSET_EN_MASK | SW_DSI_RCAL_EN_MASK,
			      SW_DSI_FSET_EN | SW_DSI_RCAL_EN);

	usleep_range(200, 400);
}

static void rk628_combtxphy_lvds_power_on(struct rk628 *rk628)
{

	struct rk628_combtxphy *combtxphy = &rk628->combtxphy;
	u32 val;
	int ret;

	/* Adjust terminal resistance 133 ohm, bypass 0.95v ldo for driver. */
	rk628_i2c_update_bits(rk628, COMBTXPHY_CON7,
			      SW_TX_RTERM_MASK | SW_TX_MODE_MASK |
			      BYPASS_095V_LDO_MASK | TX_COM_VOLT_ADJ_MASK,
			      SW_TX_RTERM(6) | SW_TX_MODE(3) |
			      BYPASS_095V_LDO(1) | TX_COM_VOLT_ADJ(0));

	rk628_i2c_write(rk628, COMBTXPHY_CON10, TX7_CKDRV_EN | TX2_CKDRV_EN);
	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
			      SW_BUS_WIDTH_MASK | SW_GVI_LVDS_EN_MASK |
			      SW_MIPI_DSI_EN_MASK,
			      SW_BUS_WIDTH_7BIT | SW_GVI_LVDS_EN);

	if (combtxphy->flags & COMBTXPHY_MODULEA_EN)
		rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
				      SW_MODULEA_EN_MASK, SW_MODULEA_EN);

	if (combtxphy->flags & COMBTXPHY_MODULEB_EN)
		rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
				      SW_MODULEB_EN_MASK, SW_MODULEB_EN);

	rk628_i2c_write(rk628, COMBTXPHY_CON5,
			SW_REF_DIV(combtxphy->ref_div - 1) |
			SW_PLL_FB_DIV(combtxphy->fb_div) |
			SW_PLL_FRAC_DIV(combtxphy->frac_div) |
			SW_RATE(combtxphy->rate_div / 2));

	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
			      SW_PD_PLL, 0);

	ret = regmap_read_poll_timeout(rk628->regmap[RK628_DEV_GRF],
				       GRF_DPHY0_STATUS, val,
				       val & DPHY_PHYLOCK, 0, 1000);
	if (ret < 0)
		dev_err(rk628->dev, "phy is not lock\n");

	usleep_range(100, 200);
	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0, SW_TX_IDLE_MASK | SW_TX_PD_MASK, 0);
}

static void rk628_combtxphy_gvi_power_on(struct rk628 *rk628)
{
	struct rk628_combtxphy *combtxphy = &rk628->combtxphy;
	int ref_div = 0;

	if (combtxphy->ref_div % 2) {
		ref_div = combtxphy->ref_div - 1;
	} else {
		ref_div = BIT(4);
		ref_div |= combtxphy->ref_div / 2 - 1;
	}

	rk628_i2c_write(rk628, COMBTXPHY_CON5,
			SW_REF_DIV(ref_div) |
			SW_PLL_FB_DIV(combtxphy->fb_div) |
			SW_PLL_FRAC_DIV(combtxphy->frac_div) |
			SW_RATE(combtxphy->rate_div / 2));
	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
			      SW_BUS_WIDTH_MASK | SW_GVI_LVDS_EN_MASK |
			      SW_MIPI_DSI_EN_MASK |
			      SW_MODULEB_EN_MASK | SW_MODULEA_EN_MASK,
			      SW_BUS_WIDTH_10BIT | SW_GVI_LVDS_EN |
			      SW_MODULEB_EN | SW_MODULEA_EN);

	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
			      SW_PD_PLL | SW_TX_PD_MASK, 0);
	usleep_range(100, 200);
	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
			      SW_TX_IDLE_MASK, 0);
}

void rk628_combtxphy_power_on(struct rk628 *rk628)
{
	struct rk628_combtxphy *combtxphy = &rk628->combtxphy;

	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0,
			      SW_TX_IDLE_MASK | SW_TX_PD_MASK |
			      SW_PD_PLL_MASK, SW_TX_IDLE(0x3ff) |
			      SW_TX_PD(0x3ff) | SW_PD_PLL);

	switch (combtxphy->mode) {
	case PHY_MODE_VIDEO_MIPI:

		rk628_i2c_update_bits(rk628, GRF_POST_PROC_CON,
				      SW_TXPHY_REFCLK_SEL_MASK,
				      SW_TXPHY_REFCLK_SEL(0));
		rk628_combtxphy_dsi_power_on(rk628);
		break;
	case PHY_MODE_VIDEO_LVDS:
		rk628_i2c_update_bits(rk628, GRF_POST_PROC_CON,
				      SW_TXPHY_REFCLK_SEL_MASK,
				      SW_TXPHY_REFCLK_SEL(1));
		rk628_combtxphy_lvds_power_on(rk628);
		break;
	case PHY_MODE_VIDEO_GVI:
		rk628_i2c_update_bits(rk628, GRF_POST_PROC_CON,
				      SW_TXPHY_REFCLK_SEL_MASK,
				      SW_TXPHY_REFCLK_SEL(2));
		rk628_combtxphy_gvi_power_on(rk628);
		break;
	default:
		break;
	}
}

void rk628_combtxphy_power_off(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, COMBTXPHY_CON0, SW_TX_IDLE_MASK |
			      SW_TX_PD_MASK | SW_PD_PLL_MASK |
			      SW_MODULEB_EN_MASK | SW_MODULEA_EN_MASK,
			      SW_TX_IDLE(0x3ff) | SW_TX_PD(0x3ff) | SW_PD_PLL);
}

void rk628_combtxphy_set_bus_width(struct rk628 *rk628, u32 bus_width)
{
	rk628->combtxphy.bus_width = bus_width;
}

u32 rk628_combtxphy_get_bus_width(struct rk628 *rk628)
{
	return rk628->combtxphy.bus_width;
}

void rk628_combtxphy_set_gvi_division_mode(struct rk628 *rk628, bool division)
{
	rk628->combtxphy.division_mode = division;
}

void rk628_combtxphy_set_mode(struct rk628 *rk628, enum phy_mode mode)
{
	struct rk628_combtxphy *combtxphy = &rk628->combtxphy;
	unsigned int fvco, fpfd, frac_rate, fin = 24;

	switch (mode) {
	case PHY_MODE_VIDEO_MIPI:
	{
		int bus_width = rk628_combtxphy_get_bus_width(rk628);
		unsigned int fhsc = bus_width >> 8;
		unsigned int flags = bus_width & 0xff;

		fhsc = fin * (fhsc / fin);
		if (fhsc < 80 || fhsc > 1500)
			return;
		else if (fhsc < 375)
			combtxphy->rate_div = 4;
		else if (fhsc < 750)
			combtxphy->rate_div = 2;
		else
			combtxphy->rate_div = 1;

		combtxphy->flags = flags;

		fvco = fhsc * 2 * combtxphy->rate_div;
		combtxphy->ref_div = 1;
		combtxphy->fb_div = fvco / 8 / fin;
		frac_rate = fvco - (fin * 8 * combtxphy->fb_div);
		if (frac_rate) {
			frac_rate <<= 10;
			frac_rate /= fin * 8;
			combtxphy->frac_div = frac_rate;
		} else {
			combtxphy->frac_div = 0;
		}

		fvco = fin * (1024 * combtxphy->fb_div + combtxphy->frac_div);
		fvco *= 8;
		fvco = DIV_ROUND_UP(fvco, 1024 * combtxphy->ref_div);
		fhsc = fvco / 2 / combtxphy->rate_div;
		combtxphy->bus_width = fhsc;

		break;
	}
	case PHY_MODE_VIDEO_LVDS:
	{
		int bus_width = rk628_combtxphy_get_bus_width(rk628);
		unsigned int flags = bus_width & 0xff;
		unsigned int rate = (bus_width >> 8) * 7;

		combtxphy->flags = flags;
		combtxphy->ref_div = 1;
		combtxphy->fb_div = 14;
		combtxphy->frac_div = 0;

		if (rate < 500)
			combtxphy->rate_div = 4;
		else if (rate < 1000)
			combtxphy->rate_div = 2;
		else
			combtxphy->rate_div = 1;
		break;
	}
	case PHY_MODE_VIDEO_GVI:
	{
		unsigned int i, delta_freq, best_delta_freq, fb_div;
		unsigned int bus_width = rk628_combtxphy_get_bus_width(rk628);
		unsigned long ref_clk;
		unsigned long long pre_clk;

		if (bus_width < 500000 || bus_width > 4000000)
			return;
		else if (bus_width < 1000000)
			combtxphy->rate_div = 4;
		else if (bus_width < 2000000)
			combtxphy->rate_div = 2;
		else
			combtxphy->rate_div = 1;
		fvco = bus_width * combtxphy->rate_div;
		ref_clk = rk628_cru_clk_get_rate(rk628, CGU_SCLK_VOP) / 1000; /* khz */
		if (combtxphy->division_mode)
			ref_clk /= 2;
		/*
		 * the reference clock at PFD(FPFD = ref_clk / ref_div) about
		 * 25MHz is recommende, FPFD must range from 16MHz to 35MHz,
		 * here to find the best rev_div.
		 */
		best_delta_freq = ref_clk;
		for (i = 1; i <= 32; i++) {
			fpfd = ref_clk / i;
			delta_freq = abs(fpfd - 25000);
			if (delta_freq < best_delta_freq) {
				best_delta_freq = delta_freq;
				combtxphy->ref_div = i;
			}
		}

		/*
		 * ref_clk / ref_div * 8 * fb_div = FVCO
		 */
		pre_clk = (unsigned long long)fvco / 8 * combtxphy->ref_div * 1024;
		do_div(pre_clk, ref_clk);
		fb_div = pre_clk / 1024;

		/*
		 * get the actually frequency
		 */
		bus_width = ref_clk / combtxphy->ref_div * 8;
		bus_width *= fb_div;
		bus_width /= combtxphy->rate_div;

		combtxphy->frac_div = 0;
		combtxphy->fb_div = fb_div;

		combtxphy->bus_width = bus_width;
		break;
	}
	default:
		break;
	}

	combtxphy->mode = mode;
}
