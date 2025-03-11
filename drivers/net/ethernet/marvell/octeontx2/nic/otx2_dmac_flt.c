// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2021 Marvell.
 *
 */

#include "otx2_common.h"

static int otx2_dmacflt_do_add(struct otx2_nic *pf, const u8 *mac,
			       u32 *dmac_index)
{
	struct cgx_mac_addr_add_req *req;
	struct cgx_mac_addr_add_rsp *rsp;
	int err;

	mutex_lock(&pf->mbox.lock);

	req = otx2_mbox_alloc_msg_cgx_mac_addr_add(&pf->mbox);
	if (!req) {
		mutex_unlock(&pf->mbox.lock);
		return -ENOMEM;
	}

	ether_addr_copy(req->mac_addr, mac);
	err = otx2_sync_mbox_msg(&pf->mbox);

	if (!err) {
		rsp = (struct cgx_mac_addr_add_rsp *)
			 otx2_mbox_get_rsp(&pf->mbox.mbox, 0, &req->hdr);
		if (IS_ERR(rsp)) {
			mutex_unlock(&pf->mbox.lock);
			return PTR_ERR(rsp);
		}

		*dmac_index = rsp->index;
	}

	mutex_unlock(&pf->mbox.lock);
	return err;
}

static int otx2_dmacflt_add_pfmac(struct otx2_nic *pf, u32 *dmac_index)
{
	struct cgx_mac_addr_set_or_get *req;
	struct cgx_mac_addr_set_or_get *rsp;
	int err;

	mutex_lock(&pf->mbox.lock);

	req = otx2_mbox_alloc_msg_cgx_mac_addr_set(&pf->mbox);
	if (!req) {
		mutex_unlock(&pf->mbox.lock);
		return -ENOMEM;
	}

	req->index = *dmac_index;

	ether_addr_copy(req->mac_addr, pf->netdev->dev_addr);
	err = otx2_sync_mbox_msg(&pf->mbox);

	if (err)
		goto out;

	rsp = (struct cgx_mac_addr_set_or_get *)
		otx2_mbox_get_rsp(&pf->mbox.mbox, 0, &req->hdr);

	if (IS_ERR_OR_NULL(rsp)) {
		err = -EINVAL;
		goto out;
	}

	*dmac_index = rsp->index;
out:
	mutex_unlock(&pf->mbox.lock);
	return err;
}

int otx2_dmacflt_add(struct otx2_nic *pf, const u8 *mac, u32 bit_pos)
{
	u32 *dmacindex;

	/* Store dmacindex returned by CGX/RPM driver which will
	 * be used for macaddr update/remove
	 */
	dmacindex = &pf->flow_cfg->bmap_to_dmacindex[bit_pos];

	if (ether_addr_equal(mac, pf->netdev->dev_addr))
		return otx2_dmacflt_add_pfmac(pf, dmacindex);
	else
		return otx2_dmacflt_do_add(pf, mac, dmacindex);
}

static int otx2_dmacflt_do_remove(struct otx2_nic *pfvf, const u8 *mac,
				  u32 dmac_index)
{
	struct cgx_mac_addr_del_req *req;
	int err;

	mutex_lock(&pfvf->mbox.lock);
	req = otx2_mbox_alloc_msg_cgx_mac_addr_del(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	req->index = dmac_index;

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	mutex_unlock(&pfvf->mbox.lock);

	return err;
}

static int otx2_dmacflt_remove_pfmac(struct otx2_nic *pf, u32 dmac_index)
{
	struct cgx_mac_addr_reset_req *req;
	int err;

	mutex_lock(&pf->mbox.lock);
	req = otx2_mbox_alloc_msg_cgx_mac_addr_reset(&pf->mbox);
	if (!req) {
		mutex_unlock(&pf->mbox.lock);
		return -ENOMEM;
	}
	req->index = dmac_index;

	err = otx2_sync_mbox_msg(&pf->mbox);

	mutex_unlock(&pf->mbox.lock);
	return err;
}

int otx2_dmacflt_remove(struct otx2_nic *pf, const u8 *mac,
			u32 bit_pos)
{
	u32 dmacindex = pf->flow_cfg->bmap_to_dmacindex[bit_pos];

	if (ether_addr_equal(mac, pf->netdev->dev_addr))
		return otx2_dmacflt_remove_pfmac(pf, dmacindex);
	else
		return otx2_dmacflt_do_remove(pf, mac, dmacindex);
}

/* CGX/RPM blocks support max unicast entries of 32.
 * on typical configuration MAC block associated
 * with 4 lmacs, each lmac will have 8 dmac entries
 */
int otx2_dmacflt_get_max_cnt(struct otx2_nic *pf)
{
	struct cgx_max_dmac_entries_get_rsp *rsp;
	struct msg_req *msg;
	int err;

	mutex_lock(&pf->mbox.lock);
	msg = otx2_mbox_alloc_msg_cgx_mac_max_entries_get(&pf->mbox);

	if (!msg) {
		mutex_unlock(&pf->mbox.lock);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pf->mbox);
	if (err)
		goto out;

	rsp = (struct cgx_max_dmac_entries_get_rsp *)
		     otx2_mbox_get_rsp(&pf->mbox.mbox, 0, &msg->hdr);

	if (IS_ERR_OR_NULL(rsp)) {
		err = -EINVAL;
		goto out;
	}

	pf->flow_cfg->dmacflt_max_flows = rsp->max_dmac_filters;

out:
	mutex_unlock(&pf->mbox.lock);
	return err;
}

int otx2_dmacflt_update(struct otx2_nic *pf, u8 *mac, u32 bit_pos)
{
	struct cgx_mac_addr_update_req *req;
	struct cgx_mac_addr_update_rsp *rsp;
	int rc;

	mutex_lock(&pf->mbox.lock);

	req = otx2_mbox_alloc_msg_cgx_mac_addr_update(&pf->mbox);

	if (!req) {
		mutex_unlock(&pf->mbox.lock);
		return -ENOMEM;
	}

	ether_addr_copy(req->mac_addr, mac);
	req->index = pf->flow_cfg->bmap_to_dmacindex[bit_pos];

	/* check the response and change index */

	rc = otx2_sync_mbox_msg(&pf->mbox);
	if (rc)
		goto out;

	rsp = (struct cgx_mac_addr_update_rsp *)
		otx2_mbox_get_rsp(&pf->mbox.mbox, 0, &req->hdr);
	if (IS_ERR(rsp)) {
		rc = PTR_ERR(rsp);
		goto out;
	}

	pf->flow_cfg->bmap_to_dmacindex[bit_pos] = rsp->index;

out:
	mutex_unlock(&pf->mbox.lock);
	return rc;
}
