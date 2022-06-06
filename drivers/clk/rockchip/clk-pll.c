// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Xing Zheng <zhengxing@rock-chips.com>
 */

#include <asm/div64.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/iopoll.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/gcd.h>
#include <linux/clk/rockchip.h>
#include <linux/mfd/syscon.h>
#include "clk.h"

#define PLL_MODE_MASK		0x3
#define PLL_MODE_SLOW		0x0
#define PLL_MODE_NORM		0x1
#define PLL_MODE_DEEP		0x2
#define PLL_RK3328_MODE_MASK	0x1

struct rockchip_clk_pll {
	struct clk_hw		hw;

	struct clk_mux		pll_mux;
	const struct clk_ops	*pll_mux_ops;

	struct notifier_block	clk_nb;

	void __iomem		*reg_base;
	int			lock_offset;
	unsigned int		lock_shift;
	enum rockchip_pll_type	type;
	u8			flags;
	const struct rockchip_pll_rate_table *rate_table;
	unsigned int		rate_count;
	int			sel;
	unsigned long		scaling;
	spinlock_t		*lock;

	struct rockchip_clk_provider *ctx;

#ifdef CONFIG_ROCKCHIP_CLK_BOOST
	bool			boost_enabled;
	u32			boost_backup_pll_usage;
	unsigned long		boost_backup_pll_rate;
	unsigned long		boost_low_rate;
	unsigned long		boost_high_rate;
	struct regmap		*boost;
#endif
#ifdef CONFIG_DEBUG_FS
	struct hlist_node	debug_node;
#endif
};

#define to_rockchip_clk_pll(_hw) container_of(_hw, struct rockchip_clk_pll, hw)
#define to_rockchip_clk_pll_nb(nb) \
			container_of(nb, struct rockchip_clk_pll, clk_nb)

#ifdef CONFIG_ROCKCHIP_CLK_BOOST
static void rockchip_boost_disable_low(struct rockchip_clk_pll *pll);
#ifdef CONFIG_DEBUG_FS
static HLIST_HEAD(clk_boost_list);
static DEFINE_MUTEX(clk_boost_lock);
#endif
#else
static inline void rockchip_boost_disable_low(struct rockchip_clk_pll *pll) {}
#endif

#define MHZ			(1000UL * 1000UL)
#define KHZ			(1000UL)

/* CLK_PLL_TYPE_RK3066_AUTO type ops */
#define PLL_FREF_MIN		(269 * KHZ)
#define PLL_FREF_MAX		(2200 * MHZ)

#define PLL_FVCO_MIN		(440 * MHZ)
#define PLL_FVCO_MAX		(2200 * MHZ)

#define PLL_FOUT_MIN		(27500 * KHZ)
#define PLL_FOUT_MAX		(2200 * MHZ)

#define PLL_NF_MAX		(4096)
#define PLL_NR_MAX		(64)
#define PLL_NO_MAX		(16)

/* CLK_PLL_TYPE_RK3036/3366/3399_AUTO type ops */
#define MIN_FOUTVCO_FREQ	(800 * MHZ)
#define MAX_FOUTVCO_FREQ	(2000 * MHZ)

static struct rockchip_pll_rate_table auto_table;

int rockchip_pll_clk_adaptive_scaling(struct clk *clk, int sel)
{
	struct clk *parent = clk_get_parent(clk);
	struct rockchip_clk_pll *pll;

	if (IS_ERR_OR_NULL(parent))
		return -EINVAL;

	pll = to_rockchip_clk_pll(__clk_get_hw(parent));
	if (!pll)
		return -EINVAL;

	pll->sel = sel;

	return 0;
}
EXPORT_SYMBOL(rockchip_pll_clk_adaptive_scaling);

int rockchip_pll_clk_rate_to_scale(struct clk *clk, unsigned long rate)
{
	const struct rockchip_pll_rate_table *rate_table;
	struct clk *parent = clk_get_parent(clk);
	struct rockchip_clk_pll *pll;
	unsigned int i;

	if (IS_ERR_OR_NULL(parent))
		return -EINVAL;

	pll = to_rockchip_clk_pll(__clk_get_hw(parent));
	if (!pll)
		return -EINVAL;

	rate_table = pll->rate_table;
	for (i = 0; i < pll->rate_count; i++) {
		if (rate >= rate_table[i].rate)
			return i;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(rockchip_pll_clk_rate_to_scale);

int rockchip_pll_clk_scale_to_rate(struct clk *clk, unsigned int scale)
{
	const struct rockchip_pll_rate_table *rate_table;
	struct clk *parent = clk_get_parent(clk);
	struct rockchip_clk_pll *pll;
	unsigned int i;

	if (IS_ERR_OR_NULL(parent))
		return -EINVAL;

	pll = to_rockchip_clk_pll(__clk_get_hw(parent));
	if (!pll)
		return -EINVAL;

	rate_table = pll->rate_table;
	for (i = 0; i < pll->rate_count; i++) {
		if (i == scale)
			return rate_table[i].rate;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(rockchip_pll_clk_scale_to_rate);

static struct rockchip_pll_rate_table *rk_pll_rate_table_get(void)
{
	return &auto_table;
}

static int rockchip_pll_clk_set_postdiv(unsigned long fout_hz,
					u32 *postdiv1,
					u32 *postdiv2,
					u32 *foutvco)
{
	unsigned long freq;

	if (fout_hz < MIN_FOUTVCO_FREQ) {
		for (*postdiv1 = 1; *postdiv1 <= 7; (*postdiv1)++) {
			for (*postdiv2 = 1; *postdiv2 <= 7; (*postdiv2)++) {
				freq = fout_hz * (*postdiv1) * (*postdiv2);
				if (freq >= MIN_FOUTVCO_FREQ &&
				    freq <= MAX_FOUTVCO_FREQ) {
					*foutvco = freq;
					return 0;
				}
			}
		}
		pr_err("CANNOT FIND postdiv1/2 to make fout in range from 800M to 2000M,fout = %lu\n",
		       fout_hz);
	} else {
		*postdiv1 = 1;
		*postdiv2 = 1;
	}
	return 0;
}

static struct rockchip_pll_rate_table *
rockchip_pll_clk_set_by_auto(struct rockchip_clk_pll *pll,
			     unsigned long fin_hz,
			     unsigned long fout_hz)
{
	struct rockchip_pll_rate_table *rate_table = rk_pll_rate_table_get();
	/* FIXME set postdiv1/2 always 1*/
	u32 foutvco = fout_hz;
	u64 fin_64, frac_64;
	u32 f_frac, postdiv1, postdiv2;
	unsigned long clk_gcd = 0;

	if (fin_hz == 0 || fout_hz == 0 || fout_hz == fin_hz)
		return NULL;

	rockchip_pll_clk_set_postdiv(fout_hz, &postdiv1, &postdiv2, &foutvco);
	rate_table->postdiv1 = postdiv1;
	rate_table->postdiv2 = postdiv2;
	rate_table->dsmpd = 1;

	if (fin_hz / MHZ * MHZ == fin_hz && fout_hz / MHZ * MHZ == fout_hz) {
		fin_hz /= MHZ;
		foutvco /= MHZ;
		clk_gcd = gcd(fin_hz, foutvco);
		rate_table->refdiv = fin_hz / clk_gcd;
		rate_table->fbdiv = foutvco / clk_gcd;

		rate_table->frac = 0;

		pr_debug("fin = %lu, fout = %lu, clk_gcd = %lu, refdiv = %u, fbdiv = %u, postdiv1 = %u, postdiv2 = %u, frac = %u\n",
			 fin_hz, fout_hz, clk_gcd, rate_table->refdiv,
			 rate_table->fbdiv, rate_table->postdiv1,
			 rate_table->postdiv2, rate_table->frac);
	} else {
		pr_debug("frac div running, fin_hz = %lu, fout_hz = %lu, fin_INT_mhz = %lu, fout_INT_mhz = %lu\n",
			 fin_hz, fout_hz,
			 fin_hz / MHZ * MHZ,
			 fout_hz / MHZ * MHZ);
		pr_debug("frac get postdiv1 = %u,  postdiv2 = %u, foutvco = %u\n",
			 rate_table->postdiv1, rate_table->postdiv2, foutvco);
		clk_gcd = gcd(fin_hz / MHZ, foutvco / MHZ);
		rate_table->refdiv = fin_hz / MHZ / clk_gcd;
		rate_table->fbdiv = foutvco / MHZ / clk_gcd;
		pr_debug("frac get refdiv = %u,  fbdiv = %u\n",
			 rate_table->refdiv, rate_table->fbdiv);

		rate_table->frac = 0;

		f_frac = (foutvco % MHZ);
		fin_64 = fin_hz;
		do_div(fin_64, (u64)rate_table->refdiv);
		frac_64 = (u64)f_frac << 24;
		do_div(frac_64, fin_64);
		rate_table->frac = (u32)frac_64;
		if (rate_table->frac > 0)
			rate_table->dsmpd = 0;
		pr_debug("frac = %x\n", rate_table->frac);
	}
	return rate_table;
}

static struct rockchip_pll_rate_table *
rockchip_rk3066_pll_clk_set_by_auto(struct rockchip_clk_pll *pll,
				    unsigned long fin_hz,
				    unsigned long fout_hz)
{
	struct rockchip_pll_rate_table *rate_table = rk_pll_rate_table_get();
	u32 nr, nf, no, nonr;
	u32 nr_out, nf_out, no_out;
	u32 n;
	u32 numerator, denominator;
	u64 fref, fvco, fout;
	unsigned long clk_gcd = 0;

	nr_out = PLL_NR_MAX + 1;
	no_out = 0;
	nf_out = 0;

	if (fin_hz == 0 || fout_hz == 0 || fout_hz == fin_hz)
		return NULL;

	clk_gcd = gcd(fin_hz, fout_hz);

	numerator = fout_hz / clk_gcd;
	denominator = fin_hz / clk_gcd;

	for (n = 1;; n++) {
		nf = numerator * n;
		nonr = denominator * n;
		if (nf > PLL_NF_MAX || nonr > (PLL_NO_MAX * PLL_NR_MAX))
			break;

		for (no = 1; no <= PLL_NO_MAX; no++) {
			if (!(no == 1 || !(no % 2)))
				continue;

			if (nonr % no)
				continue;
			nr = nonr / no;

			if (nr > PLL_NR_MAX)
				continue;

			fref = fin_hz / nr;
			if (fref < PLL_FREF_MIN || fref > PLL_FREF_MAX)
				continue;

			fvco = fref * nf;
			if (fvco < PLL_FVCO_MIN || fvco > PLL_FVCO_MAX)
				continue;

			fout = fvco / no;
			if (fout < PLL_FOUT_MIN || fout > PLL_FOUT_MAX)
				continue;

			/* select the best from all available PLL settings */
			if ((no > no_out) ||
			    ((no == no_out) && (nr < nr_out))) {
				nr_out = nr;
				nf_out = nf;
				no_out = no;
			}
		}
	}

	/* output the best PLL setting */
	if ((nr_out <= PLL_NR_MAX) && (no_out > 0)) {
		rate_table->nr = nr_out;
		rate_table->nf = nf_out;
		rate_table->no = no_out;
	} else {
		return NULL;
	}

	return rate_table;
}

static struct rockchip_pll_rate_table *
rockchip_rk3588_pll_clk_set_by_auto(struct rockchip_clk_pll *pll,
				    unsigned long fin_hz,
				    unsigned long fout_hz)
{
	struct rockchip_pll_rate_table *rate_table = rk_pll_rate_table_get();
	u64 fvco_min = 2250 * MHZ, fvco_max = 4500 * MHZ;
	u64 fout_min = 37 * MHZ, fout_max = 4500 * MHZ;
	u32 p, m, s;
	u64 fvco, fref, fout, ffrac;

	if (fin_hz == 0 || fout_hz == 0 || fout_hz == fin_hz)
		return NULL;

	if (fout_hz > fout_max || fout_hz < fout_min)
		return NULL;

	if (fin_hz / MHZ * MHZ == fin_hz && fout_hz / MHZ * MHZ == fout_hz) {
		for (s = 0; s <= 6; s++) {
			fvco = (u64)fout_hz << s;
			if (fvco < fvco_min || fvco > fvco_max)
				continue;
			for (p = 2; p <= 4; p++) {
				for (m = 64; m <= 1023; m++) {
					if (fvco == m * fin_hz / p) {
						rate_table->p = p;
						rate_table->m = m;
						rate_table->s = s;
						rate_table->k = 0;
						return rate_table;
					}
				}
			}
		}
		pr_err("CANNOT FIND Fout by auto,fout = %lu\n", fout_hz);
	} else {
		for (s = 0; s <= 6; s++) {
			fvco = (u64)fout_hz << s;
			if (fvco < fvco_min || fvco > fvco_max)
				continue;
			for (p = 1; p <= 4; p++) {
				for (m = 64; m <= 1023; m++) {
					if ((fvco >= m * fin_hz / p) && (fvco < (m + 1) * fin_hz / p)) {
						rate_table->p = p;
						rate_table->m = m;
						rate_table->s = s;
						fref = fin_hz / p;
						ffrac = fvco - (m * fref);
						fout = ffrac * 65536;
						rate_table->k = fout / fref;
						return rate_table;
					}
				}
			}
		}
		pr_err("CANNOT FIND Fout by auto,fout = %lu\n", fout_hz);
	}
	return NULL;
}

static const struct rockchip_pll_rate_table *rockchip_get_pll_settings(
			    struct rockchip_clk_pll *pll, unsigned long rate)
{
	const struct rockchip_pll_rate_table  *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate == rate_table[i].rate) {
			if (i < pll->sel) {
				pll->scaling = rate;
				return &rate_table[pll->sel];
			}
			pll->scaling = 0;
			return &rate_table[i];
		}
	}
	pll->scaling = 0;

	if (pll->type == pll_rk3066)
		return rockchip_rk3066_pll_clk_set_by_auto(pll, 24 * MHZ, rate);
	else if (pll->type == pll_rk3588 || pll->type == pll_rk3588_core)
		return rockchip_rk3588_pll_clk_set_by_auto(pll, 24 * MHZ, rate);
	else
		return rockchip_pll_clk_set_by_auto(pll, 24 * MHZ, rate);
}

static long rockchip_pll_round_rate(struct clk_hw *hw,
			    unsigned long drate, unsigned long *prate)
{
	return drate;
}

/*
 * Wait for the pll to reach the locked state.
 * The calling set_rate function is responsible for making sure the
 * grf regmap is available.
 */
static int rockchip_pll_wait_lock(struct rockchip_clk_pll *pll)
{
	struct regmap *grf = pll->ctx->grf;
	unsigned int val;
	int ret;

	ret = regmap_read_poll_timeout(grf, pll->lock_offset, val,
				       val & BIT(pll->lock_shift), 0, 1000);
	if (ret)
		pr_err("%s: timeout waiting for pll to lock\n", __func__);

	return ret;
}

/**
 * PLL used in RK3036
 */

#define RK3036_PLLCON(i)			(i * 0x4)
#define RK3036_PLLCON0_FBDIV_MASK		0xfff
#define RK3036_PLLCON0_FBDIV_SHIFT		0
#define RK3036_PLLCON0_POSTDIV1_MASK		0x7
#define RK3036_PLLCON0_POSTDIV1_SHIFT		12
#define RK3036_PLLCON1_REFDIV_MASK		0x3f
#define RK3036_PLLCON1_REFDIV_SHIFT		0
#define RK3036_PLLCON1_POSTDIV2_MASK		0x7
#define RK3036_PLLCON1_POSTDIV2_SHIFT		6
#define RK3036_PLLCON1_LOCK_STATUS		BIT(10)
#define RK3036_PLLCON1_DSMPD_MASK		0x1
#define RK3036_PLLCON1_DSMPD_SHIFT		12
#define RK3036_PLLCON1_PWRDOWN			BIT(13)
#define RK3036_PLLCON1_PLLPDSEL			BIT(15)
#define RK3036_PLLCON2_FRAC_MASK		0xffffff
#define RK3036_PLLCON2_FRAC_SHIFT		0

static int rockchip_rk3036_pll_wait_lock(struct rockchip_clk_pll *pll)
{
	u32 pllcon;
	int ret;

	/*
	 * Lock time typical 250, max 500 input clock cycles @24MHz
	 * So define a very safe maximum of 1000us, meaning 24000 cycles.
	 */
	ret = readl_relaxed_poll_timeout(pll->reg_base + RK3036_PLLCON(1),
					 pllcon,
					 pllcon & RK3036_PLLCON1_LOCK_STATUS,
					 0, 1000);
	if (ret)
		pr_err("%s: timeout waiting for pll to lock\n", __func__);

	return ret;
}

static unsigned long __maybe_unused
rockchip_rk3036_pll_con_to_rate(struct rockchip_clk_pll *pll,
				u32 con0, u32 con1)
{
	unsigned int fbdiv, postdiv1, refdiv, postdiv2;
	u64 rate64 = 24000000;

	fbdiv = ((con0 >> RK3036_PLLCON0_FBDIV_SHIFT) &
		  RK3036_PLLCON0_FBDIV_MASK);
	postdiv1 = ((con0 >> RK3036_PLLCON0_POSTDIV1_SHIFT) &
		     RK3036_PLLCON0_POSTDIV1_MASK);
	refdiv = ((con1 >> RK3036_PLLCON1_REFDIV_SHIFT) &
		   RK3036_PLLCON1_REFDIV_MASK);
	postdiv2 = ((con1 >> RK3036_PLLCON1_POSTDIV2_SHIFT) &
		     RK3036_PLLCON1_POSTDIV2_MASK);

	rate64 *= fbdiv;
	do_div(rate64, refdiv);
	do_div(rate64, postdiv1);
	do_div(rate64, postdiv2);

	return (unsigned long)rate64;
}

static void rockchip_rk3036_pll_get_params(struct rockchip_clk_pll *pll,
					struct rockchip_pll_rate_table *rate)
{
	u32 pllcon;

	pllcon = readl_relaxed(pll->reg_base + RK3036_PLLCON(0));
	rate->fbdiv = ((pllcon >> RK3036_PLLCON0_FBDIV_SHIFT)
				& RK3036_PLLCON0_FBDIV_MASK);
	rate->postdiv1 = ((pllcon >> RK3036_PLLCON0_POSTDIV1_SHIFT)
				& RK3036_PLLCON0_POSTDIV1_MASK);

	pllcon = readl_relaxed(pll->reg_base + RK3036_PLLCON(1));
	rate->refdiv = ((pllcon >> RK3036_PLLCON1_REFDIV_SHIFT)
				& RK3036_PLLCON1_REFDIV_MASK);
	rate->postdiv2 = ((pllcon >> RK3036_PLLCON1_POSTDIV2_SHIFT)
				& RK3036_PLLCON1_POSTDIV2_MASK);
	rate->dsmpd = ((pllcon >> RK3036_PLLCON1_DSMPD_SHIFT)
				& RK3036_PLLCON1_DSMPD_MASK);

	pllcon = readl_relaxed(pll->reg_base + RK3036_PLLCON(2));
	rate->frac = ((pllcon >> RK3036_PLLCON2_FRAC_SHIFT)
				& RK3036_PLLCON2_FRAC_MASK);
}

static unsigned long rockchip_rk3036_pll_recalc_rate(struct clk_hw *hw,
						     unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	struct rockchip_pll_rate_table cur;
	u64 rate64 = prate, frac_rate64 = prate;

	if (pll->sel && pll->scaling)
		return pll->scaling;

	rockchip_rk3036_pll_get_params(pll, &cur);

	rate64 *= cur.fbdiv;
	do_div(rate64, cur.refdiv);

	if (cur.dsmpd == 0) {
		/* fractional mode */
		frac_rate64 *= cur.frac;

		do_div(frac_rate64, cur.refdiv);
		rate64 += frac_rate64 >> 24;
	}

	do_div(rate64, cur.postdiv1);
	do_div(rate64, cur.postdiv2);

	return (unsigned long)rate64;
}

static int rockchip_rk3036_pll_set_params(struct rockchip_clk_pll *pll,
				const struct rockchip_pll_rate_table *rate)
{
	const struct clk_ops *pll_mux_ops = pll->pll_mux_ops;
	struct clk_mux *pll_mux = &pll->pll_mux;
	struct rockchip_pll_rate_table cur;
	u32 pllcon;
	int rate_change_remuxed = 0;
	int cur_parent;
	int ret;

	pr_debug("%s: rate settings for %lu fbdiv: %d, postdiv1: %d, refdiv: %d, postdiv2: %d, dsmpd: %d, frac: %d\n",
		__func__, rate->rate, rate->fbdiv, rate->postdiv1, rate->refdiv,
		rate->postdiv2, rate->dsmpd, rate->frac);

	rockchip_rk3036_pll_get_params(pll, &cur);
	cur.rate = 0;

	cur_parent = pll_mux_ops->get_parent(&pll_mux->hw);
	if (cur_parent == PLL_MODE_NORM) {
		pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_SLOW);
		rate_change_remuxed = 1;
	}

	/* update pll values */
	writel_relaxed(HIWORD_UPDATE(rate->fbdiv, RK3036_PLLCON0_FBDIV_MASK,
					  RK3036_PLLCON0_FBDIV_SHIFT) |
		       HIWORD_UPDATE(rate->postdiv1, RK3036_PLLCON0_POSTDIV1_MASK,
					     RK3036_PLLCON0_POSTDIV1_SHIFT),
		       pll->reg_base + RK3036_PLLCON(0));

	writel_relaxed(HIWORD_UPDATE(rate->refdiv, RK3036_PLLCON1_REFDIV_MASK,
						   RK3036_PLLCON1_REFDIV_SHIFT) |
		       HIWORD_UPDATE(rate->postdiv2, RK3036_PLLCON1_POSTDIV2_MASK,
						     RK3036_PLLCON1_POSTDIV2_SHIFT) |
		       HIWORD_UPDATE(rate->dsmpd, RK3036_PLLCON1_DSMPD_MASK,
						  RK3036_PLLCON1_DSMPD_SHIFT),
		       pll->reg_base + RK3036_PLLCON(1));

	/* GPLL CON2 is not HIWORD_MASK */
	pllcon = readl_relaxed(pll->reg_base + RK3036_PLLCON(2));
	pllcon &= ~(RK3036_PLLCON2_FRAC_MASK << RK3036_PLLCON2_FRAC_SHIFT);
	pllcon |= rate->frac << RK3036_PLLCON2_FRAC_SHIFT;
	writel_relaxed(pllcon, pll->reg_base + RK3036_PLLCON(2));

	if (IS_ENABLED(CONFIG_ROCKCHIP_CLK_BOOST))
		rockchip_boost_disable_low(pll);

	/* wait for the pll to lock */
	ret = rockchip_rk3036_pll_wait_lock(pll);
	if (ret) {
		pr_warn("%s: pll update unsuccessful, trying to restore old params\n",
			__func__);
		rockchip_rk3036_pll_set_params(pll, &cur);
	}

	if (rate_change_remuxed)
		pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_NORM);

	return ret;
}

static int rockchip_rk3036_pll_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;

	pr_debug("%s: changing %s to %lu with a parent rate of %lu\n",
		 __func__, __clk_get_name(hw->clk), drate, prate);

	/* Get required rate settings from table */
	rate = rockchip_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	return rockchip_rk3036_pll_set_params(pll, rate);
}

static int rockchip_rk3036_pll_enable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);

	writel(HIWORD_UPDATE(0, RK3036_PLLCON1_PWRDOWN, 0),
	       pll->reg_base + RK3036_PLLCON(1));
	rockchip_rk3036_pll_wait_lock(pll);

	return 0;
}

static void rockchip_rk3036_pll_disable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);

	writel(HIWORD_UPDATE(RK3036_PLLCON1_PWRDOWN,
			     RK3036_PLLCON1_PWRDOWN, 0),
	       pll->reg_base + RK3036_PLLCON(1));
}

static int rockchip_rk3036_pll_is_enabled(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	u32 pllcon = readl(pll->reg_base + RK3036_PLLCON(1));

	return !(pllcon & RK3036_PLLCON1_PWRDOWN);
}

static int rockchip_rk3036_pll_init(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;
	struct rockchip_pll_rate_table cur;
	unsigned long drate;

	if (!(pll->flags & ROCKCHIP_PLL_SYNC_RATE))
		return 0;

	drate = clk_hw_get_rate(hw);
	rate = rockchip_get_pll_settings(pll, drate);

	/* when no rate setting for the current rate, rely on clk_set_rate */
	if (!rate)
		return 0;

	rockchip_rk3036_pll_get_params(pll, &cur);

	pr_debug("%s: pll %s@%lu: Hz\n", __func__, __clk_get_name(hw->clk),
		 drate);
	pr_debug("old - fbdiv: %d, postdiv1: %d, refdiv: %d, postdiv2: %d, dsmpd: %d, frac: %d\n",
		 cur.fbdiv, cur.postdiv1, cur.refdiv, cur.postdiv2,
		 cur.dsmpd, cur.frac);
	pr_debug("new - fbdiv: %d, postdiv1: %d, refdiv: %d, postdiv2: %d, dsmpd: %d, frac: %d\n",
		 rate->fbdiv, rate->postdiv1, rate->refdiv, rate->postdiv2,
		 rate->dsmpd, rate->frac);

	if (rate->fbdiv != cur.fbdiv || rate->postdiv1 != cur.postdiv1 ||
		rate->refdiv != cur.refdiv || rate->postdiv2 != cur.postdiv2 ||
		rate->dsmpd != cur.dsmpd ||
		(!cur.dsmpd && (rate->frac != cur.frac))) {
		struct clk *parent = clk_get_parent(hw->clk);

		if (!parent) {
			pr_warn("%s: parent of %s not available\n",
				__func__, __clk_get_name(hw->clk));
			return 0;
		}

		pr_debug("%s: pll %s: rate params do not match rate table, adjusting\n",
			 __func__, __clk_get_name(hw->clk));
		rockchip_rk3036_pll_set_params(pll, rate);
	}

	return 0;
}

static const struct clk_ops rockchip_rk3036_pll_clk_norate_ops = {
	.recalc_rate = rockchip_rk3036_pll_recalc_rate,
	.enable = rockchip_rk3036_pll_enable,
	.disable = rockchip_rk3036_pll_disable,
	.is_enabled = rockchip_rk3036_pll_is_enabled,
};

static const struct clk_ops rockchip_rk3036_pll_clk_ops = {
	.recalc_rate = rockchip_rk3036_pll_recalc_rate,
	.round_rate = rockchip_pll_round_rate,
	.set_rate = rockchip_rk3036_pll_set_rate,
	.enable = rockchip_rk3036_pll_enable,
	.disable = rockchip_rk3036_pll_disable,
	.is_enabled = rockchip_rk3036_pll_is_enabled,
	.init = rockchip_rk3036_pll_init,
};

/**
 * PLL used in RK3066, RK3188 and RK3288
 */

#define RK3066_PLL_RESET_DELAY(nr)	((nr * 500) / 24 + 1)

#define RK3066_PLLCON(i)		(i * 0x4)
#define RK3066_PLLCON0_OD_MASK		0xf
#define RK3066_PLLCON0_OD_SHIFT		0
#define RK3066_PLLCON0_NR_MASK		0x3f
#define RK3066_PLLCON0_NR_SHIFT		8
#define RK3066_PLLCON1_NF_MASK		0x1fff
#define RK3066_PLLCON1_NF_SHIFT		0
#define RK3066_PLLCON2_NB_MASK		0xfff
#define RK3066_PLLCON2_NB_SHIFT		0
#define RK3066_PLLCON3_RESET		(1 << 5)
#define RK3066_PLLCON3_PWRDOWN		(1 << 1)
#define RK3066_PLLCON3_BYPASS		(1 << 0)

static void rockchip_rk3066_pll_get_params(struct rockchip_clk_pll *pll,
					struct rockchip_pll_rate_table *rate)
{
	u32 pllcon;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(0));
	rate->nr = ((pllcon >> RK3066_PLLCON0_NR_SHIFT)
				& RK3066_PLLCON0_NR_MASK) + 1;
	rate->no = ((pllcon >> RK3066_PLLCON0_OD_SHIFT)
				& RK3066_PLLCON0_OD_MASK) + 1;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(1));
	rate->nf = ((pllcon >> RK3066_PLLCON1_NF_SHIFT)
				& RK3066_PLLCON1_NF_MASK) + 1;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(2));
	rate->nb = ((pllcon >> RK3066_PLLCON2_NB_SHIFT)
				& RK3066_PLLCON2_NB_MASK) + 1;
}

static unsigned long rockchip_rk3066_pll_recalc_rate(struct clk_hw *hw,
						     unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	struct rockchip_pll_rate_table cur;
	u64 rate64 = prate;
	u32 pllcon;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(3));
	if (pllcon & RK3066_PLLCON3_BYPASS) {
		pr_debug("%s: pll %s is bypassed\n", __func__,
			clk_hw_get_name(hw));
		return prate;
	}

	if (pll->sel && pll->scaling)
		return pll->scaling;

	rockchip_rk3066_pll_get_params(pll, &cur);

	rate64 *= cur.nf;
	do_div(rate64, cur.nr);
	do_div(rate64, cur.no);

	return (unsigned long)rate64;
}

static int rockchip_rk3066_pll_set_params(struct rockchip_clk_pll *pll,
				const struct rockchip_pll_rate_table *rate)
{
	const struct clk_ops *pll_mux_ops = pll->pll_mux_ops;
	struct clk_mux *pll_mux = &pll->pll_mux;
	struct rockchip_pll_rate_table cur;
	int rate_change_remuxed = 0;
	int cur_parent;
	int ret;

	pr_debug("%s: rate settings for %lu (nr, no, nf): (%d, %d, %d)\n",
		 __func__, rate->rate, rate->nr, rate->no, rate->nf);

	rockchip_rk3066_pll_get_params(pll, &cur);
	cur.rate = 0;

	cur_parent = pll_mux_ops->get_parent(&pll_mux->hw);
	if (cur_parent == PLL_MODE_NORM) {
		pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_SLOW);
		rate_change_remuxed = 1;
	}

	/* enter reset mode */
	writel(HIWORD_UPDATE(RK3066_PLLCON3_RESET, RK3066_PLLCON3_RESET, 0),
	       pll->reg_base + RK3066_PLLCON(3));

	/* update pll values */
	writel(HIWORD_UPDATE(rate->nr - 1, RK3066_PLLCON0_NR_MASK,
					   RK3066_PLLCON0_NR_SHIFT) |
	       HIWORD_UPDATE(rate->no - 1, RK3066_PLLCON0_OD_MASK,
					   RK3066_PLLCON0_OD_SHIFT),
	       pll->reg_base + RK3066_PLLCON(0));

	writel_relaxed(HIWORD_UPDATE(rate->nf - 1, RK3066_PLLCON1_NF_MASK,
						   RK3066_PLLCON1_NF_SHIFT),
		       pll->reg_base + RK3066_PLLCON(1));
	writel_relaxed(HIWORD_UPDATE(rate->nb - 1, RK3066_PLLCON2_NB_MASK,
						   RK3066_PLLCON2_NB_SHIFT),
		       pll->reg_base + RK3066_PLLCON(2));

	/* leave reset and wait the reset_delay */
	writel(HIWORD_UPDATE(0, RK3066_PLLCON3_RESET, 0),
	       pll->reg_base + RK3066_PLLCON(3));
	udelay(RK3066_PLL_RESET_DELAY(rate->nr));

	/* wait for the pll to lock */
	ret = rockchip_pll_wait_lock(pll);
	if (ret) {
		pr_warn("%s: pll update unsuccessful, trying to restore old params\n",
			__func__);
		rockchip_rk3066_pll_set_params(pll, &cur);
	}

	if (rate_change_remuxed)
		pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_NORM);

	return ret;
}

static int rockchip_rk3066_pll_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;
	unsigned long old_rate = rockchip_rk3066_pll_recalc_rate(hw, prate);
	struct regmap *grf = pll->ctx->grf;
	int ret;

	if (IS_ERR(grf)) {
		pr_debug("%s: grf regmap not available, aborting rate change\n",
			 __func__);
		return PTR_ERR(grf);
	}

	pr_debug("%s: changing %s from %lu to %lu with a parent rate of %lu\n",
		 __func__, clk_hw_get_name(hw), old_rate, drate, prate);

	/* Get required rate settings from table */
	rate = rockchip_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, clk_hw_get_name(hw));
		return -EINVAL;
	}

	ret = rockchip_rk3066_pll_set_params(pll, rate);
	if (ret)
		pll->scaling = 0;

	return ret;
}

static int rockchip_rk3066_pll_enable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);

	writel(HIWORD_UPDATE(0, RK3066_PLLCON3_PWRDOWN, 0),
	       pll->reg_base + RK3066_PLLCON(3));
	rockchip_pll_wait_lock(pll);

	return 0;
}

static void rockchip_rk3066_pll_disable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);

	writel(HIWORD_UPDATE(RK3066_PLLCON3_PWRDOWN,
			     RK3066_PLLCON3_PWRDOWN, 0),
	       pll->reg_base + RK3066_PLLCON(3));
}

static int rockchip_rk3066_pll_is_enabled(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	u32 pllcon = readl(pll->reg_base + RK3066_PLLCON(3));

	return !(pllcon & RK3066_PLLCON3_PWRDOWN);
}

static int rockchip_rk3066_pll_init(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;
	struct rockchip_pll_rate_table cur;
	unsigned long drate;

	if (!(pll->flags & ROCKCHIP_PLL_SYNC_RATE))
		return 0;

	drate = clk_hw_get_rate(hw);
	rate = rockchip_get_pll_settings(pll, drate);

	/* when no rate setting for the current rate, rely on clk_set_rate */
	if (!rate)
		return 0;

	rockchip_rk3066_pll_get_params(pll, &cur);

	pr_debug("%s: pll %s@%lu: nr (%d:%d); no (%d:%d); nf(%d:%d), nb(%d:%d)\n",
		 __func__, clk_hw_get_name(hw), drate, rate->nr, cur.nr,
		 rate->no, cur.no, rate->nf, cur.nf, rate->nb, cur.nb);
	if (rate->nr != cur.nr || rate->no != cur.no || rate->nf != cur.nf
						     || rate->nb != cur.nb) {
		pr_debug("%s: pll %s: rate params do not match rate table, adjusting\n",
			 __func__, clk_hw_get_name(hw));
		rockchip_rk3066_pll_set_params(pll, rate);
	}

	return 0;
}

static const struct clk_ops rockchip_rk3066_pll_clk_norate_ops = {
	.recalc_rate = rockchip_rk3066_pll_recalc_rate,
	.enable = rockchip_rk3066_pll_enable,
	.disable = rockchip_rk3066_pll_disable,
	.is_enabled = rockchip_rk3066_pll_is_enabled,
};

static const struct clk_ops rockchip_rk3066_pll_clk_ops = {
	.recalc_rate = rockchip_rk3066_pll_recalc_rate,
	.round_rate = rockchip_pll_round_rate,
	.set_rate = rockchip_rk3066_pll_set_rate,
	.enable = rockchip_rk3066_pll_enable,
	.disable = rockchip_rk3066_pll_disable,
	.is_enabled = rockchip_rk3066_pll_is_enabled,
	.init = rockchip_rk3066_pll_init,
};

/**
 * PLL used in RK3399
 */

#define RK3399_PLLCON(i)			(i * 0x4)
#define RK3399_PLLCON0_FBDIV_MASK		0xfff
#define RK3399_PLLCON0_FBDIV_SHIFT		0
#define RK3399_PLLCON1_REFDIV_MASK		0x3f
#define RK3399_PLLCON1_REFDIV_SHIFT		0
#define RK3399_PLLCON1_POSTDIV1_MASK		0x7
#define RK3399_PLLCON1_POSTDIV1_SHIFT		8
#define RK3399_PLLCON1_POSTDIV2_MASK		0x7
#define RK3399_PLLCON1_POSTDIV2_SHIFT		12
#define RK3399_PLLCON2_FRAC_MASK		0xffffff
#define RK3399_PLLCON2_FRAC_SHIFT		0
#define RK3399_PLLCON2_LOCK_STATUS		BIT(31)
#define RK3399_PLLCON3_PWRDOWN			BIT(0)
#define RK3399_PLLCON3_DSMPD_MASK		0x1
#define RK3399_PLLCON3_DSMPD_SHIFT		3

static int rockchip_rk3399_pll_wait_lock(struct rockchip_clk_pll *pll)
{
	u32 pllcon;
	int ret;

	/*
	 * Lock time typical 250, max 500 input clock cycles @24MHz
	 * So define a very safe maximum of 1000us, meaning 24000 cycles.
	 */
	ret = readl_relaxed_poll_timeout(pll->reg_base + RK3399_PLLCON(2),
					 pllcon,
					 pllcon & RK3399_PLLCON2_LOCK_STATUS,
					 0, 1000);
	if (ret)
		pr_err("%s: timeout waiting for pll to lock\n", __func__);

	return ret;
}

static void rockchip_rk3399_pll_get_params(struct rockchip_clk_pll *pll,
					struct rockchip_pll_rate_table *rate)
{
	u32 pllcon;

	pllcon = readl_relaxed(pll->reg_base + RK3399_PLLCON(0));
	rate->fbdiv = ((pllcon >> RK3399_PLLCON0_FBDIV_SHIFT)
				& RK3399_PLLCON0_FBDIV_MASK);

	pllcon = readl_relaxed(pll->reg_base + RK3399_PLLCON(1));
	rate->refdiv = ((pllcon >> RK3399_PLLCON1_REFDIV_SHIFT)
				& RK3399_PLLCON1_REFDIV_MASK);
	rate->postdiv1 = ((pllcon >> RK3399_PLLCON1_POSTDIV1_SHIFT)
				& RK3399_PLLCON1_POSTDIV1_MASK);
	rate->postdiv2 = ((pllcon >> RK3399_PLLCON1_POSTDIV2_SHIFT)
				& RK3399_PLLCON1_POSTDIV2_MASK);

	pllcon = readl_relaxed(pll->reg_base + RK3399_PLLCON(2));
	rate->frac = ((pllcon >> RK3399_PLLCON2_FRAC_SHIFT)
				& RK3399_PLLCON2_FRAC_MASK);

	pllcon = readl_relaxed(pll->reg_base + RK3399_PLLCON(3));
	rate->dsmpd = ((pllcon >> RK3399_PLLCON3_DSMPD_SHIFT)
				& RK3399_PLLCON3_DSMPD_MASK);
}

static unsigned long rockchip_rk3399_pll_recalc_rate(struct clk_hw *hw,
						     unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	struct rockchip_pll_rate_table cur;
	u64 rate64 = prate;

	if (pll->sel && pll->scaling)
		return pll->scaling;

	rockchip_rk3399_pll_get_params(pll, &cur);

	rate64 *= cur.fbdiv;
	do_div(rate64, cur.refdiv);

	if (cur.dsmpd == 0) {
		/* fractional mode */
		u64 frac_rate64 = prate * cur.frac;

		do_div(frac_rate64, cur.refdiv);
		rate64 += frac_rate64 >> 24;
	}

	do_div(rate64, cur.postdiv1);
	do_div(rate64, cur.postdiv2);

	return (unsigned long)rate64;
}

static int rockchip_rk3399_pll_set_params(struct rockchip_clk_pll *pll,
				const struct rockchip_pll_rate_table *rate)
{
	const struct clk_ops *pll_mux_ops = pll->pll_mux_ops;
	struct clk_mux *pll_mux = &pll->pll_mux;
	struct rockchip_pll_rate_table cur;
	u32 pllcon;
	int rate_change_remuxed = 0;
	int cur_parent;
	int ret;

	pr_debug("%s: rate settings for %lu fbdiv: %d, postdiv1: %d, refdiv: %d, postdiv2: %d, dsmpd: %d, frac: %d\n",
		__func__, rate->rate, rate->fbdiv, rate->postdiv1, rate->refdiv,
		rate->postdiv2, rate->dsmpd, rate->frac);

	rockchip_rk3399_pll_get_params(pll, &cur);
	cur.rate = 0;

	cur_parent = pll_mux_ops->get_parent(&pll_mux->hw);
	if (cur_parent == PLL_MODE_NORM) {
		pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_SLOW);
		rate_change_remuxed = 1;
	}

	/* set pll power down */
	writel(HIWORD_UPDATE(RK3399_PLLCON3_PWRDOWN,
			     RK3399_PLLCON3_PWRDOWN, 0),
	       pll->reg_base + RK3399_PLLCON(3));

	/* update pll values */
	writel_relaxed(HIWORD_UPDATE(rate->fbdiv, RK3399_PLLCON0_FBDIV_MASK,
						  RK3399_PLLCON0_FBDIV_SHIFT),
		       pll->reg_base + RK3399_PLLCON(0));

	writel_relaxed(HIWORD_UPDATE(rate->refdiv, RK3399_PLLCON1_REFDIV_MASK,
						   RK3399_PLLCON1_REFDIV_SHIFT) |
		       HIWORD_UPDATE(rate->postdiv1, RK3399_PLLCON1_POSTDIV1_MASK,
						     RK3399_PLLCON1_POSTDIV1_SHIFT) |
		       HIWORD_UPDATE(rate->postdiv2, RK3399_PLLCON1_POSTDIV2_MASK,
						     RK3399_PLLCON1_POSTDIV2_SHIFT),
		       pll->reg_base + RK3399_PLLCON(1));

	/* xPLL CON2 is not HIWORD_MASK */
	pllcon = readl_relaxed(pll->reg_base + RK3399_PLLCON(2));
	pllcon &= ~(RK3399_PLLCON2_FRAC_MASK << RK3399_PLLCON2_FRAC_SHIFT);
	pllcon |= rate->frac << RK3399_PLLCON2_FRAC_SHIFT;
	writel_relaxed(pllcon, pll->reg_base + RK3399_PLLCON(2));

	writel_relaxed(HIWORD_UPDATE(rate->dsmpd, RK3399_PLLCON3_DSMPD_MASK,
					    RK3399_PLLCON3_DSMPD_SHIFT),
		       pll->reg_base + RK3399_PLLCON(3));

	/* set pll power up */
	writel(HIWORD_UPDATE(0,
			     RK3399_PLLCON3_PWRDOWN, 0),
	       pll->reg_base + RK3399_PLLCON(3));

	/* wait for the pll to lock */
	ret = rockchip_rk3399_pll_wait_lock(pll);
	if (ret) {
		pr_warn("%s: pll update unsuccessful, trying to restore old params\n",
			__func__);
		rockchip_rk3399_pll_set_params(pll, &cur);
	}

	if (rate_change_remuxed)
		pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_NORM);

	return ret;
}

static int rockchip_rk3399_pll_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;
	unsigned long old_rate = rockchip_rk3399_pll_recalc_rate(hw, prate);
	int ret;

	pr_debug("%s: changing %s from %lu to %lu with a parent rate of %lu\n",
		 __func__, __clk_get_name(hw->clk), old_rate, drate, prate);

	/* Get required rate settings from table */
	rate = rockchip_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	ret = rockchip_rk3399_pll_set_params(pll, rate);
	if (ret)
		pll->scaling = 0;

	return ret;
}

static int rockchip_rk3399_pll_enable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);

	writel(HIWORD_UPDATE(0, RK3399_PLLCON3_PWRDOWN, 0),
	       pll->reg_base + RK3399_PLLCON(3));
	rockchip_rk3399_pll_wait_lock(pll);

	return 0;
}

static void rockchip_rk3399_pll_disable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);

	writel(HIWORD_UPDATE(RK3399_PLLCON3_PWRDOWN,
			     RK3399_PLLCON3_PWRDOWN, 0),
	       pll->reg_base + RK3399_PLLCON(3));
}

static int rockchip_rk3399_pll_is_enabled(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	u32 pllcon = readl(pll->reg_base + RK3399_PLLCON(3));

	return !(pllcon & RK3399_PLLCON3_PWRDOWN);
}

static int rockchip_rk3399_pll_init(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;
	struct rockchip_pll_rate_table cur;
	unsigned long drate;

	if (!(pll->flags & ROCKCHIP_PLL_SYNC_RATE))
		return 0;

	drate = clk_hw_get_rate(hw);
	rate = rockchip_get_pll_settings(pll, drate);

	/* when no rate setting for the current rate, rely on clk_set_rate */
	if (!rate)
		return 0;

	rockchip_rk3399_pll_get_params(pll, &cur);

	pr_debug("%s: pll %s@%lu: Hz\n", __func__, __clk_get_name(hw->clk),
		 drate);
	pr_debug("old - fbdiv: %d, postdiv1: %d, refdiv: %d, postdiv2: %d, dsmpd: %d, frac: %d\n",
		 cur.fbdiv, cur.postdiv1, cur.refdiv, cur.postdiv2,
		 cur.dsmpd, cur.frac);
	pr_debug("new - fbdiv: %d, postdiv1: %d, refdiv: %d, postdiv2: %d, dsmpd: %d, frac: %d\n",
		 rate->fbdiv, rate->postdiv1, rate->refdiv, rate->postdiv2,
		 rate->dsmpd, rate->frac);

	if (rate->fbdiv != cur.fbdiv || rate->postdiv1 != cur.postdiv1 ||
		rate->refdiv != cur.refdiv || rate->postdiv2 != cur.postdiv2 ||
		rate->dsmpd != cur.dsmpd ||
		(!cur.dsmpd && (rate->frac != cur.frac))) {
		struct clk *parent = clk_get_parent(hw->clk);

		if (!parent) {
			pr_warn("%s: parent of %s not available\n",
				__func__, __clk_get_name(hw->clk));
			return 0;
		}

		pr_debug("%s: pll %s: rate params do not match rate table, adjusting\n",
			 __func__, __clk_get_name(hw->clk));
		rockchip_rk3399_pll_set_params(pll, rate);
	}

	return 0;
}

static const struct clk_ops rockchip_rk3399_pll_clk_norate_ops = {
	.recalc_rate = rockchip_rk3399_pll_recalc_rate,
	.enable = rockchip_rk3399_pll_enable,
	.disable = rockchip_rk3399_pll_disable,
	.is_enabled = rockchip_rk3399_pll_is_enabled,
};

static const struct clk_ops rockchip_rk3399_pll_clk_ops = {
	.recalc_rate = rockchip_rk3399_pll_recalc_rate,
	.round_rate = rockchip_pll_round_rate,
	.set_rate = rockchip_rk3399_pll_set_rate,
	.enable = rockchip_rk3399_pll_enable,
	.disable = rockchip_rk3399_pll_disable,
	.is_enabled = rockchip_rk3399_pll_is_enabled,
	.init = rockchip_rk3399_pll_init,
};

/**
 * PLL used in RK3588
 */

#define RK3588_PLLCON(i)		(i * 0x4)
#define RK3588_PLLCON0_M_MASK		0x3ff
#define RK3588_PLLCON0_M_SHIFT		0
#define RK3588_PLLCON1_P_MASK		0x3f
#define RK3588_PLLCON1_P_SHIFT		0
#define RK3588_PLLCON1_S_MASK		0x7
#define RK3588_PLLCON1_S_SHIFT		6
#define RK3588_PLLCON2_K_MASK		0xffff
#define RK3588_PLLCON2_K_SHIFT		0
#define RK3588_PLLCON1_PWRDOWN		BIT(13)
#define RK3588_PLLCON6_LOCK_STATUS	BIT(15)

static int rockchip_rk3588_pll_wait_lock(struct rockchip_clk_pll *pll)
{
	u32 pllcon;
	int ret;

	/*
	 * Lock time typical 250, max 500 input clock cycles @24MHz
	 * So define a very safe maximum of 1000us, meaning 24000 cycles.
	 */
	ret = readl_relaxed_poll_timeout(pll->reg_base + RK3588_PLLCON(6),
					 pllcon,
					 pllcon & RK3588_PLLCON6_LOCK_STATUS,
					 0, 1000);
	if (ret)
		pr_err("%s: timeout waiting for pll to lock\n", __func__);

	return ret;
}

static long rockchip_rk3588_pll_round_rate(struct clk_hw *hw,
			    unsigned long drate, unsigned long *prate)
{
	if ((drate < 37 * MHZ) || (drate > 4500 * MHZ))
		return -EINVAL;
	else
		return drate;
}

static void rockchip_rk3588_pll_get_params(struct rockchip_clk_pll *pll,
					struct rockchip_pll_rate_table *rate)
{
	u32 pllcon;

	pllcon = readl_relaxed(pll->reg_base + RK3588_PLLCON(0));
	rate->m = ((pllcon >> RK3588_PLLCON0_M_SHIFT)
				& RK3588_PLLCON0_M_MASK);

	pllcon = readl_relaxed(pll->reg_base + RK3588_PLLCON(1));
	rate->p = ((pllcon >> RK3588_PLLCON1_P_SHIFT)
				& RK3588_PLLCON1_P_MASK);
	rate->s = ((pllcon >> RK3588_PLLCON1_S_SHIFT)
				& RK3588_PLLCON1_S_MASK);

	pllcon = readl_relaxed(pll->reg_base + RK3588_PLLCON(2));
	rate->k = ((pllcon >> RK3588_PLLCON2_K_SHIFT)
				& RK3588_PLLCON2_K_MASK);
}

static unsigned long rockchip_rk3588_pll_recalc_rate(struct clk_hw *hw,
						     unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	struct rockchip_pll_rate_table cur;
	u64 rate64 = prate, postdiv;

	if (pll->sel && pll->scaling)
		return pll->scaling;

	rockchip_rk3588_pll_get_params(pll, &cur);

	rate64 *= cur.m;
	do_div(rate64, cur.p);

	if (cur.k) {
		/* fractional mode */
		u64 frac_rate64 = prate * cur.k;

		postdiv = cur.p * 65536;
		do_div(frac_rate64, postdiv);
		rate64 += frac_rate64;
	}
	rate64 = rate64 >> cur.s;

	return (unsigned long)rate64;
}

static int rockchip_rk3588_pll_set_params(struct rockchip_clk_pll *pll,
				const struct rockchip_pll_rate_table *rate)
{
	const struct clk_ops *pll_mux_ops = pll->pll_mux_ops;
	struct clk_mux *pll_mux = &pll->pll_mux;
	struct rockchip_pll_rate_table cur;
	int rate_change_remuxed = 0;
	int cur_parent;
	int ret;

	pr_debug("%s: rate settings for %lu p: %d, m: %d, s: %d, k: %d\n",
		__func__, rate->rate, rate->p, rate->m, rate->s, rate->k);

	rockchip_rk3588_pll_get_params(pll, &cur);
	cur.rate = 0;

	if (pll->type == pll_rk3588) {
		cur_parent = pll_mux_ops->get_parent(&pll_mux->hw);
		if (cur_parent == PLL_MODE_NORM) {
			pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_SLOW);
			rate_change_remuxed = 1;
		}
	}

	/* set pll power down */
	writel(HIWORD_UPDATE(RK3588_PLLCON1_PWRDOWN,
			     RK3588_PLLCON1_PWRDOWN, 0),
	       pll->reg_base + RK3588_PLLCON(1));

	/* update pll values */
	writel_relaxed(HIWORD_UPDATE(rate->m, RK3588_PLLCON0_M_MASK,
						  RK3588_PLLCON0_M_SHIFT),
		       pll->reg_base + RK3588_PLLCON(0));

	writel_relaxed(HIWORD_UPDATE(rate->p, RK3588_PLLCON1_P_MASK,
						   RK3588_PLLCON1_P_SHIFT) |
		       HIWORD_UPDATE(rate->s, RK3588_PLLCON1_S_MASK,
						     RK3588_PLLCON1_S_SHIFT),
		       pll->reg_base + RK3588_PLLCON(1));

	writel_relaxed(HIWORD_UPDATE(rate->k, RK3588_PLLCON2_K_MASK,
				     RK3588_PLLCON2_K_SHIFT),
		       pll->reg_base + RK3588_PLLCON(2));

	/* set pll power up */
	writel(HIWORD_UPDATE(0,
			     RK3588_PLLCON1_PWRDOWN, 0),
	       pll->reg_base + RK3588_PLLCON(1));

	/* wait for the pll to lock */
	ret = rockchip_rk3588_pll_wait_lock(pll);
	if (ret) {
		pr_warn("%s: pll update unsuccessful, trying to restore old params\n",
			__func__);
		rockchip_rk3588_pll_set_params(pll, &cur);
	}

	if ((pll->type == pll_rk3588) && rate_change_remuxed)
		pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_NORM);

	return ret;
}

static int rockchip_rk3588_pll_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;
	unsigned long old_rate = rockchip_rk3588_pll_recalc_rate(hw, prate);
	int ret;

	pr_debug("%s: changing %s from %lu to %lu with a parent rate of %lu\n",
		 __func__, __clk_get_name(hw->clk), old_rate, drate, prate);

	/* Get required rate settings from table */
	rate = rockchip_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	ret = rockchip_rk3588_pll_set_params(pll, rate);
	if (ret)
		pll->scaling = 0;

	return ret;
}

static int rockchip_rk3588_pll_enable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct clk_ops *pll_mux_ops = pll->pll_mux_ops;
	struct clk_mux *pll_mux = &pll->pll_mux;

	writel(HIWORD_UPDATE(0, RK3588_PLLCON1_PWRDOWN, 0),
	       pll->reg_base + RK3588_PLLCON(1));
	rockchip_rk3588_pll_wait_lock(pll);

	pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_NORM);

	return 0;
}

static void rockchip_rk3588_pll_disable(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	const struct clk_ops *pll_mux_ops = pll->pll_mux_ops;
	struct clk_mux *pll_mux = &pll->pll_mux;

	pll_mux_ops->set_parent(&pll_mux->hw, PLL_MODE_SLOW);

	writel(HIWORD_UPDATE(RK3588_PLLCON1_PWRDOWN,
			     RK3588_PLLCON1_PWRDOWN, 0),
	       pll->reg_base + RK3588_PLLCON(1));
}

static int rockchip_rk3588_pll_is_enabled(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);
	u32 pllcon = readl(pll->reg_base + RK3588_PLLCON(1));

	return !(pllcon & RK3588_PLLCON1_PWRDOWN);
}

static int rockchip_rk3588_pll_init(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll = to_rockchip_clk_pll(hw);

	if (!(pll->flags & ROCKCHIP_PLL_SYNC_RATE))
		return 0;

	return 0;
}

static const struct clk_ops rockchip_rk3588_pll_clk_norate_ops = {
	.recalc_rate = rockchip_rk3588_pll_recalc_rate,
	.enable = rockchip_rk3588_pll_enable,
	.disable = rockchip_rk3588_pll_disable,
	.is_enabled = rockchip_rk3588_pll_is_enabled,
};

static const struct clk_ops rockchip_rk3588_pll_clk_ops = {
	.recalc_rate = rockchip_rk3588_pll_recalc_rate,
	.round_rate = rockchip_rk3588_pll_round_rate,
	.set_rate = rockchip_rk3588_pll_set_rate,
	.enable = rockchip_rk3588_pll_enable,
	.disable = rockchip_rk3588_pll_disable,
	.is_enabled = rockchip_rk3588_pll_is_enabled,
	.init = rockchip_rk3588_pll_init,
};

#ifdef CONFIG_ROCKCHIP_CLK_COMPENSATION
int rockchip_pll_clk_compensation(struct clk *clk, int ppm)
{
	struct clk *parent = clk_get_parent(clk);
	struct rockchip_clk_pll *pll;
	static u32 frac, fbdiv;
	bool negative;
	u32 pllcon, pllcon0, pllcon2, fbdiv_mask, frac_mask, frac_shift;
	u64 fracdiv, m, n;

	if ((ppm > 1000) || (ppm < -1000))
		return -EINVAL;

	if (IS_ERR_OR_NULL(parent))
		return -EINVAL;

	pll = to_rockchip_clk_pll(__clk_get_hw(parent));
	if (!pll)
		return -EINVAL;

	switch (pll->type) {
	case pll_rk3036:
	case pll_rk3328:
		pllcon0 = RK3036_PLLCON(0);
		pllcon2 = RK3036_PLLCON(2);
		fbdiv_mask = RK3036_PLLCON0_FBDIV_MASK;
		frac_mask = RK3036_PLLCON2_FRAC_MASK;
		frac_shift = RK3036_PLLCON2_FRAC_SHIFT;
		if (!frac)
			writel(HIWORD_UPDATE(RK3036_PLLCON1_PLLPDSEL,
					     RK3036_PLLCON1_PLLPDSEL, 0),
			       pll->reg_base + RK3036_PLLCON(1));
		break;
	case pll_rk3066:
		return -EINVAL;
	case pll_rk3399:
		pllcon0 = RK3399_PLLCON(0);
		pllcon2 = RK3399_PLLCON(2);
		fbdiv_mask = RK3399_PLLCON0_FBDIV_MASK;
		frac_mask = RK3399_PLLCON2_FRAC_MASK;
		frac_shift = RK3399_PLLCON2_FRAC_SHIFT;
		break;
	case pll_rk3588:
		pllcon0 = RK3588_PLLCON(0);
		pllcon2 = RK3588_PLLCON(2);
		fbdiv_mask = RK3588_PLLCON0_M_MASK;
		frac_mask = RK3588_PLLCON2_K_MASK;
		frac_shift = RK3588_PLLCON2_K_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	negative = !!(ppm & BIT(31));
	ppm = negative ? ~ppm + 1 : ppm;

	if (!frac) {
		frac = readl_relaxed(pll->reg_base + pllcon2) & frac_mask;
		fbdiv = readl_relaxed(pll->reg_base + pllcon0) & fbdiv_mask;
	}

	switch (pll->type) {
	case pll_rk3036:
	case pll_rk3328:
	case pll_rk3066:
	case pll_rk3399:
		/*
		 *   delta frac                 frac          ppm
		 * -------------- = (fbdiv + ----------) * ---------
		 *    1 << 24                 1 << 24       1000000
		 *
		 */
		m = div64_u64((uint64_t)frac * ppm, 1000000);
		n = div64_u64((uint64_t)ppm << 24, 1000000) * fbdiv;

		fracdiv = negative ? frac - (m + n) : frac + (m + n);

		if (!frac || fracdiv > frac_mask)
			return -EINVAL;

		pllcon = readl_relaxed(pll->reg_base + pllcon2);
		pllcon &= ~(frac_mask << frac_shift);
		pllcon |= fracdiv << frac_shift;
		writel_relaxed(pllcon, pll->reg_base + pllcon2);
		break;
	case pll_rk3588:
		m = div64_u64((uint64_t)frac * ppm, 100000);
		n = div64_u64((uint64_t)ppm * 65535 * fbdiv, 100000);

		fracdiv = negative ? frac - (div64_u64(m + n, 10)) : frac + (div64_u64(m + n, 10));

		if (!frac || fracdiv > frac_mask)
			return -EINVAL;

		writel_relaxed(HIWORD_UPDATE(fracdiv, frac_mask, frac_shift),
			       pll->reg_base + pllcon2);
		break;
	default:
		return -EINVAL;
	}

	return  0;
}
EXPORT_SYMBOL(rockchip_pll_clk_compensation);
#endif

/*
 * Common registering of pll clocks
 */

struct clk *rockchip_clk_register_pll(struct rockchip_clk_provider *ctx,
		enum rockchip_pll_type pll_type,
		const char *name, const char *const *parent_names,
		u8 num_parents, int con_offset, int grf_lock_offset,
		int lock_shift, int mode_offset, int mode_shift,
		struct rockchip_pll_rate_table *rate_table,
		unsigned long flags, u8 clk_pll_flags)
{
	const char *pll_parents[3];
	struct clk_init_data init;
	struct rockchip_clk_pll *pll;
	struct clk_mux *pll_mux;
	struct clk *pll_clk, *mux_clk;
	char pll_name[20];

	if ((pll_type != pll_rk3328 && num_parents != 2) ||
	    (pll_type == pll_rk3328 && num_parents != 1)) {
		pr_err("%s: needs two parent clocks\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	/* name the actual pll */
	snprintf(pll_name, sizeof(pll_name), "pll_%s", name);

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	/* create the mux on top of the real pll */
	pll->pll_mux_ops = &clk_mux_ops;
	pll_mux = &pll->pll_mux;
	pll_mux->reg = ctx->reg_base + mode_offset;
	pll_mux->shift = mode_shift;
	if (pll_type == pll_rk3328)
		pll_mux->mask = PLL_RK3328_MODE_MASK;
	else
		pll_mux->mask = PLL_MODE_MASK;
	pll_mux->flags = 0;
	pll_mux->lock = &ctx->lock;
	pll_mux->hw.init = &init;
	pll_mux->flags |= CLK_MUX_HIWORD_MASK;

	/* the actual muxing is xin24m, pll-output, xin32k */
	pll_parents[0] = parent_names[0];
	pll_parents[1] = pll_name;
	pll_parents[2] = parent_names[1];

	init.name = name;
	init.flags = CLK_SET_RATE_PARENT;
	init.ops = pll->pll_mux_ops;
	init.parent_names = pll_parents;
	if (pll_type == pll_rk3328)
		init.num_parents = 2;
	else
		init.num_parents = ARRAY_SIZE(pll_parents);

	mux_clk = clk_register(NULL, &pll_mux->hw);
	if (IS_ERR(mux_clk))
		goto err_mux;

	/* now create the actual pll */
	init.name = pll_name;

#ifndef CONFIG_ROCKCHIP_LOW_PERFORMANCE
	/* keep all plls untouched for now */
	init.flags = flags | CLK_IGNORE_UNUSED;
#else
	init.flags = flags;
#endif

	init.parent_names = &parent_names[0];
	init.num_parents = 1;

	if (rate_table) {
		int len;

		/* find count of rates in rate_table */
		for (len = 0; rate_table[len].rate != 0; )
			len++;

		pll->rate_count = len;
		pll->rate_table = kmemdup(rate_table,
					pll->rate_count *
					sizeof(struct rockchip_pll_rate_table),
					GFP_KERNEL);
		WARN(!pll->rate_table,
			"%s: could not allocate rate table for %s\n",
			__func__, name);
	}

	switch (pll_type) {
	case pll_rk3036:
	case pll_rk3328:
		if (!pll->rate_table || IS_ERR(ctx->grf))
			init.ops = &rockchip_rk3036_pll_clk_norate_ops;
		else
			init.ops = &rockchip_rk3036_pll_clk_ops;
		break;
#ifdef CONFIG_ROCKCHIP_PLL_RK3066
	case pll_rk3066:
		if (!pll->rate_table || IS_ERR(ctx->grf))
			init.ops = &rockchip_rk3066_pll_clk_norate_ops;
		else
			init.ops = &rockchip_rk3066_pll_clk_ops;
		break;
#endif
#ifdef CONFIG_ROCKCHIP_PLL_RK3399
	case pll_rk3399:
		if (!pll->rate_table)
			init.ops = &rockchip_rk3399_pll_clk_norate_ops;
		else
			init.ops = &rockchip_rk3399_pll_clk_ops;
		break;
#endif
#ifdef CONFIG_ROCKCHIP_PLL_RK3588
	case pll_rk3588:
	case pll_rk3588_core:
		if (!pll->rate_table)
			init.ops = &rockchip_rk3588_pll_clk_norate_ops;
		else
			init.ops = &rockchip_rk3588_pll_clk_ops;
		init.flags = flags;
		break;
#endif
	default:
		pr_warn("%s: Unknown pll type for pll clk %s\n",
			__func__, name);
	}

	pll->hw.init = &init;
	pll->type = pll_type;
	pll->reg_base = ctx->reg_base + con_offset;
	pll->lock_offset = grf_lock_offset;
	pll->lock_shift = lock_shift;
	pll->flags = clk_pll_flags;
	pll->lock = &ctx->lock;
	pll->ctx = ctx;

	pll_clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(pll_clk)) {
		pr_err("%s: failed to register pll clock %s : %ld\n",
			__func__, name, PTR_ERR(pll_clk));
		goto err_pll;
	}

	return mux_clk;

err_pll:
	clk_unregister(mux_clk);
	mux_clk = pll_clk;
err_mux:
	kfree(pll);
	return mux_clk;
}

#ifdef CONFIG_ROCKCHIP_CLK_BOOST
static unsigned long rockchip_pll_con_to_rate(struct rockchip_clk_pll *pll,
					      u32 con0, u32 con1)
{
	switch (pll->type) {
	case pll_rk3036:
	case pll_rk3328:
		return rockchip_rk3036_pll_con_to_rate(pll, con0, con1);
	case pll_rk3066:
		break;
	case pll_rk3399:
		break;
	default:
		pr_warn("%s: Unknown pll type\n", __func__);
	}

	return 0;
}

void rockchip_boost_init(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll;
	struct device_node *np;
	u32 value, con0, con1;

	if (!hw)
		return;
	pll = to_rockchip_clk_pll(hw);
	np = of_parse_phandle(pll->ctx->cru_node, "rockchip,boost", 0);
	if (!np) {
		pr_debug("%s: failed to get boost np\n", __func__);
		return;
	}
	pll->boost = syscon_node_to_regmap(np);
	if (IS_ERR(pll->boost)) {
		pr_debug("%s: failed to get boost regmap\n", __func__);
		return;
	}

	if (!of_property_read_u32(np, "rockchip,boost-low-con0", &con0) &&
	    !of_property_read_u32(np, "rockchip,boost-low-con1", &con1)) {
		pr_debug("boost-low-con=0x%x 0x%x\n", con0, con1);
		regmap_write(pll->boost, BOOST_PLL_L_CON(0),
			     HIWORD_UPDATE(con0, BOOST_PLL_CON_MASK, 0));
		regmap_write(pll->boost, BOOST_PLL_L_CON(1),
			     HIWORD_UPDATE(con1, BOOST_PLL_CON_MASK, 0));
		pll->boost_low_rate = rockchip_pll_con_to_rate(pll, con0,
							       con1);
		pr_debug("boost-low-rate=%lu\n", pll->boost_low_rate);
	}
	if (!of_property_read_u32(np, "rockchip,boost-high-con0", &con0) &&
	    !of_property_read_u32(np, "rockchip,boost-high-con1", &con1)) {
		pr_debug("boost-high-con=0x%x 0x%x\n", con0, con1);
		regmap_write(pll->boost, BOOST_PLL_H_CON(0),
			     HIWORD_UPDATE(con0, BOOST_PLL_CON_MASK, 0));
		regmap_write(pll->boost, BOOST_PLL_H_CON(1),
			     HIWORD_UPDATE(con1, BOOST_PLL_CON_MASK, 0));
		pll->boost_high_rate = rockchip_pll_con_to_rate(pll, con0,
								con1);
		pr_debug("boost-high-rate=%lu\n", pll->boost_high_rate);
	}
	if (!of_property_read_u32(np, "rockchip,boost-backup-pll", &value)) {
		pr_debug("boost-backup-pll=0x%x\n", value);
		regmap_write(pll->boost, BOOST_CLK_CON,
			     HIWORD_UPDATE(value, BOOST_BACKUP_PLL_MASK,
					   BOOST_BACKUP_PLL_SHIFT));
	}
	if (!of_property_read_u32(np, "rockchip,boost-backup-pll-usage",
				  &pll->boost_backup_pll_usage)) {
		pr_debug("boost-backup-pll-usage=0x%x\n",
			 pll->boost_backup_pll_usage);
		regmap_write(pll->boost, BOOST_CLK_CON,
			     HIWORD_UPDATE(pll->boost_backup_pll_usage,
					   BOOST_BACKUP_PLL_USAGE_MASK,
					   BOOST_BACKUP_PLL_USAGE_SHIFT));
	}
	if (!of_property_read_u32(np, "rockchip,boost-switch-threshold",
				  &value)) {
		pr_debug("boost-switch-threshold=0x%x\n", value);
		regmap_write(pll->boost, BOOST_SWITCH_THRESHOLD, value);
	}
	if (!of_property_read_u32(np, "rockchip,boost-statis-threshold",
				  &value)) {
		pr_debug("boost-statis-threshold=0x%x\n", value);
		regmap_write(pll->boost, BOOST_STATIS_THRESHOLD, value);
	}
	if (!of_property_read_u32(np, "rockchip,boost-statis-enable",
				  &value)) {
		pr_debug("boost-statis-enable=0x%x\n", value);
		regmap_write(pll->boost, BOOST_BOOST_CON,
			     HIWORD_UPDATE(value, BOOST_STATIS_ENABLE_MASK,
					   BOOST_STATIS_ENABLE_SHIFT));
	}
	if (!of_property_read_u32(np, "rockchip,boost-enable", &value)) {
		pr_debug("boost-enable=0x%x\n", value);
		regmap_write(pll->boost, BOOST_BOOST_CON,
			     HIWORD_UPDATE(value, BOOST_ENABLE_MASK,
					   BOOST_ENABLE_SHIFT));
		if (value)
			pll->boost_enabled = true;
	}
#ifdef CONFIG_DEBUG_FS
	if (pll->boost_enabled) {
		mutex_lock(&clk_boost_lock);
		hlist_add_head(&pll->debug_node, &clk_boost_list);
		mutex_unlock(&clk_boost_lock);
	}
#endif
}

void rockchip_boost_enable_recovery_sw_low(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll;
	unsigned int val;

	if (!hw)
		return;
	pll = to_rockchip_clk_pll(hw);
	if (!pll->boost_enabled)
		return;

	regmap_write(pll->boost, BOOST_BOOST_CON,
		     HIWORD_UPDATE(1, BOOST_RECOVERY_MASK,
				   BOOST_RECOVERY_SHIFT));
	do {
		regmap_read(pll->boost, BOOST_FSM_STATUS, &val);
	} while (!(val & BOOST_BUSY_STATE));

	regmap_write(pll->boost, BOOST_BOOST_CON,
		     HIWORD_UPDATE(1, BOOST_SW_CTRL_MASK,
				   BOOST_SW_CTRL_SHIFT) |
		     HIWORD_UPDATE(1, BOOST_LOW_FREQ_EN_MASK,
				   BOOST_LOW_FREQ_EN_SHIFT));
}

static void rockchip_boost_disable_low(struct rockchip_clk_pll *pll)
{
	if (!pll->boost_enabled)
		return;

	regmap_write(pll->boost, BOOST_BOOST_CON,
		     HIWORD_UPDATE(0, BOOST_LOW_FREQ_EN_MASK,
				   BOOST_LOW_FREQ_EN_SHIFT));
}

void rockchip_boost_disable_recovery_sw(struct clk_hw *hw)
{
	struct rockchip_clk_pll *pll;

	if (!hw)
		return;
	pll = to_rockchip_clk_pll(hw);
	if (!pll->boost_enabled)
		return;

	regmap_write(pll->boost, BOOST_BOOST_CON,
		     HIWORD_UPDATE(0, BOOST_RECOVERY_MASK,
				   BOOST_RECOVERY_SHIFT));
	regmap_write(pll->boost, BOOST_BOOST_CON,
		     HIWORD_UPDATE(0, BOOST_SW_CTRL_MASK,
				   BOOST_SW_CTRL_SHIFT));
}

void rockchip_boost_add_core_div(struct clk_hw *hw, unsigned long prate)
{
	struct rockchip_clk_pll *pll;
	unsigned int div;

	if (!hw)
		return;
	pll = to_rockchip_clk_pll(hw);
	if (!pll->boost_enabled || pll->boost_backup_pll_rate == prate)
		return;

	/* todo */
	if (pll->boost_backup_pll_usage == BOOST_BACKUP_PLL_USAGE_TARGET)
		return;
	/*
	 * cpu clock rate should be less than or equal to
	 * low rate when change pll rate in boost module
	 */
	if (pll->boost_low_rate && prate > pll->boost_low_rate) {
		div =  DIV_ROUND_UP(prate, pll->boost_low_rate) - 1;
		regmap_write(pll->boost, BOOST_CLK_CON,
			     HIWORD_UPDATE(div, BOOST_CORE_DIV_MASK,
					   BOOST_CORE_DIV_SHIFT));
		pll->boost_backup_pll_rate = prate;
	}
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

#ifndef MODULE
static int boost_summary_show(struct seq_file *s, void *data)
{
	struct rockchip_clk_pll *pll = (struct rockchip_clk_pll *)s->private;
	u32 boost_count = 0;
	u32 freq_cnt0 = 0, freq_cnt1 = 0;
	u64 freq_cnt = 0, high_freq_time = 0;
	u32 short_count = 0, short_threshold = 0;
	u32 interval_time = 0;

	seq_puts(s, " device    boost_count   high_freq_count  high_freq_time  short_count  short_threshold  interval_count\n");
	seq_puts(s, "------------------------------------------------------------------------------------------------------\n");
	seq_printf(s, " %s\n", clk_hw_get_name(&pll->hw));

	regmap_read(pll->boost, BOOST_SWITCH_CNT, &boost_count);

	regmap_read(pll->boost, BOOST_HIGH_PERF_CNT0, &freq_cnt0);
	regmap_read(pll->boost, BOOST_HIGH_PERF_CNT1, &freq_cnt1);
	freq_cnt = ((u64)freq_cnt1 << 32) + (u64)freq_cnt0;
	high_freq_time = freq_cnt;
	do_div(high_freq_time, 24);

	regmap_read(pll->boost, BOOST_SHORT_SWITCH_CNT, &short_count);
	regmap_read(pll->boost, BOOST_STATIS_THRESHOLD, &short_threshold);
	regmap_read(pll->boost, BOOST_SWITCH_THRESHOLD, &interval_time);

	seq_printf(s, "%22u %17llu %15llu %12u %16u %15u\n",
		   boost_count, freq_cnt, high_freq_time, short_count,
		   short_threshold, interval_time);

	return 0;
}

static int boost_summary_open(struct inode *inode, struct file *file)
{
	return single_open(file, boost_summary_show, inode->i_private);
}

static const struct file_operations boost_summary_fops = {
	.open		= boost_summary_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int boost_config_show(struct seq_file *s, void *data)
{
	struct rockchip_clk_pll *pll = (struct rockchip_clk_pll *)s->private;

	seq_printf(s, "boost_enabled:   %d\n", pll->boost_enabled);
	seq_printf(s, "boost_low_rate:  %lu\n", pll->boost_low_rate);
	seq_printf(s, "boost_high_rate: %lu\n", pll->boost_high_rate);

	return 0;
}

static int boost_config_open(struct inode *inode, struct file *file)
{
	return single_open(file, boost_config_show, inode->i_private);
}

static const struct file_operations boost_config_fops = {
	.open		= boost_config_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int boost_debug_create_one(struct rockchip_clk_pll *pll,
				  struct dentry *rootdir)
{
	struct dentry *pdentry, *d;

	pdentry = debugfs_lookup(clk_hw_get_name(&pll->hw), rootdir);
	if (!pdentry) {
		pr_err("%s: failed to lookup %s dentry\n", __func__,
		       clk_hw_get_name(&pll->hw));
		return -ENOMEM;
	}

	d = debugfs_create_file("boost_summary", 0444, pdentry,
				pll, &boost_summary_fops);
	if (!d) {
		pr_err("%s: failed to create boost_summary file\n", __func__);
		return -ENOMEM;
	}

	d = debugfs_create_file("boost_config", 0444, pdentry,
				pll, &boost_config_fops);
	if (!d) {
		pr_err("%s: failed to create boost config file\n", __func__);
		return -ENOMEM;
	}

	return 0;
}

static int __init boost_debug_init(void)
{
	struct rockchip_clk_pll *pll;
	struct dentry *rootdir;

	rootdir = debugfs_lookup("clk", NULL);
	if (!rootdir) {
		pr_err("%s: failed to lookup clk dentry\n", __func__);
		return -ENOMEM;
	}

	mutex_lock(&clk_boost_lock);

	hlist_for_each_entry(pll, &clk_boost_list, debug_node)
		boost_debug_create_one(pll, rootdir);

	mutex_unlock(&clk_boost_lock);

	return 0;
}
late_initcall(boost_debug_init);
#endif /* MODULE */
#endif /* CONFIG_DEBUG_FS */
#endif /* CONFIG_ROCKCHIP_CLK_BOOST */
