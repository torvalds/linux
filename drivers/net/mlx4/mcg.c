/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <linux/mlx4/cmd.h>

#include "mlx4.h"

struct mlx4_mgm {
	__be32			next_gid_index;
	__be32			members_count;
	u32			reserved[2];
	u8			gid[16];
	__be32			qp[MLX4_QP_PER_MGM];
};

static const u8 zero_gid[16];	/* automatically initialized to 0 */

static int mlx4_READ_MCG(struct mlx4_dev *dev, int index,
			 struct mlx4_cmd_mailbox *mailbox)
{
	return mlx4_cmd_box(dev, 0, mailbox->dma, index, 0, MLX4_CMD_READ_MCG,
			    MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_WRITE_MCG(struct mlx4_dev *dev, int index,
			  struct mlx4_cmd_mailbox *mailbox)
{
	return mlx4_cmd(dev, mailbox->dma, index, 0, MLX4_CMD_WRITE_MCG,
			MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_MGID_HASH(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			  u16 *hash)
{
	u64 imm;
	int err;

	err = mlx4_cmd_imm(dev, mailbox->dma, &imm, 0, 0, MLX4_CMD_MGID_HASH,
			   MLX4_CMD_TIME_CLASS_A);

	if (!err)
		*hash = imm;

	return err;
}

/*
 * Caller must hold MCG table semaphore.  gid and mgm parameters must
 * be properly aligned for command interface.
 *
 *  Returns 0 unless a firmware command error occurs.
 *
 * If GID is found in MGM or MGM is empty, *index = *hash, *prev = -1
 * and *mgm holds MGM entry.
 *
 * if GID is found in AMGM, *index = index in AMGM, *prev = index of
 * previous entry in hash chain and *mgm holds AMGM entry.
 *
 * If no AMGM exists for given gid, *index = -1, *prev = index of last
 * entry in hash chain and *mgm holds end of hash chain.
 */
static int find_mgm(struct mlx4_dev *dev,
		    u8 *gid, struct mlx4_cmd_mailbox *mgm_mailbox,
		    u16 *hash, int *prev, int *index)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_mgm *mgm = mgm_mailbox->buf;
	u8 *mgid;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return -ENOMEM;
	mgid = mailbox->buf;

	memcpy(mgid, gid, 16);

	err = mlx4_MGID_HASH(dev, mailbox, hash);
	mlx4_free_cmd_mailbox(dev, mailbox);
	if (err)
		return err;

	if (0)
		mlx4_dbg(dev, "Hash for %04x:%04x:%04x:%04x:"
			  "%04x:%04x:%04x:%04x is %04x\n",
			  be16_to_cpu(((__be16 *) gid)[0]),
			  be16_to_cpu(((__be16 *) gid)[1]),
			  be16_to_cpu(((__be16 *) gid)[2]),
			  be16_to_cpu(((__be16 *) gid)[3]),
			  be16_to_cpu(((__be16 *) gid)[4]),
			  be16_to_cpu(((__be16 *) gid)[5]),
			  be16_to_cpu(((__be16 *) gid)[6]),
			  be16_to_cpu(((__be16 *) gid)[7]),
			  *hash);

	*index = *hash;
	*prev  = -1;

	do {
		err = mlx4_READ_MCG(dev, *index, mgm_mailbox);
		if (err)
			return err;

		if (!memcmp(mgm->gid, zero_gid, 16)) {
			if (*index != *hash) {
				mlx4_err(dev, "Found zero MGID in AMGM.\n");
				err = -EINVAL;
			}
			return err;
		}

		if (!memcmp(mgm->gid, gid, 16))
			return err;

		*prev = *index;
		*index = be32_to_cpu(mgm->next_gid_index) >> 6;
	} while (*index);

	*index = -1;
	return err;
}

int mlx4_multicast_attach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16])
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_mgm *mgm;
	u32 members_count;
	u16 hash;
	int index, prev;
	int link = 0;
	int i;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	mgm = mailbox->buf;

	mutex_lock(&priv->mcg_table.mutex);

	err = find_mgm(dev, gid, mailbox, &hash, &prev, &index);
	if (err)
		goto out;

	if (index != -1) {
		if (!memcmp(mgm->gid, zero_gid, 16))
			memcpy(mgm->gid, gid, 16);
	} else {
		link = 1;

		index = mlx4_bitmap_alloc(&priv->mcg_table.bitmap);
		if (index == -1) {
			mlx4_err(dev, "No AMGM entries left\n");
			err = -ENOMEM;
			goto out;
		}
		index += dev->caps.num_mgms;

		err = mlx4_READ_MCG(dev, index, mailbox);
		if (err)
			goto out;

		memset(mgm, 0, sizeof *mgm);
		memcpy(mgm->gid, gid, 16);
	}

	members_count = be32_to_cpu(mgm->members_count);
	if (members_count == MLX4_QP_PER_MGM) {
		mlx4_err(dev, "MGM at index %x is full.\n", index);
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < members_count; ++i)
		if (mgm->qp[i] == cpu_to_be32(qp->qpn)) {
			mlx4_dbg(dev, "QP %06x already a member of MGM\n", qp->qpn);
			err = 0;
			goto out;
		}

	mgm->qp[members_count++] = cpu_to_be32(qp->qpn);
	mgm->members_count       = cpu_to_be32(members_count);

	err = mlx4_WRITE_MCG(dev, index, mailbox);
	if (err)
		goto out;

	if (!link)
		goto out;

	err = mlx4_READ_MCG(dev, prev, mailbox);
	if (err)
		goto out;

	mgm->next_gid_index = cpu_to_be32(index << 6);

	err = mlx4_WRITE_MCG(dev, prev, mailbox);
	if (err)
		goto out;

out:
	if (err && link && index != -1) {
		if (index < dev->caps.num_mgms)
			mlx4_warn(dev, "Got AMGM index %d < %d",
				  index, dev->caps.num_mgms);
		else
			mlx4_bitmap_free(&priv->mcg_table.bitmap,
					 index - dev->caps.num_mgms);
	}
	mutex_unlock(&priv->mcg_table.mutex);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_multicast_attach);

int mlx4_multicast_detach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16])
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_mgm *mgm;
	u32 members_count;
	u16 hash;
	int prev, index;
	int i, loc;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	mgm = mailbox->buf;

	mutex_lock(&priv->mcg_table.mutex);

	err = find_mgm(dev, gid, mailbox, &hash, &prev, &index);
	if (err)
		goto out;

	if (index == -1) {
		mlx4_err(dev, "MGID %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x "
			  "not found\n",
			  be16_to_cpu(((__be16 *) gid)[0]),
			  be16_to_cpu(((__be16 *) gid)[1]),
			  be16_to_cpu(((__be16 *) gid)[2]),
			  be16_to_cpu(((__be16 *) gid)[3]),
			  be16_to_cpu(((__be16 *) gid)[4]),
			  be16_to_cpu(((__be16 *) gid)[5]),
			  be16_to_cpu(((__be16 *) gid)[6]),
			  be16_to_cpu(((__be16 *) gid)[7]));
		err = -EINVAL;
		goto out;
	}

	members_count = be32_to_cpu(mgm->members_count);
	for (loc = -1, i = 0; i < members_count; ++i)
		if (mgm->qp[i] == cpu_to_be32(qp->qpn))
			loc = i;

	if (loc == -1) {
		mlx4_err(dev, "QP %06x not found in MGM\n", qp->qpn);
		err = -EINVAL;
		goto out;
	}


	mgm->members_count = cpu_to_be32(--members_count);
	mgm->qp[loc]       = mgm->qp[i - 1];
	mgm->qp[i - 1]     = 0;

	err = mlx4_WRITE_MCG(dev, index, mailbox);
	if (err)
		goto out;

	if (i != 1)
		goto out;

	if (prev == -1) {
		/* Remove entry from MGM */
		int amgm_index = be32_to_cpu(mgm->next_gid_index) >> 6;
		if (amgm_index) {
			err = mlx4_READ_MCG(dev, amgm_index, mailbox);
			if (err)
				goto out;
		} else
			memset(mgm->gid, 0, 16);

		err = mlx4_WRITE_MCG(dev, index, mailbox);
		if (err)
			goto out;

		if (amgm_index) {
			if (amgm_index < dev->caps.num_mgms)
				mlx4_warn(dev, "MGM entry %d had AMGM index %d < %d",
					  index, amgm_index, dev->caps.num_mgms);
			else
				mlx4_bitmap_free(&priv->mcg_table.bitmap,
						 amgm_index - dev->caps.num_mgms);
		}
	} else {
		/* Remove entry from AMGM */
		int cur_next_index = be32_to_cpu(mgm->next_gid_index) >> 6;
		err = mlx4_READ_MCG(dev, prev, mailbox);
		if (err)
			goto out;

		mgm->next_gid_index = cpu_to_be32(cur_next_index << 6);

		err = mlx4_WRITE_MCG(dev, prev, mailbox);
		if (err)
			goto out;

		if (index < dev->caps.num_mgms)
			mlx4_warn(dev, "entry %d had next AMGM index %d < %d",
				  prev, index, dev->caps.num_mgms);
		else
			mlx4_bitmap_free(&priv->mcg_table.bitmap,
					 index - dev->caps.num_mgms);
	}

out:
	mutex_unlock(&priv->mcg_table.mutex);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_multicast_detach);

int mlx4_init_mcg_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;

	err = mlx4_bitmap_init(&priv->mcg_table.bitmap,
			       dev->caps.num_amgms, dev->caps.num_amgms - 1, 0);
	if (err)
		return err;

	mutex_init(&priv->mcg_table.mutex);

	return 0;
}

void mlx4_cleanup_mcg_table(struct mlx4_dev *dev)
{
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->mcg_table.bitmap);
}
