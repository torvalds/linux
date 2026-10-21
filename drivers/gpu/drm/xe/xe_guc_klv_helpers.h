/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GUC_KLV_HELPERS_H_
#define _XE_GUC_KLV_HELPERS_H_

#include <linux/args.h>
#include <linux/types.h>

struct drm_printer;

const char *xe_guc_klv_key_to_string(u16 key);

void xe_guc_klv_print_one(u16 key, u16 len, const u32 *value, struct drm_printer *p);
void xe_guc_klv_print(const u32 *klvs, u32 num_dwords, struct drm_printer *p);
int xe_guc_klv_count(const u32 *klvs, u32 num_dwords);

u32 *xe_guc_klv_encode_u32(u32 *klvs, u32 avail, u16 key, u32 value);
u32 *xe_guc_klv_encode_u64(u32 *klvs, u32 avail, u16 key, u64 value);
u32 *xe_guc_klv_encode_string(u32 *klvs, u32 avail, u16 key, const char *s);
u32 *xe_guc_klv_encode_object(u32 *klvs, u32 avail, u16 key, const void *obj,
			      u32 *(*encoder)(u32 *klvs, u32 avail, const void *obj));

int xe_guc_klv_parser(const u32 *klvs, u32 num_dwords, void *obj,
		      int (*decoder)(void *obj, u16 key, u16 len, const u32 *value));

/**
 * PREP_GUC_KLV - Prepare KLV header value based on provided key and len.
 * @key: KLV key
 * @len: KLV length
 *
 * Return: value of the KLV header (u32).
 */
#define PREP_GUC_KLV(key, len) \
	(FIELD_PREP(GUC_KLV_0_KEY, (key)) | \
	 FIELD_PREP(GUC_KLV_0_LEN, (len)))

/**
 * PREP_GUC_KLV_CONST - Prepare KLV header value based on const key and len.
 * @key: const KLV key
 * @len: const KLV length
 *
 * Return: value of the KLV header (u32).
 */
#define PREP_GUC_KLV_CONST(key, len) \
	(FIELD_PREP_CONST(GUC_KLV_0_KEY, (key)) | \
	 FIELD_PREP_CONST(GUC_KLV_0_LEN, (len)))

/**
 * MAKE_GUC_KLV_KEY - Prepare KLV KEY name based on unique KLV definition tag.
 * @TAG: unique tag of the KLV definition
 */
#define MAKE_GUC_KLV_KEY(TAG) CONCATENATE(CONCATENATE(GUC_KLV_, TAG), _KEY)

/**
 * MAKE_GUC_KLV_LEN - Prepare KLV LEN name based on unique KLV definition tag.
 * @TAG: unique tag of the KLV definition
 */
#define MAKE_GUC_KLV_LEN(TAG) CONCATENATE(CONCATENATE(GUC_KLV_, TAG), _LEN)

/**
 * PREP_GUC_KLV_TAG - Prepare KLV header value based on unique KLV definition tag.
 * @TAG: unique tag of the KLV definition
 *
 * Combine separate KEY and LEN definitions of the KLV identified by the TAG.
 *
 * Return: value of the KLV header (u32).
 */
#define PREP_GUC_KLV_TAG(TAG) \
	PREP_GUC_KLV_CONST(MAKE_GUC_KLV_KEY(TAG), MAKE_GUC_KLV_LEN(TAG))

#endif
