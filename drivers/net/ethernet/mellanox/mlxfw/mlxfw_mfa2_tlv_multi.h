/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2019 Mellanox Technologies. All rights reserved */

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
