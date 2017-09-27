/*
 * drivers/net/ethernet/mellanox/mlxfw/mlxfw_mfa2_tlv_multi.c
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

#define pr_fmt(fmt) "MFA2: " fmt

#include "mlxfw_mfa2_tlv_multi.h"
#include <uapi/linux/netlink.h>

#define MLXFW_MFA2_TLV_TOTAL_SIZE(tlv) \
	NLA_ALIGN(sizeof(*(tlv)) + be16_to_cpu((tlv)->len))

const struct mlxfw_mfa2_tlv *
mlxfw_mfa2_tlv_multi_child(const struct mlxfw_mfa2_file *mfa2_file,
			   const struct mlxfw_mfa2_tlv_multi *multi)
{
	size_t multi_len;

	multi_len = NLA_ALIGN(sizeof(struct mlxfw_mfa2_tlv_multi));
	return mlxfw_mfa2_tlv_get(mfa2_file, (void *) multi + multi_len);
}

const struct mlxfw_mfa2_tlv *
mlxfw_mfa2_tlv_next(const struct mlxfw_mfa2_file *mfa2_file,
		    const struct mlxfw_mfa2_tlv *tlv)
{
	const struct mlxfw_mfa2_tlv_multi *multi;
	u16 tlv_len;
	void *next;

	tlv_len = MLXFW_MFA2_TLV_TOTAL_SIZE(tlv);

	if (tlv->type == MLXFW_MFA2_TLV_MULTI_PART) {
		multi = mlxfw_mfa2_tlv_multi_get(mfa2_file, tlv);
		tlv_len = NLA_ALIGN(tlv_len + be16_to_cpu(multi->total_len));
	}

	next = (void *) tlv + tlv_len;
	return mlxfw_mfa2_tlv_get(mfa2_file, next);
}

const struct mlxfw_mfa2_tlv *
mlxfw_mfa2_tlv_advance(const struct mlxfw_mfa2_file *mfa2_file,
		       const struct mlxfw_mfa2_tlv *from_tlv, u16 count)
{
	const struct mlxfw_mfa2_tlv *tlv;
	u16 idx;

	mlxfw_mfa2_tlv_foreach(mfa2_file, tlv, idx, from_tlv, count)
		if (!tlv)
			return NULL;
	return tlv;
}

const struct mlxfw_mfa2_tlv *
mlxfw_mfa2_tlv_multi_child_find(const struct mlxfw_mfa2_file *mfa2_file,
				const struct mlxfw_mfa2_tlv_multi *multi,
				enum mlxfw_mfa2_tlv_type type, u16 index)
{
	const struct mlxfw_mfa2_tlv *tlv;
	u16 skip = 0;
	u16 idx;

	mlxfw_mfa2_tlv_multi_foreach(mfa2_file, tlv, idx, multi) {
		if (!tlv) {
			pr_err("TLV parsing error\n");
			return NULL;
		}
		if (tlv->type == type)
			if (skip++ == index)
				return tlv;
	}
	return NULL;
}

int mlxfw_mfa2_tlv_multi_child_count(const struct mlxfw_mfa2_file *mfa2_file,
				     const struct mlxfw_mfa2_tlv_multi *multi,
				     enum mlxfw_mfa2_tlv_type type,
				     u16 *p_count)
{
	const struct mlxfw_mfa2_tlv *tlv;
	u16 count = 0;
	u16 idx;

	mlxfw_mfa2_tlv_multi_foreach(mfa2_file, tlv, idx, multi) {
		if (!tlv) {
			pr_err("TLV parsing error\n");
			return -EINVAL;
		}

		if (tlv->type == type)
			count++;
	}
	*p_count = count;
	return 0;
}
