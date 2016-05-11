/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

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
#define VF_ACQUIRE_MAC_FILTERS 1

static int qed_vf_pf_acquire(struct qed_hwfn *p_hwfn)
{
	struct qed_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_acquire_resp_tlv *resp = &p_iov->pf2vf_reply->acquire_resp;
	struct pf_vf_pfdev_info *pfdev_info = &resp->pfdev_info;
	u8 rx_count = 1, tx_count = 1, num_sbs = 1;
	u8 num_mac = VF_ACQUIRE_MAC_FILTERS;
	bool resources_acquired = false;
	struct vfpf_acquire_tlv *req;
	int rc = 0, attempts = 0;

	/* clear mailbox and prep first tlv */
	req = qed_vf_pf_prep(p_hwfn, CHANNEL_TLV_ACQUIRE, sizeof(*req));

	/* starting filling the request */
	req->vfdev_info.opaque_fid = p_hwfn->hw_info.opaque_fid;

	req->resc_request.num_rxqs = rx_count;
	req->resc_request.num_txqs = tx_count;
	req->resc_request.num_sbs = num_sbs;
	req->resc_request.num_mac_filters = num_mac;
	req->resc_request.num_vlan_filters = QED_ETH_VF_NUM_VLAN_FILTERS;

	req->vfdev_info.os_type = VFPF_ACQUIRE_OS_LINUX;
	req->vfdev_info.fw_major = FW_MAJOR_VERSION;
	req->vfdev_info.fw_minor = FW_MINOR_VERSION;
	req->vfdev_info.fw_revision = FW_REVISION_VERSION;
	req->vfdev_info.fw_engineering = FW_ENGINEERING_VERSION;

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
			DP_VERBOSE(p_hwfn,
				   QED_MSG_IOV,
				   "PF unwilling to fullfill resource request. Try PF recommended amount\n");

			/* humble our request */
			req->resc_request.num_txqs = resp->resc.num_txqs;
			req->resc_request.num_rxqs = resp->resc.num_rxqs;
			req->resc_request.num_sbs = resp->resc.num_sbs;
			req->resc_request.num_mac_filters =
			    resp->resc.num_mac_filters;
			req->resc_request.num_vlan_filters =
			    resp->resc.num_vlan_filters;

			/* Clear response buffer */
			memset(p_iov->pf2vf_reply, 0, sizeof(union pfvf_tlvs));
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
