// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#include <linux/delay.h>
#include "rk628.h"
#include "rk628_combtxphy.h"
#include "rk628_cru.h"

void rk628_txphy_set_bus_width(struct rk628 *rk628, u32 bus_width)
{
	struct rk628_combtxphy *txphy = rk628->txphy;

	txphy->bus_width = bus_width;
}
EXPORT_SYMBOL(rk628_txphy_set_bus_width);

u32 rk628_txphy_get_bus_width(struct rk628 *rk628)
{
	struct rk628_combtxphy *txphy = rk628->txphy;

	return txphy->bus_width;
}
EXPORT_SYMBOL(rk628_txphy_get_bus_width);

static void rk628_combtxphy_dsi_power_on(struct rk628 *rk628)
{
	struct rk628_combtxphy *txphy = rk628->txphy;

	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_BUS_WIDTH_MASK |
			      SW_GVI_LVDS_EN_MASK |
			      SW_MIPI_DSI_EN_MASK,
			      SW_BUS_WIDTH_8BIT | SW_MIPI_DSI_EN);

	if (txphy->flags & COMBTXPHY_MODULEA_EN)
		rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_MODULEA_EN_MASK,
				      SW_MODULEA_EN);
	if (txphy->flags & COMBTXPHY_MODULEB_EN)
		rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_MODULEB_EN_MASK,
				      SW_MODULEB_EN);

	rk628_i2c_write(rk628,  COMBTXPHY_CON5, SW_REF_DIV(txphy->ref_div - 1) |
			SW_PLL_FB_DIV(txphy->fb_div) |
			SW_PLL_FRAC_DIV(txphy->frac_div) |
			SW_RATE(txphy->rate_div / 2));
	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_PD_PLL, 0);
	usleep_range(100, 200);
	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON9, SW_DSI_FSET_EN_MASK |
			      SW_DSI_RCAL_EN_MASK, SW_DSI_FSET_EN |
			      SW_DSI_RCAL_EN);
	usleep_range(100, 200);
}

static void rk628_combtxphy_lvds_power_on(struct rk628 *rk628)
{
	struct rk628_combtxphy *txphy = rk628->txphy;

	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON7, SW_TX_MODE_MASK, SW_TX_MODE(3));
	rk628_i2c_write(rk628,  COMBTXPHY_CON10, TX7_CKDRV_EN | TX2_CKDRV_EN);
	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_BUS_WIDTH_MASK |
			      SW_GVI_LVDS_EN_MASK | SW_MIPI_DSI_EN_MASK,
			      SW_BUS_WIDTH_7BIT | SW_GVI_LVDS_EN);

	if (txphy->flags & COMBTXPHY_MODULEA_EN)
		rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_MODULEA_EN_MASK,
				      SW_MODULEA_EN);
	if (txphy->flags & COMBTXPHY_MODULEB_EN)
		rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_MODULEB_EN_MASK,
				      SW_MODULEB_EN);

	rk628_i2c_write(rk628,  COMBTXPHY_CON5, SW_REF_DIV(txphy->ref_div - 1) |
			SW_PLL_FB_DIV(txphy->fb_div) |
			SW_PLL_FRAC_DIV(txphy->frac_div) |
			SW_RATE(txphy->rate_div / 2));
	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_PD_PLL | SW_TX_PD_MASK, 0);
	usleep_range(100, 200);
	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_TX_IDLE_MASK, 0);
}

static void rk628_combtxphy_gvi_power_on(struct rk628 *rk628)
{
	struct rk628_combtxphy *txphy = rk628->txphy;

	rk628_i2c_write(rk628,  COMBTXPHY_CON5, SW_REF_DIV(txphy->ref_div - 1) |
			SW_PLL_FB_DIV(txphy->fb_div) |
			SW_PLL_FRAC_DIV(txphy->frac_div) |
			SW_RATE(txphy->rate_div / 2));
	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_BUS_WIDTH_MASK |
			      SW_GVI_LVDS_EN_MASK | SW_MIPI_DSI_EN_MASK |
			      SW_MODULEB_EN_MASK | SW_MODULEA_EN_MASK,
			      SW_BUS_WIDTH_10BIT | SW_GVI_LVDS_EN |
			      SW_MODULEB_EN | SW_MODULEA_EN);
	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_PD_PLL | SW_TX_PD_MASK, 0);
	usleep_range(100, 200);
	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_TX_IDLE_MASK, 0);
}

void rk628_txphy_set_mode(struct rk628 *rk628, enum phy_mode mode)
{
	unsigned int fvco, frac_rate, fin = 24;
	struct rk628_combtxphy *txphy = rk628->txphy;

	switch (mode) {
	case PHY_MODE_VIDEO_MIPI:
	{
		int bus_width = rk628_txphy_get_bus_width(rk628);
		unsigned int fhsc = bus_width >> 8;
		unsigned int flags = bus_width & 0xff;

		fhsc = fin * (fhsc / fin);
		if (fhsc < 80 || fhsc > 1500)
			return;
		else if (fhsc < 375)
			txphy->rate_div = 4;
		else if (fhsc < 750)
			txphy->rate_div = 2;
		else
			txphy->rate_div = 1;

		txphy->flags = flags;

		fvco = fhsc * 2 * txphy->rate_div;
		txphy->ref_div = 1;
		txphy->fb_div = fvco / 8 / fin;
		frac_rate = fvco - (fin * 8 * txphy->fb_div);
		if (frac_rate) {
			frac_rate <<= 10;
			frac_rate /= fin * 8;
			txphy->frac_div = frac_rate;
		} else {
			txphy->frac_div = 0;
		}

		fvco = fin * (1024 * txphy->fb_div + txphy->frac_div);
		fvco *= 8;
		fvco = DIV_ROUND_UP(fvco, 1024 * txphy->ref_div);
		fhsc = fvco / 2 / txphy->rate_div;
		txphy->bus_width = fhsc;

		break;
	}
	case PHY_MODE_VIDEO_LVDS:
	{
		int bus_width = rk628_txphy_get_bus_width(rk628);
		unsigned int flags = bus_width & 0xff;
		unsigned int rate = (bus_width >> 8) * 7;

		txphy->flags = flags;
		txphy->ref_div = 1;
		txphy->fb_div = 14;
		txphy->frac_div = 0;

		if (rate < 500)
			txphy->rate_div = 4;
		else if (rate < 1000)
			txphy->rate_div = 2;
		else
			txphy->rate_div = 1;
		break;
	}
	case PHY_MODE_VIDEO_GVI:
	{
		unsigned int fhsc = rk628_txphy_get_bus_width(rk628) & 0xfff;

		if (fhsc < 500 || fhsc > 4000)
			return;
		else if (fhsc < 1000)
			txphy->rate_div = 4;
		else if (fhsc < 2000)
			txphy->rate_div = 2;
		else
			txphy->rate_div = 1;
		fvco = fhsc * txphy->rate_div;

		txphy->ref_div = 1;
		txphy->fb_div = fvco / 8 / fin;
		frac_rate = fvco - (fin * 8 * txphy->fb_div);

		if (frac_rate) {
			frac_rate <<= 10;
			frac_rate /= fin * 8;
			txphy->frac_div = frac_rate;
		} else {
			txphy->frac_div = 0;
		}

		fvco = fin * (1024 * txphy->fb_div + txphy->frac_div);
		fvco *= 8;
		fvco /= 1024 * txphy->ref_div;
		fhsc = fvco / txphy->rate_div;
		txphy->bus_width = fhsc;
		break;
	}
	default:
		break;
	}

	txphy->mode = mode;
}
EXPORT_SYMBOL(rk628_txphy_set_mode);

void rk628_txphy_power_on(struct rk628 *rk628)
{
	struct rk628_combtxphy *txphy = rk628->txphy;

	rk628_control_assert(rk628, RGU_TXPHY_CON);
	udelay(10);
	rk628_control_deassert(rk628, RGU_TXPHY_CON);
	udelay(10);

	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_TX_IDLE_MASK | SW_TX_PD_MASK |
			      SW_PD_PLL_MASK, SW_TX_IDLE(0x3ff) |
			      SW_TX_PD(0x3ff) | SW_PD_PLL);

	switch (txphy->mode) {
	case PHY_MODE_VIDEO_MIPI:

		rk628_i2c_update_bits(rk628,  GRF_POST_PROC_CON,
				      SW_TXPHY_REFCLK_SEL_MASK,
				      SW_TXPHY_REFCLK_SEL(0));
		rk628_combtxphy_dsi_power_on(rk628);
		break;
	case PHY_MODE_VIDEO_LVDS:
		rk628_i2c_update_bits(rk628,  GRF_POST_PROC_CON,
				      SW_TXPHY_REFCLK_SEL_MASK,
				      SW_TXPHY_REFCLK_SEL(1));
		rk628_combtxphy_lvds_power_on(rk628);
		break;
	case PHY_MODE_VIDEO_GVI:
		rk628_i2c_update_bits(rk628,  GRF_POST_PROC_CON,
				      SW_TXPHY_REFCLK_SEL_MASK,
				      SW_TXPHY_REFCLK_SEL(0));
		rk628_combtxphy_gvi_power_on(rk628);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(rk628_txphy_power_on);

void rk628_txphy_power_off(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628,  COMBTXPHY_CON0, SW_TX_IDLE_MASK | SW_TX_PD_MASK |
			      SW_PD_PLL_MASK | SW_MODULEB_EN_MASK |
			      SW_MODULEA_EN_MASK, SW_TX_IDLE(0x3ff) |
			      SW_TX_PD(0x3ff) | SW_PD_PLL);
}
EXPORT_SYMBOL(rk628_txphy_power_off);

struct rk628_combtxphy *rk628_txphy_register(struct rk628 *rk628)
{
	struct rk628_combtxphy *txphy;

	txphy = devm_kzalloc(rk628->dev, sizeof(*txphy), GFP_KERNEL);
	if (!txphy)
		return NULL;

	rk628->txphy = txphy;

	return txphy;
}
EXPORT_SYMBOL(rk628_txphy_register);
