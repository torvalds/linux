/*
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file arm_core_scaling.h
 * Example core scaling policy.
 */

#ifndef __ARM_CORE_SCALING_H__
#define __ARM_CORE_SCALING_H__

enum mali_scale_mode_t {
	MALI_PP_SCALING = 0,
	MALI_PP_FS_SCALING,
	MALI_SCALING_DISABLE,
	MALI_TURBO_MODE,
	MALI_SCALING_MODE_MAX
};

typedef struct mali_dvfs_threshold_table {
	unsigned int freq_index;
	unsigned int voltage;
	unsigned int keep_count;
	unsigned int downthreshold;
	unsigned int upthreshold;
} mali_dvfs_threshold_table;

/**
 *  restrictions on frequency and number of pp.
 */
typedef struct mali_scale_info_t {
	u32 minpp;
	u32 maxpp;
	u32 minclk;
	u32 maxclk;
} mali_scale_info_t;

/**
 * Platform spesific data for meson chips.
 */
typedef struct mali_plat_info_t {
	u32 cfg_pp; /* number of pp. */
	u32 cfg_min_pp;
	u32 turbo_clock; /* reserved clock src. */
	u32 def_clock; /* gpu clock used most of time.*/
	u32 cfg_clock; /* max clock could be used.*/
	u32 cfg_clock_bkup; /* same as cfg_clock, for backup. */
	u32 cfg_min_clock;

	u32  sc_mpp; /* number of pp used most of time.*/
	u32  bst_gpu; /* threshold for boosting gpu. */
	u32  bst_pp; /* threshold for boosting PP. */

	u32 *clk;
	u32 *clk_sample;
	u32 clk_len;
	u32 have_switch; /* have clock gate switch or not. */

	mali_dvfs_threshold_table *dvfs_table;
	u32 dvfs_table_size;

	mali_scale_info_t scale_info;

	/* set upper limit of pp or frequency, for THERMAL thermal or band width saving.*/
	u32 limit_on;

	/* for boost up gpu by user. */
	void (*plat_preheat)(void);
} mali_plat_info_t;
mali_plat_info_t* get_mali_plat_data(void);

/**
 * Initialize core scaling policy.
 *
 * @note The core scaling policy will assume that all PP cores are on initially.
 *
 * @param num_pp_cores Total number of PP cores.
 */
int mali_core_scaling_init(mali_plat_info_t*);

/**
 * Terminate core scaling policy.
 */
void mali_core_scaling_term(void);

/**
 * cancel and flush scaling job queue.
 */
void flush_scaling_job(void);

/* get current state(pp, clk). */
void get_mali_rt_clkpp(u32* clk, u32* pp);
u32 set_mali_rt_clkpp(u32 clk, u32 pp, u32 flush);
void revise_mali_rt(void);

/* get or set the scale mode. */
u32 get_mali_schel_mode(void);
void set_mali_schel_mode(u32 mode);

/* for frequency reporter in DS-5 streamline. */
u32 get_current_frequency(void);

extern int mali_pm_statue;
#endif /* __ARM_CORE_SCALING_H__ */
