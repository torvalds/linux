/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2014 Broadcom Corporation */

#ifndef _CLK_IPROC_H
#define _CLK_IPROC_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/clk-provider.h>

#define IPROC_CLK_NAME_LEN 25
#define IPROC_CLK_INVALID_OFFSET 0xffffffff
#define bit_mask(width) ((1 << (width)) - 1)

/* clocks that should not be disabled at runtime */
#define IPROC_CLK_AON BIT(0)

/* PLL that requires gating through ASIU */
#define IPROC_CLK_PLL_ASIU BIT(1)

/* PLL that has fractional part of the NDIV */
#define IPROC_CLK_PLL_HAS_NDIV_FRAC BIT(2)

/*
 * Some of the iProc PLL/clocks may have an ASIC bug that requires read back
 * of the same register following the write to flush the write transaction into
 * the intended register
 */
#define IPROC_CLK_NEEDS_READ_BACK BIT(3)

/*
 * Some PLLs require the PLL SW override bit to be set before changes can be
 * applied to the PLL
 */
#define IPROC_CLK_PLL_NEEDS_SW_CFG BIT(4)

/*
 * Some PLLs use a different way to control clock power, via the PWRDWN bit in
 * the PLL control register
 */
#define IPROC_CLK_EMBED_PWRCTRL BIT(5)

/*
 * Some PLLs have separate registers for Status and Control.  Identify this to
 * let the driver know if additional registers need to be used
 */
#define IPROC_CLK_PLL_SPLIT_STAT_CTRL BIT(6)

/*
 * Some PLLs have an additional divide by 2 in master clock calculation;
 * MCLK = VCO_freq / (Mdiv * 2). Identify this to let the driver know
 * of modified calculations
 */
#define IPROC_CLK_MCLK_DIV_BY_2 BIT(7)

/*
 * Some PLLs provide a look up table for the leaf clock frequencies and
 * auto calculates VCO frequency parameters based on the provided leaf
 * clock frequencies. They have a user mode that allows the divider
 * controls to be determined by the user
 */
#define IPROC_CLK_PLL_USER_MODE_ON BIT(8)

/*
 * Some PLLs have an active low reset
 */
#define IPROC_CLK_PLL_RESET_ACTIVE_LOW BIT(9)

/*
 * Calculate the PLL parameters are runtime, instead of using table
 */
#define IPROC_CLK_PLL_CALC_PARAM BIT(10)

/*
 * Parameters for VCO frequency configuration
 *
 * VCO frequency =
 * ((ndiv_int + ndiv_frac / 2^20) * (ref frequency  / pdiv)
 */
struct iproc_pll_vco_param {
	unsigned long rate;
	unsigned int ndiv_int;
	unsigned int ndiv_frac;
	unsigned int pdiv;
};

struct iproc_clk_reg_op {
	unsigned int offset;
	unsigned int shift;
	unsigned int width;
};

/*
 * Clock gating control at the top ASIU level
 */
struct iproc_asiu_gate {
	unsigned int offset;
	unsigned int en_shift;
};

/*
 * Control of powering on/off of a PLL
 *
 * Before powering off a PLL, input isolation (ISO) needs to be enabled
 */
struct iproc_pll_aon_pwr_ctrl {
	unsigned int offset;
	unsigned int pwr_width;
	unsigned int pwr_shift;
	unsigned int iso_shift;
};

/*
 * Control of the PLL reset
 */
struct iproc_pll_reset_ctrl {
	unsigned int offset;
	unsigned int reset_shift;
	unsigned int p_reset_shift;
};

/*
 * Control of the Ki, Kp, and Ka parameters
 */
struct iproc_pll_dig_filter_ctrl {
	unsigned int offset;
	unsigned int ki_shift;
	unsigned int ki_width;
	unsigned int kp_shift;
	unsigned int kp_width;
	unsigned int ka_shift;
	unsigned int ka_width;
};

/*
 * To enable SW control of the PLL
 */
struct iproc_pll_sw_ctrl {
	unsigned int offset;
	unsigned int shift;
};

struct iproc_pll_vco_ctrl {
	unsigned int u_offset;
	unsigned int l_offset;
};

/*
 * Main PLL control parameters
 */
struct iproc_pll_ctrl {
	unsigned long flags;
	struct iproc_pll_aon_pwr_ctrl aon;
	struct iproc_asiu_gate asiu;
	struct iproc_pll_reset_ctrl reset;
	struct iproc_pll_dig_filter_ctrl dig_filter;
	struct iproc_pll_sw_ctrl sw_ctrl;
	struct iproc_clk_reg_op ndiv_int;
	struct iproc_clk_reg_op ndiv_frac;
	struct iproc_clk_reg_op pdiv;
	struct iproc_pll_vco_ctrl vco_ctrl;
	struct iproc_clk_reg_op status;
	struct iproc_clk_reg_op macro_mode;
};

/*
 * Controls enabling/disabling a PLL derived clock
 */
struct iproc_clk_enable_ctrl {
	unsigned int offset;
	unsigned int enable_shift;
	unsigned int hold_shift;
	unsigned int bypass_shift;
};

/*
 * Main clock control parameters for clocks derived from the PLLs
 */
struct iproc_clk_ctrl {
	unsigned int channel;
	unsigned long flags;
	struct iproc_clk_enable_ctrl enable;
	struct iproc_clk_reg_op mdiv;
};

/*
 * Divisor of the ASIU clocks
 */
struct iproc_asiu_div {
	unsigned int offset;
	unsigned int en_shift;
	unsigned int high_shift;
	unsigned int high_width;
	unsigned int low_shift;
	unsigned int low_width;
};

void iproc_armpll_setup(struct device_node *node);
void iproc_pll_clk_setup(struct device_node *node,
			 const struct iproc_pll_ctrl *pll_ctrl,
			 const struct iproc_pll_vco_param *vco,
			 unsigned int num_vco_entries,
			 const struct iproc_clk_ctrl *clk_ctrl,
			 unsigned int num_clks);
void iproc_asiu_setup(struct device_node *node,
		      const struct iproc_asiu_div *div,
		      const struct iproc_asiu_gate *gate,
		      unsigned int num_clks);

#endif /* _CLK_IPROC_H */
