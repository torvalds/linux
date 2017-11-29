/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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

#include <linux/gfp.h>
#include <linux/export.h>

#include <linux/mlx4/cmd.h>
#include <linux/mlx4/qp.h>

#include "mlx4.h"
#include "icm.h"

/* QP to support BF should have bits 6,7 cleared */
#define MLX4_BF_QP_SKIP_MASK	0xc0
#define MLX4_MAX_BF_QP_RANGE	0x40

void mlx4_qp_event(struct mlx4_dev *dev, u32 qpn, int event_type)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	struct mlx4_qp *qp;

	spin_lock(&qp_table->lock);

	qp = __mlx4_qp_lookup(dev, qpn);
	if (qp)
		refcount_inc(&qp->refcount);

	spin_unlock(&qp_table->lock);

	if (!qp) {
		mlx4_dbg(dev, "Async event for none existent QP %08x\n", qpn);
		return;
	}

	qp->event(qp, event_type);

	if (refcount_dec_and_test(&qp->refcount))
		complete(&qp->free);
}

/* used for INIT/CLOSE port logic */
static int is_master_qp0(struct mlx4_dev *dev, struct mlx4_qp *qp, int *real_qp0, int *proxy_qp0)
{
	/* this procedure is called after we already know we are on the master */
	/* qp0 is either the proxy qp0, or the real qp0 */
	u32 pf_proxy_offset = dev->phys_caps.base_proxy_sqpn + 8 * mlx4_master_func_num(dev);
	*proxy_qp0 = qp->qpn >= pf_proxy_offset && qp->qpn <= pf_proxy_offset + 1;

	*real_qp0 = qp->qpn >= dev->phys_caps.base_sqpn &&
		qp->qpn <= dev->phys_caps.base_sqpn + 1;

	return *real_qp0 || *proxy_qp0;
}

static int __mlx4_qp_modify(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		     enum mlx4_qp_state cur_state, enum mlx4_qp_state new_state,
		     struct mlx4_qp_context *context,
		     enum mlx4_qp_optpar optpar,
		     int sqd_event, struct mlx4_qp *qp, int native)
{
	static const u16 op[MLX4_QP_NUM_STATE][MLX4_QP_NUM_STATE] = {
		[MLX4_QP_STATE_RST] = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_INIT]	= MLX4_CMD_RST2INIT_QP,
		},
		[MLX4_QP_STATE_INIT]  = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_INIT]	= MLX4_CMD_INIT2INIT_QP,
			[MLX4_QP_STATE_RTR]	= MLX4_CMD_INIT2RTR_QP,
		},
		[MLX4_QP_STATE_RTR]   = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_RTS]	= MLX4_CMD_RTR2RTS_QP,
		},
		[MLX4_QP_STATE_RTS]   = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_RTS]	= MLX4_CMD_RTS2RTS_QP,
			[MLX4_QP_STATE_SQD]	= MLX4_CMD_RTS2SQD_QP,
		},
		[MLX4_QP_STATE_SQD] = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_RTS]	= MLX4_CMD_SQD2RTS_QP,
			[MLX4_QP_STATE_SQD]	= MLX4_CMD_SQD2SQD_QP,
		},
		[MLX4_QP_STATE_SQER] = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
			[MLX4_QP_STATE_RTS]	= MLX4_CMD_SQERR2RTS_QP,
		},
		[MLX4_QP_STATE_ERR] = {
			[MLX4_QP_STATE_RST]	= MLX4_CMD_2RST_QP,
			[MLX4_QP_STATE_ERR]	= MLX4_CMD_2ERR_QP,
		}
	};

	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_mailbox *mailbox;
	int ret = 0;
	int real_qp0 = 0;
	int proxy_qp0 = 0;
	u8 port;

	if (cur_state >= MLX4_QP_NUM_STATE || new_state >= MLX4_QP_NUM_STATE ||
	    !op[cur_state][new_state])
		return -EINVAL;

	if (op[cur_state][new_state] == MLX4_CMD_2RST_QP) {
		ret = mlx4_cmd(dev, 0, qp->qpn, 2,
			MLX4_CMD_2RST_QP, MLX4_CMD_TIME_CLASS_A, native);
		if (mlx4_is_master(dev) && cur_state != MLX4_QP_STATE_ERR &&
		    cur_state != MLX4_QP_STATE_RST &&
		    is_master_qp0(dev, qp, &real_qp0, &proxy_qp0)) {
			port = (qp->qpn & 1) + 1;
			if (proxy_qp0)
				priv->mfunc.master.qp0_state[port].proxy_qp0_active = 0;
			else
				priv->mfunc.master.qp0_state[port].qp0_active = 0;
		}
		return ret;
	}

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	if (cur_state == MLX4_QP_STATE_RST && new_state == MLX4_QP_STATE_INIT) {
		u64 mtt_addr = mlx4_mtt_addr(dev, mtt);
		context->mtt_base_addr_h = mtt_addr >> 32;
		context->mtt_base_addr_l = cpu_to_be32(mtt_addr & 0xffffffff);
		context->log_page_size   = mtt->page_shift - MLX4_ICM_PAGE_SHIFT;
	}

	if ((cur_state == MLX4_QP_STATE_RTR) &&
	    (new_state == MLX4_QP_STATE_RTS) &&
	    dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ROCE_V1_V2)
		context->roce_entropy =
			cpu_to_be16(mlx4_qp_roce_entropy(dev, qp->qpn));

	*(__be32 *) mailbox->buf = cpu_to_be32(optpar);
	memcpy(mailbox->buf + 8, context, sizeof(*context));

	((struct mlx4_qp_context *) (mailbox->buf + 8))->local_qpn =
		cpu_to_be32(qp->qpn);

	ret = mlx4_cmd(dev, mailbox->dma,
		       qp->qpn | (!!sqd_event << 31),
		       new_state == MLX4_QP_STATE_RST ? 2 : 0,
		       op[cur_state][new_state], MLX4_CMD_TIME_CLASS_C, native);

	if (mlx4_is_master(dev) && is_master_qp0(dev, qp, &real_qp0, &proxy_qp0)) {
		port = (qp->qpn & 1) + 1;
		if (cur_state != MLX4_QP_STATE_ERR &&
		    cur_state != MLX4_QP_STATE_RST &&
		    new_state == MLX4_QP_STATE_ERR) {
			if (proxy_qp0)
				priv->mfunc.master.qp0_state[port].proxy_qp0_active = 0;
			else
				priv->mfunc.master.qp0_state[port].qp0_active = 0;
		} else if (new_state == MLX4_QP_STATE_RTR) {
			if (proxy_qp0)
				priv->mfunc.master.qp0_state[port].proxy_qp0_active = 1;
			else
				priv->mfunc.master.qp0_state[port].qp0_active = 1;
		}
	}

	mlx4_free_cmd_mailbox(dev, mailbox);
	return ret;
}

int mlx4_qp_modify(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   enum mlx4_qp_state cur_state, enum mlx4_qp_state new_state,
		   struct mlx4_qp_context *context,
		   enum mlx4_qp_optpar optpar,
		   int sqd_event, struct mlx4_qp *qp)
{
	return __mlx4_qp_modify(dev, mtt, cur_state, new_state, context,
				optpar, sqd_event, qp, 0);
}
EXPORT_SYMBOL_GPL(mlx4_qp_modify);

int __mlx4_qp_reserve_range(struct mlx4_dev *dev, int cnt, int align,
			    int *base, u8 flags)
{
	u32 uid;
	int bf_qp = !!(flags & (u8)MLX4_RESERVE_ETH_BF_QP);

	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_qp_table *qp_table = &priv->qp_table;

	if (cnt > MLX4_MAX_BF_QP_RANGE && bf_qp)
		return -ENOMEM;

	uid = MLX4_QP_TABLE_ZONE_GENERAL;
	if (flags & (u8)MLX4_RESERVE_A0_QP) {
		if (bf_qp)
			uid = MLX4_QP_TABLE_ZONE_RAW_ETH;
		else
			uid = MLX4_QP_TABLE_ZONE_RSS;
	}

	*base = mlx4_zone_alloc_entries(qp_table->zones, uid, cnt, align,
					bf_qp ? MLX4_BF_QP_SKIP_MASK : 0, NULL);
	if (*base == -1)
		return -ENOMEM;

	return 0;
}

int mlx4_qp_reserve_range(struct mlx4_dev *dev, int cnt, int align,
			  int *base, u8 flags, u8 usage)
{
	u32 in_modifier = RES_QP | (((u32)usage & 3) << 30);
	u64 in_param = 0;
	u64 out_param;
	int err;

	/* Turn off all unsupported QP allocation flags */
	flags &= dev->caps.alloc_res_qp_mask;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, (((u32)flags) << 24) | (u32)cnt);
		set_param_h(&in_param, align);
		err = mlx4_cmd_imm(dev, in_param, &out_param,
				   in_modifier, RES_OP_RESERVE,
				   MLX4_CMD_ALLOC_RES,
				   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		if (err)
			return err;

		*base = get_param_l(&out_param);
		return 0;
	}
	return __mlx4_qp_reserve_range(dev, cnt, align, base, flags);
}
EXPORT_SYMBOL_GPL(mlx4_qp_reserve_range);

void __mlx4_qp_release_range(struct mlx4_dev *dev, int base_qpn, int cnt)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_qp_table *qp_table = &priv->qp_table;

	if (mlx4_is_qp_reserved(dev, (u32) base_qpn))
		return;
	mlx4_zone_free_entries_unique(qp_table->zones, base_qpn, cnt);
}

void mlx4_qp_release_range(struct mlx4_dev *dev, int base_qpn, int cnt)
{
	u64 in_param = 0;
	int err;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, base_qpn);
		set_param_h(&in_param, cnt);
		err = mlx4_cmd(dev, in_param, RES_QP, RES_OP_RESERVE,
			       MLX4_CMD_FREE_RES,
			       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		if (err) {
			mlx4_warn(dev, "Failed to release qp range base:%d cnt:%d\n",
				  base_qpn, cnt);
		}
	} else
		 __mlx4_qp_release_range(dev, base_qpn, cnt);
}
EXPORT_SYMBOL_GPL(mlx4_qp_release_range);

int __mlx4_qp_alloc_icm(struct mlx4_dev *dev, int qpn)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_qp_table *qp_table = &priv->qp_table;
	int err;

	err = mlx4_table_get(dev, &qp_table->qp_table, qpn);
	if (err)
		goto err_out;

	err = mlx4_table_get(dev, &qp_table->auxc_table, qpn);
	if (err)
		goto err_put_qp;

	err = mlx4_table_get(dev, &qp_table->altc_table, qpn);
	if (err)
		goto err_put_auxc;

	err = mlx4_table_get(dev, &qp_table->rdmarc_table, qpn);
	if (err)
		goto err_put_altc;

	err = mlx4_table_get(dev, &qp_table->cmpt_table, qpn);
	if (err)
		goto err_put_rdmarc;

	return 0;

err_put_rdmarc:
	mlx4_table_put(dev, &qp_table->rdmarc_table, qpn);

err_put_altc:
	mlx4_table_put(dev, &qp_table->altc_table, qpn);

err_put_auxc:
	mlx4_table_put(dev, &qp_table->auxc_table, qpn);

err_put_qp:
	mlx4_table_put(dev, &qp_table->qp_table, qpn);

err_out:
	return err;
}

static int mlx4_qp_alloc_icm(struct mlx4_dev *dev, int qpn)
{
	u64 param = 0;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&param, qpn);
		return mlx4_cmd_imm(dev, param, &param, RES_QP, RES_OP_MAP_ICM,
				    MLX4_CMD_ALLOC_RES, MLX4_CMD_TIME_CLASS_A,
				    MLX4_CMD_WRAPPED);
	}
	return __mlx4_qp_alloc_icm(dev, qpn);
}

void __mlx4_qp_free_icm(struct mlx4_dev *dev, int qpn)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_qp_table *qp_table = &priv->qp_table;

	mlx4_table_put(dev, &qp_table->cmpt_table, qpn);
	mlx4_table_put(dev, &qp_table->rdmarc_table, qpn);
	mlx4_table_put(dev, &qp_table->altc_table, qpn);
	mlx4_table_put(dev, &qp_table->auxc_table, qpn);
	mlx4_table_put(dev, &qp_table->qp_table, qpn);
}

static void mlx4_qp_free_icm(struct mlx4_dev *dev, int qpn)
{
	u64 in_param = 0;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, qpn);
		if (mlx4_cmd(dev, in_param, RES_QP, RES_OP_MAP_ICM,
			     MLX4_CMD_FREE_RES, MLX4_CMD_TIME_CLASS_A,
			     MLX4_CMD_WRAPPED))
			mlx4_warn(dev, "Failed to free icm of qp:%d\n", qpn);
	} else
		__mlx4_qp_free_icm(dev, qpn);
}

struct mlx4_qp *mlx4_qp_lookup(struct mlx4_dev *dev, u32 qpn)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	struct mlx4_qp *qp;

	spin_lock(&qp_table->lock);

	qp = __mlx4_qp_lookup(dev, qpn);

	spin_unlock(&qp_table->lock);
	return qp;
}

int mlx4_qp_alloc(struct mlx4_dev *dev, int qpn, struct mlx4_qp *qp)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_qp_table *qp_table = &priv->qp_table;
	int err;

	if (!qpn)
		return -EINVAL;

	qp->qpn = qpn;

	err = mlx4_qp_alloc_icm(dev, qpn);
	if (err)
		return err;

	spin_lock_irq(&qp_table->lock);
	err = radix_tree_insert(&dev->qp_table_tree, qp->qpn &
				(dev->caps.num_qps - 1), qp);
	spin_unlock_irq(&qp_table->lock);
	if (err)
		goto err_icm;

	refcount_set(&qp->refcount, 1);
	init_completion(&qp->free);

	return 0;

err_icm:
	mlx4_qp_free_icm(dev, qpn);
	return err;
}

EXPORT_SYMBOL_GPL(mlx4_qp_alloc);

int mlx4_update_qp(struct mlx4_dev *dev, u32 qpn,
		   enum mlx4_update_qp_attr attr,
		   struct mlx4_update_qp_params *params)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_update_qp_context *cmd;
	u64 pri_addr_path_mask = 0;
	u64 qp_mask = 0;
	int err = 0;

	if (!attr || (attr & ~MLX4_UPDATE_QP_SUPPORTED_ATTRS))
		return -EINVAL;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	cmd = (struct mlx4_update_qp_context *)mailbox->buf;

	if (attr & MLX4_UPDATE_QP_SMAC) {
		pri_addr_path_mask |= 1ULL << MLX4_UPD_QP_PATH_MASK_MAC_INDEX;
		cmd->qp_context.pri_path.grh_mylmc = params->smac_index;
	}

	if (attr & MLX4_UPDATE_QP_ETH_SRC_CHECK_MC_LB) {
		if (!(dev->caps.flags2
		      & MLX4_DEV_CAP_FLAG2_UPDATE_QP_SRC_CHECK_LB)) {
			mlx4_warn(dev,
				  "Trying to set src check LB, but it isn't supported\n");
			err = -EOPNOTSUPP;
			goto out;
		}
		pri_addr_path_mask |=
			1ULL << MLX4_UPD_QP_PATH_MASK_ETH_SRC_CHECK_MC_LB;
		if (params->flags &
		    MLX4_UPDATE_QP_PARAMS_FLAGS_ETH_CHECK_MC_LB) {
			cmd->qp_context.pri_path.fl |=
				MLX4_FL_ETH_SRC_CHECK_MC_LB;
		}
	}

	if (attr & MLX4_UPDATE_QP_VSD) {
		qp_mask |= 1ULL << MLX4_UPD_QP_MASK_VSD;
		if (params->flags & MLX4_UPDATE_QP_PARAMS_FLAGS_VSD_ENABLE)
			cmd->qp_context.param3 |= cpu_to_be32(MLX4_STRIP_VLAN);
	}

	if (attr & MLX4_UPDATE_QP_RATE_LIMIT) {
		qp_mask |= 1ULL << MLX4_UPD_QP_MASK_RATE_LIMIT;
		cmd->qp_context.rate_limit_params = cpu_to_be16((params->rate_unit << 14) | params->rate_val);
	}

	if (attr & MLX4_UPDATE_QP_QOS_VPORT) {
		if (!(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_QOS_VPP)) {
			mlx4_warn(dev, "Granular QoS per VF is not enabled\n");
			err = -EOPNOTSUPP;
			goto out;
		}

		qp_mask |= 1ULL << MLX4_UPD_QP_MASK_QOS_VPP;
		cmd->qp_context.qos_vport = params->qos_vport;
	}

	cmd->primary_addr_path_mask = cpu_to_be64(pri_addr_path_mask);
	cmd->qp_mask = cpu_to_be64(qp_mask);

	err = mlx4_cmd(dev, mailbox->dma, qpn & 0xffffff, 0,
		       MLX4_CMD_UPDATE_QP, MLX4_CMD_TIME_CLASS_A,
		       MLX4_CMD_NATIVE);
out:
	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_update_qp);

void mlx4_qp_remove(struct mlx4_dev *dev, struct mlx4_qp *qp)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	unsigned long flags;

	spin_lock_irqsave(&qp_table->lock, flags);
	radix_tree_delete(&dev->qp_table_tree, qp->qpn & (dev->caps.num_qps - 1));
	spin_unlock_irqrestore(&qp_table->lock, flags);
}
EXPORT_SYMBOL_GPL(mlx4_qp_remove);

void mlx4_qp_free(struct mlx4_dev *dev, struct mlx4_qp *qp)
{
	if (refcount_dec_and_test(&qp->refcount))
		complete(&qp->free);
	wait_for_completion(&qp->free);

	mlx4_qp_free_icm(dev, qp->qpn);
}
EXPORT_SYMBOL_GPL(mlx4_qp_free);

static int mlx4_CONF_SPECIAL_QP(struct mlx4_dev *dev, u32 base_qpn)
{
	return mlx4_cmd(dev, 0, base_qpn, 0, MLX4_CMD_CONF_SPECIAL_QP,
			MLX4_CMD_TIME_CLASS_B, MLX4_CMD_NATIVE);
}

#define MLX4_QP_TABLE_RSS_ETH_PRIORITY 2
#define MLX4_QP_TABLE_RAW_ETH_PRIORITY 1
#define MLX4_QP_TABLE_RAW_ETH_SIZE     256

static int mlx4_create_zones(struct mlx4_dev *dev,
			     u32 reserved_bottom_general,
			     u32 reserved_top_general,
			     u32 reserved_bottom_rss,
			     u32 start_offset_rss,
			     u32 max_table_offset)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	struct mlx4_bitmap (*bitmap)[MLX4_QP_TABLE_ZONE_NUM] = NULL;
	int bitmap_initialized = 0;
	u32 last_offset;
	int k;
	int err;

	qp_table->zones = mlx4_zone_allocator_create(MLX4_ZONE_ALLOC_FLAGS_NO_OVERLAP);

	if (NULL == qp_table->zones)
		return -ENOMEM;

	bitmap = kmalloc(sizeof(*bitmap), GFP_KERNEL);

	if (NULL == bitmap) {
		err = -ENOMEM;
		goto free_zone;
	}

	err = mlx4_bitmap_init(*bitmap + MLX4_QP_TABLE_ZONE_GENERAL, dev->caps.num_qps,
			       (1 << 23) - 1, reserved_bottom_general,
			       reserved_top_general);

	if (err)
		goto free_bitmap;

	++bitmap_initialized;

	err = mlx4_zone_add_one(qp_table->zones, *bitmap + MLX4_QP_TABLE_ZONE_GENERAL,
				MLX4_ZONE_FALLBACK_TO_HIGHER_PRIO |
				MLX4_ZONE_USE_RR, 0,
				0, qp_table->zones_uids + MLX4_QP_TABLE_ZONE_GENERAL);

	if (err)
		goto free_bitmap;

	err = mlx4_bitmap_init(*bitmap + MLX4_QP_TABLE_ZONE_RSS,
			       reserved_bottom_rss,
			       reserved_bottom_rss - 1,
			       dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
			       reserved_bottom_rss - start_offset_rss);

	if (err)
		goto free_bitmap;

	++bitmap_initialized;

	err = mlx4_zone_add_one(qp_table->zones, *bitmap + MLX4_QP_TABLE_ZONE_RSS,
				MLX4_ZONE_ALLOW_ALLOC_FROM_LOWER_PRIO |
				MLX4_ZONE_ALLOW_ALLOC_FROM_EQ_PRIO |
				MLX4_ZONE_USE_RR, MLX4_QP_TABLE_RSS_ETH_PRIORITY,
				0, qp_table->zones_uids + MLX4_QP_TABLE_ZONE_RSS);

	if (err)
		goto free_bitmap;

	last_offset = dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW];
	/*  We have a single zone for the A0 steering QPs area of the FW. This area
	 *  needs to be split into subareas. One set of subareas is for RSS QPs
	 *  (in which qp number bits 6 and/or 7 are set); the other set of subareas
	 *  is for RAW_ETH QPs, which require that both bits 6 and 7 are zero.
	 *  Currently, the values returned by the FW (A0 steering area starting qp number
	 *  and A0 steering area size) are such that there are only two subareas -- one
	 *  for RSS and one for RAW_ETH.
	 */
	for (k = MLX4_QP_TABLE_ZONE_RSS + 1; k < sizeof(*bitmap)/sizeof((*bitmap)[0]);
	     k++) {
		int size;
		u32 offset = start_offset_rss;
		u32 bf_mask;
		u32 requested_size;

		/* Assuming MLX4_BF_QP_SKIP_MASK is consecutive ones, this calculates
		 * a mask of all LSB bits set until (and not including) the first
		 * set bit of  MLX4_BF_QP_SKIP_MASK. For example, if MLX4_BF_QP_SKIP_MASK
		 * is 0xc0, bf_mask will be 0x3f.
		 */
		bf_mask = (MLX4_BF_QP_SKIP_MASK & ~(MLX4_BF_QP_SKIP_MASK - 1)) - 1;
		requested_size = min((u32)MLX4_QP_TABLE_RAW_ETH_SIZE, bf_mask + 1);

		if (((last_offset & MLX4_BF_QP_SKIP_MASK) &&
		     ((int)(max_table_offset - last_offset)) >=
		     roundup_pow_of_two(MLX4_BF_QP_SKIP_MASK)) ||
		    (!(last_offset & MLX4_BF_QP_SKIP_MASK) &&
		     !((last_offset + requested_size - 1) &
		       MLX4_BF_QP_SKIP_MASK)))
			size = requested_size;
		else {
			u32 candidate_offset =
				(last_offset | MLX4_BF_QP_SKIP_MASK | bf_mask) + 1;

			if (last_offset & MLX4_BF_QP_SKIP_MASK)
				last_offset = candidate_offset;

			/* From this point, the BF bits are 0 */

			if (last_offset > max_table_offset) {
				/* need to skip */
				size = -1;
			} else {
				size = min3(max_table_offset - last_offset,
					    bf_mask - (last_offset & bf_mask),
					    requested_size);
				if (size < requested_size) {
					int candidate_size;

					candidate_size = min3(
						max_table_offset - candidate_offset,
						bf_mask - (last_offset & bf_mask),
						requested_size);

					/*  We will not take this path if last_offset was
					 *  already set above to candidate_offset
					 */
					if (candidate_size > size) {
						last_offset = candidate_offset;
						size = candidate_size;
					}
				}
			}
		}

		if (size > 0) {
			/* mlx4_bitmap_alloc_range will find a contiguous range of "size"
			 * QPs in which both bits 6 and 7 are zero, because we pass it the
			 * MLX4_BF_SKIP_MASK).
			 */
			offset = mlx4_bitmap_alloc_range(
					*bitmap + MLX4_QP_TABLE_ZONE_RSS,
					size, 1,
					MLX4_BF_QP_SKIP_MASK);

			if (offset == (u32)-1) {
				err = -ENOMEM;
				break;
			}

			last_offset = offset + size;

			err = mlx4_bitmap_init(*bitmap + k, roundup_pow_of_two(size),
					       roundup_pow_of_two(size) - 1, 0,
					       roundup_pow_of_two(size) - size);
		} else {
			/* Add an empty bitmap, we'll allocate from different zones (since
			 * at least one is reserved)
			 */
			err = mlx4_bitmap_init(*bitmap + k, 1,
					       MLX4_QP_TABLE_RAW_ETH_SIZE - 1, 0,
					       0);
			mlx4_bitmap_alloc_range(*bitmap + k, 1, 1, 0);
		}

		if (err)
			break;

		++bitmap_initialized;

		err = mlx4_zone_add_one(qp_table->zones, *bitmap + k,
					MLX4_ZONE_ALLOW_ALLOC_FROM_LOWER_PRIO |
					MLX4_ZONE_ALLOW_ALLOC_FROM_EQ_PRIO |
					MLX4_ZONE_USE_RR, MLX4_QP_TABLE_RAW_ETH_PRIORITY,
					offset, qp_table->zones_uids + k);

		if (err)
			break;
	}

	if (err)
		goto free_bitmap;

	qp_table->bitmap_gen = *bitmap;

	return err;

free_bitmap:
	for (k = 0; k < bitmap_initialized; k++)
		mlx4_bitmap_cleanup(*bitmap + k);
	kfree(bitmap);
free_zone:
	mlx4_zone_allocator_destroy(qp_table->zones);
	return err;
}

static void mlx4_cleanup_qp_zones(struct mlx4_dev *dev)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;

	if (qp_table->zones) {
		int i;

		for (i = 0;
		     i < sizeof(qp_table->zones_uids)/sizeof(qp_table->zones_uids[0]);
		     i++) {
			struct mlx4_bitmap *bitmap =
				mlx4_zone_get_bitmap(qp_table->zones,
						     qp_table->zones_uids[i]);

			mlx4_zone_remove_one(qp_table->zones, qp_table->zones_uids[i]);
			if (NULL == bitmap)
				continue;

			mlx4_bitmap_cleanup(bitmap);
		}
		mlx4_zone_allocator_destroy(qp_table->zones);
		kfree(qp_table->bitmap_gen);
		qp_table->bitmap_gen = NULL;
		qp_table->zones = NULL;
	}
}

int mlx4_init_qp_table(struct mlx4_dev *dev)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	int err;
	int reserved_from_top = 0;
	int reserved_from_bot;
	int k;
	int fixed_reserved_from_bot_rv = 0;
	int bottom_reserved_for_rss_bitmap;
	u32 max_table_offset = dev->caps.dmfs_high_rate_qpn_base +
			dev->caps.dmfs_high_rate_qpn_range;

	spin_lock_init(&qp_table->lock);
	INIT_RADIX_TREE(&dev->qp_table_tree, GFP_ATOMIC);
	if (mlx4_is_slave(dev))
		return 0;

	/* We reserve 2 extra QPs per port for the special QPs.  The
	 * block of special QPs must be aligned to a multiple of 8, so
	 * round up.
	 *
	 * We also reserve the MSB of the 24-bit QP number to indicate
	 * that a QP is an XRC QP.
	 */
	for (k = 0; k <= MLX4_QP_REGION_BOTTOM; k++)
		fixed_reserved_from_bot_rv += dev->caps.reserved_qps_cnt[k];

	if (fixed_reserved_from_bot_rv < max_table_offset)
		fixed_reserved_from_bot_rv = max_table_offset;

	/* We reserve at least 1 extra for bitmaps that we don't have enough space for*/
	bottom_reserved_for_rss_bitmap =
		roundup_pow_of_two(fixed_reserved_from_bot_rv + 1);
	dev->phys_caps.base_sqpn = ALIGN(bottom_reserved_for_rss_bitmap, 8);

	{
		int sort[MLX4_NUM_QP_REGION];
		int i, j;
		int last_base = dev->caps.num_qps;

		for (i = 1; i < MLX4_NUM_QP_REGION; ++i)
			sort[i] = i;

		for (i = MLX4_NUM_QP_REGION; i > MLX4_QP_REGION_BOTTOM; --i) {
			for (j = MLX4_QP_REGION_BOTTOM + 2; j < i; ++j) {
				if (dev->caps.reserved_qps_cnt[sort[j]] >
				    dev->caps.reserved_qps_cnt[sort[j - 1]])
					swap(sort[j], sort[j - 1]);
			}
		}

		for (i = MLX4_QP_REGION_BOTTOM + 1; i < MLX4_NUM_QP_REGION; ++i) {
			last_base -= dev->caps.reserved_qps_cnt[sort[i]];
			dev->caps.reserved_qps_base[sort[i]] = last_base;
			reserved_from_top +=
				dev->caps.reserved_qps_cnt[sort[i]];
		}
	}

       /* Reserve 8 real SQPs in both native and SRIOV modes.
	* In addition, in SRIOV mode, reserve 8 proxy SQPs per function
	* (for all PFs and VFs), and 8 corresponding tunnel QPs.
	* Each proxy SQP works opposite its own tunnel QP.
	*
	* The QPs are arranged as follows:
	* a. 8 real SQPs
	* b. All the proxy SQPs (8 per function)
	* c. All the tunnel QPs (8 per function)
	*/
	reserved_from_bot = mlx4_num_reserved_sqps(dev);
	if (reserved_from_bot + reserved_from_top > dev->caps.num_qps) {
		mlx4_err(dev, "Number of reserved QPs is higher than number of QPs\n");
		return -EINVAL;
	}

	err = mlx4_create_zones(dev, reserved_from_bot, reserved_from_bot,
				bottom_reserved_for_rss_bitmap,
				fixed_reserved_from_bot_rv,
				max_table_offset);

	if (err)
		return err;

	if (mlx4_is_mfunc(dev)) {
		/* for PPF use */
		dev->phys_caps.base_proxy_sqpn = dev->phys_caps.base_sqpn + 8;
		dev->phys_caps.base_tunnel_sqpn = dev->phys_caps.base_sqpn + 8 + 8 * MLX4_MFUNC_MAX;

		/* In mfunc, calculate proxy and tunnel qp offsets for the PF here,
		 * since the PF does not call mlx4_slave_caps */
		dev->caps.spec_qps = kcalloc(dev->caps.num_ports,
					     sizeof(*dev->caps.spec_qps),
					     GFP_KERNEL);
		if (!dev->caps.spec_qps) {
			err = -ENOMEM;
			goto err_mem;
		}

		for (k = 0; k < dev->caps.num_ports; k++) {
			dev->caps.spec_qps[k].qp0_proxy = dev->phys_caps.base_proxy_sqpn +
				8 * mlx4_master_func_num(dev) + k;
			dev->caps.spec_qps[k].qp0_tunnel = dev->caps.spec_qps[k].qp0_proxy + 8 * MLX4_MFUNC_MAX;
			dev->caps.spec_qps[k].qp1_proxy = dev->phys_caps.base_proxy_sqpn +
				8 * mlx4_master_func_num(dev) + MLX4_MAX_PORTS + k;
			dev->caps.spec_qps[k].qp1_tunnel = dev->caps.spec_qps[k].qp1_proxy + 8 * MLX4_MFUNC_MAX;
		}
	}


	err = mlx4_CONF_SPECIAL_QP(dev, dev->phys_caps.base_sqpn);
	if (err)
		goto err_mem;

	return err;

err_mem:
	kfree(dev->caps.spec_qps);
	dev->caps.spec_qps = NULL;
	mlx4_cleanup_qp_zones(dev);
	return err;
}

void mlx4_cleanup_qp_table(struct mlx4_dev *dev)
{
	if (mlx4_is_slave(dev))
		return;

	mlx4_CONF_SPECIAL_QP(dev, 0);

	mlx4_cleanup_qp_zones(dev);
}

int mlx4_qp_query(struct mlx4_dev *dev, struct mlx4_qp *qp,
		  struct mlx4_qp_context *context)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	err = mlx4_cmd_box(dev, 0, mailbox->dma, qp->qpn, 0,
			   MLX4_CMD_QUERY_QP, MLX4_CMD_TIME_CLASS_A,
			   MLX4_CMD_WRAPPED);
	if (!err)
		memcpy(context, mailbox->buf + 8, sizeof(*context));

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_qp_query);

int mlx4_qp_to_ready(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		     struct mlx4_qp_context *context,
		     struct mlx4_qp *qp, enum mlx4_qp_state *qp_state)
{
	int err;
	int i;
	enum mlx4_qp_state states[] = {
		MLX4_QP_STATE_RST,
		MLX4_QP_STATE_INIT,
		MLX4_QP_STATE_RTR,
		MLX4_QP_STATE_RTS
	};

	for (i = 0; i < ARRAY_SIZE(states) - 1; i++) {
		context->flags &= cpu_to_be32(~(0xf << 28));
		context->flags |= cpu_to_be32(states[i + 1] << 28);
		if (states[i + 1] != MLX4_QP_STATE_RTR)
			context->params2 &= ~cpu_to_be32(MLX4_QP_BIT_FPP);
		err = mlx4_qp_modify(dev, mtt, states[i], states[i + 1],
				     context, 0, 0, qp);
		if (err) {
			mlx4_err(dev, "Failed to bring QP to state: %d with error: %d\n",
				 states[i + 1], err);
			return err;
		}

		*qp_state = states[i + 1];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_qp_to_ready);

u16 mlx4_qp_roce_entropy(struct mlx4_dev *dev, u32 qpn)
{
	struct mlx4_qp_context context;
	struct mlx4_qp qp;
	int err;

	qp.qpn = qpn;
	err = mlx4_qp_query(dev, &qp, &context);
	if (!err) {
		u32 dest_qpn = be32_to_cpu(context.remote_qpn) & 0xffffff;
		u16 folded_dst = folded_qp(dest_qpn);
		u16 folded_src = folded_qp(qpn);

		return (dest_qpn != qpn) ?
			((folded_dst ^ folded_src) | 0xC000) :
			folded_src | 0xC000;
	}
	return 0xdead;
}
