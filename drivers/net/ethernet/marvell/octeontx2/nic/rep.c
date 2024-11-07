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
	netif_carrier_on(dev);
	netif_tx_start_all_queues(dev);
	return 0;
}

static int rvu_rep_stop(struct net_device *dev)
{
	netif_carrier_off(dev);
	netif_tx_disable(dev);

	return 0;
}

static const struct net_device_ops rvu_rep_netdev_ops = {
	.ndo_open		= rvu_rep_open,
	.ndo_stop		= rvu_rep_stop,
	.ndo_start_xmit		= rvu_rep_xmit,
	.ndo_get_stats64	= rvu_rep_get_stats64,
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
		free_netdev(rep->netdev);
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

		ndev->features |= ndev->hw_features;
		eth_hw_addr_random(ndev);
		err = register_netdev(ndev);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "PFVF reprentator registration failed");
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
