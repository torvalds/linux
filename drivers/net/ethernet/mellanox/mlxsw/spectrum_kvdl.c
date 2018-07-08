/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_kvdl.c
 * Copyright (c) 2016-2018 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016-2018 Jiri Pirko <jiri@mellanox.com>
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
#include <linux/slab.h>

#include "spectrum.h"

struct mlxsw_sp_kvdl {
	const struct mlxsw_sp_kvdl_ops *kvdl_ops;
	unsigned long priv[0];
	/* priv has to be always the last item */
};

int mlxsw_sp_kvdl_init(struct mlxsw_sp *mlxsw_sp)
{
	const struct mlxsw_sp_kvdl_ops *kvdl_ops = mlxsw_sp->kvdl_ops;
	struct mlxsw_sp_kvdl *kvdl;
	int err;

	kvdl = kzalloc(sizeof(*mlxsw_sp->kvdl) + kvdl_ops->priv_size,
		       GFP_KERNEL);
	if (!kvdl)
		return -ENOMEM;
	kvdl->kvdl_ops = kvdl_ops;
	mlxsw_sp->kvdl = kvdl;

	err = kvdl_ops->init(mlxsw_sp, kvdl->priv);
	if (err)
		goto err_init;
	return 0;

err_init:
	kfree(kvdl);
	return err;
}

void mlxsw_sp_kvdl_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_kvdl *kvdl = mlxsw_sp->kvdl;

	kvdl->kvdl_ops->fini(mlxsw_sp, kvdl->priv);
	kfree(kvdl);
}

int mlxsw_sp_kvdl_alloc(struct mlxsw_sp *mlxsw_sp,
			enum mlxsw_sp_kvdl_entry_type type,
			unsigned int entry_count, u32 *p_entry_index)
{
	struct mlxsw_sp_kvdl *kvdl = mlxsw_sp->kvdl;

	return kvdl->kvdl_ops->alloc(mlxsw_sp, kvdl->priv, type,
				     entry_count, p_entry_index);
}

void mlxsw_sp_kvdl_free(struct mlxsw_sp *mlxsw_sp,
			enum mlxsw_sp_kvdl_entry_type type,
			unsigned int entry_count, int entry_index)
{
	struct mlxsw_sp_kvdl *kvdl = mlxsw_sp->kvdl;

	kvdl->kvdl_ops->free(mlxsw_sp, kvdl->priv, type,
			     entry_count, entry_index);
}

int mlxsw_sp_kvdl_alloc_count_query(struct mlxsw_sp *mlxsw_sp,
				    enum mlxsw_sp_kvdl_entry_type type,
				    unsigned int entry_count,
				    unsigned int *p_alloc_count)
{
	struct mlxsw_sp_kvdl *kvdl = mlxsw_sp->kvdl;

	return kvdl->kvdl_ops->alloc_size_query(mlxsw_sp, kvdl->priv, type,
						entry_count, p_alloc_count);
}
