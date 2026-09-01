// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/bitfield.h>
#include <kunit/static_stub.h>
#include <drm/drm_print.h>

#include "abi/guc_klvs_abi.h"
#include "abi/xe_driver_klvs_abi.h"
#include "xe_guc_klv_helpers.h"
#include "xe_guc_klv_thresholds_set.h"

#define make_u64(hi, lo) ((u64)((u64)(u32)(hi) << 32 | (u32)(lo)))

static bool is_group_key(u16 key)
{
	KUNIT_STATIC_STUB_REDIRECT(is_group_key, key);
	return false;
}

static bool is_reserved_key(u16 key)
{
	return in_range(key, GUC_KLV_RESERVED_RANGE_START, GUC_KLV_RESERVED_RANGE_LEN);
}

/**
 * xe_guc_klv_key_to_string - Convert KLV key into friendly name.
 * @key: the `GuC KLV`_ key
 *
 * Return: name of the KLV key.
 */
const char *xe_guc_klv_key_to_string(u16 key)
{
	switch (key) {
	/* GuC Global Config KLVs */
	case GUC_KLV_GLOBAL_CFG_GROUP_SCHEDULING_AVAILABLE_KEY:
		return "group_scheduling_available";
	case GUC_KLV_GLOBAL_CFG_NUM_PAGING_ENGINE_INSTANCES_KEY:
		return "num_paging_engine_instances";
	/* VGT POLICY keys */
	case GUC_KLV_VGT_POLICY_SCHED_IF_IDLE_KEY:
		return "sched_if_idle";
	case GUC_KLV_VGT_POLICY_ADVERSE_SAMPLE_PERIOD_KEY:
		return "sample_period";
	case GUC_KLV_VGT_POLICY_ENGINE_GROUP_CONFIG_KEY:
		return "engine_group_config";
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
	case GUC_KLV_VF_CFG_ENGINE_GROUP_EXEC_QUANTUM_KEY:
		return "sched_groups_exec_quantum";
	case GUC_KLV_VF_CFG_ENGINE_GROUP_PREEMPT_TIMEOUT_KEY:
		return "sched_groups_preempt_timeout";

	/* VF CFG threshold keys */
#define define_threshold_key_to_string_case(TAG, NAME, ...)	\
								\
	case MAKE_GUC_KLV_VF_CFG_THRESHOLD_KEY(TAG):		\
		return #NAME;

	/* private: auto-generated case statements */
	MAKE_XE_GUC_KLV_THRESHOLDS_SET(define_threshold_key_to_string_case)
#undef define_threshold_key_to_string_case

	/* driver KLVs */
	case MIGRATION_KLV_DEVICE_DEVID_KEY:
		return "migration_devid";
	case MIGRATION_KLV_DEVICE_REVID_KEY:
		return "migration_revid";

	default:
		if (is_reserved_key(key))
			return "(reserved)";
		return "(unknown)";
	}
}

/**
 * xe_guc_klv_print_one() - Print single `GuC KLV`_.
 * @key: KLV key
 * @len: KLV length (in u32 dwords) of the KLV @value
 * @value: KLV value (as array of @len u32 dwords)
 * @p: the &drm_printer
 *
 * The buffer may contain more than one KLV.
 */
void xe_guc_klv_print_one(u16 key, u16 len, const u32 *value, struct drm_printer *p)
{
	const char *name = xe_guc_klv_key_to_string(key);

	if (is_group_key(key)) {
		struct drm_printer gp = drm_line_printer(p, name, 0);

		drm_printf(p, "{ key %#06x : group %u dwords } # %s\n",
			   key, len, name);

		/* print group recursively */
		xe_guc_klv_print(value, len, &gp);
		return;
	}

	switch (len) {
	case 0:
		drm_printf(p, "{ key %#06x : no value } # %s\n", key, name);
		break;
	case 1:
		drm_printf(p, "{ key %#06x : 32b value %u } # %s\n",
			   key, value[0], name);
		break;
	case 2:
		drm_printf(p, "{ key %#06x : 64b value %#llx } # %s\n",
			   key, make_u64(value[1], value[0]), name);
		break;
	default:
		drm_printf(p, "{ key %#06x : %zu bytes %*ph } # %s\n",
			   key, len * sizeof(u32), (int)(len * sizeof(u32)),
			   value, name);
		break;
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

		xe_guc_klv_print_one(key, len, klvs, p);

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

static size_t to_num_bytes(u16 dwords)
{
	return dwords * sizeof(u32);
}

static u16 to_num_dwords(size_t size)
{
	return round_up(size, sizeof(u32)) / sizeof(u32);
}

/**
 * xe_guc_klv_encode_u32() - Encode 32-bit value as KLV.
 * @klvs: the buffer where to place KLV
 * @avail: number of dwords (u32) available in the buffer
 * @key: key to be used
 * @value: value to be encoded
 *
 * Return: pointer to the buffer location past the encoded KLV or
 *         an ERR_PTR if there was no space to encode the KLV.
 */
u32 *xe_guc_klv_encode_u32(u32 *klvs, u32 avail, u16 key, u32 value)
{
	u16 len = to_num_dwords(sizeof(u32));

	if (IS_ERR(klvs))
		return klvs;

	if (avail < GUC_KLV_LEN_MIN + len)
		return ERR_PTR(-ENOSPC);

	*klvs++ = PREP_GUC_KLV(key, len);
	*klvs++ = value;
	return klvs;
}

/**
 * xe_guc_klv_encode_u64() - Encode 64-bit value as KLV.
 * @klvs: the buffer where to place KLV
 * @avail: number of dwords (u32) available in the buffer
 * @key: key to be used
 * @value: value to be encoded
 *
 * Return: pointer to the buffer location past the encoded KLV or
 *         an ERR_PTR if there was no space to encode the KLV.
 */
u32 *xe_guc_klv_encode_u64(u32 *klvs, u32 avail, u16 key, u64 value)
{
	u16 len = to_num_dwords(sizeof(u64));

	if (IS_ERR(klvs))
		return klvs;

	if (avail < GUC_KLV_LEN_MIN + len)
		return ERR_PTR(-ENOSPC);

	*klvs++ = PREP_GUC_KLV(key, len);
	*klvs++ = lower_32_bits(value);
	*klvs++ = upper_32_bits(value);
	return klvs;
}

/**
 * xe_guc_klv_encode_string() - Encode string as KLV.
 * @klvs: the buffer where to place KLV
 * @avail: number of dwords (u32) available in the buffer
 * @key: key to be used
 * @s: string to be encoded
 *
 * Return: pointer to the buffer location past the encoded KLV or
 *         an ERR_PTR if there was no space to encode the KLV.
 */
u32 *xe_guc_klv_encode_string(u32 *klvs, u32 avail, u16 key, const char *s)
{
	size_t longest = to_num_bytes(FIELD_MAX(GUC_KLV_0_LEN));
	size_t size = strnlen(s, longest) + 1; /* \0 */
	u16 len = to_num_dwords(size);

	if (IS_ERR(klvs))
		return klvs;

	if (size > longest)
		return ERR_PTR(-E2BIG);

	if (avail < GUC_KLV_LEN_MIN + len)
		return ERR_PTR(-ENOSPC);

	*klvs++ = PREP_GUC_KLV(key, len);
	strscpy_pad((void *)klvs, s, to_num_bytes(len));
	return klvs + len;
}

/**
 * xe_guc_klv_encode_object() - Encode object using custom encoder as single KLV.
 * @klvs: the buffer where to place KLV
 * @avail: number of dwords (u32) available in the buffer
 * @key: key to be used
 * @obj: opaque object pointer
 * @encoder: function pointer to the custom encoder
 *
 * Return: pointer to the buffer location past the encoded KLV or
 *         an ERR_PTR if there was no space to encode the KLV.
 */
u32 *xe_guc_klv_encode_object(u32 *klvs, u32 avail, u16 key, const void *obj,
			      u32 *(*encoder)(u32 *klvs, u32 avail, const void *obj))
{
	u32 *end;

	if (IS_ERR(klvs))
		return klvs;

	if (avail < GUC_KLV_LEN_MIN)
		return ERR_PTR(-ENOSPC);

	if (avail > GUC_KLV_LEN_MIN + FIELD_MAX(GUC_KLV_0_LEN))
		avail = GUC_KLV_LEN_MIN + FIELD_MAX(GUC_KLV_0_LEN);

	end = encoder(klvs + GUC_KLV_LEN_MIN, avail - GUC_KLV_LEN_MIN, obj);
	if (IS_ERR(end))
		return end;

	if (WARN_ON(end < klvs + GUC_KLV_LEN_MIN))
		return ERR_PTR(-EPIPE);

	if (WARN_ON(end > klvs + avail))
		return ERR_PTR(-EFBIG);

	*klvs = PREP_GUC_KLV(key, end - (klvs + GUC_KLV_LEN_MIN));
	return end;
}

/**
 * xe_guc_klv_parser() - Parse and decode stream of KLVs.
 * @klvs: the buffer with KLVs
 * @num_dwords: number of dwords (u32) available in the buffer
 * @obj: opaque pointer to be used by the @decoder function
 * @decoder: pointer to the decoder function
 *
 * Return: The sum of all results returned by the decoder or
 *         an -errno on decoder or buffer failure.
 */
int xe_guc_klv_parser(const u32 *klvs, u32 num_dwords, void *obj,
		      int (*decoder)(void *obj, u16 key, u16 len, const u32 *value))
{
	int total = 0;
	int ret;

	while (num_dwords >= GUC_KLV_LEN_MIN) {
		u16 key = FIELD_GET(GUC_KLV_0_KEY, klvs[0]);
		u16 len = FIELD_GET(GUC_KLV_0_LEN, klvs[0]);

		klvs += GUC_KLV_LEN_MIN;
		num_dwords -= GUC_KLV_LEN_MIN;

		if (num_dwords < len)
			return -ENODATA;

		ret = decoder(obj, key, len, klvs);
		if (ret < 0)
			return ret;
		total += ret;

		klvs += len;
		num_dwords -= len;
	}

	return total;
}

#if IS_BUILTIN(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_guc_klv_helpers_kunit.c"
#endif
