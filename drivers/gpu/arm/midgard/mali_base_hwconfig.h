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
 * @file
 * Software workarounds configuration for Hardware issues.
 */

#ifndef _BASE_HWCONFIG_H_
#define _BASE_HWCONFIG_H_

#include <malisw/mali_malisw.h>

/**
 * List of all hw features.
 *
 */
typedef enum base_hw_feature {
	/* Allow soft/hard stopping of job depending on job chain flag */
	BASE_HW_FEATURE_JOBCHAIN_DISAMBIGUATION,

	/* Allow writes to SHADER_PWRON and TILER_PWRON registers while these cores are currently transitioning to OFF power state */
	BASE_HW_FEATURE_PWRON_DURING_PWROFF_TRANS,

	/* The BASE_HW_FEATURE_END value must be the last feature listed in this enumeration
	 * and must be the last value in each array that contains the list of features
	 * for a particular HW version.
	 */
	BASE_HW_FEATURE_END
} base_hw_feature;

static const base_hw_feature base_hw_features_generic[] = {
	BASE_HW_FEATURE_END
}; 

static const base_hw_feature base_hw_features_t76x[] = {
	BASE_HW_FEATURE_JOBCHAIN_DISAMBIGUATION,
	BASE_HW_FEATURE_PWRON_DURING_PWROFF_TRANS,
	BASE_HW_FEATURE_END
};


/**
 * List of all workarounds.
 *
 */

typedef enum base_hw_issue {

	/* The current version of the model doesn't support Soft-Stop */
	BASE_HW_ISSUE_5736,

	/* Need way to guarantee that all previously-translated memory accesses are commited */
	BASE_HW_ISSUE_6367,

	/* Result swizzling doesn't work for GRDESC/GRDESC_DER */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_6398,

	/* Unaligned load stores crossing 128 bit boundaries will fail */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_6402,

	/* On job complete with non-done the cache is not flushed */
	BASE_HW_ISSUE_6787,

	/* WLS allocation does not respect the Instances field in the Thread Storage Descriptor */
	BASE_HW_ISSUE_7027,

	/* The clamp integer coordinate flag bit of the sampler descriptor is reserved */
	BASE_HW_ISSUE_7144,

	/* TEX_INDEX LOD is always use converted */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_8073,

	/* Write of PRFCNT_CONFIG_MODE_MANUAL to PRFCNT_CONFIG causes a instrumentation dump if
	   PRFCNT_TILER_EN is enabled */
	BASE_HW_ISSUE_8186,

	/* Do not set .skip flag on the GRDESC, GRDESC_DER, DELTA, MOV, and NOP texturing instructions */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_8215,

	/* TIB: Reports faults from a vtile which has not yet been allocated */
	BASE_HW_ISSUE_8245,

	/* WLMA memory goes wrong when run on shader cores other than core 0. */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_8250,

	/* Hierz doesn't work when stenciling is enabled */
	BASE_HW_ISSUE_8260,

	/* Livelock in L0 icache */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_8280,

	/* uTLB deadlock could occur when writing to an invalid page at the same time as
	 * access to a valid page in the same uTLB cache line ( == 4 PTEs == 16K block of mapping) */
	BASE_HW_ISSUE_8316,

	/* TLS base address mismatch, must stay below 1MB TLS */
	BASE_HW_ISSUE_8381,

	/* HT: TERMINATE for RUN command ignored if previous LOAD_DESCRIPTOR is still executing */
	BASE_HW_ISSUE_8394,

	/* CSE : Sends a TERMINATED response for a task that should not be terminated */
	/* (Note that PRLAM-8379 also uses this workaround) */
	BASE_HW_ISSUE_8401,

	/* Repeatedly Soft-stopping a job chain consisting of (Vertex Shader, Cache Flush, Tiler)
	 * jobs causes 0x58 error on tiler job. */
	BASE_HW_ISSUE_8408,

	/* Disable the Pause Buffer in the LS pipe. */
	BASE_HW_ISSUE_8443,

	/* Stencil test enable 1->0 sticks */
	BASE_HW_ISSUE_8456,

	/* Tiler heap issue using FBOs or multiple processes using the tiler simultaneously */
	/* (Note that PRLAM-9049 also uses this work-around) */
	BASE_HW_ISSUE_8564,

	/* Livelock issue using atomic instructions (particularly when using atomic_cmpxchg as a spinlock) */
	BASE_HW_ISSUE_8791,

	/* Fused jobs are not supported (for various reasons) */
	/* Jobs with relaxed dependencies do not support soft-stop */
	/* (Note that PRLAM-8803, PRLAM-8393, PRLAM-8559, PRLAM-8601 & PRLAM-8607 all use this work-around) */
	BASE_HW_ISSUE_8803,

	/* Blend shader output is wrong for certain formats */
	BASE_HW_ISSUE_8833,

	/* Occlusion queries can create false 0 result in boolean and counter modes */
	BASE_HW_ISSUE_8879,

	/* Output has half intensity with blend shaders enabled on 8xMSAA. */
	BASE_HW_ISSUE_8896,

	/* 8xMSAA does not work with CRC */
	BASE_HW_ISSUE_8975,

	/* Boolean occlusion queries don't work properly due to sdc issue. */
	BASE_HW_ISSUE_8986,

	/* Change in RMUs in use causes problems related with the core's SDC */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_8987,

	/* Occlusion query result is not updated if color writes are disabled. */
	BASE_HW_ISSUE_9010,

	/* Problem with number of work registers in the RSD if set to 0 */
	BASE_HW_ISSUE_9275,

	/* Translate load/store moves into decode instruction */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_9418,

	/* Incorrect coverage mask for 8xMSAA */
	BASE_HW_ISSUE_9423,

	/* Compute endpoint has a 4-deep queue of tasks, meaning a soft stop won't complete until all 4 tasks have completed */
	BASE_HW_ISSUE_9435,

	/* HT: Tiler returns TERMINATED for command that hasn't been terminated */
	BASE_HW_ISSUE_9510,

	/* Livelock issue using atomic_cmpxchg */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_9566,

	/* Occasionally the GPU will issue multiple page faults for the same address before the MMU page table has been read by the GPU */
	BASE_HW_ISSUE_9630,

	/* Must clear the 64 byte private state of the tiler information */
	BASE_HW_ISSUE_10127,

	/* RA DCD load request to SDC returns invalid load ignore causing colour buffer mismatch */
	BASE_HW_ISSUE_10327,

	/* Occlusion query result may be updated prematurely when fragment shader alters coverage */
	BASE_HW_ISSUE_10410,

	/* TEXGRD doesn't honor Sampler Descriptor LOD clamps nor bias */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_10471,

	/* MAG / MIN filter selection happens after image descriptor clamps were applied */
	BASE_HW_ISSUE_10472,

	/* GPU interprets sampler and image descriptor pointer array sizes as one bigger than they are defined in midg structures */
	BASE_HW_ISSUE_10487,

	/* ld_special 0x1n applies SRGB conversion */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_10607,

	/* LD_SPECIAL instruction reads incorrect RAW tile buffer value when internal tib format is R10G10B10A2 */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_10632,

	/* MMU TLB invalidation hazards */
	BASE_HW_ISSUE_10649,

	/* Missing cache flush in multi core-group configuration */
	BASE_HW_ISSUE_10676,

	/* Indexed format 95 cannot be used with a component swizzle of "set to 1" when sampled as integer texture */
	BASE_HW_ISSUE_10682,

	/* sometimes HW doesn't invalidate cached VPDs when it has to */
	BASE_HW_ISSUE_10684,

	/* Chicken bit on (t67x_r1p0 and t72x) to work for a HW workaround in compiler */
	BASE_HW_ISSUE_10797,

	/* Soft-stopping fragment jobs might fail with TILE_RANGE_FAULT */
	BASE_HW_ISSUE_10817,

	/* Fragment frontend heuristic bias to force early-z required */
	BASE_HW_ISSUE_10821,

	/* Intermittent missing interrupt on job completion */
	BASE_HW_ISSUE_10883,

	/* Depth bounds incorrectly normalized in hierz depth bounds test */
	BASE_HW_ISSUE_10931,

	/* Incorrect cubemap sampling */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_10946,

	/* Soft-stopping fragment jobs might fail with TILE_RANGE_ERROR (similar to issue 10817) and can use BASE_HW_ISSUE_10817 workaround  */
	BASE_HW_ISSUE_10959,

	/* Soft-stopped fragment shader job can restart with out-of-bound restart index  */
	BASE_HW_ISSUE_10969,

	/* Instanced arrays conformance fail, workaround by unrolling */
	BASE_HW_ISSUE_10984,

	/* TEX_INDEX lod selection (immediate , register) not working with 8.8 encoding for levels > 1 */
	/* NOTE: compiler workaround: keep in sync with _essl_hwrev_needs_workaround() */
	BASE_HW_ISSUE_10995,

	/* LD_SPECIAL instruction reads incorrect RAW tile buffer value when internal tib format is RGB565 or RGBA5551 */
	BASE_HW_ISSUE_11012,

	/* Race condition can cause tile list corruption */
	BASE_HW_ISSUE_11020,

	/* Write buffer can cause tile list corruption */
	BASE_HW_ISSUE_11024,

	/* T76X hw issues */

	/* Partial 16xMSAA support */
	BASE_HW_ISSUE_T76X_26,

	/* Forward pixel kill doesn't work with MRT */
	BASE_HW_ISSUE_T76X_2121,

	/* CRC not working with MFBD and more than one render target */
	BASE_HW_ISSUE_T76X_2315,

	/* Some indexed formats not supported for MFBD preload. */
	BASE_HW_ISSUE_T76X_2686,

	/* Must disable CRC if the tile output size is 8 bytes or less. */
	BASE_HW_ISSUE_T76X_2712,

	/* DBD clean pixel enable bit is reserved */
	BASE_HW_ISSUE_T76X_2772,

	/* AFBC is not supported for T76X beta. */
	BASE_HW_ISSUE_T76X_2906,

	/* Prevent MMU deadlock for T76X beta. */
	BASE_HW_ISSUE_T76X_3285,

	/* Clear encoder state for a hard stopped fragment job which is AFBC
	 * encoded by soft resetting the GPU. Only for T76X r0p0 and r0p1
	 */
	BASE_HW_ISSUE_T76X_3542,

	/* Do not use 8xMSAA with 16x8 pixel tile size or 16xMSAA with 8x8 pixel
	 * tile size.
	 */
	BASE_HW_ISSUE_T76X_3556,

	/* T76X cannot disable uses_discard even if depth and stencil are read-only. */
	BASE_HW_ISSUE_T76X_3700,

	/* ST_TILEBUFFER is not supported on T76X-r0p0-beta */
	BASE_HW_ISSUE_T76X_3759,

	/* Preload ignores any size or bounding box restrictions of the output image. */
	BASE_HW_ISSUE_T76X_3793,

	/* The BASE_HW_ISSUE_END value must be the last issue listed in this enumeration
	 * and must be the last value in each array that contains the list of workarounds
	 * for a particular HW version.
	 */
	BASE_HW_ISSUE_END
} base_hw_issue;

/**
 * Workarounds configuration for each HW revision
 */
/* Mali T60x r0p0-15dev0 - 2011-W39-stable-9 */
static const base_hw_issue base_hw_issues_t60x_r0p0_15dev0[] = {
	BASE_HW_ISSUE_6367,
	BASE_HW_ISSUE_6398,
	BASE_HW_ISSUE_6402,
	BASE_HW_ISSUE_6787,
	BASE_HW_ISSUE_7027,
	BASE_HW_ISSUE_7144,
	BASE_HW_ISSUE_8073,
	BASE_HW_ISSUE_8186,
	BASE_HW_ISSUE_8215,
	BASE_HW_ISSUE_8245,
	BASE_HW_ISSUE_8250,
	BASE_HW_ISSUE_8260,
	BASE_HW_ISSUE_8280,
	BASE_HW_ISSUE_8316,
	BASE_HW_ISSUE_8381,
	BASE_HW_ISSUE_8394,
	BASE_HW_ISSUE_8401,
	BASE_HW_ISSUE_8408,
	BASE_HW_ISSUE_8443,
	BASE_HW_ISSUE_8456,
	BASE_HW_ISSUE_8564,
	BASE_HW_ISSUE_8791,
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_8833,
	BASE_HW_ISSUE_8896,
	BASE_HW_ISSUE_8975,
	BASE_HW_ISSUE_8986,
	BASE_HW_ISSUE_8987,
	BASE_HW_ISSUE_9010,
	BASE_HW_ISSUE_9275,
	BASE_HW_ISSUE_9418,
	BASE_HW_ISSUE_9423,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_9510,
	BASE_HW_ISSUE_9566,
	BASE_HW_ISSUE_9630,
	BASE_HW_ISSUE_10410,
	BASE_HW_ISSUE_10471,
	BASE_HW_ISSUE_10472,
	BASE_HW_ISSUE_10487,
	BASE_HW_ISSUE_10607,
	BASE_HW_ISSUE_10632,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10676,
	BASE_HW_ISSUE_10682,
	BASE_HW_ISSUE_10684,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_10969,
	BASE_HW_ISSUE_10984,
	BASE_HW_ISSUE_10995,
	BASE_HW_ISSUE_11012,
	BASE_HW_ISSUE_11020,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T60x r0p0-00rel0 - 2011-W46-stable-13c */
static const base_hw_issue base_hw_issues_t60x_r0p0_eac[] = {
	BASE_HW_ISSUE_6367,
	BASE_HW_ISSUE_6402,
	BASE_HW_ISSUE_6787,
	BASE_HW_ISSUE_7027,
	BASE_HW_ISSUE_8408,
	BASE_HW_ISSUE_8564,
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_8975,
	BASE_HW_ISSUE_9010,
	BASE_HW_ISSUE_9275,
	BASE_HW_ISSUE_9418,
	BASE_HW_ISSUE_9423,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_9510,
	BASE_HW_ISSUE_10410,
	BASE_HW_ISSUE_10471,
	BASE_HW_ISSUE_10472,
	BASE_HW_ISSUE_10487,
	BASE_HW_ISSUE_10607,
	BASE_HW_ISSUE_10632,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10676,
	BASE_HW_ISSUE_10682,
	BASE_HW_ISSUE_10684,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_10969,
	BASE_HW_ISSUE_11012,
	BASE_HW_ISSUE_11020,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T60x r0p1 */
static const base_hw_issue base_hw_issues_t60x_r0p1[] = {
	BASE_HW_ISSUE_6367,
	BASE_HW_ISSUE_6402,
	BASE_HW_ISSUE_6787,
	BASE_HW_ISSUE_7027,
	BASE_HW_ISSUE_8408,
	BASE_HW_ISSUE_8564,
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_8975,
	BASE_HW_ISSUE_9010,
	BASE_HW_ISSUE_9275,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_9510,
	BASE_HW_ISSUE_10410,
	BASE_HW_ISSUE_10471,
	BASE_HW_ISSUE_10472,
	BASE_HW_ISSUE_10487,
	BASE_HW_ISSUE_10607,
	BASE_HW_ISSUE_10632,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10676,
	BASE_HW_ISSUE_10682,
	BASE_HW_ISSUE_10684,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_11012,
	BASE_HW_ISSUE_11020,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T62x r0p1 */
static const base_hw_issue base_hw_issues_t62x_r0p1[] = {
	BASE_HW_ISSUE_6402,
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10127,
	BASE_HW_ISSUE_10327,
	BASE_HW_ISSUE_10410,
	BASE_HW_ISSUE_10471,
	BASE_HW_ISSUE_10472,
	BASE_HW_ISSUE_10487,
	BASE_HW_ISSUE_10607,
	BASE_HW_ISSUE_10632,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10676,
	BASE_HW_ISSUE_10682,
	BASE_HW_ISSUE_10684,
	BASE_HW_ISSUE_10817,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_10959,
	BASE_HW_ISSUE_11012,
	BASE_HW_ISSUE_11020,
	BASE_HW_ISSUE_11024,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T62x r1p0 */
static const base_hw_issue base_hw_issues_t62x_r1p0[] = {
	BASE_HW_ISSUE_6402,
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10471,
	BASE_HW_ISSUE_10472,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10684,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_10959,
	BASE_HW_ISSUE_11012,
	BASE_HW_ISSUE_11020,
	BASE_HW_ISSUE_11024,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T67x r1p0 */
static const base_hw_issue base_hw_issues_t67x_r1p0[] = {
	BASE_HW_ISSUE_6402,
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10471,
	BASE_HW_ISSUE_10472,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10684,
	BASE_HW_ISSUE_10797,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_10959,
	BASE_HW_ISSUE_11012,
	BASE_HW_ISSUE_11020,
	BASE_HW_ISSUE_11024,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T76x r0p0 beta */
static const base_hw_issue base_hw_issues_t76x_r0p0_beta[] = {
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_10959,
	BASE_HW_ISSUE_11020,
	BASE_HW_ISSUE_11024,
	BASE_HW_ISSUE_T76X_26,
	BASE_HW_ISSUE_T76X_2121,
	BASE_HW_ISSUE_T76X_2315,
	BASE_HW_ISSUE_T76X_2686,
	BASE_HW_ISSUE_T76X_2712,
	BASE_HW_ISSUE_T76X_2772,
	BASE_HW_ISSUE_T76X_2906,
	BASE_HW_ISSUE_T76X_3285,
	BASE_HW_ISSUE_T76X_3700,
	BASE_HW_ISSUE_T76X_3759,
	BASE_HW_ISSUE_T76X_3793,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T76x r0p0 */
static const base_hw_issue base_hw_issues_t76x_r0p0[] = {
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_11020,
	BASE_HW_ISSUE_11024,
	BASE_HW_ISSUE_T76X_26,
	BASE_HW_ISSUE_T76X_3542,
	BASE_HW_ISSUE_T76X_3556,
	BASE_HW_ISSUE_T76X_3700,
	BASE_HW_ISSUE_T76X_3793,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T76x r0p1 */
static const base_hw_issue base_hw_issues_t76x_r0p1[] = {
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_11020,
	BASE_HW_ISSUE_11024,
	BASE_HW_ISSUE_T76X_26,
	BASE_HW_ISSUE_T76X_3542,
	BASE_HW_ISSUE_T76X_3556,
	BASE_HW_ISSUE_T76X_3700,
	BASE_HW_ISSUE_T76X_3793,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T76x r0p2 */
static const base_hw_issue base_hw_issues_t76x_r0p2[] = {
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_11020,
	BASE_HW_ISSUE_11024,
	BASE_HW_ISSUE_T76X_26,
	BASE_HW_ISSUE_T76X_3542,
	BASE_HW_ISSUE_T76X_3556,
	BASE_HW_ISSUE_T76X_3700,
	BASE_HW_ISSUE_T76X_3793,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T76x r1p0 */
static const base_hw_issue base_hw_issues_t76x_r1p0[] = {
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10946,
	BASE_HW_ISSUE_T76X_3700,
	BASE_HW_ISSUE_T76X_3793,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};


/* Mali T72x r0p0 */
static const base_hw_issue base_hw_issues_t72x_r0p0[] = {
	BASE_HW_ISSUE_6402,
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10471,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10684,
	BASE_HW_ISSUE_10797,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_10946,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Mali T72x r1p0 */
static const base_hw_issue base_hw_issues_t72x_r1p0[] = {
	BASE_HW_ISSUE_6402,
	BASE_HW_ISSUE_8803,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10471,
	BASE_HW_ISSUE_10649,
	BASE_HW_ISSUE_10684,
	BASE_HW_ISSUE_10797,
	BASE_HW_ISSUE_10821,
	BASE_HW_ISSUE_10883,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_10946,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

/* Model configuration
 */
static const base_hw_issue base_hw_issues_model_t72x[] =
{
	BASE_HW_ISSUE_5736,
	BASE_HW_ISSUE_6402, /* NOTE: Fix is present in model r125162 but is not enabled until RTL is fixed */
	BASE_HW_ISSUE_9275,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10471,
	BASE_HW_ISSUE_10797,
	BASE_HW_ISSUE_10931,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

static const base_hw_issue base_hw_issues_model_t7xx[] =
{
	BASE_HW_ISSUE_5736,
	BASE_HW_ISSUE_9275,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_11020,
	BASE_HW_ISSUE_11024,
	BASE_HW_ISSUE_T76X_3700,
	BASE_HW_ISSUE_T76X_3793,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

static const base_hw_issue base_hw_issues_model_t6xx[] =
{
	BASE_HW_ISSUE_5736,
	BASE_HW_ISSUE_6402, /* NOTE: Fix is present in model r125162 but is not enabled until RTL is fixed */
	BASE_HW_ISSUE_9275,
	BASE_HW_ISSUE_9435,
	BASE_HW_ISSUE_10472,
	BASE_HW_ISSUE_10931,
	BASE_HW_ISSUE_11012,
	BASE_HW_ISSUE_11020,
	BASE_HW_ISSUE_11024,
	/* List of hardware issues must end with BASE_HW_ISSUE_END */
	BASE_HW_ISSUE_END
};

#endif				/* _BASE_HWCONFIG_H_ */
