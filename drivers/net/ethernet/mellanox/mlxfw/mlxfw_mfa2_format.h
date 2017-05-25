/*
 * drivers/net/ethernet/mellanox/mlxfw/mlxfw_mfa2_format.h
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
#ifndef _MLXFW_MFA2_FORMAT_H
#define _MLXFW_MFA2_FORMAT_H

#include "mlxfw_mfa2_file.h"
#include "mlxfw_mfa2_tlv.h"

enum mlxfw_mfa2_tlv_type {
	MLXFW_MFA2_TLV_MULTI_PART = 0x01,
	MLXFW_MFA2_TLV_PACKAGE_DESCRIPTOR = 0x02,
	MLXFW_MFA2_TLV_COMPONENT_DESCRIPTOR = 0x04,
	MLXFW_MFA2_TLV_COMPONENT_PTR = 0x22,
	MLXFW_MFA2_TLV_PSID = 0x2A,
};

enum mlxfw_mfa2_compression_type {
	MLXFW_MFA2_COMPRESSION_TYPE_NONE,
	MLXFW_MFA2_COMPRESSION_TYPE_XZ,
};

struct mlxfw_mfa2_tlv_package_descriptor {
	__be16 num_components;
	__be16 num_devices;
	__be32 cb_offset;
	__be32 cb_archive_size;
	__be32 cb_size_h;
	__be32 cb_size_l;
	u8 padding[3];
	u8 cv_compression;
	__be32 user_data_offset;
} __packed;

MLXFW_MFA2_TLV(package_descriptor, struct mlxfw_mfa2_tlv_package_descriptor,
	       MLXFW_MFA2_TLV_PACKAGE_DESCRIPTOR);

struct mlxfw_mfa2_tlv_multi {
	__be16 num_extensions;
	__be16 total_len;
} __packed;

MLXFW_MFA2_TLV(multi, struct mlxfw_mfa2_tlv_multi,
	       MLXFW_MFA2_TLV_MULTI_PART);

struct mlxfw_mfa2_tlv_psid {
	u8 psid[0];
} __packed;

MLXFW_MFA2_TLV_VARSIZE(psid, struct mlxfw_mfa2_tlv_psid,
		       MLXFW_MFA2_TLV_PSID);

struct mlxfw_mfa2_tlv_component_ptr {
	__be16 storage_id;
	__be16 component_index;
	__be32 storage_address;
} __packed;

MLXFW_MFA2_TLV(component_ptr, struct mlxfw_mfa2_tlv_component_ptr,
	       MLXFW_MFA2_TLV_COMPONENT_PTR);

struct mlxfw_mfa2_tlv_component_descriptor {
	__be16 pldm_classification;
	__be16 identifier;
	__be32 cb_offset_h;
	__be32 cb_offset_l;
	__be32 size;
} __packed;

MLXFW_MFA2_TLV(component_descriptor, struct mlxfw_mfa2_tlv_component_descriptor,
	       MLXFW_MFA2_TLV_COMPONENT_DESCRIPTOR);

#endif
