#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk-private.h>
#include "clk-ops.h"
#include <linux/delay.h>

/* mux_ops */
struct clk_ops_table rk_clk_mux_ops_table[] = {
	{.index = CLKOPS_TABLE_END},
};

#define to_clk_divider(_hw) container_of(_hw, struct clk_divider, hw)
#define div_mask(d)	((1 << ((d)->width)) - 1)

#define MHZ	(1000 * 1000)
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

static int clk_fracdiv_get_config(unsigned long rate_out, unsigned long rate,
		u32 *numerator, u32 *denominator)
{
	u32 gcd_val;
	gcd_val = clk_gcd(rate, rate_out);
	clk_debug("%s: frac_get_seting rate=%lu, parent=%lu, gcd=%d\n",
			__func__, rate_out, rate, gcd_val);

	if (!gcd_val) {
		clk_err("gcd=0, i2s frac div is not be supported\n");
		return -EINVAL;
	}

	*numerator = rate_out / gcd_val;
	*denominator = rate / gcd_val;

	clk_debug("%s: frac_get_seting numerator=%d, denominator=%d, times=%d\n",
			__func__, *numerator, *denominator,
			*denominator / *numerator);

	if (*numerator > 0xffff || *denominator > 0xffff ||
			(*denominator / (*numerator)) < 20) {
		clk_err("can't get a available nume and deno\n");
		return -EINVAL;
	}

	return 0;

}

static int clk_fracdiv_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	u32 numerator, denominator;
	struct clk_divider *div = to_clk_divider(hw);

	struct clk *clk_parent = hw->clk->parent;
	if(clk_fracdiv_get_config(rate, parent_rate,
				&numerator, &denominator) == 0) {

		clk_parent->ops->set_rate(clk_parent->hw,
				clk_parent->parent->rate,
				clk_parent->parent->rate);
		writel(numerator << 16 | denominator, div->reg);
		clk_err("%s set rate=%lu,is ok\n", hw->clk->name, rate);

	} else {
		clk_err("clk_frac_div can't get rate=%lu,%s\n",
				rate, hw->clk->name);
		return -ENOENT;
	}
	return 0;
}

static unsigned long clk_fracdiv_recalc(struct clk_hw *hw,
		unsigned long parent_rate)
{
	unsigned long rate;
	u64 rate64;
	struct clk_divider *div = to_clk_divider(hw);
	u32 numerator, denominator, reg_val;
	reg_val = readl(div->reg);
	if (reg_val == 0)
		return parent_rate;
	numerator = reg_val >> 16;
	denominator = reg_val & 0xFFFF;
	rate64 = (u64)parent_rate * numerator;
	do_div(rate64, denominator);
	rate = rate64;
	clk_debug("%s: %s new clock rate is %lu, prate %lu (frac %u/%u)\n",
			__func__, hw->clk->name, rate, parent_rate,
			numerator, denominator);
	return rate;
}
static long clk_fracdiv_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return rate;
}
/*************************************************************************/
/* rate_ops */
#define PARENTS_NUM_MAX 3
/*
 * get the best rate from array of available rates, regarding rate which is smaller than
 * and most close to the set_rate as the best.
 */
static long get_best_rate(unsigned long array[],unsigned int num, int *n, long rate)
{
	int i = 0;
	unsigned long best_rate = 0;

	for(i = 0;  i < num;  i++){
		if(array[i] == rate){
			*n = i;
			return array[i];
		}else if((array[i] < rate) && (array[i] > best_rate)){
			best_rate = array[i];
			*n = i;
		}
	}

	if(best_rate == 0){
		clk_err("NOT array rate is <= %lu\n", rate);
	}else{
		clk_debug("get the best available rate,but it != %lu  you want to set!\n", rate);
	}

	return best_rate;
}

static struct clk *clk_get_best_parent(struct clk_hw *hw, unsigned long rate,
		unsigned int *div_out)
{
	struct clk *clk = hw->clk;
	u32 div[PARENTS_NUM_MAX] = {0};
	unsigned long new_rate[PARENTS_NUM_MAX] = {0};
	unsigned long best_rate;
	u32 i;

	memset(div, 0, sizeof(div));
	memset(new_rate, 0, sizeof(new_rate));

	if(clk->rate == rate)
		return clk->parent;

	for(i = 0; i < clk->num_parents; i++) {
		new_rate[i] = clk_divider_ops.round_rate(hw, rate,
				&(clk->parents[i]->rate));
		div[i] = (clk->parents[i]->rate)/new_rate[i];
		if(new_rate[i] == rate) {
			*div_out = div[i];
			return clk->parents[i];
		}
	}

	best_rate = get_best_rate(new_rate, PARENTS_NUM_MAX, &i, rate);
	if(best_rate == 0){
		clk_err("NOT rate is good!\n");
		return NULL;
	}

	*div_out = div[i];

	return clk->parents[i];
}

static long clk_div_round_rate_autosel_parents(struct clk_hw *hw,
		unsigned long rate, unsigned long *prate)
{
	struct clk *clk = hw->clk;
	struct clk *new_parent;
	int new_div;

	if(clk->rate == rate)
		return rate;

	new_parent = clk_get_best_parent(hw, rate, &new_div);
	if(!new_parent || (new_div <= 0)){
		clk_err("%s: clk %s could not get new_parent or new_div\n",
				__func__,clk->name);
		return -EINVAL;
	}

	return (new_parent->rate)/new_div;
}


static int clk_div_set_rate_autosel_parents(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate)
{
	//struct clk_divider *divider = to_clk_divider(hw);
	struct clk *clk = hw->clk;
	struct clk *new_parent;
	unsigned int new_div,old_div;
	unsigned long new_rate;
	int ret = 0;
	u8 index;
	int i;

	if(clk->rate == rate)
		goto out;

	new_parent = clk_get_best_parent(hw, rate, &new_div);
	if(!new_parent || (new_div == 0)){
		clk_err("%s: clk %s could not get new_parent or get "
				"new_div = 0\n", __func__,clk->name);
		ret = -EINVAL;
		goto out;
	}

	old_div = (clk->parent->rate)/(clk->rate);

	clk_debug("%s:%d: %s: %lu\n", __func__, __LINE__,
			clk->parent->name, new_parent->rate);
	if(new_div > old_div){
		new_rate = (clk->parent->rate)/new_div;
		ret = clk_divider_ops.set_rate(hw, new_rate,
				(clk->parent->rate));
		if(ret)
			goto out;
	}

	if(clk->parent != new_parent){
		for(i=0; i<clk->num_parents; i++){
			if(new_parent == clk->parents[i]){
				index = i;
				break;
			}
		}
		/*
		 * ret = clk->ops->set_parent(clk->hw, index);
		 * if(ret)
		 *         goto out;
		 */
		clk_set_parent(clk, new_parent);
		clk->ops->recalc_rate(clk->hw, clk->parent->rate);
	}

	if(new_div <= old_div){
		new_rate = (clk->parent->rate)/new_div;
		ret = clk_divider_ops.set_rate(hw, new_rate,
				(clk->parent->rate));
		if(ret)
			goto out;
	}

out:
	return ret;
}

static unsigned long clk_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return clk_divider_ops.recalc_rate(hw, hw->clk->parent->rate);
}

const struct clk_ops clkops_rate_auto_parent = {
	.recalc_rate	= clk_divider_recalc_rate,
	.round_rate	= clk_div_round_rate_autosel_parents,
	.set_rate	= clk_div_set_rate_autosel_parents,
};

static long clk_div_round_rate_even(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	int i = 0;
	struct clk_divider *divider =to_clk_divider(hw);
	int max_div = 1 << divider->width;

	for (i = 1; i < max_div; i++) {
		if (i > 1 && (i % 2 != 0))
			continue;
		if (rate >= *prate / i)
			return *prate / i;
	}
	return -EINVAL;
}

static int clk_div_set_rate_even(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	return clk_divider_ops.set_rate(hw, rate, hw->clk->parent->rate);
}

const struct clk_ops clkops_rate_evendiv = {
	.recalc_rate	= clk_divider_recalc_rate,
	.round_rate	= clk_div_round_rate_even,
	.set_rate	= clk_div_set_rate_even,
};

static long dclk_lcdc_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	long ret = 0;
	if (rate == 27 * MHZ) {
		ret = clk_div_round_rate_autosel_parents(hw, rate, prate);
	} else {
		ret = clk_div_round_rate_autosel_parents(hw, rate, prate);
	}
	return ret;
}

static int dclk_lcdc_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	return clk_div_set_rate_autosel_parents(hw, rate, parent_rate);
}

const struct clk_ops clkops_rate_dclk_lcdc = {
	.recalc_rate	= clk_divider_recalc_rate,
	.round_rate	= dclk_lcdc_round_rate,
	.set_rate	= dclk_lcdc_set_rate,
};

#define CIF_OUT_SRC_DIV	(0x0)
#define CIF_OUT_SRC_24M	(0x1)

static unsigned long cif_out_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return hw->clk->parent->rate;
}

static long cif_out_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	struct clk *clk = hw->clk;
	struct clk *parent;

	if (rate == clk->parents[CIF_OUT_SRC_24M]->rate) {
		return rate;
	} else {
		parent = clk->parents[CIF_OUT_SRC_DIV];
		return parent->ops->round_rate(parent->hw, rate,
				&(parent->parent->rate));
	}
}

static int cif_out_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk *clk = hw->clk;
	struct clk *parent;
	int ret = 0;

	if (rate == clk->parents[CIF_OUT_SRC_24M]->rate) {
		parent = clk->parents[CIF_OUT_SRC_24M];
	} else {
		parent = clk->parents[CIF_OUT_SRC_DIV];
		ret = parent->ops->set_rate(parent->hw, rate,
				parent->parent->rate);
		if (ret)
			goto out;
		else
			parent->rate = rate;
	}

	if(clk->parent != parent){
		ret = clk_set_parent(clk, parent);
#if 0
		for(i=0; i<clk->num_parents; i++){
			if(parent == clk->parents[i]){
				index = i;
				break;
			}
		}
		ret = clk->ops->set_parent(clk->hw, index);
#endif
		if(ret)
			goto out;
	}
out:
	return ret;
}
const struct clk_ops clkops_rate_cif_out = {
	.recalc_rate	= cif_out_recalc_rate,
	.round_rate	= cif_out_round_rate,
	.set_rate	= cif_out_set_rate,
};

static int clk_i2s_fracdiv_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	u32 numerator, denominator;
	struct clk *clk_parent;
	int i = 10;
	struct clk_divider *div = to_clk_divider(hw);

	clk_parent = hw->clk->parent;
	if(clk_fracdiv_get_config(rate, parent_rate,
				&numerator, &denominator) == 0) {

		clk_parent->ops->set_rate(clk_parent->hw,
				clk_parent->parent->rate,
				clk_parent->parent->rate);
		while (i--) {
			writel((numerator - 1) << 16 | denominator, div->reg);
			mdelay(1);
			writel(numerator << 16 | denominator, div->reg);
			mdelay(1);
		}
		clk_err("%s set rate=%lu,is ok\n", hw->clk->name, rate);

	} else {
		clk_err("%s: can't get rate=%lu,%s\n", __func__, rate, hw->clk->name);
		return -ENOENT;
	}
	return 0;
}

const struct clk_ops clkops_rate_i2s_frac = {
	.recalc_rate	= clk_fracdiv_recalc,
	.round_rate	= clk_fracdiv_round_rate,
	.set_rate	= clk_i2s_fracdiv_set_rate,
};
static unsigned long clk_i2s_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return hw->clk->parent->rate;
}

static long clk_i2s_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return rate;
}

#define I2S_SRC_DIV	(0x0)
#define I2S_SRC_FRAC	(0x1)
#define I2S_SRC_12M	(0x2)
static int clk_i2s_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	int ret = -EINVAL;
	u8 p_index = 0;
	struct clk *parent_tmp, *parent;
	struct clk *clk = hw->clk;


	if (rate == clk->parents[I2S_SRC_12M]->rate) {
		parent = clk->parents[I2S_SRC_12M];
		p_index = I2S_SRC_12M;
		goto set_parent;
	}

	parent_tmp = clk->parents[I2S_SRC_DIV];

	if(parent_tmp->ops->round_rate(parent_tmp->hw, rate,
				&parent_tmp->parent->rate) == rate) {
		parent = clk->parents[I2S_SRC_DIV];
		p_index = I2S_SRC_DIV;
		goto set;
	}

	parent = clk->parents[I2S_SRC_FRAC];
	p_index = I2S_SRC_FRAC;
	//ret = clk_set_rate(parent_tmp, parent_tmp->parent->rate);
	ret = parent_tmp->ops->set_rate(parent_tmp->hw,
			parent_tmp->parent->rate,
			parent_tmp->parent->rate);
	parent_tmp->rate = parent_tmp->ops->recalc_rate(parent_tmp->hw,
			parent_tmp->parent->rate);
	//ret = parent->ops->set_rate(parent->hw, rate, parent->parent->rate);
	if (ret) {
		clk_debug("%s set rate%lu err\n", clk->name, rate);
		return ret;
	}

set:
	clk_debug(" %s set rate=%lu parent %s(old %s)\n",
			clk->name, rate, parent->name, clk->parent->name);

	ret = clk_set_rate(parent, rate);
	//ret = parent->ops->set_rate(parent->hw, rate, parent->parent->rate);
	if (ret) {
		clk_debug("%s set rate%lu err\n", clk->name, rate);
		return ret;
	}

set_parent:
	clk_debug("%s: set parent\n", __func__);
	if (clk->parent != parent) {
		ret = clk_set_parent(clk, parent);
		/*
		 * clk->ops->set_parent(hw, p_index);
		 */
		if (ret) {
			clk_debug("%s can't get rate%lu,reparent err\n",
					clk->name, rate);
			return ret;
		}
	}

	return ret;
}

const struct clk_ops clkops_rate_i2s = {
	.recalc_rate	= clk_i2s_recalc_rate,
	.round_rate	= clk_i2s_round_rate,
	.set_rate	= clk_i2s_set_rate,
};
const struct clk_ops clkops_rate_hsadc_frac = {
	.recalc_rate	= clk_fracdiv_recalc,
	.round_rate	= clk_fracdiv_round_rate,
	.set_rate	= clk_fracdiv_set_rate,
};


const struct clk_ops clkops_rate_uart_frac = {
	.recalc_rate	= clk_fracdiv_recalc,
	.round_rate	= clk_fracdiv_round_rate,
	.set_rate	= clk_fracdiv_set_rate,
};

static unsigned long clk_uart_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return hw->clk->parent->rate;

}
static long clk_uart_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return rate;
}

#define UART_SRC_DIV	(0x0)
#define UART_SRC_FRAC	(0x1)
#define UART_SRC_24M	(0x2)
static int clk_uart_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	int ret = -EINVAL;
	u8 p_index = 0;
	struct clk *parent_tmp, *parent;
	struct clk *clk = hw->clk;


	if (rate == clk->parents[UART_SRC_24M]->rate) {
		parent = clk->parents[UART_SRC_24M];
		p_index = UART_SRC_24M;
		goto set_parent;
	}

	parent_tmp = clk->parents[UART_SRC_DIV];

	if(parent_tmp->ops->round_rate(parent_tmp->hw, rate,
				&parent_tmp->parent->rate) == rate) {
		parent = clk->parents[UART_SRC_DIV];
		p_index = UART_SRC_DIV;
		goto set;
	}

	parent = clk->parents[UART_SRC_FRAC];
	p_index = UART_SRC_FRAC;
	/*
	 * ret = clk_set_rate(parent_tmp, parent_tmp->parent->rate);
	 */
	ret = parent_tmp->ops->set_rate(parent_tmp->hw,
			parent_tmp->parent->rate,
			parent_tmp->parent->rate);
	parent_tmp->rate = parent_tmp->ops->recalc_rate(parent_tmp->hw,
			parent_tmp->parent->rate);
	//ret = parent->ops->set_rate(parent->hw, rate, parent->parent->rate);
	if (ret) {
		clk_debug("%s set rate%lu err\n", clk->name, rate);
		return ret;
	}

set:
	clk_debug(" %s set rate=%lu parent %s(old %s)\n",
			clk->name, rate, parent->name, clk->parent->name);

	ret = clk_set_rate(parent, rate);
	//ret = parent->ops->set_rate(parent->hw, rate, parent->parent->rate);
	if (ret) {
		clk_debug("%s set rate%lu err\n", clk->name, rate);
		return ret;
	}

set_parent:
	clk_debug("%s: set parent\n", __func__);
	if (clk->parent != parent) {
		ret = clk_set_parent(clk, parent);
		/*
		 * clk->ops->set_parent(hw, p_index);
		 */
		if (ret) {
			clk_debug("%s can't get rate%lu,reparent err\n",
					clk->name, rate);
			return ret;
		}
	}

	return ret;
}


const struct clk_ops clkops_rate_uart = {
	.recalc_rate	= clk_uart_recalc_rate,
	.round_rate	= clk_uart_round_rate,
	.set_rate	= clk_uart_set_rate,
};


static unsigned long clk_hsadc_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return hw->clk->parent->rate;

}
static long clk_hsadc_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return rate;
}

#define HSADC_SRC_DIV	(0x0)
#define HSADC_SRC_FRAC	(0x1)
#define HSADC_SRC_EXT	(0x2)
static int clk_hsadc_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	int ret = -EINVAL;
	u8 p_index = 0;
	struct clk *parent_tmp, *parent;
	struct clk *clk = hw->clk;


	if (rate == clk->parents[HSADC_SRC_EXT]->rate) {
		parent = clk->parents[HSADC_SRC_EXT];
		p_index = HSADC_SRC_EXT;
		goto set_parent;
	}

	parent_tmp = clk->parents[HSADC_SRC_DIV];

	if(parent_tmp->ops->round_rate(parent_tmp->hw, rate,
				&parent_tmp->parent->rate) == rate) {
		parent = clk->parents[HSADC_SRC_DIV];
		p_index = HSADC_SRC_DIV;
		goto set;
	}

	parent = clk->parents[HSADC_SRC_FRAC];
	p_index = HSADC_SRC_FRAC;
	/*
	 * ret = clk_set_rate(parent_tmp, parent_tmp->parent->rate);
	 */
	ret = parent_tmp->ops->set_rate(parent_tmp->hw,
			parent_tmp->parent->rate,
			parent_tmp->parent->rate);
	parent_tmp->rate = parent_tmp->ops->recalc_rate(parent_tmp->hw,
			parent_tmp->parent->rate);
	//ret = parent->ops->set_rate(parent->hw, rate, parent->parent->rate);
	if (ret) {
		clk_debug("%s set rate%lu err\n", clk->name, rate);
		return ret;
	}



set:
	clk_debug(" %s set rate=%lu parent %s(old %s)\n",
			clk->name, rate, parent->name, clk->parent->name);

	ret = clk_set_rate(parent, rate);
	//ret = parent->ops->set_rate(parent->hw, rate, parent->parent->rate);
	if (ret) {
		clk_debug("%s set rate%lu err\n", clk->name, rate);
		return ret;
	}

set_parent:
	clk_debug("%s: set parent\n", __func__);
	if (clk->parent != parent) {
		ret = clk_set_parent(clk, parent);
		/*
		 * clk->ops->set_parent(hw, p_index);
		 */
		if (ret) {
			clk_debug("%s can't get rate%lu,reparent err\n",
					clk->name, rate);
			return ret;
		}
	}

	return ret;
}


const struct clk_ops clkops_rate_hsadc = {
	.recalc_rate	= clk_hsadc_recalc_rate,
	.round_rate	= clk_hsadc_round_rate,
	.set_rate	= clk_hsadc_set_rate,
};

static unsigned long clk_mac_ref_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return hw->clk->parent->rate;

}
static long clk_mac_ref_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return rate;
}

#define MAC_SRC_DIV	(0x0)
#define RMII_CLKIN	(0x1)
static int clk_mac_ref_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	int ret = -EINVAL;
	u8 p_index = 0;
	struct clk *parent;
	struct clk *clk = hw->clk;

	clk_debug("%s: rate %lu\n", __func__, rate);

	if (rate == clk->parents[RMII_CLKIN]->rate) {
		parent = clk->parents[RMII_CLKIN];
		p_index = RMII_CLKIN;
		goto set_parent;
	}

	parent = clk->parents[MAC_SRC_DIV];
	p_index = MAC_SRC_DIV;

	clk_debug(" %s set rate=%lu parent %s(old %s)\n",
			clk->name, rate, parent->name, clk->parent->name);

	/*
	 * ret = clk_set_rate(parent, rate);
	 */
	ret = parent->ops->set_rate(parent->hw,
			rate,
			parent->parent->rate);
	parent->rate = parent->ops->recalc_rate(parent->hw,
			parent->parent->rate);
	//ret = parent->ops->set_rate(parent->hw, rate, parent->parent->rate);
	if (ret) {
		clk_debug("%s set rate%lu err\n", clk->name, rate);
		return ret;
	}

set_parent:
	clk_debug("%s: set parent\n", __func__);
	if (clk->parent != parent) {
		ret = clk_set_parent(clk, parent);
		/*
		 * clk->ops->set_parent(hw, p_index);
		 */
		if (ret) {
			clk_debug("%s can't get rate%lu,reparent err\n",
					clk->name, rate);
			return ret;
		}
	}

	return ret;
}


const struct clk_ops clkops_rate_mac_ref = {
	.recalc_rate	= clk_mac_ref_recalc_rate,
	.round_rate	= clk_mac_ref_round_rate,
	.set_rate	= clk_mac_ref_set_rate,
};


struct clk_ops_table rk_clkops_rate_table[] = {
	{.index = CLKOPS_RATE_MUX_DIV,		.clk_ops = &clkops_rate_auto_parent},
	{.index = CLKOPS_RATE_EVENDIV,		.clk_ops = &clkops_rate_evendiv},
	{.index = CLKOPS_RATE_DCLK_LCDC,	.clk_ops = &clkops_rate_dclk_lcdc},
	{.index = CLKOPS_RATE_CIFOUT,		.clk_ops = &clkops_rate_cif_out},
	{.index = CLKOPS_RATE_I2S_FRAC,		.clk_ops = &clkops_rate_i2s_frac},
	{.index = CLKOPS_RATE_I2S,		.clk_ops = &clkops_rate_i2s},
	{.index = CLKOPS_RATE_HSADC_FRAC,	.clk_ops = &clkops_rate_hsadc_frac},
	{.index = CLKOPS_RATE_UART_FRAC,	.clk_ops = &clkops_rate_uart_frac},
	{.index = CLKOPS_RATE_UART,		.clk_ops = &clkops_rate_uart},
	{.index = CLKOPS_RATE_HSADC,		.clk_ops = &clkops_rate_hsadc},
	{.index = CLKOPS_RATE_MAC_REF,		.clk_ops = &clkops_rate_mac_ref},


	{.index = CLKOPS_TABLE_END},
};
const struct clk_ops *rk_get_clkops(u32 idx)
{
	return rk_clkops_rate_table[idx].clk_ops;
}
EXPORT_SYMBOL_GPL(rk_get_clkops);
