/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <soc/tegra/fuse.h>

#include "clk.h"

static DEFINE_SPINLOCK(periph_ref_lock);

/* Macros to assist peripheral gate clock */
#define read_enb(gate) \
	readl_relaxed(gate->clk_base + (gate->regs->enb_reg))
#define write_enb_set(val, gate) \
	writel_relaxed(val, gate->clk_base + (gate->regs->enb_set_reg))
#define write_enb_clr(val, gate) \
	writel_relaxed(val, gate->clk_base + (gate->regs->enb_clr_reg))

#define read_rst(gate) \
	readl_relaxed(gate->clk_base + (gate->regs->rst_reg))
#define write_rst_clr(val, gate) \
	writel_relaxed(val, gate->clk_base + (gate->regs->rst_clr_reg))

#define periph_clk_to_bit(gate) (1 << (gate->clk_num % 32))

#define LVL2_CLK_GATE_OVRE 0x554

/* Peripheral gate clock ops */
static int clk_periph_is_enabled(struct clk_hw *hw)
{
	struct tegra_clk_periph_gate *gate = to_clk_periph_gate(hw);
	int state = 1;

	if (!(read_enb(gate) & periph_clk_to_bit(gate)))
		state = 0;

	if (!(gate->flags & TEGRA_PERIPH_NO_RESET))
		if (read_rst(gate) & periph_clk_to_bit(gate))
			state = 0;

	return state;
}

static int clk_periph_enable(struct clk_hw *hw)
{
	struct tegra_clk_periph_gate *gate = to_clk_periph_gate(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(&periph_ref_lock, flags);

	gate->enable_refcnt[gate->clk_num]++;
	if (gate->enable_refcnt[gate->clk_num] > 1) {
		spin_unlock_irqrestore(&periph_ref_lock, flags);
		return 0;
	}

	write_enb_set(periph_clk_to_bit(gate), gate);
	udelay(2);

	if (!(gate->flags & TEGRA_PERIPH_NO_RESET) &&
	    !(gate->flags & TEGRA_PERIPH_MANUAL_RESET)) {
		if (read_rst(gate) & periph_clk_to_bit(gate)) {
			udelay(5); /* reset propogation delay */
			write_rst_clr(periph_clk_to_bit(gate), gate);
		}
	}

	if (gate->flags & TEGRA_PERIPH_WAR_1005168) {
		writel_relaxed(0, gate->clk_base + LVL2_CLK_GATE_OVRE);
		writel_relaxed(BIT(22), gate->clk_base + LVL2_CLK_GATE_OVRE);
		udelay(1);
		writel_relaxed(0, gate->clk_base + LVL2_CLK_GATE_OVRE);
	}

	spin_unlock_irqrestore(&periph_ref_lock, flags);

	return 0;
}

static void clk_periph_disable(struct clk_hw *hw)
{
	struct tegra_clk_periph_gate *gate = to_clk_periph_gate(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(&periph_ref_lock, flags);

	gate->enable_refcnt[gate->clk_num]--;
	if (gate->enable_refcnt[gate->clk_num] > 0) {
		spin_unlock_irqrestore(&periph_ref_lock, flags);
		return;
	}

	/*
	 * If peripheral is in the APB bus then read the APB bus to
	 * flush the write operation in apb bus. This will avoid the
	 * peripheral access after disabling clock
	 */
	if (gate->flags & TEGRA_PERIPH_ON_APB)
		tegra_read_chipid();

	write_enb_clr(periph_clk_to_bit(gate), gate);

	spin_unlock_irqrestore(&periph_ref_lock, flags);
}

const struct clk_ops tegra_clk_periph_gate_ops = {
	.is_enabled = clk_periph_is_enabled,
	.enable = clk_periph_enable,
	.disable = clk_periph_disable,
};

struct clk *tegra_clk_register_periph_gate(const char *name,
		const char *parent_name, u8 gate_flags, void __iomem *clk_base,
		unsigned long flags, int clk_num, int *enable_refcnt)
{
	struct tegra_clk_periph_gate *gate;
	struct clk *clk;
	struct clk_init_data init;
	struct tegra_clk_periph_regs *pregs;

	pregs = get_reg_bank(clk_num);
	if (!pregs)
		return ERR_PTR(-EINVAL);

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate) {
		pr_err("%s: could not allocate periph gate clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	init.ops = &tegra_clk_periph_gate_ops;

	gate->magic = TEGRA_CLK_PERIPH_GATE_MAGIC;
	gate->clk_base = clk_base;
	gate->clk_num = clk_num;
	gate->flags = gate_flags;
	gate->enable_refcnt = enable_refcnt;
	gate->regs = pregs;

	/* Data in .init is copied by clk_register(), so stack variable OK */
	gate->hw.init = &init;

	clk = clk_register(NULL, &gate->hw);
	if (IS_ERR(clk))
		kfree(gate);

	return clk;
}
