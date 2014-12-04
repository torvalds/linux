/*
 * Copyright 2012-2014 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/imx_sema4.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include "clk.h"
#include "common.h"

/**
 * struct clk_pfd - IMX PFD clock
 * @clk_hw:	clock source
 * @reg:	PFD register address
 * @idx:	the index of PFD encoded in the register
 *
 * PFD clock found on i.MX6 series.  Each register for PFD has 4 clk_pfd
 * data encoded, and member idx is used to specify the one.  And each
 * register has SET, CLR and TOG registers at offset 0x4 0x8 and 0xc.
 */
struct clk_pfd {
	struct clk_hw	hw;
	void __iomem	*reg;
	u8		idx;
};

#define to_clk_pfd(_hw) container_of(_hw, struct clk_pfd, hw)

#define SET	0x4
#define CLR	0x8
#define OTG	0xc

static void clk_pfd_do_hardware(struct clk_pfd *pfd, bool enable)
{
	if (enable)
		writel_relaxed(1 << ((pfd->idx + 1) * 8 - 1), pfd->reg + CLR);
	else
		writel_relaxed(1 << ((pfd->idx + 1) * 8 - 1), pfd->reg + SET);
}

static void clk_pfd_do_shared_clks(struct clk_hw *hw, bool enable)
{
	struct clk_pfd *pfd = to_clk_pfd(hw);

	if (imx_src_is_m4_enabled()) {
		if (!amp_power_mutex || !shared_mem) {
			if (enable)
				clk_pfd_do_hardware(pfd, enable);
			return;
		}

		imx_sema4_mutex_lock(amp_power_mutex);
		if (shared_mem->ca9_valid != SHARED_MEM_MAGIC_NUMBER ||
			shared_mem->cm4_valid != SHARED_MEM_MAGIC_NUMBER) {
			imx_sema4_mutex_unlock(amp_power_mutex);
			return;
		}

		if (!imx_update_shared_mem(hw, enable)) {
			imx_sema4_mutex_unlock(amp_power_mutex);
			return;
		}

		clk_pfd_do_hardware(pfd, enable);

		imx_sema4_mutex_unlock(amp_power_mutex);
	} else {
		clk_pfd_do_hardware(pfd, enable);
	}
}

static int clk_pfd_enable(struct clk_hw *hw)
{
	clk_pfd_do_shared_clks(hw, true);

	return 0;
}

static void clk_pfd_disable(struct clk_hw *hw)
{
	clk_pfd_do_shared_clks(hw, false);
}

static unsigned long clk_pfd_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_pfd *pfd = to_clk_pfd(hw);
	u64 tmp = parent_rate;
	u8 frac = (readl_relaxed(pfd->reg) >> (pfd->idx * 8)) & 0x3f;

	tmp *= 18;
	do_div(tmp, frac);

	return tmp;
}

static long clk_pfd_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *prate)
{
	u64 tmp = *prate;
	u8 frac;

	tmp = tmp * 18 + rate / 2;
	do_div(tmp, rate);
	frac = tmp;
	if (frac < 12)
		frac = 12;
	else if (frac > 35)
		frac = 35;
	tmp = *prate;
	tmp *= 18;
	do_div(tmp, frac);

	return tmp;
}

static int clk_pfd_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pfd *pfd = to_clk_pfd(hw);
	u64 tmp = parent_rate;
	u8 frac;

	tmp = tmp * 18 + rate / 2;
	do_div(tmp, rate);
	frac = tmp;
	if (frac < 12)
		frac = 12;
	else if (frac > 35)
		frac = 35;

	writel_relaxed(0x3f << (pfd->idx * 8), pfd->reg + CLR);
	writel_relaxed(frac << (pfd->idx * 8), pfd->reg + SET);

	return 0;
}

static int clk_pfd_is_enabled(struct clk_hw *hw)
{
	struct clk_pfd *pfd = to_clk_pfd(hw);

	if (readl_relaxed(pfd->reg) & (1 << ((pfd->idx + 1) * 8 - 1)))
		return 0;

	return 1;
}

static const struct clk_ops clk_pfd_ops = {
	.enable		= clk_pfd_enable,
	.disable	= clk_pfd_disable,
	.recalc_rate	= clk_pfd_recalc_rate,
	.round_rate	= clk_pfd_round_rate,
	.set_rate	= clk_pfd_set_rate,
	.is_enabled     = clk_pfd_is_enabled,
};

struct clk *imx_clk_pfd(const char *name, const char *parent_name,
			void __iomem *reg, u8 idx)
{
	struct clk_pfd *pfd;
	struct clk *clk;
	struct clk_init_data init;

	pfd = kzalloc(sizeof(*pfd), GFP_KERNEL);
	if (!pfd)
		return ERR_PTR(-ENOMEM);

	pfd->reg = reg;
	pfd->idx = idx;

	init.name = name;
	init.ops = &clk_pfd_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pfd->hw.init = &init;

	clk = clk_register(NULL, &pfd->hw);
	if (IS_ERR(clk))
		kfree(pfd);

	return clk;
}
