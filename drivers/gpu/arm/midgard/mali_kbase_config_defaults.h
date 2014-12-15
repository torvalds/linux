/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_config_defaults.h
 *
 * Default values for configuration settings
 *
 */

#ifndef _KBASE_CONFIG_DEFAULTS_H_
#define _KBASE_CONFIG_DEFAULTS_H_

/* Include mandatory definitions per platform */
#include <mali_kbase_config_platform.h>

/**
 * Irq throttle. It is the minimum desired time in between two
 * consecutive gpu interrupts (given in 'us'). The irq throttle
 * gpu register will be configured after this, taking into
 * account the configured max frequency.
 *
 * Attached value: number in micro seconds
 */
#define DEFAULT_IRQ_THROTTLE_TIME_US 20

/*** Begin Scheduling defaults ***/

/**
 * Default scheduling tick granuality, in nanoseconds
 */
/* 50ms */
#define DEFAULT_JS_SCHEDULING_TICK_NS 50000000u

/**
 * Default minimum number of scheduling ticks before jobs are soft-stopped.
 *
 * This defines the time-slice for a job (which may be different from that of
 * a context)
 */
/* Between 0.1 and 0.15s before soft-stop */
#define DEFAULT_JS_SOFT_STOP_TICKS 2

/**
 * Default minimum number of scheduling ticks before CL jobs are soft-stopped.
 */
/* Between 0.05 and 0.1s before soft-stop */
#define DEFAULT_JS_SOFT_STOP_TICKS_CL 1

/**
 * Default minimum number of scheduling ticks before jobs are hard-stopped
 */
/* 1.2s before hard-stop, for a certain GLES2 test at 128x128 (bound by
 * combined vertex+tiler job)
 */
#define DEFAULT_JS_HARD_STOP_TICKS_SS_HW_ISSUE_8408 24
/* Between 0.2 and 0.25s before hard-stop */
#define DEFAULT_JS_HARD_STOP_TICKS_SS 4

/**
 * Default minimum number of scheduling ticks before CL jobs are hard-stopped.
 */
/* Between 0.1 and 0.15s before hard-stop */
#define DEFAULT_JS_HARD_STOP_TICKS_CL 2

/**
 * Default minimum number of scheduling ticks before jobs are hard-stopped
 * during dumping
 */
/* 60s @ 50ms tick */
#define DEFAULT_JS_HARD_STOP_TICKS_NSS 1200

/**
 * Default minimum number of scheduling ticks before the GPU is reset
 * to clear a "stuck" job
 */
/* 1.8s before resetting GPU, for a certain GLES2 test at 128x128 (bound by
 * combined vertex+tiler job)
 */
#define DEFAULT_JS_RESET_TICKS_SS_HW_ISSUE_8408 36
/* 0.3-0.35s before GPU is reset */
#define DEFAULT_JS_RESET_TICKS_SS 6

/**
 * Default minimum number of scheduling ticks before the GPU is reset
 * to clear a "stuck" CL job.
 */
/* 0.2-0.25s before GPU is reset */
#define DEFAULT_JS_RESET_TICKS_CL 4

/**
 * Default minimum number of scheduling ticks before the GPU is reset
 * to clear a "stuck" job during dumping.
 */
/* 60.1s @ 100ms tick */
#define DEFAULT_JS_RESET_TICKS_NSS 1202

/**
 * Number of milliseconds given for other jobs on the GPU to be
 * soft-stopped when the GPU needs to be reset.
 */
#define DEFAULT_JS_RESET_TIMEOUT_MS 3000

/**
 * Default timeslice that a context is scheduled in for, in nanoseconds.
 *
 * When a context has used up this amount of time across its jobs, it is
 * scheduled out to let another run.
 *
 * @note the resolution is nanoseconds (ns) here, because that's the format
 * often used by the OS.
 */
/* 0.05s - at 20fps a ctx does at least 1 frame before being scheduled out.
 * At 40fps, 2 frames, etc
 */
#define DEFAULT_JS_CTX_TIMESLICE_NS 50000000

/**
 *  Default Job Scheduler initial runtime of a context for the CFS Policy,
 *  in time-slices.
 *
 * This value is relative to that of the least-run context, and defines
 * where in the CFS queue a new context is added. A value of 1 means 'after
 * the least-run context has used its timeslice'. Therefore, when all
 * contexts consistently use the same amount of time, a value of 1 models a
 * FIFO. A value of 0 would model a LIFO.
 *
 * The value is represented in "numbers of time slices". Multiply this
 * value by that defined in @ref DEFAULT_JS_CTX_TIMESLICE_NS to get
 * the time value for this in nanoseconds.
 */
#define DEFAULT_JS_CFS_CTX_RUNTIME_INIT_SLICES 1

/**
 * Default Job Scheduler minimum runtime value of a context for CFS, in
 * time_slices relative to that of the least-run context.
 *
 * This is a measure of how much preferrential treatment is given to a
 * context that is not run very often.
 *
 * Specficially, this value defines how many timeslices such a context is
 * (initially) allowed to use at once. Such contexts (e.g. 'interactive'
 * processes) will appear near the front of the CFS queue, and can initially
 * use more time than contexts that run continuously (e.g. 'batch'
 * processes).
 *
 * This limit \b prevents a "stored-up timeslices" DoS attack, where a ctx
 * not run for a long time attacks the system by using a very large initial
 * number of timeslices when it finally does run.
 *
 * @note A value of zero allows not-run-often contexts to get scheduled in
 * quickly, but to only use a single timeslice when they get scheduled in.
 */
#define DEFAULT_JS_CFS_CTX_RUNTIME_MIN_SLICES 2

/**
* Boolean indicating whether the driver is configured to be secure at
* a potential loss of performance.
*
* This currently affects only r0p0-15dev0 HW and earlier.
*
* On r0p0-15dev0 HW and earlier, there are tradeoffs between security and
* performance:
*
* - When this is set to MALI_TRUE, the driver remains fully secure,
* but potentially loses performance compared with setting this to
* MALI_FALSE.
* - When set to MALI_FALSE, the driver is open to certain security
* attacks.
*
* From r0p0-00rel0 and onwards, there is no security loss by setting
* this to MALI_FALSE, and no performance loss by setting it to
* MALI_TRUE.
*/
#define DEFAULT_SECURE_BUT_LOSS_OF_PERFORMANCE MALI_FALSE

enum {
	/**
	 * Use unrestricted Address ID width on the AXI bus.
	 */
	KBASE_AID_32 = 0x0,

	/**
	 * Restrict GPU to a half of maximum Address ID count.
	 * This will reduce performance, but reduce bus load due to GPU.
	 */
	KBASE_AID_16 = 0x3,

	/**
	 * Restrict GPU to a quarter of maximum Address ID count.
	 * This will reduce performance, but reduce bus load due to GPU.
	 */
	KBASE_AID_8  = 0x2,

	/**
	 * Restrict GPU to an eighth of maximum Address ID count.
	 * This will reduce performance, but reduce bus load due to GPU.
	 */
	KBASE_AID_4  = 0x1
};

/**
 * Default setting for read Address ID limiting on AXI bus.
 *
 * Attached value: u32 register value
 *    KBASE_AID_32 - use the full 32 IDs (5 ID bits)
 *    KBASE_AID_16 - use 16 IDs (4 ID bits)
 *    KBASE_AID_8  - use 8 IDs (3 ID bits)
 *    KBASE_AID_4  - use 4 IDs (2 ID bits)
 * Default value: KBASE_AID_32 (no limit). Note hardware implementation
 * may limit to a lower value.
 */
#define DEFAULT_ARID_LIMIT KBASE_AID_32

/**
 * Default setting for write Address ID limiting on AXI.
 *
 * Attached value: u32 register value
 *    KBASE_AID_32 - use the full 32 IDs (5 ID bits)
 *    KBASE_AID_16 - use 16 IDs (4 ID bits)
 *    KBASE_AID_8  - use 8 IDs (3 ID bits)
 *    KBASE_AID_4  - use 4 IDs (2 ID bits)
 * Default value: KBASE_AID_32 (no limit). Note hardware implementation
 * may limit to a lower value.
 */
#define DEFAULT_AWID_LIMIT KBASE_AID_32

/**
 * Default setting for using alternative hardware counters.
 */
#define DEFAULT_ALTERNATIVE_HWC MALI_FALSE

/*** End Scheduling defaults ***/

/*** Begin Power Manager defaults */

/* Milliseconds */
#define DEFAULT_PM_DVFS_FREQ 500

/**
 * Default poweroff tick granuality, in nanoseconds
 */
/* 400us */
#define DEFAULT_PM_GPU_POWEROFF_TICK_NS 400000

/**
 * Default number of poweroff ticks before shader cores are powered off
 */
/* 400-800us */
#define DEFAULT_PM_POWEROFF_TICK_SHADER 2

/**
 * Default number of poweroff ticks before GPU is powered off
 */
#define DEFAULT_PM_POWEROFF_TICK_GPU 2         /* 400-800us */

/*** End Power Manager defaults ***/


/**
 * Default UMP device mapping. A UMP_DEVICE_<device>_SHIFT value which
 * defines which UMP device this GPU should be mapped to.
 */
#define DEFAULT_UMP_GPU_DEVICE_SHIFT UMP_DEVICE_Z_SHIFT

/**
 * Default value for KBASE_CONFIG_ATTR_CPU_SPEED_FUNC.
 * Points to @ref kbase_cpuprops_get_default_clock_speed.
 */
#define DEFAULT_CPU_SPEED_FUNC \
	((uintptr_t)kbase_cpuprops_get_default_clock_speed)

#endif /* _KBASE_CONFIG_DEFAULTS_H_ */

