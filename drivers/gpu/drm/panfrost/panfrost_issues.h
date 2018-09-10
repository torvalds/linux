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
	HW_ISSUE_6367,
	HW_ISSUE_6787,
	HW_ISSUE_8186,
	HW_ISSUE_8245,
	HW_ISSUE_8316,
	HW_ISSUE_8394,
	HW_ISSUE_8401,
	HW_ISSUE_8408,
	HW_ISSUE_8443,
	HW_ISSUE_8987,
	HW_ISSUE_9435,
	HW_ISSUE_9510,
	HW_ISSUE_9630,
	HW_ISSUE_10327,
	HW_ISSUE_10649,
	HW_ISSUE_10676,
	HW_ISSUE_10797,
	HW_ISSUE_10817,
	HW_ISSUE_10883,
	HW_ISSUE_10959,
	HW_ISSUE_10969,
	HW_ISSUE_11020,
	HW_ISSUE_11024,
	HW_ISSUE_11035,
	HW_ISSUE_11056,
	HW_ISSUE_T76X_3542,
	HW_ISSUE_T76X_3953,
	HW_ISSUE_TMIX_8463,
	GPUCORE_1619,
	HW_ISSUE_TMIX_8438,
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
