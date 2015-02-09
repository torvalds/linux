/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
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

#if 0

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>

#include <asm/io.h>

#include "mali_device_pause_resume.h"
#include <linux/workqueue.h>

#define MAX_MALI_DVFS_STEPS 5
#define MALI_DVFS_WATING 10 // msec

#ifdef CONFIG_EXYNOS_ASV
#include <mach/asv.h>
#include <plat/cpu.h>
#endif

static int bMaliDvfsRun = 0;

typedef struct mali_dvfs_tableTag {
	unsigned int clock;
	unsigned int freq;
	unsigned int vol;
} mali_dvfs_table;

typedef struct mali_dvfs_statusTag {
	unsigned int currentStep;
	mali_dvfs_table * pCurrentDvfs;

} mali_dvfs_currentstatus;

typedef struct mali_dvfs_thresholdTag {
	unsigned int downthreshold;
	unsigned int upthreshold;
}m ali_dvfs_threshold_table;

typedef struct mali_dvfs_staycount {
	unsigned int staycount;
} mali_dvfs_staycount_table;

typedef struct mali_dvfs_stepTag {
	int clk;
	int vol;
} mali_dvfs_step;

mali_dvfs_step step[MALI_DVFS_STEPS] = {
	/*step 0 clk*/ {160,   875000},
#if (MALI_DVFS_STEPS > 1)
	/*step 1 clk*/ {266,   900000},
#if (MALI_DVFS_STEPS > 2)
	/*step 2 clk*/ {350,   950000},
#if (MALI_DVFS_STEPS > 3)
	/*step 3 clk*/ {440,  1025000},
#if (MALI_DVFS_STEPS > 4)
	/*step 4 clk*/ {533,  1075000}
#endif
#endif
#endif
#endif
};

mali_dvfs_staycount_table mali_dvfs_staycount[MALI_DVFS_STEPS] = {
	/*step 0*/{1},
#if (MALI_DVFS_STEPS > 1)
	/*step 1*/{1},
#if (MALI_DVFS_STEPS > 2)
	/*step 2*/{1},
#if (MALI_DVFS_STEPS > 3)
	/*step 3*/{1},
#if (MALI_DVFS_STEPS > 4)
        /*step 4*/{0}
#endif
#endif
#endif
#endif
};

/* dvfs information 

	L0 = 533Mhz, 1.075V
	L1 = 440Mhz, 1.025V
	L2 = 350Mhz, 0.95V
	L3 = 266Mhz, 0.90V
	L4 = 160Mhz, 0.875V
*/

mali_dvfs_table mali_dvfs_all[MAX_MALI_DVFS_STEPS] = {
	{160   ,1000000   ,  875000},
	{266   ,1000000   ,  900000},
	{350   ,1000000   ,  950000},
	{440   ,1000000   , 1025000},
	{533   ,1000000   , 1075000} };

mali_dvfs_table mali_dvfs[MALI_DVFS_STEPS] = {
	{160   ,1000000   , 875000},
#if (MALI_DVFS_STEPS > 1)
	{266   ,1000000   , 901000},
#if (MALI_DVFS_STEPS > 2)
	{350   ,1000000   , 951000},
#if (MALI_DVFS_STEPS > 3)
	{440   ,1000000   ,1025100},
#if (MALI_DVFS_STEPS > 4)
	{533   ,1000000   ,1075000}
#endif
#endif
#endif
#endif
};

mali_dvfs_threshold_table mali_dvfs_threshold[MALI_DVFS_STEPS] = {
	{0   , 50},
#if (MALI_DVFS_STEPS > 1)
	{45  , 60},
#if (MALI_DVFS_STEPS > 2)
	{50  , 70},
#if (MALI_DVFS_STEPS > 3)
	{75  , 85},
#if (MALI_DVFS_STEPS > 4)
        {80  , 100}
#endif
#endif
#endif
#endif
};

#ifdef CONFIG_EXYNOS_ASV
#define ASV_LEVEL     12	/* ASV0, 1, 11 is reserved */
#define ASV_LEVEL_PRIME     13	/* ASV0, 1, 12 is reserved */

static unsigned int asv_3d_volt_9_table[MALI_DVFS_STEPS][ASV_LEVEL] = {
	{  950000,  925000,  900000,  900000,  875000,  875000,  875000,  875000,  875000,  875000,  875000,  850000},	/* L3(160Mhz) */
#if (MALI_DVFS_STEPS > 1)
	{  975000,  950000,  925000,  925000,  925000,  900000,  900000,  875000,  875000,  875000,  875000,  850000},	/* L2(266Mhz) */
#if (MALI_DVFS_STEPS > 2)
	{ 1050000, 1025000, 1000000, 1000000,  975000,  950000,  950000,  950000,  925000,  925000,  925000,  900000},	/* L1(350Mhz) */
#if (MALI_DVFS_STEPS > 3)
	{ 1100000, 1075000, 1050000, 1050000, 1050000, 1025000, 1025000, 1000000, 1000000, 1000000,  1100000,  950000},	/* L0(440Mhz) */
#endif
#endif
#endif
};

static unsigned int asv_3d_volt_9_table_for_prime[MALI_DVFS_STEPS][ASV_LEVEL_PRIME] = {
	{  950000,  937500,  925000,  912500,  900000,  887500,  875000,  862500,  875000,  862500,  850000,  850000,  850000},	/* L4(160Mhz) */
#if (MALI_DVFS_STEPS > 1)
	{  975000,  962500,  950000,  937500,  925000,  912500,  900000,  887500,  900000,  887500,  875000,  875000,  875000},	/* L3(266Mhz) */
#if (MALI_DVFS_STEPS > 2)
	{ 1025000, 1012500, 1000000,  987500,  975000,  962500,  950000,  937500,  950000,  937500,  912500,  900000,  887500},	/* L2(350Mhz) */
#if (MALI_DVFS_STEPS > 3)
	{ 1087500, 1075000, 1062500, 1050000, 1037500, 1025000, 1012500, 1000000, 1012500, 1000000,  975000,  962500,  950000},	/* L1(440Mhz) */
#if (MALI_DVFS_STEPS > 4)
	{ 1150000, 1137500, 1125000, 1112500, 1100000, 1087500, 1075000, 1062500, 1075000, 1062500, 1037500, 1025000, 1012500},	/* L0(533Mhz) */
#endif
#endif
#endif
#endif
};

#endif

/* dvfs status */
mali_dvfs_currentstatus maliDvfsStatus;
int mali_dvfs_control = 0;

static u32 mali_dvfs_utilization = 255;

static void mali_dvfs_work_handler(struct work_struct *w);

static struct workqueue_struct *mali_dvfs_wq = 0;
extern mali_io_address clk_register_map;
extern _mali_osk_lock_t *mali_dvfs_lock;


static DECLARE_WORK(mali_dvfs_work, mali_dvfs_work_handler);

static inline unsigned int get_mali_dvfs_status(void)
{
	return maliDvfsStatus.currentStep;
}

mali_bool set_mali_dvfs_current_step(unsigned int step)
{
	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	maliDvfsStatus.currentStep = step;
	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	return MALI_TRUE;
}
static mali_bool set_mali_dvfs_status(u32 step,mali_bool boostup)
{
	u32 validatedStep=step;

#ifdef CONFIG_REGULATOR
	if (mali_regulator_get_usecount() == 0) {
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

	set_mali_dvfs_current_step(validatedStep);
	/*for future use*/
	maliDvfsStatus.pCurrentDvfs = &mali_dvfs[validatedStep];

	mali_set_runtime_resume_params(mali_dvfs[validatedStep].clock,
					mali_dvfs[validatedStep].vol);

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
			_mali_osk_time_ubusydelay(100);// 1000 -> 100 : 20101218
		}
		/* _mali_osk_time_ubusydelay(msec*1000);*/
}

static mali_bool change_mali_dvfs_status(u32 step, mali_bool boostup )
{

	MALI_DEBUG_PRINT(1, ("> change_mali_dvfs_status: %d, %d \n",step,
				boostup));

	if (!set_mali_dvfs_status(step, boostup)) {
		MALI_DEBUG_PRINT(1, ("error on set_mali_dvfs_status: %d, %d \n"
					,step, boostup));
		return MALI_FALSE;
	}

	/*wait until clock and voltage is stablized*/
	mali_platform_wating(MALI_DVFS_WATING); /*msec*/

	return MALI_TRUE;
}

#ifdef CONFIG_EXYNOS_ASV
extern unsigned int exynos4_result_of_asv;

static mali_bool mali_dvfs_table_update(void)
{
	unsigned int i;
	if(samsung_rev() < EXYNOS4412_REV_2_0) {
#if (MALI_DVFS_STEPS > 4)
		mali_dvfs[4].vol = mali_dvfs[3].vol;
		mali_dvfs[4].clock = mali_dvfs[3].clock;
		for (i = 0; i < MALI_DVFS_STEPS - 1; i++) {
#else
		for (i = 0; i < MALI_DVFS_STEPS ; i++) {
#endif
			MALI_PRINT((":::exynos4_result_of_asv : %d\n",
				exynos4_result_of_asv));
			mali_dvfs[i].vol =
				asv_3d_volt_9_table[i][exynos4_result_of_asv];
			MALI_PRINT(("mali_dvfs[%d].vol = %d\n", i,
				mali_dvfs[i].vol));
		}
	} else {
		for (i = 0; i < MALI_DVFS_STEPS; i++) {
			MALI_PRINT((":::exynos_result_of_asv : %d \n",
				exynos4_result_of_asv));
			mali_dvfs[i].vol =
			asv_3d_volt_9_table_for_prime[i][exynos4_result_of_asv];
			MALI_PRINT(("mali_dvfs[%d].vol = %d\n", i,
				mali_dvfs[i].vol));
		} 
	}

	return MALI_TRUE;
}
#endif

static unsigned int decideNextStatus(unsigned int utilization)
{
	static unsigned int level = 0; // 0:stay, 1:up

	if (mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold
	     <= mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold) {
		MALI_PRINT(("upthreadshold is smaller than downthreshold: %d < %d\n",
				mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold,
				mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold));
		return level;
	}

	if (!mali_dvfs_control && level == maliDvfsStatus.currentStep) {
		if (utilization > (int)(256 * mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold / 100) && level < MALI_DVFS_STEPS - 1) {
			level++;
		}
		if (utilization < (int)(256 * mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold / 100) &&
				level > 0) {
			level--;
		}
	}
 
	return level;
}

static mali_bool mali_dvfs_status(u32 utilization)
{
	unsigned int nextStatus = 0;
	unsigned int curStatus = 0;
	mali_bool boostup = MALI_FALSE;
	static int stay_count = 0;
#ifdef CONFIG_EXYNOS_ASV
	static mali_bool asv_applied = MALI_FALSE;
#endif

	MALI_DEBUG_PRINT(1, ("> mali_dvfs_status: %d \n",utilization));
#ifdef CONFIG_EXYNOS_ASV
	if (asv_applied == MALI_FALSE) {
		mali_dvfs_table_update();
		change_mali_dvfs_status(1, 0);
		asv_applied = MALI_TRUE;

		return MALI_TRUE;
	}
#endif

	/*decide next step*/
	curStatus = get_mali_dvfs_status();
	nextStatus = decideNextStatus(utilization);

	/*if next status is same with current status, don't change anything*/
	if ((curStatus != nextStatus && stay_count == 0)) {
		/*check if boost up or not*/
		if (nextStatus > maliDvfsStatus.currentStep) boostup = 1;

		/*change mali dvfs status*/
		if (!change_mali_dvfs_status(nextStatus,boostup)) {
			MALI_DEBUG_PRINT(1, ("error on change_mali_dvfs_status \n"));
			return MALI_FALSE;
		}
		stay_count = mali_dvfs_staycount[maliDvfsStatus.currentStep].staycount;
	} else {
		if (stay_count > 0)
			stay_count--;
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

	/*add a error handling here*/
	set_mali_dvfs_current_step(step);

	return MALI_TRUE;
}

void deinit_mali_dvfs_status(void)
{
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

#endif
