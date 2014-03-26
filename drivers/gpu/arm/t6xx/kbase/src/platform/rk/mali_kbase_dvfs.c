/* drivers/gpu/t6xx/kbase/src/platform/rk/mali_kbase_dvfs.c
 *
 * Rockchip SoC Mali-T764 DVFS driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_dvfs.c
 * DVFS
 */

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_uku.h>
#include <kbase/src/common/mali_kbase_mem.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/linux/mali_kbase_mem_linux.h>

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

#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <kbase/src/platform/rk/mali_kbase_platform.h>
#include <kbase/src/platform/rk/mali_kbase_dvfs.h>
#include <kbase/src/common/mali_kbase_gator.h>

/***********************************************************/
/*  This table and variable are using the check time share of GPU Clock  */
/***********************************************************/

typedef struct _mali_dvfs_info {
	unsigned int voltage;
	unsigned int clock;
	int min_threshold;
	int max_threshold;
	unsigned long long time;
} mali_dvfs_info;

static mali_dvfs_info mali_dvfs_infotbl[] = {
	  {925000, 100, 0, 70, 0},
      {925000, 160, 50, 65, 0},
      {1025000, 266, 60, 78, 0},
      {1075000, 350, 65, 75, 0},
      {1125000, 400, 70, 75, 0},
      {1200000, 600, 90, 100, 0},
};

#define MALI_DVFS_STEP	ARRAY_SIZE(mali_dvfs_infotbl)

#ifdef CONFIG_MALI_T6XX_DVFS
typedef struct _mali_dvfs_status_type {
	kbase_device *kbdev;
	int step;
	int utilisation;
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	int upper_lock;
	int under_lock;
#endif

} mali_dvfs_status;

static struct workqueue_struct *mali_dvfs_wq = 0;
spinlock_t mali_dvfs_spinlock;
struct mutex mali_set_clock_lock;
struct mutex mali_enable_clock_lock;

#ifdef CONFIG_MALI_T6XX_DEBUG_SYS
static void update_time_in_state(int level);
#endif

/*dvfs status*/
static mali_dvfs_status mali_dvfs_status_current;

static void mali_dvfs_event_proc(struct work_struct *w)
{
	unsigned long flags;
	mali_dvfs_status *dvfs_status;
	struct rk_context *platform;

	mutex_lock(&mali_enable_clock_lock);
	dvfs_status = &mali_dvfs_status_current;

	if (!kbase_platform_dvfs_get_enable_status()) {
		mutex_unlock(&mali_enable_clock_lock);
		return;
	}

	platform = (struct rk_context *)dvfs_status->kbdev->platform_context;
	
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (dvfs_status->utilisation > mali_dvfs_infotbl[dvfs_status->step].max_threshold) 
	{
	#if 0
		if (dvfs_status->step==kbase_platform_dvfs_get_level(450)) 
		{
			if (platform->utilisation > mali_dvfs_infotbl[dvfs_status->step].max_threshold)
				dvfs_status->step++;
			BUG_ON(dvfs_status->step >= MALI_DVFS_STEP);
		} 
		else 
		{
			dvfs_status->step++;
			BUG_ON(dvfs_status->step >= MALI_DVFS_STEP);
		}
	#endif
		dvfs_status->step++;
		BUG_ON(dvfs_status->step >= MALI_DVFS_STEP);

	} 
	else if((dvfs_status->step > 0) && (dvfs_status->utilisation < mali_dvfs_infotbl[dvfs_status->step].min_threshold)) 
	//else if((dvfs_status->step > 0) && (platform->time_tick == MALI_DVFS_TIME_INTERVAL) && (platform->utilisation < mali_dvfs_infotbl[dvfs_status->step].min_threshold)) 
	{
		BUG_ON(dvfs_status->step <= 0);
		dvfs_status->step--;
	}
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	if ((dvfs_status->upper_lock >= 0) && (dvfs_status->step > dvfs_status->upper_lock)) 
	{
		dvfs_status->step = dvfs_status->upper_lock;
	}

	if (dvfs_status->under_lock > 0) 
	{
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
	struct rk_context *platform;

	BUG_ON(!kbdev);
	platform = (struct rk_context *)kbdev->platform_context;

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

	if ((platform->time_tick == MALI_DVFS_TIME_INTERVAL) && (platform->time_idle + platform->time_busy > 0))
		platform->utilisation = (100 * platform->time_busy) / (platform->time_idle + platform->time_busy);

	mali_dvfs_status_current.utilisation = utilisation;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	queue_work_on(0, mali_dvfs_wq, &mali_dvfs_work);
	/*add error handle here */
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
	struct rk_context *platform;

	dvfs_status = &mali_dvfs_status_current;
	kbdev = mali_dvfs_status_current.kbdev;

	BUG_ON(kbdev == NULL);
	platform = (struct rk_context *)kbdev->platform_context;

	mutex_lock(&mali_enable_clock_lock);

	if (enable != kbdev->pm.metrics.timer_active) {
		if (enable) {
			spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
			kbdev->pm.metrics.timer_active = MALI_TRUE;
			spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
			hrtimer_start(&kbdev->pm.metrics.timer,
					HR_TIMER_DELAY_MSEC(KBASE_PM_DVFS_FREQUENCY),
					HRTIMER_MODE_REL);
		} else {
			spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
			kbdev->pm.metrics.timer_active = MALI_FALSE;
			spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
			hrtimer_cancel(&kbdev->pm.metrics.timer);
		}
	}

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

	/*add a error handling here */
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	mali_dvfs_status_current.kbdev = kbdev;
	mali_dvfs_status_current.utilisation = 100;
	mali_dvfs_status_current.step = MALI_DVFS_STEP - 1;
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	mali_dvfs_status_current.upper_lock = -1;
	mali_dvfs_status_current.under_lock = -1;
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

int mali_get_dvfs_upper_locked_freq(void)
{
	unsigned long flags;
	int locked_level = -1;

#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (mali_dvfs_status_current.upper_lock >= 0)
		locked_level = mali_dvfs_infotbl[mali_dvfs_status_current.upper_lock].clock;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	return locked_level;
}

int mali_get_dvfs_under_locked_freq(void)
{
	unsigned long flags;
	int locked_level = -1;

#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (mali_dvfs_status_current.under_lock >= 0)
		locked_level = mali_dvfs_infotbl[mali_dvfs_status_current.under_lock].clock;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	return locked_level;
}

int mali_get_dvfs_current_level(void)
{
	unsigned long flags;
	int current_level = -1;

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
	if (mali_dvfs_status_current.under_lock >= 0 && mali_dvfs_status_current.under_lock > level) {
		printk(KERN_ERR " Upper lock Error : Attempting to set upper lock to below under lock\n");
		spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
		return -1;
	}
	mali_dvfs_status_current.upper_lock = level;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	printk(KERN_DEBUG " Upper Lock Set : %d\n", level);
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
	printk(KERN_DEBUG "mali Upper Lock Unset\n");
}

int mali_dvfs_freq_under_lock(int level)
{
	unsigned long flags;
#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (mali_dvfs_status_current.upper_lock >= 0 && mali_dvfs_status_current.upper_lock < level) {
		printk(KERN_ERR "mali Under lock Error : Attempting to set under lock to above upper lock\n");
		spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
		return -1;
	}
	mali_dvfs_status_current.under_lock = level;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	printk(KERN_DEBUG "mali Under Lock Set : %d\n", level);
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
	printk(KERN_DEBUG " mali clock Under Lock Unset\n");
}

void kbase_platform_dvfs_set_clock(kbase_device *kbdev, int freq)
{
	unsigned long aclk_400_rate = 0;
	struct rk_context *platform;

	if (!kbdev)
		panic("oops");

	platform = (struct rk_context *)kbdev->platform_context;
	if (NULL == platform)
		panic("oops");

	if (!platform->mali_clk_node) 
	{
		printk("mali_clk_node not init\n");
		return;
	}
	switch (freq) {
		case 600:
			aclk_400_rate = 600000000;
			break;
		case 400:
			aclk_400_rate = 400000000;
			break;
		case 350:
			aclk_400_rate = 350000000;
			break;
		case 266:
			aclk_400_rate = 266000000;
			break;
		case 160:
			aclk_400_rate = 160000000;
			break;
		case 100:
			aclk_400_rate = 100000000;
			break;
		default:
			return;
	}

	mali_dvfs_clk_set(platform->mali_clk_node,aclk_400_rate);
	/* Waiting for clock is stable
	do {
		tmp = __raw_readl(EXYNOS5_CLKDIV_STAT_TOP0);
	} while (tmp & 0x1000000);
	*/

	return;
}


int kbase_platform_dvfs_get_level(int freq)
{
	int i;
	for (i = 0; i < MALI_DVFS_STEP; i++) {
		if (mali_dvfs_infotbl[i].clock == freq)
			return i;
	}
	return -1;
}
void kbase_platform_dvfs_set_level(kbase_device *kbdev, int level)
{
	static int prev_level = -1;

	if (level == prev_level)
		return;

	if (WARN_ON((level >= MALI_DVFS_STEP) || (level < 0)))
		panic("invalid level");

#ifdef CONFIG_MALI_T6XX_FREQ_LOCK
	if (mali_dvfs_status_current.upper_lock >= 0 && level > mali_dvfs_status_current.upper_lock)
		level = mali_dvfs_status_current.upper_lock;

	if (mali_dvfs_status_current.under_lock >= 0 && level < mali_dvfs_status_current.under_lock)
		level = mali_dvfs_status_current.under_lock;
#endif

#ifdef CONFIG_MALI_T6XX_DVFS
	mutex_lock(&mali_set_clock_lock);
#endif

	kbase_platform_dvfs_set_clock(kbdev, mali_dvfs_infotbl[level].clock);

#if defined(CONFIG_MALI_T6XX_DEBUG_SYS) && defined(CONFIG_MALI_T6XX_DVFS)
	update_time_in_state(prev_level);
#endif
	prev_level = level;
#ifdef CONFIG_MALI_T6XX_DVFS
	mutex_unlock(&mali_set_clock_lock);
#endif
}

#ifdef CONFIG_MALI_T6XX_DEBUG_SYS
#ifdef CONFIG_MALI_T6XX_DVFS
static void update_time_in_state(int level)
{
	u64 current_time;
	static u64 prev_time=0;

	if (level < 0)
		return;

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

	for (i = 0; i < MALI_DVFS_STEP; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d %llu\n", mali_dvfs_infotbl[i].clock, mali_dvfs_infotbl[i].time);

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

ssize_t set_time_in_state(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i;

	for (i = 0; i < MALI_DVFS_STEP; i++)
		mali_dvfs_infotbl[i].time = 0;

	printk(KERN_DEBUG "time_in_state value is reset complete.\n");
	return count;
}
#endif
