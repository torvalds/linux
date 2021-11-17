/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2019 Mellanox Technologies. All rights reserved */

#ifndef _MLXFW_MFA2_TLV_H
#define _MLXFW_MFA2_TLV_H

#include <linux/kernel.h>
#include "mlxfw_mfa2_file.h"

struct mlxfw_mfa2_tlv {
	u8 version;
	u8 type;
	__be16 len;
	u8 data[];
} __packed;

static inline const struct mlxfw_mfa2_tlv *
mlxfw_mfa2_tlv_get(const struct mlxfw_mfa2_file *mfa2_file, const void *ptr)
{
	if (!mlxfw_mfa2_valid_ptr(mfa2_file, ptr) ||
	    !mlxfw_mfa2_valid_ptr(mfa2_file, ptr + sizeof(struct mlxfw_mfa2_tlv)))
		return NULL;
	return ptr;
}

static inline const void *
mlxfw_mfa2_tlv_payload_get(const struct mlxfw_mfa2_file *mfa2_file,
			   const struct mlxfw_mfa2_tlv *tlv, u8 payload_type,
			   size_t payload_size, bool varsize)
{
	void *tlv_top;

	tlv_top = (void *) tlv + be16_to_cpu(tlv->len) - 1;
	if (!mlxfw_mfa2_valid_ptr(mfa2_file, tlv) ||
	    !mlxfw_mfa2_valid_ptr(mfa2_file, tlv_top))
		return NULL;
	if (tlv->type != payload_type)
		return NULL;
	if (varsize && (be16_to_cpu(tlv->len) < payload_size))
		return NULL;
	if (!varsize && (be16_to_cpu(tlv->len) != payload_size))
		return NULL;

	return tlv->data;
}

#define MLXFW_MFA2_TLV(name, payload_type, tlv_type)			       \
static inline const payload_type *					       \
mlxfw_mfa2_tlv_ ## name ## _get(const struct mlxfw_mfa2_file *mfa2_file,       \
				const struct mlxfw_mfa2_tlv *tlv)	       \
{									       \
	return mlxfw_mfa2_tlv_payload_get(mfa2_file, tlv,		       \
					  tlv_type, sizeof(payload_type),      \
					  false);			       \
}

#define MLXFW_MFA2_TLV_VARSIZE(name, payload_type, tlv_type)		       \
static inline const payload_type *					       \
mlxfw_mfa2_tlv_ ## name ## _get(const struct mlxfw_mfa2_file *mfa2_file,       \
				const struct mlxfw_mfa2_tlv *tlv)	       \
{									       \
	return mlxfw_mfa2_tlv_payload_get(mfa2_file, tlv,		       \
					  tlv_type, sizeof(payload_type),      \
					  true);			       \
}

#endif
