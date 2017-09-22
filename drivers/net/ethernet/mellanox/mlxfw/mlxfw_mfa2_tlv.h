/*
 * drivers/net/ethernet/mellanox/mlxfw/mlxfw_mfa2_tlv.h
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Yotam Gigi <yotamg@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXFW_MFA2_TLV_H
#define _MLXFW_MFA2_TLV_H

#include <linux/kernel.h>
#include "mlxfw_mfa2_file.h"

struct mlxfw_mfa2_tlv {
	u8 version;
	u8 type;
	__be16 len;
	u8 data[0];
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
