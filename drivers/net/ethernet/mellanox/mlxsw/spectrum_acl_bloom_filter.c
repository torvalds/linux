// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/refcount.h>

#include "spectrum.h"
#include "spectrum_acl_tcam.h"

struct mlxsw_sp_acl_bf {
	unsigned int bank_size;
	refcount_t refcnt[0];
};

struct mlxsw_sp_acl_bf *
mlxsw_sp_acl_bf_init(struct mlxsw_sp *mlxsw_sp, unsigned int num_erp_banks)
{
	struct mlxsw_sp_acl_bf *bf;
	unsigned int bf_bank_size;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_MAX_BF_LOG))
		return ERR_PTR(-EIO);

	/* Bloom filter size per erp_table_bank
	 * is 2^ACL_MAX_BF_LOG
	 */
	bf_bank_size = 1 << MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_BF_LOG);
	bf = kzalloc(sizeof(*bf) + bf_bank_size * num_erp_banks *
		     sizeof(*bf->refcnt), GFP_KERNEL);
	if (!bf)
		return ERR_PTR(-ENOMEM);

	bf->bank_size = bf_bank_size;
	return bf;
}

void mlxsw_sp_acl_bf_fini(struct mlxsw_sp_acl_bf *bf)
{
	kfree(bf);
}
