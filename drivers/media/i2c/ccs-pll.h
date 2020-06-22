/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/media/i2c/ccs-pll.h
 *
 * Generic MIPI CCS/SMIA/SMIA++ PLL calculator
 *
 * Copyright (C) 2020 Intel Corporation
 * Copyright (C) 2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@linux.intel.com>
 */

#ifndef CCS_PLL_H
#define CCS_PLL_H

/* CSI-2 or CCP-2 */
#define CCS_PLL_BUS_TYPE_CSI2_DPHY				0x00
#define CCS_PLL_BUS_TYPE_CSI2_CPHY				0x01

/* op pix clock is for all lanes in total normally */
#define CCS_PLL_FLAG_OP_PIX_CLOCK_PER_LANE			(1 << 0)
#define CCS_PLL_FLAG_NO_OP_CLOCKS				(1 << 1)

struct ccs_pll_branch_fr {
	uint16_t pre_pll_clk_div;
	uint16_t pll_multiplier;
	uint32_t pll_ip_clk_freq_hz;
	uint32_t pll_op_clk_freq_hz;
};

struct ccs_pll_branch_bk {
	uint16_t sys_clk_div;
	uint16_t pix_clk_div;
	uint32_t sys_clk_freq_hz;
	uint32_t pix_clk_freq_hz;
};

struct ccs_pll {
	/* input values */
	uint8_t bus_type;
	struct {
		uint8_t lanes;
	} csi2;
	unsigned long flags;
	uint8_t binning_horizontal;
	uint8_t binning_vertical;
	uint8_t scale_m;
	uint8_t scale_n;
	uint8_t bits_per_pixel;
	uint32_t link_freq;
	uint32_t ext_clk_freq_hz;

	/* output values */
	struct ccs_pll_branch_fr vt_fr;
	struct ccs_pll_branch_bk vt_bk;
	struct ccs_pll_branch_bk op_bk;

	uint32_t pixel_rate_csi;
	uint32_t pixel_rate_pixel_array;
};

struct ccs_pll_branch_limits_fr {
	uint16_t min_pre_pll_clk_div;
	uint16_t max_pre_pll_clk_div;
	uint32_t min_pll_ip_clk_freq_hz;
	uint32_t max_pll_ip_clk_freq_hz;
	uint16_t min_pll_multiplier;
	uint16_t max_pll_multiplier;
	uint32_t min_pll_op_clk_freq_hz;
	uint32_t max_pll_op_clk_freq_hz;
};

struct ccs_pll_branch_limits_bk {
	uint16_t min_sys_clk_div;
	uint16_t max_sys_clk_div;
	uint32_t min_sys_clk_freq_hz;
	uint32_t max_sys_clk_freq_hz;
	uint16_t min_pix_clk_div;
	uint16_t max_pix_clk_div;
	uint32_t min_pix_clk_freq_hz;
	uint32_t max_pix_clk_freq_hz;
};

struct ccs_pll_limits {
	/* Strict PLL limits */
	uint32_t min_ext_clk_freq_hz;
	uint32_t max_ext_clk_freq_hz;

	struct ccs_pll_branch_limits_fr vt_fr;
	struct ccs_pll_branch_limits_bk vt_bk;
	struct ccs_pll_branch_limits_bk op_bk;

	/* Other relevant limits */
	uint32_t min_line_length_pck_bin;
	uint32_t min_line_length_pck;
};

struct device;

int ccs_pll_calculate(struct device *dev, const struct ccs_pll_limits *limits,
		      struct ccs_pll *pll);

#endif /* CCS_PLL_H */
