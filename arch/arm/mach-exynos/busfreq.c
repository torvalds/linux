/* linux/arch/arm/mach-exynos/busfreq.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - BUS clock frequency scaling support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/ktime.h>
#include <linux/tick.h>
#include <linux/kernel_stat.h>
#include <linux/cpufreq.h>
#include <linux/suspend.h>
#include <linux/reboot.h>
#include <linux/clk.h>
#include <linux/pm_qos_params.h>

#include <asm/mach-types.h>

#include <mach/ppmu.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/gpio.h>
#include <mach/regs-mem.h>
#include <mach/cpufreq.h>
#include <mach/asv.h>
#include <mach/sec_debug.h>

#include <plat/map-s5p.h>
#include <plat/gpio-cfg.h>
#include <plat/cpu.h>

#define MAX_LOAD		100
#define DIVIDING_FACTOR		10000
#define UP_THRESHOLD_DEFAULT	23

#define SYSFS_DEBUG_BUSFREQ

static unsigned up_threshold;
static struct regulator *int_regulator;
static struct exynos4_ppmu_hw dmc[2];
static struct exynos4_ppmu_hw cpu;
static unsigned int bus_utilization[2];
static struct cpufreq_freqs *freqs;

static unsigned int g_busfreq_lock_id;
static enum busfreq_level_request g_busfreq_lock_val[DVFS_LOCK_ID_END];
static enum busfreq_level_request g_busfreq_lock_level;

const char *const cpufreq_lock_name[DVFS_LOCK_ID_END] = {
	[DVFS_LOCK_ID_G2D] = "G2D",
	[DVFS_LOCK_ID_TV] = "TV",
	[DVFS_LOCK_ID_MFC] = "MFC",
	[DVFS_LOCK_ID_USB] = "USB",
	[DVFS_LOCK_ID_CAM] = "CAM",
	[DVFS_LOCK_ID_PM] = "PM",
	[DVFS_LOCK_ID_USER] = "USER",
	[DVFS_LOCK_ID_LCD] = "LCD",
	[DVFS_LOCK_ID_ROTATION_BOOSTER] = "ROTATION_BOOSTER",
};

static DEFINE_MUTEX(set_bus_freq_lock);

enum busfreq_level_idx {
	LV_0,
	LV_1,
	LV_2,
	LV_END
};

#ifdef SYSFS_DEBUG_BUSFREQ
static unsigned int time_in_state[LV_END];
unsigned long prejiffies;
unsigned long curjiffies;
#endif

static unsigned int p_idx;
static unsigned int curr_idx;
static bool init_done;

struct busfreq_table {
	unsigned int idx;
	unsigned int mem_clk;
	unsigned int volt;
	unsigned int clk_topdiv;
	unsigned int clk_dmcdiv;
};

static struct busfreq_table exynos4_busfreq_table[] = {
	{LV_0, 400000, 1100000, 0, 0},
	{LV_1, 267000, 1000000, 0, 0},
#ifdef CONFIG_BUSFREQ_L2_160M
	/*L2: 160MHz */
	{LV_2, 160000, 1000000, 0, 0},
#else
	/* L2: 133MHz */
	{LV_2, 133000, 950000, 0, 0},
#endif
	{0, 0, 0, 0, 0},
};

#ifdef CONFIG_BUSFREQ_QOS
enum busfreq_qos_target {
	BUS_QOS_0,
	BUS_QOS_1,
	BUS_QOS_MAX,
};

static enum busfreq_qos_target busfreq_qos = BUS_QOS_0;

/*  GDL: [3] MFC_L, [2] G3D, [1] TV, [0] Image */
/*  GDR: [5] MAUDIO, [4] MFC_R, [3] FSYS, [2] LCD1, [1] LCD0, [0] CAM */
#if defined(CONFIG_BUSFREQ_QOS_NONE)
static unsigned int exynos4_qos_value[BUS_QOS_MAX][LV_END][4] = {
	{
		{0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00},
	},
	{
		{0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00},
	}
};
#elif defined(CONFIG_BUSFREQ_QOS_1024X600)	/* For P2 */
static unsigned int exynos4_qos_value[BUS_QOS_MAX][LV_END][4] = {
	{
		{0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00},
		{0x06, 0x0b, 0x00, 0x00},
	},
	{
		{0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00},
		{0x06, 0x0b, 0x00, 0x00},
	}
};
#elif defined(CONFIG_BUSFREQ_QOS_1280X800)	/* For Q1, P8 */
static unsigned int exynos4_qos_value[BUS_QOS_MAX][LV_END][4] = {
	{
		{0x06, 0x03, 0x06, 0x2f},
		{0x06, 0x03, 0x06, 0x2f},
		{0x03, 0x0b, 0x00, 0x00},
	},
	{
		{0x06, 0x0b, 0x00, 0x00},
		{0x06, 0x0b, 0x00, 0x00},
		{0x03, 0x0b, 0x00, 0x00},
	}
};
#endif
#endif

#define ASV_GROUP	5
static unsigned int exynos4_asv_volt[ASV_GROUP][LV_END] = {
	{1150000, 1050000, 1050000},
	{1125000, 1025000, 1025000},
	{1100000, 1000000, 1000000},
	{1075000, 975000, 975000},
	{1050000, 950000, 950000},
};

static unsigned int clkdiv_dmc0[LV_END][8] = {
	/*
	 * Clock divider value for following
	 * { DIVACP, DIVACP_PCLK, DIVDPHY, DIVDMC, DIVDMCD
	 *              DIVDMCP, DIVCOPY2, DIVCORE_TIMERS }
	 */

	/* DMC L0: 400MHz */
	{ 3, 2, 1, 1, 1, 1, 3, 1 },

	/* DMC L1: 266.7MHz */
	{ 4, 2, 1, 2, 1, 1, 3, 1 },

#ifdef CONFIG_BUSFREQ_L2_160M
	/* DMC L2: 160MHz */
	{ 5, 1, 1, 4, 1, 1, 3, 1 },
#else
	/* DMC L2: 133MHz */
	{ 5, 2, 1, 5, 1, 1, 3, 1 },
#endif
};

static unsigned int clkdiv_top[LV_END][5] = {
	/*
	 * Clock divider value for following
	 * { DIVACLK200, DIVACLK100, DIVACLK160, DIVACLK133, DIVONENAND }
	 */

	/* ACLK200 L0: 200MHz */
	{ 3, 7, 4, 5, 1 },

	/* ACLK200 L1: 160MHz */
	{ 4, 7, 5, 6, 1 },

	/* ACLK200 L2: 133MHz */
	{ 5, 7, 7, 7, 1 },
};

static unsigned int clkdiv_lr_bus[LV_END][2] = {
	/*
	 * Clock divider value for following
	 * { DIVGDL/R, DIVGPL/R }
	 */

	/* ACLK_GDL/R L1: 200MHz */
	{ 3, 1 },

	/* ACLK_GDL/R L2: 160MHz */
	{ 4, 1 },

	/* ACLK_GDL/R L3: 133MHz */
	{ 5, 1 },
};

static unsigned int clkdiv_ip_bus[LV_END][3] = {
	/*
	 * Clock divider value for following
	 * { DIV_MFC, DIV_G2D, DIV_FIMC }
	 */

	/* L0: MFC 200MHz G2D 266MHz FIMC 160MHz */
	{ 3, 2, 4 },

	/* L1: MFC 200MHz G2D 160MHz FIMC 133MHz */
	/* { 4, 4, 5 }, */
	{ 3, 4, 5 },

	/* L2: MFC 200MHz G2D 133MHz FIMC 100MHz */
	/* { 5, 5, 7 }, */
	{ 3, 5, 7 },
};

#ifdef CONFIG_BUSFREQ_QOS
static void exynos4_set_qos(unsigned int index)
{
	/* printk(KERN_INFO "exynos4_set_qos level %d\n", index); */
	__raw_writel(exynos4_qos_value[busfreq_qos][index][0], S5P_VA_GDL + 0x400);
	__raw_writel(exynos4_qos_value[busfreq_qos][index][1], S5P_VA_GDL + 0x404);
	__raw_writel(exynos4_qos_value[busfreq_qos][index][2], S5P_VA_GDR + 0x400);
	__raw_writel(exynos4_qos_value[busfreq_qos][index][3], S5P_VA_GDR + 0x404);
}
#endif

static void exynos4_set_busfreq(unsigned int div_index)
{
	unsigned int tmp, val;

	sec_debug_aux_log(SEC_DEBUG_AUXLOG_CPU_BUS_CLOCK_CHANGE,
			"%s: div_index=%d(%ps)", __func__, div_index,
			__builtin_return_address(0));

	/* Change Divider - DMC0 */
	tmp = exynos4_busfreq_table[div_index].clk_dmcdiv;

	__raw_writel(tmp, EXYNOS4_CLKDIV_DMC0);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_DMC0);
	} while (tmp & 0x11111111);

	/* Change Divider - TOP */
	tmp = exynos4_busfreq_table[div_index].clk_topdiv;

	__raw_writel(tmp, EXYNOS4_CLKDIV_TOP);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_TOP);
	} while (tmp & 0x11111);

	/* Change Divider - LEFTBUS */
	tmp = __raw_readl(EXYNOS4_CLKDIV_LEFTBUS);

	tmp &= ~(EXYNOS4_CLKDIV_BUS_GDLR_MASK | EXYNOS4_CLKDIV_BUS_GPLR_MASK);

	tmp |= ((clkdiv_lr_bus[div_index][0] << EXYNOS4_CLKDIV_BUS_GDLR_SHIFT) |
		(clkdiv_lr_bus[div_index][1] << EXYNOS4_CLKDIV_BUS_GPLR_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_LEFTBUS);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_LEFTBUS);
	} while (tmp & 0x11);

	/* Change Divider - RIGHTBUS */
	tmp = __raw_readl(EXYNOS4_CLKDIV_RIGHTBUS);

	tmp &= ~(EXYNOS4_CLKDIV_BUS_GDLR_MASK | EXYNOS4_CLKDIV_BUS_GPLR_MASK);

	tmp |= ((clkdiv_lr_bus[div_index][0] << EXYNOS4_CLKDIV_BUS_GDLR_SHIFT) |
		(clkdiv_lr_bus[div_index][1] << EXYNOS4_CLKDIV_BUS_GPLR_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_RIGHTBUS);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_RIGHTBUS);
	} while (tmp & 0x11);

	/* Change Divider - SCLK_MFC */
	tmp = __raw_readl(EXYNOS4_CLKDIV_MFC);

	tmp &= ~EXYNOS4_CLKDIV_MFC_MASK;

	tmp |= (clkdiv_ip_bus[div_index][0] << EXYNOS4_CLKDIV_MFC_SHIFT);

	__raw_writel(tmp, EXYNOS4_CLKDIV_MFC);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_MFC);
	} while (tmp & 0x1);

	/* Change Divider - SCLK_G2D */
	tmp = __raw_readl(EXYNOS4_CLKDIV_IMAGE);

	tmp &= ~EXYNOS4_CLKDIV_IMAGE_MASK;

	tmp |= (clkdiv_ip_bus[div_index][1] << EXYNOS4_CLKDIV_IMAGE_SHIFT);

	__raw_writel(tmp, EXYNOS4_CLKDIV_IMAGE);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_IMAGE);
	} while (tmp & 0x1);

	/* Change Divider - SCLK_FIMC */
	tmp = __raw_readl(EXYNOS4_CLKDIV_CAM);

	tmp &= ~EXYNOS4_CLKDIV_CAM_MASK;

	val = clkdiv_ip_bus[div_index][2];
	tmp |= ((val << 0) | (val << 4) | (val << 8) | (val << 12));

	__raw_writel(tmp, EXYNOS4_CLKDIV_CAM);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_CAM);
	} while (tmp & 0x1111);
}

static unsigned int calc_bus_utilization(struct exynos4_ppmu_hw *ppmu)
{
	if (ppmu->ccnt == 0) {
		pr_err("%s: 0 value is not permitted\n", __func__);
		return MAX_LOAD;
	}

	if (!(ppmu->ccnt >> 7))
		return (ppmu->count[0] * 100) / ppmu->ccnt;
	else
		return ((ppmu->count[0] >> 7) * 100) / (ppmu->ccnt >> 7);
}

static int busfreq_target(struct busfreq_table *freq_table,
			  unsigned int ppc_load,
			  unsigned int ppmu_load,
			  unsigned int pre_idx,
			  unsigned int *index)
{
	unsigned int i, target_freq, idx = 0;

	if (ppc_load > MAX_LOAD)
		return -EINVAL;

	if (ppc_load > 50) {
		pr_debug("Busfreq: Bus Load is larger than 40(%d)\n", ppc_load);
		ppc_load = 50;
	}

#ifdef CONFIG_BUSFREQ_L2_160M
		target_freq = (ppc_load * freq_table[p_idx].mem_clk) /
							(up_threshold);

		for (i = 1; i <= LV_END; i++) {
			if (target_freq >= freq_table[i].mem_clk) {
				idx = i - 1;
				break;
			}
		}

		idx = freq_table[idx].idx;
#else
	if (ppc_load >= up_threshold) {
		target_freq = freq_table[0].mem_clk;
	} else {
		target_freq = (ppc_load * freq_table[pre_idx].mem_clk) /
			up_threshold;

		if (target_freq >= freq_table[pre_idx].mem_clk) {
			for (i = 0; (freq_table[i].mem_clk != 0); i++) {
				unsigned int freq = freq_table[i].mem_clk;

				if (freq <= target_freq) {
					idx = i;
					break;
				}
			}

		} else {
			for (i = 0; (freq_table[i].mem_clk != 0); i++) {
				unsigned int freq = freq_table[i].mem_clk;

				if (freq >= target_freq) {
					idx = i;
					continue;
				}

				if (freq < target_freq)
					break;
			}
		}
	}

	if ((freqs->new == exynos_info->freq_table[exynos_info->max_support_idx].frequency)
			&& (ppc_load == 0))
		idx = pre_idx;
#endif

	if ((idx > LV_1) && (ppmu_load > 5))
		idx = LV_1;

	if (idx > g_busfreq_lock_level)
		idx = g_busfreq_lock_level;

	*index = idx;

	return 0;
}

static void busfreq_mon_reset(void)
{
	unsigned int i;

	for (i = 0; i < 2; i++) {
		exynos4_ppc_reset(&dmc[i]);
		exynos4_ppc_setevent(&dmc[i], 0x6);
		exynos4_ppc_start(&dmc[i]);
	}

	exynos4_ppmu_reset(&cpu);

	exynos4_ppmu_setevent(&cpu, 3);

	exynos4_ppmu_start(&cpu);
}

static unsigned int busfreq_monitor(void)
{
	unsigned int i, index = 0, ret;
	unsigned long long ppcload, ppmuload;
#ifdef SYSFS_DEBUG_BUSFREQ
	unsigned long level_state_jiffies;
#endif

	for (i = 0; i < 2; i++) {
		exynos4_ppc_stop(&dmc[i]);
		exynos4_ppc_update(&dmc[i]);
		bus_utilization[i] = calc_bus_utilization(&dmc[i]);
	}

	exynos4_ppmu_stop(&cpu);
	ppmuload = exynos4_ppmu_update(&cpu, 3);

	if (ppmuload > 10) {
		index = LV_0;
		goto out;
	}

	ppcload = max(bus_utilization[0], bus_utilization[1]);
	index = p_idx;

	/* Change bus frequency */
	ret = busfreq_target(exynos4_busfreq_table, ppcload,
		ppmuload, p_idx, &index);
	if (ret)
		pr_err("%s: (%d)\n", __func__, ret);

#ifdef SYSFS_DEBUG_BUSFREQ
	curjiffies = jiffies;
	if (prejiffies != 0)
		level_state_jiffies = curjiffies - prejiffies;
	else
		level_state_jiffies = 0;

	prejiffies = jiffies;

	switch (p_idx) {
	case LV_0:
		time_in_state[LV_0] += level_state_jiffies;
		break;
	case LV_1:
		time_in_state[LV_1] += level_state_jiffies;
		break;
	case LV_2:
		time_in_state[LV_2] += level_state_jiffies;
		break;
	default:
		break;
	}
#endif

out:
	pr_debug("Bus freq(%d-%d)\n", p_idx, index);

	busfreq_mon_reset();

	return index;
}

static int exynos4_busfreq_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	unsigned int voltage;

	switch (event) {
	case CPUFREQ_PRECHANGE:
		freqs = (struct cpufreq_freqs *)ptr;
		curr_idx = busfreq_monitor();
		break;
	case CPUFREQ_POSTCHANGE:
		voltage = exynos4_busfreq_table[curr_idx].volt;
		if (p_idx > curr_idx)
			regulator_set_voltage(int_regulator, voltage,
					voltage);

		if (p_idx != curr_idx) {
#ifdef CONFIG_BUSFREQ_QOS
			exynos4_set_qos(curr_idx);
#endif
			exynos4_set_busfreq(curr_idx);
		}

		if (p_idx < curr_idx)
			regulator_set_voltage(int_regulator, voltage,
					voltage);
		p_idx = curr_idx;
		break;
	case CPUFREQ_RESUMECHANGE:
		break;
	default:
		/* ignore */
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block exynos4_busfreq_notifier = {
	.notifier_call = exynos4_busfreq_notifier_event,
};

static int exynos4_buspm_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		exynos4_busfreq_lock(DVFS_LOCK_ID_PM, BUS_L0);
		return NOTIFY_OK;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		exynos4_busfreq_lock_free(DVFS_LOCK_ID_PM);
		busfreq_mon_reset();
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static struct notifier_block exynos4_buspm_notifier = {
	.notifier_call = exynos4_buspm_notifier_event,
};

static int exynos4_busfreq_reboot_notify(struct notifier_block *this,
		unsigned long code, void *unused)
{
	if (exynos4_busfreq_lock(DVFS_LOCK_ID_PM, BUS_L0) < 0)
		return NOTIFY_BAD;

	printk(KERN_INFO "REBOOT Notifier for BUSFREQ\n");
	return NOTIFY_DONE;
}

static struct notifier_block exynos4_busfreq_reboot_notifier = {
	.notifier_call = exynos4_busfreq_reboot_notify,
};

#ifdef CONFIG_BUSFREQ_QOS
static int exynos4_bus_qos_notify(struct notifier_block *nb,
		unsigned long l, void *v)
{
	busfreq_qos = (int)l;
	printk(KERN_INFO "exynos4_bus_qos_notify table %d\n", busfreq_qos);
	exynos4_set_qos(curr_idx);

	return NOTIFY_OK;
}

static struct notifier_block exynos4_busqos_notifier = {
	.notifier_call = exynos4_bus_qos_notify,
};
#endif

int exynos4_busfreq_lock(unsigned int nId,
	enum busfreq_level_request busfreq_level)
{
	int ret = 0;
	unsigned int int_volt;

	if (!init_done) {
		pr_debug("Busfreq does not support on this system\n");
		return -ENODEV;
	}

	mutex_lock(&set_bus_freq_lock);
	if (g_busfreq_lock_id & (1 << nId)) {
		pr_err("This device [%d] already locked busfreq\n", nId);
		ret = -EINVAL;
		goto err;
	}
	g_busfreq_lock_id |= (1 << nId);
	g_busfreq_lock_val[nId] = busfreq_level;

	/* If the requested cpufreq is higher than current min frequency */
	if (busfreq_level < g_busfreq_lock_level) {
		g_busfreq_lock_level = busfreq_level;
		/* get the voltage value */
		int_volt = exynos4_busfreq_table[busfreq_level].volt;
#ifdef CONFIG_BUSFREQ_QOS
		exynos4_set_qos(curr_idx);
#endif
		regulator_set_voltage(int_regulator, int_volt,
				int_volt);
		exynos4_set_busfreq(busfreq_level);
	}
err:
	mutex_unlock(&set_bus_freq_lock);

	return ret;
}

void exynos4_busfreq_lock_free(unsigned int nId)
{
	unsigned int i;

	if (!init_done) {
		pr_debug("Busfreq does not support on this system\n");
		return;
	}

	mutex_lock(&set_bus_freq_lock);
	g_busfreq_lock_id &= ~(1 << nId);
	g_busfreq_lock_val[nId] = BUS_LEVEL_END - 1;
	g_busfreq_lock_level = BUS_LEVEL_END - 1;

	if (g_busfreq_lock_id) {
		for (i = 0; i < DVFS_LOCK_ID_END; i++) {
			if (g_busfreq_lock_val[i] < g_busfreq_lock_level)
				g_busfreq_lock_level = g_busfreq_lock_val[i];
		}
	}
	mutex_unlock(&set_bus_freq_lock);
}

void exynos4_request_apply(unsigned long freq, struct device *dev)
{
	/* not supported yet */
}

static void __init exynos4_set_bus_volt(void)
{
	unsigned int asv_group;
	unsigned int i;

	asv_group = exynos_result_of_asv & 0xF;

	printk(KERN_INFO "DVFS : VDD_INT Voltage table set with %d Group\n", asv_group);

	for (i = 0 ; i < LV_END ; i++) {

		switch (asv_group) {
		case 0:
			exynos4_busfreq_table[i].volt =
				exynos4_asv_volt[0][i];
			break;
		case 1:
		case 2:
			exynos4_busfreq_table[i].volt =
				exynos4_asv_volt[1][i];
			break;
		case 3:
		case 4:
			exynos4_busfreq_table[i].volt =
				exynos4_asv_volt[2][i];
			break;
		case 5:
		case 6:
			exynos4_busfreq_table[i].volt =
				exynos4_asv_volt[3][i];
			break;
		case 7:
			exynos4_busfreq_table[i].volt =
				exynos4_asv_volt[4][i];
			break;
		}
	}

	return;
}

#ifdef SYSFS_DEBUG_BUSFREQ
static ssize_t show_time_in_state(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i;

	for (i = 0; i < LV_END; i++)
		len += sprintf(buf + len, "%u: %u\n",
			exynos4_busfreq_table[i].mem_clk, time_in_state[i]);

	return len;
}

static ssize_t store_time_in_state(struct kobject *kobj,
		struct attribute *attr, const char *buf, size_t count)
{
	return count;
}

static struct global_attr busfreq_time_in_state_attr = __ATTR(busfreq_time_in_state,
		0644, show_time_in_state, store_time_in_state);

static ssize_t show_up_threshold(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", up_threshold);
}

static ssize_t store_up_threshold(struct kobject *kobj,
		struct attribute *attr, const char *buf, size_t count)
{
	int ret;

	ret = sscanf(buf, "%u", &up_threshold);
	if (ret != 1)
		return -EINVAL;
	printk(KERN_ERR "** Up_Threshold is changed to %u **\n", up_threshold);

	return count;
}

static struct global_attr busfreq_up_threshold_attr = __ATTR(busfreq_up_threshold,
		0644, show_up_threshold, store_up_threshold);

static ssize_t show_busfreq_level_lock(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", g_busfreq_lock_level);
}

static ssize_t store_busfreq_level_lock(struct kobject *kobj,
		struct attribute *attr, const char *buf, size_t count)
{
	int level;
	sscanf(buf, "%d", &level);
	if (level >= BUS_LEVEL_END)
		return -EINVAL;

	if (level < 0)
		exynos4_busfreq_lock_free(DVFS_LOCK_ID_USER);
	else
		exynos4_busfreq_lock(DVFS_LOCK_ID_USER, level);
	return count;
}

static struct global_attr busfreq_level__lock_attr = __ATTR(busfreq_level_lock,
		0644, show_busfreq_level_lock, store_busfreq_level_lock);

static ssize_t show_busfreq_level(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	int i;
	int ret = 0;
	ret = sprintf(buf, "Lock Name = ");
	for (i = 0; i < DVFS_LOCK_ID_END; i++) {
		if (g_busfreq_lock_id & (1 << i))
			ret += sprintf(&buf[ret], "%s - %d\n",
				cpufreq_lock_name[i], g_busfreq_lock_val[i]);
	}
	ret += sprintf(&buf[ret], "\nCurrent Busfreq Level : %d\n", p_idx);
	return ret;
}

static struct global_attr busfreq_level_attr = __ATTR(busfreq_current_level,
		S_IRUGO, show_busfreq_level, NULL);
#endif

static int __init busfreq_mon_init(void)
{
	unsigned int i;
	unsigned int tmp;
	unsigned int val;
	struct clk *ppmu_clk = NULL;

	if (!soc_is_exynos4210())
		return -ENODEV;

	val = __raw_readl(S5P_VA_DMC0 + 0x4);
	val = (val >> 8) & 0xf;

	/* Check Memory Type Only support -> 0x5: 0xLPDDR2 */
	if (val != 0x05) {
		pr_err("[ %x ] Memory Type Undertermined.\n", val);
		return -ENODEV;
	}

	init_done = true;

	for (i = 0; i < DVFS_LOCK_ID_END; i++)
		g_busfreq_lock_val[i] = BUS_LEVEL_END - 1;

	g_busfreq_lock_level = BUS_LEVEL_END - 1;

	cpu.hw_base = S5P_VA_PPMU_CPU;
	cpu.weight = 1;
	cpu.event[3] = 0x7;

	dmc[DMC0].hw_base = S5P_VA_DMC0;
	dmc[DMC0].weight = 1;
	dmc[DMC1].hw_base = S5P_VA_DMC1;
	dmc[DMC1].weight = 1;

	p_idx = LV_0;
	up_threshold = UP_THRESHOLD_DEFAULT;

	tmp = __raw_readl(EXYNOS4_CLKDIV_DMC0);

	for (i = 0; i <  LV_END; i++) {
		tmp &= ~(EXYNOS4_CLKDIV_DMC0_ACP_MASK |
			EXYNOS4_CLKDIV_DMC0_ACPPCLK_MASK |
			EXYNOS4_CLKDIV_DMC0_DPHY_MASK |
			EXYNOS4_CLKDIV_DMC0_DMC_MASK |
			EXYNOS4_CLKDIV_DMC0_DMCD_MASK |
			EXYNOS4_CLKDIV_DMC0_DMCP_MASK |
			EXYNOS4_CLKDIV_DMC0_COPY2_MASK |
			EXYNOS4_CLKDIV_DMC0_CORETI_MASK);

		tmp |= ((clkdiv_dmc0[i][0] << EXYNOS4_CLKDIV_DMC0_ACP_SHIFT) |
			(clkdiv_dmc0[i][1] << EXYNOS4_CLKDIV_DMC0_ACPPCLK_SHIFT) |
			(clkdiv_dmc0[i][2] << EXYNOS4_CLKDIV_DMC0_DPHY_SHIFT) |
			(clkdiv_dmc0[i][3] << EXYNOS4_CLKDIV_DMC0_DMC_SHIFT) |
			(clkdiv_dmc0[i][4] << EXYNOS4_CLKDIV_DMC0_DMCD_SHIFT) |
			(clkdiv_dmc0[i][5] << EXYNOS4_CLKDIV_DMC0_DMCP_SHIFT) |
			(clkdiv_dmc0[i][6] << EXYNOS4_CLKDIV_DMC0_COPY2_SHIFT) |
			(clkdiv_dmc0[i][7] << EXYNOS4_CLKDIV_DMC0_CORETI_SHIFT));

		exynos4_busfreq_table[i].clk_dmcdiv = tmp;
	}

	tmp = __raw_readl(EXYNOS4_CLKDIV_TOP);

	for (i = 0; i <  LV_END; i++) {
		tmp &= ~(EXYNOS4_CLKDIV_TOP_ACLK200_MASK |
			EXYNOS4_CLKDIV_TOP_ACLK100_MASK |
			EXYNOS4_CLKDIV_TOP_ACLK160_MASK |
			EXYNOS4_CLKDIV_TOP_ACLK133_MASK |
			EXYNOS4_CLKDIV_TOP_ONENAND_MASK);

		tmp |= ((clkdiv_top[i][0] << EXYNOS4_CLKDIV_TOP_ACLK200_SHIFT) |
			(clkdiv_top[i][1] << EXYNOS4_CLKDIV_TOP_ACLK100_SHIFT) |
			(clkdiv_top[i][2] << EXYNOS4_CLKDIV_TOP_ACLK160_SHIFT) |
			(clkdiv_top[i][3] << EXYNOS4_CLKDIV_TOP_ACLK133_SHIFT) |
			(clkdiv_top[i][4] << EXYNOS4_CLKDIV_TOP_ONENAND_SHIFT));

		exynos4_busfreq_table[i].clk_topdiv = tmp;
	}

	exynos4_set_bus_volt();

	int_regulator = regulator_get(NULL, "vdd_int");
	if (IS_ERR(int_regulator)) {
		pr_err("failed to get resource %s\n", "vdd_int");
		return -ENODEV;
	}

	/* PPMUs using for cpufreq get clk from clk_list */
	ppmu_clk = clk_get(NULL, "ppmudmc0");
	if (IS_ERR(ppmu_clk)) {
		pr_err("Failed to get ppmudmc0 clock\n");
		goto err_clk;
	}
	clk_enable(ppmu_clk);
	clk_put(ppmu_clk);

	ppmu_clk = clk_get(NULL, "ppmudmc1");
	if (IS_ERR(ppmu_clk)) {
		pr_err("Failed to get ppmudmc1 clock\n");
		goto err_clk;
	}
	clk_enable(ppmu_clk);
	clk_put(ppmu_clk);

	ppmu_clk = clk_get(NULL, "ppmucpu");
	if (IS_ERR(ppmu_clk)) {
		pr_err("Failed to get ppmucpu clock\n");
		goto err_clk;
	}
	clk_enable(ppmu_clk);
	clk_put(ppmu_clk);

	busfreq_mon_reset();

	if (cpufreq_register_notifier(&exynos4_busfreq_notifier,
				CPUFREQ_TRANSITION_NOTIFIER)) {
		pr_err("Failed to setup cpufreq notifier\n");
		goto err_cpufreq;
	}

	if (register_pm_notifier(&exynos4_buspm_notifier)) {
		pr_err("Failed to setup buspm notifier\n");
		goto err_pm;
	}

	if (register_reboot_notifier(&exynos4_busfreq_reboot_notifier))
		pr_err("Failed to setup reboot notifier\n");

#ifdef SYSFS_DEBUG_BUSFREQ
	if (sysfs_create_file(cpufreq_global_kobject,
			&busfreq_level__lock_attr.attr))
		pr_err("Failed to create sysfs file(lock)\n");

	if (sysfs_create_file(cpufreq_global_kobject,
			&busfreq_time_in_state_attr.attr))
		pr_err("Failed to create sysfs file(time_in_state)\n");

	if (sysfs_create_file(cpufreq_global_kobject,
			&busfreq_up_threshold_attr.attr))
		pr_err("Failed to create sysfs file(up_threshold)\n");

	if (sysfs_create_file(cpufreq_global_kobject, &busfreq_level_attr.attr))
		pr_err("Failed to create sysfs file(level)\n");
#endif
#ifdef CONFIG_BUSFREQ_QOS
	pm_qos_add_notifier(PM_QOS_BUS_QOS, &exynos4_busqos_notifier);
#endif

	return 0;

err_pm:
	cpufreq_unregister_notifier(&exynos4_busfreq_notifier,
				CPUFREQ_TRANSITION_NOTIFIER);
err_cpufreq:
err_clk:
	if (!IS_ERR(int_regulator))
		regulator_put(int_regulator);

	return -ENODEV;
}
late_initcall(busfreq_mon_init);
