// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU representor driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/net_tstamp.h>
#include <linux/sort.h>

#include "otx2_common.h"
#include "cn10k.h"
#include "otx2_reg.h"
#include "rep.h"

#define DRV_NAME	"rvu_rep"
#define DRV_STRING	"Marvell RVU Representor Driver"

static const struct pci_device_id rvu_rep_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_RVU_REP) },
	{ }
};

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION(DRV_STRING);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, rvu_rep_id_table);

static int rvu_rep_notify_pfvf(struct otx2_nic *priv, u16 event,
			       struct rep_event *data);

static int rvu_rep_mcam_flow_init(struct rep_dev *rep)
{
	struct npc_mcam_alloc_entry_req *req;
	struct npc_mcam_alloc_entry_rsp *rsp;
	struct otx2_nic *priv = rep->mdev;
	int ent, allocated = 0;
	int count;

	rep->flow_cfg = kcalloc(1, sizeof(struct otx2_flow_config), GFP_KERNEL);

	if (!rep->flow_cfg)
		return -ENOMEM;

	count = OTX2_DEFAULT_FLOWCOUNT;

	rep->flow_cfg->flow_ent = kcalloc(count, sizeof(u16), GFP_KERNEL);
	if (!rep->flow_cfg->flow_ent)
		return -ENOMEM;

	while (allocated < count) {
		req = otx2_mbox_alloc_msg_npc_mcam_alloc_entry(&priv->mbox);
		if (!req)
			goto exit;

		req->hdr.pcifunc = rep->pcifunc;
		req->contig = false;
		req->ref_entry = 0;
		req->count = (count - allocated) > NPC_MAX_NONCONTIG_ENTRIES ?
				NPC_MAX_NONCONTIG_ENTRIES : count - allocated;

		if (otx2_sync_mbox_msg(&priv->mbox))
			goto exit;

		rsp = (struct npc_mcam_alloc_entry_rsp *)otx2_mbox_get_rsp
			(&priv->mbox.mbox, 0, &req->hdr);

		for (ent = 0; ent < rsp->count; ent++)
			rep->flow_cfg->flow_ent[ent + allocated] = rsp->entry_list[ent];

		allocated += rsp->count;

		if (rsp->count != req->count)
			break;
	}
exit:
	/* Multiple MCAM entry alloc requests could result in non-sequential
	 * MCAM entries in the flow_ent[] array. Sort them in an ascending
	 * order, otherwise user installed ntuple filter index and MCAM entry
	 * index will not be in sync.
	 */
	if (allocated)
		sort(&rep->flow_cfg->flow_ent[0], allocated,
		     sizeof(rep->flow_cfg->flow_ent[0]), mcam_entry_cmp, NULL);

	mutex_unlock(&priv->mbox.lock);

	rep->flow_cfg->max_flows = allocated;

	if (allocated) {
		rep->flags |= OTX2_FLAG_MCAM_ENTRIES_ALLOC;
		rep->flags |= OTX2_FLAG_NTUPLE_SUPPORT;
		rep->flags |= OTX2_FLAG_TC_FLOWER_SUPPORT;
	}

	INIT_LIST_HEAD(&rep->flow_cfg->flow_list);
	INIT_LIST_HEAD(&rep->flow_cfg->flow_list_tc);
	return 0;
}

static int rvu_rep_setup_tc_cb(enum tc_setup_type type,
			       void *type_data, void *cb_priv)
{
	struct rep_dev *rep = cb_priv;
	struct otx2_nic *priv = rep->mdev;

	if (!(rep->flags & RVU_REP_VF_INITIALIZED))
		return -EINVAL;

	if (!(rep->flags & OTX2_FLAG_TC_FLOWER_SUPPORT))
		rvu_rep_mcam_flow_init(rep);

	priv->netdev = rep->netdev;
	priv->flags = rep->flags;
	priv->pcifunc = rep->pcifunc;
	priv->flow_cfg = rep->flow_cfg;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return otx2_setup_tc_cls_flower(priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(rvu_rep_block_cb_list);
static int rvu_rep_setup_tc(struct net_device *netdev, enum tc_setup_type type,
			    void *type_data)
{
	struct rvu_rep *rep = netdev_priv(netdev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &rvu_rep_block_cb_list,
						  rvu_rep_setup_tc_cb,
						  rep, rep, true);
	default:
		return -EOPNOTSUPP;
	}
}

static int
rvu_rep_sp_stats64(const struct net_device *dev,
		   struct rtnl_link_stats64 *stats)
{
	struct rep_dev *rep = netdev_priv(dev);
	struct otx2_nic *priv = rep->mdev;
	struct otx2_rcv_queue *rq;
	struct otx2_snd_queue *sq;
	u16 qidx = rep->rep_id;

	otx2_update_rq_stats(priv, qidx);
	rq = &priv->qset.rq[qidx];

	otx2_update_sq_stats(priv, qidx);
	sq = &priv->qset.sq[qidx];

	stats->tx_bytes = sq->stats.bytes;
	stats->tx_packets = sq->stats.pkts;
	stats->rx_bytes = rq->stats.bytes;
	stats->rx_packets = rq->stats.pkts;
	return 0;
}

static bool
rvu_rep_has_offload_stats(const struct net_device *dev, int attr_id)
{
	return attr_id == IFLA_OFFLOAD_XSTATS_CPU_HIT;
}

static int
rvu_rep_get_offload_stats(int attr_id, const struct net_device *dev,
			  void *sp)
{
	if (attr_id == IFLA_OFFLOAD_XSTATS_CPU_HIT)
		return rvu_rep_sp_stats64(dev, (struct rtnl_link_stats64 *)sp);

	return -EINVAL;
}

static int rvu_rep_dl_port_fn_hw_addr_get(struct devlink_port *port,
					  u8 *hw_addr, int *hw_addr_len,
					  struct netlink_ext_ack *extack)
{
	struct rep_dev *rep = container_of(port, struct rep_dev, dl_port);

	ether_addr_copy(hw_addr, rep->mac);
	*hw_addr_len = ETH_ALEN;
	return 0;
}

static int rvu_rep_dl_port_fn_hw_addr_set(struct devlink_port *port,
					  const u8 *hw_addr, int hw_addr_len,
					  struct netlink_ext_ack *extack)
{
	struct rep_dev *rep = container_of(port, struct rep_dev, dl_port);
	struct otx2_nic *priv = rep->mdev;
	struct rep_event evt = {0};

	eth_hw_addr_set(rep->netdev, hw_addr);
	ether_addr_copy(rep->mac, hw_addr);

	ether_addr_copy(evt.evt_data.mac, hw_addr);
	evt.pcifunc = rep->pcifunc;
	rvu_rep_notify_pfvf(priv, RVU_EVENT_MAC_ADDR_CHANGE, &evt);
	return 0;
}

static const struct devlink_port_ops rvu_rep_dl_port_ops = {
	.port_fn_hw_addr_get = rvu_rep_dl_port_fn_hw_addr_get,
	.port_fn_hw_addr_set = rvu_rep_dl_port_fn_hw_addr_set,
};

static void
rvu_rep_devlink_set_switch_id(struct otx2_nic *priv,
			      struct netdev_phys_item_id *ppid)
{
	struct pci_dev *pdev = priv->pdev;
	u64 id;

	id = pci_get_dsn(pdev);

	ppid->id_len = sizeof(id);
	put_unaligned_be64(id, &ppid->id);
}

static void rvu_rep_devlink_port_unregister(struct rep_dev *rep)
{
	devlink_port_unregister(&rep->dl_port);
}

static int rvu_rep_devlink_port_register(struct rep_dev *rep)
{
	struct devlink_port_attrs attrs = {};
	struct otx2_nic *priv = rep->mdev;
	struct devlink *dl = priv->dl->dl;
	int err;

	if (!(rep->pcifunc & RVU_PFVF_FUNC_MASK)) {
		attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
		attrs.phys.port_number = rvu_get_pf(rep->pcifunc);
	} else {
		attrs.flavour = DEVLINK_PORT_FLAVOUR_PCI_VF;
		attrs.pci_vf.pf = rvu_get_pf(rep->pcifunc);
		attrs.pci_vf.vf = rep->pcifunc & RVU_PFVF_FUNC_MASK;
	}

	rvu_rep_devlink_set_switch_id(priv, &attrs.switch_id);
	devlink_port_attrs_set(&rep->dl_port, &attrs);

	err = devl_port_register_with_ops(dl, &rep->dl_port, rep->rep_id,
					  &rvu_rep_dl_port_ops);
	if (err) {
		dev_err(rep->mdev->dev, "devlink_port_register failed: %d\n",
			err);
		return err;
	}
	return 0;
}

static int rvu_rep_get_repid(struct otx2_nic *priv, u16 pcifunc)
{
	int rep_id;

	for (rep_id = 0; rep_id < priv->rep_cnt; rep_id++)
		if (priv->rep_pf_map[rep_id] == pcifunc)
			return rep_id;
	return -EINVAL;
}

static int rvu_rep_notify_pfvf(struct otx2_nic *priv, u16 event,
			       struct rep_event *data)
{
	struct rep_event *req;

	mutex_lock(&priv->mbox.lock);
	req = otx2_mbox_alloc_msg_rep_event_notify(&priv->mbox);
	if (!req) {
		mutex_unlock(&priv->mbox.lock);
		return -ENOMEM;
	}
	req->event = event;
	req->pcifunc = data->pcifunc;

	memcpy(&req->evt_data, &data->evt_data, sizeof(struct rep_evt_data));
	otx2_sync_mbox_msg(&priv->mbox);
	mutex_unlock(&priv->mbox.lock);
	return 0;
}

static void rvu_rep_state_evt_handler(struct otx2_nic *priv,
				      struct rep_event *info)
{
	struct rep_dev *rep;
	int rep_id;

	rep_id = rvu_rep_get_repid(priv, info->pcifunc);
	rep = priv->reps[rep_id];
	if (info->evt_data.vf_state)
		rep->flags |= RVU_REP_VF_INITIALIZED;
	else
		rep->flags &= ~RVU_REP_VF_INITIALIZED;
}

int rvu_event_up_notify(struct otx2_nic *pf, struct rep_event *info)
{
	if (info->event & RVU_EVENT_PFVF_STATE)
		rvu_rep_state_evt_handler(pf, info);
	return 0;
}

static int rvu_rep_change_mtu(struct net_device *dev, int new_mtu)
{
	struct rep_dev *rep = netdev_priv(dev);
	struct otx2_nic *priv = rep->mdev;
	struct rep_event evt = {0};

	netdev_info(dev, "Changing MTU from %d to %d\n",
		    dev->mtu, new_mtu);
	dev->mtu = new_mtu;

	evt.evt_data.mtu = new_mtu;
	evt.pcifunc = rep->pcifunc;
	rvu_rep_notify_pfvf(priv, RVU_EVENT_MTU_CHANGE, &evt);
	return 0;
}

static void rvu_rep_get_stats(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct nix_stats_req *req;
	struct nix_stats_rsp *rsp;
	struct rep_stats *stats;
	struct otx2_nic *priv;
	struct rep_dev *rep;
	int err;

	rep = container_of(del_work, struct rep_dev, stats_wrk);
	priv = rep->mdev;

	mutex_lock(&priv->mbox.lock);
	req = otx2_mbox_alloc_msg_nix_lf_stats(&priv->mbox);
	if (!req) {
		mutex_unlock(&priv->mbox.lock);
		return;
	}
	req->pcifunc = rep->pcifunc;
	err = otx2_sync_mbox_msg_busy_poll(&priv->mbox);
	if (err)
		goto exit;

	rsp = (struct nix_stats_rsp *)
	      otx2_mbox_get_rsp(&priv->mbox.mbox, 0, &req->hdr);

	if (IS_ERR(rsp)) {
		err = PTR_ERR(rsp);
		goto exit;
	}

	stats = &rep->stats;
	stats->rx_bytes = rsp->rx.octs;
	stats->rx_frames = rsp->rx.ucast + rsp->rx.bcast +
			    rsp->rx.mcast;
	stats->rx_drops = rsp->rx.drop;
	stats->rx_mcast_frames = rsp->rx.mcast;
	stats->tx_bytes = rsp->tx.octs;
	stats->tx_frames = rsp->tx.ucast + rsp->tx.bcast + rsp->tx.mcast;
	stats->tx_drops = rsp->tx.drop;
exit:
	mutex_unlock(&priv->mbox.lock);
}

static void rvu_rep_get_stats64(struct net_device *dev,
				struct rtnl_link_stats64 *stats)
{
	struct rep_dev *rep = netdev_priv(dev);

	if (!(rep->flags & RVU_REP_VF_INITIALIZED))
		return;

	stats->rx_packets = rep->stats.rx_frames;
	stats->rx_bytes = rep->stats.rx_bytes;
	stats->rx_dropped = rep->stats.rx_drops;
	stats->multicast = rep->stats.rx_mcast_frames;

	stats->tx_packets = rep->stats.tx_frames;
	stats->tx_bytes = rep->stats.tx_bytes;
	stats->tx_dropped = rep->stats.tx_drops;

	schedule_delayed_work(&rep->stats_wrk, msecs_to_jiffies(100));
}

static int rvu_eswitch_config(struct otx2_nic *priv, u8 ena)
{
	struct esw_cfg_req *req;

	mutex_lock(&priv->mbox.lock);
	req = otx2_mbox_alloc_msg_esw_cfg(&priv->mbox);
	if (!req) {
		mutex_unlock(&priv->mbox.lock);
		return -ENOMEM;
	}
	req->ena = ena;
	otx2_sync_mbox_msg(&priv->mbox);
	mutex_unlock(&priv->mbox.lock);
	return 0;
}

static netdev_tx_t rvu_rep_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct rep_dev *rep = netdev_priv(dev);
	struct otx2_nic *pf = rep->mdev;
	struct otx2_snd_queue *sq;
	struct netdev_queue *txq;

	sq = &pf->qset.sq[rep->rep_id];
	txq = netdev_get_tx_queue(dev, 0);

	if (!otx2_sq_append_skb(pf, txq, sq, skb, rep->rep_id)) {
		netif_tx_stop_queue(txq);

		/* Check again, in case SQBs got freed up */
		smp_mb();
		if (((sq->num_sqbs - *sq->aura_fc_addr) * sq->sqe_per_sqb)
							> sq->sqe_thresh)
			netif_tx_wake_queue(txq);

		return NETDEV_TX_BUSY;
	}
	return NETDEV_TX_OK;
}

static int rvu_rep_open(struct net_device *dev)
{
	struct rep_dev *rep = netdev_priv(dev);
	struct otx2_nic *priv = rep->mdev;
	struct rep_event evt = {0};

	if (!(rep->flags & RVU_REP_VF_INITIALIZED))
		return 0;

	netif_carrier_on(dev);
	netif_tx_start_all_queues(dev);

	evt.event = RVU_EVENT_PORT_STATE;
	evt.evt_data.port_state = 1;
	evt.pcifunc = rep->pcifunc;
	rvu_rep_notify_pfvf(priv, RVU_EVENT_PORT_STATE, &evt);
	return 0;
}

static int rvu_rep_stop(struct net_device *dev)
{
	struct rep_dev *rep = netdev_priv(dev);
	struct otx2_nic *priv = rep->mdev;
	struct rep_event evt = {0};

	if (!(rep->flags & RVU_REP_VF_INITIALIZED))
		return 0;

	netif_carrier_off(dev);
	netif_tx_disable(dev);

	evt.event = RVU_EVENT_PORT_STATE;
	evt.pcifunc = rep->pcifunc;
	rvu_rep_notify_pfvf(priv, RVU_EVENT_PORT_STATE, &evt);
	return 0;
}

static const struct net_device_ops rvu_rep_netdev_ops = {
	.ndo_open		= rvu_rep_open,
	.ndo_stop		= rvu_rep_stop,
	.ndo_start_xmit		= rvu_rep_xmit,
	.ndo_get_stats64	= rvu_rep_get_stats64,
	.ndo_change_mtu		= rvu_rep_change_mtu,
	.ndo_has_offload_stats	= rvu_rep_has_offload_stats,
	.ndo_get_offload_stats	= rvu_rep_get_offload_stats,
	.ndo_setup_tc		= rvu_rep_setup_tc,
};

static int rvu_rep_napi_init(struct otx2_nic *priv,
			     struct netlink_ext_ack *extack)
{
	struct otx2_qset *qset = &priv->qset;
	struct otx2_cq_poll *cq_poll = NULL;
	struct otx2_hw *hw = &priv->hw;
	int err = 0, qidx, vec;
	char *irq_name;

	qset->napi = kcalloc(hw->cint_cnt, sizeof(*cq_poll), GFP_KERNEL);
	if (!qset->napi)
		return -ENOMEM;

	/* Register NAPI handler */
	for (qidx = 0; qidx < hw->cint_cnt; qidx++) {
		cq_poll = &qset->napi[qidx];
		cq_poll->cint_idx = qidx;
		cq_poll->cq_ids[CQ_RX] =
			(qidx <  hw->rx_queues) ? qidx : CINT_INVALID_CQ;
		cq_poll->cq_ids[CQ_TX] = (qidx < hw->tx_queues) ?
					  qidx + hw->rx_queues :
					  CINT_INVALID_CQ;
		cq_poll->cq_ids[CQ_XDP] = CINT_INVALID_CQ;
		cq_poll->cq_ids[CQ_QOS] = CINT_INVALID_CQ;

		cq_poll->dev = (void *)priv;
		netif_napi_add(priv->reps[qidx]->netdev, &cq_poll->napi,
			       otx2_napi_handler);
		napi_enable(&cq_poll->napi);
	}
	/* Register CQ IRQ handlers */
	vec = hw->nix_msixoff + NIX_LF_CINT_VEC_START;
	for (qidx = 0; qidx < hw->cint_cnt; qidx++) {
		irq_name = &hw->irq_name[vec * NAME_SIZE];

		snprintf(irq_name, NAME_SIZE, "rep%d-rxtx-%d", qidx, qidx);

		err = request_irq(pci_irq_vector(priv->pdev, vec),
				  otx2_cq_intr_handler, 0, irq_name,
				  &qset->napi[qidx]);
		if (err) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "RVU REP IRQ registration failed for CQ%d",
					       qidx);
			goto err_free_cints;
		}
		vec++;

		/* Enable CQ IRQ */
		otx2_write64(priv, NIX_LF_CINTX_INT(qidx), BIT_ULL(0));
		otx2_write64(priv, NIX_LF_CINTX_ENA_W1S(qidx), BIT_ULL(0));
	}
	priv->flags &= ~OTX2_FLAG_INTF_DOWN;
	return 0;

err_free_cints:
	otx2_free_cints(priv, qidx);
	otx2_disable_napi(priv);
	return err;
}

static void rvu_rep_free_cq_rsrc(struct otx2_nic *priv)
{
	struct otx2_qset *qset = &priv->qset;
	struct otx2_cq_poll *cq_poll = NULL;
	int qidx, vec;

	/* Cleanup CQ NAPI and IRQ */
	vec = priv->hw.nix_msixoff + NIX_LF_CINT_VEC_START;
	for (qidx = 0; qidx < priv->hw.cint_cnt; qidx++) {
		/* Disable interrupt */
		otx2_write64(priv, NIX_LF_CINTX_ENA_W1C(qidx), BIT_ULL(0));

		synchronize_irq(pci_irq_vector(priv->pdev, vec));

		cq_poll = &qset->napi[qidx];
		napi_synchronize(&cq_poll->napi);
		vec++;
	}
	otx2_free_cints(priv, priv->hw.cint_cnt);
	otx2_disable_napi(priv);
}

static void rvu_rep_rsrc_free(struct otx2_nic *priv)
{
	struct otx2_qset *qset = &priv->qset;
	struct delayed_work *work;
	int wrk;

	for (wrk = 0; wrk < priv->qset.cq_cnt; wrk++) {
		work = &priv->refill_wrk[wrk].pool_refill_work;
		cancel_delayed_work_sync(work);
	}
	devm_kfree(priv->dev, priv->refill_wrk);

	otx2_free_hw_resources(priv);
	otx2_free_queue_mem(qset);
}

static int rvu_rep_rsrc_init(struct otx2_nic *priv)
{
	struct otx2_qset *qset = &priv->qset;
	int err;

	err = otx2_alloc_queue_mem(priv);
	if (err)
		return err;

	priv->hw.max_mtu = otx2_get_max_mtu(priv);
	priv->tx_max_pktlen = priv->hw.max_mtu + OTX2_ETH_HLEN;
	priv->rbsize = ALIGN(priv->hw.rbuf_len, OTX2_ALIGN) + OTX2_HEAD_ROOM;

	err = otx2_init_hw_resources(priv);
	if (err)
		goto err_free_rsrc;

	/* Set maximum frame size allowed in HW */
	err = otx2_hw_set_mtu(priv, priv->hw.max_mtu);
	if (err) {
		dev_err(priv->dev, "Failed to set HW MTU\n");
		goto err_free_rsrc;
	}
	return 0;

err_free_rsrc:
	otx2_free_hw_resources(priv);
	otx2_free_queue_mem(qset);
	return err;
}

void rvu_rep_destroy(struct otx2_nic *priv)
{
	struct rep_dev *rep;
	int rep_id;

	rvu_eswitch_config(priv, false);
	priv->flags |= OTX2_FLAG_INTF_DOWN;
	rvu_rep_free_cq_rsrc(priv);
	for (rep_id = 0; rep_id < priv->rep_cnt; rep_id++) {
		rep = priv->reps[rep_id];
		unregister_netdev(rep->netdev);
		rvu_rep_devlink_port_unregister(rep);
		free_netdev(rep->netdev);
		kfree(rep->flow_cfg);
	}
	kfree(priv->reps);
	rvu_rep_rsrc_free(priv);
}

int rvu_rep_create(struct otx2_nic *priv, struct netlink_ext_ack *extack)
{
	int rep_cnt = priv->rep_cnt;
	struct net_device *ndev;
	struct rep_dev *rep;
	int rep_id, err;
	u16 pcifunc;

	err = rvu_rep_rsrc_init(priv);
	if (err)
		return -ENOMEM;

	priv->reps = kcalloc(rep_cnt, sizeof(struct rep_dev *), GFP_KERNEL);
	if (!priv->reps)
		return -ENOMEM;

	for (rep_id = 0; rep_id < rep_cnt; rep_id++) {
		ndev = alloc_etherdev(sizeof(*rep));
		if (!ndev) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "PFVF representor:%d creation failed",
					       rep_id);
			err = -ENOMEM;
			goto exit;
		}

		rep = netdev_priv(ndev);
		priv->reps[rep_id] = rep;
		rep->mdev = priv;
		rep->netdev = ndev;
		rep->rep_id = rep_id;

		ndev->min_mtu = OTX2_MIN_MTU;
		ndev->max_mtu = priv->hw.max_mtu;
		ndev->netdev_ops = &rvu_rep_netdev_ops;
		pcifunc = priv->rep_pf_map[rep_id];
		rep->pcifunc = pcifunc;

		snprintf(ndev->name, sizeof(ndev->name), "Rpf%dvf%d",
			 rvu_get_pf(pcifunc), (pcifunc & RVU_PFVF_FUNC_MASK));

		ndev->hw_features = (NETIF_F_RXCSUM | NETIF_F_IP_CSUM |
			       NETIF_F_IPV6_CSUM | NETIF_F_RXHASH |
			       NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO6);

		ndev->hw_features |= NETIF_F_HW_TC;
		ndev->features |= ndev->hw_features;
		eth_hw_addr_random(ndev);
		err = rvu_rep_devlink_port_register(rep);
		if (err) {
			free_netdev(ndev);
			goto exit;
		}

		SET_NETDEV_DEVLINK_PORT(ndev, &rep->dl_port);
		err = register_netdev(ndev);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "PFVF representor registration failed");
			rvu_rep_devlink_port_unregister(rep);
			free_netdev(ndev);
			goto exit;
		}

		INIT_DELAYED_WORK(&rep->stats_wrk, rvu_rep_get_stats);
	}
	err = rvu_rep_napi_init(priv, extack);
	if (err)
		goto exit;

	rvu_eswitch_config(priv, true);
	return 0;
exit:
	while (--rep_id >= 0) {
		rep = priv->reps[rep_id];
		unregister_netdev(rep->netdev);
		rvu_rep_devlink_port_unregister(rep);
		free_netdev(rep->netdev);
	}
	kfree(priv->reps);
	rvu_rep_rsrc_free(priv);
	return err;
}

static int rvu_get_rep_cnt(struct otx2_nic *priv)
{
	struct get_rep_cnt_rsp *rsp;
	struct mbox_msghdr *msghdr;
	struct msg_req *req;
	int err, rep;

	mutex_lock(&priv->mbox.lock);
	req = otx2_mbox_alloc_msg_get_rep_cnt(&priv->mbox);
	if (!req) {
		mutex_unlock(&priv->mbox.lock);
		return -ENOMEM;
	}
	err = otx2_sync_mbox_msg(&priv->mbox);
	if (err)
		goto exit;

	msghdr = otx2_mbox_get_rsp(&priv->mbox.mbox, 0, &req->hdr);
	if (IS_ERR(msghdr)) {
		err = PTR_ERR(msghdr);
		goto exit;
	}

	rsp = (struct get_rep_cnt_rsp *)msghdr;
	priv->hw.tx_queues = rsp->rep_cnt;
	priv->hw.rx_queues = rsp->rep_cnt;
	priv->rep_cnt = rsp->rep_cnt;
	for (rep = 0; rep < priv->rep_cnt; rep++)
		priv->rep_pf_map[rep] = rsp->rep_pf_map[rep];

exit:
	mutex_unlock(&priv->mbox.lock);
	return err;
}

static int rvu_rep_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct otx2_nic *priv;
	struct otx2_hw *hw;
	int err;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		return err;
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "DMA mask config failed, abort\n");
		goto err_release_regions;
	}

	pci_set_master(pdev);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		err = -ENOMEM;
		goto err_release_regions;
	}

	pci_set_drvdata(pdev, priv);
	priv->pdev = pdev;
	priv->dev = dev;
	priv->flags |= OTX2_FLAG_INTF_DOWN;
	priv->flags |= OTX2_FLAG_REP_MODE_ENABLED;

	hw = &priv->hw;
	hw->pdev = pdev;
	hw->max_queues = OTX2_MAX_CQ_CNT;
	hw->rbuf_len = OTX2_DEFAULT_RBUF_LEN;
	hw->xqe_size = 128;

	err = otx2_init_rsrc(pdev, priv);
	if (err)
		goto err_release_regions;

	priv->iommu_domain = iommu_get_domain_for_dev(dev);

	err = rvu_get_rep_cnt(priv);
	if (err)
		goto err_detach_rsrc;

	err = otx2_register_dl(priv);
	if (err)
		goto err_detach_rsrc;

	return 0;

err_detach_rsrc:
	if (priv->hw.lmt_info)
		free_percpu(priv->hw.lmt_info);
	if (test_bit(CN10K_LMTST, &priv->hw.cap_flag))
		qmem_free(priv->dev, priv->dync_lmt);
	otx2_detach_resources(&priv->mbox);
	otx2_disable_mbox_intr(priv);
	otx2_pfaf_mbox_destroy(priv);
	pci_free_irq_vectors(pdev);
err_release_regions:
	pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);
	return err;
}

static void rvu_rep_remove(struct pci_dev *pdev)
{
	struct otx2_nic *priv = pci_get_drvdata(pdev);

	otx2_unregister_dl(priv);
	if (!(priv->flags & OTX2_FLAG_INTF_DOWN))
		rvu_rep_destroy(priv);
	otx2_detach_resources(&priv->mbox);
	if (priv->hw.lmt_info)
		free_percpu(priv->hw.lmt_info);
	if (test_bit(CN10K_LMTST, &priv->hw.cap_flag))
		qmem_free(priv->dev, priv->dync_lmt);
	otx2_disable_mbox_intr(priv);
	otx2_pfaf_mbox_destroy(priv);
	pci_free_irq_vectors(priv->pdev);
	pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);
}

static struct pci_driver rvu_rep_driver = {
	.name = DRV_NAME,
	.id_table = rvu_rep_id_table,
	.probe = rvu_rep_probe,
	.remove = rvu_rep_remove,
	.shutdown = rvu_rep_remove,
};

static int __init rvu_rep_init_module(void)
{
	return pci_register_driver(&rvu_rep_driver);
}

static void __exit rvu_rep_cleanup_module(void)
{
	pci_unregister_driver(&rvu_rep_driver);
}

module_init(rvu_rep_init_module);
module_exit(rvu_rep_cleanup_module);
