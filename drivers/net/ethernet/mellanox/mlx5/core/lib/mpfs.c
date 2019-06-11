/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/etherdevice.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/eswitch.h>
#include "mlx5_core.h"
#include "lib/mpfs.h"

/* HW L2 Table (MPFS) management */
static int set_l2table_entry_cmd(struct mlx5_core_dev *dev, u32 index, u8 *mac)
{
	u32 in[MLX5_ST_SZ_DW(set_l2_table_entry_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(set_l2_table_entry_out)] = {0};
	u8 *in_mac_addr;

	MLX5_SET(set_l2_table_entry_in, in, opcode, MLX5_CMD_OP_SET_L2_TABLE_ENTRY);
	MLX5_SET(set_l2_table_entry_in, in, table_index, index);

	in_mac_addr = MLX5_ADDR_OF(set_l2_table_entry_in, in, mac_address);
	ether_addr_copy(&in_mac_addr[2], mac);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static int del_l2table_entry_cmd(struct mlx5_core_dev *dev, u32 index)
{
	u32 in[MLX5_ST_SZ_DW(delete_l2_table_entry_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(delete_l2_table_entry_out)] = {0};

	MLX5_SET(delete_l2_table_entry_in, in, opcode, MLX5_CMD_OP_DELETE_L2_TABLE_ENTRY);
	MLX5_SET(delete_l2_table_entry_in, in, table_index, index);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

/* UC L2 table hash node */
struct l2table_node {
	struct l2addr_node node;
	u32                index; /* index in HW l2 table */
};

struct mlx5_mpfs {
	struct hlist_head    hash[MLX5_L2_ADDR_HASH_SIZE];
	struct mutex         lock; /* Synchronize l2 table access */
	u32                  size;
	unsigned long        *bitmap;
};

static int alloc_l2table_index(struct mlx5_mpfs *l2table, u32 *ix)
{
	int err = 0;

	*ix = find_first_zero_bit(l2table->bitmap, l2table->size);
	if (*ix >= l2table->size)
		err = -ENOSPC;
	else
		__set_bit(*ix, l2table->bitmap);

	return err;
}

static void free_l2table_index(struct mlx5_mpfs *l2table, u32 ix)
{
	__clear_bit(ix, l2table->bitmap);
}

int mlx5_mpfs_init(struct mlx5_core_dev *dev)
{
	int l2table_size = 1 << MLX5_CAP_GEN(dev, log_max_l2_table);
	struct mlx5_mpfs *mpfs;

	if (!MLX5_ESWITCH_MANAGER(dev))
		return 0;

	mpfs = kzalloc(sizeof(*mpfs), GFP_KERNEL);
	if (!mpfs)
		return -ENOMEM;

	mutex_init(&mpfs->lock);
	mpfs->size   = l2table_size;
	mpfs->bitmap = bitmap_zalloc(l2table_size, GFP_KERNEL);
	if (!mpfs->bitmap) {
		kfree(mpfs);
		return -ENOMEM;
	}

	dev->priv.mpfs = mpfs;
	return 0;
}

void mlx5_mpfs_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_mpfs *mpfs = dev->priv.mpfs;

	if (!MLX5_ESWITCH_MANAGER(dev))
		return;

	WARN_ON(!hlist_empty(mpfs->hash));
	bitmap_free(mpfs->bitmap);
	kfree(mpfs);
}

int mlx5_mpfs_add_mac(struct mlx5_core_dev *dev, u8 *mac)
{
	struct mlx5_mpfs *mpfs = dev->priv.mpfs;
	struct l2table_node *l2addr;
	int err = 0;
	u32 index;

	if (!MLX5_ESWITCH_MANAGER(dev))
		return 0;

	mutex_lock(&mpfs->lock);

	l2addr = l2addr_hash_find(mpfs->hash, mac, struct l2table_node);
	if (l2addr) {
		err = -EEXIST;
		goto out;
	}

	err = alloc_l2table_index(mpfs, &index);
	if (err)
		goto out;

	l2addr = l2addr_hash_add(mpfs->hash, mac, struct l2table_node, GFP_KERNEL);
	if (!l2addr) {
		err = -ENOMEM;
		goto hash_add_err;
	}

	err = set_l2table_entry_cmd(dev, index, mac);
	if (err)
		goto set_table_entry_err;

	l2addr->index = index;

	mlx5_core_dbg(dev, "MPFS mac added %pM, index (%d)\n", mac, index);
	goto out;

set_table_entry_err:
	l2addr_hash_del(l2addr);
hash_add_err:
	free_l2table_index(mpfs, index);
out:
	mutex_unlock(&mpfs->lock);
	return err;
}

int mlx5_mpfs_del_mac(struct mlx5_core_dev *dev, u8 *mac)
{
	struct mlx5_mpfs *mpfs = dev->priv.mpfs;
	struct l2table_node *l2addr;
	int err = 0;
	u32 index;

	if (!MLX5_ESWITCH_MANAGER(dev))
		return 0;

	mutex_lock(&mpfs->lock);

	l2addr = l2addr_hash_find(mpfs->hash, mac, struct l2table_node);
	if (!l2addr) {
		err = -ENOENT;
		goto unlock;
	}

	index = l2addr->index;
	del_l2table_entry_cmd(dev, index);
	l2addr_hash_del(l2addr);
	free_l2table_index(mpfs, index);
	mlx5_core_dbg(dev, "MPFS mac deleted %pM, index (%d)\n", mac, index);
unlock:
	mutex_unlock(&mpfs->lock);
	return err;
}
