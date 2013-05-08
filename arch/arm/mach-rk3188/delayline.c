/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

//#define DEBUG 1
#define pr_fmt(fmt) "delayline: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <mach/cpu.h>

#define RK30_DELAYLINE_BASE		(RK30_TIMER0_BASE + 0x1000)
#define delayline_readl(offset)		readl_relaxed(RK30_DELAYLINE_BASE + offset)
#define delayline_writel(val, offset)	writel_relaxed(val, RK30_DELAYLINE_BASE + offset)

#define DELAYLINE_CON0			(0x0000)
#define DELAYLINE_STOP			(0 << 5)
#define DELAYLINE_START			(1 << 5)
#define DELAYLINE_CLK_SEL_24M		(0 << 4)
#define DELAYLINE_CLK_SEL_ACLK_CPU	(1 << 4)
#define DELAYLINE_DIV_MAX		(0xf)

#define DELAYLINE_CON1			(0x0004)
#define DELAYLINE_INCREMENT(n)		((n & 0xff) << 8)
#define DELAYLINE_START_POINT(n)	(n & 0xff)

#define DELAYLINE_STATUS		(0x0008)
#define DELAYLINE_LOCKED		(1 << 8)
#define DELAYLINE_VALUE_MASK		(0xff)

static u32 delayline_div;
static u32 start_point;

int rk3188_get_delayline_value(void)
{
	u32 status;
	u32 loop = 1000;

	if (!start_point)
		return 0;

	delayline_writel(DELAYLINE_INCREMENT(4) | start_point, DELAYLINE_CON1);
	delayline_writel(DELAYLINE_START | DELAYLINE_CLK_SEL_ACLK_CPU | delayline_div, DELAYLINE_CON0);
	dsb();
	delayline_writel(DELAYLINE_STOP | DELAYLINE_CLK_SEL_ACLK_CPU | delayline_div, DELAYLINE_CON0);
	dsb();

	do {
		status = delayline_readl(DELAYLINE_STATUS);
		if (status & DELAYLINE_LOCKED)
			break;
		udelay(1);
		loop--;
	} while (loop);

	if (!loop) {
		start_point >>= 1;
		return 0;
	}

	return status & DELAYLINE_VALUE_MASK;
}

static int __init delayline_set_rate(unsigned long parent_rate, unsigned long rate)
{
	u32 div;
	for (div = 0; div <= DELAYLINE_DIV_MAX; div++) {
		u32 new_rate = parent_rate / (div + 1);
		if (new_rate <= rate) {
			delayline_div = div;
			return 0;
		}
	}
	return -ENOENT;
}

static int __init rk3188_delayline_init(void)
{
	struct clk *clk_aclk_cpu;
	unsigned long aclk_rate;

	if (!soc_is_rk3188plus())
		return 0;

	clk_aclk_cpu = clk_get(NULL, "aclk_cpu");
	if (IS_ERR_OR_NULL(clk_aclk_cpu)) {
		pr_err("can not get parent clock 'aclk_cpu'\n");
		return 0;
	}

	aclk_rate = clk_get_rate(clk_aclk_cpu);
	clk_put(clk_aclk_cpu);
	delayline_set_rate(aclk_rate, 100 * 1000 * 1000);
	start_point = 0x10;
	start_point = rk3188_get_delayline_value() >> 1;
	printk(KERN_DEBUG "delayline: aclk_cpu %lu div %u clk %lu start point %u\n", aclk_rate, delayline_div, aclk_rate / (delayline_div + 1), start_point);

	return 0;
}

pure_initcall(rk3188_delayline_init);
