/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2019 Mellanox Technologies. All rights reserved */

#ifndef _MLXFW_MFA2_H
#define _MLXFW_MFA2_H

#include <linux/firmware.h>
#include "mlxfw.h"

struct mlxfw_mfa2_component {
	u16 index;
	u32 data_size;
	u8 *data;
};

struct mlxfw_mfa2_file;

bool mlxfw_mfa2_check(const struct firmware *fw);

struct mlxfw_mfa2_file *mlxfw_mfa2_file_init(const struct firmware *fw);

int mlxfw_mfa2_file_component_count(const struct mlxfw_mfa2_file *mfa2_file,
				    const char *psid, u32 psid_size,
				    u32 *p_count);

struct mlxfw_mfa2_component *
mlxfw_mfa2_file_component_get(const struct mlxfw_mfa2_file *mfa2_file,
			      const char *psid, int psid_size,
			      int component_index);

void mlxfw_mfa2_file_component_put(struct mlxfw_mfa2_component *component);

void mlxfw_mfa2_file_fini(struct mlxfw_mfa2_file *mfa2_file);

#endif
