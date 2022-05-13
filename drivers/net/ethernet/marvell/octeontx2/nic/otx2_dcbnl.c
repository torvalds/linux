// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2021 Marvell.
 *
 */

#include "otx2_common.h"

int otx2_config_priority_flow_ctrl(struct otx2_nic *pfvf)
{
	struct cgx_pfc_cfg *req;
	struct cgx_pfc_rsp *rsp;
	int err = 0;

	if (is_otx2_lbkvf(pfvf->pdev))
		return 0;

	mutex_lock(&pfvf->mbox.lock);
	req = otx2_mbox_alloc_msg_cgx_prio_flow_ctrl_cfg(&pfvf->mbox);
	if (!req) {
		err = -ENOMEM;
		goto unlock;
	}

	if (pfvf->pfc_en) {
		req->rx_pause = true;
		req->tx_pause = true;
	} else {
		req->rx_pause = false;
		req->tx_pause = false;
	}
	req->pfc_en = pfvf->pfc_en;

	if (!otx2_sync_mbox_msg(&pfvf->mbox)) {
		rsp = (struct cgx_pfc_rsp *)
		       otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0, &req->hdr);
		if (req->rx_pause != rsp->rx_pause || req->tx_pause != rsp->tx_pause) {
			dev_warn(pfvf->dev,
				 "Failed to config PFC\n");
			err = -EPERM;
		}
	}
unlock:
	mutex_unlock(&pfvf->mbox.lock);
	return err;
}

void otx2_update_bpid_in_rqctx(struct otx2_nic *pfvf, int vlan_prio, int qidx,
			       bool pfc_enable)
{
	bool if_up = netif_running(pfvf->netdev);
	struct npa_aq_enq_req *npa_aq;
	struct nix_aq_enq_req *aq;
	int err = 0;

	if (pfvf->queue_to_pfc_map[qidx] && pfc_enable) {
		dev_warn(pfvf->dev,
			 "PFC enable not permitted as Priority %d already mapped to Queue %d\n",
			 pfvf->queue_to_pfc_map[qidx], qidx);
		return;
	}

	if (if_up) {
		netif_tx_stop_all_queues(pfvf->netdev);
		netif_carrier_off(pfvf->netdev);
	}

	pfvf->queue_to_pfc_map[qidx] = vlan_prio;

	aq = otx2_mbox_alloc_msg_nix_aq_enq(&pfvf->mbox);
	if (!aq) {
		err = -ENOMEM;
		goto out;
	}

	aq->cq.bpid = pfvf->bpid[vlan_prio];
	aq->cq_mask.bpid = GENMASK(8, 0);

	/* Fill AQ info */
	aq->qidx = qidx;
	aq->ctype = NIX_AQ_CTYPE_CQ;
	aq->op = NIX_AQ_INSTOP_WRITE;

	otx2_sync_mbox_msg(&pfvf->mbox);

	npa_aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
	if (!npa_aq) {
		err = -ENOMEM;
		goto out;
	}
	npa_aq->aura.nix0_bpid = pfvf->bpid[vlan_prio];
	npa_aq->aura_mask.nix0_bpid = GENMASK(8, 0);

	/* Fill NPA AQ info */
	npa_aq->aura_id = qidx;
	npa_aq->ctype = NPA_AQ_CTYPE_AURA;
	npa_aq->op = NPA_AQ_INSTOP_WRITE;
	otx2_sync_mbox_msg(&pfvf->mbox);

out:
	if (if_up) {
		netif_carrier_on(pfvf->netdev);
		netif_tx_start_all_queues(pfvf->netdev);
	}

	if (err)
		dev_warn(pfvf->dev,
			 "Updating BPIDs in CQ and Aura contexts of RQ%d failed with err %d\n",
			 qidx, err);
}

static int otx2_dcbnl_ieee_getpfc(struct net_device *dev, struct ieee_pfc *pfc)
{
	struct otx2_nic *pfvf = netdev_priv(dev);

	pfc->pfc_cap = IEEE_8021QAZ_MAX_TCS;
	pfc->pfc_en = pfvf->pfc_en;

	return 0;
}

static int otx2_dcbnl_ieee_setpfc(struct net_device *dev, struct ieee_pfc *pfc)
{
	struct otx2_nic *pfvf = netdev_priv(dev);
	int err;

	/* Save PFC configuration to interface */
	pfvf->pfc_en = pfc->pfc_en;

	err = otx2_config_priority_flow_ctrl(pfvf);
	if (err)
		return err;

	/* Request Per channel Bpids */
	if (pfc->pfc_en)
		otx2_nix_config_bp(pfvf, true);

	return 0;
}

static u8 otx2_dcbnl_getdcbx(struct net_device __always_unused *dev)
{
	return DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_IEEE;
}

static u8 otx2_dcbnl_setdcbx(struct net_device __always_unused *dev, u8 mode)
{
	return (mode != (DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_IEEE)) ? 1 : 0;
}

static const struct dcbnl_rtnl_ops otx2_dcbnl_ops = {
	.ieee_getpfc	= otx2_dcbnl_ieee_getpfc,
	.ieee_setpfc	= otx2_dcbnl_ieee_setpfc,
	.getdcbx	= otx2_dcbnl_getdcbx,
	.setdcbx	= otx2_dcbnl_setdcbx,
};

int otx2_dcbnl_set_ops(struct net_device *dev)
{
	struct otx2_nic *pfvf = netdev_priv(dev);

	pfvf->queue_to_pfc_map = devm_kzalloc(pfvf->dev, pfvf->hw.rx_queues,
					      GFP_KERNEL);
	if (!pfvf->queue_to_pfc_map)
		return -ENOMEM;
	dev->dcbnl_ops = &otx2_dcbnl_ops;

	return 0;
}
