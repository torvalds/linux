/* bnx2x_vfpf.c: QLogic Everest network driver.
 *
 * Copyright 2009-2013 Broadcom Corporation
 * Copyright 2014 QLogic Corporation
 * All rights reserved
 *
 * Unless you and QLogic execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other QLogic software provided under a
 * license other than the GPL, without QLogic's express prior written
 * consent.
 *
 * Maintained by: Ariel Elior <ariel.elior@qlogic.com>
 * Written by: Shmulik Ravid
 *	       Ariel Elior <ariel.elior@qlogic.com>
 */

#include "bnx2x.h"
#include "bnx2x_cmn.h"
#include <linux/crc32.h>

static int bnx2x_vfpf_teardown_queue(struct bnx2x *bp, int qidx);

/* place a given tlv on the tlv buffer at a given offset */
static void bnx2x_add_tlv(struct bnx2x *bp, void *tlvs_list,
			  u16 offset, u16 type, u16 length)
{
	struct channel_tlv *tl =
		(struct channel_tlv *)(tlvs_list + offset);

	tl->type = type;
	tl->length = length;
}

/* Clear the mailbox and init the header of the first tlv */
static void bnx2x_vfpf_prep(struct bnx2x *bp, struct vfpf_first_tlv *first_tlv,
			    u16 type, u16 length)
{
	mutex_lock(&bp->vf2pf_mutex);

	DP(BNX2X_MSG_IOV, "preparing to send %d tlv over vf pf channel\n",
	   type);

	/* Clear mailbox */
	memset(bp->vf2pf_mbox, 0, sizeof(struct bnx2x_vf_mbx_msg));

	/* init type and length */
	bnx2x_add_tlv(bp, &first_tlv->tl, 0, type, length);

	/* init first tlv header */
	first_tlv->resp_msg_offset = sizeof(bp->vf2pf_mbox->req);
}

/* releases the mailbox */
static void bnx2x_vfpf_finalize(struct bnx2x *bp,
				struct vfpf_first_tlv *first_tlv)
{
	DP(BNX2X_MSG_IOV, "done sending [%d] tlv over vf pf channel\n",
	   first_tlv->tl.type);

	mutex_unlock(&bp->vf2pf_mutex);
}

/* Finds a TLV by type in a TLV buffer; If found, returns pointer to the TLV */
static void *bnx2x_search_tlv_list(struct bnx2x *bp, void *tlvs_list,
				   enum channel_tlvs req_tlv)
{
	struct channel_tlv *tlv = (struct channel_tlv *)tlvs_list;

	do {
		if (tlv->type == req_tlv)
			return tlv;

		if (!tlv->length) {
			BNX2X_ERR("Found TLV with length 0\n");
			return NULL;
		}

		tlvs_list += tlv->length;
		tlv = (struct channel_tlv *)tlvs_list;
	} while (tlv->type != CHANNEL_TLV_LIST_END);

	DP(BNX2X_MSG_IOV, "TLV list does not contain %d TLV\n", req_tlv);

	return NULL;
}

/* list the types and lengths of the tlvs on the buffer */
static void bnx2x_dp_tlv_list(struct bnx2x *bp, void *tlvs_list)
{
	int i = 1;
	struct channel_tlv *tlv = (struct channel_tlv *)tlvs_list;

	while (tlv->type != CHANNEL_TLV_LIST_END) {
		/* output tlv */
		DP(BNX2X_MSG_IOV, "TLV number %d: type %d, length %d\n", i,
		   tlv->type, tlv->length);

		/* advance to next tlv */
		tlvs_list += tlv->length;

		/* cast general tlv list pointer to channel tlv header*/
		tlv = (struct channel_tlv *)tlvs_list;

		i++;

		/* break condition for this loop */
		if (i > MAX_TLVS_IN_LIST) {
			WARN(true, "corrupt tlvs");
			return;
		}
	}

	/* output last tlv */
	DP(BNX2X_MSG_IOV, "TLV number %d: type %d, length %d\n", i,
	   tlv->type, tlv->length);
}

/* test whether we support a tlv type */
bool bnx2x_tlv_supported(u16 tlvtype)
{
	return CHANNEL_TLV_NONE < tlvtype && tlvtype < CHANNEL_TLV_MAX;
}

static inline int bnx2x_pfvf_status_codes(int rc)
{
	switch (rc) {
	case 0:
		return PFVF_STATUS_SUCCESS;
	case -ENOMEM:
		return PFVF_STATUS_NO_RESOURCE;
	default:
		return PFVF_STATUS_FAILURE;
	}
}

static int bnx2x_send_msg2pf(struct bnx2x *bp, u8 *done, dma_addr_t msg_mapping)
{
	struct cstorm_vf_zone_data __iomem *zone_data =
		REG_ADDR(bp, PXP_VF_ADDR_CSDM_GLOBAL_START);
	int tout = 100, interval = 100; /* wait for 10 seconds */

	if (*done) {
		BNX2X_ERR("done was non zero before message to pf was sent\n");
		WARN_ON(true);
		return -EINVAL;
	}

	/* if PF indicated channel is down avoid sending message. Return success
	 * so calling flow can continue
	 */
	bnx2x_sample_bulletin(bp);
	if (bp->old_bulletin.valid_bitmap & 1 << CHANNEL_DOWN) {
		DP(BNX2X_MSG_IOV, "detecting channel down. Aborting message\n");
		*done = PFVF_STATUS_SUCCESS;
		return -EINVAL;
	}

	/* Write message address */
	writel(U64_LO(msg_mapping),
	       &zone_data->non_trigger.vf_pf_channel.msg_addr_lo);
	writel(U64_HI(msg_mapping),
	       &zone_data->non_trigger.vf_pf_channel.msg_addr_hi);

	/* make sure the address is written before FW accesses it */
	wmb();

	/* Trigger the PF FW */
	writeb_relaxed(1, &zone_data->trigger.vf_pf_channel.addr_valid);

	/* Wait for PF to complete */
	while ((tout >= 0) && (!*done)) {
		msleep(interval);
		tout -= 1;

		/* progress indicator - HV can take its own sweet time in
		 * answering VFs...
		 */
		DP_CONT(BNX2X_MSG_IOV, ".");
	}

	if (!*done) {
		BNX2X_ERR("PF response has timed out\n");
		return -EAGAIN;
	}
	DP(BNX2X_MSG_SP, "Got a response from PF\n");
	return 0;
}

static int bnx2x_get_vf_id(struct bnx2x *bp, u32 *vf_id)
{
	u32 me_reg;
	int tout = 10, interval = 100; /* Wait for 1 sec */

	do {
		/* pxp traps vf read of doorbells and returns me reg value */
		me_reg = readl(bp->doorbells);
		if (GOOD_ME_REG(me_reg))
			break;

		msleep(interval);

		BNX2X_ERR("Invalid ME register value: 0x%08x\n. Is pf driver up?",
			  me_reg);
	} while (tout-- > 0);

	if (!GOOD_ME_REG(me_reg)) {
		BNX2X_ERR("Invalid ME register value: 0x%08x\n", me_reg);
		return -EINVAL;
	}

	DP(BNX2X_MSG_IOV, "valid ME register value: 0x%08x\n", me_reg);

	*vf_id = (me_reg & ME_REG_VF_NUM_MASK) >> ME_REG_VF_NUM_SHIFT;

	return 0;
}

int bnx2x_vfpf_acquire(struct bnx2x *bp, u8 tx_count, u8 rx_count)
{
	int rc = 0, attempts = 0;
	struct vfpf_acquire_tlv *req = &bp->vf2pf_mbox->req.acquire;
	struct pfvf_acquire_resp_tlv *resp = &bp->vf2pf_mbox->resp.acquire_resp;
	struct vfpf_port_phys_id_resp_tlv *phys_port_resp;
	struct vfpf_fp_hsi_resp_tlv *fp_hsi_resp;
	u32 vf_id;
	bool resources_acquired = false;

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_ACQUIRE, sizeof(*req));

	if (bnx2x_get_vf_id(bp, &vf_id)) {
		rc = -EAGAIN;
		goto out;
	}

	req->vfdev_info.vf_id = vf_id;
	req->vfdev_info.vf_os = 0;
	req->vfdev_info.fp_hsi_ver = ETH_FP_HSI_VERSION;

	req->resc_request.num_rxqs = rx_count;
	req->resc_request.num_txqs = tx_count;
	req->resc_request.num_sbs = bp->igu_sb_cnt;
	req->resc_request.num_mac_filters = VF_ACQUIRE_MAC_FILTERS;
	req->resc_request.num_mc_filters = VF_ACQUIRE_MC_FILTERS;
	req->resc_request.num_vlan_filters = VF_ACQUIRE_VLAN_FILTERS;

	/* pf 2 vf bulletin board address */
	req->bulletin_addr = bp->pf2vf_bulletin_mapping;

	/* Request physical port identifier */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length,
		      CHANNEL_TLV_PHYS_PORT_ID, sizeof(struct channel_tlv));

	/* Bulletin support for bulletin board with length > legacy length */
	req->vfdev_info.caps |= VF_CAP_SUPPORT_EXT_BULLETIN;
	/* vlan filtering is supported */
	req->vfdev_info.caps |= VF_CAP_SUPPORT_VLAN_FILTER;

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req,
		      req->first_tlv.tl.length + sizeof(struct channel_tlv),
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	while (!resources_acquired) {
		DP(BNX2X_MSG_SP, "attempting to acquire resources\n");

		/* send acquire request */
		rc = bnx2x_send_msg2pf(bp,
				       &resp->hdr.status,
				       bp->vf2pf_mbox_mapping);

		/* PF timeout */
		if (rc)
			goto out;

		/* copy acquire response from buffer to bp */
		memcpy(&bp->acquire_resp, resp, sizeof(bp->acquire_resp));

		attempts++;

		/* test whether the PF accepted our request. If not, humble
		 * the request and try again.
		 */
		if (bp->acquire_resp.hdr.status == PFVF_STATUS_SUCCESS) {
			DP(BNX2X_MSG_SP, "resources acquired\n");
			resources_acquired = true;
		} else if (bp->acquire_resp.hdr.status ==
			   PFVF_STATUS_NO_RESOURCE &&
			   attempts < VF_ACQUIRE_THRESH) {
			DP(BNX2X_MSG_SP,
			   "PF unwilling to fulfill resource request. Try PF recommended amount\n");

			/* humble our request */
			req->resc_request.num_txqs =
				min(req->resc_request.num_txqs,
				    bp->acquire_resp.resc.num_txqs);
			req->resc_request.num_rxqs =
				min(req->resc_request.num_rxqs,
				    bp->acquire_resp.resc.num_rxqs);
			req->resc_request.num_sbs =
				min(req->resc_request.num_sbs,
				    bp->acquire_resp.resc.num_sbs);
			req->resc_request.num_mac_filters =
				min(req->resc_request.num_mac_filters,
				    bp->acquire_resp.resc.num_mac_filters);
			req->resc_request.num_vlan_filters =
				min(req->resc_request.num_vlan_filters,
				    bp->acquire_resp.resc.num_vlan_filters);
			req->resc_request.num_mc_filters =
				min(req->resc_request.num_mc_filters,
				    bp->acquire_resp.resc.num_mc_filters);

			/* Clear response buffer */
			memset(&bp->vf2pf_mbox->resp, 0,
			       sizeof(union pfvf_tlvs));
		} else {
			/* Determine reason of PF failure of acquire process */
			fp_hsi_resp = bnx2x_search_tlv_list(bp, resp,
							    CHANNEL_TLV_FP_HSI_SUPPORT);
			if (fp_hsi_resp && !fp_hsi_resp->is_supported)
				BNX2X_ERR("Old hypervisor - doesn't support current fastpath HSI version; Need to downgrade VF driver [or upgrade hypervisor]\n");
			else
				BNX2X_ERR("Failed to get the requested amount of resources: %d. Breaking...\n",
					  bp->acquire_resp.hdr.status);
			rc = -EAGAIN;
			goto out;
		}
	}

	/* Retrieve physical port id (if possible) */
	phys_port_resp = (struct vfpf_port_phys_id_resp_tlv *)
			 bnx2x_search_tlv_list(bp, resp,
					       CHANNEL_TLV_PHYS_PORT_ID);
	if (phys_port_resp) {
		memcpy(bp->phys_port_id, phys_port_resp->id, ETH_ALEN);
		bp->flags |= HAS_PHYS_PORT_ID;
	}

	/* Old Hypevisors might not even support the FP_HSI_SUPPORT TLV.
	 * If that's the case, we need to make certain required FW was
	 * supported by such a hypervisor [i.e., v0-v2].
	 */
	fp_hsi_resp = bnx2x_search_tlv_list(bp, resp,
					    CHANNEL_TLV_FP_HSI_SUPPORT);
	if (!fp_hsi_resp && (ETH_FP_HSI_VERSION > ETH_FP_HSI_VER_2)) {
		BNX2X_ERR("Old hypervisor - need to downgrade VF's driver\n");

		/* Since acquire succeeded on the PF side, we need to send a
		 * release message in order to allow future probes.
		 */
		bnx2x_vfpf_finalize(bp, &req->first_tlv);
		bnx2x_vfpf_release(bp);

		rc = -EINVAL;
		goto out;
	}

	/* get HW info */
	bp->common.chip_id |= (bp->acquire_resp.pfdev_info.chip_num & 0xffff);
	bp->link_params.chip_id = bp->common.chip_id;
	bp->db_size = bp->acquire_resp.pfdev_info.db_size;
	bp->common.int_block = INT_BLOCK_IGU;
	bp->common.chip_port_mode = CHIP_2_PORT_MODE;
	bp->igu_dsb_id = -1;
	bp->mf_ov = 0;
	bp->mf_mode = 0;
	bp->common.flash_size = 0;
	bp->flags |=
		NO_WOL_FLAG | NO_ISCSI_OOO_FLAG | NO_ISCSI_FLAG | NO_FCOE_FLAG;
	bp->igu_sb_cnt = bp->acquire_resp.resc.num_sbs;
	bp->igu_base_sb = bp->acquire_resp.resc.hw_sbs[0].hw_sb_id;
	bp->vlan_credit = bp->acquire_resp.resc.num_vlan_filters;

	strlcpy(bp->fw_ver, bp->acquire_resp.pfdev_info.fw_ver,
		sizeof(bp->fw_ver));

	if (is_valid_ether_addr(bp->acquire_resp.resc.current_mac_addr))
		eth_hw_addr_set(bp->dev,
				bp->acquire_resp.resc.current_mac_addr);

out:
	bnx2x_vfpf_finalize(bp, &req->first_tlv);
	return rc;
}

int bnx2x_vfpf_release(struct bnx2x *bp)
{
	struct vfpf_release_tlv *req = &bp->vf2pf_mbox->req.release;
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	u32 rc, vf_id;

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_RELEASE, sizeof(*req));

	if (bnx2x_get_vf_id(bp, &vf_id)) {
		rc = -EAGAIN;
		goto out;
	}

	req->vf_id = vf_id;

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	/* send release request */
	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);

	if (rc)
		/* PF timeout */
		goto out;

	if (resp->hdr.status == PFVF_STATUS_SUCCESS) {
		/* PF released us */
		DP(BNX2X_MSG_SP, "vf released\n");
	} else {
		/* PF reports error */
		BNX2X_ERR("PF failed our release request - are we out of sync? Response status: %d\n",
			  resp->hdr.status);
		rc = -EAGAIN;
		goto out;
	}
out:
	bnx2x_vfpf_finalize(bp, &req->first_tlv);

	return rc;
}

/* Tell PF about SB addresses */
int bnx2x_vfpf_init(struct bnx2x *bp)
{
	struct vfpf_init_tlv *req = &bp->vf2pf_mbox->req.init;
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	int rc, i;

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_INIT, sizeof(*req));

	/* status blocks */
	for_each_eth_queue(bp, i)
		req->sb_addr[i] = (dma_addr_t)bnx2x_fp(bp, i,
						       status_blk_mapping);

	/* statistics - requests only supports single queue for now */
	req->stats_addr = bp->fw_stats_data_mapping +
			  offsetof(struct bnx2x_fw_stats_data, queue_stats);

	req->stats_stride = sizeof(struct per_queue_stats);

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);
	if (rc)
		goto out;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		BNX2X_ERR("INIT VF failed: %d. Breaking...\n",
			  resp->hdr.status);
		rc = -EAGAIN;
		goto out;
	}

	DP(BNX2X_MSG_SP, "INIT VF Succeeded\n");
out:
	bnx2x_vfpf_finalize(bp, &req->first_tlv);

	return rc;
}

/* CLOSE VF - opposite to INIT_VF */
void bnx2x_vfpf_close_vf(struct bnx2x *bp)
{
	struct vfpf_close_tlv *req = &bp->vf2pf_mbox->req.close;
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	int i, rc;
	u32 vf_id;

	/* If we haven't got a valid VF id, there is no sense to
	 * continue with sending messages
	 */
	if (bnx2x_get_vf_id(bp, &vf_id))
		goto free_irq;

	/* Close the queues */
	for_each_queue(bp, i)
		bnx2x_vfpf_teardown_queue(bp, i);

	/* remove mac */
	bnx2x_vfpf_config_mac(bp, bp->dev->dev_addr, bp->fp->index, false);

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_CLOSE, sizeof(*req));

	req->vf_id = vf_id;

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);

	if (rc)
		BNX2X_ERR("Sending CLOSE failed. rc was: %d\n", rc);

	else if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		BNX2X_ERR("Sending CLOSE failed: pf response was %d\n",
			  resp->hdr.status);

	bnx2x_vfpf_finalize(bp, &req->first_tlv);

free_irq:
	/* Disable HW interrupts, NAPI */
	bnx2x_netif_stop(bp, 0);
	/* Delete all NAPI objects */
	bnx2x_del_all_napi(bp);

	/* Release IRQs */
	bnx2x_free_irq(bp);
}

static void bnx2x_leading_vfq_init(struct bnx2x *bp, struct bnx2x_virtf *vf,
				   struct bnx2x_vf_queue *q)
{
	u8 cl_id = vfq_cl_id(vf, q);
	u8 func_id = FW_VF_HANDLE(vf->abs_vfid);

	/* mac */
	bnx2x_init_mac_obj(bp, &q->mac_obj,
			   cl_id, q->cid, func_id,
			   bnx2x_vf_sp(bp, vf, mac_rdata),
			   bnx2x_vf_sp_map(bp, vf, mac_rdata),
			   BNX2X_FILTER_MAC_PENDING,
			   &vf->filter_state,
			   BNX2X_OBJ_TYPE_RX_TX,
			   &vf->vf_macs_pool);
	/* vlan */
	bnx2x_init_vlan_obj(bp, &q->vlan_obj,
			    cl_id, q->cid, func_id,
			    bnx2x_vf_sp(bp, vf, vlan_rdata),
			    bnx2x_vf_sp_map(bp, vf, vlan_rdata),
			    BNX2X_FILTER_VLAN_PENDING,
			    &vf->filter_state,
			    BNX2X_OBJ_TYPE_RX_TX,
			    &vf->vf_vlans_pool);
	/* vlan-mac */
	bnx2x_init_vlan_mac_obj(bp, &q->vlan_mac_obj,
				cl_id, q->cid, func_id,
				bnx2x_vf_sp(bp, vf, vlan_mac_rdata),
				bnx2x_vf_sp_map(bp, vf, vlan_mac_rdata),
				BNX2X_FILTER_VLAN_MAC_PENDING,
				&vf->filter_state,
				BNX2X_OBJ_TYPE_RX_TX,
				&vf->vf_macs_pool,
				&vf->vf_vlans_pool);
	/* mcast */
	bnx2x_init_mcast_obj(bp, &vf->mcast_obj, cl_id,
			     q->cid, func_id, func_id,
			     bnx2x_vf_sp(bp, vf, mcast_rdata),
			     bnx2x_vf_sp_map(bp, vf, mcast_rdata),
			     BNX2X_FILTER_MCAST_PENDING,
			     &vf->filter_state,
			     BNX2X_OBJ_TYPE_RX_TX);

	/* rss */
	bnx2x_init_rss_config_obj(bp, &vf->rss_conf_obj, cl_id, q->cid,
				  func_id, func_id,
				  bnx2x_vf_sp(bp, vf, rss_rdata),
				  bnx2x_vf_sp_map(bp, vf, rss_rdata),
				  BNX2X_FILTER_RSS_CONF_PENDING,
				  &vf->filter_state,
				  BNX2X_OBJ_TYPE_RX_TX);

	vf->leading_rss = cl_id;
	q->is_leading = true;
	q->sp_initialized = true;
}

/* ask the pf to open a queue for the vf */
int bnx2x_vfpf_setup_q(struct bnx2x *bp, struct bnx2x_fastpath *fp,
		       bool is_leading)
{
	struct vfpf_setup_q_tlv *req = &bp->vf2pf_mbox->req.setup_q;
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	u8 fp_idx = fp->index;
	u16 tpa_agg_size = 0, flags = 0;
	int rc;

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_SETUP_Q, sizeof(*req));

	/* select tpa mode to request */
	if (fp->mode != TPA_MODE_DISABLED) {
		flags |= VFPF_QUEUE_FLG_TPA;
		flags |= VFPF_QUEUE_FLG_TPA_IPV6;
		if (fp->mode == TPA_MODE_GRO)
			flags |= VFPF_QUEUE_FLG_TPA_GRO;
		tpa_agg_size = TPA_AGG_SIZE;
	}

	if (is_leading)
		flags |= VFPF_QUEUE_FLG_LEADING_RSS;

	/* calculate queue flags */
	flags |= VFPF_QUEUE_FLG_STATS;
	flags |= VFPF_QUEUE_FLG_CACHE_ALIGN;
	flags |= VFPF_QUEUE_FLG_VLAN;

	/* Common */
	req->vf_qid = fp_idx;
	req->param_valid = VFPF_RXQ_VALID | VFPF_TXQ_VALID;

	/* Rx */
	req->rxq.rcq_addr = fp->rx_comp_mapping;
	req->rxq.rcq_np_addr = fp->rx_comp_mapping + BCM_PAGE_SIZE;
	req->rxq.rxq_addr = fp->rx_desc_mapping;
	req->rxq.sge_addr = fp->rx_sge_mapping;
	req->rxq.vf_sb = fp_idx;
	req->rxq.sb_index = HC_INDEX_ETH_RX_CQ_CONS;
	req->rxq.hc_rate = bp->rx_ticks ? 1000000/bp->rx_ticks : 0;
	req->rxq.mtu = bp->dev->mtu;
	req->rxq.buf_sz = fp->rx_buf_size;
	req->rxq.sge_buf_sz = BCM_PAGE_SIZE * PAGES_PER_SGE;
	req->rxq.tpa_agg_sz = tpa_agg_size;
	req->rxq.max_sge_pkt = SGE_PAGE_ALIGN(bp->dev->mtu) >> SGE_PAGE_SHIFT;
	req->rxq.max_sge_pkt = ((req->rxq.max_sge_pkt + PAGES_PER_SGE - 1) &
			  (~(PAGES_PER_SGE-1))) >> PAGES_PER_SGE_SHIFT;
	req->rxq.flags = flags;
	req->rxq.drop_flags = 0;
	req->rxq.cache_line_log = BNX2X_RX_ALIGN_SHIFT;
	req->rxq.stat_id = -1; /* No stats at the moment */

	/* Tx */
	req->txq.txq_addr = fp->txdata_ptr[FIRST_TX_COS_INDEX]->tx_desc_mapping;
	req->txq.vf_sb = fp_idx;
	req->txq.sb_index = HC_INDEX_ETH_TX_CQ_CONS_COS0;
	req->txq.hc_rate = bp->tx_ticks ? 1000000/bp->tx_ticks : 0;
	req->txq.flags = flags;
	req->txq.traffic_type = LLFC_TRAFFIC_TYPE_NW;

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);
	if (rc)
		BNX2X_ERR("Sending SETUP_Q message for queue[%d] failed!\n",
			  fp_idx);

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		BNX2X_ERR("Status of SETUP_Q for queue[%d] is %d\n",
			  fp_idx, resp->hdr.status);
		rc = -EINVAL;
	}

	bnx2x_vfpf_finalize(bp, &req->first_tlv);

	return rc;
}

static int bnx2x_vfpf_teardown_queue(struct bnx2x *bp, int qidx)
{
	struct vfpf_q_op_tlv *req = &bp->vf2pf_mbox->req.q_op;
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	int rc;

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_TEARDOWN_Q,
			sizeof(*req));

	req->vf_qid = qidx;

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);

	if (rc) {
		BNX2X_ERR("Sending TEARDOWN for queue %d failed: %d\n", qidx,
			  rc);
		goto out;
	}

	/* PF failed the transaction */
	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		BNX2X_ERR("TEARDOWN for queue %d failed: %d\n", qidx,
			  resp->hdr.status);
		rc = -EINVAL;
	}

out:
	bnx2x_vfpf_finalize(bp, &req->first_tlv);

	return rc;
}

/* request pf to add a mac for the vf */
int bnx2x_vfpf_config_mac(struct bnx2x *bp, u8 *addr, u8 vf_qid, bool set)
{
	struct vfpf_set_q_filters_tlv *req = &bp->vf2pf_mbox->req.set_q_filters;
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	struct pf_vf_bulletin_content bulletin = bp->pf2vf_bulletin->content;
	int rc = 0;

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_SET_Q_FILTERS,
			sizeof(*req));

	req->flags = VFPF_SET_Q_FILTERS_MAC_VLAN_CHANGED;
	req->vf_qid = vf_qid;
	req->n_mac_vlan_filters = 1;

	req->filters[0].flags = VFPF_Q_FILTER_DEST_MAC_VALID;
	if (set)
		req->filters[0].flags |= VFPF_Q_FILTER_SET;

	/* sample bulletin board for new mac */
	bnx2x_sample_bulletin(bp);

	/* copy mac from device to request */
	memcpy(req->filters[0].mac, addr, ETH_ALEN);

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	/* send message to pf */
	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);
	if (rc) {
		BNX2X_ERR("failed to send message to pf. rc was %d\n", rc);
		goto out;
	}

	/* failure may mean PF was configured with a new mac for us */
	while (resp->hdr.status == PFVF_STATUS_FAILURE) {
		DP(BNX2X_MSG_IOV,
		   "vfpf SET MAC failed. Check bulletin board for new posts\n");

		/* copy mac from bulletin to device */
		eth_hw_addr_set(bp->dev, bulletin.mac);

		/* check if bulletin board was updated */
		if (bnx2x_sample_bulletin(bp) == PFVF_BULLETIN_UPDATED) {
			/* copy mac from device to request */
			memcpy(req->filters[0].mac, bp->dev->dev_addr,
			       ETH_ALEN);

			/* send message to pf */
			rc = bnx2x_send_msg2pf(bp, &resp->hdr.status,
					       bp->vf2pf_mbox_mapping);
		} else {
			/* no new info in bulletin */
			break;
		}
	}

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		BNX2X_ERR("vfpf SET MAC failed: %d\n", resp->hdr.status);
		rc = -EINVAL;
	}
out:
	bnx2x_vfpf_finalize(bp, &req->first_tlv);

	return rc;
}

/* request pf to config rss table for vf queues*/
int bnx2x_vfpf_config_rss(struct bnx2x *bp,
			  struct bnx2x_config_rss_params *params)
{
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	struct vfpf_rss_tlv *req = &bp->vf2pf_mbox->req.update_rss;
	int rc = 0;

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_UPDATE_RSS,
			sizeof(*req));

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	memcpy(req->ind_table, params->ind_table, T_ETH_INDIRECTION_TABLE_SIZE);
	memcpy(req->rss_key, params->rss_key, sizeof(params->rss_key));
	req->ind_table_size = T_ETH_INDIRECTION_TABLE_SIZE;
	req->rss_key_size = T_ETH_RSS_KEY;
	req->rss_result_mask = params->rss_result_mask;

	/* flags handled individually for backward/forward compatibility */
	if (params->rss_flags & (1 << BNX2X_RSS_MODE_DISABLED))
		req->rss_flags |= VFPF_RSS_MODE_DISABLED;
	if (params->rss_flags & (1 << BNX2X_RSS_MODE_REGULAR))
		req->rss_flags |= VFPF_RSS_MODE_REGULAR;
	if (params->rss_flags & (1 << BNX2X_RSS_SET_SRCH))
		req->rss_flags |= VFPF_RSS_SET_SRCH;
	if (params->rss_flags & (1 << BNX2X_RSS_IPV4))
		req->rss_flags |= VFPF_RSS_IPV4;
	if (params->rss_flags & (1 << BNX2X_RSS_IPV4_TCP))
		req->rss_flags |= VFPF_RSS_IPV4_TCP;
	if (params->rss_flags & (1 << BNX2X_RSS_IPV4_UDP))
		req->rss_flags |= VFPF_RSS_IPV4_UDP;
	if (params->rss_flags & (1 << BNX2X_RSS_IPV6))
		req->rss_flags |= VFPF_RSS_IPV6;
	if (params->rss_flags & (1 << BNX2X_RSS_IPV6_TCP))
		req->rss_flags |= VFPF_RSS_IPV6_TCP;
	if (params->rss_flags & (1 << BNX2X_RSS_IPV6_UDP))
		req->rss_flags |= VFPF_RSS_IPV6_UDP;

	DP(BNX2X_MSG_IOV, "rss flags %x\n", req->rss_flags);

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	/* send message to pf */
	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);
	if (rc) {
		BNX2X_ERR("failed to send message to pf. rc was %d\n", rc);
		goto out;
	}

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		/* Since older drivers don't support this feature (and VF has
		 * no way of knowing other than failing this), don't propagate
		 * an error in this case.
		 */
		DP(BNX2X_MSG_IOV,
		   "Failed to send rss message to PF over VF-PF channel [%d]\n",
		   resp->hdr.status);
	}
out:
	bnx2x_vfpf_finalize(bp, &req->first_tlv);

	return rc;
}

int bnx2x_vfpf_set_mcast(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct vfpf_set_q_filters_tlv *req = &bp->vf2pf_mbox->req.set_q_filters;
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	int rc = 0, i = 0;
	struct netdev_hw_addr *ha;

	if (bp->state != BNX2X_STATE_OPEN) {
		DP(NETIF_MSG_IFUP, "state is %x, returning\n", bp->state);
		return -EINVAL;
	}

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_SET_Q_FILTERS,
			sizeof(*req));

	/* Get Rx mode requested */
	DP(NETIF_MSG_IFUP, "dev->flags = %x\n", dev->flags);

	/* We support PFVF_MAX_MULTICAST_PER_VF mcast addresses tops */
	if (netdev_mc_count(dev) > PFVF_MAX_MULTICAST_PER_VF) {
		DP(NETIF_MSG_IFUP,
		   "VF supports not more than %d multicast MAC addresses\n",
		   PFVF_MAX_MULTICAST_PER_VF);
		rc = -EINVAL;
		goto out;
	}

	netdev_for_each_mc_addr(ha, dev) {
		DP(NETIF_MSG_IFUP, "Adding mcast MAC: %pM\n",
		   bnx2x_mc_addr(ha));
		memcpy(req->multicast[i], bnx2x_mc_addr(ha), ETH_ALEN);
		i++;
	}

	req->n_multicast = i;
	req->flags |= VFPF_SET_Q_FILTERS_MULTICAST_CHANGED;
	req->vf_qid = 0;

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);
	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);
	if (rc) {
		BNX2X_ERR("Sending a message failed: %d\n", rc);
		goto out;
	}

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		BNX2X_ERR("Set Rx mode/multicast failed: %d\n",
			  resp->hdr.status);
		rc = -EINVAL;
	}
out:
	bnx2x_vfpf_finalize(bp, &req->first_tlv);

	return rc;
}

/* request pf to add a vlan for the vf */
int bnx2x_vfpf_update_vlan(struct bnx2x *bp, u16 vid, u8 vf_qid, bool add)
{
	struct vfpf_set_q_filters_tlv *req = &bp->vf2pf_mbox->req.set_q_filters;
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	int rc = 0;

	if (!(bp->acquire_resp.pfdev_info.pf_cap & PFVF_CAP_VLAN_FILTER)) {
		DP(BNX2X_MSG_IOV, "HV does not support vlan filtering\n");
		return 0;
	}

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_SET_Q_FILTERS,
			sizeof(*req));

	req->flags = VFPF_SET_Q_FILTERS_MAC_VLAN_CHANGED;
	req->vf_qid = vf_qid;
	req->n_mac_vlan_filters = 1;

	req->filters[0].flags = VFPF_Q_FILTER_VLAN_TAG_VALID;

	if (add)
		req->filters[0].flags |= VFPF_Q_FILTER_SET;

	/* sample bulletin board for hypervisor vlan */
	bnx2x_sample_bulletin(bp);

	if (bp->shadow_bulletin.content.valid_bitmap & 1 << VLAN_VALID) {
		BNX2X_ERR("Hypervisor will decline the request, avoiding\n");
		rc = -EINVAL;
		goto out;
	}

	req->filters[0].vlan_tag = vid;

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	/* send message to pf */
	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);
	if (rc) {
		BNX2X_ERR("failed to send message to pf. rc was %d\n", rc);
		goto out;
	}

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		BNX2X_ERR("vfpf %s VLAN %d failed\n", add ? "add" : "del",
			  vid);
		rc = -EINVAL;
	}
out:
	bnx2x_vfpf_finalize(bp, &req->first_tlv);

	return rc;
}

int bnx2x_vfpf_storm_rx_mode(struct bnx2x *bp)
{
	int mode = bp->rx_mode;
	struct vfpf_set_q_filters_tlv *req = &bp->vf2pf_mbox->req.set_q_filters;
	struct pfvf_general_resp_tlv *resp = &bp->vf2pf_mbox->resp.general_resp;
	int rc;

	/* clear mailbox and prep first tlv */
	bnx2x_vfpf_prep(bp, &req->first_tlv, CHANNEL_TLV_SET_Q_FILTERS,
			sizeof(*req));

	DP(NETIF_MSG_IFUP, "Rx mode is %d\n", mode);

	/* Ignore everything accept MODE_NONE */
	if (mode  == BNX2X_RX_MODE_NONE) {
		req->rx_mask = VFPF_RX_MASK_ACCEPT_NONE;
	} else {
		/* Current PF driver will not look at the specific flags,
		 * but they are required when working with older drivers on hv.
		 */
		req->rx_mask = VFPF_RX_MASK_ACCEPT_MATCHED_MULTICAST;
		req->rx_mask |= VFPF_RX_MASK_ACCEPT_MATCHED_UNICAST;
		req->rx_mask |= VFPF_RX_MASK_ACCEPT_BROADCAST;
		if (mode == BNX2X_RX_MODE_PROMISC)
			req->rx_mask |= VFPF_RX_MASK_ACCEPT_ANY_VLAN;
	}

	if (bp->accept_any_vlan)
		req->rx_mask |= VFPF_RX_MASK_ACCEPT_ANY_VLAN;

	req->flags |= VFPF_SET_Q_FILTERS_RX_MASK_CHANGED;
	req->vf_qid = 0;

	/* add list termination tlv */
	bnx2x_add_tlv(bp, req, req->first_tlv.tl.length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* output tlvs list */
	bnx2x_dp_tlv_list(bp, req);

	rc = bnx2x_send_msg2pf(bp, &resp->hdr.status, bp->vf2pf_mbox_mapping);
	if (rc)
		BNX2X_ERR("Sending a message failed: %d\n", rc);

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		BNX2X_ERR("Set Rx mode failed: %d\n", resp->hdr.status);
		rc = -EINVAL;
	}

	bnx2x_vfpf_finalize(bp, &req->first_tlv);

	return rc;
}

/* General service functions */
static void storm_memset_vf_mbx_ack(struct bnx2x *bp, u16 abs_fid)
{
	u32 addr = BAR_CSTRORM_INTMEM +
		   CSTORM_VF_PF_CHANNEL_STATE_OFFSET(abs_fid);

	REG_WR8(bp, addr, VF_PF_CHANNEL_STATE_READY);
}

static void storm_memset_vf_mbx_valid(struct bnx2x *bp, u16 abs_fid)
{
	u32 addr = BAR_CSTRORM_INTMEM +
		   CSTORM_VF_PF_CHANNEL_VALID_OFFSET(abs_fid);

	REG_WR8(bp, addr, 1);
}

/* enable vf_pf mailbox (aka vf-pf-channel) */
void bnx2x_vf_enable_mbx(struct bnx2x *bp, u8 abs_vfid)
{
	bnx2x_vf_flr_clnup_epilog(bp, abs_vfid);

	/* enable the mailbox in the FW */
	storm_memset_vf_mbx_ack(bp, abs_vfid);
	storm_memset_vf_mbx_valid(bp, abs_vfid);

	/* enable the VF access to the mailbox */
	bnx2x_vf_enable_access(bp, abs_vfid);
}

/* this works only on !E1h */
static int bnx2x_copy32_vf_dmae(struct bnx2x *bp, u8 from_vf,
				dma_addr_t pf_addr, u8 vfid, u32 vf_addr_hi,
				u32 vf_addr_lo, u32 len32)
{
	struct dmae_command dmae;

	if (CHIP_IS_E1x(bp)) {
		BNX2X_ERR("Chip revision does not support VFs\n");
		return DMAE_NOT_RDY;
	}

	if (!bp->dmae_ready) {
		BNX2X_ERR("DMAE is not ready, can not copy\n");
		return DMAE_NOT_RDY;
	}

	/* set opcode and fixed command fields */
	bnx2x_prep_dmae_with_comp(bp, &dmae, DMAE_SRC_PCI, DMAE_DST_PCI);

	if (from_vf) {
		dmae.opcode_iov = (vfid << DMAE_COMMAND_SRC_VFID_SHIFT) |
			(DMAE_SRC_VF << DMAE_COMMAND_SRC_VFPF_SHIFT) |
			(DMAE_DST_PF << DMAE_COMMAND_DST_VFPF_SHIFT);

		dmae.opcode |= (DMAE_C_DST << DMAE_COMMAND_C_FUNC_SHIFT);

		dmae.src_addr_lo = vf_addr_lo;
		dmae.src_addr_hi = vf_addr_hi;
		dmae.dst_addr_lo = U64_LO(pf_addr);
		dmae.dst_addr_hi = U64_HI(pf_addr);
	} else {
		dmae.opcode_iov = (vfid << DMAE_COMMAND_DST_VFID_SHIFT) |
			(DMAE_DST_VF << DMAE_COMMAND_DST_VFPF_SHIFT) |
			(DMAE_SRC_PF << DMAE_COMMAND_SRC_VFPF_SHIFT);

		dmae.opcode |= (DMAE_C_SRC << DMAE_COMMAND_C_FUNC_SHIFT);

		dmae.src_addr_lo = U64_LO(pf_addr);
		dmae.src_addr_hi = U64_HI(pf_addr);
		dmae.dst_addr_lo = vf_addr_lo;
		dmae.dst_addr_hi = vf_addr_hi;
	}
	dmae.len = len32;

	/* issue the command and wait for completion */
	return bnx2x_issue_dmae_with_comp(bp, &dmae, bnx2x_sp(bp, wb_comp));
}

static void bnx2x_vf_mbx_resp_single_tlv(struct bnx2x *bp,
					 struct bnx2x_virtf *vf)
{
	struct bnx2x_vf_mbx *mbx = BP_VF_MBX(bp, vf->index);
	u16 length, type;

	/* prepare response */
	type = mbx->first_tlv.tl.type;
	length = type == CHANNEL_TLV_ACQUIRE ?
		sizeof(struct pfvf_acquire_resp_tlv) :
		sizeof(struct pfvf_general_resp_tlv);
	bnx2x_add_tlv(bp, &mbx->msg->resp, 0, type, length);
	bnx2x_add_tlv(bp, &mbx->msg->resp, length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));
}

static void bnx2x_vf_mbx_resp_send_msg(struct bnx2x *bp,
				       struct bnx2x_virtf *vf,
				       int vf_rc)
{
	struct bnx2x_vf_mbx *mbx = BP_VF_MBX(bp, vf->index);
	struct pfvf_general_resp_tlv *resp = &mbx->msg->resp.general_resp;
	dma_addr_t pf_addr;
	u64 vf_addr;
	int rc;

	bnx2x_dp_tlv_list(bp, resp);
	DP(BNX2X_MSG_IOV, "mailbox vf address hi 0x%x, lo 0x%x, offset 0x%x\n",
	   mbx->vf_addr_hi, mbx->vf_addr_lo, mbx->first_tlv.resp_msg_offset);

	resp->hdr.status = bnx2x_pfvf_status_codes(vf_rc);

	/* send response */
	vf_addr = HILO_U64(mbx->vf_addr_hi, mbx->vf_addr_lo) +
		  mbx->first_tlv.resp_msg_offset;
	pf_addr = mbx->msg_mapping +
		  offsetof(struct bnx2x_vf_mbx_msg, resp);

	/* Copy the response buffer. The first u64 is written afterwards, as
	 * the vf is sensitive to the header being written
	 */
	vf_addr += sizeof(u64);
	pf_addr += sizeof(u64);
	rc = bnx2x_copy32_vf_dmae(bp, false, pf_addr, vf->abs_vfid,
				  U64_HI(vf_addr),
				  U64_LO(vf_addr),
				  (sizeof(union pfvf_tlvs) - sizeof(u64))/4);
	if (rc) {
		BNX2X_ERR("Failed to copy response body to VF %d\n",
			  vf->abs_vfid);
		goto mbx_error;
	}
	vf_addr -= sizeof(u64);
	pf_addr -= sizeof(u64);

	/* ack the FW */
	storm_memset_vf_mbx_ack(bp, vf->abs_vfid);

	/* copy the response header including status-done field,
	 * must be last dmae, must be after FW is acked
	 */
	rc = bnx2x_copy32_vf_dmae(bp, false, pf_addr, vf->abs_vfid,
				  U64_HI(vf_addr),
				  U64_LO(vf_addr),
				  sizeof(u64)/4);

	/* unlock channel mutex */
	bnx2x_unlock_vf_pf_channel(bp, vf, mbx->first_tlv.tl.type);

	if (rc) {
		BNX2X_ERR("Failed to copy response status to VF %d\n",
			  vf->abs_vfid);
		goto mbx_error;
	}
	return;

mbx_error:
	bnx2x_vf_release(bp, vf);
}

static void bnx2x_vf_mbx_resp(struct bnx2x *bp,
			      struct bnx2x_virtf *vf,
			      int rc)
{
	bnx2x_vf_mbx_resp_single_tlv(bp, vf);
	bnx2x_vf_mbx_resp_send_msg(bp, vf, rc);
}

static void bnx2x_vf_mbx_resp_phys_port(struct bnx2x *bp,
					struct bnx2x_virtf *vf,
					void *buffer,
					u16 *offset)
{
	struct vfpf_port_phys_id_resp_tlv *port_id;

	if (!(bp->flags & HAS_PHYS_PORT_ID))
		return;

	bnx2x_add_tlv(bp, buffer, *offset, CHANNEL_TLV_PHYS_PORT_ID,
		      sizeof(struct vfpf_port_phys_id_resp_tlv));

	port_id = (struct vfpf_port_phys_id_resp_tlv *)
		  (((u8 *)buffer) + *offset);
	memcpy(port_id->id, bp->phys_port_id, ETH_ALEN);

	/* Offset should continue representing the offset to the tail
	 * of TLV data (outside this function scope)
	 */
	*offset += sizeof(struct vfpf_port_phys_id_resp_tlv);
}

static void bnx2x_vf_mbx_resp_fp_hsi_ver(struct bnx2x *bp,
					 struct bnx2x_virtf *vf,
					 void *buffer,
					 u16 *offset)
{
	struct vfpf_fp_hsi_resp_tlv *fp_hsi;

	bnx2x_add_tlv(bp, buffer, *offset, CHANNEL_TLV_FP_HSI_SUPPORT,
		      sizeof(struct vfpf_fp_hsi_resp_tlv));

	fp_hsi = (struct vfpf_fp_hsi_resp_tlv *)
		 (((u8 *)buffer) + *offset);
	fp_hsi->is_supported = (vf->fp_hsi > ETH_FP_HSI_VERSION) ? 0 : 1;

	/* Offset should continue representing the offset to the tail
	 * of TLV data (outside this function scope)
	 */
	*offset += sizeof(struct vfpf_fp_hsi_resp_tlv);
}

static void bnx2x_vf_mbx_acquire_resp(struct bnx2x *bp, struct bnx2x_virtf *vf,
				      struct bnx2x_vf_mbx *mbx, int vfop_status)
{
	int i;
	struct pfvf_acquire_resp_tlv *resp = &mbx->msg->resp.acquire_resp;
	struct pf_vf_resc *resc = &resp->resc;
	u8 status = bnx2x_pfvf_status_codes(vfop_status);
	u16 length;

	memset(resp, 0, sizeof(*resp));

	/* fill in pfdev info */
	resp->pfdev_info.chip_num = bp->common.chip_id;
	resp->pfdev_info.db_size = bp->db_size;
	resp->pfdev_info.indices_per_sb = HC_SB_MAX_INDICES_E2;
	resp->pfdev_info.pf_cap = (PFVF_CAP_RSS |
				   PFVF_CAP_TPA |
				   PFVF_CAP_TPA_UPDATE |
				   PFVF_CAP_VLAN_FILTER);
	bnx2x_fill_fw_str(bp, resp->pfdev_info.fw_ver,
			  sizeof(resp->pfdev_info.fw_ver));

	if (status == PFVF_STATUS_NO_RESOURCE ||
	    status == PFVF_STATUS_SUCCESS) {
		/* set resources numbers, if status equals NO_RESOURCE these
		 * are max possible numbers
		 */
		resc->num_rxqs = vf_rxq_count(vf) ? :
			bnx2x_vf_max_queue_cnt(bp, vf);
		resc->num_txqs = vf_txq_count(vf) ? :
			bnx2x_vf_max_queue_cnt(bp, vf);
		resc->num_sbs = vf_sb_count(vf);
		resc->num_mac_filters = vf_mac_rules_cnt(vf);
		resc->num_vlan_filters = vf_vlan_rules_cnt(vf);
		resc->num_mc_filters = 0;

		if (status == PFVF_STATUS_SUCCESS) {
			/* fill in the allocated resources */
			struct pf_vf_bulletin_content *bulletin =
				BP_VF_BULLETIN(bp, vf->index);

			for_each_vfq(vf, i)
				resc->hw_qid[i] =
					vfq_qzone_id(vf, vfq_get(vf, i));

			for_each_vf_sb(vf, i) {
				resc->hw_sbs[i].hw_sb_id = vf_igu_sb(vf, i);
				resc->hw_sbs[i].sb_qid = vf_hc_qzone(vf, i);
			}

			/* if a mac has been set for this vf, supply it */
			if (bulletin->valid_bitmap & 1 << MAC_ADDR_VALID) {
				memcpy(resc->current_mac_addr, bulletin->mac,
				       ETH_ALEN);
			}
		}
	}

	DP(BNX2X_MSG_IOV, "VF[%d] ACQUIRE_RESPONSE: pfdev_info- chip_num=0x%x, db_size=%d, idx_per_sb=%d, pf_cap=0x%x\n"
	   "resources- n_rxq-%d, n_txq-%d, n_sbs-%d, n_macs-%d, n_vlans-%d, n_mcs-%d, fw_ver: '%s'\n",
	   vf->abs_vfid,
	   resp->pfdev_info.chip_num,
	   resp->pfdev_info.db_size,
	   resp->pfdev_info.indices_per_sb,
	   resp->pfdev_info.pf_cap,
	   resc->num_rxqs,
	   resc->num_txqs,
	   resc->num_sbs,
	   resc->num_mac_filters,
	   resc->num_vlan_filters,
	   resc->num_mc_filters,
	   resp->pfdev_info.fw_ver);

	DP_CONT(BNX2X_MSG_IOV, "hw_qids- [ ");
	for (i = 0; i < vf_rxq_count(vf); i++)
		DP_CONT(BNX2X_MSG_IOV, "%d ", resc->hw_qid[i]);
	DP_CONT(BNX2X_MSG_IOV, "], sb_info- [ ");
	for (i = 0; i < vf_sb_count(vf); i++)
		DP_CONT(BNX2X_MSG_IOV, "%d:%d ",
			resc->hw_sbs[i].hw_sb_id,
			resc->hw_sbs[i].sb_qid);
	DP_CONT(BNX2X_MSG_IOV, "]\n");

	/* prepare response */
	length = sizeof(struct pfvf_acquire_resp_tlv);
	bnx2x_add_tlv(bp, &mbx->msg->resp, 0, CHANNEL_TLV_ACQUIRE, length);

	/* Handle possible VF requests for physical port identifiers.
	 * 'length' should continue to indicate the offset of the first empty
	 * place in the buffer (i.e., where next TLV should be inserted)
	 */
	if (bnx2x_search_tlv_list(bp, &mbx->msg->req,
				  CHANNEL_TLV_PHYS_PORT_ID))
		bnx2x_vf_mbx_resp_phys_port(bp, vf, &mbx->msg->resp, &length);

	/* `New' vfs will want to know if fastpath HSI is supported, since
	 * if that's not the case they could print into system log the fact
	 * the driver version must be updated.
	 */
	bnx2x_vf_mbx_resp_fp_hsi_ver(bp, vf, &mbx->msg->resp, &length);

	bnx2x_add_tlv(bp, &mbx->msg->resp, length, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	/* send the response */
	bnx2x_vf_mbx_resp_send_msg(bp, vf, vfop_status);
}

static bool bnx2x_vf_mbx_is_windows_vm(struct bnx2x *bp,
				       struct vfpf_acquire_tlv *acquire)
{
	/* Windows driver does one of three things:
	 * 1. Old driver doesn't have bulletin board address set.
	 * 2. 'Middle' driver sends mc_num == 32.
	 * 3. New driver sets the OS field.
	 */
	if (!acquire->bulletin_addr ||
	    acquire->resc_request.num_mc_filters == 32 ||
	    ((acquire->vfdev_info.vf_os & VF_OS_MASK) ==
	     VF_OS_WINDOWS))
		return true;

	return false;
}

static int bnx2x_vf_mbx_acquire_chk_dorq(struct bnx2x *bp,
					 struct bnx2x_virtf *vf,
					 struct bnx2x_vf_mbx *mbx)
{
	/* Linux drivers which correctly set the doorbell size also
	 * send a physical port request
	 */
	if (bnx2x_search_tlv_list(bp, &mbx->msg->req,
				  CHANNEL_TLV_PHYS_PORT_ID))
		return 0;

	/* Issue does not exist in windows VMs */
	if (bnx2x_vf_mbx_is_windows_vm(bp, &mbx->msg->req.acquire))
		return 0;

	return -EOPNOTSUPP;
}

static void bnx2x_vf_mbx_acquire(struct bnx2x *bp, struct bnx2x_virtf *vf,
				 struct bnx2x_vf_mbx *mbx)
{
	int rc;
	struct vfpf_acquire_tlv *acquire = &mbx->msg->req.acquire;

	/* log vfdef info */
	DP(BNX2X_MSG_IOV,
	   "VF[%d] ACQUIRE: vfdev_info- vf_id %d, vf_os %d resources- n_rxq-%d, n_txq-%d, n_sbs-%d, n_macs-%d, n_vlans-%d, n_mcs-%d\n",
	   vf->abs_vfid, acquire->vfdev_info.vf_id, acquire->vfdev_info.vf_os,
	   acquire->resc_request.num_rxqs, acquire->resc_request.num_txqs,
	   acquire->resc_request.num_sbs, acquire->resc_request.num_mac_filters,
	   acquire->resc_request.num_vlan_filters,
	   acquire->resc_request.num_mc_filters);

	/* Prevent VFs with old drivers from loading, since they calculate
	 * CIDs incorrectly requiring a VF-flr [VM reboot] in order to recover
	 * while being upgraded.
	 */
	rc = bnx2x_vf_mbx_acquire_chk_dorq(bp, vf, mbx);
	if (rc) {
		DP(BNX2X_MSG_IOV,
		   "VF [%d] - Can't support acquire request due to doorbell mismatch. Please update VM driver\n",
		   vf->abs_vfid);
		goto out;
	}

	/* Verify the VF fastpath HSI can be supported by the loaded FW.
	 * Linux vfs should be oblivious to changes between v0 and v2.
	 */
	if (bnx2x_vf_mbx_is_windows_vm(bp, &mbx->msg->req.acquire))
		vf->fp_hsi = acquire->vfdev_info.fp_hsi_ver;
	else
		vf->fp_hsi = max_t(u8, acquire->vfdev_info.fp_hsi_ver,
				   ETH_FP_HSI_VER_2);
	if (vf->fp_hsi > ETH_FP_HSI_VERSION) {
		DP(BNX2X_MSG_IOV,
		   "VF [%d] - Can't support acquire request since VF requests a FW version which is too new [%02x > %02x]\n",
		   vf->abs_vfid, acquire->vfdev_info.fp_hsi_ver,
		   ETH_FP_HSI_VERSION);
		rc = -EINVAL;
		goto out;
	}

	/* acquire the resources */
	rc = bnx2x_vf_acquire(bp, vf, &acquire->resc_request);

	/* store address of vf's bulletin board */
	vf->bulletin_map = acquire->bulletin_addr;
	if (acquire->vfdev_info.caps & VF_CAP_SUPPORT_EXT_BULLETIN) {
		DP(BNX2X_MSG_IOV, "VF[%d] supports long bulletin boards\n",
		   vf->abs_vfid);
		vf->cfg_flags |= VF_CFG_EXT_BULLETIN;
	} else {
		vf->cfg_flags &= ~VF_CFG_EXT_BULLETIN;
	}

	if (acquire->vfdev_info.caps & VF_CAP_SUPPORT_VLAN_FILTER) {
		DP(BNX2X_MSG_IOV, "VF[%d] supports vlan filtering\n",
		   vf->abs_vfid);
		vf->cfg_flags |= VF_CFG_VLAN_FILTER;
	} else {
		vf->cfg_flags &= ~VF_CFG_VLAN_FILTER;
	}

out:
	/* response */
	bnx2x_vf_mbx_acquire_resp(bp, vf, mbx, rc);
}

static void bnx2x_vf_mbx_init_vf(struct bnx2x *bp, struct bnx2x_virtf *vf,
			      struct bnx2x_vf_mbx *mbx)
{
	struct vfpf_init_tlv *init = &mbx->msg->req.init;
	int rc;

	/* record ghost addresses from vf message */
	vf->fw_stat_map = init->stats_addr;
	vf->stats_stride = init->stats_stride;
	rc = bnx2x_vf_init(bp, vf, (dma_addr_t *)init->sb_addr);

	/* set VF multiqueue statistics collection mode */
	if (init->flags & VFPF_INIT_FLG_STATS_COALESCE)
		vf->cfg_flags |= VF_CFG_STATS_COALESCE;

	/* Update VF's view of link state */
	if (vf->cfg_flags & VF_CFG_EXT_BULLETIN)
		bnx2x_iov_link_update_vf(bp, vf->index);

	/* response */
	bnx2x_vf_mbx_resp(bp, vf, rc);
}

/* convert MBX queue-flags to standard SP queue-flags */
static void bnx2x_vf_mbx_set_q_flags(struct bnx2x *bp, u32 mbx_q_flags,
				     unsigned long *sp_q_flags)
{
	if (mbx_q_flags & VFPF_QUEUE_FLG_TPA)
		__set_bit(BNX2X_Q_FLG_TPA, sp_q_flags);
	if (mbx_q_flags & VFPF_QUEUE_FLG_TPA_IPV6)
		__set_bit(BNX2X_Q_FLG_TPA_IPV6, sp_q_flags);
	if (mbx_q_flags & VFPF_QUEUE_FLG_TPA_GRO)
		__set_bit(BNX2X_Q_FLG_TPA_GRO, sp_q_flags);
	if (mbx_q_flags & VFPF_QUEUE_FLG_STATS)
		__set_bit(BNX2X_Q_FLG_STATS, sp_q_flags);
	if (mbx_q_flags & VFPF_QUEUE_FLG_VLAN)
		__set_bit(BNX2X_Q_FLG_VLAN, sp_q_flags);
	if (mbx_q_flags & VFPF_QUEUE_FLG_COS)
		__set_bit(BNX2X_Q_FLG_COS, sp_q_flags);
	if (mbx_q_flags & VFPF_QUEUE_FLG_HC)
		__set_bit(BNX2X_Q_FLG_HC, sp_q_flags);
	if (mbx_q_flags & VFPF_QUEUE_FLG_DHC)
		__set_bit(BNX2X_Q_FLG_DHC, sp_q_flags);
	if (mbx_q_flags & VFPF_QUEUE_FLG_LEADING_RSS)
		__set_bit(BNX2X_Q_FLG_LEADING_RSS, sp_q_flags);

	/* outer vlan removal is set according to PF's multi function mode */
	if (IS_MF_SD(bp))
		__set_bit(BNX2X_Q_FLG_OV, sp_q_flags);
}

static void bnx2x_vf_mbx_setup_q(struct bnx2x *bp, struct bnx2x_virtf *vf,
				 struct bnx2x_vf_mbx *mbx)
{
	struct vfpf_setup_q_tlv *setup_q = &mbx->msg->req.setup_q;
	struct bnx2x_vf_queue_construct_params qctor;
	int rc = 0;

	/* verify vf_qid */
	if (setup_q->vf_qid >= vf_rxq_count(vf)) {
		BNX2X_ERR("vf_qid %d invalid, max queue count is %d\n",
			  setup_q->vf_qid, vf_rxq_count(vf));
		rc = -EINVAL;
		goto response;
	}

	/* tx queues must be setup alongside rx queues thus if the rx queue
	 * is not marked as valid there's nothing to do.
	 */
	if (setup_q->param_valid & (VFPF_RXQ_VALID|VFPF_TXQ_VALID)) {
		struct bnx2x_vf_queue *q = vfq_get(vf, setup_q->vf_qid);
		unsigned long q_type = 0;

		struct bnx2x_queue_init_params *init_p;
		struct bnx2x_queue_setup_params *setup_p;

		if (bnx2x_vfq_is_leading(q))
			bnx2x_leading_vfq_init(bp, vf, q);

		/* re-init the VF operation context */
		memset(&qctor, 0 ,
		       sizeof(struct bnx2x_vf_queue_construct_params));
		setup_p = &qctor.prep_qsetup;
		init_p =  &qctor.qstate.params.init;

		/* activate immediately */
		__set_bit(BNX2X_Q_FLG_ACTIVE, &setup_p->flags);

		if (setup_q->param_valid & VFPF_TXQ_VALID) {
			struct bnx2x_txq_setup_params *txq_params =
				&setup_p->txq_params;

			__set_bit(BNX2X_Q_TYPE_HAS_TX, &q_type);

			/* save sb resource index */
			q->sb_idx = setup_q->txq.vf_sb;

			/* tx init */
			init_p->tx.hc_rate = setup_q->txq.hc_rate;
			init_p->tx.sb_cq_index = setup_q->txq.sb_index;

			bnx2x_vf_mbx_set_q_flags(bp, setup_q->txq.flags,
						 &init_p->tx.flags);

			/* tx setup - flags */
			bnx2x_vf_mbx_set_q_flags(bp, setup_q->txq.flags,
						 &setup_p->flags);

			/* tx setup - general, nothing */

			/* tx setup - tx */
			txq_params->dscr_map = setup_q->txq.txq_addr;
			txq_params->sb_cq_index = setup_q->txq.sb_index;
			txq_params->traffic_type = setup_q->txq.traffic_type;

			bnx2x_vfop_qctor_dump_tx(bp, vf, init_p, setup_p,
						 q->index, q->sb_idx);
		}

		if (setup_q->param_valid & VFPF_RXQ_VALID) {
			struct bnx2x_rxq_setup_params *rxq_params =
							&setup_p->rxq_params;

			__set_bit(BNX2X_Q_TYPE_HAS_RX, &q_type);

			/* Note: there is no support for different SBs
			 * for TX and RX
			 */
			q->sb_idx = setup_q->rxq.vf_sb;

			/* rx init */
			init_p->rx.hc_rate = setup_q->rxq.hc_rate;
			init_p->rx.sb_cq_index = setup_q->rxq.sb_index;
			bnx2x_vf_mbx_set_q_flags(bp, setup_q->rxq.flags,
						 &init_p->rx.flags);

			/* rx setup - flags */
			bnx2x_vf_mbx_set_q_flags(bp, setup_q->rxq.flags,
						 &setup_p->flags);

			/* rx setup - general */
			setup_p->gen_params.mtu = setup_q->rxq.mtu;

			/* rx setup - rx */
			rxq_params->drop_flags = setup_q->rxq.drop_flags;
			rxq_params->dscr_map = setup_q->rxq.rxq_addr;
			rxq_params->sge_map = setup_q->rxq.sge_addr;
			rxq_params->rcq_map = setup_q->rxq.rcq_addr;
			rxq_params->rcq_np_map = setup_q->rxq.rcq_np_addr;
			rxq_params->buf_sz = setup_q->rxq.buf_sz;
			rxq_params->tpa_agg_sz = setup_q->rxq.tpa_agg_sz;
			rxq_params->max_sges_pkt = setup_q->rxq.max_sge_pkt;
			rxq_params->sge_buf_sz = setup_q->rxq.sge_buf_sz;
			rxq_params->cache_line_log =
				setup_q->rxq.cache_line_log;
			rxq_params->sb_cq_index = setup_q->rxq.sb_index;

			/* rx setup - multicast engine */
			if (bnx2x_vfq_is_leading(q)) {
				u8 mcast_id = FW_VF_HANDLE(vf->abs_vfid);

				rxq_params->mcast_engine_id = mcast_id;
				__set_bit(BNX2X_Q_FLG_MCAST, &setup_p->flags);
			}

			bnx2x_vfop_qctor_dump_rx(bp, vf, init_p, setup_p,
						 q->index, q->sb_idx);
		}
		/* complete the preparations */
		bnx2x_vfop_qctor_prep(bp, vf, q, &qctor, q_type);

		rc = bnx2x_vf_queue_setup(bp, vf, q->index, &qctor);
		if (rc)
			goto response;
	}
response:
	bnx2x_vf_mbx_resp(bp, vf, rc);
}

static int bnx2x_vf_mbx_macvlan_list(struct bnx2x *bp,
				     struct bnx2x_virtf *vf,
				     struct vfpf_set_q_filters_tlv *tlv,
				     struct bnx2x_vf_mac_vlan_filters **pfl,
				     u32 type_flag)
{
	int i, j;
	struct bnx2x_vf_mac_vlan_filters *fl = NULL;

	fl = kzalloc(struct_size(fl, filters, tlv->n_mac_vlan_filters),
		     GFP_KERNEL);
	if (!fl)
		return -ENOMEM;

	for (i = 0, j = 0; i < tlv->n_mac_vlan_filters; i++) {
		struct vfpf_q_mac_vlan_filter *msg_filter = &tlv->filters[i];

		if ((msg_filter->flags & type_flag) != type_flag)
			continue;
		memset(&fl->filters[j], 0, sizeof(fl->filters[j]));
		if (type_flag & VFPF_Q_FILTER_DEST_MAC_VALID) {
			fl->filters[j].mac = msg_filter->mac;
			fl->filters[j].type |= BNX2X_VF_FILTER_MAC;
		}
		if (type_flag & VFPF_Q_FILTER_VLAN_TAG_VALID) {
			fl->filters[j].vid = msg_filter->vlan_tag;
			fl->filters[j].type |= BNX2X_VF_FILTER_VLAN;
		}
		fl->filters[j].add = !!(msg_filter->flags & VFPF_Q_FILTER_SET);
		fl->count++;
		j++;
	}
	if (!fl->count)
		kfree(fl);
	else
		*pfl = fl;

	return 0;
}

static int bnx2x_vf_filters_contain(struct vfpf_set_q_filters_tlv *filters,
				    u32 flags)
{
	int i, cnt = 0;

	for (i = 0; i < filters->n_mac_vlan_filters; i++)
		if  ((filters->filters[i].flags & flags) == flags)
			cnt++;

	return cnt;
}

static void bnx2x_vf_mbx_dp_q_filter(struct bnx2x *bp, int msglvl, int idx,
				       struct vfpf_q_mac_vlan_filter *filter)
{
	DP(msglvl, "MAC-VLAN[%d] -- flags=0x%x\n", idx, filter->flags);
	if (filter->flags & VFPF_Q_FILTER_VLAN_TAG_VALID)
		DP_CONT(msglvl, ", vlan=%d", filter->vlan_tag);
	if (filter->flags & VFPF_Q_FILTER_DEST_MAC_VALID)
		DP_CONT(msglvl, ", MAC=%pM", filter->mac);
	DP_CONT(msglvl, "\n");
}

static void bnx2x_vf_mbx_dp_q_filters(struct bnx2x *bp, int msglvl,
				       struct vfpf_set_q_filters_tlv *filters)
{
	int i;

	if (filters->flags & VFPF_SET_Q_FILTERS_MAC_VLAN_CHANGED)
		for (i = 0; i < filters->n_mac_vlan_filters; i++)
			bnx2x_vf_mbx_dp_q_filter(bp, msglvl, i,
						 &filters->filters[i]);

	if (filters->flags & VFPF_SET_Q_FILTERS_RX_MASK_CHANGED)
		DP(msglvl, "RX-MASK=0x%x\n", filters->rx_mask);

	if (filters->flags & VFPF_SET_Q_FILTERS_MULTICAST_CHANGED)
		for (i = 0; i < filters->n_multicast; i++)
			DP(msglvl, "MULTICAST=%pM\n", filters->multicast[i]);
}

#define VFPF_MAC_FILTER		VFPF_Q_FILTER_DEST_MAC_VALID
#define VFPF_VLAN_FILTER	VFPF_Q_FILTER_VLAN_TAG_VALID
#define VFPF_VLAN_MAC_FILTER	(VFPF_VLAN_FILTER | VFPF_MAC_FILTER)

static int bnx2x_vf_mbx_qfilters(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int rc = 0;

	struct vfpf_set_q_filters_tlv *msg =
		&BP_VF_MBX(bp, vf->index)->msg->req.set_q_filters;

	/* check for any mac/vlan changes */
	if (msg->flags & VFPF_SET_Q_FILTERS_MAC_VLAN_CHANGED) {
		struct bnx2x_vf_mac_vlan_filters *fl = NULL;

		/* build vlan-mac list */
		rc = bnx2x_vf_mbx_macvlan_list(bp, vf, msg, &fl,
					       VFPF_VLAN_MAC_FILTER);
		if (rc)
			goto op_err;

		if (fl) {

			/* set vlan-mac list */
			rc = bnx2x_vf_mac_vlan_config_list(bp, vf, fl,
							   msg->vf_qid,
							   false);
			if (rc)
				goto op_err;
		}

		/* build mac list */
		fl = NULL;

		rc = bnx2x_vf_mbx_macvlan_list(bp, vf, msg, &fl,
					       VFPF_MAC_FILTER);
		if (rc)
			goto op_err;

		if (fl) {
			/* set mac list */
			rc = bnx2x_vf_mac_vlan_config_list(bp, vf, fl,
							   msg->vf_qid,
							   false);
			if (rc)
				goto op_err;
		}

		/* build vlan list */
		fl = NULL;

		rc = bnx2x_vf_mbx_macvlan_list(bp, vf, msg, &fl,
					       VFPF_VLAN_FILTER);
		if (rc)
			goto op_err;

		if (fl) {
			/* set vlan list */
			rc = bnx2x_vf_mac_vlan_config_list(bp, vf, fl,
							   msg->vf_qid,
							   false);
			if (rc)
				goto op_err;
		}

	}

	if (msg->flags & VFPF_SET_Q_FILTERS_RX_MASK_CHANGED) {
		unsigned long accept = 0;
		struct pf_vf_bulletin_content *bulletin =
					BP_VF_BULLETIN(bp, vf->index);

		/* Ignore VF requested mode; instead set a regular mode */
		if (msg->rx_mask !=  VFPF_RX_MASK_ACCEPT_NONE) {
			__set_bit(BNX2X_ACCEPT_UNICAST, &accept);
			__set_bit(BNX2X_ACCEPT_MULTICAST, &accept);
			__set_bit(BNX2X_ACCEPT_BROADCAST, &accept);
		}

		/* any_vlan is not configured if HV is forcing VLAN
		 * any_vlan is configured if
		 *   1. VF does not support vlan filtering
		 *   OR
		 *   2. VF supports vlan filtering and explicitly requested it
		 */
		if (!(bulletin->valid_bitmap & (1 << VLAN_VALID)) &&
		    (!(vf->cfg_flags & VF_CFG_VLAN_FILTER) ||
		     msg->rx_mask & VFPF_RX_MASK_ACCEPT_ANY_VLAN))
			__set_bit(BNX2X_ACCEPT_ANY_VLAN, &accept);

		/* set rx-mode */
		rc = bnx2x_vf_rxmode(bp, vf, msg->vf_qid, accept);
		if (rc)
			goto op_err;
	}

	if (msg->flags & VFPF_SET_Q_FILTERS_MULTICAST_CHANGED) {
		/* set mcasts */
		rc = bnx2x_vf_mcast(bp, vf, msg->multicast,
				    msg->n_multicast, false);
		if (rc)
			goto op_err;
	}
op_err:
	if (rc)
		BNX2X_ERR("QFILTERS[%d:%d] error: rc %d\n",
			  vf->abs_vfid, msg->vf_qid, rc);
	return rc;
}

static int bnx2x_filters_validate_mac(struct bnx2x *bp,
				      struct bnx2x_virtf *vf,
				      struct vfpf_set_q_filters_tlv *filters)
{
	struct pf_vf_bulletin_content *bulletin = BP_VF_BULLETIN(bp, vf->index);
	int rc = 0;

	/* if a mac was already set for this VF via the set vf mac ndo, we only
	 * accept mac configurations of that mac. Why accept them at all?
	 * because PF may have been unable to configure the mac at the time
	 * since queue was not set up.
	 */
	if (bulletin->valid_bitmap & 1 << MAC_ADDR_VALID) {
		struct vfpf_q_mac_vlan_filter *filter = NULL;
		int i;

		for (i = 0; i < filters->n_mac_vlan_filters; i++) {
			if (!(filters->filters[i].flags &
			      VFPF_Q_FILTER_DEST_MAC_VALID))
				continue;

			/* once a mac was set by ndo can only accept
			 * a single mac...
			 */
			if (filter) {
				BNX2X_ERR("VF[%d] requested the addition of multiple macs after set_vf_mac ndo was called [%d filters]\n",
					  vf->abs_vfid,
					  filters->n_mac_vlan_filters);
				rc = -EPERM;
				goto response;
			}

			filter = &filters->filters[i];
		}

		/* ...and only the mac set by the ndo */
		if (filter &&
		    !ether_addr_equal(filter->mac, bulletin->mac)) {
			BNX2X_ERR("VF[%d] requested the addition of a mac address not matching the one configured by set_vf_mac ndo\n",
				  vf->abs_vfid);

			rc = -EPERM;
			goto response;
		}
	}

response:
	return rc;
}

static int bnx2x_filters_validate_vlan(struct bnx2x *bp,
				       struct bnx2x_virtf *vf,
				       struct vfpf_set_q_filters_tlv *filters)
{
	struct pf_vf_bulletin_content *bulletin = BP_VF_BULLETIN(bp, vf->index);
	int rc = 0;

	/* if vlan was set by hypervisor we don't allow guest to config vlan */
	if (bulletin->valid_bitmap & 1 << VLAN_VALID) {
		/* search for vlan filters */

		if (bnx2x_vf_filters_contain(filters,
					     VFPF_Q_FILTER_VLAN_TAG_VALID)) {
			BNX2X_ERR("VF[%d] attempted to configure vlan but one was already set by Hypervisor. Aborting request\n",
				  vf->abs_vfid);
			rc = -EPERM;
			goto response;
		}
	}

	/* verify vf_qid */
	if (filters->vf_qid > vf_rxq_count(vf)) {
		rc = -EPERM;
		goto response;
	}

response:
	return rc;
}

static void bnx2x_vf_mbx_set_q_filters(struct bnx2x *bp,
				       struct bnx2x_virtf *vf,
				       struct bnx2x_vf_mbx *mbx)
{
	struct vfpf_set_q_filters_tlv *filters = &mbx->msg->req.set_q_filters;
	int rc;

	rc = bnx2x_filters_validate_mac(bp, vf, filters);
	if (rc)
		goto response;

	rc = bnx2x_filters_validate_vlan(bp, vf, filters);
	if (rc)
		goto response;

	DP(BNX2X_MSG_IOV, "VF[%d] Q_FILTERS: queue[%d]\n",
	   vf->abs_vfid,
	   filters->vf_qid);

	/* print q_filter message */
	bnx2x_vf_mbx_dp_q_filters(bp, BNX2X_MSG_IOV, filters);

	rc = bnx2x_vf_mbx_qfilters(bp, vf);
response:
	bnx2x_vf_mbx_resp(bp, vf, rc);
}

static void bnx2x_vf_mbx_teardown_q(struct bnx2x *bp, struct bnx2x_virtf *vf,
				    struct bnx2x_vf_mbx *mbx)
{
	int qid = mbx->msg->req.q_op.vf_qid;
	int rc;

	DP(BNX2X_MSG_IOV, "VF[%d] Q_TEARDOWN: vf_qid=%d\n",
	   vf->abs_vfid, qid);

	rc = bnx2x_vf_queue_teardown(bp, vf, qid);
	bnx2x_vf_mbx_resp(bp, vf, rc);
}

static void bnx2x_vf_mbx_close_vf(struct bnx2x *bp, struct bnx2x_virtf *vf,
				  struct bnx2x_vf_mbx *mbx)
{
	int rc;

	DP(BNX2X_MSG_IOV, "VF[%d] VF_CLOSE\n", vf->abs_vfid);

	rc = bnx2x_vf_close(bp, vf);
	bnx2x_vf_mbx_resp(bp, vf, rc);
}

static void bnx2x_vf_mbx_release_vf(struct bnx2x *bp, struct bnx2x_virtf *vf,
				    struct bnx2x_vf_mbx *mbx)
{
	int rc;

	DP(BNX2X_MSG_IOV, "VF[%d] VF_RELEASE\n", vf->abs_vfid);

	rc = bnx2x_vf_free(bp, vf);
	bnx2x_vf_mbx_resp(bp, vf, rc);
}

static void bnx2x_vf_mbx_update_rss(struct bnx2x *bp, struct bnx2x_virtf *vf,
				    struct bnx2x_vf_mbx *mbx)
{
	struct bnx2x_config_rss_params rss;
	struct vfpf_rss_tlv *rss_tlv = &mbx->msg->req.update_rss;
	int rc = 0;

	if (rss_tlv->ind_table_size != T_ETH_INDIRECTION_TABLE_SIZE ||
	    rss_tlv->rss_key_size != T_ETH_RSS_KEY) {
		BNX2X_ERR("failing rss configuration of vf %d due to size mismatch\n",
			  vf->index);
		rc = -EINVAL;
		goto mbx_resp;
	}

	memset(&rss, 0, sizeof(struct bnx2x_config_rss_params));

	/* set vfop params according to rss tlv */
	memcpy(rss.ind_table, rss_tlv->ind_table,
	       T_ETH_INDIRECTION_TABLE_SIZE);
	memcpy(rss.rss_key, rss_tlv->rss_key, sizeof(rss_tlv->rss_key));
	rss.rss_obj = &vf->rss_conf_obj;
	rss.rss_result_mask = rss_tlv->rss_result_mask;

	/* flags handled individually for backward/forward compatibility */
	rss.rss_flags = 0;
	rss.ramrod_flags = 0;

	if (rss_tlv->rss_flags & VFPF_RSS_MODE_DISABLED)
		__set_bit(BNX2X_RSS_MODE_DISABLED, &rss.rss_flags);
	if (rss_tlv->rss_flags & VFPF_RSS_MODE_REGULAR)
		__set_bit(BNX2X_RSS_MODE_REGULAR, &rss.rss_flags);
	if (rss_tlv->rss_flags & VFPF_RSS_SET_SRCH)
		__set_bit(BNX2X_RSS_SET_SRCH, &rss.rss_flags);
	if (rss_tlv->rss_flags & VFPF_RSS_IPV4)
		__set_bit(BNX2X_RSS_IPV4, &rss.rss_flags);
	if (rss_tlv->rss_flags & VFPF_RSS_IPV4_TCP)
		__set_bit(BNX2X_RSS_IPV4_TCP, &rss.rss_flags);
	if (rss_tlv->rss_flags & VFPF_RSS_IPV4_UDP)
		__set_bit(BNX2X_RSS_IPV4_UDP, &rss.rss_flags);
	if (rss_tlv->rss_flags & VFPF_RSS_IPV6)
		__set_bit(BNX2X_RSS_IPV6, &rss.rss_flags);
	if (rss_tlv->rss_flags & VFPF_RSS_IPV6_TCP)
		__set_bit(BNX2X_RSS_IPV6_TCP, &rss.rss_flags);
	if (rss_tlv->rss_flags & VFPF_RSS_IPV6_UDP)
		__set_bit(BNX2X_RSS_IPV6_UDP, &rss.rss_flags);

	if ((!(rss_tlv->rss_flags & VFPF_RSS_IPV4_TCP) &&
	     rss_tlv->rss_flags & VFPF_RSS_IPV4_UDP) ||
	    (!(rss_tlv->rss_flags & VFPF_RSS_IPV6_TCP) &&
	     rss_tlv->rss_flags & VFPF_RSS_IPV6_UDP)) {
		BNX2X_ERR("about to hit a FW assert. aborting...\n");
		rc = -EINVAL;
		goto mbx_resp;
	}

	rc = bnx2x_vf_rss_update(bp, vf, &rss);
mbx_resp:
	bnx2x_vf_mbx_resp(bp, vf, rc);
}

static int bnx2x_validate_tpa_params(struct bnx2x *bp,
				       struct vfpf_tpa_tlv *tpa_tlv)
{
	int rc = 0;

	if (tpa_tlv->tpa_client_info.max_sges_for_packet >
	    U_ETH_MAX_SGES_FOR_PACKET) {
		rc = -EINVAL;
		BNX2X_ERR("TPA update: max_sges received %d, max is %d\n",
			  tpa_tlv->tpa_client_info.max_sges_for_packet,
			  U_ETH_MAX_SGES_FOR_PACKET);
	}

	if (tpa_tlv->tpa_client_info.max_tpa_queues > MAX_AGG_QS(bp)) {
		rc = -EINVAL;
		BNX2X_ERR("TPA update: max_tpa_queues received %d, max is %d\n",
			  tpa_tlv->tpa_client_info.max_tpa_queues,
			  MAX_AGG_QS(bp));
	}

	return rc;
}

static void bnx2x_vf_mbx_update_tpa(struct bnx2x *bp, struct bnx2x_virtf *vf,
				    struct bnx2x_vf_mbx *mbx)
{
	struct bnx2x_queue_update_tpa_params vf_op_params;
	struct vfpf_tpa_tlv *tpa_tlv = &mbx->msg->req.update_tpa;
	int rc = 0;

	memset(&vf_op_params, 0, sizeof(vf_op_params));

	if (bnx2x_validate_tpa_params(bp, tpa_tlv))
		goto mbx_resp;

	vf_op_params.complete_on_both_clients =
		tpa_tlv->tpa_client_info.complete_on_both_clients;
	vf_op_params.dont_verify_thr =
		tpa_tlv->tpa_client_info.dont_verify_thr;
	vf_op_params.max_agg_sz =
		tpa_tlv->tpa_client_info.max_agg_size;
	vf_op_params.max_sges_pkt =
		tpa_tlv->tpa_client_info.max_sges_for_packet;
	vf_op_params.max_tpa_queues =
		tpa_tlv->tpa_client_info.max_tpa_queues;
	vf_op_params.sge_buff_sz =
		tpa_tlv->tpa_client_info.sge_buff_size;
	vf_op_params.sge_pause_thr_high =
		tpa_tlv->tpa_client_info.sge_pause_thr_high;
	vf_op_params.sge_pause_thr_low =
		tpa_tlv->tpa_client_info.sge_pause_thr_low;
	vf_op_params.tpa_mode =
		tpa_tlv->tpa_client_info.tpa_mode;
	vf_op_params.update_ipv4 =
		tpa_tlv->tpa_client_info.update_ipv4;
	vf_op_params.update_ipv6 =
		tpa_tlv->tpa_client_info.update_ipv6;

	rc = bnx2x_vf_tpa_update(bp, vf, tpa_tlv, &vf_op_params);

mbx_resp:
	bnx2x_vf_mbx_resp(bp, vf, rc);
}

/* dispatch request */
static void bnx2x_vf_mbx_request(struct bnx2x *bp, struct bnx2x_virtf *vf,
				  struct bnx2x_vf_mbx *mbx)
{
	int i;

	if (vf->state == VF_LOST) {
		/* Just ack the FW and return if VFs are lost
		 * in case of parity error. VFs are supposed to be timedout
		 * on waiting for PF response.
		 */
		DP(BNX2X_MSG_IOV,
		   "VF 0x%x lost, not handling the request\n", vf->abs_vfid);

		storm_memset_vf_mbx_ack(bp, vf->abs_vfid);
		return;
	}

	/* check if tlv type is known */
	if (bnx2x_tlv_supported(mbx->first_tlv.tl.type)) {
		/* Lock the per vf op mutex and note the locker's identity.
		 * The unlock will take place in mbx response.
		 */
		bnx2x_lock_vf_pf_channel(bp, vf, mbx->first_tlv.tl.type);

		/* switch on the opcode */
		switch (mbx->first_tlv.tl.type) {
		case CHANNEL_TLV_ACQUIRE:
			bnx2x_vf_mbx_acquire(bp, vf, mbx);
			return;
		case CHANNEL_TLV_INIT:
			bnx2x_vf_mbx_init_vf(bp, vf, mbx);
			return;
		case CHANNEL_TLV_SETUP_Q:
			bnx2x_vf_mbx_setup_q(bp, vf, mbx);
			return;
		case CHANNEL_TLV_SET_Q_FILTERS:
			bnx2x_vf_mbx_set_q_filters(bp, vf, mbx);
			return;
		case CHANNEL_TLV_TEARDOWN_Q:
			bnx2x_vf_mbx_teardown_q(bp, vf, mbx);
			return;
		case CHANNEL_TLV_CLOSE:
			bnx2x_vf_mbx_close_vf(bp, vf, mbx);
			return;
		case CHANNEL_TLV_RELEASE:
			bnx2x_vf_mbx_release_vf(bp, vf, mbx);
			return;
		case CHANNEL_TLV_UPDATE_RSS:
			bnx2x_vf_mbx_update_rss(bp, vf, mbx);
			return;
		case CHANNEL_TLV_UPDATE_TPA:
			bnx2x_vf_mbx_update_tpa(bp, vf, mbx);
			return;
		}

	} else {
		/* unknown TLV - this may belong to a VF driver from the future
		 * - a version written after this PF driver was written, which
		 * supports features unknown as of yet. Too bad since we don't
		 * support them. Or this may be because someone wrote a crappy
		 * VF driver and is sending garbage over the channel.
		 */
		BNX2X_ERR("unknown TLV. type %d length %d vf->state was %d. first 20 bytes of mailbox buffer:\n",
			  mbx->first_tlv.tl.type, mbx->first_tlv.tl.length,
			  vf->state);
		for (i = 0; i < 20; i++)
			DP_CONT(BNX2X_MSG_IOV, "%x ",
				mbx->msg->req.tlv_buf_size.tlv_buffer[i]);
	}

	/* can we respond to VF (do we have an address for it?) */
	if (vf->state == VF_ACQUIRED || vf->state == VF_ENABLED) {
		/* notify the VF that we do not support this request */
		bnx2x_vf_mbx_resp(bp, vf, PFVF_STATUS_NOT_SUPPORTED);
	} else {
		/* can't send a response since this VF is unknown to us
		 * just ack the FW to release the mailbox and unlock
		 * the channel.
		 */
		storm_memset_vf_mbx_ack(bp, vf->abs_vfid);
		/* Firmware ack should be written before unlocking channel */
		bnx2x_unlock_vf_pf_channel(bp, vf, mbx->first_tlv.tl.type);
	}
}

void bnx2x_vf_mbx_schedule(struct bnx2x *bp,
			   struct vf_pf_event_data *vfpf_event)
{
	u8 vf_idx;

	DP(BNX2X_MSG_IOV,
	   "vf pf event received: vfid %d, address_hi %x, address lo %x",
	   vfpf_event->vf_id, vfpf_event->msg_addr_hi, vfpf_event->msg_addr_lo);
	/* Sanity checks consider removing later */

	/* check if the vf_id is valid */
	if (vfpf_event->vf_id - BP_VFDB(bp)->sriov.first_vf_in_pf >
	    BNX2X_NR_VIRTFN(bp)) {
		BNX2X_ERR("Illegal vf_id %d max allowed: %d\n",
			  vfpf_event->vf_id, BNX2X_NR_VIRTFN(bp));
		return;
	}

	vf_idx = bnx2x_vf_idx_by_abs_fid(bp, vfpf_event->vf_id);

	/* Update VFDB with current message and schedule its handling */
	mutex_lock(&BP_VFDB(bp)->event_mutex);
	BP_VF_MBX(bp, vf_idx)->vf_addr_hi =
		le32_to_cpu(vfpf_event->msg_addr_hi);
	BP_VF_MBX(bp, vf_idx)->vf_addr_lo =
		le32_to_cpu(vfpf_event->msg_addr_lo);
	BP_VFDB(bp)->event_occur |= (1ULL << vf_idx);
	mutex_unlock(&BP_VFDB(bp)->event_mutex);

	bnx2x_schedule_iov_task(bp, BNX2X_IOV_HANDLE_VF_MSG);
}

/* handle new vf-pf messages */
void bnx2x_vf_mbx(struct bnx2x *bp)
{
	struct bnx2x_vfdb *vfdb = BP_VFDB(bp);
	u64 events;
	u8 vf_idx;
	int rc;

	if (!vfdb)
		return;

	mutex_lock(&vfdb->event_mutex);
	events = vfdb->event_occur;
	vfdb->event_occur = 0;
	mutex_unlock(&vfdb->event_mutex);

	for_each_vf(bp, vf_idx) {
		struct bnx2x_vf_mbx *mbx = BP_VF_MBX(bp, vf_idx);
		struct bnx2x_virtf *vf = BP_VF(bp, vf_idx);

		/* Handle VFs which have pending events */
		if (!(events & (1ULL << vf_idx)))
			continue;

		DP(BNX2X_MSG_IOV,
		   "Handling vf pf event vfid %d, address: [%x:%x], resp_offset 0x%x\n",
		   vf_idx, mbx->vf_addr_hi, mbx->vf_addr_lo,
		   mbx->first_tlv.resp_msg_offset);

		/* dmae to get the VF request */
		rc = bnx2x_copy32_vf_dmae(bp, true, mbx->msg_mapping,
					  vf->abs_vfid, mbx->vf_addr_hi,
					  mbx->vf_addr_lo,
					  sizeof(union vfpf_tlvs)/4);
		if (rc) {
			BNX2X_ERR("Failed to copy request VF %d\n",
				  vf->abs_vfid);
			bnx2x_vf_release(bp, vf);
			return;
		}

		/* process the VF message header */
		mbx->first_tlv = mbx->msg->req.first_tlv;

		/* Clean response buffer to refrain from falsely
		 * seeing chains.
		 */
		memset(&mbx->msg->resp, 0, sizeof(union pfvf_tlvs));

		/* dispatch the request (will prepare the response) */
		bnx2x_vf_mbx_request(bp, vf, mbx);
	}
}

void bnx2x_vf_bulletin_finalize(struct pf_vf_bulletin_content *bulletin,
				bool support_long)
{
	/* Older VFs contain a bug where they can't check CRC for bulletin
	 * boards of length greater than legacy size.
	 */
	bulletin->length = support_long ? BULLETIN_CONTENT_SIZE :
					  BULLETIN_CONTENT_LEGACY_SIZE;
	bulletin->crc = bnx2x_crc_vf_bulletin(bulletin);
}

/* propagate local bulletin board to vf */
int bnx2x_post_vf_bulletin(struct bnx2x *bp, int vf)
{
	struct pf_vf_bulletin_content *bulletin = BP_VF_BULLETIN(bp, vf);
	dma_addr_t pf_addr = BP_VF_BULLETIN_DMA(bp)->mapping +
		vf * BULLETIN_CONTENT_SIZE;
	dma_addr_t vf_addr = bnx2x_vf(bp, vf, bulletin_map);
	int rc;

	/* can only update vf after init took place */
	if (bnx2x_vf(bp, vf, state) != VF_ENABLED &&
	    bnx2x_vf(bp, vf, state) != VF_ACQUIRED)
		return 0;

	/* increment bulletin board version and compute crc */
	bulletin->version++;
	bnx2x_vf_bulletin_finalize(bulletin,
				   (bnx2x_vf(bp, vf, cfg_flags) &
				    VF_CFG_EXT_BULLETIN) ? true : false);

	/* propagate bulletin board via dmae to vm memory */
	rc = bnx2x_copy32_vf_dmae(bp, false, pf_addr,
				  bnx2x_vf(bp, vf, abs_vfid), U64_HI(vf_addr),
				  U64_LO(vf_addr), bulletin->length / 4);
	return rc;
}
