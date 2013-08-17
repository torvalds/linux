/* drivers/gpu/t6xx/kbase/src/platform/mali_kbase_dvfs.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T604 DVFS driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_dvfs.c
 * DVFS
 */

#include <osk/mali_osk.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_uku.h>
#include <kbase/src/common/mali_kbase_mem.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/linux/mali_kbase_mem_linux.h>
#include <kbase/mali_ukk.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <mach/map.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <mach/regs-clock.h>
#include <asm/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <kbase/src/platform/mali_kbase_platform.h>
#include <kbase/src/platform/mali_kbase_dvfs.h>
#include <kbase/src/common/mali_kbase_gator.h>
#ifdef CONFIG_EXYNOS5_CPUFREQ
#include <mach/cpufreq.h>
#endif
#include <mach/exynos5_bus.h>

#ifdef MALI_DVFS_ASV_ENABLE
#include <mach/asv-exynos.h>
#define ASV_STATUS_INIT 1
#define ASV_STATUS_NOT_INIT 0
#define ASV_STATUS_DISABLE_REQ 2

#define ASV_CMD_DISABLE	-1
#define ASV_CMD_ENABLE 0
#endif

#ifdef CONFIG_REGULATOR
static struct regulator *g3d_regulator=NULL;
#endif

static struct exynos5_bus_mif_handle *mem_freq_req;

/***********************************************************/
/*  This table and variable are using the check time share of GPU Clock  */
/***********************************************************/

typedef struct _mali_dvfs_info{
	unsigned int voltage;
	unsigned int clock;
	int min_threshold;
	int	max_threshold;
	unsigned long long time;
	int mem_freq;
}mali_dvfs_info;

static mali_dvfs_info mali_dvfs_infotbl[] = {
	{925000, 100, 0, 70, 0, 100000},
	{925000, 160, 50, 65, 0, 160000},
	{1025000, 266, 60, 78, 0, 400000},
	{1075000, 350, 70, 80, 0, 400000},
	{1125000, 400, 70, 80, 0, 667000},
	{1150000, 450, 76, 99, 0, 800000},
	{1200000, 533, 99, 100, 0, 800000},
};

#define MALI_DVFS_STEP	ARRAY_SIZE(mali_dvfs_infotbl)

#ifdef CONFIG_MALI_T6XX_DVFS
typedef struct _mali_dvfs_status_type{
	kbase_device *kbdev;
	int step;
	int utilisation;
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	int upper_lock;
	int under_lock;
#endif
#ifdef MALI_DVFS_ASV_ENABLE
	int asv_status;
#endif
}mali_dvfs_status;

static struct workqueue_struct *mali_dvfs_wq = 0;
spinlock_t mali_dvfs_spinlock;
struct mutex mali_set_clock_lock;
struct mutex mali_enable_clock_lock;
static int kbase_platform_dvfs_get_bw(int level);
#ifdef CONFIG_MALI_T6XX_DEBUG_SYS
static void update_time_in_state(int level);
#endif

/*dvfs status*/
static mali_dvfs_status mali_dvfs_status_current;
#ifdef MALI_DVFS_ASV_ENABLE
static const unsigned int mali_dvfs_vol_default[]=
	{ 925000, 925000, 1025000, 1075000, 1125000, 1150000, 1200000};


static int mali_dvfs_update_asv(int cmd)
{
	int i;
	int voltage = 0;

	if (cmd == ASV_CMD_DISABLE) {
		for (i=0; i<MALI_DVFS_STEP; i++)
		{
			mali_dvfs_infotbl[i].voltage = mali_dvfs_vol_default[i];
		}
		printk("mali_dvfs_update_asv use default table\n");
		return ASV_STATUS_INIT;
	}
	for (i=0; i<MALI_DVFS_STEP; i++) {
		voltage = asv_get_volt(ID_G3D, mali_dvfs_infotbl[i].clock*1000);
		if (voltage == 0) {
			return ASV_STATUS_NOT_INIT;
		}
		mali_dvfs_infotbl[i].voltage = voltage;
	}

	return ASV_STATUS_INIT;
}
#endif

static void mali_dvfs_event_proc(struct work_struct *w)
{
	unsigned long flags;
	mali_dvfs_status *dvfs_status;
	struct exynos_context *platform;

	mutex_lock(&mali_enable_clock_lock);
	dvfs_status = &mali_dvfs_status_current;

	if (!kbase_platform_dvfs_get_enable_status()) {
		mutex_unlock(&mali_enable_clock_lock);
		return;
	}

	platform = (struct exynos_context *)dvfs_status->kbdev->platform_context;
#ifdef MALI_DVFS_ASV_ENABLE
	if (dvfs_status->asv_status==ASV_STATUS_DISABLE_REQ) {
		dvfs_status->asv_status=mali_dvfs_update_asv(ASV_CMD_DISABLE);
	} else if (dvfs_status->asv_status==ASV_STATUS_NOT_INIT) {
		dvfs_status->asv_status=mali_dvfs_update_asv(ASV_CMD_ENABLE);
	}
#endif
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (dvfs_status->utilisation > mali_dvfs_infotbl[dvfs_status->step].max_threshold) {
		if (dvfs_status->step==kbase_platform_dvfs_get_level(450)) {
			if (platform->utilisation > mali_dvfs_infotbl[dvfs_status->step].max_threshold)
				dvfs_status->step++;
			OSK_ASSERT(dvfs_status->step < MALI_DVFS_STEP);
		} else {
			dvfs_status->step++;
			OSK_ASSERT(dvfs_status->step < MALI_DVFS_STEP);
		}
	}else if ((dvfs_status->step>0) &&
			(platform->time_tick == MALI_DVFS_TIME_INTERVAL) &&
			(platform->utilisation < mali_dvfs_infotbl[dvfs_status->step].min_threshold)) {
		OSK_ASSERT(dvfs_status->step > 0);
		dvfs_status->step--;
	}
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	if ((dvfs_status->upper_lock >= 0)&&(dvfs_status->step > dvfs_status->upper_lock)) {
		dvfs_status->step = dvfs_status->upper_lock;
	}
	if (dvfs_status->under_lock > 0) {
		if (dvfs_status->step < dvfs_status->under_lock)
			dvfs_status->step = dvfs_status->under_lock;
	}
#endif
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	kbase_platform_dvfs_set_level(dvfs_status->kbdev, dvfs_status->step);

	mutex_unlock(&mali_enable_clock_lock);
}

static DECLARE_WORK(mali_dvfs_work, mali_dvfs_event_proc);

int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation)
{
	unsigned long flags;
	struct exynos_context *platform;

	OSK_ASSERT(kbdev != NULL);
	platform = (struct exynos_context *) kbdev->platform_context;

	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (platform->time_tick < MALI_DVFS_TIME_INTERVAL) {
		platform->time_tick++;
		platform->time_busy += kbdev->pm.metrics.time_busy;
		platform->time_idle += kbdev->pm.metrics.time_idle;
	} else {
		platform->time_busy = kbdev->pm.metrics.time_busy;
		platform->time_idle = kbdev->pm.metrics.time_idle;
		platform->time_tick = 0;
	}

	if ((platform->time_tick == MALI_DVFS_TIME_INTERVAL) &&
		(platform->time_idle + platform->time_busy > 0))
			platform->utilisation = (100*platform->time_busy) / (platform->time_idle + platform->time_busy);

	mali_dvfs_status_current.utilisation = utilisation;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	queue_work_on(0, mali_dvfs_wq, &mali_dvfs_work);
	/*add error handle here*/
	return MALI_TRUE;
}

int kbase_platform_dvfs_get_utilisation(void)
{
	unsigned long flags;
	int utilisation = 0;

	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	utilisation = mali_dvfs_status_current.utilisation;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	return utilisation;
}

int kbase_platform_dvfs_get_enable_status(void)
{
	struct kbase_device *kbdev;
	unsigned long flags;
	int enable;

	kbdev = mali_dvfs_status_current.kbdev;
	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
	enable = kbdev->pm.metrics.timer_active;
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);

	return enable;
}

int kbase_platform_dvfs_enable(bool enable, int freq)
{
	mali_dvfs_status *dvfs_status;
	struct kbase_device *kbdev;
	unsigned long flags;
	struct exynos_context *platform;
	int f;

	dvfs_status = &mali_dvfs_status_current;
	kbdev = mali_dvfs_status_current.kbdev;

	OSK_ASSERT(kbdev != NULL);
	platform = (struct exynos_context *)kbdev->platform_context;

	mutex_lock(&mali_enable_clock_lock);

	if (freq != MALI_DVFS_CURRENT_FREQ) {
		spin_lock_irqsave(&mali_dvfs_spinlock, flags);
		platform->time_tick = 0;
		platform->time_busy = 0;
		platform->time_idle = 0;
		platform->utilisation = 0;
		dvfs_status->step = kbase_platform_dvfs_get_level(freq);
		spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

		kbase_platform_dvfs_set_level(dvfs_status->kbdev, dvfs_status->step);
	}

	if (enable != kbdev->pm.metrics.timer_active) {
		if (enable) {
			spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
			kbdev->pm.metrics.timer_active = MALI_TRUE;
			spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
			hrtimer_start(&kbdev->pm.metrics.timer,
					HR_TIMER_DELAY_MSEC(KBASE_PM_DVFS_FREQUENCY),
					HRTIMER_MODE_REL);
			f = mali_dvfs_infotbl[dvfs_status->step].mem_freq;
			exynos5_bus_mif_update(mem_freq_req, f);
		} else {
			spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
			kbdev->pm.metrics.timer_active = MALI_FALSE;
			spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
			hrtimer_cancel(&kbdev->pm.metrics.timer);
			exynos5_bus_mif_update(mem_freq_req, 0);
		}
	}
	mutex_unlock(&mali_enable_clock_lock);

	return MALI_TRUE;
}

int kbase_platform_dvfs_init(struct kbase_device *kbdev)
{
	unsigned long flags;
	/*default status
	  add here with the right function to get initilization value.
	 */
	if (!mali_dvfs_wq)
		mali_dvfs_wq = create_singlethread_workqueue("mali_dvfs");

	spin_lock_init(&mali_dvfs_spinlock);
	mutex_init(&mali_set_clock_lock);
	mutex_init(&mali_enable_clock_lock);

	mem_freq_req = exynos5_bus_mif_min(0);

	/*add a error handling here*/
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	mali_dvfs_status_current.kbdev = kbdev;
	mali_dvfs_status_current.utilisation = 100;
	mali_dvfs_status_current.step = MALI_DVFS_STEP-1;
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	mali_dvfs_status_current.upper_lock = -1;
	mali_dvfs_status_current.under_lock = -1;
#endif
#ifdef MALI_DVFS_ASV_ENABLE
	mali_dvfs_status_current.asv_status=ASV_STATUS_NOT_INIT;
#endif
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	return MALI_TRUE;
}

void kbase_platform_dvfs_term(void)
{
	if (mali_dvfs_wq)
		destroy_workqueue(mali_dvfs_wq);

	mali_dvfs_wq = NULL;
}
#endif /*CONFIG_MALI_T6XX_DVFS*/

static int kbase_platform_dvfs_get_bw(int level)
{
	int bw;

	if (level == 0)
		return -1;

	bw = mali_dvfs_infotbl[level].clock * 16;
	return clamp(bw, 0, 6400);
}

int mali_get_dvfs_upper_locked_freq(void)
{
	unsigned long flags;
	unsigned int locked_level = -1;

#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	locked_level = mali_dvfs_infotbl[mali_dvfs_status_current.upper_lock].clock;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	return locked_level;
}

int mali_get_dvfs_under_locked_freq(void)
{
	unsigned long flags;
	unsigned int locked_level = -1;

#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	locked_level = mali_dvfs_infotbl[mali_dvfs_status_current.under_lock].clock;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	return locked_level;
}

int mali_get_dvfs_current_level(void)
{
	unsigned long flags;
	unsigned int current_level = -1;

#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	current_level = mali_dvfs_status_current.step;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	return current_level;
}

int mali_dvfs_freq_lock(int level)
{
	unsigned long flags;
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (mali_dvfs_status_current.under_lock >= 0) {
		printk( KERN_ERR "[G3D] Upper lock Error : Under lock is already set\n");
		spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
		return -1;
	}
	mali_dvfs_status_current.upper_lock = level;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	printk( "[G3D] Upper Lock Set : %d\n", level );
#endif
	return 0;
}
void mali_dvfs_freq_unlock(void)
{
	unsigned long flags;
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	mali_dvfs_status_current.upper_lock = -1;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif

	printk("[G3D] Upper Lock Unset\n");
}

int mali_dvfs_freq_under_lock(int level)
{
	unsigned long flags;
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (mali_dvfs_status_current.upper_lock >= 0) {
		printk( KERN_ERR "[G3D] Under lock Error : Upper lock is already set\n");
		spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
		return -1;
	}
	mali_dvfs_status_current.under_lock = level;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	printk( "[G3D] Under Lock Set : %d\n", level );
#endif
	return 0;
}
void mali_dvfs_freq_under_unlock(void)
{
	unsigned long flags;
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	mali_dvfs_status_current.under_lock = -1;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	printk("[G3D] Under Lock Unset\n");
}

int kbase_platform_regulator_init(void)
{

#ifdef CONFIG_REGULATOR
	int mali_gpu_vol = 0;
	g3d_regulator = regulator_get(NULL, "vdd_g3d");
	if (IS_ERR(g3d_regulator)) {
		printk("[kbase_platform_regulator_init] failed to get mali t6xx regulator\n");
		return -1;
	}

	if (regulator_enable(g3d_regulator) != 0) {
		printk("[kbase_platform_regulator_init] failed to enable mali t6xx regulator\n");
		return -1;
	}
#ifdef MALI_DVFS_ASV_ENABLE
	mali_gpu_vol = asv_get_volt(ID_G3D, MALI_DVFS_BL_CONFIG_FREQ*1000);
#endif
	if (mali_gpu_vol == 0)
		mali_gpu_vol = mali_dvfs_infotbl[ARRAY_SIZE(mali_dvfs_infotbl)-1].voltage;

	if (regulator_set_voltage(g3d_regulator, mali_gpu_vol, mali_gpu_vol) != 0) {
		printk("[kbase_platform_regulator_init] failed to set mali t6xx operating voltage [%d]\n", mali_gpu_vol);
		return -1;
	}
#endif
	return 0;
}

int kbase_platform_regulator_disable(void)
{
#ifdef CONFIG_REGULATOR
	if (!g3d_regulator) {
		printk("[kbase_platform_regulator_disable] g3d_regulator is not initialized\n");
		return -1;
	}

	if (regulator_disable(g3d_regulator) != 0) {
		printk("[kbase_platform_regulator_disable] failed to disable g3d regulator\n");
		return -1;
	}
#endif
	return 0;
}

int kbase_platform_regulator_enable(void)
{
#ifdef CONFIG_REGULATOR
	if (!g3d_regulator) {
		printk("[kbase_platform_regulator_enable] g3d_regulator is not initialized\n");
		return -1;
	}

	if (regulator_enable(g3d_regulator) != 0) {
		printk("[kbase_platform_regulator_enable] failed to enable g3d regulator\n");
		return -1;
	}
#endif
	return 0;
}

int kbase_platform_get_voltage(struct device *dev, int *vol)
{
#ifdef CONFIG_REGULATOR
	if (!g3d_regulator) {
		printk("[kbase_platform_get_voltage] g3d_regulator is not initialized\n");
		return -1;
	}

	*vol = regulator_get_voltage(g3d_regulator);
#else
	*vol = 0;
#endif
	return 0;
}

int kbase_platform_set_voltage(struct device *dev, int vol)
{
#ifdef CONFIG_REGULATOR
	if (!g3d_regulator) {
		printk("[kbase_platform_set_voltage] g3d_regulator is not initialized\n");
		return -1;
	}

	if (regulator_set_voltage(g3d_regulator, vol, vol) != 0)
	{
		printk("[kbase_platform_set_voltage] failed to set voltage\n");
		return -1;
	}
#endif
	return 0;
}

void kbase_platform_dvfs_set_clock(kbase_device *kbdev, int freq)
{
	static struct clk * mout_gpll = NULL;
	static struct clk * fin_gpll = NULL;
	static struct clk * fout_gpll = NULL;
	static int _freq = -1;
	static unsigned long gpll_rate_prev = 0;
	unsigned long gpll_rate = 0, aclk_400_rate = 0;
	unsigned long tmp = 0;
	struct exynos_context *platform;

	if (!kbdev)
		panic("oops");

	platform = (struct exynos_context *) kbdev->platform_context;
	if (NULL == platform)
	{
		panic("oops");
	}

	if (mout_gpll==NULL) {
		mout_gpll = clk_get(kbdev->osdev.dev, "mout_gpll");
		fin_gpll = clk_get(kbdev->osdev.dev, "ext_xtal");
		fout_gpll = clk_get(kbdev->osdev.dev, "fout_gpll");
		if (IS_ERR(mout_gpll) || IS_ERR(fin_gpll) || IS_ERR(fout_gpll))
			panic("clk_get ERROR");
	}

	if (platform->sclk_g3d == 0)
		return;

	if (freq == _freq)
		return;

	switch(freq) {
		case 533:
			gpll_rate = 533000000;
			aclk_400_rate = 533000000;
			break;
		case 450:
			gpll_rate = 450000000;
			aclk_400_rate = 450000000;
			break;
		case 400:
			gpll_rate = 800000000;
			aclk_400_rate = 400000000;
			break;
		case 350:
			gpll_rate = 1400000000;
			aclk_400_rate = 350000000;
			break;
		case 266:
			gpll_rate = 800000000;
			aclk_400_rate = 267000000;
			break;
		case 160:
			gpll_rate = 800000000;
			aclk_400_rate = 160000000;
			break;
		case 100:
			gpll_rate = 800000000;
			aclk_400_rate = 100000000;
			break;
		default:
			return;
	}

	/* if changed the GPLL rate, set rate for GPLL and wait for lock time */
	if (gpll_rate != gpll_rate_prev) {
		/*for stable clock input.*/
		clk_set_rate(platform->sclk_g3d, 100000000);
		clk_set_parent(mout_gpll, fin_gpll);

		/*change gpll*/
		clk_set_rate( fout_gpll, gpll_rate );

		/*restore parent*/
		clk_set_parent(mout_gpll, fout_gpll);
		gpll_rate_prev = gpll_rate;
	}

	_freq = freq;
	clk_set_rate(platform->sclk_g3d, aclk_400_rate);

	/* Waiting for clock is stable */
	do {
		tmp = __raw_readl(EXYNOS5_CLKDIV_STAT_TOP0);
	} while (tmp & 0x1000000);

	return;
}

static void kbase_platform_dvfs_set_vol(unsigned int vol)
{
	static int _vol = -1;

	if (_vol == vol)
		return;

	kbase_platform_set_voltage(NULL, vol);
	_vol = vol;

	return;
}

int kbase_platform_dvfs_get_level(int freq)
{
	int i;
	for (i=0; i < MALI_DVFS_STEP; i++) {
		if (mali_dvfs_infotbl[i].clock == freq)
			return i;
	}

	return -1;
}

void kbase_platform_dvfs_set_level(kbase_device *kbdev, int level)
{
	static int prev_level = -1;
	int f;

	if (level == prev_level)
		return;

	if (WARN_ON((level >= MALI_DVFS_STEP)||(level < 0)))
		panic("invalid level");

#ifdef CONFIG_MALI_T6XX_DVFS
	mutex_lock(&mali_set_clock_lock);
#endif

	f = mali_dvfs_infotbl[level].mem_freq;

	if (level > prev_level) {
		exynos5_bus_mif_update(mem_freq_req, f);
		kbase_platform_dvfs_set_vol(mali_dvfs_infotbl[level].voltage);
		kbase_platform_dvfs_set_clock(kbdev, mali_dvfs_infotbl[level].clock);
	} else {
		kbase_platform_dvfs_set_clock(kbdev, mali_dvfs_infotbl[level].clock);
		kbase_platform_dvfs_set_vol(mali_dvfs_infotbl[level].voltage);
		exynos5_bus_mif_update(mem_freq_req, f);
	}
#if defined(CONFIG_MALI_T6XX_DEBUG_SYS) && defined(CONFIG_MALI_T6XX_DVFS)
	update_time_in_state(prev_level);
#endif
	prev_level = level;
#ifdef CONFIG_MALI_T6XX_DVFS
	mutex_unlock(&mali_set_clock_lock);
#endif
}

int kbase_platform_dvfs_sprint_avs_table(char *buf)
{
#ifdef MALI_DVFS_ASV_ENABLE
	int i, cnt=0;
	if (buf==NULL)
		return 0;

	for (i=MALI_DVFS_STEP-1; i >= 0; i--) {
		cnt+=sprintf(buf+cnt,"%dMhz:%d\n",
				mali_dvfs_infotbl[i].clock, mali_dvfs_infotbl[i].voltage);
	}
	return cnt;
#else
	return 0;
#endif
}

int kbase_platform_dvfs_set(int enable)
{
	unsigned long flags;
#ifdef MALI_DVFS_ASV_ENABLE
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (enable) {
		mali_dvfs_status_current.asv_status=ASV_STATUS_NOT_INIT;
	} else {
		mali_dvfs_status_current.asv_status=ASV_STATUS_DISABLE_REQ;
	}
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	return 0;
}

#ifdef CONFIG_MALI_T6XX_DEBUG_SYS
#ifdef CONFIG_MALI_T6XX_DVFS
static void update_time_in_state(int level)
{
	u64 current_time;
	static u64 prev_time=0;

	if (!kbase_platform_dvfs_get_enable_status())
		return;

	if (prev_time ==0)
		prev_time=get_jiffies_64();

	current_time = get_jiffies_64();
	mali_dvfs_infotbl[level].time += current_time-prev_time;
	prev_time = current_time;
}
#endif

ssize_t show_time_in_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int i;

	kbdev = dev_get_drvdata(dev);

#ifdef CONFIG_MALI_T6XX_DVFS
	update_time_in_state(mali_dvfs_status_current.step);
#endif
	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < MALI_DVFS_STEP; i++) {
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "%d %llu\n",
				mali_dvfs_infotbl[i].clock,
				mali_dvfs_infotbl[i].time);
	}

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "\n");
	} else {
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
	}

	return ret;
}

ssize_t set_time_in_state(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i;

	for (i = 0; i < MALI_DVFS_STEP; i++) {
		mali_dvfs_infotbl[i].time = 0;
	}

	return count;
}
#endif
