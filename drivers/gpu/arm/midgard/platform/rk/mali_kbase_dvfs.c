/* drivers/gpu/t6xx/kbase/src/platform/manta/mali_kbase_dvfs.c
  * 
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

#include <mali_kbase.h>
#include <mali_kbase_uku.h>
#include <mali_kbase_mem.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_mem_linux.h>

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
#include <linux/cpufreq.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/rk_fb.h>
#include <linux/input.h>
#include <linux/rockchip/common.h>

#include <platform/rk/mali_kbase_platform.h>
#include <platform/rk/mali_kbase_dvfs.h>
#include <mali_kbase_gator.h>
#include <linux/rockchip/dvfs.h>
/***********************************************************/
/*  This table and variable are using the check time share of GPU Clock  */
/***********************************************************/
extern int rockchip_tsadc_get_temp(int chn);
#define gpu_temp_limit 110
#define gpu_temp_statis_time 1
#define level0_min 0
#define level0_max 70
#define levelf_max 100
static u32 div_dvfs = 0 ;

static mali_dvfs_info mali_dvfs_infotbl[] = {
	  {925000, 100000, 0, 70, 0},
      {925000, 160000, 50, 65, 0},
      {1025000, 266000, 60, 78, 0},
      {1075000, 350000, 65, 75, 0},
      {1125000, 400000, 70, 75, 0},
      {1200000, 500000, 90, 100, 0},
};
mali_dvfs_info *p_mali_dvfs_infotbl = NULL;

unsigned int MALI_DVFS_STEP = ARRAY_SIZE(mali_dvfs_infotbl);

static struct cpufreq_frequency_table *mali_freq_table = NULL;
#ifdef CONFIG_MALI_MIDGARD_DVFS
typedef struct _mali_dvfs_status_type {
	kbase_device *kbdev;
	int step;
	int utilisation;
	u32 temperature;
	u32 temperature_time;
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	int upper_lock;
	int under_lock;
#endif

} mali_dvfs_status;

static struct workqueue_struct *mali_dvfs_wq = 0;
spinlock_t mali_dvfs_spinlock;
struct mutex mali_set_clock_lock;
struct mutex mali_enable_clock_lock;

#ifdef CONFIG_MALI_MIDGARD_DEBUG_SYS
static void update_time_in_state(int level);
#endif
/*dvfs status*/
static mali_dvfs_status mali_dvfs_status_current;

#define LIMIT_FPS 60
#define LIMIT_FPS_POWER_SAVE 50

#ifdef CONFIG_MALI_MIDGARD_DVFS
static void gpufreq_input_event(struct input_handle *handle, unsigned int type,
										unsigned int code, int value)
{
	mali_dvfs_status *dvfs_status;
	struct rk_context *platform;
	unsigned long flags;
	
	if (type != EV_ABS)
		return;
	
	dvfs_status = &mali_dvfs_status_current;
	platform = (struct rk_context *)dvfs_status->kbdev->platform_context;
	
	spin_lock_irqsave(&platform->gpu_in_touch_lock, flags);
	platform->gpu_in_touch = true;
	spin_unlock_irqrestore(&platform->gpu_in_touch_lock, flags);
}

static int gpufreq_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "gpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;
	pr_info("%s\n",__func__);
	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void gpufreq_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
	pr_info("%s\n",__func__);
}

static const struct input_device_id gpufreq_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	{ },
};

static struct input_handler gpufreq_input_handler = {
	.event		= gpufreq_input_event,
	.connect	= gpufreq_input_connect,
	.disconnect	= gpufreq_input_disconnect,
	.name		= "gpufreq",
	.id_table	= gpufreq_ids,
};
#endif

static void mali_dvfs_event_proc(struct work_struct *w)
{
	unsigned long flags;
	mali_dvfs_status *dvfs_status;
	static int level_down_time = 0;
	static int level_up_time = 0;
	static u32 temp_tmp;
	struct rk_context *platform;
	u32 fps=0;
	u32 fps_limit;
	u32 policy;
	mutex_lock(&mali_enable_clock_lock);
	dvfs_status = &mali_dvfs_status_current;

	if (!kbase_platform_dvfs_get_enable_status()) {
		mutex_unlock(&mali_enable_clock_lock);
		return;
	}
	platform = (struct rk_context *)dvfs_status->kbdev->platform_context;
	
	fps = rk_get_real_fps(0);

	dvfs_status->temperature_time++;
	
	temp_tmp += rockchip_tsadc_get_temp(1);
	
	if(dvfs_status->temperature_time >= gpu_temp_statis_time) {
		dvfs_status->temperature_time = 0;
		dvfs_status->temperature = temp_tmp / gpu_temp_statis_time;
		temp_tmp = 0;
	}

	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	/*
	policy = rockchip_pm_get_policy();
	*/
	policy = ROCKCHIP_PM_POLICY_NORMAL;
	
	if (ROCKCHIP_PM_POLICY_PERFORMANCE == policy) {
		dvfs_status->step = MALI_DVFS_STEP - 1;
	} else {
		fps_limit = (ROCKCHIP_PM_POLICY_NORMAL == policy)?LIMIT_FPS : LIMIT_FPS_POWER_SAVE;
		/*
		printk("policy : %d , fps_limit = %d\n",policy,fps_limit);
		*/
		
		/*give priority to temperature unless in performance mode */
		if (dvfs_status->temperature > gpu_temp_limit) {
			if(dvfs_status->step > 0)
				dvfs_status->step--;
			
			if(gpu_temp_statis_time > 1)
				dvfs_status->temperature = 0;
			/*
			pr_info("decrease step for temperature over %d,next clock = %d\n",
					gpu_temp_limit, mali_dvfs_infotbl[dvfs_status->step].clock);
			*/
		} else if ((dvfs_status->utilisation > mali_dvfs_infotbl[dvfs_status->step].max_threshold) &&
				   (dvfs_status->step < MALI_DVFS_STEP-1) && fps < fps_limit) {
			level_up_time++;
			if (level_up_time == MALI_DVFS_TIME_INTERVAL) {
				/*
				printk("up,utilisation=%d,current clock=%d,fps = %d,temperature = %d",
						dvfs_status->utilisation, mali_dvfs_infotbl[dvfs_status->step].clock,
						fps,dvfs_status->temperature);
				*/
				dvfs_status->step++;
				level_up_time = 0;
				/*
				printk(" next clock=%d\n",mali_dvfs_infotbl[dvfs_status->step].clock);
				*/
				BUG_ON(dvfs_status->step >= MALI_DVFS_STEP);
			}
			level_down_time = 0;
		} else if ((dvfs_status->step > 0) &&
					(dvfs_status->utilisation < mali_dvfs_infotbl[dvfs_status->step].min_threshold)) {
			level_down_time++;
			if (level_down_time==MALI_DVFS_TIME_INTERVAL) {
				/*
				printk("down,utilisation=%d,current clock=%d,fps = %d,temperature = %d",
						dvfs_status->utilisation,
						mali_dvfs_infotbl[dvfs_status->step].clock,fps,dvfs_status->temperature);
				*/
				BUG_ON(dvfs_status->step <= 0);
				dvfs_status->step--;
				level_down_time = 0;
				/*
				printk(" next clock=%d\n",mali_dvfs_infotbl[dvfs_status->step].clock);
				*/
			}
			level_up_time = 0;
		} else {
			level_down_time = 0;
			level_up_time = 0;
			/*
			printk("keep,utilisation=%d,current clock=%d,fps = %d,temperature = %d\n",
					dvfs_status->utilisation,
					mali_dvfs_infotbl[dvfs_status->step].clock,fps,dvfs_status->temperature);			
			*/
		}
	}
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	if ((dvfs_status->upper_lock >= 0) && (dvfs_status->step > dvfs_status->upper_lock))
		dvfs_status->step = dvfs_status->upper_lock;

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

int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation,
										  u32 util_gl_share_no_use,u32 util_cl_share_no_use[2])
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

	if ((platform->time_tick == MALI_DVFS_TIME_INTERVAL) &&
		(platform->time_idle + platform->time_busy > 0))
		platform->utilisation = (100 * platform->time_busy) /
								(platform->time_idle + platform->time_busy);

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
#define dividend 7
#define fix_float(a) ((((a)*dividend)%10)?((((a)*dividend)/10)+1):(((a)*dividend)/10))
static bool calculate_dvfs_max_min_threshold(u32 level)
{
	u32 pre_level;
	u32	tmp ;
	if (0 == level) {
		if ((MALI_DVFS_STEP-1) == level) {
			mali_dvfs_infotbl[level].min_threshold = level0_min;
			mali_dvfs_infotbl[level].max_threshold = levelf_max;
		} else {
			mali_dvfs_infotbl[level].min_threshold = level0_min;
			mali_dvfs_infotbl[level].max_threshold = level0_max;
		}
	} else {
		pre_level = level - 1;
		if ((MALI_DVFS_STEP-1) == level) {
			mali_dvfs_infotbl[level].max_threshold = levelf_max;
		} else {
			mali_dvfs_infotbl[level].max_threshold = mali_dvfs_infotbl[pre_level].max_threshold +
													 div_dvfs;
		}
		mali_dvfs_infotbl[level].min_threshold = (mali_dvfs_infotbl[pre_level].max_threshold *
												  (mali_dvfs_infotbl[pre_level].clock/1000)) /
												  (mali_dvfs_infotbl[level].clock/1000); 
		
		tmp = mali_dvfs_infotbl[level].max_threshold - mali_dvfs_infotbl[level].min_threshold;
		
		mali_dvfs_infotbl[level].min_threshold += fix_float(tmp);
	}
	pr_info("mali_dvfs_infotbl[%d].clock=%d,min_threshold=%d,max_threshold=%d\n",
			level,mali_dvfs_infotbl[level].clock, mali_dvfs_infotbl[level].min_threshold,
			mali_dvfs_infotbl[level].max_threshold);
	return MALI_TRUE;
}

int kbase_platform_dvfs_init(struct kbase_device *kbdev)
{
	unsigned long flags;
	/*default status
	   add here with the right function to get initilization value.
	 */
	struct rk_context *platform;
	int i;
	int rc;
	
	platform = (struct rk_context *)kbdev->platform_context;
	if (NULL == platform)
		panic("oops");
		    
	mali_freq_table = dvfs_get_freq_volt_table(platform->mali_clk_node);
	
	if (mali_freq_table == NULL) {
		printk("mali freq table not assigned yet,use default\n");
		goto not_assigned ;
	} else {
		/*recalculte step*/
		MALI_DVFS_STEP = 0;
		for (i = 0; mali_freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
			mali_dvfs_infotbl[i].clock = mali_freq_table[i].frequency;
			MALI_DVFS_STEP++;
		}
		if(MALI_DVFS_STEP > 1)
			div_dvfs = round_up(((levelf_max - level0_max)/(MALI_DVFS_STEP-1)),1);
		printk("MALI_DVFS_STEP=%d,div_dvfs=%d\n",MALI_DVFS_STEP,div_dvfs);
		
		for(i=0;i<MALI_DVFS_STEP;i++)
			calculate_dvfs_max_min_threshold(i);
		p_mali_dvfs_infotbl = mali_dvfs_infotbl;				
	}
not_assigned :
	if (!mali_dvfs_wq)
		mali_dvfs_wq = create_singlethread_workqueue("mali_dvfs");

	spin_lock_init(&mali_dvfs_spinlock);
	mutex_init(&mali_set_clock_lock);
	mutex_init(&mali_enable_clock_lock);

	spin_lock_init(&platform->gpu_in_touch_lock);
	rc = input_register_handler(&gpufreq_input_handler);

	/*add a error handling here */
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	mali_dvfs_status_current.kbdev = kbdev;
	mali_dvfs_status_current.utilisation = 0;
	mali_dvfs_status_current.step = 0;
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
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
	
	input_unregister_handler(&gpufreq_input_handler);
}
#endif /*CONFIG_MALI_MIDGARD_DVFS*/

int mali_get_dvfs_upper_locked_freq(void)
{
	unsigned long flags;
	int locked_level = -1;

#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
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

#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
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

#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	current_level = mali_dvfs_status_current.step;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	return current_level;
}

int mali_dvfs_freq_lock(int level)
{
	unsigned long flags;
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (mali_dvfs_status_current.under_lock >= 0 &&
		mali_dvfs_status_current.under_lock > level) {
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
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	mali_dvfs_status_current.upper_lock = -1;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	printk(KERN_DEBUG "mali Upper Lock Unset\n");
}

int mali_dvfs_freq_under_lock(int level)
{
	unsigned long flags;
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (mali_dvfs_status_current.upper_lock >= 0 &&
		mali_dvfs_status_current.upper_lock < level) {
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
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	mali_dvfs_status_current.under_lock = -1;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	printk(KERN_DEBUG " mali clock Under Lock Unset\n");
}

void kbase_platform_dvfs_set_clock(kbase_device *kbdev, int freq)
{
	struct rk_context *platform;

	if (!kbdev)
		panic("oops");

	platform = (struct rk_context *)kbdev->platform_context;
	if (NULL == platform)
		panic("oops");

	if (!platform->mali_clk_node) {
		printk("mali_clk_node not init\n");
		return;
	}
	mali_dvfs_clk_set(platform->mali_clk_node,freq);
	
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

	if (WARN_ON((level >= MALI_DVFS_STEP) || (level < 0))) {
		printk("unkown mali dvfs level:level = %d,set clock not done \n",level);
	 	return  ;
	}
	/*panic("invalid level");*/
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	if (mali_dvfs_status_current.upper_lock >= 0 &&
		level > mali_dvfs_status_current.upper_lock)
		level = mali_dvfs_status_current.upper_lock;
	if (mali_dvfs_status_current.under_lock >= 0 &&
		level < mali_dvfs_status_current.under_lock)
		level = mali_dvfs_status_current.under_lock;
#endif
#ifdef CONFIG_MALI_MIDGARD_DVFS
	mutex_lock(&mali_set_clock_lock);
#endif

	kbase_platform_dvfs_set_clock(kbdev, mali_dvfs_infotbl[level].clock);
#if defined(CONFIG_MALI_MIDGARD_DEBUG_SYS) && defined(CONFIG_MALI_MIDGARD_DVFS)
	update_time_in_state(prev_level);
#endif
	prev_level = level;
#ifdef CONFIG_MALI_MIDGARD_DVFS
	mutex_unlock(&mali_set_clock_lock);
#endif
}

#ifdef CONFIG_MALI_MIDGARD_DEBUG_SYS
#ifdef CONFIG_MALI_MIDGARD_DVFS
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

ssize_t show_time_in_state(struct device *dev, struct device_attribute *attr,
									char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;
	int i;

	kbdev = dev_get_drvdata(dev);

#ifdef CONFIG_MALI_MIDGARD_DVFS
	update_time_in_state(mali_dvfs_status_current.step);
#endif
	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < MALI_DVFS_STEP; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
						"%d %llu\n",
						mali_dvfs_infotbl[i].clock, mali_dvfs_infotbl[i].time);

	if (ret < PAGE_SIZE - 1)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

ssize_t set_time_in_state(struct device *dev, struct device_attribute *attr,
								const char *buf, size_t count)
{
	int i;

	for (i = 0; i < MALI_DVFS_STEP; i++)
		mali_dvfs_infotbl[i].time = 0;

	printk(KERN_DEBUG "time_in_state value is reset complete.\n");
	return count;
}
#endif
