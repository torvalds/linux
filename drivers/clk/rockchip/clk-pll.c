#include <linux/slab.h>
#include <asm/io.h>

#include "clk-ops.h"
#include "clk-pll.h"



//static unsigned long lpj_gpll;

#define PLLS_IN_NORM(pll_id) (((cru_readl(RK3188_CRU_MODE_CON)&RK3188_PLL_MODE_MSK(pll_id))\
			==(RK3188_PLL_MODE_NORM(pll_id)&RK3188_PLL_MODE_MSK(pll_id)))\
		&&!(cru_readl(RK3188_PLL_CONS(pll_id,3))&RK3188_PLL_BYPASS))


static const struct apll_clk_set apll_table[] = {
	//            (_mhz,	nr,	nf,	no,	_periph_div,	_aclk_div)
	_RK3188_APLL_SET_CLKS(2208, 	1, 	92,	1, 	8,	81),
	_RK3188_APLL_SET_CLKS(2184,	1,	91,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(2160,	1,	90,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(2136,	1,	89,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(2112,	1,	88,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(2088,	1,	87,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(2064,	1,	86,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(2040,	1,	85,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(2016,	1,	84,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1992,	1,	83,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1968,	1,	82,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1944,	1,	81,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1920,	1,	80,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1896,	1,	79,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1872,	1,	78,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1848,	1,	77,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1824,	1,	76,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1800,	1,	75,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1776,	1,	74,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1752,	1,	73,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1728,	1,	72,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1704,	1,	71,	1,	8,	81),
	_RK3188_APLL_SET_CLKS(1680,	1,	70,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1656,	1,	69,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1632,	1,	68,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1608,	1,	67,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1560,	1,	65,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1512,	1,	63,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1488,	1,	62,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1464,	1,	61,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1440,	1,	60,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1416,	1,	59,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1392,	1,	58,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1368,	1,	57,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1344,	1,	56,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1320,	1,	55,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1296,	1,	54,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1272,	1,	53,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1248,	1,	52,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1224,	1,	51,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1200,	1,	50,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1176,	1,	49,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1128,	1,	47,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1104,	1,	46,	1,	8,	41),
	_RK3188_APLL_SET_CLKS(1008,	1,	84,	2,	8,	41),
	_RK3188_APLL_SET_CLKS(912, 	1,	76,	2,	8,	41),
	_RK3188_APLL_SET_CLKS(888, 	1,	74,	2,	8,	41),
	_RK3188_APLL_SET_CLKS(816,	1,	68,	2,	8,	41),
	_RK3188_APLL_SET_CLKS(792,	1,	66,	2,	8,	41),
	_RK3188_APLL_SET_CLKS(696,	1,	58,	2,	8,	41),
	_RK3188_APLL_SET_CLKS(600,	1,	50,	2,	4,	41),
	_RK3188_APLL_SET_CLKS(552,	1,	92,	4,	4,	41),
	_RK3188_APLL_SET_CLKS(504,	1,	84,	4,	4,	41),
	_RK3188_APLL_SET_CLKS(408,	1,	68,	4,	4,	21),
	_RK3188_APLL_SET_CLKS(312,	1,	52,	4,	2,	21),
	_RK3188_APLL_SET_CLKS(252,	1,	84,	8,	2,	21),
	_RK3188_APLL_SET_CLKS(216,	1,	72,	8,	2,	21),
	_RK3188_APLL_SET_CLKS(126,	1,	84,	16,	2,	11),
	_RK3188_APLL_SET_CLKS(48,  	1,	32,	16,	2,	11),
	_RK3188_APLL_SET_CLKS(0,	1,	32,	16,	2,	11),
};

static const struct pll_clk_set pll_com_table[] = {
	_RK3188_PLL_SET_CLKS(1200000,	1,	50,	1),
	_RK3188_PLL_SET_CLKS(1188000,	2,	99,	1),
	_RK3188_PLL_SET_CLKS(891000,	8,	594,	2),
	_RK3188_PLL_SET_CLKS(768000,	1,	64,	2),
	_RK3188_PLL_SET_CLKS(594000,	2,	198,	4),
	_RK3188_PLL_SET_CLKS(408000,	1,	68,	4),
	_RK3188_PLL_SET_CLKS(384000,	2,	128,	4),
	_RK3188_PLL_SET_CLKS(360000,	1,	60,	4),
	_RK3188_PLL_SET_CLKS(300000,	1,	50,	4),
	_RK3188_PLL_SET_CLKS(297000,	2,	198,	8),
	_RK3188_PLL_SET_CLKS(148500,	2,	99,	8),
	_RK3188_PLL_SET_CLKS(0,		0,	0,	0),
};

static void pll_wait_lock(int pll_idx)
{
	u32 pll_state[4] = {1, 0, 2, 3};
	u32 bit = 0x20u << pll_state[pll_idx];
	int delay = 24000000;
	while (delay > 0) {
		if (grf_readl(RK3188_GRF_SOC_STATUS0) & bit)
			break;
		delay--;
	}
	
	if (delay == 0) {
		clk_err("PLL_ID=%d\npll_con0=%08x\npll_con1=%08x\n"
				"pll_con2=%08x\npll_con3=%08x\n",
				pll_idx,
				cru_readl(RK3188_PLL_CONS(pll_idx, 0)),
				cru_readl(RK3188_PLL_CONS(pll_idx, 1)),
				cru_readl(RK3188_PLL_CONS(pll_idx, 2)),
				cru_readl(RK3188_PLL_CONS(pll_idx, 3)));

		clk_err("wait pll bit 0x%x time out!\n", bit);
		while(1);
	}
}


/* recalc_rate */
static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u8 pll_id = pll->id;
	unsigned long rate;

	if (PLLS_IN_NORM(pll_id)) {
		u32 pll_con0 = cru_readl(RK3188_PLL_CONS(pll_id, 0));
		u32 pll_con1 = cru_readl(RK3188_PLL_CONS(pll_id, 1));

		u64 rate64 = (u64)parent_rate * RK3188_PLL_NF(pll_con1);

		do_div(rate64, RK3188_PLL_NR(pll_con0));
		do_div(rate64, RK3188_PLL_NO(pll_con0));

		rate = rate64;
	} else {
		rate = parent_rate;
		clk_debug("pll id=%d  slow mode\n", pll_id);
	}

	clk_debug("pll id=%d, recalc rate =%lu\n", pll->id, rate);

	return rate;
}

/* round_rate */
/* get rate that is most close to target */
static const struct apll_clk_set *apll_get_best_set(unsigned long rate,
		const struct apll_clk_set *table)
{
	const struct apll_clk_set *ps, *pt;

	ps = pt = table;
	while (pt->rate) {
		if (pt->rate == rate) {
			ps = pt;
			break;
		}

		if ((pt->rate > rate || (rate - pt->rate < ps->rate - rate)))
			ps = pt;
		if (pt->rate < rate)
			break;
		pt++;
	}

	return ps;
}

/* get rate that is most close to target */
static const struct pll_clk_set *pll_com_get_best_set(unsigned long rate,
		const struct pll_clk_set *table)
{
	const struct pll_clk_set *ps, *pt;

	ps = pt = table;
	while (pt->rate) {
		if (pt->rate == rate) {
			ps = pt;
			break;
		}

		if ((pt->rate > rate || (rate - pt->rate < ps->rate - rate)))
			ps = pt;
		if (pt->rate < rate)
			break;
		pt++;
	}

	return ps;
}

static long clk_apll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return (apll_get_best_set(rate, apll_table)->rate);
}

static long clk_pll_com_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return (pll_com_get_best_set(rate, pll_com_table)->rate);
}

static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct clk *parent = __clk_get_parent(hw->clk);
	long rate_out = 0;

	if (parent && (rate==__clk_get_rate(parent))) {
		clk_debug("pll id=%d round rate=%lu equal to parent rate\n",
				pll->id, rate);
		return rate;
	}

	switch (pll->id){
		case RK3188_APLL_ID: {
				      rate_out = clk_apll_round_rate(hw, rate, prate);
				      break;
			      }
		default: {
				 rate_out = clk_pll_com_round_rate(hw, rate, prate);
				 break;
			 }
	}

	return rate_out;
}

/* set_rate */
static int _pll_clk_set_rate(struct pll_clk_set *clk_set, u8 pll_id,
		spinlock_t *lock)
{
	unsigned long flags = 0;


	clk_debug("_pll_clk_set_rate start!\n");

	if(lock)
		spin_lock_irqsave(lock, flags);

	//enter slowmode
	cru_writel(RK3188_PLL_MODE_SLOW(pll_id), RK3188_CRU_MODE_CON);
	cru_writel((0x1<<(16+1))|(0x1<<1), RK3188_PLL_CONS(pll_id, 3));
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	cru_writel(clk_set->pllcon0, RK3188_PLL_CONS(pll_id, 0));
	cru_writel(clk_set->pllcon1, RK3188_PLL_CONS(pll_id, 1));

	udelay(1);

	cru_writel((0x1<<(16+1)), RK3188_PLL_CONS(pll_id, 3));

	pll_wait_lock(pll_id);

	//return from slow
	cru_writel(RK3188_PLL_MODE_NORM(pll_id), RK3188_CRU_MODE_CON);

	if (lock)
		spin_unlock_irqrestore(lock, flags);

	clk_debug("pll id=%d, dump reg: con0=0x%08x, con1=0x%08x, mode=0x%08x\n",
			pll_id,
			cru_readl(RK3188_PLL_CONS(pll_id,0)),
			cru_readl(RK3188_PLL_CONS(pll_id,1)),
			cru_readl(RK3188_CRU_MODE_CON));

	clk_debug("_pll_clk_set_rate end!\n");

	return 0;
}

static int clk_pll_com_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct pll_clk_set *clk_set = (struct pll_clk_set *)(pll_com_table);
	int ret = 0;


	if (rate == parent_rate) {
		clk_debug("pll id=%d set rate=%lu equal to parent rate\n",
				pll->id, rate);
		cru_writel(RK3188_PLL_MODE_SLOW(pll->id), RK3188_CRU_MODE_CON);
		cru_writel((0x1 << (16+1)) | (0x1<<1), RK3188_PLL_CONS(pll->id, 3));
		clk_debug("pll id=%d enter slow mode, set rate OK!\n", pll->id);
		return 0;
	}

	while(clk_set->rate) {
		if (clk_set->rate == rate) {
			break;
		}
		clk_set++;
	}

	if (clk_set->rate == rate) {
		ret = _pll_clk_set_rate(clk_set, pll->id, pll->lock);
		clk_debug("pll id=%d set rate=%lu OK!\n", pll->id, rate);
	} else {
		clk_err("pll id=%d is no corresponding rate=%lu\n",
				pll->id, rate);
		return -EINVAL;
	}

	return ret;
}

/* 1: use
 * 0: no use
 */
#define USE_ARM_GPLL	1

static int clk_apll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct clk *clk = hw->clk;
	struct clk *arm_gpll = __clk_lookup("clk_arm_gpll");
	unsigned long arm_gpll_rate;
	const struct apll_clk_set *ps;
	u32 old_aclk_div = 0, new_aclk_div = 0;
	u32 temp_div;
	unsigned long flags;
	int sel_gpll = 0;


	if (rate == parent_rate) {
		clk_debug("pll id=%d set rate=%lu equal to parent rate\n",
				pll->id, rate);
		cru_writel(RK3188_PLL_MODE_SLOW(pll->id), RK3188_CRU_MODE_CON);
		cru_writel((0x1 << (16+1)) | (0x1<<1), RK3188_PLL_CONS(pll->id, 3));
		clk_debug("pll id=%d enter slow mode, set rate OK!\n", pll->id);
		return 0;
	}

#if !USE_ARM_GPLL
	goto CHANGE_APLL;
#endif

	/* prepare arm_gpll before reparent clk_core to it */
	if (!arm_gpll) {
		clk_err("clk arm_gpll is NULL!\n");
		goto CHANGE_APLL;
	}

	/* In rk3188, arm_gpll and cpu_gpll share a same gate,
	 * and aclk_cpu selects cpu_gpll as parent, thus this
	 * gate must keep enabled.
	 */
#if 0
	if (clk_prepare(arm_gpll)) {
		clk_err("fail to prepare arm_gpll path\n");
		clk_unprepare(arm_gpll);
		goto CHANGE_APLL;
	}

	if (clk_enable(arm_gpll)) {
		clk_err("fail to enable arm_gpll path\n");
		clk_disable(arm_gpll);
		clk_unprepare(arm_gpll);
		goto CHANGE_APLL;
	}
#endif

	arm_gpll_rate = __clk_get_rate(arm_gpll);
	temp_div = DIV_ROUND_UP(arm_gpll_rate, __clk_get_rate(clk));
	temp_div = (temp_div == 0) ? 1 : temp_div;
	if (temp_div > RK3188_CORE_CLK_MAX_DIV) {
		clk_debug("temp_div %d > max_div %d\n", temp_div,
				RK3188_CORE_CLK_MAX_DIV);
		clk_debug("can't get rate %lu from arm_gpll rate %lu\n",
				__clk_get_rate(clk), arm_gpll_rate);
		//clk_disable(arm_gpll);
		//clk_unprepare(arm_gpll);
		goto CHANGE_APLL;
	}

	local_irq_save(flags);

	/* firstly set div, then select arm_gpll path */
	cru_writel(RK3188_CORE_CLK_DIV_W_MSK|RK3188_CORE_CLK_DIV(temp_div), RK3188_CRU_CLKSELS_CON(0));
	cru_writel(RK3188_CORE_SEL_PLL_W_MSK|RK3188_CORE_SEL_GPLL, RK3188_CRU_CLKSELS_CON(0));

	sel_gpll = 1;
	//loops_per_jiffy = lpj_gpll / temp_div;
	smp_wmb();

	local_irq_restore(flags);

	clk_debug("temp select arm_gpll path, get rate %lu\n",
			arm_gpll_rate/temp_div);
	clk_debug("from arm_gpll rate %lu, temp_div %d\n", arm_gpll_rate,
			temp_div);

CHANGE_APLL:
	ps = apll_get_best_set(rate, apll_table);
	clk_debug("apll will set rate %lu\n", ps->rate);
	clk_debug("table con:%08x,%08x,%08x, sel:%08x,%08x\n",
			ps->pllcon0, ps->pllcon1, ps->pllcon2,
			ps->clksel0, ps->clksel1);

	local_irq_save(flags);

	/* If core src don't select gpll, apll need to enter slow mode
	 * before power down
	 */
	//FIXME
	//if (!sel_gpll)
	cru_writel(RK3188_PLL_MODE_SLOW(pll->id), RK3188_CRU_MODE_CON);

	/* PLL power down */
	cru_writel((0x1<<(16+1))|(0x1<<1), RK3188_PLL_CONS(pll->id, 3));
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	cru_writel(ps->pllcon0, RK3188_PLL_CONS(pll->id, 0));
	cru_writel(ps->pllcon1, RK3188_PLL_CONS(pll->id, 1));

	udelay(1);

	/* PLL power up and wait for locked */
	cru_writel((0x1<<(16+1)), RK3188_PLL_CONS(pll->id, 3));
	pll_wait_lock(pll->id);

	old_aclk_div = RK3188_GET_CORE_ACLK_VAL(cru_readl(RK3188_CRU_CLKSELS_CON(1)) &
			RK3188_CORE_ACLK_MSK);
	new_aclk_div = RK3188_GET_CORE_ACLK_VAL(ps->clksel1 & RK3188_CORE_ACLK_MSK);

	if (new_aclk_div >= old_aclk_div) {
		cru_writel(ps->clksel0, RK3188_CRU_CLKSELS_CON(0));
		cru_writel(ps->clksel1, RK3188_CRU_CLKSELS_CON(1));
	}

	/* PLL return from slow mode */
	//FIXME
	//if (!sel_gpll)
	cru_writel(RK3188_PLL_MODE_NORM(pll->id), RK3188_CRU_MODE_CON);

	/* reparent to apll, and set div to 1 */
	if (sel_gpll) {
		cru_writel(RK3188_CORE_SEL_PLL_W_MSK|RK3188_CORE_SEL_APLL, RK3188_CRU_CLKSELS_CON(0));
		cru_writel(RK3188_CORE_CLK_DIV_W_MSK|RK3188_CORE_CLK_DIV(1), RK3188_CRU_CLKSELS_CON(0));
	}

	if (old_aclk_div > new_aclk_div) {
		cru_writel(ps->clksel0, RK3188_CRU_CLKSELS_CON(0));
		cru_writel(ps->clksel1, RK3188_CRU_CLKSELS_CON(1));
	}

	//loops_per_jiffy = ps->lpj;
	smp_wmb();

	local_irq_restore(flags);

	if (sel_gpll) {
		sel_gpll = 0;
		//clk_disable(arm_gpll);
		//clk_unprepare(arm_gpll);
	}

	//clk_debug("apll set loops_per_jiffy =%lu\n", loops_per_jiffy);

	clk_debug("apll set rate %lu, con(%x,%x,%x,%x), sel(%x,%x)\n",
			ps->rate,
			cru_readl(RK3188_PLL_CONS(pll->id, 0)),cru_readl(RK3188_PLL_CONS(pll->id, 1)),
			cru_readl(RK3188_PLL_CONS(pll->id, 2)),cru_readl(RK3188_PLL_CONS(pll->id, 3)),
			cru_readl(RK3188_CRU_CLKSELS_CON(0)),cru_readl(RK3188_CRU_CLKSELS_CON(1)));

	return 0;
}

static int clk_dpll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	return 0;
}

static int clk_gpll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	int ret = clk_pll_com_set_rate(hw, rate, parent_rate);

	//if(!ret)
	//	lpj_gpll = CLK_LOOPS_RECALC(clk_pll_recalc_rate(hw, parent_rate));

	return ret;
}

static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	int ret = 0;

	switch (pll->id){
		case RK3188_APLL_ID: {
				      ret = clk_apll_set_rate(hw, rate, parent_rate);
				      break;
			      }
		case RK3188_DPLL_ID: {
				      ret = clk_dpll_set_rate(hw, rate, parent_rate);
				      break;
			      }
		case RK3188_GPLL_ID: {
				      ret = clk_gpll_set_rate(hw, rate, parent_rate);
				      break;
			      }
		default: {
				 ret = clk_pll_com_set_rate(hw, rate, parent_rate);
				 break;
			 }
	}

	return ret;
}

const struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_round_rate,
	.set_rate = clk_pll_set_rate,
};

EXPORT_SYMBOL_GPL(clk_pll_ops);


struct clk *rk_clk_register_pll(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags, void __iomem *reg,
		u32 width, u8 id, spinlock_t *lock)
{
	struct clk_pll *pll;
	struct clk *clk;
	struct clk_init_data init;


	clk_debug("%s: pll name = %s, id = %d, register start!\n",__func__,name,id);

#if 0
	if(id >= END_PLL_ID){
		clk_err("%s: PLL id = %d >= END_PLL_ID = %d\n", __func__,
				id, END_PLL_ID);
		return ERR_PTR(-EINVAL);
	}
#endif

	/* allocate the pll */
	pll = kzalloc(sizeof(struct clk_pll), GFP_KERNEL);
	if (!pll) {
		clk_err("%s: could not allocate pll clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);
	init.ops = &clk_pll_ops;

	/* struct clk_pll assignments */
	pll->reg = reg;
	pll->width = width;
	pll->id = id;
	pll->lock = lock;
	pll->hw.init = &init;

	/* register the clock */
	clk = clk_register(dev, &pll->hw);

	if (IS_ERR(clk))
		kfree(pll);

	clk_debug("%s: pll name = %s, id = %d, register finish!\n",__func__,name,id);

	return clk;
}

