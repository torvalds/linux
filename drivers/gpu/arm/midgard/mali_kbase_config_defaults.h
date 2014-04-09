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

/* Default irq throttle time. This is the default desired minimum time in
 * between two consecutive interrupts from the gpu. The irq throttle gpu
 * register is set after this value. */
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
 * Default initial runtime of a context for CFS, in ticks.
 *
 * This value is relative to that of the least-run context, and defines where
 * in the CFS queue a new context is added.
 */
#define DEFAULT_JS_CFS_CTX_RUNTIME_INIT_SLICES 1

/**
 * Default minimum runtime value of a context for CFS, in ticks.
 *
 * This value is relative to that of the least-run context. This prevents
 * "stored-up timeslices" DoS attacks.
 */
#define DEFAULT_JS_CFS_CTX_RUNTIME_MIN_SLICES 2

/**
 * Default setting for whether to prefer security or performance.
 *
 * Currently affects only r0p0-15dev0 HW and earlier.
 */
#define DEFAULT_SECURE_BUT_LOSS_OF_PERFORMANCE MALI_FALSE

/**
 * Default setting for read Address ID limiting on AXI.
 */
#define DEFAULT_ARID_LIMIT KBASE_AID_32

/**
 * Default setting for write Address ID limiting on AXI.
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

