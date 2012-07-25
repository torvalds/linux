/*
 * drivers/media/video/smiapp-pll.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef SMIAPP_PLL_H
#define SMIAPP_PLL_H

#include <linux/device.h>

struct smiapp_pll {
	uint8_t lanes;
	uint8_t binning_horizontal;
	uint8_t binning_vertical;
	uint8_t scale_m;
	uint8_t scale_n;
	uint8_t bits_per_pixel;
	uint16_t flags;
	uint32_t link_freq;

	uint16_t pre_pll_clk_div;
	uint16_t pll_multiplier;
	uint16_t op_sys_clk_div;
	uint16_t op_pix_clk_div;
	uint16_t vt_sys_clk_div;
	uint16_t vt_pix_clk_div;

	uint32_t ext_clk_freq_hz;
	uint32_t pll_ip_clk_freq_hz;
	uint32_t pll_op_clk_freq_hz;
	uint32_t op_sys_clk_freq_hz;
	uint32_t op_pix_clk_freq_hz;
	uint32_t vt_sys_clk_freq_hz;
	uint32_t vt_pix_clk_freq_hz;

	uint32_t pixel_rate_csi;
};

struct smiapp_pll_limits {
	/* Strict PLL limits */
	uint32_t min_ext_clk_freq_hz;
	uint32_t max_ext_clk_freq_hz;
	uint16_t min_pre_pll_clk_div;
	uint16_t max_pre_pll_clk_div;
	uint32_t min_pll_ip_freq_hz;
	uint32_t max_pll_ip_freq_hz;
	uint16_t min_pll_multiplier;
	uint16_t max_pll_multiplier;
	uint32_t min_pll_op_freq_hz;
	uint32_t max_pll_op_freq_hz;

	uint16_t min_vt_sys_clk_div;
	uint16_t max_vt_sys_clk_div;
	uint32_t min_vt_sys_clk_freq_hz;
	uint32_t max_vt_sys_clk_freq_hz;
	uint16_t min_vt_pix_clk_div;
	uint16_t max_vt_pix_clk_div;
	uint32_t min_vt_pix_clk_freq_hz;
	uint32_t max_vt_pix_clk_freq_hz;

	uint16_t min_op_sys_clk_div;
	uint16_t max_op_sys_clk_div;
	uint32_t min_op_sys_clk_freq_hz;
	uint32_t max_op_sys_clk_freq_hz;
	uint16_t min_op_pix_clk_div;
	uint16_t max_op_pix_clk_div;
	uint32_t min_op_pix_clk_freq_hz;
	uint32_t max_op_pix_clk_freq_hz;

	/* Other relevant limits */
	uint32_t min_line_length_pck_bin;
	uint32_t min_line_length_pck;
};

/* op pix clock is for all lanes in total normally */
#define SMIAPP_PLL_FLAG_OP_PIX_CLOCK_PER_LANE			(1 << 0)
#define SMIAPP_PLL_FLAG_NO_OP_CLOCKS				(1 << 1)

struct device;

int smiapp_pll_calculate(struct device *dev, struct smiapp_pll_limits *limits,
			 struct smiapp_pll *pll);

#endif /* SMIAPP_PLL_H */
