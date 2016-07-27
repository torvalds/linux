/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_kvdl.c
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
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

#include <linux/kernel.h>
#include <linux/bitops.h>

#include "spectrum.h"

#define MLXSW_SP_KVDL_SINGLE_BASE 0
#define MLXSW_SP_KVDL_SINGLE_SIZE 16384
#define MLXSW_SP_KVDL_CHUNKS_BASE \
	(MLXSW_SP_KVDL_SINGLE_BASE + MLXSW_SP_KVDL_SINGLE_SIZE)
#define MLXSW_SP_KVDL_CHUNKS_SIZE \
	(MLXSW_SP_KVD_LINEAR_SIZE - MLXSW_SP_KVDL_CHUNKS_BASE)
#define MLXSW_SP_CHUNK_MAX 32

int mlxsw_sp_kvdl_alloc(struct mlxsw_sp *mlxsw_sp, unsigned int entry_count)
{
	int entry_index;
	int size;
	int type_base;
	int type_size;
	int type_entries;

	if (entry_count == 0 || entry_count > MLXSW_SP_CHUNK_MAX) {
		return -EINVAL;
	} else if (entry_count == 1) {
		type_base = MLXSW_SP_KVDL_SINGLE_BASE;
		type_size = MLXSW_SP_KVDL_SINGLE_SIZE;
		type_entries = 1;
	} else {
		type_base = MLXSW_SP_KVDL_CHUNKS_BASE;
		type_size = MLXSW_SP_KVDL_CHUNKS_SIZE;
		type_entries = MLXSW_SP_CHUNK_MAX;
	}

	entry_index = type_base;
	size = type_base + type_size;
	for_each_clear_bit_from(entry_index, mlxsw_sp->kvdl.usage, size) {
		int i;

		for (i = 0; i < type_entries; i++)
			set_bit(entry_index + i, mlxsw_sp->kvdl.usage);
		return entry_index;
	}
	return -ENOBUFS;
}

void mlxsw_sp_kvdl_free(struct mlxsw_sp *mlxsw_sp, int entry_index)
{
	int type_entries;
	int i;

	if (entry_index < MLXSW_SP_KVDL_CHUNKS_BASE)
		type_entries = 1;
	else
		type_entries = MLXSW_SP_CHUNK_MAX;
	for (i = 0; i < type_entries; i++)
		clear_bit(entry_index + i, mlxsw_sp->kvdl.usage);
}
