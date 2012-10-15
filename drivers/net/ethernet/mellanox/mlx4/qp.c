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
#include <linux/init.h>

#include <linux/mlx4/cmd.h>
#include <linux/mlx4/qp.h>

#include "mlx4.h"
#include "icm.h"

void mlx4_qp_event(struct mlx4_dev *dev, u32 qpn, int event_type)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	struct mlx4_qp *qp;

	spin_lock(&qp_table->lock);

	qp = __mlx4_qp_lookup(dev, qpn);
	if (qp)
		atomic_inc(&qp->refcount);

	spin_unlock(&qp_table->lock);

	if (!qp) {
		mlx4_dbg(dev, "Async event for none existent QP %08x\n", qpn);
		return;
	}

	qp->event(qp, event_type);

	if (atomic_dec_and_test(&qp->refcount))
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

	*(__be32 *) mailbox->buf = cpu_to_be32(optpar);
	memcpy(mailbox->buf + 8, context, sizeof *context);

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
				   int *base)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_qp_table *qp_table = &priv->qp_table;

	*base = mlx4_bitmap_alloc_range(&qp_table->bitmap, cnt, align);
	if (*base == -1)
		return -ENOMEM;

	return 0;
}

int mlx4_qp_reserve_range(struct mlx4_dev *dev, int cnt, int align, int *base)
{
	u64 in_param;
	u64 out_param;
	int err;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, cnt);
		set_param_h(&in_param, align);
		err = mlx4_cmd_imm(dev, in_param, &out_param,
				   RES_QP, RES_OP_RESERVE,
				   MLX4_CMD_ALLOC_RES,
				   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		if (err)
			return err;

		*base = get_param_l(&out_param);
		return 0;
	}
	return __mlx4_qp_reserve_range(dev, cnt, align, base);
}
EXPORT_SYMBOL_GPL(mlx4_qp_reserve_range);

void __mlx4_qp_release_range(struct mlx4_dev *dev, int base_qpn, int cnt)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_qp_table *qp_table = &priv->qp_table;

	if (mlx4_is_qp_reserved(dev, (u32) base_qpn))
		return;
	mlx4_bitmap_free_range(&qp_table->bitmap, base_qpn, cnt);
}

void mlx4_qp_release_range(struct mlx4_dev *dev, int base_qpn, int cnt)
{
	u64 in_param;
	int err;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, base_qpn);
		set_param_h(&in_param, cnt);
		err = mlx4_cmd(dev, in_param, RES_QP, RES_OP_RESERVE,
			       MLX4_CMD_FREE_RES,
			       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		if (err) {
			mlx4_warn(dev, "Failed to release qp range"
				  " base:%d cnt:%d\n", base_qpn, cnt);
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
	u64 param;

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
	u64 in_param;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, qpn);
		if (mlx4_cmd(dev, in_param, RES_QP, RES_OP_MAP_ICM,
			     MLX4_CMD_FREE_RES, MLX4_CMD_TIME_CLASS_A,
			     MLX4_CMD_WRAPPED))
			mlx4_warn(dev, "Failed to free icm of qp:%d\n", qpn);
	} else
		__mlx4_qp_free_icm(dev, qpn);
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

	atomic_set(&qp->refcount, 1);
	init_completion(&qp->free);

	return 0;

err_icm:
	mlx4_qp_free_icm(dev, qpn);
	return err;
}

EXPORT_SYMBOL_GPL(mlx4_qp_alloc);

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
	if (atomic_dec_and_test(&qp->refcount))
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

int mlx4_init_qp_table(struct mlx4_dev *dev)
{
	struct mlx4_qp_table *qp_table = &mlx4_priv(dev)->qp_table;
	int err;
	int reserved_from_top = 0;
	int k;

	spin_lock_init(&qp_table->lock);
	INIT_RADIX_TREE(&dev->qp_table_tree, GFP_ATOMIC);
	if (mlx4_is_slave(dev))
		return 0;

	/*
	 * We reserve 2 extra QPs per port for the special QPs.  The
	 * block of special QPs must be aligned to a multiple of 8, so
	 * round up.
	 *
	 * We also reserve the MSB of the 24-bit QP number to indicate
	 * that a QP is an XRC QP.
	 */
	dev->phys_caps.base_sqpn =
		ALIGN(dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW], 8);

	{
		int sort[MLX4_NUM_QP_REGION];
		int i, j, tmp;
		int last_base = dev->caps.num_qps;

		for (i = 1; i < MLX4_NUM_QP_REGION; ++i)
			sort[i] = i;

		for (i = MLX4_NUM_QP_REGION; i > 0; --i) {
			for (j = 2; j < i; ++j) {
				if (dev->caps.reserved_qps_cnt[sort[j]] >
				    dev->caps.reserved_qps_cnt[sort[j - 1]]) {
					tmp             = sort[j];
					sort[j]         = sort[j - 1];
					sort[j - 1]     = tmp;
				}
			}
		}

		for (i = 1; i < MLX4_NUM_QP_REGION; ++i) {
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

	err = mlx4_bitmap_init(&qp_table->bitmap, dev->caps.num_qps,
			       (1 << 23) - 1, dev->phys_caps.base_sqpn + 8 +
			       16 * MLX4_MFUNC_MAX * !!mlx4_is_master(dev),
			       reserved_from_top);
	if (err)
		return err;

	if (mlx4_is_mfunc(dev)) {
		/* for PPF use */
		dev->phys_caps.base_proxy_sqpn = dev->phys_caps.base_sqpn + 8;
		dev->phys_caps.base_tunnel_sqpn = dev->phys_caps.base_sqpn + 8 + 8 * MLX4_MFUNC_MAX;

		/* In mfunc, calculate proxy and tunnel qp offsets for the PF here,
		 * since the PF does not call mlx4_slave_caps */
		dev->caps.qp0_tunnel = kcalloc(dev->caps.num_ports, sizeof (u32), GFP_KERNEL);
		dev->caps.qp0_proxy = kcalloc(dev->caps.num_ports, sizeof (u32), GFP_KERNEL);
		dev->caps.qp1_tunnel = kcalloc(dev->caps.num_ports, sizeof (u32), GFP_KERNEL);
		dev->caps.qp1_proxy = kcalloc(dev->caps.num_ports, sizeof (u32), GFP_KERNEL);

		if (!dev->caps.qp0_tunnel || !dev->caps.qp0_proxy ||
		    !dev->caps.qp1_tunnel || !dev->caps.qp1_proxy) {
			err = -ENOMEM;
			goto err_mem;
		}

		for (k = 0; k < dev->caps.num_ports; k++) {
			dev->caps.qp0_proxy[k] = dev->phys_caps.base_proxy_sqpn +
				8 * mlx4_master_func_num(dev) + k;
			dev->caps.qp0_tunnel[k] = dev->caps.qp0_proxy[k] + 8 * MLX4_MFUNC_MAX;
			dev->caps.qp1_proxy[k] = dev->phys_caps.base_proxy_sqpn +
				8 * mlx4_master_func_num(dev) + MLX4_MAX_PORTS + k;
			dev->caps.qp1_tunnel[k] = dev->caps.qp1_proxy[k] + 8 * MLX4_MFUNC_MAX;
		}
	}


	err = mlx4_CONF_SPECIAL_QP(dev, dev->phys_caps.base_sqpn);
	if (err)
		goto err_mem;
	return 0;

err_mem:
	kfree(dev->caps.qp0_tunnel);
	kfree(dev->caps.qp0_proxy);
	kfree(dev->caps.qp1_tunnel);
	kfree(dev->caps.qp1_proxy);
	dev->caps.qp0_tunnel = dev->caps.qp0_proxy =
		dev->caps.qp1_tunnel = dev->caps.qp1_proxy = NULL;
	return err;
}

void mlx4_cleanup_qp_table(struct mlx4_dev *dev)
{
	if (mlx4_is_slave(dev))
		return;

	mlx4_CONF_SPECIAL_QP(dev, 0);
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->qp_table.bitmap);
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
		memcpy(context, mailbox->buf + 8, sizeof *context);

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
		err = mlx4_qp_modify(dev, mtt, states[i], states[i + 1],
				     context, 0, 0, qp);
		if (err) {
			mlx4_err(dev, "Failed to bring QP to state: "
				 "%d with error: %d\n",
				 states[i + 1], err);
			return err;
		}

		*qp_state = states[i + 1];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_qp_to_ready);
