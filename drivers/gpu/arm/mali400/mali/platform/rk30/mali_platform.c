/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2012 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for a default platform
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"

#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/clk.h>
#include <linux/device.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif /* CONFIG_HAS_EARLYSUSPEND */
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/cpufreq.h>

#include <linux/rockchip/dvfs.h>

/*author@xxm*/
#define GPUCLK_NAME  				 "gpu"
#define GPUCLK_PD_NAME				 "pd_gpu"
#define GPU_MHZ 					 1000000
static struct dvfs_node  *mali_clock = 0;
static struct clk		 *mali_clock_pd = 0;


#define MALI_DVFS_DEFAULT_STEP 0 // 50Mhz default

u32 mali_dvfs[] = {50,100,133,160,200,266,400};
int num_clock;

u32 mali_init_clock = 50;
static int minuend = 0;

static struct cpufreq_frequency_table *freq_table = NULL;

module_param_array(mali_dvfs, int, &num_clock,S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mali_dvfs,"mali clock table");

module_param(mali_init_clock, int,S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mali_init_clock,"mali init clock value");

u32 mali_group_error = 0;
u32 scale_enable = 1;
u32 gpu_power_state = 0;
static u32 utilization_global = 0;

u32 mali_utilization_timeout = 10;
u32 sampling_enable = 1;
#define mali_freq_workqueue_name	 "mali_freq_workqueue"
#define mali_freq_work_name 		 "mali_freq_work"
struct mali_freq_data {
	struct workqueue_struct *wq;
	struct work_struct work;
	u32 freq;
}*mali_freq_data;

typedef struct mali_dvfs_tableTag{
	u32 clock;
	u32 vol;
}mali_dvfs_table;

typedef struct mali_dvfs_statusTag{
    int currentStep;
    mali_dvfs_table * pCurrentDvfs;

}mali_dvfs_status;

mali_dvfs_status maliDvfsStatus;

#define GPU_DVFS_UP_THRESHOLD	((int)((255*50)/100))	
#define GPU_DVFS_DOWN_THRESHOLD ((int)((255*35)/100))	

_mali_osk_mutex_t *clockSetlock;

struct clk* mali_clk_get(unsigned char *name)
{
	struct clk *clk;
	clk = clk_get(NULL,name);
	return clk;
}
unsigned long mali_clk_get_rate(struct dvfs_node *clk)
{
	return dvfs_clk_get_rate(clk);
}

void mali_clk_set_rate(struct dvfs_node *clk,u32 value)
{
	unsigned long rate = (unsigned long)value * GPU_MHZ;
	dvfs_clk_set_rate(clk, rate);
	rate = mali_clk_get_rate(clk);
}

static struct kobject *mali400_utility_object;

static u32 get_mali_dvfs_status(void)
{
	return maliDvfsStatus.currentStep;
}
static void set_mali_dvfs_step(u32 value)
{
	maliDvfsStatus.currentStep = value;
}

static void scale_enable_set(u32 value)
{
	scale_enable = value;
}
static u32 mali_dvfs_search(u32 value)
{
	u32 i;	
	u32 clock = value;
	for(i=0;i<num_clock;i++)
	{
		if(clock == mali_dvfs[i])
		{
			_mali_osk_mutex_wait(clockSetlock);
			mali_clk_set_rate(mali_clock,clock);
			_mali_osk_mutex_signal(clockSetlock);
			set_mali_dvfs_step(i);
			scale_enable_set(0);
			return 0;
		}
		if(i>=7)
		MALI_DEBUG_PRINT(2,("USER set clock not in the mali_dvfs table\r\n"));
	}
	return 1;
}

static int mali400_utility_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", utilization_global);
}
static int mali400_clock_set(struct device *dev,struct device_attribute *attr, const char *buf,u32 count)
{
	u32 clock;
	u32 currentStep;
	u64 timeValue;
	clock = simple_strtoul(buf, NULL, 10);
	currentStep = get_mali_dvfs_status();
	timeValue = _mali_osk_time_get_ns();
	/*MALI_PRINT(("USER SET CLOCK,%d\r\n",clock));*/
	if(!clock)
	{
		scale_enable_set(1);
	}
	else
	{
		mali_dvfs_search(clock);
	}
	return count;
}
static int clock_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	u32 i;
	char *pos = buf;
	pos += snprintf(pos,PAGE_SIZE,"%d,",num_clock);
	for(i=0;i<(num_clock-1);i++)
	{
		pos += snprintf(pos,PAGE_SIZE,"%d,",mali_dvfs[i]);
	}
	pos +=snprintf(pos,PAGE_SIZE,"%d\n",mali_dvfs[i]); 
	return pos - buf;
}
static int sampling_timeout_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "mali_utilization_timeout = %d\n", mali_utilization_timeout);
}
static int sampling_timeout_set(struct device *dev,struct device_attribute *attr, const char *buf,u32 count)
{
	u32 sampling;
	sampling = simple_strtoul(buf, NULL, 10);
	
	if(sampling == 0 )
	{
		sampling_enable = 0;
		MALI_PRINT(("disable mali clock frequency scalling\r\n"));
	}
	else
	{
		mali_utilization_timeout = sampling;
		sampling_enable = 1;
		MALI_PRINT(("enable mali clock frequency scalling ,mali_utilization_timeout : %dms\r\n",mali_utilization_timeout));
	}
	return count;
}
static int error_count_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", mali_group_error);
}

static DEVICE_ATTR(utility, 0644, mali400_utility_show, mali400_clock_set);
static DEVICE_ATTR(param, 0644, clock_show, NULL);
static DEVICE_ATTR(sampling_timeout, 0644, sampling_timeout_show,sampling_timeout_set);
static DEVICE_ATTR(error_count, 0644, error_count_show, NULL);


static mali_bool mali400_utility_sysfs_init(void)
{
	u32 ret ;

	mali400_utility_object = kobject_create_and_add("mali400_utility", NULL);
	if (mali400_utility_object == NULL) {
		return -1;
	}
	ret = sysfs_create_file(mali400_utility_object, &dev_attr_utility.attr);
	if (ret) {
		return -1;
	}
	ret = sysfs_create_file(mali400_utility_object, &dev_attr_param.attr);
	if (ret) {
		return -1;
	}
	ret = sysfs_create_file(mali400_utility_object, &dev_attr_sampling_timeout.attr);
	if(ret){
		return -1;	
	}
	ret = sysfs_create_file(mali400_utility_object, &dev_attr_error_count.attr);
	if(ret){
		return -1;
	}
	return 0 ;
}	
static unsigned int decideNextStatus(unsigned int utilization)
{
    u32 level=0;
#if 1
	{
	    if(utilization>GPU_DVFS_UP_THRESHOLD && maliDvfsStatus.currentStep==0 && maliDvfsStatus.currentStep<(num_clock-minuend))
	        level=1;
	    else if(utilization>GPU_DVFS_UP_THRESHOLD && maliDvfsStatus.currentStep==1 && maliDvfsStatus.currentStep<(num_clock-minuend))
	        level=2;
		else if(utilization>GPU_DVFS_UP_THRESHOLD && maliDvfsStatus.currentStep==2 && maliDvfsStatus.currentStep<(num_clock-minuend))
			level=3;
		else if(utilization>GPU_DVFS_UP_THRESHOLD && maliDvfsStatus.currentStep==3 && maliDvfsStatus.currentStep<(num_clock-minuend))
			level=4;
		else if(utilization>GPU_DVFS_UP_THRESHOLD && maliDvfsStatus.currentStep==4 && maliDvfsStatus.currentStep<(num_clock-minuend))
			level=5;
		else if(utilization>GPU_DVFS_UP_THRESHOLD && maliDvfsStatus.currentStep==5 && maliDvfsStatus.currentStep<(num_clock-minuend))
			level=6;
		/*
			determined by minuend to up to level 6
		*/
		else if(utilization<GPU_DVFS_DOWN_THRESHOLD && maliDvfsStatus.currentStep==6 /*&& maliDvfsStatus.currentStep<num_clock*/)
			level=5;
		else if(utilization<GPU_DVFS_DOWN_THRESHOLD && maliDvfsStatus.currentStep==5 /*&& maliDvfsStatus.currentStep<num_clock*/)
			level=4;
		else if(utilization<GPU_DVFS_DOWN_THRESHOLD && maliDvfsStatus.currentStep==4 /*&& maliDvfsStatus.currentStep<num_clock*/)
			level=3;
		else if(utilization<GPU_DVFS_DOWN_THRESHOLD && maliDvfsStatus.currentStep==3 /*&& maliDvfsStatus.currentStep<num_clock*/)
			level=2;
		else if(utilization<GPU_DVFS_DOWN_THRESHOLD && maliDvfsStatus.currentStep==2 /*&& maliDvfsStatus.currentStep<num_clock*/)
			level=1;
		else if(utilization<GPU_DVFS_DOWN_THRESHOLD && maliDvfsStatus.currentStep==1 /*&& maliDvfsStatus.currentStep<num_clock*/)
			level=0;
	    else
	        level = maliDvfsStatus.currentStep;
	}
#endif
    return level;
}

static mali_bool set_mali_dvfs_status(u32 step)
{
    u32 validatedStep=step;	
	if(1)
	{
		_mali_osk_mutex_wait(clockSetlock);
    	mali_clk_set_rate(mali_clock, mali_dvfs[validatedStep]);
		_mali_osk_mutex_signal(clockSetlock);
		set_mali_dvfs_step(validatedStep);
	}	
    return MALI_TRUE;
}

static mali_bool change_mali_dvfs_status(u32 step)
{
    if(!set_mali_dvfs_status(step))
    {
        MALI_DEBUG_PRINT(2,("error on set_mali_dvfs_status: %d\n",step));
        return MALI_FALSE;
    }
	return MALI_TRUE;
}

static void  mali_freq_scale_work(struct work_struct *work)
{	

	u32 nextStatus = 0;
	u32 curStatus = 0;

	curStatus = get_mali_dvfs_status();
	nextStatus = decideNextStatus(utilization_global);
	
	if(curStatus!=nextStatus)
	{
		if(!change_mali_dvfs_status(nextStatus))
		{
			MALI_DEBUG_PRINT(1, ("error on change_mali_dvfs_status \n"));
		}
	}		
}
static mali_bool init_mali_clock(void)
{
	mali_bool ret = MALI_TRUE;
	int i;
	if (mali_clock != 0 || mali_clock_pd != 0)
		return ret; 
	
	mali_clock_pd = clk_get(NULL,GPUCLK_PD_NAME);
	if (IS_ERR(mali_clock_pd))
	{
		MALI_PRINT( ("MALI Error : failed to get source mali pd\n"));
		ret = MALI_FALSE;
		goto err_gpu_clk;
	}
	clk_prepare_enable(mali_clock_pd);
	
	mali_clock = clk_get_dvfs_node(GPUCLK_NAME);
	if (IS_ERR(mali_clock))
	{
		MALI_PRINT( ("MALI Error : failed to get source mali clock\n"));
		ret = MALI_FALSE;
		goto err_gpu_clk;
	}
	dvfs_clk_prepare_enable(mali_clock);
	freq_table = dvfs_get_freq_volt_table(mali_clock);
	if(!freq_table)
	{
		MALI_PRINT(("Stop,dvfs table should be set in dts\n"));
		return MALI_FALSE;
	}
	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) 
	{
		mali_dvfs[i] = freq_table[i].frequency/1000;
	}
	mali_init_clock = mali_dvfs[0];
	num_clock = i;
	minuend = 1;
	MALI_PRINT(("Mali400 inside of rk3036\r\n"));

	mali_clk_set_rate(mali_clock, mali_init_clock);
	gpu_power_state = 1;

	return MALI_TRUE;

err_gpu_clk:
	MALI_PRINT(("::clk_put:: %s mali_clock\n", __FUNCTION__));
	gpu_power_state = 0;
	clk_disable_unprepare(mali_clock_pd);
	dvfs_clk_disable_unprepare(mali_clock);
	mali_clock = 0;
	mali_clock_pd = 0;

	return ret;
}

static mali_bool deinit_mali_clock(void)
{
	if (mali_clock == 0 && mali_clock_pd == 0)
		return MALI_TRUE;
	dvfs_clk_disable_unprepare(mali_clock);
	clk_disable_unprepare(mali_clock_pd);
	mali_clock = 0;
	mali_clock_pd = 0;
	if(gpu_power_state)
		gpu_power_state = 0;
	return MALI_TRUE;
}

mali_bool init_mali_dvfs_status(int step)
{
	set_mali_dvfs_step(step);
    return MALI_TRUE;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mali_pm_early_suspend(struct early_suspend *mali_dev)
{
	/*do nothing*/
}
static void mali_pm_late_resume(struct early_suspend *mali_dev)
{
	/*do nothing*/
}
static struct early_suspend mali_dev_early_suspend = {
	.suspend = mali_pm_early_suspend,
	.resume = mali_pm_late_resume,
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
};
#endif /* CONFIG_HAS_EARLYSUSPEND */

_mali_osk_errcode_t mali_platform_init(void)
{
	MALI_CHECK(init_mali_clock(), _MALI_OSK_ERR_FAULT);
	
	clockSetlock = _mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_ORDERED,_MALI_OSK_LOCK_ORDER_UTILIZATION);

	if(!init_mali_dvfs_status(MALI_DVFS_DEFAULT_STEP))
		MALI_DEBUG_PRINT(1, ("init_mali_dvfs_status failed\n"));
	
	if(mali400_utility_sysfs_init())
		MALI_PRINT(("mali400_utility_sysfs_init error\r\n"));
	
	mali_freq_data = kmalloc(sizeof(struct mali_freq_data), GFP_KERNEL);
	if(!mali_freq_data)
	{
		MALI_PRINT(("kmalloc error\r\n"));
		MALI_ERROR(-1);
	}
	mali_freq_data->wq = create_workqueue(mali_freq_workqueue_name);
	if(!mali_freq_data->wq)
		MALI_ERROR(-1);
	INIT_WORK(&mali_freq_data->work,mali_freq_scale_work);
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&mali_dev_early_suspend);
#endif

    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit(void)
{
	deinit_mali_clock();
	_mali_osk_mutex_term(clockSetlock);

    MALI_SUCCESS;
}
_mali_osk_errcode_t mali_power_domain_control(u32 bpower_off)
{
	if (!bpower_off)
	{
		if(!gpu_power_state)
		{
			clk_prepare_enable(mali_clock_pd);
			dvfs_clk_prepare_enable(mali_clock);
			gpu_power_state = 1 ;
		}		
	}
	else if (bpower_off == 2)
	{
		;
	}
	else if (bpower_off == 1)
	{
		if(gpu_power_state)
		{
			dvfs_clk_disable_unprepare(mali_clock);
			clk_disable_unprepare(mali_clock_pd);	
			gpu_power_state = 0;
		}
	}
    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
#if 0
	switch(power_mode)
	{
		case MALI_POWER_MODE_ON:
			MALI_DEBUG_PRINT(2,("MALI_POWER_MODE_ON\r\n"));
			mali_power_domain_control(MALI_POWER_MODE_ON);
			break;
		case MALI_POWER_MODE_LIGHT_SLEEP:
			MALI_DEBUG_PRINT(2,("MALI_POWER_MODE_LIGHT_SLEEP\r\n"));
			mali_power_domain_control(MALI_POWER_MODE_LIGHT_SLEEP);
			break;
		case MALI_POWER_MODE_DEEP_SLEEP:
			MALI_DEBUG_PRINT(2,("MALI_POWER_MODE_DEEP_SLEEP\r\n"));
			mali_power_domain_control(MALI_POWER_MODE_DEEP_SLEEP);
			break;
		default:
			MALI_DEBUG_PRINT(2,("mali_platform_power_mode_change:power_mode(%d) not support \r\n",power_mode));
	}
#endif
    MALI_SUCCESS;
}

void mali_gpu_utilization_handler(struct mali_gpu_utilization_data *data)
{
	if(data->utilization_pp > 256)
		return;
	utilization_global = data->utilization_pp;
	/*
	MALI_PRINT(("utilization_global = %d\r\n",utilization_global));
	*/
	if(scale_enable && sampling_enable)
		queue_work(mali_freq_data->wq,&mali_freq_data->work);
	
	return ;
}

void set_mali_parent_power_domain(void* dev)
{
}



