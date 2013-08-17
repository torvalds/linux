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
#include <linux/delay.h>

#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/devfreq.h>
#include <mach/asv-exynos.h>
#include <mach/smc.h>
#include <mach/bts.h>
#include <mach/tmu.h>

#include <plat/pll.h>

#include "noc_probe.h"
#include "exynos5410_volt_ctrl.h"

static bool en_profile = false;
static struct device *mif_dev;

#define BPLL_S_ONLY_CHANGE
#define SET_DREX_TIMING

#define SAFE_MIF_VOLT(x)	(x + 25000)

#ifndef CONFIG_ARM_TRUSTZONE
static void __iomem *exynos5_base_drexI_1;
static void __iomem *exynos5_base_drexI_0;
#endif
static void __iomem *phy0_base;
static void __iomem *phy1_base;

#define AREF_CRITICAL		0x17
#define AREF_HOT		0x2E
#define AREF_NORMAL		0x5D

#define DREX_TIMINGAREF		0x30
#define DREX_TIMINGROW		0x34
#define DREX_TIMINGDATA		0x38
#define DREX_TIMINGPOWER	0x3C

static struct pm_qos_request exynos5_mif_qos;
static struct pm_qos_request boot_mif_qos;
cputime64_t mif_pre_time;

static LIST_HEAD(mif_dvfs_list);

/* NoC list for MIF block */
static LIST_HEAD(mif_noc_list);

struct busfreq_data_mif {
	struct device *dev;
	struct devfreq *devfreq;
	struct opp *curr_opp;

	struct mutex lock;
};

enum mif_bus_idx {
	LV_0 = 0,
	LV_1,
	LV_2,
	LV_3,
	LV_4,
	LV_5,
	LV_6,
	LV_7,
	LV_END,
};

struct mif_bus_opp_table {
	unsigned int idx;
	unsigned long clk;
	unsigned long volt;
	cputime64_t time_in_state;
};

struct mif_bus_opp_table mif_bus_opp_list[] = {
	{LV_0, 800000, 1062500, 0},
	{LV_1, 667000, 1062500, 0},
	{LV_2, 533000, 1062500, 0},
	{LV_3, 400000, 1062500, 0},
	{LV_4, 267000, 1062500, 0},
	{LV_5, 200000, 1062500, 0},
	{LV_6, 160000, 1062500, 0},
	{LV_7, 100000, 1062500, 0},
};

struct mif_regs_value {
	unsigned int reg_value;
	unsigned int reg_mask;
};

struct mif_bpll_pms {
	unsigned int p;
	unsigned int m;
	unsigned int s;
};

struct mif_dram_param {
	unsigned int row;
	unsigned int data;
	unsigned int power;
};

struct mif_clkdiv_info {
	struct list_head list;
	unsigned int lv_idx;
	struct mif_regs_value cdrex0;
	struct mif_regs_value g2d;
	struct mif_bpll_pms target_pms;
	struct mif_dram_param target_dram_param;
};

static unsigned int exynos5410_clkdiv_cdrex0[][4] = {
/* PCLK_CDREX SCLK_CDREX ACLK_CDREX1 CLK2X_PHY0*/
	{3, 0, 1, 0},
	{3, 0, 1, 0},
	{3, 0, 1, 0},
	{3, 0, 1, 0},
	{3, 0, 1, 0},
	{3, 0, 1, 0},
	{3, 0, 1, 0},
	{3, 0, 1, 0},
};

static unsigned int exynos5410_clkdiv_g2d[][2] = {
/* PCLK_ACP ACLK_ACP */
	{1, 1},
	{1, 1},
	{1, 1},
	{1, 2},
	{1, 2},
	{1, 2},
	{1, 2},
	{1, 3},
};

static unsigned int exynos5410_bpll_pms_value[][3] = {
#ifndef BPLL_S_ONLY_CHANGE	/* PMS change method has some problem */
	{3, 200, 1},	/* 800Mhz */
	{3, 167, 1},	/* 667Mhz */
	{3, 266, 2},	/* 533Mhz */
	{3, 200, 2},	/* 400Mhz */
	{3, 266, 3},	/* 267Mhz */
	{3, 200, 3},	/* 200Mhz */
	{3, 160, 3},	/* 160Mhz */
	{3, 200, 4},	/* 100Mhz */
#else				/* S value only change */
	{3, 200, 1},	/* 800Mhz */
	{3, 200, 1},	/* 667Mhz Invalid */
	{3, 200, 1},	/* 533Mhz Invalid */
	{3, 200, 2},	/* 400Mhz */
	{3, 200, 2},	/* 267Mhz Invalid */
	{3, 200, 3},	/* 200Mhz */
	{3, 200, 3},	/* 160Mhz Invalid */
	{3, 200, 4},	/* 100Mhz */
#endif
};

static unsigned int exynos5410_dram_param[][3] = {
	/* timiningRow, timingData, timingPower */
	{0x345A8692, 0x3630560C, 0x50380336},	/* 800Mhz */
	{0x2C48758F, 0x3630560C, 0x442F0336},	/* 667Mhz */
	{0x2C48758F, 0x3630560C, 0x38260224},	/* 533Mhz */
	{0x1A354349, 0x2620560C, 0x281C0223},	/* 400Mhz */
	{0x1A354349, 0x2620560C, 0x1C130223},	/* 267Mhz */
	{0x112321C5, 0x2620560C, 0x140E0223},	/* 200Mhz */
	{0x112321C5, 0x2620560C, 0x100C0223},	/* 160Mhz */
	{0x11222103, 0x2620560C, 0x100C0223},	/* 100Mhz */
};

/*
 * MIF devfreq notifier
 */
static struct srcu_notifier_head exynos5_mif_transition_notifier_list;

static int __init exynos5_mif_transition_notifier_list_init(void)
{
	srcu_init_notifier_head(&exynos5_mif_transition_notifier_list);

	return 0;
}
pure_initcall(exynos5_mif_transition_notifier_list_init);

int exynos5_mif_register_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_register(&exynos5_mif_transition_notifier_list, nb);
}
EXPORT_SYMBOL(exynos5_mif_register_notifier);

int exynos5_mif_unregister_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&exynos5_mif_transition_notifier_list, nb);
}
EXPORT_SYMBOL(exynos5_mif_unregister_notifier);

void exynos5_mif_notify_transition(struct devfreq_info *info, unsigned int state)
{
	BUG_ON(irqs_disabled());

	return srcu_notifier_call_chain(&exynos5_mif_transition_notifier_list, state, info);
}
EXPORT_SYMBOL_GPL(exynos5_mif_notify_transition);

/*
 * MIF devfreq BPLL change notifier
 */
static struct srcu_notifier_head exynos5_mif_bpll_transition_notifier_list;

static int __init exynos5_mif_bpll_transition_notifier_list_init(void)
{
	srcu_init_notifier_head(&exynos5_mif_bpll_transition_notifier_list);

	return 0;
}
pure_initcall(exynos5_mif_bpll_transition_notifier_list_init);

int exynos5_mif_bpll_register_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_register(&exynos5_mif_bpll_transition_notifier_list, nb);
}
EXPORT_SYMBOL(exynos5_mif_bpll_register_notifier);

int exynos5_mif_bpll_unregister_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&exynos5_mif_bpll_transition_notifier_list, nb);
}
EXPORT_SYMBOL(exynos5_mif_bpll_unregister_notifier);

void exynos5_mif_bpll_transition_notify(struct devfreq_info *info, unsigned int state)
{
	BUG_ON(irqs_disabled());

	return srcu_notifier_call_chain(&exynos5_mif_bpll_transition_notifier_list, state, info);
}
EXPORT_SYMBOL_GPL(exynos5_mif_bpll_transition_notify);

static bool mif_is_need_div_change(unsigned int old_cdrex0_value,
				unsigned int old_g2d_value,
				unsigned int new_cdrex0_value,
				unsigned int new_g2d_value)
{
	if ((old_cdrex0_value == new_cdrex0_value) &&
		(old_g2d_value == new_g2d_value))
		return false;
	else
		return true;
}

static bool mif_is_need_pms_change(unsigned int old_value, unsigned int new_value)
{
	return (old_value == new_value) ? false : true;
}

#ifdef SET_DREX_TIMING
static void exynos5_mif_set_drex(unsigned long target_freq)
{
	unsigned int target_idx = LV_0;
	unsigned int i;
	unsigned int utiming_row_cur, utiming_row_cur_target;
	struct mif_clkdiv_info *target_mif_clkdiv;

	/*
	 * Find setting value with target frequency
	 */
	for (i = LV_0; i < LV_END; i++) {
		if (mif_bus_opp_list[i].clk == target_freq)
			target_idx = mif_bus_opp_list[i].idx;
	}

	list_for_each_entry(target_mif_clkdiv, &mif_dvfs_list, list) {
		if (target_mif_clkdiv->lv_idx == target_idx)
			break;
	}
#ifdef CONFIG_ARM_TRUSTZONE
	/* Set DREX PARAMETER_ROW */
	exynos_smc_readsfr(EXYNOS5_PA_DREXI_0 + DREX_TIMINGROW, &utiming_row_cur);
	utiming_row_cur_target = (utiming_row_cur | target_mif_clkdiv->target_dram_param.row);

	/*
	 * Writing (old value | target value) in TIMINGROW register for META Stability
	 */
	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_1 + DREX_TIMINGROW),
			utiming_row_cur_target, 0);
	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_1 + DREX_TIMINGROW),
			target_mif_clkdiv->target_dram_param.row, 0);

	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_0 + DREX_TIMINGROW),
			utiming_row_cur_target, 0);
	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_0 + DREX_TIMINGROW),
			target_mif_clkdiv->target_dram_param.row, 0);

	/* Set DREX PARAMETER_DATA */
	exynos_smc_readsfr(EXYNOS5_PA_DREXI_0 + DREX_TIMINGDATA, &utiming_row_cur);
	utiming_row_cur_target = (utiming_row_cur | target_mif_clkdiv->target_dram_param.data);

	/*
	 * Writing (old value | target value) in TIMINGDATA register for META Stability
	 */
	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_1 + DREX_TIMINGDATA),
			utiming_row_cur_target, 0);
	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_1 + DREX_TIMINGDATA),
			target_mif_clkdiv->target_dram_param.data, 0);

	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_0 + DREX_TIMINGDATA),
			utiming_row_cur_target, 0);
	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_0 + DREX_TIMINGDATA),
			target_mif_clkdiv->target_dram_param.data, 0);

	/* Set DREX PARAMETER_POWER */
	exynos_smc_readsfr(EXYNOS5_PA_DREXI_0 + DREX_TIMINGPOWER, &utiming_row_cur);
	utiming_row_cur_target = (utiming_row_cur | target_mif_clkdiv->target_dram_param.power);

	/*
	 * Writing (old value | target value) in TIMINGPOWER register for META Stability
	 */
	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_1 + DREX_TIMINGPOWER),
			utiming_row_cur_target, 0);
	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_1 + DREX_TIMINGPOWER),
			target_mif_clkdiv->target_dram_param.power, 0);

	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_0 + DREX_TIMINGPOWER),
			utiming_row_cur_target, 0);
	exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_0 + DREX_TIMINGPOWER),
			target_mif_clkdiv->target_dram_param.power, 0);
#else
	/* Set DREX PARAMETER_ROW */
	utiming_row_cur = __raw_readl(exynos5_base_drexI_0 + DREX_TIMINGROW);
	utiming_row_cur_target = (utiming_row_cur | target_mif_clkdiv->target_dram_param.row);

	/*
	 * Writing (old value | target value) in TIMINGROW register for META Stability
	 */
	__raw_writel(utiming_row_cur_target, exynos5_base_drexI_1 + DREX_TIMINGROW);
	__raw_writel(target_mif_clkdiv->target_dram_param.row, exynos5_base_drexI_1 + DREX_TIMINGROW);

	__raw_writel(utiming_row_cur_target, exynos5_base_drexI_0 + DREX_TIMINGROW);
	__raw_writel(target_mif_clkdiv->target_dram_param.row, exynos5_base_drexI_0 + DREX_TIMINGROW);

	/* Set DREX PARAMETER_DATA */
	utiming_row_cur = __raw_readl(exynos5_base_drexI_0 + DREX_TIMINGDATA);
	utiming_row_cur_target = (utiming_row_cur | target_mif_clkdiv->target_dram_param.data);

	/*
	 * Writing (old value | target value) in TIMINGDATA register for META Stability
	 */
	__raw_writel(utiming_row_cur_target, exynos5_base_drexI_1 + DREX_TIMINGDATA);
	__raw_writel(target_mif_clkdiv->target_dram_param.data, exynos5_base_drexI_1 + DREX_TIMINGDATA);

	__raw_writel(utiming_row_cur_target, exynos5_base_drexI_0 + DREX_TIMINGDATA);
	__raw_writel(target_mif_clkdiv->target_dram_param.data, exynos5_base_drexI_0 + DREX_TIMINGDATA);

	/* Set DREX PARAMETER_POWER */
	utiming_row_cur = __raw_readl(exynos5_base_drexI_0 + DREX_TIMINGPOWER);
	utiming_row_cur_target = (utiming_row_cur | target_mif_clkdiv->target_dram_param.power);

	/*
	 * Writing (old value | target value) in TIMINGPOWER register for META Stability
	 */
	__raw_writel(utiming_row_cur_target, exynos5_base_drexI_1 + DREX_TIMINGPOWER);
	__raw_writel(target_mif_clkdiv->target_dram_param.power, exynos5_base_drexI_1 + DREX_TIMINGPOWER);

	__raw_writel(utiming_row_cur_target, exynos5_base_drexI_0 + DREX_TIMINGPOWER);
	__raw_writel(target_mif_clkdiv->target_dram_param.power, exynos5_base_drexI_0 + DREX_TIMINGPOWER);
#endif
}
#else
#define exynos5_mif_set_drex(target_freq) do { } while (0)
#endif
static void exynos5_mif_set_freq(unsigned long target_freq)
{
	unsigned int i;
	unsigned int tmp;
	unsigned int target_idx = LV_0;
	unsigned int old_cdrex0, old_g2d;
	unsigned int old_pms, new_pms;
	struct mif_clkdiv_info *target_mif_clkdiv;

	/*
	 * Find setting value with target frequency
	 */
	for (i = LV_0; i < LV_END; i++) {
		if (mif_bus_opp_list[i].clk == target_freq)
			target_idx = mif_bus_opp_list[i].idx;
	}

	list_for_each_entry(target_mif_clkdiv, &mif_dvfs_list, list) {
		if (target_mif_clkdiv->lv_idx == target_idx)
			break;
	}

	/* If need to change DIV for CDREX0,1, modify CDREX DIV with new value */
	old_cdrex0 = __raw_readl(EXYNOS5_CLKDIV_CDREX) & target_mif_clkdiv->cdrex0.reg_mask;
	old_g2d = __raw_readl(EXYNOS5410_CLKDIV_G2D) & target_mif_clkdiv->g2d.reg_mask;

	if (mif_is_need_div_change(old_cdrex0, old_g2d,
			target_mif_clkdiv->cdrex0.reg_value, target_mif_clkdiv->g2d.reg_value)) {
		/*
		 * Setting for CDREX_0
		 */
		tmp = __raw_readl(EXYNOS5_CLKDIV_CDREX);
		tmp &= ~target_mif_clkdiv->cdrex0.reg_mask;
		tmp |= target_mif_clkdiv->cdrex0.reg_value;
		__raw_writel(tmp, EXYNOS5_CLKDIV_CDREX);

		/*
		 * Setting for G2D
		 */
		tmp = __raw_readl(EXYNOS5410_CLKDIV_G2D);
		tmp &= ~target_mif_clkdiv->g2d.reg_mask;
		tmp |= target_mif_clkdiv->g2d.reg_value;
		__raw_writel(tmp, EXYNOS5410_CLKDIV_G2D);

		/*
		 * Wait for divider change done
		 */
		wait_clkdiv_stable_time(EXYNOS5_CLKDIV_STAT_CDREX, 0x11010014, 0);
		wait_clkdiv_stable_time(EXYNOS5410_DIV_STAT_G2D, 0x11, 0);
	}

	old_pms = __raw_readl(EXYNOS5_BPLL_CON0);
	old_pms &= ((PLL2550_MDIV_MASK << PLL2550_MDIV_SHIFT) |\
		(PLL2550_PDIV_MASK << PLL2550_PDIV_SHIFT) |\
		(PLL2550_SDIV_MASK << PLL2550_SDIV_SHIFT));

	new_pms = ((target_mif_clkdiv->target_pms.p << PLL2550_PDIV_SHIFT) |
		   (target_mif_clkdiv->target_pms.m << PLL2550_MDIV_SHIFT) |
		   (target_mif_clkdiv->target_pms.s << PLL2550_SDIV_SHIFT));

	if (mif_is_need_pms_change(old_pms, new_pms)) {
#ifndef BPLL_S_ONLY_CHANGE
		/* Setup BPLL FOUT with divide 2 for S value */
		tmp = __raw_readl(EXYNOS5_BPLL_CON0);

		tmp &= ~((PLL2550_MDIV_MASK << PLL2550_MDIV_SHIFT) |
			 (PLL2550_PDIV_MASK << PLL2550_PDIV_SHIFT) |
			 (PLL2550_SDIV_MASK << PLL2550_SDIV_SHIFT));

		tmp |= target_mif_clkdiv->target_pms.p << PLL2550_PDIV_SHIFT;
		tmp |= target_mif_clkdiv->target_pms.m << PLL2550_MDIV_SHIFT;
		tmp |= (target_mif_clkdiv->target_pms.s + 1) << PLL2550_SDIV_SHIFT;

		__raw_writel(tmp, EXYNOS5_BPLL_CON0);

		do {
			tmp = __raw_readl(EXYNOS5_BPLL_CON0);
		} while (!(tmp & (0x1 << PLL2550_LOCKED)));
#endif
		/* Setup BPLL FOUT with real divide value */
		tmp = __raw_readl(EXYNOS5_BPLL_CON0);

		tmp &= ~((PLL2550_MDIV_MASK << PLL2550_MDIV_SHIFT) |
			 (PLL2550_PDIV_MASK << PLL2550_PDIV_SHIFT) |
			 (PLL2550_SDIV_MASK << PLL2550_SDIV_SHIFT));

		tmp |= target_mif_clkdiv->target_pms.p << PLL2550_PDIV_SHIFT;
		tmp |= target_mif_clkdiv->target_pms.m << PLL2550_MDIV_SHIFT;
		tmp |= target_mif_clkdiv->target_pms.s << PLL2550_SDIV_SHIFT;

		__raw_writel(tmp, EXYNOS5_BPLL_CON0);
	}
}

static void exynos5_clkm_gate(bool en)
{
	unsigned int tmp;

	/* Disable CLKM_PHY0,1  */
	tmp = __raw_readl(EXYNOS5_CLKGATE_BUS_CDREX);

	if (en)
		tmp |= (EXYNOS5_CLKM_PHY1_ENABLE | EXYNOS5_CLKM_PHY0_ENABLE);
	else
		tmp &= ~(EXYNOS5_CLKM_PHY1_ENABLE | EXYNOS5_CLKM_PHY0_ENABLE);

	__raw_writel(tmp, EXYNOS5_CLKGATE_BUS_CDREX);
}

static void exynos5_mif_update_state(unsigned int target_freq)
{
	cputime64_t cur_time = get_jiffies_64();
	cputime64_t tmp_cputime;
	unsigned int target_idx = LV_0;
	unsigned int i;

	/*
	 * Find setting value with target frequency
	 */
	for (i = LV_0; i < LV_END; i++) {
		if (mif_bus_opp_list[i].clk == target_freq)
			target_idx = mif_bus_opp_list[i].idx;
	}

	tmp_cputime = cur_time - mif_pre_time;

	mif_bus_opp_list[target_idx].time_in_state =
		mif_bus_opp_list[target_idx].time_in_state + tmp_cputime;

	mif_pre_time = cur_time;
}

unsigned long curr_mif_freq;

static int exynos5_mif_busfreq_target(struct device *dev,
				      unsigned long *_freq, u32 flags)
{
	int err = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct busfreq_data_mif *data = platform_get_drvdata(pdev);
	struct opp *opp;
	struct devfreq_info info;
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

	exynos5_mif_update_state(old_freq);

	if (old_freq == freq)
		goto out;

	info.old = old_freq;
	info.new = freq;

	bts_change_bustraffic(&info, MIF_DEVFREQ_PRECHANGE);

	/*
	 * If target freq is higher than old freq
	 * after change voltage, setting freq ratio
	 */
	if (old_freq < freq) {
		err = exynos5_volt_ctrl(VDD_MIF, target_volt, freq);

		if (err)
			goto out;

		exynos5_mif_set_drex(freq);

		exynos5_mif_bpll_transition_notify(&info, MIF_DEVFREQ_PRECHANGE);
		exynos5_mif_set_freq(freq);
		exynos5_mif_bpll_transition_notify(&info, MIF_DEVFREQ_POSTCHANGE);

		if (freq == data->devfreq->max_freq)
			exynos5_clkm_gate(true);
	} else {
		if (old_freq == data->devfreq->max_freq)
			exynos5_clkm_gate(false);

		exynos5_mif_bpll_transition_notify(&info, MIF_DEVFREQ_PRECHANGE);
		exynos5_mif_set_freq(freq);
		exynos5_mif_bpll_transition_notify(&info, MIF_DEVFREQ_POSTCHANGE);

		exynos5_mif_set_drex(freq);

		err = exynos5_volt_ctrl(VDD_MIF, target_volt, freq);

		if (err)
			goto out;
	}

	bts_change_bustraffic(&info, MIF_DEVFREQ_POSTCHANGE);
	
	curr_mif_freq = freq;
	data->curr_opp = opp;
out:
	mutex_unlock(&data->lock);

	return err;
}

static int exynos5_mif_bus_get_dev_status(struct device *dev,
				      struct devfreq_dev_status *stat)
{
	struct nocp_cnt tmp_nocp_cnt;
	struct busfreq_data_mif *data = dev_get_drvdata(dev);

	nocp_get_aver_cnt(&mif_noc_list, &tmp_nocp_cnt);

	rcu_read_lock();
	stat->current_frequency = opp_get_freq(data->curr_opp);
	rcu_read_unlock();
	/*
	 * Bandwidth of memory interface is 128bits
	 * So bus can transfer 16bytes per cycle
	 */
	tmp_nocp_cnt.total_byte_cnt >>= 4;

	stat->total_time = tmp_nocp_cnt.cycle_cnt;
	stat->busy_time = tmp_nocp_cnt.total_byte_cnt;

	if (en_profile)
		pr_info("%lu,%lu\n", tmp_nocp_cnt.total_byte_cnt, tmp_nocp_cnt.cycle_cnt);

	return 0;
}

static struct devfreq_dev_profile exynos5_mif_devfreq_profile = {
	.initial_freq	= 800000,
	.polling_ms	= 100,
	.target		= exynos5_mif_busfreq_target,
	.get_dev_status	= exynos5_mif_bus_get_dev_status,
};

static int exynos5410_mif_table(struct busfreq_data_mif *data)
{
	unsigned int i;
	unsigned int ret;
	unsigned int asv_volt;
	struct mif_clkdiv_info *tmp_mif_table;

	/* make list for setting value for int DVS */
	for (i = LV_0; i < LV_END; i++) {
		tmp_mif_table = kzalloc(sizeof(struct mif_clkdiv_info), GFP_KERNEL);

		tmp_mif_table->lv_idx = i;

		tmp_mif_table->cdrex0.reg_value = ((exynos5410_clkdiv_cdrex0[i][0] << EXYNOS5_CLKDIV_PCLK_CDREX_SHIFT) |\
						  (exynos5410_clkdiv_cdrex0[i][1] << EXYNOS5_CLKDIV_SCLK_CDREX_SHIFT) |\
						  (exynos5410_clkdiv_cdrex0[i][2] << EXYNOS5_CLKDIV_ACLK_CDREX1_SHIFT) |\
						  (exynos5410_clkdiv_cdrex0[i][3] << EXYNOS5_CLKDIV_CLK2X_PHY0_SHIFT));
		tmp_mif_table->cdrex0.reg_mask	= (EXYNOS5_CLKDIV_PCLK_CDREX_MASK | EXYNOS5_CLKDIV_SCLK_CDREX_MASK |\
						   EXYNOS5_CLKDIV_ACLK_CDREX1_MASK | EXYNOS5_CLKDIV_CLK2X_PHY0_MASK);

		tmp_mif_table->g2d.reg_value = ((exynos5410_clkdiv_g2d[i][0] << EXYNOS5410_CLKDIV_G2D_PCLK_SHIFT) |\
						  (exynos5410_clkdiv_g2d[i][1] << EXYNOS5410_CLKDIV_G2D_ACLK_SHIFT));
		tmp_mif_table->g2d.reg_mask = (EXYNOS5410_CLKDIV_G2D_PCLK_MASK | EXYNOS5410_CLKDIV_G2D_ACLK_MASK);

		tmp_mif_table->target_pms.p = exynos5410_bpll_pms_value[i][0];
		tmp_mif_table->target_pms.m = exynos5410_bpll_pms_value[i][1];
		tmp_mif_table->target_pms.s = exynos5410_bpll_pms_value[i][2];

		tmp_mif_table->target_dram_param.row = exynos5410_dram_param[i][0];
		tmp_mif_table->target_dram_param.data = exynos5410_dram_param[i][1];
		tmp_mif_table->target_dram_param.power = exynos5410_dram_param[i][2];

		list_add(&tmp_mif_table->list, &mif_dvfs_list);
	}

	/* will add code for ASV information setting function in here */

	for (i = 0; i < ARRAY_SIZE(mif_bus_opp_list); i++) {
		asv_volt = get_match_volt(ID_MIF, mif_bus_opp_list[i].clk);

		if (!asv_volt)
			asv_volt = mif_bus_opp_list[i].volt;

		pr_info("MIF %luKhz ASV is %duV\n", mif_bus_opp_list[i].clk, asv_volt);

		ret = opp_add(data->dev, mif_bus_opp_list[i].clk, asv_volt);

		if (ret) {
			dev_err(data->dev, "Fail to add opp entries.\n");
			return ret;
		}
	}

#ifdef BPLL_S_ONLY_CHANGE
	pr_info("S divider change for DFS of MIF block\n");
	opp_disable(data->dev, 667000);
	opp_disable(data->dev, 533000);
	opp_disable(data->dev, 267000);
	opp_disable(data->dev, 160000);
#endif

	return 0;
}

struct nocp_info nocp_mem0_0 = {
	.name		= "mem0_0",
	.id		= MEM0_0,
	.pa_base	= NOCP_BASE(MEM0_0),
};

struct nocp_info nocp_mem0_1 = {
	.name		= "mem0_1",
	.id		= MEM0_1,
	.pa_base	= NOCP_BASE(MEM0_1),
	.weight		= 5,
};

struct nocp_info nocp_mem1_0 = {
	.name		= "mem1_0",
	.id		= MEM1_0,
	.pa_base	= NOCP_BASE(MEM1_0),
};

struct nocp_info nocp_mem1_1 = {
	.name		= "mem1_1",
	.id		= MEM1_1,
	.pa_base	= NOCP_BASE(MEM1_1),
	.weight		= 5,
};

struct nocp_info nocp_cci = {
	.name		= "cci",
	.id		= CCI,
	.pa_base	= NOCP_BASE(CCI),
};

struct nocp_info *exynos5_mif_nocp_list[] = {
	&nocp_mem0_0,
	&nocp_mem0_1,
	&nocp_mem1_0,
	&nocp_mem1_1,
	&nocp_cci,
};

static struct devfreq_simple_usage_data exynos5_mif_governor_data = {
	.upthreshold		= 85,
	.target_percentage	= 80,
	.proportional		= 100,
	.cal_qos_max		= 800000,
	.pm_qos_class		= PM_QOS_BUS_THROUGHPUT,
	.en_monitoring		= true,
};

static ssize_t mif_show_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned int i;
	ssize_t len = 0;
	ssize_t write_cnt = (ssize_t)((PAGE_SIZE / LV_END) - 2);

	for (i = LV_0; i < LV_END; i++)
		len += snprintf(buf + len, write_cnt, "%ld %llu\n", mif_bus_opp_list[i].clk,
				(unsigned long long)mif_bus_opp_list[i].time_in_state);

	return len;
}

static DEVICE_ATTR(mif_time_in_state, S_IRUSR | S_IRGRP, mif_show_state, NULL);

static ssize_t show_upthreshold(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", exynos5_mif_governor_data.upthreshold);
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

	exynos5_mif_governor_data.upthreshold = value;
out:
	return count;
}

static DEVICE_ATTR(upthreshold, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
		show_upthreshold, store_upthreshold);

static ssize_t show_target_percentage(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", exynos5_mif_governor_data.target_percentage);
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

	exynos5_mif_governor_data.target_percentage = value;
out:
	return count;
}

static DEVICE_ATTR(target_percentage, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
		show_target_percentage, store_target_percentage);

static ssize_t show_proportional(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", exynos5_mif_governor_data.proportional);
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

	exynos5_mif_governor_data.proportional = value;
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

static struct attribute *busfreq_mif_entries[] = {
	&dev_attr_mif_time_in_state.attr,
	&dev_attr_upthreshold.attr,
	&dev_attr_target_percentage.attr,
	&dev_attr_proportional.attr,
	&dev_attr_en_profile.attr,
	NULL,
};

static struct attribute_group busfreq_mif_attr_group = {
	.name	= "time_in_state",
	.attrs	= busfreq_mif_entries,
};

static ssize_t show_freq_table(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i, count = 0;
	struct opp *opp;
	ssize_t write_cnt = (ssize_t)((PAGE_SIZE / ARRAY_SIZE(mif_bus_opp_list)) - 2);

	if (!unlikely(mif_dev)) {
		pr_err("%s: device is not probed\n", __func__);
		return -ENODEV;
	}

	rcu_read_lock();
	for (i = 0; i < ARRAY_SIZE(mif_bus_opp_list); i++) {
		opp = opp_find_freq_exact(mif_dev, mif_bus_opp_list[i].clk, true);
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
			exynos5_mif_governor_data.en_monitoring ? "true" : "false");
}

static ssize_t store_en_monitoring(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned int input;

	if (!sscanf(buf, "%u", &input))
		return -EINVAL;

	if (input) {
		exynos5_mif_governor_data.en_monitoring = true;
		pm_qos_update_request(&exynos5_mif_qos, 200000);
		exynos5_mif_notify_transition(NULL, MIF_DEVFREQ_EN_MONITORING);
	} else {
		exynos5_mif_governor_data.en_monitoring = false;
		exynos5_mif_notify_transition(NULL, MIF_DEVFREQ_DIS_MONITORING);
	}

	return count;
}

static DEVICE_ATTR(en_monitoring, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
			show_en_monitoring, store_en_monitoring);

static struct exynos_devfreq_platdata default_qos_mif_pd = {
	.default_qos = 160000,
};

static int exynos5_mif_reboot_notifier_call(struct notifier_block *this,
				   unsigned long code, void *_cmd)
{
	pm_qos_update_request(&exynos5_mif_qos, 800000);

	return NOTIFY_DONE;
}

static struct notifier_block exynos5_mif_reboot_notifier = {
	.notifier_call = exynos5_mif_reboot_notifier_call,
};

static int exynos5_mif_cpufreq_notifier_call(struct notifier_block *this,
				   unsigned long code, void *_cmd)
{
	struct cpufreq_freqs *freq = (struct cpufreq_freqs *)_cmd;
	unsigned long type;

	if (exynos5_mif_governor_data.en_monitoring)
		return NOTIFY_DONE;

	if (freq->cpu)
		return NOTIFY_DONE;

	/*
	 * If freq->new and freq->old are same, cpufreq driver does not
	 * notify the transition event. So, this notifier call function
	 * does not check whether or not they are same.
	 */
	if (freq->new > freq->old)
		type = CPUFREQ_PRECHANGE;
	else
		type = CPUFREQ_POSTCHANGE;

	if (type == code) {
		if (freq->new <= 500000)
			pm_qos_update_request(&exynos5_mif_qos, 200000);
		else if (freq->new <= 600000)
			pm_qos_update_request(&exynos5_mif_qos, 400000);
		else if (freq->new > 600000)
			pm_qos_update_request(&exynos5_mif_qos, 800000);
	}

	return NOTIFY_DONE;
}

static struct notifier_block exynos5_mif_cpufreq_notifier = {
	.notifier_call = exynos5_mif_cpufreq_notifier_call,
};

static unsigned int  dll_lock_and_forcing(void __iomem *base)
{
	unsigned int reg;

	/* wait until dll lock */
	while ((__raw_readl(base + 0x34) & 0x1) != 0x1);

	/* Read dll lock value */
	reg = (__raw_readl(base + 0x34) & (0x7f << 10)) >> 10;
	pr_debug("Before dll lock[%d]\n", reg);

	/* For more stability */
	reg = reg - 5;

	/* forcing dll lock value */
	__raw_writel((__raw_readl(base + 0x30) & ~(0x7f << 8) | (reg << 8)), base + 0x30);

	/* dll off */
	__raw_writel(__raw_readl(base + 0x30) & ~(1 << 5), base + 0x30);

	pr_debug("After dll lock[%d], PHY reg[0x%x]\n", reg, __raw_readl(base + 0x30));

	return reg;
}

static int exynos5_bus_mif_tmu_notifier(struct notifier_block *notifier,
						unsigned long event, void *v)
{
	unsigned int *on = v;

	if (event == TMU_COLD) {
		if (*on) {
			printk(" MIF 800MHZ requested\n");

			if (pm_qos_request_active(&exynos5_mif_qos))
				pm_qos_update_request(&exynos5_mif_qos, 800000);
		} else {
			printk(" MIF 160MHZ requested\n");

			if (pm_qos_request_active(&exynos5_mif_qos))
				pm_qos_update_request(&exynos5_mif_qos, 160000);

		}

		return NOTIFY_OK;
	}

	switch (event) {
#ifdef CONFIG_ARM_TRUSTZONE
	case TMU_NORMAL:
		exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_0 + DREX_TIMINGAREF),
				 AREF_NORMAL, 0);
		exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_1 + DREX_TIMINGAREF),
				 AREF_NORMAL, 0);
		break;
	case TMU_HOT:
		exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_0 + DREX_TIMINGAREF),
				AREF_HOT, 0);
		exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_1 + DREX_TIMINGAREF),
				AREF_HOT, 0);
		break;
	case TMU_CRITICAL:
		exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_0 + DREX_TIMINGAREF),
				AREF_CRITICAL, 0);
		exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXI_1 + DREX_TIMINGAREF),
				AREF_CRITICAL, 0);
		break;
#else
	case TMU_NORMAL:
		__raw_writel(AREF_NORMAL,(exynos5_base_drexI_0 + DREX_TIMINGAREF));
		__raw_writel(AREF_NORMAL,(exynos5_base_drexI_1 + DREX_TIMINGAREF));
		break;
	case TMU_HOT:
		__raw_writel(AREF_HOT,(exynos5_base_drexI_0 + DREX_TIMINGAREF));
		__raw_writel(AREF_HOT,(exynos5_base_drexI_1 + DREX_TIMINGAREF));
		break;
	case TMU_CRITICAL:
		__raw_writel(AREF_CRITICAL,(exynos5_base_drexI_0 + DREX_TIMINGAREF));
		__raw_writel(AREF_CRITICAL,(exynos5_base_drexI_1 + DREX_TIMINGAREF));
		break;
#endif
	}

	return NOTIFY_OK;
}

static struct notifier_block exynos5_bus_mif_tmu_nb = {
	.notifier_call = exynos5_bus_mif_tmu_notifier,
};

static __devinit int exynos5_busfreq_mif_probe(struct platform_device *pdev)
{
	struct busfreq_data_mif *data;
	struct opp *opp;
	struct device *dev = &pdev->dev;
	unsigned int tmp;
	unsigned long tmpfreq;
	struct exynos_devfreq_platdata *pdata;
	int err = 0;
	unsigned int init_volt;

	data = kzalloc(sizeof(struct busfreq_data_mif), GFP_KERNEL);

	if (data == NULL) {
		dev_err(dev, "Cannot allocate memory for INT.\n");
		return -ENOMEM;
	}

	/* Enable pause function for DREX2 DVFS */
	tmp = __raw_readl(EXYNOS5_DMC_PAUSE_CTRL);
	tmp |= EXYNOS5_DMC_PAUSE_ENABLE;
	__raw_writel(tmp, EXYNOS5_DMC_PAUSE_CTRL);

#ifndef CONFIG_ARM_TRUSTZONE
	/* ioremap for drex base address */
	exynos5_base_drexI_0 = ioremap(EXYNOS5_PA_DREXI_0, SZ_128K);
	exynos5_base_drexI_1 = ioremap(EXYNOS5_PA_DREXI_1, SZ_128K);
#endif

	phy0_base = ioremap(0x10c00000, SZ_4K);
	phy1_base = ioremap(0x10c10000, SZ_4K);

	data->dev = dev;
	mutex_init(&data->lock);

	/* Setting table for int*/
	exynos5410_mif_table(data);

	/* Initialization NoC for MIF block */
	regist_nocp(&mif_noc_list, exynos5_mif_nocp_list,
		ARRAY_SIZE(exynos5_mif_nocp_list), NOCP_USAGE_MIF);

	rcu_read_lock();
	opp = opp_find_freq_floor(dev, &exynos5_mif_devfreq_profile.initial_freq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "Invalid initial frequency %lu kHz.\n",
			       exynos5_mif_devfreq_profile.initial_freq);
		err = PTR_ERR(opp);
		goto err_opp_add;
	}
	rcu_read_unlock();

	mif_pre_time = get_jiffies_64();

	data->curr_opp = opp;

	platform_set_drvdata(pdev, data);

#if defined(CONFIG_MACH_ODROIDXU)
	data->devfreq = devfreq_add_device(dev, &exynos5_mif_devfreq_profile,
					   &devfreq_performance, NULL);
#else
	data->devfreq = devfreq_add_device(dev, &exynos5_mif_devfreq_profile,
					   &devfreq_simple_usage, &exynos5_mif_governor_data);
#endif

	if (IS_ERR(data->devfreq)) {
		err = PTR_ERR(data->devfreq);
		goto err_opp_add;
	}

	/* Set Max information for devfreq */
	tmpfreq = ULONG_MAX;
	rcu_read_lock();
	opp = opp_find_freq_floor(dev, &tmpfreq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "%s: Invalid OPP.\n", __func__);
		err = PTR_ERR(opp);

		goto err_opp_add;
	}
	data->devfreq->max_freq = opp_get_freq(opp);
	init_volt = opp_get_voltage(opp);
	rcu_read_unlock();

	devfreq_register_opp_notifier(dev, data->devfreq);
	mif_dev = data->dev;

	/* Create file for time_in_state */
	err = sysfs_create_group(&data->devfreq->dev.kobj, &busfreq_mif_attr_group);

	/* Add sysfs for freq_table */
	err = device_create_file(&data->devfreq->dev, &dev_attr_freq_table);
	if (err)
		pr_err("%s: Fail to create sysfs file\n", __func__);

	/* Add sysfs for en_monitoring */
	err = device_create_file(&data->devfreq->dev, &dev_attr_en_monitoring);
	if (err)
		pr_err("%s: Fail to create sysfs file\n", __func__);

	pdata = pdev->dev.platform_data;
	if (!pdata)
		pdata = &default_qos_mif_pd;

	pm_qos_add_request(&exynos5_mif_qos, PM_QOS_BUS_THROUGHPUT, pdata->default_qos);
	pm_qos_add_request(&boot_mif_qos, PM_QOS_BUS_THROUGHPUT, 0);
	pm_qos_update_request_timeout(&boot_mif_qos, 800000, 40000 * 1000);

	pr_info("init_volt[%d], freq[%d]\n", init_volt, data->devfreq->max_freq);

	err = exynos5_volt_ctrl(VDD_MIF, init_volt, data->devfreq->max_freq);
	__raw_writel(((dll_lock_and_forcing(phy1_base) & 0x7f) << 16) |
					((dll_lock_and_forcing(phy0_base) & 0x7f)), EXYNOS_PMU_SPARE1);

	pr_debug("PMU_SPARE1 0x%x\n", __raw_readl(EXYNOS_PMU_SPARE1));

	register_reboot_notifier(&exynos5_mif_reboot_notifier);
#ifdef CONFIG_EXYNOS_THERMAL
	exynos_tmu_add_notifier(&exynos5_bus_mif_tmu_nb);
#endif
	cpufreq_register_notifier(&exynos5_mif_cpufreq_notifier, CPUFREQ_TRANSITION_NOTIFIER);

	return 0;

err_opp_add:
	kfree(data);

	return err;
}

static __devexit int exynos5_busfreq_mif_remove(struct platform_device *pdev)
{
	struct busfreq_data_mif *data = platform_get_drvdata(pdev);

	devfreq_remove_device(data->devfreq);
	kfree(data);

	pm_qos_remove_request(&exynos5_mif_qos);

#ifndef CONFIG_ARM_TRUSTZONE
	iounmap(exynos5_base_drexI_0);
	iounmap(exynos5_base_drexI_1);
#endif

	return 0;
}

unsigned int exynos5_bb_con_save;

static int exynos5_busfreq_mif_suspend(struct device *dev)
{
	if (pm_qos_request_active(&exynos5_mif_qos))
		pm_qos_update_request(&exynos5_mif_qos, 800000);

	/* Save for BB_CON1 */
	exynos5_bb_con_save = __raw_readl(EXYNOS5410_BB_CON1);

	return 0;
}

static int exynos5_busfreq_mif_resume(struct device *dev)
{
	unsigned int tmp;

	/* Enable pause function for DREX2 DVFS */
	tmp = __raw_readl(EXYNOS5_DMC_PAUSE_CTRL);
	tmp |= EXYNOS5_DMC_PAUSE_ENABLE;
	__raw_writel(tmp, EXYNOS5_DMC_PAUSE_CTRL);

	/* Restore for BB_CON1 */
	__raw_writel(exynos5_bb_con_save, EXYNOS5410_BB_CON1);

	resume_nocp(&mif_noc_list);

	if (pm_qos_request_active(&exynos5_mif_qos))
		pm_qos_update_request(&exynos5_mif_qos, 160000);

	pr_debug("phy0 lock[%d], phy1 lock[%d]\n", (__raw_readl(phy0_base + 0x30) & (0x7f << 8)) >> 8,
						(__raw_readl(phy1_base + 0x30) & (0x7f << 8)) >> 8);

	pr_debug("PMU_SPARE1 0x%x\n", __raw_readl(EXYNOS_PMU_SPARE1));

	return 0;
}

static const struct dev_pm_ops exynos5_busfreq_mif_pm = {
	.suspend = exynos5_busfreq_mif_suspend,
	.resume	= exynos5_busfreq_mif_resume,
};

static struct platform_driver exynos5_busfreq_mif_driver = {
	.probe	= exynos5_busfreq_mif_probe,
	.remove	= __devexit_p(exynos5_busfreq_mif_remove),
	.driver = {
		.name	= "exynos5-busfreq-mif",
		.owner	= THIS_MODULE,
		.pm	= &exynos5_busfreq_mif_pm,
	},

};

static int __init exynos5_busfreq_mif_init(void)
{
	return platform_driver_register(&exynos5_busfreq_mif_driver);
}
late_initcall(exynos5_busfreq_mif_init);

static void __exit exynos5_busfreq_mif_exit(void)
{
	platform_driver_unregister(&exynos5_busfreq_mif_driver);
}
module_exit(exynos5_busfreq_mif_exit);
