/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for a default platform
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "exynos4_pmm.h"
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#endif

#if defined(CONFIG_PM_RUNTIME)
#include <plat/pd.h>
#endif

#include <asm/io.h>
#include <mach/regs-pmu.h>

#include <linux/workqueue.h>

#define MALI_DVFS_STEPS 5
#define MALI_DVFS_WATING 10 // msec
#define MALI_DVFS_DEFAULT_STEP 3

#define MALI_DVFS_CLK_DEBUG 0
#define SEC_THRESHOLD 1
#define LWJ 1

#define CPUFREQ_LOCK_DURING_440 0

static int bMaliDvfsRun = 0;

typedef struct mali_dvfs_tableTag{
	unsigned int clock;
	unsigned int freq;
	unsigned int vol;
#if SEC_THRESHOLD
	unsigned int downthreshold;
    unsigned int upthreshold;
#endif
}mali_dvfs_table;

typedef struct mali_dvfs_statusTag{
	unsigned int currentStep;
	mali_dvfs_table * pCurrentDvfs;

} mali_dvfs_status_t;

/*dvfs status*/
mali_dvfs_status_t maliDvfsStatus;

/*dvfs table*/

mali_dvfs_table mali_dvfs[MALI_DVFS_STEPS]={
#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
			/*step 0*/{160  ,1000000    ,875000    , 0   , 70},
			/*step 1*/{266  ,1000000    ,900000    ,62   , 90},
			/*step 2*/{350  ,1000000    ,950000    ,85   , 90},
			/*step 3*/{440  ,1000000    ,1025000   ,85   , 90},
			/*step 4*/{533  ,1000000    ,1075000   ,85   ,100} };
#else
			/*step 0*/{134  ,1000000    , 950000   ,85   , 90},
			/*step 1*/{267  ,1000000    ,1050000   ,85   ,100} };
#endif


#define EXTXTALCLK_NAME  "ext_xtal"
#define VPLLSRCCLK_NAME  "vpll_src"
#define FOUTVPLLCLK_NAME "fout_vpll"
#define SCLVPLLCLK_NAME  "sclk_vpll"
#define GPUMOUT1CLK_NAME "mout_g3d1"

#define MPLLCLK_NAME     "mout_mpll"
#define GPUMOUT0CLK_NAME "mout_g3d0"
#define GPUCLK_NAME      "sclk_g3d"
#define CLK_DIV_STAT_G3D 0x1003C62C
#define CLK_DESC         "clk-divider-status"

static struct clk *ext_xtal_clock = NULL;
static struct clk *vpll_src_clock = NULL;
static struct clk *fout_vpll_clock = NULL;
static struct clk *sclk_vpll_clock = NULL;

static struct clk *mpll_clock = NULL;
static struct clk *mali_parent_clock = NULL;
static struct clk *mali_clock = NULL;

#if defined(CONFIG_CPU_EXYNOS4412) || defined(CONFIG_CPU_EXYNOS4212)
/* Pegasus */
static const mali_bool bis_vpll  = MALI_TRUE;
unsigned int mali_gpu_clk = 440;
unsigned int mali_gpu_vol = 1025000;
#else
/* Orion */
static const mali_bool bis_vpll  = MALI_FALSE;
int mali_gpu_clk                 = 267;
static unsigned int mali_gpu_vol = 1050000; /* 1.05V */
#endif

static unsigned int GPU_MHZ	=		1000000;

int  gpu_power_state;
static int bPoweroff;

#ifdef CONFIG_REGULATOR
struct regulator *g3d_regulator = NULL;
#endif

mali_io_address clk_register_map = 0;

/* DVFS */
static unsigned int mali_dvfs_utilization = 255;

static void mali_dvfs_work_handler(struct work_struct *w);

static struct workqueue_struct *mali_dvfs_wq = 0;

extern mali_io_address clk_register_map;

_mali_osk_lock_t *mali_dvfs_lock = 0; 

static DECLARE_WORK(mali_dvfs_work, mali_dvfs_work_handler);


/* export GPU frequency as a read-only parameter so that it can be read in /sys */
module_param(mali_gpu_clk, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_gpu_clk, "GPU frequency in MHz");

#ifdef CONFIG_REGULATOR
void mali_regulator_disable(void)
{
	if(IS_ERR_OR_NULL(g3d_regulator))
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_disable : g3d_regulator is null\n"));
		return;
	}
	regulator_disable(g3d_regulator);
}

void mali_regulator_enable(void)
{
	if(IS_ERR_OR_NULL(g3d_regulator))
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_enable : g3d_regulator is null\n"));
		return;
	}
	regulator_enable(g3d_regulator);
}

void mali_regulator_set_voltage(int min_uV, int max_uV)
{
	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	if(IS_ERR_OR_NULL(g3d_regulator))
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_set_voltage : g3d_regulator is null\n"));
		return;
	}
	MALI_PRINT(("= regulator_set_voltage: %d, %d \n",min_uV, max_uV));
	regulator_set_voltage(g3d_regulator, min_uV, max_uV);
	mali_gpu_vol = regulator_get_voltage(g3d_regulator);
	MALI_DEBUG_PRINT(1, ("Mali voltage: %d\n", mali_gpu_vol));
	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
}
#endif

unsigned long mali_clk_get_rate(void)
{
//	return clk_get_rate(mali_clock);
#if LWJ
	return maliDvfsStatus.currentStep;
#endif
}


static unsigned int get_mali_dvfs_status(void)
{
	return maliDvfsStatus.currentStep;
}

mali_bool mali_clk_get(void)
{
	if (bis_vpll)
	{
		if (ext_xtal_clock == NULL)
		{
			ext_xtal_clock = clk_get(NULL,EXTXTALCLK_NAME);
			if (IS_ERR(ext_xtal_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source ext_xtal_clock\n"));
				return MALI_FALSE;
			}
		}

		if (vpll_src_clock == NULL)
		{
			vpll_src_clock = clk_get(NULL,VPLLSRCCLK_NAME);
			if (IS_ERR(vpll_src_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source vpll_src_clock\n"));
				return MALI_FALSE;
			}
		}

		if (fout_vpll_clock == NULL)
		{
			fout_vpll_clock = clk_get(NULL,FOUTVPLLCLK_NAME);
			if (IS_ERR(fout_vpll_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source fout_vpll_clock\n"));
				return MALI_FALSE;
			}
		}

		if (sclk_vpll_clock == NULL)
		{
			sclk_vpll_clock = clk_get(NULL,SCLVPLLCLK_NAME);
			if (IS_ERR(sclk_vpll_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source sclk_vpll_clock\n"));
				return MALI_FALSE;
			}
		}

		if (mali_parent_clock == NULL)
		{
			mali_parent_clock = clk_get(NULL, GPUMOUT1CLK_NAME);

			if (IS_ERR(mali_parent_clock)) {
				MALI_PRINT( ( "MALI Error : failed to get source mali parent clock\n"));
				return MALI_FALSE;
			}
		}
	}
	else // mpll
	{
		if (mpll_clock == NULL)
		{
			mpll_clock = clk_get(NULL,MPLLCLK_NAME);

			if (IS_ERR(mpll_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source mpll clock\n"));
				return MALI_FALSE;
			}
		}

		if (mali_parent_clock == NULL)
		{
			mali_parent_clock = clk_get(NULL, GPUMOUT0CLK_NAME);

			if (IS_ERR(mali_parent_clock)) {
				MALI_PRINT( ( "MALI Error : failed to get source mali parent clock\n"));
				return MALI_FALSE;
			}
		}
	}

	// mali clock get always.
	if (mali_clock == NULL)
	{
		mali_clock = clk_get(NULL, GPUCLK_NAME);

		if (IS_ERR(mali_clock)) {
			MALI_PRINT( ("MALI Error : failed to get source mali clock\n"));
			return MALI_FALSE;
		}
	}

	return MALI_TRUE;
}

void mali_clk_put(mali_bool binc_mali_clock)
{
	if (mali_parent_clock)
	{
		clk_put(mali_parent_clock);
		mali_parent_clock = NULL;
	}

	if (mpll_clock)
	{
		clk_put(mpll_clock);
		mpll_clock = NULL;
	}

	if (sclk_vpll_clock)
	{
		clk_put(sclk_vpll_clock);
		sclk_vpll_clock = NULL;
	}

	if (binc_mali_clock && fout_vpll_clock)
	{
		clk_put(fout_vpll_clock);
		fout_vpll_clock = NULL;
	}

	if (vpll_src_clock)
	{
		clk_put(vpll_src_clock);
		vpll_src_clock = NULL;
	}

	if (ext_xtal_clock)
	{
		clk_put(ext_xtal_clock);
		ext_xtal_clock = NULL;
	}

	if (binc_mali_clock && mali_clock)
	{
		clk_put(mali_clock);
		mali_clock = NULL;
	}
}

void mali_clk_set_rate(unsigned int clk, unsigned int mhz)
{
	int err;
	unsigned long rate = (unsigned long)clk * (unsigned long)mhz;

	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	MALI_DEBUG_PRINT(3, ("Mali platform: Setting frequency to %d mhz\n", clk));
	if (bis_vpll && fout_vpll_clock)
	{
		err = clk_set_rate(fout_vpll_clock, (unsigned int)mali_gpu_clk * GPU_MHZ);
		if (err) MALI_PRINT_ERROR(("Failed to set vpll: %d\n", err));
	}
	err = clk_set_rate(mali_clock, rate);
	if (err) MALI_PRINT_ERROR(("Failed to set Mali clock: %d\n", err));

	rate = mali_clk_get_rate();

	MALI_PRINT(("Mali frequency %d\n", rate / mhz));
	GPU_MHZ = mhz;
	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
}

mali_bool set_mali_dvfs_current_step(unsigned int step)
{
	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	maliDvfsStatus.currentStep = step % MALI_DVFS_STEPS;
//	if (step >= MAX_MALI_DVFS_STEPS) //LWJ
//		mali_runtime_resumed = maliDvfsStatus.currentStep;
	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	return MALI_TRUE;
}


static mali_bool set_mali_dvfs_status(u32 step,mali_bool boostup)
{
	u32 validatedStep=step;
#if MALI_DVFS_CLK_DEBUG
	unsigned int *pRegMaliClkDiv;
	unsigned int *pRegMaliMpll;
#endif

	if(boostup)
	{
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(mali_dvfs[step].vol, mali_dvfs[step].vol);
#endif
		/*change the clock*/
		mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);
	}
	else
	{
		/*change the clock*/
		mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(mali_dvfs[step].vol, mali_dvfs[step].vol);
#endif
	}

#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE| MALI_PROFILING_EVENT_CHANNEL_GPU|MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,mali_gpu_clk, mali_gpu_vol/1000, 0, 0, 0);
#endif
	mali_clk_put(MALI_FALSE);

#if MALI_DVFS_CLK_DEBUG
	pRegMaliClkDiv = ioremap(0x1003c52c,32);
	pRegMaliMpll = ioremap(0x1003c22c,32);
	MALI_PRINT( ("Mali MPLL reg:%d, CLK DIV: %d \n",*pRegMaliMpll, *pRegMaliClkDiv));
#endif

#ifdef EXYNOS4_ASV_ENABLED
	if (samsung_rev() < EXYNOS4412_REV_2_0) {
		if (mali_dvfs[step].clock == 160)
			exynos4x12_set_abb_member(ABB_G3D, ABB_MODE_100V);
		else
			exynos4x12_set_abb_member(ABB_G3D, ABB_MODE_130V);
	}
#endif


//	maliDvfsStatus.currentStep = validatedStep;
	set_mali_dvfs_current_step(validatedStep);
	/*for future use*/
	maliDvfsStatus.pCurrentDvfs = &mali_dvfs[validatedStep];

#if CPUFREQ_LOCK_DURING_440
	/* lock/unlock CPU freq by Mali */
	if (mali_dvfs[step].clock == 440)
		err = cpufreq_lock_by_mali(1200);
	else
		cpufreq_unlock_by_mali();
#endif


	return MALI_TRUE;
}

static void mali_platform_wating(u32 msec)
{
	/*sample wating
	change this in the future with proper check routine.
	*/
	unsigned int read_val;
	while(1)
	{
		read_val = _mali_osk_mem_ioread32(clk_register_map, 0x00);
		if ((read_val & 0x8000)==0x0000) break;

		_mali_osk_time_ubusydelay(100); /* 1000 -> 100 : 20101218 */
	}
	/* _mali_osk_time_ubusydelay(msec*1000);*/
}

static mali_bool change_mali_dvfs_status(u32 step, mali_bool boostup )
{
	MALI_DEBUG_PRINT(4, ("> change_mali_dvfs_status: %d, %d \n",step, boostup));

	if(!set_mali_dvfs_status(step, boostup))
	{
		MALI_DEBUG_PRINT(1, ("error on set_mali_dvfs_status: %d, %d \n",step, boostup));
		return MALI_FALSE;
	}

	/*wait until clock and voltage is stablized*/
	mali_platform_wating(MALI_DVFS_WATING); /*msec*/

	return MALI_TRUE;
}

static unsigned int decideNextStatus(unsigned int utilization)
{
	unsigned int level=0; /* 0:stay, 1:up */
#if 0
	if( utilization>127 && maliDvfsStatus.currentStep==0 ) /* over 60% -> 50% (up fast) */
		level=1;
	else if( utilization<51 && maliDvfsStatus.currentStep==1 ) /* under 30% -> 20% (down slow) */
		level=0;
	else
		level = maliDvfsStatus.currentStep;
#else

	level = maliDvfsStatus.currentStep;
//	if (maliDvfsStatus.currentStep) {
		if (utilization > (int)(255 * mali_dvfs[maliDvfsStatus.currentStep].upthreshold / 100) &&
				level < MALI_DVFS_STEPS - 1) {
			level++;
//			if ((samsung_rev() < EXYNOS4412_REV_2_0) && (maliDvfsStatus.currentStep == 3)) {
//				level=get_mali_dvfs_status();
//			}
		}
		if (utilization < (int)(255 * mali_dvfs[maliDvfsStatus.currentStep].downthreshold / 100) &&
				level > 0) {
			level--;
		}
//	} 
#endif
	printk("LWJ %s Level %d\n", __func__, level);
	return level;
}

static mali_bool mali_dvfs_status(unsigned int utilization)
{
	unsigned int nextStatus=0;
	unsigned int curStatus=0;
	mali_bool boostup=0;

	MALI_DEBUG_PRINT(4, ("> mali_dvfs_status: %d \n",utilization));

	/*decide next step*/
	curStatus = get_mali_dvfs_status();
	printk("LWJ curStatus %d\n",curStatus);
	nextStatus = decideNextStatus(utilization);

	MALI_DEBUG_PRINT(4, ("= curStatus %d, nextStatus %d, maliDvfsStatus.currentStep %d \n", curStatus, nextStatus, maliDvfsStatus.currentStep));
	/*if next status is same with current status, don't change anything*/
	if(curStatus!=nextStatus)
	{
		/*check if boost up or not*/
		if(nextStatus > maliDvfsStatus.currentStep) boostup = 1;

		/*change mali dvfs status*/
		if(!change_mali_dvfs_status(nextStatus,boostup))
		{
			MALI_DEBUG_PRINT(1, ("error on change_mali_dvfs_status \n"));
			return MALI_FALSE;
		}
	}
	return MALI_TRUE;
}


int mali_dvfs_is_running(void)
{
	return bMaliDvfsRun;
}


static void mali_dvfs_work_handler(struct work_struct *w)
{
	bMaliDvfsRun=1;

	MALI_DEBUG_PRINT(3, ("=== mali_dvfs_work_handler\n"));

	if(!mali_dvfs_status(mali_dvfs_utilization))
	MALI_DEBUG_PRINT(1,( "error on mali dvfs status in mali_dvfs_work_handler"));

	bMaliDvfsRun=0;
}


mali_bool init_mali_dvfs_status(void)
{
	/*default status
	add here with the right function to get initilization value.
	*/

	if (!mali_dvfs_wq)
	{
		mali_dvfs_wq = create_singlethread_workqueue("mali_dvfs");
	}

	/*add a error handling here*/
	maliDvfsStatus.currentStep = MALI_DVFS_DEFAULT_STEP;

	return MALI_TRUE;
}

void deinit_mali_dvfs_status(void)
{
	if (mali_dvfs_wq)
	{
		destroy_workqueue(mali_dvfs_wq);
		mali_dvfs_wq = NULL;
	}
}

mali_bool mali_dvfs_handler(unsigned int utilization)
{
	mali_dvfs_utilization = utilization;
	queue_work(mali_dvfs_wq,&mali_dvfs_work);

	/*add error handle here*/

	return MALI_TRUE;
}

static mali_bool init_mali_clock(void)
{
	mali_bool ret = MALI_TRUE;
	gpu_power_state = 0;
	bPoweroff = 1;

	if (mali_clock != 0)
		return ret; /* already initialized */

	mali_dvfs_lock = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE
			| _MALI_OSK_LOCKFLAG_ONELOCK, 0, 0);
	if (mali_dvfs_lock == NULL)
		return _MALI_OSK_ERR_FAULT;



	if (!mali_clk_get())
	{
		MALI_PRINT(("Error: Failed to get Mali clock\n"));
		goto err_clk;
	}

	if (bis_vpll)
	{
		clk_set_parent(vpll_src_clock, ext_xtal_clock);
		clk_set_parent(sclk_vpll_clock, fout_vpll_clock);

		clk_set_parent(mali_parent_clock, sclk_vpll_clock);
		clk_set_parent(mali_clock, mali_parent_clock);
	}
	else
	{
		clk_set_parent(mali_parent_clock, mpll_clock);
		clk_set_parent(mali_clock, mali_parent_clock);
	}

	if (clk_enable(mali_clock) < 0)
	{
		MALI_PRINT(("Error: Failed to enable clock\n"));
		goto err_clk;
	}

	mali_clk_set_rate((unsigned int)mali_gpu_clk, GPU_MHZ);

	MALI_PRINT(("init_mali_clock mali_clock %x\n", mali_clock));

#ifdef CONFIG_REGULATOR
	g3d_regulator = regulator_get(NULL, "vdd_g3d");

	if (IS_ERR(g3d_regulator))
	{
		MALI_PRINT( ("MALI Error : failed to get vdd_g3d\n"));
		ret = MALI_FALSE;
		goto err_regulator;
	}

	regulator_enable(g3d_regulator);
	mali_regulator_set_voltage(mali_gpu_vol, mali_gpu_vol);
#endif

#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE|
			MALI_PROFILING_EVENT_CHANNEL_GPU|MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
			mali_gpu_clk, mali_gpu_vol/1000, 0, 0, 0);
#endif

	mali_clk_put(MALI_FALSE);

	return MALI_TRUE;

#ifdef CONFIG_REGULATOR
err_regulator:
	regulator_put(g3d_regulator);
#endif
err_clk:
	mali_clk_put(MALI_TRUE);

	return ret;
}

static mali_bool deinit_mali_clock(void)
{
	if (mali_clock == 0)
		return MALI_TRUE;

#ifdef CONFIG_REGULATOR
	if (g3d_regulator)
	{
		regulator_put(g3d_regulator);
		g3d_regulator = NULL;
	}
#endif

	mali_clk_put(MALI_TRUE);

	return MALI_TRUE;
}


static _mali_osk_errcode_t enable_mali_clocks(void)
{
	int err;
	err = clk_enable(mali_clock);
	MALI_DEBUG_PRINT(3,("enable_mali_clocks mali_clock %p error %d \n", mali_clock, err));

	/* set clock rate */
	mali_clk_set_rate((unsigned int)mali_gpu_clk, GPU_MHZ);

	maliDvfsStatus.currentStep = MALI_DVFS_DEFAULT_STEP;

	MALI_SUCCESS;
}

static _mali_osk_errcode_t disable_mali_clocks(void)
{
	clk_disable(mali_clock);
	MALI_DEBUG_PRINT(3,("disable_mali_clocks mali_clock %p \n", mali_clock));

	MALI_SUCCESS;
}

/* Some defines changed names in later Odroid-A kernels. Make sure it works for both. */
#ifndef S5P_G3D_CONFIGURATION
#define S5P_G3D_CONFIGURATION S5P_PMU_G3D_CONF
#endif
#ifndef S5P_G3D_STATUS
#define S5P_G3D_STATUS S5P_PMU_G3D_CONF + 0x4
#endif

_mali_osk_errcode_t g3d_power_domain_control(int bpower_on)
{
	if (bpower_on)
	{
		void __iomem *status;
		u32 timeout;
		__raw_writel(EXYNOS_INT_LOCAL_PWR_EN, S5P_G3D_CONFIGURATION);
		status = S5P_G3D_STATUS;

		timeout = 10;
		while ((__raw_readl(status) & EXYNOS_INT_LOCAL_PWR_EN)
			!= EXYNOS_INT_LOCAL_PWR_EN) {
			if (timeout == 0) {
				MALI_PRINTF(("Power domain  enable failed.\n"));
				return -ETIMEDOUT;
			}
			timeout--;
			_mali_osk_time_ubusydelay(100);
		}
	}
	else
	{
		void __iomem *status;
		u32 timeout;
		__raw_writel(0, S5P_G3D_CONFIGURATION);

		status = S5P_G3D_STATUS;
		/* Wait max 1ms */
		timeout = 10;
		while (__raw_readl(status) & EXYNOS_INT_LOCAL_PWR_EN)
		{
			if (timeout == 0) {
				MALI_PRINTF(("Power domain  disable failed.\n" ));
				return -ETIMEDOUT;
			}
			timeout--;
			_mali_osk_time_ubusydelay( 100);
		}
	}

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_init(void)
{
	MALI_CHECK(init_mali_clock(), _MALI_OSK_ERR_FAULT);
#ifdef CONFIG_MALI_DVFS
	if (!clk_register_map) clk_register_map = _mali_osk_mem_mapioregion( CLK_DIV_STAT_G3D, 0x20, CLK_DESC );
	if(!init_mali_dvfs_status())
		MALI_DEBUG_PRINT(1, ("mali_platform_init failed\n"));
#endif
	mali_platform_power_mode_change(MALI_POWER_MODE_ON);

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit(void)
{

	mali_platform_power_mode_change(MALI_POWER_MODE_DEEP_SLEEP);
	deinit_mali_clock();

#ifdef CONFIG_MALI_DVFS
	deinit_mali_dvfs_status();
	if (clk_register_map )
	{
		_mali_osk_mem_unmapioregion(CLK_DIV_STAT_G3D, 0x20, clk_register_map);
		clk_register_map = NULL;
	}
#endif

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
	switch (power_mode)
	{
		case MALI_POWER_MODE_ON:
			MALI_DEBUG_PRINT(3, ("Mali platform: Got MALI_POWER_MODE_ON event, %s\n",
			                     bPoweroff ? "powering on" : "already on"));
			if (bPoweroff == 1)
			{
#if !defined(CONFIG_PM_RUNTIME)
				g3d_power_domain_control(1);
#endif
				MALI_DEBUG_PRINT(4,("enable clock \n"));
				enable_mali_clocks();
#if defined(CONFIG_MALI400_PROFILING)
				_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
						MALI_PROFILING_EVENT_CHANNEL_GPU |
						MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE, mali_gpu_clk,
						mali_gpu_vol/1000, 0, 0, 0);

#endif
				bPoweroff=0;
			}
			break;
		case MALI_POWER_MODE_LIGHT_SLEEP:
		case MALI_POWER_MODE_DEEP_SLEEP:
			MALI_DEBUG_PRINT(3, ("Mali platform: Got %s event, %s\n", power_mode ==
						MALI_POWER_MODE_LIGHT_SLEEP ?  "MALI_POWER_MODE_LIGHT_SLEEP" :
						"MALI_POWER_MODE_DEEP_SLEEP", bPoweroff ? "already off" : "powering off"));
			if (bPoweroff == 0)
			{
				disable_mali_clocks();
#if defined(CONFIG_MALI400_PROFILING)
				_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
						MALI_PROFILING_EVENT_CHANNEL_GPU |
						MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE, 0, 0, 0, 0, 0);
#endif

#if !defined(CONFIG_PM_RUNTIME)
				g3d_power_domain_control(0);
#endif
				bPoweroff=1;
			}

			break;
	}
	MALI_SUCCESS;
}

void mali_gpu_utilization_handler(unsigned int utilization)
{
	printk("LWJ utilization %d\n",utilization);
	if (bPoweroff==0)
	{
#ifdef CONFIG_MALI_DVFS
		if(!mali_dvfs_handler(utilization))
			MALI_DEBUG_PRINT(1,( "error on mali dvfs status in utilization\n"));
#endif
	}
}


/*REGISTER
 Enable Power
 pPowerPTR = ioremap(0x10023C60,32);
 *pPowerPTR |= 0x7;

 Enable Clock
 pPTR = ioremap(0x1003C92C,32);
 *pPTR |= 0x1;
*/

