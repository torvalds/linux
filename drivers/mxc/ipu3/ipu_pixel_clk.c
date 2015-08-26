/*
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file ipu_pixel_clk.c
 *
 * @brief IPU pixel clock implementation
 *
 * @ingroup IPU
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ipu-v3.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ipu_prv.h"
#include "ipu_regs.h"

 /*
 * muxd clock implementation
 */
struct clk_di_mux {
	struct clk_hw hw;
	u8		ipu_id;
	u8		di_id;
	u8		flags;
	u8		index;
};
#define to_clk_di_mux(_hw) container_of(_hw, struct clk_di_mux, hw)

static int _ipu_pixel_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_di_mux *mux = to_clk_di_mux(hw);
	struct ipu_soc *ipu = ipu_get_soc(mux->ipu_id);
	u32 di_gen;

	di_gen = ipu_di_read(ipu, mux->di_id, DI_GENERAL);
	if (index == 0)
		/* ipu1_clk or ipu2_clk internal clk */
		di_gen &= ~DI_GEN_DI_CLK_EXT;
	else
		di_gen |= DI_GEN_DI_CLK_EXT;

	ipu_di_write(ipu, mux->di_id, di_gen, DI_GENERAL);
	mux->index = index;
	pr_debug("ipu_pixel_clk: di_clk_ext:0x%x, di_gen reg:0x%x.\n",
			!(di_gen & DI_GEN_DI_CLK_EXT), di_gen);
	return 0;
}

static u8 _ipu_pixel_clk_get_parent(struct clk_hw *hw)
{
	struct clk_di_mux *mux = to_clk_di_mux(hw);

	return mux->index;
}

const struct clk_ops clk_mux_di_ops = {
	.get_parent = _ipu_pixel_clk_get_parent,
	.set_parent = _ipu_pixel_clk_set_parent,
};

struct clk *clk_register_mux_pix_clk(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents, unsigned long flags,
		u8 ipu_id, u8 di_id, u8 clk_mux_flags)
{
	struct clk_di_mux *mux;
	struct clk *clk;
	struct clk_init_data init;

	mux = kzalloc(sizeof(struct clk_di_mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_mux_di_ops;
	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	mux->ipu_id = ipu_id;
	mux->di_id = di_id;
	mux->flags = clk_mux_flags | CLK_SET_RATE_PARENT;
	mux->hw.init = &init;

	clk = clk_register(dev, &mux->hw);
	if (IS_ERR(clk))
		kfree(mux);

	return clk;
}

/*
 * Gated clock implementation
 */
struct clk_di_div {
	struct clk_hw hw;
	u8		ipu_id;
	u8		di_id;
	u8		flags;
};
#define to_clk_di_div(_hw) container_of(_hw, struct clk_di_div, hw)

static unsigned long _ipu_pixel_clk_div_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_di_div *di_div = to_clk_di_div(hw);
	struct ipu_soc *ipu = ipu_get_soc(di_div->ipu_id);
	u32 div;
	u64 final_rate = (unsigned long long)parent_rate * 16;

	_ipu_get(ipu);
	div = ipu_di_read(ipu, di_div->di_id, DI_BS_CLKGEN0);
	_ipu_put(ipu);
	pr_debug("ipu_di%d read BS_CLKGEN0 div:%d, final_rate:%lld, prate:%ld\n",
			di_div->di_id, div, final_rate, parent_rate);

	if (div == 0)
		return 0;
	do_div(final_rate, div);

	return (unsigned long)final_rate;
}

static long _ipu_pixel_clk_div_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_clk_rate)
{
	u64 div, final_rate;
	u32 remainder;
	u64 parent_rate = (unsigned long long)(*parent_clk_rate) * 16;

	/*
	 * Calculate divider
	 * Fractional part is 4 bits,
	 * so simply multiply by 2^4 to get fractional part.
	 */
	div = parent_rate;
	remainder = do_div(div, rate);
	/* Round the divider value */
	if (remainder > (rate/2))
		div++;
	if (div < 0x10)            /* Min DI disp clock divider is 1 */
		div = 0x10;
	if (div & ~0xFEF)
		div &= 0xFF8;
	else {
		/* Round up divider if it gets us closer to desired pix clk */
		if ((div & 0xC) == 0xC) {
			div += 0x10;
			div &= ~0xF;
		}
	}
	final_rate = parent_rate;
	do_div(final_rate, div);

	return final_rate;
}

static int _ipu_pixel_clk_div_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_clk_rate)
{
	struct clk_di_div *di_div = to_clk_di_div(hw);
	struct ipu_soc *ipu = ipu_get_soc(di_div->ipu_id);
	u64 div, parent_rate;
	u32 remainder;

	parent_rate = (unsigned long long)parent_clk_rate * 16;
	div = parent_rate;
	remainder = do_div(div, rate);
	/* Round the divider value */
	if (remainder > (rate/2))
		div++;

	/* Round up divider if it gets us closer to desired pix clk */
	if ((div & 0xC) == 0xC) {
		div += 0x10;
		div &= ~0xF;
	}
	if (div > 0x1000)
		pr_err("Overflow, di:%d, DI_BS_CLKGEN0 div:0x%x\n",
				di_div->di_id, (u32)div);
	_ipu_get(ipu);
	ipu_di_write(ipu, di_div->di_id, (u32)div, DI_BS_CLKGEN0);

	/* Setup pixel clock timing */
	/* FIXME: needs to be more flexible */
	/* Down time is half of period */
	ipu_di_write(ipu, di_div->di_id, ((u32)div / 16) << 16, DI_BS_CLKGEN1);
	_ipu_put(ipu);

	return 0;
}

static struct clk_ops clk_div_ops = {
	.recalc_rate = _ipu_pixel_clk_div_recalc_rate,
	.round_rate = _ipu_pixel_clk_div_round_rate,
	.set_rate = _ipu_pixel_clk_div_set_rate,
};

struct clk *clk_register_div_pix_clk(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		u8 ipu_id, u8 di_id, u8 clk_div_flags)
{
	struct clk_di_div *di_div;
	struct clk *clk;
	struct clk_init_data init;

	di_div = kzalloc(sizeof(struct clk_di_div), GFP_KERNEL);
	if (!di_div)
		return ERR_PTR(-ENOMEM);

	/* struct clk_di_div assignments */
	di_div->ipu_id = ipu_id;
	di_div->di_id = di_id;
	di_div->flags = clk_div_flags;

	init.name = name;
	init.ops = &clk_div_ops;
	init.flags = flags | CLK_SET_RATE_PARENT;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	di_div->hw.init = &init;

	clk = clk_register(dev, &di_div->hw);
	if (IS_ERR(clk))
		kfree(clk);

	return clk;
}

/*
 * Gated clock implementation
 */
struct clk_di_gate {
	struct clk_hw hw;
	u8		ipu_id;
	u8		di_id;
	u8		flags;
};
#define to_clk_di_gate(_hw) container_of(_hw, struct clk_di_gate, hw)

static int _ipu_pixel_clk_enable(struct clk_hw *hw)
{
	struct clk_di_gate *gate = to_clk_di_gate(hw);
	struct ipu_soc *ipu = ipu_get_soc(gate->ipu_id);
	u32 disp_gen;

	disp_gen = ipu_cm_read(ipu, IPU_DISP_GEN);
	disp_gen |= gate->di_id ? DI1_COUNTER_RELEASE : DI0_COUNTER_RELEASE;
	ipu_cm_write(ipu, disp_gen, IPU_DISP_GEN);

	return 0;
}

static void _ipu_pixel_clk_disable(struct clk_hw *hw)
{
	struct clk_di_gate *gate = to_clk_di_gate(hw);
	struct ipu_soc *ipu = ipu_get_soc(gate->ipu_id);
	u32 disp_gen;

	disp_gen = ipu_cm_read(ipu, IPU_DISP_GEN);
	disp_gen &= gate->di_id ? ~DI1_COUNTER_RELEASE : ~DI0_COUNTER_RELEASE;
	ipu_cm_write(ipu, disp_gen, IPU_DISP_GEN);

}


static struct clk_ops clk_gate_di_ops = {
	.enable = _ipu_pixel_clk_enable,
	.disable = _ipu_pixel_clk_disable,
};

struct clk *clk_register_gate_pix_clk(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		u8 ipu_id, u8 di_id, u8 clk_gate_flags)
{
	struct clk_di_gate *gate;
	struct clk *clk;
	struct clk_init_data init;

	gate = kzalloc(sizeof(struct clk_di_gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->ipu_id = ipu_id;
	gate->di_id = di_id;
	gate->flags = clk_gate_flags;

	init.name = name;
	init.ops = &clk_gate_di_ops;
	init.flags = flags | CLK_SET_RATE_PARENT;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	gate->hw.init = &init;

	clk = clk_register(dev, &gate->hw);
	if (IS_ERR(clk))
		kfree(clk);

	return clk;
}
