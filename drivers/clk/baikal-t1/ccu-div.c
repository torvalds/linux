// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *   Dmitry Dunaev <dmitry.dunaev@baikalelectronics.ru>
 *
 * Baikal-T1 CCU Dividers interface driver
 */

#define pr_fmt(fmt) "bt1-ccu-div: " fmt

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/time64.h>
#include <linux/debugfs.h>

#include "ccu-div.h"

#define CCU_DIV_CTL			0x00
#define CCU_DIV_CTL_EN			BIT(0)
#define CCU_DIV_CTL_RST			BIT(1)
#define CCU_DIV_CTL_SET_CLKDIV		BIT(2)
#define CCU_DIV_CTL_CLKDIV_FLD		4
#define CCU_DIV_CTL_CLKDIV_MASK(_width) \
	GENMASK((_width) + CCU_DIV_CTL_CLKDIV_FLD - 1, CCU_DIV_CTL_CLKDIV_FLD)
#define CCU_DIV_CTL_LOCK_SHIFTED	BIT(27)
#define CCU_DIV_CTL_LOCK_NORMAL		BIT(31)

#define CCU_DIV_RST_DELAY_US		1
#define CCU_DIV_LOCK_CHECK_RETRIES	50

#define CCU_DIV_CLKDIV_MIN		0
#define CCU_DIV_CLKDIV_MAX(_mask) \
	((_mask) >> CCU_DIV_CTL_CLKDIV_FLD)

/*
 * Use the next two methods until there are generic field setter and
 * getter available with non-constant mask support.
 */
static inline u32 ccu_div_get(u32 mask, u32 val)
{
	return (val & mask) >> CCU_DIV_CTL_CLKDIV_FLD;
}

static inline u32 ccu_div_prep(u32 mask, u32 val)
{
	return (val << CCU_DIV_CTL_CLKDIV_FLD) & mask;
}

static inline unsigned long ccu_div_lock_delay_ns(unsigned long ref_clk,
						  unsigned long div)
{
	u64 ns = 4ULL * (div ?: 1) * NSEC_PER_SEC;

	do_div(ns, ref_clk);

	return ns;
}

static inline unsigned long ccu_div_calc_freq(unsigned long ref_clk,
					      unsigned long div)
{
	return ref_clk / (div ?: 1);
}

static int ccu_div_var_update_clkdiv(struct ccu_div *div,
				     unsigned long parent_rate,
				     unsigned long divider)
{
	unsigned long nd;
	u32 val = 0;
	u32 lock;
	int count;

	nd = ccu_div_lock_delay_ns(parent_rate, divider);

	if (div->features & CCU_DIV_LOCK_SHIFTED)
		lock = CCU_DIV_CTL_LOCK_SHIFTED;
	else
		lock = CCU_DIV_CTL_LOCK_NORMAL;

	regmap_update_bits(div->sys_regs, div->reg_ctl,
			   CCU_DIV_CTL_SET_CLKDIV, CCU_DIV_CTL_SET_CLKDIV);

	/*
	 * Until there is nsec-version of readl_poll_timeout() is available
	 * we have to implement the next polling loop.
	 */
	count = CCU_DIV_LOCK_CHECK_RETRIES;
	do {
		ndelay(nd);
		regmap_read(div->sys_regs, div->reg_ctl, &val);
		if (val & lock)
			return 0;
	} while (--count);

	return -ETIMEDOUT;
}

static int ccu_div_var_enable(struct clk_hw *hw)
{
	struct clk_hw *parent_hw = clk_hw_get_parent(hw);
	struct ccu_div *div = to_ccu_div(hw);
	unsigned long flags;
	u32 val = 0;
	int ret;

	if (!parent_hw) {
		pr_err("Can't enable '%s' with no parent", clk_hw_get_name(hw));
		return -EINVAL;
	}

	regmap_read(div->sys_regs, div->reg_ctl, &val);
	if (val & CCU_DIV_CTL_EN)
		return 0;

	spin_lock_irqsave(&div->lock, flags);
	ret = ccu_div_var_update_clkdiv(div, clk_hw_get_rate(parent_hw),
					ccu_div_get(div->mask, val));
	if (!ret)
		regmap_update_bits(div->sys_regs, div->reg_ctl,
				   CCU_DIV_CTL_EN, CCU_DIV_CTL_EN);
	spin_unlock_irqrestore(&div->lock, flags);
	if (ret)
		pr_err("Divider '%s' lock timed out\n", clk_hw_get_name(hw));

	return ret;
}

static int ccu_div_gate_enable(struct clk_hw *hw)
{
	struct ccu_div *div = to_ccu_div(hw);
	unsigned long flags;

	spin_lock_irqsave(&div->lock, flags);
	regmap_update_bits(div->sys_regs, div->reg_ctl,
			   CCU_DIV_CTL_EN, CCU_DIV_CTL_EN);
	spin_unlock_irqrestore(&div->lock, flags);

	return 0;
}

static void ccu_div_gate_disable(struct clk_hw *hw)
{
	struct ccu_div *div = to_ccu_div(hw);
	unsigned long flags;

	spin_lock_irqsave(&div->lock, flags);
	regmap_update_bits(div->sys_regs, div->reg_ctl, CCU_DIV_CTL_EN, 0);
	spin_unlock_irqrestore(&div->lock, flags);
}

static int ccu_div_gate_is_enabled(struct clk_hw *hw)
{
	struct ccu_div *div = to_ccu_div(hw);
	u32 val = 0;

	regmap_read(div->sys_regs, div->reg_ctl, &val);

	return !!(val & CCU_DIV_CTL_EN);
}

static unsigned long ccu_div_var_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct ccu_div *div = to_ccu_div(hw);
	unsigned long divider;
	u32 val = 0;

	regmap_read(div->sys_regs, div->reg_ctl, &val);
	divider = ccu_div_get(div->mask, val);

	return ccu_div_calc_freq(parent_rate, divider);
}

static inline unsigned long ccu_div_var_calc_divider(unsigned long rate,
						     unsigned long parent_rate,
						     unsigned int mask)
{
	unsigned long divider;

	divider = parent_rate / rate;
	return clamp_t(unsigned long, divider, CCU_DIV_CLKDIV_MIN,
		       CCU_DIV_CLKDIV_MAX(mask));
}

static long ccu_div_var_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct ccu_div *div = to_ccu_div(hw);
	unsigned long divider;

	divider = ccu_div_var_calc_divider(rate, *parent_rate, div->mask);

	return ccu_div_calc_freq(*parent_rate, divider);
}

/*
 * This method is used for the clock divider blocks, which support the
 * on-the-fly rate change. So due to lacking the EN bit functionality
 * they can't be gated before the rate adjustment.
 */
static int ccu_div_var_set_rate_slow(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct ccu_div *div = to_ccu_div(hw);
	unsigned long flags, divider;
	u32 val;
	int ret;

	divider = ccu_div_var_calc_divider(rate, parent_rate, div->mask);
	if (divider == 1 && div->features & CCU_DIV_SKIP_ONE) {
		divider = 0;
	} else if (div->features & CCU_DIV_SKIP_ONE_TO_THREE) {
		if (divider == 1 || divider == 2)
			divider = 0;
		else if (divider == 3)
			divider = 4;
	}

	val = ccu_div_prep(div->mask, divider);

	spin_lock_irqsave(&div->lock, flags);
	regmap_update_bits(div->sys_regs, div->reg_ctl, div->mask, val);
	ret = ccu_div_var_update_clkdiv(div, parent_rate, divider);
	spin_unlock_irqrestore(&div->lock, flags);
	if (ret)
		pr_err("Divider '%s' lock timed out\n", clk_hw_get_name(hw));

	return ret;
}

/*
 * This method is used for the clock divider blocks, which don't support
 * the on-the-fly rate change.
 */
static int ccu_div_var_set_rate_fast(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct ccu_div *div = to_ccu_div(hw);
	unsigned long flags, divider;
	u32 val;

	divider = ccu_div_var_calc_divider(rate, parent_rate, div->mask);
	val = ccu_div_prep(div->mask, divider);

	/*
	 * Also disable the clock divider block if it was enabled by default
	 * or by the bootloader.
	 */
	spin_lock_irqsave(&div->lock, flags);
	regmap_update_bits(div->sys_regs, div->reg_ctl,
			   div->mask | CCU_DIV_CTL_EN, val);
	spin_unlock_irqrestore(&div->lock, flags);

	return 0;
}

static unsigned long ccu_div_fixed_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct ccu_div *div = to_ccu_div(hw);

	return ccu_div_calc_freq(parent_rate, div->divider);
}

static long ccu_div_fixed_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *parent_rate)
{
	struct ccu_div *div = to_ccu_div(hw);

	return ccu_div_calc_freq(*parent_rate, div->divider);
}

static int ccu_div_fixed_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	return 0;
}

int ccu_div_reset_domain(struct ccu_div *div)
{
	unsigned long flags;

	if (!div || !(div->features & CCU_DIV_RESET_DOMAIN))
		return -EINVAL;

	spin_lock_irqsave(&div->lock, flags);
	regmap_update_bits(div->sys_regs, div->reg_ctl,
			   CCU_DIV_CTL_RST, CCU_DIV_CTL_RST);
	spin_unlock_irqrestore(&div->lock, flags);

	/* The next delay must be enough to cover all the resets. */
	udelay(CCU_DIV_RST_DELAY_US);

	return 0;
}

#ifdef CONFIG_DEBUG_FS

struct ccu_div_dbgfs_bit {
	struct ccu_div *div;
	const char *name;
	u32 mask;
};

#define CCU_DIV_DBGFS_BIT_ATTR(_name, _mask) {	\
		.name = _name,			\
		.mask = _mask			\
	}

static const struct ccu_div_dbgfs_bit ccu_div_bits[] = {
	CCU_DIV_DBGFS_BIT_ATTR("div_en", CCU_DIV_CTL_EN),
	CCU_DIV_DBGFS_BIT_ATTR("div_rst", CCU_DIV_CTL_RST),
	CCU_DIV_DBGFS_BIT_ATTR("div_bypass", CCU_DIV_CTL_SET_CLKDIV),
	CCU_DIV_DBGFS_BIT_ATTR("div_lock", CCU_DIV_CTL_LOCK_NORMAL)
};

#define CCU_DIV_DBGFS_BIT_NUM	ARRAY_SIZE(ccu_div_bits)

/*
 * It can be dangerous to change the Divider settings behind clock framework
 * back, therefore we don't provide any kernel config based compile time option
 * for this feature to enable.
 */
#undef CCU_DIV_ALLOW_WRITE_DEBUGFS
#ifdef CCU_DIV_ALLOW_WRITE_DEBUGFS

static int ccu_div_dbgfs_bit_set(void *priv, u64 val)
{
	const struct ccu_div_dbgfs_bit *bit = priv;
	struct ccu_div *div = bit->div;
	unsigned long flags;

	spin_lock_irqsave(&div->lock, flags);
	regmap_update_bits(div->sys_regs, div->reg_ctl,
			   bit->mask, val ? bit->mask : 0);
	spin_unlock_irqrestore(&div->lock, flags);

	return 0;
}

static int ccu_div_dbgfs_var_clkdiv_set(void *priv, u64 val)
{
	struct ccu_div *div = priv;
	unsigned long flags;
	u32 data;

	val = clamp_t(u64, val, CCU_DIV_CLKDIV_MIN,
		      CCU_DIV_CLKDIV_MAX(div->mask));
	data = ccu_div_prep(div->mask, val);

	spin_lock_irqsave(&div->lock, flags);
	regmap_update_bits(div->sys_regs, div->reg_ctl, div->mask, data);
	spin_unlock_irqrestore(&div->lock, flags);

	return 0;
}

#define ccu_div_dbgfs_mode		0644

#else /* !CCU_DIV_ALLOW_WRITE_DEBUGFS */

#define ccu_div_dbgfs_bit_set		NULL
#define ccu_div_dbgfs_var_clkdiv_set	NULL
#define ccu_div_dbgfs_mode		0444

#endif /* !CCU_DIV_ALLOW_WRITE_DEBUGFS */

static int ccu_div_dbgfs_bit_get(void *priv, u64 *val)
{
	const struct ccu_div_dbgfs_bit *bit = priv;
	struct ccu_div *div = bit->div;
	u32 data = 0;

	regmap_read(div->sys_regs, div->reg_ctl, &data);
	*val = !!(data & bit->mask);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(ccu_div_dbgfs_bit_fops,
	ccu_div_dbgfs_bit_get, ccu_div_dbgfs_bit_set, "%llu\n");

static int ccu_div_dbgfs_var_clkdiv_get(void *priv, u64 *val)
{
	struct ccu_div *div = priv;
	u32 data = 0;

	regmap_read(div->sys_regs, div->reg_ctl, &data);
	*val = ccu_div_get(div->mask, data);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(ccu_div_dbgfs_var_clkdiv_fops,
	ccu_div_dbgfs_var_clkdiv_get, ccu_div_dbgfs_var_clkdiv_set, "%llu\n");

static int ccu_div_dbgfs_fixed_clkdiv_get(void *priv, u64 *val)
{
	struct ccu_div *div = priv;

	*val = div->divider;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(ccu_div_dbgfs_fixed_clkdiv_fops,
	ccu_div_dbgfs_fixed_clkdiv_get, NULL, "%llu\n");

static void ccu_div_var_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	struct ccu_div *div = to_ccu_div(hw);
	struct ccu_div_dbgfs_bit *bits;
	int didx, bidx, num = 2;
	const char *name;

	num += !!(div->flags & CLK_SET_RATE_GATE) +
		!!(div->features & CCU_DIV_RESET_DOMAIN);

	bits = kcalloc(num, sizeof(*bits), GFP_KERNEL);
	if (!bits)
		return;

	for (didx = 0, bidx = 0; bidx < CCU_DIV_DBGFS_BIT_NUM; ++bidx) {
		name = ccu_div_bits[bidx].name;
		if (!(div->flags & CLK_SET_RATE_GATE) &&
		    !strcmp("div_en", name)) {
			continue;
		}

		if (!(div->features & CCU_DIV_RESET_DOMAIN) &&
		    !strcmp("div_rst", name)) {
			continue;
		}

		bits[didx] = ccu_div_bits[bidx];
		bits[didx].div = div;

		if (div->features & CCU_DIV_LOCK_SHIFTED &&
		    !strcmp("div_lock", name)) {
			bits[didx].mask = CCU_DIV_CTL_LOCK_SHIFTED;
		}

		debugfs_create_file_unsafe(bits[didx].name, ccu_div_dbgfs_mode,
					   dentry, &bits[didx],
					   &ccu_div_dbgfs_bit_fops);
		++didx;
	}

	debugfs_create_file_unsafe("div_clkdiv", ccu_div_dbgfs_mode, dentry,
				   div, &ccu_div_dbgfs_var_clkdiv_fops);
}

static void ccu_div_gate_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	struct ccu_div *div = to_ccu_div(hw);
	struct ccu_div_dbgfs_bit *bit;

	bit = kmalloc(sizeof(*bit), GFP_KERNEL);
	if (!bit)
		return;

	*bit = ccu_div_bits[0];
	bit->div = div;
	debugfs_create_file_unsafe(bit->name, ccu_div_dbgfs_mode, dentry, bit,
				   &ccu_div_dbgfs_bit_fops);

	debugfs_create_file_unsafe("div_clkdiv", 0400, dentry, div,
				   &ccu_div_dbgfs_fixed_clkdiv_fops);
}

static void ccu_div_fixed_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	struct ccu_div *div = to_ccu_div(hw);

	debugfs_create_file_unsafe("div_clkdiv", 0400, dentry, div,
				   &ccu_div_dbgfs_fixed_clkdiv_fops);
}

#else /* !CONFIG_DEBUG_FS */

#define ccu_div_var_debug_init NULL
#define ccu_div_gate_debug_init NULL
#define ccu_div_fixed_debug_init NULL

#endif /* !CONFIG_DEBUG_FS */

static const struct clk_ops ccu_div_var_gate_to_set_ops = {
	.enable = ccu_div_var_enable,
	.disable = ccu_div_gate_disable,
	.is_enabled = ccu_div_gate_is_enabled,
	.recalc_rate = ccu_div_var_recalc_rate,
	.round_rate = ccu_div_var_round_rate,
	.set_rate = ccu_div_var_set_rate_fast,
	.debug_init = ccu_div_var_debug_init
};

static const struct clk_ops ccu_div_var_nogate_ops = {
	.recalc_rate = ccu_div_var_recalc_rate,
	.round_rate = ccu_div_var_round_rate,
	.set_rate = ccu_div_var_set_rate_slow,
	.debug_init = ccu_div_var_debug_init
};

static const struct clk_ops ccu_div_gate_ops = {
	.enable = ccu_div_gate_enable,
	.disable = ccu_div_gate_disable,
	.is_enabled = ccu_div_gate_is_enabled,
	.recalc_rate = ccu_div_fixed_recalc_rate,
	.round_rate = ccu_div_fixed_round_rate,
	.set_rate = ccu_div_fixed_set_rate,
	.debug_init = ccu_div_gate_debug_init
};

static const struct clk_ops ccu_div_fixed_ops = {
	.recalc_rate = ccu_div_fixed_recalc_rate,
	.round_rate = ccu_div_fixed_round_rate,
	.set_rate = ccu_div_fixed_set_rate,
	.debug_init = ccu_div_fixed_debug_init
};

struct ccu_div *ccu_div_hw_register(const struct ccu_div_init_data *div_init)
{
	struct clk_parent_data parent_data = { };
	struct clk_init_data hw_init = { };
	struct ccu_div *div;
	int ret;

	if (!div_init)
		return ERR_PTR(-EINVAL);

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	/*
	 * Note since Baikal-T1 System Controller registers are MMIO-backed
	 * we won't check the regmap IO operations return status, because it
	 * must be zero anyway.
	 */
	div->hw.init = &hw_init;
	div->id = div_init->id;
	div->reg_ctl = div_init->base + CCU_DIV_CTL;
	div->sys_regs = div_init->sys_regs;
	div->flags = div_init->flags;
	div->features = div_init->features;
	spin_lock_init(&div->lock);

	hw_init.name = div_init->name;
	hw_init.flags = div_init->flags;

	if (div_init->type == CCU_DIV_VAR) {
		if (hw_init.flags & CLK_SET_RATE_GATE)
			hw_init.ops = &ccu_div_var_gate_to_set_ops;
		else
			hw_init.ops = &ccu_div_var_nogate_ops;
		div->mask = CCU_DIV_CTL_CLKDIV_MASK(div_init->width);
	} else if (div_init->type == CCU_DIV_GATE) {
		hw_init.ops = &ccu_div_gate_ops;
		div->divider = div_init->divider;
	} else if (div_init->type == CCU_DIV_FIXED) {
		hw_init.ops = &ccu_div_fixed_ops;
		div->divider = div_init->divider;
	} else {
		ret = -EINVAL;
		goto err_free_div;
	}

	if (!div_init->parent_name) {
		ret = -EINVAL;
		goto err_free_div;
	}
	parent_data.fw_name = div_init->parent_name;
	parent_data.name = div_init->parent_name;
	hw_init.parent_data = &parent_data;
	hw_init.num_parents = 1;

	ret = of_clk_hw_register(div_init->np, &div->hw);
	if (ret)
		goto err_free_div;

	return div;

err_free_div:
	kfree(div);

	return ERR_PTR(ret);
}

void ccu_div_hw_unregister(struct ccu_div *div)
{
	clk_hw_unregister(&div->hw);

	kfree(div);
}
