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
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

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

#define RK3368_PVTM_CON0 (0x800)
#define RK3368_PVTM_CON1 (0x804)
#define RK3368_PVTM_CON2 (0x808)
#define RK3368_PVTM_STATUS0 (0x80c)
#define RK3368_PVTM_STATUS1 (0x810)
#define RK3368_PVTM_STATUS2 (0x814)

#define RK3368_PMUPVTM_CON0 (0x180)
#define RK3368_PMUPVTM_CON1 (0x184)
#define RK3368_PMUPVTM_STATUS0 (0x190)
#define RK3368_PMUPVTM_STATUS1 (0x194)

#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT+offset)
#define grf_writel(val, offset)	writel_relaxed(val, RK_GRF_VIRT+offset)

#define wr_msk_bit(v, off, msk)  ((v)<<(off)|(msk<<(16+(off))))

static struct clk *ch_clk[3];

struct rockchip_pvtm {
	u32 (*get_value)(u32 ch , u32 time_us);
};
static struct rockchip_pvtm pvtm;

static struct regmap *grf_regmap;
static struct regmap *pmugrf_regmap;

static DEFINE_MUTEX(pvtm_mutex);

/* 0 core, 1 gpu, 2 pmu */
static u32 rk3368_pvtm_get_value(u32 ch , u32 time_us)
{
	u32 val = 0, clk_cnt, check_cnt, pvtm_done_bit;
	u32 sta = 0;

	if (ch > 2)
		return 0;
	/* 24m clk ,24cnt=1us */
	clk_cnt = time_us*24;

	if (ch == 2) {
		regmap_write(pmugrf_regmap, RK3368_PMUPVTM_CON1, clk_cnt);
		regmap_write(pmugrf_regmap, RK3368_PMUPVTM_CON0,
			     wr_msk_bit(3, 0, 0x3));
	} else {
		regmap_write(grf_regmap, RK3368_PVTM_CON0+(ch+1)*4, clk_cnt);
		regmap_write(grf_regmap, RK3368_PVTM_CON0,
			     wr_msk_bit(3, ch*8, 0x3));
	}

	if (time_us >= 1000)
		mdelay(time_us / 1000);
	udelay(time_us % 1000);

	if (ch == 0)
		pvtm_done_bit = 0;
	else if (ch == 1)
		pvtm_done_bit = 1;

	check_cnt = 100;
	if (ch == 2) {
		while (check_cnt--) {
			regmap_read(pmugrf_regmap, RK3368_PMUPVTM_STATUS0,
				    &sta);
			if (sta & 0x1)
				break;
			udelay(4);
		}

	} else {
		while (check_cnt--) {
			regmap_read(grf_regmap, RK3368_PVTM_STATUS0, &sta);
			if (sta & (1 << pvtm_done_bit))
				break;
			udelay(4);
		}
	}

	if (check_cnt)
		if (ch == 2)
			regmap_read(pmugrf_regmap, RK3368_PMUPVTM_STATUS1,
				    &val);
		else
			regmap_read(grf_regmap, RK3368_PVTM_STATUS0+(ch+1)*4,
				    &val);
	else {
		pr_err("%s: wait pvtm_done timeout!\n", __func__);
		val = 0;
	}

	if (ch == 2)
		regmap_write(pmugrf_regmap, RK3368_PMUPVTM_CON0,
			     wr_msk_bit(0, 0, 0x3));
	else
		regmap_write(grf_regmap, RK3368_PVTM_CON0,
			     wr_msk_bit(0, ch*8, 0x3));

	return val;
}

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
	val = pvtm.get_value(ch, time_us);
	mutex_unlock(&pvtm_mutex);
	clk_disable_unprepare(ch_clk[ch]);

	return val;
}

static int __init rk3368_pmupvtm_clk_init(void)
{
	u32 pvtm_cnt = 0, div, val, time_us;
	unsigned long rate = 32;/* KHZ */
	int ret = 0;

	pr_info("%s\n", __func__);

	time_us = 1000;
	pvtm_cnt = pvtm_get_value(2, time_us);
	pr_debug("get pvtm_cnt = %d\n", pvtm_cnt);

	/* set pvtm_div to get rate */
	div = DIV_ROUND_UP(1000*pvtm_cnt, time_us*rate);
	val = DIV_ROUND_UP(div-1, 4);

	if (val > 0x3f) {
		pr_err("need pvtm_div out of bounary! set max instead\n");
		val = 0x3f;
	}

	pr_debug("will set div %d, val %d, rate %luKHZ\n", div, val, rate);
	ret = regmap_write(pmugrf_regmap, RK3368_PMUPVTM_CON0,
			   wr_msk_bit(val, 2, 0x3f));
	if (ret != 0)
		goto out;

	/* pvtm oscilator enable */
	ret = regmap_write(pmugrf_regmap, RK3368_PMUPVTM_CON0,
			   wr_msk_bit(1, 1, 0x1));
out:
	if (ret != 0)
		pr_err("%s: fail to write register\n", __func__);

	return ret;
}

static int __init pvtm_init(void)
{
	struct device_node *np;
	int ret;
	u32 clk_out;

	np = of_find_node_by_name(NULL, "pvtm");
	if (!IS_ERR_OR_NULL(np)) {
		grf_regmap = syscon_regmap_lookup_by_phandle(np,
							     "rockchip,grf");
		if (IS_ERR(grf_regmap)) {
			pr_err("pvtm: dts couldn't find grf regmap\n");
			return PTR_ERR(grf_regmap);
		}
		pmugrf_regmap = syscon_regmap_lookup_by_phandle(np,
								"rockchip,pmugrf");
		if (IS_ERR(pmugrf_regmap)) {
			pr_err("pvtm: dts couldn't find pmugrf regmap\n");
			return PTR_ERR(pmugrf_regmap);
		}

		if (of_device_is_compatible(np, "rockchip,rk3368-pvtm")) {
			ch_clk[0] = clk_get(NULL, "clk_pvtm_core");
			ch_clk[1] = clk_get(NULL, "clk_pvtm_gpu");
			ch_clk[2] = clk_get(NULL, "clk_pvtm_pmu");
			pvtm.get_value = rk3368_pvtm_get_value;
			ret = of_property_read_u32(np, "rockchip,pvtm-clk-out",
						   &clk_out);
			if (!ret && clk_out) {
				ret = rk3368_pmupvtm_clk_init();
				if (ret != 0) {
					pr_err("rk3368_pmupvtm_clk_init failed\n");
					return ret;
				}
			}
		}

		return 0;
	}

	if (cpu_is_rk3288() || cpu_is_rk312x()) {
		ch_clk[0] = clk_get(NULL, "g_clk_pvtm_core");
		ch_clk[1] = clk_get(NULL, "g_clk_pvtm_gpu");
		ch_clk[2] = clk_get(NULL, "g_clk_pvtm_func");
		if (cpu_is_rk3288())
			pvtm.get_value = rk3288_pvtm_get_value;
		else if (cpu_is_rk312x())
			pvtm.get_value = rk312x_pvtm_get_value;
	}
	return 0;
}

#ifdef CONFIG_ARM64
arch_initcall_sync(pvtm_init);
#else
core_initcall(pvtm_init);
#endif

