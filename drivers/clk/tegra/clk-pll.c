/*
 * Copyright (c) 2012, 2013, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>

#include "clk.h"

#define PLL_BASE_BYPASS BIT(31)
#define PLL_BASE_ENABLE BIT(30)
#define PLL_BASE_REF_ENABLE BIT(29)
#define PLL_BASE_OVERRIDE BIT(28)

#define PLL_BASE_DIVP_SHIFT 20
#define PLL_BASE_DIVP_WIDTH 3
#define PLL_BASE_DIVN_SHIFT 8
#define PLL_BASE_DIVN_WIDTH 10
#define PLL_BASE_DIVM_SHIFT 0
#define PLL_BASE_DIVM_WIDTH 5
#define PLLU_POST_DIVP_MASK 0x1

#define PLL_MISC_DCCON_SHIFT 20
#define PLL_MISC_CPCON_SHIFT 8
#define PLL_MISC_CPCON_WIDTH 4
#define PLL_MISC_CPCON_MASK ((1 << PLL_MISC_CPCON_WIDTH) - 1)
#define PLL_MISC_LFCON_SHIFT 4
#define PLL_MISC_LFCON_WIDTH 4
#define PLL_MISC_LFCON_MASK ((1 << PLL_MISC_LFCON_WIDTH) - 1)
#define PLL_MISC_VCOCON_SHIFT 0
#define PLL_MISC_VCOCON_WIDTH 4
#define PLL_MISC_VCOCON_MASK ((1 << PLL_MISC_VCOCON_WIDTH) - 1)

#define OUT_OF_TABLE_CPCON 8

#define PMC_PLLP_WB0_OVERRIDE 0xf8
#define PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE BIT(12)
#define PMC_PLLP_WB0_OVERRIDE_PLLM_OVERRIDE BIT(11)

#define PLL_POST_LOCK_DELAY 50

#define PLLDU_LFCON_SET_DIVN 600

#define PLLE_BASE_DIVCML_SHIFT 24
#define PLLE_BASE_DIVCML_WIDTH 4
#define PLLE_BASE_DIVP_SHIFT 16
#define PLLE_BASE_DIVP_WIDTH 7
#define PLLE_BASE_DIVN_SHIFT 8
#define PLLE_BASE_DIVN_WIDTH 8
#define PLLE_BASE_DIVM_SHIFT 0
#define PLLE_BASE_DIVM_WIDTH 8

#define PLLE_MISC_SETUP_BASE_SHIFT 16
#define PLLE_MISC_SETUP_BASE_MASK (0xffff << PLLE_MISC_SETUP_BASE_SHIFT)
#define PLLE_MISC_LOCK_ENABLE BIT(9)
#define PLLE_MISC_READY BIT(15)
#define PLLE_MISC_SETUP_EX_SHIFT 2
#define PLLE_MISC_SETUP_EX_MASK (3 << PLLE_MISC_SETUP_EX_SHIFT)
#define PLLE_MISC_SETUP_MASK (PLLE_MISC_SETUP_BASE_MASK |	\
			      PLLE_MISC_SETUP_EX_MASK)
#define PLLE_MISC_SETUP_VALUE (7 << PLLE_MISC_SETUP_BASE_SHIFT)

#define PLLE_SS_CTRL 0x68
#define PLLE_SS_DISABLE (7 << 10)

#define PMC_SATA_PWRGT 0x1ac
#define PMC_SATA_PWRGT_PLLE_IDDQ_VALUE BIT(5)
#define PMC_SATA_PWRGT_PLLE_IDDQ_SWCTL BIT(4)

#define pll_readl(offset, p) readl_relaxed(p->clk_base + offset)
#define pll_readl_base(p) pll_readl(p->params->base_reg, p)
#define pll_readl_misc(p) pll_readl(p->params->misc_reg, p)

#define pll_writel(val, offset, p) writel_relaxed(val, p->clk_base + offset)
#define pll_writel_base(val, p) pll_writel(val, p->params->base_reg, p)
#define pll_writel_misc(val, p) pll_writel(val, p->params->misc_reg, p)

#define mask(w) ((1 << (w)) - 1)
#define divm_mask(p) mask(p->divm_width)
#define divn_mask(p) mask(p->divn_width)
#define divp_mask(p) (p->flags & TEGRA_PLLU ? PLLU_POST_DIVP_MASK :	\
		      mask(p->divp_width))

#define divm_max(p) (divm_mask(p))
#define divn_max(p) (divn_mask(p))
#define divp_max(p) (1 << (divp_mask(p)))

static void clk_pll_enable_lock(struct tegra_clk_pll *pll)
{
	u32 val;

	if (!(pll->flags & TEGRA_PLL_USE_LOCK))
		return;

	if (!(pll->flags & TEGRA_PLL_HAS_LOCK_ENABLE))
		return;

	val = pll_readl_misc(pll);
	val |= BIT(pll->params->lock_enable_bit_idx);
	pll_writel_misc(val, pll);
}

static int clk_pll_wait_for_lock(struct tegra_clk_pll *pll)
{
	int i;
	u32 val, lock_mask;
	void __iomem *lock_addr;

	if (!(pll->flags & TEGRA_PLL_USE_LOCK)) {
		udelay(pll->params->lock_delay);
		return 0;
	}

	lock_addr = pll->clk_base;
	if (pll->flags & TEGRA_PLL_LOCK_MISC)
		lock_addr += pll->params->misc_reg;
	else
		lock_addr += pll->params->base_reg;

	lock_mask = pll->params->lock_mask;

	for (i = 0; i < pll->params->lock_delay; i++) {
		val = readl_relaxed(lock_addr);
		if ((val & lock_mask) == lock_mask) {
			udelay(PLL_POST_LOCK_DELAY);
			return 0;
		}
		udelay(2); /* timeout = 2 * lock time */
	}

	pr_err("%s: Timed out waiting for pll %s lock\n", __func__,
	       __clk_get_name(pll->hw.clk));

	return -1;
}

static int clk_pll_is_enabled(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val;

	if (pll->flags & TEGRA_PLLM) {
		val = readl_relaxed(pll->pmc + PMC_PLLP_WB0_OVERRIDE);
		if (val & PMC_PLLP_WB0_OVERRIDE_PLLM_OVERRIDE)
			return val & PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE ? 1 : 0;
	}

	val = pll_readl_base(pll);

	return val & PLL_BASE_ENABLE ? 1 : 0;
}

static void _clk_pll_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val;

	clk_pll_enable_lock(pll);

	val = pll_readl_base(pll);
	if (pll->flags & TEGRA_PLL_BYPASS)
		val &= ~PLL_BASE_BYPASS;
	val |= PLL_BASE_ENABLE;
	pll_writel_base(val, pll);

	if (pll->flags & TEGRA_PLLM) {
		val = readl_relaxed(pll->pmc + PMC_PLLP_WB0_OVERRIDE);
		val |= PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE;
		writel_relaxed(val, pll->pmc + PMC_PLLP_WB0_OVERRIDE);
	}
}

static void _clk_pll_disable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val;

	val = pll_readl_base(pll);
	if (pll->flags & TEGRA_PLL_BYPASS)
		val &= ~PLL_BASE_BYPASS;
	val &= ~PLL_BASE_ENABLE;
	pll_writel_base(val, pll);

	if (pll->flags & TEGRA_PLLM) {
		val = readl_relaxed(pll->pmc + PMC_PLLP_WB0_OVERRIDE);
		val &= ~PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE;
		writel_relaxed(val, pll->pmc + PMC_PLLP_WB0_OVERRIDE);
	}
}

static int clk_pll_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;
	int ret;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_clk_pll_enable(hw);

	ret = clk_pll_wait_for_lock(pll);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static void clk_pll_disable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_clk_pll_disable(hw);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

static int _get_table_rate(struct clk_hw *hw,
			   struct tegra_clk_pll_freq_table *cfg,
			   unsigned long rate, unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table *sel;

	for (sel = pll->freq_table; sel->input_rate != 0; sel++)
		if (sel->input_rate == parent_rate &&
		    sel->output_rate == rate)
			break;

	if (sel->input_rate == 0)
		return -EINVAL;

	cfg->input_rate = sel->input_rate;
	cfg->output_rate = sel->output_rate;
	cfg->m = sel->m;
	cfg->n = sel->n;
	cfg->p = sel->p;
	cfg->cpcon = sel->cpcon;

	return 0;
}

static int _calc_rate(struct clk_hw *hw, struct tegra_clk_pll_freq_table *cfg,
		      unsigned long rate, unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct pdiv_map *p_tohw = pll->params->pdiv_tohw;
	unsigned long cfreq;
	u32 p_div = 0;

	switch (parent_rate) {
	case 12000000:
	case 26000000:
		cfreq = (rate <= 1000000 * 1000) ? 1000000 : 2000000;
		break;
	case 13000000:
		cfreq = (rate <= 1000000 * 1000) ? 1000000 : 2600000;
		break;
	case 16800000:
	case 19200000:
		cfreq = (rate <= 1200000 * 1000) ? 1200000 : 2400000;
		break;
	case 9600000:
	case 28800000:
		/*
		 * PLL_P_OUT1 rate is not listed in PLLA table
		 */
		cfreq = parent_rate/(parent_rate/1000000);
		break;
	default:
		pr_err("%s Unexpected reference rate %lu\n",
		       __func__, parent_rate);
		BUG();
	}

	/* Raise VCO to guarantee 0.5% accuracy */
	for (cfg->output_rate = rate; cfg->output_rate < 200 * cfreq;
	     cfg->output_rate <<= 1)
		p_div++;

	cfg->m = parent_rate / cfreq;
	cfg->n = cfg->output_rate / cfreq;
	cfg->cpcon = OUT_OF_TABLE_CPCON;

	if (cfg->m > divm_max(pll) || cfg->n > divn_max(pll) ||
	    (1 << p_div) > divp_max(pll)
	    || cfg->output_rate > pll->params->vco_max) {
		pr_err("%s: Failed to set %s rate %lu\n",
		       __func__, __clk_get_name(hw->clk), rate);
		return -EINVAL;
	}

	if (p_tohw) {
		p_div = 1 << p_div;
		while (p_tohw->pdiv) {
			if (p_div <= p_tohw->pdiv) {
				cfg->p = p_tohw->hw_val;
				break;
			}
			p_tohw++;
		}
		if (!p_tohw->pdiv)
			return -EINVAL;
	} else
		cfg->p = p_div;

	return 0;
}

static void _update_pll_mnp(struct tegra_clk_pll *pll,
			    struct tegra_clk_pll_freq_table *cfg)
{
	u32 val;

	val = pll_readl_base(pll);

	val &= ~((divm_mask(pll) << pll->divm_shift) |
		 (divn_mask(pll) << pll->divn_shift) |
		 (divp_mask(pll) << pll->divp_shift));
	val |= ((cfg->m << pll->divm_shift) |
		(cfg->n << pll->divn_shift) |
		(cfg->p << pll->divp_shift));

	pll_writel_base(val, pll);
}

static void _get_pll_mnp(struct tegra_clk_pll *pll,
			 struct tegra_clk_pll_freq_table *cfg)
{
	u32 val;

	val = pll_readl_base(pll);

	cfg->m = (val >> pll->divm_shift) & (divm_mask(pll));
	cfg->n = (val >> pll->divn_shift) & (divn_mask(pll));
	cfg->p = (val >> pll->divp_shift) & (divp_mask(pll));
}

static void _update_pll_cpcon(struct tegra_clk_pll *pll,
			      struct tegra_clk_pll_freq_table *cfg,
			      unsigned long rate)
{
	u32 val;

	val = pll_readl_misc(pll);

	val &= ~(PLL_MISC_CPCON_MASK << PLL_MISC_CPCON_SHIFT);
	val |= cfg->cpcon << PLL_MISC_CPCON_SHIFT;

	if (pll->flags & TEGRA_PLL_SET_LFCON) {
		val &= ~(PLL_MISC_LFCON_MASK << PLL_MISC_LFCON_SHIFT);
		if (cfg->n >= PLLDU_LFCON_SET_DIVN)
			val |= 1 << PLL_MISC_LFCON_SHIFT;
	} else if (pll->flags & TEGRA_PLL_SET_DCCON) {
		val &= ~(1 << PLL_MISC_DCCON_SHIFT);
		if (rate >= (pll->params->vco_max >> 1))
			val |= 1 << PLL_MISC_DCCON_SHIFT;
	}

	pll_writel_misc(val, pll);
}

static int _program_pll(struct clk_hw *hw, struct tegra_clk_pll_freq_table *cfg,
			unsigned long rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	int state, ret = 0;

	state = clk_pll_is_enabled(hw);

	if (state)
		_clk_pll_disable(hw);

	_update_pll_mnp(pll, cfg);

	if (pll->flags & TEGRA_PLL_HAS_CPCON)
		_update_pll_cpcon(pll, cfg, rate);

	if (state) {
		_clk_pll_enable(hw);
		ret = clk_pll_wait_for_lock(pll);
	}

	return ret;
}

static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table cfg, old_cfg;
	unsigned long flags = 0;
	int ret = 0;

	if (pll->flags & TEGRA_PLL_FIXED) {
		if (rate != pll->fixed_rate) {
			pr_err("%s: Can not change %s fixed rate %lu to %lu\n",
				__func__, __clk_get_name(hw->clk),
				pll->fixed_rate, rate);
			return -EINVAL;
		}
		return 0;
	}

	if (_get_table_rate(hw, &cfg, rate, parent_rate) &&
	    _calc_rate(hw, &cfg, rate, parent_rate))
		return -EINVAL;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_get_pll_mnp(pll, &old_cfg);

	if (old_cfg.m != cfg.m || old_cfg.n != cfg.n || old_cfg.p != cfg.p)
		ret = _program_pll(hw, &cfg, rate);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *prate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table cfg;
	u64 output_rate = *prate;

	if (pll->flags & TEGRA_PLL_FIXED)
		return pll->fixed_rate;

	/* PLLM is used for memory; we do not change rate */
	if (pll->flags & TEGRA_PLLM)
		return __clk_get_rate(hw->clk);

	if (_get_table_rate(hw, &cfg, rate, *prate) &&
	    _calc_rate(hw, &cfg, rate, *prate))
		return -EINVAL;

	output_rate *= cfg.n;
	do_div(output_rate, cfg.m * (1 << cfg.p));

	return output_rate;
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table cfg;
	struct pdiv_map *p_tohw = pll->params->pdiv_tohw;
	u32 val;
	u64 rate = parent_rate;
	int pdiv;

	val = pll_readl_base(pll);

	if ((pll->flags & TEGRA_PLL_BYPASS) && (val & PLL_BASE_BYPASS))
		return parent_rate;

	if ((pll->flags & TEGRA_PLL_FIXED) && !(val & PLL_BASE_OVERRIDE)) {
		struct tegra_clk_pll_freq_table sel;
		if (_get_table_rate(hw, &sel, pll->fixed_rate, parent_rate)) {
			pr_err("Clock %s has unknown fixed frequency\n",
			       __clk_get_name(hw->clk));
			BUG();
		}
		return pll->fixed_rate;
	}

	_get_pll_mnp(pll, &cfg);

	if (p_tohw) {
		while (p_tohw->pdiv) {
			if (cfg.p == p_tohw->hw_val) {
				pdiv = p_tohw->pdiv;
				break;
			}
			p_tohw++;
		}

		if (!p_tohw->pdiv) {
			WARN_ON(1);
			pdiv = 1;
		}
	} else
		pdiv = 1 << cfg.p;

	cfg.m *= pdiv;

	rate *= cfg.n;
	do_div(rate, cfg.m);

	return rate;
}

static int clk_plle_training(struct tegra_clk_pll *pll)
{
	u32 val;
	unsigned long timeout;

	if (!pll->pmc)
		return -ENOSYS;

	/*
	 * PLLE is already disabled, and setup cleared;
	 * create falling edge on PLLE IDDQ input.
	 */
	val = readl(pll->pmc + PMC_SATA_PWRGT);
	val |= PMC_SATA_PWRGT_PLLE_IDDQ_VALUE;
	writel(val, pll->pmc + PMC_SATA_PWRGT);

	val = readl(pll->pmc + PMC_SATA_PWRGT);
	val |= PMC_SATA_PWRGT_PLLE_IDDQ_SWCTL;
	writel(val, pll->pmc + PMC_SATA_PWRGT);

	val = readl(pll->pmc + PMC_SATA_PWRGT);
	val &= ~PMC_SATA_PWRGT_PLLE_IDDQ_VALUE;
	writel(val, pll->pmc + PMC_SATA_PWRGT);

	val = pll_readl_misc(pll);

	timeout = jiffies + msecs_to_jiffies(100);
	while (1) {
		val = pll_readl_misc(pll);
		if (val & PLLE_MISC_READY)
			break;
		if (time_after(jiffies, timeout)) {
			pr_err("%s: timeout waiting for PLLE\n", __func__);
			return -EBUSY;
		}
		udelay(300);
	}

	return 0;
}

static int clk_plle_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long input_rate = clk_get_rate(clk_get_parent(hw->clk));
	struct tegra_clk_pll_freq_table sel;
	u32 val;
	int err;

	if (_get_table_rate(hw, &sel, pll->fixed_rate, input_rate))
		return -EINVAL;

	clk_pll_disable(hw);

	val = pll_readl_misc(pll);
	val &= ~(PLLE_MISC_LOCK_ENABLE | PLLE_MISC_SETUP_MASK);
	pll_writel_misc(val, pll);

	val = pll_readl_misc(pll);
	if (!(val & PLLE_MISC_READY)) {
		err = clk_plle_training(pll);
		if (err)
			return err;
	}

	if (pll->flags & TEGRA_PLLE_CONFIGURE) {
		/* configure dividers */
		val = pll_readl_base(pll);
		val &= ~(divm_mask(pll) | divn_mask(pll) | divp_mask(pll));
		val &= ~(PLLE_BASE_DIVCML_WIDTH << PLLE_BASE_DIVCML_SHIFT);
		val |= sel.m << pll->divm_shift;
		val |= sel.n << pll->divn_shift;
		val |= sel.p << pll->divp_shift;
		val |= sel.cpcon << PLLE_BASE_DIVCML_SHIFT;
		pll_writel_base(val, pll);
	}

	val = pll_readl_misc(pll);
	val |= PLLE_MISC_SETUP_VALUE;
	val |= PLLE_MISC_LOCK_ENABLE;
	pll_writel_misc(val, pll);

	val = readl(pll->clk_base + PLLE_SS_CTRL);
	val |= PLLE_SS_DISABLE;
	writel(val, pll->clk_base + PLLE_SS_CTRL);

	val |= pll_readl_base(pll);
	val |= (PLL_BASE_BYPASS | PLL_BASE_ENABLE);
	pll_writel_base(val, pll);

	clk_pll_wait_for_lock(pll);

	return 0;
}

static unsigned long clk_plle_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val = pll_readl_base(pll);
	u32 divn = 0, divm = 0, divp = 0;
	u64 rate = parent_rate;

	divp = (val >> pll->divp_shift) & (divp_mask(pll));
	divn = (val >> pll->divn_shift) & (divn_mask(pll));
	divm = (val >> pll->divm_shift) & (divm_mask(pll));
	divm *= divp;

	rate *= divn;
	do_div(rate, divm);
	return rate;
}

const struct clk_ops tegra_clk_pll_ops = {
	.is_enabled = clk_pll_is_enabled,
	.enable = clk_pll_enable,
	.disable = clk_pll_disable,
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_round_rate,
	.set_rate = clk_pll_set_rate,
};

const struct clk_ops tegra_clk_plle_ops = {
	.recalc_rate = clk_plle_recalc_rate,
	.is_enabled = clk_pll_is_enabled,
	.disable = clk_pll_disable,
	.enable = clk_plle_enable,
};

static struct tegra_clk_pll *_tegra_init_pll(void __iomem *clk_base,
		void __iomem *pmc, unsigned long fixed_rate,
		struct tegra_clk_pll_params *pll_params, u32 pll_flags,
		struct tegra_clk_pll_freq_table *freq_table, spinlock_t *lock)
{
	struct tegra_clk_pll *pll;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->clk_base = clk_base;
	pll->pmc = pmc;

	pll->freq_table = freq_table;
	pll->params = pll_params;
	pll->fixed_rate = fixed_rate;
	pll->flags = pll_flags;
	pll->lock = lock;

	pll->divp_shift = PLL_BASE_DIVP_SHIFT;
	pll->divp_width = PLL_BASE_DIVP_WIDTH;
	pll->divn_shift = PLL_BASE_DIVN_SHIFT;
	pll->divn_width = PLL_BASE_DIVN_WIDTH;
	pll->divm_shift = PLL_BASE_DIVM_SHIFT;
	pll->divm_width = PLL_BASE_DIVM_WIDTH;

	return pll;
}

static struct clk *_tegra_clk_register_pll(struct tegra_clk_pll *pll,
		const char *name, const char *parent_name, unsigned long flags,
		const struct clk_ops *ops)
{
	struct clk_init_data init;

	init.name = name;
	init.ops = ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* Data in .init is copied by clk_register(), so stack variable OK */
	pll->hw.init = &init;

	return clk_register(NULL, &pll->hw);
}

struct clk *tegra_clk_register_pll(const char *name, const char *parent_name,
		void __iomem *clk_base, void __iomem *pmc,
		unsigned long flags, unsigned long fixed_rate,
		struct tegra_clk_pll_params *pll_params, u32 pll_flags,
		struct tegra_clk_pll_freq_table *freq_table, spinlock_t *lock)
{
	struct tegra_clk_pll *pll;
	struct clk *clk;

	pll_flags |= TEGRA_PLL_BYPASS;
	pll_flags |= TEGRA_PLL_HAS_LOCK_ENABLE;
	pll = _tegra_init_pll(clk_base, pmc, fixed_rate, pll_params, pll_flags,
			      freq_table, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
				      &tegra_clk_pll_ops);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

struct clk *tegra_clk_register_plle(const char *name, const char *parent_name,
		void __iomem *clk_base, void __iomem *pmc,
		unsigned long flags, unsigned long fixed_rate,
		struct tegra_clk_pll_params *pll_params, u32 pll_flags,
		struct tegra_clk_pll_freq_table *freq_table, spinlock_t *lock)
{
	struct tegra_clk_pll *pll;
	struct clk *clk;

	pll_flags |= TEGRA_PLL_LOCK_MISC | TEGRA_PLL_BYPASS;
	pll_flags |= TEGRA_PLL_HAS_LOCK_ENABLE;
	pll = _tegra_init_pll(clk_base, pmc, fixed_rate, pll_params, pll_flags,
			      freq_table, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
				      &tegra_clk_plle_ops);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}
