/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QCOM_CLK_KRAIT_H
#define __QCOM_CLK_KRAIT_H

#include <linux/clk-provider.h>

struct krait_mux_clk {
	unsigned int	*parent_map;
	u32		offset;
	u32		mask;
	u32		shift;
	u32		en_mask;
	bool		lpl;
	u8		safe_sel;
	u8		old_index;
	bool		reparent;
	bool		disable_sec_src_gating;

	struct clk_hw	hw;
	struct notifier_block   clk_nb;
};

#define to_krait_mux_clk(_hw) container_of(_hw, struct krait_mux_clk, hw)

extern const struct clk_ops krait_mux_clk_ops;

struct krait_div2_clk {
	u32		offset;
	u8		width;
	u32		shift;
	bool		lpl;

	struct clk_hw	hw;
};

#define to_krait_div2_clk(_hw) container_of(_hw, struct krait_div2_clk, hw)

extern const struct clk_ops krait_div2_clk_ops;

#endif
