/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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
 *
 * $Id: ipoib_verbs.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <rdma/ib_cache.h>

#include "ipoib.h"

int ipoib_mcast_attach(struct net_device *dev, u16 mlid, union ib_gid *mgid)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_qp_attr *qp_attr;
	int ret;
	u16 pkey_index;

	ret = -ENOMEM;
	qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
	if (!qp_attr)
		goto out;

	if (ib_find_cached_pkey(priv->ca, priv->port, priv->pkey, &pkey_index)) {
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
		ret = -ENXIO;
		goto out;
	}
	set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);

	/* set correct QKey for QP */
	qp_attr->qkey = priv->qkey;
	ret = ib_modify_qp(priv->qp, qp_attr, IB_QP_QKEY);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP, ret = %d\n", ret);
		goto out;
	}

	/* attach QP to multicast group */
	mutex_lock(&priv->mcast_mutex);
	ret = ib_attach_mcast(priv->qp, mgid, mlid);
	mutex_unlock(&priv->mcast_mutex);
	if (ret)
		ipoib_warn(priv, "failed to attach to multicast group, ret = %d\n", ret);

out:
	kfree(qp_attr);
	return ret;
}

int ipoib_mcast_detach(struct net_device *dev, u16 mlid, union ib_gid *mgid)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int ret;

	mutex_lock(&priv->mcast_mutex);
	ret = ib_detach_mcast(priv->qp, mgid, mlid);
	mutex_unlock(&priv->mcast_mutex);
	if (ret)
		ipoib_warn(priv, "ib_detach_mcast failed (result = %d)\n", ret);

	return ret;
}

int ipoib_init_qp(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int ret;
	u16 pkey_index;
	struct ib_qp_attr qp_attr;
	int attr_mask;

	/*
	 * Search through the port P_Key table for the requested pkey value.
	 * The port has to be assigned to the respective IB partition in
	 * advance.
	 */
	ret = ib_find_cached_pkey(priv->ca, priv->port, priv->pkey, &pkey_index);
	if (ret) {
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
		return ret;
	}
	set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);

	qp_attr.qp_state = IB_QPS_INIT;
	qp_attr.qkey = 0;
	qp_attr.port_num = priv->port;
	qp_attr.pkey_index = pkey_index;
	attr_mask =
	    IB_QP_QKEY |
	    IB_QP_PORT |
	    IB_QP_PKEY_INDEX |
	    IB_QP_STATE;
	ret = ib_modify_qp(priv->qp, &qp_attr, attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to init, ret = %d\n", ret);
		goto out_fail;
	}

	qp_attr.qp_state = IB_QPS_RTR;
	/* Can't set this in a INIT->RTR transition */
	attr_mask &= ~IB_QP_PORT;
	ret = ib_modify_qp(priv->qp, &qp_attr, attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTR, ret = %d\n", ret);
		goto out_fail;
	}

	qp_attr.qp_state = IB_QPS_RTS;
	qp_attr.sq_psn = 0;
	attr_mask |= IB_QP_SQ_PSN;
	attr_mask &= ~IB_QP_PKEY_INDEX;
	ret = ib_modify_qp(priv->qp, &qp_attr, attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTS, ret = %d\n", ret);
		goto out_fail;
	}

	return 0;

out_fail:
	qp_attr.qp_state = IB_QPS_RESET;
	if (ib_modify_qp(priv->qp, &qp_attr, IB_QP_STATE))
		ipoib_warn(priv, "Failed to modify QP to RESET state\n");

	return ret;
}

int ipoib_transport_dev_init(struct net_device *dev, struct ib_device *ca)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_qp_init_attr init_attr = {
		.cap = {
			.max_send_wr  = ipoib_sendq_size,
			.max_recv_wr  = ipoib_recvq_size,
			.max_send_sge = 1,
			.max_recv_sge = 1
		},
		.sq_sig_type = IB_SIGNAL_ALL_WR,
		.qp_type     = IB_QPT_UD
	};

	priv->pd = ib_alloc_pd(priv->ca);
	if (IS_ERR(priv->pd)) {
		printk(KERN_WARNING "%s: failed to allocate PD\n", ca->name);
		return -ENODEV;
	}

	priv->cq = ib_create_cq(priv->ca, ipoib_ib_completion, NULL, dev,
				ipoib_sendq_size + ipoib_recvq_size + 1);
	if (IS_ERR(priv->cq)) {
		printk(KERN_WARNING "%s: failed to create CQ\n", ca->name);
		goto out_free_pd;
	}

	if (ib_req_notify_cq(priv->cq, IB_CQ_NEXT_COMP))
		goto out_free_cq;

	priv->mr = ib_get_dma_mr(priv->pd, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(priv->mr)) {
		printk(KERN_WARNING "%s: ib_get_dma_mr failed\n", ca->name);
		goto out_free_cq;
	}

	init_attr.send_cq = priv->cq;
	init_attr.recv_cq = priv->cq,

	priv->qp = ib_create_qp(priv->pd, &init_attr);
	if (IS_ERR(priv->qp)) {
		printk(KERN_WARNING "%s: failed to create QP\n", ca->name);
		goto out_free_mr;
	}

	priv->dev->dev_addr[1] = (priv->qp->qp_num >> 16) & 0xff;
	priv->dev->dev_addr[2] = (priv->qp->qp_num >>  8) & 0xff;
	priv->dev->dev_addr[3] = (priv->qp->qp_num      ) & 0xff;

	priv->tx_sge.lkey 	= priv->mr->lkey;

	priv->tx_wr.opcode 	= IB_WR_SEND;
	priv->tx_wr.sg_list 	= &priv->tx_sge;
	priv->tx_wr.num_sge 	= 1;
	priv->tx_wr.send_flags 	= IB_SEND_SIGNALED;

	return 0;

out_free_mr:
	ib_dereg_mr(priv->mr);

out_free_cq:
	ib_destroy_cq(priv->cq);

out_free_pd:
	ib_dealloc_pd(priv->pd);
	return -ENODEV;
}

void ipoib_transport_dev_cleanup(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	if (priv->qp) {
		if (ib_destroy_qp(priv->qp))
			ipoib_warn(priv, "ib_qp_destroy failed\n");

		priv->qp = NULL;
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
	}

	if (ib_dereg_mr(priv->mr))
		ipoib_warn(priv, "ib_dereg_mr failed\n");

	if (ib_destroy_cq(priv->cq))
		ipoib_warn(priv, "ib_cq_destroy failed\n");

	if (ib_dealloc_pd(priv->pd))
		ipoib_warn(priv, "ib_dealloc_pd failed\n");
}

void ipoib_event(struct ib_event_handler *handler,
		 struct ib_event *record)
{
	struct ipoib_dev_priv *priv =
		container_of(handler, struct ipoib_dev_priv, event_handler);

	if (record->event == IB_EVENT_PORT_ERR    ||
	    record->event == IB_EVENT_PKEY_CHANGE ||
	    record->event == IB_EVENT_PORT_ACTIVE ||
	    record->event == IB_EVENT_LID_CHANGE  ||
	    record->event == IB_EVENT_SM_CHANGE) {
		ipoib_dbg(priv, "Port state change event\n");
		queue_work(ipoib_workqueue, &priv->flush_task);
	}
}
