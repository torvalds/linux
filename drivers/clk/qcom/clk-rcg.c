// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include <asm/div64.h>

#include "clk-rcg.h"
#include "common.h"

static u32 ns_to_src(struct src_sel *s, u32 ns)
{
	ns >>= s->src_sel_shift;
	ns &= SRC_SEL_MASK;
	return ns;
}

static u32 src_to_ns(struct src_sel *s, u8 src, u32 ns)
{
	u32 mask;

	mask = SRC_SEL_MASK;
	mask <<= s->src_sel_shift;
	ns &= ~mask;

	ns |= src << s->src_sel_shift;
	return ns;
}

static u8 clk_rcg_get_parent(struct clk_hw *hw)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	int num_parents = clk_hw_get_num_parents(hw);
	u32 ns;
	int i, ret;

	ret = regmap_read(rcg->clkr.regmap, rcg->ns_reg, &ns);
	if (ret)
		goto err;
	ns = ns_to_src(&rcg->s, ns);
	for (i = 0; i < num_parents; i++)
		if (ns == rcg->s.parent_map[i].cfg)
			return i;

err:
	pr_debug("%s: Clock %s has invalid parent, using default.\n",
		 __func__, clk_hw_get_name(hw));
	return 0;
}

static int reg_to_bank(struct clk_dyn_rcg *rcg, u32 bank)
{
	bank &= BIT(rcg->mux_sel_bit);
	return !!bank;
}

static u8 clk_dyn_rcg_get_parent(struct clk_hw *hw)
{
	struct clk_dyn_rcg *rcg = to_clk_dyn_rcg(hw);
	int num_parents = clk_hw_get_num_parents(hw);
	u32 ns, reg;
	int bank;
	int i, ret;
	struct src_sel *s;

	ret = regmap_read(rcg->clkr.regmap, rcg->bank_reg, &reg);
	if (ret)
		goto err;
	bank = reg_to_bank(rcg, reg);
	s = &rcg->s[bank];

	ret = regmap_read(rcg->clkr.regmap, rcg->ns_reg[bank], &ns);
	if (ret)
		goto err;
	ns = ns_to_src(s, ns);

	for (i = 0; i < num_parents; i++)
		if (ns == s->parent_map[i].cfg)
			return i;

err:
	pr_debug("%s: Clock %s has invalid parent, using default.\n",
		 __func__, clk_hw_get_name(hw));
	return 0;
}

static int clk_rcg_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	u32 ns;

	regmap_read(rcg->clkr.regmap, rcg->ns_reg, &ns);
	ns = src_to_ns(&rcg->s, rcg->s.parent_map[index].cfg, ns);
	regmap_write(rcg->clkr.regmap, rcg->ns_reg, ns);

	return 0;
}

static u32 md_to_m(struct mn *mn, u32 md)
{
	md >>= mn->m_val_shift;
	md &= BIT(mn->width) - 1;
	return md;
}

static u32 ns_to_pre_div(struct pre_div *p, u32 ns)
{
	ns >>= p->pre_div_shift;
	ns &= BIT(p->pre_div_width) - 1;
	return ns;
}

static u32 pre_div_to_ns(struct pre_div *p, u8 pre_div, u32 ns)
{
	u32 mask;

	mask = BIT(p->pre_div_width) - 1;
	mask <<= p->pre_div_shift;
	ns &= ~mask;

	ns |= pre_div << p->pre_div_shift;
	return ns;
}

static u32 mn_to_md(struct mn *mn, u32 m, u32 n, u32 md)
{
	u32 mask, mask_w;

	mask_w = BIT(mn->width) - 1;
	mask = (mask_w << mn->m_val_shift) | mask_w;
	md &= ~mask;

	if (n) {
		m <<= mn->m_val_shift;
		md |= m;
		md |= ~n & mask_w;
	}

	return md;
}

static u32 ns_m_to_n(struct mn *mn, u32 ns, u32 m)
{
	ns = ~ns >> mn->n_val_shift;
	ns &= BIT(mn->width) - 1;
	return ns + m;
}

static u32 reg_to_mnctr_mode(struct mn *mn, u32 val)
{
	val >>= mn->mnctr_mode_shift;
	val &= MNCTR_MODE_MASK;
	return val;
}

static u32 mn_to_ns(struct mn *mn, u32 m, u32 n, u32 ns)
{
	u32 mask;

	mask = BIT(mn->width) - 1;
	mask <<= mn->n_val_shift;
	ns &= ~mask;

	if (n) {
		n = n - m;
		n = ~n;
		n &= BIT(mn->width) - 1;
		n <<= mn->n_val_shift;
		ns |= n;
	}

	return ns;
}

static u32 mn_to_reg(struct mn *mn, u32 m, u32 n, u32 val)
{
	u32 mask;

	mask = MNCTR_MODE_MASK << mn->mnctr_mode_shift;
	mask |= BIT(mn->mnctr_en_bit);
	val &= ~mask;

	if (n) {
		val |= BIT(mn->mnctr_en_bit);
		val |= MNCTR_MODE_DUAL << mn->mnctr_mode_shift;
	}

	return val;
}

static int configure_bank(struct clk_dyn_rcg *rcg, const struct freq_tbl *f)
{
	u32 ns, md, reg;
	int bank, new_bank, ret, index;
	struct mn *mn;
	struct pre_div *p;
	struct src_sel *s;
	bool enabled;
	u32 md_reg, ns_reg;
	bool banked_mn = !!rcg->mn[1].width;
	bool banked_p = !!rcg->p[1].pre_div_width;
	struct clk_hw *hw = &rcg->clkr.hw;

	enabled = __clk_is_enabled(hw->clk);

	ret = regmap_read(rcg->clkr.regmap, rcg->bank_reg, &reg);
	if (ret)
		return ret;
	bank = reg_to_bank(rcg, reg);
	new_bank = enabled ? !bank : bank;

	ns_reg = rcg->ns_reg[new_bank];
	ret = regmap_read(rcg->clkr.regmap, ns_reg, &ns);
	if (ret)
		return ret;

	if (banked_mn) {
		mn = &rcg->mn[new_bank];
		md_reg = rcg->md_reg[new_bank];

		ns |= BIT(mn->mnctr_reset_bit);
		ret = regmap_write(rcg->clkr.regmap, ns_reg, ns);
		if (ret)
			return ret;

		ret = regmap_read(rcg->clkr.regmap, md_reg, &md);
		if (ret)
			return ret;
		md = mn_to_md(mn, f->m, f->n, md);
		ret = regmap_write(rcg->clkr.regmap, md_reg, md);
		if (ret)
			return ret;
		ns = mn_to_ns(mn, f->m, f->n, ns);
		ret = regmap_write(rcg->clkr.regmap, ns_reg, ns);
		if (ret)
			return ret;

		/* Two NS registers means mode control is in NS register */
		if (rcg->ns_reg[0] != rcg->ns_reg[1]) {
			ns = mn_to_reg(mn, f->m, f->n, ns);
			ret = regmap_write(rcg->clkr.regmap, ns_reg, ns);
			if (ret)
				return ret;
		} else {
			reg = mn_to_reg(mn, f->m, f->n, reg);
			ret = regmap_write(rcg->clkr.regmap, rcg->bank_reg,
					   reg);
			if (ret)
				return ret;
		}

		ns &= ~BIT(mn->mnctr_reset_bit);
		ret = regmap_write(rcg->clkr.regmap, ns_reg, ns);
		if (ret)
			return ret;
	}

	if (banked_p) {
		p = &rcg->p[new_bank];
		ns = pre_div_to_ns(p, f->pre_div - 1, ns);
	}

	s = &rcg->s[new_bank];
	index = qcom_find_src_index(hw, s->parent_map, f->src);
	if (index < 0)
		return index;
	ns = src_to_ns(s, s->parent_map[index].cfg, ns);
	ret = regmap_write(rcg->clkr.regmap, ns_reg, ns);
	if (ret)
		return ret;

	if (enabled) {
		ret = regmap_read(rcg->clkr.regmap, rcg->bank_reg, &reg);
		if (ret)
			return ret;
		reg ^= BIT(rcg->mux_sel_bit);
		ret = regmap_write(rcg->clkr.regmap, rcg->bank_reg, reg);
		if (ret)
			return ret;
	}
	return 0;
}

static int clk_dyn_rcg_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_dyn_rcg *rcg = to_clk_dyn_rcg(hw);
	u32 ns, md, reg;
	int bank;
	struct freq_tbl f = { 0 };
	bool banked_mn = !!rcg->mn[1].width;
	bool banked_p = !!rcg->p[1].pre_div_width;

	regmap_read(rcg->clkr.regmap, rcg->bank_reg, &reg);
	bank = reg_to_bank(rcg, reg);

	regmap_read(rcg->clkr.regmap, rcg->ns_reg[bank], &ns);

	if (banked_mn) {
		regmap_read(rcg->clkr.regmap, rcg->md_reg[bank], &md);
		f.m = md_to_m(&rcg->mn[bank], md);
		f.n = ns_m_to_n(&rcg->mn[bank], ns, f.m);
	}

	if (banked_p)
		f.pre_div = ns_to_pre_div(&rcg->p[bank], ns) + 1;

	f.src = qcom_find_src_index(hw, rcg->s[bank].parent_map, index);
	return configure_bank(rcg, &f);
}

/*
 * Calculate m/n:d rate
 *
 *          parent_rate     m
 *   rate = ----------- x  ---
 *            pre_div       n
 */
static unsigned long
calc_rate(unsigned long rate, u32 m, u32 n, u32 mode, u32 pre_div)
{
	if (pre_div)
		rate /= pre_div + 1;

	if (mode) {
		u64 tmp = rate;
		tmp *= m;
		do_div(tmp, n);
		rate = tmp;
	}

	return rate;
}

static unsigned long
clk_rcg_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	u32 pre_div, m = 0, n = 0, ns, md, mode = 0;
	struct mn *mn = &rcg->mn;

	regmap_read(rcg->clkr.regmap, rcg->ns_reg, &ns);
	pre_div = ns_to_pre_div(&rcg->p, ns);

	if (rcg->mn.width) {
		regmap_read(rcg->clkr.regmap, rcg->md_reg, &md);
		m = md_to_m(mn, md);
		n = ns_m_to_n(mn, ns, m);
		/* MN counter mode is in hw.enable_reg sometimes */
		if (rcg->clkr.enable_reg != rcg->ns_reg)
			regmap_read(rcg->clkr.regmap, rcg->clkr.enable_reg, &mode);
		else
			mode = ns;
		mode = reg_to_mnctr_mode(mn, mode);
	}

	return calc_rate(parent_rate, m, n, mode, pre_div);
}

static unsigned long
clk_dyn_rcg_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_dyn_rcg *rcg = to_clk_dyn_rcg(hw);
	u32 m, n, pre_div, ns, md, mode, reg;
	int bank;
	struct mn *mn;
	bool banked_p = !!rcg->p[1].pre_div_width;
	bool banked_mn = !!rcg->mn[1].width;

	regmap_read(rcg->clkr.regmap, rcg->bank_reg, &reg);
	bank = reg_to_bank(rcg, reg);

	regmap_read(rcg->clkr.regmap, rcg->ns_reg[bank], &ns);
	m = n = pre_div = mode = 0;

	if (banked_mn) {
		mn = &rcg->mn[bank];
		regmap_read(rcg->clkr.regmap, rcg->md_reg[bank], &md);
		m = md_to_m(mn, md);
		n = ns_m_to_n(mn, ns, m);
		/* Two NS registers means mode control is in NS register */
		if (rcg->ns_reg[0] != rcg->ns_reg[1])
			reg = ns;
		mode = reg_to_mnctr_mode(mn, reg);
	}

	if (banked_p)
		pre_div = ns_to_pre_div(&rcg->p[bank], ns);

	return calc_rate(parent_rate, m, n, mode, pre_div);
}

static int _freq_tbl_determine_rate(struct clk_hw *hw, const struct freq_tbl *f,
		struct clk_rate_request *req,
		const struct parent_map *parent_map)
{
	unsigned long clk_flags, rate = req->rate;
	struct clk_hw *p;
	int index;

	f = qcom_find_freq(f, rate);
	if (!f)
		return -EINVAL;

	index = qcom_find_src_index(hw, parent_map, f->src);
	if (index < 0)
		return index;

	clk_flags = clk_hw_get_flags(hw);
	p = clk_hw_get_parent_by_index(hw, index);
	if (clk_flags & CLK_SET_RATE_PARENT) {
		rate = rate * f->pre_div;
		if (f->n) {
			u64 tmp = rate;
			tmp = tmp * f->n;
			do_div(tmp, f->m);
			rate = tmp;
		}
	} else {
		rate =  clk_hw_get_rate(p);
	}
	req->best_parent_hw = p;
	req->best_parent_rate = rate;
	req->rate = f->freq;

	return 0;
}

static int clk_rcg_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);

	return _freq_tbl_determine_rate(hw, rcg->freq_tbl, req,
					rcg->s.parent_map);
}

static int clk_dyn_rcg_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	struct clk_dyn_rcg *rcg = to_clk_dyn_rcg(hw);
	u32 reg;
	int bank;
	struct src_sel *s;

	regmap_read(rcg->clkr.regmap, rcg->bank_reg, &reg);
	bank = reg_to_bank(rcg, reg);
	s = &rcg->s[bank];

	return _freq_tbl_determine_rate(hw, rcg->freq_tbl, req, s->parent_map);
}

static int clk_rcg_bypass_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	const struct freq_tbl *f = rcg->freq_tbl;
	struct clk_hw *p;
	int index = qcom_find_src_index(hw, rcg->s.parent_map, f->src);

	req->best_parent_hw = p = clk_hw_get_parent_by_index(hw, index);
	req->best_parent_rate = clk_hw_round_rate(p, req->rate);
	req->rate = req->best_parent_rate;

	return 0;
}

static int __clk_rcg_set_rate(struct clk_rcg *rcg, const struct freq_tbl *f)
{
	u32 ns, md, ctl;
	struct mn *mn = &rcg->mn;
	u32 mask = 0;
	unsigned int reset_reg;

	if (rcg->mn.reset_in_cc)
		reset_reg = rcg->clkr.enable_reg;
	else
		reset_reg = rcg->ns_reg;

	if (rcg->mn.width) {
		mask = BIT(mn->mnctr_reset_bit);
		regmap_update_bits(rcg->clkr.regmap, reset_reg, mask, mask);

		regmap_read(rcg->clkr.regmap, rcg->md_reg, &md);
		md = mn_to_md(mn, f->m, f->n, md);
		regmap_write(rcg->clkr.regmap, rcg->md_reg, md);

		regmap_read(rcg->clkr.regmap, rcg->ns_reg, &ns);
		/* MN counter mode is in hw.enable_reg sometimes */
		if (rcg->clkr.enable_reg != rcg->ns_reg) {
			regmap_read(rcg->clkr.regmap, rcg->clkr.enable_reg, &ctl);
			ctl = mn_to_reg(mn, f->m, f->n, ctl);
			regmap_write(rcg->clkr.regmap, rcg->clkr.enable_reg, ctl);
		} else {
			ns = mn_to_reg(mn, f->m, f->n, ns);
		}
		ns = mn_to_ns(mn, f->m, f->n, ns);
	} else {
		regmap_read(rcg->clkr.regmap, rcg->ns_reg, &ns);
	}

	ns = pre_div_to_ns(&rcg->p, f->pre_div - 1, ns);
	regmap_write(rcg->clkr.regmap, rcg->ns_reg, ns);

	regmap_update_bits(rcg->clkr.regmap, reset_reg, mask, 0);

	return 0;
}

static int clk_rcg_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	const struct freq_tbl *f;

	f = qcom_find_freq(rcg->freq_tbl, rate);
	if (!f)
		return -EINVAL;

	return __clk_rcg_set_rate(rcg, f);
}

static int clk_rcg_bypass_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);

	return __clk_rcg_set_rate(rcg, rcg->freq_tbl);
}

static int clk_rcg_bypass2_determine_rate(struct clk_hw *hw,
				struct clk_rate_request *req)
{
	struct clk_hw *p;

	p = req->best_parent_hw;
	req->best_parent_rate = clk_hw_round_rate(p, req->rate);
	req->rate = req->best_parent_rate;

	return 0;
}

static int clk_rcg_bypass2_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	struct freq_tbl f = { 0 };
	u32 ns, src;
	int i, ret, num_parents = clk_hw_get_num_parents(hw);

	ret = regmap_read(rcg->clkr.regmap, rcg->ns_reg, &ns);
	if (ret)
		return ret;

	src = ns_to_src(&rcg->s, ns);
	f.pre_div = ns_to_pre_div(&rcg->p, ns) + 1;

	for (i = 0; i < num_parents; i++) {
		if (src == rcg->s.parent_map[i].cfg) {
			f.src = rcg->s.parent_map[i].src;
			return __clk_rcg_set_rate(rcg, &f);
		}
	}

	return -EINVAL;
}

static int clk_rcg_bypass2_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	/* Read the hardware to determine parent during set_rate */
	return clk_rcg_bypass2_set_rate(hw, rate, parent_rate);
}

struct frac_entry {
	int num;
	int den;
};

static const struct frac_entry pixel_table[] = {
	{ 1, 2 },
	{ 1, 3 },
	{ 3, 16 },
	{ }
};

static int clk_rcg_pixel_determine_rate(struct clk_hw *hw,
		struct clk_rate_request *req)
{
	int delta = 100000;
	const struct frac_entry *frac = pixel_table;
	unsigned long request, src_rate;

	for (; frac->num; frac++) {
		request = (req->rate * frac->den) / frac->num;

		src_rate = clk_hw_round_rate(req->best_parent_hw, request);

		if ((src_rate < (request - delta)) ||
			(src_rate > (request + delta)))
			continue;

		req->best_parent_rate = src_rate;
		req->rate = (src_rate * frac->num) / frac->den;
		return 0;
	}

	return -EINVAL;
}

static int clk_rcg_pixel_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	int delta = 100000;
	const struct frac_entry *frac = pixel_table;
	unsigned long request;
	struct freq_tbl f = { 0 };
	u32 ns, src;
	int i, ret, num_parents = clk_hw_get_num_parents(hw);

	ret = regmap_read(rcg->clkr.regmap, rcg->ns_reg, &ns);
	if (ret)
		return ret;

	src = ns_to_src(&rcg->s, ns);

	for (i = 0; i < num_parents; i++) {
		if (src == rcg->s.parent_map[i].cfg) {
			f.src = rcg->s.parent_map[i].src;
			break;
		}
	}

	/* bypass the pre divider */
	f.pre_div = 1;

	/* let us find appropriate m/n values for this */
	for (; frac->num; frac++) {
		request = (rate * frac->den) / frac->num;

		if ((parent_rate < (request - delta)) ||
			(parent_rate > (request + delta)))
			continue;

		f.m = frac->num;
		f.n = frac->den;

		return __clk_rcg_set_rate(rcg, &f);
	}

	return -EINVAL;
}

static int clk_rcg_pixel_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return clk_rcg_pixel_set_rate(hw, rate, parent_rate);
}

static int clk_rcg_esc_determine_rate(struct clk_hw *hw,
		struct clk_rate_request *req)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	int pre_div_max = BIT(rcg->p.pre_div_width);
	int div;
	unsigned long src_rate;

	if (req->rate == 0)
		return -EINVAL;

	src_rate = clk_hw_get_rate(req->best_parent_hw);

	div = src_rate / req->rate;

	if (div >= 1 && div <= pre_div_max) {
		req->best_parent_rate = src_rate;
		req->rate = src_rate / div;
		return 0;
	}

	return -EINVAL;
}

static int clk_rcg_esc_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	struct freq_tbl f = { 0 };
	int pre_div_max = BIT(rcg->p.pre_div_width);
	int div;
	u32 ns;
	int i, ret, num_parents = clk_hw_get_num_parents(hw);

	if (rate == 0)
		return -EINVAL;

	ret = regmap_read(rcg->clkr.regmap, rcg->ns_reg, &ns);
	if (ret)
		return ret;

	ns = ns_to_src(&rcg->s, ns);

	for (i = 0; i < num_parents; i++) {
		if (ns == rcg->s.parent_map[i].cfg) {
			f.src = rcg->s.parent_map[i].src;
			break;
		}
	}

	div = parent_rate / rate;

	if (div >= 1 && div <= pre_div_max) {
		f.pre_div = div;
		return __clk_rcg_set_rate(rcg, &f);
	}

	return -EINVAL;
}

static int clk_rcg_esc_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return clk_rcg_esc_set_rate(hw, rate, parent_rate);
}

/*
 * This type of clock has a glitch-free mux that switches between the output of
 * the M/N counter and an always on clock source (XO). When clk_set_rate() is
 * called we need to make sure that we don't switch to the M/N counter if it
 * isn't clocking because the mux will get stuck and the clock will stop
 * outputting a clock. This can happen if the framework isn't aware that this
 * clock is on and so clk_set_rate() doesn't turn on the new parent. To fix
 * this we switch the mux in the enable/disable ops and reprogram the M/N
 * counter in the set_rate op. We also make sure to switch away from the M/N
 * counter in set_rate if software thinks the clock is off.
 */
static int clk_rcg_lcc_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	const struct freq_tbl *f;
	int ret;
	u32 gfm = BIT(10);

	f = qcom_find_freq(rcg->freq_tbl, rate);
	if (!f)
		return -EINVAL;

	/* Switch to XO to avoid glitches */
	regmap_update_bits(rcg->clkr.regmap, rcg->ns_reg, gfm, 0);
	ret = __clk_rcg_set_rate(rcg, f);
	/* Switch back to M/N if it's clocking */
	if (__clk_is_enabled(hw->clk))
		regmap_update_bits(rcg->clkr.regmap, rcg->ns_reg, gfm, gfm);

	return ret;
}

static int clk_rcg_lcc_enable(struct clk_hw *hw)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	u32 gfm = BIT(10);

	/* Use M/N */
	return regmap_update_bits(rcg->clkr.regmap, rcg->ns_reg, gfm, gfm);
}

static void clk_rcg_lcc_disable(struct clk_hw *hw)
{
	struct clk_rcg *rcg = to_clk_rcg(hw);
	u32 gfm = BIT(10);

	/* Use XO */
	regmap_update_bits(rcg->clkr.regmap, rcg->ns_reg, gfm, 0);
}

static int __clk_dyn_rcg_set_rate(struct clk_hw *hw, unsigned long rate)
{
	struct clk_dyn_rcg *rcg = to_clk_dyn_rcg(hw);
	const struct freq_tbl *f;

	f = qcom_find_freq(rcg->freq_tbl, rate);
	if (!f)
		return -EINVAL;

	return configure_bank(rcg, f);
}

static int clk_dyn_rcg_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	return __clk_dyn_rcg_set_rate(hw, rate);
}

static int clk_dyn_rcg_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return __clk_dyn_rcg_set_rate(hw, rate);
}

const struct clk_ops clk_rcg_ops = {
	.enable = clk_enable_regmap,
	.disable = clk_disable_regmap,
	.get_parent = clk_rcg_get_parent,
	.set_parent = clk_rcg_set_parent,
	.recalc_rate = clk_rcg_recalc_rate,
	.determine_rate = clk_rcg_determine_rate,
	.set_rate = clk_rcg_set_rate,
};
EXPORT_SYMBOL_GPL(clk_rcg_ops);

const struct clk_ops clk_rcg_bypass_ops = {
	.enable = clk_enable_regmap,
	.disable = clk_disable_regmap,
	.get_parent = clk_rcg_get_parent,
	.set_parent = clk_rcg_set_parent,
	.recalc_rate = clk_rcg_recalc_rate,
	.determine_rate = clk_rcg_bypass_determine_rate,
	.set_rate = clk_rcg_bypass_set_rate,
};
EXPORT_SYMBOL_GPL(clk_rcg_bypass_ops);

const struct clk_ops clk_rcg_bypass2_ops = {
	.enable = clk_enable_regmap,
	.disable = clk_disable_regmap,
	.get_parent = clk_rcg_get_parent,
	.set_parent = clk_rcg_set_parent,
	.recalc_rate = clk_rcg_recalc_rate,
	.determine_rate = clk_rcg_bypass2_determine_rate,
	.set_rate = clk_rcg_bypass2_set_rate,
	.set_rate_and_parent = clk_rcg_bypass2_set_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg_bypass2_ops);

const struct clk_ops clk_rcg_pixel_ops = {
	.enable = clk_enable_regmap,
	.disable = clk_disable_regmap,
	.get_parent = clk_rcg_get_parent,
	.set_parent = clk_rcg_set_parent,
	.recalc_rate = clk_rcg_recalc_rate,
	.determine_rate = clk_rcg_pixel_determine_rate,
	.set_rate = clk_rcg_pixel_set_rate,
	.set_rate_and_parent = clk_rcg_pixel_set_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg_pixel_ops);

const struct clk_ops clk_rcg_esc_ops = {
	.enable = clk_enable_regmap,
	.disable = clk_disable_regmap,
	.get_parent = clk_rcg_get_parent,
	.set_parent = clk_rcg_set_parent,
	.recalc_rate = clk_rcg_recalc_rate,
	.determine_rate = clk_rcg_esc_determine_rate,
	.set_rate = clk_rcg_esc_set_rate,
	.set_rate_and_parent = clk_rcg_esc_set_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg_esc_ops);

const struct clk_ops clk_rcg_lcc_ops = {
	.enable = clk_rcg_lcc_enable,
	.disable = clk_rcg_lcc_disable,
	.get_parent = clk_rcg_get_parent,
	.set_parent = clk_rcg_set_parent,
	.recalc_rate = clk_rcg_recalc_rate,
	.determine_rate = clk_rcg_determine_rate,
	.set_rate = clk_rcg_lcc_set_rate,
};
EXPORT_SYMBOL_GPL(clk_rcg_lcc_ops);

const struct clk_ops clk_dyn_rcg_ops = {
	.enable = clk_enable_regmap,
	.is_enabled = clk_is_enabled_regmap,
	.disable = clk_disable_regmap,
	.get_parent = clk_dyn_rcg_get_parent,
	.set_parent = clk_dyn_rcg_set_parent,
	.recalc_rate = clk_dyn_rcg_recalc_rate,
	.determine_rate = clk_dyn_rcg_determine_rate,
	.set_rate = clk_dyn_rcg_set_rate,
	.set_rate_and_parent = clk_dyn_rcg_set_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_dyn_rcg_ops);
