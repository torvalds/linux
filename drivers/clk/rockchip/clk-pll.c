#include <linux/slab.h>
#include <asm/io.h>

#include "clk-ops.h"
#include "clk-pll.h"


static unsigned long lpj_gpll;

//fixme
extern void __iomem *reg_start;
#define RK30_CRU_BASE	(reg_start)

#define cru_readl(offset)	readl(RK30_CRU_BASE + offset)
#define cru_writel(v, offset)	do {writel(v, RK30_CRU_BASE + offset); dsb();} \
	while (0)

//fixme
//#define grf_readl(offset)	readl_relaxed(RK30_GRF_BASE + offset)


#define PLLS_IN_NORM(pll_id) (((cru_readl(CRU_MODE_CON)&PLL_MODE_MSK(pll_id))\
			==(PLL_MODE_NORM(pll_id)&PLL_MODE_MSK(pll_id)))\
		&&!(cru_readl(PLL_CONS(pll_id,3))&PLL_BYPASS))


static const struct apll_clk_set apll_table[] = {
};

static const struct pll_clk_set pll_com_table[] = {
	_PLL_SET_CLKS(1200000,	1,	50,	1),
	_PLL_SET_CLKS(1188000,	2,	99,	1),
	_PLL_SET_CLKS(891000,	8,	594,	2),
	_PLL_SET_CLKS(768000,	1,	64,	2),
	_PLL_SET_CLKS(594000,	2,	198,	4),
	_PLL_SET_CLKS(408000,	1,	68,	4),
	_PLL_SET_CLKS(384000,	2,	128,	4),
	_PLL_SET_CLKS(360000,	1,	60,	4),
	_PLL_SET_CLKS(300000,	1,	50,	4),
	_PLL_SET_CLKS(297000,	2,	198,	8),
	_PLL_SET_CLKS(148500,	2,	99,	8),
	_PLL_SET_CLKS(0,	0,	0,	0),
};


static void pll_wait_lock(int pll_idx)
{
#if 1
	//fixme
	udelay(10);
#else
	u32 pll_state[4] = {1, 0, 2, 3};
	u32 bit = 0x20u << pll_state[pll_idx];
	int delay = 24000000;
	while (delay > 0) {
		if (regfile_readl(GRF_SOC_STATUS0) & bit)
			break;
		delay--;
	}
	if (delay == 0) {
		clk_err("PLL_ID=%d\npll_con0=%08x\npll_con1=%08x\n"
				"pll_con2=%08x\npll_con3=%08x\n",
				pll_idx,
				cru_readl(PLL_CONS(pll_idx, 0)),
				cru_readl(PLL_CONS(pll_idx, 1)),
				cru_readl(PLL_CONS(pll_idx, 2)),
				cru_readl(PLL_CONS(pll_idx, 3)));

		clk_err("wait pll bit 0x%x time out!\n", bit);
		while(1);
	}
#endif
}


/*recalc_rate*/
static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u8 pll_id = pll->id;
	unsigned long rate;

	if (PLLS_IN_NORM(pll_id)) {
		u32 pll_con0 = cru_readl(PLL_CONS(pll_id, 0));
		u32 pll_con1 = cru_readl(PLL_CONS(pll_id, 1));

		u64 rate64 = (u64)parent_rate * PLL_NF(pll_con1);

		do_div(rate64, PLL_NR(pll_con0));
		do_div(rate64, PLL_NO(pll_con0));

		rate = rate64;
	} else {
		rate = parent_rate;
		clk_debug("pll id=%d  by pass mode\n", pll_id);
	}

	clk_debug("pll id=%d, recalc rate =%lu\n", pll->id, rate);

	return rate;
}

/*round_rate*/
/*get rate that is most close to target*/
static const struct pll_clk_set *pll_clk_get_best_set(unsigned long rate,
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
	return rate;
}

static long clk_pll_com_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return (pll_clk_get_best_set(rate, pll_com_table)->rate);
}

static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	long rate_out = 0;

	switch (pll->id){
		case APLL_ID: {
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

/*set_rate*/
static int _pll_clk_set_rate(struct pll_clk_set *clk_set, u8 pll_id,
		spinlock_t *lock)
{
	unsigned long flags = 0;


	clk_debug("_pll_clk_set_rate start!\n");

	if(lock)
		spin_lock_irqsave(lock, flags);

	//enter slowmode
	cru_writel(PLL_MODE_SLOW(pll_id), CRU_MODE_CON);
	cru_writel((0x1<<(16+1))|(0x1<<1), PLL_CONS(pll_id, 3));
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	dsb();
	cru_writel(clk_set->pllcon0, PLL_CONS(pll_id, 0));
	cru_writel(clk_set->pllcon1, PLL_CONS(pll_id, 1));

	rk30_clock_udelay(1);

	cru_writel((0x1<<(16+1)), PLL_CONS(pll_id, 3));

	pll_wait_lock(pll_id);

	//return from slow
	cru_writel(PLL_MODE_NORM(pll_id), CRU_MODE_CON);

	if (lock)
		spin_unlock_irqrestore(lock, flags);

	clk_debug("pll id=%d, dump reg:\n con0=%x,con1=%x,mode=%x\n", pll_id,
			cru_readl(PLL_CONS(pll_id,0)),(PLL_CONS(pll_id,1)),
			cru_readl(CRU_MODE_CON));

	clk_debug("_pll_clk_set_rate end!\n");

	return 0;
}

static int clk_pll_com_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct pll_clk_set *clk_set = (struct pll_clk_set *)(pll_com_table);
	int ret = 0;


	if(rate == parent_rate) {
		clk_debug("pll id=%d set rate=%lu equal to parent rate\n",
				pll->id, rate);
		cru_writel(PLL_MODE_SLOW(pll->id), CRU_MODE_CON);
		cru_writel((0x1 << (16+1)) | (0x1<<1), PLL_CONS(pll->id, 3));
		clk_debug("pll id=%d enter slow mode, set rate OK!\n", pll->id);
		return 0;
	}

	while(clk_set->rate) {
		if (clk_set->rate == rate) {
			break;
		}
		clk_set++;
	}

	if(clk_set->rate == rate) {
		ret = _pll_clk_set_rate(clk_set, pll->id, pll->lock);
		clk_debug("pll id=%d set rate=%lu OK!\n", pll->id, rate);
	} else {
		clk_err("pll id=%d is no corresponding rate=%lu\n",
				pll->id, rate);
		return -EINVAL;
	}

	return ret;
}

static int clk_apll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
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

        if(!ret)
                lpj_gpll = CLK_LOOPS_RECALC(clk_pll_recalc_rate(hw, parent_rate));

        return ret;
}

static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	int ret = 0;

	switch (pll->id){
		case APLL_ID: {
				      ret = clk_apll_set_rate(hw, rate, parent_rate);
				      break;
			      }
		case DPLL_ID: {
				      ret = clk_dpll_set_rate(hw, rate, parent_rate);
				      break;
			      }
		case GPLL_ID: {
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

