/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform_dvfs.c
 * Platform specific Mali driver dvfs functions
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>

#include <asm/io.h>

#ifdef CONFIG_CPU_FREQ
#include <mach/asv.h>
#include <mach/regs-pmu.h>
#define EXYNOS4_ASV_ENABLED
#endif

#include "mali_device_pause_resume.h"
#include <linux/workqueue.h>

#define MALI_DVFS_WATING 10 // msec

static int bMaliDvfsRun=0;

#if MALI_GPU_BOTTOM_LOCK
static _mali_osk_atomic_t bottomlock_status;
#endif

typedef struct mali_dvfs_tableTag{
	unsigned int clock;
	unsigned int freq;
	unsigned int vol;
}mali_dvfs_table;

typedef struct mali_dvfs_statusTag{
	unsigned int currentStep;
	mali_dvfs_table * pCurrentDvfs;

}mali_dvfs_currentstatus;

typedef struct mali_dvfs_thresholdTag{
	unsigned int downthreshold;
	unsigned int upthreshold;
}mali_dvfs_threshold_table;

typedef struct mali_dvfs_staycount{
	unsigned int staycount;
}mali_dvfs_staycount_table;

mali_dvfs_staycount_table mali_dvfs_staycount[MALI_DVFS_STEPS]={
	/*step 0*/{1},
	/*step 1*/{1},};

/*dvfs threshold*/
mali_dvfs_threshold_table mali_dvfs_threshold[MALI_DVFS_STEPS]={
	/*step 0*/{((int)((255*0)/100))   ,((int)((255*65)/100))},
	/*step 1*/{((int)((255*30)/100))  ,((int)((255*100)/100))}};

/*dvfs status*/
mali_dvfs_currentstatus maliDvfsStatus;
int mali_dvfs_control=0;

/*dvfs table*/
mali_dvfs_table mali_dvfs[MALI_DVFS_STEPS]={
	/*step 0*/{160  ,1000000    , 950000},
	/*step 1*/{267  ,1000000    ,1000000} };

#ifdef EXYNOS4_ASV_ENABLED

#define ASV_8_LEVEL	8
#define ASV_5_LEVEL	5
#define ASV_LEVEL_SUPPORT 0

static unsigned int asv_3d_volt_5_table[ASV_5_LEVEL][MALI_DVFS_STEPS] = {
	/* L3(160MHz), L2(266MHz) */
	{1000000, 1100000},	/* S */
	{1000000, 1100000},	/* A */
	{ 950000, 1000000},	/* B */
	{ 950000, 1000000},	/* C */
	{ 950000,  950000},	/* D */
};

static unsigned int asv_3d_volt_8_table[ASV_8_LEVEL][MALI_DVFS_STEPS] = {
	/* L3(160MHz), L2(266MHz)) */
	{1000000, 1100000},	/* SS */
	{1000000, 1100000},	/* A1 */
	{1000000, 1100000},	/* A2 */
	{ 950000, 1000000},	/* B1 */
	{ 950000, 1000000},	/* B2 */
	{ 950000, 1000000},	/* C1 */
	{ 950000, 1000000},	/* C2 */
	{ 950000,  950000},	/* D1 */
};
#endif

static u32 mali_dvfs_utilization = 255;

static void mali_dvfs_work_handler(struct work_struct *w);

static struct workqueue_struct *mali_dvfs_wq = 0;
extern mali_io_address clk_register_map;

#if MALI_GPU_BOTTOM_LOCK
extern _mali_osk_lock_t *mali_dvfs_lock;
#endif

static DECLARE_WORK(mali_dvfs_work, mali_dvfs_work_handler);

static unsigned int get_mali_dvfs_status(void)
{
	return maliDvfsStatus.currentStep;
}

#if MALI_GPU_BOTTOM_LOCK
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
int get_mali_dvfs_control_status(void)
{
	return mali_dvfs_control;
}

mali_bool set_mali_dvfs_current_step(unsigned int step)
{
	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	maliDvfsStatus.currentStep = step;
	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	return MALI_TRUE;
}
#endif
#endif

static mali_bool set_mali_dvfs_status(u32 step,mali_bool boostup)
{
	u32 validatedStep=step;

#ifdef CONFIG_REGULATOR
	if (mali_regulator_get_usecount()==0) {
		MALI_DEBUG_PRINT(1, ("regulator use_count is 0 \n"));
		return MALI_FALSE;
	}
#endif

	if (boostup) {
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(mali_dvfs[step].vol, mali_dvfs[step].vol);
#endif
		/*change the clock*/
		mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);
	} else {
		/*change the clock*/
		mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(mali_dvfs[step].vol, mali_dvfs[step].vol);
#endif
	}

	maliDvfsStatus.currentStep = validatedStep;
	/*for future use*/
	maliDvfsStatus.pCurrentDvfs = &mali_dvfs[validatedStep];

	return MALI_TRUE;
}

static void mali_platform_wating(u32 msec)
{
	/*sample wating
	change this in the future with proper check routine.
	*/
	unsigned int read_val;
	while(1) {
		read_val = _mali_osk_mem_ioread32(clk_register_map, 0x00);
		if ((read_val & 0x8000)==0x0000) break;

		_mali_osk_time_ubusydelay(100); // 1000 -> 100 : 20101218
	}
	/* _mali_osk_time_ubusydelay(msec*1000);*/
}

static mali_bool change_mali_dvfs_status(u32 step, mali_bool boostup )
{

	MALI_DEBUG_PRINT(1, ("> change_mali_dvfs_status: %d, %d \n",step, boostup));

	if (!set_mali_dvfs_status(step, boostup)) {
		MALI_DEBUG_PRINT(1, ("error on set_mali_dvfs_status: %d, %d \n",step, boostup));
		return MALI_FALSE;
	}

	/*wait until clock and voltage is stablized*/
	mali_platform_wating(MALI_DVFS_WATING); /*msec*/

	return MALI_TRUE;
}

static unsigned int decideNextStatus(unsigned int utilization)
{
	unsigned int level=0; // 0:stay, 1:up

	if (!mali_dvfs_control) {
#if MALI_GPU_BOTTOM_LOCK
		if (_mali_osk_atomic_read(&bottomlock_status) > 0)
			level = 1;	/* or bigger */
		else if (utilization > mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold)
#else
		if (utilization > mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold)
#endif
			level=1;
		else if (utilization < mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold)
			level=0;
		else
			level = maliDvfsStatus.currentStep;
	} else	{
		if ((mali_dvfs_control > 0) && (mali_dvfs_control < mali_dvfs[1].clock))
			level=0;
		else
			level=1;
	}

	return level;
}

#ifdef EXYNOS4_ASV_ENABLED
static mali_bool mali_dvfs_table_update(void)
{
	unsigned int exynos_result_of_asv_group;
	unsigned int i;
	exynos_result_of_asv_group = exynos_result_of_asv & 0xf;
	MALI_PRINT(("exynos_result_of_asv_group = 0x%x\n", exynos_result_of_asv_group));

	if (ASV_LEVEL_SUPPORT) { //asv level information will be added.
		for (i = 0; i < MALI_DVFS_STEPS; i++) {
			mali_dvfs[i].vol = asv_3d_volt_5_table[exynos_result_of_asv_group][i];
			MALI_PRINT(("mali_dvfs[%d].vol = %d\n", i, mali_dvfs[i].vol));
		}
	} else {
		for (i = 0; i < MALI_DVFS_STEPS; i++) {
			mali_dvfs[i].vol = asv_3d_volt_8_table[exynos_result_of_asv_group][i];
			MALI_PRINT(("mali_dvfs[%d].vol = %d\n", i, mali_dvfs[i].vol));
		}
	}

	return MALI_TRUE;

}
#endif

static mali_bool mali_dvfs_status(u32 utilization)
{
	unsigned int nextStatus = 0;
	unsigned int curStatus = 0;
	mali_bool boostup = MALI_FALSE;
#ifdef EXYNOS4_ASV_ENABLED
	static mali_bool asv_applied = MALI_FALSE;
#endif
	static int stay_count = 0; // to prevent frequent switch

	MALI_DEBUG_PRINT(1, ("> mali_dvfs_status: %d \n",utilization));
#ifdef EXYNOS4_ASV_ENABLED
	if (asv_applied == MALI_FALSE) {
		mali_dvfs_table_update();
		change_mali_dvfs_status(0,0);
		asv_applied = MALI_TRUE;

		return MALI_TRUE;
	}
#endif

	/*decide next step*/
	curStatus = get_mali_dvfs_status();
	nextStatus = decideNextStatus(utilization);

	MALI_DEBUG_PRINT(1, ("= curStatus %d, nextStatus %d, maliDvfsStatus.currentStep %d \n", curStatus, nextStatus, maliDvfsStatus.currentStep));

	/*if next status is same with current status, don't change anything*/
	if ((curStatus!=nextStatus && stay_count==0)) {
		/*check if boost up or not*/
		if (nextStatus > maliDvfsStatus.currentStep)
			boostup = 1;

		/*change mali dvfs status*/
		if (!change_mali_dvfs_status(nextStatus,boostup)) {
			MALI_DEBUG_PRINT(1, ("error on change_mali_dvfs_status \n"));
			return MALI_FALSE;
		}
		stay_count = mali_dvfs_staycount[maliDvfsStatus.currentStep].staycount;
	} else {
		if (stay_count>0)
			stay_count--;
	}

	return MALI_TRUE;
}



int mali_dvfs_is_running(void)
{
	return bMaliDvfsRun;
}



void mali_dvfs_late_resume(void)
{
	// set the init clock as low when resume
	set_mali_dvfs_status(0,0);
}


static void mali_dvfs_work_handler(struct work_struct *w)
{
	bMaliDvfsRun=1;

	MALI_DEBUG_PRINT(3, ("=== mali_dvfs_work_handler\n"));

	if (!mali_dvfs_status(mali_dvfs_utilization))
		MALI_DEBUG_PRINT(1,( "error on mali dvfs status in mali_dvfs_work_handler"));

	bMaliDvfsRun=0;
}


mali_bool init_mali_dvfs_status(int step)
{
	/*default status
	add here with the right function to get initilization value.
	*/
	if (!mali_dvfs_wq)
		mali_dvfs_wq = create_singlethread_workqueue("mali_dvfs");

#if MALI_GPU_BOTTOM_LOCK
	_mali_osk_atomic_init(&bottomlock_status, 0);
#endif

	/*add a error handling here*/
	maliDvfsStatus.currentStep = step;

	return MALI_TRUE;
}

void deinit_mali_dvfs_status(void)
{
#if MALI_GPU_BOTTOM_LOCK
	_mali_osk_atomic_term(&bottomlock_status);
#endif

	if (mali_dvfs_wq)
		destroy_workqueue(mali_dvfs_wq);
	mali_dvfs_wq = NULL;
}

mali_bool mali_dvfs_handler(u32 utilization)
{
	mali_dvfs_utilization = utilization;
	queue_work_on(0, mali_dvfs_wq,&mali_dvfs_work);

	/*add error handle here*/
	return MALI_TRUE;
}

void mali_default_step_set(int step, mali_bool boostup)
{
	mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);

	if (maliDvfsStatus.currentStep == 1)
		set_mali_dvfs_status(step, boostup);
}

#if MALI_GPU_BOTTOM_LOCK
int mali_dvfs_bottom_lock_push(void)
{
	int prev_status = _mali_osk_atomic_read(&bottomlock_status);

	if (prev_status < 0) {
		MALI_PRINT(("gpu bottom lock status is not valid for push"));
		return -1;
	}

	if (prev_status == 0) {
		mali_regulator_set_voltage(mali_dvfs[1].vol, mali_dvfs[1].vol);
		mali_clk_set_rate(mali_dvfs[1].clock, mali_dvfs[1].freq);
		set_mali_dvfs_current_step(1);
	}

	return _mali_osk_atomic_inc_return(&bottomlock_status);
}

int mali_dvfs_bottom_lock_pop(void)
{
	if (_mali_osk_atomic_read(&bottomlock_status) <= 0) {
		MALI_PRINT(("gpu bottom lock status is not valid for pop"));
		return -1;
	}

	return _mali_osk_atomic_dec_return(&bottomlock_status);
}
#endif
