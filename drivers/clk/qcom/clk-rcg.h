/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __QCOM_CLK_RCG_H__
#define __QCOM_CLK_RCG_H__

#include <linux/clk-provider.h>
#include "clk-regmap.h"

struct freq_tbl {
	unsigned long freq;
	u8 src;
	u8 pre_div;
	u16 m;
	u16 n;
};

/**
 * struct parent_map - map table for PLL source select configuration values
 * @src: source PLL
 * @cfg: configuration value
 */
struct parent_map {
	u8 src;
	u8 cfg;
};

/**
 * struct mn - M/N:D counter
 * @mnctr_en_bit: bit to enable mn counter
 * @mnctr_reset_bit: bit to assert mn counter reset
 * @mnctr_mode_shift: lowest bit of mn counter mode field
 * @n_val_shift: lowest bit of n value field
 * @m_val_shift: lowest bit of m value field
 * @width: number of bits in m/n/d values
 * @reset_in_cc: true if the mnctr_reset_bit is in the CC register
 */
struct mn {
	u8		mnctr_en_bit;
	u8		mnctr_reset_bit;
	u8		mnctr_mode_shift;
#define MNCTR_MODE_DUAL 0x2
#define MNCTR_MODE_MASK 0x3
	u8		n_val_shift;
	u8		m_val_shift;
	u8		width;
	bool		reset_in_cc;
};

/**
 * struct pre_div - pre-divider
 * @pre_div_shift: lowest bit of pre divider field
 * @pre_div_width: number of bits in predivider
 */
struct pre_div {
	u8		pre_div_shift;
	u8		pre_div_width;
};

/**
 * struct src_sel - source selector
 * @src_sel_shift: lowest bit of source selection field
 * @parent_map: map from software's parent index to hardware's src_sel field
 */
struct src_sel {
	u8		src_sel_shift;
#define SRC_SEL_MASK	0x7
	const struct parent_map	*parent_map;
};

/**
 * struct clk_rcg - root clock generator
 *
 * @ns_reg: NS register
 * @md_reg: MD register
 * @mn: mn counter
 * @p: pre divider
 * @s: source selector
 * @freq_tbl: frequency table
 * @clkr: regmap clock handle
 * @lock: register lock
 *
 */
struct clk_rcg {
	u32		ns_reg;
	u32		md_reg;

	struct mn	mn;
	struct pre_div	p;
	struct src_sel	s;

	const struct freq_tbl	*freq_tbl;

	struct clk_regmap	clkr;
};

extern const struct clk_ops clk_rcg_ops;
extern const struct clk_ops clk_rcg_bypass_ops;
extern const struct clk_ops clk_rcg_bypass2_ops;
extern const struct clk_ops clk_rcg_pixel_ops;
extern const struct clk_ops clk_rcg_esc_ops;
extern const struct clk_ops clk_rcg_lcc_ops;

#define to_clk_rcg(_hw) container_of(to_clk_regmap(_hw), struct clk_rcg, clkr)

/**
 * struct clk_dyn_rcg - root clock generator with glitch free mux
 *
 * @mux_sel_bit: bit to switch glitch free mux
 * @ns_reg: NS0 and NS1 register
 * @md_reg: MD0 and MD1 register
 * @bank_reg: register to XOR @mux_sel_bit into to switch glitch free mux
 * @mn: mn counter (banked)
 * @s: source selector (banked)
 * @freq_tbl: frequency table
 * @clkr: regmap clock handle
 * @lock: register lock
 *
 */
struct clk_dyn_rcg {
	u32	ns_reg[2];
	u32	md_reg[2];
	u32	bank_reg;

	u8	mux_sel_bit;

	struct mn	mn[2];
	struct pre_div	p[2];
	struct src_sel	s[2];

	const struct freq_tbl *freq_tbl;

	struct clk_regmap clkr;
};

extern const struct clk_ops clk_dyn_rcg_ops;

#define to_clk_dyn_rcg(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_dyn_rcg, clkr)

/**
 * struct clk_rcg2 - root clock generator
 *
 * @cmd_rcgr: corresponds to *_CMD_RCGR
 * @mnd_width: number of bits in m/n/d values
 * @hid_width: number of bits in half integer divider
 * @parent_map: map from software's parent index to hardware's src_sel field
 * @freq_tbl: frequency table
 * @clkr: regmap clock handle
 *
 */
struct clk_rcg2 {
	u32			cmd_rcgr;
	u8			mnd_width;
	u8			hid_width;
	const struct parent_map	*parent_map;
	const struct freq_tbl	*freq_tbl;
	struct clk_regmap	clkr;
};

#define to_clk_rcg2(_hw) container_of(to_clk_regmap(_hw), struct clk_rcg2, clkr)

extern const struct clk_ops clk_rcg2_ops;
extern const struct clk_ops clk_rcg2_floor_ops;
extern const struct clk_ops clk_edp_pixel_ops;
extern const struct clk_ops clk_byte_ops;
extern const struct clk_ops clk_byte2_ops;
extern const struct clk_ops clk_pixel_ops;
extern const struct clk_ops clk_gfx3d_ops;

#endif
