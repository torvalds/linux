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

// #define ENABLE_DEBUG_LOG
#include "custom_log.h"

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
/** gpu 温度上限. */
#define gpu_temp_limit 110
/** 经过 gpu_temp_statis_time 次测量记录之后, 对温度数据取平均. */
#define gpu_temp_statis_time 1

#define level0_min 0
#define level0_max 70
#define levelf_max 100

static u32 div_dvfs = 0 ;

/**
 * .DP : mali_dvfs_level_table.
 * 其中的 level_items 的 gpu_clk_freq 从低到高.
 *
 * 运行时初始化阶段, 将从 'mali_freq_table' 进行运行时初始化,
 * 若获取 'mali_freq_table' 失败, 则使用这里的 缺省配置.
 * 参见 kbase_platform_dvfs_init.
 */
static mali_dvfs_info mali_dvfs_infotbl[] = {
	{925000, 100000, 0, 70, 0},
	{925000, 160000, 50, 65, 0},
	{1025000, 266000, 60, 78, 0},
	{1075000, 350000, 65, 75, 0},
	{1125000, 400000, 70, 75, 0},
	{1200000, 500000, 90, 100, 0},
};
/**
 * pointer_to_mali_dvfs_level_table.
 */
mali_dvfs_info *p_mali_dvfs_infotbl = NULL;

/**
 * num_of_mali_dvfs_levels : mali_dvfs_level_table 中有效的 level_item 的数量.
 */
unsigned int MALI_DVFS_STEP = ARRAY_SIZE(mali_dvfs_infotbl);

/**
 * mali_dvfs_level_table 中可以容纳的 level_items 的最大数量.
 */
const unsigned int MAX_NUM_OF_MALI_DVFS_LEVELS = ARRAY_SIZE(mali_dvfs_infotbl);

/**
 * gpu_clk_freq_table_from_system_dvfs_module, 从 system_dvfs_module 得到的 gpu_clk 的 频点表.
 * 原始的 频点配置信息在 .dts 文件中.
 */
static struct cpufreq_frequency_table *mali_freq_table = NULL;
#ifdef CONFIG_MALI_MIDGARD_DVFS

/** mali_dvfs_status_t. */
typedef struct _mali_dvfs_status_type {
	struct kbase_device *kbdev;
	/** 
	 * .DP : current_dvfs_level : 当前使用的 mali_dvfs_level 在 mali_dvfs_level_table 中的 index.
	 * 参见 mali_dvfs_infotbl. 
	 */
	int step;
	/** 最新的 由 metrics_system 报告的 current_calculated_utilisation. */
	int utilisation;
	/** 最近一次完成的 temperature_record_section 记录得到的温度数据. */
	u32 temperature;
	/** 当前 temperature_record_section 中, 已经记录温度的次数. */
	u32 temperature_time;
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	/** 
	 * gpu_freq_upper_limit, 即 dvfs_level_upper_limit.
	 * 量纲是 index of mali_dvfs_level_table.
	 * 若是 -1, 则表示当前未设置 dvfs_level_upper_limit.
	 */
	int upper_lock;
	/** 
	 * gpu_freq_lower_limit, 即 dvfs_level_lower_limit.
	 * 量纲是 index of mali_dvfs_level_table.
	 * 若是 -1, 则表示当前未设置 dvfs_level_lower_limit.
	 */
	int under_lock;
#endif

} mali_dvfs_status;

static struct workqueue_struct *mali_dvfs_wq = 0;

/**
 * 用来在并发环境下, 保护 mali_dvfs_status_current 等数据.
 */
spinlock_t mali_dvfs_spinlock;
struct mutex mali_set_clock_lock;
struct mutex mali_enable_clock_lock;

#ifdef CONFIG_MALI_MIDGARD_DEBUG_SYS
static void update_time_in_state(int level);
#endif
/* .DP : current_mali_dvfs_status. */
static mali_dvfs_status mali_dvfs_status_current;

#define LIMIT_FPS 60
#define LIMIT_FPS_POWER_SAVE 50

/*---------------------------------------------------------------------------*/
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
	/* 有 input_event 到来, 设置对应标识. */
	platform->gpu_in_touch = true;
	spin_unlock_irqrestore(&platform->gpu_in_touch_lock, flags);
}

static int gpufreq_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;	// 用于关联 'dev' 和 'handler'.
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;		// 'handle' 关联的 input_dev.
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

/**
 * 待处理(关联) 的 input_device_ids_table.
 */
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
/*---------------------------------------------------------------------------*/

/**
 * mali_dvfs_work 的实现主体, 即对 dvfs_event 的处理流程的主体函数. 
 */
static void mali_dvfs_event_proc(struct work_struct *w)
{
	unsigned long flags;
	mali_dvfs_status *dvfs_status;

	static int level_down_time = 0; // counter_of_requests_to_jump_down_in_dvfs_level_table : 
					//      对 mali_dvfs_level 下跳 请求 发生的次数的静态计数. 
	static int level_up_time = 0;   // counter_of_requests_to_jump_up_in_dvfs_level_table : 
					//      对 mali_dvfs_level 上跳 请求发生的次数的静态计数. 
	static u32 temp_tmp;
	struct rk_context *platform;
	u32 fps = 0;            // real_fps.
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

	temp_tmp += rockchip_tsadc_get_temp(1);         // .Q : 获取当前温度? "1" : 意义? 指定特定的测试通道? 

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
		V("policy : %d , fps_limit = %d", policy, fps_limit);

		/*give priority to temperature unless in performance mode */
		if (dvfs_status->temperature > gpu_temp_limit)  // 若记录的 gpu 温度 超过了 上限, 则 ...
		{
			if(dvfs_status->step > 0)
				dvfs_status->step--;
			
			if(gpu_temp_statis_time > 1)
				dvfs_status->temperature = 0;
			/*
			   pr_info("decrease step for temperature over %d,next clock = %d\n",
			   gpu_temp_limit, mali_dvfs_infotbl[dvfs_status->step].clock);
			 */
			V("jump down in dvfs_level_table to level '%d', for temperature over %d, next clock = %d",
					dvfs_status->step,
					gpu_temp_limit,
					mali_dvfs_infotbl[dvfs_status->step].clock);
		} 
		// 若 current_calculated_utilisation 要求 上调 mali_dvfs_level, 
		//      且 current_dvfs_level 还可能被上调, 
		//      且 real_fps "小于" fps_limit, 
		// 则 .... 
		else if ( (dvfs_status->utilisation > mali_dvfs_infotbl[dvfs_status->step].max_threshold) 
				&& (dvfs_status->step < MALI_DVFS_STEP - 1) 
				&& fps < fps_limit ) 
		{
			// 至此, 可认为一次请求 mali_dvfs_level 上跳 发生.

			level_up_time++;

			/* 若 上跳请求的次数 达到 执行具体上跳 要求, 则... */
			if (level_up_time == MALI_DVFS_UP_TIME_INTERVAL) 
			{
				V("to jump up in dvfs_level_table, utilisation=%d, current clock=%d, fps = %d, temperature = %d",
						dvfs_status->utilisation,
						mali_dvfs_infotbl[dvfs_status->step].clock,
						fps,
						dvfs_status->temperature);
				/* 预置 current_dvfs_level 上跳. */      // 具体生效将在最后.
				dvfs_status->step++;
				/* 清 上跳请求计数. */
				level_up_time = 0;

				V(" next clock=%d.", mali_dvfs_infotbl[dvfs_status->step].clock);
				BUG_ON(dvfs_status->step >= MALI_DVFS_STEP);    // 数组中元素的 index 总是比 size 小. 
			}

			/* 清 下跳请求计数. */
			level_down_time = 0;
		} 
		/* 否则, 若 current_calculated_utilisation 要求 current_dvfs_level 下跳, 且 还可以下跳, 则... */
		else if ((dvfs_status->step > 0) 
				&& (dvfs_status->utilisation < mali_dvfs_infotbl[dvfs_status->step].min_threshold)) 
		{
			level_down_time++;

			if (level_down_time==MALI_DVFS_DOWN_TIME_INTERVAL) 
			{
				V("to jump down in dvfs_level_table ,utilisation=%d, current clock=%d, fps = %d, temperature = %d",
						dvfs_status->utilisation,
						mali_dvfs_infotbl[dvfs_status->step].clock,
						fps,
						dvfs_status->temperature);

				BUG_ON(dvfs_status->step <= 0);
				dvfs_status->step--;
				level_down_time = 0;

				V(" next clock=%d",mali_dvfs_infotbl[dvfs_status->step].clock);
			}

			level_up_time = 0;
		} 
		/* 否则, ... */
		else 
		{
			level_down_time = 0;
			level_up_time = 0;

			V("keep current_dvfs_level, utilisation=%d,current clock=%d,fps = %d,temperature = %d\n",
					dvfs_status->utilisation,
					mali_dvfs_infotbl[dvfs_status->step].clock,
					fps,
					dvfs_status->temperature);			
		}
	}
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	// #error               // 目前配置下, 本段代码有效. 

	// 若 指定了 dvfs_level_upper_limit, 
	//      且 预置的 current_dvfs_level "大于" dvfs_level_upper_limit,
	// 则...
	if ((dvfs_status->upper_lock >= 0) 
			&& (dvfs_status->step > dvfs_status->upper_lock))
	{
		/* 将 预置的 current_dvfs_level 调整到 dvfs_level_upper_limit. */
		dvfs_status->step = dvfs_status->upper_lock;
	}

	if (dvfs_status->under_lock > 0) {
		if (dvfs_status->step < dvfs_status->under_lock)
			dvfs_status->step = dvfs_status->under_lock;
	}
#endif
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
	/* 将命令 dvfs_module 让 current_dvfs_level 具体生效. */
	kbase_platform_dvfs_set_level(dvfs_status->kbdev, dvfs_status->step);

	mutex_unlock(&mali_enable_clock_lock);
}

/**
 * mali_dvfs_work : 处理来自 kbase_platform_dvfs_event 的 dvfs_event 的 work.
 */
static DECLARE_WORK(mali_dvfs_work, mali_dvfs_event_proc);


/* ############################################################################################# */
// callback_interface_to_common_parts_in_mdd

/**
 * 由 common_parts_in_mdd 调用的, 将 dvfs_event (utilisation_report_event) 通知回调到 platform_dependent_part_in_mdd.
 */
int kbase_platform_dvfs_event(struct kbase_device *kbdev,
				u32 utilisation,          // current_calculated_utilisation 
				u32 util_gl_share_no_use,
				u32 util_cl_share_no_use[2] )
{
	unsigned long flags;
	struct rk_context *platform;

	BUG_ON(!kbdev);
	platform = (struct rk_context *)kbdev->platform_context;

	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (platform->time_tick < MALI_DVFS_UP_TIME_INTERVAL) {
		platform->time_tick++;
		platform->time_busy += kbdev->pm.backend.metrics.time_busy;
                
		platform->time_idle += kbdev->pm.backend.metrics.time_idle;
	} else {
		platform->time_busy = kbdev->pm.backend.metrics.time_busy;
		platform->time_idle = kbdev->pm.backend.metrics.time_idle;
		platform->time_tick = 0;
	}

	if ((platform->time_tick == MALI_DVFS_UP_TIME_INTERVAL) &&
		(platform->time_idle + platform->time_busy > 0))
		platform->utilisation = (100 * platform->time_busy) /
								(platform->time_idle + platform->time_busy);

	/* 记录 current_calculated_utilisation. */
	mali_dvfs_status_current.utilisation = utilisation;
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	/* 要求在 cpu_0 上, 使用 workqueue mali_dvfs_wq, 执行 mali_dvfs_work. */
	queue_work_on(0, mali_dvfs_wq, &mali_dvfs_work);
	/*add error handle here */
	return MALI_TRUE;
}
/* ############################################################################################# */

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
	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
	enable = kbdev->pm.backend.metrics.timer_active;
	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);

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

	if (enable != kbdev->pm.backend.metrics.timer_active) {
		/* 若要 使能 dvfs, 则... */
		if (enable) {
			spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
			kbdev->pm.backend.metrics.timer_active = MALI_TRUE;
			spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
			hrtimer_start(&kbdev->pm.backend.metrics.timer,
					HR_TIMER_DELAY_MSEC(KBASE_PM_DVFS_FREQUENCY),
					HRTIMER_MODE_REL);
		}
		/* 否则, 即要 disable dvfs, 则 ... */
		else {
			spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
			kbdev->pm.backend.metrics.timer_active = MALI_FALSE;
			spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
			hrtimer_cancel(&kbdev->pm.backend.metrics.timer);
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
/**
 * 为 'mali_dvfs_info' 中 index 是 'level' 的 level_item, 计算 min_threshold 和 max_threshold.
 */
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
		    
	D("to get gpu_clk_freq_table from system_dvfs_module.");
	mali_freq_table = dvfs_get_freq_volt_table(platform->mali_clk_node);
	if (mali_freq_table == NULL) {
		printk("mali freq table not assigned yet,use default\n");
		goto not_assigned ;
	} else {
		D("we got valid gpu_clk_freq_table, to init mali_dvfs_level_table with it.");

		/*recalculte step*/
		MALI_DVFS_STEP = 0;

		for ( i = 0; 
		      mali_freq_table[i].frequency != CPUFREQ_TABLE_END 
			&& i < MAX_NUM_OF_MALI_DVFS_LEVELS;
		      i++ ) 
		{
			mali_dvfs_infotbl[i].clock = mali_freq_table[i].frequency;
			MALI_DVFS_STEP++;
		}

		if(MALI_DVFS_STEP > 1)
			div_dvfs = round_up( ( (levelf_max - level0_max) / (MALI_DVFS_STEP - 1) ), 1);

		printk("MALI_DVFS_STEP = %d, div_dvfs = %d \n",MALI_DVFS_STEP, div_dvfs);
		
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

	/*add a error handling here */
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	mali_dvfs_status_current.kbdev = kbdev;
	mali_dvfs_status_current.utilisation = 0;
	mali_dvfs_status_current.step = 0;
#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	mali_dvfs_status_current.upper_lock = -1;	// 初始时, 未设置. 
	mali_dvfs_status_current.under_lock = -1;
#endif
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);

	spin_lock_init(&platform->gpu_in_touch_lock);
	rc = input_register_handler(&gpufreq_input_handler);
	if ( 0 != rc )
	{
		E("fail to register gpufreq_input_handler.");
	}

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
	int  gpu_clk_freq = -1;	// gpu_clk_freq_of_upper_limit

#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (mali_dvfs_status_current.upper_lock >= 0)
	{
		gpu_clk_freq = mali_dvfs_infotbl[mali_dvfs_status_current.upper_lock].clock;
	}
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	return gpu_clk_freq;
}

int mali_get_dvfs_under_locked_freq(void)
{
	unsigned long flags;
	int  gpu_clk_freq = -1;	// gpu_clk_freq_of_upper_limit

#ifdef CONFIG_MALI_MIDGARD_FREQ_LOCK
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);
	if (mali_dvfs_status_current.under_lock >= 0)
	{
		gpu_clk_freq = mali_dvfs_infotbl[mali_dvfs_status_current.under_lock].clock;
	}
	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
#endif
	return gpu_clk_freq;
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
        /*-----------------------------------*/
	spin_lock_irqsave(&mali_dvfs_spinlock, flags);

	if (mali_dvfs_status_current.under_lock >= 0 
		&& mali_dvfs_status_current.under_lock > level) 
	{
		printk(KERN_ERR " Upper lock Error : Attempting to set upper lock to below under lock\n");
		spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
		return -1;
	}

	V("to set current dvfs_upper_lock to level '%d'.", level);
	mali_dvfs_status_current.upper_lock = level;

	spin_unlock_irqrestore(&mali_dvfs_spinlock, flags);
        /*-----------------------------------*/

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

void kbase_platform_dvfs_set_clock(struct kbase_device *kbdev, int freq)
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
	/* .KP : 将调用平台特定接口, 设置 gpu_clk. */
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

void kbase_platform_dvfs_set_level(struct kbase_device *kbdev, int level)
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

	/* 令 mali_dvfs_status_current 的 current_dvfs_level 的具体时钟配置生效. */
	kbase_platform_dvfs_set_clock(kbdev, mali_dvfs_infotbl[level].clock);
#if defined(CONFIG_MALI_MIDGARD_DEBUG_SYS) && defined(CONFIG_MALI_MIDGARD_DVFS)
	// 将实际退出 prev_level, update mali_dvfs_level_table 中 prev_level 的 total_time_in_this_level.
	update_time_in_state(prev_level);
#endif
	prev_level = level;
#ifdef CONFIG_MALI_MIDGARD_DVFS
	mutex_unlock(&mali_set_clock_lock);
#endif
}

#ifdef CONFIG_MALI_MIDGARD_DEBUG_SYS
#ifdef CONFIG_MALI_MIDGARD_DVFS
static u64 prev_time = 0;
/**
 * update mali_dvfs_level_table 中当前 dvfs_level 'level' 的 total_time_in_this_level.
 */
static void update_time_in_state(int level)
{
	u64 current_time;

	if (level < 0)
		return;

#if 0
        /* 若当前 mali_dvfs "未开启", 则... */
	if (!kbase_platform_dvfs_get_enable_status())
        {
		return;
        }
#endif

	if (prev_time ==0)
		prev_time=get_jiffies_64();

	current_time = get_jiffies_64();
	mali_dvfs_infotbl[level].time += current_time - prev_time;

	prev_time = current_time;
}
#endif

ssize_t show_time_in_state(struct device *dev,
			   struct device_attribute *attr,
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
	{
		return -ENODEV;
	}

	ret += snprintf(buf + ret, 
			PAGE_SIZE - ret,
			"------------------------------------------------------------------------------");
	ret += snprintf(buf + ret, 
			PAGE_SIZE - ret,
			"\n%-16s\t%-24s\t%-24s",
			"index_of_level",
			"gpu_clk_freq (KHz)",
			"time_in_this_level (s)");
	ret += snprintf(buf + ret, 
			PAGE_SIZE - ret,
			"\n------------------------------------------------------------------------------");

	for ( i = 0; i < MALI_DVFS_STEP; i++ )
	{
		ret += snprintf(buf + ret, 
				PAGE_SIZE - ret,
				"\n%-16d\t%-24u\t%-24u",
				i,
				mali_dvfs_infotbl[i].clock / 1000,
				jiffies_to_msecs(mali_dvfs_infotbl[i].time) / 1000);
	}
	ret += snprintf(buf + ret, 
			PAGE_SIZE - ret,
			"\n------------------------------------------------------------------------------");

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

	/* reset 所有 level 的 total_time_in_this_level. */
	for (i = 0; i < MALI_DVFS_STEP; i++)
	{
		mali_dvfs_infotbl[i].time = 0;
	}

        prev_time = 0;

	printk(KERN_DEBUG "time_in_state value is reset complete.\n");
	return count;
}
#endif
