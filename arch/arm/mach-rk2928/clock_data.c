/* arch/arm/mach-rk2928/clock_data.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
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
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <mach/cru.h>
#include <mach/iomux.h>
#include <mach/clock.h>
#include "clock.h"
//#include <mach/pmu.h>

#define MHZ	(1000 * 1000)
#define KHZ	(1000)
#define CLK_LOOPS_JIFFY_REF 11996091ULL
#define CLK_LOOPS_RATE_REF (1200) //Mhz
#define CLK_LOOPS_RECALC(new_rate)  div_u64(CLK_LOOPS_JIFFY_REF*(new_rate),CLK_LOOPS_RATE_REF*MHZ)
#define LPJ_24M	(CLK_LOOPS_JIFFY_REF * 24) / CLK_LOOPS_RATE_REF


struct apll_clk_set {
	unsigned long rate;
	u32	pllcon0;
	u32	pllcon1; 
	u32	pllcon2; //nb=bwadj+1;0:11;nb=nf/2
	u32	clksel0;
	u32	clksel1;
	u32 	rst_dly;//us
	unsigned long lpj;	//loop per jeffise
};

struct pll_clk_set {
	unsigned long rate;
	u32	pllcon0;
	u32	pllcon1; 
	u32	pllcon2; //nb=bwadj+1;0:11;nb=nf/2
	u32 	rst_dly;//us
};
#if 0
#define CLKDATA_DBG(fmt, args...) printk(KERN_DEBUG "CLKDATA_DBG:\t"fmt, ##args)
#define CLKDATA_LOG(fmt, args...) printk(KERN_INFO "CLKDATA_LOG:\t"fmt, ##args)
#else
#define CLKDATA_DBG(fmt, args...) do {} while(0)
#define CLKDATA_LOG(fmt, args...) do {} while(0)
#endif
#define CLKDATA_ERR(fmt, args...) printk(KERN_ERR "CLKDATA_ERR:\t"fmt, ##args)

#define cru_readl(offset)	readl_relaxed(RK2928_CRU_BASE + offset)
#define cru_writel(v, offset)	do { writel_relaxed(v, RK2928_CRU_BASE + offset); dsb(); } while (0)

#define rk_clock_udelay(a) udelay(a);

#define PLLS_IN_NORM(pll_id) \
	(((cru_readl(CRU_MODE_CON) & PLL_MODE_MSK(pll_id)) == (PLL_MODE_NORM(pll_id) & PLL_MODE_MSK(pll_id)))\
	 && !(cru_readl(PLL_CONS(pll_id, 0)) & PLL_BYPASS))

#define get_cru_bits(con, mask, shift)\
	((cru_readl((con)) >> (shift)) & (mask))

#define CRU_DIV_SET(mask, shift, max) \
	.div_mask = (mask),\
.div_shift = (shift),\
.div_max = (max)

#define CRU_SRC_SET(mask, shift ) \
	.src_mask = (mask),\
.src_shift = (shift)

#define CRU_PARENTS_SET(parents_array) \
	.parents = (parents_array),\
.parents_num = ARRAY_SIZE((parents_array))

#define get_cru_bits(con,mask,shift)\
	((cru_readl((con)) >> (shift)) & (mask))

#define set_cru_bits_w_msk(val,mask,shift,con)\
	cru_writel(((mask)<<(shift+16))|((val)<<(shift)),(con))
#define regfile_readl(offset)	readl_relaxed(RK2928_GRF_BASE + offset)
#define regfile_writel(v, offset) do { writel_relaxed(v, RK2928_GRF_BASE + offset); dsb(); } while (0)
#define cru_writel_frac(v,offset) cru_writel((v),(offset))
/*******************PLL CON0 BITS***************************/
#define SET_PLL_DATA(_pll_id,_table) \
{\
	.id=(_pll_id),\
	.table=(_table),\
}

#define GATE_CLK(NAME,PARENT,ID) \
	static struct clk clk_##NAME = { \
		.name		= #NAME, \
		.parent		= &PARENT, \
		.mode		= gate_mode, \
		.gate_idx	= CLK_GATE_##ID, \
	}

//FIXME
//lpj
#define _APLL_SET_CLKS(_mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac, \
		_periph_div, _aclk_core_div, _axi_div, _apb_div, _ahb_div) \
{ \
	.rate	= (_mhz) * MHZ,	\
	.pllcon0 = PLL_SET_POSTDIV1(_postdiv1) | PLL_SET_FBDIV(_fbdiv),	\
	.pllcon1 = PLL_SET_DSMPD(_dsmpd) | PLL_SET_POSTDIV2(_postdiv2) | PLL_SET_REFDIV(_refdiv),	\
	.pllcon2 = PLL_SET_FRAC(_frac),	\
	.clksel0 = ACLK_CPU_DIV(RATIO_##_axi_div),\
	.clksel1 = PCLK_CPU_DIV(RATIO_##_apb_div) | HCLK_CPU_DIV(RATIO_##_ahb_div) \
	| ACLK_CORE_DIV(RATIO_##_aclk_core_div) | CLK_CORE_PERI_DIV(RATIO_##_periph_div),	\
	.lpj	= (CLK_LOOPS_JIFFY_REF * _mhz) / CLK_LOOPS_RATE_REF,	\
}

static const struct apll_clk_set apll_clks[] = {
	_APLL_SET_CLKS( 650, 6, 325, 2, 1, 1, 0, 41, 21, 81, 21, 21),
	_APLL_SET_CLKS(1000, 3, 125, 1, 1, 1, 0, 41, 21, 41, 21, 21),
};

#define _PLL_SET_CLKS(_mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac) \
{ \
	.rate	= (_mhz) * KHZ, \
	.pllcon0 = PLL_SET_POSTDIV1(_postdiv1) | PLL_SET_FBDIV(_fbdiv),	\
	.pllcon1 = PLL_SET_DSMPD(_dsmpd) | PLL_SET_POSTDIV2(_postdiv2) | PLL_SET_REFDIV(_refdiv),	\
	.pllcon2 = PLL_SET_FRAC(_frac),	\
}
static const struct pll_clk_set cpll_clks[] = {
	_PLL_SET_CLKS(798000, 4, 133, 1, 1, 1, 0),
	_PLL_SET_CLKS(1064000, 3, 133, 1, 1, 1, 0),
};

static const struct pll_clk_set gpll_clks[] = {
	_PLL_SET_CLKS(297000, 2, 99, 4, 1, 1, 0),
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

static int frac_div_get_seting(unsigned long rate_out,unsigned long rate,
		u32 *numerator,u32 *denominator)
{
	u32 gcd_vl;
	gcd_vl = clk_gcd(rate, rate_out);
	CLKDATA_DBG("frac_get_seting rate=%lu,parent=%lu,gcd=%d\n",rate_out,rate, gcd_vl);

	if (!gcd_vl) {
		CLKDATA_ERR("gcd=0, i2s frac div is not be supported\n");
		return -ENOENT;
	}

	*numerator = rate_out / gcd_vl;
	*denominator = rate/ gcd_vl;

	CLKDATA_DBG("frac_get_seting numerator=%d,denominator=%d,times=%d\n",
			*numerator, *denominator, *denominator / *numerator);

	if (*numerator > 0xffff || *denominator > 0xffff||
			(*denominator/(*numerator))<20) {
		CLKDATA_ERR("can't get a available nume and deno\n");
		return -ENOENT;
	}	

	return 0;

}
/************************option functions*****************/
/************************clk recalc div rate**************/

//for free div
static unsigned long clksel_recalc_div(struct clk *clk)
{
	u32 div = get_cru_bits(clk->clksel_con, clk->div_mask, clk->div_shift) + 1;
	unsigned long rate = clk->parent->rate / div;

	CLKDATA_DBG("ENTER %s clk=%s\n", __func__, clk->name);
	CLKDATA_DBG("%s new clock rate is %lu (div %u)\n", clk->name, rate, div);
	return rate;
}

//for div 2^n
static unsigned long clksel_recalc_shift(struct clk *clk)
{
	u32 shift = get_cru_bits(clk->clksel_con, clk->div_mask, clk->div_shift);
	unsigned long rate = clk->parent->rate >> shift;
	CLKDATA_DBG("ENTER %s clk=%s\n", __func__, clk->name);
	CLKDATA_DBG("%s new clock rate is %lu (shift %u)\n", clk->name, rate, shift);
	return rate;
}

//for rate equal to parent
static unsigned long clksel_recalc_equal_parent(struct clk *clk)
{
	unsigned long rate = clk->parent->rate;
	CLKDATA_DBG("ENTER %s clk=%s\n", __func__, clk->name);
	CLKDATA_DBG("%s new clock rate is %lu (equal to parent)\n", clk->name, rate);

	return rate;
}

//for Fixed divide ratio
static unsigned long clksel_recalc_fixed_div2(struct clk *clk)
{
	unsigned long rate = clk->parent->rate >> 1;
	CLKDATA_DBG("ENTER %s clk=%s\n", __func__, clk->name);
	CLKDATA_DBG("%s new clock rate is %lu (div %u)\n", clk->name, rate, 2);

	return rate;
}

static unsigned long clksel_recalc_fixed_div4(struct clk *clk)
{	
	unsigned long rate = clk->parent->rate >> 2;
	CLKDATA_DBG("ENTER %s clk=%s\n", __func__, clk->name);
	CLKDATA_DBG("%s new clock rate is %lu (div %u)\n", clk->name, rate, 4);

	return rate;
}

static unsigned long clksel_recalc_frac(struct clk *clk)
{
	unsigned long rate;
	u64 rate64;
	u32 r = cru_readl(clk->clksel_con), numerator, denominator;
	CLKDATA_DBG("ENTER %s clk=%s\n", __func__, clk->name);
	if (r == 0) // FPGA ?
		return clk->parent->rate;
	numerator = r >> 16;
	denominator = r & 0xFFFF;
	rate64 = (u64)clk->parent->rate * numerator;
	do_div(rate64, denominator);
	rate = rate64;
	CLKDATA_DBG("%s new clock rate is %lu (frac %u/%u)\n", clk->name, rate, numerator, denominator);
	return rate;
}

#define FRAC_MODE	0
static unsigned long pll_clk_recalc(u8 pll_id, unsigned long parent_rate)
{
	unsigned long rate;
	unsigned int dsmp = 0;
	u64 rate64 = 0, frac_rate64 = 0;
	dsmp = PLL_GET_DSMPD(cru_readl(PLL_CONS(pll_id, 1)));

	if (PLLS_IN_NORM(pll_id)) {
		u32 pll_con0 = cru_readl(PLL_CONS(pll_id, 0));
		u32 pll_con1 = cru_readl(PLL_CONS(pll_id, 1));
		//integer mode
		rate64 = (u64)parent_rate * PLL_GET_FBDIV(pll_con0);
		do_div(rate64, PLL_GET_REFDIV(pll_con1));

		if (FRAC_MODE == dsmp) {
			//fractional mode
			frac_rate64 = (u64)parent_rate * PLL_GET_FRAC(pll_con1);
			do_div(frac_rate64, PLL_GET_REFDIV(pll_con1));
			rate64 += frac_rate64 >> 24;
			CLKDATA_DBG("%s id=%d frac_rate=%llu(0x%08x/2^24) by pass mode\n", 
					__func__, pll_id, frac_rate64, PLL_GET_FRAC(pll_con1));	
		} 		
		do_div(rate64, PLL_GET_POSTDIV1(pll_con0));
		do_div(rate64, PLL_GET_POSTDIV2(pll_con1));

		rate = rate64;
	} else {
		rate = parent_rate;	
		CLKDATA_DBG("pll_clk_recalc id=%d rate=%lu by pass mode\n", pll_id, rate);	
	}
	return rate;
}

static unsigned long plls_clk_recalc(struct clk *clk)
{
	return pll_clk_recalc(clk->pll->id, clk->parent->rate);
}

/************************clk set rate*********************************/
static int clksel_set_rate_freediv(struct clk *clk, unsigned long rate)
{
	u32 div;
	for (div = 0; div < clk->div_max; div++) {
		u32 new_rate = clk->parent->rate / (div + 1);
		if (new_rate <= rate) {
			set_cru_bits_w_msk(div,clk->div_mask,clk->div_shift,clk->clksel_con);
			//clk->rate = new_rate;
			CLKDATA_DBG("clksel_set_rate_freediv for clock %s to rate %ld (div %d)\n", clk->name, rate, div + 1);
			return 0;
		}
	}
	return -ENOENT;
}

//for div 1 2 4 2^n
static int clksel_set_rate_shift(struct clk *clk, unsigned long rate)
{
	u32 shift;
	for (shift = 0; (1 << shift) < clk->div_max; shift++) {
		u32 new_rate = clk->parent->rate >> shift;
		if (new_rate <= rate) {
			set_cru_bits_w_msk(shift,clk->div_mask,clk->div_shift,clk->clksel_con);
			clk->rate = new_rate;
			CLKDATA_DBG("clksel_set_rate_shift for clock %s to rate %ld (shift %d)\n", clk->name, rate, shift);
			return 0;
		}
	}
	return -ENOENT;
}
#if 0
//for div 2 4 2^n
static int clksel_set_rate_shift_2(struct clk *clk, unsigned long rate)
{
	u32 shift;

	for (shift = 1; (1 << shift) < clk->div_max; shift++) {
		u32 new_rate = clk->parent->rate >> shift;
		if (new_rate <= rate) {
			set_cru_bits_w_msk(shift-1,clk->div_mask,clk->div_shift,clk->clksel_con);
			clk->rate = new_rate;
			CLKDATA_DBG("clksel_set_rate_shift for clock %s to rate %ld (shift %d)\n", clk->name, rate, shift);
			return 0;
		}
	}
	return -ENOENT;
}
#endif
static u32 clk_get_freediv(unsigned long rate_out, unsigned long rate ,u32 div_max)
{
	u32 div;
	unsigned long new_rate;
	for (div = 0; div <div_max; div++) {
		new_rate = rate / (div + 1);
		if (new_rate <= rate_out) {
			return div+1;
		}
	}	
	return div_max?div_max:1;
}
struct clk *get_freediv_parents_div(struct clk *clk,unsigned long rate,u32 *div_out)
{
	u32 div[2]={0,0};
	unsigned long new_rate[2]={0,0};
	u32 i;

	if(clk->rate==rate)
		return clk->parent;
	for(i=0;i<2;i++)
	{
		div[i]=clk_get_freediv(rate,clk->parents[i]->rate,clk->div_max);
		new_rate[i] = clk->parents[i]->rate/div[i];
		if(new_rate[i]==rate)
		{
			*div_out=div[i];
			return clk->parents[i];
		}	
	}
	if(new_rate[0]<new_rate[1])
		i=1;
	else
		i=0;
	*div_out=div[i];
	return clk->parents[i];
}

static int clkset_rate_freediv_autosel_parents(struct clk *clk, unsigned long rate)
{
	struct clk *p_clk;
	u32 div,old_div;
	int ret=0;
	if(clk->rate==rate)
		return 0;
	p_clk=get_freediv_parents_div(clk,rate,&div);

	if(!p_clk)
		return -ENOENT;

	CLKDATA_DBG("%s %lu,form %s\n",clk->name,rate,p_clk->name);
	if (clk->parent != p_clk)
	{
		old_div=CRU_GET_REG_BITS_VAL(cru_readl(clk->clksel_con),clk->div_shift,clk->div_mask)+1;

		if(div>old_div)
		{
			set_cru_bits_w_msk(div-1,clk->div_mask,clk->div_shift,clk->clksel_con);
		}
		ret=clk_set_parent_nolock(clk,p_clk);
		if(ret)
		{
			CLKDATA_ERR("%s can't set %lu,reparent err\n",clk->name,rate);
			return -ENOENT;
		}
	}
	//set div
	set_cru_bits_w_msk(div-1,clk->div_mask,clk->div_shift,clk->clksel_con);
	return 0;	
}
#if 0
//rate==div rate //hdmi
static int clk_freediv_autosel_parents_set_fixed_rate(struct clk *clk, unsigned long rate)
{
	struct clk *p_clk;
	u32 div,old_div;
	int ret;
	p_clk=get_freediv_parents_div(clk,rate,&div);

	if(!p_clk)
		return -ENOENT;

	if((p_clk->rate/div)!=rate||(p_clk->rate%div))
		return -ENOENT;

	if (clk->parent != p_clk)
	{
		old_div=CRU_GET_REG_BITS_VAL(cru_readl(clk->clksel_con),
				clk->div_shift,clk->div_mask)+1;
		if(div>old_div)
		{
			set_cru_bits_w_msk(div-1,clk->div_mask,clk->div_shift,clk->clksel_con);
		}
		ret=clk_set_parent_nolock(clk,p_clk);
		if (ret)
		{
			CLKDATA_DBG("%s can't get rate%lu,reparent err\n",clk->name,rate);
			return ret;
		}
	}
	//set div
	set_cru_bits_w_msk(div-1,clk->div_mask,clk->div_shift,clk->clksel_con);
	return 0;	
}
#endif
/************************round functions*****************/
static long clksel_freediv_round_rate(struct clk *clk, unsigned long rate)
{
	return clk->parent->rate/clk_get_freediv(rate,clk->parent->rate,clk->div_max);
}

static long clk_freediv_round_autosel_parents_rate(struct clk *clk, unsigned long rate)
{
	u32 div;
	struct clk *p_clk;
	if(clk->rate == rate)
		return clk->rate;
	p_clk=get_freediv_parents_div(clk,rate,&div);
	if(!p_clk)
		return 0;
	return p_clk->rate/div;
}

static const struct apll_clk_set* apll_clk_get_best_pll_set(unsigned long rate,
		struct apll_clk_set *tables)
{
	const struct apll_clk_set *ps, *pt;

	/* find the arm_pll we want. */
	ps = pt = tables;
	while (pt->rate) {
		if (pt->rate == rate) {
			ps = pt;
			break;
		}
		// we are sorted, and ps->rate > pt->rate.
		if ((pt->rate > rate || (rate - pt->rate < ps->rate - rate)))
			ps = pt;
		if (pt->rate < rate)
			break;
		pt++;
	}
	//CLKDATA_DBG("arm pll best rate=%lu\n",ps->rate);
	return ps;
}
static long apll_clk_round_rate(struct clk *clk, unsigned long rate)
{
	return apll_clk_get_best_pll_set(rate, clk->pll->table)->rate;
}

/************************others functions*****************/
static void pll_wait_lock(int pll_idx)
{
	u32 pll_state[4]={1,0,2,3};
	u32 bit = 0x10u << pll_state[pll_idx];
	int delay = 24000000;
	while (delay > 0) {
		if ((cru_readl(PLL_CONS(pll_idx, 1)) & (0x1 << PLL_LOCK_SHIFT))) {
			//CLKDATA_DBG("%s %08x\n", __func__, cru_readl(PLL_CONS(pll_idx, 1)) & (0x1 << PLL_LOCK_SHIFT));
			//CLKDATA_DBG("%s ! %08x\n", __func__, !(cru_readl(PLL_CONS(pll_idx, 1)) & (0x1 << PLL_LOCK_SHIFT)));
			break;
		}
		delay--;
	}
	if (delay == 0) {
		CLKDATA_ERR("wait pll bit 0x%x time out!\n", bit);
		while(1);
	}
}
static int pll_clk_mode(struct clk *clk, int on)
{
	u8 pll_id = clk->pll->id;
	// FIXME here 500 must be changed
	u32 dly = 1500;

	CLKDATA_DBG("pll_mode %s(%d)\n", clk->name, on);
	//FIXME
	if (on) {
		cru_writel(CRU_W_MSK_SETBIT(PLL_PWR_ON, PLL_LOCK_SHIFT), PLL_CONS(pll_id, 1));
		rk_clock_udelay(dly);
		pll_wait_lock(pll_id);
		cru_writel(PLL_MODE_NORM(pll_id), CRU_MODE_CON);
	} else {
		cru_writel(PLL_MODE_SLOW(pll_id), CRU_MODE_CON);
		cru_writel(CRU_W_MSK_SETBIT(PLL_PWR_DN, PLL_LOCK_SHIFT), PLL_CONS(pll_id, 1));
	}
	return 0;
}
static struct clk* clksel_get_parent(struct clk *clk)
{
	return clk->parents[(cru_readl(clk->clksel_con) >> clk->src_shift) & clk->src_mask];
}
static int clksel_set_parent(struct clk *clk, struct clk *parent)
{
	u32 i;
	if (unlikely(!clk->parents))
		return -EINVAL;
	for (i = 0; (i <clk->parents_num); i++) {
		if (clk->parents[i]!= parent)
			continue;
		set_cru_bits_w_msk(i,clk->src_mask,clk->src_shift,clk->clksel_con);
		return 0;
	}
	return -EINVAL;
}

static int gate_mode(struct clk *clk, int on)
{
	int idx = clk->gate_idx;
	CLKDATA_DBG("ENTER %s clk=%s, on=%d\n", __func__, clk->name, on);
	if (idx >= CLK_GATE_MAX)
		return -EINVAL;
	if(on) {
		cru_writel(CLK_GATE_W_MSK(idx) | CLK_UN_GATE(idx), CLK_GATE_CLKID_CONS(idx));
	} else {
		cru_writel(CLK_GATE_W_MSK(idx) | CLK_GATE(idx), CLK_GATE_CLKID_CONS(idx));
	}
	return 0;
}
#define PLL_INT_MODE	1
#define PLL_FRAC_MODE	0

#define rk2928_clock_udelay(a) udelay(a);
static int pll_clk_set_rate(struct pll_clk_set *clk_set, u8 pll_id)
{
	//enter slowmode
	cru_writel(PLL_MODE_SLOW(pll_id), CRU_MODE_CON);
	
	cru_writel(clk_set->pllcon0, PLL_CONS(pll_id,0));
	cru_writel(clk_set->pllcon1, PLL_CONS(pll_id,1));
	cru_writel(clk_set->pllcon2, PLL_CONS(pll_id,2));

	CLKDATA_DBG("id=%d,pllcon0%08x\n", pll_id, cru_readl(PLL_CONS(pll_id,0)));
	CLKDATA_DBG("id=%d,pllcon1%08x\n", pll_id, cru_readl(PLL_CONS(pll_id,1)));
	CLKDATA_DBG("id=%d,pllcon2%08x\n", pll_id, cru_readl(PLL_CONS(pll_id,2)));
	//rk2928_clock_udelay(5);

	//wating lock state
	rk2928_clock_udelay(clk_set->rst_dly);
	pll_wait_lock(pll_id);

	//return form slow
	cru_writel(PLL_MODE_NORM(pll_id), CRU_MODE_CON);

	return 0;
}
#define PLL_FREF_MIN (183*KHZ)
#define PLL_FREF_MAX (1500*MHZ)

#define PLL_FVCO_MIN (300*MHZ)
#define PLL_FVCO_MAX (1500*MHZ)

#define PLL_FOUT_MIN (18750*KHZ)
#define PLL_FOUT_MAX (1500*MHZ)

#define PLL_NF_MAX (4096)
#define PLL_NR_MAX (64)
#define PLL_NO_MAX (16)

static int pll_clk_check_legality(unsigned long fin_hz,unsigned long fout_hz,
		u32 refdiv, u32 fbdiv, u32 postdiv1, u32 postdiv2)
{
	fin_hz /= MHZ;
	if (fin_hz < 1 || fin_hz > 800) {
		CLKDATA_ERR("%s fbdiv out of [1, 800]MHz\n", __func__);
		return -1;
	}

	if (fbdiv < 16 || fbdiv > 1600) {
		CLKDATA_ERR("%s fbdiv out of [16, 1600]MHz\n", __func__);
		return -1;
	}

	if (fin_hz / refdiv < 1 || fin_hz / refdiv > 40) {
		CLKDATA_ERR("%s fin / refdiv out of [1, 40]MHz\n", __func__);
		return -1;
	}

	if (fin_hz * fbdiv / refdiv < 400 || fin_hz * fbdiv / refdiv > 1600) {
		CLKDATA_ERR("%s fin_hz * fbdiv / refdiv out of [400, 1600]MHz\n", __func__);
		return -1;
	}

	if (fin_hz * fbdiv / refdiv / postdiv1 / postdiv2 < 8 
			|| fin_hz * fbdiv / refdiv / postdiv1 / postdiv2 > 1600) {
		CLKDATA_ERR("%s fin_hz * fbdiv / refdiv / postdiv1 / postdiv2  out of [8, 1600]MHz\n", __func__);
		return -1;
	}

}

static int pll_clk_check_legality_frac(unsigned long fin_hz,unsigned long fout_hz,
		u32 refdiv, u32 fbdiv, u32 postdiv1, u32 postdiv2, u32 frac)
{
	fin_hz /= MHZ;
	if (fin_hz < 10 || fin_hz > 800) {
		CLKDATA_ERR("%s fin_hz out of [10, 800]MHz\n", __func__);
		return -1;
	}
	if (fbdiv < 19 || fbdiv > 160) {
		CLKDATA_ERR("%s fbdiv out of [19, 160]MHz\n", __func__);
		return -1;
	}

	if (fin_hz / refdiv < 1 || fin_hz / refdiv > 40) {
		CLKDATA_ERR("%s fin / refdiv out of [1, 40]MHz\n", __func__);
		return -1;
	}

	if (fin_hz * fbdiv / refdiv < 400 || fin_hz * fbdiv / refdiv > 1600) {
		CLKDATA_ERR("%s fin_hz * fbdiv / refdiv out of [400, 1600]MHz\n", __func__);
		return -1;
	}

	if (fin_hz * fbdiv / refdiv / postdiv1 / postdiv2 < 8 
			|| fin_hz * fbdiv / refdiv / postdiv1 / postdiv2 > 1600) {
		CLKDATA_ERR("%s fin_hz * fbdiv / refdiv / postdiv1 / postdiv2  out of [8, 1600]MHz\n", __func__);
		return -1;
	}

}
static int pll_clk_get_set(unsigned long fin_hz,unsigned long fout_hz,
		u32 *refdiv, u32 *fbdiv, u32 *postdiv1, u32 *postdiv2, u32 *frac)
{
	// FIXME set postdiv1/2 always 1 	
	u32 gcd;
	
	if(!fin_hz || !fout_hz || fout_hz == fin_hz)
		return -1;

	fin_hz /= MHZ;
	fout_hz /= MHZ;
	gcd = clk_gcd(fin_hz, fout_hz);
	*refdiv = fin_hz / gcd;
	*fbdiv = fout_hz / gcd;
	*postdiv1 = 1;
	*postdiv2 = 1;

	*frac = 0;

	CLKDATA_DBG("fin=%lu,fout=%lu,gcd=%u,refdiv=%u,fbdiv=%u,postdiv1=%u,postdiv2=%u,frac=%u\n",
			fin_hz, fout_hz, gcd, *refdiv, *fbdiv, *postdiv1, *postdiv2, *frac);

	return 0;
}
static int pll_set_con(u8 id, u32 refdiv, u32 fbdiv, u32 postdiv1, u32 postdiv2, u32 frac)
{
	struct pll_clk_set temp_clk_set;
	temp_clk_set.pllcon0 = PLL_SET_FBDIV(fbdiv) | PLL_SET_POSTDIV1(postdiv1) ;
	temp_clk_set.pllcon1 = PLL_SET_REFDIV(refdiv) | PLL_SET_POSTDIV2(postdiv2);
	temp_clk_set.pllcon2 = PLL_SET_FRAC(frac);
	temp_clk_set.rst_dly = 1500;
	CLKDATA_DBG("setting....\n");
	return pll_clk_set_rate(&temp_clk_set, id);
}
static int apll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	struct _pll_data *pll_data=clk->pll;
	struct apll_clk_set *clk_set=(struct apll_clk_set*)pll_data->table;

	u32 fin_hz, fout_hz;
	u32 refdiv, fbdiv, postdiv1, postdiv2, frac;
	u8 pll_id = pll_data->id;
	
	fin_hz = clk->parent->rate;
	fout_hz = rate;

	while(clk_set->rate) {
		if (clk_set->rate == rate) {
			break;
		}
		clk_set++;
	}

	CLKDATA_DBG("%s %s %lu\n", __func__, clk->name, rate);
	CLKDATA_DBG("pllcon0 %08x\n", cru_readl(PLL_CONS(0,0)));
	CLKDATA_DBG("pllcon1 %08x\n", cru_readl(PLL_CONS(0,1)));
	CLKDATA_DBG("pllcon2 %08x\n", cru_readl(PLL_CONS(0,2)));
	CLKDATA_DBG("pllcon3 %08x\n", cru_readl(PLL_CONS(0,3)));
	CLKDATA_DBG("clksel0 %08x\n", cru_readl(CRU_CLKSELS_CON(0)));
	CLKDATA_DBG("clksel1 %08x\n", cru_readl(CRU_CLKSELS_CON(1)));
	if(clk_set->rate==rate) {
		CLKDATA_DBG("apll get a rate\n");
	
		//enter slowmode
		local_irq_save(flags);
		cru_writel(PLL_MODE_SLOW(pll_id), CRU_MODE_CON);
		loops_per_jiffy = LPJ_24M;

		cru_writel(clk_set->pllcon0, PLL_CONS(pll_id,0));
		cru_writel(clk_set->pllcon1, PLL_CONS(pll_id,1));
		cru_writel(clk_set->pllcon2, PLL_CONS(pll_id,2));
		cru_writel(clk_set->clksel0, CRU_CLKSELS_CON(0));
		cru_writel(clk_set->clksel1, CRU_CLKSELS_CON(1));
		local_irq_restore(flags);

		CLKDATA_DBG("pllcon0 %08x\n", cru_readl(PLL_CONS(0,0)));
		CLKDATA_DBG("pllcon1 %08x\n", cru_readl(PLL_CONS(0,1)));
		CLKDATA_DBG("pllcon2 %08x\n", cru_readl(PLL_CONS(0,2)));
		CLKDATA_DBG("pllcon3 %08x\n", cru_readl(PLL_CONS(0,3)));
		CLKDATA_DBG("clksel0 %08x\n", cru_readl(CRU_CLKSELS_CON(0)));
		CLKDATA_DBG("clksel1 %08x\n", cru_readl(CRU_CLKSELS_CON(1)));
		//rk2928_clock_udelay(5);

		//wating lock state
		rk2928_clock_udelay(clk_set->rst_dly);
		pll_wait_lock(pll_id);

		//return form slow
		local_irq_save(flags);
		cru_writel(PLL_MODE_NORM(pll_id), CRU_MODE_CON);
		loops_per_jiffy = clk_set->lpj;
		local_irq_restore(flags);
	} else {
		// FIXME 
		pll_clk_get_set(clk->parent->rate, rate, &refdiv, &fbdiv, &postdiv1, &postdiv2, &frac);	
		pll_set_con(clk->pll->id, refdiv, fbdiv, postdiv1, postdiv2, frac);
	}

	CLKDATA_DBG("setting OK\n");
	return 0;
}

static int dpll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	// FIXME do nothing here
	CLKDATA_DBG("setting OK\n");
	return 0;
}

static int cpll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	// FIXME 	
	struct _pll_data *pll_data=clk->pll;
	struct pll_clk_set *clk_set=(struct pll_clk_set*)pll_data->table;

	unsigned long fin_hz, fout_hz;
	u32 refdiv, fbdiv, postdiv1, postdiv2, frac;
	fin_hz = clk->parent->rate;
	fout_hz = rate;

	while(clk_set->rate) {
		if (clk_set->rate == rate) {
			break;
		}
		clk_set++;
	}

	if(clk_set->rate==rate) {
		CLKDATA_DBG("cpll get a rate\n");
		pll_clk_set_rate(clk_set, pll_data->id);
	
	} else {
		CLKDATA_DBG("cpll get auto calc a rate\n");
		if(pll_clk_get_set(clk->parent->rate, rate, &refdiv, &fbdiv, &postdiv1, &postdiv2, &frac) != 0) {
			pr_err("cpll auto set rate error\n");
			return -ENOENT;
		}
		CLKDATA_DBG("%s get fin=%lu, fout=%lu, rate=%lu, refdiv=%u, fbdiv=%u, postdiv1=%u, postdiv2=%u",
				__func__, fin_hz, fout_hz, rate, refdiv, fbdiv, postdiv1, postdiv2);
		pll_set_con(pll_data->id, refdiv, fbdiv, postdiv1, postdiv2, frac);
	}

	CLKDATA_DBG("setting OK\n");
	return 0;	
}

static int gpll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	// FIXME 		
	struct _pll_data *pll_data=clk->pll;
	struct pll_clk_set *clk_set=(struct pll_clk_set*)pll_data->table;
	
	CLKDATA_DBG("******%s\n", __func__);
	while(clk_set->rate)
	{
		CLKDATA_DBG("******%s clk_set->rate=%lu\n", __func__, clk_set->rate);
		if (clk_set->rate == rate) {
			break;
		}
		clk_set++;
	}
	if(clk_set->rate== rate)
	{
		pll_clk_set_rate(clk_set,pll_data->id);
		//lpj_gpll = CLK_LOOPS_RECALC(rate);
	}
	else
	{
		CLKDATA_ERR("gpll is no corresponding rate=%lu\n", rate);
		return -1;
	}
	CLKDATA_DBG("******%s end\n", __func__);

	return 0;
}

/**********************pll datas*************************/
static u32 rk2928_clock_flags = 0;
static struct _pll_data apll_data = SET_PLL_DATA(APLL_ID, (void *)apll_clks);
static struct _pll_data dpll_data = SET_PLL_DATA(DPLL_ID, NULL);
static struct _pll_data cpll_data = SET_PLL_DATA(CPLL_ID, (void *)cpll_clks);
static struct _pll_data gpll_data = SET_PLL_DATA(GPLL_ID, (void *)gpll_clks);
/*********************************************************/
/************************clocks***************************/
/*********************************************************/

static struct clk xin24m = {
	.name		= "xin24m",
	.rate		= 24 * MHZ,
	.flags		= RATE_FIXED,
};

static struct clk clk_12m = {
	.name		= "clk_12m",
	.parent		= &xin24m,
	.rate		= 12 * MHZ,
	.flags		= RATE_FIXED,
};
/************************plls***********************/
static struct clk arm_pll_clk = {
	.name		= "arm_pll",
	.parent		= &xin24m,
	.mode 		= pll_clk_mode,
	.recalc		= plls_clk_recalc,
	.set_rate	= apll_clk_set_rate,
	.round_rate	= apll_clk_round_rate,
	.pll		= &apll_data,
};

static struct clk ddr_pll_clk = {
	.name		= "ddr_pll",
	.parent		= &xin24m,
	.mode 		= pll_clk_mode,
	.recalc		= plls_clk_recalc,
	.set_rate	= dpll_clk_set_rate,
	.pll		= &dpll_data,
};

static struct clk codec_pll_clk = {
	.name		= "codec_pll",
	.parent		= &xin24m,
	.mode 		= pll_clk_mode,
	.recalc		= plls_clk_recalc,
	.set_rate	= cpll_clk_set_rate,
	.pll		= &cpll_data,
};

static struct clk general_pll_clk = {
	.name		= "general_pll",
	.parent		= &xin24m,
	.mode 		= pll_clk_mode,
	.gate_idx	= CLK_GATE_CPU_GPLL,
	.recalc		= plls_clk_recalc,
	.set_rate	= gpll_clk_set_rate,
	.pll		= &gpll_data,
};
#define SELECT_FROM_2PLLS {&general_pll_clk, &codec_pll_clk}
/*********ddr******/
static int ddr_clk_set_rate(struct clk *c, unsigned long rate)
{
	// need to do nothing
	return 0;
}

static struct clk clk_ddrphy2x = {
	.name		= "ddrphy2x",
	.parent		= &ddr_pll_clk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_DDRPHY_SRC,
	.recalc		= clksel_recalc_shift,
	.set_rate	= ddr_clk_set_rate,
	.clksel_con	= CRU_CLKSELS_CON(26),
};

static struct clk clk_ddrc = {
	.name		= "ddrc",
	.parent		= &clk_ddrphy2x,
	.recalc		= clksel_recalc_fixed_div2,
};

static struct clk clk_ddrphy = {
	.name		= "ddrphy",
	.parent		= &clk_ddrphy2x,
	.recalc		= clksel_recalc_fixed_div2,
};

/****************core*******************/
#if 0
static unsigned long core_clk_get_rate(struct clk *c)
{
	u32 div=(get_cru_bits(c->clksel_con,c->div_mask,c->div_shift)+1);
	//c->parent->rate=c->parent->recalc(c->parent);
	return c->parent->rate/div;
}
#endif
static long core_clk_round_rate(struct clk *clk, unsigned long rate)
{
	u32 div=(get_cru_bits(clk->clksel_con,clk->div_mask,clk->div_shift)+1);
	return clk_round_rate_nolock(clk->parent,rate)/div;
}

static int core_clksel_set_parent(struct clk *clk, struct clk *new_prt)
{
	// FIXME
	u32 temp_div;
	struct clk *old_prt;

	if(clk->parent==new_prt)
		return 0;
	if (unlikely(!clk->parents))
		return -EINVAL;
	CLKDATA_DBG("%s,reparent %s\n",clk->name,new_prt->name);
	//arm
	old_prt=clk->parent;

	if(clk->parents[0]==new_prt)
	{
		new_prt->set_rate(new_prt,300*MHZ);
		set_cru_bits_w_msk(0,clk->div_mask,clk->div_shift,clk->clksel_con);		
	}
	else if(clk->parents[1]==new_prt)
	{

		if(new_prt->rate>old_prt->rate)	
		{
			temp_div=clk_get_freediv(old_prt->rate,new_prt->rate,clk->div_max);
			set_cru_bits_w_msk(temp_div-1,clk->div_mask,clk->div_shift,clk->clksel_con);	
		}
		set_cru_bits_w_msk(1,clk->src_mask,clk->src_shift,clk->clksel_con);
		new_prt->set_rate(new_prt,300*MHZ);
	}
	else
		return -1;

	return 0;
}

static struct clk *clk_core_pre_parents[2]		= {&arm_pll_clk, &general_pll_clk};
// this clk is cpu?
static int arm_core_clk_set_rate(struct clk *c, unsigned long rate)
{
	int ret;
	//set arm pll div 1
	//set_cru_bits_w_msk(0,c->div_mask,c->div_shift,c->clksel_con);
	
	CLKDATA_DBG("Failed to change clk pll %s to %lu\n",c->name,rate);
	ret = clk_set_rate_nolock(c->parent, rate);
	if (ret) {
		CLKDATA_ERR("Failed to change clk pll %s to %lu\n",c->name,rate);
		return ret;
	}
	CLKDATA_DBG("change clk pll %s to %lu OK\n",c->name,rate);
	return 0;
}
static struct clk clk_core_pre = {
	.name		= "core_pre",
	.parent		= &arm_pll_clk,
	.recalc		= clksel_recalc_div,
	.set_rate 	= arm_core_clk_set_rate,
	.round_rate	= core_clk_round_rate,
	.set_parent 	= core_clksel_set_parent,

	.clksel_con	= CRU_CLKSELS_CON(0),
	CRU_DIV_SET(A9_CORE_DIV_MASK, A9_CORE_DIV_SHIFT, 32),
	CRU_SRC_SET(0x1, CORE_CLK_PLL_SEL_SHIFT),
	CRU_PARENTS_SET(clk_core_pre_parents),
};

static struct clk clk_core_periph = {
	.name		= "core_periph",
	.parent		= &clk_core_pre,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_CORE_PERIPH,
	.recalc		= clksel_recalc_div,
	.set_rate 	= clksel_set_rate_freediv,
	.clksel_con	= CRU_CLKSELS_CON(1),
	CRU_DIV_SET(CORE_PERIPH_DIV_MASK, CORE_PERIPH_DIV_SHIFT, 16),
};

static struct clk clken_core_periph = {
	.name		= "core_periph_en",
	.parent		= &clk_core_periph,
	.recalc		= clksel_recalc_equal_parent,
};

static struct clk clk_l2c = {
	.name		= "l2c",
	.parent		= &clk_core_pre,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_CLK_L2C,
};

static struct clk aclk_core_pre = {
	.name		= "aclk_core_pre",
	.parent		= &clk_core_pre,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_ACLK_CORE,
	.recalc		= clksel_recalc_div,
	.set_rate 	= clksel_set_rate_freediv,
	.clksel_con	= CRU_CLKSELS_CON(1),
	CRU_DIV_SET(ACLK_CORE_DIV_MASK, ACLK_CORE_DIV_SHIFT, 2),
};

/****************cpu*******************/

static struct clk *clk_cpu_div_parents[]		= {&arm_pll_clk, &general_pll_clk};
/*seperate because of gating*/
static struct clk clk_cpu_div = {
	.name		= "cpu_div",
	.parent		= &general_pll_clk,
	.recalc		= clksel_recalc_div,
	.set_rate 	= clksel_set_rate_freediv,
	.clksel_con	= CRU_CLKSELS_CON(0),
	CRU_DIV_SET(ACLK_CPU_DIV_MASK, ACLK_CPU_DIV_SHIFT, 32),
	CRU_SRC_SET(0x1, CPU_CLK_PLL_SEL_SHIFT),
	CRU_PARENTS_SET(clk_cpu_div_parents),
};
static struct clk aclk_cpu_pre = {
	.name		= "aclk_cpu_pre",
	.parent		= &clk_cpu_div,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_ACLK_CPU,
	.recalc		= clksel_recalc_equal_parent,
};
static struct clk hclk_cpu_pre = {
	.name		= "hclk_cpu_pre",
	.parent		= &clk_cpu_div,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_HCLK_CPU,
	.recalc		= clksel_recalc_div,
	.set_rate 	= clksel_set_rate_freediv,
	.clksel_con	= CRU_CLKSELS_CON(1),
	CRU_DIV_SET(HCLK_CPU_DIV_MASK, HCLK_CPU_DIV_SHIFT, 4),
};
static struct clk pclk_cpu_pre = {
	.name		= "pclk_cpu_pre",
	.parent		= &clk_cpu_div,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_PCLK_CPU,
	.recalc		= clksel_recalc_div,
	.set_rate 	= clksel_set_rate_freediv,
	.clksel_con	= CRU_CLKSELS_CON(1),
	CRU_DIV_SET(PCLK_CPU_DIV_MASK, PCLK_CPU_DIV_SHIFT, 8),
};
/****************vcodec*******************/
static struct clk *clk_aclk_vepu_parents[]		= SELECT_FROM_2PLLS;
static struct clk *clk_aclk_vdpu_parents[]		= SELECT_FROM_2PLLS;
static struct clk aclk_vepu = {
	.name		= "aclk_vepu",
	.parent		= &codec_pll_clk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_ACLK_VEPU_SRC,
	.recalc		= clksel_recalc_div,
	.clksel_con	= CRU_CLKSELS_CON(32),
	.set_rate	= clkset_rate_freediv_autosel_parents,
	CRU_DIV_SET(0x1f, 0, 32),
	CRU_SRC_SET(0x1, 7),
	CRU_PARENTS_SET(clk_aclk_vepu_parents),
};
static struct clk aclk_vdpu = {
	.name		= "aclk_vdpu",
	.parent		= &clk_cpu_div,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_ACLK_VDPU_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clkset_rate_freediv_autosel_parents,	
	.clksel_con	= CRU_CLKSELS_CON(32),
	CRU_DIV_SET(0x1f, 8, 32),
	CRU_SRC_SET(0x1, 15),	
	CRU_PARENTS_SET(clk_aclk_vdpu_parents),
};
static struct clk hclk_vepu = {
	.name		= "hclk_vepu",
	.parent		= &aclk_vepu,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_HCLK_VEPU,
	.recalc		= clksel_recalc_fixed_div4,
};
static struct clk hclk_vdpu = {
	.name		= "hclk_vdpu",
	.parent		= &aclk_vdpu,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_HCLK_VDPU,
	.recalc		= clksel_recalc_fixed_div4,
};

/****************vio*******************/
// name: lcdc0_aclk
static struct clk *clk_aclk_vio_pre_parents[]		= SELECT_FROM_2PLLS;
static struct clk aclk_vio_pre = {
	.name		= "aclk_vio_pre",
	.parent		= &clk_cpu_div,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_ACLK_VIO_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.clksel_con	= CRU_CLKSELS_CON(31),
	CRU_DIV_SET(0x1f, 0, 32),
	CRU_PARENTS_SET(clk_aclk_vio_pre_parents),
};
static struct clk hclk_vio_pre = {
	.name		= "hclk_vio_pre",
	.parent		= &aclk_vio_pre,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_HCLK_VIO_PRE,
	.recalc		= clksel_recalc_fixed_div4,
};

/****************periph*******************/
static struct clk *peri_aclk_parents[]		= SELECT_FROM_2PLLS;
static struct clk peri_aclk = {
	.name		= "peri_aclk",
	.parent		= &general_pll_clk,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_PERIPH_SRC,
	.clksel_con	= CRU_CLKSELS_CON(10),
	CRU_DIV_SET(PERI_ACLK_DIV_MASK, PERI_ACLK_DIV_SHIFT, 32),
	CRU_SRC_SET(0x1, PERI_PLL_SEL_SHIFT),	
	CRU_PARENTS_SET(peri_aclk_parents),
};

static struct clk peri_hclk = {
	.name		= "peri_hclk",
	.parent		= &peri_aclk,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_shift,
	.clksel_con	= CRU_CLKSELS_CON(10),
	CRU_DIV_SET(PERI_HCLK_DIV_MASK, PERI_HCLK_DIV_SHIFT, 8),
};

static struct clk peri_pclk = {
	.name		= "peri_pclk",
	.parent		= &peri_aclk,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_shift,
	.clksel_con	= CRU_CLKSELS_CON(10),
	CRU_DIV_SET(PERI_PCLK_DIV_MASK, PERI_PCLK_DIV_SHIFT, 4),
};

static struct clk aclk_periph_pre = {
	.name		= "aclk_periph_pre",
	.parent		= &peri_aclk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_ACLK_PERIPH,
	.recalc		= clksel_recalc_equal_parent,
};

static struct clk hclk_periph_pre = {
	.name		= "hclk_periph_pre",
	.parent		= &peri_hclk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_HCLK_PERIPH,
	.recalc		= clksel_recalc_equal_parent,
};

static struct clk pclk_periph_pre = {
	.name		= "pclk_periph_pre",
	.parent		= &peri_pclk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_PCLK_PERIPH,
	.recalc		= clksel_recalc_equal_parent,
};
/****************timer*******************/
static struct clk *clk_timer0_parents[]		= {&xin24m, &peri_pclk};
static struct clk *clk_timer1_parents[]		= {&xin24m, &peri_pclk};
static struct clk clk_timer0 = {
	.name		= "timer0",
	.parent		= &xin24m,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_TIMER0,
	.recalc		= clksel_recalc_equal_parent,
	.clksel_con	= CRU_CLKSELS_CON(10),
	CRU_SRC_SET(0x1, 4),	
	CRU_PARENTS_SET(clk_timer0_parents),
};
static struct clk clk_timer1 = {
	.name		= "timer1",
	.parent		= &xin24m,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_TIMER1,
	.recalc		= clksel_recalc_equal_parent,
	.clksel_con	= CRU_CLKSELS_CON(10),
	CRU_SRC_SET(0x1, 5),	
	CRU_PARENTS_SET(clk_timer1_parents),
};
/****************spi*******************/
static struct clk clk_spi = {
	.name		= "spi",
	.parent		= &peri_pclk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_SPI0_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.clksel_con	= CRU_CLKSELS_CON(25),
	CRU_DIV_SET(0x7f, 0, 128),
};
/****************sdmmc*******************/
static struct clk *clk_sdmmc0_parents[]		= SELECT_FROM_2PLLS;
static struct clk clk_sdmmc0 = {
	.name		= "sdmmc0",
	.parent		= &general_pll_clk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_MMC0_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.clksel_con	= CRU_CLKSELS_CON(11),
	CRU_SRC_SET(0x1, 6),	
	CRU_DIV_SET(0x3f,0,64),
	CRU_PARENTS_SET(clk_sdmmc0_parents),
};
#if 0
static struct clk clk_sdmmc0_sample = {
	.name		= "sdmmc0_sample",
	.parent		= &general_pll_clk,
	.recalc		= ,
	//.set_rate	= ,
	.clksel_con	= ,
	CRU_DIV_SET(,,),
};
static struct clk clk_sdmmc0_drv = {
	.name		= "sdmmc0_drv",
	.parent		= &clk_sdmmc0,
	.recalc		= ,
	//.set_rate	= ,
	.clksel_con	= ,
	CRU_DIV_SET(,,),
};
#endif
/****************sdio*******************/
static struct clk *clk_sdio_parents[]		= SELECT_FROM_2PLLS;
static struct clk clk_sdio = {
	.name		= "sdio",
	.parent		= &general_pll_clk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_SDIO_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.clksel_con 	= CRU_CLKSELS_CON(12),
	CRU_DIV_SET(0x3f,0,64),
	CRU_PARENTS_SET(clk_sdio_parents),
};
#if 0
static struct clk clk_sdio_sample = {
	.name		= "sdio_sample",
	.parent		= &general_pll_clk,
	.recalc		= ,
	//.set_rate	= ,
	.clksel_con	= ,
	CRU_DIV_SET(,,),
};
static struct clk clk_sdio_drv = {
	.name		= "sdio_drv",
	.parent		= &clk_sdio,
	.recalc		= ,
	//.set_rate	= ,
	.clksel_con	= ,
	CRU_DIV_SET(,,),
};
#endif 
/****************emmc*******************/
static struct clk *clk_emmc_parents[]		= SELECT_FROM_2PLLS;
static struct clk clk_emmc = {
	.name		= "emmc",
	.parent		= &general_pll_clk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_EMMC_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.clksel_con =CRU_CLKSELS_CON(12),
	CRU_DIV_SET(0x3f,8,64),
	CRU_PARENTS_SET(clk_emmc_parents),
};
#if 0
static struct clk clk_emmc_sample = {
	.name		= "emmc_sample",
	.parent		= &general_pll_clk,
	.recalc		= ,
	//.set_rate	= ,
	.clksel_con	= ,
	CRU_DIV_SET(,,),
};
static struct clk clk_emmc_drv = {
	.name		= "emmc_drv",
	.parent		= &clk_emmc,
	.recalc		= ,
	//.set_rate	= ,
	.clksel_con	= ,
	CRU_DIV_SET(,,),
};
#endif
/****************lcdc*******************/
static struct clk *dclk_lcdc_parents[]		= {&arm_pll_clk, &general_pll_clk, &codec_pll_clk};
static struct clk dclk_lcdc = {
	.name		= "dclk_lcdc",
	.parent		= &general_pll_clk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_DCLK_LCDC0_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clkset_rate_freediv_autosel_parents,
	.clksel_con	= CRU_CLKSELS_CON(27),
	CRU_DIV_SET(0xff, 8, 256),
	CRU_SRC_SET(0x3, 0),
	CRU_PARENTS_SET(dclk_lcdc_parents),
};
static struct clk *sclk_lcdc_parents[]		= SELECT_FROM_2PLLS;
static struct clk sclk_lcdc = {
	.name		= "sclk_lcdc",
	.parent		= &general_pll_clk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_SCLK_LCDC_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clkset_rate_freediv_autosel_parents,
	.clksel_con	= CRU_CLKSELS_CON(28),
	CRU_DIV_SET(0xff, 8, 256),
	CRU_SRC_SET(0x1, 0),
	CRU_PARENTS_SET(sclk_lcdc_parents),
};
/****************gps*******************/
#if 0
static struct clk hclk_gps_parents		= SELECT_FROM_2PLLS;
static struct clk hclk_gps = {
	.name		= "hclk_gps",
	.parent		= &general_pll_clk,
	.recalc		= ,
	//.set_rate	= ,
	.clksel_con	= ,
	CRU_DIV_SET(,,),
};
#endif
/****************camera*******************/
static struct clk *clk_cif_out_div_parents[]		= SELECT_FROM_2PLLS;
static struct clk clk_cif_out_div = {
	.name		= "cif_out_div",
	.parent		= &general_pll_clk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_CIF_OUT_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clkset_rate_freediv_autosel_parents,
	.clksel_con	= CRU_CLKSELS_CON(29),
	CRU_SRC_SET(0x1, 0),
	CRU_DIV_SET(0x1f, 1, 32),
	CRU_PARENTS_SET(clk_cif_out_div_parents),
};
static struct clk *clk_cif_out_parents[]		= {&xin24m, &clk_cif_out_div};
static struct clk clk_cif_out = {
	.name		= "cif0_out",
	.parent		= &clk_cif_out_div,
	.clksel_con	= CRU_CLKSELS_CON(29),
	CRU_SRC_SET(0x1, 7),
	CRU_PARENTS_SET(clk_cif_out_parents),
};

/*External clock*/
static struct clk pclkin_cif0 = {
	.name		= "pclkin_cif0",
	.mode		= gate_mode,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_PCLKIN_CIF,	
};

static struct clk inv_cif0 = {
	.name		= "inv_cif0",
	.parent		= &pclkin_cif0,
};

static struct clk *cif0_in_parents[]			= {&pclkin_cif0, &inv_cif0};
static struct clk cif0_in = {
	.name		= "cif0_in",
	.parent		= &pclkin_cif0,
	.clksel_con	= CRU_CLKSELS_CON(30),
	CRU_SRC_SET(0x1, 8),
	CRU_PARENTS_SET(cif0_in_parents),
};

/****************i2s*******************/
#define I2S_SRC_12M  (0x0)
#define I2S_SRC_DIV  (0x1)
#define I2S_SRC_FRAC  (0x2)

static int i2s_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;
	struct clk *parent;

	if (rate == clk->parents[I2S_SRC_12M]->rate){
		parent = clk->parents[I2S_SRC_12M];
	}else if((long)clk_round_rate_nolock(clk->parents[I2S_SRC_DIV],rate)==rate)
	{
		parent = clk->parents[I2S_SRC_DIV]; 
	}
	else 
	{
		parent =clk->parents[I2S_SRC_FRAC];
	}

	CLKDATA_DBG(" %s set rate=%lu parent %s(old %s)\n",
		clk->name,rate,parent->name,clk->parent->name);

	if(parent!=clk->parents[I2S_SRC_12M])
	{
		ret = clk_set_rate_nolock(parent,rate);//div 1:1
		if (ret)
		{
			CLKDATA_DBG("%s set rate%lu err\n",clk->name,rate);
			return ret;
		}
	}

	if (clk->parent != parent)
	{
		ret = clk_set_parent_nolock(clk, parent);
		if (ret)
		{
			CLKDATA_DBG("%s can't get rate%lu,reparent err\n",clk->name,rate);
			return ret;
		}
	}

	return ret;
};
static struct clk *clk_i2s_div_parents[]		= SELECT_FROM_2PLLS;
static struct clk clk_i2s_pll = {
	.name		= "i2s_pll",
	.parent		= &general_pll_clk,
	.clksel_con	= CRU_CLKSELS_CON(2),
	CRU_SRC_SET(0x1,15),
	CRU_PARENTS_SET(clk_i2s_div_parents),
};

static struct clk clk_i2s_div = {
	.name		= "i2s_div",
	.parent		= &clk_i2s_pll,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_I2S_SRC,	
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.round_rate	= clksel_freediv_round_rate,
	.clksel_con	= CRU_CLKSELS_CON(3),
	CRU_DIV_SET(0x7f, 0, 64),
};
static struct clk clk_i2s_frac_div = {
	.name		= "i2s_frac_div",
	.parent		= &clk_i2s_div,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.round_rate	= clksel_freediv_round_rate,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_I2S_FRAC_SRC,
	.clksel_con	= CRU_CLKSELS_CON(7),
};

static struct clk *clk_i2s_parents[]		= {&clk_12m, &clk_i2s_div, &clk_i2s_frac_div};
static struct clk clk_i2s = {
	.name		= "i2s",
	.parent		= &clk_i2s_div,
	.set_rate	= i2s_set_rate,
	.clksel_con	= CRU_CLKSELS_CON(3),
	CRU_SRC_SET(0x3, 8),
	CRU_PARENTS_SET(clk_i2s_parents),
};

/****************otgphy*******************/
#if 0
static struct clk clk_otgphy0 = {
	.name		= "otgphy0",
	.parent		= &clk_12m,
	.recalc		= ,
	//.set_rate	= ,
	.clksel_con	= ,
	CRU_DIV_SET(,,),
};
static struct clk clk_otgphy1 = {
	.name		= "otgphy1",
	.parent		= &clk_12m,
	.recalc		= ,
	//.set_rate	= ,
	.clksel_con	= ,
	CRU_DIV_SET(,,),
};
#endif
GATE_CLK(otgphy0, clk_12m, OTGPHY0);
GATE_CLK(otgphy1, clk_12m, OTGPHY1);
/****************saradc*******************/
static struct clk clk_saradc = {
	.name		= "saradc",
	.parent		= &xin24m,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_SARADC_SRC,
	.clksel_con	= CRU_CLKSELS_CON(24),
	CRU_DIV_SET(0xff,8,256),
};
/****************gpu_pre*******************/
// name: gpu_aclk
static struct clk *clk_gpu_pre_parents[]		= SELECT_FROM_2PLLS;
static struct clk clk_gpu_pre = {
	.name		= "gpu_pre",
	.parent		= &general_pll_clk,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_GPU_PRE,
	.recalc		= clksel_recalc_div,
	.set_rate	= clkset_rate_freediv_autosel_parents,
	.round_rate	= clk_freediv_round_autosel_parents_rate,
	.clksel_con	= CRU_CLKSELS_CON(34),
	CRU_DIV_SET(0x1f, 0, 32),
	CRU_PARENTS_SET(clk_gpu_pre_parents),
};
/****************uart*******************/
static int clk_uart_fracdiv_set_rate(struct clk *clk, unsigned long rate)
{
	u32 numerator, denominator;
	//clk_uart0_div->clk_uart_pll->gpll/cpll
	//clk->parent->parent
	if(frac_div_get_seting(rate,clk->parent->parent->rate,
				&numerator,&denominator)==0)
	{
		clk_set_rate_nolock(clk->parent,clk->parent->parent->rate);//PLL:DIV 1:

		cru_writel_frac(numerator << 16 | denominator, clk->clksel_con);

		CLKDATA_DBG("%s set rate=%lu,is ok\n",clk->name,rate);
	}
	else
	{
		CLKDATA_ERR("clk_frac_div can't get rate=%lu,%s\n",rate,clk->name);
		return -ENOENT;
	} 
	return 0;
}
#define UART_SRC_DIV 0
#define UART_SRC_FRAC 1
#define UART_SRC_24M 2
static int clk_uart_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = 0;
	struct clk *parent;

	if(rate==clk->parents[UART_SRC_24M]->rate)//24m
	{	
		parent = clk->parents[UART_SRC_24M];
	}
	else if((long)clk_round_rate_nolock(clk->parents[UART_SRC_DIV], rate)==rate)
	{
		parent = clk->parents[UART_SRC_DIV];
	}
	else
	{
		parent = clk->parents[UART_SRC_FRAC];
	}



	CLKDATA_DBG(" %s set rate=%lu parent %s(old %s)\n",
			clk->name,rate,parent->name,clk->parent->name);


	if(parent!=clk->parents[UART_SRC_24M])
	{
		ret = clk_set_rate_nolock(parent,rate);	
		if (ret)
		{
			CLKDATA_DBG("%s set rate%lu err\n",clk->name,rate);
			return ret;
		}
	}

	if (clk->parent != parent)
	{
		ret = clk_set_parent_nolock(clk, parent);
		if (ret)
		{
			CLKDATA_DBG("%s can't get rate%lu,reparent err\n",clk->name,rate);
			return ret;
		}
	}


	return ret;
}


static struct clk *clk_uart_pll_src_parents[]	= SELECT_FROM_2PLLS;
static struct clk clk_uart_pll = {
	.name		= "uart_pll",
	.parent		= &general_pll_clk,
	.clksel_con 	= CRU_CLKSELS_CON(12),
	CRU_SRC_SET(0x1, 15),
	CRU_PARENTS_SET(clk_uart_pll_src_parents),
};
//static struct clk clk_uart0_div_parents		= SELECT_FROM_2PLLS;
//static struct clk clk_uart1_div_parents		= SELECT_FROM_2PLLS;
//static struct clk clk_uart2_div_parents		= SELECT_FROM_2PLLS;
static struct clk clk_uart0_div = {
	.name		= "uart0_div",
	.parent		= &clk_uart_pll,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_UART0_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.round_rate	= clksel_freediv_round_rate,
	.clksel_con	= CRU_CLKSELS_CON(13),
	CRU_DIV_SET(0x7f, 0, 64),	
};
static struct clk clk_uart1_div = {
	.name		= "uart1_div",
	.parent		= &clk_uart_pll,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_UART1_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.round_rate	= clksel_freediv_round_rate,
	.clksel_con	= CRU_CLKSELS_CON(15),
	CRU_DIV_SET(0x7f, 0, 64),	
};
static struct clk clk_uart2_div = {
	.name		= "uart2_div",
	.parent		= &clk_uart_pll,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_UART2_SRC,
	.recalc		= clksel_recalc_div,
	.set_rate	= clksel_set_rate_freediv,
	.round_rate	= clksel_freediv_round_rate,
	.clksel_con	= CRU_CLKSELS_CON(15),
	CRU_DIV_SET(0x7f, 0, 64),	
};
static struct clk clk_uart0_frac_div = {
	.name		= "uart0_frac_div",
	.parent		= &clk_uart0_div,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_UART0_FRAC_SRC,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_uart_fracdiv_set_rate,
	.clksel_con	= CRU_CLKSELS_CON(17),
};
static struct clk clk_uart1_frac_div = {
	.name		= "uart1_frac_div",
	.parent		= &clk_uart1_div,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_UART1_FRAC_SRC,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_uart_fracdiv_set_rate,
	.clksel_con	= CRU_CLKSELS_CON(18),
};
static struct clk clk_uart2_frac_div = {
	.name		= "uart2_frac_div",
	.parent		= &clk_uart2_div,
	.mode		= gate_mode,
	.gate_idx	= CLK_GATE_UART2_FRAC_SRC,
	.recalc		= clksel_recalc_frac,
	.set_rate	= clk_uart_fracdiv_set_rate,
	.clksel_con	= CRU_CLKSELS_CON(19),
};

static struct clk *clk_uart0_parents[]		= {&clk_uart0_div, &clk_uart0_frac_div, &xin24m};
static struct clk *clk_uart1_parents[]		= {&clk_uart1_div, &clk_uart1_frac_div, &xin24m};
static struct clk *clk_uart2_parents[]		= {&clk_uart2_div, &clk_uart2_frac_div, &xin24m};
static struct clk clk_uart0= {
	.name		= "uart0",
	.parent		= &xin24m,
	.set_rate	= clk_uart_set_rate,
	.clksel_con	= CRU_CLKSELS_CON(13),
	CRU_SRC_SET(0x3, 8),		
	CRU_PARENTS_SET(clk_uart0_parents),
};
static struct clk clk_uart1= {
	.name		= "uart1",
	.parent		= &xin24m,
	.set_rate	= clk_uart_set_rate,
	.clksel_con	= CRU_CLKSELS_CON(14),
	CRU_SRC_SET(0x3, 8),			
	CRU_PARENTS_SET(clk_uart1_parents),
};
static struct clk clk_uart2= {
	.name		= "uart2",
	.parent		= &xin24m,
	.set_rate	= clk_uart_set_rate,
	.clksel_con	= CRU_CLKSELS_CON(15),
	CRU_SRC_SET(0x3, 8),		
	CRU_PARENTS_SET(clk_uart2_parents),
};
/****************sub clock---pre*******************/
/*************************aclk_cpu***********************/
GATE_CLK(aclk_intmem,	aclk_cpu_pre,	ACLK_INTMEM);
GATE_CLK(aclk_strc_sys,	aclk_cpu_pre,	ACLK_STRC_SYS);

/*************************hclk_cpu***********************/
//FIXME
//GATE_CLK(hclk_cpubus,	hclk_cpu_pre, 	HCLK_CPUBUS);
GATE_CLK(hclk_rom,	hclk_cpu_pre,	HCLK_ROM);

/*************************pclk_cpu***********************/
//FIXME
GATE_CLK(pclk_hdmi,	pclk_cpu_pre, PCLK_HDMI);
GATE_CLK(pclk_ddrupctl,	pclk_cpu_pre, PCLK_DDRUPCTL);
GATE_CLK(pclk_grf,	pclk_cpu_pre, PCLK_GRF);
GATE_CLK(pclk_acodec,	pclk_cpu_pre, PCLK_ACODEC);

/*************************aclk_periph********************/
GATE_CLK(aclk_dma2,	aclk_periph_pre, ACLK_DMAC2);
GATE_CLK(aclk_peri_niu,	aclk_periph_pre, ACLK_PERI_NIU);
GATE_CLK(aclk_cpu_peri,	aclk_periph_pre, ACLK_CPU_PERI);
GATE_CLK(aclk_peri_axi_matrix,	aclk_periph_pre, ACLK_PERI_AXI_MATRIX);
//FIXME
//GATE_CLK(aclk_gps,	aclk_periph_pre, ACLK_GPS);

/*************************hclk_periph***********************/
GATE_CLK(hclk_peri_axi_matrix,	hclk_periph_pre, HCLK_PERI_AXI_MATRIX);
GATE_CLK(hclk_peri_ahb_arbi,	hclk_periph_pre, HCLK_PERI_ARBI);
GATE_CLK(hclk_nandc, hclk_periph_pre, HCLK_NANDC);
GATE_CLK(hclk_usb_peri, hclk_periph_pre, HCLK_USB_PERI);
GATE_CLK(hclk_otg0, hclk_periph_pre, HCLK_OTG0);
GATE_CLK(hclk_otg1, hclk_periph_pre, HCLK_OTG1);
GATE_CLK(hclk_i2s, hclk_periph_pre, HCLK_I2S);
GATE_CLK(hclk_sdmmc0, hclk_periph_pre, HCLK_SDMMC0);
GATE_CLK(hclk_sdio, hclk_periph_pre, HCLK_SDIO);
GATE_CLK(hclk_emmc, hclk_periph_pre, HCLK_EMMC);

/*************************pclk_periph***********************/
GATE_CLK(pclk_peri_axi_matrix, pclk_periph_pre, PCLK_PERI_AXI_MATRIX);
GATE_CLK(pclk_pwm01, pclk_periph_pre, PCLK_PWM01);
GATE_CLK(pclk_wdt, pclk_periph_pre, PCLK_WDT);
GATE_CLK(pclk_spi0, pclk_periph_pre, PCLK_SPI0);
GATE_CLK(pclk_uart0, pclk_periph_pre, PCLK_UART0);
GATE_CLK(pclk_uart1, pclk_periph_pre, PCLK_UART1);
GATE_CLK(pclk_uart2, pclk_periph_pre, PCLK_UART2);
GATE_CLK(pclk_i2c0, pclk_periph_pre, PCLK_I2C0);
GATE_CLK(pclk_i2c1, pclk_periph_pre, PCLK_I2C1);
GATE_CLK(pclk_i2c2, pclk_periph_pre, PCLK_I2C2);
GATE_CLK(pclk_i2c3, pclk_periph_pre, PCLK_I2C3);
GATE_CLK(pclk_timer0, pclk_periph_pre, PCLK_TIMER0);
GATE_CLK(pclk_timer1, pclk_periph_pre, PCLK_TIMER1);
GATE_CLK(pclk_gpio0, pclk_periph_pre, PCLK_GPIO0);
GATE_CLK(pclk_gpio1, pclk_periph_pre, PCLK_GPIO1);
GATE_CLK(pclk_gpio2, pclk_periph_pre, PCLK_GPIO2);
GATE_CLK(pclk_gpio3, pclk_periph_pre, PCLK_GPIO3);
GATE_CLK(pclk_saradc, pclk_periph_pre, PCLK_SARADC);
GATE_CLK(pclk_efuse, pclk_periph_pre, PCLK_EFUSE);

/*************************aclk_vio***********************/
GATE_CLK(aclk_vio0, aclk_vio_pre, ACLK_VIO0);
GATE_CLK(aclk_lcdc0, aclk_vio_pre, ACLK_LCDC0);
GATE_CLK(aclk_cif0, aclk_vio_pre, ACLK_CIF);
GATE_CLK(aclk_rga,  aclk_vio_pre, ACLK_RGA);

/*************************hclk_vio***********************/
GATE_CLK(hclk_lcdc0, hclk_vio_pre, HCLK_LCDC0);
GATE_CLK(hclk_cif0, hclk_vio_pre, HCLK_CIF);
GATE_CLK(hclk_rga,  hclk_vio_pre, HCLK_RGA);
GATE_CLK(hclk_vio_bus, hclk_vio_pre, HCLK_VIO_BUS);

/* Power domain, not exist in fact*/
enum pmu_power_domain {
	PD_A9_0 = 0,
	PD_A9_1,
	PD_ALIVE,
	PD_RTC,
	PD_SCU,
	PD_CPU,
	PD_PERI = 6,
	PD_VIO,
	PD_VIDEO,
	PD_VCODEC = PD_VIDEO,
	PD_GPU,
	PD_DBG,
};

static int pm_off_mode(struct clk *clk, int on)
{
	return 0;
}
static struct clk pd_peri = {
	.name		= "pd_peri",
	.flags		= IS_PD,
	.mode		= pm_off_mode,
	.gate_idx	= PD_PERI,
};

static int pd_display_mode(struct clk *clk, int on)
{
	return 0;
}

static struct clk pd_display = {
	.name		= "pd_display",
	.flags  	= IS_PD,
	.mode		= pd_display_mode,
	.gate_idx	= PD_VIO,
};

static struct clk pd_lcdc0 = {
	.parent		= &pd_display,
	.name		= "pd_lcdc0",
};
static struct clk pd_lcdc1 = {
	.parent		= &pd_display,
	.name		= "pd_lcdc1",
};
static struct clk pd_cif0 = {
	.parent		= &pd_display,
	.name		= "pd_cif0",
};
static struct clk pd_cif1 = {
	.parent		= &pd_display,
	.name		= "pd_cif1",
};
static struct clk pd_rga = {
	.parent		= &pd_display,
	.name		= "pd_rga",
};
static struct clk pd_ipp = {
	.parent		= &pd_display,
	.name		= "pd_ipp",
};
static int pd_video_mode(struct clk *clk, int on)
{
	return 0;
}

static struct clk pd_video = {
	.name		= "pd_video",
	.flags  	= IS_PD,
	.mode		= pd_video_mode,
	.gate_idx	= PD_VIDEO,
};

static int pd_gpu_mode(struct clk *clk, int on)
{
	return 0;
}

static struct clk pd_gpu = {
	.name		= "pd_gpu",
	.flags  	= IS_PD,
	.mode		= pd_gpu_mode,
	.gate_idx	= PD_GPU,
};
static struct clk pd_dbg = {
	.name		= "pd_dbg",
	.flags  	= IS_PD,
	.mode		= pm_off_mode,
	.gate_idx	= PD_DBG,
};

#define PD_CLK(name) \
{\
	.dev_id = NULL,\
	.con_id = #name,\
	.clk = &name,\
}
/* Power domain END, not exist in fact*/

#define CLK(dev, con, ck) \
{\
	.dev_id = dev,\
	.con_id = con,\
	.clk = ck,\
}

#define CLK_GATE_NODEV(name) \
{\
	.dev_id = NULL,\
	.con_id = #name,\
	.clk = &clk_##name,\
}

static struct clk_lookup clks[] = {
	CLK(NULL, "xin24m", &xin24m),
	CLK(NULL, "xin12m", &clk_12m),

	CLK(NULL, "arm_pll", &arm_pll_clk),
	CLK(NULL, "ddr_pll", &ddr_pll_clk),
	CLK(NULL, "codec_pll", &codec_pll_clk),
	CLK(NULL, "general_pll", &general_pll_clk),

	CLK(NULL, "ddrphy2x", &clk_ddrphy2x),
	CLK(NULL, "ddrphy", &clk_ddrphy),
	CLK(NULL, "ddrc", &clk_ddrc),

	CLK(NULL, "cpu", &clk_core_pre),
	CLK(NULL, "core_periph", &clk_core_periph),
	CLK(NULL, "core_periph_en", &clken_core_periph),
	CLK(NULL, "l2c", &clk_l2c),
	CLK(NULL, "aclk_core_pre", &aclk_core_pre),

	CLK(NULL, "cpu_div", &clk_cpu_div),
	CLK(NULL, "aclk_cpu_pre", &aclk_cpu_pre),
	CLK(NULL, "pclk_cpu_pre", &pclk_cpu_pre),
	CLK(NULL, "hclk_cpu_pre", &hclk_cpu_pre),

	CLK(NULL, "aclk_vepu", &aclk_vepu),
	CLK(NULL, "aclk_vdpu", &aclk_vdpu),
	CLK(NULL, "hclk_vepu", &hclk_vepu),
	CLK(NULL, "hclk_vdpu", &hclk_vdpu),

	CLK(NULL, "aclk_vio_pre", &aclk_vio_pre),
	CLK(NULL, "hclk_vio_pre", &hclk_vio_pre),

	CLK(NULL, "peri_aclk", &peri_aclk),
	CLK(NULL, "peri_pclk", &peri_pclk),
	CLK(NULL, "peri_hclk", &peri_hclk),

	CLK(NULL, "timer0", &clk_timer0),
	CLK(NULL, "timer1", &clk_timer1),

	CLK("rk29xx_spim.0", "spi", &clk_spi),

	CLK("rk29_sdmmc.0", "mmc", &clk_sdmmc0),
	//CLK("rk29_sdmmc.0", "mmc_sample", &clk_sdmmc0_sample),
	//CLK("rk29_sdmmc.0", "mmc_drv", &clk_sdmmc0_drv),

	CLK("rk29_sdmmc.1", "mmc", &clk_sdio),
	//CLK("rk29_sdmmc.1", "mmc_sample", &clk_sdio_sample),
	//CLK("rk29_sdmmc.1", "mmc_drv", &clk_sdio_drv),

	CLK(NULL, "emmc", &clk_emmc),
	//CLK(NULL, "emmc_sample", &clk_emmc_sample),
	//CLK(NULL, "emmc_drv", &clk_emmc_drv),

	CLK(NULL, "dclk_lcdc0", &dclk_lcdc),
	CLK(NULL, "sclk_lcdc0", &sclk_lcdc),
	//FIXME
	//CLK(NULL, "hclk_gps", &hclk_gps),

	CLK(NULL, "cif_out_div", &clk_cif_out_div),
	CLK(NULL, "cif0_out", &clk_cif_out),
	CLK(NULL, "pclkin_cif0", &pclkin_cif0),
	CLK(NULL, "inv_cif0", &inv_cif0),
	CLK(NULL, "cif0_in", &cif0_in),

	CLK(NULL, "i2s_pll", &clk_i2s_pll),
	CLK("rk29_i2s.0", "i2s_div", &clk_i2s_div),
	CLK("rk29_i2s.0", "i2s_frac_div", &clk_i2s_frac_div),
	CLK("rk29_i2s.0", "i2s", &clk_i2s),

	CLK(NULL, "otgphy0", &clk_otgphy0),
	CLK(NULL, "otgphy1", &clk_otgphy1),
	CLK(NULL, "saradc", &clk_saradc),
	CLK(NULL, "gpu", &clk_gpu_pre),

	CLK(NULL, "uart_pll", &clk_uart_pll),
	CLK("rk_serial.0", "uart_div", &clk_uart0_div),
	CLK("rk_serial.1", "uart_div", &clk_uart1_div),
	CLK("rk_serial.2", "uart_div", &clk_uart2_div),
	CLK("rk_serial.0", "uart_frac_div", &clk_uart0_frac_div),
	CLK("rk_serial.1", "uart_frac_div", &clk_uart1_frac_div),
	CLK("rk_serial.2", "uart_frac_div", &clk_uart2_frac_div),
	CLK("rk_serial.0", "uart", &clk_uart0),
	CLK("rk_serial.1", "uart", &clk_uart1),
	CLK("rk_serial.2", "uart", &clk_uart2),

	CLK(NULL, "aclk_periph_pre", &aclk_periph_pre),
	CLK(NULL, "hclk_periph_pre", &hclk_periph_pre),
	CLK(NULL, "pclk_periph_pre", &pclk_periph_pre),

	/*********fixed clock ******/
	CLK_GATE_NODEV(aclk_intmem),
	CLK_GATE_NODEV(aclk_strc_sys),

	//FIXME
	//CLK_GATE_NODEV(hclk_cpubus),
	CLK_GATE_NODEV(hclk_rom),

	//FIXME
	CLK_GATE_NODEV(pclk_hdmi),
	CLK_GATE_NODEV(pclk_ddrupctl),
	CLK_GATE_NODEV(pclk_grf),
	CLK_GATE_NODEV(pclk_acodec),

	CLK_GATE_NODEV(aclk_dma2),
	CLK_GATE_NODEV(aclk_peri_niu),
	CLK_GATE_NODEV(aclk_cpu_peri),
	CLK_GATE_NODEV(aclk_peri_axi_matrix),
	//FIXME
	//CLK_GATE_NODEV(aclk_gps),

	CLK_GATE_NODEV(hclk_peri_axi_matrix),
	CLK_GATE_NODEV(hclk_peri_ahb_arbi),
	CLK_GATE_NODEV(hclk_nandc),
	CLK_GATE_NODEV(hclk_usb_peri),
	CLK_GATE_NODEV(hclk_otg0),
	CLK_GATE_NODEV(hclk_otg1),
	CLK_GATE_NODEV(hclk_i2s),
	CLK("rk29_sdmmc.0", "hclk_mmc", &clk_hclk_sdmmc0),
	CLK("rk29_sdmmc.1", "hclk_mmc", &clk_hclk_sdio),
	CLK("rk29_sdmmc.2", "hclk_mmc", &clk_hclk_emmc),

	CLK_GATE_NODEV(pclk_peri_axi_matrix),
	CLK(NULL, "pwm01", &clk_pclk_pwm01),
	CLK_GATE_NODEV(pclk_wdt),
	CLK_GATE_NODEV(pclk_spi0),
	CLK("rk_serial.0", "pclk_uart", &clk_pclk_uart0),
	CLK("rk_serial.1", "pclk_uart", &clk_pclk_uart1),
	CLK("rk_serial.2", "pclk_uart", &clk_pclk_uart2),
	CLK("rk30_i2c.0", "i2c", &clk_pclk_i2c0),
	CLK("rk30_i2c.1", "i2c", &clk_pclk_i2c1),
	CLK("rk30_i2c.2", "i2c", &clk_pclk_i2c2),
	CLK("rk30_i2c.3", "i2c", &clk_pclk_i2c3),
	CLK_GATE_NODEV(pclk_timer0),
	CLK_GATE_NODEV(pclk_timer1),
	CLK_GATE_NODEV(pclk_gpio0),
	CLK_GATE_NODEV(pclk_gpio1),
	CLK_GATE_NODEV(pclk_gpio2),
	CLK_GATE_NODEV(pclk_gpio3),
	CLK_GATE_NODEV(pclk_saradc),
	CLK_GATE_NODEV(pclk_efuse),

	CLK_GATE_NODEV(aclk_vio0),
	CLK_GATE_NODEV(aclk_lcdc0),
	CLK_GATE_NODEV(aclk_cif0),
	CLK_GATE_NODEV(aclk_rga),

	CLK_GATE_NODEV(hclk_lcdc0),
	CLK_GATE_NODEV(hclk_cif0),
	CLK_GATE_NODEV(hclk_rga),
	CLK_GATE_NODEV(hclk_vio_bus),

	/* Power domain, not exist in fact*/
	PD_CLK(pd_peri),
	PD_CLK(pd_display),
	PD_CLK(pd_video),
	PD_CLK(pd_lcdc0),
	PD_CLK(pd_lcdc1),
	PD_CLK(pd_cif0),
	PD_CLK(pd_cif1),
	PD_CLK(pd_rga),
	PD_CLK(pd_ipp),
	PD_CLK(pd_video),
	PD_CLK(pd_gpu),
	PD_CLK(pd_dbg),

};

static void __init rk30_init_enable_clocks(void)
{
	CLKDATA_DBG("ENTER %s\n", __func__);
	clk_enable_nolock(&clk_core_pre);	//cpu
	clk_enable_nolock(&clk_core_periph);
	clk_enable_nolock(&aclk_cpu_pre);
	clk_enable_nolock(&hclk_cpu_pre);
	clk_enable_nolock(&pclk_cpu_pre);

	clk_enable_nolock(&aclk_periph_pre);
	clk_enable_nolock(&pclk_periph_pre);
	clk_enable_nolock(&hclk_periph_pre);

#if CONFIG_RK_DEBUG_UART == 0
	clk_enable_nolock(&clk_uart0);
	clk_enable_nolock(&clk_pclk_uart0);

#elif CONFIG_RK_DEBUG_UART == 1
	clk_enable_nolock(&clk_uart1);
	clk_enable_nolock(&clk_pclk_uart1);

#elif CONFIG_RK_DEBUG_UART == 2
	clk_enable_nolock(&clk_uart2);
	clk_enable_nolock(&clk_pclk_uart2);
#endif

	/*************************aclk_cpu***********************/
	clk_enable_nolock(&clk_aclk_intmem);
	clk_enable_nolock(&clk_aclk_strc_sys);

	/*************************hclk_cpu***********************/
	clk_enable_nolock(&clk_hclk_rom);

	/*************************pclk_cpu***********************/
	clk_enable_nolock(&clk_pclk_ddrupctl);
	clk_enable_nolock(&clk_pclk_grf);

	/*************************aclk_periph***********************/
	clk_enable_nolock(&clk_aclk_dma2);
	clk_enable_nolock(&clk_aclk_peri_niu);
	clk_enable_nolock(&clk_aclk_cpu_peri);
	clk_enable_nolock(&clk_aclk_peri_axi_matrix);

	/*************************hclk_periph***********************/
	clk_enable_nolock(&clk_hclk_peri_axi_matrix);
	clk_enable_nolock(&clk_hclk_peri_ahb_arbi);
	clk_enable_nolock(&clk_hclk_nandc);

	/*************************pclk_periph***********************/
	clk_enable_nolock(&clk_pclk_peri_axi_matrix);
	/*************************hclk_vio***********************/
	clk_enable_nolock(&clk_hclk_vio_bus);
}

#ifdef CONFIG_PROC_FS

static void dump_clock(struct seq_file *s, struct clk *clk, int deep,const struct list_head *root_clocks)
{
	struct clk* ck;
	int i;
	unsigned long rate = clk->rate;
	//CLKDATA_DBG("dump_clock %s\n",clk->name);
	for (i = 0; i < deep; i++)
		seq_printf(s, "    ");

	seq_printf(s, "%-11s ", clk->name);

	if ((clk->mode == gate_mode) && (clk->gate_idx < CLK_GATE_MAX)) {
		int idx = clk->gate_idx;
		u32 v;
		v = cru_readl(CLK_GATE_CLKID_CONS(idx))&((0x1)<<(idx%16));
		seq_printf(s, "%s ", v ? "off" : "on ");
	}

	if (clk->pll)
	{
		u32 pll_mode;
		u32 pll_id=clk->pll->id;
		pll_mode=cru_readl(CRU_MODE_CON)&PLL_MODE_MSK(pll_id);
		if(pll_mode==PLL_MODE_SLOW(pll_id))
			seq_printf(s, "slow   ");
		else if(pll_mode==PLL_MODE_NORM(pll_id))
			seq_printf(s, "normal ");
		if(cru_readl(PLL_CONS(pll_id,3)) & PLL_BYPASS) 
			seq_printf(s, "bypass ");
	}
	else if(clk == &ddr_pll_clk) {
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

	list_for_each_entry(ck, root_clocks, node) {
		if (ck->parent == clk)
			dump_clock(s, ck, deep + 1,root_clocks);
	}
}

static void dump_regs(struct seq_file *s)
{
	int i=0;
	seq_printf(s, "\nPLL(id=0 apll,id=1,dpll,id=2,cpll,id=3 cpll)\n");
	seq_printf(s, "\nPLLRegisters:\n");
	for(i=0;i<END_PLL_ID;i++)
	{
		seq_printf(s,"pll%d        :cons:%x,%x,%x,%x\n",i,
				cru_readl(PLL_CONS(i,0)),
				cru_readl(PLL_CONS(i,1)),
				cru_readl(PLL_CONS(i,2)),
				cru_readl(PLL_CONS(i,3))
			  );
	}
	seq_printf(s, "MODE        :%x\n", cru_readl(CRU_MODE_CON));

	for(i=0;i<CRU_CLKSELS_CON_CNT;i++)
	{
		seq_printf(s,"CLKSEL%d 	   :%x\n",i,cru_readl(CRU_CLKSELS_CON(i)));
	}
	for(i=0;i<CRU_CLKGATES_CON_CNT;i++)
	{
		seq_printf(s,"CLKGATE%d 	  :%x\n",i,cru_readl(CRU_CLKGATES_CON(i)));
	}
	seq_printf(s,"GLB_SRST_FST:%x\n",cru_readl(CRU_GLB_SRST_FST));
	seq_printf(s,"GLB_SRST_SND:%x\n",cru_readl(CRU_GLB_SRST_SND));

	for(i=0;i<CRU_SOFTRSTS_CON_CNT;i++)
	{
		seq_printf(s,"CLKGATE%d 	  :%x\n",i,cru_readl(CRU_SOFTRSTS_CON(i)));
	}
	seq_printf(s,"CRU MISC    :%x\n",cru_readl(CRU_MISC_CON));
	seq_printf(s,"GLB_CNT_TH  :%x\n",cru_readl(CRU_GLB_CNT_TH));

}

void rk30_clk_dump_regs(void)
{
	int i=0;
	CLKDATA_DBG("\nPLL(id=0 apll,id=1,dpll,id=2,cpll,id=3 cpll)\n");
	CLKDATA_DBG("\nPLLRegisters:\n");
	for(i=0;i<END_PLL_ID;i++)
	{
		CLKDATA_DBG("pll%d        :cons:%x,%x,%x,%x\n",i,
				cru_readl(PLL_CONS(i,0)),
				cru_readl(PLL_CONS(i,1)),
				cru_readl(PLL_CONS(i,2)),
				cru_readl(PLL_CONS(i,3))
		      );
	}
	CLKDATA_DBG("MODE        :%x\n", cru_readl(CRU_MODE_CON));

	for(i=0;i<CRU_CLKSELS_CON_CNT;i++)
	{
		CLKDATA_DBG("CLKSEL%d 	   :%x\n",i,cru_readl(CRU_CLKSELS_CON(i)));
	}
	for(i=0;i<CRU_CLKGATES_CON_CNT;i++)
	{
		CLKDATA_DBG("CLKGATE%d 	  :%x\n",i,cru_readl(CRU_CLKGATES_CON(i)));
	}
	CLKDATA_DBG("GLB_SRST_FST:%x\n",cru_readl(CRU_GLB_SRST_FST));
	CLKDATA_DBG("GLB_SRST_SND:%x\n",cru_readl(CRU_GLB_SRST_SND));

	for(i=0;i<CRU_SOFTRSTS_CON_CNT;i++)
	{
		CLKDATA_DBG("SOFTRST%d 	  :%x\n",i,cru_readl(CRU_SOFTRSTS_CON(i)));
	}
	CLKDATA_DBG("CRU MISC    :%x\n",cru_readl(CRU_MISC_CON));
	CLKDATA_DBG("GLB_CNT_TH  :%x\n",cru_readl(CRU_GLB_CNT_TH));

}

static struct clk def_ops_clk={
	.get_parent=clksel_get_parent,
	.set_parent=clksel_set_parent,
};
#ifdef CONFIG_PROC_FS
static void dump_clock(struct seq_file *s, struct clk *clk, int deep,const struct list_head *root_clocks);
struct clk_dump_ops dump_ops={
	.dump_clk=dump_clock,
	.dump_regs=dump_regs,
};
#endif
#endif

static void periph_clk_set_init(void)
{
	unsigned long aclk_p, hclk_p, pclk_p;
	unsigned long ppll_rate=general_pll_clk.rate;
	//aclk 148.5
	
	/* general pll */
	switch (ppll_rate) {
	case 148500* KHZ:
		aclk_p = 148500*KHZ;
		hclk_p = aclk_p>>1;
		pclk_p = aclk_p>>2;
		break;
	case 1188*MHZ:
		aclk_p = aclk_p>>3;// 0 
		hclk_p = aclk_p>>1;
		pclk_p = aclk_p>>2;

	case 297 * MHZ:
		aclk_p = ppll_rate>>1;
		hclk_p = aclk_p>>0;
		pclk_p = aclk_p>>1;
		break;

	case 300 * MHZ:
		aclk_p = ppll_rate>>1;
		hclk_p = aclk_p>>0;
		pclk_p = aclk_p>>1;
		break;	
	default:
		aclk_p = 150 * MHZ;
		hclk_p = 150 * MHZ;
		pclk_p = 75 * MHZ;
		break;	
	}
	clk_set_parent_nolock(&aclk_periph_pre, &general_pll_clk);
	clk_set_rate_nolock(&aclk_periph_pre, aclk_p);
	clk_set_rate_nolock(&hclk_periph_pre, hclk_p);
	clk_set_rate_nolock(&pclk_periph_pre, pclk_p);
}


#define CLK_FLG_MAX_I2S_12288KHZ 	(1<<1)
#define CLK_FLG_MAX_I2S_22579_2KHZ 	(1<<2)
#define CLK_FLG_MAX_I2S_24576KHZ 	(1<<3)
#define CLK_FLG_MAX_I2S_49152KHZ 	(1<<4)

void rk2928_clock_common_i2s_init(void)
{
	unsigned long i2s_rate;
	//struct clk *max_clk,*min_clk;
	//20 times
	if(rk2928_clock_flags&CLK_FLG_MAX_I2S_49152KHZ)
	{
		i2s_rate=49152000;	
	}else if(rk2928_clock_flags&CLK_FLG_MAX_I2S_24576KHZ)
	{
		i2s_rate=24576000;
	}
	else if(rk2928_clock_flags&CLK_FLG_MAX_I2S_22579_2KHZ)
	{
		i2s_rate=22579000;
	}
	else if(rk2928_clock_flags&CLK_FLG_MAX_I2S_12288KHZ)
	{
		i2s_rate=12288000;
	}
	else
	{
		i2s_rate=49152000;	
	}	

	if(((i2s_rate*20)<=general_pll_clk.rate)||!(general_pll_clk.rate%i2s_rate))
	{
		clk_set_parent_nolock(&clk_i2s_pll, &general_pll_clk);
	}
	else if(((i2s_rate*20)<=codec_pll_clk.rate)||!(codec_pll_clk.rate%i2s_rate))
	{
		clk_set_parent_nolock(&clk_i2s_pll, &codec_pll_clk);
	}
	else
	{
		if(general_pll_clk.rate>codec_pll_clk.rate)	
			clk_set_parent_nolock(&clk_i2s_pll, &general_pll_clk);
		else
			clk_set_parent_nolock(&clk_i2s_pll, &codec_pll_clk);	
	}
		
}
static void __init rk2928_clock_common_init(unsigned long gpll_rate,unsigned long cpll_rate)
{
	CLKDATA_DBG("ENTER %s\n", __func__);

	clk_set_rate_nolock(&clk_core_pre, 650 * MHZ);//816
	//general
	clk_set_rate_nolock(&general_pll_clk, gpll_rate);
	//code pll
	clk_set_rate_nolock(&codec_pll_clk, cpll_rate);
	//periph clk
	periph_clk_set_init();

	//i2s
	rk2928_clock_common_i2s_init();

	// spi
	clk_set_rate_nolock(&clk_spi, clk_spi.parent->rate);

	// uart
#if 0 
	clk_set_parent_nolock(&clk_uart_pll, &codec_pll_clk);
#else
	clk_set_parent_nolock(&clk_uart_pll, &general_pll_clk);
#endif
	//mac	
	// FIXME
#if 0
	if(!(gpll_rate%(50*MHZ)))
		clk_set_parent_nolock(&clk_mac_pll_div, &general_pll_clk);
	else if(!(ddr_pll_clk.rate%(50*MHZ)))
		clk_set_parent_nolock(&clk_mac_pll_div, &ddr_pll_clk);
	else
		CRU_PRINTK_ERR("mac can't get 50mhz\n");
#endif
	//hsadc
	//auto pll sel
	//clk_set_parent_nolock(&clk_hsadc_pll_div, &general_pll_clk);

	//lcdc1  hdmi
	//clk_set_parent_nolock(&dclk_lcdc1_div, &general_pll_clk);

	//lcdc0 lcd auto sel pll
	//clk_set_parent_nolock(&dclk_lcdc0_div, &general_pll_clk);

	//cif
	clk_set_parent_nolock(&clk_cif_out_div, &general_pll_clk);

	//axi lcdc auto sel
	//clk_set_parent_nolock(&aclk_lcdc0, &general_pll_clk);
	//clk_set_parent_nolock(&aclk_lcdc1, &general_pll_clk);
	// FIXME
#if 0
	clk_set_rate_nolock(&aclk_lcdc0_ipp_parent, 300*MHZ);
	clk_set_rate_nolock(&aclk_lcdc1_rga_parent, 300*MHZ);
#endif
	//axi vepu auto sel
	//clk_set_parent_nolock(&aclk_vepu, &general_pll_clk);
	//clk_set_parent_nolock(&aclk_vdpu, &general_pll_clk);

	clk_set_rate_nolock(&aclk_vepu, 300*MHZ);
	clk_set_rate_nolock(&aclk_vdpu, 300*MHZ);
	//gpu auto sel
	//clk_set_parent_nolock(&clk_gpu, &general_pll_clk);
	//
}
void __init _rk2928_clock_data_init(unsigned long gpll,unsigned long cpll,int flags)
{
	struct clk_lookup *clk;
	clk_register_dump_ops(&dump_ops);
	clk_register_default_ops_clk(&def_ops_clk);

	rk2928_clock_flags = flags;

	CLKDATA_DBG("%s total %d clks\n", __func__, ARRAY_SIZE(clks));
	for (clk = clks; clk < clks + ARRAY_SIZE(clks); clk++) {
		CLKDATA_DBG("%s add dev_id=%s, con_id=%s\n", 
				__func__, clk->dev_id ? clk->dev_id : "NULL", clk->con_id ? clk->con_id : "NULL");
		clkdev_add(clk);
		clk_register(clk->clk);
	}

	CLKDATA_DBG("clk_recalculate_root_clocks_nolock\n");
	clk_recalculate_root_clocks_nolock();

	loops_per_jiffy = CLK_LOOPS_RECALC(arm_pll_clk.rate);

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	rk30_init_enable_clocks();

	/*
	 * Disable any unused clocks left on by the bootloader
	 */
	//clk_disable_unused();
	CLKDATA_DBG("rk2928_clock_common_init, gpll=%lu, cpll=%lu\n", gpll, cpll);
	rk2928_clock_common_init(gpll, cpll);
	preset_lpj = loops_per_jiffy;

	CLKDATA_DBG("%s clks init finish\n", __func__);
}


void __init rk2928_clock_data_init(unsigned long gpll,unsigned long cpll,u32 flags)
{
	printk("%s version:	2012-8-7\n", __func__);
	_rk2928_clock_data_init(gpll,cpll,flags);
	//rk2928_dvfs_init();
}

