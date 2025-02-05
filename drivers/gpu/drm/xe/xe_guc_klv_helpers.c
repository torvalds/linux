// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/bitfield.h>
#include <drm/drm_print.h>

#include "abi/guc_klvs_abi.h"
#include "xe_guc_klv_helpers.h"
#include "xe_guc_klv_thresholds_set.h"

#define make_u64(hi, lo) ((u64)((u64)(u32)(hi) << 32 | (u32)(lo)))

/**
 * xe_guc_klv_key_to_string - Convert KLV key into friendly name.
 * @key: the `GuC KLV`_ key
 *
 * Return: name of the KLV key.
 */
const char *xe_guc_klv_key_to_string(u16 key)
{
	switch (key) {
	/* VGT POLICY keys */
	case GUC_KLV_VGT_POLICY_SCHED_IF_IDLE_KEY:
		return "sched_if_idle";
	case GUC_KLV_VGT_POLICY_ADVERSE_SAMPLE_PERIOD_KEY:
		return "sample_period";
	case GUC_KLV_VGT_POLICY_RESET_AFTER_VF_SWITCH_KEY:
		return "reset_engine";
	/* VF CFG keys */
	case GUC_KLV_VF_CFG_GGTT_START_KEY:
		return "ggtt_start";
	case GUC_KLV_VF_CFG_GGTT_SIZE_KEY:
		return "ggtt_size";
	case GUC_KLV_VF_CFG_LMEM_SIZE_KEY:
		return "lmem_size";
	case GUC_KLV_VF_CFG_NUM_CONTEXTS_KEY:
		return "num_contexts";
	case GUC_KLV_VF_CFG_TILE_MASK_KEY:
		return "tile_mask";
	case GUC_KLV_VF_CFG_NUM_DOORBELLS_KEY:
		return "num_doorbells";
	case GUC_KLV_VF_CFG_EXEC_QUANTUM_KEY:
		return "exec_quantum";
	case GUC_KLV_VF_CFG_PREEMPT_TIMEOUT_KEY:
		return "preempt_timeout";
	case GUC_KLV_VF_CFG_BEGIN_DOORBELL_ID_KEY:
		return "begin_db_id";
	case GUC_KLV_VF_CFG_BEGIN_CONTEXT_ID_KEY:
		return "begin_ctx_id";
	case GUC_KLV_VF_CFG_SCHED_PRIORITY_KEY:
		return "sched_priority";

	/* VF CFG threshold keys */
#define define_threshold_key_to_string_case(TAG, NAME, ...)	\
								\
	case MAKE_GUC_KLV_VF_CFG_THRESHOLD_KEY(TAG):		\
		return #NAME;

	/* private: auto-generated case statements */
	MAKE_XE_GUC_KLV_THRESHOLDS_SET(define_threshold_key_to_string_case)
#undef define_threshold_key_to_string_case

	default:
		return "(unknown)";
	}
}

/**
 * xe_guc_klv_print - Print content of the buffer with `GuC KLV`_.
 * @klvs: the buffer with KLVs
 * @num_dwords: number of dwords (u32) available in the buffer
 * @p: the &drm_printer
 *
 * The buffer may contain more than one KLV.
 */
void xe_guc_klv_print(const u32 *klvs, u32 num_dwords, struct drm_printer *p)
{
	while (num_dwords >= GUC_KLV_LEN_MIN) {
		u32 key = FIELD_GET(GUC_KLV_0_KEY, klvs[0]);
		u32 len = FIELD_GET(GUC_KLV_0_LEN, klvs[0]);

		klvs += GUC_KLV_LEN_MIN;
		num_dwords -= GUC_KLV_LEN_MIN;

		if (num_dwords < len) {
			drm_printf(p, "{ key %#06x : truncated %zu of %zu bytes %*ph } # %s\n",
				   key, num_dwords * sizeof(u32), len * sizeof(u32),
				   (int)(num_dwords * sizeof(u32)), klvs,
				   xe_guc_klv_key_to_string(key));
			return;
		}

		switch (len) {
		case 0:
			drm_printf(p, "{ key %#06x : no value } # %s\n",
				   key, xe_guc_klv_key_to_string(key));
			break;
		case 1:
			drm_printf(p, "{ key %#06x : 32b value %u } # %s\n",
				   key, klvs[0], xe_guc_klv_key_to_string(key));
			break;
		case 2:
			drm_printf(p, "{ key %#06x : 64b value %#llx } # %s\n",
				   key, make_u64(klvs[1], klvs[0]),
				   xe_guc_klv_key_to_string(key));
			break;
		default:
			drm_printf(p, "{ key %#06x : %zu bytes %*ph } # %s\n",
				   key, len * sizeof(u32), (int)(len * sizeof(u32)),
				   klvs, xe_guc_klv_key_to_string(key));
			break;
		}

		klvs += len;
		num_dwords -= len;
	}

	/* we don't expect any leftovers, fix if KLV header is ever changed */
	BUILD_BUG_ON(GUC_KLV_LEN_MIN > 1);
}

/**
 * xe_guc_klv_count - Count KLVs present in the buffer.
 * @klvs: the buffer with KLVs
 * @num_dwords: number of dwords (u32) in the buffer
 *
 * Return: number of recognized KLVs or
 *         a negative error code if KLV buffer is truncated.
 */
int xe_guc_klv_count(const u32 *klvs, u32 num_dwords)
{
	int num_klvs = 0;

	while (num_dwords >= GUC_KLV_LEN_MIN) {
		u32 len = FIELD_GET(GUC_KLV_0_LEN, klvs[0]);

		if (num_dwords < len + GUC_KLV_LEN_MIN)
			break;

		klvs += GUC_KLV_LEN_MIN + len;
		num_dwords -= GUC_KLV_LEN_MIN + len;
		num_klvs++;
	}

	return num_dwords ? -ENODATA : num_klvs;
}
