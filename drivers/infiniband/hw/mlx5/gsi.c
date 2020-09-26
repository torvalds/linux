/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#include "mlx5_ib.h"

struct mlx5_ib_gsi_wr {
	struct ib_cqe cqe;
	struct ib_wc wc;
	int send_flags;
	bool completed:1;
};

static bool mlx5_ib_deth_sqpn_cap(struct mlx5_ib_dev *dev)
{
	return MLX5_CAP_GEN(dev->mdev, set_deth_sqpn);
}

/* Call with gsi->lock locked */
static void generate_completions(struct mlx5_ib_qp *mqp)
{
	struct mlx5_ib_gsi_qp *gsi = &mqp->gsi;
	struct ib_cq *gsi_cq = mqp->ibqp.send_cq;
	struct mlx5_ib_gsi_wr *wr;
	u32 index;

	for (index = gsi->outstanding_ci; index != gsi->outstanding_pi;
	     index++) {
		wr = &gsi->outstanding_wrs[index % gsi->cap.max_send_wr];

		if (!wr->completed)
			break;

		if (gsi->sq_sig_type == IB_SIGNAL_ALL_WR ||
		    wr->send_flags & IB_SEND_SIGNALED)
			WARN_ON_ONCE(mlx5_ib_generate_wc(gsi_cq, &wr->wc));

		wr->completed = false;
	}

	gsi->outstanding_ci = index;
}

static void handle_single_completion(struct ib_cq *cq, struct ib_wc *wc)
{
	struct mlx5_ib_gsi_qp *gsi = cq->cq_context;
	struct mlx5_ib_gsi_wr *wr =
		container_of(wc->wr_cqe, struct mlx5_ib_gsi_wr, cqe);
	struct mlx5_ib_qp *mqp = container_of(gsi, struct mlx5_ib_qp, gsi);
	u64 wr_id;
	unsigned long flags;

	spin_lock_irqsave(&gsi->lock, flags);
	wr->completed = true;
	wr_id = wr->wc.wr_id;
	wr->wc = *wc;
	wr->wc.wr_id = wr_id;
	wr->wc.qp = &mqp->ibqp;

	generate_completions(mqp);
	spin_unlock_irqrestore(&gsi->lock, flags);
}

int mlx5_ib_create_gsi(struct ib_pd *pd, struct mlx5_ib_qp *mqp,
		       struct ib_qp_init_attr *attr)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_gsi_qp *gsi;
	struct ib_qp_init_attr hw_init_attr = *attr;
	const u8 port_num = attr->port_num;
	int num_qps = 0;
	int ret;

	if (mlx5_ib_deth_sqpn_cap(dev)) {
		if (MLX5_CAP_GEN(dev->mdev,
				 port_type) == MLX5_CAP_PORT_TYPE_IB)
			num_qps = pd->device->attrs.max_pkeys;
		else if (dev->lag_active)
			num_qps = MLX5_MAX_PORTS;
	}

	gsi = &mqp->gsi;
	gsi->tx_qps = kcalloc(num_qps, sizeof(*gsi->tx_qps), GFP_KERNEL);
	if (!gsi->tx_qps)
		return -ENOMEM;

	gsi->outstanding_wrs =
		kcalloc(attr->cap.max_send_wr, sizeof(*gsi->outstanding_wrs),
			GFP_KERNEL);
	if (!gsi->outstanding_wrs) {
		ret = -ENOMEM;
		goto err_free_tx;
	}

	mutex_lock(&dev->devr.mutex);

	if (dev->devr.ports[port_num - 1].gsi) {
		mlx5_ib_warn(dev, "GSI QP already exists on port %d\n",
			     port_num);
		ret = -EBUSY;
		goto err_free_wrs;
	}
	gsi->num_qps = num_qps;
	spin_lock_init(&gsi->lock);

	gsi->cap = attr->cap;
	gsi->sq_sig_type = attr->sq_sig_type;
	gsi->port_num = port_num;

	gsi->cq = ib_alloc_cq(pd->device, gsi, attr->cap.max_send_wr, 0,
			      IB_POLL_SOFTIRQ);
	if (IS_ERR(gsi->cq)) {
		mlx5_ib_warn(dev, "unable to create send CQ for GSI QP. error %ld\n",
			     PTR_ERR(gsi->cq));
		ret = PTR_ERR(gsi->cq);
		goto err_free_wrs;
	}

	hw_init_attr.qp_type = MLX5_IB_QPT_HW_GSI;
	hw_init_attr.send_cq = gsi->cq;
	if (num_qps) {
		hw_init_attr.cap.max_send_wr = 0;
		hw_init_attr.cap.max_send_sge = 0;
		hw_init_attr.cap.max_inline_data = 0;
	}

	gsi->rx_qp = mlx5_ib_create_qp(pd, &hw_init_attr, NULL);
	if (IS_ERR(gsi->rx_qp)) {
		mlx5_ib_warn(dev, "unable to create hardware GSI QP. error %ld\n",
			     PTR_ERR(gsi->rx_qp));
		ret = PTR_ERR(gsi->rx_qp);
		goto err_destroy_cq;
	}
	gsi->rx_qp->device = pd->device;
	gsi->rx_qp->pd = pd;
	gsi->rx_qp->real_qp = gsi->rx_qp;

	gsi->rx_qp->qp_type = hw_init_attr.qp_type;
	gsi->rx_qp->send_cq = hw_init_attr.send_cq;
	gsi->rx_qp->recv_cq = hw_init_attr.recv_cq;
	gsi->rx_qp->event_handler = hw_init_attr.event_handler;
	spin_lock_init(&gsi->rx_qp->mr_lock);
	INIT_LIST_HEAD(&gsi->rx_qp->rdma_mrs);
	INIT_LIST_HEAD(&gsi->rx_qp->sig_mrs);

	dev->devr.ports[attr->port_num - 1].gsi = gsi;

	mutex_unlock(&dev->devr.mutex);

	return 0;

err_destroy_cq:
	ib_free_cq(gsi->cq);
err_free_wrs:
	mutex_unlock(&dev->devr.mutex);
	kfree(gsi->outstanding_wrs);
err_free_tx:
	kfree(gsi->tx_qps);
	return ret;
}

int mlx5_ib_destroy_gsi(struct mlx5_ib_qp *mqp)
{
	struct mlx5_ib_dev *dev = to_mdev(mqp->ibqp.device);
	struct mlx5_ib_gsi_qp *gsi = &mqp->gsi;
	const int port_num = gsi->port_num;
	int qp_index;
	int ret;

	mutex_lock(&dev->devr.mutex);
	ret = mlx5_ib_destroy_qp(gsi->rx_qp, NULL);
	if (ret) {
		mlx5_ib_warn(dev, "unable to destroy hardware GSI QP. error %d\n",
			     ret);
		mutex_unlock(&dev->devr.mutex);
		return ret;
	}
	dev->devr.ports[port_num - 1].gsi = NULL;
	mutex_unlock(&dev->devr.mutex);
	gsi->rx_qp = NULL;

	for (qp_index = 0; qp_index < gsi->num_qps; ++qp_index) {
		if (!gsi->tx_qps[qp_index])
			continue;
		WARN_ON_ONCE(ib_destroy_qp(gsi->tx_qps[qp_index]));
		gsi->tx_qps[qp_index] = NULL;
	}

	ib_free_cq(gsi->cq);

	kfree(gsi->outstanding_wrs);
	kfree(gsi->tx_qps);
	kfree(mqp);

	return 0;
}

static struct ib_qp *create_gsi_ud_qp(struct mlx5_ib_gsi_qp *gsi)
{
	struct ib_pd *pd = gsi->rx_qp->pd;
	struct ib_qp_init_attr init_attr = {
		.event_handler = gsi->rx_qp->event_handler,
		.qp_context = gsi->rx_qp->qp_context,
		.send_cq = gsi->cq,
		.recv_cq = gsi->rx_qp->recv_cq,
		.cap = {
			.max_send_wr = gsi->cap.max_send_wr,
			.max_send_sge = gsi->cap.max_send_sge,
			.max_inline_data = gsi->cap.max_inline_data,
		},
		.sq_sig_type = gsi->sq_sig_type,
		.qp_type = IB_QPT_UD,
		.create_flags = MLX5_IB_QP_CREATE_SQPN_QP1,
	};

	return ib_create_qp(pd, &init_attr);
}

static int modify_to_rts(struct mlx5_ib_gsi_qp *gsi, struct ib_qp *qp,
			 u16 pkey_index)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct ib_qp_attr attr;
	int mask;
	int ret;

	mask = IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_QKEY | IB_QP_PORT;
	attr.qp_state = IB_QPS_INIT;
	attr.pkey_index = pkey_index;
	attr.qkey = IB_QP1_QKEY;
	attr.port_num = gsi->port_num;
	ret = ib_modify_qp(qp, &attr, mask);
	if (ret) {
		mlx5_ib_err(dev, "could not change QP%d state to INIT: %d\n",
			    qp->qp_num, ret);
		return ret;
	}

	attr.qp_state = IB_QPS_RTR;
	ret = ib_modify_qp(qp, &attr, IB_QP_STATE);
	if (ret) {
		mlx5_ib_err(dev, "could not change QP%d state to RTR: %d\n",
			    qp->qp_num, ret);
		return ret;
	}

	attr.qp_state = IB_QPS_RTS;
	attr.sq_psn = 0;
	ret = ib_modify_qp(qp, &attr, IB_QP_STATE | IB_QP_SQ_PSN);
	if (ret) {
		mlx5_ib_err(dev, "could not change QP%d state to RTS: %d\n",
			    qp->qp_num, ret);
		return ret;
	}

	return 0;
}

static void setup_qp(struct mlx5_ib_gsi_qp *gsi, u16 qp_index)
{
	struct ib_device *device = gsi->rx_qp->device;
	struct mlx5_ib_dev *dev = to_mdev(device);
	int pkey_index = qp_index;
	struct mlx5_ib_qp *mqp;
	struct ib_qp *qp;
	unsigned long flags;
	u16 pkey;
	int ret;

	if (MLX5_CAP_GEN(dev->mdev,  port_type) != MLX5_CAP_PORT_TYPE_IB)
		pkey_index = 0;

	ret = ib_query_pkey(device, gsi->port_num, pkey_index, &pkey);
	if (ret) {
		mlx5_ib_warn(dev, "unable to read P_Key at port %d, index %d\n",
			     gsi->port_num, qp_index);
		return;
	}

	if (!pkey) {
		mlx5_ib_dbg(dev, "invalid P_Key at port %d, index %d.  Skipping.\n",
			    gsi->port_num, qp_index);
		return;
	}

	spin_lock_irqsave(&gsi->lock, flags);
	qp = gsi->tx_qps[qp_index];
	spin_unlock_irqrestore(&gsi->lock, flags);
	if (qp) {
		mlx5_ib_dbg(dev, "already existing GSI TX QP at port %d, index %d. Skipping\n",
			    gsi->port_num, qp_index);
		return;
	}

	qp = create_gsi_ud_qp(gsi);
	if (IS_ERR(qp)) {
		mlx5_ib_warn(dev, "unable to create hardware UD QP for GSI: %ld\n",
			     PTR_ERR(qp));
		return;
	}

	mqp = to_mqp(qp);
	if (dev->lag_active)
		mqp->gsi_lag_port = qp_index + 1;
	ret = modify_to_rts(gsi, qp, pkey_index);
	if (ret)
		goto err_destroy_qp;

	spin_lock_irqsave(&gsi->lock, flags);
	WARN_ON_ONCE(gsi->tx_qps[qp_index]);
	gsi->tx_qps[qp_index] = qp;
	spin_unlock_irqrestore(&gsi->lock, flags);

	return;

err_destroy_qp:
	WARN_ON_ONCE(qp);
}

static void setup_qps(struct mlx5_ib_gsi_qp *gsi)
{
	struct mlx5_ib_dev *dev = to_mdev(gsi->rx_qp->device);
	u16 qp_index;

	mutex_lock(&dev->devr.mutex);
	for (qp_index = 0; qp_index < gsi->num_qps; ++qp_index)
		setup_qp(gsi, qp_index);
	mutex_unlock(&dev->devr.mutex);
}

int mlx5_ib_gsi_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
			  int attr_mask)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_ib_qp *mqp = to_mqp(qp);
	struct mlx5_ib_gsi_qp *gsi = &mqp->gsi;
	int ret;

	mlx5_ib_dbg(dev, "modifying GSI QP to state %d\n", attr->qp_state);

	ret = ib_modify_qp(gsi->rx_qp, attr, attr_mask);
	if (ret) {
		mlx5_ib_warn(dev, "unable to modify GSI rx QP: %d\n", ret);
		return ret;
	}

	if (to_mqp(gsi->rx_qp)->state == IB_QPS_RTS)
		setup_qps(gsi);
	return 0;
}

int mlx5_ib_gsi_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
			 int qp_attr_mask,
			 struct ib_qp_init_attr *qp_init_attr)
{
	struct mlx5_ib_qp *mqp = to_mqp(qp);
	struct mlx5_ib_gsi_qp *gsi = &mqp->gsi;
	int ret;

	ret = ib_query_qp(gsi->rx_qp, qp_attr, qp_attr_mask, qp_init_attr);
	qp_init_attr->cap = gsi->cap;
	return ret;
}

/* Call with gsi->lock locked */
static int mlx5_ib_add_outstanding_wr(struct mlx5_ib_qp *mqp,
				      struct ib_ud_wr *wr, struct ib_wc *wc)
{
	struct mlx5_ib_gsi_qp *gsi = &mqp->gsi;
	struct mlx5_ib_dev *dev = to_mdev(gsi->rx_qp->device);
	struct mlx5_ib_gsi_wr *gsi_wr;

	if (gsi->outstanding_pi == gsi->outstanding_ci + gsi->cap.max_send_wr) {
		mlx5_ib_warn(dev, "no available GSI work request.\n");
		return -ENOMEM;
	}

	gsi_wr = &gsi->outstanding_wrs[gsi->outstanding_pi %
				       gsi->cap.max_send_wr];
	gsi->outstanding_pi++;

	if (!wc) {
		memset(&gsi_wr->wc, 0, sizeof(gsi_wr->wc));
		gsi_wr->wc.pkey_index = wr->pkey_index;
		gsi_wr->wc.wr_id = wr->wr.wr_id;
	} else {
		gsi_wr->wc = *wc;
		gsi_wr->completed = true;
	}

	gsi_wr->cqe.done = &handle_single_completion;
	wr->wr.wr_cqe = &gsi_wr->cqe;

	return 0;
}

/* Call with gsi->lock locked */
static int mlx5_ib_gsi_silent_drop(struct mlx5_ib_qp *mqp, struct ib_ud_wr *wr)
{
	struct ib_wc wc = {
		{ .wr_id = wr->wr.wr_id },
		.status = IB_WC_SUCCESS,
		.opcode = IB_WC_SEND,
		.qp = &mqp->ibqp,
	};
	int ret;

	ret = mlx5_ib_add_outstanding_wr(mqp, wr, &wc);
	if (ret)
		return ret;

	generate_completions(mqp);

	return 0;
}

/* Call with gsi->lock locked */
static struct ib_qp *get_tx_qp(struct mlx5_ib_gsi_qp *gsi, struct ib_ud_wr *wr)
{
	struct mlx5_ib_dev *dev = to_mdev(gsi->rx_qp->device);
	struct mlx5_ib_ah *ah = to_mah(wr->ah);
	int qp_index = wr->pkey_index;

	if (!gsi->num_qps)
		return gsi->rx_qp;

	if (dev->lag_active && ah->xmit_port)
		qp_index = ah->xmit_port - 1;

	if (qp_index >= gsi->num_qps)
		return NULL;

	return gsi->tx_qps[qp_index];
}

int mlx5_ib_gsi_post_send(struct ib_qp *qp, const struct ib_send_wr *wr,
			  const struct ib_send_wr **bad_wr)
{
	struct mlx5_ib_qp *mqp = to_mqp(qp);
	struct mlx5_ib_gsi_qp *gsi = &mqp->gsi;
	struct ib_qp *tx_qp;
	unsigned long flags;
	int ret;

	for (; wr; wr = wr->next) {
		struct ib_ud_wr cur_wr = *ud_wr(wr);

		cur_wr.wr.next = NULL;

		spin_lock_irqsave(&gsi->lock, flags);
		tx_qp = get_tx_qp(gsi, &cur_wr);
		if (!tx_qp) {
			ret = mlx5_ib_gsi_silent_drop(mqp, &cur_wr);
			if (ret)
				goto err;
			spin_unlock_irqrestore(&gsi->lock, flags);
			continue;
		}

		ret = mlx5_ib_add_outstanding_wr(mqp, &cur_wr, NULL);
		if (ret)
			goto err;

		ret = ib_post_send(tx_qp, &cur_wr.wr, bad_wr);
		if (ret) {
			/* Undo the effect of adding the outstanding wr */
			gsi->outstanding_pi--;
			goto err;
		}
		spin_unlock_irqrestore(&gsi->lock, flags);
	}

	return 0;

err:
	spin_unlock_irqrestore(&gsi->lock, flags);
	*bad_wr = wr;
	return ret;
}

int mlx5_ib_gsi_post_recv(struct ib_qp *qp, const struct ib_recv_wr *wr,
			  const struct ib_recv_wr **bad_wr)
{
	struct mlx5_ib_qp *mqp = to_mqp(qp);
	struct mlx5_ib_gsi_qp *gsi = &mqp->gsi;

	return ib_post_recv(gsi->rx_qp, wr, bad_wr);
}

void mlx5_ib_gsi_pkey_change(struct mlx5_ib_gsi_qp *gsi)
{
	if (!gsi)
		return;

	setup_qps(gsi);
}
