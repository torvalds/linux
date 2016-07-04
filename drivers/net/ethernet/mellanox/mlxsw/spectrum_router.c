/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_router.c
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2016 Ido Schimmel <idosch@mellanox.com>
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
#include <linux/types.h>
#include <linux/rhashtable.h>
#include <linux/bitops.h>
#include <linux/in6.h>

#include "spectrum.h"
#include "core.h"
#include "reg.h"

static void
mlxsw_sp_prefix_usage_set(struct mlxsw_sp_prefix_usage *prefix_usage,
			  unsigned char prefix_len)
{
	set_bit(prefix_len, prefix_usage->b);
}

static void
mlxsw_sp_prefix_usage_clear(struct mlxsw_sp_prefix_usage *prefix_usage,
			    unsigned char prefix_len)
{
	clear_bit(prefix_len, prefix_usage->b);
}

struct mlxsw_sp_fib_key {
	unsigned char addr[sizeof(struct in6_addr)];
	unsigned char prefix_len;
};

struct mlxsw_sp_fib_entry {
	struct rhash_head ht_node;
	struct mlxsw_sp_fib_key key;
};

struct mlxsw_sp_fib {
	struct rhashtable ht;
	unsigned long prefix_ref_count[MLXSW_SP_PREFIX_COUNT];
	struct mlxsw_sp_prefix_usage prefix_usage;
};

static const struct rhashtable_params mlxsw_sp_fib_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_fib_entry, key),
	.head_offset = offsetof(struct mlxsw_sp_fib_entry, ht_node),
	.key_len = sizeof(struct mlxsw_sp_fib_key),
	.automatic_shrinking = true,
};

static int mlxsw_sp_fib_entry_insert(struct mlxsw_sp_fib *fib,
				     struct mlxsw_sp_fib_entry *fib_entry)
{
	unsigned char prefix_len = fib_entry->key.prefix_len;
	int err;

	err = rhashtable_insert_fast(&fib->ht, &fib_entry->ht_node,
				     mlxsw_sp_fib_ht_params);
	if (err)
		return err;
	if (fib->prefix_ref_count[prefix_len]++ == 0)
		mlxsw_sp_prefix_usage_set(&fib->prefix_usage, prefix_len);
	return 0;
}

static void mlxsw_sp_fib_entry_remove(struct mlxsw_sp_fib *fib,
				      struct mlxsw_sp_fib_entry *fib_entry)
{
	unsigned char prefix_len = fib_entry->key.prefix_len;

	if (--fib->prefix_ref_count[prefix_len] == 0)
		mlxsw_sp_prefix_usage_clear(&fib->prefix_usage, prefix_len);
	rhashtable_remove_fast(&fib->ht, &fib_entry->ht_node,
			       mlxsw_sp_fib_ht_params);
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib_entry_create(struct mlxsw_sp_fib *fib, const void *addr,
			  size_t addr_len, unsigned char prefix_len)
{
	struct mlxsw_sp_fib_entry *fib_entry;

	fib_entry = kzalloc(sizeof(*fib_entry), GFP_KERNEL);
	if (!fib_entry)
		return NULL;
	memcpy(fib_entry->key.addr, addr, addr_len);
	fib_entry->key.prefix_len = prefix_len;
	return fib_entry;
}

static void mlxsw_sp_fib_entry_destroy(struct mlxsw_sp_fib_entry *fib_entry)
{
	kfree(fib_entry);
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_fib_entry_lookup(struct mlxsw_sp_fib *fib, const void *addr,
			  size_t addr_len, unsigned char prefix_len)
{
	struct mlxsw_sp_fib_key key = {{ 0 } };

	memcpy(key.addr, addr, addr_len);
	key.prefix_len = prefix_len;
	return rhashtable_lookup_fast(&fib->ht, &key, mlxsw_sp_fib_ht_params);
}

static struct mlxsw_sp_fib *mlxsw_sp_fib_create(void)
{
	struct mlxsw_sp_fib *fib;
	int err;

	fib = kzalloc(sizeof(*fib), GFP_KERNEL);
	if (!fib)
		return ERR_PTR(-ENOMEM);
	err = rhashtable_init(&fib->ht, &mlxsw_sp_fib_ht_params);
	if (err)
		goto err_rhashtable_init;
	return fib;

err_rhashtable_init:
	kfree(fib);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib_destroy(struct mlxsw_sp_fib *fib)
{
	rhashtable_destroy(&fib->ht);
	kfree(fib);
}

static int __mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	char rgcr_pl[MLXSW_REG_RGCR_LEN];

	mlxsw_reg_rgcr_pack(rgcr_pl, true);
	mlxsw_reg_rgcr_max_router_interfaces_set(rgcr_pl, MLXSW_SP_RIF_MAX);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);
}

static void __mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	char rgcr_pl[MLXSW_REG_RGCR_LEN];

	mlxsw_reg_rgcr_pack(rgcr_pl, false);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);
}

int mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	return __mlxsw_sp_router_init(mlxsw_sp);
}

void mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	__mlxsw_sp_router_fini(mlxsw_sp);
}
