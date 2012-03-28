/* arch/arm/mach-rk29/clock.c
 *
 * Copyright (C) 2010, 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

//#define DEBUG
#define pr_fmt(fmt) "clock: %s: " fmt, __func__

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
#include <linux/clkdev.h>
#else
#include <asm/clkdev.h>
#endif
#include <mach/rk29_iomap.h>
#include <mach/cru.h>
#include <mach/pmu.h>
#include <mach/sram.h>
#include <mach/board.h>
#include <mach/clock.h>


/* Clock flags */
/* bit 0 is free */
#define RATE_FIXED		(1 << 1)	/* Fixed clock rate */
#define CONFIG_PARTICIPANT	(1 << 10)	/* Fundamental clock */
#define IS_PD			(1 << 2)	/* Power Domain */

#define regfile_readl(offset)	readl(RK29_GRF_BASE + offset)
#define pmu_readl(offset)	readl(RK29_PMU_BASE + offset)

#define MHZ			(1000*1000)
#define KHZ			1000

struct clk {
	struct list_head	node;
	const char		*name;
	struct clk		*parent;
	struct list_head	children;
	struct list_head	sibling;	/* node for children */
	unsigned long		rate;
	u32			flags;
	int			(*mode)(struct clk *clk, int on);
	unsigned long		(*recalc)(struct clk *);	/* if null, follow parent */
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	struct clk*		(*get_parent)(struct clk *);	/* get clk's parent from the hardware. default is clksel_get_parent if parents present */
	int			(*set_parent)(struct clk *, struct clk *);	/* default is clksel_set_parent if parents present */
	s16			usecount;
	u16			notifier_count;
	u8			gate_idx;
	u8			pll_idx;
	u8			clksel_con;
	u8			clksel_mask;
	u8			clksel_shift;
	u8			clksel_maxdiv;
	u8			clksel_parent_mask;
	u8			clksel_parent_shift;
	struct clk		**parents;
};

static void clk_notify(struct clk *clk, unsigned long msg,
		       unsigned long old_rate, unsigned long new_rate);
static int clk_enable_nolock(struct clk *clk);
static void clk_disable_nolock(struct clk *clk);
static int clk_set_rate_nolock(struct clk *clk, unsigned long rate);
static int clk_set_parent_nolock(struct clk *clk, struct clk *parent);
static void __clk_reparent(struct clk *child, struct clk *parent);
static void __propagate_rate(struct clk *tclk);
static struct clk codec_pll_clk;
static struct clk general_pll_clk;
static bool has_xin27m = true;

static unsigned long clksel_recalc_div(struct clk *clk)
{
	u32 div = ((cru_readl(clk->clksel_con) >> clk->clksel_shift) & clk->clksel_mask) + 1;
	unsigned long rate = clk->parent->rate / div;
	pr_debug("%s new clock rate is %lu (div %u)\n", clk->name, rate, div);
	return rate;
}

static unsigned long clksel_recalc_shift(struct clk *clk)
{
	u32 shift = (cru_readl(clk->clksel_con) >> clk->clksel_shift) & clk->clksel_mask;
	unsigned long rate = clk->parent->rate >> shift;
	pr_debug("%s new clock rate is %lu (shift %u)\n", clk->name, rate, shift);
	return rate;
}

static unsigned long clksel_recalc_frac(struct clk *clk)
{
	unsigned long rate;
	u64 rate64;
	u32 r = cru_readl(clk->clksel_con), numerator, denominator;
	if (r == 0) // FPGA ?
		return clk->parent->rate;
	numerator = r >> 16;
	denominator = r & 0xFFFF;
	rate64 = (u64)clk->parent->rate * numerator;
	do_div(rate64, denominator);
	rate = rate64;
	pr_debug("%s new clock rate is %lu (frac %u/%u)\n", clk->name, rate, numerator, denominator);
	return rate;
}

static int clksel_set_rate_div(struct clk *clk, unsigned long rate)
{
	u32 div;

	for (div = 0; div <= clk->clksel_mask; div++) {
		u32 new_rate = clk->parent->rate / (div + 1);
		if (new_rate <= rate) {
			u32 v = cru_readl(clk->clksel_con);
			v &= ~((u32) clk->clksel_mask << clk->clksel_shift);
			v |= div << clk->clksel_shift;
			cru_writel(v, clk->clksel_con);
			clk->rate = new_rate;
			pr_debug("clksel_set_rate_div for clock %s to rate %ld (div %d)\n", clk->name, rate, div + 1);
			return 0;
		}
	}

	return -ENOENT;
}

static long clksel_round_rate_div_by_parent(struct clk *clk, unsigned long rate, struct clk *parent, unsigned long max_rate)
{
	u32 div;
	unsigned long prev = ULONG_MAX, actual = parent->rate;

	if (max_rate < rate)
		max_rate = rate;
	for (div = 0; div <= clk->clksel_mask; div++) {
		actual = parent->rate / (div + 1);
		if (actual > max_rate)
			continue;
		if (actual > rate)
			prev = actual;
		if (actual && actual <= rate) {
			if ((prev - rate) <= (rate - actual)) {
				actual = prev;
				div--;
			}
			break;
		}
	}
	if (div > clk->clksel_mask)
		div = clk->clksel_mask;
	pr_debug("clock %s, target rate %ld, max rate %ld, rounded rate %ld (div %d)\n", clk->name, rate, max_rate, actual, div + 1);

	return actual;
}

#if 0
static long clksel_round_rate_div(struct clk *clk, unsigned long rate)
{
	return clksel_round_rate_div_by_parent(clk, rate, clk->parent, ULONG_MAX);
}
#endif

static int clksel_set_rate_shift(struct clk *clk, unsigned long rate)
{
	u32 shift;

	for (shift = 0; (1 << shift) <= clk->clksel_maxdiv; shift++) {
		u32 new_rate = clk->parent->rate >> shift;
		if (new_rate <= rate) {
			u32 v = cru_readl(clk->clksel_con);
			v &= ~((u32) clk->clksel_mask << clk->clksel_shift);
			v |= shift << clk->clksel_shift;
			cru_writel(v, clk->clksel_con);
			clk->rate = new_rate;
			pr_debug("clksel_set_rate_shift for clock %s to rate %ld (shift %d)\n", clk->name, rate, shift);
			return 0;
		}
	}

	return -ENOENT;
}

static struct clk* clksel_get_parent(struct clk *clk)
{
	return clk->parents[(cru_readl(clk->clksel_con) >> clk->clksel_parent_shift) & clk->clksel_parent_mask];
}

static int clksel_set_parent(struct clk *clk, struct clk *parent)
{
	struct clk **p = clk->parents;
	u32 i;

	if (unlikely(!p))
		return -EINVAL;
	for (i = 0; (i <= clk->clksel_parent_mask) && *p; i++, p++) {
		u32 v;
		if (*p != parent)
			continue;
		v = cru_readl(clk->clksel_con);
		v &= ~((u32) clk->clksel_parent_mask << clk->clksel_parent_shift);
		v |= (i << clk->clksel_parent_shift);
		cru_writel(v, clk->clksel_con);
		return 0;
	}
	return -EINVAL;
}

/* Work around CRU_CLKGATE3_CON bit21~20 bug */
volatile u32 cru_clkgate3_con_mirror;

static int gate_mode(struct clk *clk, int on)
{
	unsigned long flags;
	u32 reg;
	int idx = clk->gate_idx;
	u32 v;

	if (idx >= CLK_GATE_MAX)
		return -EINVAL;

	reg = CRU_CLKGATE0_CON;
	reg += (idx >> 5) << 2;
	idx &= 0x1F;

	/* ddr reconfig may change gate */
	local_irq_save(flags);

	if (reg == CRU_CLKGATE3_CON)
		v = cru_clkgate3_con_mirror;
	else
		v = cru_readl(reg);

	if (on)
		v &= ~(1 << idx);	// clear bit
	else
		v |= (1 << idx);	// set bit

	if (reg == CRU_CLKGATE3_CON)
		cru_clkgate3_con_mirror = v;
	cru_writel(v, reg);

	local_irq_restore(flags);
	return 0;
}

/* Work around CRU_SOFTRST0_CON bit29~27 bug */
static volatile u32 cru_softrst0_con_mirror;

void cru_set_soft_reset(enum cru_soft_reset idx, bool on)
{
	unsigned long flags;
	u32 reg = CRU_SOFTRST0_CON + ((idx >> 5) << 2);
	u32 mask = 1 << (idx & 31);
	u32 v;

	if (idx >= SOFT_RST_MAX)
		return;

	local_irq_save(flags);

	if (reg == CRU_SOFTRST0_CON)
		v = cru_softrst0_con_mirror;
	else
		v = cru_readl(reg);

	if (on)
		v |= mask;
	else
		v &= ~mask;

	if (reg == CRU_SOFTRST0_CON)
		cru_softrst0_con_mirror = v;
	cru_writel(v, reg);

	local_irq_restore(flags);
}

static struct clk xin24m = {
	.name		= "xin24m",
	.rate		= 24 * MHZ,
	.flags		= RATE_FIXED,
};

static struct clk clk_12m = {
	.name		= "clk_12m",
	.rate		= 12 * MHZ,
	.parent		= &xin24m,
	.flags		= RATE_FIXED,
};

static struct clk xin27m = {
	.name		= "xin27m",
	.rate		= 27 * MHZ,
	.flags		= RATE_FIXED,
};

static struct clk otgphy0_clkin = {
	.name		= "otgphy0_clkin",
	.rate		= 480 * MHZ,
	.flags		= RATE_FIXED,
};

static struct clk otgphy1_clkin = {
	.name		= "otgphy1_clkin",
	.rate		= 480 * MHZ,
	.flags		= RATE_FIXED,
};


static noinline void delay_500ns(void)
{
	udelay(1);
}

static noinline void delay_300us(void)
{
	udelay(300);
}

#define GENERAL_PLL_IDX     0
#define CODEC_PLL_IDX      1
#define ARM_PLL_IDX        2
#define DDR_PLL_IDX        3

#define GRF_SOC_CON0       0xbc
static void pll_wait_lock(int pll_idx)
{
	u32 bit = 0x2000000u << pll_idx;
	int delay = 2400000;
	while (delay > 0) {
		if (regfile_readl(GRF_SOC_CON0) & bit)
			break;
		delay--;
	}
	if (delay == 0) {
		pr_warning("wait pll bit 0x%x time out!\n", bit);
	}
}

static unsigned long arm_pll_clk_recalc(struct clk *clk)
{
	unsigned long rate;

	if ((cru_readl(CRU_MODE_CON) & CRU_CPU_MODE_MASK) == CRU_CPU_MODE_NORMAL) {
		u32 v = cru_readl(CRU_APLL_CON);
		u64 rate64 = (u64) clk->parent->rate * PLL_NF2(v);
		do_div(rate64, PLL_NR(v));
		rate = rate64 >> PLL_NO_SHIFT(v);
		pr_debug("%s new clock rate is %ld (NF %d NR %d NO %d)\n", clk->name, rate, PLL_NF2(v), PLL_NR(v), 1 << PLL_NO_SHIFT(v));
	} else {
		rate = clk->parent->rate;
		pr_debug("%s new clock rate is %ld (slow mode)\n", clk->name, rate);
	}

	return rate;
}

struct arm_pll_set {
	unsigned long rate;
	u32 apll_con;
	u32 clksel0_con;
	unsigned long lpj;
};

#define CORE_ACLK_11	(0 << 5)
#define CORE_ACLK_21	(1 << 5)
#define CORE_ACLK_31	(2 << 5)
#define CORE_ACLK_41	(3 << 5)
#define CORE_ACLK_81	(4 << 5)
#define CORE_ACLK_MASK	(7 << 5)

#define ACLK_HCLK_11	(0 << 8)
#define ACLK_HCLK_21	(1 << 8)
#define ACLK_HCLK_41	(2 << 8)
#define ACLK_HCLK_MASK	(3 << 8)

#define ACLK_PCLK_11	(0 << 10)
#define ACLK_PCLK_21	(1 << 10)
#define ACLK_PCLK_41	(2 << 10)
#define ACLK_PCLK_81	(3 << 10)
#define ACLK_PCLK_MASK	(3 << 10)

#define LPJ_600MHZ	2998368ULL
static unsigned long lpj_gpll;

#define ARM_PLL(_mhz, nr, nf, no, _axi_div, _ahb_div, _apb_div) \
{ \
	.rate		= _mhz * MHZ, \
	.apll_con	= PLL_CLKR(nr) | PLL_CLKF(nf >> 1) | PLL_NO_##no, \
	.clksel0_con	= CORE_ACLK_##_axi_div | ACLK_HCLK_##_ahb_div | ACLK_PCLK_##_apb_div, \
	.lpj		= LPJ_600MHZ * _mhz / 600, \
}

static const struct arm_pll_set arm_pll[] = {
	// rate = 24 * NF / (NR * NO)
	//      rate NR  NF NO adiv hdiv pdiv
	ARM_PLL(1200, 1, 50, 1, 31, 21, 81),
	ARM_PLL(1176, 2, 98, 1, 31, 21, 81),
	ARM_PLL(1104, 1, 46, 1, 31, 21, 81),
	ARM_PLL(1008, 1, 42, 1, 21, 21, 81),
	ARM_PLL( 912, 1, 38, 1, 21, 21, 81),
	ARM_PLL( 888, 2, 74, 1, 21, 21, 81),
	ARM_PLL( 816, 1, 34, 1, 21, 21, 81),
	ARM_PLL( 696, 1, 58, 2, 21, 21, 81),
	ARM_PLL( 624, 1, 52, 2, 21, 21, 81),
	ARM_PLL( 600, 1, 50, 2, 21, 21, 81),
	ARM_PLL( 504, 1, 42, 2, 21, 21, 81),
	ARM_PLL( 408, 1, 34, 2, 21, 21, 81),
	ARM_PLL( 300, 1, 50, 4, 21, 21, 41),
	ARM_PLL( 204, 1, 34, 4, 21, 21, 41),
	ARM_PLL( 102, 1, 34, 8, 21, 21, 41),
	// last item, pll power down.
	ARM_PLL(  24, 1, 64, 8, 21, 21, 41),
};

#define CORE_PARENT_MASK	(3 << 23)
#define CORE_PARENT_ARM_PLL	(0 << 23)
#define CORE_PARENT_GENERAL_PLL	(1 << 23)

static const struct arm_pll_set* arm_pll_clk_get_best_pll_set(unsigned long rate)
{
	const struct arm_pll_set *ps, *pt;

	/* find the arm_pll we want. */
	ps = pt = &arm_pll[0];
	while (1) {
		if (pt->rate == rate) {
			ps = pt;
			break;
		}
		// we are sorted, and ps->rate > pt->rate.
		if ((pt->rate > rate || (rate - pt->rate < ps->rate - rate)))
			ps = pt;
		if (pt->rate < rate || pt->rate == 24 * MHZ)
			break;
		pt++;
	}

	return ps;
}

static int arm_pll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	const struct arm_pll_set *ps;
	u32 clksel0_con;
	bool aclk_limit = rate & 1;

	rate &= ~1;
	ps = arm_pll_clk_get_best_pll_set(rate);
	clksel0_con = ps->clksel0_con;

	if (aclk_limit) {
		u32 aclk_div = clksel0_con & CORE_ACLK_MASK;
		if (rate > 408 * MHZ && aclk_div < CORE_ACLK_41)
			aclk_div = CORE_ACLK_41;
		clksel0_con = (clksel0_con & ~CORE_ACLK_MASK) | aclk_div;
	}

	if (ps->apll_con == cru_readl(CRU_APLL_CON)) {
		cru_writel((cru_readl(CRU_CLKSEL0_CON) & ~(CORE_ACLK_MASK | ACLK_HCLK_MASK | ACLK_PCLK_MASK)) | clksel0_con, CRU_CLKSEL0_CON);
		return 0;
	}

	local_irq_save(flags);
	/* make aclk safe & reparent to general pll */
	cru_writel((cru_readl(CRU_CLKSEL0_CON) & ~(CORE_PARENT_MASK | CORE_ACLK_MASK)) | CORE_PARENT_GENERAL_PLL | CORE_ACLK_21, CRU_CLKSEL0_CON);
	loops_per_jiffy = lpj_gpll;

	/* enter slow mode */
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CPU_MODE_MASK) | CRU_CPU_MODE_SLOW, CRU_MODE_CON);

	/* power down */
	cru_writel(cru_readl(CRU_APLL_CON) | PLL_PD, CRU_APLL_CON);
	local_irq_restore(flags);

	delay_500ns();

	cru_writel(ps->apll_con | PLL_PD, CRU_APLL_CON);

	delay_500ns();

	/* power up */
	cru_writel(ps->apll_con, CRU_APLL_CON);

	delay_300us();
	pll_wait_lock(ARM_PLL_IDX);

	local_irq_save(flags);
	/* enter normal mode */
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CPU_MODE_MASK) | CRU_CPU_MODE_NORMAL, CRU_MODE_CON);

	loops_per_jiffy = ps->lpj;
	/* reparent to arm pll & set aclk/hclk/pclk */
	cru_writel((cru_readl(CRU_CLKSEL0_CON) & ~(CORE_PARENT_MASK | CORE_ACLK_MASK | ACLK_HCLK_MASK | ACLK_PCLK_MASK)) | CORE_PARENT_ARM_PLL | clksel0_con, CRU_CLKSEL0_CON);
	local_irq_restore(flags);

	return 0;
}

static long arm_pll_clk_round_rate(struct clk *clk, unsigned long rate)
{
	return arm_pll_clk_get_best_pll_set(rate)->rate;
}

static struct clk *arm_pll_parents[2] = { &xin24m, &xin27m };

static struct clk arm_pll_clk = {
	.name		= "arm_pll",
	.parent		= &xin24m,
	.recalc		= arm_pll_clk_recalc,
	.set_rate	= arm_pll_clk_set_rate,
	.round_rate	= arm_pll_clk_round_rate,
	.clksel_con	= CRU_MODE_CON,
	.clksel_parent_mask	= 1,
	.clksel_parent_shift	= 8,
	.parents	= arm_pll_parents,
};

static unsigned long ddr_pll_clk_recalc(struct clk *clk)
{
	unsigned long rate;

	if ((cru_readl(CRU_MODE_CON) & CRU_DDR_MODE_MASK) == CRU_DDR_MODE_NORMAL) {
		u32 v = cru_readl(CRU_DPLL_CON);
		u64 rate64 = (u64) clk->parent->rate * PLL_NF(v);
		do_div(rate64, PLL_NR(v));
		rate = rate64 >> PLL_NO_SHIFT(v);
		pr_debug("%s new clock rate is %ld (NF %d NR %d NO %d)\n", clk->name, rate, PLL_NF(v), PLL_NR(v), 1 << PLL_NO_SHIFT(v));
	} else {
		rate = clk->parent->rate;
		pr_debug("%s new clock rate is %ld (slow mode)\n", clk->name, rate);
	}

	return rate;
}

static int ddr_pll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	/* do nothing here */
	return 0;
}

static struct clk *ddr_pll_parents[4] = { &xin24m, &xin27m, &codec_pll_clk, &general_pll_clk };

static struct clk ddr_pll_clk = {
	.name		= "ddr_pll",
	.parent		= &xin24m,
	.recalc		= ddr_pll_clk_recalc,
	.set_rate	= ddr_pll_clk_set_rate,
	.clksel_con	= CRU_MODE_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 13,
	.parents	= ddr_pll_parents,
};


static int codec_pll_clk_mode(struct clk *clk, int on)
{
	u32 cpll = cru_readl(CRU_CPLL_CON);
	if (on) {
		cru_writel(cpll & ~(PLL_PD | PLL_BYPASS), CRU_CPLL_CON);
		delay_300us();
		pll_wait_lock(CODEC_PLL_IDX);
		cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | CRU_CODEC_MODE_NORMAL, CRU_MODE_CON);
	} else {
		cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | CRU_CODEC_MODE_SLOW, CRU_MODE_CON);
		cru_writel(cpll | PLL_BYPASS, CRU_CPLL_CON);
		cru_writel(cpll | PLL_PD | PLL_BYPASS, CRU_CPLL_CON);
		delay_500ns();
	}
	return 0;
}

static unsigned long codec_pll_clk_recalc(struct clk *clk)
{
	unsigned long rate;
	u32 v = cru_readl(CRU_CPLL_CON);
	u64 rate64 = (u64) clk->parent->rate * PLL_NF(v);
	do_div(rate64, PLL_NR(v));
	rate = rate64 >> PLL_NO_SHIFT(v);
	pr_debug("%s new clock rate is %ld (NF %d NR %d NO %d)\n", clk->name, rate, PLL_NF(v), PLL_NR(v), 1 << PLL_NO_SHIFT(v));
	if ((cru_readl(CRU_MODE_CON) & CRU_CODEC_MODE_MASK) != CRU_CODEC_MODE_NORMAL)
		pr_debug("%s rate is %ld (slow mode) actually\n", clk->name, clk->parent->rate);
	return rate;
}

#define CODEC_PLL_PARENT_MASK	(3 << 11)
#define CODEC_PLL_PARENT_XIN24M	(0 << 11)
#define CODEC_PLL_PARENT_XIN27M	(1 << 11)
#define CODEC_PLL_PARENT_DDR_PLL	(2 << 11)
#define CODEC_PLL_PARENT_GENERAL_PLL	(3 << 11)

struct codec_pll_set {
	unsigned long rate;
	u32 pll_con;
	u32 parent_con;
};

#define CODEC_PLL(_khz, _parent, band, nr, nf, no) \
{ \
	.rate		= _khz * KHZ, \
	.pll_con	= PLL_##band##_BAND | PLL_CLKR(nr) | PLL_CLKF(nf) | PLL_NO_##no, \
	.parent_con	= CODEC_PLL_PARENT_XIN##_parent##M, \
}

static const struct codec_pll_set codec_pll[] = {
	//        rate parent band NR  NF NO
	CODEC_PLL(108000, 24,  LOW, 1, 18, 4),	// for TV
	CODEC_PLL(648000, 24, HIGH, 1, 27, 1),
	CODEC_PLL(148500, 27,  LOW, 2, 88, 8),	//change for jetta hdmi dclk jitter  20120322// for HDMI
	CODEC_PLL(297000, 27,  LOW, 2, 88, 4),  //change for jetta hdmi dclk jitter  20120322// for HDMI
	CODEC_PLL(445500, 27,  LOW, 2, 33, 1),
	CODEC_PLL(594000, 27, HIGH, 1, 22, 1),
	CODEC_PLL(891000, 27, HIGH, 1, 33, 1),
	CODEC_PLL(300000, 24,  LOW, 1, 25, 2),	// for GPU
	CODEC_PLL(360000, 24,  LOW, 1, 15, 1),
	CODEC_PLL(408000, 24,  LOW, 1, 17, 1),
	CODEC_PLL(456000, 24,  LOW, 1, 19, 1),
	CODEC_PLL(504000, 24,  LOW, 1, 21, 1),
	CODEC_PLL(552000, 24,  LOW, 1, 23, 1),
	CODEC_PLL(600000, 24, HIGH, 1, 25, 1),
};

static int codec_pll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	int i;
	u32 work_mode;

	const struct codec_pll_set *ps = NULL;

	for (i = 0; i < ARRAY_SIZE(codec_pll); i++) {
		if (codec_pll[i].rate == rate) {
			ps = &codec_pll[i];
			break;
		}
	}
	if (!ps)
		return -ENOENT;

	if (!has_xin27m && ps->parent_con == CODEC_PLL_PARENT_XIN27M)
		return -ENOENT;

	work_mode = cru_readl(CRU_MODE_CON) & CRU_CODEC_MODE_MASK;

	/* enter slow mode */
	cru_writel((cru_readl(CRU_MODE_CON) & ~(CRU_CODEC_MODE_MASK | CODEC_PLL_PARENT_MASK)) | CRU_CODEC_MODE_SLOW | ps->parent_con, CRU_MODE_CON);

	/* power down */
	cru_writel(cru_readl(CRU_CPLL_CON) | PLL_PD, CRU_CPLL_CON);

	delay_500ns();

	cru_writel(ps->pll_con | PLL_PD, CRU_CPLL_CON);

	delay_500ns();

	/* power up */
	cru_writel(ps->pll_con, CRU_CPLL_CON);

	delay_300us();
	pll_wait_lock(CODEC_PLL_IDX);

	/* enter normal mode */
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CODEC_MODE_MASK) | work_mode, CRU_MODE_CON);

	clk_set_parent_nolock(clk, ps->parent_con == CODEC_PLL_PARENT_XIN24M ? &xin24m : &xin27m);

	return 0;
}

static struct clk *codec_pll_parents[4] = { &xin24m, &xin27m, &ddr_pll_clk, &general_pll_clk };

static struct clk codec_pll_clk = {
	.name		= "codec_pll",
	.parent		= &xin24m,
	.mode		= codec_pll_clk_mode,
	.recalc		= codec_pll_clk_recalc,
	.set_rate	= codec_pll_clk_set_rate,
	.clksel_con	= CRU_MODE_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 11,
	.parents	= codec_pll_parents,
};


static unsigned long general_pll_clk_recalc(struct clk *clk)
{
	unsigned long rate;

	if ((cru_readl(CRU_MODE_CON) & CRU_GENERAL_MODE_MASK) == CRU_GENERAL_MODE_NORMAL) {
		u32 v = cru_readl(CRU_GPLL_CON);
		u64 rate64 = (u64) clk->parent->rate * PLL_NF(v);
		do_div(rate64, PLL_NR(v));
		rate = rate64 >> PLL_NO_SHIFT(v);
		pr_debug("%s new clock rate is %ld (NF %d NR %d NO %d)\n", clk->name, rate, PLL_NF(v), PLL_NR(v), 1 << PLL_NO_SHIFT(v));
	} else {
		rate = clk->parent->rate;
		pr_debug("%s new clock rate is %ld (slow mode)\n", clk->name, rate);
	}

	return rate;
}

static int general_pll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	u32 pll_con;

	switch (rate) {
	case 96 * MHZ:
		/* 96M: low-band, NR=1, NF=16, NO=4 */
		pll_con = PLL_LOW_BAND | PLL_CLKR(1) | PLL_CLKF(16) | PLL_NO_4;
	break;
	case 144*MHZ:
		/* 96M: low-band, NR=1, NF=16, NO=4 */
		pll_con = PLL_LOW_BAND | PLL_CLKR(1) | PLL_CLKF(24) | PLL_NO_4;
	break;
	case 288 * MHZ:
		/* 288M: low-band, NR=1, NF=24, NO=2 */
		pll_con = PLL_LOW_BAND | PLL_CLKR(1) | PLL_CLKF(24) | PLL_NO_2;
		break;
	case 300 * MHZ:
		/* 300M: low-band, NR=1, NF=25, NO=2 */
		pll_con = PLL_LOW_BAND | PLL_CLKR(1) | PLL_CLKF(25) | PLL_NO_2;
	break;
	case 624 * MHZ:
		/* 624M: high-band, NR=1, NF=26, NO=1 */
		pll_con = PLL_HIGH_BAND | PLL_CLKR(1) | PLL_CLKF(26) | PLL_NO_1;
		break;
	default:
		return -ENOENT;
		break;
	}

	/* enter slow mode */
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | CRU_GENERAL_MODE_SLOW, CRU_MODE_CON);

	/* power down */
	cru_writel(cru_readl(CRU_GPLL_CON) | PLL_PD, CRU_GPLL_CON);

	delay_500ns();

	cru_writel(pll_con | PLL_PD, CRU_GPLL_CON);

	delay_500ns();

	/* power up */
	cru_writel(pll_con, CRU_GPLL_CON);

	delay_300us();
	pll_wait_lock(GENERAL_PLL_IDX);

	/* enter normal mode */
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_GENERAL_MODE_MASK) | CRU_GENERAL_MODE_NORMAL, CRU_MODE_CON);

	return 0;
}

static struct clk *general_pll_parents[4] = { &xin24m, &xin27m, &ddr_pll_clk, &codec_pll_clk };

static struct clk general_pll_clk = {
	.name		= "general_pll",
	.parent		= &xin24m,
	.recalc		= general_pll_clk_recalc,
	.set_rate	= general_pll_clk_set_rate,
	.clksel_con	= CRU_MODE_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 9,
	.parents	= general_pll_parents,
};


static struct clk *clk_core_parents[4] = { &arm_pll_clk, &general_pll_clk, &codec_pll_clk, &ddr_pll_clk };

static struct clk clk_core = {
	.name		= "core",
	.parent		= &arm_pll_clk,
	.recalc		= clksel_recalc_div,
	.clksel_con	= CRU_CLKSEL0_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 0,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 23,
	.parents	= clk_core_parents,
};

static unsigned long aclk_cpu_recalc(struct clk *clk)
{
	unsigned long rate;
	u32 div = ((cru_readl(CRU_CLKSEL0_CON) >> 5) & 0x7) + 1;

	BUG_ON(div > 5);
	if (div >= 5)
		div = 8;
	rate = clk->parent->rate / div;
	pr_debug("%s new clock rate is %ld (div %d)\n", clk->name, rate, div);

	return rate;
}

static struct clk aclk_cpu = {
	.name		= "aclk_cpu",
	.parent		= &clk_core,
	.recalc		= aclk_cpu_recalc,
};

static struct clk hclk_cpu = {
	.name		= "hclk_cpu",
	.parent		= &aclk_cpu,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.clksel_con	= CRU_CLKSEL0_CON,
	.clksel_mask	= 3,
	.clksel_shift	= 8,
	.clksel_maxdiv	= 4,
};

static struct clk pclk_cpu = {
	.name		= "pclk_cpu",
	.parent		= &aclk_cpu,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.clksel_con	= CRU_CLKSEL0_CON,
	.clksel_mask	= 3,
	.clksel_shift	= 10,
	.clksel_maxdiv	= 8,
};

static struct clk *aclk_periph_parents[4] = { &general_pll_clk, &arm_pll_clk, &ddr_pll_clk, &codec_pll_clk };

static struct clk aclk_periph = {
	.name		= "aclk_periph",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_ACLK_PEIRPH,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL0_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 14,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 12,
	.parents	= aclk_periph_parents,
};

static struct clk pclk_periph = {
	.name		= "pclk_periph",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_PCLK_PEIRPH,
	.parent		= &aclk_periph,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.clksel_con	= CRU_CLKSEL0_CON,
	.clksel_mask	= 3,
	.clksel_shift	= 19,
	.clksel_maxdiv	= 8,
};

static struct clk hclk_periph = {
	.name		= "hclk_periph",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_HCLK_PEIRPH,
	.parent		= &aclk_periph,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.clksel_con	= CRU_CLKSEL0_CON,
	.clksel_mask	= 3,
	.clksel_shift	= 21,
	.clksel_maxdiv	= 4,
};


static unsigned long uhost_recalc(struct clk *clk)
{
	unsigned long rate = clksel_recalc_div(clk);
	if (rate != 48 * MHZ) {
		clksel_set_rate_div(clk, 48 * MHZ);
		rate = clksel_recalc_div(clk);
	}
	return rate;
}

static struct clk *clk_uhost_parents[8] = { &general_pll_clk, &ddr_pll_clk, &codec_pll_clk, &arm_pll_clk, &otgphy0_clkin, &otgphy1_clkin };

static struct clk clk_uhost = {
	.name		= "uhost",
	.mode		= gate_mode,
	.recalc		= uhost_recalc,
	.set_rate	= clksel_set_rate_div,
	.gate_idx	= CLK_GATE_UHOST,
	.clksel_con	= CRU_CLKSEL1_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 16,
	.clksel_parent_mask	= 7,
	.clksel_parent_shift	= 13,
	.parents	= clk_uhost_parents,
};

static struct clk *clk_otgphy_parents[4] = { &xin24m, &clk_12m, &clk_uhost };

static struct clk clk_otgphy0 = {
	.name		= "otgphy0",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_USBPHY0,
	.clksel_con	= CRU_CLKSEL1_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 9,
	.parents	= clk_otgphy_parents,
};

static struct clk clk_otgphy1 = {
	.name		= "otgphy1",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_USBPHY1,
	.clksel_con	= CRU_CLKSEL1_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 11,
	.parents	= clk_otgphy_parents,
};


static struct clk rmii_clkin = {
	.name		= "rmii_clkin",
};

static struct clk *clk_mac_ref_div_parents[4] = { &arm_pll_clk, &general_pll_clk, &codec_pll_clk, &ddr_pll_clk };

static struct clk clk_mac_ref_div = {
	.name		= "mac_ref_div",
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL1_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 23,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 21,
	.parents	= clk_mac_ref_div_parents,
};

static struct clk *clk_mac_ref_parents[2] = { &clk_mac_ref_div, &rmii_clkin };

static struct clk clk_mac_ref = {
	.name		= "mac_ref",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_MAC_REF,
	.clksel_con	= CRU_CLKSEL1_CON,
	.clksel_parent_mask	= 1,
	.clksel_parent_shift	= 28,
	.parents	= clk_mac_ref_parents,
};


static struct clk *clk_i2s_div_parents[8] = { &codec_pll_clk, &general_pll_clk, &arm_pll_clk, &ddr_pll_clk, &otgphy0_clkin, &otgphy1_clkin };

static struct clk clk_i2s0_div = {
	.name		= "i2s0_div",
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL2_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 3,
	.clksel_parent_mask	= 7,
	.clksel_parent_shift	= 0,
	.parents	= clk_i2s_div_parents,
};

static struct clk clk_i2s1_div = {
	.name		= "i2s1_div",
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL2_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 13,
	.clksel_parent_mask	= 7,
	.clksel_parent_shift	= 10,
	.parents	= clk_i2s_div_parents,
};

static struct clk clk_spdif_div = {
	.name		= "spdif_div",
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL2_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 23,
	.clksel_parent_mask	= 7,
	.clksel_parent_shift	= 20,
	.parents	= clk_i2s_div_parents,
};

static u32 clk_gcd(u32 numerator, u32 denominator)
{
	u32 a, b;

	if (!numerator || !denominator)
		return 0;
	if (numerator > denominator) {
		a = numerator;
		b = denominator;
	} else {
		a = denominator;
		b = numerator;
	}
	while (b != 0) {
		int r = b;
		b = a % b;
		a = r;
	}

	return a;
}

static int clk_i2s_frac_div_set_rate(struct clk *clk, unsigned long rate)
{
	u32 numerator, denominator;
	u32 gcd;

	gcd = clk_gcd(rate, clk->parent->rate);
	pr_debug("i2s rate=%ld,parent=%ld,gcd=%d\n", rate, clk->parent->rate, gcd);
	if (!gcd) {
		pr_err("gcd=0, i2s frac div is not be supported\n");
		return -ENOENT;
	}

	numerator = rate / gcd;
	denominator = clk->parent->rate / gcd;
	pr_debug("i2s numerator=%d,denominator=%d,times=%d\n",
		numerator, denominator, denominator / numerator);
	if (numerator > 0xffff || denominator > 0xffff) {
		pr_err("i2s can't get a available nume and deno\n");
		return -ENOENT;
	}

	pr_debug("set clock %s to rate %ld (%d/%d)\n", clk->name, rate, numerator, denominator);
	cru_writel(numerator << 16 | denominator, clk->clksel_con);

	return 0;
}

static struct clk clk_i2s0_frac_div = {
	.name		= "i2s0_frac_div",
	.parent		= &clk_i2s0_div,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_i2s_frac_div_set_rate,
	.clksel_con	= CRU_CLKSEL3_CON,
};

static struct clk clk_i2s1_frac_div = {
	.name		= "i2s1_frac_div",
	.parent		= &clk_i2s1_div,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_i2s_frac_div_set_rate,
	.clksel_con	= CRU_CLKSEL4_CON,
};

static struct clk clk_spdif_frac_div = {
	.name		= "spdif_frac_div",
	.parent		= &clk_spdif_div,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_i2s_frac_div_set_rate,
	.clksel_con	= CRU_CLKSEL5_CON,
};

static int i2s_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = 0;
	struct clk *parent;

	if (rate == 12 * MHZ) {
		parent = &clk_12m;
	} else {
		parent = clk->parents[1]; /* frac div */
		ret = clk_set_rate_nolock(parent, rate);
		if (ret)
			return ret;
	}
	if (clk->parent != parent)
		ret = clk_set_parent_nolock(clk, parent);

	return ret;
}

static struct clk *clk_i2s0_parents[4] = { &clk_i2s0_div, &clk_i2s0_frac_div, &clk_12m, &xin24m };

static struct clk clk_i2s0 = {
	.name		= "i2s0",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_I2S0,
	.set_rate	= i2s_set_rate,
	.clksel_con	= CRU_CLKSEL2_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 8,
	.parents	= clk_i2s0_parents,
};

static struct clk *clk_i2s1_parents[4] = { &clk_i2s1_div, &clk_i2s1_frac_div, &clk_12m, &xin24m };

static struct clk clk_i2s1 = {
	.name		= "i2s1",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_I2S1,
	.set_rate	= i2s_set_rate,
	.clksel_con	= CRU_CLKSEL2_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 18,
	.parents	= clk_i2s1_parents,
};

static struct clk *clk_spdif_parents[4] = { &clk_spdif_div, &clk_spdif_frac_div, &clk_12m, &xin24m };

static struct clk clk_spdif = {
	.name		= "spdif",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_SPDIF,
	.set_rate	= i2s_set_rate,
	.clksel_con	= CRU_CLKSEL2_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 28,
	.parents	= clk_spdif_parents,
};


static struct clk *clk_spi_src_parents[4] = { &general_pll_clk, &ddr_pll_clk, &codec_pll_clk, &arm_pll_clk };

static struct clk clk_spi_src = {
	.name		= "spi_src",
	.clksel_con	= CRU_CLKSEL6_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 0,
	.parents	= clk_spi_src_parents,
};

static struct clk clk_spi0 = {
	.name		= "spi0",
	.parent		= &clk_spi_src,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.gate_idx	= CLK_GATE_SPI0,
	.clksel_con	= CRU_CLKSEL6_CON,
	.clksel_mask	= 0x7F,
	.clksel_shift	= 2,
};

static struct clk clk_spi1 = {
	.name		= "spi1",
	.parent		= &clk_spi_src,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.gate_idx	= CLK_GATE_SPI1,
	.clksel_con	= CRU_CLKSEL6_CON,
	.clksel_mask	= 0x7F,
	.clksel_shift	= 11,
};


static struct clk clk_saradc = {
	.name		= "saradc",
	.parent		= &pclk_periph,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.gate_idx	= CLK_GATE_SARADC,
	.clksel_con	= CRU_CLKSEL6_CON,
	.clksel_mask	= 0xFF,
	.clksel_shift	= 18,
};


static struct clk *clk_cpu_timer_parents[2] = { &pclk_cpu, &xin24m };

static struct clk clk_timer0 = {
	.name		= "timer0",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_TIMER0,
	.clksel_con	= CRU_CLKSEL6_CON,
	.clksel_parent_mask	= 1,
	.clksel_parent_shift	= 26,
	.parents	= clk_cpu_timer_parents,
};

static struct clk clk_timer1 = {
	.name		= "timer1",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_TIMER1,
	.clksel_con	= CRU_CLKSEL6_CON,
	.clksel_parent_mask	= 1,
	.clksel_parent_shift	= 27,
	.parents	= clk_cpu_timer_parents,
};

static struct clk *clk_periph_timer_parents[2] = { &pclk_periph, &xin24m };

static struct clk clk_timer2 = {
	.name		= "timer2",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_TIMER2,
	.clksel_con	= CRU_CLKSEL6_CON,
	.clksel_parent_mask	= 1,
	.clksel_parent_shift	= 28,
	.parents	= clk_periph_timer_parents,
};

static struct clk clk_timer3 = {
	.name		= "timer3",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_TIMER3,
	.clksel_con	= CRU_CLKSEL6_CON,
	.clksel_parent_mask	= 1,
	.clksel_parent_shift	= 29,
	.parents	= clk_periph_timer_parents,
};


static struct clk *clk_mmc_src_parents[4] = { &arm_pll_clk, &general_pll_clk, &codec_pll_clk, &ddr_pll_clk };

static struct clk clk_mmc_src = {
	.name		= "mmc_src",
	.clksel_con	= CRU_CLKSEL7_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 0,
	.parents	= clk_mmc_src_parents,
};

static struct clk clk_mmc0 = {
	.name		= "mmc0",
	.parent		= &clk_mmc_src,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.gate_idx	= CLK_GATE_MMC0,
	.clksel_con	= CRU_CLKSEL7_CON,
	.clksel_mask	= 0x3F,
	.clksel_shift	= 2,
};

static struct clk clk_mmc1 = {
	.name		= "mmc1",
	.parent		= &clk_mmc_src,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.gate_idx	= CLK_GATE_MMC1,
	.clksel_con	= CRU_CLKSEL7_CON,
	.clksel_mask	= 0x3F,
	.clksel_shift	= 10,
};

static struct clk clk_emmc = {
	.name		= "emmc",
	.parent		= &clk_mmc_src,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.gate_idx	= CLK_GATE_EMMC,
	.clksel_con	= CRU_CLKSEL7_CON,
	.clksel_mask	= 0x3F,
	.clksel_shift	= 18,
};


static struct clk *clk_ddr_parents[8] = { &ddr_pll_clk, &general_pll_clk, &codec_pll_clk, &arm_pll_clk };

static struct clk clk_ddr = {
	.name		= "ddr",
	.recalc		= clksel_recalc_shift,
	.clksel_con	= CRU_CLKSEL7_CON,
	.clksel_mask	= 7,
	.clksel_shift	= 26,
	.clksel_maxdiv	= 32,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 24,
	.parents	= clk_ddr_parents,
};


static int clk_uart_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = 0;
	struct clk *parent;
	struct clk *clk_div = clk->parents[0];

	switch (rate) {
	case 24*MHZ: /* 1.5M/0.5M/50/75/150/200/300/600/1200/2400 */
		parent = clk->parents[2]; /* xin24m */
		break;
	case 19200*16:
	case 38400*16:
	case 57600*16:
	case 115200*16:
	case 230400*16:
	case 460800*16:
	case 576000*16:
		parent = clk->parents[1]; /* frac div */
		/* reset div to 1 */
		ret = clk_set_rate_nolock(clk_div, clk_div->parent->rate);
		if (ret)
			return ret;
		break;
	default:
		parent = clk_div;
		break;
	}

	if (parent->set_rate) {
		ret = clk_set_rate_nolock(parent, rate);
		if (ret)
			return ret;
	}

	if (clk->parent != parent)
		ret = clk_set_parent_nolock(clk, parent);

	return ret;
}

static int clk_uart_frac_div_set_rate(struct clk *clk, unsigned long rate)
{
	u32 numerator, denominator;
	u32 gcd;

	gcd = clk_gcd(rate, clk->parent->rate);
	pr_debug("uart rate=%ld,parent=%ld,gcd=%d\n", rate, clk->parent->rate, gcd);
	if (!gcd) {
		pr_err("gcd=0, uart frac div is not be supported\n");
		return -ENOENT;
	}

	numerator = rate / gcd;
	denominator = clk->parent->rate / gcd;
	pr_debug("uart numerator=%d,denominator=%d,times=%d\n",
		numerator, denominator, denominator / numerator);
	if (numerator > 0xffff || denominator > 0xffff) {
		pr_err("uart_frac can't get a available nume and deno\n");
		return -ENOENT;
	}

	pr_debug("set clock %s to rate %ld (%d/%d)\n", clk->name, rate, numerator, denominator);
	cru_writel(numerator << 16 | denominator, clk->clksel_con);

	return 0;
}

static struct clk *clk_uart_src_parents[8] = { &general_pll_clk, &ddr_pll_clk, &codec_pll_clk, &arm_pll_clk, &otgphy0_clkin, &otgphy1_clkin };

static struct clk clk_uart01_src = {
	.name		= "uart01_src",
	.clksel_con	= CRU_CLKSEL8_CON,
	.clksel_parent_mask	= 7,
	.clksel_parent_shift	= 0,
	.parents	= clk_uart_src_parents,
};

static struct clk clk_uart0_div = {
	.name		= "uart0_div",
	.parent		= &clk_uart01_src,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL8_CON,
	.clksel_mask	= 0x3F,
	.clksel_shift	= 3,
};

static struct clk clk_uart0_frac_div = {
	.name		= "uart0_frac_div",
	.parent		= &clk_uart0_div,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_uart_frac_div_set_rate,
	.clksel_con	= CRU_CLKSEL10_CON,
};

static struct clk *clk_uart0_parents[4] = { &clk_uart0_div, &clk_uart0_frac_div, &xin24m };

static struct clk clk_uart0 = {
	.name		= "uart0",
	.mode		= gate_mode,
	.set_rate	= clk_uart_set_rate,
	.gate_idx	= CLK_GATE_UART0,
	.clksel_con	= CRU_CLKSEL8_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 9,
	.parents	= clk_uart0_parents,
};

static struct clk clk_uart1_div = {
	.name		= "uart1_div",
	.parent		= &clk_uart01_src,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL8_CON,
	.clksel_mask	= 0x3F,
	.clksel_shift	= 14,
};

static struct clk clk_uart1_frac_div = {
	.name		= "uart1_frac_div",
	.parent		= &clk_uart1_div,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_uart_frac_div_set_rate,
	.clksel_con	= CRU_CLKSEL11_CON,
};

static struct clk *clk_uart1_parents[4] = { &clk_uart1_div, &clk_uart1_frac_div, &xin24m };

static struct clk clk_uart1 = {
	.name		= "uart1",
	.mode		= gate_mode,
	.set_rate	= clk_uart_set_rate,
	.gate_idx	= CLK_GATE_UART1,
	.clksel_con	= CRU_CLKSEL8_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 20,
	.parents	= clk_uart1_parents,
};

static struct clk clk_uart23_src = {
	.name		= "uart23_src",
	.clksel_con	= CRU_CLKSEL9_CON,
	.clksel_parent_mask	= 7,
	.clksel_parent_shift	= 0,
	.parents	= clk_uart_src_parents,
};

static struct clk clk_uart2_div = {
	.name		= "uart2_div",
	.parent		= &clk_uart23_src,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL9_CON,
	.clksel_mask	= 0x3F,
	.clksel_shift	= 3,
};

static struct clk clk_uart2_frac_div = {
	.name		= "uart2_frac_div",
	.parent		= &clk_uart2_div,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_uart_frac_div_set_rate,
	.clksel_con	= CRU_CLKSEL12_CON,
};

static struct clk *clk_uart2_parents[4] = { &clk_uart2_div, &clk_uart2_frac_div, &xin24m };

static struct clk clk_uart2 = {
	.name		= "uart2",
	.mode		= gate_mode,
	.set_rate	= clk_uart_set_rate,
	.gate_idx	= CLK_GATE_UART2,
	.clksel_con	= CRU_CLKSEL9_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 9,
	.parents	= clk_uart2_parents,
};

static struct clk clk_uart3_div = {
	.name		= "uart3_div",
	.parent		= &clk_uart23_src,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL9_CON,
	.clksel_mask	= 0x3F,
	.clksel_shift	= 14,
};

static struct clk clk_uart3_frac_div = {
	.name		= "uart3_frac_div",
	.parent		= &clk_uart3_div,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_uart_frac_div_set_rate,
	.clksel_con	= CRU_CLKSEL13_CON,
};

static struct clk *clk_uart3_parents[4] = { &clk_uart3_div, &clk_uart3_frac_div, &xin24m };

static struct clk clk_uart3 = {
	.name		= "uart3",
	.mode		= gate_mode,
	.set_rate	= clk_uart_set_rate,
	.gate_idx	= CLK_GATE_UART3,
	.clksel_con	= CRU_CLKSEL9_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 20,
	.parents	= clk_uart3_parents,
};


static struct clk *clk_hsadc_div_parents[8] = { &codec_pll_clk, &ddr_pll_clk, &general_pll_clk, &arm_pll_clk, &otgphy0_clkin, &otgphy1_clkin };

static struct clk clk_hsadc_div = {
	.name		= "hsadc_div",
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL14_CON,
	.clksel_mask	= 0xFF,
	.clksel_shift	= 10,
	.clksel_parent_mask	= 7,
	.clksel_parent_shift	= 7,
	.parents	= clk_hsadc_div_parents,
};

static struct clk clk_hsadc_frac_div = {
	.name		= "hsadc_frac_div",
	.parent		= &clk_hsadc_div,
	.recalc		= clksel_recalc_frac,
	.clksel_con	= CRU_CLKSEL15_CON,
};

static struct clk *clk_demod_parents[4] = { &clk_hsadc_div, &clk_hsadc_frac_div, &xin27m };

static struct clk clk_demod = {
	.name		= "demod",
	.clksel_con	= CRU_CLKSEL14_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 18,
	.parents	= clk_demod_parents,
};

static struct clk gpsclk = {
	.name		= "gpsclk",
};

static struct clk *clk_hsadc_parents[2] = { &clk_demod, &gpsclk };

static struct clk clk_hsadc = {
	.name		= "hsadc",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_HSADC,
	.clksel_con	= CRU_CLKSEL14_CON,
	.clksel_parent_mask	= 1,
	.clksel_parent_shift	= 21,
	.parents	= clk_hsadc_parents,
};

static unsigned long div2_recalc(struct clk *clk)
{
	return clk->parent->rate >> 1;
}

static struct clk clk_hsadc_div2 = {
	.name		= "hsadc_div2",
	.parent		= &clk_demod,
	.recalc		= div2_recalc,
};

static struct clk clk_hsadc_div2_inv = {
	.name		= "hsadc_div2_inv",
	.parent		= &clk_demod,
	.recalc		= div2_recalc,
};

static struct clk *clk_hsadc_out_parents[2] = { &clk_hsadc_div2, &clk_hsadc_div2_inv };

static struct clk clk_hsadc_out = {
	.name		= "hsadc_out",
	.clksel_con	= CRU_CLKSEL14_CON,
	.clksel_parent_mask	= 1,
	.clksel_parent_shift	= 20,
	.parents	= clk_hsadc_out_parents,
};


static int dclk_lcdc_div_set_rate(struct clk *clk, unsigned long rate)
{
	struct clk *parent;

	switch (rate) {
	case 27000 * KHZ:
	case 74250 * KHZ:
	case 148500 * KHZ:
	case 297 * MHZ:
	case 594 * MHZ:
		parent = &codec_pll_clk;
		break;
	default:
		parent = &general_pll_clk;
		break;
	}
	if (clk->parent != parent)
		clk_set_parent_nolock(clk, parent);

	return clksel_set_rate_div(clk, rate);
}

static struct clk *dclk_lcdc_div_parents[4] = { &codec_pll_clk, &ddr_pll_clk, &general_pll_clk, &arm_pll_clk };

static struct clk dclk_lcdc_div = {
	.name		= "dclk_lcdc_div",
	.recalc		= clksel_recalc_div,
	.set_rate	= dclk_lcdc_div_set_rate,
	.clksel_con	= CRU_CLKSEL16_CON,
	.clksel_mask	= 0xFF,
	.clksel_shift	= 2,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 0,
	.parents	= dclk_lcdc_div_parents,
};

static int dclk_lcdc_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = 0;
	struct clk *parent;

	if (rate == 27 * MHZ && has_xin27m) {
		parent = &xin27m;
	} else {
		parent = &dclk_lcdc_div;
		ret = clk_set_rate_nolock(parent, rate);
		if (ret)
			return ret;
	}
	if (clk->parent != parent)
		ret = clk_set_parent_nolock(clk, parent);

	return ret;
}

static struct clk *dclk_lcdc_parents[2] = { &dclk_lcdc_div, &xin27m };

static struct clk dclk_lcdc = {
	.name		= "dclk_lcdc",
	.mode		= gate_mode,
	.set_rate	= dclk_lcdc_set_rate,
	.gate_idx	= CLK_GATE_DCLK_LCDC,
	.clksel_con	= CRU_CLKSEL16_CON,
	.clksel_parent_mask	= 1,
	.clksel_parent_shift	= 10,
	.parents	= dclk_lcdc_parents,
};

static struct clk dclk_ebook = {
	.name		= "dclk_ebook",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_DCLK_EBOOK,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL16_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 13,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 11,
	.parents	= dclk_lcdc_div_parents,
};

static struct clk *aclk_lcdc_parents[4] = { &ddr_pll_clk, &codec_pll_clk, &general_pll_clk, &arm_pll_clk };

static struct clk aclk_lcdc = {
	.name		= "aclk_lcdc",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_ACLK_LCDC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL16_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 20,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 18,
	.parents	= aclk_lcdc_parents,
};

static struct clk hclk_lcdc = {
	.name		= "hclk_lcdc",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_HCLK_LCDC,
	.parent		= &aclk_lcdc,
	.clksel_con	= CRU_CLKSEL16_CON,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.clksel_mask	= 3,
	.clksel_shift	= 25,
	.clksel_maxdiv	= 4,
};

/* for vpu power on notify */
static struct clk clk_vpu = {
	.name		= "vpu",
};

static struct clk *xpu_parents[4] = { &general_pll_clk, &ddr_pll_clk, &codec_pll_clk, &arm_pll_clk };

static struct clk aclk_vepu = {
	.name		= "aclk_vepu",
	.mode		= gate_mode,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.gate_idx	= CLK_GATE_ACLK_VEPU,
	.clksel_con	= CRU_CLKSEL17_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 2,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 0,
	.parents	= xpu_parents,
};

static struct clk hclk_vepu = {
	.name		= "hclk_vepu",
	.parent		= &aclk_vepu,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.gate_idx	= CLK_GATE_HCLK_VEPU,
	.clksel_con	= CRU_CLKSEL17_CON,
	.clksel_mask	= 3,
	.clksel_shift	= 28,
	.clksel_maxdiv	= 4,
};

static struct clk aclk_vdpu = {
	.name		= "aclk_vdpu",
	.parent		= &general_pll_clk,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.gate_idx	= CLK_GATE_ACLK_VDPU,
	.clksel_con	= CRU_CLKSEL17_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 9,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 7,
	.parents	= xpu_parents,
};

static struct clk hclk_vdpu = {
	.name		= "hclk_vdpu",
	.parent		= &aclk_vdpu,
	.mode		= gate_mode,
	.recalc		= clksel_recalc_shift,
	.set_rate	= clksel_set_rate_shift,
	.gate_idx	= CLK_GATE_HCLK_VDPU,
	.clksel_con	= CRU_CLKSEL17_CON,
	.clksel_mask	= 3,
	.clksel_shift	= 30,
	.clksel_maxdiv	= 4,
};

static int clk_gpu_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long max_rate = rate / 100 * 105;	/* +5% */
	struct clk *parents[] = { &general_pll_clk, &codec_pll_clk, &ddr_pll_clk };
	int i;
	unsigned long best_rate = 0;
	struct clk *best_parent = clk->parent;

	for (i = 0; i < ARRAY_SIZE(parents); i++) {
		unsigned long new_rate = clksel_round_rate_div_by_parent(clk, rate, parents[i], max_rate);
		if (new_rate == rate) {
			best_rate = new_rate;
			best_parent = parents[i];
			break;
		}
		if (new_rate > max_rate)
			continue;
		if (new_rate > best_rate) {
			best_rate = new_rate;
			best_parent = parents[i];
		}
	}
	if (!best_rate)
		return -ENOENT;
	if (best_parent != clk->parent)
		clk_set_parent_nolock(clk, best_parent);
	return clksel_set_rate_div(clk, best_rate);
}

static struct clk clk_gpu = {
	.name		= "gpu",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_GPU,
	.recalc		= clksel_recalc_div,
	.set_rate	= clk_gpu_set_rate,
	.clksel_con	= CRU_CLKSEL17_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 16,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 14,
	.parents	= xpu_parents,
};

static struct clk aclk_gpu = {
	.name		= "aclk_gpu",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_ACLK_GPU,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_div,
	.clksel_con	= CRU_CLKSEL17_CON,
	.clksel_mask	= 0x1F,
	.clksel_shift	= 23,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 21,
	.parents	= xpu_parents,
};


static struct clk vip_clkin = {
	.name		= "vip_clkin",
};

static struct clk *clk_vip_parents[4] = { &xin24m, &xin27m, &dclk_ebook };

static struct clk clk_vip_out = {
	.name		= "vip_out",
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_VIP_OUT,
	.clksel_con	= CRU_CLKSEL1_CON,
	.clksel_parent_mask	= 3,
	.clksel_parent_shift	= 7,
	.parents	= clk_vip_parents,
};


#define GATE_CLK(NAME,PARENT,ID) \
static struct clk clk_##NAME = { \
	.name		= #NAME, \
	.parent		= &PARENT, \
	.mode		= gate_mode, \
	.gate_idx	= CLK_GATE_##ID, \
}

GATE_CLK(i2c0, pclk_cpu, I2C0);
GATE_CLK(i2c1, pclk_periph, I2C1);
GATE_CLK(i2c2, pclk_periph, I2C2);
GATE_CLK(i2c3, pclk_periph, I2C3);

GATE_CLK(gpio0, pclk_cpu, GPIO0);
GATE_CLK(gpio1, pclk_periph, GPIO1);
GATE_CLK(gpio2, pclk_periph, GPIO2);
GATE_CLK(gpio3, pclk_periph, GPIO3);
GATE_CLK(gpio4, pclk_cpu, GPIO4);
GATE_CLK(gpio5, pclk_periph, GPIO5);
GATE_CLK(gpio6, pclk_cpu, GPIO6);

GATE_CLK(dma1, aclk_cpu, DMA1);
GATE_CLK(dma2, aclk_periph, DMA2);

GATE_CLK(gic, aclk_cpu, GIC);
GATE_CLK(intmem, aclk_cpu, INTMEM);
GATE_CLK(rom, hclk_cpu, ROM);
GATE_CLK(ddr_phy, aclk_cpu, DDR_PHY);
GATE_CLK(ddr_reg, aclk_cpu, DDR_REG);
GATE_CLK(ddr_cpu, aclk_cpu, DDR_CPU);
GATE_CLK(efuse, pclk_cpu, EFUSE);
GATE_CLK(tzpc, pclk_cpu, TZPC);
GATE_CLK(debug, pclk_cpu, DEBUG);
GATE_CLK(tpiu, pclk_cpu, TPIU);
GATE_CLK(rtc, pclk_cpu, RTC);
GATE_CLK(pmu, pclk_cpu, PMU);
GATE_CLK(grf, pclk_cpu, GRF);

GATE_CLK(emem, hclk_periph, EMEM);
GATE_CLK(hclk_usb_peri, hclk_periph, HCLK_USB_PERI);
GATE_CLK(aclk_ddr_peri, aclk_periph, ACLK_DDR_PERI);
GATE_CLK(aclk_cpu_peri, aclk_cpu, ACLK_CPU_PERI);
GATE_CLK(aclk_smc, aclk_periph, ACLK_SMC);
GATE_CLK(smc, pclk_periph, SMC);
GATE_CLK(hclk_mac, hclk_periph, HCLK_MAC);
GATE_CLK(mii_tx, hclk_periph, MII_TX);
GATE_CLK(mii_rx, hclk_periph, MII_RX);
GATE_CLK(hif, hclk_periph, HIF);
GATE_CLK(nandc, hclk_periph, NANDC);
GATE_CLK(hclk_hsadc, hclk_periph, HCLK_HSADC);
GATE_CLK(usbotg0, hclk_periph, USBOTG0);
GATE_CLK(usbotg1, hclk_periph, USBOTG1);
GATE_CLK(hclk_uhost, hclk_periph, HCLK_UHOST);
GATE_CLK(pid_filter, hclk_periph, PID_FILTER);

GATE_CLK(vip_slave, hclk_lcdc, VIP_SLAVE);
GATE_CLK(wdt, pclk_periph, WDT);
GATE_CLK(pwm, pclk_periph, PWM);
GATE_CLK(vip_bus, aclk_cpu, VIP_BUS);
GATE_CLK(vip_matrix, clk_vip_bus, VIP_MATRIX);
GATE_CLK(vip_input, vip_clkin, VIP_INPUT);
GATE_CLK(jtag, aclk_cpu, JTAG);

GATE_CLK(aclk_ddr_lcdc, aclk_lcdc, ACLK_DDR_LCDC);
GATE_CLK(aclk_ipp, aclk_lcdc, ACLK_IPP);
GATE_CLK(hclk_ipp, hclk_lcdc, HCLK_IPP);
GATE_CLK(hclk_ebook, hclk_lcdc, HCLK_EBOOK);
GATE_CLK(aclk_disp_matrix, aclk_lcdc, ACLK_DISP_MATRIX);
GATE_CLK(hclk_disp_matrix, hclk_lcdc, HCLK_DISP_MATRIX);
GATE_CLK(aclk_ddr_vepu, aclk_vepu, ACLK_DDR_VEPU);
GATE_CLK(aclk_ddr_vdpu, aclk_vdpu, ACLK_DDR_VDPU);
GATE_CLK(aclk_ddr_gpu, aclk_gpu, ACLK_DDR_GPU);
GATE_CLK(hclk_gpu, hclk_cpu, HCLK_GPU);
GATE_CLK(hclk_cpu_vcodec, hclk_cpu, HCLK_CPU_VCODEC);
GATE_CLK(hclk_cpu_display, hclk_cpu, HCLK_CPU_DISPLAY);

GATE_CLK(hclk_mmc0, hclk_periph, HCLK_MMC0);
GATE_CLK(hclk_mmc1, hclk_periph, HCLK_MMC1);
GATE_CLK(hclk_emmc, hclk_periph, HCLK_EMMC);


static void __sramfunc pmu_set_power_domain_sram(enum pmu_power_domain pd, bool on)
{
	if (on)
		writel(readl(RK29_PMU_BASE + PMU_PD_CON) & ~(1 << pd), RK29_PMU_BASE + PMU_PD_CON);
	else
		writel(readl(RK29_PMU_BASE + PMU_PD_CON) |  (1 << pd), RK29_PMU_BASE + PMU_PD_CON);
	dsb();

	while (pmu_power_domain_is_on(pd) != on)
		;
}

static noinline void do_pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	static unsigned long save_sp;

	DDR_SAVE_SP(save_sp);
	pmu_set_power_domain_sram(pd, on);
	DDR_RESTORE_SP(save_sp);
}

void pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	unsigned long flags;

	mdelay(10);
	local_irq_save(flags);
	do_pmu_set_power_domain(pd, on);
	local_irq_restore(flags);
	mdelay(10);
}
EXPORT_SYMBOL(pmu_set_power_domain);

static int pd_vcodec_mode(struct clk *clk, int on)
{
	if (on) {
		u32 gate;

		gate = cru_clkgate3_con_mirror;
		gate |= (1 << CLK_GATE_ACLK_DDR_VEPU % 32);
		gate &= ~((1 << CLK_GATE_ACLK_VEPU % 32)
			| (1 << CLK_GATE_HCLK_VEPU % 32)
			| (1 << CLK_GATE_HCLK_CPU_VCODEC % 32));
		cru_writel(gate, CRU_CLKGATE3_CON);

		pmu_set_power_domain(PD_VCODEC, true);

		cru_writel(cru_clkgate3_con_mirror, CRU_CLKGATE3_CON);
	} else {
		pmu_set_power_domain(PD_VCODEC, false);
	}

	return 0;
}

static struct clk pd_vcodec = {
	.name	= "pd_vcodec",
	.flags  = IS_PD,
	.mode	= pd_vcodec_mode,
	.gate_idx	= PD_VCODEC,
};

static int pd_display_mode(struct clk *clk, int on)
{
#if 0	/* display power domain is buggy, always keep it on.  */
	if (on) {
		u32 gate, gate2;

		gate = cru_clkgate3_con_mirror;
		gate |= (1 << CLK_GATE_ACLK_DDR_LCDC % 32);
		gate &= ~((1 << CLK_GATE_HCLK_CPU_DISPLAY % 32)
			| (1 << CLK_GATE_HCLK_DISP_MATRIX % 32)
			| (1 << CLK_GATE_ACLK_DISP_MATRIX % 32)
			| (1 << CLK_GATE_DCLK_EBOOK % 32)
			| (1 << CLK_GATE_HCLK_EBOOK % 32)
			| (1 << CLK_GATE_HCLK_IPP % 32)
			| (1 << CLK_GATE_ACLK_IPP % 32)
			| (1 << CLK_GATE_DCLK_LCDC % 32)
			| (1 << CLK_GATE_HCLK_LCDC % 32)
			| (1 << CLK_GATE_ACLK_LCDC % 32));
		cru_writel(gate, CRU_CLKGATE3_CON);

		gate2 = cru_readl(CRU_CLKGATE2_CON);
		gate = gate2;
		gate &= ~((1 << CLK_GATE_VIP_OUT % 32)
			| (1 << CLK_GATE_VIP_SLAVE % 32)
			| (1 << CLK_GATE_VIP_MATRIX % 32)
			| (1 << CLK_GATE_VIP_BUS % 32));
		cru_writel(gate, CRU_CLKGATE2_CON);

		pmu_set_power_domain(PD_DISPLAY, true);

		cru_writel(gate2, CRU_CLKGATE2_CON);
		cru_writel(cru_clkgate3_con_mirror, CRU_CLKGATE3_CON);
	} else {
		pmu_set_power_domain(PD_DISPLAY, false);
	}
#endif

	return 0;
}

static struct clk pd_display = {
	.name	= "pd_display",
	.flags  = IS_PD,
	.mode	= pd_display_mode,
	.gate_idx	= PD_DISPLAY,
};

static int pd_gpu_mode(struct clk *clk, int on)
{
	if (on) {
		pmu_set_power_domain(PD_GPU, true);
	} else {
		pmu_set_power_domain(PD_GPU, false);
	}

	return 0;
}

static struct clk pd_gpu = {
	.name	= "pd_gpu",
	.flags  = IS_PD,
	.mode	= pd_gpu_mode,
	.gate_idx	= PD_GPU,
};


#define CLK(dev, con, ck) \
	{ \
		.dev_id = dev, \
		.con_id = con, \
		.clk = ck, \
	}

#define CLK1(name) \
	{ \
		.dev_id = NULL, \
		.con_id = #name, \
		.clk = &clk_##name, \
	}

static struct clk_lookup clks[] = {
	CLK(NULL, "xin24m", &xin24m),
	CLK(NULL, "xin27m", &xin27m),
	CLK(NULL, "otgphy0_clkin", &otgphy0_clkin),
	CLK(NULL, "otgphy1_clkin", &otgphy1_clkin),
	CLK(NULL, "gpsclk", &gpsclk),
	CLK(NULL, "vip_clkin", &vip_clkin),

	CLK1(12m),
	CLK(NULL, "arm_pll", &arm_pll_clk),
	CLK(NULL, "ddr_pll", &ddr_pll_clk),
	CLK(NULL, "codec_pll", &codec_pll_clk),
	CLK(NULL, "general_pll", &general_pll_clk),

	CLK1(core),
	CLK(NULL, "aclk_cpu", &aclk_cpu),
	CLK(NULL, "hclk_cpu", &hclk_cpu),
	CLK(NULL, "pclk_cpu", &pclk_cpu),

	CLK(NULL, "aclk_periph", &aclk_periph),
	CLK(NULL, "hclk_periph", &hclk_periph),
	CLK(NULL, "pclk_periph", &pclk_periph),

	CLK1(vip_out),
	CLK1(otgphy0),
	CLK1(otgphy1),
	CLK1(uhost),
	CLK1(mac_ref_div),
	CLK1(mac_ref),

	CLK("rk29_i2s.0", "i2s_div", &clk_i2s0_div),
	CLK("rk29_i2s.0", "i2s_frac_div", &clk_i2s0_frac_div),
	CLK("rk29_i2s.0", "i2s", &clk_i2s0),
	CLK("rk29_i2s.1", "i2s_div", &clk_i2s1_div),
	CLK("rk29_i2s.1", "i2s_frac_div", &clk_i2s1_frac_div),
	CLK("rk29_i2s.1", "i2s", &clk_i2s1),
	CLK(NULL, "spdif_div", &clk_spdif_div),
	CLK(NULL, "spdif_frac_div", &clk_spdif_frac_div),
	CLK(NULL, "spdif", &clk_spdif),

	CLK1(spi_src),
	CLK("rk29xx_spim.0", "spi", &clk_spi0),
	CLK("rk29xx_spim.1", "spi", &clk_spi1),

	CLK1(saradc),
	CLK1(timer0),
	CLK1(timer1),
	CLK1(timer2),
	CLK1(timer3),

	CLK1(mmc_src),
	CLK("rk29_sdmmc.0", "mmc", &clk_mmc0),
	CLK("rk29_sdmmc.0", "hclk_mmc", &clk_hclk_mmc0),
	CLK("rk29_sdmmc.1", "mmc", &clk_mmc1),
	CLK("rk29_sdmmc.1", "hclk_mmc", &clk_hclk_mmc1),
	CLK1(emmc),
	CLK1(hclk_emmc),
	CLK1(ddr),

	CLK1(uart01_src),
	CLK("rk29_serial.0", "uart", &clk_uart0),
	CLK("rk29_serial.0", "uart_div", &clk_uart0_div),
	CLK("rk29_serial.0", "uart_frac_div", &clk_uart0_frac_div),
	CLK("rk29_serial.1", "uart", &clk_uart1),
	CLK("rk29_serial.1", "uart_div", &clk_uart1_div),
	CLK("rk29_serial.1", "uart_frac_div", &clk_uart1_frac_div),

	CLK1(uart23_src),
	CLK("rk29_serial.2", "uart", &clk_uart2),
	CLK("rk29_serial.2", "uart_div", &clk_uart2_div),
	CLK("rk29_serial.2", "uart_frac_div", &clk_uart2_frac_div),
	CLK("rk29_serial.3", "uart", &clk_uart3),
	CLK("rk29_serial.3", "uart_div", &clk_uart3_div),
	CLK("rk29_serial.3", "uart_frac_div", &clk_uart3_frac_div),

	CLK1(hsadc_div),
	CLK1(hsadc_frac_div),
	CLK1(demod),
	CLK1(hsadc),
	CLK1(hsadc_div2),
	CLK1(hsadc_div2_inv),
	CLK1(hsadc_out),

	CLK(NULL, "dclk_lcdc_div", &dclk_lcdc_div),
	CLK(NULL, "dclk_lcdc", &dclk_lcdc),
	CLK(NULL, "dclk_ebook", &dclk_ebook),
	CLK(NULL, "aclk_lcdc", &aclk_lcdc),
	CLK(NULL, "hclk_lcdc", &hclk_lcdc),

	CLK1(vpu),
	CLK(NULL, "aclk_vepu", &aclk_vepu),
	CLK(NULL, "hclk_vepu", &hclk_vepu),
	CLK(NULL, "aclk_vdpu", &aclk_vdpu),
	CLK(NULL, "hclk_vdpu", &hclk_vdpu),
	CLK1(gpu),
	CLK(NULL, "aclk_gpu", &aclk_gpu),

	CLK("rk29_i2c.0", "i2c", &clk_i2c0),
	CLK("rk29_i2c.1", "i2c", &clk_i2c1),
	CLK("rk29_i2c.2", "i2c", &clk_i2c2),
	CLK("rk29_i2c.3", "i2c", &clk_i2c3),

	CLK1(gpio0),
	CLK1(gpio1),
	CLK1(gpio2),
	CLK1(gpio3),
	CLK1(gpio4),
	CLK1(gpio5),
	CLK1(gpio6),

	CLK1(dma1),
	CLK1(dma2),

	CLK1(gic),
	CLK1(intmem),
	CLK1(rom),
	CLK1(ddr_phy),
	CLK1(ddr_reg),
	CLK1(ddr_cpu),
	CLK1(efuse),
	CLK1(tzpc),
	CLK1(debug),
	CLK1(tpiu),
	CLK1(rtc),
	CLK1(pmu),
	CLK1(grf),

	CLK1(emem),
	CLK1(hclk_usb_peri),
	CLK1(aclk_ddr_peri),
	CLK1(aclk_cpu_peri),
	CLK1(aclk_smc),
	CLK1(smc),
	CLK1(hclk_mac),
	CLK1(mii_tx),
	CLK1(mii_rx),
	CLK1(hif),
	CLK1(nandc),
	CLK1(hclk_hsadc),
	CLK1(usbotg0),
	CLK1(usbotg1),
	CLK1(hclk_uhost),
	CLK1(pid_filter),

	CLK1(vip_slave),
	CLK1(wdt),
	CLK1(pwm),
	CLK1(vip_bus),
	CLK1(vip_matrix),
	CLK1(vip_input),
	CLK1(jtag),

	CLK1(aclk_ddr_lcdc),
	CLK1(aclk_ipp),
	CLK1(hclk_ipp),
	CLK1(hclk_ebook),
	CLK1(aclk_disp_matrix),
	CLK1(hclk_disp_matrix),
	CLK1(aclk_ddr_vepu),
	CLK1(aclk_ddr_vdpu),
	CLK1(aclk_ddr_gpu),
	CLK1(hclk_gpu),
	CLK1(hclk_cpu_vcodec),
	CLK1(hclk_cpu_display),

	CLK(NULL, "pd_vcodec", &pd_vcodec),
	CLK(NULL, "pd_display", &pd_display),
	CLK(NULL, "pd_gpu", &pd_gpu),
};

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);
#define LOCK() do { WARN_ON(in_irq()); if (!irqs_disabled()) spin_lock_bh(&clockfw_lock); } while (0)
#define UNLOCK() do { if (!irqs_disabled()) spin_unlock_bh(&clockfw_lock); } while (0)

static int clk_enable_nolock(struct clk *clk)
{
	int ret = 0;

	if (clk->usecount == 0) {
		if (clk->parent) {
			ret = clk_enable_nolock(clk->parent);
			if (ret)
				return ret;
		}

		if (clk->notifier_count)
			clk_notify(clk, CLK_PRE_ENABLE, clk->rate, clk->rate);
		if (clk->mode)
			ret = clk->mode(clk, 1);
		if (clk->notifier_count)
			clk_notify(clk, ret ? CLK_ABORT_ENABLE : CLK_POST_ENABLE, clk->rate, clk->rate);
		if (ret) {
			if (clk->parent)
				clk_disable_nolock(clk->parent);
			return ret;
		}
		pr_debug("%s enabled\n", clk->name);
	}
	clk->usecount++;

	return ret;
}

int clk_enable(struct clk *clk)
{
	int ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	LOCK();
	ret = clk_enable_nolock(clk);
	UNLOCK();

	return ret;
}
EXPORT_SYMBOL(clk_enable);

static void clk_disable_nolock(struct clk *clk)
{
	if (clk->usecount == 0) {
		printk(KERN_ERR "Trying disable clock %s with 0 usecount\n", clk->name);
		WARN_ON(1);
		return;
	}

	if (--clk->usecount == 0) {
		int ret = 0;
		if (clk->notifier_count)
			clk_notify(clk, CLK_PRE_DISABLE, clk->rate, clk->rate);
		if (clk->mode)
			ret = clk->mode(clk, 0);
		if (clk->notifier_count)
			clk_notify(clk, ret ? CLK_ABORT_DISABLE : CLK_POST_DISABLE, clk->rate, clk->rate);
		pr_debug("%s disabled\n", clk->name);
		if (ret == 0 && clk->parent)
			clk_disable_nolock(clk->parent);
	}
}

void clk_disable(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	LOCK();
	clk_disable_nolock(clk);
	UNLOCK();
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return 0;

	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

/*-------------------------------------------------------------------------
 * Optional clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/

/* Given a clock and a rate apply a clock specific rounding function */
static long clk_round_rate_nolock(struct clk *clk, unsigned long rate)
{
	if (clk->round_rate)
		return clk->round_rate(clk, rate);

	if (clk->flags & RATE_FIXED)
		printk(KERN_ERR "clock: clk_round_rate called on fixed-rate clock %s\n", clk->name);

	return clk->rate;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	long ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	LOCK();
	ret = clk_round_rate_nolock(clk, rate);
	UNLOCK();

	return ret;
}
EXPORT_SYMBOL(clk_round_rate);

static void __clk_recalc(struct clk *clk)
{
	if (unlikely(clk->flags & RATE_FIXED))
		return;
	if (clk->recalc)
		clk->rate = clk->recalc(clk);
	else if (clk->parent)
		clk->rate = clk->parent->rate;
	pr_debug("%s new clock rate is %lu\n", clk->name, clk->rate);
}

static int clk_set_rate_nolock(struct clk *clk, unsigned long rate)
{
	int ret;
	unsigned long old_rate;

	if (rate == clk->rate)
		return 0;

	pr_debug("set_rate for clock %s to rate %ld\n", clk->name, rate);

	if (clk->flags & CONFIG_PARTICIPANT)
		return -EINVAL;

	if (!clk->set_rate)
		return -EINVAL;

	old_rate = clk->rate;
	if (clk->notifier_count)
		clk_notify(clk, CLK_PRE_RATE_CHANGE, old_rate, rate);

	ret = clk->set_rate(clk, rate);

	if (ret == 0) {
		__clk_recalc(clk);
		__propagate_rate(clk);
	}

	if (clk->notifier_count)
		clk_notify(clk, ret ? CLK_ABORT_RATE_CHANGE : CLK_POST_RATE_CHANGE, old_rate, clk->rate);

	return ret;
}

/* Set the clock rate for a clock source */
int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	LOCK();
	ret = clk_set_rate_nolock(clk, rate);
	UNLOCK();

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

static int clk_set_parent_nolock(struct clk *clk, struct clk *parent)
{
	int ret;
	int enabled = clk->usecount > 0;
	struct clk *old_parent = clk->parent;

	if (clk->parent == parent)
		return 0;

	/* if clk is already enabled, enable new parent first and disable old parent later. */
	if (enabled)
		clk_enable_nolock(parent);

	if (clk->set_parent)
		ret = clk->set_parent(clk, parent);
	else
		ret = clksel_set_parent(clk, parent);

	if (ret == 0) {
		/* OK */
		__clk_reparent(clk, parent);
		__clk_recalc(clk);
		__propagate_rate(clk);
		if (enabled)
			clk_disable_nolock(old_parent);
	} else {
		if (enabled)
			clk_disable_nolock(parent);
	}

	return ret;
}

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk) || parent == NULL || IS_ERR(parent))
		return ret;

	if (clk->set_parent == NULL && clk->parents == NULL)
		return ret;

	LOCK();
	if (clk->usecount == 0)
		ret = clk_set_parent_nolock(clk, parent);
	else
		ret = -EBUSY;
	UNLOCK();

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);

static void __clk_reparent(struct clk *child, struct clk *parent)
{
	if (child->parent == parent)
		return;
	pr_debug("%s reparent to %s (was %s)\n", child->name, parent->name, ((child->parent) ? child->parent->name : "NULL"));

	list_del_init(&child->sibling);
	if (parent)
		list_add(&child->sibling, &parent->children);
	child->parent = parent;
}

/* Propagate rate to children */
static void __propagate_rate(struct clk *tclk)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &tclk->children, sibling) {
		__clk_recalc(clkp);
		__propagate_rate(clkp);
	}
}

static LIST_HEAD(root_clks);

/**
 * recalculate_root_clocks - recalculate and propagate all root clocks
 *
 * Recalculates all root clocks (clocks with no parent), which if the
 * clock's .recalc is set correctly, should also propagate their rates.
 * Called at init.
 */
static void clk_recalculate_root_clocks_nolock(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &root_clks, sibling) {
		__clk_recalc(clkp);
		__propagate_rate(clkp);
	}
}

void clk_recalculate_root_clocks(void)
{
	LOCK();
	clk_recalculate_root_clocks_nolock();
	UNLOCK();
}


/**
 * clk_preinit - initialize any fields in the struct clk before clk init
 * @clk: struct clk * to initialize
 *
 * Initialize any struct clk fields needed before normal clk initialization
 * can run.  No return value.
 */
static void clk_preinit(struct clk *clk)
{
	INIT_LIST_HEAD(&clk->children);
}

static int clk_register(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	/*
	 * trap out already registered clocks
	 */
	if (clk->node.next || clk->node.prev)
		return 0;

	mutex_lock(&clocks_mutex);

	if (clk->get_parent)
		clk->parent = clk->get_parent(clk);
	else if (clk->parents)
		clk->parent = clksel_get_parent(clk);

	if (clk->parent)
		list_add(&clk->sibling, &clk->parent->children);
	else
		list_add(&clk->sibling, &root_clks);

	list_add(&clk->node, &clocks);

	mutex_unlock(&clocks_mutex);

	return 0;
}

static unsigned int __initdata armclk = 300 * MHZ;

/*
 * You can override arm_clk rate with armclk= cmdline option.
 */
static int __init armclk_setup(char *str)
{
	get_option(&str, &armclk);

	if (!armclk)
		return 0;

	if (armclk < 10000)
		armclk *= MHZ;

	clk_set_rate_nolock(&arm_pll_clk, armclk);
	return 0;
}
early_param("armclk", armclk_setup);

static void __init rk29_clock_common_init(unsigned long ppll_rate, unsigned long cpll_rate)
{
	unsigned long aclk_p, hclk_p, pclk_p;
	struct clk *aclk_vepu_parent, *aclk_vdpu_parent, *aclk_gpu_parent;

	/* general pll */
	switch (ppll_rate) {
	case 96 * MHZ:
		aclk_p = 96 * MHZ;
		hclk_p = 48 * MHZ;
		pclk_p = 24 * MHZ;
		aclk_gpu_parent = aclk_vdpu_parent = aclk_vepu_parent = &codec_pll_clk;
		break;
	case 144 * MHZ:
		aclk_p = 144 * MHZ;
		hclk_p = 72 * MHZ;
		pclk_p = 36 * MHZ;
		aclk_gpu_parent = aclk_vdpu_parent = aclk_vepu_parent = &codec_pll_clk;
		break;
	default:
		ppll_rate = 288 * MHZ;
	case 288 * MHZ:
	case 300 * MHZ:
		aclk_p = ppll_rate / 2;
		hclk_p = ppll_rate / 2;
		pclk_p = ppll_rate / 8;
		aclk_gpu_parent = aclk_vdpu_parent = aclk_vepu_parent = &general_pll_clk;
		break;
	}

	clk_set_rate_nolock(&general_pll_clk, ppll_rate);
	lpj_gpll = div_u64(LPJ_600MHZ * general_pll_clk.rate, 600 * MHZ);
	clk_set_parent_nolock(&aclk_periph, &general_pll_clk);
	clk_set_rate_nolock(&aclk_periph, aclk_p);
	clk_set_rate_nolock(&hclk_periph, hclk_p);
	clk_set_rate_nolock(&pclk_periph, pclk_p);
	clk_set_parent_nolock(&clk_uhost, &general_pll_clk);
	clk_set_rate_nolock(&clk_uhost, 48 * MHZ);
	if (clk_uhost.rate != 48 * MHZ)
		clk_set_parent_nolock(&clk_uhost, &otgphy1_clkin);
	clk_set_parent_nolock(&clk_i2s0_div, &general_pll_clk);
	clk_set_parent_nolock(&clk_i2s1_div, &general_pll_clk);
	clk_set_parent_nolock(&clk_spdif_div, &general_pll_clk);
	clk_set_parent_nolock(&clk_spi_src, &general_pll_clk);
	clk_set_rate_nolock(&clk_spi0, 40 * MHZ);
	clk_set_rate_nolock(&clk_spi1, 40 * MHZ);
	clk_set_parent_nolock(&clk_mmc_src, &general_pll_clk);
	clk_set_parent_nolock(&clk_uart01_src, &general_pll_clk);
	clk_set_parent_nolock(&clk_uart23_src, &general_pll_clk);
	clk_set_parent_nolock(&dclk_lcdc_div, &general_pll_clk);
	clk_set_parent_nolock(&clk_mac_ref_div, &general_pll_clk);
	clk_set_parent_nolock(&clk_hsadc_div, &general_pll_clk);

	/* codec pll */
	clk_set_rate_nolock(&codec_pll_clk, cpll_rate);
	clk_set_parent_nolock(&clk_gpu, &codec_pll_clk);

	/* ddr pll */
	clk_set_parent_nolock(&aclk_lcdc, &ddr_pll_clk);

	/* arm pll */
	clk_set_rate_nolock(&arm_pll_clk, armclk);

	/*you can choose clk parent form codec pll or periph pll for following logic*/
	clk_set_parent_nolock(&aclk_vepu, aclk_vepu_parent);
	clk_set_rate_nolock(&aclk_vepu, 300 * MHZ);
	clk_set_rate_nolock(&clk_aclk_ddr_vepu, 300 * MHZ);
	clk_set_rate_nolock(&hclk_vepu, 150 * MHZ);
	clk_set_parent_nolock(&aclk_vdpu, aclk_vdpu_parent);
	clk_set_parent_nolock(&aclk_gpu, aclk_gpu_parent);
	clk_set_rate_nolock(&aclk_gpu, 300 * MHZ);
}

static void __init clk_enable_init_clocks(void)
{
	clk_enable_nolock(&hclk_cpu);
	clk_enable_nolock(&pclk_cpu);
	clk_enable_nolock(&hclk_periph);
	clk_enable_nolock(&pclk_periph);
	clk_enable_nolock(&clk_nandc);
	clk_enable_nolock(&clk_aclk_cpu_peri);
	clk_enable_nolock(&clk_aclk_ddr_peri);
	clk_enable_nolock(&clk_grf);
	clk_enable_nolock(&clk_pmu);
	clk_enable_nolock(&clk_ddr_cpu);
	clk_enable_nolock(&clk_ddr_reg);
	clk_enable_nolock(&clk_ddr_phy);
	clk_enable_nolock(&clk_gic);
	clk_enable_nolock(&clk_dma2);
	clk_enable_nolock(&clk_dma1);
	clk_enable_nolock(&clk_emem);
	clk_enable_nolock(&clk_intmem);
	clk_enable_nolock(&clk_debug);
	clk_enable_nolock(&clk_tpiu);
	clk_enable_nolock(&clk_jtag);
	clk_enable_nolock(&clk_uart1);
}

static int __init clk_disable_unused(void)
{
	struct clk *ck;

	list_for_each_entry(ck, &clocks, node) {
		if (ck->usecount > 0 || ck->mode == NULL || (ck->flags & IS_PD))
			continue;

		LOCK();
		clk_enable_nolock(ck);
		clk_disable_nolock(ck);
		UNLOCK();
	}

	return 0;
}

void __init rk29_clock_init2(enum periph_pll ppll_rate, enum codec_pll cpll_rate, bool _has_xin27m)
{
	struct clk_lookup *lk;

	has_xin27m = _has_xin27m;

	cru_clkgate3_con_mirror = cru_readl(CRU_CLKGATE3_CON);
	cru_softrst0_con_mirror = cru_readl(CRU_SOFTRST0_CON);

	for (lk = clks; lk < clks + ARRAY_SIZE(clks); lk++)
		clk_preinit(lk->clk);

	for (lk = clks; lk < clks + ARRAY_SIZE(clks); lk++) {
		clkdev_add(lk);
		clk_register(lk->clk);
	}

	clk_recalculate_root_clocks_nolock();

	loops_per_jiffy = div_u64(LPJ_600MHZ * arm_pll_clk.rate, 600 * MHZ);

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable_init_clocks();

	/*
	 * Disable any unused clocks left on by the bootloader
	 */
	clk_disable_unused();

	rk29_clock_common_init(ppll_rate, cpll_rate);

	printk(KERN_INFO "Clocking rate (apll/dpll/cpll/gpll/core/aclk_cpu/hclk_cpu/pclk_cpu/aclk_periph/hclk_periph/pclk_periph): %ld/%ld/%ld/%ld/%ld/%ld/%ld/%ld/%ld/%ld/%ld MHz",
	       arm_pll_clk.rate / MHZ, ddr_pll_clk.rate / MHZ, codec_pll_clk.rate / MHZ, general_pll_clk.rate / MHZ, clk_core.rate / MHZ,
	       aclk_cpu.rate / MHZ, hclk_cpu.rate / MHZ, pclk_cpu.rate / MHZ, aclk_periph.rate / MHZ, hclk_periph.rate / MHZ, pclk_periph.rate / MHZ);
	printk(KERN_CONT " (20110909)\n");

	preset_lpj = loops_per_jiffy;
}

void __init rk29_clock_init(enum periph_pll ppll_rate)
{
	rk29_clock_init2(ppll_rate, codec_pll_297mhz, true);
}

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static void dump_clock(struct seq_file *s, struct clk *clk, int deep)
{
	struct clk* ck;
	int i;
	unsigned long rate = clk->rate;

	for (i = 0; i < deep; i++)
		seq_printf(s, "    ");

	seq_printf(s, "%-11s ", clk->name);

	if (clk->flags & IS_PD) {
		seq_printf(s, "%s ", pmu_power_domain_is_on(clk->gate_idx) ? "on " : "off");
	}

	if ((clk->mode == gate_mode) && (clk->gate_idx < CLK_GATE_MAX)) {
		u32 reg;
		int idx = clk->gate_idx;
		u32 v;

		reg = CRU_CLKGATE0_CON;
		reg += (idx >> 5) << 2;
		idx &= 0x1F;

		if (reg == CRU_CLKGATE3_CON)
			v = cru_clkgate3_con_mirror & (1 << idx);
		else
			v = cru_readl(reg) & (1 << idx);

		seq_printf(s, "%s ", v ? "off" : "on ");
	}

	if (clk == &arm_pll_clk) {
		switch (cru_readl(CRU_MODE_CON) & CRU_CPU_MODE_MASK) {
		case CRU_CPU_MODE_SLOW:   seq_printf(s, "slow   "); break;
		case CRU_CPU_MODE_NORMAL: seq_printf(s, "normal "); break;
		case CRU_CPU_MODE_SLOW27: seq_printf(s, "slow27 "); break;
		}
		if (cru_readl(CRU_APLL_CON) & PLL_BYPASS) seq_printf(s, "bypass ");
	} else if (clk == &ddr_pll_clk) {
		switch (cru_readl(CRU_MODE_CON) & CRU_DDR_MODE_MASK) {
		case CRU_DDR_MODE_SLOW:   seq_printf(s, "slow   "); break;
		case CRU_DDR_MODE_NORMAL: seq_printf(s, "normal "); break;
		case CRU_DDR_MODE_SLOW27: seq_printf(s, "slow27 "); break;
		}
		if (cru_readl(CRU_DPLL_CON) & PLL_BYPASS) seq_printf(s, "bypass ");
	} else if (clk == &codec_pll_clk) {
		switch (cru_readl(CRU_MODE_CON) & CRU_CODEC_MODE_MASK) {
		case CRU_CODEC_MODE_SLOW:   seq_printf(s, "slow   "); break;
		case CRU_CODEC_MODE_NORMAL: seq_printf(s, "normal "); break;
		case CRU_CODEC_MODE_SLOW27: seq_printf(s, "slow27 "); break;
		}
		if (cru_readl(CRU_CPLL_CON) & PLL_BYPASS) seq_printf(s, "bypass ");
	} else if (clk == &general_pll_clk) {
		switch (cru_readl(CRU_MODE_CON) & CRU_GENERAL_MODE_MASK) {
		case CRU_GENERAL_MODE_SLOW:   seq_printf(s, "slow   "); break;
		case CRU_GENERAL_MODE_NORMAL: seq_printf(s, "normal "); break;
		case CRU_GENERAL_MODE_SLOW27: seq_printf(s, "slow27 "); break;
		}
		if (cru_readl(CRU_GPLL_CON) & PLL_BYPASS) seq_printf(s, "bypass ");
	} else if (clk == &clk_ddr) {
		rate = clk->recalc(clk);
	}

	if (rate >= MHZ) {
		if (rate % MHZ)
			seq_printf(s, "%ld.%06ld MHz", rate / MHZ, rate % MHZ);
		else
			seq_printf(s, "%ld MHz", rate / MHZ);
	} else if (rate >= KHZ) {
		if (rate % KHZ)
			seq_printf(s, "%ld.%03ld KHz", rate / KHZ, rate % KHZ);
		else
			seq_printf(s, "%ld KHz", rate / KHZ);
	} else {
		seq_printf(s, "%ld Hz", rate);
	}

	seq_printf(s, " usecount = %d", clk->usecount);

	if (clk->parent)
		seq_printf(s, " parent = %s", clk->parent->name);

	seq_printf(s, "\n");

	list_for_each_entry(ck, &clocks, node) {
		if (ck->parent == clk)
			dump_clock(s, ck, deep + 1);
	}
}

static int proc_clk_show(struct seq_file *s, void *v)
{
	struct clk* clk;

	mutex_lock(&clocks_mutex);
	list_for_each_entry(clk, &clocks, node) {
		if (!clk->parent)
			dump_clock(s, clk, 0);
	}
	mutex_unlock(&clocks_mutex);

	seq_printf(s, "\nCRU Registers:\n");
	seq_printf(s, "APLL     : 0x%08x\n", cru_readl(CRU_APLL_CON));
	seq_printf(s, "DPLL     : 0x%08x\n", cru_readl(CRU_DPLL_CON));
	seq_printf(s, "CPLL     : 0x%08x\n", cru_readl(CRU_CPLL_CON));
	seq_printf(s, "GPLL     : 0x%08x\n", cru_readl(CRU_GPLL_CON));
	seq_printf(s, "MODE     : 0x%08x\n", cru_readl(CRU_MODE_CON));
	seq_printf(s, "CLKSEL0  : 0x%08x\n", cru_readl(CRU_CLKSEL0_CON));
	seq_printf(s, "CLKSEL1  : 0x%08x\n", cru_readl(CRU_CLKSEL1_CON));
	seq_printf(s, "CLKSEL2  : 0x%08x\n", cru_readl(CRU_CLKSEL2_CON));
	seq_printf(s, "CLKSEL3  : 0x%08x\n", cru_readl(CRU_CLKSEL3_CON));
	seq_printf(s, "CLKSEL4  : 0x%08x\n", cru_readl(CRU_CLKSEL4_CON));
	seq_printf(s, "CLKSEL5  : 0x%08x\n", cru_readl(CRU_CLKSEL5_CON));
	seq_printf(s, "CLKSEL6  : 0x%08x\n", cru_readl(CRU_CLKSEL6_CON));
	seq_printf(s, "CLKSEL7  : 0x%08x\n", cru_readl(CRU_CLKSEL7_CON));
	seq_printf(s, "CLKSEL8  : 0x%08x\n", cru_readl(CRU_CLKSEL8_CON));
	seq_printf(s, "CLKSEL9  : 0x%08x\n", cru_readl(CRU_CLKSEL9_CON));
	seq_printf(s, "CLKSEL10 : 0x%08x\n", cru_readl(CRU_CLKSEL10_CON));
	seq_printf(s, "CLKSEL11 : 0x%08x\n", cru_readl(CRU_CLKSEL11_CON));
	seq_printf(s, "CLKSEL12 : 0x%08x\n", cru_readl(CRU_CLKSEL12_CON));
	seq_printf(s, "CLKSEL13 : 0x%08x\n", cru_readl(CRU_CLKSEL13_CON));
	seq_printf(s, "CLKSEL14 : 0x%08x\n", cru_readl(CRU_CLKSEL14_CON));
	seq_printf(s, "CLKSEL15 : 0x%08x\n", cru_readl(CRU_CLKSEL15_CON));
	seq_printf(s, "CLKSEL16 : 0x%08x\n", cru_readl(CRU_CLKSEL16_CON));
	seq_printf(s, "CLKSEL17 : 0x%08x\n", cru_readl(CRU_CLKSEL17_CON));
	seq_printf(s, "CLKGATE0 : 0x%08x\n", cru_readl(CRU_CLKGATE0_CON));
	seq_printf(s, "CLKGATE1 : 0x%08x\n", cru_readl(CRU_CLKGATE1_CON));
	seq_printf(s, "CLKGATE2 : 0x%08x\n", cru_readl(CRU_CLKGATE2_CON));
	seq_printf(s, "CLKGATE3 : 0x%08x\n", cru_readl(CRU_CLKGATE3_CON));
	seq_printf(s, "CLKGATE3M: 0x%08x\n", cru_clkgate3_con_mirror);
	seq_printf(s, "SOFTRST0 : 0x%08x\n", cru_readl(CRU_SOFTRST0_CON));
	seq_printf(s, "SOFTRST0M: 0x%08x\n", cru_softrst0_con_mirror);
	seq_printf(s, "SOFTRST1 : 0x%08x\n", cru_readl(CRU_SOFTRST1_CON));
	seq_printf(s, "SOFTRST2 : 0x%08x\n", cru_readl(CRU_SOFTRST2_CON));

	seq_printf(s, "\nPMU Registers:\n");
	seq_printf(s, "WAKEUP_EN0 : 0x%08x\n", pmu_readl(PMU_WAKEUP_EN0));
	seq_printf(s, "WAKEUP_EN1 : 0x%08x\n", pmu_readl(PMU_WAKEUP_EN1));
	seq_printf(s, "WAKEUP_EN2 : 0x%08x\n", pmu_readl(PMU_WAKEUP_EN2));
	seq_printf(s, "PD_CON     : 0x%08x\n", pmu_readl(PMU_PD_CON));
	seq_printf(s, "MISC_CON   : 0x%08x\n", pmu_readl(PMU_MISC_CON));
	seq_printf(s, "PLL_CNT    : 0x%08x\n", pmu_readl(PMU_PLL_CNT));
	seq_printf(s, "PD_ST      : 0x%08x\n", pmu_readl(PMU_PD_ST));
	seq_printf(s, "INT_ST     : 0x%08x\n", pmu_readl(PMU_INT_ST));

	return 0;
}

static int proc_clk_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_clk_show, NULL);
}

static const struct file_operations proc_clk_fops = {
	.open		= proc_clk_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init clk_proc_init(void)
{
	proc_create("clocks", 0, NULL, &proc_clk_fops);
	return 0;

}
late_initcall(clk_proc_init);
#endif /* CONFIG_PROC_FS */

/* Clk notifier implementation */

/**
 * struct clk_notifier - associate a clk with a notifier
 * @clk: struct clk * to associate the notifier with
 * @notifier_head: a raw_notifier_head for this clk
 * @node: linked list pointers
 *
 * A list of struct clk_notifier is maintained by the notifier code.
 * An entry is created whenever code registers the first notifier on a
 * particular @clk.  Future notifiers on that @clk are added to the
 * @notifier_head.
 */
struct clk_notifier {
	struct clk			*clk;
	struct raw_notifier_head	notifier_head;
	struct list_head		node;
};

static LIST_HEAD(clk_notifier_list);

/**
 * _clk_free_notifier_chain - safely remove struct clk_notifier
 * @cn: struct clk_notifier *
 *
 * Removes the struct clk_notifier @cn from the clk_notifier_list and
 * frees it.
 */
static void _clk_free_notifier_chain(struct clk_notifier *cn)
{
	list_del(&cn->node);
	kfree(cn);
}

/**
 * clk_notify - call clk notifier chain
 * @clk: struct clk * that is changing rate
 * @msg: clk notifier type (i.e., CLK_POST_RATE_CHANGE; see mach/clock.h)
 * @old_rate: old rate
 * @new_rate: new rate
 *
 * Triggers a notifier call chain on the post-clk-rate-change notifier
 * for clock 'clk'.  Passes a pointer to the struct clk and the
 * previous and current rates to the notifier callback.  Intended to be
 * called by internal clock code only.  No return value.
 */
static void clk_notify(struct clk *clk, unsigned long msg,
		       unsigned long old_rate, unsigned long new_rate)
{
	struct clk_notifier *cn;
	struct clk_notifier_data cnd;

	cnd.clk = clk;
	cnd.old_rate = old_rate;
	cnd.new_rate = new_rate;

	UNLOCK();
	list_for_each_entry(cn, &clk_notifier_list, node) {
		if (cn->clk == clk) {
			pr_debug("%s msg %lu rate %lu -> %lu\n", clk->name, msg, old_rate, new_rate);
			raw_notifier_call_chain(&cn->notifier_head, msg, &cnd);
			break;
		}
	}
	LOCK();
}

/**
 * clk_notifier_register - add a clock parameter change notifier
 * @clk: struct clk * to watch
 * @nb: struct notifier_block * with callback info
 *
 * Request notification for changes to the clock 'clk'.  This uses a
 * blocking notifier.  Callback code must not call into the clock
 * framework, as clocks_mutex is held.  Pre-notifier callbacks will be
 * passed the previous and new rate of the clock.
 *
 * clk_notifier_register() must be called from process
 * context.  Returns -EINVAL if called with null arguments, -ENOMEM
 * upon allocation failure; otherwise, passes along the return value
 * of blocking_notifier_chain_register().
 */
int clk_notifier_register(struct clk *clk, struct notifier_block *nb)
{
	struct clk_notifier *cn = NULL, *cn_new = NULL;
	int r;
	struct clk *clkp;

	if (!clk || IS_ERR(clk) || !nb)
		return -EINVAL;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(cn, &clk_notifier_list, node)
		if (cn->clk == clk)
			break;

	if (cn->clk != clk) {
		cn_new = kzalloc(sizeof(struct clk_notifier), GFP_KERNEL);
		if (!cn_new) {
			r = -ENOMEM;
			goto cnr_out;
		};

		cn_new->clk = clk;
		RAW_INIT_NOTIFIER_HEAD(&cn_new->notifier_head);

		list_add(&cn_new->node, &clk_notifier_list);
		cn = cn_new;
	}

	r = raw_notifier_chain_register(&cn->notifier_head, nb);
	if (!IS_ERR_VALUE(r)) {
		clkp = clk;
		do {
			clkp->notifier_count++;
		} while ((clkp = clkp->parent));
	} else {
		if (cn_new)
			_clk_free_notifier_chain(cn);
	}

cnr_out:
	mutex_unlock(&clocks_mutex);

	return r;
}
EXPORT_SYMBOL(clk_notifier_register);

/**
 * clk_notifier_unregister - remove a clock change notifier
 * @clk: struct clk *
 * @nb: struct notifier_block * with callback info
 *
 * Request no further notification for changes to clock 'clk'.
 * Returns -EINVAL if called with null arguments; otherwise, passes
 * along the return value of blocking_notifier_chain_unregister().
 */
int clk_notifier_unregister(struct clk *clk, struct notifier_block *nb)
{
	struct clk_notifier *cn = NULL;
	struct clk *clkp;
	int r = -EINVAL;

	if (!clk || IS_ERR(clk) || !nb)
		return -EINVAL;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(cn, &clk_notifier_list, node)
		if (cn->clk == clk)
			break;

	if (cn->clk != clk) {
		r = -ENOENT;
		goto cnu_out;
	};

	r = raw_notifier_chain_unregister(&cn->notifier_head, nb);
	if (!IS_ERR_VALUE(r)) {
		clkp = clk;
		do {
			clkp->notifier_count--;
		} while ((clkp = clkp->parent));
	}

	/*
	 * XXX ugh, layering violation.  There should be some
	 * support in the notifier code for this.
	 */
	if (!cn->notifier_head.head)
		_clk_free_notifier_chain(cn);

cnu_out:
	mutex_unlock(&clocks_mutex);

	return r;
}
EXPORT_SYMBOL(clk_notifier_unregister);

