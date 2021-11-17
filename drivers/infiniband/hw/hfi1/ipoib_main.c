// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

/*
 * This file contains HFI1 support for ipoib functionality
 */

#include "ipoib.h"
#include "hfi.h"

static u32 qpn_from_mac(u8 *mac_arr)
{
	return (u32)mac_arr[1] << 16 | mac_arr[2] << 8 | mac_arr[3];
}

static int hfi1_ipoib_dev_init(struct net_device *dev)
{
	struct hfi1_ipoib_dev_priv *priv = hfi1_ipoib_priv(dev);
	int ret;

	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);

	ret = priv->netdev_ops->ndo_init(dev);
	if (ret)
		return ret;

	ret = hfi1_netdev_add_data(priv->dd,
				   qpn_from_mac(priv->netdev->dev_addr),
				   dev);
	if (ret < 0) {
		priv->netdev_ops->ndo_uninit(dev);
		return ret;
	}

	return 0;
}

static void hfi1_ipoib_dev_uninit(struct net_device *dev)
{
	struct hfi1_ipoib_dev_priv *priv = hfi1_ipoib_priv(dev);

	hfi1_netdev_remove_data(priv->dd, qpn_from_mac(priv->netdev->dev_addr));

	priv->netdev_ops->ndo_uninit(dev);
}

static int hfi1_ipoib_dev_open(struct net_device *dev)
{
	struct hfi1_ipoib_dev_priv *priv = hfi1_ipoib_priv(dev);
	int ret;

	ret = priv->netdev_ops->ndo_open(dev);
	if (!ret) {
		struct hfi1_ibport *ibp = to_iport(priv->device,
						   priv->port_num);
		struct rvt_qp *qp;
		u32 qpn = qpn_from_mac(priv->netdev->dev_addr);

		rcu_read_lock();
		qp = rvt_lookup_qpn(ib_to_rvt(priv->device), &ibp->rvp, qpn);
		if (!qp) {
			rcu_read_unlock();
			priv->netdev_ops->ndo_stop(dev);
			return -EINVAL;
		}
		rvt_get_qp(qp);
		priv->qp = qp;
		rcu_read_unlock();

		hfi1_netdev_enable_queues(priv->dd);
		hfi1_ipoib_napi_tx_enable(dev);
	}

	return ret;
}

static int hfi1_ipoib_dev_stop(struct net_device *dev)
{
	struct hfi1_ipoib_dev_priv *priv = hfi1_ipoib_priv(dev);

	if (!priv->qp)
		return 0;

	hfi1_ipoib_napi_tx_disable(dev);
	hfi1_netdev_disable_queues(priv->dd);

	rvt_put_qp(priv->qp);
	priv->qp = NULL;

	return priv->netdev_ops->ndo_stop(dev);
}

static const struct net_device_ops hfi1_ipoib_netdev_ops = {
	.ndo_init         = hfi1_ipoib_dev_init,
	.ndo_uninit       = hfi1_ipoib_dev_uninit,
	.ndo_open         = hfi1_ipoib_dev_open,
	.ndo_stop         = hfi1_ipoib_dev_stop,
	.ndo_get_stats64  = dev_get_tstats64,
};

static int hfi1_ipoib_mcast_attach(struct net_device *dev,
				   struct ib_device *device,
				   union ib_gid *mgid,
				   u16 mlid,
				   int set_qkey,
				   u32 qkey)
{
	struct hfi1_ipoib_dev_priv *priv = hfi1_ipoib_priv(dev);
	u32 qpn = (u32)qpn_from_mac(priv->netdev->dev_addr);
	struct hfi1_ibport *ibp = to_iport(priv->device, priv->port_num);
	struct rvt_qp *qp;
	int ret = -EINVAL;

	rcu_read_lock();

	qp = rvt_lookup_qpn(ib_to_rvt(priv->device), &ibp->rvp, qpn);
	if (qp) {
		rvt_get_qp(qp);
		rcu_read_unlock();
		if (set_qkey)
			priv->qkey = qkey;

		/* attach QP to multicast group */
		ret = ib_attach_mcast(&qp->ibqp, mgid, mlid);
		rvt_put_qp(qp);
	} else {
		rcu_read_unlock();
	}

	return ret;
}

static int hfi1_ipoib_mcast_detach(struct net_device *dev,
				   struct ib_device *device,
				   union ib_gid *mgid,
				   u16 mlid)
{
	struct hfi1_ipoib_dev_priv *priv = hfi1_ipoib_priv(dev);
	u32 qpn = (u32)qpn_from_mac(priv->netdev->dev_addr);
	struct hfi1_ibport *ibp = to_iport(priv->device, priv->port_num);
	struct rvt_qp *qp;
	int ret = -EINVAL;

	rcu_read_lock();

	qp = rvt_lookup_qpn(ib_to_rvt(priv->device), &ibp->rvp, qpn);
	if (qp) {
		rvt_get_qp(qp);
		rcu_read_unlock();
		ret = ib_detach_mcast(&qp->ibqp, mgid, mlid);
		rvt_put_qp(qp);
	} else {
		rcu_read_unlock();
	}
	return ret;
}

static void hfi1_ipoib_netdev_dtor(struct net_device *dev)
{
	struct hfi1_ipoib_dev_priv *priv = hfi1_ipoib_priv(dev);

	hfi1_ipoib_txreq_deinit(priv);
	hfi1_ipoib_rxq_deinit(priv->netdev);

	free_percpu(dev->tstats);
}

static void hfi1_ipoib_free_rdma_netdev(struct net_device *dev)
{
	hfi1_ipoib_netdev_dtor(dev);
	free_netdev(dev);
}

static void hfi1_ipoib_set_id(struct net_device *dev, int id)
{
	struct hfi1_ipoib_dev_priv *priv = hfi1_ipoib_priv(dev);

	priv->pkey_index = (u16)id;
	ib_query_pkey(priv->device,
		      priv->port_num,
		      priv->pkey_index,
		      &priv->pkey);
}

static int hfi1_ipoib_setup_rn(struct ib_device *device,
			       u32 port_num,
			       struct net_device *netdev,
			       void *param)
{
	struct hfi1_devdata *dd = dd_from_ibdev(device);
	struct rdma_netdev *rn = netdev_priv(netdev);
	struct hfi1_ipoib_dev_priv *priv;
	int rc;

	rn->send = hfi1_ipoib_send;
	rn->tx_timeout = hfi1_ipoib_tx_timeout;
	rn->attach_mcast = hfi1_ipoib_mcast_attach;
	rn->detach_mcast = hfi1_ipoib_mcast_detach;
	rn->set_id = hfi1_ipoib_set_id;
	rn->hca = device;
	rn->port_num = port_num;
	rn->mtu = netdev->mtu;

	priv = hfi1_ipoib_priv(netdev);
	priv->dd = dd;
	priv->netdev = netdev;
	priv->device = device;
	priv->port_num = port_num;
	priv->netdev_ops = netdev->netdev_ops;

	netdev->netdev_ops = &hfi1_ipoib_netdev_ops;

	ib_query_pkey(device, port_num, priv->pkey_index, &priv->pkey);

	rc = hfi1_ipoib_txreq_init(priv);
	if (rc) {
		dd_dev_err(dd, "IPoIB netdev TX init - failed(%d)\n", rc);
		hfi1_ipoib_free_rdma_netdev(netdev);
		return rc;
	}

	rc = hfi1_ipoib_rxq_init(netdev);
	if (rc) {
		dd_dev_err(dd, "IPoIB netdev RX init - failed(%d)\n", rc);
		hfi1_ipoib_free_rdma_netdev(netdev);
		return rc;
	}

	netdev->priv_destructor = hfi1_ipoib_netdev_dtor;
	netdev->needs_free_netdev = true;

	return 0;
}

int hfi1_ipoib_rn_get_params(struct ib_device *device,
			     u32 port_num,
			     enum rdma_netdev_t type,
			     struct rdma_netdev_alloc_params *params)
{
	struct hfi1_devdata *dd = dd_from_ibdev(device);

	if (type != RDMA_NETDEV_IPOIB)
		return -EOPNOTSUPP;

	if (!HFI1_CAP_IS_KSET(AIP) || !dd->num_netdev_contexts)
		return -EOPNOTSUPP;

	if (!port_num || port_num > dd->num_pports)
		return -EINVAL;

	params->sizeof_priv = sizeof(struct hfi1_ipoib_rdma_netdev);
	params->txqs = dd->num_sdma;
	params->rxqs = dd->num_netdev_contexts;
	params->param = NULL;
	params->initialize_rdma_netdev = hfi1_ipoib_setup_rn;

	return 0;
}
