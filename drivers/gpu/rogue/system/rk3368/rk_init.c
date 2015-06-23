#if defined(SUPPORT_ION)
#include "ion_sys.h"
#endif /* defined(SUPPORT_ION) */

#include <linux/hardirq.h>
#include <linux/clk.h>
#include <linux/clk-private.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/rk_fb.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/rockchip/dvfs.h>
#include <linux/rockchip/common.h>
#include <linux/workqueue.h>
#include <linux/clkdev.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/freezer.h>
#include <linux/sched/rt.h>
#include "power.h"
#include "rk_init.h"

#include <asm/compiler.h>


#if RK_TF_VERSION
#define PSCI_RKSIP_TF_VERSION (0x82000001)


static noinline int __invoke_psci_fn_smc(u64 function_id, u64 arg0, u64 arg1,
      u64 arg2)
{
 asm volatile(
   __asmeq("%0", "x0")
   __asmeq("%1", "x1")
   __asmeq("%2", "x2")
   __asmeq("%3", "x3")
   "smc #0\n"
  : "+r" (function_id)
  : "r" (arg0), "r" (arg1), "r" (arg2));


 return function_id;
}

static int (*invoke_psci_fn)(u64, u64 , u64, u64) = __invoke_psci_fn_smc;


static int rk_tf_get_version(void)
{
    int ver_num;
    ver_num = invoke_psci_fn(PSCI_RKSIP_TF_VERSION, 0, 0, 0);

    return ver_num;
}

static int rk_tf_check_version(void)
{
    int version=0;
    int high_16=0;
    int low_16=0;
    IMG_PINT pNULL=NULL;

    version = rk_tf_get_version();
    high_16 = (version >> 16) & ~(0xFFFF << 16);
    low_16 = (version & ~(0xFFFF << 16));

    printk("raw version=0x%x,rk_tf_version=%x.%x\n",version,high_16,low_16);

    if((version != 0xFFFFFFFF) && (high_16 >= 1) && (low_16 >= 3))
    {
        return 0;
    }
    else
    {
        printk("Error:%s-line:%d This version cann't support rk3328\n",__func__,__LINE__);
        *pNULL=0; //crash system
        return -1;
    }
}

#endif

extern struct platform_device *gpsPVRLDMDev;

#if RK33_DVFS_SUPPORT
#define gpu_temp_limit                  110
#define gpu_temp_statis_time            1
#define level0_threshold_min            0
#define level0_threshold_max            40
#define levelf_threshold_max            100
#define level0_coef_max                 95

static IMG_UINT32 div_dvfs = 0 ;

/*dvfs status*/
static struct workqueue_struct *rgx_dvfs_wq = IMG_NULL;
spinlock_t rgx_dvfs_spinlock;
//struct mutex rgx_set_clock_lock;
struct mutex rgx_enable_clock_lock;
static struct cpufreq_frequency_table *rgx_freq_table = NULL;
#endif

/* voltage     clock   min_threshold   max_threshold   time  */
static rgx_dvfs_info rgx_dvfs_infotbl[] =
{
    {925, 100, 0, 70, 0, 100},
    {925, 160, 50, 65, 0, 95},
    {1025, 266, 60, 78, 0, 90},
    {1075, 350, 65, 75, 0, 85},
    {1125, 400, 70, 75, 0, 80},
    {1200, 500, 90, 100, 0, 75},
};
rgx_dvfs_info *p_rgx_dvfs_infotbl = rgx_dvfs_infotbl;

unsigned int RGX_DVFS_STEP = ARRAY_SIZE(rgx_dvfs_infotbl);

static int rk33_clk_set_normal_node(struct clk* node, unsigned long rate)
{
    int ret = 0;

    if (!node)
    {
        printk("rk33_clk_set_normal_node error \r\n");
        ret = -1;
    }
    ret = clk_set_rate(node, rate * ONE_MHZ);
    if (ret)
    {
        printk("clk_set_rate error \r\n");
    }
    return ret;
}


static int rk33_clk_set_dvfs_node(struct dvfs_node *node, unsigned long rate)
{
    int ret = 0;
    if (!node)
    {
        printk("rk33_clk_set_dvfs_node error \r\n");
        ret = -1;
    }
    ret = dvfs_clk_set_rate(node, rate * ONE_MHZ);
    if (ret)
    {
        printk("dvfs_clk_set_rate error \r\n");
    }
    return ret;
}
#if RK33_DVFS_SUPPORT
#define dividend 7
#define fix_float(a) ((((a)*dividend)%10)?((((a)*dividend)/10)+1):(((a)*dividend)/10))
static IMG_BOOL calculate_dvfs_max_min_threshold(IMG_UINT32 level)
{
    IMG_UINT32 pre_level;
    IMG_UINT32 tmp ;

    if (0 == level)
    {
        if ((RGX_DVFS_STEP - 1) == level)
        {
            rgx_dvfs_infotbl[level].min_threshold = level0_threshold_min;
            rgx_dvfs_infotbl[level].max_threshold = levelf_threshold_max;
        }
        else
        {
            rgx_dvfs_infotbl[level].min_threshold = level0_threshold_min;
            rgx_dvfs_infotbl[level].max_threshold = level0_threshold_max;
        }
#if RK33_USE_CL_COUNT_UTILS
        rgx_dvfs_infotbl[level].coef = level0_coef_max;
#endif
    }
    else
    {
        pre_level = level - 1;
        if ((RGX_DVFS_STEP - 1) == level)
        {
            rgx_dvfs_infotbl[level].max_threshold = levelf_threshold_max;
        }
        else
        {
            rgx_dvfs_infotbl[level].max_threshold = rgx_dvfs_infotbl[pre_level].max_threshold + div_dvfs;
        }
        rgx_dvfs_infotbl[level].min_threshold = (rgx_dvfs_infotbl[pre_level].max_threshold * (rgx_dvfs_infotbl[pre_level].clock))
                                                / (rgx_dvfs_infotbl[level].clock);

        tmp = rgx_dvfs_infotbl[level].max_threshold - rgx_dvfs_infotbl[level].min_threshold;

        rgx_dvfs_infotbl[level].min_threshold += fix_float(tmp);
#if RK33_USE_CL_COUNT_UTILS
        rgx_dvfs_infotbl[level].coef = (rgx_dvfs_infotbl[pre_level].clock * rgx_dvfs_infotbl[pre_level].coef + 2000)
                                       / (rgx_dvfs_infotbl[level].clock);
#endif
    }

#if 1
    printk("rgx_dvfs_infotbl[%d].clock=%d,min_threshold=%d,max_threshold=%d,coef=%d\n", level,
           rgx_dvfs_infotbl[level].clock,
           rgx_dvfs_infotbl[level].min_threshold,
           rgx_dvfs_infotbl[level].max_threshold,
           rgx_dvfs_infotbl[level].coef
          );
#endif
    return IMG_TRUE;
}

static void rk33_dvfs_set_clock(int freq)
{
    struct rk_context       *platform;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (NULL == platform)
        panic("oops");

    if ( !platform->aclk_gpu_mem || !platform->aclk_gpu_cfg || !platform->dvfs_enabled)
    {
        printk("aclk_gpu_mem or aclk_gpu_cfg not init\n");
        return;
    }
    //mutex_lock(&rgx_set_clock_lock);
    rk33_clk_set_normal_node(platform->aclk_gpu_mem, freq);
    rk33_clk_set_normal_node(platform->aclk_gpu_cfg, freq);

    if(platform->gpu_clk_node)
        rk33_clk_set_dvfs_node(platform->gpu_clk_node, freq);
    else if(platform->clk_gpu)
        rk33_clk_set_normal_node(platform->clk_gpu, freq);

    //mutex_unlock(&rgx_set_clock_lock);
    return;
}

#if RK33_DVFS_FREQ_LIMIT
static int rk33_dvfs_get_freq(int level)
{
    if (WARN_ON((level >= RGX_DVFS_STEP) || (level < 0)))
    {
        printk("unkown rgx dvfs level:level = %d,set clock not done \n", level);
        return  -1;
    }
    return (rgx_dvfs_infotbl[level].clock);
}
#endif

static int rk33_dvfs_get_level(int freq)
{
    int i;
    for (i = 0; i < RGX_DVFS_STEP; i++)
    {
        if (rgx_dvfs_infotbl[i].clock == freq)
            return i;
    }
    return -1;
}

static void rk33_dvfs_set_level(int level)
{
    static int prev_level = -1;

    if (level == prev_level)
        return;

    if (WARN_ON((level >= RGX_DVFS_STEP) || (level < 0)))
    {
        printk("unkown rgx dvfs level:level = %d,set clock not done \n", level);
        return  ;
    }
    /*panic("invalid level");*/
#if 0
    if (mali_dvfs_status_current.upper_lock >= 0 && level > mali_dvfs_status_current.upper_lock)
        level = mali_dvfs_status_current.upper_lock;
    if (mali_dvfs_status_current.under_lock >= 0 && level < mali_dvfs_status_current.under_lock)
        level = mali_dvfs_status_current.under_lock;
#endif
    rk33_dvfs_set_clock(rgx_dvfs_infotbl[level].clock);
#if 0
    update_time_in_state(prev_level);
#endif
    prev_level = level;
}

static int rk33_dvfs_get_enable_status(void)
{
    struct rk_context       *platform;
    unsigned long           flags;
    int                     enable;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    spin_lock_irqsave(&platform->timer_lock, flags);
    enable = platform->timer_active;
    spin_unlock_irqrestore(&platform->timer_lock, flags);

    return enable;
}

#if 0
static int rk33_dvfs_enable(bool enable, int freq)
{
    struct rk_context       *platform;
    unsigned long flags;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    BUG_ON(platform == NULL);

    mutex_lock(&rgx_enable_clock_lock);

    if (enable != platform->timer_active)
    {
        if (enable)
        {
            spin_lock_irqsave(&platform->timer_lock, flags);
            platform->timer_active = IMG_TRUE;
            spin_unlock_irqrestore(&platform->timer_lock, flags);
            hrtimer_start(&platform->timer,
                          HR_TIMER_DELAY_MSEC(RK33_DEFAULT_CLOCK),
                          HRTIMER_MODE_REL);
        }
        else
        {
            spin_lock_irqsave(&platform->timer_lock, flags);
            platform->timer_active = IMG_FALSE;
            spin_unlock_irqrestore(&platform->timer_lock, flags);
            hrtimer_cancel(&platform->timer);
        }
    }

    if (freq != RGX_DVFS_CURRENT_FREQ)
    {
        spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
        platform->time_tick = 0;
        platform->time_busy = 0;
        platform->time_idle = 0;
        platform->utilisation = 0;
        platform->freq_level = rk33_dvfs_get_level(freq);
        spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
        rk33_dvfs_set_level(platform->freq_level);
    }

    mutex_unlock(&rgx_enable_clock_lock);

    return IMG_TRUE;
}
#endif


#if RK33_USE_CL_COUNT_UTILS
static void rk33_dvfs_event_proc(struct work_struct *w)
{
    unsigned long flags;
    static IMG_UINT32 temp_tmp;
    IMG_UINT32 fps = 0;
    IMG_UINT32 fps_limit;
    IMG_UINT32 policy;
    IMG_INT32 absload;
    IMG_INT32 new_index;
    struct rk_context *platform;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    mutex_lock(&rgx_enable_clock_lock);

    if (!rk33_dvfs_get_enable_status())
    {
        mutex_unlock(&rgx_enable_clock_lock);
        return;
    }

    fps = rk_get_real_fps(0);

    platform->temperature_time++;
    /*
    temp_tmp += rockchip_tsadc_get_temp(2);
    */
    if (platform->temperature_time >= gpu_temp_statis_time)
    {
        platform->temperature_time = 0;
        platform->temperature = temp_tmp / gpu_temp_statis_time;
        temp_tmp = 0;
        /*pr_info("platform->temperature = %d\n",platform->temperature);*/
    }

    spin_lock_irqsave(&rgx_dvfs_spinlock, flags);

    platform->abs_load[0] = platform->abs_load[1];
    platform->abs_load[1] = platform->abs_load[2];
    platform->abs_load[2] = platform->abs_load[3];
    platform->abs_load[3] = (platform->utilisation * rgx_dvfs_infotbl[platform->freq_level].clock * rgx_dvfs_infotbl[platform->freq_level].coef) / 100;
    absload = (platform->abs_load[3] * 4 + platform->abs_load[2] * 3 + platform->abs_load[1] * 2 + platform->abs_load[0]);

    /*
    policy = rockchip_pm_get_policy();
    */
    policy = ROCKCHIP_PM_POLICY_NORMAL;

    if (ROCKCHIP_PM_POLICY_PERFORMANCE == policy)
    {
        platform->freq_level = RGX_DVFS_STEP - 1; /*Highest level when performance mode*/
    }
    else if (platform->fix_freq > 0)
    {
        platform->freq_level = rk33_dvfs_get_level(platform->fix_freq);

        if (platform->debug_level == DBG_HIGH)
            printk("fix clock=%d\n", platform->fix_freq);
    }
    else
    {
        fps_limit = (ROCKCHIP_PM_POLICY_NORMAL == policy) ? LIMIT_FPS : LIMIT_FPS_POWER_SAVE;
        /*
        printk("policy : %d , fps_limit = %d\n",policy,fps_limit);
        */


        /*give priority to temperature unless in performance mode */
        if (platform->temperature > gpu_temp_limit)
        {
            if (platform->freq_level > 0)
                platform->freq_level--;

            if (gpu_temp_statis_time > 1)
                platform->temperature = 0;
        }
        else if (absload == 0 || platform->gpu_active == IMG_FALSE)
        {
            platform->freq_level = 0;
        }
        else if ((platform->freq_level < RGX_DVFS_STEP - 1) && fps < fps_limit)
        {
            //freq_hint=0 or freq_hint>sRK30_DVFS.u8FreqNums,  select freq automatically, find the right index
            for (new_index = 0; new_index < RGX_DVFS_STEP; new_index++)
            {
                if (absload <= (rgx_dvfs_infotbl[new_index].clock*rgx_dvfs_infotbl[new_index].coef*9))
                {
                    if (platform->debug_level == DBG_HIGH)
                        printk("absload=%d,cur_coef[%d]=%d\n", absload, new_index, rgx_dvfs_infotbl[new_index].clock*rgx_dvfs_infotbl[new_index].coef*9);
                    break;
                }
            }

            //ensure the new_index in the reasonable range
            if (new_index >= RGX_DVFS_STEP)
            {
                new_index = RGX_DVFS_STEP - 1;
            }

            //if fps>=50, should not run at the higher frequency
            if (new_index > platform->freq_level && fps >= fps_limit)
            {
                new_index = platform->freq_level;
            }
            //if running at highest frequency & fps>53 & absload<90%, try to run at a lower frequency
            else if (platform->freq_level == RGX_DVFS_STEP - 1 && fps > 53 && absload <= (rgx_dvfs_infotbl[new_index].clock*rgx_dvfs_infotbl[new_index].coef*9))
            {
                new_index = platform->freq_level - 1;
            }
            if (platform->debug_level == DBG_HIGH)
                printk("absload=%d,freq_level=%d,freq=%dM\n", absload, new_index, rgx_dvfs_infotbl[new_index].clock);

            platform->freq_level = new_index;
        }
    }
#if RK33_SYSFS_FILE_SUPPORT && RK33_DVFS_FREQ_LIMIT
    if ((platform->up_level >= 0) && (platform->freq_level > platform->up_level))
    {
        platform->freq_level = platform->up_level;
    }

    if ((platform->down_level >= 0) && (platform->freq_level < platform->down_level))
    {
        platform->freq_level = platform->down_level;
    }
#endif

    spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

    rk33_dvfs_set_level(platform->freq_level);

    platform->time_busy = 0;
    platform->time_idle = 0;
    platform->utilisation = 0;

    mutex_unlock(&rgx_enable_clock_lock);
}
#else
static void rk33_dvfs_event_proc(struct work_struct *w)
{
    unsigned long flags;
    static int level_down_time = 0;
    static int level_up_time = 0;
    static IMG_UINT32 temp_tmp;
    IMG_UINT32 fps = 0;
    IMG_UINT32 fps_limit;
    IMG_UINT32 policy;
    struct rk_context *platform;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    mutex_lock(&rgx_enable_clock_lock);

    if (!rk33_dvfs_get_enable_status())
    {
        mutex_unlock(&rgx_enable_clock_lock);
        return;
    }

    fps = rk_get_real_fps(0);

    platform->temperature_time++;
    /*
    temp_tmp += rockchip_tsadc_get_temp(2);
    */
    if (platform->temperature_time >= gpu_temp_statis_time)
    {
        platform->temperature_time = 0;
        platform->temperature = temp_tmp / gpu_temp_statis_time;
        temp_tmp = 0;
        /*pr_info("platform->temperature = %d\n",platform->temperature);*/
    }

    spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
    /*
    policy = rockchip_pm_get_policy();
    */
    policy = ROCKCHIP_PM_POLICY_NORMAL;

    if (ROCKCHIP_PM_POLICY_PERFORMANCE == policy)
    {
        platform->freq_level = RGX_DVFS_STEP - 1; /*Highest level when performance mode*/
    }
    else if (platform->fix_freq > 0)
    {
        platform->freq_level = rk33_dvfs_get_level(platform->fix_freq);

        if (platform->debug_level == DBG_HIGH)
            printk("fix clock=%d\n", platform->fix_freq);
    }
    else
    {
        fps_limit = (ROCKCHIP_PM_POLICY_NORMAL == policy) ? LIMIT_FPS : LIMIT_FPS_POWER_SAVE;
#if 0
        printk("policy : %d , fps_limit = %d\n", policy, fps_limit);
#endif

        /*give priority to temperature unless in performance mode */
        if (platform->temperature > gpu_temp_limit)
        {
            if (platform->freq_level > 0)
                platform->freq_level--;

            if (gpu_temp_statis_time > 1)
                platform->temperature = 0;
        }
        else if ((platform->utilisation > rgx_dvfs_infotbl[platform->freq_level].max_threshold) && (platform->freq_level < RGX_DVFS_STEP - 1) && fps < fps_limit)
        {
            level_up_time++;
            if (level_up_time == RGX_DVFS_LEVEL_INTERVAL)
            {
                if (platform->debug_level == DBG_HIGH)
                    printk("up,utilisation=%d,current clock=%d,fps = %d\n", platform->utilisation, rgx_dvfs_infotbl[platform->freq_level].clock, fps);

                platform->freq_level++;
                level_up_time = 0;

                if (platform->debug_level == DBG_HIGH)
                    printk(" next clock=%d\n", rgx_dvfs_infotbl[platform->freq_level].clock);

                BUG_ON(platform->freq_level >= RGX_DVFS_STEP);
            }
            level_down_time = 0;
        }
        else if ((platform->freq_level > 0) && (platform->utilisation < rgx_dvfs_infotbl[platform->freq_level].min_threshold))
            /*else if((dvfs_status->step > 0) && (platform->time_tick == RGX_DVFS_LEVEL_INTERVAL) && (platform->utilisation < mali_dvfs_infotbl[dvfs_status->step].min_threshold)) */
        {
            level_down_time++;
            if (level_down_time == RGX_DVFS_LEVEL_INTERVAL)
            {
                if (platform->debug_level == DBG_HIGH)
                    printk("down,utilisation=%d,current clock=%d,fps = %d\n", platform->utilisation, rgx_dvfs_infotbl[platform->freq_level].clock, fps);

                BUG_ON(platform->freq_level <= 0);
                platform->freq_level--;
                level_down_time = 0;

                if (platform->debug_level == DBG_HIGH)
                    printk(" next clock=%d\n", rgx_dvfs_infotbl[platform->freq_level].clock);
            }
            level_up_time = 0;
        }
        else
        {
            level_down_time = 0;
            level_up_time = 0;

            if (platform->debug_level == DBG_HIGH)
                printk("keep,utilisation=%d,current clock=%d,fps = %d\n", platform->utilisation, rgx_dvfs_infotbl[platform->freq_level].clock, fps);
        }
    }
#if RK33_SYSFS_FILE_SUPPORT && RK33_DVFS_FREQ_LIMIT
    if ((platform->up_level >= 0) && (platform->freq_level > platform->up_level))
    {
        platform->freq_level = platform->up_level;
    }

    if ((platform->down_level >= 0) && (platform->freq_level < platform->down_level))
    {
        platform->freq_level = platform->down_level;
    }
#endif
    spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

    rk33_dvfs_set_level(platform->freq_level);

    platform->time_busy = 0;
    platform->time_idle = 0;
    platform->utilisation = 0;

    mutex_unlock(&rgx_enable_clock_lock);
}
#endif
static DECLARE_WORK(rgx_dvfs_work, rk33_dvfs_event_proc);

static IMG_BOOL rk33_dvfs_event(RGXFWIF_GPU_UTIL_STATS* psUtilStats)
{
    struct rk_context       *platform;
#if !RK33_USE_CUSTOMER_GET_GPU_UTIL
    IMG_UINT32              time_busy = 0;
    IMG_UINT32              time_idle = 0;
#endif
    unsigned long           flags;
#if RK33_USE_CUSTOMER_GET_GPU_UTIL
    ktime_t now = ktime_get();
    ktime_t diff;
    IMG_INT i;
#endif

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    PVR_ASSERT(platform != NULL);

#if RK33_USE_RGX_GET_GPU_UTIL
    PVR_ASSERT(psUtilStats != NULL);

    if (platform->debug_level == DBG_HIGH)
        printk("GPU util info:valid[%d],bIncompleteData[%d],ActiveHigh[%d],ActiveLow[%d],Blocked[%d],Idle[%d]\n",
               psUtilStats->bValid, psUtilStats->bIncompleteData,
               psUtilStats->ui32GpuStatActiveHigh, psUtilStats->ui32GpuStatActiveLow,
               psUtilStats->ui32GpuStatBlocked, psUtilStats->ui32GpuStatIdle);

    if (psUtilStats->bValid /*&& psUtilStats->bIncompleteData*/)
    {
        time_busy = psUtilStats->ui32GpuStatActiveHigh + psUtilStats->ui32GpuStatActiveLow ;
        time_idle = psUtilStats->ui32GpuStatIdle + psUtilStats->ui32GpuStatBlocked;

        //check valid
        if (time_busy + time_idle == 0)
        {
            goto err;
        }

        spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
        platform->time_busy += time_busy;
        platform->time_idle += time_idle;
        platform->utilisation = (time_busy * 100) / (time_busy + time_idle);
        spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
        queue_work_on(0, rgx_dvfs_wq, &rgx_dvfs_work);
    }
#elif RK33_USE_CUSTOMER_GET_GPU_UTIL

    diff = ktime_sub(now, platform->time_period_start);

    if (platform->gpu_active)
    {
        platform->time_busy += (IMG_UINT32)(ktime_to_ns(diff) >> RK_PM_TIME_SHIFT);
        platform->time_period_start = now;
    }
    else
    {
        platform->time_idle += (IMG_UINT32)(ktime_to_ns(diff) >> RK_PM_TIME_SHIFT);
        platform->time_period_start = now;
    }


    //check valid
    if (platform->time_busy + platform->time_idle == 0)
    {
        goto err;
    }

    spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
    for(i=0;i<RK33_MAX_UTILIS-1;i++)
    {
        platform->stUtilis.time_busys[i]=platform->stUtilis.time_busys[i+1];
        platform->stUtilis.time_idles[i]=platform->stUtilis.time_idles[i+1];
        platform->stUtilis.utilis[i]=platform->stUtilis.utilis[i+1];
    }
    platform->stUtilis.time_busys[RK33_MAX_UTILIS-1] = platform->time_busy;
    platform->stUtilis.time_idles[RK33_MAX_UTILIS-1] = platform->time_idle;
    platform->stUtilis.utilis[RK33_MAX_UTILIS-1] = (platform->time_busy * 10) / (platform->time_busy + platform->time_idle);
    platform->utilisation = platform->stUtilis.utilis[3] * 4 + platform->stUtilis.utilis[2] * 3 +\
                            platform->stUtilis.utilis[1] * 2 + platform->stUtilis.utilis[0] * 1;
    spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

    if (platform->debug_level == DBG_HIGH)
        printk("GPU util info:time_busy=%d,time_idle=%d,utilisation=%d\n",platform->time_busy,platform->time_idle,platform->utilisation);

    queue_work_on(0, rgx_dvfs_wq, &rgx_dvfs_work);
#endif

    return IMG_TRUE;

err:

    platform->time_busy = 0;
    platform->time_idle = 0;
    platform->utilisation = 0;

    return IMG_FALSE;
}

#if USE_HRTIMER
static enum hrtimer_restart dvfs_callback(struct hrtimer *timer)
{
    unsigned long           flags;
    struct rk_context       *platform;
    PVRSRV_RGXDEV_INFO  *psDevInfo;

    PVR_ASSERT(timer != NULL);

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    PVR_ASSERT(platform != NULL);

#if 0
    if (platform->psDeviceNode == NULL)
    {
        //Fix dvfs_callback crash problem when psDeviceNode hasn't been set.
        PVR_DPF((PVR_DBG_WARNING, "%s:line=%d, DeviceNode is null", __func__, __LINE__));
        spin_lock_irqsave(&platform->timer_lock, flags);
        if (platform->timer_active)
            hrtimer_start(timer,
                          HR_TIMER_DELAY_MSEC(10*RK33_DVFS_FREQ),  //delay 500ms
                          HRTIMER_MODE_REL);
        spin_unlock_irqrestore(&platform->timer_lock, flags);
    }
    else
#endif
    spin_lock_irqsave(&platform->timer_lock, flags);

    if(platform->psDeviceNode)
    {
        psDevInfo = platform->psDeviceNode->pvDevice;

        if(psDevInfo && psDevInfo->pfnGetGpuUtilStats && platform->gpu_active)
        {
            //Measuring GPU Utilisation
            platform->sUtilStats = ((psDevInfo->pfnGetGpuUtilStats)(platform->psDeviceNode));
            rk33_dvfs_event(&platform->sUtilStats);
        }
        else
        {
            if(!psDevInfo || !psDevInfo->pfnGetGpuUtilStats)
                PVR_DPF((PVR_DBG_ERROR,"%s:line=%d,devinfo is null\n",__func__,__LINE__));
        }
    }

    if (platform->timer_active)
        hrtimer_start(timer,
                      HR_TIMER_DELAY_MSEC(RK33_DVFS_FREQ),
                      HRTIMER_MODE_REL);
    spin_unlock_irqrestore(&platform->timer_lock, flags);

    return HRTIMER_NORESTART;
}
#elif USE_KTHREAD
static int gpu_dvfs_task(void *data)
{
	static long sTimeout=msecs_to_jiffies(RK33_DVFS_FREQ);
	long timeout = sTimeout;
	unsigned long           flags;
    struct rk_context *platform=data;
    PVRSRV_RGXDEV_INFO  *psDevInfo;

    PVR_ASSERT(platform != NULL);

	set_freezable();

	do {
	        if(platform->psDeviceNode)
	        {
                psDevInfo = platform->psDeviceNode->pvDevice;
                spin_lock_irqsave(&platform->timer_lock, flags);
                if(psDevInfo && psDevInfo->pfnGetGpuUtilStats && platform->gpu_active)
                {
                    //Measuring GPU Utilisation
                    platform->sUtilStats = ((psDevInfo->pfnGetGpuUtilStats)(platform->psDeviceNode));
                    rk33_dvfs_event(&platform->sUtilStats);
                }
                else
                {
                    if(!psDevInfo || !psDevInfo->pfnGetGpuUtilStats)
                        PVR_DPF((PVR_DBG_ERROR,"%s:line=%d,devinfo is null\n",__func__,__LINE__));
                }
                spin_unlock_irqrestore(&platform->timer_lock, flags);
            }
		wait_event_freezable_timeout(platform->dvfs_wait, kthread_should_stop(), timeout);
	} while (!kthread_should_stop());

	return 0;
}
#endif

static IMG_VOID rk33_dvfs_utils_init(struct rk_context *platform)
{
#if USE_KTHREAD
    int iRet=-1;
    struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
#endif
#if RK33_USE_CUSTOMER_GET_GPU_UTIL
    IMG_INT i;
#endif
    PVR_ASSERT(platform != NULL);

// spin_lock_irqsave(&rgx_dvfs_spinlock, flags);

    platform->utilisation = 0;
    platform->freq_level = 0;
    platform->freq = 0;
    platform->time_tick = 0;
#if RK33_USE_CUSTOMER_GET_GPU_UTIL
    platform->time_period_start = ktime_get();
    for(i=0;i<RK33_MAX_UTILIS;i++)
    {
        platform->stUtilis.time_busys[i]=0;
        platform->stUtilis.time_idles[i]=0;
        platform->stUtilis.utilis[i]=0;
    }
#endif
    platform->gpu_active = IMG_FALSE;
    platform->time_busy = 0;
    platform->time_idle = 0;
    platform->temperature = 0;
    platform->temperature_time = 0;
    platform->timer_active = IMG_FALSE;

#if RK33_USE_CL_COUNT_UTILS
    platform->abs_load[0] = platform->abs_load[1] = platform->abs_load[2] = platform->abs_load[3] = 0;
#endif
#if RK33_SYSFS_FILE_SUPPORT
    platform->debug_level = DBG_OFF;
    platform->fix_freq = 0;
    platform->fps_gap = FPS_DEFAULT_GAP;
#if RK33_DVFS_FREQ_LIMIT
    platform->up_level = -1;
    platform->down_level = -1;
#endif //RK33_DVFS_FREQ_LIMIT
#endif //RK33_SYSFS_FILE_SUPPORT


#if USE_HRTIMER
    {
        hrtimer_init(&platform->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    }
    platform->timer.function = dvfs_callback;
#endif

#if USE_KTHREAD
    platform->dvfs_task = kthread_create(gpu_dvfs_task, platform, "GpuDvfsD");
	if (IS_ERR(platform->dvfs_task)) {
		iRet = PTR_ERR(platform->dvfs_task);
		PVR_DPF((PVR_DBG_ERROR, "failed to create kthread! error %d\n", iRet));
        return;
	}

	sched_setscheduler_nocheck(platform->dvfs_task, SCHED_FIFO, &param);
	get_task_struct(platform->dvfs_task);
	kthread_bind(platform->dvfs_task, 0);

    init_waitqueue_head(&platform->dvfs_wait);

#endif
    //spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
}

static void rk33_dvfs_utils_term(struct rk_context *platform)
{
    unsigned long flags;

    PVR_ASSERT(platform != NULL);


    if(platform->timer_active)
    {
        spin_lock_irqsave(&platform->timer_lock, flags);
        platform->timer_active = IMG_FALSE;
        spin_unlock_irqrestore(&platform->timer_lock, flags);
#if USE_HRTIMER
        hrtimer_cancel(&platform->timer);
#elif USE_KTHREAD
        kthread_stop(platform->dvfs_task);
#endif
    }
}

#if RK33_USE_CUSTOMER_GET_GPU_UTIL
/*caller needs to hold kbdev->pm.metrics.lock before calling this function*/
static void rk33_dvfs_record_busy_utils(struct rk_context *platform)
{
    ktime_t now;
    ktime_t diff;
    IMG_UINT32 ns_time;

    PVR_ASSERT(platform != NULL);

    now = ktime_get();
    diff = ktime_sub(now, platform->time_period_start);

    ns_time = (IMG_UINT32)(ktime_to_ns(diff) >> RK_PM_TIME_SHIFT);
    platform->time_busy += ns_time;
    platform->time_period_start = now;
}

static void rk33_dvfs_record_idle_utils(struct rk_context *platform)
{
    ktime_t now;
    ktime_t diff;
    IMG_UINT32 ns_time;

    PVR_ASSERT(platform != NULL);

    now = ktime_get();
    diff = ktime_sub(now, platform->time_period_start);

    ns_time = (IMG_UINT32)(ktime_to_ns(diff) >> RK_PM_TIME_SHIFT);
    platform->time_idle += ns_time;
    platform->time_period_start = now;
}
#endif

static void rk33_dvfs_record_gpu_idle(struct rk_context *platform)
{
    unsigned long flags;
    PVR_ASSERT(platform != NULL);

    if(!platform->gpu_active)
        return;

    spin_lock_irqsave(&platform->timer_lock, flags);

    //PVR_ASSERT(platform->gpu_active == IMG_TRUE);

    platform->gpu_active = IMG_FALSE;

#if RK33_USE_CUSTOMER_GET_GPU_UTIL
    rk33_dvfs_record_busy_utils(platform);
#endif

    spin_unlock_irqrestore(&platform->timer_lock, flags);
}


static void rk33_dvfs_record_gpu_active(struct rk_context *platform)
{
    unsigned long flags;

    PVR_ASSERT(platform != NULL);

    if(platform->gpu_active)
        return;

    spin_lock_irqsave(&platform->timer_lock, flags);

    //PVR_ASSERT(platform->gpu_active == IMG_FALSE);

    platform->gpu_active = IMG_TRUE;

#if RK33_USE_CUSTOMER_GET_GPU_UTIL
    rk33_dvfs_record_idle_utils(platform);
#endif

    spin_unlock_irqrestore(&platform->timer_lock, flags);
}

static IMG_BOOL rk33_dvfs_get_freq_table(struct rk_context *platform)
{
    IMG_INT i;

    PVR_ASSERT(platform != NULL);

    //get freq table
    rgx_freq_table = dvfs_get_freq_volt_table(platform->gpu_clk_node);

    if (rgx_freq_table == NULL)
    {
        printk("rgx freq table not assigned yet,use default\n");
        return IMG_FALSE ;
    }
    else
    {
        /*recalculte step*/
        RGX_DVFS_STEP = 0;

        PVR_DPF((PVR_DBG_WARNING, "The raw GPU freq_table:"));
        for (i = 0; rgx_freq_table[i].frequency != CPUFREQ_TABLE_END; i++)
        {
            rgx_dvfs_infotbl[i].clock = rgx_freq_table[i].frequency / ONE_KHZ;
            PVR_DPF((PVR_DBG_WARNING, "%dM,", rgx_dvfs_infotbl[i].clock));
            RGX_DVFS_STEP++;
        }

        if (RGX_DVFS_STEP > 1)
            div_dvfs = round_up(((levelf_threshold_max - level0_threshold_max) / (RGX_DVFS_STEP - 1)), 1);

        PVR_DPF((PVR_DBG_WARNING, "RGX_DVFS_STEP=%d,div_dvfs=%d\n", RGX_DVFS_STEP, div_dvfs));

        for (i = 0;i < RGX_DVFS_STEP;i++)
        {
            calculate_dvfs_max_min_threshold(i);
        }
        p_rgx_dvfs_infotbl = rgx_dvfs_infotbl;
    }

    return IMG_TRUE;
}

IMG_BOOL rk33_dvfs_init(IMG_VOID)
{
    struct rk_context *platform;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (IMG_FALSE == rk33_dvfs_get_freq_table(platform))
        return IMG_FALSE;

    if (!rgx_dvfs_wq)
        rgx_dvfs_wq = create_singlethread_workqueue("rgx_dvfs");

    spin_lock_init(&rgx_dvfs_spinlock);
   // mutex_init(&rgx_set_clock_lock);
    mutex_init(&rgx_enable_clock_lock);

    rk33_dvfs_utils_init(platform);

    return IMG_TRUE;
}


IMG_VOID rk33_dvfs_term(IMG_VOID)
{
    struct rk_context *platform;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    PVR_ASSERT(platform != NULL);

    rk33_dvfs_utils_term(platform);

    if (rgx_dvfs_wq)
        destroy_workqueue(rgx_dvfs_wq);

    rgx_dvfs_wq = NULL;
}

#if RK33_SYSFS_FILE_SUPPORT
#if RK33_DVFS_FREQ_LIMIT
static int rk33_dvfs_up_limit_on(int level)
{
    struct rk_context *platform;
    unsigned long flags;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (!platform || level < 0)
        return -ENODEV;

    spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
    if (platform->down_level >= 0 &&
            platform->down_level > level)
    {
        PVR_DPF((PVR_DBG_ERROR, " rk33_dvfs_up_limit_on : Attempting to set upper lock (%d) to below under lock(%d)\n", level, platform->down_level));
        spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
        return -1;
    }
    platform->up_level = level;
    spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

    PVR_DPF((PVR_DBG_WARNING, " Up Level Set : %d\n", level));

    return 0;
}

static int rk33_dvfs_up_limit_off(void)
{
    struct rk_context *platform;
    unsigned long flags;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (!platform)
        return -ENODEV;

    spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
    platform->up_level = -1;
    spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

    PVR_DPF((PVR_DBG_WARNING, "Up Level Unset\n"));

    return 0;
}

static int rk33_dvfs_down_limit_on(int level)
{
    struct rk_context *platform;
    unsigned long flags;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (!platform || level < 0)
        return -ENODEV;

    spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
    if (platform->up_level >= 0 &&
            platform->up_level < level)
    {
        PVR_DPF((PVR_DBG_ERROR, " rk33_dvfs_down_limit_on : Attempting to set under lock (%d) to above upper lock(%d)\n", level, platform->up_level));
        spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
        return -1;
    }
    platform->down_level = level;
    spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

    PVR_DPF((PVR_DBG_WARNING, " Down Level Set : %d\n", level));

    return 0;
}

static int rk33_dvfs_down_limit_off(void)
{
    struct rk_context *platform;
    unsigned long flags;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (!platform)
        return -ENODEV;

    spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
    platform->down_level = -1;
    spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

    PVR_DPF((PVR_DBG_WARNING, "Up Level Unset\n"));

    return 0;
}

static int rk33_dvfs_get_dvfs_up_limit_freq(void)
{
    struct rk_context *platform;
    unsigned long flags;
    int up_level = -1;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (!platform)
        return -ENODEV;

    spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
    up_level = rk33_dvfs_get_freq(platform->up_level);
    spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

    return up_level;
}

static int rk33_dvfs_get_dvfs_down_limit_freq(void)
{
    struct rk_context *platform;
    unsigned long flags;
    int down_level = -1;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (!platform)
        return -ENODEV;

    spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
    down_level = rk33_dvfs_get_freq(platform->down_level);
    spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

    return down_level;
}
#endif //end of RK33_DVFS_FREQ_LIMIT

static ssize_t show_freq(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct rk_context *platform;
    ssize_t ret = 0;
    unsigned int clkrate;
    int i ;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (!platform)
        return -ENODEV;

    if (platform->debug_level > DBG_OFF)
    {
#if RK33_DVFS_SUPPORT
        if (!platform->gpu_clk_node)
        {
            PVR_DPF((PVR_DBG_ERROR, "gpu_clk_node not init!"));
            return -ENODEV;
        }
        clkrate = dvfs_clk_get_rate(platform->gpu_clk_node);
#else
        if (!platform->aclk_gpu_mem)
        {
            PVR_DPF((PVR_DBG_ERROR, "aclk_gpu_mem not init!"));
            return -ENODEV;
        }
        clkrate = clk_get_rate(platform->aclk_gpu_mem);
#endif
        if (platform->dvfs_enabled)
        {
            ret += snprintf(buf + ret, PAGE_SIZE - ret, "DVFS is on");

            ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nCurrent clk rgx = %dMhz", clkrate / ONE_MHZ);
            /* To be revised  */
            ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings:");
            for (i = 0;i < RGX_DVFS_STEP;i++)
                ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ", p_rgx_dvfs_infotbl[i].clock);
            ret += snprintf(buf + ret, PAGE_SIZE - ret, "Mhz");
        }
        else
            ret += snprintf(buf + ret, PAGE_SIZE - ret, "DVFS is off");

        if (ret < PAGE_SIZE - 1)
            ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
        else
        {
            buf[PAGE_SIZE - 2] = '\n';
            buf[PAGE_SIZE - 1] = '\0';
            ret = PAGE_SIZE - 1;
        }
    }
    return ret;
}

static ssize_t set_freq(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rk_context *platform;
    unsigned int tmp = 0, freq = 0;
    tmp = 0;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);
    if (!platform)
        return -ENODEV;

    if (!platform->gpu_clk_node)
        return -ENODEV;
#if 0
    if (sysfs_streq("500", buf))
    {
        freq = 500;
    }
    else if (sysfs_streq("400", buf))
    {
        freq = 400;
    }
    else if (sysfs_streq("350", buf))
    {
        freq = 350;
    }
    else if (sysfs_streq("266", buf))
    {
        freq = 266;
    }
    else if (sysfs_streq("160", buf))
    {
        freq = 160;
    }
    else if (sysfs_streq("100", buf))
    {
        freq = 100;
    }
    else
    {
        dev_err(dev, "set_clock: invalid value\n");
        return -ENOENT;
    }
#endif

    if (memcmp(buf, "debug_hi", 8) == 0)
    {
        platform->debug_level = DBG_HIGH;
        return (count);
    }
    if (memcmp(buf, "debug_lo", 8) == 0)
    {
        platform->debug_level = DBG_LOW;
        return (count);
    }
    else if (memcmp(buf, "debug_off", 9) == 0)
    {
        platform->debug_level = DBG_OFF;
        return (count);
    }

// freq = simple_strtoul(buf, NULL, 10);
    if (sscanf(buf, "%i", &freq) == 0)
    {
        PVR_DPF((PVR_DBG_ERROR, "invalid value"));
        return -EINVAL;
    }
    else
    {
        if (rk33_dvfs_get_level(freq) >= RGX_DVFS_STEP || rk33_dvfs_get_level(freq) < 0)
        {
            PVR_DPF((PVR_DBG_ERROR, "invalid freq(%d)", freq));
            platform->fix_freq = 0; //open dvfs
            return count;
        }
    }

    rk33_dvfs_set_level(rk33_dvfs_get_level(freq));

    platform->fix_freq = freq;  //close dvfs

    return count;
}

static ssize_t show_utilisation(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct rk_context *platform;
    PVRSRV_RGXDEV_INFO  *psDevInfo;
    IMG_INT         utilisation;
    IMG_UINT32      time_busy;
    IMG_UINT32      time_idle;
    ssize_t ret = 0;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);
    if (!platform || !platform->psDeviceNode)
        return -ENODEV;

    if (platform->debug_level > DBG_OFF)
    {
        psDevInfo = platform->psDeviceNode->pvDevice;

        //Measuring GPU Utilisation
        platform->sUtilStats = ((psDevInfo->pfnGetGpuUtilStats)(platform->psDeviceNode));
        time_busy = platform->sUtilStats.ui32GpuStatActiveHigh + platform->sUtilStats.ui32GpuStatActiveLow;
        time_idle = platform->sUtilStats.ui32GpuStatIdle + platform->sUtilStats.ui32GpuStatBlocked;
        utilisation = (time_busy * 100) / (time_busy + time_idle);

        ret += snprintf(buf + ret, PAGE_SIZE - ret, "Utilisation=%d", utilisation);
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nDetail: ActiveHigh=%d,ActiveLow=%d,Blocked=%d,Idle=%d", \
                        platform->sUtilStats.ui32GpuStatActiveHigh, platform->sUtilStats.ui32GpuStatActiveLow, platform->sUtilStats.ui32GpuStatBlocked, platform->sUtilStats.ui32GpuStatIdle);

        if (ret < PAGE_SIZE - 1)
            ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
        else
        {
            buf[PAGE_SIZE - 2] = '\n';
            buf[PAGE_SIZE - 1] = '\0';
            ret = PAGE_SIZE - 1;
        }
    }
    return ret;
}

static ssize_t set_utilisation(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int utilisation = 0;
    utilisation = simple_strtoul(buf, NULL, 10);

    return count;
}

static ssize_t show_fbdev(struct device *dev, struct device_attribute *attr, char *buf)
{
    int i, ret = 0;

    for (i = 0; i < num_registered_fb; i++)
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "fb[%d] xres=%d, yres=%d, addr=0x%lx\n", i, registered_fb[i]->var.xres, registered_fb[i]->var.yres, registered_fb[i]->fix.smem_start);

    if (ret < PAGE_SIZE - 1)
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
    else
    {
        buf[PAGE_SIZE - 2] = '\n';
        buf[PAGE_SIZE - 1] = '\0';
        ret = PAGE_SIZE - 1;
    }

    return ret;
}

static ssize_t show_fps(struct device *dev, struct device_attribute *attr, char *buf)
{
    int ret = 0;
    IMG_UINT32 fps = 0;

    fps = rk_get_real_fps(0);

    ret += snprintf(buf + ret, PAGE_SIZE - ret, "fbs=%d", fps);

    if (ret < PAGE_SIZE - 1)
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
    else
    {
        buf[PAGE_SIZE - 2] = '\n';
        buf[PAGE_SIZE - 1] = '\0';
        ret = PAGE_SIZE - 1;
    }

    return ret;
}
static enum hrtimer_restart fps_callback(struct hrtimer *timer)
{
    struct rk_context *platform;
    IMG_UINT32 fps = 0;

    PVR_ASSERT(timer != NULL);

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);
    PVR_ASSERT(platform != NULL);

    fps = rk_get_real_fps(0);
    printk("Current fps=%d\n", fps);

    hrtimer_start(timer,
                  HR_TIMER_DELAY_MSEC(platform->fps_gap),
                  HRTIMER_MODE_REL);

    return HRTIMER_NORESTART;
}

static ssize_t set_fps(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rk_context *platform;
    static IMG_BOOL bOpen = IMG_FALSE;
    static struct hrtimer timer;
    IMG_UINT  gap = FPS_DEFAULT_GAP;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (!platform)
        return -ENODEV;

    gap = simple_strtoul(buf, NULL, 10);

    if (sysfs_streq("on", buf) && !bOpen)
    {
        bOpen = IMG_TRUE;
        hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        timer.function = fps_callback;
        hrtimer_start(&timer, HR_TIMER_DELAY_MSEC(platform->fps_gap), HRTIMER_MODE_REL);
        printk("on fps\n");
    }
    else if (sysfs_streq("off", buf))
    {
        printk("off fps\n");

        if(bOpen)
        {
            bOpen = IMG_FALSE;
            hrtimer_cancel(&timer);
        }
    }
    else
    {
        if (gap > 0  && gap < FPS_MAX_GAP)
            platform->fps_gap = gap;
    }

    return count;
}

#if RK33_DVFS_FREQ_LIMIT
static ssize_t show_up_limit(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct rk_context *platform;
    ssize_t ret = 0;
    int i;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);
    if (!platform)
        return -ENODEV;

    if (platform->up_level >= 0 && platform->up_level < RGX_DVFS_STEP)
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current Up limit freq = %dMhz", rk33_dvfs_get_dvfs_up_limit_freq());
    else
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "Unset the up limit level");

    ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings :");
    for (i = 0;i < RGX_DVFS_STEP;i++)
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ", p_rgx_dvfs_infotbl[i].clock);
    ret += snprintf(buf + ret, PAGE_SIZE - ret, "Mhz");
    ret += snprintf(buf + ret, PAGE_SIZE - ret, ", If you want to unlock : off");

    if (ret < PAGE_SIZE - 1)
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
    else
    {
        buf[PAGE_SIZE - 2] = '\n';
        buf[PAGE_SIZE - 1] = '\0';
        ret = PAGE_SIZE - 1;
    }

    return ret;
}

static ssize_t set_up_limit(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rk_context *platform;
    unsigned int freq = 0;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);
    if (!platform)
        return -ENODEV;

    freq = simple_strtoul(buf, NULL, 10);

    if (sysfs_streq("off", buf))
    {
        rk33_dvfs_up_limit_off();
    }
    else
    {
        rk33_dvfs_up_limit_on(rk33_dvfs_get_level(freq));
    }

    return count;
}

static ssize_t show_down_limit(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct rk_context *platform;
    ssize_t ret = 0;
    int i;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);
    if (!platform)
        return -ENODEV;

    if (platform->down_level >= 0 && platform->down_level < RGX_DVFS_STEP)
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current down limit freq = %dMhz", rk33_dvfs_get_dvfs_down_limit_freq());
    else
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "Unset the down limit level");

    ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings :");
    for (i = 0;i < RGX_DVFS_STEP;i++)
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ", p_rgx_dvfs_infotbl[i].clock);
    ret += snprintf(buf + ret, PAGE_SIZE - ret, "Mhz");
    ret += snprintf(buf + ret, PAGE_SIZE - ret, ", If you want to unlock : off");

    if (ret < PAGE_SIZE - 1)
        ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
    else
    {
        buf[PAGE_SIZE - 2] = '\n';
        buf[PAGE_SIZE - 1] = '\0';
        ret = PAGE_SIZE - 1;
    }

    return ret;
}

static ssize_t set_down_limit(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct rk_context *platform;
    unsigned int freq = 0;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);
    if (!platform)
        return -ENODEV;

    freq = simple_strtoul(buf, NULL, 10);

    if (sysfs_streq("off", buf))
    {
        rk33_dvfs_down_limit_off();
    }
    else
    {
        rk33_dvfs_down_limit_on(rk33_dvfs_get_level(freq));
    }

    return count;
}
#endif

/** The sysfs file @c clock, fbdev.
 *
 * This is used for obtaining information about the mali t6xx operating clock & framebuffer address,
 */
DEVICE_ATTR(freq, S_IRUGO | S_IWUSR, show_freq, set_freq);
DEVICE_ATTR(util, S_IRUGO | S_IWUSR, show_utilisation, set_utilisation);
DEVICE_ATTR(fbdev, S_IRUGO, show_fbdev, NULL);
DEVICE_ATTR(fps, S_IRUGO | S_IWUSR, show_fps, set_fps);
#if RK33_DVFS_FREQ_LIMIT
DEVICE_ATTR(dvfs_up_limit, S_IRUGO | S_IWUSR, show_up_limit, set_up_limit);
DEVICE_ATTR(dvfs_down_limit, S_IRUGO | S_IWUSR, show_down_limit, set_down_limit);
#endif

static IMG_INT rk_create_sysfs_file(struct device *dev)
{
    if (device_create_file(dev, &dev_attr_freq))
    {
        dev_err(dev, "Couldn't create sysfs file [freq]\n");
        goto out;
    }

    if (device_create_file(dev, &dev_attr_util))
    {
        dev_err(dev, "Couldn't create sysfs file [utilisation]\n");
        goto out;
    }

    if (device_create_file(dev, &dev_attr_fbdev))
    {
        dev_err(dev, "Couldn't create sysfs file [fbdev]\n");
        goto out;
    }

    if (device_create_file(dev, &dev_attr_fps))
    {
        dev_err(dev, "Couldn't create sysfs file [fbdev]\n");
        goto out;
    }

#if RK33_DVFS_FREQ_LIMIT
    if (device_create_file(dev, &dev_attr_dvfs_up_limit))
    {
        dev_err(dev, "Couldn't create sysfs file [dvfs_upper_lock]\n");
        goto out;
    }

    if (device_create_file(dev, &dev_attr_dvfs_down_limit))
    {
        dev_err(dev, "Couldn't create sysfs file [dvfs_under_lock]\n");
        goto out;
    }
#endif

    return 0;
out:
    return -ENOENT;
}

static IMG_VOID rk_remove_sysfs_file(struct device *dev)
{
    device_remove_file(dev, &dev_attr_freq);
    device_remove_file(dev, &dev_attr_util);
    device_remove_file(dev, &dev_attr_fbdev);
    device_remove_file(dev, &dev_attr_fps);
#if RK33_DVFS_FREQ_LIMIT
    device_remove_file(dev, &dev_attr_dvfs_up_limit);
    device_remove_file(dev, &dev_attr_dvfs_down_limit);
#endif
}



#endif //end of RK33_SYSFS_FILE_SUPPORT

#if RK33_USE_RGX_GET_GPU_UTIL
IMG_BOOL rk33_set_device_node(IMG_HANDLE hDevCookie)
{
    struct rk_context *platform;
    unsigned long flags;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if(platform)
    {
        platform->psDeviceNode = (PVRSRV_DEVICE_NODE*)hDevCookie;

        //start timer
#if USE_HRTIMER
        if(platform->psDeviceNode && platform->dvfs_enabled && platform->timer.function && !platform->timer_active)
#elif USE_KTHREAD
        if(platform->psDeviceNode && platform->dvfs_enabled && !platform->timer_active)
#endif
        {
            spin_lock_irqsave(&platform->timer_lock, flags);
            platform->timer_active = IMG_TRUE;
            spin_unlock_irqrestore(&platform->timer_lock, flags);
#if USE_HRTIMER
            hrtimer_start(&platform->timer, HR_TIMER_DELAY_MSEC(RK33_DVFS_FREQ), HRTIMER_MODE_REL);
#elif USE_KTHREAD
            wake_up_process(platform->dvfs_task);
#endif
         }
    }
    else
    {
        //call PVRSRVRegisterPowerDevice before RgxRkInit.
        PVR_DPF((PVR_DBG_ERROR,"PVR_K:%s platform is null\n",__func__));
        return IMG_FALSE;
    }

    return IMG_TRUE;
}

IMG_BOOL rk33_clear_device_node(IMG_VOID)
{
    struct rk_context *platform;
    unsigned long flags;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if(platform)
    {
        //cacel timer
        if(platform->timer_active && platform->dvfs_enabled)
        {
            spin_lock_irqsave(&platform->timer_lock, flags);
            platform->timer_active = IMG_FALSE;
            spin_unlock_irqrestore(&platform->timer_lock, flags);
#if USE_HRTIMER
            hrtimer_cancel(&platform->timer);
#endif
        }
        platform->psDeviceNode = NULL;
    }
    else
    {
        PVR_DPF((PVR_DBG_ERROR,"PVR_K:%s platform is null\n",__func__));
        return IMG_FALSE;
    }

    return IMG_TRUE;
}
#endif //RK33_USE_RGX_GET_GPU_UTIL

#endif //end of RK33_DVFS_SUPPORT

static IMG_VOID RgxEnableClock(IMG_VOID)
{
    struct rk_context *platform;
#if RK33_DVFS_SUPPORT
    unsigned long flags;
#endif

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (platform->aclk_gpu_mem && platform->aclk_gpu_cfg && !platform->gpu_active)
    {
        if(platform->gpu_clk_node)
            dvfs_clk_prepare_enable(platform->gpu_clk_node);
        else if(platform->clk_gpu)
            clk_prepare_enable(platform->clk_gpu);
        clk_prepare_enable(platform->aclk_gpu_mem);
        clk_prepare_enable(platform->aclk_gpu_cfg);

#if RK33_DVFS_SUPPORT
        rk33_dvfs_record_gpu_active(platform);

        if(platform->psDeviceNode && platform->dvfs_enabled && !platform->timer_active)
        {

            spin_lock_irqsave(&platform->timer_lock, flags);
            platform->timer_active = IMG_TRUE;
            spin_unlock_irqrestore(&platform->timer_lock, flags);
#if USE_HRTIMER
            hrtimer_start(&platform->timer, HR_TIMER_DELAY_MSEC(RK33_DVFS_FREQ), HRTIMER_MODE_REL);
#endif
        }
#endif
    }
    else
    {
        PVR_DPF((PVR_DBG_WARNING, "Failed to enable clock!"));
    }

}

static IMG_VOID RgxDisableClock(IMG_VOID)
{
    struct rk_context *platform;
#if RK33_DVFS_SUPPORT
    unsigned long flags;
#endif

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    if (platform->aclk_gpu_mem && platform->aclk_gpu_cfg && platform->gpu_active)
    {
#if RK33_DVFS_SUPPORT
        if(platform->fix_freq <= 0)
        {
            //Force to drop freq to the lowest.
            rk33_dvfs_set_level(0);
        }

        if(platform->dvfs_enabled && platform->timer_active)
        {
            spin_lock_irqsave(&platform->timer_lock, flags);
            platform->timer_active = IMG_FALSE;
            spin_unlock_irqrestore(&platform->timer_lock, flags);
#if USE_HRTIMER
            hrtimer_cancel(&platform->timer);
#endif
        }

        rk33_dvfs_record_gpu_idle(platform);

#endif
        clk_disable_unprepare(platform->aclk_gpu_cfg);
        clk_disable_unprepare(platform->aclk_gpu_mem);
        if(platform->gpu_clk_node)
            dvfs_clk_disable_unprepare(platform->gpu_clk_node);
        else if(platform->clk_gpu)
            clk_disable_unprepare(platform->clk_gpu);
    }
    else
    {
        PVR_DPF((PVR_DBG_WARNING, "Failed to disable clock!"));
    }
}

#if RK_RESERVED
static IMG_BOOL RgxIsPowerOn(IMG_VOID)
{
    struct rk_context *platform;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    PVR_ASSERT(platform != NULL);

    return platform->bEnablePd;
}
#endif

#if OPEN_GPU_PD
/*
 * The power management
 * software must power down pd_gpu_1 before power down pd_gpu_0,
 * and power up pd_gpu_1 after power up pd_gpu_0.
 */
static IMG_VOID RgxEnablePower(IMG_VOID)
{
    struct rk_context *platform;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    PVR_ASSERT(platform != NULL);

    if (!platform->bEnablePd && platform->pd_gpu_0 && platform->pd_gpu_1)
    {
        clk_prepare_enable(platform->pd_gpu_0);
        clk_prepare_enable(platform->pd_gpu_1);
        platform->bEnablePd = IMG_TRUE;
    }
    else
    {
        PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
    }
}

static IMG_VOID RgxDisablePower(IMG_VOID)
{
    struct rk_context *platform;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    PVR_ASSERT(platform != NULL);

    if (platform->bEnablePd && platform->pd_gpu_0 && platform->pd_gpu_1)
    {
        clk_disable_unprepare(platform->pd_gpu_1);
        clk_disable_unprepare(platform->pd_gpu_0);
        platform->bEnablePd = IMG_FALSE;
    }
    else
    {
        PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
    }
}
#endif

IMG_VOID RgxResume(IMG_VOID)
{
#if OPEN_GPU_PD
    RgxEnablePower();
#endif

    //mdelay(2);

    /* set external isolation invalid */
    //writel(0, SUNXI_R_PRCM_VBASE + GPU_PWROFF_GATING);

    //DeAssertGpuResetSignal();

    RgxEnableClock();

    /* delay for internal power stability */
    //writel(0x100, SUNXI_GPU_CTRL_VBASE + 0x18);
}

IMG_VOID RgxSuspend(IMG_VOID)
{
    RgxDisableClock();

    //AssertGpuResetSignal();

    /* set external isolation valid */
    //writel(1, SUNXI_R_PRCM_VBASE + GPU_PWROFF_GATING);

#if OPEN_GPU_PD
    RgxDisablePower();
#endif

}

PVRSRV_ERROR RkPrePowerState(PVRSRV_DEV_POWER_STATE eNewPowerState, PVRSRV_DEV_POWER_STATE eCurrentPowerState, IMG_BOOL bForced)
{
    if (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON)
    {
        RgxResume();
    }
    return PVRSRV_OK;
}

PVRSRV_ERROR RkPostPowerState(PVRSRV_DEV_POWER_STATE eNewPowerState, PVRSRV_DEV_POWER_STATE eCurrentPowerState, IMG_BOOL bForced)
{
    if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
    {
        RgxSuspend();
    }
    return PVRSRV_OK;
}

IMG_VOID RgxRkInit(IMG_VOID)
{
    struct rk_context *platform;

    platform = kzalloc(sizeof(struct rk_context), GFP_KERNEL);

    if (NULL == platform)
    {
        PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to kzalloc rk_context"));
        return;
    }

#if RK_TF_VERSION
    rk_tf_check_version();
#endif

    dev_set_drvdata(&gpsPVRLDMDev->dev, platform);

#if OPEN_GPU_PD
    platform->bEnablePd = IMG_FALSE;
#endif
    platform->cmu_pmu_status = 0;
    platform->psDeviceNode = NULL;

    spin_lock_init(&platform->cmu_pmu_lock);
    spin_lock_init(&platform->timer_lock);

#if OPEN_GPU_PD
    platform->pd_gpu_0 = devm_clk_get(&gpsPVRLDMDev->dev, "pd_gpu_0");
    if (IS_ERR_OR_NULL(platform->pd_gpu_0))
    {
        PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find pd_gpu_0 clock source"));
        goto fail0;
    }

    platform->pd_gpu_1 = devm_clk_get(&gpsPVRLDMDev->dev, "pd_gpu_1");
    if (IS_ERR_OR_NULL(platform->pd_gpu_1))
    {
        PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find pd_gpu_1 clock source"));
        goto fail1;
    }
#endif

    platform->aclk_gpu_mem = devm_clk_get(&gpsPVRLDMDev->dev, "aclk_gpu_mem");
    if (IS_ERR_OR_NULL(platform->aclk_gpu_mem))
    {
        PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find aclk_gpu_mem clock source"));
        goto fail2;
    }

    platform->aclk_gpu_cfg = devm_clk_get(&gpsPVRLDMDev->dev, "aclk_gpu_cfg");
    if (IS_ERR_OR_NULL(platform->aclk_gpu_cfg))
    {
        PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find aclk_gpu_cfg clock source"));
        goto fail3;
    }

    platform->gpu_clk_node  = clk_get_dvfs_node("clk_gpu");
    if (IS_ERR_OR_NULL(platform->gpu_clk_node))
    {
        platform->dvfs_enabled = IMG_FALSE;
        PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: GPU Dvfs is disabled"));
        platform->clk_gpu = devm_clk_get(&gpsPVRLDMDev->dev, "clk_gpu");
        if (IS_ERR_OR_NULL(platform->clk_gpu))
        {
            PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find clk_gpu clock source"));
            goto fail4;
        }
    }
    else
    {
        platform->dvfs_enabled = IMG_TRUE;
        rk33_clk_set_dvfs_node(platform->gpu_clk_node, RK33_DEFAULT_CLOCK);
        rk33_clk_set_normal_node(platform->aclk_gpu_mem, RK33_DEFAULT_CLOCK);
        rk33_clk_set_normal_node(platform->aclk_gpu_cfg, RK33_DEFAULT_CLOCK);
#if RK33_DVFS_SUPPORT
        rk33_dvfs_init();
#if RK33_SYSFS_FILE_SUPPORT
        //create sysfs file node
        rk_create_sysfs_file(&gpsPVRLDMDev->dev);
#endif

#endif //end of RK33_DVFS_SUPPORT
    }

    RgxResume();

    return;

fail4:
    devm_clk_put(&gpsPVRLDMDev->dev, platform->aclk_gpu_cfg);
    platform->aclk_gpu_cfg = NULL;
fail3:
    devm_clk_put(&gpsPVRLDMDev->dev, platform->aclk_gpu_mem);
    platform->aclk_gpu_mem = NULL;
fail2:

#if OPEN_GPU_PD
    devm_clk_put(&gpsPVRLDMDev->dev, platform->pd_gpu_1);
    platform->pd_gpu_1 = NULL;
fail1:
    devm_clk_put(&gpsPVRLDMDev->dev, platform->pd_gpu_0);
    platform->pd_gpu_0 = NULL;
fail0:
#endif //end of OPEN_GPU_PD

    kfree(platform);
}

IMG_VOID RgxRkUnInit(IMG_VOID)
{
    struct rk_context *platform;

    platform = dev_get_drvdata(&gpsPVRLDMDev->dev);

    RgxSuspend();

    if(platform->gpu_clk_node)
    {
        clk_put_dvfs_node(platform->gpu_clk_node);
        platform->gpu_clk_node = NULL;
    }
    else if(platform->clk_gpu)
    {
        devm_clk_put(&gpsPVRLDMDev->dev, platform->clk_gpu);
        platform->clk_gpu = NULL;
    }

    if(platform->aclk_gpu_cfg)
    {
        devm_clk_put(&gpsPVRLDMDev->dev, platform->aclk_gpu_cfg);
        platform->aclk_gpu_cfg = NULL;
    }
    if(platform->aclk_gpu_mem)
    {
        devm_clk_put(&gpsPVRLDMDev->dev, platform->aclk_gpu_mem);
        platform->aclk_gpu_mem = NULL;
    }
#if OPEN_GPU_PD
    if(platform->pd_gpu_1)
    {
        devm_clk_put(&gpsPVRLDMDev->dev, platform->pd_gpu_1);
        platform->pd_gpu_1 = NULL;
    }
    if(platform->pd_gpu_0)
    {
        devm_clk_put(&gpsPVRLDMDev->dev, platform->pd_gpu_0);
        platform->pd_gpu_0 = NULL;
    }
#endif

    if(platform->dvfs_enabled)
    {
#if RK33_DVFS_SUPPORT
#if RK33_SYSFS_FILE_SUPPORT
        rk_remove_sysfs_file(&gpsPVRLDMDev->dev);
#endif
        rk33_dvfs_term();
#endif
    }
    kfree(platform);
}


#if defined(SUPPORT_ION)
struct ion_device *g_psIonDev;
extern struct ion_device *rockchip_ion_dev;

PVRSRV_ERROR IonInit(void *phPrivateData)
{
    g_psIonDev    = rockchip_ion_dev;
    return PVRSRV_OK;
}

struct ion_device *IonDevAcquire(IMG_VOID)
{
    return g_psIonDev;
}

IMG_VOID IonDevRelease(struct ion_device *psIonDev)
{
    /* Nothing to do, sanity check the pointer we're passed back */
    PVR_ASSERT(psIonDev == g_psIonDev);
}

IMG_UINT32 IonPhysHeapID(IMG_VOID)
{
    return 0;
}

IMG_VOID IonDeinit(IMG_VOID)
{
    g_psIonDev = NULL;
}
#endif /* defined(SUPPORT_ION) */
