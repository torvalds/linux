/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2013-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/**
 * DOC: Default values for configuration settings
 *
 */

#ifndef _KBASE_CONFIG_DEFAULTS_H_
#define _KBASE_CONFIG_DEFAULTS_H_

/* Include mandatory definitions per platform */
#include <mali_kbase_config_platform.h>

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

enum {
	/**
	 * Use unrestricted Address ID width on the AXI bus.
	 * Restricting ID width will reduce performance & bus load due to GPU.
	 */
	KBASE_3BIT_AID_32 = 0x0,

	/* Restrict GPU to 7/8 of maximum Address ID count. */
	KBASE_3BIT_AID_28 = 0x1,

	/* Restrict GPU to 3/4 of maximum Address ID count. */
	KBASE_3BIT_AID_24 = 0x2,

	/* Restrict GPU to 5/8 of maximum Address ID count. */
	KBASE_3BIT_AID_20 = 0x3,

	/* Restrict GPU to 1/2 of maximum Address ID count.  */
	KBASE_3BIT_AID_16 = 0x4,

	/* Restrict GPU to 3/8 of maximum Address ID count. */
	KBASE_3BIT_AID_12 = 0x5,

	/* Restrict GPU to 1/4 of maximum Address ID count. */
	KBASE_3BIT_AID_8  = 0x6,

	/* Restrict GPU to 1/8 of maximum Address ID count. */
	KBASE_3BIT_AID_4  = 0x7
};

/**
 * Default period for DVFS sampling (can be overridden by platform header)
 */
#ifndef DEFAULT_PM_DVFS_PERIOD
#define DEFAULT_PM_DVFS_PERIOD 100 /* 100ms */
#endif

/**
 * Power Management poweroff tick granuality. This is in nanoseconds to
 * allow HR timer support (can be overridden by platform header).
 *
 * On each scheduling tick, the power manager core may decide to:
 * -# Power off one or more shader cores
 * -# Power off the entire GPU
 */
#ifndef DEFAULT_PM_GPU_POWEROFF_TICK_NS
#define DEFAULT_PM_GPU_POWEROFF_TICK_NS (400000) /* 400us */
#endif

/**
 * Power Manager number of ticks before shader cores are powered off
 * (can be overridden by platform header).
 */
#ifndef DEFAULT_PM_POWEROFF_TICK_SHADER
#define DEFAULT_PM_POWEROFF_TICK_SHADER (2) /* 400-800us */
#endif

/**
 * Default scheduling tick granuality (can be overridden by platform header)
 */
#ifndef DEFAULT_JS_SCHEDULING_PERIOD_NS
#define DEFAULT_JS_SCHEDULING_PERIOD_NS    (100000000u) /* 100ms */
#endif

/**
 * Default minimum number of scheduling ticks before jobs are soft-stopped.
 *
 * This defines the time-slice for a job (which may be different from that of a
 * context)
 */
#define DEFAULT_JS_SOFT_STOP_TICKS       (1) /* 100ms-200ms */

/**
 * Default minimum number of scheduling ticks before CL jobs are soft-stopped.
 */
#define DEFAULT_JS_SOFT_STOP_TICKS_CL    (1) /* 100ms-200ms */

/**
 * Default minimum number of scheduling ticks before jobs are hard-stopped
 */
#define DEFAULT_JS_HARD_STOP_TICKS_SS    (50) /* 5s */

/**
 * Default minimum number of scheduling ticks before CL jobs are hard-stopped.
 */
#define DEFAULT_JS_HARD_STOP_TICKS_CL    (50) /* 5s */

/**
 * Default minimum number of scheduling ticks before jobs are hard-stopped
 * during dumping
 */
#define DEFAULT_JS_HARD_STOP_TICKS_DUMPING   (15000) /* 1500s */

/**
 * Default timeout for some software jobs, after which the software event wait
 * jobs will be cancelled.
 */
#define DEFAULT_JS_SOFT_JOB_TIMEOUT (3000) /* 3s */

/**
 * Default minimum number of scheduling ticks before the GPU is reset to clear a
 * "stuck" job
 */
#define DEFAULT_JS_RESET_TICKS_SS           (55) /* 5.5s */

/**
 * Default minimum number of scheduling ticks before the GPU is reset to clear a
 * "stuck" CL job.
 */
#define DEFAULT_JS_RESET_TICKS_CL        (55) /* 5.5s */

/**
 * Default minimum number of scheduling ticks before the GPU is reset to clear a
 * "stuck" job during dumping.
 */
#define DEFAULT_JS_RESET_TICKS_DUMPING   (15020) /* 1502s */

/**
 * Default number of milliseconds given for other jobs on the GPU to be
 * soft-stopped when the GPU needs to be reset.
 */
#define DEFAULT_RESET_TIMEOUT_MS (3000) /* 3s */

/**
 * Default timeslice that a context is scheduled in for, in nanoseconds.
 *
 * When a context has used up this amount of time across its jobs, it is
 * scheduled out to let another run.
 *
 * @note the resolution is nanoseconds (ns) here, because that's the format
 * often used by the OS.
 */
#define DEFAULT_JS_CTX_TIMESLICE_NS (50000000) /* 50ms */

/**
 * Maximum frequency (in kHz) that the GPU can be clocked. For some platforms
 * this isn't available, so we simply define a dummy value here. If devfreq
 * is enabled the value will be read from there, otherwise this should be
 * overridden by defining GPU_FREQ_KHZ_MAX in the platform file.
 */
#define DEFAULT_GPU_FREQ_KHZ_MAX (5000)

/**
 * Default timeout for task execution on an endpoint
 *
 * Number of GPU clock cycles before the driver terminates a task that is
 * making no forward progress on an endpoint (e.g. shader core).
 * Value chosen is equivalent to the time after which a job is hard stopped
 * which is 5 seconds (assuming the GPU is usually clocked at ~500 MHZ).
 */
#define DEFAULT_PROGRESS_TIMEOUT ((u64)5 * 500 * 1024 * 1024)

/**
 * Default threshold at which to switch to incremental rendering
 *
 * Fraction of the maximum size of an allocation that grows on GPU page fault
 * that can be used up before the driver switches to incremental rendering,
 * in 256ths. 0 means disable incremental rendering.
 */
#define DEFAULT_IR_THRESHOLD (192)

#endif /* _KBASE_CONFIG_DEFAULTS_H_ */

