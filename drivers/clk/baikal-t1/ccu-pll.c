// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *   Dmitry Dunaev <dmitry.dunaev@baikalelectronics.ru>
 *
 * Baikal-T1 CCU PLL interface driver
 */

#define pr_fmt(fmt) "bt1-ccu-pll: " fmt

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/limits.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>
#include <linux/iopoll.h>
#include <linux/time64.h>
#include <linux/rational.h>
#include <linux/debugfs.h>

#include "ccu-pll.h"

#define CCU_PLL_CTL			0x000
#define CCU_PLL_CTL_EN			BIT(0)
#define CCU_PLL_CTL_RST			BIT(1)
#define CCU_PLL_CTL_CLKR_FLD		2
#define CCU_PLL_CTL_CLKR_MASK		GENMASK(7, CCU_PLL_CTL_CLKR_FLD)
#define CCU_PLL_CTL_CLKF_FLD		8
#define CCU_PLL_CTL_CLKF_MASK		GENMASK(20, CCU_PLL_CTL_CLKF_FLD)
#define CCU_PLL_CTL_CLKOD_FLD		21
#define CCU_PLL_CTL_CLKOD_MASK		GENMASK(24, CCU_PLL_CTL_CLKOD_FLD)
#define CCU_PLL_CTL_BYPASS		BIT(30)
#define CCU_PLL_CTL_LOCK		BIT(31)
#define CCU_PLL_CTL1			0x004
#define CCU_PLL_CTL1_BWADJ_FLD		3
#define CCU_PLL_CTL1_BWADJ_MASK		GENMASK(14, CCU_PLL_CTL1_BWADJ_FLD)

#define CCU_PLL_LOCK_CHECK_RETRIES	50

#define CCU_PLL_NR_MAX \
	((CCU_PLL_CTL_CLKR_MASK >> CCU_PLL_CTL_CLKR_FLD) + 1)
#define CCU_PLL_NF_MAX \
	((CCU_PLL_CTL_CLKF_MASK >> (CCU_PLL_CTL_CLKF_FLD + 1)) + 1)
#define CCU_PLL_OD_MAX \
	((CCU_PLL_CTL_CLKOD_MASK >> CCU_PLL_CTL_CLKOD_FLD) + 1)
#define CCU_PLL_NB_MAX \
	((CCU_PLL_CTL1_BWADJ_MASK >> CCU_PLL_CTL1_BWADJ_FLD) + 1)
#define CCU_PLL_FDIV_MIN		427000UL
#define CCU_PLL_FDIV_MAX		3500000000UL
#define CCU_PLL_FOUT_MIN		200000000UL
#define CCU_PLL_FOUT_MAX		2500000000UL
#define CCU_PLL_FVCO_MIN		700000000UL
#define CCU_PLL_FVCO_MAX		3500000000UL
#define CCU_PLL_CLKOD_FACTOR		2

static inline unsigned long ccu_pll_lock_delay_us(unsigned long ref_clk,
						  unsigned long nr)
{
	u64 us = 500ULL * nr * USEC_PER_SEC;

	do_div(us, ref_clk);

	return us;
}

static inline unsigned long ccu_pll_calc_freq(unsigned long ref_clk,
					      unsigned long nr,
					      unsigned long nf,
					      unsigned long od)
{
	u64 tmp = ref_clk;

	do_div(tmp, nr);
	tmp *= nf;
	do_div(tmp, od);

	return tmp;
}

static int ccu_pll_reset(struct ccu_pll *pll, unsigned long ref_clk,
			 unsigned long nr)
{
	unsigned long ud, ut;
	u32 val;

	ud = ccu_pll_lock_delay_us(ref_clk, nr);
	ut = ud * CCU_PLL_LOCK_CHECK_RETRIES;

	regmap_update_bits(pll->sys_regs, pll->reg_ctl,
			   CCU_PLL_CTL_RST, CCU_PLL_CTL_RST);

	return regmap_read_poll_timeout_atomic(pll->sys_regs, pll->reg_ctl, val,
					       val & CCU_PLL_CTL_LOCK, ud, ut);
}

static int ccu_pll_enable(struct clk_hw *hw)
{
	struct clk_hw *parent_hw = clk_hw_get_parent(hw);
	struct ccu_pll *pll = to_ccu_pll(hw);
	unsigned long flags;
	u32 val = 0;
	int ret;

	if (!parent_hw) {
		pr_err("Can't enable '%s' with no parent", clk_hw_get_name(hw));
		return -EINVAL;
	}

	regmap_read(pll->sys_regs, pll->reg_ctl, &val);
	if (val & CCU_PLL_CTL_EN)
		return 0;

	spin_lock_irqsave(&pll->lock, flags);
	regmap_write(pll->sys_regs, pll->reg_ctl, val | CCU_PLL_CTL_EN);
	ret = ccu_pll_reset(pll, clk_hw_get_rate(parent_hw),
			    FIELD_GET(CCU_PLL_CTL_CLKR_MASK, val) + 1);
	spin_unlock_irqrestore(&pll->lock, flags);
	if (ret)
		pr_err("PLL '%s' reset timed out\n", clk_hw_get_name(hw));

	return ret;
}

static void ccu_pll_disable(struct clk_hw *hw)
{
	struct ccu_pll *pll = to_ccu_pll(hw);
	unsigned long flags;

	spin_lock_irqsave(&pll->lock, flags);
	regmap_update_bits(pll->sys_regs, pll->reg_ctl, CCU_PLL_CTL_EN, 0);
	spin_unlock_irqrestore(&pll->lock, flags);
}

static int ccu_pll_is_enabled(struct clk_hw *hw)
{
	struct ccu_pll *pll = to_ccu_pll(hw);
	u32 val = 0;

	regmap_read(pll->sys_regs, pll->reg_ctl, &val);

	return !!(val & CCU_PLL_CTL_EN);
}

static unsigned long ccu_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct ccu_pll *pll = to_ccu_pll(hw);
	unsigned long nr, nf, od;
	u32 val = 0;

	regmap_read(pll->sys_regs, pll->reg_ctl, &val);
	nr = FIELD_GET(CCU_PLL_CTL_CLKR_MASK, val) + 1;
	nf = FIELD_GET(CCU_PLL_CTL_CLKF_MASK, val) + 1;
	od = FIELD_GET(CCU_PLL_CTL_CLKOD_MASK, val) + 1;

	return ccu_pll_calc_freq(parent_rate, nr, nf, od);
}

static void ccu_pll_calc_factors(unsigned long rate, unsigned long parent_rate,
				 unsigned long *nr, unsigned long *nf,
				 unsigned long *od)
{
	unsigned long err, freq, min_err = ULONG_MAX;
	unsigned long num, denom, n1, d1, nri;
	unsigned long nr_max, nf_max, od_max;

	/*
	 * Make sure PLL is working with valid input signal (Fdiv). If
	 * you want to speed the function up just reduce CCU_PLL_NR_MAX.
	 * This will cause a worse approximation though.
	 */
	nri = (parent_rate / CCU_PLL_FDIV_MAX) + 1;
	nr_max = min(parent_rate / CCU_PLL_FDIV_MIN, CCU_PLL_NR_MAX);

	/*
	 * Find a closest [nr;nf;od] vector taking into account the
	 * limitations like: 1) 700MHz <= Fvco <= 3.5GHz, 2) PLL Od is
	 * either 1 or even number within the acceptable range (alas 1s
	 * is also excluded by the next loop).
	 */
	for (; nri <= nr_max; ++nri) {
		/* Use Od factor to fulfill the limitation 2). */
		num = CCU_PLL_CLKOD_FACTOR * rate;
		denom = parent_rate / nri;

		/*
		 * Make sure Fvco is within the acceptable range to fulfill
		 * the condition 1). Note due to the CCU_PLL_CLKOD_FACTOR value
		 * the actual upper limit is also divided by that factor.
		 * It's not big problem for us since practically there is no
		 * need in clocks with that high frequency.
		 */
		nf_max = min(CCU_PLL_FVCO_MAX / denom, CCU_PLL_NF_MAX);
		od_max = CCU_PLL_OD_MAX / CCU_PLL_CLKOD_FACTOR;

		/*
		 * Bypass the out-of-bound values, which can't be properly
		 * handled by the rational fraction approximation algorithm.
		 */
		if (num / denom >= nf_max) {
			n1 = nf_max;
			d1 = 1;
		} else if (denom / num >= od_max) {
			n1 = 1;
			d1 = od_max;
		} else {
			rational_best_approximation(num, denom, nf_max, od_max,
						    &n1, &d1);
		}

		/* Select the best approximation of the target rate. */
		freq = ccu_pll_calc_freq(parent_rate, nri, n1, d1);
		err = abs((int64_t)freq - num);
		if (err < min_err) {
			min_err = err;
			*nr = nri;
			*nf = n1;
			*od = CCU_PLL_CLKOD_FACTOR * d1;
		}
	}
}

static int ccu_pll_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	unsigned long nr = 1, nf = 1, od = 1;

	ccu_pll_calc_factors(req->rate, req->best_parent_rate, &nr, &nf, &od);

	req->rate = ccu_pll_calc_freq(req->best_parent_rate, nr, nf, od);

	return 0;
}

/*
 * This method is used for PLLs, which support the on-the-fly dividers
 * adjustment. So there is no need in gating such clocks.
 */
static int ccu_pll_set_rate_reset(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct ccu_pll *pll = to_ccu_pll(hw);
	unsigned long nr, nf, od;
	unsigned long flags;
	u32 mask, val;
	int ret;

	ccu_pll_calc_factors(rate, parent_rate, &nr, &nf, &od);

	mask = CCU_PLL_CTL_CLKR_MASK | CCU_PLL_CTL_CLKF_MASK |
	       CCU_PLL_CTL_CLKOD_MASK;
	val = FIELD_PREP(CCU_PLL_CTL_CLKR_MASK, nr - 1) |
	      FIELD_PREP(CCU_PLL_CTL_CLKF_MASK, nf - 1) |
	      FIELD_PREP(CCU_PLL_CTL_CLKOD_MASK, od - 1);

	spin_lock_irqsave(&pll->lock, flags);
	regmap_update_bits(pll->sys_regs, pll->reg_ctl, mask, val);
	ret = ccu_pll_reset(pll, parent_rate, nr);
	spin_unlock_irqrestore(&pll->lock, flags);
	if (ret)
		pr_err("PLL '%s' reset timed out\n", clk_hw_get_name(hw));

	return ret;
}

/*
 * This method is used for PLLs, which don't support the on-the-fly dividers
 * adjustment. So the corresponding clocks are supposed to be gated first.
 */
static int ccu_pll_set_rate_norst(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct ccu_pll *pll = to_ccu_pll(hw);
	unsigned long nr, nf, od;
	unsigned long flags;
	u32 mask, val;

	ccu_pll_calc_factors(rate, parent_rate, &nr, &nf, &od);

	/*
	 * Disable PLL if it was enabled by default or left enabled by the
	 * system bootloader.
	 */
	mask = CCU_PLL_CTL_CLKR_MASK | CCU_PLL_CTL_CLKF_MASK |
	       CCU_PLL_CTL_CLKOD_MASK | CCU_PLL_CTL_EN;
	val = FIELD_PREP(CCU_PLL_CTL_CLKR_MASK, nr - 1) |
	      FIELD_PREP(CCU_PLL_CTL_CLKF_MASK, nf - 1) |
	      FIELD_PREP(CCU_PLL_CTL_CLKOD_MASK, od - 1);

	spin_lock_irqsave(&pll->lock, flags);
	regmap_update_bits(pll->sys_regs, pll->reg_ctl, mask, val);
	spin_unlock_irqrestore(&pll->lock, flags);

	return 0;
}

#ifdef CONFIG_DEBUG_FS

struct ccu_pll_dbgfs_bit {
	struct ccu_pll *pll;
	const char *name;
	unsigned int reg;
	u32 mask;
};

struct ccu_pll_dbgfs_fld {
	struct ccu_pll *pll;
	const char *name;
	unsigned int reg;
	unsigned int lsb;
	u32 mask;
	u32 min;
	u32 max;
};

#define CCU_PLL_DBGFS_BIT_ATTR(_name, _reg, _mask)	\
	{						\
		.name = _name,				\
		.reg = _reg,				\
		.mask = _mask				\
	}

#define CCU_PLL_DBGFS_FLD_ATTR(_name, _reg, _lsb, _mask, _min, _max)	\
	{								\
		.name = _name,						\
		.reg = _reg,						\
		.lsb = _lsb,						\
		.mask = _mask,						\
		.min = _min,						\
		.max = _max						\
	}

static const struct ccu_pll_dbgfs_bit ccu_pll_bits[] = {
	CCU_PLL_DBGFS_BIT_ATTR("pll_en", CCU_PLL_CTL, CCU_PLL_CTL_EN),
	CCU_PLL_DBGFS_BIT_ATTR("pll_rst", CCU_PLL_CTL, CCU_PLL_CTL_RST),
	CCU_PLL_DBGFS_BIT_ATTR("pll_bypass", CCU_PLL_CTL, CCU_PLL_CTL_BYPASS),
	CCU_PLL_DBGFS_BIT_ATTR("pll_lock", CCU_PLL_CTL, CCU_PLL_CTL_LOCK)
};

#define CCU_PLL_DBGFS_BIT_NUM	ARRAY_SIZE(ccu_pll_bits)

static const struct ccu_pll_dbgfs_fld ccu_pll_flds[] = {
	CCU_PLL_DBGFS_FLD_ATTR("pll_nr", CCU_PLL_CTL, CCU_PLL_CTL_CLKR_FLD,
				CCU_PLL_CTL_CLKR_MASK, 1, CCU_PLL_NR_MAX),
	CCU_PLL_DBGFS_FLD_ATTR("pll_nf", CCU_PLL_CTL, CCU_PLL_CTL_CLKF_FLD,
				CCU_PLL_CTL_CLKF_MASK, 1, CCU_PLL_NF_MAX),
	CCU_PLL_DBGFS_FLD_ATTR("pll_od", CCU_PLL_CTL, CCU_PLL_CTL_CLKOD_FLD,
				CCU_PLL_CTL_CLKOD_MASK, 1, CCU_PLL_OD_MAX),
	CCU_PLL_DBGFS_FLD_ATTR("pll_nb", CCU_PLL_CTL1, CCU_PLL_CTL1_BWADJ_FLD,
				CCU_PLL_CTL1_BWADJ_MASK, 1, CCU_PLL_NB_MAX)
};

#define CCU_PLL_DBGFS_FLD_NUM	ARRAY_SIZE(ccu_pll_flds)

/*
 * It can be dangerous to change the PLL settings behind clock framework back,
 * therefore we don't provide any kernel config based compile time option for
 * this feature to enable.
 */
#undef CCU_PLL_ALLOW_WRITE_DEBUGFS
#ifdef CCU_PLL_ALLOW_WRITE_DEBUGFS

static int ccu_pll_dbgfs_bit_set(void *priv, u64 val)
{
	const struct ccu_pll_dbgfs_bit *bit = priv;
	struct ccu_pll *pll = bit->pll;
	unsigned long flags;

	spin_lock_irqsave(&pll->lock, flags);
	regmap_update_bits(pll->sys_regs, pll->reg_ctl + bit->reg,
			   bit->mask, val ? bit->mask : 0);
	spin_unlock_irqrestore(&pll->lock, flags);

	return 0;
}

static int ccu_pll_dbgfs_fld_set(void *priv, u64 val)
{
	struct ccu_pll_dbgfs_fld *fld = priv;
	struct ccu_pll *pll = fld->pll;
	unsigned long flags;
	u32 data;

	val = clamp_t(u64, val, fld->min, fld->max);
	data = ((val - 1) << fld->lsb) & fld->mask;

	spin_lock_irqsave(&pll->lock, flags);
	regmap_update_bits(pll->sys_regs, pll->reg_ctl + fld->reg, fld->mask,
			   data);
	spin_unlock_irqrestore(&pll->lock, flags);

	return 0;
}

#define ccu_pll_dbgfs_mode	0644

#else /* !CCU_PLL_ALLOW_WRITE_DEBUGFS */

#define ccu_pll_dbgfs_bit_set	NULL
#define ccu_pll_dbgfs_fld_set	NULL
#define ccu_pll_dbgfs_mode	0444

#endif /* !CCU_PLL_ALLOW_WRITE_DEBUGFS */

static int ccu_pll_dbgfs_bit_get(void *priv, u64 *val)
{
	struct ccu_pll_dbgfs_bit *bit = priv;
	struct ccu_pll *pll = bit->pll;
	u32 data = 0;

	regmap_read(pll->sys_regs, pll->reg_ctl + bit->reg, &data);
	*val = !!(data & bit->mask);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(ccu_pll_dbgfs_bit_fops,
	ccu_pll_dbgfs_bit_get, ccu_pll_dbgfs_bit_set, "%llu\n");

static int ccu_pll_dbgfs_fld_get(void *priv, u64 *val)
{
	struct ccu_pll_dbgfs_fld *fld = priv;
	struct ccu_pll *pll = fld->pll;
	u32 data = 0;

	regmap_read(pll->sys_regs, pll->reg_ctl + fld->reg, &data);
	*val = ((data & fld->mask) >> fld->lsb) + 1;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(ccu_pll_dbgfs_fld_fops,
	ccu_pll_dbgfs_fld_get, ccu_pll_dbgfs_fld_set, "%llu\n");

static void ccu_pll_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	struct ccu_pll *pll = to_ccu_pll(hw);
	struct ccu_pll_dbgfs_bit *bits;
	struct ccu_pll_dbgfs_fld *flds;
	int idx;

	bits = kcalloc(CCU_PLL_DBGFS_BIT_NUM, sizeof(*bits), GFP_KERNEL);
	if (!bits)
		return;

	for (idx = 0; idx < CCU_PLL_DBGFS_BIT_NUM; ++idx) {
		bits[idx] = ccu_pll_bits[idx];
		bits[idx].pll = pll;

		debugfs_create_file_unsafe(bits[idx].name, ccu_pll_dbgfs_mode,
					   dentry, &bits[idx],
					   &ccu_pll_dbgfs_bit_fops);
	}

	flds = kcalloc(CCU_PLL_DBGFS_FLD_NUM, sizeof(*flds), GFP_KERNEL);
	if (!flds)
		return;

	for (idx = 0; idx < CCU_PLL_DBGFS_FLD_NUM; ++idx) {
		flds[idx] = ccu_pll_flds[idx];
		flds[idx].pll = pll;

		debugfs_create_file_unsafe(flds[idx].name, ccu_pll_dbgfs_mode,
					   dentry, &flds[idx],
					   &ccu_pll_dbgfs_fld_fops);
	}
}

#else /* !CONFIG_DEBUG_FS */

#define ccu_pll_debug_init NULL

#endif /* !CONFIG_DEBUG_FS */

static const struct clk_ops ccu_pll_gate_to_set_ops = {
	.enable = ccu_pll_enable,
	.disable = ccu_pll_disable,
	.is_enabled = ccu_pll_is_enabled,
	.recalc_rate = ccu_pll_recalc_rate,
	.determine_rate = ccu_pll_determine_rate,
	.set_rate = ccu_pll_set_rate_norst,
	.debug_init = ccu_pll_debug_init
};

static const struct clk_ops ccu_pll_straight_set_ops = {
	.enable = ccu_pll_enable,
	.disable = ccu_pll_disable,
	.is_enabled = ccu_pll_is_enabled,
	.recalc_rate = ccu_pll_recalc_rate,
	.determine_rate = ccu_pll_determine_rate,
	.set_rate = ccu_pll_set_rate_reset,
	.debug_init = ccu_pll_debug_init
};

struct ccu_pll *ccu_pll_hw_register(const struct ccu_pll_init_data *pll_init)
{
	struct clk_parent_data parent_data = { };
	struct clk_init_data hw_init = { };
	struct ccu_pll *pll;
	int ret;

	if (!pll_init)
		return ERR_PTR(-EINVAL);

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	/*
	 * Note since Baikal-T1 System Controller registers are MMIO-backed
	 * we won't check the regmap IO operations return status, because it
	 * must be zero anyway.
	 */
	pll->hw.init = &hw_init;
	pll->reg_ctl = pll_init->base + CCU_PLL_CTL;
	pll->reg_ctl1 = pll_init->base + CCU_PLL_CTL1;
	pll->sys_regs = pll_init->sys_regs;
	pll->id = pll_init->id;
	spin_lock_init(&pll->lock);

	hw_init.name = pll_init->name;
	hw_init.flags = pll_init->flags;

	if (hw_init.flags & CLK_SET_RATE_GATE)
		hw_init.ops = &ccu_pll_gate_to_set_ops;
	else
		hw_init.ops = &ccu_pll_straight_set_ops;

	if (!pll_init->parent_name) {
		ret = -EINVAL;
		goto err_free_pll;
	}
	parent_data.fw_name = pll_init->parent_name;
	hw_init.parent_data = &parent_data;
	hw_init.num_parents = 1;

	ret = of_clk_hw_register(pll_init->np, &pll->hw);
	if (ret)
		goto err_free_pll;

	return pll;

err_free_pll:
	kfree(pll);

	return ERR_PTR(ret);
}

void ccu_pll_hw_unregister(struct ccu_pll *pll)
{
	clk_hw_unregister(&pll->hw);

	kfree(pll);
}
