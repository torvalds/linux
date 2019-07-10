/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/media/i2c/smiapp-pll.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 */

#ifndef SMIAPP_PLL_H
#define SMIAPP_PLL_H

/* CSI-2 or CCP-2 */
#define SMIAPP_PLL_BUS_TYPE_CSI2				0x00
#define SMIAPP_PLL_BUS_TYPE_PARALLEL				0x01

/* op pix clock is for all lanes in total normally */
#define SMIAPP_PLL_FLAG_OP_PIX_CLOCK_PER_LANE			(1 << 0)
#define SMIAPP_PLL_FLAG_NO_OP_CLOCKS				(1 << 1)

struct smiapp_pll_branch {
	uint16_t sys_clk_div;
	uint16_t pix_clk_div;
	uint32_t sys_clk_freq_hz;
	uint32_t pix_clk_freq_hz;
};

struct smiapp_pll {
	/* input values */
	uint8_t bus_type;
	union {
		struct {
			uint8_t lanes;
		} csi2;
		struct {
			uint8_t bus_width;
		} parallel;
	};
	unsigned long flags;
	uint8_t binning_horizontal;
	uint8_t binning_vertical;
	uint8_t scale_m;
	uint8_t scale_n;
	uint8_t bits_per_pixel;
	uint32_t link_freq;
	uint32_t ext_clk_freq_hz;

	/* output values */
	uint16_t pre_pll_clk_div;
	uint16_t pll_multiplier;
	uint32_t pll_ip_clk_freq_hz;
	uint32_t pll_op_clk_freq_hz;
	struct smiapp_pll_branch vt;
	struct smiapp_pll_branch op;

	uint32_t pixel_rate_csi;
	uint32_t pixel_rate_pixel_array;
};

struct smiapp_pll_branch_limits {
	uint16_t min_sys_clk_div;
	uint16_t max_sys_clk_div;
	uint32_t min_sys_clk_freq_hz;
	uint32_t max_sys_clk_freq_hz;
	uint16_t min_pix_clk_div;
	uint16_t max_pix_clk_div;
	uint32_t min_pix_clk_freq_hz;
	uint32_t max_pix_clk_freq_hz;
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

	struct smiapp_pll_branch_limits vt;
	struct smiapp_pll_branch_limits op;

	/* Other relevant limits */
	uint32_t min_line_length_pck_bin;
	uint32_t min_line_length_pck;
};

struct device;

int smiapp_pll_calculate(struct device *dev,
			 const struct smiapp_pll_limits *limits,
			 struct smiapp_pll *pll);

#endif /* SMIAPP_PLL_H */
