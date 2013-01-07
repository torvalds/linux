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

#include "mali_device_pause_resume.h"
#include <linux/workqueue.h>

#define MAX_MALI_DVFS_STEPS 5
#define MALI_DVFS_WATING 10 // msec

#ifdef CONFIG_CPU_FREQ
#include <mach/asv.h>
#define EXYNOS4_ASV_ENABLED
#endif

#include <plat/cpu.h>

static int bMaliDvfsRun=0;

static _mali_osk_atomic_t bottomlock_status;
int bottom_lock_step = 0;

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

typedef struct mali_dvfs_stepTag{
	int clk;
	int vol;
}mali_dvfs_step;

mali_dvfs_step step[MALI_DVFS_STEPS]={
	/*step 0 clk*/ {160,   875000},
#if (MALI_DVFS_STEPS > 1)
	/*step 1 clk*/ {266,   900000},
#if (MALI_DVFS_STEPS > 2)
	/*step 2 clk*/ {350,   950000},
#if (MALI_DVFS_STEPS > 3)
	/*step 3 clk*/ {440,  1025000},
#if (MALI_DVFS_STEPS > 4)
	/*step 4 clk*/ {533,  1075000},
#endif
#endif
#endif
#endif
};

mali_dvfs_staycount_table mali_dvfs_staycount[MALI_DVFS_STEPS]={
	/*step 0*/{0},
#if (MALI_DVFS_STEPS > 1)
	/*step 1*/{0},
#if (MALI_DVFS_STEPS > 2)
	/*step 2*/{0},
#if (MALI_DVFS_STEPS > 3)
	/*step 3*/{0},
#if (MALI_DVFS_STEPS > 4)
	/*step 4*/{0}
#endif
#endif
#endif
#endif
};

/* dvfs information */
// L0 = 533Mhz, 1.075V
// L1 = 440Mhz, 1.025V
// L2 = 350Mhz, 0.95V
// L3 = 266Mhz, 0.90V
// L4 = 160Mhz, 0.875V

int step0_clk = 160;
int step0_vol = 875000;
#if (MALI_DVFS_STEPS > 1)
int step1_clk = 266;
int step1_vol = 900000;
int step0_up = 70;
int step1_down = 62;
#if (MALI_DVFS_STEPS > 2)
int step2_clk = 350;
int step2_vol = 950000;
int step1_up = 90;
int step2_down = 85;
#if (MALI_DVFS_STEPS > 3)
int step3_clk = 440;
int step3_vol = 1025000;
int step2_up = 90;
int step3_down = 85;
#if (MALI_DVFS_STEPS > 4)
int step4_clk = 533;
int step4_vol = 1075000;
int step3_up = 90;
int step4_down = 95;
#endif
#endif
#endif
#endif

mali_dvfs_table mali_dvfs_all[MAX_MALI_DVFS_STEPS]={
	{160   ,1000000   ,  875000},
	{266   ,1000000   ,  900000},
	{350   ,1000000   ,  950000},
	{440   ,1000000   , 1025000},
	{533   ,1000000   , 1075000} };

mali_dvfs_table mali_dvfs[MALI_DVFS_STEPS]={
	{160   ,1000000   , 875000},
#if (MALI_DVFS_STEPS > 1)
	{266   ,1000000   , 900000},
#if (MALI_DVFS_STEPS > 2)
	{350   ,1000000   , 950000},
#if (MALI_DVFS_STEPS > 3)
	{440   ,1000000   ,1025000},
#if (MALI_DVFS_STEPS > 4)
	{533   ,1000000   ,1075000}
#endif
#endif
#endif
#endif
};

mali_dvfs_threshold_table mali_dvfs_threshold[MALI_DVFS_STEPS]={
	{0   , 70},
#if (MALI_DVFS_STEPS > 1)
	{62  , 90},
#if (MALI_DVFS_STEPS > 2)
	{85  , 90},
#if (MALI_DVFS_STEPS > 3)
	{85  , 90},
#if (MALI_DVFS_STEPS > 4)
	{95  ,100}
#endif
#endif
#endif
#endif
};

#ifdef EXYNOS4_ASV_ENABLED
#define ASV_LEVEL     12	/* ASV0, 1, 11 is reserved */

static unsigned int asv_3d_volt_4412_9_table[MALI_DVFS_STEPS][ASV_LEVEL] = {
	{  950000,  925000,  900000,  900000,  875000,  875000,  875000,  875000,  850000,  850000,  850000,  850000},  /* L3(160Mhz) */
#if (MALI_DVFS_STEPS > 1)
	{  975000,  950000,  925000,  925000,  925000,  900000,  900000,  875000,  875000,  875000,  875000,  850000},  /* L2(266Mhz) */
#if (MALI_DVFS_STEPS > 2)
	{ 1050000, 1025000, 1000000, 1000000,  975000,  950000,  950000,  950000,  925000,  925000,  925000,  900000},  /* L1(350Mhz) */
#if (MALI_DVFS_STEPS > 3)
	{ 1100000, 1075000, 1050000, 1050000, 1050000, 1025000, 1025000, 1000000, 1000000, 1000000,  975000,  950000},  /* L0(440Mhz) */
#endif
#endif
#endif
};

static unsigned int asv_3d_volt_9_table_for_prime[MALI_DVFS_STEPS][ASV_LEVEL] = {
	{  950000,  937500,  925000,  912500,  900000,  887500,  875000,  862500,  875000,  862500,  850000,  850000},	/* L4(160Mhz) */
#if (MALI_DVFS_STEPS > 1)
	{  975000,  962500,  950000,  937500,  925000,  912500,  900000,  887500,  900000,  887500,  875000,  862500},	/* L3(266Mhz) */
#if (MALI_DVFS_STEPS > 2)
	{ 1025000, 1012500, 1000000,  987500,  975000,  962500,  950000,  937500,  950000,  937500,  925000,  912500},	/* L2(350Mhz) */
#if (MALI_DVFS_STEPS > 3)
	{ 1087500, 1075000, 1062500, 1050000, 1037500, 1025000, 1012500, 1000000, 1012500, 1000000,  987500,  975000},	/* L1(440Mhz) */
#if (MALI_DVFS_STEPS > 4)
	{ 1150000, 1137500, 1125000, 1112500, 1100000, 1087500, 1075000, 1062500, 1087500, 1075000, 1062500, 1050000},	/* L0(533Mhz) */
#endif
#endif
#endif
#endif
};

static unsigned int asv_3d_volt_4212_9_table[MALI_DVFS_STEPS][ASV_LEVEL] = {
	{  912500,  900000,  900000,  900000,  900000,  900000,  900000,  900000,  875000,  850000,  850000,  850000},	/* L3(160Mhz) */
#if (MALI_DVFS_STEPS > 1)
	{  937500,  925000,  925000,  900000,  925000,  925000,  925000,  900000,  900000,  900000,  875000,  862500},	/* L2(266Mhz) */
#if (MALI_DVFS_STEPS > 2)
	{  987500,  975000,  975000,  950000,  975000,  950000,  950000,  925000,  925000,  925000,  925000,  912500},	/* L1(350Mhz) */
#if (MALI_DVFS_STEPS > 3)
	{ 1062500, 1050000, 1050000, 1025000, 1050000, 1050000, 1025000, 1000000, 1000000,  975000,  975000,  962500},	/* L0(440Mhz) */
#endif
#endif
#endif
};
#endif

/*dvfs status*/
mali_dvfs_currentstatus maliDvfsStatus;
int mali_dvfs_control=0;

static u32 mali_dvfs_utilization = 255;

static void mali_dvfs_work_handler(struct work_struct *w);

static struct workqueue_struct *mali_dvfs_wq = 0;
extern mali_io_address clk_register_map;
extern _mali_osk_lock_t *mali_dvfs_lock;

int mali_runtime_resumed = -1;

static DECLARE_WORK(mali_dvfs_work, mali_dvfs_work_handler);

/* lock/unlock CPU freq by Mali */
#include <linux/types.h>
#include <mach/cpufreq.h>

atomic_t mali_cpufreq_lock;

int cpufreq_lock_by_mali(unsigned int freq)
{
#ifdef CONFIG_EXYNOS4_CPUFREQ
/* #if defined(CONFIG_CPU_FREQ) && defined(CONFIG_ARCH_EXYNOS4) */
	unsigned int level;

	if (atomic_read(&mali_cpufreq_lock) == 0) {
		if (exynos_cpufreq_get_level(freq * 1000, &level)) {
			printk(KERN_ERR
				"Mali: failed to get cpufreq level for %dMHz",
				freq);
			return -EINVAL;
		}

		if (exynos_cpufreq_lock(DVFS_LOCK_ID_G3D, level)) {
			printk(KERN_ERR
				"Mali: failed to cpufreq lock for L%d", level);
			return -EINVAL;
		}

		atomic_set(&mali_cpufreq_lock, 1);
		printk(KERN_DEBUG "Mali: cpufreq locked on <%d>%dMHz\n", level,
									freq);
	}
#endif
	return 0;
}

void cpufreq_unlock_by_mali(void)
{
#ifdef CONFIG_EXYNOS4_CPUFREQ
/* #if defined(CONFIG_CPU_FREQ) && defined(CONFIG_ARCH_EXYNOS4) */
	if (atomic_read(&mali_cpufreq_lock) == 1) {
		exynos_cpufreq_lock_free(DVFS_LOCK_ID_G3D);
		atomic_set(&mali_cpufreq_lock, 0);
		printk(KERN_DEBUG "Mali: cpufreq locked off\n");
	}
#endif
}

static unsigned int get_mali_dvfs_status(void)
{
	return maliDvfsStatus.currentStep;
}
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
int get_mali_dvfs_control_status(void)
{
	return mali_dvfs_control;
}

mali_bool set_mali_dvfs_current_step(unsigned int step)
{
	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	maliDvfsStatus.currentStep = step % MAX_MALI_DVFS_STEPS;
	if (step >= MAX_MALI_DVFS_STEPS)
		mali_runtime_resumed = maliDvfsStatus.currentStep;
	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	return MALI_TRUE;
}
#endif
static mali_bool set_mali_dvfs_status(u32 step,mali_bool boostup)
{
	u32 validatedStep=step;
	int err;

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

#ifdef EXYNOS4_ASV_ENABLED
	if (mali_dvfs[step].clock == 160)
		exynos4x12_set_abb_member(ABB_G3D, ABB_MODE_100V);
	else
		exynos4x12_set_abb_member(ABB_G3D, ABB_MODE_130V);
#endif


	set_mali_dvfs_current_step(validatedStep);
	/*for future use*/
	maliDvfsStatus.pCurrentDvfs = &mali_dvfs[validatedStep];

	/* lock/unlock CPU freq by Mali */
	/* if ((mali_dvfs[step].clock == 533) || (mali_dvfs[step].clock == 440))
		#if defined(CONFIG_EXYNOS4X12_1800MHZ_SUPPORT)
		err = cpufreq_lock_by_mali(1800);
		#elif defined(CONFIG_EXYNOS4X12_1600MHZ_SUPPORT)
		err = cpufreq_lock_by_mali(1600);
		#elif defined(CONFIG_EXYNOS4X12_1500MHZ_SUPPORT)
		err = cpufreq_lock_by_mali(1500);
		#elif defined(CONFIG_EXYNOS4X12_1400MHZ_SUPPORT)
		err = cpufreq_lock_by_mali(1400);
		#else
		err = cpufreq_lock_by_mali(1200);
		#endif
	else
		cpufreq_unlock_by_mali(); */

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

#ifdef EXYNOS4_ASV_ENABLED
extern unsigned int exynos_result_of_asv;

static mali_bool mali_dvfs_table_update(void)
{
	unsigned int i;
	unsigned int step_num = MALI_DVFS_STEPS;

	if(soc_is_exynos4412()) {
		/*check it's pega-prime or pega-Q*/
		if((is_special_flag() >> G3D_LOCK_FLAG) & 0x1) {
			for (i = 0; i < step_num; i++) {
				MALI_PRINT((":::exynos_result_of_asv : %d\n", exynos_result_of_asv));
				mali_dvfs[i].vol = asv_3d_volt_9_table_for_prime[i][exynos_result_of_asv] + 25000;
				MALI_PRINT(("mali_dvfs[%d].vol = %d\n", i, mali_dvfs[i].vol));
			}
		}
		/* pega-prime default ASV table */
		else {
			for (i = 0; i < step_num; i++) {
				MALI_PRINT((":::exynos_result_of_asv : %d\n", exynos_result_of_asv));
				mali_dvfs[i].vol = asv_3d_volt_9_table_for_prime[i][exynos_result_of_asv];
				MALI_PRINT(("mali_dvfs[%d].vol = %d\n", i, mali_dvfs[i].vol));
			} 
		}
	}
	else if(soc_is_exynos4212()) {
		for (i = 0; i < step_num; i++) {
			MALI_PRINT((":::exynos_result_of_asv : %d\n", exynos_result_of_asv));
			mali_dvfs[i].vol = asv_3d_volt_4212_9_table[i][exynos_result_of_asv];
			MALI_PRINT(("mali_dvfs[%d].vol = %d\n", i, mali_dvfs[i].vol));
		}
	}

	return MALI_TRUE;
}
#endif

static unsigned int decideNextStatus(unsigned int utilization)
{
	static unsigned int level = 0; // 0:stay, 1:up
	static int mali_dvfs_clk = 0;

	if (mali_runtime_resumed >= 0) {
		level = mali_runtime_resumed;
		mali_runtime_resumed = -1;
	}

	if (mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold
			<= mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold) {
		MALI_PRINT(("upthreadshold is smaller than downthreshold: %d < %d\n",
				mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold,
				mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold));
		return level;
	}

	if (!mali_dvfs_control && level == maliDvfsStatus.currentStep) {
		if (utilization > (int)(255 * mali_dvfs_threshold[maliDvfsStatus.currentStep].upthreshold / 100) &&
				level < MALI_DVFS_STEPS - 1) {
			level++;
		}
		if (utilization < (int)(255 * mali_dvfs_threshold[maliDvfsStatus.currentStep].downthreshold / 100) &&
				level > 0) {
			level--;
		}
	} else if (mali_dvfs_control == 999) {
		int i = 0;
		for (i = 0; i < MALI_DVFS_STEPS; i++) {
			step[i].clk = mali_dvfs_all[i].clock;
		}
#ifdef EXYNOS4_ASV_ENABLED
		mali_dvfs_table_update();
#endif
		i = 0;
		for (i = 0; i < MALI_DVFS_STEPS; i++) {
			mali_dvfs[i].clock = step[i].clk;
		}
		mali_dvfs_control = 0;
		level = 0;

		step0_clk = step[0].clk;
		change_dvfs_tableset(step0_clk, 0);
#if (MALI_DVFS_STEPS > 1)
		step1_clk = step[1].clk;
		change_dvfs_tableset(step1_clk, 1);
#if (MALI_DVFS_STEPS > 2)
		step2_clk = step[2].clk;
		change_dvfs_tableset(step2_clk, 2);
#if (MALI_DVFS_STEPS > 3)
		step3_clk = step[3].clk;
		change_dvfs_tableset(step3_clk, 3);
#if (MALI_DVFS_STEPS > 4)
		step4_clk = step[4].clk;
		change_dvfs_tableset(step4_clk, 4);
#endif
#endif
#endif
#endif
	} else if (mali_dvfs_control != mali_dvfs_clk && mali_dvfs_control != 999) {
		if (mali_dvfs_control < mali_dvfs_all[1].clock && mali_dvfs_control > 0) {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk = mali_dvfs_all[0].clock;
			}
			maliDvfsStatus.currentStep = 0;
		} else if (mali_dvfs_control < mali_dvfs_all[2].clock && mali_dvfs_control >= mali_dvfs_all[1].clock) {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk = mali_dvfs_all[1].clock;
			}
			maliDvfsStatus.currentStep = 1;
		} else if (mali_dvfs_control < mali_dvfs_all[3].clock && mali_dvfs_control >= mali_dvfs_all[2].clock) {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk = mali_dvfs_all[2].clock;
			}
			maliDvfsStatus.currentStep = 2;
		} else if (mali_dvfs_control < mali_dvfs_all[4].clock && mali_dvfs_control >= mali_dvfs_all[3].clock) {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk = mali_dvfs_all[3].clock;
			}
			maliDvfsStatus.currentStep = 3;
		} else {
			int i = 0;
			for (i = 0; i < MALI_DVFS_STEPS; i++) {
				step[i].clk  = mali_dvfs_all[4].clock;
			}
			maliDvfsStatus.currentStep = 4;
		}
		step0_clk = step[0].clk;
		change_dvfs_tableset(step0_clk, 0);
#if (MALI_DVFS_STEPS > 1)
		step1_clk = step[1].clk;
		change_dvfs_tableset(step1_clk, 1);
#if (MALI_DVFS_STEPS > 2)
		step2_clk = step[2].clk;
		change_dvfs_tableset(step2_clk, 2);
#if (MALI_DVFS_STEPS > 3)
		step3_clk = step[3].clk;
		change_dvfs_tableset(step3_clk, 3);
#if (MALI_DVFS_STEPS > 4)
		step4_clk = step[4].clk;
		change_dvfs_tableset(step4_clk, 4);
#endif
#endif
#endif
#endif
		level = maliDvfsStatus.currentStep;
	}

	mali_dvfs_clk = mali_dvfs_control;

	if (_mali_osk_atomic_read(&bottomlock_status) > 0) {
		if (level < bottom_lock_step)
			level = bottom_lock_step;
	}

	return level;
}

static mali_bool mali_dvfs_status(u32 utilization)
{
	unsigned int nextStatus = 0;
	unsigned int curStatus = 0;
	mali_bool boostup = MALI_FALSE;
	static int stay_count = 0;
#ifdef EXYNOS4_ASV_ENABLED
	static mali_bool asv_applied = MALI_FALSE;
#endif

	MALI_DEBUG_PRINT(1, ("> mali_dvfs_status: %d \n",utilization));
#ifdef EXYNOS4_ASV_ENABLED
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

	MALI_DEBUG_PRINT(1, ("= curStatus %d, nextStatus %d, maliDvfsStatus.currentStep %d \n", curStatus, nextStatus, maliDvfsStatus.currentStep));

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



void mali_dvfs_late_resume(void)
{
	// set the init clock as low when resume
	set_mali_dvfs_status(0,0);
}


static void mali_dvfs_work_handler(struct work_struct *w)
{
	int change_clk = 0;
	int change_step = 0;
	bMaliDvfsRun=1;

	/* dvfs table change when clock was changed */
	if (step0_clk != mali_dvfs[0].clock) {
		MALI_PRINT(("::: step0_clk change to %d Mhz\n", step0_clk));
		change_clk = step0_clk;
		change_step = 0;
		step0_clk = change_dvfs_tableset(change_clk, change_step);
	}
#if (MALI_DVFS_STEPS > 1)
	if (step1_clk != mali_dvfs[1].clock) {
		MALI_PRINT(("::: step1_clk change to %d Mhz\n", step1_clk));
		change_clk = step1_clk;
		change_step = 1;
		step1_clk = change_dvfs_tableset(change_clk, change_step);
	}
	if (step0_up != mali_dvfs_threshold[0].upthreshold) {
		MALI_PRINT(("::: step0_up change to %d %\n", step0_up));
		mali_dvfs_threshold[0].upthreshold = step0_up;
	}
	if (step1_down != mali_dvfs_threshold[1].downthreshold) {
		MALI_PRINT((":::step1_down change to %d %\n", step1_down));
		mali_dvfs_threshold[1].downthreshold = step1_down;
	}
#if (MALI_DVFS_STEPS > 2)
	if (step2_clk != mali_dvfs[2].clock) {
		MALI_PRINT(("::: step2_clk change to %d Mhz\n", step2_clk));
		change_clk = step2_clk;
		change_step = 2;
		step2_clk = change_dvfs_tableset(change_clk, change_step);
	}
	if (step1_up != mali_dvfs_threshold[1].upthreshold) {
		MALI_PRINT((":::step1_up change to %d %\n", step1_up));
		mali_dvfs_threshold[1].upthreshold = step1_up;
	}
	if (step2_down != mali_dvfs_threshold[2].downthreshold) {
		MALI_PRINT((":::step2_down change to %d %\n", step2_down));
		mali_dvfs_threshold[2].downthreshold = step2_down;
	}
#if (MALI_DVFS_STEPS > 3)
	if (step3_clk != mali_dvfs[3].clock) {
		MALI_PRINT(("::: step3_clk change to %d Mhz\n", step3_clk));
		change_clk = step3_clk;
		change_step = 3;
		step3_clk = change_dvfs_tableset(change_clk, change_step);
	}
	if (step2_up != mali_dvfs_threshold[2].upthreshold) {
		MALI_PRINT((":::step2_up change to %d %\n", step2_up));
		mali_dvfs_threshold[2].upthreshold = step2_up;
	}
	if (step3_down != mali_dvfs_threshold[3].downthreshold) {
		MALI_PRINT((":::step3_down change to %d %\n", step3_down));
		mali_dvfs_threshold[3].downthreshold = step3_down;
	}
#if (MALI_DVFS_STEPS > 4)
	if (step4_clk != mali_dvfs[4].clock) {
		MALI_PRINT(("::: step4_clk change to %d Mhz\n", step4_clk));
		change_clk = step4_clk;
		change_step = 4;
		step4_clk = change_dvfs_tableset(change_clk, change_step);
	}
	if (step3_up != mali_dvfs_threshold[3].upthreshold) {
		MALI_PRINT((":::step3_up change to %d %\n", step3_up));
		mali_dvfs_threshold[3].upthreshold = step3_up;
	}
	if (step4_down != mali_dvfs_threshold[4].downthreshold) {
		MALI_PRINT((":::step4_down change to %d %\n", step4_down));
		mali_dvfs_threshold[4].downthreshold = step4_down;
	}
#endif
#endif
#endif
#endif


#ifdef DEBUG
	mali_dvfs[0].vol = step0_vol;
	mali_dvfs[1].vol = step1_vol;
	mali_dvfs[2].vol = step2_vol;
	mali_dvfs[3].vol = step3_vol;
	mali_dvfs[4].vol = step4_vol;
#endif
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

	_mali_osk_atomic_init(&bottomlock_status, 0);

	/*add a error handling here*/
	set_mali_dvfs_current_step(step);

	return MALI_TRUE;
}

void deinit_mali_dvfs_status(void)
{
	if (mali_dvfs_wq)
		destroy_workqueue(mali_dvfs_wq);

	_mali_osk_atomic_term(&bottomlock_status);

	mali_dvfs_wq = NULL;
}

mali_bool mali_dvfs_handler(u32 utilization)
{
	mali_dvfs_utilization = utilization;
	queue_work_on(0, mali_dvfs_wq,&mali_dvfs_work);

	/*add error handle here*/
	return MALI_TRUE;
}

int change_dvfs_tableset(int change_clk, int change_step)
{
	int err;

	if (change_clk < mali_dvfs_all[1].clock) {
		mali_dvfs[change_step].clock = mali_dvfs_all[0].clock;
	} else if (change_clk < mali_dvfs_all[2].clock && change_clk >= mali_dvfs_all[1].clock) {
		mali_dvfs[change_step].clock = mali_dvfs_all[1].clock;
	} else if (change_clk < mali_dvfs_all[3].clock && change_clk >= mali_dvfs_all[2].clock) {
		mali_dvfs[change_step].clock = mali_dvfs_all[2].clock;
	} else if (change_clk < mali_dvfs_all[4].clock && change_clk >= mali_dvfs_all[3].clock) {
		mali_dvfs[change_step].clock = mali_dvfs_all[3].clock;
	} else {
		mali_dvfs[change_step].clock = mali_dvfs_all[4].clock;
	}

	MALI_PRINT((":::mali dvfs step %d clock and voltage = %d Mhz, %d V\n",change_step, mali_dvfs[change_step].clock, mali_dvfs[change_step].vol));

	if (maliDvfsStatus.currentStep == change_step) {
#ifdef CONFIG_REGULATOR
		/*change the voltage*/
		mali_regulator_set_voltage(mali_dvfs[change_step].vol, mali_dvfs[change_step].vol);
#endif
		/*change the clock*/
		mali_clk_set_rate(mali_dvfs[change_step].clock, mali_dvfs[change_step].freq);

		/* lock/unlock CPU freq by Mali */

		/* if ((mali_dvfs[change_step].clock == 533) || (mali_dvfs[change_step].clock == 440))
			#if defined(CONFIG_EXYNOS4X12_1800MHZ_SUPPORT)
			err = cpufreq_lock_by_mali(1800);
			#elif defined(CONFIG_EXYNOS4X12_1600MHZ_SUPPORT)
			err = cpufreq_lock_by_mali(1600);
			#elif defined(CONFIG_EXYNOS4X12_1500MHZ_SUPPORT)
			err = cpufreq_lock_by_mali(1500);
			#elif defined(CONFIG_EXYNOS4X12_1400MHZ_SUPPORT)
			err = cpufreq_lock_by_mali(1400);
			#else
			err = cpufreq_lock_by_mali(1200);
			#endif
		else
			cpufreq_unlock_by_mali(); */
	}

	return mali_dvfs[change_step].clock;
}

void mali_default_step_set(int step, mali_bool boostup)
{
	mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);

	if (maliDvfsStatus.currentStep == 1)
		set_mali_dvfs_status(step, boostup);
}

int mali_dvfs_bottom_lock_push(int lock_step)
{
	int prev_status = _mali_osk_atomic_read(&bottomlock_status);

	if (prev_status < 0) {
		MALI_PRINT(("gpu bottom lock status is not valid for push\n"));
		return -1;
	}
	if (bottom_lock_step < lock_step) {
		bottom_lock_step = lock_step;
		if (get_mali_dvfs_status() < lock_step) {
			mali_regulator_set_voltage(mali_dvfs[lock_step].vol, mali_dvfs[lock_step].vol);
			mali_clk_set_rate(mali_dvfs[lock_step].clock, mali_dvfs[lock_step].freq);
			set_mali_dvfs_current_step(lock_step);
		}
	}

	return _mali_osk_atomic_inc_return(&bottomlock_status);
}

int mali_dvfs_bottom_lock_pop(void)
{
	int prev_status = _mali_osk_atomic_read(&bottomlock_status);
	if (prev_status <= 0) {
		MALI_PRINT(("gpu bottom lock status is not valid for pop\n"));
		return -1;
	} else if (prev_status == 1) {
		bottom_lock_step = 0;
		MALI_PRINT(("gpu bottom lock release\n"));
	}

	return _mali_osk_atomic_dec_return(&bottomlock_status);
}

#if MALI_VOLTAGE_LOCK
int mali_vol_get_from_table(int vol)
{
	int i;
	for (i = 0; i < MALI_DVFS_STEPS; i++) {
		if (mali_dvfs[i].vol >= vol)
			return mali_dvfs[i].vol;
	}
	MALI_PRINT(("Failed to get voltage from mali_dvfs table, maximum voltage is %d uV\n", mali_dvfs[MALI_DVFS_STEPS-1].vol));
	return 0;
}
#endif
