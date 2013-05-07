/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <mach/clock.h>

#define KHZ				(1000UL)
#define MHZ				(1000UL * 1000UL)
#define RK30_DELAYLINE_BASE		(RK30_TIMER0_BASE + 0x1000)
#define delayline_readl(offset)		readl_relaxed(RK30_DELAYLINE_BASE + offset)
#define delayline_writel(val, offset)	writel_relaxed(val, RK30_DELAYLINE_BASE + offset)

#define get_delayline_bits(con, mask, shift)\
	((delayline_readl((con)) >> (shift)) & (mask))
#define set_delayline_bits(val, mask, shift, con)\
	delayline_writel(((delayline_readl(con) & (~(mask << shift))) | (val << shift)), con)
#define set_delayline_bits_w_msk(val, mask, shift, con)\
	delayline_writel(((mask) << (shift + 16)) | ((val) << (shift)), (con))

#define DELAYLINE_CON0			(0x0000)
#define DELAYLINE_CON1			(0x0004)
#define DELAYLINE_STATUS		(0x0008)

#if 1
#define DELAYLINE_DBG(fmt, args...) printk(KERN_DEBUG "DELAYLINE_DBG:\t"fmt, ##args)
#else
#define DELAYLINE_DBG(fmt, args...) do {} while(0)
#endif

#define DELAYLINE_ERR(fmt, args...) printk("DELAYLINE_ERR:\t"fmt, ##args)

static struct clk *clk_delayline = NULL, *clk_xin24m = NULL, *clk_aclk_cpu = NULL;
static struct clk *clk_delayline_parents[2];

static unsigned long clk_delayline_recalc_freediv(struct clk *clk)
{
	u32 div = get_delayline_bits(clk->clksel_con, clk->div_mask, clk->div_shift) + 1;
	unsigned long rate = clk->parent->rate / div;

	DELAYLINE_DBG("%s new clock rate is %lu (div %u)\n", clk->name, rate, div);
	return rate;
}

static int clk_delayline_set_rate_freediv(struct clk *clk, unsigned long rate)
{
	u32 div;
	for (div = 0; div < clk->div_max; div++) {
		u32 new_rate = clk->parent->rate / (div + 1);
		if (new_rate <= rate) {
			set_delayline_bits(div, clk->div_mask, clk->div_shift, clk->clksel_con);
			DELAYLINE_DBG("clksel_set_rate_freediv for clock %s to rate %ld (div %d)\n", 
					clk->name, rate, div + 1);
			return 0;
		}
	}
	return -ENOENT;
}

static int clk_delayline_set_parent(struct clk *clk, struct clk *parent)
{
	u32 i;
	if (unlikely(!clk->parents))
		return -EINVAL;
	for (i = 0; (i < clk->parents_num); i++) {
		if (clk->parents[i] != parent)
			continue;
		set_delayline_bits(i, clk->src_mask, clk->src_shift, clk->clksel_con);
		return 0;
	}
	return -EINVAL;
}

static struct clk *clk_delayline_get_parent(struct clk *clk) {
	return clk->parents[(delayline_readl(clk->clksel_con) >> clk->src_shift) & clk->src_mask];
}

int rk3188_get_delayline_value(void)
{
	struct clk *clk = clk_delayline;
	clk->set_parent(clk, clk_aclk_cpu);
	clk->set_rate(clk, 75 * MHZ);

	set_delayline_bits(0x4, 0xff, 8, DELAYLINE_CON1);
	set_delayline_bits(0xff, 0xff, 0, DELAYLINE_CON1);
	set_delayline_bits(0x1, 0x1, 5, clk->clksel_con);
	mdelay(1);
	set_delayline_bits(0x0, 0x1, 5, clk->clksel_con);
	mdelay(1);
	
	while(get_delayline_bits(DELAYLINE_STATUS, 0x1, 8) == 0);

	return delayline_readl(DELAYLINE_STATUS) & 0xff;
}
EXPORT_SYMBOL(rk3188_get_delayline_value);

static int __init rk3188_delayline_init(void)
{
	DELAYLINE_DBG("%s......\n", __func__);

	clk_delayline = clk_get(NULL, "delayline");
	if (IS_ERR_OR_NULL(clk_delayline)) {
		DELAYLINE_ERR("%s: can not get clock 'delay_line'\n"
				"\tplease check 'mach-rk3188/clock_data.c' had 'clk_delayline' or not\n", __func__);
		return -EINVAL;
	}

	clk_xin24m = clk_get(NULL, "xin24m");
	if (IS_ERR_OR_NULL(clk_xin24m)) {
		DELAYLINE_ERR("%s: can not get parent clock 'xin24m'\n", __func__);
		return -EINVAL;
	}
	clk_delayline_parents[0] = clk_xin24m;

	clk_aclk_cpu = clk_get(NULL, "aclk_cpu");
	if (IS_ERR_OR_NULL(clk_aclk_cpu)) {
		DELAYLINE_ERR("%s: can not get parent clock 'aclk_cpu'\n", __func__);
		return -EINVAL;
	}
	clk_delayline_parents[1] = clk_aclk_cpu;

	clk_delayline->clksel_con	= DELAYLINE_CON0;
	clk_delayline->recalc		= clk_delayline_recalc_freediv;
	clk_delayline->set_rate		= clk_delayline_set_rate_freediv;
	clk_delayline->set_parent	= clk_delayline_set_parent;
	clk_delayline->get_parent	= clk_delayline_get_parent;
	clk_delayline->parents		= clk_delayline_parents;
	clk_delayline->parents_num	= ARRAY_SIZE(clk_delayline_parents);

	clk_delayline->parent = clk_delayline->get_parent(clk_delayline);
	clk_delayline->rate = clk_delayline->recalc(clk_delayline);
#if 1
	DELAYLINE_DBG("clksel_con=%08x\n", clk_delayline->clksel_con);
	DELAYLINE_DBG("current parent=%s\n", clk_delayline->parent->name);
	DELAYLINE_DBG("parents[0]=%s\n", clk_delayline->parents[0]->name);
	DELAYLINE_DBG("parents[1]=%s\n", clk_delayline->parents[1]->name);
	DELAYLINE_DBG("parents_num=%d\n", clk_delayline->parents_num);
#endif
	return 0;
}

late_initcall(rk3188_delayline_init);

MODULE_DESCRIPTION("Driver for delayline");
MODULE_AUTHOR("chenxing, chenxing@rock-chips.com");
MODULE_LICENSE("GPL");
