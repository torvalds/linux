/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/opp.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/devfreq.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/pm_qos.h>
#include <linux/reboot.h>
#include <linux/kobject.h>

#include <plat/pll.h>
#include <mach/regs-clock.h>
#include <mach/devfreq.h>
#include <mach/asv-exynos.h>
#include <mach/tmu.h>

#include "noc_probe.h"
#include "exynos5410_volt_ctrl.h"

static bool en_profile = false;
static struct device *int_dev;

static struct pm_qos_request exynos5_int_qos;
cputime64_t int_pre_time;

static LIST_HEAD(int_dvfs_list);

/* NoC list for INT block */
static LIST_HEAD(int_noc_list);

struct busfreq_data_int {
	struct device *dev;
	struct devfreq *devfreq;
	struct opp *curr_opp;

	struct mutex lock;
};

DEFINE_SPINLOCK(int_div_lock);

enum int_bus_idx {
	LV_0,
	LV_1,
	LV_1_1,
	LV_1_2,
	LV_1_3,
	LV_2,
	LV_3,
	LV_4,
	LV_5,
	LV_6,
	LV_7,
	LV_END,
};

struct int_bus_opp_table {
	unsigned int idx;
	unsigned long clk;
	unsigned long volt;
	cputime64_t time_in_state;
};

struct int_bus_opp_table int_bus_opp_list[] = {
	{LV_0, 800000, 1137500, 0},	/* ISP Special Level */
	{LV_1, 700000, 1137500, 0},	/* ISP Special Level */
	{LV_1_1, 650000, 1137500, 0},	/* ISP Special Level */
	{LV_1_2, 600000, 1137500, 0},	/* ISP Special Level */
	{LV_1_3, 550000, 1137500, 0},	/* ISP Special Level */
	{LV_2, 400000, 1137500, 0},
	{LV_3, 267000, 1137500, 0},
	{LV_4, 200000, 1137500, 0},
	{LV_5, 160000, 1137500, 0},
	{LV_6, 100000, 1137500, 0},
	{LV_7,  50000, 1137500, 0},
};

struct int_regs_value {
	unsigned int reg_value;
	unsigned int reg_mask;
	unsigned int wait_mask;
};

struct int_pll_pms {
	unsigned int p;
	unsigned int m;
	unsigned int s;
};

struct int_clkdiv_info {
	struct list_head list;
	unsigned int lv_idx;
	struct int_regs_value top0;
	struct int_regs_value top1;
	struct int_regs_value top2;
	struct int_pll_pms mpll_pms;
	struct int_pll_pms dpll_pms;
	struct int_pll_pms ipll_pms;
};

/*
 * Setting value for INT DFS(Dynamic Frequence scaling)
 */
static unsigned int exynos5410_clkdiv_top0[][6] = {
/* ACLK_400, ACLK_333, ACLK_266, ACLK_200, ACLK_166, ACLK_66 */
	{2, 1, 1, 2, 3, 3},	/* L0(For ISP) */
	{2, 1, 3, 2, 3, 3},	/* L1(For ISP) */
	{2, 1, 2, 2, 3, 3},	/* L1-1(For ISP) */
	{2, 1, 3, 2, 3, 3},	/* L1-2(For ISP) */
	{2, 1, 6, 2, 3, 3},	/* L1-3(For ISP) */
	{2, 1, 1, 2, 3, 3},	/* L2 */
	{2, 2, 2, 2, 4, 3},	/* L3 */
	{2, 3, 3, 3, 5, 3},	/* L4 */
	{2, 5, 3, 3, 7, 3},	/* L5 */
	{2, 7, 5, 7, 7, 3},	/* L6 */
	{3, 7, 7, 7, 7, 3},	/* L7 */
};

static unsigned int exynos5410_clkdiv_top1[][2] = {
/* ACLK_MIPI_HIS_TXBASE, ACLK66_PRE, ACLK_400_ISP */
	{7, 1},		/* L0(For ISP) */
	{7, 1},		/* L1(For ISP) */
	{7, 1},		/* L1-1(For ISP) */
	{7, 1},		/* L1-2(For ISP) */
	{7, 1},		/* L1-3(For ISP) */
	{7, 1},		/* L2 */
	{7, 1},		/* L3 */
	{7, 1},		/* L4 */
	{7, 1},		/* L5 */
	{7, 1},		/* L6 */
	{7, 1},		/* L7 */
};

#ifdef CONFIG_S5P_DP
static unsigned int exynos5410_clkdiv_top2[][7] = {
/*
 * ACLK_300_JPEG, ACLK_333_432_GSCL, ACLK_333_432_ISP, ACLK_300_DISP1,
 * ACLK_300_DISP0, ACLK_300_GSCL, ACLK_266_GSCL
 */
	{1, 0, 0, 1, 2, 1, 7},	/* L0(For ISP) */
	{1, 0, 2, 1, 2, 1, 7},	/* L1(For ISP) */
	{1, 7, 7, 1, 2, 1, 7},	/* L2 */
	{1, 7, 7, 1, 2, 1, 7},	/* L3 */
	{4, 7, 7, 1, 2, 1, 7},	/* L4 */
	{5, 7, 7, 1, 2, 1, 7},	/* L5 */
	{7, 7, 7, 1, 2, 7, 7},	/* L6 */
	{7, 7, 7, 1, 3, 7, 7},	/* L7 */
};
#else
static unsigned int exynos5410_clkdiv_top2[][7] = {
/*
 * ACLK_300_JPEG, ACLK_333_432_GSCL, ACLK_333_432_ISP, ACLK_300_DISP1,
 * ACLK_300_DISP0, ACLK_300_GSCL, ACLK_266_GSCL
 */
	{1, 0, 0, 2, 2, 1, 7},	/* L0(For ISP) */
	{1, 0, 2, 2, 2, 1, 7},	/* L1(For ISP) */
	{1, 4, 1, 2, 2, 1, 7},	/* L1-1(For ISP) */
	{1, 0, 2, 2, 2, 1, 7},	/* L1-2(For ISP) */
	{1, 4, 4, 2, 2, 1, 7},	/* L1-3(For ISP) */
	{1, 7, 7, 2, 2, 1, 7},	/* L2 */
	{1, 7, 7, 2, 2, 1, 7},	/* L3 */
	{4, 7, 7, 2, 2, 1, 7},	/* L4 */
	{5, 7, 7, 2, 2, 1, 7},	/* L5 */
	{7, 7, 7, 2, 2, 7, 7},	/* L6 */
	{7, 7, 7, 3, 3, 7, 7},	/* L7 */
};
#endif

static unsigned int exynos5410_mpll_pms_value[][3] = {
	{3, 266, 2},	/* ISP LEVEL */
	{3, 266, 2},	/* ISP LEVEL */
	{3, 266, 2},	/* ISP LEVEL */
	{3, 266, 2},	/* ISP LEVEL */
	{3, 266, 2},	/* ISP LEVEL */
	{3, 266, 2},	/* 400Mhz */
	{3, 266, 2},	/* 267Mhz */
	{3, 266, 2},	/* 200Mhz */
	{3, 266, 2},	/* 160Mhz */
	{3, 266, 2},	/* 100Mhz */
	{3, 266, 2},	/*  50Mhz */
};

static unsigned int exynos5410_dpll_pms_value[][3] = {
	{4, 200, 1},	/* ISP LEVEL */
	{4, 200, 1},	/* ISP LEVEL */
	{4, 200, 1},	/* ISP LEVEL */
	{4, 200, 1},	/* ISP LEVEL */
	{4, 200, 1},	/* ISP LEVEL */
	{4, 200, 1},	/* 400Mhz */
	{4, 200, 1},	/* 267Mhz */
	{4, 200, 1},	/* 200Mhz */
	{4, 200, 1},	/* 160Mhz */
	{4, 200, 1},	/* 100Mhz */
	{4, 200, 1},	/*  50Mhz */
};

static unsigned int exynos5410_ipll_pms_value[][3] = {
	{4, 288, 2},	/* ISP LEVEL */
	{4, 288, 2},	/* ISP LEVEL */
	{4, 288, 2},	/* ISP LEVEL */
	{4, 288, 2},	/* ISP LEVEL */
	{4, 288, 2},	/* ISP LEVEL */
	{4, 288, 2},	/* 400Mhz */
	{4, 288, 2},	/* 267Mhz */
	{4, 288, 2},	/* 200Mhz */
	{4, 288, 2},	/* 160Mhz */
	{4, 288, 2},	/* 100Mhz */
	{4, 288, 2},	/*  50Mhz */
};

static void exynos5_int_set_div(struct int_clkdiv_info *target_int_clkdiv)
{
	unsigned int tmp;

	spin_lock(&int_div_lock);

	/*
	 * Setting for TOP_0
	 */
	tmp = __raw_readl(EXYNOS5_CLKDIV_TOP0);
	tmp &= ~target_int_clkdiv->top0.reg_mask;
	tmp |= target_int_clkdiv->top0.reg_value;
	__raw_writel(tmp, EXYNOS5_CLKDIV_TOP0);

	wait_clkdiv_stable_time(EXYNOS5_CLKDIV_STAT_TOP0,
			target_int_clkdiv->top0.wait_mask, 0);

	/*
	 * Setting for TOP_1
	 */
	tmp = __raw_readl(EXYNOS5_CLKDIV_TOP1);
	tmp &= ~target_int_clkdiv->top1.reg_mask;
	tmp |= target_int_clkdiv->top1.reg_value;
	__raw_writel(tmp, EXYNOS5_CLKDIV_TOP1);

	wait_clkdiv_stable_time(EXYNOS5_CLKDIV_STAT_TOP1,
			target_int_clkdiv->top1.wait_mask, 0);

	/*
	 * Setting for TOP_2
	 */
	tmp = __raw_readl(EXYNOS5_CLKDIV_TOP2);
	tmp &= ~target_int_clkdiv->top2.reg_mask;
	tmp |= target_int_clkdiv->top2.reg_value;
	__raw_writel(tmp, EXYNOS5_CLKDIV_TOP2);

	wait_clkdiv_stable_time(EXYNOS5_CLKDIV_STAT_TOP2,
			target_int_clkdiv->top2.wait_mask, 0);

	spin_unlock(&int_div_lock);
}

static bool int_is_need_pms_change(unsigned int old_value, unsigned int new_value)
{
	return (old_value == new_value) ? false : true;
}

static void exynos5_int_set_pms(struct int_clkdiv_info *target_int_clkdiv)
{
	unsigned int tmp;
	unsigned int old_pms, new_pms;

	/* Set MPLL PMS change */
	old_pms = __raw_readl(EXYNOS5_MPLL_CON0);
	old_pms &= ((PLL2550_MDIV_MASK << PLL2550_MDIV_SHIFT) |\
		(PLL2550_PDIV_MASK << PLL2550_PDIV_SHIFT) |\
		(PLL2550_SDIV_MASK << PLL2550_SDIV_SHIFT));

	new_pms = ((target_int_clkdiv->mpll_pms.p << PLL2550_PDIV_SHIFT) |
		   (target_int_clkdiv->mpll_pms.m << PLL2550_MDIV_SHIFT) |
		   (target_int_clkdiv->mpll_pms.s << PLL2550_SDIV_SHIFT));

	if (int_is_need_pms_change(old_pms, new_pms)) {
		/* Change PMS with target value */
		tmp = __raw_readl(EXYNOS5_MPLL_CON0);

		tmp &= ~((PLL2550_MDIV_MASK << PLL2550_MDIV_SHIFT) |
			 (PLL2550_PDIV_MASK << PLL2550_PDIV_SHIFT) |
			 (PLL2550_SDIV_MASK << PLL2550_SDIV_SHIFT));

		tmp |= target_int_clkdiv->mpll_pms.p << PLL2550_PDIV_SHIFT;
		tmp |= target_int_clkdiv->mpll_pms.m << PLL2550_MDIV_SHIFT;
		tmp |= target_int_clkdiv->mpll_pms.s << PLL2550_SDIV_SHIFT;

		__raw_writel(tmp, EXYNOS5_MPLL_CON0);
	}

	/* Set DPLL PMS change */
	old_pms = __raw_readl(EXYNOS5_DPLL_CON0);
	old_pms &= ((PLL2550_MDIV_MASK << PLL2550_MDIV_SHIFT) |\
		(PLL2550_PDIV_MASK << PLL2550_PDIV_SHIFT) |\
		(PLL2550_SDIV_MASK << PLL2550_SDIV_SHIFT));

	new_pms = ((target_int_clkdiv->dpll_pms.p << PLL2550_PDIV_SHIFT) |
		   (target_int_clkdiv->dpll_pms.m << PLL2550_MDIV_SHIFT) |
		   (target_int_clkdiv->dpll_pms.s << PLL2550_SDIV_SHIFT));

	if (int_is_need_pms_change(old_pms, new_pms)) {
		tmp = __raw_readl(EXYNOS5_DPLL_CON0);

		tmp &= ~((PLL2550_MDIV_MASK << PLL2550_MDIV_SHIFT) |
			 (PLL2550_PDIV_MASK << PLL2550_PDIV_SHIFT) |
			 (PLL2550_SDIV_MASK << PLL2550_SDIV_SHIFT));

		tmp |= target_int_clkdiv->dpll_pms.p << PLL2550_PDIV_SHIFT;
		tmp |= target_int_clkdiv->dpll_pms.m << PLL2550_MDIV_SHIFT;
		tmp |= target_int_clkdiv->dpll_pms.s << PLL2550_SDIV_SHIFT;

		__raw_writel(tmp, EXYNOS5_DPLL_CON0);
	}

	/* Set IPLL PMS change */
	old_pms = __raw_readl(EXYNOS5_IPLL_CON0);
	old_pms &= ((PLL2550_MDIV_MASK << PLL2550_MDIV_SHIFT) |\
		(PLL2550_PDIV_MASK << PLL2550_PDIV_SHIFT) |\
		(PLL2550_SDIV_MASK << PLL2550_SDIV_SHIFT));

	new_pms = ((target_int_clkdiv->ipll_pms.p << PLL2550_PDIV_SHIFT) |
		   (target_int_clkdiv->ipll_pms.m << PLL2550_MDIV_SHIFT) |
		   (target_int_clkdiv->ipll_pms.s << PLL2550_SDIV_SHIFT));

	if (int_is_need_pms_change(old_pms, new_pms)) {
		tmp = __raw_readl(EXYNOS5_IPLL_CON0);

		tmp &= ~((PLL2550_MDIV_MASK << PLL2550_MDIV_SHIFT) |
			 (PLL2550_PDIV_MASK << PLL2550_PDIV_SHIFT) |
			 (PLL2550_SDIV_MASK << PLL2550_SDIV_SHIFT));

		tmp |= target_int_clkdiv->ipll_pms.p << PLL2550_PDIV_SHIFT;
		tmp |= target_int_clkdiv->ipll_pms.m << PLL2550_MDIV_SHIFT;
		tmp |= target_int_clkdiv->ipll_pms.s << PLL2550_SDIV_SHIFT;

		__raw_writel(tmp, EXYNOS5_IPLL_CON0);
	}
}

static void exynos5_int_set_freq(unsigned long target_freq, unsigned long old_freq)
{
	unsigned int i;
	unsigned int target_idx = LV_0;
	struct int_clkdiv_info *target_int_clkdiv;

	/*
	 * Find setting value with target frequency
	 */
	for (i = LV_0; i < LV_END; i++) {
		if (int_bus_opp_list[i].clk == target_freq)
			target_idx = int_bus_opp_list[i].idx;
	}

	list_for_each_entry(target_int_clkdiv, &int_dvfs_list, list) {
		if (target_int_clkdiv->lv_idx == target_idx)
			break;
	}

	if (target_freq > old_freq) {
		exynos5_int_set_div(target_int_clkdiv);

		exynos5_int_set_pms(target_int_clkdiv);
	} else {
		exynos5_int_set_pms(target_int_clkdiv);

		exynos5_int_set_div(target_int_clkdiv);
	}
}

static bool exynos5_int_valid_freq(unsigned long old_freq)
{
	unsigned int i;
	unsigned int old_idx = LV_0;
	unsigned int tmp;
	struct int_clkdiv_info *target_int_clkdiv;

	/*
	 * Find setting value with target frequency
	 */
	for (i = LV_0; i < LV_END; i++) {
		if (int_bus_opp_list[i].clk == old_freq)
			old_idx = int_bus_opp_list[i].idx;
	}

	list_for_each_entry(target_int_clkdiv, &int_dvfs_list, list) {
		if (target_int_clkdiv->lv_idx == old_idx)
			break;
	}

	/* Check TOP0 is valid */
	tmp = __raw_readl(EXYNOS5_CLKDIV_TOP0);
	tmp &= target_int_clkdiv->top0.reg_mask;

	if (target_int_clkdiv->top0.reg_value != tmp)
		return true;

	/* Check TOP1 is valid */
	tmp = __raw_readl(EXYNOS5_CLKDIV_TOP1);
	tmp &= target_int_clkdiv->top1.reg_mask;

	if (target_int_clkdiv->top1.reg_value != tmp)
		return true;

	/* Check TOP2 is valid */
	tmp = __raw_readl(EXYNOS5_CLKDIV_TOP2);
	tmp &= target_int_clkdiv->top2.reg_mask;

	if (target_int_clkdiv->top2.reg_value != tmp)
		return true;

	return false;
}

static void exynos5_int_update_state(unsigned int target_freq)
{
	cputime64_t cur_time = get_jiffies_64();
	cputime64_t tmp_cputime;
	unsigned int target_idx = LV_0;
	unsigned int i;

	/*
	 * Find setting value with target frequency
	 */
	for (i = LV_0; i < LV_END; i++) {
		if (int_bus_opp_list[i].clk == target_freq)
			target_idx = int_bus_opp_list[i].idx;
	}

	tmp_cputime = cur_time - int_pre_time;

	int_bus_opp_list[target_idx].time_in_state =
		int_bus_opp_list[target_idx].time_in_state + tmp_cputime;

	int_pre_time = cur_time;
}

static int exynos5_int_busfreq_target(struct device *dev,
				      unsigned long *_freq, u32 flags)
{
	int err = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct busfreq_data_int *data = platform_get_drvdata(pdev);
	struct opp *opp;
	unsigned long freq;
	unsigned long old_freq;
	unsigned long target_volt;

	mutex_lock(&data->lock);

	/* get available opp information */
	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, _freq, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "%s: Invalid OPP.\n", __func__);
		mutex_unlock(&data->lock);
		return PTR_ERR(opp);
	}

	freq = opp_get_freq(opp);
	target_volt = opp_get_voltage(opp);
	rcu_read_unlock();

	/* get olg opp information */
	rcu_read_lock();
	old_freq = opp_get_freq(data->curr_opp);
	rcu_read_unlock();

	exynos5_int_update_state(old_freq);

	if ((old_freq == freq) && (!exynos5_int_valid_freq(old_freq)))
		goto out;

	/*
	 * If target freq is higher than old freq
	 * after change voltage, setting freq ratio
	 */
	if (old_freq < freq) {
		err = exynos5_volt_ctrl(VDD_INT, target_volt, freq);
		if (err)
			goto out;

		exynos5_int_set_freq(freq, old_freq);
	} else {
		exynos5_int_set_freq(freq, old_freq);

		err = exynos5_volt_ctrl(VDD_INT, target_volt, freq);
		if (err)
			goto out;
	}

	data->curr_opp = opp;
out:
	mutex_unlock(&data->lock);

	return err;
}

static int exynos5_int_bus_get_dev_status(struct device *dev,
				      struct devfreq_dev_status *stat)
{
	struct nocp_cnt tmp_nocp_cnt;
	struct busfreq_data_int *data = dev_get_drvdata(dev);

	nocp_get_aver_cnt(&int_noc_list, &tmp_nocp_cnt);

	rcu_read_lock();
	stat->current_frequency = opp_get_freq(data->curr_opp);
	rcu_read_unlock();

	/*
	 * Bandwidth of memory interface is 128bits
	 * So bus can transfer 16bytes per cycle
	 */
	tmp_nocp_cnt.total_byte_cnt >>= 4;

	/* Value fixed on Kbytes */
	stat->total_time = (tmp_nocp_cnt.cycle_cnt >> 6);
	stat->busy_time = (tmp_nocp_cnt.total_byte_cnt >> 6);

	if (en_profile)
		pr_info("%lu, %lu\n", tmp_nocp_cnt.total_byte_cnt, tmp_nocp_cnt.cycle_cnt);

	return 0;
}

static struct devfreq_dev_profile exynos5_int_devfreq_profile = {
	.initial_freq	= 400000,
	.polling_ms	= 100,
	.target		= exynos5_int_busfreq_target,
	.get_dev_status	= exynos5_int_bus_get_dev_status,
};

static struct devfreq_simple_usage_data exynos5_int_governor_data = {
	.upthreshold		= 80,
	.target_percentage	= 70,
	.proportional		= 100,
	.cal_qos_max		= 400000,
	.pm_qos_class		= PM_QOS_DEVICE_THROUGHPUT,
	.en_monitoring		= true,
};

static int exynos5410_init_int_table(struct busfreq_data_int *data)
{
	unsigned int i;
	unsigned int ret;
	unsigned int asv_volt;
	struct int_clkdiv_info *tmp_int_table;

	/* make list for setting value for int DVS */
	for (i = LV_0; i < LV_END; i++) {
		tmp_int_table = kzalloc(sizeof(struct int_clkdiv_info), GFP_KERNEL);

		tmp_int_table->lv_idx = i;

		tmp_int_table->top0.reg_value = ((exynos5410_clkdiv_top0[i][0] << EXYNOS5_CLKDIV_ACLK_400_SHIFT) |\
					       (exynos5410_clkdiv_top0[i][1] << EXYNOS5_CLKDIV_ACLK_333_SHIFT) |\
					       (exynos5410_clkdiv_top0[i][2] << EXYNOS5_CLKDIV_ACLK_266_SHIFT) |\
					       (exynos5410_clkdiv_top0[i][3] << EXYNOS5_CLKDIV_ACLK_200_SHIFT) |\
					       (exynos5410_clkdiv_top0[i][4] << EXYNOS5_CLKDIV_ACLK_166_SHIFT) |\
					       (exynos5410_clkdiv_top0[i][5] << EXYNOS5_CLKDIV_ACLK_66_SHIFT));
		tmp_int_table->top0.reg_mask = ((EXYNOS5_CLKDIV_ACLK_400_MASK | EXYNOS5_CLKDIV_ACLK_333_MASK |\
					       EXYNOS5_CLKDIV_ACLK_266_MASK | EXYNOS5_CLKDIV_ACLK_200_MASK |\
					       EXYNOS5_CLKDIV_ACLK_166_MASK | EXYNOS5_CLKDIV_ACLK_66_MASK));
		tmp_int_table->top0.wait_mask = 0x1111101;

		tmp_int_table->top1.reg_value = ((exynos5410_clkdiv_top1[i][0] << EXYNOS5_CLKDIV_ACLK_MIPI_TXBASE_SHIFT) |\
					       (exynos5410_clkdiv_top1[i][1] << EXYNOS5_CLKDIV_ACLK_66_PRE_SHIFT));
		tmp_int_table->top1.reg_mask = ((EXYNOS5_CLKDIV_ACLK_MIPI_TXBASE_MASK |\
					       EXYNOS5_CLKDIV_ACLK_66_PRE_MASK));
		tmp_int_table->top1.wait_mask = 0x11000000;

		tmp_int_table->top2.reg_value = ((exynos5410_clkdiv_top2[i][0] << EXYNOS5_CLKDIV_ACLK_300_JPEG_SHIFT) |\
						(exynos5410_clkdiv_top2[i][1] << EXYNOS5_CLKDIV_ACLK_333_432_GSCL_SHIFT) |\
						(exynos5410_clkdiv_top2[i][2] << EXYNOS5_CLKDIV_ACLK_333_432_ISP_SHIFT) |\
						(exynos5410_clkdiv_top2[i][3] << EXYNOS5_CLKDIV_ACLK_300_DISP1_SHIFT) |\
						(exynos5410_clkdiv_top2[i][4] << EXYNOS5_CLKDIV_ACLK_300_DISP0_SHIFT) |\
						(exynos5410_clkdiv_top2[i][5] << EXYNOS5_CLKDIV_ACLK_300_GSCL_SHIFT) |\
						(exynos5410_clkdiv_top2[i][6] << EXYNOS5_CLKDIV_ACLK_266_GSCL_SHIFT));
		tmp_int_table->top2.reg_mask = (EXYNOS5_CLKDIV_ACLK_300_JPEG_MASK | EXYNOS5_CLKDIV_ACLK_333_432_GSCL_MASK |\
					       EXYNOS5_CLKDIV_ACLK_333_432_ISP_MASK | EXYNOS5_CLKDIV_ACLK_300_DISP1_MASK |\
					       EXYNOS5_CLKDIV_ACLK_300_DISP0_MASK | EXYNOS5_CLKDIV_ACLK_300_GSCL_MASK |\
					       EXYNOS5_CLKDIV_ACLK_266_GSCL_MASK);
		tmp_int_table->top2.wait_mask = 0x11111110;

		tmp_int_table->mpll_pms.p = exynos5410_mpll_pms_value[i][0];
		tmp_int_table->mpll_pms.m = exynos5410_mpll_pms_value[i][1];
		tmp_int_table->mpll_pms.s = exynos5410_mpll_pms_value[i][2];

		tmp_int_table->dpll_pms.p = exynos5410_dpll_pms_value[i][0];
		tmp_int_table->dpll_pms.m = exynos5410_dpll_pms_value[i][1];
		tmp_int_table->dpll_pms.s = exynos5410_dpll_pms_value[i][2];

		tmp_int_table->ipll_pms.p = exynos5410_ipll_pms_value[i][0];
		tmp_int_table->ipll_pms.m = exynos5410_ipll_pms_value[i][1];
		tmp_int_table->ipll_pms.s = exynos5410_ipll_pms_value[i][2];

		list_add(&tmp_int_table->list, &int_dvfs_list);
	}

	/* will add code for ASV information setting function in here */

	for (i = 0; i < ARRAY_SIZE(int_bus_opp_list); i++) {
		asv_volt = get_match_volt(ID_INT_MIF_L0, int_bus_opp_list[i].clk);

		if (!asv_volt)
			asv_volt = int_bus_opp_list[i].volt;

		pr_info("INT %luKhz ASV is %duV\n", int_bus_opp_list[i].clk, asv_volt);

		ret = opp_add(data->dev, int_bus_opp_list[i].clk,
				get_match_volt(ID_INT_MIF_L0, int_bus_opp_list[i].clk));

		if (ret) {
			dev_err(data->dev, "Fail to add opp entries.\n");
			return ret;
		}
	}

	return 0;
}

struct nocp_info nocp_mfc0 = {
	.name		= "mfc0",
	.id		= MFC0,
	.pa_base	= NOCP_BASE(MFC0),
};

struct nocp_info nocp_mfc1 = {
	.name		= "mfc1",
	.id		= MFC1,
	.pa_base	= NOCP_BASE(MFC1),
};

struct nocp_info nocp_gsc23 = {
	.name		= "gsc23",
	.id		= GSC23,
	.pa_base	= NOCP_BASE(GSC23),
};

struct nocp_info nocp_isp0 = {
	.name		= "isp0",
	.id		= ISP0,
	.pa_base	= NOCP_BASE(ISP0),
};

struct nocp_info nocp_isp1 = {
	.name		= "isp1",
	.id		= ISP1,
	.pa_base	= NOCP_BASE(ISP1),
};

struct nocp_info nocp_gen = {
	.name		= "gen",
	.id		= GEN,
	.pa_base	= NOCP_BASE(GEN),
};

struct nocp_info nocp_gsc0 = {
	.name		= "gsc0",
	.id		= GSC0,
	.pa_base	= NOCP_BASE(GSC0),
};

struct nocp_info nocp_gsc1 = {
	.name		= "gsc1",
	.id		= GSC1,
	.pa_base	= NOCP_BASE(GSC1),
};

struct nocp_info nocp_disp0 = {
	.name		= "disp0",
	.id		= DISP0,
	.pa_base	= NOCP_BASE(DISP0),
};

struct nocp_info nocp_disp1 = {
	.name		= "disp1",
	.id		= DISP1,
	.pa_base	= NOCP_BASE(DISP1),
};

struct nocp_info nocp_fsys = {
	.name		= "fsys",
	.id		= FSYS,
	.pa_base	= NOCP_BASE(FSYS),
};

struct nocp_info *exynos5_int_nocp_list[] = {
	&nocp_mfc0,
	&nocp_mfc1,
	&nocp_gsc23,
	&nocp_isp0,
	&nocp_isp1,
	&nocp_gen,
	&nocp_gsc0,
	&nocp_gsc1,
	&nocp_disp0,
	&nocp_disp1,
	&nocp_fsys,
};

static ssize_t int_show_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned int i;
	ssize_t len = 0;
	ssize_t write_cnt = (ssize_t)((PAGE_SIZE / LV_END) - 2);

	for (i = LV_0; i < LV_END; i++)
		len += snprintf(buf + len, write_cnt, "%ld %llu\n", int_bus_opp_list[i].clk,
				(unsigned long long)int_bus_opp_list[i].time_in_state);

	return len;
}

static DEVICE_ATTR(int_time_in_state, S_IRUSR | S_IRGRP, int_show_state, NULL);

static ssize_t show_upthreshold(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", exynos5_int_governor_data.upthreshold);
}

static ssize_t store_upthreshold(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned int value;
	int ret;

	ret = sscanf(buf, "%u", &value);
	if (ret != 1)
		goto out;

	exynos5_int_governor_data.upthreshold = value;
out:
	return count;
}

static DEVICE_ATTR(upthreshold, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
		show_upthreshold, store_upthreshold);

static ssize_t show_target_percentage(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", exynos5_int_governor_data.target_percentage);
}

static ssize_t store_target_percentage(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned int value;
	int ret;

	ret = sscanf(buf, "%u", &value);
	if (ret != 1)
		goto out;

	exynos5_int_governor_data.target_percentage = value;
out:
	return count;
}

static DEVICE_ATTR(target_percentage, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
		show_target_percentage, store_target_percentage);

static ssize_t show_proportional(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", exynos5_int_governor_data.proportional);
}

static ssize_t store_proportional(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned int value;
	int ret;

	ret = sscanf(buf, "%u", &value);
	if (ret != 1)
		goto out;

	exynos5_int_governor_data.proportional = value;
out:
	return count;
}

static DEVICE_ATTR(proportional, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
		show_proportional, store_proportional);

static ssize_t show_en_profile(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", en_profile ? "true" : "false");
}

static ssize_t store_en_profile(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned int value;
	int ret;

	ret = sscanf(buf, "%u", &value);
	if (ret != 1)
		goto out;

	if (value)
		en_profile = true;
	else
		en_profile = false;

out:
	return count;
}

static DEVICE_ATTR(en_profile, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
		show_en_profile, store_en_profile);

static struct attribute *busfreq_int_entries[] = {
	&dev_attr_int_time_in_state.attr,
	&dev_attr_upthreshold.attr,
	&dev_attr_target_percentage.attr,
	&dev_attr_proportional.attr,
	&dev_attr_en_profile.attr,
	NULL,
};
static struct attribute_group busfreq_int_attr_group = {
	.name	= "time_in_state",
	.attrs	= busfreq_int_entries,
};

static ssize_t show_freq_table(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i, count = 0;
	struct opp *opp;
	ssize_t write_cnt = (ssize_t)((PAGE_SIZE / ARRAY_SIZE(int_bus_opp_list)) - 2);

	if (!unlikely(int_dev)) {
		pr_err("%s: device is not probed\n", __func__);
		return -ENODEV;
	}

	rcu_read_lock();
	for (i = 0; i < ARRAY_SIZE(int_bus_opp_list); i++) {
		opp = opp_find_freq_exact(int_dev, int_bus_opp_list[i].clk, true);
		if (!IS_ERR_OR_NULL(opp))
			count += snprintf(&buf[count], write_cnt, "%lu ", opp_get_freq(opp));
	}
	rcu_read_unlock();

	count += snprintf(&buf[count], 2, "\n");
	return count;
}

static DEVICE_ATTR(freq_table, S_IRUSR | S_IRGRP, show_freq_table, NULL);

static ssize_t show_en_monitoring(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
			exynos5_int_governor_data.en_monitoring ? "true" : "false");
}

static ssize_t store_en_monitoring(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned int input;

	if (!sscanf(buf, "%u", &input))
		return -EINVAL;

	if (input)
		exynos5_int_governor_data.en_monitoring = true;
	else
		exynos5_int_governor_data.en_monitoring = false;

	return count;
}

static DEVICE_ATTR(en_monitoring, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
			show_en_monitoring, store_en_monitoring);

static struct exynos_devfreq_platdata default_qos_int_pd = {
	.default_qos = 100000,
};

static int exynos5_int_reboot_notifier_call(struct notifier_block *this,
				   unsigned long code, void *_cmd)
{
	pm_qos_update_request(&exynos5_int_qos, 400000);

	return NOTIFY_DONE;
}

static struct notifier_block exynos5_int_reboot_notifier = {
	.notifier_call = exynos5_int_reboot_notifier_call,
};

#ifdef CONFIG_EXYNOS_THERMAL
static int exynos5_int_devfreq_tmu_notifier(struct notifier_block *notifier,
						unsigned long event, void *v)
{
	unsigned int *on = v;

	if (event != TMU_COLD)
		return NOTIFY_OK;

	if (*on) {
		if (pm_qos_request_active(&exynos5_int_qos))
			pm_qos_update_request(&exynos5_int_qos, 400000);
	} else {
		if (pm_qos_request_active(&exynos5_int_qos))
			pm_qos_update_request(&exynos5_int_qos, 50000);
	}

	return NOTIFY_OK;
}

static struct notifier_block exynos5_int_devfreq_tmu_nb = {
	.notifier_call = exynos5_int_devfreq_tmu_notifier,
};
#endif

static __devinit int exynos5_busfreq_int_probe(struct platform_device *pdev)
{
	struct busfreq_data_int *data;
	struct opp *opp;
	struct device *dev = &pdev->dev;
	struct exynos_devfreq_platdata *pdata;
	int err = 0;

	data = kzalloc(sizeof(struct busfreq_data_int), GFP_KERNEL);

	if (data == NULL) {
		dev_err(dev, "Cannot allocate memory for INT.\n");
		return -ENOMEM;
	}

	data->dev = dev;
	mutex_init(&data->lock);

	/* Setting table for int */
	exynos5410_init_int_table(data);

	/* Initialization NoC for MIF block */
	regist_nocp(&int_noc_list, exynos5_int_nocp_list,
			ARRAY_SIZE(exynos5_int_nocp_list), NOCP_USAGE_INT);

	rcu_read_lock();
	opp = opp_find_freq_floor(dev, &exynos5_int_devfreq_profile.initial_freq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "Invalid initial frequency %lu kHz.\n",
			       exynos5_int_devfreq_profile.initial_freq);
		err = PTR_ERR(opp);
		goto err_opp_add;
	}
	rcu_read_unlock();

	int_pre_time = get_jiffies_64();

	data->curr_opp = opp;

	platform_set_drvdata(pdev, data);

#if defined(CONFIG_MACH_ODROIDXU)
	data->devfreq = devfreq_add_device(dev, &exynos5_int_devfreq_profile,
					   &devfreq_performance, NULL);
#else
	data->devfreq = devfreq_add_device(dev, &exynos5_int_devfreq_profile,
					   &devfreq_simple_usage, &exynos5_int_governor_data);
#endif
	if (IS_ERR(data->devfreq)) {
		err = PTR_ERR(data->devfreq);
		goto err_opp_add;
	}

	devfreq_register_opp_notifier(dev, data->devfreq);
	int_dev = data->dev;

	/* Create file for time_in_state */
	err = sysfs_create_group(&data->devfreq->dev.kobj, &busfreq_int_attr_group);

	/* Add sysfs for freq_table */
	err = device_create_file(&data->devfreq->dev, &dev_attr_freq_table);
	if (err)
		pr_err("%s: Fail to create sysfs file\n", __func__);

	/* Add sysfs for en_monitoring */
	err = device_create_file(&data->devfreq->dev, &dev_attr_en_monitoring);
	if (err)
		pr_err("%s: Fail to create sysfs file\n", __func__);

	bw_monitor_create_sysfs(&data->devfreq->dev.kobj);

	pdata = pdev->dev.platform_data;
	if (!pdata)
		pdata = &default_qos_int_pd;

	pm_qos_add_request(&exynos5_int_qos, PM_QOS_DEVICE_THROUGHPUT, pdata->default_qos);
#ifdef CONFIG_ARM_EXYNOS5410_CPUFREQ
	pm_qos_add_request(&exynos5_cpu_int_qos, PM_QOS_DEVICE_THROUGHPUT, 400000);
#endif

	register_reboot_notifier(&exynos5_int_reboot_notifier);

#ifdef CONFIG_EXYNOS_THERMAL
	exynos_tmu_add_notifier(&exynos5_int_devfreq_tmu_nb);
#endif
	return 0;

err_opp_add:
	kfree(data);

	return err;
}

static __devexit int exynos5_busfreq_int_remove(struct platform_device *pdev)
{
	struct busfreq_data_int *data = platform_get_drvdata(pdev);

	devfreq_remove_device(data->devfreq);
	kfree(data);

	pm_qos_remove_request(&exynos5_int_qos);

	return 0;
}

static int exynos5_busfreq_int_suspend(struct device *dev)
{
	if (pm_qos_request_active(&exynos5_int_qos))
		pm_qos_update_request(&exynos5_int_qos, 400000);

	return 0;
}

static int exynos5_busfreq_int_resume(struct device *dev)
{
	struct exynos_devfreq_platdata *pdata = dev->platform_data;

	resume_nocp(&int_noc_list);

	if (pm_qos_request_active(&exynos5_int_qos))
		pm_qos_update_request(&exynos5_int_qos, pdata->default_qos);

	return 0;
}

static const struct dev_pm_ops exynos5_busfreq_int_pm = {
	.suspend	= exynos5_busfreq_int_suspend,
	.resume		= exynos5_busfreq_int_resume,
};

static struct platform_driver exynos5_busfreq_int_driver = {
	.probe	= exynos5_busfreq_int_probe,
	.remove	= __devexit_p(exynos5_busfreq_int_remove),
	.driver = {
		.name	= "exynos5-busfreq-int",
		.owner	= THIS_MODULE,
		.pm	= &exynos5_busfreq_int_pm,
	},
};

static int __init exynos5_busfreq_int_init(void)
{
	return platform_driver_register(&exynos5_busfreq_int_driver);
}
late_initcall(exynos5_busfreq_int_init);

static void __exit exynos5_busfreq_int_exit(void)
{
	platform_driver_unregister(&exynos5_busfreq_int_driver);
}
module_exit(exynos5_busfreq_int_exit);
