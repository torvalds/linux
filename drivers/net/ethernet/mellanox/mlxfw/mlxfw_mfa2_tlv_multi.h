/*
 * drivers/net/ethernet/mellanox/mlxfw/mlxfw_mfa2_tlv_multi.h
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
#ifndef _MLXFW_MFA2_TLV_MULTI_H
#define _MLXFW_MFA2_TLV_MULTI_H

#include "mlxfw_mfa2_tlv.h"
#include "mlxfw_mfa2_format.h"
#include "mlxfw_mfa2_file.h"

const struct mlxfw_mfa2_tlv *
mlxfw_mfa2_tlv_multi_child(const struct mlxfw_mfa2_file *mfa2_file,
			   const struct mlxfw_mfa2_tlv_multi *multi);

const struct mlxfw_mfa2_tlv *
mlxfw_mfa2_tlv_next(const struct mlxfw_mfa2_file *mfa2_file,
		    const struct mlxfw_mfa2_tlv *tlv);

const struct mlxfw_mfa2_tlv *
mlxfw_mfa2_tlv_advance(const struct mlxfw_mfa2_file *mfa2_file,
		       const struct mlxfw_mfa2_tlv *from_tlv, u16 count);

const struct mlxfw_mfa2_tlv *
mlxfw_mfa2_tlv_multi_child_find(const struct mlxfw_mfa2_file *mfa2_file,
				const struct mlxfw_mfa2_tlv_multi *multi,
				enum mlxfw_mfa2_tlv_type type, u16 index);

int mlxfw_mfa2_tlv_multi_child_count(const struct mlxfw_mfa2_file *mfa2_file,
				     const struct mlxfw_mfa2_tlv_multi *multi,
				     enum mlxfw_mfa2_tlv_type type,
				     u16 *p_count);

#define mlxfw_mfa2_tlv_foreach(mfa2_file, tlv, idx, from_tlv, count) \
	for (idx = 0, tlv = from_tlv; idx < (count); \
	     idx++, tlv = mlxfw_mfa2_tlv_next(mfa2_file, tlv))

#define mlxfw_mfa2_tlv_multi_foreach(mfa2_file, tlv, idx, multi) \
	mlxfw_mfa2_tlv_foreach(mfa2_file, tlv, idx, \
			       mlxfw_mfa2_tlv_multi_child(mfa2_file, multi), \
			       be16_to_cpu(multi->num_extensions) + 1)
#endif
