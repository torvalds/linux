/*
 * drivers/net/ethernet/mellanox/mlxfw/mlxfw_mfa2.h
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
