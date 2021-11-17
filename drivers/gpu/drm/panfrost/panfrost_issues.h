/* SPDX-License-Identifier: GPL-2.0 */
/* (C) COPYRIGHT 2014-2018 ARM Limited. All rights reserved. */
/* Copyright 2019 Linaro, Ltd., Rob Herring <robh@kernel.org> */
#ifndef __PANFROST_ISSUES_H__
#define __PANFROST_ISSUES_H__

#include <linux/bitops.h>

#include "panfrost_device.h"

/*
 * This is not a complete list of issues, but only the ones the driver needs
 * to care about.
 */
enum panfrost_hw_issue {
	/* Need way to guarantee that all previously-translated memory accesses
	 * are commited */
	HW_ISSUE_6367,

	/* On job complete with non-done the cache is not flushed */
	HW_ISSUE_6787,

	/* Write of PRFCNT_CONFIG_MODE_MANUAL to PRFCNT_CONFIG causes a
	 * instrumentation dump if PRFCNT_TILER_EN is enabled */
	HW_ISSUE_8186,

	/* TIB: Reports faults from a vtile which has not yet been allocated */
	HW_ISSUE_8245,

	/* uTLB deadlock could occur when writing to an invalid page at the
	 * same time as access to a valid page in the same uTLB cache line ( ==
	 * 4 PTEs == 16K block of mapping) */
	HW_ISSUE_8316,

	/* HT: TERMINATE for RUN command ignored if previous LOAD_DESCRIPTOR is
	 * still executing */
	HW_ISSUE_8394,

	/* CSE: Sends a TERMINATED response for a task that should not be
	 * terminated */
	HW_ISSUE_8401,

	/* Repeatedly Soft-stopping a job chain consisting of (Vertex Shader,
	 * Cache Flush, Tiler) jobs causes DATA_INVALID_FAULT on tiler job. */
	HW_ISSUE_8408,

	/* Disable the Pause Buffer in the LS pipe. */
	HW_ISSUE_8443,

	/* Change in RMUs in use causes problems related with the core's SDC */
	HW_ISSUE_8987,

	/* Compute endpoint has a 4-deep queue of tasks, meaning a soft stop
	 * won't complete until all 4 tasks have completed */
	HW_ISSUE_9435,

	/* HT: Tiler returns TERMINATED for non-terminated command */
	HW_ISSUE_9510,

	/* Occasionally the GPU will issue multiple page faults for the same
	 * address before the MMU page table has been read by the GPU */
	HW_ISSUE_9630,

	/* RA DCD load request to SDC returns invalid load ignore causing
	 * colour buffer mismatch */
	HW_ISSUE_10327,

	/* MMU TLB invalidation hazards */
	HW_ISSUE_10649,

	/* Missing cache flush in multi core-group configuration */
	HW_ISSUE_10676,

	/* Chicken bit on T72X for a hardware workaround in compiler */
	HW_ISSUE_10797,

	/* Soft-stopping fragment jobs might fail with TILE_RANGE_FAULT */
	HW_ISSUE_10817,

	/* Intermittent missing interrupt on job completion */
	HW_ISSUE_10883,

	/* Soft-stopping fragment jobs might fail with TILE_RANGE_ERROR
	 * (similar to issue 10817) and can use #10817 workaround */
	HW_ISSUE_10959,

	/* Soft-stopped fragment shader job can restart with out-of-bound
	 * restart index */
	HW_ISSUE_10969,

	/* Race condition can cause tile list corruption */
	HW_ISSUE_11020,

	/* Write buffer can cause tile list corruption */
	HW_ISSUE_11024,

	/* Pause buffer can cause a fragment job hang */
	HW_ISSUE_11035,

	/* Dynamic Core Scaling not supported due to errata */
	HW_ISSUE_11056,

	/* Clear encoder state for a hard stopped fragment job which is AFBC
	 * encoded by soft resetting the GPU. Only for T76X r0p0, r0p1 and
	 * r0p1_50rel0 */
	HW_ISSUE_T76X_3542,

	/* Keep tiler module clock on to prevent GPU stall */
	HW_ISSUE_T76X_3953,

	/* Must ensure L2 is not transitioning when we reset. Workaround with a
	 * busy wait until L2 completes transition; ensure there is a maximum
	 * loop count as she may never complete her transition. (On chips
	 * without this errata, it's totally okay if L2 transitions.) */
	HW_ISSUE_TMIX_8463,

	/* Don't set SC_LS_ATTR_CHECK_DISABLE/SC_LS_ALLOW_ATTR_TYPES */
	GPUCORE_1619,

	/* When a hard-stop follows close after a soft-stop, the completion
	 * code for the terminated job may be incorrectly set to STOPPED */
	HW_ISSUE_TMIX_8438,

	/* "Protected mode" is buggy on Mali-G31 some Bifrost chips, so the
	 * kernel must fiddle with L2 caches to prevent data leakage */
	HW_ISSUE_TGOX_R1_1234,

	HW_ISSUE_END
};

#define hw_issues_all (\
	BIT_ULL(HW_ISSUE_9435))

#define hw_issues_t600 (\
	BIT_ULL(HW_ISSUE_6367) | \
	BIT_ULL(HW_ISSUE_6787) | \
	BIT_ULL(HW_ISSUE_8408) | \
	BIT_ULL(HW_ISSUE_9510) | \
	BIT_ULL(HW_ISSUE_10649) | \
	BIT_ULL(HW_ISSUE_10676) | \
	BIT_ULL(HW_ISSUE_10883) | \
	BIT_ULL(HW_ISSUE_11020) | \
	BIT_ULL(HW_ISSUE_11035) | \
	BIT_ULL(HW_ISSUE_11056) | \
	BIT_ULL(HW_ISSUE_TMIX_8438))

#define hw_issues_t600_r0p0_15dev0 (\
	BIT_ULL(HW_ISSUE_8186) | \
	BIT_ULL(HW_ISSUE_8245) | \
	BIT_ULL(HW_ISSUE_8316) | \
	BIT_ULL(HW_ISSUE_8394) | \
	BIT_ULL(HW_ISSUE_8401) | \
	BIT_ULL(HW_ISSUE_8443) | \
	BIT_ULL(HW_ISSUE_8987) | \
	BIT_ULL(HW_ISSUE_9630) | \
	BIT_ULL(HW_ISSUE_10969) | \
	BIT_ULL(GPUCORE_1619))

#define hw_issues_t620 (\
	BIT_ULL(HW_ISSUE_10649) | \
	BIT_ULL(HW_ISSUE_10883) | \
	BIT_ULL(HW_ISSUE_10959) | \
	BIT_ULL(HW_ISSUE_11056) | \
	BIT_ULL(HW_ISSUE_TMIX_8438))

#define hw_issues_t620_r0p1 (\
	BIT_ULL(HW_ISSUE_10327) | \
	BIT_ULL(HW_ISSUE_10676) | \
	BIT_ULL(HW_ISSUE_10817) | \
	BIT_ULL(HW_ISSUE_11020) | \
	BIT_ULL(HW_ISSUE_11024) | \
	BIT_ULL(HW_ISSUE_11035))

#define hw_issues_t620_r1p0 (\
	BIT_ULL(HW_ISSUE_11020) | \
	BIT_ULL(HW_ISSUE_11024))

#define hw_issues_t720 (\
	BIT_ULL(HW_ISSUE_10649) | \
	BIT_ULL(HW_ISSUE_10797) | \
	BIT_ULL(HW_ISSUE_10883) | \
	BIT_ULL(HW_ISSUE_11056) | \
	BIT_ULL(HW_ISSUE_TMIX_8438))

#define hw_issues_t760 (\
	BIT_ULL(HW_ISSUE_10883) | \
	BIT_ULL(HW_ISSUE_T76X_3953) | \
	BIT_ULL(HW_ISSUE_TMIX_8438))

#define hw_issues_t760_r0p0 (\
	BIT_ULL(HW_ISSUE_11020) | \
	BIT_ULL(HW_ISSUE_11024) | \
	BIT_ULL(HW_ISSUE_T76X_3542))

#define hw_issues_t760_r0p1 (\
	BIT_ULL(HW_ISSUE_11020) | \
	BIT_ULL(HW_ISSUE_11024) | \
	BIT_ULL(HW_ISSUE_T76X_3542))

#define hw_issues_t760_r0p1_50rel0 (\
	BIT_ULL(HW_ISSUE_T76X_3542))

#define hw_issues_t760_r0p2 (\
	BIT_ULL(HW_ISSUE_11020) | \
	BIT_ULL(HW_ISSUE_11024) | \
	BIT_ULL(HW_ISSUE_T76X_3542))

#define hw_issues_t760_r0p3 (\
	BIT_ULL(HW_ISSUE_T76X_3542))

#define hw_issues_t820 (\
	BIT_ULL(HW_ISSUE_10883) | \
	BIT_ULL(HW_ISSUE_T76X_3953) | \
	BIT_ULL(HW_ISSUE_TMIX_8438))

#define hw_issues_t830 (\
	BIT_ULL(HW_ISSUE_10883) | \
	BIT_ULL(HW_ISSUE_T76X_3953) | \
	BIT_ULL(HW_ISSUE_TMIX_8438))

#define hw_issues_t860 (\
	BIT_ULL(HW_ISSUE_10883) | \
	BIT_ULL(HW_ISSUE_T76X_3953) | \
	BIT_ULL(HW_ISSUE_TMIX_8438))

#define hw_issues_t880 (\
	BIT_ULL(HW_ISSUE_10883) | \
	BIT_ULL(HW_ISSUE_T76X_3953) | \
	BIT_ULL(HW_ISSUE_TMIX_8438))

#define hw_issues_g31 0

#define hw_issues_g31_r1p0 (\
	BIT_ULL(HW_ISSUE_TGOX_R1_1234))

#define hw_issues_g51 0

#define hw_issues_g52 0

#define hw_issues_g71 (\
	BIT_ULL(HW_ISSUE_TMIX_8463) | \
	BIT_ULL(HW_ISSUE_TMIX_8438))

#define hw_issues_g71_r0p0_05dev0 (\
	BIT_ULL(HW_ISSUE_T76X_3953))

#define hw_issues_g72 0

#define hw_issues_g76 0

static inline bool panfrost_has_hw_issue(struct panfrost_device *pfdev,
					 enum panfrost_hw_issue issue)
{
	return test_bit(issue, pfdev->features.hw_issues);
}

#endif /* __PANFROST_ISSUES_H__ */
