/*************************************************************************/ /*!
@File
@Title		RK Initialisation
@Copyright	Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Initialisation routines
@License	Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if defined(SUPPORT_ION)
#include "ion_sys.h"
#endif /* defined(SUPPORT_ION) */
#include "rk_init.h"

#include <linux/hardirq.h>
#include <linux/clk.h>
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
#include <linux/clk-private.h>
#else
#include <linux/clk-provider.h>
#include <linux/pm_runtime.h>
#include <linux/pm_opp.h>
#endif
#include "power.h"
#include "rgxinit.h"
#include <asm/compiler.h>

static struct rk_context *g_platform = NULL;

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
	int version = 0;
	int high_16 = 0;
	int low_16 = 0;
	IMG_PINT pNULL = NULL;

	version = rk_tf_get_version();
	high_16 = (version >> 16) & ~(0xFFFF << 16);
	low_16 = (version & ~(0xFFFF << 16));

	printk("raw version=0x%x,rk_tf_version=%x.%x\n", version, high_16, low_16);

	if ((version != 0xFFFFFFFF) && (high_16 >= 1) && (low_16 >= 3)) {
		return 0;
	} else {
		printk("Error:%s-line:%d This version can't support rk3328\n", __func__, __LINE__);
		*pNULL = 0; /*crash system*/
		return -1;
	}
}

#endif

#if RK33_DVFS_SUPPORT
#define gpu_temp_limit			110
#define gpu_temp_statis_time		1
#define level0_threshold_min		0
#define level0_threshold_max		40
#define levelf_threshold_max		100
#define level0_coef_max			95

static IMG_UINT32 div_dvfs = 0 ;

/*dvfs status*/
static struct workqueue_struct *rgx_dvfs_wq;
spinlock_t rgx_dvfs_spinlock;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
static struct cpufreq_frequency_table *rgx_freq_table = NULL;
#endif
#endif
static IMG_HANDLE ghGpuUtilDvfs = NULL;

/* voltage     clock   min_threshold   max_threshold   time  */
static rgx_dvfs_info rgx_dvfs_infotbl[] = {
	{925, 100, 0, 70, 0, 100},
	{925, 160, 50, 65, 0, 95},
	{1025, 266, 60, 78, 0, 90},
	{1075, 350, 65, 75, 0, 85},
	{1125, 400, 70, 75, 0, 80},
	{1200, 500, 90, 100, 0, 75},
};
rgx_dvfs_info *p_rgx_dvfs_infotbl = rgx_dvfs_infotbl;
unsigned int RGX_DVFS_STEP = ARRAY_SIZE(rgx_dvfs_infotbl);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
static int rk33_clk_set_normal_node(struct clk* node, unsigned long rate)
{
	int ret = 0;

	if (!node) {
		printk("rk33_clk_set_normal_node error \r\n");
		ret = -1;
	}
	ret = clk_set_rate(node, rate * ONE_MHZ);
	if (ret)
		printk("clk_set_rate error \r\n");

	return ret;
}

static int rk33_clk_set_dvfs_node(struct dvfs_node *node, unsigned long rate)
{
	int ret = 0;

	if (!node) {
		printk("rk33_clk_set_dvfs_node error \r\n");
		ret = -1;
	}
	ret = dvfs_clk_set_rate(node, rate * ONE_MHZ);
	if (ret)
		printk("dvfs_clk_set_rate error \r\n");

	return ret;
}
#endif
#if RK33_DVFS_SUPPORT
#define dividend 7
#define fix_float(a) ((((a)*dividend)%10)?((((a)*dividend)/10)+1):(((a)*dividend)/10))
static IMG_BOOL calculate_dvfs_max_min_threshold(IMG_UINT32 level)
{
	IMG_UINT32 pre_level;
	IMG_UINT32 tmp;

	if (level == 0) {
		if ((RGX_DVFS_STEP - 1) == level) {
			rgx_dvfs_infotbl[level].min_threshold = level0_threshold_min;
			rgx_dvfs_infotbl[level].max_threshold = levelf_threshold_max;
		} else {
			rgx_dvfs_infotbl[level].min_threshold = level0_threshold_min;
			rgx_dvfs_infotbl[level].max_threshold = level0_threshold_max;
		}
#if RK33_USE_CL_COUNT_UTILS
		rgx_dvfs_infotbl[level].coef = level0_coef_max;
#endif
	} else {
		pre_level = level - 1;
		if ((RGX_DVFS_STEP - 1) == level) {
			rgx_dvfs_infotbl[level].max_threshold = levelf_threshold_max;
		} else {
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

#if RK33_DVFS_FREQ_LIMIT
static int rk33_dvfs_get_freq(int level)
{
	if (WARN_ON((level >= RGX_DVFS_STEP) || (level < 0))) {
		printk("unknown rgx dvfs level:level = %d,set clock not done\n", level);
		return	-1;
	}
	return rgx_dvfs_infotbl[level].clock;
}
#endif

static int rk33_dvfs_get_level(int freq)
{
	int i;
	for (i = 0; i < RGX_DVFS_STEP; i++) {
		if (rgx_dvfs_infotbl[i].clock == freq)
		    return i;
	}
	return -1;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
static void rk33_dvfs_set_clock(struct rk_context *platform, int freq)
{
	unsigned long old_freq;
#if USE_PVR_SPEED_CHANGE
	PVRSRV_ERROR err;

	err = PVRSRVDevicePreClockSpeedChange(platform->dev_config->psDevNode, IMG_TRUE, NULL);
	if (err != PVRSRV_OK) {
		return;
	}
#endif

	if (!platform->aclk_gpu_mem || !platform->aclk_gpu_cfg || !platform->dvfs_enabled) {
		printk("aclk_gpu_mem or aclk_gpu_cfg not init\n");
		return;
	}

	if (!platform->gpu_clk_node && !platform->clk_gpu) {
		pr_err("%s:clk_gpu & gpu_clk_node is null\n", __func__);
		return;
	}

	if (platform->gpu_clk_node)
		old_freq = clk_get_rate(platform->gpu_clk_node->clk);
	else if (platform->clk_gpu)
		old_freq = clk_get_rate(platform->clk_gpu);

	if (old_freq > freq) {
		if (platform->gpu_clk_node)
			rk33_clk_set_dvfs_node(platform->gpu_clk_node, freq);
		else if (platform->clk_gpu)
			rk33_clk_set_normal_node(platform->clk_gpu, freq);
	}

	rk33_clk_set_normal_node(platform->aclk_gpu_mem, freq);
	rk33_clk_set_normal_node(platform->aclk_gpu_cfg, freq);

	if (old_freq < freq) {
		if (platform->gpu_clk_node)
			rk33_clk_set_dvfs_node(platform->gpu_clk_node, freq);
		else if (platform->clk_gpu)
			rk33_clk_set_normal_node(platform->clk_gpu, freq);
	}

#if USE_PVR_SPEED_CHANGE
	PVRSRVDevicePostClockSpeedChange(platform->dev_config->psDevNode, IMG_TRUE, NULL);
#endif
}

static void rk33_dvfs_set_level(struct rk_context *platform, int level)
{
    static int prev_level = -1;

    if (level == prev_level)
	return;

    if (WARN_ON((level >= RGX_DVFS_STEP) || (level < 0))) {
	printk("unknown rgx dvfs level:level = %d,set clock not done\n", level);
	return	;
    }

    rk33_dvfs_set_clock(platform, rgx_dvfs_infotbl[level].clock);

    prev_level = level;
}
#else
static void rk33_dvfs_set_level(struct rk_context *platform, int level)
{
	static int prev_level = -1;
	int ret = 0;
	unsigned int old_freq, new_freq;
	unsigned int old_volt, new_volt;

	if (NULL == platform)
		panic("oops");

	if (!platform->dvfs_enabled) {
		printk("dvfs not enabled\n");
		return;
	}

	if (level == prev_level)
		return;

	if (WARN_ON((level >= RGX_DVFS_STEP) || (level < 0))) {
		printk("unknown rgx dvfs level:level = %d,set clock not done \n", level);
		return;
	}

	old_freq = clk_get_rate(platform->sclk_gpu_core) / ONE_MHZ;
	new_freq = rgx_dvfs_infotbl[level].clock;
	old_volt = regulator_get_voltage(platform->gpu_reg);
	new_volt = rgx_dvfs_infotbl[level].voltage;

#if 0
	dev_info(&gpsPVRLDMDev->dev, "%d MHz, %d mV --> %d MHz, %d mV\n",
		 old_freq, (old_volt > 0) ? old_volt / 1000 : -1,
		 new_freq, new_volt ? new_volt / 1000 : -1);
#endif

	if (new_freq > old_freq) {
		ret = regulator_set_voltage(platform->gpu_reg, new_volt, INT_MAX);
		if (ret) {
			PVR_DPF((PVR_DBG_ERROR, "failed to scale voltage up: %d\n", ret));
			return;
		}
	}
	ret = clk_set_rate(platform->aclk_gpu_mem, new_freq * ONE_MHZ);
	if (ret) {
		PVR_DPF((PVR_DBG_ERROR, "failed to set aclk_gpu_mem rate: %d\n", ret));
		if (old_volt > 0)
			regulator_set_voltage(platform->gpu_reg, old_volt, INT_MAX);
		return;
	}
	ret = clk_set_rate(platform->aclk_gpu_cfg, new_freq * ONE_MHZ);
	if (ret) {
		PVR_DPF((PVR_DBG_ERROR, "failed to set aclk_gpu_cfg rate: %d\n", ret));
		clk_set_rate(platform->aclk_gpu_mem, old_freq * ONE_MHZ);
		if (old_volt > 0)
			regulator_set_voltage(platform->gpu_reg, old_volt, INT_MAX);
		return;
	}
	ret = clk_set_rate(platform->sclk_gpu_core, new_freq * ONE_MHZ);
	if (ret) {
		PVR_DPF((PVR_DBG_ERROR, "failed to set sclk_gpu_core rate: %d\n", ret));
		clk_set_rate(platform->aclk_gpu_mem, old_freq * ONE_MHZ);
		clk_set_rate(platform->aclk_gpu_cfg, old_freq * ONE_MHZ);
		if (old_volt > 0)
			regulator_set_voltage(platform->gpu_reg, old_volt, INT_MAX);
		return;
	}
	if (new_freq < old_freq) {
		ret =
		    regulator_set_voltage(platform->gpu_reg, new_volt, INT_MAX);
		if (ret) {
			PVR_DPF((PVR_DBG_ERROR, "failed to scale voltage down: %d\n", ret));
			clk_set_rate(platform->aclk_gpu_mem, old_freq * ONE_MHZ);
			clk_set_rate(platform->aclk_gpu_cfg, old_freq * ONE_MHZ);
			clk_set_rate(platform->sclk_gpu_core, old_freq * ONE_MHZ);
			return;
		}
	}
#if 0
	update_time_in_state(prev_level);
#endif
	prev_level = level;
}
#endif

static int rk33_dvfs_get_enable_status(struct rk_context *platform)
{
	unsigned long		flags;
	int			enable;

	spin_lock_irqsave(&platform->timer_lock, flags);
	enable = platform->timer_active;
	spin_unlock_irqrestore(&platform->timer_lock, flags);

	return enable;
}

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

	platform = container_of(w, struct rk_context, rgx_dvfs_work);
	spin_lock_irqsave(&rgx_dvfs_spinlock, flags);

	if (!rk33_dvfs_get_enable_status(platform)) {
		spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
		return;
	}

	fps = rk_get_real_fps(0);

	platform->temperature_time++;
	/*temp_tmp += rockchip_tsadc_get_temp(2);*/
	if (platform->temperature_time >= gpu_temp_statis_time) {
		platform->temperature_time = 0;
		platform->temperature = temp_tmp / gpu_temp_statis_time;
		temp_tmp = 0;
		/*pr_info("platform->temperature = %d\n",platform->temperature);*/
	}

	platform->abs_load[0] = platform->abs_load[1];
	platform->abs_load[1] = platform->abs_load[2];
	platform->abs_load[2] = platform->abs_load[3];
	platform->abs_load[3] = (platform->utilisation * rgx_dvfs_infotbl[platform->freq_level].clock * rgx_dvfs_infotbl[platform->freq_level].coef) / 100;
	absload = (platform->abs_load[3] * 4 + platform->abs_load[2] * 3 + platform->abs_load[1] * 2 + platform->abs_load[0]);

	/*policy = rockchip_pm_get_policy();*/
	policy = ROCKCHIP_PM_POLICY_NORMAL;

	if (ROCKCHIP_PM_POLICY_PERFORMANCE == policy) {
		platform->freq_level = RGX_DVFS_STEP - 1; /*Highest level when performance mode*/
	} else if (platform->fix_freq > 0) {
		platform->freq_level = rk33_dvfs_get_level(platform->fix_freq);

		if (platform->debug_level == DBG_HIGH)
			printk("fix clock=%d\n", platform->fix_freq);
	} else {
		fps_limit = (ROCKCHIP_PM_POLICY_NORMAL == policy) ? LIMIT_FPS : LIMIT_FPS_POWER_SAVE;
		/*printk("policy : %d , fps_limit = %d\n",policy,fps_limit);*/
		/*give priority to temperature unless in performance mode */
		if (platform->temperature > gpu_temp_limit) {
			if (platform->freq_level > 0)
				platform->freq_level--;

			if (gpu_temp_statis_time > 1)
				platform->temperature = 0;
		} else if (absload == 0 || platform->gpu_active == IMG_FALSE) {
			platform->freq_level = 0;
		} else if ((platform->freq_level < RGX_DVFS_STEP - 1) && fps < fps_limit) {
			/*freq_hint=0 or freq_hint>sRK30_DVFS.u8FreqNums,  select freq automatically, find the right index*/
			for (new_index = 0; new_index < RGX_DVFS_STEP; new_index++) {
				if (absload <= ((rgx_dvfs_infotbl[new_index].clock) * (rgx_dvfs_infotbl[new_index].coef) * 9)) {
					if (platform->debug_level == DBG_HIGH)
						printk("absload=%d,cur_coef[%d]=%d\n", absload, new_index, (rgx_dvfs_infotbl[new_index].clock) * (rgx_dvfs_infotbl[new_index].coef)*9);
					break;
				}
			}

			/*ensure the new_index in the reasonable range*/
			if (new_index >= RGX_DVFS_STEP)
				new_index = RGX_DVFS_STEP - 1;

			/*if fps>=50, should not run at the higher frequency*/
			if (new_index > platform->freq_level && fps >= fps_limit) {
				new_index = platform->freq_level;
			} else if (platform->freq_level == RGX_DVFS_STEP - 1 && fps > 53 && absload <= ((rgx_dvfs_infotbl[new_index].clock) * (rgx_dvfs_infotbl[new_index].coef) * 9)) {
			/*if running at highest frequency & fps>53 & absload<90%, try to run at a lower frequency*/
				new_index = platform->freq_level - 1;
			}

			if (platform->debug_level == DBG_HIGH)
				printk("absload=%d,freq_level=%d,freq=%dM\n", absload, new_index, rgx_dvfs_infotbl[new_index].clock);

			platform->freq_level = new_index;
		}
	}
#if RK33_SYSFS_FILE_SUPPORT && RK33_DVFS_FREQ_LIMIT
	if ((platform->up_level >= 0) && (platform->freq_level > platform->up_level))
		platform->freq_level = platform->up_level;

	if ((platform->down_level >= 0) && (platform->freq_level < platform->down_level))
		platform->freq_level = platform->down_level;
#endif
	platform->time_busy = 0;
	platform->time_idle = 0;
	platform->utilisation = 0;
	spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

	rk33_dvfs_set_level(platform, platform->freq_level);
}
#else
static void rk33_dvfs_event_proc(struct work_struct *w)
{
	unsigned long flags;
	static int level_down_time;
	static int level_up_time;
	static IMG_UINT32 temp_tmp;
	IMG_UINT32 fps = 0;
	IMG_UINT32 fps_limit;
	IMG_UINT32 policy;
	struct rk_context *platform;

	platform = container_of(w, struct rk_context, rgx_dvfs_work);
	spin_lock_irqsave(&rgx_dvfs_spinlock, flags);

	if (!rk33_dvfs_get_enable_status(platform)) {
		spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
		return;
	}

	fps = rk_get_real_fps(0);

	platform->temperature_time++;
	/*temp_tmp += rockchip_tsadc_get_temp(2);*/
	if (platform->temperature_time >= gpu_temp_statis_time) {
		platform->temperature_time = 0;
		platform->temperature = temp_tmp / gpu_temp_statis_time;
		temp_tmp = 0;
		/*pr_info("platform->temperature = %d\n",platform->temperature);*/
	}

	/*
	policy = rockchip_pm_get_policy();
	*/
	policy = ROCKCHIP_PM_POLICY_NORMAL;

	if (policy == ROCKCHIP_PM_POLICY_PERFORMANCE) {
		platform->freq_level = RGX_DVFS_STEP - 1; /*Highest level when performance mode*/
	} else if (platform->fix_freq > 0) {
		platform->freq_level = rk33_dvfs_get_level(platform->fix_freq);

		if (platform->debug_level == DBG_HIGH)
			printk("fix clock=%d\n", platform->fix_freq);
	} else {
		fps_limit = (policy == ROCKCHIP_PM_POLICY_NORMAL) ? LIMIT_FPS : LIMIT_FPS_POWER_SAVE;
#if 0
		printk("policy : %d , fps_limit = %d\n", policy, fps_limit);
#endif

	/*give priority to temperature unless in performance mode */
	if (platform->temperature > gpu_temp_limit) {
		if (platform->freq_level > 0)
			platform->freq_level--;

		if (gpu_temp_statis_time > 1)
			platform->temperature = 0;
	} else if ((platform->utilisation > rgx_dvfs_infotbl[platform->freq_level].max_threshold) && (platform->freq_level < RGX_DVFS_STEP - 1) /*&& fps < fps_limit*/) {
		level_up_time++;
		if (level_up_time == RGX_DVFS_LEVEL_INTERVAL) {
			if (platform->debug_level == DBG_HIGH)
				printk("up,utilisation=%d,current clock=%d,fps = %d\n", platform->utilisation, rgx_dvfs_infotbl[platform->freq_level].clock, fps);

			platform->freq_level++;
			level_up_time = 0;

			if (platform->debug_level == DBG_HIGH)
				printk(" next clock=%d\n", rgx_dvfs_infotbl[platform->freq_level].clock);

			BUG_ON(platform->freq_level >= RGX_DVFS_STEP);
		}
		level_down_time = 0;
	} else if ((platform->freq_level > 0) && (platform->utilisation < rgx_dvfs_infotbl[platform->freq_level].min_threshold)) {
		level_down_time++;
		if (level_down_time == RGX_DVFS_LEVEL_INTERVAL) {
			if (platform->debug_level == DBG_HIGH)
				printk("down,utilisation=%d,current clock=%d,fps = %d\n", platform->utilisation, rgx_dvfs_infotbl[platform->freq_level].clock, fps);
				BUG_ON(platform->freq_level <= 0);
				platform->freq_level--;
				level_down_time = 0;

				if (platform->debug_level == DBG_HIGH)
					printk(" next clock=%d\n", rgx_dvfs_infotbl[platform->freq_level].clock);
			}
		level_up_time = 0;
		} else {
			level_down_time = 0;
			level_up_time = 0;

			if (platform->debug_level == DBG_HIGH)
				printk("keep,utilisation=%d,current clock=%d,fps = %d\n", platform->utilisation, rgx_dvfs_infotbl[platform->freq_level].clock, fps);
		}
	}
#if RK33_SYSFS_FILE_SUPPORT && RK33_DVFS_FREQ_LIMIT
	if ((platform->up_level >= 0) && (platform->freq_level > platform->up_level))
		platform->freq_level = platform->up_level;

	if ((platform->down_level >= 0) && (platform->freq_level < platform->down_level))
		platform->freq_level = platform->down_level;
#endif
	platform->time_busy = 0;
	platform->time_idle = 0;
	platform->utilisation = 0;
	spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

	rk33_dvfs_set_level(platform, platform->freq_level);
}
#endif

static IMG_BOOL rk33_dvfs_event(RGXFWIF_GPU_UTIL_STATS * psUtilStats)
{
	struct rk_context *platform;
#if !RK33_USE_CUSTOMER_GET_GPU_UTIL
	IMG_UINT32 time_busy = 0;
	IMG_UINT32 time_idle = 0;
#endif
	unsigned long flags;
#if RK33_USE_CUSTOMER_GET_GPU_UTIL
	ktime_t now = ktime_get();
	ktime_t diff;
	IMG_INT i;
#endif

	platform = container_of(psUtilStats, struct rk_context, sUtilStats);
	PVR_ASSERT(platform != NULL);

#if RK33_USE_RGX_GET_GPU_UTIL
	PVR_ASSERT(psUtilStats != NULL);

	if (platform->debug_level == DBG_HIGH)
		printk("GPU util info:valid[%d],ActiveHigh[%llu],ActiveLow[%llu],Blocked[%llu],Idle[%llu],Sum[%llu]\n",
				psUtilStats->bValid, psUtilStats->ui64GpuStatActiveHigh,
				psUtilStats->ui64GpuStatActiveLow,
				psUtilStats->ui64GpuStatBlocked,
				psUtilStats->ui64GpuStatIdle,
				psUtilStats->ui64GpuStatCumulative);

	if (psUtilStats->bValid /*&& psUtilStats->bIncompleteData */) {
		time_busy = psUtilStats->ui64GpuStatActiveHigh + psUtilStats->ui64GpuStatActiveLow;
		time_idle = psUtilStats->ui64GpuStatIdle + psUtilStats->ui64GpuStatBlocked;

		//check valid
		if (time_busy + time_idle == 0)
			goto err;

		spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
		platform->time_busy += time_busy;
		platform->time_idle += time_idle;
		platform->utilisation = (time_busy * 100) / (time_busy + time_idle);
		spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
		queue_work_on(0, rgx_dvfs_wq, &platform->rgx_dvfs_work);
	}
#elif RK33_USE_CUSTOMER_GET_GPU_UTIL
	diff = ktime_sub(now, platform->time_period_start);

	if (platform->gpu_active) {
		platform->time_busy += (IMG_UINT32) (ktime_to_ns(diff) >> RK_PM_TIME_SHIFT);
		platform->time_period_start = now;
	} else {
		platform->time_idle += (IMG_UINT32) (ktime_to_ns(diff) >> RK_PM_TIME_SHIFT);
		platform->time_period_start = now;
	}

	//check valid
	if (platform->time_busy + platform->time_idle == 0)
		goto err;

	spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
	for (i = 0; i < RK33_MAX_UTILIS - 1; i++) {
		platform->stUtilis.time_busys[i] = platform->stUtilis.time_busys[i + 1];
		platform->stUtilis.time_idles[i] = platform->stUtilis.time_idles[i + 1];
		platform->stUtilis.utilis[i] = platform->stUtilis.utilis[i + 1];
	}
	platform->stUtilis.time_busys[RK33_MAX_UTILIS - 1] = platform->time_busy;
	platform->stUtilis.time_idles[RK33_MAX_UTILIS - 1] = platform->time_idle;
	platform->stUtilis.utilis[RK33_MAX_UTILIS - 1] = (platform->time_busy * 10) / (platform->time_busy + platform->time_idle);
	platform->utilisation =
				platform->stUtilis.utilis[3] * 4 +
				platform->stUtilis.utilis[2] * 3 +
				platform->stUtilis.utilis[1] * 2 + platform->stUtilis.utilis[0] * 1;
	spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

	if (platform->debug_level == DBG_HIGH)
		printk("GPU util info:time_busy=%d,time_idle=%d,utilisation=%d\n",
				platform->time_busy, platform->time_idle,
				platform->utilisation);
	queue_work_on(0, rgx_dvfs_wq, &platform->rgx_dvfs_work);
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
	unsigned long		flags;
	struct rk_context	*platform;
	PVRSRV_RGXDEV_INFO  *psDevInfo;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(timer != NULL);

	platform = container_of(timer, struct rk_context, timer);
	PVR_ASSERT(platform != NULL);

	spin_lock_irqsave(&platform->timer_lock, flags);

	if (platform->dev_config->psDevNode) {
		psDevInfo = platform->dev_config->psDevNode->pvDevice;

		if (psDevInfo && psDevInfo->pfnGetGpuUtilStats && platform->gpu_active) {
			/*Measuring GPU Utilisation*/
			eError = psDevInfo->pfnGetGpuUtilStats(platform->dev_config->psDevNode, ghGpuUtilDvfs, &platform->sUtilStats);
			rk33_dvfs_event(&platform->sUtilStats);
		} else {
			if (!psDevInfo || !psDevInfo->pfnGetGpuUtilStats)
				PVR_DPF((PVR_DBG_ERROR, "%s:line=%d,devinfo is null\n", __func__, __LINE__));
		}
	}

	if (platform->timer_active)
		hrtimer_start(timer, HR_TIMER_DELAY_MSEC(RK33_DVFS_FREQ), HRTIMER_MODE_REL);

	spin_unlock_irqrestore(&platform->timer_lock, flags);

	return HRTIMER_NORESTART;
}
#elif USE_KTHREAD
static int gpu_dvfs_task(void *data)
{
	static long sTimeout = msecs_to_jiffies(RK33_DVFS_FREQ);
	long timeout = sTimeout;
	unsigned long		flags;
	struct rk_context *platform = data;
	PVRSRV_RGXDEV_INFO  *psDevInfo;

	PVR_ASSERT(platform != NULL);

	set_freezable();

	do {
		if (platform->dev_config->psDevNode) {
			psDevInfo = platform->dev_config->psDevNode->pvDevice;
			spin_lock_irqsave(&platform->timer_lock, flags);
			if (psDevInfo && psDevInfo->pfnGetGpuUtilStats && platform->gpu_active) {
				/*Measuring GPU Utilisation*/
				platform->sUtilStats = psDevInfo->pfnGetGpuUtilStats(platform->dev_config->psDevNode);
				rk33_dvfs_event(&platform->sUtilStats);
			} else {
				if (!psDevInfo || !psDevInfo->pfnGetGpuUtilStats)
					PVR_DPF((PVR_DBG_ERROR, "%s:line=%d,devinfo is null\n", __func__, __LINE__));
			}
			spin_unlock_irqrestore(&platform->timer_lock, flags);
		}
		wait_event_freezable_timeout(platform->dvfs_wait, kthread_should_stop(), timeout);
	} while (!kthread_should_stop());

	return 0;
}
#endif

static void rk33_dvfs_utils_init(struct rk_context *platform)
{
#if USE_KTHREAD
	int iRet = -1;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
#endif
#if RK33_USE_CUSTOMER_GET_GPU_UTIL
	IMG_INT i;
#endif
	PVR_ASSERT(platform != NULL);

	/*spin_lock_irqsave(&rgx_dvfs_spinlock, flags);*/
	platform->utilisation = 0;
	platform->freq_level = 0;
	platform->freq = 0;
	platform->time_tick = 0;
#if RK33_USE_CUSTOMER_GET_GPU_UTIL
	platform->time_period_start = ktime_get();
	for (i = 0; i < RK33_MAX_UTILIS; i++) {
		platform->stUtilis.time_busys[i] = 0;
		platform->stUtilis.time_idles[i] = 0;
		platform->stUtilis.utilis[i] = 0;
	}
#endif
	platform->gpu_active = IMG_FALSE;
	platform->time_busy = 0;
	platform->time_idle = 0;
	platform->temperature = 0;
	platform->temperature_time = 0;
	platform->timer_active = IMG_FALSE;

	INIT_WORK(&platform->rgx_dvfs_work, rk33_dvfs_event_proc);

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
	hrtimer_init(&platform->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
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


	if (platform->timer_active) {
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

	if (!platform->gpu_active)
		return;

	spin_lock_irqsave(&platform->timer_lock, flags);
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

	if (platform->gpu_active)
	return;

	spin_lock_irqsave(&platform->timer_lock, flags);
	platform->gpu_active = IMG_TRUE;
#if RK33_USE_CUSTOMER_GET_GPU_UTIL
	rk33_dvfs_record_idle_utils(platform);
#endif
	spin_unlock_irqrestore(&platform->timer_lock, flags);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
static IMG_BOOL rk33_dvfs_get_freq_table(struct rk_context *platform)
{
	IMG_INT i;

	PVR_ASSERT(platform != NULL);

	/*get freq table*/
	rgx_freq_table = dvfs_get_freq_volt_table(platform->gpu_clk_node);

	if (rgx_freq_table == NULL) {
		printk("rgx freq table not assigned yet,use default\n");
		return IMG_FALSE ;
	} else {
		/*recalculate step*/
		RGX_DVFS_STEP = 0;

		PVR_DPF((PVR_DBG_WARNING, "The raw GPU freq_table:"));
		for (i = 0; rgx_freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
			rgx_dvfs_infotbl[i].clock = rgx_freq_table[i].frequency / ONE_KHZ;
			PVR_DPF((PVR_DBG_WARNING, "%dM,", rgx_dvfs_infotbl[i].clock));
			RGX_DVFS_STEP++;
		}

		if (RGX_DVFS_STEP > 1)
		div_dvfs = round_up(((levelf_threshold_max - level0_threshold_max) / (RGX_DVFS_STEP - 1)), 1);

		PVR_DPF((PVR_DBG_WARNING, "RGX_DVFS_STEP=%d,div_dvfs=%d\n", RGX_DVFS_STEP, div_dvfs));

		for (i = 0; i < RGX_DVFS_STEP; i++)
			calculate_dvfs_max_min_threshold(i);
		p_rgx_dvfs_infotbl = rgx_dvfs_infotbl;
	}

	return IMG_TRUE;
}
#else
static IMG_BOOL rk33_dvfs_get_freq_table(struct rk_context *platform)
{
	IMG_INT i;
	int length;
	unsigned long rate;
	struct device *dev = (struct device *)platform->dev_config->pvOSDevice;

	PVR_ASSERT(platform != NULL);


	RGX_DVFS_STEP = 0;

	length = dev_pm_opp_get_opp_count(dev);
	if (length <= 0) {
		PVR_DPF((PVR_DBG_ERROR, "rgx freq table not assigned yet,use default\n"));
		return IMG_FALSE;
	}

	for (i = 0, rate = 0; i < length; i++, rate++) {
		struct dev_pm_opp *opp;

		opp = dev_pm_opp_find_freq_ceil(dev, &rate);
		if (IS_ERR(opp)) {
			PVR_DPF((PVR_DBG_ERROR, "Error getting opp %d\n", i));
			break;
		}
		rgx_dvfs_infotbl[i].voltage = dev_pm_opp_get_voltage(opp);
		dev_pm_opp_put(opp);
		rgx_dvfs_infotbl[i].clock = clk_round_rate(platform->sclk_gpu_core, rate) / ONE_MHZ;
		PVR_DPF((PVR_DBG_WARNING, "%dM,%dMv", rgx_dvfs_infotbl[i].clock, rgx_dvfs_infotbl[i].voltage));
		RGX_DVFS_STEP++;
	}

	if (RGX_DVFS_STEP > 1)
		div_dvfs = round_up(((levelf_threshold_max - level0_threshold_max) / (RGX_DVFS_STEP - 1)), 1);

	PVR_DPF((PVR_DBG_WARNING, "RGX_DVFS_STEP=%d,div_dvfs=%d\n", RGX_DVFS_STEP, div_dvfs));

	for (i = 0; i < RGX_DVFS_STEP; i++)
		calculate_dvfs_max_min_threshold(i);

	p_rgx_dvfs_infotbl = rgx_dvfs_infotbl;

	return IMG_TRUE;
}
#endif

IMG_BOOL rk33_dvfs_init(struct rk_context *platform)
{
	if (IMG_FALSE == rk33_dvfs_get_freq_table(platform))
		return IMG_FALSE;

	if (!rgx_dvfs_wq)
		rgx_dvfs_wq = create_singlethread_workqueue("rgx_dvfs");

	spin_lock_init(&rgx_dvfs_spinlock);
	rk33_dvfs_utils_init(platform);

	return IMG_TRUE;
}


void rk33_dvfs_term(struct rk_context *platform)
{
	rk33_dvfs_utils_term(platform);

	if (rgx_dvfs_wq)
		destroy_workqueue(rgx_dvfs_wq);

	rgx_dvfs_wq = NULL;
}

#if RK33_SYSFS_FILE_SUPPORT
#if RK33_DVFS_FREQ_LIMIT
static int rk33_dvfs_up_limit_on(struct rk_context *platform, int level)
{
	unsigned long flags;

	if (!platform || level < 0)
	return -ENODEV;

	spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
	if (platform->down_level >= 0 &&
	    platform->down_level > level) {
		PVR_DPF((PVR_DBG_ERROR, " rk33_dvfs_up_limit_on : Attempting to set upper lock (%d) to below under lock(%d)\n", level, platform->down_level));
		spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
		return -1;
	}
	platform->up_level = level;
	spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

	PVR_DPF((PVR_DBG_WARNING, " Up Level Set : %d\n", level));

	return 0;
}

static int rk33_dvfs_up_limit_off(struct rk_context *platform)
{
	unsigned long flags;

	if (!platform)
	return -ENODEV;

	spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
	platform->up_level = -1;
	spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

	PVR_DPF((PVR_DBG_WARNING, "Up Level Unset\n"));

	return 0;
}

static int rk33_dvfs_down_limit_on(struct rk_context *platform, int level)
{
	unsigned long flags;

	if (!platform || level < 0)
		return -ENODEV;

	spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
	if (platform->up_level >= 0 && platform->up_level < level) {
		PVR_DPF((PVR_DBG_ERROR, " rk33_dvfs_down_limit_on : Attempting to set under lock (%d) to above upper lock(%d)\n", level, platform->up_level));
		spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);
		return -1;
	}
	platform->down_level = level;
	spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

	PVR_DPF((PVR_DBG_WARNING, " Down Level Set : %d\n", level));

	return 0;
}

static int rk33_dvfs_down_limit_off(struct rk_context *platform)
{
	unsigned long flags;

	if (!platform)
		return -ENODEV;

	spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
	platform->down_level = -1;
	spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

	PVR_DPF((PVR_DBG_WARNING, "Up Level Unset\n"));

	return 0;
}

static int rk33_dvfs_get_dvfs_up_limit_freq(struct rk_context *platform)
{
	unsigned long flags;
	int up_level = -1;

	if (!platform)
		return -ENODEV;

	spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
	up_level = rk33_dvfs_get_freq(platform->up_level);
	spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

	return up_level;
}

static int rk33_dvfs_get_dvfs_down_limit_freq(struct rk_context *platform)
{
	unsigned long flags;
	int down_level = -1;

	if (!platform)
		return -ENODEV;

	spin_lock_irqsave(&rgx_dvfs_spinlock, flags);
	down_level = rk33_dvfs_get_freq(platform->down_level);
	spin_unlock_irqrestore(&rgx_dvfs_spinlock, flags);

	return down_level;
}
#endif //end of RK33_DVFS_FREQ_LIMIT

#define to_dev_ext_attribute(a) container_of(a, struct dev_ext_attribute, attr)

static ssize_t show_freq(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ext_attr = to_dev_ext_attribute(attr);
	struct rk_context *platform;
	ssize_t ret = 0;
	unsigned int clkrate;
	int i ;

	platform = (struct rk_context *)ext_attr->var;
	if (!platform)
		return -ENODEV;

	if (platform->debug_level > DBG_OFF) {
#if RK33_DVFS_SUPPORT
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
		if (!platform->gpu_clk_node) {
			PVR_DPF((PVR_DBG_ERROR, "gpu_clk_node not init!"));
			return -ENODEV;
		}
		clkrate = dvfs_clk_get_rate(platform->gpu_clk_node);
#else
		if (!platform->sclk_gpu_core) {
			PVR_DPF((PVR_DBG_ERROR, "sclk_gpu_core not init!"));
			return -ENODEV;
		}
		clkrate = clk_get_rate(platform->sclk_gpu_core);
#endif
#endif
		if (platform->dvfs_enabled) {
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "DVFS is on");

			ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nCurrent clk rgx = %dMhz", clkrate / ONE_MHZ);
			/* To be revised  */
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings:");
			for (i = 0; i < RGX_DVFS_STEP; i++)
				ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ", p_rgx_dvfs_infotbl[i].clock);
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "Mhz");
		} else {
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "DVFS is off");
		}

		if (ret < PAGE_SIZE - 1) {
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
		} else {
			buf[PAGE_SIZE - 2] = '\n';
			buf[PAGE_SIZE - 1] = '\0';
			ret = PAGE_SIZE - 1;
		}
	}
	return ret;
}

static ssize_t set_freq(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct dev_ext_attribute *ext_attr = to_dev_ext_attribute(attr);
	struct rk_context *platform;
	unsigned int tmp = 0, freq = 0;

	tmp = 0;

	platform = (struct rk_context *)ext_attr->var;
	if (!platform)
		return -ENODEV;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	if (!platform->gpu_clk_node) {
#else
	if (!platform->sclk_gpu_core) {
#endif
		PVR_DPF((PVR_DBG_ERROR, "sclk_gpu_core not init!"));
		return -ENODEV;
	}

	if (memcmp(buf, "debug_hi", 8) == 0) {
		platform->debug_level = DBG_HIGH;
		return count;
	} else if (memcmp(buf, "debug_lo", 8) == 0) {
		platform->debug_level = DBG_LOW;
		return count;
	} else if (memcmp(buf, "debug_off", 9) == 0) {
		platform->debug_level = DBG_OFF;
		return count;
	} else if (memcmp(buf, "perf", 4) == 0) {
		set_freq_mode(PERFORMANCE_MODE);
		return count;
	} else if (memcmp(buf, "dvfs", 4) == 0) {
		set_freq_mode(DVFS_MODE);
		return count;
	} else if (memcmp(buf, "power", 5) == 0) {
		set_freq_mode(POWER_MODE);
		return count;
	}

	if (sscanf(buf, "%i", &freq) == 0) {
		PVR_DPF((PVR_DBG_ERROR, "invalid value"));
		return -EINVAL;
	} else {
		if (rk33_dvfs_get_level(freq) >= RGX_DVFS_STEP || rk33_dvfs_get_level(freq) < 0) {
			PVR_DPF((PVR_DBG_ERROR, "invalid freq(%d)", freq));
			platform->fix_freq = 0; /*open dvfs*/
			return count;
		}
	}

	rk33_dvfs_set_level(platform, rk33_dvfs_get_level(freq));

	platform->fix_freq = freq;  /*close dvfs*/

	return count;
}

#if RK33_DVFS_MODE
int set_freq_mode(int freq_mode)
{
        struct rk_context *platform;
        static int s_freq_mode = DVFS_MODE;
        platform = g_platform;

        if(freq_mode == s_freq_mode)
        {
                return 0;
        }

        switch (freq_mode)
        {
                case DVFS_MODE:
                        platform->fix_freq = 0;
                        printk("%s:enter dvfs mode,freq=%d\n",__FUNCTION__,platform->fix_freq);
                        break;
                case PERFORMANCE_MODE:
                        platform->fix_freq = rk33_dvfs_get_freq(RGX_DVFS_STEP - 1);
                        printk("%s:enter performance mode,freq=%d\n",__FUNCTION__,platform->fix_freq);
                        break;
                case POWER_MODE:
                        platform->fix_freq = rk33_dvfs_get_freq(0);
                        printk("%s:enter power mode,freq=%d\n",__FUNCTION__,platform->fix_freq);
                        break;
                default:
                        printk("%s:error mode=%d\n",__FUNCTION__,freq_mode);
                        return -1;
        }

        s_freq_mode = freq_mode;
        return 0;
}
#endif

static ssize_t show_utilisation(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ext_attr = to_dev_ext_attribute(attr);
	struct rk_context *platform;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_INT utilisation;
	IMG_UINT32 time_busy;
	IMG_UINT32 time_idle;
	ssize_t ret = 0;
	PVRSRV_ERROR eError = PVRSRV_OK;

	platform = (struct rk_context *)ext_attr->var;
	if (!platform || !platform->dev_config->psDevNode)
		return -ENODEV;

	if (platform->debug_level > DBG_OFF) {
		psDevInfo = platform->dev_config->psDevNode->pvDevice;

		/*Measuring GPU Utilisation*/
		eError = (psDevInfo->pfnGetGpuUtilStats(platform->dev_config->psDevNode, ghGpuUtilDvfs, &platform->sUtilStats));
		time_busy = platform->sUtilStats.ui64GpuStatActiveHigh + platform->sUtilStats.ui64GpuStatActiveLow;
		time_idle = platform->sUtilStats.ui64GpuStatIdle + platform->sUtilStats.ui64GpuStatBlocked;
		utilisation = (time_busy * 100) / (time_busy + time_idle);

		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Utilisation=%d", utilisation);
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
			     "\nDetail: ActiveHigh=%llu,ActiveLow=%llu,Blocked=%llu,Idle=%llu",
			     platform->sUtilStats.ui64GpuStatActiveHigh,
			     platform->sUtilStats.ui64GpuStatActiveLow,
			     platform->sUtilStats.ui64GpuStatBlocked,
			     platform->sUtilStats.ui64GpuStatIdle);

		if (ret < PAGE_SIZE - 1)
			ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
		else {
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

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
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

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
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

	platform = container_of(timer, struct rk_context, fps_timer);
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
	struct dev_ext_attribute *ext_attr = to_dev_ext_attribute(attr);
	struct rk_context *platform;
	static IMG_BOOL bOpen = IMG_FALSE;
	IMG_UINT  gap = FPS_DEFAULT_GAP;

	platform = (struct rk_context *)ext_attr->var;
	if (!platform)
		return -ENODEV;

    gap = simple_strtoul(buf, NULL, 10);

	if (sysfs_streq("on", buf) && !bOpen) {
		bOpen = IMG_TRUE;
		hrtimer_init(&platform->fps_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		platform->fps_timer.function = fps_callback;
		hrtimer_start(&platform->fps_timer, HR_TIMER_DELAY_MSEC(platform->fps_gap), HRTIMER_MODE_REL);
		printk("on fps\n");
	} else if (sysfs_streq("off", buf)) {
		printk("off fps\n");

		if (bOpen) {
			bOpen = IMG_FALSE;
			hrtimer_cancel(&platform->fps_timer);
		}
	} else {
		if (gap > 0  && gap < FPS_MAX_GAP)
			platform->fps_gap = gap;
	}

	return count;
}

#if RK33_DVFS_FREQ_LIMIT
static ssize_t show_up_limit(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ext_attr = to_dev_ext_attribute(attr);
	struct rk_context *platform;
	ssize_t ret = 0;
	int i;

	platform = (struct rk_context *)ext_attr->var;
	if (!platform)
		return -ENODEV;

	if (platform->up_level >= 0 && platform->up_level < RGX_DVFS_STEP)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current Up limit freq = %dMhz", rk33_dvfs_get_dvfs_up_limit_freq(platform));
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Unset the up limit level");

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings :");
	for (i = 0; i < RGX_DVFS_STEP; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ", p_rgx_dvfs_infotbl[i].clock);
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "Mhz");
	ret += snprintf(buf + ret, PAGE_SIZE - ret, ", If you want to unlock : off");

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

    return ret;
}

static ssize_t set_up_limit(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct dev_ext_attribute *ext_attr = to_dev_ext_attribute(attr);
	struct rk_context *platform;
	unsigned int freq = 0;

	platform = (struct rk_context *)ext_attr->var;
	if (!platform)
		return -ENODEV;

	freq = simple_strtoul(buf, NULL, 10);

	if (sysfs_streq("off", buf))
		rk33_dvfs_up_limit_off(platform);
	else
		rk33_dvfs_up_limit_on(platform, rk33_dvfs_get_level(freq));

	return count;
}

static ssize_t show_down_limit(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ext_attr = to_dev_ext_attribute(attr);
	struct rk_context *platform;
	ssize_t ret = 0;
	int i;

	platform = (struct rk_context *)ext_attr->var;
	if (!platform)
		return -ENODEV;

	if (platform->down_level >= 0 && platform->down_level < RGX_DVFS_STEP)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Current down limit freq = %dMhz", rk33_dvfs_get_dvfs_down_limit_freq(platform));
	else
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "Unset the down limit level");

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\nPossible settings :");
	for (i = 0; i < RGX_DVFS_STEP; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d ", p_rgx_dvfs_infotbl[i].clock);
	ret += snprintf(buf + ret, PAGE_SIZE - ret, "Mhz");
	ret += snprintf(buf + ret, PAGE_SIZE - ret, ", If you want to unlock : off");

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

static ssize_t set_down_limit(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct dev_ext_attribute *ext_attr = to_dev_ext_attribute(attr);
	struct rk_context *platform;
	unsigned int freq = 0;

	platform = (struct rk_context *)ext_attr->var;
	if (!platform)
		return -ENODEV;

	freq = simple_strtoul(buf, NULL, 10);

	if (sysfs_streq("off", buf))
		rk33_dvfs_down_limit_off(platform);
	else
		rk33_dvfs_down_limit_on(platform, rk33_dvfs_get_level(freq));

	return count;
}
#endif

/** The sysfs file @c clock, fbdev.
 *
 * This is used for obtaining information about operating clock & framebuffer address,
 */

static struct dev_ext_attribute dev_attr_freq = {
	.attr = __ATTR(freq, S_IRUGO | S_IWUSR, show_freq, set_freq),
};

static struct dev_ext_attribute dev_attr_util = {
	.attr = __ATTR(util, S_IRUGO | S_IWUSR, show_utilisation, set_utilisation),
};

static struct dev_ext_attribute dev_attr_fbdev = {
	.attr = __ATTR(fbdev, S_IRUGO, show_fbdev, NULL),
};

static struct dev_ext_attribute dev_attr_fps = {
	.attr = __ATTR(fps, S_IRUGO | S_IWUSR, show_fps, set_fps),
};

#if RK33_DVFS_FREQ_LIMIT
static struct dev_ext_attribute dev_attr_dvfs_up_limit = {
	.attr = __ATTR(dvfs_up_limit, S_IRUGO | S_IWUSR, show_up_limit, set_up_limit),
};

static struct dev_ext_attribute dev_attr_dvfs_down_limit = {
	.attr = __ATTR(dvfs_down_limit, S_IRUGO | S_IWUSR, show_down_limit, set_down_limit),
};
#endif

static IMG_INT rk_create_sysfs_file(struct rk_context *platform)
{
	struct device *dev = (struct device *)platform->dev_config->pvOSDevice;

	dev_attr_freq.var = platform;
	dev_attr_util.var = platform;
	dev_attr_fbdev.var = platform;
	dev_attr_fps.var = platform;

#if RK33_DVFS_FREQ_LIMIT
	dev_attr_dvfs_up_limit.var = platform;
	dev_attr_dvfs_down_limit.var = platform;
#endif
	
	if (device_create_file(dev, &dev_attr_freq.attr)) {
		dev_err(dev, "Couldn't create sysfs file [freq]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_util.attr)) {
		dev_err(dev, "Couldn't create sysfs file [utilisation]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_fbdev.attr)) {
		dev_err(dev, "Couldn't create sysfs file [fbdev]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_fps.attr)) {
		dev_err(dev, "Couldn't create sysfs file [fbdev]\n");
		goto out;
	}

#if RK33_DVFS_FREQ_LIMIT
	if (device_create_file(dev, &dev_attr_dvfs_up_limit.attr)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs_upper_lock]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_dvfs_down_limit.attr)) {
		dev_err(dev, "Couldn't create sysfs file [dvfs_under_lock]\n");
		goto out;
	}
#endif

	return 0;
out:
	return -ENOENT;
}

static void rk_remove_sysfs_file(struct rk_context *platform)
{
	struct device *dev = (struct device *)platform->dev_config->pvOSDevice;

	device_remove_file(dev, &dev_attr_freq.attr);
	device_remove_file(dev, &dev_attr_util.attr);
	device_remove_file(dev, &dev_attr_fbdev.attr);
	device_remove_file(dev, &dev_attr_fps.attr);
#if RK33_DVFS_FREQ_LIMIT
	device_remove_file(dev, &dev_attr_dvfs_up_limit.attr);
	device_remove_file(dev, &dev_attr_dvfs_down_limit.attr);
#endif
}



#endif //end of RK33_SYSFS_FILE_SUPPORT

#if RK33_USE_RGX_GET_GPU_UTIL
IMG_BOOL rk33_set_device_node(IMG_HANDLE hDevCookie)
{
	struct rk_context *platform = g_platform;
	unsigned long flags;

	if (platform) {
		if (hDevCookie != platform->dev_config->psDevNode) {
			printk("device node is not match,hDevCookie=%p,psDevNode=%p\n",
				hDevCookie, platform->dev_config->psDevNode);
			return IMG_FALSE;
		}

	/*start timer*/
#if USE_HRTIMER
		if (platform->dev_config->psDevNode && platform->dvfs_enabled && platform->timer.function && !platform->timer_active) {
#elif USE_KTHREAD
		if (platform->dev_config->psDevNode && platform->dvfs_enabled && !platform->timer_active) {
#endif
			spin_lock_irqsave(&platform->timer_lock, flags);
			platform->timer_active = IMG_TRUE;
			spin_unlock_irqrestore(&platform->timer_lock, flags);
			if (ghGpuUtilDvfs == NULL) {
				if (RGXRegisterGpuUtilStats(&ghGpuUtilDvfs) != PVRSRV_OK) {
					PVR_DPF((PVR_DBG_ERROR, "PVR_K:%s RGXRegisterGpuUtilStats fail\n", __func__));
					return IMG_FALSE;
				}
			}
#if USE_HRTIMER
			hrtimer_start(&platform->timer, HR_TIMER_DELAY_MSEC(RK33_DVFS_FREQ), HRTIMER_MODE_REL);
#elif USE_KTHREAD
			wake_up_process(platform->dvfs_task);
#endif
		}
	} else {
		/*call PVRSRVRegisterPowerDevice before RgxRkInit.*/
		PVR_DPF((PVR_DBG_ERROR, "PVR_K:%s platform is null\n", __func__));
		return IMG_FALSE;
	}

	return IMG_TRUE;
}

IMG_BOOL rk33_clear_device_node(void)
{
	struct rk_context *platform = g_platform;
	unsigned long flags;

	if (platform) {
		/*cancel timer*/
		if (platform->timer_active && platform->dvfs_enabled) {
			spin_lock_irqsave(&platform->timer_lock, flags);
			platform->timer_active = IMG_FALSE;
			spin_unlock_irqrestore(&platform->timer_lock, flags);
#if USE_HRTIMER
			hrtimer_cancel(&platform->timer);
#endif
		}
	} else {
		PVR_DPF((PVR_DBG_ERROR, "PVR_K:%s platform is null\n", __func__));
		return IMG_FALSE;
	}

	return IMG_TRUE;
}
#endif //RK33_USE_RGX_GET_GPU_UTIL

#endif //end of RK33_DVFS_SUPPORT

static void RgxEnableClock(struct rk_context *platform)
{
#if RK33_DVFS_SUPPORT
	unsigned long flags;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	if (!platform->gpu_clk_node && !platform->clk_gpu)
	{
		printk("gpu_clk_node and clk_gpu are both null\n");
		return;
	}
#else
	if (!platform->sclk_gpu_core)
	{
		printk("sclk_gpu_core is null\n");
		return;
	}
#endif

	if (platform->aclk_gpu_mem && platform->aclk_gpu_cfg && !platform->gpu_active) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
		if (platform->gpu_clk_node)
			dvfs_clk_prepare_enable(platform->gpu_clk_node);
		else if (platform->clk_gpu)
			clk_prepare_enable(platform->clk_gpu);
#else
		clk_prepare_enable(platform->sclk_gpu_core);
#endif
		clk_prepare_enable(platform->aclk_gpu_mem);
		clk_prepare_enable(platform->aclk_gpu_cfg);

#if RK33_DVFS_SUPPORT
		rk33_dvfs_record_gpu_active(platform);

		if (platform->dev_config->psDevNode && platform->dvfs_enabled && !platform->timer_active) {
			spin_lock_irqsave(&platform->timer_lock, flags);
			platform->timer_active = IMG_TRUE;
			spin_unlock_irqrestore(&platform->timer_lock, flags);

#if USE_HRTIMER
			hrtimer_start(&platform->timer, HR_TIMER_DELAY_MSEC(RK33_DVFS_FREQ), HRTIMER_MODE_REL);
#endif
		}
#endif
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable clock!"));
	}
}

static void RgxDisableClock(struct rk_context *platform)
{
#if RK33_DVFS_SUPPORT
	unsigned long flags;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	if (!platform->gpu_clk_node && !platform->clk_gpu) {
		printk("gpu_clk_node and clk_gpu are both null\n");
		return;
	}
#else
	if (!platform->sclk_gpu_core) {
		printk("sclk_gpu_core is null");
		return;
	}
#endif

	if (platform->aclk_gpu_mem && platform->aclk_gpu_cfg && platform->gpu_active) {
#if RK33_DVFS_SUPPORT
		if (platform->fix_freq <= 0) {
			/*Force to drop freq to the lowest.*/
			rk33_dvfs_set_level(platform, 0);
		}

		if (platform->dvfs_enabled && platform->timer_active) {
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
		if (platform->gpu_clk_node)
			dvfs_clk_disable_unprepare(platform->gpu_clk_node);
		else if (platform->clk_gpu)
			clk_disable_unprepare(platform->clk_gpu);
#else
		clk_disable_unprepare(platform->sclk_gpu_core);
#endif
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to disable clock!"));
	}
}

#if OPEN_GPU_PD
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
/*
 * The power management
 * software must power down pd_gpu_1 before power down pd_gpu_0,
 * and power up pd_gpu_1 after power up pd_gpu_0.
 */
static void RgxEnablePower(struct rk_context *platform)
{
	if (!platform->bEnablePd && platform->pd_gpu_0 && platform->pd_gpu_1) {
		clk_prepare_enable(platform->pd_gpu_0);
		clk_prepare_enable(platform->pd_gpu_1);
		platform->bEnablePd = IMG_TRUE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
	}
}

static void RgxDisablePower(struct rk_context *platform)
{
	if (platform->bEnablePd && platform->pd_gpu_0 && platform->pd_gpu_1) {
		clk_disable_unprepare(platform->pd_gpu_1);
		clk_disable_unprepare(platform->pd_gpu_0);
		platform->bEnablePd = IMG_FALSE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
	}
}
#else
static void RgxEnablePower(struct rk_context *platform)
{
	struct device *dev = (struct device *)platform->dev_config->pvOSDevice;
	if (!platform->bEnablePd) {
		pm_runtime_get_sync(dev);
		platform->bEnablePd = IMG_TRUE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
	}
}

static void RgxDisablePower(struct rk_context *platform)
{
	struct device *dev = (struct device *)platform->dev_config->pvOSDevice;

	if (platform->bEnablePd) {
		pm_runtime_put(dev);
		platform->bEnablePd = IMG_FALSE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
	}
}
#endif
#endif //end of OPEN_GPU_PD

void RgxResume(struct rk_context *platform)
{
#if OPEN_GPU_PD
	RgxEnablePower(platform);
#endif
	RgxEnableClock(platform);
 }

void RgxSuspend(struct rk_context *platform)
{
	RgxDisableClock(platform);
#if OPEN_GPU_PD
	RgxDisablePower(platform);
#endif

}

PVRSRV_ERROR RkPrePowerState(IMG_HANDLE hSysData,
							 PVRSRV_DEV_POWER_STATE eNewPowerState,
							 PVRSRV_DEV_POWER_STATE eCurrentPowerState,
							 IMG_BOOL bForced)
{
	struct rk_context *platform = (struct rk_context *)hSysData;

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON)
		RgxResume(platform);
	return PVRSRV_OK;
}

PVRSRV_ERROR RkPostPowerState(IMG_HANDLE hSysData,
							  PVRSRV_DEV_POWER_STATE eNewPowerState,
							  PVRSRV_DEV_POWER_STATE eCurrentPowerState,
							  IMG_BOOL bForced)
{
	struct rk_context *platform = (struct rk_context *)hSysData;

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
		RgxSuspend(platform);
	return PVRSRV_OK;
}

struct rk_context *RgxRkInit(PVRSRV_DEVICE_CONFIG *dev_config)
{
	struct device *dev = (struct device *)dev_config->pvOSDevice;
	struct rk_context *platform;

	platform = devm_kzalloc(dev, sizeof(struct rk_context), GFP_KERNEL);
	g_platform = platform;
	if (NULL == platform) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to kzalloc rk_context"));
		return NULL;
	}

	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;

	PVR_DPF((PVR_DBG_ERROR, "%s: dma_mask = %llx", __func__, dev->coherent_dma_mask));

#if RK_TF_VERSION
	rk_tf_check_version();
#endif

	platform->dev_config = dev_config;
	
#if OPEN_GPU_PD
	platform->bEnablePd = IMG_FALSE;
#endif
	platform->cmu_pmu_status = 0;

	spin_lock_init(&platform->cmu_pmu_lock);
	spin_lock_init(&platform->timer_lock);

#if OPEN_GPU_PD
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	platform->pd_gpu_0 = devm_clk_get(dev, "pd_gpu_0");
	if (IS_ERR_OR_NULL(platform->pd_gpu_0)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find pd_gpu_0 clock source"));
		goto fail0;
	}

	platform->pd_gpu_1 = devm_clk_get(dev, "pd_gpu_1");
	if (IS_ERR_OR_NULL(platform->pd_gpu_1)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find pd_gpu_1 clock source"));
		goto fail1;
	}
#else
	pm_runtime_enable(dev);
#endif
#endif

	platform->aclk_gpu_mem = devm_clk_get(dev, "aclk_gpu_mem");
	if (IS_ERR_OR_NULL(platform->aclk_gpu_mem)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find aclk_gpu_mem clock source"));
		goto fail2;
	}

	platform->aclk_gpu_cfg = devm_clk_get(dev, "aclk_gpu_cfg");
	if (IS_ERR_OR_NULL(platform->aclk_gpu_cfg)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find aclk_gpu_cfg clock source"));
		goto fail3;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	platform->gpu_clk_node	= clk_get_dvfs_node("clk_gpu");
	if (IS_ERR_OR_NULL(platform->gpu_clk_node))
	{
		platform->dvfs_enabled = IMG_FALSE;
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: GPU Dvfs is disabled"));
		platform->clk_gpu = devm_clk_get(dev, "clk_gpu");
		if (IS_ERR_OR_NULL(platform->clk_gpu)) {
			PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find clk_gpu clock source"));
			goto fail4;
		} else {
			rk33_clk_set_normal_node(platform->clk_gpu, RK33_DEFAULT_CLOCK);
		}
	} else {
#if RK33_DVFS_SUPPORT
		rk33_dvfs_init(platform);
#if RK33_SYSFS_FILE_SUPPORT
		/* create sysfs file node */
		rk_create_sysfs_file(platform);
#endif
#endif /* end of RK33_DVFS_SUPPORT */
		platform->dvfs_enabled = IMG_TRUE;
		rk33_clk_set_dvfs_node(platform->gpu_clk_node, RK33_DEFAULT_CLOCK);
	}
#else
	platform->sclk_gpu_core = devm_clk_get(dev, "sclk_gpu_core");
	if (IS_ERR_OR_NULL(platform->sclk_gpu_core)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find sclk_gpu_core clock source"));
		goto fail4;
	}

	platform->gpu_reg = devm_regulator_get_optional(dev, "logic");
	if (!IS_ERR_OR_NULL(platform->gpu_reg)) {
		if (dev_pm_opp_of_add_table(dev)) {
			platform->dvfs_enabled = IMG_FALSE;
			PVR_DPF((PVR_DBG_ERROR, "Invalid operating-points in device tree."));
			goto fail5;
		} else {
#if RK33_DVFS_SUPPORT
			rk33_dvfs_init(platform);
#if RK33_SYSFS_FILE_SUPPORT
			/* create sysfs file node */
			rk_create_sysfs_file(platform);
#endif
#endif /* end of RK33_DVFS_SUPPORT */
			platform->dvfs_enabled = IMG_TRUE;
		}
	}
	clk_set_rate(platform->sclk_gpu_core, RK33_DEFAULT_CLOCK * ONE_MHZ);
#endif
	clk_set_rate(platform->aclk_gpu_mem, RK33_DEFAULT_CLOCK * ONE_MHZ);
	clk_set_rate(platform->aclk_gpu_cfg, RK33_DEFAULT_CLOCK * ONE_MHZ);

	RgxResume(platform);

	return platform;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
fail5:
	devm_clk_put(dev, platform->sclk_gpu_core);
	platform->sclk_gpu_core = NULL;
#endif
fail4:
	devm_clk_put(dev, platform->aclk_gpu_cfg);
	platform->aclk_gpu_cfg = NULL;
fail3:
	devm_clk_put(dev, platform->aclk_gpu_mem);
	platform->aclk_gpu_mem = NULL;
fail2:

#if OPEN_GPU_PD && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	devm_clk_put(dev, platform->pd_gpu_1);
	platform->pd_gpu_1 = NULL;
fail1:
	devm_clk_put(dev, platform->pd_gpu_0);
	platform->pd_gpu_0 = NULL;
fail0:
	devm_kfree(dev, platform);
	return NULL;
#else
	devm_kfree(dev, platform);
	return NULL;
#endif //end of OPEN_GPU_PD

}

void RgxRkUnInit(struct rk_context *platform)
{
	struct device *dev = (struct device *)platform->dev_config->pvOSDevice;

	RgxSuspend(platform);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	if (platform->gpu_clk_node) {
		clk_put_dvfs_node(platform->gpu_clk_node);
		platform->gpu_clk_node = NULL;
	} else if (platform->clk_gpu) {
		devm_clk_put(dev, platform->clk_gpu);
		platform->clk_gpu = NULL;
	}
#else
	if (platform->sclk_gpu_core) {
		devm_clk_put(dev, platform->sclk_gpu_core);
		platform->sclk_gpu_core = NULL;
	}
#endif

	if (platform->aclk_gpu_cfg) {
		devm_clk_put(dev, platform->aclk_gpu_cfg);
		platform->aclk_gpu_cfg = NULL;
	}
	if (platform->aclk_gpu_mem) {
		devm_clk_put(dev, platform->aclk_gpu_mem);
		platform->aclk_gpu_mem = NULL;
	}
#if OPEN_GPU_PD
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	if (platform->pd_gpu_1) {
		devm_clk_put(dev, platform->pd_gpu_1);
		platform->pd_gpu_1 = NULL;
	}
	if (platform->pd_gpu_0) {
		devm_clk_put(dev, platform->pd_gpu_0);
		platform->pd_gpu_0 = NULL;
	}
#else
	pm_runtime_disable(dev);
#endif
#endif

	if (platform->dvfs_enabled) {
#if RK33_DVFS_SUPPORT
#if RK33_SYSFS_FILE_SUPPORT
		rk_remove_sysfs_file(platform);
#endif
		rk33_dvfs_term(platform);
#endif
	}
	devm_kfree(dev, platform);
}


#if defined(SUPPORT_ION)
struct ion_device *g_psIonDev;

PVRSRV_ERROR IonInit(void *phPrivateData)
{
	g_psIonDev = NULL;
	return PVRSRV_OK;
}

struct ion_device *IonDevAcquire(void)
{
	return g_psIonDev;
}

void IonDevRelease(struct ion_device *psIonDev)
{
	/* Nothing to do, sanity check the pointer we're passed back */
	PVR_ASSERT(psIonDev == g_psIonDev);
}

void IonDeinit(void)
{
	g_psIonDev = NULL;
}
#endif /* defined(SUPPORT_ION) */
