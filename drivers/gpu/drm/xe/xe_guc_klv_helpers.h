/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GUC_KLV_HELPERS_H_
#define _XE_GUC_KLV_HELPERS_H_

#include <linux/types.h>

struct drm_printer;

const char *xe_guc_klv_key_to_string(u16 key);

void xe_guc_klv_print(const u32 *klvs, u32 num_dwords, struct drm_printer *p);
int xe_guc_klv_count(const u32 *klvs, u32 num_dwords);

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
 * PREP_GUC_KLV_TAG - Prepare KLV header value based on unique KLV definition tag.
 * @TAG: unique tag of the KLV definition
 *
 * Combine separate KEY and LEN definitions of the KLV identified by the TAG.
 *
 * Return: value of the KLV header (u32).
 */
#define PREP_GUC_KLV_TAG(TAG) \
	PREP_GUC_KLV_CONST(GUC_KLV_##TAG##_KEY, GUC_KLV_##TAG##_LEN)

#endif
