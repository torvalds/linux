/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2019 Mellanox Technologies. All rights reserved */

#ifndef _MLXFW_MFA2_FILE_H
#define _MLXFW_MFA2_FILE_H

#include <linux/firmware.h>
#include <linux/kernel.h>

struct mlxfw_mfa2_file {
	const struct firmware *fw;
	const struct mlxfw_mfa2_tlv *first_dev;
	u16 dev_count;
	const struct mlxfw_mfa2_tlv *first_component;
	u16 component_count;
	const void *cb; /* components block */
	u32 cb_archive_size; /* size of compressed components block */
};

static inline bool mlxfw_mfa2_valid_ptr(const struct mlxfw_mfa2_file *mfa2_file,
					const void *ptr)
{
	const void *valid_to = mfa2_file->fw->data + mfa2_file->fw->size;
	const void *valid_from = mfa2_file->fw->data;

	return ptr > valid_from && ptr < valid_to;
}

#endif
