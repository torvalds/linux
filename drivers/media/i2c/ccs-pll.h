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

#include <linux/bits.h>

/* CSI-2 or CCP-2 */
#define CCS_PLL_BUS_TYPE_CSI2_DPHY				0x00
#define CCS_PLL_BUS_TYPE_CSI2_CPHY				0x01

/* Old SMIA and implementation specific flags */
/* op pix clock is for all lanes in total normally */
#define CCS_PLL_FLAG_OP_PIX_CLOCK_PER_LANE			BIT(0)
#define CCS_PLL_FLAG_NO_OP_CLOCKS				BIT(1)
/* CCS PLL flags */
#define CCS_PLL_FLAG_LANE_SPEED_MODEL				BIT(2)
#define CCS_PLL_FLAG_LINK_DECOUPLED				BIT(3)
#define CCS_PLL_FLAG_EXT_IP_PLL_DIVIDER				BIT(4)
#define CCS_PLL_FLAG_FLEXIBLE_OP_PIX_CLK_DIV			BIT(5)
#define CCS_PLL_FLAG_FIFO_DERATING				BIT(6)
#define CCS_PLL_FLAG_FIFO_OVERRATING				BIT(7)
#define CCS_PLL_FLAG_DUAL_PLL					BIT(8)
#define CCS_PLL_FLAG_OP_SYS_DDR					BIT(9)
#define CCS_PLL_FLAG_OP_PIX_DDR					BIT(10)

/**
 * struct ccs_pll_branch_fr - CCS PLL configuration (front)
 *
 * A single branch front-end of the CCS PLL tree.
 *
 * @pre_pll_clk_div: Pre-PLL clock divisor
 * @pll_multiplier: PLL multiplier
 * @pll_ip_clk_freq_hz: PLL input clock frequency
 * @pll_op_clk_freq_hz: PLL output clock frequency
 */
struct ccs_pll_branch_fr {
	u16 pre_pll_clk_div;
	u16 pll_multiplier;
	u32 pll_ip_clk_freq_hz;
	u32 pll_op_clk_freq_hz;
};

/**
 * struct ccs_pll_branch_bk - CCS PLL configuration (back)
 *
 * A single branch back-end of the CCS PLL tree.
 *
 * @sys_clk_div: System clock divider
 * @pix_clk_div: Pixel clock divider
 * @sys_clk_freq_hz: System clock frequency
 * @pix_clk_freq_hz: Pixel clock frequency
 */
struct ccs_pll_branch_bk {
	u16 sys_clk_div;
	u16 pix_clk_div;
	u32 sys_clk_freq_hz;
	u32 pix_clk_freq_hz;
};

/**
 * struct ccs_pll - Full CCS PLL configuration
 *
 * All information required to calculate CCS PLL configuration.
 *
 * @bus_type: Type of the data bus, CCS_PLL_BUS_TYPE_* (input)
 * @op_lanes: Number of operational lanes (input)
 * @vt_lanes: Number of video timing lanes (input)
 * @csi2: CSI-2 related parameters
 * @csi2.lanes: The number of the CSI-2 data lanes (input)
 * @binning_vertical: Vertical binning factor (input)
 * @binning_horizontal: Horizontal binning factor (input)
 * @scale_m: Downscaling factor, M component, [16, max] (input)
 * @scale_n: Downscaling factor, N component, typically 16 (input)
 * @bits_per_pixel: Bits per pixel on the output data bus (input)
 * @op_bits_per_lane: Number of bits per OP lane (input)
 * @flags: CCS_PLL_FLAG_* (input)
 * @link_freq: Chosen link frequency (input)
 * @ext_clk_freq_hz: External clock frequency, i.e. the sensor's input clock
 *		     (input)
 * @vt_fr: Video timing front-end configuration (output)
 * @vt_bk: Video timing back-end configuration (output)
 * @op_fr: Operational timing front-end configuration (output)
 * @op_bk: Operational timing back-end configuration (output)
 * @pixel_rate_csi: Pixel rate on the output data bus (output)
 * @pixel_rate_pixel_array: Nominal pixel rate in the sensor's pixel array
 *			    (output)
 */
struct ccs_pll {
	/* input values */
	u8 bus_type;
	u8 op_lanes;
	u8 vt_lanes;
	struct {
		u8 lanes;
	} csi2;
	u8 binning_horizontal;
	u8 binning_vertical;
	u8 scale_m;
	u8 scale_n;
	u8 bits_per_pixel;
	u8 op_bits_per_lane;
	u16 flags;
	u32 link_freq;
	u32 ext_clk_freq_hz;

	/* output values */
	struct ccs_pll_branch_fr vt_fr;
	struct ccs_pll_branch_bk vt_bk;
	struct ccs_pll_branch_fr op_fr;
	struct ccs_pll_branch_bk op_bk;

	u32 pixel_rate_csi;
	u32 pixel_rate_pixel_array;
};

/**
 * struct ccs_pll_branch_limits_fr - CCS PLL front-end limits
 *
 * @min_pre_pll_clk_div: Minimum pre-PLL clock divider
 * @max_pre_pll_clk_div: Maximum pre-PLL clock divider
 * @min_pll_ip_clk_freq_hz: Minimum PLL input clock frequency
 * @max_pll_ip_clk_freq_hz: Maximum PLL input clock frequency
 * @min_pll_multiplier: Minimum PLL multiplier
 * @max_pll_multiplier: Maximum PLL multiplier
 * @min_pll_op_clk_freq_hz: Minimum PLL output clock frequency
 * @max_pll_op_clk_freq_hz: Maximum PLL output clock frequency
 */
struct ccs_pll_branch_limits_fr {
	u16 min_pre_pll_clk_div;
	u16 max_pre_pll_clk_div;
	u32 min_pll_ip_clk_freq_hz;
	u32 max_pll_ip_clk_freq_hz;
	u16 min_pll_multiplier;
	u16 max_pll_multiplier;
	u32 min_pll_op_clk_freq_hz;
	u32 max_pll_op_clk_freq_hz;
};

/**
 * struct ccs_pll_branch_limits_bk - CCS PLL back-end limits
 *
 * @min_sys_clk_div: Minimum system clock divider
 * @max_sys_clk_div: Maximum system clock divider
 * @min_sys_clk_freq_hz: Minimum system clock frequency
 * @max_sys_clk_freq_hz: Maximum system clock frequency
 * @min_pix_clk_div: Minimum pixel clock divider
 * @max_pix_clk_div: Maximum pixel clock divider
 * @min_pix_clk_freq_hz: Minimum pixel clock frequency
 * @max_pix_clk_freq_hz: Maximum pixel clock frequency
 */
struct ccs_pll_branch_limits_bk {
	u16 min_sys_clk_div;
	u16 max_sys_clk_div;
	u32 min_sys_clk_freq_hz;
	u32 max_sys_clk_freq_hz;
	u16 min_pix_clk_div;
	u16 max_pix_clk_div;
	u32 min_pix_clk_freq_hz;
	u32 max_pix_clk_freq_hz;
};

/**
 * struct ccs_pll_limits - CCS PLL limits
 *
 * @min_ext_clk_freq_hz: Minimum external clock frequency
 * @max_ext_clk_freq_hz: Maximum external clock frequency
 * @vt_fr: Video timing front-end limits
 * @vt_bk: Video timing back-end limits
 * @op_fr: Operational timing front-end limits
 * @op_bk: Operational timing back-end limits
 * @min_line_length_pck_bin: Minimum line length in pixels, with binning
 * @min_line_length_pck: Minimum line length in pixels without binning
 */
struct ccs_pll_limits {
	/* Strict PLL limits */
	u32 min_ext_clk_freq_hz;
	u32 max_ext_clk_freq_hz;

	struct ccs_pll_branch_limits_fr vt_fr;
	struct ccs_pll_branch_limits_bk vt_bk;
	struct ccs_pll_branch_limits_fr op_fr;
	struct ccs_pll_branch_limits_bk op_bk;

	/* Other relevant limits */
	u32 min_line_length_pck_bin;
	u32 min_line_length_pck;
};

struct device;

/**
 * ccs_pll_calculate - Calculate CCS PLL configuration based on input parameters
 *
 * @dev: Device pointer, used for printing messages
 * @limits: Limits specific to the sensor
 * @pll: Given PLL configuration
 *
 * Calculate the CCS PLL configuration based on the limits as well as given
 * device specific, system specific or user configured input data.
 */
int ccs_pll_calculate(struct device *dev, const struct ccs_pll_limits *limits,
		      struct ccs_pll *pll);

#endif /* CCS_PLL_H */
