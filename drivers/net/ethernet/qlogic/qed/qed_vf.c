/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include "qed.h"
#include "qed_sriov.h"
#include "qed_vf.h"

static void *qed_vf_pf_prep(struct qed_hwfn *p_hwfn, u16 type, u16 length)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	void *p_tlv;

	/* This lock is released when we receive PF's response
	 * in qed_send_msg2pf().
	 * So, qed_vf_pf_prep() and qed_send_msg2pf()
	 * must come in sequence.
	 */
	mutex_lock(&(p_iov->mutex));

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "preparing to send 0x%04x tlv over vf pf channel\n",
		   type);

	/* Reset Requst offset */
	p_iov->offset = (u8 *)p_iov->vf2pf_request;

	/* Clear mailbox - both request and reply */
	memset(p_iov->vf2pf_request, 0, sizeof(union vfpf_tlvs));
	memset(p_iov->pf2vf_reply, 0, sizeof(union pfvf_tlvs));

	/* Init type and length */
	p_tlv = qed_add_tlv(p_hwfn, &p_iov->offset, type, length);

	/* Init first tlv header */
	((struct vfpf_first_tlv *)p_tlv)->reply_address =
	    (u64)p_iov->pf2vf_reply_phys;

	return p_tlv;
}

static int qed_send_msg2pf(struct qed_hwfn *p_hwfn, u8 *done, u32 resp_size)
{
	union vfpf_tlvs *p_req = p_hwfn->vf_iov_info->vf2pf_request;
	struct ustorm_trigger_vf_zone trigger;
	struct ustorm_vf_zone *zone_data;
	int rc = 0, time = 100;

	zone_data = (struct ustorm_vf_zone *)PXP_VF_BAR0_START_USDM_ZONE_B;

	/* output tlvs list */
	qed_dp_tlv_list(p_hwfn, p_req);

	/* need to add the END TLV to the message size */
	resp_size += sizeof(struct channel_list_end_tlv);

	/* Send TLVs over HW channel */
	memset(&trigger, 0, sizeof(struct ustorm_trigger_vf_zone));
	trigger.vf_pf_msg_valid = 1;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "VF -> PF [%02x] message: [%08x, %08x] --> %p, %08x --> %p\n",
		   GET_FIELD(p_hwfn->hw_info.concrete_fid,
			     PXP_CONCRETE_FID_PFID),
		   upper_32_bits(p_hwfn->vf_iov_info->vf2pf_request_phys),
		   lower_32_bits(p_hwfn->vf_iov_info->vf2pf_request_phys),
		   &zone_data->non_trigger.vf_pf_msg_addr,
		   *((u32 *)&trigger), &zone_data->trigger);

	REG_WR(p_hwfn,
	       (uintptr_t)&zone_data->non_trigger.vf_pf_msg_addr.lo,
	       lower_32_bits(p_hwfn->vf_iov_info->vf2pf_request_phys));

	REG_WR(p_hwfn,
	       (uintptr_t)&zone_data->non_trigger.vf_pf_msg_addr.hi,
	       upper_32_bits(p_hwfn->vf_iov_info->vf2pf_request_phys));

	/* The message data must be written first, to prevent trigger before
	 * data is written.
	 */
	wmb();

	REG_WR(p_hwfn, (uintptr_t)&zone_data->trigger, *((u32 *)&trigger));

	/* When PF would be done with the response, it would write back to the
	 * `done' address. Poll until then.
	 */
	while ((!*done) && time) {
		msleep(25);
		time--;
	}

	if (!*done) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF <-- PF Timeout [Type %d]\n",
			   p_req->first_tlv.tl.type);
		rc = -EBUSY;
		goto exit;
	} else {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "PF response: %d [Type %d]\n",
			   *done, p_req->first_tlv.tl.type);
	}

exit:
	mutex_unlock(&(p_hwfn->vf_iov_info->mutex));

	return rc;
}

#define VF_ACQUIRE_THRESH 3
static void qed_vf_pf_acquire_reduce_resc(struct qed_hwfn *p_hwfn,
					  struct vf_pf_resc_request *p_req,
					  struct pf_vf_resc *p_resp)
{
	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "PF unwilling to fullill resource request: rxq [%02x/%02x] txq [%02x/%02x] sbs [%02x/%02x] mac [%02x/%02x] vlan [%02x/%02x] mc [%02x/%02x]. Try PF recommended amount\n",
		   p_req->num_rxqs,
		   p_resp->num_rxqs,
		   p_req->num_rxqs,
		   p_resp->num_txqs,
		   p_req->num_sbs,
		   p_resp->num_sbs,
		   p_req->num_mac_filters,
		   p_resp->num_mac_filters,
		   p_req->num_vlan_filters,
		   p_resp->num_vlan_filters,
		   p_req->num_mc_filters, p_resp->num_mc_filters);

	/* humble our request */
	p_req->num_txqs = p_resp->num_txqs;
	p_req->num_rxqs = p_resp->num_rxqs;
	p_req->num_sbs = p_resp->num_sbs;
	p_req->num_mac_filters = p_resp->num_mac_filters;
	p_req->num_vlan_filters = p_resp->num_vlan_filters;
	p_req->num_mc_filters = p_resp->num_mc_filters;
}

static int qed_vf_pf_acquire(struct qed_hwfn *p_hwfn)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_acquire_resp_tlv *resp = &p_iov->pf2vf_reply->acquire_resp;
	struct pf_vf_pfdev_info *pfdev_info = &resp->pfdev_info;
	struct vf_pf_resc_request *p_resc;
	bool resources_acquired = false;
	struct vfpf_acquire_tlv *req;
	int rc = 0, attempts = 0;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_ACQUIRE, sizeof(*req));
	p_resc = &req->resc_request;

	/* starting filling the request */
	req->vfdev_info.opaque_fid = p_hwfn->hw_info.opaque_fid;

	p_resc->num_rxqs = QED_MAX_VF_CHAINS_PER_PF;
	p_resc->num_txqs = QED_MAX_VF_CHAINS_PER_PF;
	p_resc->num_sbs = QED_MAX_VF_CHAINS_PER_PF;
	p_resc->num_mac_filters = QED_ETH_VF_NUM_MAC_FILTERS;
	p_resc->num_vlan_filters = QED_ETH_VF_NUM_VLAN_FILTERS;

	req->vfdev_info.os_type = VFPF_ACQUIRE_OS_LINUX;
	req->vfdev_info.fw_major = FW_MAJOR_VERSION;
	req->vfdev_info.fw_minor = FW_MINOR_VERSION;
	req->vfdev_info.fw_revision = FW_REVISION_VERSION;
	req->vfdev_info.fw_engineering = FW_ENGINEERING_VERSION;
	req->vfdev_info.eth_fp_hsi_major = ETH_HSI_VER_MAJOR;
	req->vfdev_info.eth_fp_hsi_minor = ETH_HSI_VER_MINOR;

	/* Fill capability field with any non-deprecated config we support */
	req->vfdev_info.capabilities |= VFPF_ACQUIRE_CAP_100G;

	/* pf 2 vf bulletin board address */
	req->bulletin_addr = p_iov->bulletin.phys;
	req->bulletin_size = p_iov->bulletin.size;

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	while (!resources_acquired) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV, "attempting to acquire resources\n");

		/* send acquire request */
		rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
		if (rc)
			return rc;

		/* copy acquire response from buffer to p_hwfn */
		memcpy(&p_iov->acquire_resp, resp, sizeof(p_iov->acquire_resp));

		attempts++;

		if (resp->hdr.status == PFVF_STATUS_SUCCESS) {
			/* PF agrees to allocate our resources */
			if (!(resp->pfdev_info.capabilities &
			      PFVF_ACQUIRE_CAP_POST_FW_OVERRIDE)) {
				DP_INFO(p_hwfn,
					"PF is using old incompatible driver; Either downgrade driver or request provider to update hypervisor version\n");
				return -EINVAL;
			}
			DP_VERBOSE(p_hwfn, QED_MSG_IOV, "resources acquired\n");
			resources_acquired = true;
		} else if (resp->hdr.status == PFVF_STATUS_NO_RESOURCE &&
			   attempts < VF_ACQUIRE_THRESH) {
			qed_vf_pf_acquire_reduce_resc(p_hwfn, p_resc,
						      &resp->resc);

			/* Clear response buffer */
			memset(p_iov->pf2vf_reply, 0, sizeof(union pfvf_tlvs));
		} else if ((resp->hdr.status == PFVF_STATUS_NOT_SUPPORTED) &&
			   pfdev_info->major_fp_hsi &&
			   (pfdev_info->major_fp_hsi != ETH_HSI_VER_MAJOR)) {
			DP_NOTICE(p_hwfn,
				  "PF uses an incompatible fastpath HSI %02x.%02x [VF requires %02x.%02x]. Please change to a VF driver using %02x.xx.\n",
				  pfdev_info->major_fp_hsi,
				  pfdev_info->minor_fp_hsi,
				  ETH_HSI_VER_MAJOR,
				  ETH_HSI_VER_MINOR, pfdev_info->major_fp_hsi);
			return -EINVAL;
		} else {
			DP_ERR(p_hwfn,
			       "PF returned error %d to VF acquisition request\n",
			       resp->hdr.status);
			return -EAGAIN;
		}
	}

	/* Update bulletin board size with response from PF */
	p_iov->bulletin.size = resp->bulletin_size;

	/* get HW info */
	p_hwfn->cdev->type = resp->pfdev_info.dev_type;
	p_hwfn->cdev->chip_rev = resp->pfdev_info.chip_rev;

	p_hwfn->cdev->chip_num = pfdev_info->chip_num & 0xffff;

	/* Learn of the possibility of CMT */
	if (IS_LEAD_HWFN(p_hwfn)) {
		if (resp->pfdev_info.capabilities & PFVF_ACQUIRE_CAP_100G) {
			DP_NOTICE(p_hwfn, "100g VF\n");
			p_hwfn->cdev->num_hwfns = 2;
		}
	}

	if (ETH_HSI_VER_MINOR &&
	    (resp->pfdev_info.minor_fp_hsi < ETH_HSI_VER_MINOR)) {
		DP_INFO(p_hwfn,
			"PF is using older fastpath HSI; %02x.%02x is configured\n",
			ETH_HSI_VER_MAJOR, resp->pfdev_info.minor_fp_hsi);
	}

	return 0;
}

int qed_vf_hw_prepare(struct qed_hwfn *p_hwfn)
{
	struct qed_vf_iov *p_iov;
	u32 reg;

	/* Set number of hwfns - might be overriden once leading hwfn learns
	 * actual configuration from PF.
	 */
	if (IS_LEAD_HWFN(p_hwfn))
		p_hwfn->cdev->num_hwfns = 1;

	/* Set the doorbell bar. Assumption: regview is set */
	p_hwfn->doorbells = (u8 __iomem *)p_hwfn->regview +
					  PXP_VF_BAR0_START_DQ;

	reg = PXP_VF_BAR0_ME_OPAQUE_ADDRESS;
	p_hwfn->hw_info.opaque_fid = (u16)REG_RD(p_hwfn, reg);

	reg = PXP_VF_BAR0_ME_CONCRETE_ADDRESS;
	p_hwfn->hw_info.concrete_fid = REG_RD(p_hwfn, reg);

	/* Allocate vf sriov info */
	p_iov = kzalloc(sizeof(*p_iov), GFP_KERNEL);
	if (!p_iov) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct qed_sriov'\n");
		return -ENOMEM;
	}

	/* Allocate vf2pf msg */
	p_iov->vf2pf_request = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
						  sizeof(union vfpf_tlvs),
						  &p_iov->vf2pf_request_phys,
						  GFP_KERNEL);
	if (!p_iov->vf2pf_request) {
		DP_NOTICE(p_hwfn,
			  "Failed to allocate `vf2pf_request' DMA memory\n");
		goto free_p_iov;
	}

	p_iov->pf2vf_reply = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
						sizeof(union pfvf_tlvs),
						&p_iov->pf2vf_reply_phys,
						GFP_KERNEL);
	if (!p_iov->pf2vf_reply) {
		DP_NOTICE(p_hwfn,
			  "Failed to allocate `pf2vf_reply' DMA memory\n");
		goto free_vf2pf_request;
	}

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "VF's Request mailbox [%p virt 0x%llx phys], Response mailbox [%p virt 0x%llx phys]\n",
		   p_iov->vf2pf_request,
		   (u64) p_iov->vf2pf_request_phys,
		   p_iov->pf2vf_reply, (u64)p_iov->pf2vf_reply_phys);

	/* Allocate Bulletin board */
	p_iov->bulletin.size = sizeof(struct qed_bulletin_content);
	p_iov->bulletin.p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
						    p_iov->bulletin.size,
						    &p_iov->bulletin.phys,
						    GFP_KERNEL);
	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "VF's bulletin Board [%p virt 0x%llx phys 0x%08x bytes]\n",
		   p_iov->bulletin.p_virt,
		   (u64)p_iov->bulletin.phys, p_iov->bulletin.size);

	mutex_init(&p_iov->mutex);

	p_hwfn->vf_iov_info = p_iov;

	p_hwfn->hw_info.personality = QED_PCI_ETH;

	return qed_vf_pf_acquire(p_hwfn);

free_vf2pf_request:
	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  sizeof(union vfpf_tlvs),
			  p_iov->vf2pf_request, p_iov->vf2pf_request_phys);
free_p_iov:
	kfree(p_iov);

	return -ENOMEM;
}

int qed_vf_pf_rxq_start(struct qed_hwfn *p_hwfn,
			u8 rx_qid,
			u16 sb,
			u8 sb_index,
			u16 bd_max_bytes,
			dma_addr_t bd_chain_phys_addr,
			dma_addr_t cqe_pbl_addr,
			u16 cqe_pbl_size, void __iomem **pp_prod)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_start_queue_resp_tlv *resp;
	struct vfpf_start_rxq_tlv *req;
	int rc;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_START_RXQ, sizeof(*req));

	req->rx_qid = rx_qid;
	req->cqe_pbl_addr = cqe_pbl_addr;
	req->cqe_pbl_size = cqe_pbl_size;
	req->rxq_addr = bd_chain_phys_addr;
	req->hw_sb = sb;
	req->sb_index = sb_index;
	req->bd_max_bytes = bd_max_bytes;
	req->stat_id = -1;

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->queue_start;
	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EINVAL;

	/* Learn the address of the producer from the response */
	if (pp_prod) {
		u64 init_prod_val = 0;

		*pp_prod = (u8 __iomem *)p_hwfn->regview + resp->offset;
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Rxq[0x%02x]: producer at %p [offset 0x%08x]\n",
			   rx_qid, *pp_prod, resp->offset);

		/* Init the rcq, rx bd and rx sge (if valid) producers to 0 */
		__internal_ram_wr(p_hwfn, *pp_prod, sizeof(u64),
				  (u32 *)&init_prod_val);
	}

	return rc;
}

int qed_vf_pf_rxq_stop(struct qed_hwfn *p_hwfn, u16 rx_qid, bool cqe_completion)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_stop_rxqs_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	int rc;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_STOP_RXQS, sizeof(*req));

	req->rx_qid = rx_qid;
	req->num_rxqs = 1;
	req->cqe_completion = cqe_completion;

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EINVAL;

	return rc;
}

int qed_vf_pf_txq_start(struct qed_hwfn *p_hwfn,
			u16 tx_queue_id,
			u16 sb,
			u8 sb_index,
			dma_addr_t pbl_addr,
			u16 pbl_size, void __iomem **pp_doorbell)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_start_txq_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	int rc;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_START_TXQ, sizeof(*req));

	req->tx_qid = tx_queue_id;

	/* Tx */
	req->pbl_addr = pbl_addr;
	req->pbl_size = pbl_size;
	req->hw_sb = sb;
	req->sb_index = sb_index;

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EINVAL;

	if (pp_doorbell) {
		u8 cid = p_iov->acquire_resp.resc.cid[tx_queue_id];

		*pp_doorbell = (u8 __iomem *)p_hwfn->doorbells +
					     qed_db_addr(cid, DQ_DEMS_LEGACY);
	}

	return rc;
}

int qed_vf_pf_txq_stop(struct qed_hwfn *p_hwfn, u16 tx_qid)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_stop_txqs_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	int rc;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_STOP_TXQS, sizeof(*req));

	req->tx_qid = tx_qid;
	req->num_txqs = 1;

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EINVAL;

	return rc;
}

int qed_vf_pf_vport_start(struct qed_hwfn *p_hwfn,
			  u8 vport_id,
			  u16 mtu,
			  u8 inner_vlan_removal,
			  enum qed_tpa_mode tpa_mode,
			  u8 max_buffers_per_cqe, u8 only_untagged)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_vport_start_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	int rc, i;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_VPORT_START, sizeof(*req));

	req->mtu = mtu;
	req->vport_id = vport_id;
	req->inner_vlan_removal = inner_vlan_removal;
	req->tpa_mode = tpa_mode;
	req->max_buffers_per_cqe = max_buffers_per_cqe;
	req->only_untagged = only_untagged;

	/* status blocks */
	for (i = 0; i < p_hwfn->vf_iov_info->acquire_resp.resc.num_sbs; i++)
		if (p_hwfn->sbs_info[i])
			req->sb_addr[i] = p_hwfn->sbs_info[i]->sb_phys;

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EINVAL;

	return rc;
}

int qed_vf_pf_vport_stop(struct qed_hwfn *p_hwfn)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *resp = &p_iov->pf2vf_reply->default_resp;
	int rc;

	/* clear mailbox and prep first tlv */
	qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_VPORT_TEARDOWN,
		       sizeof(struct vfpf_first_tlv));

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EINVAL;

	return rc;
}

static bool
qed_vf_handle_vp_update_is_needed(struct qed_hwfn *p_hwfn,
				  struct qed_sp_vport_update_params *p_data,
				  u16 tlv)
{
	switch (tlv) {
	case CHANNEL_TLV_VPORT_UPDATE_ACTIVATE:
		return !!(p_data->update_vport_active_rx_flg ||
			  p_data->update_vport_active_tx_flg);
	case CHANNEL_TLV_VPORT_UPDATE_TX_SWITCH:
		return !!p_data->update_tx_switching_flg;
	case CHANNEL_TLV_VPORT_UPDATE_VLAN_STRIP:
		return !!p_data->update_inner_vlan_removal_flg;
	case CHANNEL_TLV_VPORT_UPDATE_ACCEPT_ANY_VLAN:
		return !!p_data->update_accept_any_vlan_flg;
	case CHANNEL_TLV_VPORT_UPDATE_MCAST:
		return !!p_data->update_approx_mcast_flg;
	case CHANNEL_TLV_VPORT_UPDATE_ACCEPT_PARAM:
		return !!(p_data->accept_flags.update_rx_mode_config ||
			  p_data->accept_flags.update_tx_mode_config);
	case CHANNEL_TLV_VPORT_UPDATE_RSS:
		return !!p_data->rss_params;
	case CHANNEL_TLV_VPORT_UPDATE_SGE_TPA:
		return !!p_data->sge_tpa_params;
	default:
		DP_INFO(p_hwfn, "Unexpected vport-update TLV[%d]\n",
			tlv);
		return false;
	}
}

static void
qed_vf_handle_vp_update_tlvs_resp(struct qed_hwfn *p_hwfn,
				  struct qed_sp_vport_update_params *p_data)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *p_resp;
	u16 tlv;

	for (tlv = CHANNEL_TLV_VPORT_UPDATE_ACTIVATE;
	     tlv < CHANNEL_TLV_VPORT_UPDATE_MAX; tlv++) {
		if (!qed_vf_handle_vp_update_is_needed(p_hwfn, p_data, tlv))
			continue;

		p_resp = (struct pfvf_def_resp_tlv *)
			 qed_iov_search_list_tlvs(p_hwfn, p_iov->pf2vf_reply,
						  tlv);
		if (p_resp && p_resp->hdr.status)
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "TLV[%d] Configuration %s\n",
				   tlv,
				   (p_resp && p_resp->hdr.status) ? "succeeded"
								  : "failed");
	}
}

int qed_vf_pf_vport_update(struct qed_hwfn *p_hwfn,
			   struct qed_sp_vport_update_params *p_params)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_vport_update_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	u8 update_rx, update_tx;
	u32 resp_size = 0;
	u16 size, tlv;
	int rc;

	resp = &p_iov->pf2vf_reply->default_resp;
	resp_size = sizeof(*resp);

	update_rx = p_params->update_vport_active_rx_flg;
	update_tx = p_params->update_vport_active_tx_flg;

	/* clear mailbox and prep header tlv */
	qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_VPORT_UPDATE, sizeof(*req));

	/* Prepare extended tlvs */
	if (update_rx || update_tx) {
		struct vfpf_vport_update_activate_tlv *p_act_tlv;

		size = sizeof(struct vfpf_vport_update_activate_tlv);
		p_act_tlv = qed_add_tlv(p_hwfn, &p_iov->offset,
					CHANNEL_TLV_VPORT_UPDATE_ACTIVATE,
					size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		if (update_rx) {
			p_act_tlv->update_rx = update_rx;
			p_act_tlv->active_rx = p_params->vport_active_rx_flg;
		}

		if (update_tx) {
			p_act_tlv->update_tx = update_tx;
			p_act_tlv->active_tx = p_params->vport_active_tx_flg;
		}
	}

	if (p_params->update_tx_switching_flg) {
		struct vfpf_vport_update_tx_switch_tlv *p_tx_switch_tlv;

		size = sizeof(struct vfpf_vport_update_tx_switch_tlv);
		tlv = CHANNEL_TLV_VPORT_UPDATE_TX_SWITCH;
		p_tx_switch_tlv = qed_add_tlv(p_hwfn, &p_iov->offset,
					      tlv, size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		p_tx_switch_tlv->tx_switching = p_params->tx_switching_flg;
	}

	if (p_params->update_approx_mcast_flg) {
		struct vfpf_vport_update_mcast_bin_tlv *p_mcast_tlv;

		size = sizeof(struct vfpf_vport_update_mcast_bin_tlv);
		p_mcast_tlv = qed_add_tlv(p_hwfn, &p_iov->offset,
					  CHANNEL_TLV_VPORT_UPDATE_MCAST, size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		memcpy(p_mcast_tlv->bins, p_params->bins,
		       sizeof(unsigned long) * ETH_MULTICAST_MAC_BINS_IN_REGS);
	}

	update_rx = p_params->accept_flags.update_rx_mode_config;
	update_tx = p_params->accept_flags.update_tx_mode_config;

	if (update_rx || update_tx) {
		struct vfpf_vport_update_accept_param_tlv *p_accept_tlv;

		tlv = CHANNEL_TLV_VPORT_UPDATE_ACCEPT_PARAM;
		size = sizeof(struct vfpf_vport_update_accept_param_tlv);
		p_accept_tlv = qed_add_tlv(p_hwfn, &p_iov->offset, tlv, size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		if (update_rx) {
			p_accept_tlv->update_rx_mode = update_rx;
			p_accept_tlv->rx_accept_filter =
			    p_params->accept_flags.rx_accept_filter;
		}

		if (update_tx) {
			p_accept_tlv->update_tx_mode = update_tx;
			p_accept_tlv->tx_accept_filter =
			    p_params->accept_flags.tx_accept_filter;
		}
	}

	if (p_params->rss_params) {
		struct qed_rss_params *rss_params = p_params->rss_params;
		struct vfpf_vport_update_rss_tlv *p_rss_tlv;

		size = sizeof(struct vfpf_vport_update_rss_tlv);
		p_rss_tlv = qed_add_tlv(p_hwfn,
					&p_iov->offset,
					CHANNEL_TLV_VPORT_UPDATE_RSS, size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		if (rss_params->update_rss_config)
			p_rss_tlv->update_rss_flags |=
			    VFPF_UPDATE_RSS_CONFIG_FLAG;
		if (rss_params->update_rss_capabilities)
			p_rss_tlv->update_rss_flags |=
			    VFPF_UPDATE_RSS_CAPS_FLAG;
		if (rss_params->update_rss_ind_table)
			p_rss_tlv->update_rss_flags |=
			    VFPF_UPDATE_RSS_IND_TABLE_FLAG;
		if (rss_params->update_rss_key)
			p_rss_tlv->update_rss_flags |= VFPF_UPDATE_RSS_KEY_FLAG;

		p_rss_tlv->rss_enable = rss_params->rss_enable;
		p_rss_tlv->rss_caps = rss_params->rss_caps;
		p_rss_tlv->rss_table_size_log = rss_params->rss_table_size_log;
		memcpy(p_rss_tlv->rss_ind_table, rss_params->rss_ind_table,
		       sizeof(rss_params->rss_ind_table));
		memcpy(p_rss_tlv->rss_key, rss_params->rss_key,
		       sizeof(rss_params->rss_key));
	}

	if (p_params->update_accept_any_vlan_flg) {
		struct vfpf_vport_update_accept_any_vlan_tlv *p_any_vlan_tlv;

		size = sizeof(struct vfpf_vport_update_accept_any_vlan_tlv);
		tlv = CHANNEL_TLV_VPORT_UPDATE_ACCEPT_ANY_VLAN;
		p_any_vlan_tlv = qed_add_tlv(p_hwfn, &p_iov->offset, tlv, size);

		resp_size += sizeof(struct pfvf_def_resp_tlv);
		p_any_vlan_tlv->accept_any_vlan = p_params->accept_any_vlan;
		p_any_vlan_tlv->update_accept_any_vlan_flg =
		    p_params->update_accept_any_vlan_flg;
	}

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, resp_size);
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EINVAL;

	qed_vf_handle_vp_update_tlvs_resp(p_hwfn, p_params);

	return rc;
}

int qed_vf_pf_reset(struct qed_hwfn *p_hwfn)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *resp;
	struct vfpf_first_tlv *req;
	int rc;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_CLOSE, sizeof(*req));

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EAGAIN;

	p_hwfn->b_int_enabled = 0;

	return 0;
}

int qed_vf_pf_release(struct qed_hwfn *p_hwfn)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *resp;
	struct vfpf_first_tlv *req;
	u32 size;
	int rc;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_RELEASE, sizeof(*req));

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));

	if (!rc && resp->hdr.status != PFVF_STATUS_SUCCESS)
		rc = -EAGAIN;

	p_hwfn->b_int_enabled = 0;

	if (p_iov->vf2pf_request)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  sizeof(union vfpf_tlvs),
				  p_iov->vf2pf_request,
				  p_iov->vf2pf_request_phys);
	if (p_iov->pf2vf_reply)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  sizeof(union pfvf_tlvs),
				  p_iov->pf2vf_reply, p_iov->pf2vf_reply_phys);

	if (p_iov->bulletin.p_virt) {
		size = sizeof(struct qed_bulletin_content);
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  size,
				  p_iov->bulletin.p_virt, p_iov->bulletin.phys);
	}

	kfree(p_hwfn->vf_iov_info);
	p_hwfn->vf_iov_info = NULL;

	return rc;
}

void qed_vf_pf_filter_mcast(struct qed_hwfn *p_hwfn,
			    struct qed_filter_mcast *p_filter_cmd)
{
	struct qed_sp_vport_update_params sp_params;
	int i;

	memset(&sp_params, 0, sizeof(sp_params));
	sp_params.update_approx_mcast_flg = 1;

	if (p_filter_cmd->opcode == QED_FILTER_ADD) {
		for (i = 0; i < p_filter_cmd->num_mc_addrs; i++) {
			u32 bit;

			bit = qed_mcast_bin_from_mac(p_filter_cmd->mac[i]);
			__set_bit(bit, sp_params.bins);
		}
	}

	qed_vf_pf_vport_update(p_hwfn, &sp_params);
}

int qed_vf_pf_filter_ucast(struct qed_hwfn *p_hwfn,
			   struct qed_filter_ucast *p_ucast)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_ucast_filter_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	int rc;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_UCAST_FILTER, sizeof(*req));
	req->opcode = (u8) p_ucast->opcode;
	req->type = (u8) p_ucast->type;
	memcpy(req->mac, p_ucast->mac, ETH_ALEN);
	req->vlan = p_ucast->vlan;

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EAGAIN;

	return 0;
}

int qed_vf_pf_int_cleanup(struct qed_hwfn *p_hwfn)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *resp = &p_iov->pf2vf_reply->default_resp;
	int rc;

	/* clear mailbox and prep first tlv */
	qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_INT_CLEANUP,
		       sizeof(struct vfpf_first_tlv));

	/* add list termination tlv */
	qed_add_tlv(p_hwfn, &p_iov->offset,
		    CHANNEL_TLV_LIST_END, sizeof(struct channel_list_end_tlv));

	rc = qed_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		return rc;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		return -EINVAL;

	return 0;
}

u16 qed_vf_get_igu_sb_id(struct qed_hwfn *p_hwfn, u16 sb_id)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;

	if (!p_iov) {
		DP_NOTICE(p_hwfn, "vf_sriov_info isn't initialized\n");
		return 0;
	}

	return p_iov->acquire_resp.resc.hw_sbs[sb_id].hw_sb_id;
}

int qed_vf_read_bulletin(struct qed_hwfn *p_hwfn, u8 *p_change)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct qed_bulletin_content shadow;
	u32 crc, crc_size;

	crc_size = sizeof(p_iov->bulletin.p_virt->crc);
	*p_change = 0;

	/* Need to guarantee PF is not in the middle of writing it */
	memcpy(&shadow, p_iov->bulletin.p_virt, p_iov->bulletin.size);

	/* If version did not update, no need to do anything */
	if (shadow.version == p_iov->bulletin_shadow.version)
		return 0;

	/* Verify the bulletin we see is valid */
	crc = crc32(0, (u8 *)&shadow + crc_size,
		    p_iov->bulletin.size - crc_size);
	if (crc != shadow.crc)
		return -EAGAIN;

	/* Set the shadow bulletin and process it */
	memcpy(&p_iov->bulletin_shadow, &shadow, p_iov->bulletin.size);

	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "Read a bulletin update %08x\n", shadow.version);

	*p_change = 1;

	return 0;
}

void __qed_vf_get_link_params(struct qed_hwfn *p_hwfn,
			      struct qed_mcp_link_params *p_params,
			      struct qed_bulletin_content *p_bulletin)
{
	memset(p_params, 0, sizeof(*p_params));

	p_params->speed.autoneg = p_bulletin->req_autoneg;
	p_params->speed.advertised_speeds = p_bulletin->req_adv_speed;
	p_params->speed.forced_speed = p_bulletin->req_forced_speed;
	p_params->pause.autoneg = p_bulletin->req_autoneg_pause;
	p_params->pause.forced_rx = p_bulletin->req_forced_rx;
	p_params->pause.forced_tx = p_bulletin->req_forced_tx;
	p_params->loopback_mode = p_bulletin->req_loopback;
}

void qed_vf_get_link_params(struct qed_hwfn *p_hwfn,
			    struct qed_mcp_link_params *params)
{
	__qed_vf_get_link_params(p_hwfn, params,
				 &(p_hwfn->vf_iov_info->bulletin_shadow));
}

void __qed_vf_get_link_state(struct qed_hwfn *p_hwfn,
			     struct qed_mcp_link_state *p_link,
			     struct qed_bulletin_content *p_bulletin)
{
	memset(p_link, 0, sizeof(*p_link));

	p_link->link_up = p_bulletin->link_up;
	p_link->speed = p_bulletin->speed;
	p_link->full_duplex = p_bulletin->full_duplex;
	p_link->an = p_bulletin->autoneg;
	p_link->an_complete = p_bulletin->autoneg_complete;
	p_link->parallel_detection = p_bulletin->parallel_detection;
	p_link->pfc_enabled = p_bulletin->pfc_enabled;
	p_link->partner_adv_speed = p_bulletin->partner_adv_speed;
	p_link->partner_tx_flow_ctrl_en = p_bulletin->partner_tx_flow_ctrl_en;
	p_link->partner_rx_flow_ctrl_en = p_bulletin->partner_rx_flow_ctrl_en;
	p_link->partner_adv_pause = p_bulletin->partner_adv_pause;
	p_link->sfp_tx_fault = p_bulletin->sfp_tx_fault;
}

void qed_vf_get_link_state(struct qed_hwfn *p_hwfn,
			   struct qed_mcp_link_state *link)
{
	__qed_vf_get_link_state(p_hwfn, link,
				&(p_hwfn->vf_iov_info->bulletin_shadow));
}

void __qed_vf_get_link_caps(struct qed_hwfn *p_hwfn,
			    struct qed_mcp_link_capabilities *p_link_caps,
			    struct qed_bulletin_content *p_bulletin)
{
	memset(p_link_caps, 0, sizeof(*p_link_caps));
	p_link_caps->speed_capabilities = p_bulletin->capability_speed;
}

void qed_vf_get_link_caps(struct qed_hwfn *p_hwfn,
			  struct qed_mcp_link_capabilities *p_link_caps)
{
	__qed_vf_get_link_caps(p_hwfn, p_link_caps,
			       &(p_hwfn->vf_iov_info->bulletin_shadow));
}

void qed_vf_get_num_rxqs(struct qed_hwfn *p_hwfn, u8 *num_rxqs)
{
	*num_rxqs = p_hwfn->vf_iov_info->acquire_resp.resc.num_rxqs;
}

void qed_vf_get_port_mac(struct qed_hwfn *p_hwfn, u8 *port_mac)
{
	memcpy(port_mac,
	       p_hwfn->vf_iov_info->acquire_resp.pfdev_info.port_mac, ETH_ALEN);
}

void qed_vf_get_num_vlan_filters(struct qed_hwfn *p_hwfn, u8 *num_vlan_filters)
{
	struct qed_vf_iov *p_vf;

	p_vf = p_hwfn->vf_iov_info;
	*num_vlan_filters = p_vf->acquire_resp.resc.num_vlan_filters;
}

bool qed_vf_check_mac(struct qed_hwfn *p_hwfn, u8 *mac)
{
	struct qed_bulletin_content *bulletin;

	bulletin = &p_hwfn->vf_iov_info->bulletin_shadow;
	if (!(bulletin->valid_bitmap & (1 << MAC_ADDR_FORCED)))
		return true;

	/* Forbid VF from changing a MAC enforced by PF */
	if (ether_addr_equal(bulletin->mac, mac))
		return false;

	return false;
}

bool qed_vf_bulletin_get_forced_mac(struct qed_hwfn *hwfn,
				    u8 *dst_mac, u8 *p_is_forced)
{
	struct qed_bulletin_content *bulletin;

	bulletin = &hwfn->vf_iov_info->bulletin_shadow;

	if (bulletin->valid_bitmap & (1 << MAC_ADDR_FORCED)) {
		if (p_is_forced)
			*p_is_forced = 1;
	} else if (bulletin->valid_bitmap & (1 << VFPF_BULLETIN_MAC_ADDR)) {
		if (p_is_forced)
			*p_is_forced = 0;
	} else {
		return false;
	}

	ether_addr_copy(dst_mac, bulletin->mac);

	return true;
}

void qed_vf_get_fw_version(struct qed_hwfn *p_hwfn,
			   u16 *fw_major, u16 *fw_minor,
			   u16 *fw_rev, u16 *fw_eng)
{
	struct pf_vf_pfdev_info *info;

	info = &p_hwfn->vf_iov_info->acquire_resp.pfdev_info;

	*fw_major = info->fw_major;
	*fw_minor = info->fw_minor;
	*fw_rev = info->fw_rev;
	*fw_eng = info->fw_eng;
}

static void qed_handle_bulletin_change(struct qed_hwfn *hwfn)
{
	struct qed_eth_cb_ops *ops = hwfn->cdev->protocol_ops.eth;
	u8 mac[ETH_ALEN], is_mac_exist, is_mac_forced;
	void *cookie = hwfn->cdev->ops_cookie;

	is_mac_exist = qed_vf_bulletin_get_forced_mac(hwfn, mac,
						      &is_mac_forced);
	if (is_mac_exist && is_mac_forced && cookie)
		ops->force_mac(cookie, mac);

	/* Always update link configuration according to bulletin */
	qed_link_update(hwfn);
}

void qed_iov_vf_task(struct work_struct *work)
{
	struct qed_hwfn *hwfn = container_of(work, struct qed_hwfn,
					     iov_task.work);
	u8 change = 0;

	if (test_and_clear_bit(QED_IOV_WQ_STOP_WQ_FLAG, &hwfn->iov_task_flags))
		return;

	/* Handle bulletin board changes */
	qed_vf_read_bulletin(hwfn, &change);
	if (change)
		qed_handle_bulletin_change(hwfn);

	/* As VF is polling bulletin board, need to constantly re-schedule */
	queue_delayed_work(hwfn->iov_wq, &hwfn->iov_task, HZ);
}
