/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GUC_KLV_THRESHOLDS_SET_H_
#define _XE_GUC_KLV_THRESHOLDS_SET_H_

#include "abi/guc_klvs_abi.h"
#include "xe_guc_klv_helpers.h"
#include "xe_guc_klv_thresholds_set_types.h"

/**
 * MAKE_GUC_KLV_VF_CFG_THRESHOLD_KEY - Prepare the name of the KLV key constant.
 * @TAG: unique tag of the GuC threshold KLV key.
 */
#define MAKE_GUC_KLV_VF_CFG_THRESHOLD_KEY(TAG) \
	MAKE_GUC_KLV_KEY(CONCATENATE(VF_CFG_THRESHOLD_, TAG))

/**
 * MAKE_GUC_KLV_VF_CFG_THRESHOLD_LEN - Prepare the name of the KLV length constant.
 * @TAG: unique tag of the GuC threshold KLV key.
 */
#define MAKE_GUC_KLV_VF_CFG_THRESHOLD_LEN(TAG) \
	MAKE_GUC_KLV_LEN(CONCATENATE(VF_CFG_THRESHOLD_, TAG))

/**
 * xe_guc_klv_threshold_key_to_index - Find index of the tracked GuC threshold.
 * @key: GuC threshold KLV key.
 *
 * This translation is automatically generated using &MAKE_XE_GUC_KLV_THRESHOLDS_SET.
 * Return: index of the GuC threshold KLV or -1 if not found.
 */
static inline int xe_guc_klv_threshold_key_to_index(u32 key)
{
	switch (key) {
#define define_xe_guc_klv_threshold_key_to_index_case(TAG, ...)		\
									\
	case MAKE_GUC_KLV_VF_CFG_THRESHOLD_KEY(TAG):			\
		return MAKE_XE_GUC_KLV_THRESHOLD_INDEX(TAG);

	/* private: auto-generated case statements */
	MAKE_XE_GUC_KLV_THRESHOLDS_SET(define_xe_guc_klv_threshold_key_to_index_case)
	}
	return -1;
#undef define_xe_guc_klv_threshold_key_to_index_case
}

/**
 * xe_guc_klv_threshold_index_to_key - Get tracked GuC threshold KLV key.
 * @index: GuC threshold KLV index.
 *
 * This translation is automatically generated using &MAKE_XE_GUC_KLV_THRESHOLDS_SET.
 * Return: key of the GuC threshold KLV or 0 on malformed index.
 */
static inline u32 xe_guc_klv_threshold_index_to_key(enum xe_guc_klv_threshold_index index)
{
	switch (index) {
#define define_xe_guc_klv_threshold_index_to_key_case(TAG, ...)		\
									\
	case MAKE_XE_GUC_KLV_THRESHOLD_INDEX(TAG):			\
		return MAKE_GUC_KLV_VF_CFG_THRESHOLD_KEY(TAG);

	/* private: auto-generated case statements */
	MAKE_XE_GUC_KLV_THRESHOLDS_SET(define_xe_guc_klv_threshold_index_to_key_case)
	}
	return 0; /* unreachable */
#undef define_xe_guc_klv_threshold_index_to_key_case
}

#endif
