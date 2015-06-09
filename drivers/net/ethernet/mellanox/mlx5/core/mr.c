/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include "mlx5_core.h"

void mlx5_init_mr_table(struct mlx5_core_dev *dev)
{
	struct mlx5_mr_table *table = &dev->priv.mr_table;

	rwlock_init(&table->lock);
	INIT_RADIX_TREE(&table->tree, GFP_ATOMIC);
}

void mlx5_cleanup_mr_table(struct mlx5_core_dev *dev)
{
}

int mlx5_core_create_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr,
			  struct mlx5_create_mkey_mbox_in *in, int inlen,
			  mlx5_cmd_cbk_t callback, void *context,
			  struct mlx5_create_mkey_mbox_out *out)
{
	struct mlx5_mr_table *table = &dev->priv.mr_table;
	struct mlx5_create_mkey_mbox_out lout;
	int err;
	u8 key;

	memset(&lout, 0, sizeof(lout));
	spin_lock_irq(&dev->priv.mkey_lock);
	key = dev->priv.mkey_key++;
	spin_unlock_irq(&dev->priv.mkey_lock);
	in->seg.qpn_mkey7_0 |= cpu_to_be32(key);
	in->hdr.opcode = cpu_to_be16(MLX5_CMD_OP_CREATE_MKEY);
	if (callback) {
		err = mlx5_cmd_exec_cb(dev, in, inlen, out, sizeof(*out),
				       callback, context);
		return err;
	} else {
		err = mlx5_cmd_exec(dev, in, inlen, &lout, sizeof(lout));
	}

	if (err) {
		mlx5_core_dbg(dev, "cmd exec failed %d\n", err);
		return err;
	}

	if (lout.hdr.status) {
		mlx5_core_dbg(dev, "status %d\n", lout.hdr.status);
		return mlx5_cmd_status_to_err(&lout.hdr);
	}

	mr->iova = be64_to_cpu(in->seg.start_addr);
	mr->size = be64_to_cpu(in->seg.len);
	mr->key = mlx5_idx_to_mkey(be32_to_cpu(lout.mkey) & 0xffffff) | key;
	mr->pd = be32_to_cpu(in->seg.flags_pd) & 0xffffff;

	mlx5_core_dbg(dev, "out 0x%x, key 0x%x, mkey 0x%x\n",
		      be32_to_cpu(lout.mkey), key, mr->key);

	/* connect to MR tree */
	write_lock_irq(&table->lock);
	err = radix_tree_insert(&table->tree, mlx5_base_mkey(mr->key), mr);
	write_unlock_irq(&table->lock);
	if (err) {
		mlx5_core_warn(dev, "failed radix tree insert of mr 0x%x, %d\n",
			       mlx5_base_mkey(mr->key), err);
		mlx5_core_destroy_mkey(dev, mr);
	}

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_mkey);

int mlx5_core_destroy_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr)
{
	struct mlx5_mr_table *table = &dev->priv.mr_table;
	struct mlx5_destroy_mkey_mbox_in in;
	struct mlx5_destroy_mkey_mbox_out out;
	struct mlx5_core_mr *deleted_mr;
	unsigned long flags;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));

	write_lock_irqsave(&table->lock, flags);
	deleted_mr = radix_tree_delete(&table->tree, mlx5_base_mkey(mr->key));
	write_unlock_irqrestore(&table->lock, flags);
	if (!deleted_mr) {
		mlx5_core_warn(dev, "failed radix tree delete of mr 0x%x\n",
			       mlx5_base_mkey(mr->key));
		return -ENOENT;
	}

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_DESTROY_MKEY);
	in.mkey = cpu_to_be32(mlx5_mkey_to_idx(mr->key));
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	return err;
}
EXPORT_SYMBOL(mlx5_core_destroy_mkey);

int mlx5_core_query_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr,
			 struct mlx5_query_mkey_mbox_out *out, int outlen)
{
	struct mlx5_query_mkey_mbox_in in;
	int err;

	memset(&in, 0, sizeof(in));
	memset(out, 0, outlen);

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_MKEY);
	in.mkey = cpu_to_be32(mlx5_mkey_to_idx(mr->key));
	err = mlx5_cmd_exec(dev, &in, sizeof(in), out, outlen);
	if (err)
		return err;

	if (out->hdr.status)
		return mlx5_cmd_status_to_err(&out->hdr);

	return err;
}
EXPORT_SYMBOL(mlx5_core_query_mkey);

int mlx5_core_dump_fill_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr,
			     u32 *mkey)
{
	struct mlx5_query_special_ctxs_mbox_in in;
	struct mlx5_query_special_ctxs_mbox_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	*mkey = be32_to_cpu(out.dump_fill_mkey);

	return err;
}
EXPORT_SYMBOL(mlx5_core_dump_fill_mkey);

int mlx5_core_create_psv(struct mlx5_core_dev *dev, u32 pdn,
			 int npsvs, u32 *sig_index)
{
	struct mlx5_allocate_psv_in in;
	struct mlx5_allocate_psv_out out;
	int i, err;

	if (npsvs > MLX5_MAX_PSVS)
		return -EINVAL;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_CREATE_PSV);
	in.npsv_pd = cpu_to_be32((npsvs << 28) | pdn);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err) {
		mlx5_core_err(dev, "cmd exec failed %d\n", err);
		return err;
	}

	if (out.hdr.status) {
		mlx5_core_err(dev, "create_psv bad status %d\n",
			      out.hdr.status);
		return mlx5_cmd_status_to_err(&out.hdr);
	}

	for (i = 0; i < npsvs; i++)
		sig_index[i] = be32_to_cpu(out.psv_idx[i]) & 0xffffff;

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_psv);

int mlx5_core_destroy_psv(struct mlx5_core_dev *dev, int psv_num)
{
	struct mlx5_destroy_psv_in in;
	struct mlx5_destroy_psv_out out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));

	in.psv_number = cpu_to_be32(psv_num);
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_DESTROY_PSV);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err) {
		mlx5_core_err(dev, "destroy_psv cmd exec failed %d\n", err);
		goto out;
	}

	if (out.hdr.status) {
		mlx5_core_err(dev, "destroy_psv bad status %d\n",
			      out.hdr.status);
		err = mlx5_cmd_status_to_err(&out.hdr);
		goto out;
	}

out:
	return err;
}
EXPORT_SYMBOL(mlx5_core_destroy_psv);
