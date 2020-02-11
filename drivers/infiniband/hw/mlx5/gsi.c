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

struct mlx5_ib_gsi_qp {
	struct ib_qp ibqp;
	struct ib_qp *rx_qp;
	u8 port_num;
	struct ib_qp_cap cap;
	enum ib_sig_type sq_sig_type;
	/* Serialize qp state modifications */
	struct mutex mutex;
	struct ib_cq *cq;
	struct mlx5_ib_gsi_wr *outstanding_wrs;
	u32 outstanding_pi, outstanding_ci;
	int num_qps;
	/* Protects access to the tx_qps. Post send operations synchronize
	 * with tx_qp creation in setup_qp(). Also protects the
	 * outstanding_wrs array and indices.
	 */
	spinlock_t lock;
	struct ib_qp **tx_qps;
};

static struct mlx5_ib_gsi_qp *gsi_qp(struct ib_qp *qp)
{
	return container_of(qp, struct mlx5_ib_gsi_qp, ibqp);
}

static bool mlx5_ib_deth_sqpn_cap(struct mlx5_ib_dev *dev)
{
	return MLX5_CAP_GEN(dev->mdev, set_deth_sqpn);
}

/* Call with gsi->lock locked */
static void generate_completions(struct mlx5_ib_gsi_qp *gsi)
{
	struct ib_cq *gsi_cq = gsi->ibqp.send_cq;
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
	u64 wr_id;
	unsigned long flags;

	spin_lock_irqsave(&gsi->lock, flags);
	wr->completed = true;
	wr_id = wr->wc.wr_id;
	wr->wc = *wc;
	wr->wc.wr_id = wr_id;
	wr->wc.qp = &gsi->ibqp;

	generate_completions(gsi);
	spin_unlock_irqrestore(&gsi->lock, flags);
}

struct ib_qp *mlx5_ib_gsi_create_qp(struct ib_pd *pd,
				    struct ib_qp_init_attr *init_attr)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_gsi_qp *gsi;
	struct ib_qp_init_attr hw_init_attr = *init_attr;
	const u8 port_num = init_attr->port_num;
	const int num_pkeys = pd->device->attrs.max_pkeys;
	const int num_qps = mlx5_ib_deth_sqpn_cap(dev) ? num_pkeys : 0;
	int ret;

	mlx5_ib_dbg(dev, "creating GSI QP\n");

	if (port_num > ARRAY_SIZE(dev->devr.ports) || port_num < 1) {
		mlx5_ib_warn(dev,
			     "invalid port number %d during GSI QP creation\n",
			     port_num);
		return ERR_PTR(-EINVAL);
	}

	gsi = kzalloc(sizeof(*gsi), GFP_KERNEL);
	if (!gsi)
		return ERR_PTR(-ENOMEM);

	gsi->tx_qps = kcalloc(num_qps, sizeof(*gsi->tx_qps), GFP_KERNEL);
	if (!gsi->tx_qps) {
		ret = -ENOMEM;
		goto err_free;
	}

	gsi->outstanding_wrs = kcalloc(init_attr->cap.max_send_wr,
				       sizeof(*gsi->outstanding_wrs),
				       GFP_KERNEL);
	if (!gsi->outstanding_wrs) {
		ret = -ENOMEM;
		goto err_free_tx;
	}

	mutex_init(&gsi->mutex);

	mutex_lock(&dev->devr.mutex);

	if (dev->devr.ports[port_num - 1].gsi) {
		mlx5_ib_warn(dev, "GSI QP already exists on port %d\n",
			     port_num);
		ret = -EBUSY;
		goto err_free_wrs;
	}
	gsi->num_qps = num_qps;
	spin_lock_init(&gsi->lock);

	gsi->cap = init_attr->cap;
	gsi->sq_sig_type = init_attr->sq_sig_type;
	gsi->ibqp.qp_num = 1;
	gsi->port_num = port_num;

	gsi->cq = ib_alloc_cq(pd->device, gsi, init_attr->cap.max_send_wr, 0,
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
	gsi->rx_qp = ib_create_qp(pd, &hw_init_attr);
	if (IS_ERR(gsi->rx_qp)) {
		mlx5_ib_warn(dev, "unable to create hardware GSI QP. error %ld\n",
			     PTR_ERR(gsi->rx_qp));
		ret = PTR_ERR(gsi->rx_qp);
		goto err_destroy_cq;
	}

	dev->devr.ports[init_attr->port_num - 1].gsi = gsi;

	mutex_unlock(&dev->devr.mutex);

	return &gsi->ibqp;

err_destroy_cq:
	ib_free_cq(gsi->cq);
err_free_wrs:
	mutex_unlock(&dev->devr.mutex);
	kfree(gsi->outstanding_wrs);
err_free_tx:
	kfree(gsi->tx_qps);
err_free:
	kfree(gsi);
	return ERR_PTR(ret);
}

int mlx5_ib_gsi_destroy_qp(struct ib_qp *qp)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);
	const int port_num = gsi->port_num;
	int qp_index;
	int ret;

	mlx5_ib_dbg(dev, "destroying GSI QP\n");

	mutex_lock(&dev->devr.mutex);
	ret = ib_destroy_qp(gsi->rx_qp);
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
	kfree(gsi);

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
		.create_flags = mlx5_ib_create_qp_sqpn_qp1(),
	};

	return ib_create_qp(pd, &init_attr);
}

static int modify_to_rts(struct mlx5_ib_gsi_qp *gsi, struct ib_qp *qp,
			 u16 qp_index)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct ib_qp_attr attr;
	int mask;
	int ret;

	mask = IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_QKEY | IB_QP_PORT;
	attr.qp_state = IB_QPS_INIT;
	attr.pkey_index = qp_index;
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
	struct ib_qp *qp;
	unsigned long flags;
	u16 pkey;
	int ret;

	ret = ib_query_pkey(device, gsi->port_num, qp_index, &pkey);
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

	ret = modify_to_rts(gsi, qp, qp_index);
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
	u16 qp_index;

	for (qp_index = 0; qp_index < gsi->num_qps; ++qp_index)
		setup_qp(gsi, qp_index);
}

int mlx5_ib_gsi_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
			  int attr_mask)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);
	int ret;

	mlx5_ib_dbg(dev, "modifying GSI QP to state %d\n", attr->qp_state);

	mutex_lock(&gsi->mutex);
	ret = ib_modify_qp(gsi->rx_qp, attr, attr_mask);
	if (ret) {
		mlx5_ib_warn(dev, "unable to modify GSI rx QP: %d\n", ret);
		goto unlock;
	}

	if (to_mqp(gsi->rx_qp)->state == IB_QPS_RTS)
		setup_qps(gsi);

unlock:
	mutex_unlock(&gsi->mutex);

	return ret;
}

int mlx5_ib_gsi_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
			 int qp_attr_mask,
			 struct ib_qp_init_attr *qp_init_attr)
{
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);
	int ret;

	mutex_lock(&gsi->mutex);
	ret = ib_query_qp(gsi->rx_qp, qp_attr, qp_attr_mask, qp_init_attr);
	qp_init_attr->cap = gsi->cap;
	mutex_unlock(&gsi->mutex);

	return ret;
}

/* Call with gsi->lock locked */
static int mlx5_ib_add_outstanding_wr(struct mlx5_ib_gsi_qp *gsi,
				      struct ib_ud_wr *wr, struct ib_wc *wc)
{
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
static int mlx5_ib_gsi_silent_drop(struct mlx5_ib_gsi_qp *gsi,
				    struct ib_ud_wr *wr)
{
	struct ib_wc wc = {
		{ .wr_id = wr->wr.wr_id },
		.status = IB_WC_SUCCESS,
		.opcode = IB_WC_SEND,
		.qp = &gsi->ibqp,
	};
	int ret;

	ret = mlx5_ib_add_outstanding_wr(gsi, wr, &wc);
	if (ret)
		return ret;

	generate_completions(gsi);

	return 0;
}

/* Call with gsi->lock locked */
static struct ib_qp *get_tx_qp(struct mlx5_ib_gsi_qp *gsi, struct ib_ud_wr *wr)
{
	struct mlx5_ib_dev *dev = to_mdev(gsi->rx_qp->device);
	int qp_index = wr->pkey_index;

	if (!mlx5_ib_deth_sqpn_cap(dev))
		return gsi->rx_qp;

	if (qp_index >= gsi->num_qps)
		return NULL;

	return gsi->tx_qps[qp_index];
}

int mlx5_ib_gsi_post_send(struct ib_qp *qp, const struct ib_send_wr *wr,
			  const struct ib_send_wr **bad_wr)
{
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);
	struct ib_qp *tx_qp;
	unsigned long flags;
	int ret;

	for (; wr; wr = wr->next) {
		struct ib_ud_wr cur_wr = *ud_wr(wr);

		cur_wr.wr.next = NULL;

		spin_lock_irqsave(&gsi->lock, flags);
		tx_qp = get_tx_qp(gsi, &cur_wr);
		if (!tx_qp) {
			ret = mlx5_ib_gsi_silent_drop(gsi, &cur_wr);
			if (ret)
				goto err;
			spin_unlock_irqrestore(&gsi->lock, flags);
			continue;
		}

		ret = mlx5_ib_add_outstanding_wr(gsi, &cur_wr, NULL);
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
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);

	return ib_post_recv(gsi->rx_qp, wr, bad_wr);
}

void mlx5_ib_gsi_pkey_change(struct mlx5_ib_gsi_qp *gsi)
{
	if (!gsi)
		return;

	mutex_lock(&gsi->mutex);
	setup_qps(gsi);
	mutex_unlock(&gsi->mutex);
}
