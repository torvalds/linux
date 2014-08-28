/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/cpu.h>

#define RK3288_PVTM_CON0 (0x368)
#define RK3288_PVTM_CON1 (0x36c)
#define RK3288_PVTM_CON2 (0x370)
#define RK3288_PVTM_STATUS0 (0x374)
#define RK3288_PVTM_STATUS1 (0x378)
#define RK3288_PVTM_STATUS2 (0x37c)

#define RK312X_PVTM_CON0 (0x200)
#define RK312X_PVTM_CON1 (0x204)
#define RK312X_PVTM_CON2 (0x208)
#define RK312X_PVTM_CON3 (0x20c)
#define RK312X_PVTM_STATUS0 (0x210)
#define RK312X_PVTM_STATUS1 (0x214)
#define RK312X_PVTM_STATUS2 (0x218)
#define RK312X_PVTM_STATUS3 (0x21c)

#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT+offset)
#define grf_writel(val, offset)	writel_relaxed(val, RK_GRF_VIRT+offset)

#define wr_msk_bit(v, off, msk)  ((v)<<(off)|(msk<<(16+(off))))

static struct clk *ch_clk[3];

static DEFINE_MUTEX(pvtm_mutex);

/* 0 core, 1 gpu*/
static u32 rk3288_pvtm_get_value(u32 ch , u32 time_us)
{
	u32 val = 0, clk_cnt, check_cnt, pvtm_done_bit;

	if (ch > 1)
		return 0;

	/*24m clk ,24cnt=1us*/
	clk_cnt = time_us*24;

	grf_writel(clk_cnt, RK3288_PVTM_CON0+(ch+1)*4);
	grf_writel(wr_msk_bit(3, ch*8, 0x3), RK3288_PVTM_CON0);

	if (time_us >= 1000)
		mdelay(time_us / 1000);
	udelay(time_us % 1000);

	if (ch == 0)
		pvtm_done_bit = 1;
	else if (ch == 1)
		pvtm_done_bit = 0;

	check_cnt = 100;
	while (!(grf_readl(RK3288_PVTM_STATUS0) & (1 << pvtm_done_bit))) {
		udelay(4);
		check_cnt--;
		if (!check_cnt)
			break;
	}

	if (check_cnt)
		val = grf_readl(RK3288_PVTM_STATUS0+(ch+1)*4);

	grf_writel(wr_msk_bit(0, ch*8, 0x3), RK3288_PVTM_CON0);

	return val;
}

/*0 core, 1 gpu, 2 func*/
static u32 rk312x_pvtm_get_value(u32 ch , u32 time_us)
{
	u32 val = 0, clk_cnt, check_cnt, pvtm_done_bit;

	if (ch > 2)
		return 0;

	/*24m clk ,24cnt=1us*/
	clk_cnt = time_us*24;

	grf_writel(clk_cnt, RK312X_PVTM_CON0+(ch+1)*4);
	if ((ch == 0) || (ch == 1))
		grf_writel(wr_msk_bit(3, ch*8, 0x3), RK312X_PVTM_CON0);
	else if (ch == 2)
		grf_writel(wr_msk_bit(3, 12, 0x3), RK312X_PVTM_CON0);

	if (time_us >= 1000)
		mdelay(time_us / 1000);
	udelay(time_us % 1000);

	if (ch == 0)
		pvtm_done_bit = 1;
	else if (ch == 1)
		pvtm_done_bit = 0;
	else if (ch == 2)
		pvtm_done_bit = 2;

	check_cnt = 100;
	while (!(grf_readl(RK312X_PVTM_STATUS0) & (1 << pvtm_done_bit))) {
		udelay(4);
		check_cnt--;
		if (!check_cnt)
			break;
	}

	if (check_cnt)
		val = grf_readl(RK312X_PVTM_STATUS0+(ch+1)*4);
	if ((ch == 0) || (ch == 1))
		grf_writel(wr_msk_bit(0, ch*8, 0x3), RK312X_PVTM_CON0);
	else if (ch == 2)
		grf_writel(wr_msk_bit(0, 12, 0x3), RK312X_PVTM_CON0);

	return val;
}

u32 pvtm_get_value(u32 ch, u32 time_us)
{
	u32 val = 0;

	if (IS_ERR_OR_NULL(ch_clk[ch]))
		return 0;

	clk_prepare_enable(ch_clk[ch]);
	mutex_lock(&pvtm_mutex);
	if (cpu_is_rk3288())
		val = rk3288_pvtm_get_value(ch, time_us);
	else if (cpu_is_rk312x())
		val = rk312x_pvtm_get_value(ch, time_us);
	mutex_unlock(&pvtm_mutex);
	clk_disable_unprepare(ch_clk[ch]);

	return val;
}

static int __init pvtm_init(void)
{
	ch_clk[0] = clk_get(NULL, "g_clk_pvtm_core");
	ch_clk[1] = clk_get(NULL, "g_clk_pvtm_gpu");
	ch_clk[2] = clk_get(NULL, "g_clk_pvtm_func");

	return 0;
}

core_initcall(pvtm_init);

