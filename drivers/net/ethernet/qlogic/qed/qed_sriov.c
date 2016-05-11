/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include "qed_hw.h"
#include "qed_int.h"
#include "qed_reg_addr.h"
#include "qed_sriov.h"
#include "qed_vf.h"

bool qed_iov_is_valid_vfid(struct qed_hwfn *p_hwfn,
			   int rel_vf_id, bool b_enabled_only)
{
	if (!p_hwfn->pf_iov_info) {
		DP_NOTICE(p_hwfn->cdev, "No iov info\n");
		return false;
	}

	if ((rel_vf_id >= p_hwfn->cdev->p_iov_info->total_vfs) ||
	    (rel_vf_id < 0))
		return false;

	if ((!p_hwfn->pf_iov_info->vfs_array[rel_vf_id].b_init) &&
	    b_enabled_only)
		return false;

	return true;
}

static struct qed_vf_info *qed_iov_get_vf_info(struct qed_hwfn *p_hwfn,
					       u16 relative_vf_id,
					       bool b_enabled_only)
{
	struct qed_vf_info *vf = NULL;

	if (!p_hwfn->pf_iov_info) {
		DP_NOTICE(p_hwfn->cdev, "No iov info\n");
		return NULL;
	}

	if (qed_iov_is_valid_vfid(p_hwfn, relative_vf_id, b_enabled_only))
		vf = &p_hwfn->pf_iov_info->vfs_array[relative_vf_id];
	else
		DP_ERR(p_hwfn, "qed_iov_get_vf_info: VF[%d] is not enabled\n",
		       relative_vf_id);

	return vf;
}

static int qed_iov_pci_cfg_info(struct qed_dev *cdev)
{
	struct qed_hw_sriov_info *iov = cdev->p_iov_info;
	int pos = iov->pos;

	DP_VERBOSE(cdev, QED_MSG_IOV, "sriov ext pos %d\n", pos);
	pci_read_config_word(cdev->pdev, pos + PCI_SRIOV_CTRL, &iov->ctrl);

	pci_read_config_word(cdev->pdev,
			     pos + PCI_SRIOV_TOTAL_VF, &iov->total_vfs);
	pci_read_config_word(cdev->pdev,
			     pos + PCI_SRIOV_INITIAL_VF, &iov->initial_vfs);

	pci_read_config_word(cdev->pdev, pos + PCI_SRIOV_NUM_VF, &iov->num_vfs);
	if (iov->num_vfs) {
		DP_VERBOSE(cdev,
			   QED_MSG_IOV,
			   "Number of VFs are already set to non-zero value. Ignoring PCI configuration value\n");
		iov->num_vfs = 0;
	}

	pci_read_config_word(cdev->pdev,
			     pos + PCI_SRIOV_VF_OFFSET, &iov->offset);

	pci_read_config_word(cdev->pdev,
			     pos + PCI_SRIOV_VF_STRIDE, &iov->stride);

	pci_read_config_word(cdev->pdev,
			     pos + PCI_SRIOV_VF_DID, &iov->vf_device_id);

	pci_read_config_dword(cdev->pdev,
			      pos + PCI_SRIOV_SUP_PGSIZE, &iov->pgsz);

	pci_read_config_dword(cdev->pdev, pos + PCI_SRIOV_CAP, &iov->cap);

	pci_read_config_byte(cdev->pdev, pos + PCI_SRIOV_FUNC_LINK, &iov->link);

	DP_VERBOSE(cdev,
		   QED_MSG_IOV,
		   "IOV info: nres %d, cap 0x%x, ctrl 0x%x, total %d, initial %d, num vfs %d, offset %d, stride %d, page size 0x%x\n",
		   iov->nres,
		   iov->cap,
		   iov->ctrl,
		   iov->total_vfs,
		   iov->initial_vfs,
		   iov->nr_virtfn, iov->offset, iov->stride, iov->pgsz);

	/* Some sanity checks */
	if (iov->num_vfs > NUM_OF_VFS(cdev) ||
	    iov->total_vfs > NUM_OF_VFS(cdev)) {
		/* This can happen only due to a bug. In this case we set
		 * num_vfs to zero to avoid memory corruption in the code that
		 * assumes max number of vfs
		 */
		DP_NOTICE(cdev,
			  "IOV: Unexpected number of vfs set: %d setting num_vf to zero\n",
			  iov->num_vfs);

		iov->num_vfs = 0;
		iov->total_vfs = 0;
	}

	return 0;
}

static void qed_iov_clear_vf_igu_blocks(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt)
{
	struct qed_igu_block *p_sb;
	u16 sb_id;
	u32 val;

	if (!p_hwfn->hw_info.p_igu_info) {
		DP_ERR(p_hwfn,
		       "qed_iov_clear_vf_igu_blocks IGU Info not initialized\n");
		return;
	}

	for (sb_id = 0; sb_id < QED_MAPPING_MEMORY_SIZE(p_hwfn->cdev);
	     sb_id++) {
		p_sb = &p_hwfn->hw_info.p_igu_info->igu_map.igu_blocks[sb_id];
		if ((p_sb->status & QED_IGU_STATUS_FREE) &&
		    !(p_sb->status & QED_IGU_STATUS_PF)) {
			val = qed_rd(p_hwfn, p_ptt,
				     IGU_REG_MAPPING_MEMORY + sb_id * 4);
			SET_FIELD(val, IGU_MAPPING_LINE_VALID, 0);
			qed_wr(p_hwfn, p_ptt,
			       IGU_REG_MAPPING_MEMORY + 4 * sb_id, val);
		}
	}
}

static void qed_iov_setup_vfdb(struct qed_hwfn *p_hwfn)
{
	struct qed_hw_sriov_info *p_iov = p_hwfn->cdev->p_iov_info;
	struct qed_pf_iov *p_iov_info = p_hwfn->pf_iov_info;
	struct qed_bulletin_content *p_bulletin_virt;
	dma_addr_t req_p, rply_p, bulletin_p;
	union pfvf_tlvs *p_reply_virt_addr;
	union vfpf_tlvs *p_req_virt_addr;
	u8 idx = 0;

	memset(p_iov_info->vfs_array, 0, sizeof(p_iov_info->vfs_array));

	p_req_virt_addr = p_iov_info->mbx_msg_virt_addr;
	req_p = p_iov_info->mbx_msg_phys_addr;
	p_reply_virt_addr = p_iov_info->mbx_reply_virt_addr;
	rply_p = p_iov_info->mbx_reply_phys_addr;
	p_bulletin_virt = p_iov_info->p_bulletins;
	bulletin_p = p_iov_info->bulletins_phys;
	if (!p_req_virt_addr || !p_reply_virt_addr || !p_bulletin_virt) {
		DP_ERR(p_hwfn,
		       "qed_iov_setup_vfdb called without allocating mem first\n");
		return;
	}

	for (idx = 0; idx < p_iov->total_vfs; idx++) {
		struct qed_vf_info *vf = &p_iov_info->vfs_array[idx];
		u32 concrete;

		vf->vf_mbx.req_virt = p_req_virt_addr + idx;
		vf->vf_mbx.req_phys = req_p + idx * sizeof(union vfpf_tlvs);
		vf->vf_mbx.reply_virt = p_reply_virt_addr + idx;
		vf->vf_mbx.reply_phys = rply_p + idx * sizeof(union pfvf_tlvs);

		vf->state = VF_STOPPED;
		vf->b_init = false;

		vf->bulletin.phys = idx *
				    sizeof(struct qed_bulletin_content) +
				    bulletin_p;
		vf->bulletin.p_virt = p_bulletin_virt + idx;
		vf->bulletin.size = sizeof(struct qed_bulletin_content);

		vf->relative_vf_id = idx;
		vf->abs_vf_id = idx + p_iov->first_vf_in_pf;
		concrete = qed_vfid_to_concrete(p_hwfn, vf->abs_vf_id);
		vf->concrete_fid = concrete;
		vf->opaque_fid = (p_hwfn->hw_info.opaque_fid & 0xff) |
				 (vf->abs_vf_id << 8);
		vf->vport_id = idx + 1;
	}
}

static int qed_iov_allocate_vfdb(struct qed_hwfn *p_hwfn)
{
	struct qed_pf_iov *p_iov_info = p_hwfn->pf_iov_info;
	void **p_v_addr;
	u16 num_vfs = 0;

	num_vfs = p_hwfn->cdev->p_iov_info->total_vfs;

	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "qed_iov_allocate_vfdb for %d VFs\n", num_vfs);

	/* Allocate PF Mailbox buffer (per-VF) */
	p_iov_info->mbx_msg_size = sizeof(union vfpf_tlvs) * num_vfs;
	p_v_addr = &p_iov_info->mbx_msg_virt_addr;
	*p_v_addr = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				       p_iov_info->mbx_msg_size,
				       &p_iov_info->mbx_msg_phys_addr,
				       GFP_KERNEL);
	if (!*p_v_addr)
		return -ENOMEM;

	/* Allocate PF Mailbox Reply buffer (per-VF) */
	p_iov_info->mbx_reply_size = sizeof(union pfvf_tlvs) * num_vfs;
	p_v_addr = &p_iov_info->mbx_reply_virt_addr;
	*p_v_addr = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				       p_iov_info->mbx_reply_size,
				       &p_iov_info->mbx_reply_phys_addr,
				       GFP_KERNEL);
	if (!*p_v_addr)
		return -ENOMEM;

	p_iov_info->bulletins_size = sizeof(struct qed_bulletin_content) *
				     num_vfs;
	p_v_addr = &p_iov_info->p_bulletins;
	*p_v_addr = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				       p_iov_info->bulletins_size,
				       &p_iov_info->bulletins_phys,
				       GFP_KERNEL);
	if (!*p_v_addr)
		return -ENOMEM;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "PF's Requests mailbox [%p virt 0x%llx phys],  Response mailbox [%p virt 0x%llx phys] Bulletins [%p virt 0x%llx phys]\n",
		   p_iov_info->mbx_msg_virt_addr,
		   (u64) p_iov_info->mbx_msg_phys_addr,
		   p_iov_info->mbx_reply_virt_addr,
		   (u64) p_iov_info->mbx_reply_phys_addr,
		   p_iov_info->p_bulletins, (u64) p_iov_info->bulletins_phys);

	return 0;
}

static void qed_iov_free_vfdb(struct qed_hwfn *p_hwfn)
{
	struct qed_pf_iov *p_iov_info = p_hwfn->pf_iov_info;

	if (p_hwfn->pf_iov_info->mbx_msg_virt_addr)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  p_iov_info->mbx_msg_size,
				  p_iov_info->mbx_msg_virt_addr,
				  p_iov_info->mbx_msg_phys_addr);

	if (p_hwfn->pf_iov_info->mbx_reply_virt_addr)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  p_iov_info->mbx_reply_size,
				  p_iov_info->mbx_reply_virt_addr,
				  p_iov_info->mbx_reply_phys_addr);

	if (p_iov_info->p_bulletins)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  p_iov_info->bulletins_size,
				  p_iov_info->p_bulletins,
				  p_iov_info->bulletins_phys);
}

int qed_iov_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_pf_iov *p_sriov;

	if (!IS_PF_SRIOV(p_hwfn)) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "No SR-IOV - no need for IOV db\n");
		return 0;
	}

	p_sriov = kzalloc(sizeof(*p_sriov), GFP_KERNEL);
	if (!p_sriov) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct qed_sriov'\n");
		return -ENOMEM;
	}

	p_hwfn->pf_iov_info = p_sriov;

	return qed_iov_allocate_vfdb(p_hwfn);
}

void qed_iov_setup(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	if (!IS_PF_SRIOV(p_hwfn) || !IS_PF_SRIOV_ALLOC(p_hwfn))
		return;

	qed_iov_setup_vfdb(p_hwfn);
	qed_iov_clear_vf_igu_blocks(p_hwfn, p_ptt);
}

void qed_iov_free(struct qed_hwfn *p_hwfn)
{
	if (IS_PF_SRIOV_ALLOC(p_hwfn)) {
		qed_iov_free_vfdb(p_hwfn);
		kfree(p_hwfn->pf_iov_info);
	}
}

void qed_iov_free_hw_info(struct qed_dev *cdev)
{
	kfree(cdev->p_iov_info);
	cdev->p_iov_info = NULL;
}

int qed_iov_hw_info(struct qed_hwfn *p_hwfn)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	int pos;
	int rc;

	/* Learn the PCI configuration */
	pos = pci_find_ext_capability(p_hwfn->cdev->pdev,
				      PCI_EXT_CAP_ID_SRIOV);
	if (!pos) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV, "No PCIe IOV support\n");
		return 0;
	}

	/* Allocate a new struct for IOV information */
	cdev->p_iov_info = kzalloc(sizeof(*cdev->p_iov_info), GFP_KERNEL);
	if (!cdev->p_iov_info) {
		DP_NOTICE(p_hwfn, "Can't support IOV due to lack of memory\n");
		return -ENOMEM;
	}
	cdev->p_iov_info->pos = pos;

	rc = qed_iov_pci_cfg_info(cdev);
	if (rc)
		return rc;

	/* We want PF IOV to be synonemous with the existance of p_iov_info;
	 * In case the capability is published but there are no VFs, simply
	 * de-allocate the struct.
	 */
	if (!cdev->p_iov_info->total_vfs) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "IOV capabilities, but no VFs are published\n");
		kfree(cdev->p_iov_info);
		cdev->p_iov_info = NULL;
		return 0;
	}

	/* Calculate the first VF index - this is a bit tricky; Basically,
	 * VFs start at offset 16 relative to PF0, and 2nd engine VFs begin
	 * after the first engine's VFs.
	 */
	cdev->p_iov_info->first_vf_in_pf = p_hwfn->cdev->p_iov_info->offset +
					   p_hwfn->abs_pf_id - 16;
	if (QED_PATH_ID(p_hwfn))
		cdev->p_iov_info->first_vf_in_pf -= MAX_NUM_VFS_BB;

	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "First VF in hwfn 0x%08x\n",
		   cdev->p_iov_info->first_vf_in_pf);

	return 0;
}

static bool qed_iov_pf_sanity_check(struct qed_hwfn *p_hwfn, int vfid)
{
	/* Check PF supports sriov */
	if (!IS_QED_SRIOV(p_hwfn->cdev) || !IS_PF_SRIOV_ALLOC(p_hwfn))
		return false;

	/* Check VF validity */
	if (!qed_iov_is_valid_vfid(p_hwfn, vfid, true))
		return false;

	return true;
}

static bool qed_iov_tlv_supported(u16 tlvtype)
{
	return CHANNEL_TLV_NONE < tlvtype && tlvtype < CHANNEL_TLV_MAX;
}

/* place a given tlv on the tlv buffer, continuing current tlv list */
void *qed_add_tlv(struct qed_hwfn *p_hwfn, u8 **offset, u16 type, u16 length)
{
	struct channel_tlv *tl = (struct channel_tlv *)*offset;

	tl->type = type;
	tl->length = length;

	/* Offset should keep pointing to next TLV (the end of the last) */
	*offset += length;

	/* Return a pointer to the start of the added tlv */
	return *offset - length;
}

/* list the types and lengths of the tlvs on the buffer */
void qed_dp_tlv_list(struct qed_hwfn *p_hwfn, void *tlvs_list)
{
	u16 i = 1, total_length = 0;
	struct channel_tlv *tlv;

	do {
		tlv = (struct channel_tlv *)((u8 *)tlvs_list + total_length);

		/* output tlv */
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "TLV number %d: type %d, length %d\n",
			   i, tlv->type, tlv->length);

		if (tlv->type == CHANNEL_TLV_LIST_END)
			return;

		/* Validate entry - protect against malicious VFs */
		if (!tlv->length) {
			DP_NOTICE(p_hwfn, "TLV of length 0 found\n");
			return;
		}

		total_length += tlv->length;

		if (total_length >= sizeof(struct tlv_buffer_size)) {
			DP_NOTICE(p_hwfn, "TLV ==> Buffer overflow\n");
			return;
		}

		i++;
	} while (1);
}

static void qed_iov_send_response(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_vf_info *p_vf,
				  u16 length, u8 status)
{
	struct qed_iov_vf_mbx *mbx = &p_vf->vf_mbx;
	struct qed_dmae_params params;
	u8 eng_vf_id;

	mbx->reply_virt->default_resp.hdr.status = status;

	qed_dp_tlv_list(p_hwfn, mbx->reply_virt);

	eng_vf_id = p_vf->abs_vf_id;

	memset(&params, 0, sizeof(struct qed_dmae_params));
	params.flags = QED_DMAE_FLAG_VF_DST;
	params.dst_vfid = eng_vf_id;

	qed_dmae_host2host(p_hwfn, p_ptt, mbx->reply_phys + sizeof(u64),
			   mbx->req_virt->first_tlv.reply_address +
			   sizeof(u64),
			   (sizeof(union pfvf_tlvs) - sizeof(u64)) / 4,
			   &params);

	qed_dmae_host2host(p_hwfn, p_ptt, mbx->reply_phys,
			   mbx->req_virt->first_tlv.reply_address,
			   sizeof(u64) / 4, &params);

	REG_WR(p_hwfn,
	       GTT_BAR0_MAP_REG_USDM_RAM +
	       USTORM_VF_PF_CHANNEL_READY_OFFSET(eng_vf_id), 1);
}

static void qed_iov_prepare_resp(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 struct qed_vf_info *vf_info,
				 u16 type, u16 length, u8 status)
{
	struct qed_iov_vf_mbx *mbx = &vf_info->vf_mbx;

	mbx->offset = (u8 *)mbx->reply_virt;

	qed_add_tlv(p_hwfn, &mbx->offset, type, length);
	qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_LIST_END,
		    sizeof(struct channel_list_end_tlv));

	qed_iov_send_response(p_hwfn, p_ptt, vf_info, length, status);
}

static void qed_iov_process_mbx_dummy_resp(struct qed_hwfn *p_hwfn,
					   struct qed_ptt *p_ptt,
					   struct qed_vf_info *p_vf)
{
	qed_iov_prepare_resp(p_hwfn, p_ptt, p_vf, CHANNEL_TLV_NONE,
			     sizeof(struct pfvf_def_resp_tlv),
			     PFVF_STATUS_SUCCESS);
}

static void qed_iov_process_mbx_req(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt, int vfid)
{
	struct qed_iov_vf_mbx *mbx;
	struct qed_vf_info *p_vf;
	int i;

	p_vf = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!p_vf)
		return;

	mbx = &p_vf->vf_mbx;

	/* qed_iov_process_mbx_request */
	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "qed_iov_process_mbx_req vfid %d\n", p_vf->abs_vf_id);

	mbx->first_tlv = mbx->req_virt->first_tlv;

	/* check if tlv type is known */
	if (qed_iov_tlv_supported(mbx->first_tlv.tl.type)) {
		qed_iov_process_mbx_dummy_resp(p_hwfn, p_ptt, p_vf);
	} else {
		/* unknown TLV - this may belong to a VF driver from the future
		 * - a version written after this PF driver was written, which
		 * supports features unknown as of yet. Too bad since we don't
		 * support them. Or this may be because someone wrote a crappy
		 * VF driver and is sending garbage over the channel.
		 */
		DP_ERR(p_hwfn,
		       "unknown TLV. type %d length %d. first 20 bytes of mailbox buffer:\n",
		       mbx->first_tlv.tl.type, mbx->first_tlv.tl.length);

		for (i = 0; i < 20; i++) {
			DP_VERBOSE(p_hwfn,
				   QED_MSG_IOV,
				   "%x ",
				   mbx->req_virt->tlv_buf_size.tlv_buffer[i]);
		}
	}
}

void qed_iov_pf_add_pending_events(struct qed_hwfn *p_hwfn, u8 vfid)
{
	u64 add_bit = 1ULL << (vfid % 64);

	p_hwfn->pf_iov_info->pending_events[vfid / 64] |= add_bit;
}

static void qed_iov_pf_get_and_clear_pending_events(struct qed_hwfn *p_hwfn,
						    u64 *events)
{
	u64 *p_pending_events = p_hwfn->pf_iov_info->pending_events;

	memcpy(events, p_pending_events, sizeof(u64) * QED_VF_ARRAY_LENGTH);
	memset(p_pending_events, 0, sizeof(u64) * QED_VF_ARRAY_LENGTH);
}

static int qed_sriov_vfpf_msg(struct qed_hwfn *p_hwfn,
			      u16 abs_vfid, struct regpair *vf_msg)
{
	u8 min = (u8)p_hwfn->cdev->p_iov_info->first_vf_in_pf;
	struct qed_vf_info *p_vf;

	if (!qed_iov_pf_sanity_check(p_hwfn, (int)abs_vfid - min)) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "Got a message from VF [abs 0x%08x] that cannot be handled by PF\n",
			   abs_vfid);
		return 0;
	}
	p_vf = &p_hwfn->pf_iov_info->vfs_array[(u8)abs_vfid - min];

	/* List the physical address of the request so that handler
	 * could later on copy the message from it.
	 */
	p_vf->vf_mbx.pending_req = (((u64)vf_msg->hi) << 32) | vf_msg->lo;

	/* Mark the event and schedule the workqueue */
	qed_iov_pf_add_pending_events(p_hwfn, p_vf->relative_vf_id);
	qed_schedule_iov(p_hwfn, QED_IOV_WQ_MSG_FLAG);

	return 0;
}

int qed_sriov_eqe_event(struct qed_hwfn *p_hwfn,
			u8 opcode, __le16 echo, union event_ring_data *data)
{
	switch (opcode) {
	case COMMON_EVENT_VF_PF_CHANNEL:
		return qed_sriov_vfpf_msg(p_hwfn, le16_to_cpu(echo),
					  &data->vf_pf_channel.msg_addr);
	default:
		DP_INFO(p_hwfn->cdev, "Unknown sriov eqe event 0x%02x\n",
			opcode);
		return -EINVAL;
	}
}

u16 qed_iov_get_next_active_vf(struct qed_hwfn *p_hwfn, u16 rel_vf_id)
{
	struct qed_hw_sriov_info *p_iov = p_hwfn->cdev->p_iov_info;
	u16 i;

	if (!p_iov)
		goto out;

	for (i = rel_vf_id; i < p_iov->total_vfs; i++)
		if (qed_iov_is_valid_vfid(p_hwfn, rel_vf_id, true))
			return i;

out:
	return MAX_NUM_VFS;
}

static int qed_iov_copy_vf_msg(struct qed_hwfn *p_hwfn, struct qed_ptt *ptt,
			       int vfid)
{
	struct qed_dmae_params params;
	struct qed_vf_info *vf_info;

	vf_info = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!vf_info)
		return -EINVAL;

	memset(&params, 0, sizeof(struct qed_dmae_params));
	params.flags = QED_DMAE_FLAG_VF_SRC | QED_DMAE_FLAG_COMPLETION_DST;
	params.src_vfid = vf_info->abs_vf_id;

	if (qed_dmae_host2host(p_hwfn, ptt,
			       vf_info->vf_mbx.pending_req,
			       vf_info->vf_mbx.req_phys,
			       sizeof(union vfpf_tlvs) / 4, &params)) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Failed to copy message from VF 0x%02x\n", vfid);

		return -EIO;
	}

	return 0;
}

/**
 * qed_schedule_iov - schedules IOV task for VF and PF
 * @hwfn: hardware function pointer
 * @flag: IOV flag for VF/PF
 */
void qed_schedule_iov(struct qed_hwfn *hwfn, enum qed_iov_wq_flag flag)
{
	smp_mb__before_atomic();
	set_bit(flag, &hwfn->iov_task_flags);
	smp_mb__after_atomic();
	DP_VERBOSE(hwfn, QED_MSG_IOV, "Scheduling iov task [Flag: %d]\n", flag);
	queue_delayed_work(hwfn->iov_wq, &hwfn->iov_task, 0);
}

static void qed_handle_vf_msg(struct qed_hwfn *hwfn)
{
	u64 events[QED_VF_ARRAY_LENGTH];
	struct qed_ptt *ptt;
	int i;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt) {
		DP_VERBOSE(hwfn, QED_MSG_IOV,
			   "Can't acquire PTT; re-scheduling\n");
		qed_schedule_iov(hwfn, QED_IOV_WQ_MSG_FLAG);
		return;
	}

	qed_iov_pf_get_and_clear_pending_events(hwfn, events);

	DP_VERBOSE(hwfn, QED_MSG_IOV,
		   "Event mask of VF events: 0x%llx 0x%llx 0x%llx\n",
		   events[0], events[1], events[2]);

	qed_for_each_vf(hwfn, i) {
		/* Skip VFs with no pending messages */
		if (!(events[i / 64] & (1ULL << (i % 64))))
			continue;

		DP_VERBOSE(hwfn, QED_MSG_IOV,
			   "Handling VF message from VF 0x%02x [Abs 0x%02x]\n",
			   i, hwfn->cdev->p_iov_info->first_vf_in_pf + i);

		/* Copy VF's message to PF's request buffer for that VF */
		if (qed_iov_copy_vf_msg(hwfn, ptt, i))
			continue;

		qed_iov_process_mbx_req(hwfn, ptt, i);
	}

	qed_ptt_release(hwfn, ptt);
}

void qed_iov_pf_task(struct work_struct *work)
{
	struct qed_hwfn *hwfn = container_of(work, struct qed_hwfn,
					     iov_task.work);

	if (test_and_clear_bit(QED_IOV_WQ_STOP_WQ_FLAG, &hwfn->iov_task_flags))
		return;

	if (test_and_clear_bit(QED_IOV_WQ_MSG_FLAG, &hwfn->iov_task_flags))
		qed_handle_vf_msg(hwfn);
}

void qed_iov_wq_stop(struct qed_dev *cdev, bool schedule_first)
{
	int i;

	for_each_hwfn(cdev, i) {
		if (!cdev->hwfns[i].iov_wq)
			continue;

		if (schedule_first) {
			qed_schedule_iov(&cdev->hwfns[i],
					 QED_IOV_WQ_STOP_WQ_FLAG);
			cancel_delayed_work_sync(&cdev->hwfns[i].iov_task);
		}

		flush_workqueue(cdev->hwfns[i].iov_wq);
		destroy_workqueue(cdev->hwfns[i].iov_wq);
	}
}

int qed_iov_wq_start(struct qed_dev *cdev)
{
	char name[NAME_SIZE];
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		/* PFs needs a dedicated workqueue only if they support IOV. */
		if (!IS_PF_SRIOV(p_hwfn))
			continue;

		snprintf(name, NAME_SIZE, "iov-%02x:%02x.%02x",
			 cdev->pdev->bus->number,
			 PCI_SLOT(cdev->pdev->devfn), p_hwfn->abs_pf_id);

		p_hwfn->iov_wq = create_singlethread_workqueue(name);
		if (!p_hwfn->iov_wq) {
			DP_NOTICE(p_hwfn, "Cannot create iov workqueue\n");
			return -ENOMEM;
		}

		INIT_DELAYED_WORK(&p_hwfn->iov_task, qed_iov_pf_task);
	}

	return 0;
}
