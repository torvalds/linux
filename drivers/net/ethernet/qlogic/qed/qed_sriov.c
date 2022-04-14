// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/vmalloc.h>
#include <linux/crash_dump.h>
#include <linux/qed/qed_iov_if.h>
#include "qed_cxt.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_int.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"
#include "qed_sriov.h"
#include "qed_vf.h"
static int qed_sriov_eqe_event(struct qed_hwfn *p_hwfn,
			       u8 opcode,
			       __le16 echo,
			       union event_ring_data *data, u8 fw_return_code);
static int qed_iov_bulletin_set_mac(struct qed_hwfn *p_hwfn, u8 *mac, int vfid);

static u8 qed_vf_calculate_legacy(struct qed_vf_info *p_vf)
{
	u8 legacy = 0;

	if (p_vf->acquire.vfdev_info.eth_fp_hsi_minor ==
	    ETH_HSI_VER_NO_PKT_LEN_TUNN)
		legacy |= QED_QCID_LEGACY_VF_RX_PROD;

	if (!(p_vf->acquire.vfdev_info.capabilities &
	      VFPF_ACQUIRE_CAP_QUEUE_QIDS))
		legacy |= QED_QCID_LEGACY_VF_CID;

	return legacy;
}

/* IOV ramrods */
static int qed_sp_vf_start(struct qed_hwfn *p_hwfn, struct qed_vf_info *p_vf)
{
	struct vf_start_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;
	u8 fp_minor;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_vf->opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_VF_START,
				 PROTOCOLID_COMMON, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.vf_start;

	p_ramrod->vf_id = GET_FIELD(p_vf->concrete_fid, PXP_CONCRETE_FID_VFID);
	p_ramrod->opaque_fid = cpu_to_le16(p_vf->opaque_fid);

	switch (p_hwfn->hw_info.personality) {
	case QED_PCI_ETH:
		p_ramrod->personality = PERSONALITY_ETH;
		break;
	case QED_PCI_ETH_ROCE:
	case QED_PCI_ETH_IWARP:
		p_ramrod->personality = PERSONALITY_RDMA_AND_ETH;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown VF personality %d\n",
			  p_hwfn->hw_info.personality);
		qed_sp_destroy_request(p_hwfn, p_ent);
		return -EINVAL;
	}

	fp_minor = p_vf->acquire.vfdev_info.eth_fp_hsi_minor;
	if (fp_minor > ETH_HSI_VER_MINOR &&
	    fp_minor != ETH_HSI_VER_NO_PKT_LEN_TUNN) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF [%d] - Requested fp hsi %02x.%02x which is slightly newer than PF's %02x.%02x; Configuring PFs version\n",
			   p_vf->abs_vf_id,
			   ETH_HSI_VER_MAJOR,
			   fp_minor, ETH_HSI_VER_MAJOR, ETH_HSI_VER_MINOR);
		fp_minor = ETH_HSI_VER_MINOR;
	}

	p_ramrod->hsi_fp_ver.major_ver_arr[ETH_VER_KEY] = ETH_HSI_VER_MAJOR;
	p_ramrod->hsi_fp_ver.minor_ver_arr[ETH_VER_KEY] = fp_minor;

	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "VF[%d] - Starting using HSI %02x.%02x\n",
		   p_vf->abs_vf_id, ETH_HSI_VER_MAJOR, fp_minor);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_vf_stop(struct qed_hwfn *p_hwfn,
			  u32 concrete_vfid, u16 opaque_vfid)
{
	struct vf_stop_ramrod_data *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = opaque_vfid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_VF_STOP,
				 PROTOCOLID_COMMON, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.vf_stop;

	p_ramrod->vf_id = GET_FIELD(concrete_vfid, PXP_CONCRETE_FID_VFID);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

bool qed_iov_is_valid_vfid(struct qed_hwfn *p_hwfn,
			   int rel_vf_id,
			   bool b_enabled_only, bool b_non_malicious)
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

	if ((p_hwfn->pf_iov_info->vfs_array[rel_vf_id].b_malicious) &&
	    b_non_malicious)
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

	if (qed_iov_is_valid_vfid(p_hwfn, relative_vf_id,
				  b_enabled_only, false))
		vf = &p_hwfn->pf_iov_info->vfs_array[relative_vf_id];
	else
		DP_ERR(p_hwfn, "qed_iov_get_vf_info: VF[%d] is not enabled\n",
		       relative_vf_id);

	return vf;
}

static struct qed_queue_cid *
qed_iov_get_vf_rx_queue_cid(struct qed_vf_queue *p_queue)
{
	int i;

	for (i = 0; i < MAX_QUEUES_PER_QZONE; i++) {
		if (p_queue->cids[i].p_cid && !p_queue->cids[i].b_is_tx)
			return p_queue->cids[i].p_cid;
	}

	return NULL;
}

enum qed_iov_validate_q_mode {
	QED_IOV_VALIDATE_Q_NA,
	QED_IOV_VALIDATE_Q_ENABLE,
	QED_IOV_VALIDATE_Q_DISABLE,
};

static bool qed_iov_validate_queue_mode(struct qed_hwfn *p_hwfn,
					struct qed_vf_info *p_vf,
					u16 qid,
					enum qed_iov_validate_q_mode mode,
					bool b_is_tx)
{
	int i;

	if (mode == QED_IOV_VALIDATE_Q_NA)
		return true;

	for (i = 0; i < MAX_QUEUES_PER_QZONE; i++) {
		struct qed_vf_queue_cid *p_qcid;

		p_qcid = &p_vf->vf_queues[qid].cids[i];

		if (!p_qcid->p_cid)
			continue;

		if (p_qcid->b_is_tx != b_is_tx)
			continue;

		return mode == QED_IOV_VALIDATE_Q_ENABLE;
	}

	/* In case we haven't found any valid cid, then its disabled */
	return mode == QED_IOV_VALIDATE_Q_DISABLE;
}

static bool qed_iov_validate_rxq(struct qed_hwfn *p_hwfn,
				 struct qed_vf_info *p_vf,
				 u16 rx_qid,
				 enum qed_iov_validate_q_mode mode)
{
	if (rx_qid >= p_vf->num_rxqs) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[0x%02x] - can't touch Rx queue[%04x]; Only 0x%04x are allocated\n",
			   p_vf->abs_vf_id, rx_qid, p_vf->num_rxqs);
		return false;
	}

	return qed_iov_validate_queue_mode(p_hwfn, p_vf, rx_qid, mode, false);
}

static bool qed_iov_validate_txq(struct qed_hwfn *p_hwfn,
				 struct qed_vf_info *p_vf,
				 u16 tx_qid,
				 enum qed_iov_validate_q_mode mode)
{
	if (tx_qid >= p_vf->num_txqs) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[0x%02x] - can't touch Tx queue[%04x]; Only 0x%04x are allocated\n",
			   p_vf->abs_vf_id, tx_qid, p_vf->num_txqs);
		return false;
	}

	return qed_iov_validate_queue_mode(p_hwfn, p_vf, tx_qid, mode, true);
}

static bool qed_iov_validate_sb(struct qed_hwfn *p_hwfn,
				struct qed_vf_info *p_vf, u16 sb_idx)
{
	int i;

	for (i = 0; i < p_vf->num_sbs; i++)
		if (p_vf->igu_sbs[i] == sb_idx)
			return true;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "VF[0%02x] - tried using sb_idx %04x which doesn't exist as one of its 0x%02x SBs\n",
		   p_vf->abs_vf_id, sb_idx, p_vf->num_sbs);

	return false;
}

static bool qed_iov_validate_active_rxq(struct qed_hwfn *p_hwfn,
					struct qed_vf_info *p_vf)
{
	u8 i;

	for (i = 0; i < p_vf->num_rxqs; i++)
		if (qed_iov_validate_queue_mode(p_hwfn, p_vf, i,
						QED_IOV_VALIDATE_Q_ENABLE,
						false))
			return true;

	return false;
}

static bool qed_iov_validate_active_txq(struct qed_hwfn *p_hwfn,
					struct qed_vf_info *p_vf)
{
	u8 i;

	for (i = 0; i < p_vf->num_txqs; i++)
		if (qed_iov_validate_queue_mode(p_hwfn, p_vf, i,
						QED_IOV_VALIDATE_Q_ENABLE,
						true))
			return true;

	return false;
}

static int qed_iov_post_vf_bulletin(struct qed_hwfn *p_hwfn,
				    int vfid, struct qed_ptt *p_ptt)
{
	struct qed_bulletin_content *p_bulletin;
	int crc_size = sizeof(p_bulletin->crc);
	struct qed_dmae_params params;
	struct qed_vf_info *p_vf;

	p_vf = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!p_vf)
		return -EINVAL;

	if (!p_vf->vf_bulletin)
		return -EINVAL;

	p_bulletin = p_vf->bulletin.p_virt;

	/* Increment bulletin board version and compute crc */
	p_bulletin->version++;
	p_bulletin->crc = crc32(0, (u8 *)p_bulletin + crc_size,
				p_vf->bulletin.size - crc_size);

	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "Posting Bulletin 0x%08x to VF[%d] (CRC 0x%08x)\n",
		   p_bulletin->version, p_vf->relative_vf_id, p_bulletin->crc);

	/* propagate bulletin board via dmae to vm memory */
	memset(&params, 0, sizeof(params));
	SET_FIELD(params.flags, QED_DMAE_PARAMS_DST_VF_VALID, 0x1);
	params.dst_vfid = p_vf->abs_vf_id;
	return qed_dmae_host2host(p_hwfn, p_ptt, p_vf->bulletin.phys,
				  p_vf->vf_bulletin, p_vf->bulletin.size / 4,
				  &params);
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

		vf->num_mac_filters = QED_ETH_VF_NUM_MAC_FILTERS;
		vf->num_vlan_filters = QED_ETH_VF_NUM_VLAN_FILTERS;
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
	if (!p_sriov)
		return -ENOMEM;

	p_hwfn->pf_iov_info = p_sriov;

	qed_spq_register_async_cb(p_hwfn, PROTOCOLID_COMMON,
				  qed_sriov_eqe_event);

	return qed_iov_allocate_vfdb(p_hwfn);
}

void qed_iov_setup(struct qed_hwfn *p_hwfn)
{
	if (!IS_PF_SRIOV(p_hwfn) || !IS_PF_SRIOV_ALLOC(p_hwfn))
		return;

	qed_iov_setup_vfdb(p_hwfn);
}

void qed_iov_free(struct qed_hwfn *p_hwfn)
{
	qed_spq_unregister_async_cb(p_hwfn, PROTOCOLID_COMMON);

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

	if (is_kdump_kernel())
		return 0;

	if (IS_VF(p_hwfn->cdev))
		return 0;

	/* Learn the PCI configuration */
	pos = pci_find_ext_capability(p_hwfn->cdev->pdev,
				      PCI_EXT_CAP_ID_SRIOV);
	if (!pos) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV, "No PCIe IOV support\n");
		return 0;
	}

	/* Allocate a new struct for IOV information */
	cdev->p_iov_info = kzalloc(sizeof(*cdev->p_iov_info), GFP_KERNEL);
	if (!cdev->p_iov_info)
		return -ENOMEM;

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

	/* First VF index based on offset is tricky:
	 *  - If ARI is supported [likely], offset - (16 - pf_id) would
	 *    provide the number for eng0. 2nd engine Vfs would begin
	 *    after the first engine's VFs.
	 *  - If !ARI, VFs would start on next device.
	 *    so offset - (256 - pf_id) would provide the number.
	 * Utilize the fact that (256 - pf_id) is achieved only by later
	 * to differentiate between the two.
	 */

	if (p_hwfn->cdev->p_iov_info->offset < (256 - p_hwfn->abs_pf_id)) {
		u32 first = p_hwfn->cdev->p_iov_info->offset +
			    p_hwfn->abs_pf_id - 16;

		cdev->p_iov_info->first_vf_in_pf = first;

		if (QED_PATH_ID(p_hwfn))
			cdev->p_iov_info->first_vf_in_pf -= MAX_NUM_VFS_BB;
	} else {
		u32 first = p_hwfn->cdev->p_iov_info->offset +
			    p_hwfn->abs_pf_id - 256;

		cdev->p_iov_info->first_vf_in_pf = first;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "First VF in hwfn 0x%08x\n",
		   cdev->p_iov_info->first_vf_in_pf);

	return 0;
}

static bool _qed_iov_pf_sanity_check(struct qed_hwfn *p_hwfn,
				     int vfid, bool b_fail_malicious)
{
	/* Check PF supports sriov */
	if (IS_VF(p_hwfn->cdev) || !IS_QED_SRIOV(p_hwfn->cdev) ||
	    !IS_PF_SRIOV_ALLOC(p_hwfn))
		return false;

	/* Check VF validity */
	if (!qed_iov_is_valid_vfid(p_hwfn, vfid, true, b_fail_malicious))
		return false;

	return true;
}

static bool qed_iov_pf_sanity_check(struct qed_hwfn *p_hwfn, int vfid)
{
	return _qed_iov_pf_sanity_check(p_hwfn, vfid, true);
}

static void qed_iov_set_vf_to_disable(struct qed_dev *cdev,
				      u16 rel_vf_id, u8 to_disable)
{
	struct qed_vf_info *vf;
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		vf = qed_iov_get_vf_info(p_hwfn, rel_vf_id, false);
		if (!vf)
			continue;

		vf->to_disable = to_disable;
	}
}

static void qed_iov_set_vfs_to_disable(struct qed_dev *cdev, u8 to_disable)
{
	u16 i;

	if (!IS_QED_SRIOV(cdev))
		return;

	for (i = 0; i < cdev->p_iov_info->total_vfs; i++)
		qed_iov_set_vf_to_disable(cdev, i, to_disable);
}

static void qed_iov_vf_pglue_clear_err(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt, u8 abs_vfid)
{
	qed_wr(p_hwfn, p_ptt,
	       PGLUE_B_REG_WAS_ERROR_VF_31_0_CLR + (abs_vfid >> 5) * 4,
	       1 << (abs_vfid & 0x1f));
}

static void qed_iov_vf_igu_reset(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt, struct qed_vf_info *vf)
{
	int i;

	/* Set VF masks and configuration - pretend */
	qed_fid_pretend(p_hwfn, p_ptt, (u16) vf->concrete_fid);

	qed_wr(p_hwfn, p_ptt, IGU_REG_STATISTIC_NUM_VF_MSG_SENT, 0);

	/* unpretend */
	qed_fid_pretend(p_hwfn, p_ptt, (u16) p_hwfn->hw_info.concrete_fid);

	/* iterate over all queues, clear sb consumer */
	for (i = 0; i < vf->num_sbs; i++)
		qed_int_igu_init_pure_rt_single(p_hwfn, p_ptt,
						vf->igu_sbs[i],
						vf->opaque_fid, true);
}

static void qed_iov_vf_igu_set_int(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct qed_vf_info *vf, bool enable)
{
	u32 igu_vf_conf;

	qed_fid_pretend(p_hwfn, p_ptt, (u16) vf->concrete_fid);

	igu_vf_conf = qed_rd(p_hwfn, p_ptt, IGU_REG_VF_CONFIGURATION);

	if (enable)
		igu_vf_conf |= IGU_VF_CONF_MSI_MSIX_EN;
	else
		igu_vf_conf &= ~IGU_VF_CONF_MSI_MSIX_EN;

	qed_wr(p_hwfn, p_ptt, IGU_REG_VF_CONFIGURATION, igu_vf_conf);

	/* unpretend */
	qed_fid_pretend(p_hwfn, p_ptt, (u16) p_hwfn->hw_info.concrete_fid);
}

static int
qed_iov_enable_vf_access_msix(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt, u8 abs_vf_id, u8 num_sbs)
{
	u8 current_max = 0;
	int i;

	/* For AH onward, configuration is per-PF. Find maximum of all
	 * the currently enabled child VFs, and set the number to be that.
	 */
	if (!QED_IS_BB(p_hwfn->cdev)) {
		qed_for_each_vf(p_hwfn, i) {
			struct qed_vf_info *p_vf;

			p_vf = qed_iov_get_vf_info(p_hwfn, (u16)i, true);
			if (!p_vf)
				continue;

			current_max = max_t(u8, current_max, p_vf->num_sbs);
		}
	}

	if (num_sbs > current_max)
		return qed_mcp_config_vf_msix(p_hwfn, p_ptt,
					      abs_vf_id, num_sbs);

	return 0;
}

static int qed_iov_enable_vf_access(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    struct qed_vf_info *vf)
{
	u32 igu_vf_conf = IGU_VF_CONF_FUNC_EN;
	int rc;

	/* It's possible VF was previously considered malicious -
	 * clear the indication even if we're only going to disable VF.
	 */
	vf->b_malicious = false;

	if (vf->to_disable)
		return 0;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "Enable internal access for vf %x [abs %x]\n",
		   vf->abs_vf_id, QED_VF_ABS_ID(p_hwfn, vf));

	qed_iov_vf_pglue_clear_err(p_hwfn, p_ptt, QED_VF_ABS_ID(p_hwfn, vf));

	qed_iov_vf_igu_reset(p_hwfn, p_ptt, vf);

	rc = qed_iov_enable_vf_access_msix(p_hwfn, p_ptt,
					   vf->abs_vf_id, vf->num_sbs);
	if (rc)
		return rc;

	qed_fid_pretend(p_hwfn, p_ptt, (u16) vf->concrete_fid);

	SET_FIELD(igu_vf_conf, IGU_VF_CONF_PARENT, p_hwfn->rel_pf_id);
	STORE_RT_REG(p_hwfn, IGU_REG_VF_CONFIGURATION_RT_OFFSET, igu_vf_conf);

	qed_init_run(p_hwfn, p_ptt, PHASE_VF, vf->abs_vf_id,
		     p_hwfn->hw_info.hw_mode);

	/* unpretend */
	qed_fid_pretend(p_hwfn, p_ptt, (u16) p_hwfn->hw_info.concrete_fid);

	vf->state = VF_FREE;

	return rc;
}

/**
 * qed_iov_config_perm_table() - Configure the permission zone table.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT window for writing the registers.
 * @vf: VF info data.
 * @enable: The actual permision for this VF.
 *
 * In E4, queue zone permission table size is 320x9. There
 * are 320 VF queues for single engine device (256 for dual
 * engine device), and each entry has the following format:
 * {Valid, VF[7:0]}
 */
static void qed_iov_config_perm_table(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      struct qed_vf_info *vf, u8 enable)
{
	u32 reg_addr, val;
	u16 qzone_id = 0;
	int qid;

	for (qid = 0; qid < vf->num_rxqs; qid++) {
		qed_fw_l2_queue(p_hwfn, vf->vf_queues[qid].fw_rx_qid,
				&qzone_id);

		reg_addr = PSWHST_REG_ZONE_PERMISSION_TABLE + qzone_id * 4;
		val = enable ? (vf->abs_vf_id | BIT(8)) : 0;
		qed_wr(p_hwfn, p_ptt, reg_addr, val);
	}
}

static void qed_iov_enable_vf_traffic(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      struct qed_vf_info *vf)
{
	/* Reset vf in IGU - interrupts are still disabled */
	qed_iov_vf_igu_reset(p_hwfn, p_ptt, vf);

	qed_iov_vf_igu_set_int(p_hwfn, p_ptt, vf, 1);

	/* Permission Table */
	qed_iov_config_perm_table(p_hwfn, p_ptt, vf, true);
}

static u8 qed_iov_alloc_vf_igu_sbs(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct qed_vf_info *vf, u16 num_rx_queues)
{
	struct qed_igu_block *p_block;
	struct cau_sb_entry sb_entry;
	int qid = 0;
	u32 val = 0;

	if (num_rx_queues > p_hwfn->hw_info.p_igu_info->usage.free_cnt_iov)
		num_rx_queues = p_hwfn->hw_info.p_igu_info->usage.free_cnt_iov;
	p_hwfn->hw_info.p_igu_info->usage.free_cnt_iov -= num_rx_queues;

	SET_FIELD(val, IGU_MAPPING_LINE_FUNCTION_NUMBER, vf->abs_vf_id);
	SET_FIELD(val, IGU_MAPPING_LINE_VALID, 1);
	SET_FIELD(val, IGU_MAPPING_LINE_PF_VALID, 0);

	for (qid = 0; qid < num_rx_queues; qid++) {
		p_block = qed_get_igu_free_sb(p_hwfn, false);
		vf->igu_sbs[qid] = p_block->igu_sb_id;
		p_block->status &= ~QED_IGU_STATUS_FREE;
		SET_FIELD(val, IGU_MAPPING_LINE_VECTOR_NUMBER, qid);

		qed_wr(p_hwfn, p_ptt,
		       IGU_REG_MAPPING_MEMORY +
		       sizeof(u32) * p_block->igu_sb_id, val);

		/* Configure igu sb in CAU which were marked valid */
		qed_init_cau_sb_entry(p_hwfn, &sb_entry,
				      p_hwfn->rel_pf_id, vf->abs_vf_id, 1);

		qed_dmae_host2grc(p_hwfn, p_ptt,
				  (u64)(uintptr_t)&sb_entry,
				  CAU_REG_SB_VAR_MEMORY +
				  p_block->igu_sb_id * sizeof(u64), 2, NULL);
	}

	vf->num_sbs = (u8) num_rx_queues;

	return vf->num_sbs;
}

static void qed_iov_free_vf_igu_sbs(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    struct qed_vf_info *vf)
{
	struct qed_igu_info *p_info = p_hwfn->hw_info.p_igu_info;
	int idx, igu_id;
	u32 addr, val;

	/* Invalidate igu CAM lines and mark them as free */
	for (idx = 0; idx < vf->num_sbs; idx++) {
		igu_id = vf->igu_sbs[idx];
		addr = IGU_REG_MAPPING_MEMORY + sizeof(u32) * igu_id;

		val = qed_rd(p_hwfn, p_ptt, addr);
		SET_FIELD(val, IGU_MAPPING_LINE_VALID, 0);
		qed_wr(p_hwfn, p_ptt, addr, val);

		p_info->entry[igu_id].status |= QED_IGU_STATUS_FREE;
		p_hwfn->hw_info.p_igu_info->usage.free_cnt_iov++;
	}

	vf->num_sbs = 0;
}

static void qed_iov_set_link(struct qed_hwfn *p_hwfn,
			     u16 vfid,
			     struct qed_mcp_link_params *params,
			     struct qed_mcp_link_state *link,
			     struct qed_mcp_link_capabilities *p_caps)
{
	struct qed_vf_info *p_vf = qed_iov_get_vf_info(p_hwfn,
						       vfid,
						       false);
	struct qed_bulletin_content *p_bulletin;

	if (!p_vf)
		return;

	p_bulletin = p_vf->bulletin.p_virt;
	p_bulletin->req_autoneg = params->speed.autoneg;
	p_bulletin->req_adv_speed = params->speed.advertised_speeds;
	p_bulletin->req_forced_speed = params->speed.forced_speed;
	p_bulletin->req_autoneg_pause = params->pause.autoneg;
	p_bulletin->req_forced_rx = params->pause.forced_rx;
	p_bulletin->req_forced_tx = params->pause.forced_tx;
	p_bulletin->req_loopback = params->loopback_mode;

	p_bulletin->link_up = link->link_up;
	p_bulletin->speed = link->speed;
	p_bulletin->full_duplex = link->full_duplex;
	p_bulletin->autoneg = link->an;
	p_bulletin->autoneg_complete = link->an_complete;
	p_bulletin->parallel_detection = link->parallel_detection;
	p_bulletin->pfc_enabled = link->pfc_enabled;
	p_bulletin->partner_adv_speed = link->partner_adv_speed;
	p_bulletin->partner_tx_flow_ctrl_en = link->partner_tx_flow_ctrl_en;
	p_bulletin->partner_rx_flow_ctrl_en = link->partner_rx_flow_ctrl_en;
	p_bulletin->partner_adv_pause = link->partner_adv_pause;
	p_bulletin->sfp_tx_fault = link->sfp_tx_fault;

	p_bulletin->capability_speed = p_caps->speed_capabilities;
}

static int qed_iov_init_hw_for_vf(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_iov_vf_init_params *p_params)
{
	struct qed_mcp_link_capabilities link_caps;
	struct qed_mcp_link_params link_params;
	struct qed_mcp_link_state link_state;
	u8 num_of_vf_avaiable_chains = 0;
	struct qed_vf_info *vf = NULL;
	u16 qid, num_irqs;
	int rc = 0;
	u32 cids;
	u8 i;

	vf = qed_iov_get_vf_info(p_hwfn, p_params->rel_vf_id, false);
	if (!vf) {
		DP_ERR(p_hwfn, "qed_iov_init_hw_for_vf : vf is NULL\n");
		return -EINVAL;
	}

	if (vf->b_init) {
		DP_NOTICE(p_hwfn, "VF[%d] is already active.\n",
			  p_params->rel_vf_id);
		return -EINVAL;
	}

	/* Perform sanity checking on the requested queue_id */
	for (i = 0; i < p_params->num_queues; i++) {
		u16 min_vf_qzone = FEAT_NUM(p_hwfn, QED_PF_L2_QUE);
		u16 max_vf_qzone = min_vf_qzone +
		    FEAT_NUM(p_hwfn, QED_VF_L2_QUE) - 1;

		qid = p_params->req_rx_queue[i];
		if (qid < min_vf_qzone || qid > max_vf_qzone) {
			DP_NOTICE(p_hwfn,
				  "Can't enable Rx qid [%04x] for VF[%d]: qids [0x%04x,...,0x%04x] available\n",
				  qid,
				  p_params->rel_vf_id,
				  min_vf_qzone, max_vf_qzone);
			return -EINVAL;
		}

		qid = p_params->req_tx_queue[i];
		if (qid > max_vf_qzone) {
			DP_NOTICE(p_hwfn,
				  "Can't enable Tx qid [%04x] for VF[%d]: max qid 0x%04x\n",
				  qid, p_params->rel_vf_id, max_vf_qzone);
			return -EINVAL;
		}

		/* If client *really* wants, Tx qid can be shared with PF */
		if (qid < min_vf_qzone)
			DP_VERBOSE(p_hwfn,
				   QED_MSG_IOV,
				   "VF[%d] is using PF qid [0x%04x] for Txq[0x%02x]\n",
				   p_params->rel_vf_id, qid, i);
	}

	/* Limit number of queues according to number of CIDs */
	qed_cxt_get_proto_cid_count(p_hwfn, PROTOCOLID_ETH, &cids);
	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "VF[%d] - requesting to initialize for 0x%04x queues [0x%04x CIDs available]\n",
		   vf->relative_vf_id, p_params->num_queues, (u16)cids);
	num_irqs = min_t(u16, p_params->num_queues, ((u16)cids));

	num_of_vf_avaiable_chains = qed_iov_alloc_vf_igu_sbs(p_hwfn,
							     p_ptt,
							     vf, num_irqs);
	if (!num_of_vf_avaiable_chains) {
		DP_ERR(p_hwfn, "no available igu sbs\n");
		return -ENOMEM;
	}

	/* Choose queue number and index ranges */
	vf->num_rxqs = num_of_vf_avaiable_chains;
	vf->num_txqs = num_of_vf_avaiable_chains;

	for (i = 0; i < vf->num_rxqs; i++) {
		struct qed_vf_queue *p_queue = &vf->vf_queues[i];

		p_queue->fw_rx_qid = p_params->req_rx_queue[i];
		p_queue->fw_tx_qid = p_params->req_tx_queue[i];

		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF[%d] - Q[%d] SB %04x, qid [Rx %04x Tx %04x]\n",
			   vf->relative_vf_id, i, vf->igu_sbs[i],
			   p_queue->fw_rx_qid, p_queue->fw_tx_qid);
	}

	/* Update the link configuration in bulletin */
	memcpy(&link_params, qed_mcp_get_link_params(p_hwfn),
	       sizeof(link_params));
	memcpy(&link_state, qed_mcp_get_link_state(p_hwfn), sizeof(link_state));
	memcpy(&link_caps, qed_mcp_get_link_capabilities(p_hwfn),
	       sizeof(link_caps));
	qed_iov_set_link(p_hwfn, p_params->rel_vf_id,
			 &link_params, &link_state, &link_caps);

	rc = qed_iov_enable_vf_access(p_hwfn, p_ptt, vf);
	if (!rc) {
		vf->b_init = true;

		if (IS_LEAD_HWFN(p_hwfn))
			p_hwfn->cdev->p_iov_info->num_vfs++;
	}

	return rc;
}

static int qed_iov_release_hw_for_vf(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt, u16 rel_vf_id)
{
	struct qed_mcp_link_capabilities caps;
	struct qed_mcp_link_params params;
	struct qed_mcp_link_state link;
	struct qed_vf_info *vf = NULL;

	vf = qed_iov_get_vf_info(p_hwfn, rel_vf_id, true);
	if (!vf) {
		DP_ERR(p_hwfn, "qed_iov_release_hw_for_vf : vf is NULL\n");
		return -EINVAL;
	}

	if (vf->bulletin.p_virt)
		memset(vf->bulletin.p_virt, 0, sizeof(*vf->bulletin.p_virt));

	memset(&vf->p_vf_info, 0, sizeof(vf->p_vf_info));

	/* Get the link configuration back in bulletin so
	 * that when VFs are re-enabled they get the actual
	 * link configuration.
	 */
	memcpy(&params, qed_mcp_get_link_params(p_hwfn), sizeof(params));
	memcpy(&link, qed_mcp_get_link_state(p_hwfn), sizeof(link));
	memcpy(&caps, qed_mcp_get_link_capabilities(p_hwfn), sizeof(caps));
	qed_iov_set_link(p_hwfn, rel_vf_id, &params, &link, &caps);

	/* Forget the VF's acquisition message */
	memset(&vf->acquire, 0, sizeof(vf->acquire));

	/* disablng interrupts and resetting permission table was done during
	 * vf-close, however, we could get here without going through vf_close
	 */
	/* Disable Interrupts for VF */
	qed_iov_vf_igu_set_int(p_hwfn, p_ptt, vf, 0);

	/* Reset Permission table */
	qed_iov_config_perm_table(p_hwfn, p_ptt, vf, 0);

	vf->num_rxqs = 0;
	vf->num_txqs = 0;
	qed_iov_free_vf_igu_sbs(p_hwfn, p_ptt, vf);

	if (vf->b_init) {
		vf->b_init = false;

		if (IS_LEAD_HWFN(p_hwfn))
			p_hwfn->cdev->p_iov_info->num_vfs--;
	}

	return 0;
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

	memset(&params, 0, sizeof(params));
	SET_FIELD(params.flags, QED_DMAE_PARAMS_DST_VF_VALID, 0x1);
	params.dst_vfid = eng_vf_id;

	qed_dmae_host2host(p_hwfn, p_ptt, mbx->reply_phys + sizeof(u64),
			   mbx->req_virt->first_tlv.reply_address +
			   sizeof(u64),
			   (sizeof(union pfvf_tlvs) - sizeof(u64)) / 4,
			   &params);

	/* Once PF copies the rc to the VF, the latter can continue
	 * and send an additional message. So we have to make sure the
	 * channel would be re-set to ready prior to that.
	 */
	REG_WR(p_hwfn,
	       GTT_BAR0_MAP_REG_USDM_RAM +
	       USTORM_VF_PF_CHANNEL_READY_OFFSET(eng_vf_id), 1);

	qed_dmae_host2host(p_hwfn, p_ptt, mbx->reply_phys,
			   mbx->req_virt->first_tlv.reply_address,
			   sizeof(u64) / 4, &params);
}

static u16 qed_iov_vport_to_tlv(struct qed_hwfn *p_hwfn,
				enum qed_iov_vport_update_flag flag)
{
	switch (flag) {
	case QED_IOV_VP_UPDATE_ACTIVATE:
		return CHANNEL_TLV_VPORT_UPDATE_ACTIVATE;
	case QED_IOV_VP_UPDATE_VLAN_STRIP:
		return CHANNEL_TLV_VPORT_UPDATE_VLAN_STRIP;
	case QED_IOV_VP_UPDATE_TX_SWITCH:
		return CHANNEL_TLV_VPORT_UPDATE_TX_SWITCH;
	case QED_IOV_VP_UPDATE_MCAST:
		return CHANNEL_TLV_VPORT_UPDATE_MCAST;
	case QED_IOV_VP_UPDATE_ACCEPT_PARAM:
		return CHANNEL_TLV_VPORT_UPDATE_ACCEPT_PARAM;
	case QED_IOV_VP_UPDATE_RSS:
		return CHANNEL_TLV_VPORT_UPDATE_RSS;
	case QED_IOV_VP_UPDATE_ACCEPT_ANY_VLAN:
		return CHANNEL_TLV_VPORT_UPDATE_ACCEPT_ANY_VLAN;
	case QED_IOV_VP_UPDATE_SGE_TPA:
		return CHANNEL_TLV_VPORT_UPDATE_SGE_TPA;
	default:
		return 0;
	}
}

static u16 qed_iov_prep_vp_update_resp_tlvs(struct qed_hwfn *p_hwfn,
					    struct qed_vf_info *p_vf,
					    struct qed_iov_vf_mbx *p_mbx,
					    u8 status,
					    u16 tlvs_mask, u16 tlvs_accepted)
{
	struct pfvf_def_resp_tlv *resp;
	u16 size, total_len, i;

	memset(p_mbx->reply_virt, 0, sizeof(union pfvf_tlvs));
	p_mbx->offset = (u8 *)p_mbx->reply_virt;
	size = sizeof(struct pfvf_def_resp_tlv);
	total_len = size;

	qed_add_tlv(p_hwfn, &p_mbx->offset, CHANNEL_TLV_VPORT_UPDATE, size);

	/* Prepare response for all extended tlvs if they are found by PF */
	for (i = 0; i < QED_IOV_VP_UPDATE_MAX; i++) {
		if (!(tlvs_mask & BIT(i)))
			continue;

		resp = qed_add_tlv(p_hwfn, &p_mbx->offset,
				   qed_iov_vport_to_tlv(p_hwfn, i), size);

		if (tlvs_accepted & BIT(i))
			resp->hdr.status = status;
		else
			resp->hdr.status = PFVF_STATUS_NOT_SUPPORTED;

		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[%d] - vport_update response: TLV %d, status %02x\n",
			   p_vf->relative_vf_id,
			   qed_iov_vport_to_tlv(p_hwfn, i), resp->hdr.status);

		total_len += size;
	}

	qed_add_tlv(p_hwfn, &p_mbx->offset, CHANNEL_TLV_LIST_END,
		    sizeof(struct channel_list_end_tlv));

	return total_len;
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

static struct
qed_public_vf_info *qed_iov_get_public_vf_info(struct qed_hwfn *p_hwfn,
					       u16 relative_vf_id,
					       bool b_enabled_only)
{
	struct qed_vf_info *vf = NULL;

	vf = qed_iov_get_vf_info(p_hwfn, relative_vf_id, b_enabled_only);
	if (!vf)
		return NULL;

	return &vf->p_vf_info;
}

static void qed_iov_clean_vf(struct qed_hwfn *p_hwfn, u8 vfid)
{
	struct qed_public_vf_info *vf_info;

	vf_info = qed_iov_get_public_vf_info(p_hwfn, vfid, false);

	if (!vf_info)
		return;

	/* Clear the VF mac */
	eth_zero_addr(vf_info->mac);

	vf_info->rx_accept_mode = 0;
	vf_info->tx_accept_mode = 0;
}

static void qed_iov_vf_cleanup(struct qed_hwfn *p_hwfn,
			       struct qed_vf_info *p_vf)
{
	u32 i, j;

	p_vf->vf_bulletin = 0;
	p_vf->vport_instance = 0;
	p_vf->configured_features = 0;

	/* If VF previously requested less resources, go back to default */
	p_vf->num_rxqs = p_vf->num_sbs;
	p_vf->num_txqs = p_vf->num_sbs;

	p_vf->num_active_rxqs = 0;

	for (i = 0; i < QED_MAX_VF_CHAINS_PER_PF; i++) {
		struct qed_vf_queue *p_queue = &p_vf->vf_queues[i];

		for (j = 0; j < MAX_QUEUES_PER_QZONE; j++) {
			if (!p_queue->cids[j].p_cid)
				continue;

			qed_eth_queue_cid_release(p_hwfn,
						  p_queue->cids[j].p_cid);
			p_queue->cids[j].p_cid = NULL;
		}
	}

	memset(&p_vf->shadow_config, 0, sizeof(p_vf->shadow_config));
	memset(&p_vf->acquire, 0, sizeof(p_vf->acquire));
	qed_iov_clean_vf(p_hwfn, p_vf->relative_vf_id);
}

/* Returns either 0, or log(size) */
static u32 qed_iov_vf_db_bar_size(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt)
{
	u32 val = qed_rd(p_hwfn, p_ptt, PGLUE_B_REG_VF_BAR1_SIZE);

	if (val)
		return val + 11;
	return 0;
}

static void
qed_iov_vf_mbx_acquire_resc_cids(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 struct qed_vf_info *p_vf,
				 struct vf_pf_resc_request *p_req,
				 struct pf_vf_resc *p_resp)
{
	u8 num_vf_cons = p_hwfn->pf_params.eth_pf_params.num_vf_cons;
	u8 db_size = qed_db_addr_vf(1, DQ_DEMS_LEGACY) -
		     qed_db_addr_vf(0, DQ_DEMS_LEGACY);
	u32 bar_size;

	p_resp->num_cids = min_t(u8, p_req->num_cids, num_vf_cons);

	/* If VF didn't bother asking for QIDs than don't bother limiting
	 * number of CIDs. The VF doesn't care about the number, and this
	 * has the likely result of causing an additional acquisition.
	 */
	if (!(p_vf->acquire.vfdev_info.capabilities &
	      VFPF_ACQUIRE_CAP_QUEUE_QIDS))
		return;

	/* If doorbell bar was mapped by VF, limit the VF CIDs to an amount
	 * that would make sure doorbells for all CIDs fall within the bar.
	 * If it doesn't, make sure regview window is sufficient.
	 */
	if (p_vf->acquire.vfdev_info.capabilities &
	    VFPF_ACQUIRE_CAP_PHYSICAL_BAR) {
		bar_size = qed_iov_vf_db_bar_size(p_hwfn, p_ptt);
		if (bar_size)
			bar_size = 1 << bar_size;

		if (p_hwfn->cdev->num_hwfns > 1)
			bar_size /= 2;
	} else {
		bar_size = PXP_VF_BAR0_DQ_LENGTH;
	}

	if (bar_size / db_size < 256)
		p_resp->num_cids = min_t(u8, p_resp->num_cids,
					 (u8)(bar_size / db_size));
}

static u8 qed_iov_vf_mbx_acquire_resc(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      struct qed_vf_info *p_vf,
				      struct vf_pf_resc_request *p_req,
				      struct pf_vf_resc *p_resp)
{
	u8 i;

	/* Queue related information */
	p_resp->num_rxqs = p_vf->num_rxqs;
	p_resp->num_txqs = p_vf->num_txqs;
	p_resp->num_sbs = p_vf->num_sbs;

	for (i = 0; i < p_resp->num_sbs; i++) {
		p_resp->hw_sbs[i].hw_sb_id = p_vf->igu_sbs[i];
		p_resp->hw_sbs[i].sb_qid = 0;
	}

	/* These fields are filled for backward compatibility.
	 * Unused by modern vfs.
	 */
	for (i = 0; i < p_resp->num_rxqs; i++) {
		qed_fw_l2_queue(p_hwfn, p_vf->vf_queues[i].fw_rx_qid,
				(u16 *)&p_resp->hw_qid[i]);
		p_resp->cid[i] = i;
	}

	/* Filter related information */
	p_resp->num_mac_filters = min_t(u8, p_vf->num_mac_filters,
					p_req->num_mac_filters);
	p_resp->num_vlan_filters = min_t(u8, p_vf->num_vlan_filters,
					 p_req->num_vlan_filters);

	qed_iov_vf_mbx_acquire_resc_cids(p_hwfn, p_ptt, p_vf, p_req, p_resp);

	/* This isn't really needed/enforced, but some legacy VFs might depend
	 * on the correct filling of this field.
	 */
	p_resp->num_mc_filters = QED_MAX_MC_ADDRS;

	/* Validate sufficient resources for VF */
	if (p_resp->num_rxqs < p_req->num_rxqs ||
	    p_resp->num_txqs < p_req->num_txqs ||
	    p_resp->num_sbs < p_req->num_sbs ||
	    p_resp->num_mac_filters < p_req->num_mac_filters ||
	    p_resp->num_vlan_filters < p_req->num_vlan_filters ||
	    p_resp->num_mc_filters < p_req->num_mc_filters ||
	    p_resp->num_cids < p_req->num_cids) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[%d] - Insufficient resources: rxq [%02x/%02x] txq [%02x/%02x] sbs [%02x/%02x] mac [%02x/%02x] vlan [%02x/%02x] mc [%02x/%02x] cids [%02x/%02x]\n",
			   p_vf->abs_vf_id,
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
			   p_req->num_mc_filters,
			   p_resp->num_mc_filters,
			   p_req->num_cids, p_resp->num_cids);

		/* Some legacy OSes are incapable of correctly handling this
		 * failure.
		 */
		if ((p_vf->acquire.vfdev_info.eth_fp_hsi_minor ==
		     ETH_HSI_VER_NO_PKT_LEN_TUNN) &&
		    (p_vf->acquire.vfdev_info.os_type ==
		     VFPF_ACQUIRE_OS_WINDOWS))
			return PFVF_STATUS_SUCCESS;

		return PFVF_STATUS_NO_RESOURCE;
	}

	return PFVF_STATUS_SUCCESS;
}

static void qed_iov_vf_mbx_acquire_stats(struct qed_hwfn *p_hwfn,
					 struct pfvf_stats_info *p_stats)
{
	p_stats->mstats.address = PXP_VF_BAR0_START_MSDM_ZONE_B +
				  offsetof(struct mstorm_vf_zone,
					   non_trigger.eth_queue_stat);
	p_stats->mstats.len = sizeof(struct eth_mstorm_per_queue_stat);
	p_stats->ustats.address = PXP_VF_BAR0_START_USDM_ZONE_B +
				  offsetof(struct ustorm_vf_zone,
					   non_trigger.eth_queue_stat);
	p_stats->ustats.len = sizeof(struct eth_ustorm_per_queue_stat);
	p_stats->pstats.address = PXP_VF_BAR0_START_PSDM_ZONE_B +
				  offsetof(struct pstorm_vf_zone,
					   non_trigger.eth_queue_stat);
	p_stats->pstats.len = sizeof(struct eth_pstorm_per_queue_stat);
	p_stats->tstats.address = 0;
	p_stats->tstats.len = 0;
}

static void qed_iov_vf_mbx_acquire(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct qed_vf_info *vf)
{
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct pfvf_acquire_resp_tlv *resp = &mbx->reply_virt->acquire_resp;
	struct pf_vf_pfdev_info *pfdev_info = &resp->pfdev_info;
	struct vfpf_acquire_tlv *req = &mbx->req_virt->acquire;
	u8 vfpf_status = PFVF_STATUS_NOT_SUPPORTED;
	struct pf_vf_resc *resc = &resp->resc;
	int rc;

	memset(resp, 0, sizeof(*resp));

	/* Write the PF version so that VF would know which version
	 * is supported - might be later overriden. This guarantees that
	 * VF could recognize legacy PF based on lack of versions in reply.
	 */
	pfdev_info->major_fp_hsi = ETH_HSI_VER_MAJOR;
	pfdev_info->minor_fp_hsi = ETH_HSI_VER_MINOR;

	if (vf->state != VF_FREE && vf->state != VF_STOPPED) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[%d] sent ACQUIRE but is already in state %d - fail request\n",
			   vf->abs_vf_id, vf->state);
		goto out;
	}

	/* Validate FW compatibility */
	if (req->vfdev_info.eth_fp_hsi_major != ETH_HSI_VER_MAJOR) {
		if (req->vfdev_info.capabilities &
		    VFPF_ACQUIRE_CAP_PRE_FP_HSI) {
			struct vf_pf_vfdev_info *p_vfdev = &req->vfdev_info;

			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "VF[%d] is pre-fastpath HSI\n",
				   vf->abs_vf_id);
			p_vfdev->eth_fp_hsi_major = ETH_HSI_VER_MAJOR;
			p_vfdev->eth_fp_hsi_minor = ETH_HSI_VER_NO_PKT_LEN_TUNN;
		} else {
			DP_INFO(p_hwfn,
				"VF[%d] needs fastpath HSI %02x.%02x, which is incompatible with loaded FW's fastpath HSI %02x.%02x\n",
				vf->abs_vf_id,
				req->vfdev_info.eth_fp_hsi_major,
				req->vfdev_info.eth_fp_hsi_minor,
				ETH_HSI_VER_MAJOR, ETH_HSI_VER_MINOR);

			goto out;
		}
	}

	/* On 100g PFs, prevent old VFs from loading */
	if ((p_hwfn->cdev->num_hwfns > 1) &&
	    !(req->vfdev_info.capabilities & VFPF_ACQUIRE_CAP_100G)) {
		DP_INFO(p_hwfn,
			"VF[%d] is running an old driver that doesn't support 100g\n",
			vf->abs_vf_id);
		goto out;
	}

	/* Store the acquire message */
	memcpy(&vf->acquire, req, sizeof(vf->acquire));

	vf->opaque_fid = req->vfdev_info.opaque_fid;

	vf->vf_bulletin = req->bulletin_addr;
	vf->bulletin.size = (vf->bulletin.size < req->bulletin_size) ?
			    vf->bulletin.size : req->bulletin_size;

	/* fill in pfdev info */
	pfdev_info->chip_num = p_hwfn->cdev->chip_num;
	pfdev_info->db_size = 0;
	pfdev_info->indices_per_sb = PIS_PER_SB_E4;

	pfdev_info->capabilities = PFVF_ACQUIRE_CAP_DEFAULT_UNTAGGED |
				   PFVF_ACQUIRE_CAP_POST_FW_OVERRIDE;
	if (p_hwfn->cdev->num_hwfns > 1)
		pfdev_info->capabilities |= PFVF_ACQUIRE_CAP_100G;

	/* Share our ability to use multiple queue-ids only with VFs
	 * that request it.
	 */
	if (req->vfdev_info.capabilities & VFPF_ACQUIRE_CAP_QUEUE_QIDS)
		pfdev_info->capabilities |= PFVF_ACQUIRE_CAP_QUEUE_QIDS;

	/* Share the sizes of the bars with VF */
	resp->pfdev_info.bar_size = qed_iov_vf_db_bar_size(p_hwfn, p_ptt);

	qed_iov_vf_mbx_acquire_stats(p_hwfn, &pfdev_info->stats_info);

	memcpy(pfdev_info->port_mac, p_hwfn->hw_info.hw_mac_addr, ETH_ALEN);

	pfdev_info->fw_major = FW_MAJOR_VERSION;
	pfdev_info->fw_minor = FW_MINOR_VERSION;
	pfdev_info->fw_rev = FW_REVISION_VERSION;
	pfdev_info->fw_eng = FW_ENGINEERING_VERSION;

	/* Incorrect when legacy, but doesn't matter as legacy isn't reading
	 * this field.
	 */
	pfdev_info->minor_fp_hsi = min_t(u8, ETH_HSI_VER_MINOR,
					 req->vfdev_info.eth_fp_hsi_minor);
	pfdev_info->os_type = VFPF_ACQUIRE_OS_LINUX;
	qed_mcp_get_mfw_ver(p_hwfn, p_ptt, &pfdev_info->mfw_ver, NULL);

	pfdev_info->dev_type = p_hwfn->cdev->type;
	pfdev_info->chip_rev = p_hwfn->cdev->chip_rev;

	/* Fill resources available to VF; Make sure there are enough to
	 * satisfy the VF's request.
	 */
	vfpf_status = qed_iov_vf_mbx_acquire_resc(p_hwfn, p_ptt, vf,
						  &req->resc_request, resc);
	if (vfpf_status != PFVF_STATUS_SUCCESS)
		goto out;

	/* Start the VF in FW */
	rc = qed_sp_vf_start(p_hwfn, vf);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to start VF[%02x]\n", vf->abs_vf_id);
		vfpf_status = PFVF_STATUS_FAILURE;
		goto out;
	}

	/* Fill agreed size of bulletin board in response */
	resp->bulletin_size = vf->bulletin.size;
	qed_iov_post_vf_bulletin(p_hwfn, vf->relative_vf_id, p_ptt);

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "VF[%d] ACQUIRE_RESPONSE: pfdev_info- chip_num=0x%x, db_size=%d, idx_per_sb=%d, pf_cap=0x%llx\n"
		   "resources- n_rxq-%d, n_txq-%d, n_sbs-%d, n_macs-%d, n_vlans-%d\n",
		   vf->abs_vf_id,
		   resp->pfdev_info.chip_num,
		   resp->pfdev_info.db_size,
		   resp->pfdev_info.indices_per_sb,
		   resp->pfdev_info.capabilities,
		   resc->num_rxqs,
		   resc->num_txqs,
		   resc->num_sbs,
		   resc->num_mac_filters,
		   resc->num_vlan_filters);
	vf->state = VF_ACQUIRED;

	/* Prepare Response */
out:
	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_ACQUIRE,
			     sizeof(struct pfvf_acquire_resp_tlv), vfpf_status);
}

static int __qed_iov_spoofchk_set(struct qed_hwfn *p_hwfn,
				  struct qed_vf_info *p_vf, bool val)
{
	struct qed_sp_vport_update_params params;
	int rc;

	if (val == p_vf->spoof_chk) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Spoofchk value[%d] is already configured\n", val);
		return 0;
	}

	memset(&params, 0, sizeof(struct qed_sp_vport_update_params));
	params.opaque_fid = p_vf->opaque_fid;
	params.vport_id = p_vf->vport_id;
	params.update_anti_spoofing_en_flg = 1;
	params.anti_spoofing_en = val;

	rc = qed_sp_vport_update(p_hwfn, &params, QED_SPQ_MODE_EBLOCK, NULL);
	if (!rc) {
		p_vf->spoof_chk = val;
		p_vf->req_spoofchk_val = p_vf->spoof_chk;
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Spoofchk val[%d] configured\n", val);
	} else {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Spoofchk configuration[val:%d] failed for VF[%d]\n",
			   val, p_vf->relative_vf_id);
	}

	return rc;
}

static int qed_iov_reconfigure_unicast_vlan(struct qed_hwfn *p_hwfn,
					    struct qed_vf_info *p_vf)
{
	struct qed_filter_ucast filter;
	int rc = 0;
	int i;

	memset(&filter, 0, sizeof(filter));
	filter.is_rx_filter = 1;
	filter.is_tx_filter = 1;
	filter.vport_to_add_to = p_vf->vport_id;
	filter.opcode = QED_FILTER_ADD;

	/* Reconfigure vlans */
	for (i = 0; i < QED_ETH_VF_NUM_VLAN_FILTERS + 1; i++) {
		if (!p_vf->shadow_config.vlans[i].used)
			continue;

		filter.type = QED_FILTER_VLAN;
		filter.vlan = p_vf->shadow_config.vlans[i].vid;
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Reconfiguring VLAN [0x%04x] for VF [%04x]\n",
			   filter.vlan, p_vf->relative_vf_id);
		rc = qed_sp_eth_filter_ucast(p_hwfn, p_vf->opaque_fid,
					     &filter, QED_SPQ_MODE_CB, NULL);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "Failed to configure VLAN [%04x] to VF [%04x]\n",
				  filter.vlan, p_vf->relative_vf_id);
			break;
		}
	}

	return rc;
}

static int
qed_iov_reconfigure_unicast_shadow(struct qed_hwfn *p_hwfn,
				   struct qed_vf_info *p_vf, u64 events)
{
	int rc = 0;

	if ((events & BIT(VLAN_ADDR_FORCED)) &&
	    !(p_vf->configured_features & (1 << VLAN_ADDR_FORCED)))
		rc = qed_iov_reconfigure_unicast_vlan(p_hwfn, p_vf);

	return rc;
}

static int qed_iov_configure_vport_forced(struct qed_hwfn *p_hwfn,
					  struct qed_vf_info *p_vf, u64 events)
{
	int rc = 0;
	struct qed_filter_ucast filter;

	if (!p_vf->vport_instance)
		return -EINVAL;

	if ((events & BIT(MAC_ADDR_FORCED)) ||
	    p_vf->p_vf_info.is_trusted_configured) {
		/* Since there's no way [currently] of removing the MAC,
		 * we can always assume this means we need to force it.
		 */
		memset(&filter, 0, sizeof(filter));
		filter.type = QED_FILTER_MAC;
		filter.opcode = QED_FILTER_REPLACE;
		filter.is_rx_filter = 1;
		filter.is_tx_filter = 1;
		filter.vport_to_add_to = p_vf->vport_id;
		ether_addr_copy(filter.mac, p_vf->bulletin.p_virt->mac);

		rc = qed_sp_eth_filter_ucast(p_hwfn, p_vf->opaque_fid,
					     &filter, QED_SPQ_MODE_CB, NULL);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "PF failed to configure MAC for VF\n");
			return rc;
		}
		if (p_vf->p_vf_info.is_trusted_configured)
			p_vf->configured_features |=
				BIT(VFPF_BULLETIN_MAC_ADDR);
		else
			p_vf->configured_features |=
				BIT(MAC_ADDR_FORCED);
	}

	if (events & BIT(VLAN_ADDR_FORCED)) {
		struct qed_sp_vport_update_params vport_update;
		u8 removal;
		int i;

		memset(&filter, 0, sizeof(filter));
		filter.type = QED_FILTER_VLAN;
		filter.is_rx_filter = 1;
		filter.is_tx_filter = 1;
		filter.vport_to_add_to = p_vf->vport_id;
		filter.vlan = p_vf->bulletin.p_virt->pvid;
		filter.opcode = filter.vlan ? QED_FILTER_REPLACE :
					      QED_FILTER_FLUSH;

		/* Send the ramrod */
		rc = qed_sp_eth_filter_ucast(p_hwfn, p_vf->opaque_fid,
					     &filter, QED_SPQ_MODE_CB, NULL);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "PF failed to configure VLAN for VF\n");
			return rc;
		}

		/* Update the default-vlan & silent vlan stripping */
		memset(&vport_update, 0, sizeof(vport_update));
		vport_update.opaque_fid = p_vf->opaque_fid;
		vport_update.vport_id = p_vf->vport_id;
		vport_update.update_default_vlan_enable_flg = 1;
		vport_update.default_vlan_enable_flg = filter.vlan ? 1 : 0;
		vport_update.update_default_vlan_flg = 1;
		vport_update.default_vlan = filter.vlan;

		vport_update.update_inner_vlan_removal_flg = 1;
		removal = filter.vlan ? 1
				      : p_vf->shadow_config.inner_vlan_removal;
		vport_update.inner_vlan_removal_flg = removal;
		vport_update.silent_vlan_removal_flg = filter.vlan ? 1 : 0;
		rc = qed_sp_vport_update(p_hwfn,
					 &vport_update,
					 QED_SPQ_MODE_EBLOCK, NULL);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "PF failed to configure VF vport for vlan\n");
			return rc;
		}

		/* Update all the Rx queues */
		for (i = 0; i < QED_MAX_VF_CHAINS_PER_PF; i++) {
			struct qed_vf_queue *p_queue = &p_vf->vf_queues[i];
			struct qed_queue_cid *p_cid = NULL;

			/* There can be at most 1 Rx queue on qzone. Find it */
			p_cid = qed_iov_get_vf_rx_queue_cid(p_queue);
			if (!p_cid)
				continue;

			rc = qed_sp_eth_rx_queues_update(p_hwfn,
							 (void **)&p_cid,
							 1, 0, 1,
							 QED_SPQ_MODE_EBLOCK,
							 NULL);
			if (rc) {
				DP_NOTICE(p_hwfn,
					  "Failed to send Rx update fo queue[0x%04x]\n",
					  p_cid->rel.queue_id);
				return rc;
			}
		}

		if (filter.vlan)
			p_vf->configured_features |= 1 << VLAN_ADDR_FORCED;
		else
			p_vf->configured_features &= ~BIT(VLAN_ADDR_FORCED);
	}

	/* If forced features are terminated, we need to configure the shadow
	 * configuration back again.
	 */
	if (events)
		qed_iov_reconfigure_unicast_shadow(p_hwfn, p_vf, events);

	return rc;
}

static void qed_iov_vf_mbx_start_vport(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       struct qed_vf_info *vf)
{
	struct qed_sp_vport_start_params params = { 0 };
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct vfpf_vport_start_tlv *start;
	u8 status = PFVF_STATUS_SUCCESS;
	struct qed_vf_info *vf_info;
	u64 *p_bitmap;
	int sb_id;
	int rc;

	vf_info = qed_iov_get_vf_info(p_hwfn, (u16) vf->relative_vf_id, true);
	if (!vf_info) {
		DP_NOTICE(p_hwfn->cdev,
			  "Failed to get VF info, invalid vfid [%d]\n",
			  vf->relative_vf_id);
		return;
	}

	vf->state = VF_ENABLED;
	start = &mbx->req_virt->start_vport;

	qed_iov_enable_vf_traffic(p_hwfn, p_ptt, vf);

	/* Initialize Status block in CAU */
	for (sb_id = 0; sb_id < vf->num_sbs; sb_id++) {
		if (!start->sb_addr[sb_id]) {
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "VF[%d] did not fill the address of SB %d\n",
				   vf->relative_vf_id, sb_id);
			break;
		}

		qed_int_cau_conf_sb(p_hwfn, p_ptt,
				    start->sb_addr[sb_id],
				    vf->igu_sbs[sb_id], vf->abs_vf_id, 1);
	}

	vf->mtu = start->mtu;
	vf->shadow_config.inner_vlan_removal = start->inner_vlan_removal;

	/* Take into consideration configuration forced by hypervisor;
	 * If none is configured, use the supplied VF values [for old
	 * vfs that would still be fine, since they passed '0' as padding].
	 */
	p_bitmap = &vf_info->bulletin.p_virt->valid_bitmap;
	if (!(*p_bitmap & BIT(VFPF_BULLETIN_UNTAGGED_DEFAULT_FORCED))) {
		u8 vf_req = start->only_untagged;

		vf_info->bulletin.p_virt->default_only_untagged = vf_req;
		*p_bitmap |= 1 << VFPF_BULLETIN_UNTAGGED_DEFAULT;
	}

	params.tpa_mode = start->tpa_mode;
	params.remove_inner_vlan = start->inner_vlan_removal;
	params.tx_switching = true;

	params.only_untagged = vf_info->bulletin.p_virt->default_only_untagged;
	params.drop_ttl0 = false;
	params.concrete_fid = vf->concrete_fid;
	params.opaque_fid = vf->opaque_fid;
	params.vport_id = vf->vport_id;
	params.max_buffers_per_cqe = start->max_buffers_per_cqe;
	params.mtu = vf->mtu;

	/* Non trusted VFs should enable control frame filtering */
	params.check_mac = !vf->p_vf_info.is_trusted_configured;

	rc = qed_sp_eth_vport_start(p_hwfn, &params);
	if (rc) {
		DP_ERR(p_hwfn,
		       "qed_iov_vf_mbx_start_vport returned error %d\n", rc);
		status = PFVF_STATUS_FAILURE;
	} else {
		vf->vport_instance++;

		/* Force configuration if needed on the newly opened vport */
		qed_iov_configure_vport_forced(p_hwfn, vf, *p_bitmap);

		__qed_iov_spoofchk_set(p_hwfn, vf, vf->req_spoofchk_val);
	}
	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_VPORT_START,
			     sizeof(struct pfvf_def_resp_tlv), status);
}

static void qed_iov_vf_mbx_stop_vport(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      struct qed_vf_info *vf)
{
	u8 status = PFVF_STATUS_SUCCESS;
	int rc;

	vf->vport_instance--;
	vf->spoof_chk = false;

	if ((qed_iov_validate_active_rxq(p_hwfn, vf)) ||
	    (qed_iov_validate_active_txq(p_hwfn, vf))) {
		vf->b_malicious = true;
		DP_NOTICE(p_hwfn,
			  "VF [%02x] - considered malicious; Unable to stop RX/TX queues\n",
			  vf->abs_vf_id);
		status = PFVF_STATUS_MALICIOUS;
		goto out;
	}

	rc = qed_sp_vport_stop(p_hwfn, vf->opaque_fid, vf->vport_id);
	if (rc) {
		DP_ERR(p_hwfn, "qed_iov_vf_mbx_stop_vport returned error %d\n",
		       rc);
		status = PFVF_STATUS_FAILURE;
	}

	/* Forget the configuration on the vport */
	vf->configured_features = 0;
	memset(&vf->shadow_config, 0, sizeof(vf->shadow_config));

out:
	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_VPORT_TEARDOWN,
			     sizeof(struct pfvf_def_resp_tlv), status);
}

static void qed_iov_vf_mbx_start_rxq_resp(struct qed_hwfn *p_hwfn,
					  struct qed_ptt *p_ptt,
					  struct qed_vf_info *vf,
					  u8 status, bool b_legacy)
{
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct pfvf_start_queue_resp_tlv *p_tlv;
	struct vfpf_start_rxq_tlv *req;
	u16 length;

	mbx->offset = (u8 *)mbx->reply_virt;

	/* Taking a bigger struct instead of adding a TLV to list was a
	 * mistake, but one which we're now stuck with, as some older
	 * clients assume the size of the previous response.
	 */
	if (!b_legacy)
		length = sizeof(*p_tlv);
	else
		length = sizeof(struct pfvf_def_resp_tlv);

	p_tlv = qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_START_RXQ,
			    length);
	qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_LIST_END,
		    sizeof(struct channel_list_end_tlv));

	/* Update the TLV with the response */
	if ((status == PFVF_STATUS_SUCCESS) && !b_legacy) {
		req = &mbx->req_virt->start_rxq;
		p_tlv->offset = PXP_VF_BAR0_START_MSDM_ZONE_B +
				offsetof(struct mstorm_vf_zone,
					 non_trigger.eth_rx_queue_producers) +
				sizeof(struct eth_rx_prod_data) * req->rx_qid;
	}

	qed_iov_send_response(p_hwfn, p_ptt, vf, length, status);
}

static u8 qed_iov_vf_mbx_qid(struct qed_hwfn *p_hwfn,
			     struct qed_vf_info *p_vf, bool b_is_tx)
{
	struct qed_iov_vf_mbx *p_mbx = &p_vf->vf_mbx;
	struct vfpf_qid_tlv *p_qid_tlv;

	/* Search for the qid if the VF published its going to provide it */
	if (!(p_vf->acquire.vfdev_info.capabilities &
	      VFPF_ACQUIRE_CAP_QUEUE_QIDS)) {
		if (b_is_tx)
			return QED_IOV_LEGACY_QID_TX;
		else
			return QED_IOV_LEGACY_QID_RX;
	}

	p_qid_tlv = (struct vfpf_qid_tlv *)
		    qed_iov_search_list_tlvs(p_hwfn, p_mbx->req_virt,
					     CHANNEL_TLV_QID);
	if (!p_qid_tlv) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF[%2x]: Failed to provide qid\n",
			   p_vf->relative_vf_id);

		return QED_IOV_QID_INVALID;
	}

	if (p_qid_tlv->qid >= MAX_QUEUES_PER_QZONE) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF[%02x]: Provided qid out-of-bounds %02x\n",
			   p_vf->relative_vf_id, p_qid_tlv->qid);
		return QED_IOV_QID_INVALID;
	}

	return p_qid_tlv->qid;
}

static void qed_iov_vf_mbx_start_rxq(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_vf_info *vf)
{
	struct qed_queue_start_common_params params;
	struct qed_queue_cid_vf_params vf_params;
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	u8 status = PFVF_STATUS_NO_RESOURCE;
	u8 qid_usage_idx, vf_legacy = 0;
	struct vfpf_start_rxq_tlv *req;
	struct qed_vf_queue *p_queue;
	struct qed_queue_cid *p_cid;
	struct qed_sb_info sb_dummy;
	int rc;

	req = &mbx->req_virt->start_rxq;

	if (!qed_iov_validate_rxq(p_hwfn, vf, req->rx_qid,
				  QED_IOV_VALIDATE_Q_DISABLE) ||
	    !qed_iov_validate_sb(p_hwfn, vf, req->hw_sb))
		goto out;

	qid_usage_idx = qed_iov_vf_mbx_qid(p_hwfn, vf, false);
	if (qid_usage_idx == QED_IOV_QID_INVALID)
		goto out;

	p_queue = &vf->vf_queues[req->rx_qid];
	if (p_queue->cids[qid_usage_idx].p_cid)
		goto out;

	vf_legacy = qed_vf_calculate_legacy(vf);

	/* Acquire a new queue-cid */
	memset(&params, 0, sizeof(params));
	params.queue_id = p_queue->fw_rx_qid;
	params.vport_id = vf->vport_id;
	params.stats_id = vf->abs_vf_id + 0x10;
	/* Since IGU index is passed via sb_info, construct a dummy one */
	memset(&sb_dummy, 0, sizeof(sb_dummy));
	sb_dummy.igu_sb_id = req->hw_sb;
	params.p_sb = &sb_dummy;
	params.sb_idx = req->sb_index;

	memset(&vf_params, 0, sizeof(vf_params));
	vf_params.vfid = vf->relative_vf_id;
	vf_params.vf_qid = (u8)req->rx_qid;
	vf_params.vf_legacy = vf_legacy;
	vf_params.qid_usage_idx = qid_usage_idx;
	p_cid = qed_eth_queue_to_cid(p_hwfn, vf->opaque_fid,
				     &params, true, &vf_params);
	if (!p_cid)
		goto out;

	/* Legacy VFs have their Producers in a different location, which they
	 * calculate on their own and clean the producer prior to this.
	 */
	if (!(vf_legacy & QED_QCID_LEGACY_VF_RX_PROD))
		REG_WR(p_hwfn,
		       GTT_BAR0_MAP_REG_MSDM_RAM +
		       MSTORM_ETH_VF_PRODS_OFFSET(vf->abs_vf_id, req->rx_qid),
		       0);

	rc = qed_eth_rxq_start_ramrod(p_hwfn, p_cid,
				      req->bd_max_bytes,
				      req->rxq_addr,
				      req->cqe_pbl_addr, req->cqe_pbl_size);
	if (rc) {
		status = PFVF_STATUS_FAILURE;
		qed_eth_queue_cid_release(p_hwfn, p_cid);
	} else {
		p_queue->cids[qid_usage_idx].p_cid = p_cid;
		p_queue->cids[qid_usage_idx].b_is_tx = false;
		status = PFVF_STATUS_SUCCESS;
		vf->num_active_rxqs++;
	}

out:
	qed_iov_vf_mbx_start_rxq_resp(p_hwfn, p_ptt, vf, status,
				      !!(vf_legacy &
					 QED_QCID_LEGACY_VF_RX_PROD));
}

static void
qed_iov_pf_update_tun_response(struct pfvf_update_tunn_param_tlv *p_resp,
			       struct qed_tunnel_info *p_tun,
			       u16 tunn_feature_mask)
{
	p_resp->tunn_feature_mask = tunn_feature_mask;
	p_resp->vxlan_mode = p_tun->vxlan.b_mode_enabled;
	p_resp->l2geneve_mode = p_tun->l2_geneve.b_mode_enabled;
	p_resp->ipgeneve_mode = p_tun->ip_geneve.b_mode_enabled;
	p_resp->l2gre_mode = p_tun->l2_gre.b_mode_enabled;
	p_resp->ipgre_mode = p_tun->l2_gre.b_mode_enabled;
	p_resp->vxlan_clss = p_tun->vxlan.tun_cls;
	p_resp->l2gre_clss = p_tun->l2_gre.tun_cls;
	p_resp->ipgre_clss = p_tun->ip_gre.tun_cls;
	p_resp->l2geneve_clss = p_tun->l2_geneve.tun_cls;
	p_resp->ipgeneve_clss = p_tun->ip_geneve.tun_cls;
	p_resp->geneve_udp_port = p_tun->geneve_port.port;
	p_resp->vxlan_udp_port = p_tun->vxlan_port.port;
}

static void
__qed_iov_pf_update_tun_param(struct vfpf_update_tunn_param_tlv *p_req,
			      struct qed_tunn_update_type *p_tun,
			      enum qed_tunn_mode mask, u8 tun_cls)
{
	if (p_req->tun_mode_update_mask & BIT(mask)) {
		p_tun->b_update_mode = true;

		if (p_req->tunn_mode & BIT(mask))
			p_tun->b_mode_enabled = true;
	}

	p_tun->tun_cls = tun_cls;
}

static void
qed_iov_pf_update_tun_param(struct vfpf_update_tunn_param_tlv *p_req,
			    struct qed_tunn_update_type *p_tun,
			    struct qed_tunn_update_udp_port *p_port,
			    enum qed_tunn_mode mask,
			    u8 tun_cls, u8 update_port, u16 port)
{
	if (update_port) {
		p_port->b_update_port = true;
		p_port->port = port;
	}

	__qed_iov_pf_update_tun_param(p_req, p_tun, mask, tun_cls);
}

static bool
qed_iov_pf_validate_tunn_param(struct vfpf_update_tunn_param_tlv *p_req)
{
	bool b_update_requested = false;

	if (p_req->tun_mode_update_mask || p_req->update_tun_cls ||
	    p_req->update_geneve_port || p_req->update_vxlan_port)
		b_update_requested = true;

	return b_update_requested;
}

static void qed_pf_validate_tunn_mode(struct qed_tunn_update_type *tun, int *rc)
{
	if (tun->b_update_mode && !tun->b_mode_enabled) {
		tun->b_update_mode = false;
		*rc = -EINVAL;
	}
}

static int
qed_pf_validate_modify_tunn_config(struct qed_hwfn *p_hwfn,
				   u16 *tun_features, bool *update,
				   struct qed_tunnel_info *tun_src)
{
	struct qed_eth_cb_ops *ops = p_hwfn->cdev->protocol_ops.eth;
	struct qed_tunnel_info *tun = &p_hwfn->cdev->tunnel;
	u16 bultn_vxlan_port, bultn_geneve_port;
	void *cookie = p_hwfn->cdev->ops_cookie;
	int i, rc = 0;

	*tun_features = p_hwfn->cdev->tunn_feature_mask;
	bultn_vxlan_port = tun->vxlan_port.port;
	bultn_geneve_port = tun->geneve_port.port;
	qed_pf_validate_tunn_mode(&tun_src->vxlan, &rc);
	qed_pf_validate_tunn_mode(&tun_src->l2_geneve, &rc);
	qed_pf_validate_tunn_mode(&tun_src->ip_geneve, &rc);
	qed_pf_validate_tunn_mode(&tun_src->l2_gre, &rc);
	qed_pf_validate_tunn_mode(&tun_src->ip_gre, &rc);

	if ((tun_src->b_update_rx_cls || tun_src->b_update_tx_cls) &&
	    (tun_src->vxlan.tun_cls != QED_TUNN_CLSS_MAC_VLAN ||
	     tun_src->l2_geneve.tun_cls != QED_TUNN_CLSS_MAC_VLAN ||
	     tun_src->ip_geneve.tun_cls != QED_TUNN_CLSS_MAC_VLAN ||
	     tun_src->l2_gre.tun_cls != QED_TUNN_CLSS_MAC_VLAN ||
	     tun_src->ip_gre.tun_cls != QED_TUNN_CLSS_MAC_VLAN)) {
		tun_src->b_update_rx_cls = false;
		tun_src->b_update_tx_cls = false;
		rc = -EINVAL;
	}

	if (tun_src->vxlan_port.b_update_port) {
		if (tun_src->vxlan_port.port == tun->vxlan_port.port) {
			tun_src->vxlan_port.b_update_port = false;
		} else {
			*update = true;
			bultn_vxlan_port = tun_src->vxlan_port.port;
		}
	}

	if (tun_src->geneve_port.b_update_port) {
		if (tun_src->geneve_port.port == tun->geneve_port.port) {
			tun_src->geneve_port.b_update_port = false;
		} else {
			*update = true;
			bultn_geneve_port = tun_src->geneve_port.port;
		}
	}

	qed_for_each_vf(p_hwfn, i) {
		qed_iov_bulletin_set_udp_ports(p_hwfn, i, bultn_vxlan_port,
					       bultn_geneve_port);
	}

	qed_schedule_iov(p_hwfn, QED_IOV_WQ_BULLETIN_UPDATE_FLAG);
	ops->ports_update(cookie, bultn_vxlan_port, bultn_geneve_port);

	return rc;
}

static void qed_iov_vf_mbx_update_tunn_param(struct qed_hwfn *p_hwfn,
					     struct qed_ptt *p_ptt,
					     struct qed_vf_info *p_vf)
{
	struct qed_tunnel_info *p_tun = &p_hwfn->cdev->tunnel;
	struct qed_iov_vf_mbx *mbx = &p_vf->vf_mbx;
	struct pfvf_update_tunn_param_tlv *p_resp;
	struct vfpf_update_tunn_param_tlv *p_req;
	u8 status = PFVF_STATUS_SUCCESS;
	bool b_update_required = false;
	struct qed_tunnel_info tunn;
	u16 tunn_feature_mask = 0;
	int i, rc = 0;

	mbx->offset = (u8 *)mbx->reply_virt;

	memset(&tunn, 0, sizeof(tunn));
	p_req = &mbx->req_virt->tunn_param_update;

	if (!qed_iov_pf_validate_tunn_param(p_req)) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "No tunnel update requested by VF\n");
		status = PFVF_STATUS_FAILURE;
		goto send_resp;
	}

	tunn.b_update_rx_cls = p_req->update_tun_cls;
	tunn.b_update_tx_cls = p_req->update_tun_cls;

	qed_iov_pf_update_tun_param(p_req, &tunn.vxlan, &tunn.vxlan_port,
				    QED_MODE_VXLAN_TUNN, p_req->vxlan_clss,
				    p_req->update_vxlan_port,
				    p_req->vxlan_port);
	qed_iov_pf_update_tun_param(p_req, &tunn.l2_geneve, &tunn.geneve_port,
				    QED_MODE_L2GENEVE_TUNN,
				    p_req->l2geneve_clss,
				    p_req->update_geneve_port,
				    p_req->geneve_port);
	__qed_iov_pf_update_tun_param(p_req, &tunn.ip_geneve,
				      QED_MODE_IPGENEVE_TUNN,
				      p_req->ipgeneve_clss);
	__qed_iov_pf_update_tun_param(p_req, &tunn.l2_gre,
				      QED_MODE_L2GRE_TUNN, p_req->l2gre_clss);
	__qed_iov_pf_update_tun_param(p_req, &tunn.ip_gre,
				      QED_MODE_IPGRE_TUNN, p_req->ipgre_clss);

	/* If PF modifies VF's req then it should
	 * still return an error in case of partial configuration
	 * or modified configuration as opposed to requested one.
	 */
	rc = qed_pf_validate_modify_tunn_config(p_hwfn, &tunn_feature_mask,
						&b_update_required, &tunn);

	if (rc)
		status = PFVF_STATUS_FAILURE;

	/* If QED client is willing to update anything ? */
	if (b_update_required) {
		u16 geneve_port;

		rc = qed_sp_pf_update_tunn_cfg(p_hwfn, p_ptt, &tunn,
					       QED_SPQ_MODE_EBLOCK, NULL);
		if (rc)
			status = PFVF_STATUS_FAILURE;

		geneve_port = p_tun->geneve_port.port;
		qed_for_each_vf(p_hwfn, i) {
			qed_iov_bulletin_set_udp_ports(p_hwfn, i,
						       p_tun->vxlan_port.port,
						       geneve_port);
		}
	}

send_resp:
	p_resp = qed_add_tlv(p_hwfn, &mbx->offset,
			     CHANNEL_TLV_UPDATE_TUNN_PARAM, sizeof(*p_resp));

	qed_iov_pf_update_tun_response(p_resp, p_tun, tunn_feature_mask);
	qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_LIST_END,
		    sizeof(struct channel_list_end_tlv));

	qed_iov_send_response(p_hwfn, p_ptt, p_vf, sizeof(*p_resp), status);
}

static void qed_iov_vf_mbx_start_txq_resp(struct qed_hwfn *p_hwfn,
					  struct qed_ptt *p_ptt,
					  struct qed_vf_info *p_vf,
					  u32 cid, u8 status)
{
	struct qed_iov_vf_mbx *mbx = &p_vf->vf_mbx;
	struct pfvf_start_queue_resp_tlv *p_tlv;
	bool b_legacy = false;
	u16 length;

	mbx->offset = (u8 *)mbx->reply_virt;

	/* Taking a bigger struct instead of adding a TLV to list was a
	 * mistake, but one which we're now stuck with, as some older
	 * clients assume the size of the previous response.
	 */
	if (p_vf->acquire.vfdev_info.eth_fp_hsi_minor ==
	    ETH_HSI_VER_NO_PKT_LEN_TUNN)
		b_legacy = true;

	if (!b_legacy)
		length = sizeof(*p_tlv);
	else
		length = sizeof(struct pfvf_def_resp_tlv);

	p_tlv = qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_START_TXQ,
			    length);
	qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_LIST_END,
		    sizeof(struct channel_list_end_tlv));

	/* Update the TLV with the response */
	if ((status == PFVF_STATUS_SUCCESS) && !b_legacy)
		p_tlv->offset = qed_db_addr_vf(cid, DQ_DEMS_LEGACY);

	qed_iov_send_response(p_hwfn, p_ptt, p_vf, length, status);
}

static void qed_iov_vf_mbx_start_txq(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_vf_info *vf)
{
	struct qed_queue_start_common_params params;
	struct qed_queue_cid_vf_params vf_params;
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	u8 status = PFVF_STATUS_NO_RESOURCE;
	struct vfpf_start_txq_tlv *req;
	struct qed_vf_queue *p_queue;
	struct qed_queue_cid *p_cid;
	struct qed_sb_info sb_dummy;
	u8 qid_usage_idx, vf_legacy;
	u32 cid = 0;
	int rc;
	u16 pq;

	memset(&params, 0, sizeof(params));
	req = &mbx->req_virt->start_txq;

	if (!qed_iov_validate_txq(p_hwfn, vf, req->tx_qid,
				  QED_IOV_VALIDATE_Q_NA) ||
	    !qed_iov_validate_sb(p_hwfn, vf, req->hw_sb))
		goto out;

	qid_usage_idx = qed_iov_vf_mbx_qid(p_hwfn, vf, true);
	if (qid_usage_idx == QED_IOV_QID_INVALID)
		goto out;

	p_queue = &vf->vf_queues[req->tx_qid];
	if (p_queue->cids[qid_usage_idx].p_cid)
		goto out;

	vf_legacy = qed_vf_calculate_legacy(vf);

	/* Acquire a new queue-cid */
	params.queue_id = p_queue->fw_tx_qid;
	params.vport_id = vf->vport_id;
	params.stats_id = vf->abs_vf_id + 0x10;

	/* Since IGU index is passed via sb_info, construct a dummy one */
	memset(&sb_dummy, 0, sizeof(sb_dummy));
	sb_dummy.igu_sb_id = req->hw_sb;
	params.p_sb = &sb_dummy;
	params.sb_idx = req->sb_index;

	memset(&vf_params, 0, sizeof(vf_params));
	vf_params.vfid = vf->relative_vf_id;
	vf_params.vf_qid = (u8)req->tx_qid;
	vf_params.vf_legacy = vf_legacy;
	vf_params.qid_usage_idx = qid_usage_idx;

	p_cid = qed_eth_queue_to_cid(p_hwfn, vf->opaque_fid,
				     &params, false, &vf_params);
	if (!p_cid)
		goto out;

	pq = qed_get_cm_pq_idx_vf(p_hwfn, vf->relative_vf_id);
	rc = qed_eth_txq_start_ramrod(p_hwfn, p_cid,
				      req->pbl_addr, req->pbl_size, pq);
	if (rc) {
		status = PFVF_STATUS_FAILURE;
		qed_eth_queue_cid_release(p_hwfn, p_cid);
	} else {
		status = PFVF_STATUS_SUCCESS;
		p_queue->cids[qid_usage_idx].p_cid = p_cid;
		p_queue->cids[qid_usage_idx].b_is_tx = true;
		cid = p_cid->cid;
	}

out:
	qed_iov_vf_mbx_start_txq_resp(p_hwfn, p_ptt, vf, cid, status);
}

static int qed_iov_vf_stop_rxqs(struct qed_hwfn *p_hwfn,
				struct qed_vf_info *vf,
				u16 rxq_id,
				u8 qid_usage_idx, bool cqe_completion)
{
	struct qed_vf_queue *p_queue;
	int rc = 0;

	if (!qed_iov_validate_rxq(p_hwfn, vf, rxq_id, QED_IOV_VALIDATE_Q_NA)) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[%d] Tried Closing Rx 0x%04x.%02x which is inactive\n",
			   vf->relative_vf_id, rxq_id, qid_usage_idx);
		return -EINVAL;
	}

	p_queue = &vf->vf_queues[rxq_id];

	/* We've validated the index and the existence of the active RXQ -
	 * now we need to make sure that it's using the correct qid.
	 */
	if (!p_queue->cids[qid_usage_idx].p_cid ||
	    p_queue->cids[qid_usage_idx].b_is_tx) {
		struct qed_queue_cid *p_cid;

		p_cid = qed_iov_get_vf_rx_queue_cid(p_queue);
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[%d] - Tried Closing Rx 0x%04x.%02x, but Rx is at %04x.%02x\n",
			   vf->relative_vf_id,
			   rxq_id, qid_usage_idx, rxq_id, p_cid->qid_usage_idx);
		return -EINVAL;
	}

	/* Now that we know we have a valid Rx-queue - close it */
	rc = qed_eth_rx_queue_stop(p_hwfn,
				   p_queue->cids[qid_usage_idx].p_cid,
				   false, cqe_completion);
	if (rc)
		return rc;

	p_queue->cids[qid_usage_idx].p_cid = NULL;
	vf->num_active_rxqs--;

	return 0;
}

static int qed_iov_vf_stop_txqs(struct qed_hwfn *p_hwfn,
				struct qed_vf_info *vf,
				u16 txq_id, u8 qid_usage_idx)
{
	struct qed_vf_queue *p_queue;
	int rc = 0;

	if (!qed_iov_validate_txq(p_hwfn, vf, txq_id, QED_IOV_VALIDATE_Q_NA))
		return -EINVAL;

	p_queue = &vf->vf_queues[txq_id];
	if (!p_queue->cids[qid_usage_idx].p_cid ||
	    !p_queue->cids[qid_usage_idx].b_is_tx)
		return -EINVAL;

	rc = qed_eth_tx_queue_stop(p_hwfn, p_queue->cids[qid_usage_idx].p_cid);
	if (rc)
		return rc;

	p_queue->cids[qid_usage_idx].p_cid = NULL;
	return 0;
}

static void qed_iov_vf_mbx_stop_rxqs(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_vf_info *vf)
{
	u16 length = sizeof(struct pfvf_def_resp_tlv);
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	u8 status = PFVF_STATUS_FAILURE;
	struct vfpf_stop_rxqs_tlv *req;
	u8 qid_usage_idx;
	int rc;

	/* There has never been an official driver that used this interface
	 * for stopping multiple queues, and it is now considered deprecated.
	 * Validate this isn't used here.
	 */
	req = &mbx->req_virt->stop_rxqs;
	if (req->num_rxqs != 1) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Odd; VF[%d] tried stopping multiple Rx queues\n",
			   vf->relative_vf_id);
		status = PFVF_STATUS_NOT_SUPPORTED;
		goto out;
	}

	/* Find which qid-index is associated with the queue */
	qid_usage_idx = qed_iov_vf_mbx_qid(p_hwfn, vf, false);
	if (qid_usage_idx == QED_IOV_QID_INVALID)
		goto out;

	rc = qed_iov_vf_stop_rxqs(p_hwfn, vf, req->rx_qid,
				  qid_usage_idx, req->cqe_completion);
	if (!rc)
		status = PFVF_STATUS_SUCCESS;
out:
	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_STOP_RXQS,
			     length, status);
}

static void qed_iov_vf_mbx_stop_txqs(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_vf_info *vf)
{
	u16 length = sizeof(struct pfvf_def_resp_tlv);
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	u8 status = PFVF_STATUS_FAILURE;
	struct vfpf_stop_txqs_tlv *req;
	u8 qid_usage_idx;
	int rc;

	/* There has never been an official driver that used this interface
	 * for stopping multiple queues, and it is now considered deprecated.
	 * Validate this isn't used here.
	 */
	req = &mbx->req_virt->stop_txqs;
	if (req->num_txqs != 1) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Odd; VF[%d] tried stopping multiple Tx queues\n",
			   vf->relative_vf_id);
		status = PFVF_STATUS_NOT_SUPPORTED;
		goto out;
	}

	/* Find which qid-index is associated with the queue */
	qid_usage_idx = qed_iov_vf_mbx_qid(p_hwfn, vf, true);
	if (qid_usage_idx == QED_IOV_QID_INVALID)
		goto out;

	rc = qed_iov_vf_stop_txqs(p_hwfn, vf, req->tx_qid, qid_usage_idx);
	if (!rc)
		status = PFVF_STATUS_SUCCESS;

out:
	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_STOP_TXQS,
			     length, status);
}

static void qed_iov_vf_mbx_update_rxqs(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       struct qed_vf_info *vf)
{
	struct qed_queue_cid *handlers[QED_MAX_VF_CHAINS_PER_PF];
	u16 length = sizeof(struct pfvf_def_resp_tlv);
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct vfpf_update_rxq_tlv *req;
	u8 status = PFVF_STATUS_FAILURE;
	u8 complete_event_flg;
	u8 complete_cqe_flg;
	u8 qid_usage_idx;
	int rc;
	u8 i;

	req = &mbx->req_virt->update_rxq;
	complete_cqe_flg = !!(req->flags & VFPF_RXQ_UPD_COMPLETE_CQE_FLAG);
	complete_event_flg = !!(req->flags & VFPF_RXQ_UPD_COMPLETE_EVENT_FLAG);

	qid_usage_idx = qed_iov_vf_mbx_qid(p_hwfn, vf, false);
	if (qid_usage_idx == QED_IOV_QID_INVALID)
		goto out;

	/* There shouldn't exist a VF that uses queue-qids yet uses this
	 * API with multiple Rx queues. Validate this.
	 */
	if ((vf->acquire.vfdev_info.capabilities &
	     VFPF_ACQUIRE_CAP_QUEUE_QIDS) && req->num_rxqs != 1) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF[%d] supports QIDs but sends multiple queues\n",
			   vf->relative_vf_id);
		goto out;
	}

	/* Validate inputs - for the legacy case this is still true since
	 * qid_usage_idx for each Rx queue would be LEGACY_QID_RX.
	 */
	for (i = req->rx_qid; i < req->rx_qid + req->num_rxqs; i++) {
		if (!qed_iov_validate_rxq(p_hwfn, vf, i,
					  QED_IOV_VALIDATE_Q_NA) ||
		    !vf->vf_queues[i].cids[qid_usage_idx].p_cid ||
		    vf->vf_queues[i].cids[qid_usage_idx].b_is_tx) {
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "VF[%d]: Incorrect Rxqs [%04x, %02x]\n",
				   vf->relative_vf_id, req->rx_qid,
				   req->num_rxqs);
			goto out;
		}
	}

	/* Prepare the handlers */
	for (i = 0; i < req->num_rxqs; i++) {
		u16 qid = req->rx_qid + i;

		handlers[i] = vf->vf_queues[qid].cids[qid_usage_idx].p_cid;
	}

	rc = qed_sp_eth_rx_queues_update(p_hwfn, (void **)&handlers,
					 req->num_rxqs,
					 complete_cqe_flg,
					 complete_event_flg,
					 QED_SPQ_MODE_EBLOCK, NULL);
	if (rc)
		goto out;

	status = PFVF_STATUS_SUCCESS;
out:
	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_UPDATE_RXQ,
			     length, status);
}

void *qed_iov_search_list_tlvs(struct qed_hwfn *p_hwfn,
			       void *p_tlvs_list, u16 req_type)
{
	struct channel_tlv *p_tlv = (struct channel_tlv *)p_tlvs_list;
	int len = 0;

	do {
		if (!p_tlv->length) {
			DP_NOTICE(p_hwfn, "Zero length TLV found\n");
			return NULL;
		}

		if (p_tlv->type == req_type) {
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "Extended tlv type %d, length %d found\n",
				   p_tlv->type, p_tlv->length);
			return p_tlv;
		}

		len += p_tlv->length;
		p_tlv = (struct channel_tlv *)((u8 *)p_tlv + p_tlv->length);

		if ((len + p_tlv->length) > TLV_BUFFER_SIZE) {
			DP_NOTICE(p_hwfn, "TLVs has overrun the buffer size\n");
			return NULL;
		}
	} while (p_tlv->type != CHANNEL_TLV_LIST_END);

	return NULL;
}

static void
qed_iov_vp_update_act_param(struct qed_hwfn *p_hwfn,
			    struct qed_sp_vport_update_params *p_data,
			    struct qed_iov_vf_mbx *p_mbx, u16 *tlvs_mask)
{
	struct vfpf_vport_update_activate_tlv *p_act_tlv;
	u16 tlv = CHANNEL_TLV_VPORT_UPDATE_ACTIVATE;

	p_act_tlv = (struct vfpf_vport_update_activate_tlv *)
		    qed_iov_search_list_tlvs(p_hwfn, p_mbx->req_virt, tlv);
	if (!p_act_tlv)
		return;

	p_data->update_vport_active_rx_flg = p_act_tlv->update_rx;
	p_data->vport_active_rx_flg = p_act_tlv->active_rx;
	p_data->update_vport_active_tx_flg = p_act_tlv->update_tx;
	p_data->vport_active_tx_flg = p_act_tlv->active_tx;
	*tlvs_mask |= 1 << QED_IOV_VP_UPDATE_ACTIVATE;
}

static void
qed_iov_vp_update_vlan_param(struct qed_hwfn *p_hwfn,
			     struct qed_sp_vport_update_params *p_data,
			     struct qed_vf_info *p_vf,
			     struct qed_iov_vf_mbx *p_mbx, u16 *tlvs_mask)
{
	struct vfpf_vport_update_vlan_strip_tlv *p_vlan_tlv;
	u16 tlv = CHANNEL_TLV_VPORT_UPDATE_VLAN_STRIP;

	p_vlan_tlv = (struct vfpf_vport_update_vlan_strip_tlv *)
		     qed_iov_search_list_tlvs(p_hwfn, p_mbx->req_virt, tlv);
	if (!p_vlan_tlv)
		return;

	p_vf->shadow_config.inner_vlan_removal = p_vlan_tlv->remove_vlan;

	/* Ignore the VF request if we're forcing a vlan */
	if (!(p_vf->configured_features & BIT(VLAN_ADDR_FORCED))) {
		p_data->update_inner_vlan_removal_flg = 1;
		p_data->inner_vlan_removal_flg = p_vlan_tlv->remove_vlan;
	}

	*tlvs_mask |= 1 << QED_IOV_VP_UPDATE_VLAN_STRIP;
}

static void
qed_iov_vp_update_tx_switch(struct qed_hwfn *p_hwfn,
			    struct qed_sp_vport_update_params *p_data,
			    struct qed_iov_vf_mbx *p_mbx, u16 *tlvs_mask)
{
	struct vfpf_vport_update_tx_switch_tlv *p_tx_switch_tlv;
	u16 tlv = CHANNEL_TLV_VPORT_UPDATE_TX_SWITCH;

	p_tx_switch_tlv = (struct vfpf_vport_update_tx_switch_tlv *)
			  qed_iov_search_list_tlvs(p_hwfn, p_mbx->req_virt,
						   tlv);
	if (!p_tx_switch_tlv)
		return;

	p_data->update_tx_switching_flg = 1;
	p_data->tx_switching_flg = p_tx_switch_tlv->tx_switching;
	*tlvs_mask |= 1 << QED_IOV_VP_UPDATE_TX_SWITCH;
}

static void
qed_iov_vp_update_mcast_bin_param(struct qed_hwfn *p_hwfn,
				  struct qed_sp_vport_update_params *p_data,
				  struct qed_iov_vf_mbx *p_mbx, u16 *tlvs_mask)
{
	struct vfpf_vport_update_mcast_bin_tlv *p_mcast_tlv;
	u16 tlv = CHANNEL_TLV_VPORT_UPDATE_MCAST;

	p_mcast_tlv = (struct vfpf_vport_update_mcast_bin_tlv *)
	    qed_iov_search_list_tlvs(p_hwfn, p_mbx->req_virt, tlv);
	if (!p_mcast_tlv)
		return;

	p_data->update_approx_mcast_flg = 1;
	memcpy(p_data->bins, p_mcast_tlv->bins,
	       sizeof(u32) * ETH_MULTICAST_MAC_BINS_IN_REGS);
	*tlvs_mask |= 1 << QED_IOV_VP_UPDATE_MCAST;
}

static void
qed_iov_vp_update_accept_flag(struct qed_hwfn *p_hwfn,
			      struct qed_sp_vport_update_params *p_data,
			      struct qed_iov_vf_mbx *p_mbx, u16 *tlvs_mask)
{
	struct qed_filter_accept_flags *p_flags = &p_data->accept_flags;
	struct vfpf_vport_update_accept_param_tlv *p_accept_tlv;
	u16 tlv = CHANNEL_TLV_VPORT_UPDATE_ACCEPT_PARAM;

	p_accept_tlv = (struct vfpf_vport_update_accept_param_tlv *)
	    qed_iov_search_list_tlvs(p_hwfn, p_mbx->req_virt, tlv);
	if (!p_accept_tlv)
		return;

	p_flags->update_rx_mode_config = p_accept_tlv->update_rx_mode;
	p_flags->rx_accept_filter = p_accept_tlv->rx_accept_filter;
	p_flags->update_tx_mode_config = p_accept_tlv->update_tx_mode;
	p_flags->tx_accept_filter = p_accept_tlv->tx_accept_filter;
	*tlvs_mask |= 1 << QED_IOV_VP_UPDATE_ACCEPT_PARAM;
}

static void
qed_iov_vp_update_accept_any_vlan(struct qed_hwfn *p_hwfn,
				  struct qed_sp_vport_update_params *p_data,
				  struct qed_iov_vf_mbx *p_mbx, u16 *tlvs_mask)
{
	struct vfpf_vport_update_accept_any_vlan_tlv *p_accept_any_vlan;
	u16 tlv = CHANNEL_TLV_VPORT_UPDATE_ACCEPT_ANY_VLAN;

	p_accept_any_vlan = (struct vfpf_vport_update_accept_any_vlan_tlv *)
			    qed_iov_search_list_tlvs(p_hwfn, p_mbx->req_virt,
						     tlv);
	if (!p_accept_any_vlan)
		return;

	p_data->accept_any_vlan = p_accept_any_vlan->accept_any_vlan;
	p_data->update_accept_any_vlan_flg =
		    p_accept_any_vlan->update_accept_any_vlan_flg;
	*tlvs_mask |= 1 << QED_IOV_VP_UPDATE_ACCEPT_ANY_VLAN;
}

static void
qed_iov_vp_update_rss_param(struct qed_hwfn *p_hwfn,
			    struct qed_vf_info *vf,
			    struct qed_sp_vport_update_params *p_data,
			    struct qed_rss_params *p_rss,
			    struct qed_iov_vf_mbx *p_mbx,
			    u16 *tlvs_mask, u16 *tlvs_accepted)
{
	struct vfpf_vport_update_rss_tlv *p_rss_tlv;
	u16 tlv = CHANNEL_TLV_VPORT_UPDATE_RSS;
	bool b_reject = false;
	u16 table_size;
	u16 i, q_idx;

	p_rss_tlv = (struct vfpf_vport_update_rss_tlv *)
		    qed_iov_search_list_tlvs(p_hwfn, p_mbx->req_virt, tlv);
	if (!p_rss_tlv) {
		p_data->rss_params = NULL;
		return;
	}

	memset(p_rss, 0, sizeof(struct qed_rss_params));

	p_rss->update_rss_config = !!(p_rss_tlv->update_rss_flags &
				      VFPF_UPDATE_RSS_CONFIG_FLAG);
	p_rss->update_rss_capabilities = !!(p_rss_tlv->update_rss_flags &
					    VFPF_UPDATE_RSS_CAPS_FLAG);
	p_rss->update_rss_ind_table = !!(p_rss_tlv->update_rss_flags &
					 VFPF_UPDATE_RSS_IND_TABLE_FLAG);
	p_rss->update_rss_key = !!(p_rss_tlv->update_rss_flags &
				   VFPF_UPDATE_RSS_KEY_FLAG);

	p_rss->rss_enable = p_rss_tlv->rss_enable;
	p_rss->rss_eng_id = vf->relative_vf_id + 1;
	p_rss->rss_caps = p_rss_tlv->rss_caps;
	p_rss->rss_table_size_log = p_rss_tlv->rss_table_size_log;
	memcpy(p_rss->rss_key, p_rss_tlv->rss_key, sizeof(p_rss->rss_key));

	table_size = min_t(u16, ARRAY_SIZE(p_rss->rss_ind_table),
			   (1 << p_rss_tlv->rss_table_size_log));

	for (i = 0; i < table_size; i++) {
		struct qed_queue_cid *p_cid;

		q_idx = p_rss_tlv->rss_ind_table[i];
		if (!qed_iov_validate_rxq(p_hwfn, vf, q_idx,
					  QED_IOV_VALIDATE_Q_ENABLE)) {
			DP_VERBOSE(p_hwfn,
				   QED_MSG_IOV,
				   "VF[%d]: Omitting RSS due to wrong queue %04x\n",
				   vf->relative_vf_id, q_idx);
			b_reject = true;
			goto out;
		}

		p_cid = qed_iov_get_vf_rx_queue_cid(&vf->vf_queues[q_idx]);
		p_rss->rss_ind_table[i] = p_cid;
	}

	p_data->rss_params = p_rss;
out:
	*tlvs_mask |= 1 << QED_IOV_VP_UPDATE_RSS;
	if (!b_reject)
		*tlvs_accepted |= 1 << QED_IOV_VP_UPDATE_RSS;
}

static void
qed_iov_vp_update_sge_tpa_param(struct qed_hwfn *p_hwfn,
				struct qed_vf_info *vf,
				struct qed_sp_vport_update_params *p_data,
				struct qed_sge_tpa_params *p_sge_tpa,
				struct qed_iov_vf_mbx *p_mbx, u16 *tlvs_mask)
{
	struct vfpf_vport_update_sge_tpa_tlv *p_sge_tpa_tlv;
	u16 tlv = CHANNEL_TLV_VPORT_UPDATE_SGE_TPA;

	p_sge_tpa_tlv = (struct vfpf_vport_update_sge_tpa_tlv *)
	    qed_iov_search_list_tlvs(p_hwfn, p_mbx->req_virt, tlv);

	if (!p_sge_tpa_tlv) {
		p_data->sge_tpa_params = NULL;
		return;
	}

	memset(p_sge_tpa, 0, sizeof(struct qed_sge_tpa_params));

	p_sge_tpa->update_tpa_en_flg =
	    !!(p_sge_tpa_tlv->update_sge_tpa_flags & VFPF_UPDATE_TPA_EN_FLAG);
	p_sge_tpa->update_tpa_param_flg =
	    !!(p_sge_tpa_tlv->update_sge_tpa_flags &
		VFPF_UPDATE_TPA_PARAM_FLAG);

	p_sge_tpa->tpa_ipv4_en_flg =
	    !!(p_sge_tpa_tlv->sge_tpa_flags & VFPF_TPA_IPV4_EN_FLAG);
	p_sge_tpa->tpa_ipv6_en_flg =
	    !!(p_sge_tpa_tlv->sge_tpa_flags & VFPF_TPA_IPV6_EN_FLAG);
	p_sge_tpa->tpa_pkt_split_flg =
	    !!(p_sge_tpa_tlv->sge_tpa_flags & VFPF_TPA_PKT_SPLIT_FLAG);
	p_sge_tpa->tpa_hdr_data_split_flg =
	    !!(p_sge_tpa_tlv->sge_tpa_flags & VFPF_TPA_HDR_DATA_SPLIT_FLAG);
	p_sge_tpa->tpa_gro_consistent_flg =
	    !!(p_sge_tpa_tlv->sge_tpa_flags & VFPF_TPA_GRO_CONSIST_FLAG);

	p_sge_tpa->tpa_max_aggs_num = p_sge_tpa_tlv->tpa_max_aggs_num;
	p_sge_tpa->tpa_max_size = p_sge_tpa_tlv->tpa_max_size;
	p_sge_tpa->tpa_min_size_to_start = p_sge_tpa_tlv->tpa_min_size_to_start;
	p_sge_tpa->tpa_min_size_to_cont = p_sge_tpa_tlv->tpa_min_size_to_cont;
	p_sge_tpa->max_buffers_per_cqe = p_sge_tpa_tlv->max_buffers_per_cqe;

	p_data->sge_tpa_params = p_sge_tpa;

	*tlvs_mask |= 1 << QED_IOV_VP_UPDATE_SGE_TPA;
}

static int qed_iov_pre_update_vport(struct qed_hwfn *hwfn,
				    u8 vfid,
				    struct qed_sp_vport_update_params *params,
				    u16 *tlvs)
{
	u8 mask = QED_ACCEPT_UCAST_UNMATCHED | QED_ACCEPT_MCAST_UNMATCHED;
	struct qed_filter_accept_flags *flags = &params->accept_flags;
	struct qed_public_vf_info *vf_info;
	u16 tlv_mask;

	tlv_mask = BIT(QED_IOV_VP_UPDATE_ACCEPT_PARAM) |
		   BIT(QED_IOV_VP_UPDATE_ACCEPT_ANY_VLAN);

	/* Untrusted VFs can't even be trusted to know that fact.
	 * Simply indicate everything is configured fine, and trace
	 * configuration 'behind their back'.
	 */
	if (!(*tlvs & tlv_mask))
		return 0;

	vf_info = qed_iov_get_public_vf_info(hwfn, vfid, true);

	if (flags->update_rx_mode_config) {
		vf_info->rx_accept_mode = flags->rx_accept_filter;
		if (!vf_info->is_trusted_configured)
			flags->rx_accept_filter &= ~mask;
	}

	if (flags->update_tx_mode_config) {
		vf_info->tx_accept_mode = flags->tx_accept_filter;
		if (!vf_info->is_trusted_configured)
			flags->tx_accept_filter &= ~mask;
	}

	if (params->update_accept_any_vlan_flg) {
		vf_info->accept_any_vlan = params->accept_any_vlan;

		if (vf_info->forced_vlan && !vf_info->is_trusted_configured)
			params->accept_any_vlan = false;
	}

	return 0;
}

static void qed_iov_vf_mbx_vport_update(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					struct qed_vf_info *vf)
{
	struct qed_rss_params *p_rss_params = NULL;
	struct qed_sp_vport_update_params params;
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct qed_sge_tpa_params sge_tpa_params;
	u16 tlvs_mask = 0, tlvs_accepted = 0;
	u8 status = PFVF_STATUS_SUCCESS;
	u16 length;
	int rc;

	/* Valiate PF can send such a request */
	if (!vf->vport_instance) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "No VPORT instance available for VF[%d], failing vport update\n",
			   vf->abs_vf_id);
		status = PFVF_STATUS_FAILURE;
		goto out;
	}
	p_rss_params = vzalloc(sizeof(*p_rss_params));
	if (p_rss_params == NULL) {
		status = PFVF_STATUS_FAILURE;
		goto out;
	}

	memset(&params, 0, sizeof(params));
	params.opaque_fid = vf->opaque_fid;
	params.vport_id = vf->vport_id;
	params.rss_params = NULL;

	/* Search for extended tlvs list and update values
	 * from VF in struct qed_sp_vport_update_params.
	 */
	qed_iov_vp_update_act_param(p_hwfn, &params, mbx, &tlvs_mask);
	qed_iov_vp_update_vlan_param(p_hwfn, &params, vf, mbx, &tlvs_mask);
	qed_iov_vp_update_tx_switch(p_hwfn, &params, mbx, &tlvs_mask);
	qed_iov_vp_update_mcast_bin_param(p_hwfn, &params, mbx, &tlvs_mask);
	qed_iov_vp_update_accept_flag(p_hwfn, &params, mbx, &tlvs_mask);
	qed_iov_vp_update_accept_any_vlan(p_hwfn, &params, mbx, &tlvs_mask);
	qed_iov_vp_update_sge_tpa_param(p_hwfn, vf, &params,
					&sge_tpa_params, mbx, &tlvs_mask);

	tlvs_accepted = tlvs_mask;

	/* Some of the extended TLVs need to be validated first; In that case,
	 * they can update the mask without updating the accepted [so that
	 * PF could communicate to VF it has rejected request].
	 */
	qed_iov_vp_update_rss_param(p_hwfn, vf, &params, p_rss_params,
				    mbx, &tlvs_mask, &tlvs_accepted);

	if (qed_iov_pre_update_vport(p_hwfn, vf->relative_vf_id,
				     &params, &tlvs_accepted)) {
		tlvs_accepted = 0;
		status = PFVF_STATUS_NOT_SUPPORTED;
		goto out;
	}

	if (!tlvs_accepted) {
		if (tlvs_mask)
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "Upper-layer prevents VF vport configuration\n");
		else
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "No feature tlvs found for vport update\n");
		status = PFVF_STATUS_NOT_SUPPORTED;
		goto out;
	}

	rc = qed_sp_vport_update(p_hwfn, &params, QED_SPQ_MODE_EBLOCK, NULL);

	if (rc)
		status = PFVF_STATUS_FAILURE;

out:
	vfree(p_rss_params);
	length = qed_iov_prep_vp_update_resp_tlvs(p_hwfn, vf, mbx, status,
						  tlvs_mask, tlvs_accepted);
	qed_iov_send_response(p_hwfn, p_ptt, vf, length, status);
}

static int qed_iov_vf_update_vlan_shadow(struct qed_hwfn *p_hwfn,
					 struct qed_vf_info *p_vf,
					 struct qed_filter_ucast *p_params)
{
	int i;

	/* First remove entries and then add new ones */
	if (p_params->opcode == QED_FILTER_REMOVE) {
		for (i = 0; i < QED_ETH_VF_NUM_VLAN_FILTERS + 1; i++)
			if (p_vf->shadow_config.vlans[i].used &&
			    p_vf->shadow_config.vlans[i].vid ==
			    p_params->vlan) {
				p_vf->shadow_config.vlans[i].used = false;
				break;
			}
		if (i == QED_ETH_VF_NUM_VLAN_FILTERS + 1) {
			DP_VERBOSE(p_hwfn,
				   QED_MSG_IOV,
				   "VF [%d] - Tries to remove a non-existing vlan\n",
				   p_vf->relative_vf_id);
			return -EINVAL;
		}
	} else if (p_params->opcode == QED_FILTER_REPLACE ||
		   p_params->opcode == QED_FILTER_FLUSH) {
		for (i = 0; i < QED_ETH_VF_NUM_VLAN_FILTERS + 1; i++)
			p_vf->shadow_config.vlans[i].used = false;
	}

	/* In forced mode, we're willing to remove entries - but we don't add
	 * new ones.
	 */
	if (p_vf->bulletin.p_virt->valid_bitmap & BIT(VLAN_ADDR_FORCED))
		return 0;

	if (p_params->opcode == QED_FILTER_ADD ||
	    p_params->opcode == QED_FILTER_REPLACE) {
		for (i = 0; i < QED_ETH_VF_NUM_VLAN_FILTERS + 1; i++) {
			if (p_vf->shadow_config.vlans[i].used)
				continue;

			p_vf->shadow_config.vlans[i].used = true;
			p_vf->shadow_config.vlans[i].vid = p_params->vlan;
			break;
		}

		if (i == QED_ETH_VF_NUM_VLAN_FILTERS + 1) {
			DP_VERBOSE(p_hwfn,
				   QED_MSG_IOV,
				   "VF [%d] - Tries to configure more than %d vlan filters\n",
				   p_vf->relative_vf_id,
				   QED_ETH_VF_NUM_VLAN_FILTERS + 1);
			return -EINVAL;
		}
	}

	return 0;
}

static int qed_iov_vf_update_mac_shadow(struct qed_hwfn *p_hwfn,
					struct qed_vf_info *p_vf,
					struct qed_filter_ucast *p_params)
{
	int i;

	/* If we're in forced-mode, we don't allow any change */
	if (p_vf->bulletin.p_virt->valid_bitmap & BIT(MAC_ADDR_FORCED))
		return 0;

	/* Don't keep track of shadow copy since we don't intend to restore. */
	if (p_vf->p_vf_info.is_trusted_configured)
		return 0;

	/* First remove entries and then add new ones */
	if (p_params->opcode == QED_FILTER_REMOVE) {
		for (i = 0; i < QED_ETH_VF_NUM_MAC_FILTERS; i++) {
			if (ether_addr_equal(p_vf->shadow_config.macs[i],
					     p_params->mac)) {
				eth_zero_addr(p_vf->shadow_config.macs[i]);
				break;
			}
		}

		if (i == QED_ETH_VF_NUM_MAC_FILTERS) {
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "MAC isn't configured\n");
			return -EINVAL;
		}
	} else if (p_params->opcode == QED_FILTER_REPLACE ||
		   p_params->opcode == QED_FILTER_FLUSH) {
		for (i = 0; i < QED_ETH_VF_NUM_MAC_FILTERS; i++)
			eth_zero_addr(p_vf->shadow_config.macs[i]);
	}

	/* List the new MAC address */
	if (p_params->opcode != QED_FILTER_ADD &&
	    p_params->opcode != QED_FILTER_REPLACE)
		return 0;

	for (i = 0; i < QED_ETH_VF_NUM_MAC_FILTERS; i++) {
		if (is_zero_ether_addr(p_vf->shadow_config.macs[i])) {
			ether_addr_copy(p_vf->shadow_config.macs[i],
					p_params->mac);
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "Added MAC at %d entry in shadow\n", i);
			break;
		}
	}

	if (i == QED_ETH_VF_NUM_MAC_FILTERS) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV, "No available place for MAC\n");
		return -EINVAL;
	}

	return 0;
}

static int
qed_iov_vf_update_unicast_shadow(struct qed_hwfn *p_hwfn,
				 struct qed_vf_info *p_vf,
				 struct qed_filter_ucast *p_params)
{
	int rc = 0;

	if (p_params->type == QED_FILTER_MAC) {
		rc = qed_iov_vf_update_mac_shadow(p_hwfn, p_vf, p_params);
		if (rc)
			return rc;
	}

	if (p_params->type == QED_FILTER_VLAN)
		rc = qed_iov_vf_update_vlan_shadow(p_hwfn, p_vf, p_params);

	return rc;
}

static int qed_iov_chk_ucast(struct qed_hwfn *hwfn,
			     int vfid, struct qed_filter_ucast *params)
{
	struct qed_public_vf_info *vf;

	vf = qed_iov_get_public_vf_info(hwfn, vfid, true);
	if (!vf)
		return -EINVAL;

	/* No real decision to make; Store the configured MAC */
	if (params->type == QED_FILTER_MAC ||
	    params->type == QED_FILTER_MAC_VLAN) {
		ether_addr_copy(vf->mac, params->mac);

		if (vf->is_trusted_configured) {
			qed_iov_bulletin_set_mac(hwfn, vf->mac, vfid);

			/* Update and post bulleitin again */
			qed_schedule_iov(hwfn, QED_IOV_WQ_BULLETIN_UPDATE_FLAG);
		}
	}

	return 0;
}

static void qed_iov_vf_mbx_ucast_filter(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					struct qed_vf_info *vf)
{
	struct qed_bulletin_content *p_bulletin = vf->bulletin.p_virt;
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct vfpf_ucast_filter_tlv *req;
	u8 status = PFVF_STATUS_SUCCESS;
	struct qed_filter_ucast params;
	int rc;

	/* Prepare the unicast filter params */
	memset(&params, 0, sizeof(struct qed_filter_ucast));
	req = &mbx->req_virt->ucast_filter;
	params.opcode = (enum qed_filter_opcode)req->opcode;
	params.type = (enum qed_filter_ucast_type)req->type;

	params.is_rx_filter = 1;
	params.is_tx_filter = 1;
	params.vport_to_remove_from = vf->vport_id;
	params.vport_to_add_to = vf->vport_id;
	memcpy(params.mac, req->mac, ETH_ALEN);
	params.vlan = req->vlan;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "VF[%d]: opcode 0x%02x type 0x%02x [%s %s] [vport 0x%02x] MAC %pM, vlan 0x%04x\n",
		   vf->abs_vf_id, params.opcode, params.type,
		   params.is_rx_filter ? "RX" : "",
		   params.is_tx_filter ? "TX" : "",
		   params.vport_to_add_to,
		   params.mac, params.vlan);

	if (!vf->vport_instance) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "No VPORT instance available for VF[%d], failing ucast MAC configuration\n",
			   vf->abs_vf_id);
		status = PFVF_STATUS_FAILURE;
		goto out;
	}

	/* Update shadow copy of the VF configuration */
	if (qed_iov_vf_update_unicast_shadow(p_hwfn, vf, &params)) {
		status = PFVF_STATUS_FAILURE;
		goto out;
	}

	/* Determine if the unicast filtering is acceptible by PF */
	if ((p_bulletin->valid_bitmap & BIT(VLAN_ADDR_FORCED)) &&
	    (params.type == QED_FILTER_VLAN ||
	     params.type == QED_FILTER_MAC_VLAN)) {
		/* Once VLAN is forced or PVID is set, do not allow
		 * to add/replace any further VLANs.
		 */
		if (params.opcode == QED_FILTER_ADD ||
		    params.opcode == QED_FILTER_REPLACE)
			status = PFVF_STATUS_FORCED;
		goto out;
	}

	if ((p_bulletin->valid_bitmap & BIT(MAC_ADDR_FORCED)) &&
	    (params.type == QED_FILTER_MAC ||
	     params.type == QED_FILTER_MAC_VLAN)) {
		if (!ether_addr_equal(p_bulletin->mac, params.mac) ||
		    (params.opcode != QED_FILTER_ADD &&
		     params.opcode != QED_FILTER_REPLACE))
			status = PFVF_STATUS_FORCED;
		goto out;
	}

	rc = qed_iov_chk_ucast(p_hwfn, vf->relative_vf_id, &params);
	if (rc) {
		status = PFVF_STATUS_FAILURE;
		goto out;
	}

	rc = qed_sp_eth_filter_ucast(p_hwfn, vf->opaque_fid, &params,
				     QED_SPQ_MODE_CB, NULL);
	if (rc)
		status = PFVF_STATUS_FAILURE;

out:
	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_UCAST_FILTER,
			     sizeof(struct pfvf_def_resp_tlv), status);
}

static void qed_iov_vf_mbx_int_cleanup(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       struct qed_vf_info *vf)
{
	int i;

	/* Reset the SBs */
	for (i = 0; i < vf->num_sbs; i++)
		qed_int_igu_init_pure_rt_single(p_hwfn, p_ptt,
						vf->igu_sbs[i],
						vf->opaque_fid, false);

	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_INT_CLEANUP,
			     sizeof(struct pfvf_def_resp_tlv),
			     PFVF_STATUS_SUCCESS);
}

static void qed_iov_vf_mbx_close(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt, struct qed_vf_info *vf)
{
	u16 length = sizeof(struct pfvf_def_resp_tlv);
	u8 status = PFVF_STATUS_SUCCESS;

	/* Disable Interrupts for VF */
	qed_iov_vf_igu_set_int(p_hwfn, p_ptt, vf, 0);

	/* Reset Permission table */
	qed_iov_config_perm_table(p_hwfn, p_ptt, vf, 0);

	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_CLOSE,
			     length, status);
}

static void qed_iov_vf_mbx_release(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct qed_vf_info *p_vf)
{
	u16 length = sizeof(struct pfvf_def_resp_tlv);
	u8 status = PFVF_STATUS_SUCCESS;
	int rc = 0;

	qed_iov_vf_cleanup(p_hwfn, p_vf);

	if (p_vf->state != VF_STOPPED && p_vf->state != VF_FREE) {
		/* Stopping the VF */
		rc = qed_sp_vf_stop(p_hwfn, p_vf->concrete_fid,
				    p_vf->opaque_fid);

		if (rc) {
			DP_ERR(p_hwfn, "qed_sp_vf_stop returned error %d\n",
			       rc);
			status = PFVF_STATUS_FAILURE;
		}

		p_vf->state = VF_STOPPED;
	}

	qed_iov_prepare_resp(p_hwfn, p_ptt, p_vf, CHANNEL_TLV_RELEASE,
			     length, status);
}

static void qed_iov_vf_pf_get_coalesce(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       struct qed_vf_info *p_vf)
{
	struct qed_iov_vf_mbx *mbx = &p_vf->vf_mbx;
	struct pfvf_read_coal_resp_tlv *p_resp;
	struct vfpf_read_coal_req_tlv *req;
	u8 status = PFVF_STATUS_FAILURE;
	struct qed_vf_queue *p_queue;
	struct qed_queue_cid *p_cid;
	u16 coal = 0, qid, i;
	bool b_is_rx;
	int rc = 0;

	mbx->offset = (u8 *)mbx->reply_virt;
	req = &mbx->req_virt->read_coal_req;

	qid = req->qid;
	b_is_rx = req->is_rx ? true : false;

	if (b_is_rx) {
		if (!qed_iov_validate_rxq(p_hwfn, p_vf, qid,
					  QED_IOV_VALIDATE_Q_ENABLE)) {
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "VF[%d]: Invalid Rx queue_id = %d\n",
				   p_vf->abs_vf_id, qid);
			goto send_resp;
		}

		p_cid = qed_iov_get_vf_rx_queue_cid(&p_vf->vf_queues[qid]);
		rc = qed_get_rxq_coalesce(p_hwfn, p_ptt, p_cid, &coal);
		if (rc)
			goto send_resp;
	} else {
		if (!qed_iov_validate_txq(p_hwfn, p_vf, qid,
					  QED_IOV_VALIDATE_Q_ENABLE)) {
			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "VF[%d]: Invalid Tx queue_id = %d\n",
				   p_vf->abs_vf_id, qid);
			goto send_resp;
		}
		for (i = 0; i < MAX_QUEUES_PER_QZONE; i++) {
			p_queue = &p_vf->vf_queues[qid];
			if ((!p_queue->cids[i].p_cid) ||
			    (!p_queue->cids[i].b_is_tx))
				continue;

			p_cid = p_queue->cids[i].p_cid;

			rc = qed_get_txq_coalesce(p_hwfn, p_ptt, p_cid, &coal);
			if (rc)
				goto send_resp;
			break;
		}
	}

	status = PFVF_STATUS_SUCCESS;

send_resp:
	p_resp = qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_COALESCE_READ,
			     sizeof(*p_resp));
	p_resp->coal = coal;

	qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_LIST_END,
		    sizeof(struct channel_list_end_tlv));

	qed_iov_send_response(p_hwfn, p_ptt, p_vf, sizeof(*p_resp), status);
}

static void qed_iov_vf_pf_set_coalesce(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       struct qed_vf_info *vf)
{
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct vfpf_update_coalesce *req;
	u8 status = PFVF_STATUS_FAILURE;
	struct qed_queue_cid *p_cid;
	u16 rx_coal, tx_coal;
	int rc = 0, i;
	u16 qid;

	req = &mbx->req_virt->update_coalesce;

	rx_coal = req->rx_coal;
	tx_coal = req->tx_coal;
	qid = req->qid;

	if (!qed_iov_validate_rxq(p_hwfn, vf, qid,
				  QED_IOV_VALIDATE_Q_ENABLE) && rx_coal) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF[%d]: Invalid Rx queue_id = %d\n",
			   vf->abs_vf_id, qid);
		goto out;
	}

	if (!qed_iov_validate_txq(p_hwfn, vf, qid,
				  QED_IOV_VALIDATE_Q_ENABLE) && tx_coal) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF[%d]: Invalid Tx queue_id = %d\n",
			   vf->abs_vf_id, qid);
		goto out;
	}

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "VF[%d]: Setting coalesce for VF rx_coal = %d, tx_coal = %d at queue = %d\n",
		   vf->abs_vf_id, rx_coal, tx_coal, qid);

	if (rx_coal) {
		p_cid = qed_iov_get_vf_rx_queue_cid(&vf->vf_queues[qid]);

		rc = qed_set_rxq_coalesce(p_hwfn, p_ptt, rx_coal, p_cid);
		if (rc) {
			DP_VERBOSE(p_hwfn,
				   QED_MSG_IOV,
				   "VF[%d]: Unable to set rx queue = %d coalesce\n",
				   vf->abs_vf_id, vf->vf_queues[qid].fw_rx_qid);
			goto out;
		}
		vf->rx_coal = rx_coal;
	}

	if (tx_coal) {
		struct qed_vf_queue *p_queue = &vf->vf_queues[qid];

		for (i = 0; i < MAX_QUEUES_PER_QZONE; i++) {
			if (!p_queue->cids[i].p_cid)
				continue;

			if (!p_queue->cids[i].b_is_tx)
				continue;

			rc = qed_set_txq_coalesce(p_hwfn, p_ptt, tx_coal,
						  p_queue->cids[i].p_cid);

			if (rc) {
				DP_VERBOSE(p_hwfn,
					   QED_MSG_IOV,
					   "VF[%d]: Unable to set tx queue coalesce\n",
					   vf->abs_vf_id);
				goto out;
			}
		}
		vf->tx_coal = tx_coal;
	}

	status = PFVF_STATUS_SUCCESS;
out:
	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_COALESCE_UPDATE,
			     sizeof(struct pfvf_def_resp_tlv), status);
}
static int
qed_iov_vf_flr_poll_dorq(struct qed_hwfn *p_hwfn,
			 struct qed_vf_info *p_vf, struct qed_ptt *p_ptt)
{
	int cnt;
	u32 val;

	qed_fid_pretend(p_hwfn, p_ptt, (u16) p_vf->concrete_fid);

	for (cnt = 0; cnt < 50; cnt++) {
		val = qed_rd(p_hwfn, p_ptt, DORQ_REG_VF_USAGE_CNT);
		if (!val)
			break;
		msleep(20);
	}
	qed_fid_pretend(p_hwfn, p_ptt, (u16) p_hwfn->hw_info.concrete_fid);

	if (cnt == 50) {
		DP_ERR(p_hwfn,
		       "VF[%d] - dorq failed to cleanup [usage 0x%08x]\n",
		       p_vf->abs_vf_id, val);
		return -EBUSY;
	}

	return 0;
}

static int
qed_iov_vf_flr_poll_pbf(struct qed_hwfn *p_hwfn,
			struct qed_vf_info *p_vf, struct qed_ptt *p_ptt)
{
	u32 cons[MAX_NUM_VOQS_E4], distance[MAX_NUM_VOQS_E4];
	int i, cnt;

	/* Read initial consumers & producers */
	for (i = 0; i < MAX_NUM_VOQS_E4; i++) {
		u32 prod;

		cons[i] = qed_rd(p_hwfn, p_ptt,
				 PBF_REG_NUM_BLOCKS_ALLOCATED_CONS_VOQ0 +
				 i * 0x40);
		prod = qed_rd(p_hwfn, p_ptt,
			      PBF_REG_NUM_BLOCKS_ALLOCATED_PROD_VOQ0 +
			      i * 0x40);
		distance[i] = prod - cons[i];
	}

	/* Wait for consumers to pass the producers */
	i = 0;
	for (cnt = 0; cnt < 50; cnt++) {
		for (; i < MAX_NUM_VOQS_E4; i++) {
			u32 tmp;

			tmp = qed_rd(p_hwfn, p_ptt,
				     PBF_REG_NUM_BLOCKS_ALLOCATED_CONS_VOQ0 +
				     i * 0x40);
			if (distance[i] > tmp - cons[i])
				break;
		}

		if (i == MAX_NUM_VOQS_E4)
			break;

		msleep(20);
	}

	if (cnt == 50) {
		DP_ERR(p_hwfn, "VF[%d] - pbf polling failed on VOQ %d\n",
		       p_vf->abs_vf_id, i);
		return -EBUSY;
	}

	return 0;
}

static int qed_iov_vf_flr_poll(struct qed_hwfn *p_hwfn,
			       struct qed_vf_info *p_vf, struct qed_ptt *p_ptt)
{
	int rc;

	rc = qed_iov_vf_flr_poll_dorq(p_hwfn, p_vf, p_ptt);
	if (rc)
		return rc;

	rc = qed_iov_vf_flr_poll_pbf(p_hwfn, p_vf, p_ptt);
	if (rc)
		return rc;

	return 0;
}

static int
qed_iov_execute_vf_flr_cleanup(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt,
			       u16 rel_vf_id, u32 *ack_vfs)
{
	struct qed_vf_info *p_vf;
	int rc = 0;

	p_vf = qed_iov_get_vf_info(p_hwfn, rel_vf_id, false);
	if (!p_vf)
		return 0;

	if (p_hwfn->pf_iov_info->pending_flr[rel_vf_id / 64] &
	    (1ULL << (rel_vf_id % 64))) {
		u16 vfid = p_vf->abs_vf_id;

		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF[%d] - Handling FLR\n", vfid);

		qed_iov_vf_cleanup(p_hwfn, p_vf);

		/* If VF isn't active, no need for anything but SW */
		if (!p_vf->b_init)
			goto cleanup;

		rc = qed_iov_vf_flr_poll(p_hwfn, p_vf, p_ptt);
		if (rc)
			goto cleanup;

		rc = qed_final_cleanup(p_hwfn, p_ptt, vfid, true);
		if (rc) {
			DP_ERR(p_hwfn, "Failed handle FLR of VF[%d]\n", vfid);
			return rc;
		}

		/* Workaround to make VF-PF channel ready, as FW
		 * doesn't do that as a part of FLR.
		 */
		REG_WR(p_hwfn,
		       GTT_BAR0_MAP_REG_USDM_RAM +
		       USTORM_VF_PF_CHANNEL_READY_OFFSET(vfid), 1);

		/* VF_STOPPED has to be set only after final cleanup
		 * but prior to re-enabling the VF.
		 */
		p_vf->state = VF_STOPPED;

		rc = qed_iov_enable_vf_access(p_hwfn, p_ptt, p_vf);
		if (rc) {
			DP_ERR(p_hwfn, "Failed to re-enable VF[%d] acces\n",
			       vfid);
			return rc;
		}
cleanup:
		/* Mark VF for ack and clean pending state */
		if (p_vf->state == VF_RESET)
			p_vf->state = VF_STOPPED;
		ack_vfs[vfid / 32] |= BIT((vfid % 32));
		p_hwfn->pf_iov_info->pending_flr[rel_vf_id / 64] &=
		    ~(1ULL << (rel_vf_id % 64));
		p_vf->vf_mbx.b_pending_msg = false;
	}

	return rc;
}

static int
qed_iov_vf_flr_cleanup(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 ack_vfs[VF_MAX_STATIC / 32];
	int rc = 0;
	u16 i;

	memset(ack_vfs, 0, sizeof(u32) * (VF_MAX_STATIC / 32));

	/* Since BRB <-> PRS interface can't be tested as part of the flr
	 * polling due to HW limitations, simply sleep a bit. And since
	 * there's no need to wait per-vf, do it before looping.
	 */
	msleep(100);

	for (i = 0; i < p_hwfn->cdev->p_iov_info->total_vfs; i++)
		qed_iov_execute_vf_flr_cleanup(p_hwfn, p_ptt, i, ack_vfs);

	rc = qed_mcp_ack_vf_flr(p_hwfn, p_ptt, ack_vfs);
	return rc;
}

bool qed_iov_mark_vf_flr(struct qed_hwfn *p_hwfn, u32 *p_disabled_vfs)
{
	bool found = false;
	u16 i;

	DP_VERBOSE(p_hwfn, QED_MSG_IOV, "Marking FLR-ed VFs\n");
	for (i = 0; i < (VF_MAX_STATIC / 32); i++)
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "[%08x,...,%08x]: %08x\n",
			   i * 32, (i + 1) * 32 - 1, p_disabled_vfs[i]);

	if (!p_hwfn->cdev->p_iov_info) {
		DP_NOTICE(p_hwfn, "VF flr but no IOV\n");
		return false;
	}

	/* Mark VFs */
	for (i = 0; i < p_hwfn->cdev->p_iov_info->total_vfs; i++) {
		struct qed_vf_info *p_vf;
		u8 vfid;

		p_vf = qed_iov_get_vf_info(p_hwfn, i, false);
		if (!p_vf)
			continue;

		vfid = p_vf->abs_vf_id;
		if (BIT((vfid % 32)) & p_disabled_vfs[vfid / 32]) {
			u64 *p_flr = p_hwfn->pf_iov_info->pending_flr;
			u16 rel_vf_id = p_vf->relative_vf_id;

			DP_VERBOSE(p_hwfn, QED_MSG_IOV,
				   "VF[%d] [rel %d] got FLR-ed\n",
				   vfid, rel_vf_id);

			p_vf->state = VF_RESET;

			/* No need to lock here, since pending_flr should
			 * only change here and before ACKing MFw. Since
			 * MFW will not trigger an additional attention for
			 * VF flr until ACKs, we're safe.
			 */
			p_flr[rel_vf_id / 64] |= 1ULL << (rel_vf_id % 64);
			found = true;
		}
	}

	return found;
}

static int qed_iov_get_link(struct qed_hwfn *p_hwfn,
			    u16 vfid,
			    struct qed_mcp_link_params *p_params,
			    struct qed_mcp_link_state *p_link,
			    struct qed_mcp_link_capabilities *p_caps)
{
	struct qed_vf_info *p_vf = qed_iov_get_vf_info(p_hwfn,
						       vfid,
						       false);
	struct qed_bulletin_content *p_bulletin;

	if (!p_vf)
		return -EINVAL;

	p_bulletin = p_vf->bulletin.p_virt;

	if (p_params)
		__qed_vf_get_link_params(p_hwfn, p_params, p_bulletin);
	if (p_link)
		__qed_vf_get_link_state(p_hwfn, p_link, p_bulletin);
	if (p_caps)
		__qed_vf_get_link_caps(p_hwfn, p_caps, p_bulletin);
	return 0;
}

static int
qed_iov_vf_pf_bulletin_update_mac(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_vf_info *p_vf)
{
	struct qed_bulletin_content *p_bulletin = p_vf->bulletin.p_virt;
	struct qed_iov_vf_mbx *mbx = &p_vf->vf_mbx;
	struct vfpf_bulletin_update_mac_tlv *p_req;
	u8 status = PFVF_STATUS_SUCCESS;
	int rc = 0;

	if (!p_vf->p_vf_info.is_trusted_configured) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "Blocking bulletin update request from untrusted VF[%d]\n",
			   p_vf->abs_vf_id);
		status = PFVF_STATUS_NOT_SUPPORTED;
		rc = -EINVAL;
		goto send_status;
	}

	p_req = &mbx->req_virt->bulletin_update_mac;
	ether_addr_copy(p_bulletin->mac, p_req->mac);
	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "Updated bulletin of VF[%d] with requested MAC[%pM]\n",
		   p_vf->abs_vf_id, p_req->mac);

send_status:
	qed_iov_prepare_resp(p_hwfn, p_ptt, p_vf,
			     CHANNEL_TLV_BULLETIN_UPDATE_MAC,
			     sizeof(struct pfvf_def_resp_tlv), status);
	return rc;
}

static void qed_iov_process_mbx_req(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt, int vfid)
{
	struct qed_iov_vf_mbx *mbx;
	struct qed_vf_info *p_vf;

	p_vf = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!p_vf)
		return;

	mbx = &p_vf->vf_mbx;

	/* qed_iov_process_mbx_request */
	if (!mbx->b_pending_msg) {
		DP_NOTICE(p_hwfn,
			  "VF[%02x]: Trying to process mailbox message when none is pending\n",
			  p_vf->abs_vf_id);
		return;
	}
	mbx->b_pending_msg = false;

	mbx->first_tlv = mbx->req_virt->first_tlv;

	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "VF[%02x]: Processing mailbox message [type %04x]\n",
		   p_vf->abs_vf_id, mbx->first_tlv.tl.type);

	/* check if tlv type is known */
	if (qed_iov_tlv_supported(mbx->first_tlv.tl.type) &&
	    !p_vf->b_malicious) {
		switch (mbx->first_tlv.tl.type) {
		case CHANNEL_TLV_ACQUIRE:
			qed_iov_vf_mbx_acquire(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_VPORT_START:
			qed_iov_vf_mbx_start_vport(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_VPORT_TEARDOWN:
			qed_iov_vf_mbx_stop_vport(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_START_RXQ:
			qed_iov_vf_mbx_start_rxq(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_START_TXQ:
			qed_iov_vf_mbx_start_txq(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_STOP_RXQS:
			qed_iov_vf_mbx_stop_rxqs(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_STOP_TXQS:
			qed_iov_vf_mbx_stop_txqs(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_UPDATE_RXQ:
			qed_iov_vf_mbx_update_rxqs(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_VPORT_UPDATE:
			qed_iov_vf_mbx_vport_update(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_UCAST_FILTER:
			qed_iov_vf_mbx_ucast_filter(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_CLOSE:
			qed_iov_vf_mbx_close(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_INT_CLEANUP:
			qed_iov_vf_mbx_int_cleanup(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_RELEASE:
			qed_iov_vf_mbx_release(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_UPDATE_TUNN_PARAM:
			qed_iov_vf_mbx_update_tunn_param(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_COALESCE_UPDATE:
			qed_iov_vf_pf_set_coalesce(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_COALESCE_READ:
			qed_iov_vf_pf_get_coalesce(p_hwfn, p_ptt, p_vf);
			break;
		case CHANNEL_TLV_BULLETIN_UPDATE_MAC:
			qed_iov_vf_pf_bulletin_update_mac(p_hwfn, p_ptt, p_vf);
			break;
		}
	} else if (qed_iov_tlv_supported(mbx->first_tlv.tl.type)) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF [%02x] - considered malicious; Ignoring TLV [%04x]\n",
			   p_vf->abs_vf_id, mbx->first_tlv.tl.type);

		qed_iov_prepare_resp(p_hwfn, p_ptt, p_vf,
				     mbx->first_tlv.tl.type,
				     sizeof(struct pfvf_def_resp_tlv),
				     PFVF_STATUS_MALICIOUS);
	} else {
		/* unknown TLV - this may belong to a VF driver from the future
		 * - a version written after this PF driver was written, which
		 * supports features unknown as of yet. Too bad since we don't
		 * support them. Or this may be because someone wrote a crappy
		 * VF driver and is sending garbage over the channel.
		 */
		DP_NOTICE(p_hwfn,
			  "VF[%02x]: unknown TLV. type %04x length %04x padding %08x reply address %llu\n",
			  p_vf->abs_vf_id,
			  mbx->first_tlv.tl.type,
			  mbx->first_tlv.tl.length,
			  mbx->first_tlv.padding, mbx->first_tlv.reply_address);

		/* Try replying in case reply address matches the acquisition's
		 * posted address.
		 */
		if (p_vf->acquire.first_tlv.reply_address &&
		    (mbx->first_tlv.reply_address ==
		     p_vf->acquire.first_tlv.reply_address)) {
			qed_iov_prepare_resp(p_hwfn, p_ptt, p_vf,
					     mbx->first_tlv.tl.type,
					     sizeof(struct pfvf_def_resp_tlv),
					     PFVF_STATUS_NOT_SUPPORTED);
		} else {
			DP_VERBOSE(p_hwfn,
				   QED_MSG_IOV,
				   "VF[%02x]: Can't respond to TLV - no valid reply address\n",
				   p_vf->abs_vf_id);
		}
	}
}

static void qed_iov_pf_get_pending_events(struct qed_hwfn *p_hwfn, u64 *events)
{
	int i;

	memset(events, 0, sizeof(u64) * QED_VF_ARRAY_LENGTH);

	qed_for_each_vf(p_hwfn, i) {
		struct qed_vf_info *p_vf;

		p_vf = &p_hwfn->pf_iov_info->vfs_array[i];
		if (p_vf->vf_mbx.b_pending_msg)
			events[i / 64] |= 1ULL << (i % 64);
	}
}

static struct qed_vf_info *qed_sriov_get_vf_from_absid(struct qed_hwfn *p_hwfn,
						       u16 abs_vfid)
{
	u8 min = (u8) p_hwfn->cdev->p_iov_info->first_vf_in_pf;

	if (!_qed_iov_pf_sanity_check(p_hwfn, (int)abs_vfid - min, false)) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "Got indication for VF [abs 0x%08x] that cannot be handled by PF\n",
			   abs_vfid);
		return NULL;
	}

	return &p_hwfn->pf_iov_info->vfs_array[(u8) abs_vfid - min];
}

static int qed_sriov_vfpf_msg(struct qed_hwfn *p_hwfn,
			      u16 abs_vfid, struct regpair *vf_msg)
{
	struct qed_vf_info *p_vf = qed_sriov_get_vf_from_absid(p_hwfn,
			   abs_vfid);

	if (!p_vf)
		return 0;

	/* List the physical address of the request so that handler
	 * could later on copy the message from it.
	 */
	p_vf->vf_mbx.pending_req = HILO_64(vf_msg->hi, vf_msg->lo);

	/* Mark the event and schedule the workqueue */
	p_vf->vf_mbx.b_pending_msg = true;
	qed_schedule_iov(p_hwfn, QED_IOV_WQ_MSG_FLAG);

	return 0;
}

static void qed_sriov_vfpf_malicious(struct qed_hwfn *p_hwfn,
				     struct malicious_vf_eqe_data *p_data)
{
	struct qed_vf_info *p_vf;

	p_vf = qed_sriov_get_vf_from_absid(p_hwfn, p_data->vf_id);

	if (!p_vf)
		return;

	if (!p_vf->b_malicious) {
		DP_NOTICE(p_hwfn,
			  "VF [%d] - Malicious behavior [%02x]\n",
			  p_vf->abs_vf_id, p_data->err_id);

		p_vf->b_malicious = true;
	} else {
		DP_INFO(p_hwfn,
			"VF [%d] - Malicious behavior [%02x]\n",
			p_vf->abs_vf_id, p_data->err_id);
	}
}

static int qed_sriov_eqe_event(struct qed_hwfn *p_hwfn, u8 opcode, __le16 echo,
			       union event_ring_data *data, u8 fw_return_code)
{
	switch (opcode) {
	case COMMON_EVENT_VF_PF_CHANNEL:
		return qed_sriov_vfpf_msg(p_hwfn, le16_to_cpu(echo),
					  &data->vf_pf_channel.msg_addr);
	case COMMON_EVENT_MALICIOUS_VF:
		qed_sriov_vfpf_malicious(p_hwfn, &data->malicious_vf);
		return 0;
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
		if (qed_iov_is_valid_vfid(p_hwfn, rel_vf_id, true, false))
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

	memset(&params, 0, sizeof(params));
	SET_FIELD(params.flags, QED_DMAE_PARAMS_SRC_VF_VALID, 0x1);
	SET_FIELD(params.flags, QED_DMAE_PARAMS_COMPLETION_DST, 0x1);
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

static void qed_iov_bulletin_set_forced_mac(struct qed_hwfn *p_hwfn,
					    u8 *mac, int vfid)
{
	struct qed_vf_info *vf_info;
	u64 feature;

	vf_info = qed_iov_get_vf_info(p_hwfn, (u16)vfid, true);
	if (!vf_info) {
		DP_NOTICE(p_hwfn->cdev,
			  "Can not set forced MAC, invalid vfid [%d]\n", vfid);
		return;
	}

	if (vf_info->b_malicious) {
		DP_NOTICE(p_hwfn->cdev,
			  "Can't set forced MAC to malicious VF [%d]\n", vfid);
		return;
	}

	if (vf_info->p_vf_info.is_trusted_configured) {
		feature = BIT(VFPF_BULLETIN_MAC_ADDR);
		/* Trust mode will disable Forced MAC */
		vf_info->bulletin.p_virt->valid_bitmap &=
			~BIT(MAC_ADDR_FORCED);
	} else {
		feature = BIT(MAC_ADDR_FORCED);
		/* Forced MAC will disable MAC_ADDR */
		vf_info->bulletin.p_virt->valid_bitmap &=
			~BIT(VFPF_BULLETIN_MAC_ADDR);
	}

	memcpy(vf_info->bulletin.p_virt->mac, mac, ETH_ALEN);

	vf_info->bulletin.p_virt->valid_bitmap |= feature;

	qed_iov_configure_vport_forced(p_hwfn, vf_info, feature);
}

static int qed_iov_bulletin_set_mac(struct qed_hwfn *p_hwfn, u8 *mac, int vfid)
{
	struct qed_vf_info *vf_info;
	u64 feature;

	vf_info = qed_iov_get_vf_info(p_hwfn, (u16)vfid, true);
	if (!vf_info) {
		DP_NOTICE(p_hwfn->cdev, "Can not set MAC, invalid vfid [%d]\n",
			  vfid);
		return -EINVAL;
	}

	if (vf_info->b_malicious) {
		DP_NOTICE(p_hwfn->cdev, "Can't set MAC to malicious VF [%d]\n",
			  vfid);
		return -EINVAL;
	}

	if (vf_info->bulletin.p_virt->valid_bitmap & BIT(MAC_ADDR_FORCED)) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Can not set MAC, Forced MAC is configured\n");
		return -EINVAL;
	}

	feature = BIT(VFPF_BULLETIN_MAC_ADDR);
	ether_addr_copy(vf_info->bulletin.p_virt->mac, mac);

	vf_info->bulletin.p_virt->valid_bitmap |= feature;

	if (vf_info->p_vf_info.is_trusted_configured)
		qed_iov_configure_vport_forced(p_hwfn, vf_info, feature);

	return 0;
}

static void qed_iov_bulletin_set_forced_vlan(struct qed_hwfn *p_hwfn,
					     u16 pvid, int vfid)
{
	struct qed_vf_info *vf_info;
	u64 feature;

	vf_info = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!vf_info) {
		DP_NOTICE(p_hwfn->cdev,
			  "Can not set forced MAC, invalid vfid [%d]\n", vfid);
		return;
	}

	if (vf_info->b_malicious) {
		DP_NOTICE(p_hwfn->cdev,
			  "Can't set forced vlan to malicious VF [%d]\n", vfid);
		return;
	}

	feature = 1 << VLAN_ADDR_FORCED;
	vf_info->bulletin.p_virt->pvid = pvid;
	if (pvid)
		vf_info->bulletin.p_virt->valid_bitmap |= feature;
	else
		vf_info->bulletin.p_virt->valid_bitmap &= ~feature;

	qed_iov_configure_vport_forced(p_hwfn, vf_info, feature);
}

void qed_iov_bulletin_set_udp_ports(struct qed_hwfn *p_hwfn,
				    int vfid, u16 vxlan_port, u16 geneve_port)
{
	struct qed_vf_info *vf_info;

	vf_info = qed_iov_get_vf_info(p_hwfn, (u16)vfid, true);
	if (!vf_info) {
		DP_NOTICE(p_hwfn->cdev,
			  "Can not set udp ports, invalid vfid [%d]\n", vfid);
		return;
	}

	if (vf_info->b_malicious) {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Can not set udp ports to malicious VF [%d]\n",
			   vfid);
		return;
	}

	vf_info->bulletin.p_virt->vxlan_udp_port = vxlan_port;
	vf_info->bulletin.p_virt->geneve_udp_port = geneve_port;
}

static bool qed_iov_vf_has_vport_instance(struct qed_hwfn *p_hwfn, int vfid)
{
	struct qed_vf_info *p_vf_info;

	p_vf_info = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!p_vf_info)
		return false;

	return !!p_vf_info->vport_instance;
}

static bool qed_iov_is_vf_stopped(struct qed_hwfn *p_hwfn, int vfid)
{
	struct qed_vf_info *p_vf_info;

	p_vf_info = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!p_vf_info)
		return true;

	return p_vf_info->state == VF_STOPPED;
}

static bool qed_iov_spoofchk_get(struct qed_hwfn *p_hwfn, int vfid)
{
	struct qed_vf_info *vf_info;

	vf_info = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!vf_info)
		return false;

	return vf_info->spoof_chk;
}

static int qed_iov_spoofchk_set(struct qed_hwfn *p_hwfn, int vfid, bool val)
{
	struct qed_vf_info *vf;
	int rc = -EINVAL;

	if (!qed_iov_pf_sanity_check(p_hwfn, vfid)) {
		DP_NOTICE(p_hwfn,
			  "SR-IOV sanity check failed, can't set spoofchk\n");
		goto out;
	}

	vf = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!vf)
		goto out;

	if (!qed_iov_vf_has_vport_instance(p_hwfn, vfid)) {
		/* After VF VPORT start PF will configure spoof check */
		vf->req_spoofchk_val = val;
		rc = 0;
		goto out;
	}

	rc = __qed_iov_spoofchk_set(p_hwfn, vf, val);

out:
	return rc;
}

static u8 *qed_iov_bulletin_get_mac(struct qed_hwfn *p_hwfn, u16 rel_vf_id)
{
	struct qed_vf_info *p_vf;

	p_vf = qed_iov_get_vf_info(p_hwfn, rel_vf_id, true);
	if (!p_vf || !p_vf->bulletin.p_virt)
		return NULL;

	if (!(p_vf->bulletin.p_virt->valid_bitmap &
	      BIT(VFPF_BULLETIN_MAC_ADDR)))
		return NULL;

	return p_vf->bulletin.p_virt->mac;
}

static u8 *qed_iov_bulletin_get_forced_mac(struct qed_hwfn *p_hwfn,
					   u16 rel_vf_id)
{
	struct qed_vf_info *p_vf;

	p_vf = qed_iov_get_vf_info(p_hwfn, rel_vf_id, true);
	if (!p_vf || !p_vf->bulletin.p_virt)
		return NULL;

	if (!(p_vf->bulletin.p_virt->valid_bitmap & BIT(MAC_ADDR_FORCED)))
		return NULL;

	return p_vf->bulletin.p_virt->mac;
}

static u16
qed_iov_bulletin_get_forced_vlan(struct qed_hwfn *p_hwfn, u16 rel_vf_id)
{
	struct qed_vf_info *p_vf;

	p_vf = qed_iov_get_vf_info(p_hwfn, rel_vf_id, true);
	if (!p_vf || !p_vf->bulletin.p_virt)
		return 0;

	if (!(p_vf->bulletin.p_virt->valid_bitmap & BIT(VLAN_ADDR_FORCED)))
		return 0;

	return p_vf->bulletin.p_virt->pvid;
}

static int qed_iov_configure_tx_rate(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt, int vfid, int val)
{
	struct qed_vf_info *vf;
	u8 abs_vp_id = 0;
	u16 rl_id;
	int rc;

	vf = qed_iov_get_vf_info(p_hwfn, (u16)vfid, true);
	if (!vf)
		return -EINVAL;

	rc = qed_fw_vport(p_hwfn, vf->vport_id, &abs_vp_id);
	if (rc)
		return rc;

	rl_id = abs_vp_id;	/* The "rl_id" is set as the "vport_id" */
	return qed_init_global_rl(p_hwfn, p_ptt, rl_id, (u32)val);
}

static int
qed_iov_configure_min_tx_rate(struct qed_dev *cdev, int vfid, u32 rate)
{
	struct qed_vf_info *vf;
	u8 vport_id;
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (!qed_iov_pf_sanity_check(p_hwfn, vfid)) {
			DP_NOTICE(p_hwfn,
				  "SR-IOV sanity check failed, can't set min rate\n");
			return -EINVAL;
		}
	}

	vf = qed_iov_get_vf_info(QED_LEADING_HWFN(cdev), (u16)vfid, true);
	vport_id = vf->vport_id;

	return qed_configure_vport_wfq(cdev, vport_id, rate);
}

static int qed_iov_get_vf_min_rate(struct qed_hwfn *p_hwfn, int vfid)
{
	struct qed_wfq_data *vf_vp_wfq;
	struct qed_vf_info *vf_info;

	vf_info = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!vf_info)
		return 0;

	vf_vp_wfq = &p_hwfn->qm_info.wfq_data[vf_info->vport_id];

	if (vf_vp_wfq->configured)
		return vf_vp_wfq->min_speed;
	else
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

void qed_vf_start_iov_wq(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i)
	    queue_delayed_work(cdev->hwfns[i].iov_wq,
			       &cdev->hwfns[i].iov_task, 0);
}

int qed_sriov_disable(struct qed_dev *cdev, bool pci_enabled)
{
	int i, j;

	for_each_hwfn(cdev, i)
	    if (cdev->hwfns[i].iov_wq)
		flush_workqueue(cdev->hwfns[i].iov_wq);

	/* Mark VFs for disablement */
	qed_iov_set_vfs_to_disable(cdev, true);

	if (cdev->p_iov_info && cdev->p_iov_info->num_vfs && pci_enabled)
		pci_disable_sriov(cdev->pdev);

	if (cdev->recov_in_prog) {
		DP_VERBOSE(cdev,
			   QED_MSG_IOV,
			   "Skip SRIOV disable operations in the device since a recovery is in progress\n");
		goto out;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *hwfn = &cdev->hwfns[i];
		struct qed_ptt *ptt = qed_ptt_acquire(hwfn);

		/* Failure to acquire the ptt in 100g creates an odd error
		 * where the first engine has already relased IOV.
		 */
		if (!ptt) {
			DP_ERR(hwfn, "Failed to acquire ptt\n");
			return -EBUSY;
		}

		/* Clean WFQ db and configure equal weight for all vports */
		qed_clean_wfq_db(hwfn, ptt);

		qed_for_each_vf(hwfn, j) {
			int k;

			if (!qed_iov_is_valid_vfid(hwfn, j, true, false))
				continue;

			/* Wait until VF is disabled before releasing */
			for (k = 0; k < 100; k++) {
				if (!qed_iov_is_vf_stopped(hwfn, j))
					msleep(20);
				else
					break;
			}

			if (k < 100)
				qed_iov_release_hw_for_vf(&cdev->hwfns[i],
							  ptt, j);
			else
				DP_ERR(hwfn,
				       "Timeout waiting for VF's FLR to end\n");
		}

		qed_ptt_release(hwfn, ptt);
	}
out:
	qed_iov_set_vfs_to_disable(cdev, false);

	return 0;
}

static void qed_sriov_enable_qid_config(struct qed_hwfn *hwfn,
					u16 vfid,
					struct qed_iov_vf_init_params *params)
{
	u16 base, i;

	/* Since we have an equal resource distribution per-VF, and we assume
	 * PF has acquired the QED_PF_L2_QUE first queues, we start setting
	 * sequentially from there.
	 */
	base = FEAT_NUM(hwfn, QED_PF_L2_QUE) + vfid * params->num_queues;

	params->rel_vf_id = vfid;
	for (i = 0; i < params->num_queues; i++) {
		params->req_rx_queue[i] = base + i;
		params->req_tx_queue[i] = base + i;
	}
}

static int qed_sriov_enable(struct qed_dev *cdev, int num)
{
	struct qed_iov_vf_init_params params;
	struct qed_hwfn *hwfn;
	struct qed_ptt *ptt;
	int i, j, rc;

	if (num >= RESC_NUM(&cdev->hwfns[0], QED_VPORT)) {
		DP_NOTICE(cdev, "Can start at most %d VFs\n",
			  RESC_NUM(&cdev->hwfns[0], QED_VPORT) - 1);
		return -EINVAL;
	}

	memset(&params, 0, sizeof(params));

	/* Initialize HW for VF access */
	for_each_hwfn(cdev, j) {
		hwfn = &cdev->hwfns[j];
		ptt = qed_ptt_acquire(hwfn);

		/* Make sure not to use more than 16 queues per VF */
		params.num_queues = min_t(int,
					  FEAT_NUM(hwfn, QED_VF_L2_QUE) / num,
					  16);

		if (!ptt) {
			DP_ERR(hwfn, "Failed to acquire ptt\n");
			rc = -EBUSY;
			goto err;
		}

		for (i = 0; i < num; i++) {
			if (!qed_iov_is_valid_vfid(hwfn, i, false, true))
				continue;

			qed_sriov_enable_qid_config(hwfn, i, &params);
			rc = qed_iov_init_hw_for_vf(hwfn, ptt, &params);
			if (rc) {
				DP_ERR(cdev, "Failed to enable VF[%d]\n", i);
				qed_ptt_release(hwfn, ptt);
				goto err;
			}
		}

		qed_ptt_release(hwfn, ptt);
	}

	/* Enable SRIOV PCIe functions */
	rc = pci_enable_sriov(cdev->pdev, num);
	if (rc) {
		DP_ERR(cdev, "Failed to enable sriov [%d]\n", rc);
		goto err;
	}

	hwfn = QED_LEADING_HWFN(cdev);
	ptt = qed_ptt_acquire(hwfn);
	if (!ptt) {
		DP_ERR(hwfn, "Failed to acquire ptt\n");
		rc = -EBUSY;
		goto err;
	}

	rc = qed_mcp_ov_update_eswitch(hwfn, ptt, QED_OV_ESWITCH_VEB);
	if (rc)
		DP_INFO(cdev, "Failed to update eswitch mode\n");
	qed_ptt_release(hwfn, ptt);

	return num;

err:
	qed_sriov_disable(cdev, false);
	return rc;
}

static int qed_sriov_configure(struct qed_dev *cdev, int num_vfs_param)
{
	if (!IS_QED_SRIOV(cdev)) {
		DP_VERBOSE(cdev, QED_MSG_IOV, "SR-IOV is not supported\n");
		return -EOPNOTSUPP;
	}

	if (num_vfs_param)
		return qed_sriov_enable(cdev, num_vfs_param);
	else
		return qed_sriov_disable(cdev, true);
}

static int qed_sriov_pf_set_mac(struct qed_dev *cdev, u8 *mac, int vfid)
{
	int i;

	if (!IS_QED_SRIOV(cdev) || !IS_PF_SRIOV_ALLOC(&cdev->hwfns[0])) {
		DP_VERBOSE(cdev, QED_MSG_IOV,
			   "Cannot set a VF MAC; Sriov is not enabled\n");
		return -EINVAL;
	}

	if (!qed_iov_is_valid_vfid(&cdev->hwfns[0], vfid, true, true)) {
		DP_VERBOSE(cdev, QED_MSG_IOV,
			   "Cannot set VF[%d] MAC (VF is not active)\n", vfid);
		return -EINVAL;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *hwfn = &cdev->hwfns[i];
		struct qed_public_vf_info *vf_info;

		vf_info = qed_iov_get_public_vf_info(hwfn, vfid, true);
		if (!vf_info)
			continue;

		/* Set the MAC, and schedule the IOV task */
		if (vf_info->is_trusted_configured)
			ether_addr_copy(vf_info->mac, mac);
		else
			ether_addr_copy(vf_info->forced_mac, mac);

		qed_schedule_iov(hwfn, QED_IOV_WQ_SET_UNICAST_FILTER_FLAG);
	}

	return 0;
}

static int qed_sriov_pf_set_vlan(struct qed_dev *cdev, u16 vid, int vfid)
{
	int i;

	if (!IS_QED_SRIOV(cdev) || !IS_PF_SRIOV_ALLOC(&cdev->hwfns[0])) {
		DP_VERBOSE(cdev, QED_MSG_IOV,
			   "Cannot set a VF MAC; Sriov is not enabled\n");
		return -EINVAL;
	}

	if (!qed_iov_is_valid_vfid(&cdev->hwfns[0], vfid, true, true)) {
		DP_VERBOSE(cdev, QED_MSG_IOV,
			   "Cannot set VF[%d] MAC (VF is not active)\n", vfid);
		return -EINVAL;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *hwfn = &cdev->hwfns[i];
		struct qed_public_vf_info *vf_info;

		vf_info = qed_iov_get_public_vf_info(hwfn, vfid, true);
		if (!vf_info)
			continue;

		/* Set the forced vlan, and schedule the IOV task */
		vf_info->forced_vlan = vid;
		qed_schedule_iov(hwfn, QED_IOV_WQ_SET_UNICAST_FILTER_FLAG);
	}

	return 0;
}

static int qed_get_vf_config(struct qed_dev *cdev,
			     int vf_id, struct ifla_vf_info *ivi)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_public_vf_info *vf_info;
	struct qed_mcp_link_state link;
	u32 tx_rate;
	int ret;

	/* Sanitize request */
	if (IS_VF(cdev))
		return -EINVAL;

	if (!qed_iov_is_valid_vfid(&cdev->hwfns[0], vf_id, true, false)) {
		DP_VERBOSE(cdev, QED_MSG_IOV,
			   "VF index [%d] isn't active\n", vf_id);
		return -EINVAL;
	}

	vf_info = qed_iov_get_public_vf_info(hwfn, vf_id, true);

	ret = qed_iov_get_link(hwfn, vf_id, NULL, &link, NULL);
	if (ret)
		return ret;

	/* Fill information about VF */
	ivi->vf = vf_id;

	if (is_valid_ether_addr(vf_info->forced_mac))
		ether_addr_copy(ivi->mac, vf_info->forced_mac);
	else
		ether_addr_copy(ivi->mac, vf_info->mac);

	ivi->vlan = vf_info->forced_vlan;
	ivi->spoofchk = qed_iov_spoofchk_get(hwfn, vf_id);
	ivi->linkstate = vf_info->link_state;
	tx_rate = vf_info->tx_rate;
	ivi->max_tx_rate = tx_rate ? tx_rate : link.speed;
	ivi->min_tx_rate = qed_iov_get_vf_min_rate(hwfn, vf_id);
	ivi->trusted = vf_info->is_trusted_request;

	return 0;
}

void qed_inform_vf_link_state(struct qed_hwfn *hwfn)
{
	struct qed_hwfn *lead_hwfn = QED_LEADING_HWFN(hwfn->cdev);
	struct qed_mcp_link_capabilities caps;
	struct qed_mcp_link_params params;
	struct qed_mcp_link_state link;
	int i;

	if (!hwfn->pf_iov_info)
		return;

	/* Update bulletin of all future possible VFs with link configuration */
	for (i = 0; i < hwfn->cdev->p_iov_info->total_vfs; i++) {
		struct qed_public_vf_info *vf_info;

		vf_info = qed_iov_get_public_vf_info(hwfn, i, false);
		if (!vf_info)
			continue;

		/* Only hwfn0 is actually interested in the link speed.
		 * But since only it would receive an MFW indication of link,
		 * need to take configuration from it - otherwise things like
		 * rate limiting for hwfn1 VF would not work.
		 */
		memcpy(&params, qed_mcp_get_link_params(lead_hwfn),
		       sizeof(params));
		memcpy(&link, qed_mcp_get_link_state(lead_hwfn), sizeof(link));
		memcpy(&caps, qed_mcp_get_link_capabilities(lead_hwfn),
		       sizeof(caps));

		/* Modify link according to the VF's configured link state */
		switch (vf_info->link_state) {
		case IFLA_VF_LINK_STATE_DISABLE:
			link.link_up = false;
			break;
		case IFLA_VF_LINK_STATE_ENABLE:
			link.link_up = true;
			/* Set speed according to maximum supported by HW.
			 * that is 40G for regular devices and 100G for CMT
			 * mode devices.
			 */
			link.speed = (hwfn->cdev->num_hwfns > 1) ?
				     100000 : 40000;
			break;
		default:
			/* In auto mode pass PF link image to VF */
			break;
		}

		if (link.link_up && vf_info->tx_rate) {
			struct qed_ptt *ptt;
			int rate;

			rate = min_t(int, vf_info->tx_rate, link.speed);

			ptt = qed_ptt_acquire(hwfn);
			if (!ptt) {
				DP_NOTICE(hwfn, "Failed to acquire PTT\n");
				return;
			}

			if (!qed_iov_configure_tx_rate(hwfn, ptt, i, rate)) {
				vf_info->tx_rate = rate;
				link.speed = rate;
			}

			qed_ptt_release(hwfn, ptt);
		}

		qed_iov_set_link(hwfn, i, &params, &link, &caps);
	}

	qed_schedule_iov(hwfn, QED_IOV_WQ_BULLETIN_UPDATE_FLAG);
}

static int qed_set_vf_link_state(struct qed_dev *cdev,
				 int vf_id, int link_state)
{
	int i;

	/* Sanitize request */
	if (IS_VF(cdev))
		return -EINVAL;

	if (!qed_iov_is_valid_vfid(&cdev->hwfns[0], vf_id, true, true)) {
		DP_VERBOSE(cdev, QED_MSG_IOV,
			   "VF index [%d] isn't active\n", vf_id);
		return -EINVAL;
	}

	/* Handle configuration of link state */
	for_each_hwfn(cdev, i) {
		struct qed_hwfn *hwfn = &cdev->hwfns[i];
		struct qed_public_vf_info *vf;

		vf = qed_iov_get_public_vf_info(hwfn, vf_id, true);
		if (!vf)
			continue;

		if (vf->link_state == link_state)
			continue;

		vf->link_state = link_state;
		qed_inform_vf_link_state(&cdev->hwfns[i]);
	}

	return 0;
}

static int qed_spoof_configure(struct qed_dev *cdev, int vfid, bool val)
{
	int i, rc = -EINVAL;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		rc = qed_iov_spoofchk_set(p_hwfn, vfid, val);
		if (rc)
			break;
	}

	return rc;
}

static int qed_configure_max_vf_rate(struct qed_dev *cdev, int vfid, int rate)
{
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct qed_public_vf_info *vf;

		if (!qed_iov_pf_sanity_check(p_hwfn, vfid)) {
			DP_NOTICE(p_hwfn,
				  "SR-IOV sanity check failed, can't set tx rate\n");
			return -EINVAL;
		}

		vf = qed_iov_get_public_vf_info(p_hwfn, vfid, true);

		vf->tx_rate = rate;

		qed_inform_vf_link_state(p_hwfn);
	}

	return 0;
}

static int qed_set_vf_rate(struct qed_dev *cdev,
			   int vfid, u32 min_rate, u32 max_rate)
{
	int rc_min = 0, rc_max = 0;

	if (max_rate)
		rc_max = qed_configure_max_vf_rate(cdev, vfid, max_rate);

	if (min_rate)
		rc_min = qed_iov_configure_min_tx_rate(cdev, vfid, min_rate);

	if (rc_max | rc_min)
		return -EINVAL;

	return 0;
}

static int qed_set_vf_trust(struct qed_dev *cdev, int vfid, bool trust)
{
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *hwfn = &cdev->hwfns[i];
		struct qed_public_vf_info *vf;

		if (!qed_iov_pf_sanity_check(hwfn, vfid)) {
			DP_NOTICE(hwfn,
				  "SR-IOV sanity check failed, can't set trust\n");
			return -EINVAL;
		}

		vf = qed_iov_get_public_vf_info(hwfn, vfid, true);

		if (vf->is_trusted_request == trust)
			return 0;
		vf->is_trusted_request = trust;

		qed_schedule_iov(hwfn, QED_IOV_WQ_TRUST_FLAG);
	}

	return 0;
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

	qed_iov_pf_get_pending_events(hwfn, events);

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

static bool qed_pf_validate_req_vf_mac(struct qed_hwfn *hwfn,
				       u8 *mac,
				       struct qed_public_vf_info *info)
{
	if (info->is_trusted_configured) {
		if (is_valid_ether_addr(info->mac) &&
		    (!mac || !ether_addr_equal(mac, info->mac)))
			return true;
	} else {
		if (is_valid_ether_addr(info->forced_mac) &&
		    (!mac || !ether_addr_equal(mac, info->forced_mac)))
			return true;
	}

	return false;
}

static void qed_set_bulletin_mac(struct qed_hwfn *hwfn,
				 struct qed_public_vf_info *info,
				 int vfid)
{
	if (info->is_trusted_configured)
		qed_iov_bulletin_set_mac(hwfn, info->mac, vfid);
	else
		qed_iov_bulletin_set_forced_mac(hwfn, info->forced_mac, vfid);
}

static void qed_handle_pf_set_vf_unicast(struct qed_hwfn *hwfn)
{
	int i;

	qed_for_each_vf(hwfn, i) {
		struct qed_public_vf_info *info;
		bool update = false;
		u8 *mac;

		info = qed_iov_get_public_vf_info(hwfn, i, true);
		if (!info)
			continue;

		/* Update data on bulletin board */
		if (info->is_trusted_configured)
			mac = qed_iov_bulletin_get_mac(hwfn, i);
		else
			mac = qed_iov_bulletin_get_forced_mac(hwfn, i);

		if (qed_pf_validate_req_vf_mac(hwfn, mac, info)) {
			DP_VERBOSE(hwfn,
				   QED_MSG_IOV,
				   "Handling PF setting of VF MAC to VF 0x%02x [Abs 0x%02x]\n",
				   i,
				   hwfn->cdev->p_iov_info->first_vf_in_pf + i);

			/* Update bulletin board with MAC */
			qed_set_bulletin_mac(hwfn, info, i);
			update = true;
		}

		if (qed_iov_bulletin_get_forced_vlan(hwfn, i) ^
		    info->forced_vlan) {
			DP_VERBOSE(hwfn,
				   QED_MSG_IOV,
				   "Handling PF setting of pvid [0x%04x] to VF 0x%02x [Abs 0x%02x]\n",
				   info->forced_vlan,
				   i,
				   hwfn->cdev->p_iov_info->first_vf_in_pf + i);
			qed_iov_bulletin_set_forced_vlan(hwfn,
							 info->forced_vlan, i);
			update = true;
		}

		if (update)
			qed_schedule_iov(hwfn, QED_IOV_WQ_BULLETIN_UPDATE_FLAG);
	}
}

static void qed_handle_bulletin_post(struct qed_hwfn *hwfn)
{
	struct qed_ptt *ptt;
	int i;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt) {
		DP_NOTICE(hwfn, "Failed allocating a ptt entry\n");
		qed_schedule_iov(hwfn, QED_IOV_WQ_BULLETIN_UPDATE_FLAG);
		return;
	}

	qed_for_each_vf(hwfn, i)
	    qed_iov_post_vf_bulletin(hwfn, i, ptt);

	qed_ptt_release(hwfn, ptt);
}

static void qed_update_mac_for_vf_trust_change(struct qed_hwfn *hwfn, int vf_id)
{
	struct qed_public_vf_info *vf_info;
	struct qed_vf_info *vf;
	u8 *force_mac;
	int i;

	vf_info = qed_iov_get_public_vf_info(hwfn, vf_id, true);
	vf = qed_iov_get_vf_info(hwfn, vf_id, true);

	if (!vf_info || !vf)
		return;

	/* Force MAC converted to generic MAC in case of VF trust on */
	if (vf_info->is_trusted_configured &&
	    (vf->bulletin.p_virt->valid_bitmap & BIT(MAC_ADDR_FORCED))) {
		force_mac = qed_iov_bulletin_get_forced_mac(hwfn, vf_id);

		if (force_mac) {
			/* Clear existing shadow copy of MAC to have a clean
			 * slate.
			 */
			for (i = 0; i < QED_ETH_VF_NUM_MAC_FILTERS; i++) {
				if (ether_addr_equal(vf->shadow_config.macs[i],
						     vf_info->mac)) {
					eth_zero_addr(vf->shadow_config.macs[i]);
					DP_VERBOSE(hwfn, QED_MSG_IOV,
						   "Shadow MAC %pM removed for VF 0x%02x, VF trust mode is ON\n",
						    vf_info->mac, vf_id);
					break;
				}
			}

			ether_addr_copy(vf_info->mac, force_mac);
			eth_zero_addr(vf_info->forced_mac);
			vf->bulletin.p_virt->valid_bitmap &=
					~BIT(MAC_ADDR_FORCED);
			qed_schedule_iov(hwfn, QED_IOV_WQ_BULLETIN_UPDATE_FLAG);
		}
	}

	/* Update shadow copy with VF MAC when trust mode is turned off */
	if (!vf_info->is_trusted_configured) {
		u8 empty_mac[ETH_ALEN];

		eth_zero_addr(empty_mac);
		for (i = 0; i < QED_ETH_VF_NUM_MAC_FILTERS; i++) {
			if (ether_addr_equal(vf->shadow_config.macs[i],
					     empty_mac)) {
				ether_addr_copy(vf->shadow_config.macs[i],
						vf_info->mac);
				DP_VERBOSE(hwfn, QED_MSG_IOV,
					   "Shadow is updated with %pM for VF 0x%02x, VF trust mode is OFF\n",
					    vf_info->mac, vf_id);
				break;
			}
		}
		/* Clear bulletin when trust mode is turned off,
		 * to have a clean slate for next (normal) operations.
		 */
		qed_iov_bulletin_set_mac(hwfn, empty_mac, vf_id);
		qed_schedule_iov(hwfn, QED_IOV_WQ_BULLETIN_UPDATE_FLAG);
	}
}

static void qed_iov_handle_trust_change(struct qed_hwfn *hwfn)
{
	struct qed_sp_vport_update_params params;
	struct qed_filter_accept_flags *flags;
	struct qed_public_vf_info *vf_info;
	struct qed_vf_info *vf;
	u8 mask;
	int i;

	mask = QED_ACCEPT_UCAST_UNMATCHED | QED_ACCEPT_MCAST_UNMATCHED;
	flags = &params.accept_flags;

	qed_for_each_vf(hwfn, i) {
		/* Need to make sure current requested configuration didn't
		 * flip so that we'll end up configuring something that's not
		 * needed.
		 */
		vf_info = qed_iov_get_public_vf_info(hwfn, i, true);
		if (vf_info->is_trusted_configured ==
		    vf_info->is_trusted_request)
			continue;
		vf_info->is_trusted_configured = vf_info->is_trusted_request;

		/* Handle forced MAC mode */
		qed_update_mac_for_vf_trust_change(hwfn, i);

		/* Validate that the VF has a configured vport */
		vf = qed_iov_get_vf_info(hwfn, i, true);
		if (!vf->vport_instance)
			continue;

		memset(&params, 0, sizeof(params));
		params.opaque_fid = vf->opaque_fid;
		params.vport_id = vf->vport_id;

		params.update_ctl_frame_check = 1;
		params.mac_chk_en = !vf_info->is_trusted_configured;
		params.update_accept_any_vlan_flg = 0;

		if (vf_info->accept_any_vlan && vf_info->forced_vlan) {
			params.update_accept_any_vlan_flg = 1;
			params.accept_any_vlan = vf_info->accept_any_vlan;
		}

		if (vf_info->rx_accept_mode & mask) {
			flags->update_rx_mode_config = 1;
			flags->rx_accept_filter = vf_info->rx_accept_mode;
		}

		if (vf_info->tx_accept_mode & mask) {
			flags->update_tx_mode_config = 1;
			flags->tx_accept_filter = vf_info->tx_accept_mode;
		}

		/* Remove if needed; Otherwise this would set the mask */
		if (!vf_info->is_trusted_configured) {
			flags->rx_accept_filter &= ~mask;
			flags->tx_accept_filter &= ~mask;
			params.accept_any_vlan = false;
		}

		if (flags->update_rx_mode_config ||
		    flags->update_tx_mode_config ||
		    params.update_ctl_frame_check ||
		    params.update_accept_any_vlan_flg) {
			DP_VERBOSE(hwfn, QED_MSG_IOV,
				   "vport update config for %s VF[abs 0x%x rel 0x%x]\n",
				   vf_info->is_trusted_configured ? "trusted" : "untrusted",
				   vf->abs_vf_id, vf->relative_vf_id);
			qed_sp_vport_update(hwfn, &params,
					    QED_SPQ_MODE_EBLOCK, NULL);
		}
	}
}

static void qed_iov_pf_task(struct work_struct *work)

{
	struct qed_hwfn *hwfn = container_of(work, struct qed_hwfn,
					     iov_task.work);
	int rc;

	if (test_and_clear_bit(QED_IOV_WQ_STOP_WQ_FLAG, &hwfn->iov_task_flags))
		return;

	if (test_and_clear_bit(QED_IOV_WQ_FLR_FLAG, &hwfn->iov_task_flags)) {
		struct qed_ptt *ptt = qed_ptt_acquire(hwfn);

		if (!ptt) {
			qed_schedule_iov(hwfn, QED_IOV_WQ_FLR_FLAG);
			return;
		}

		rc = qed_iov_vf_flr_cleanup(hwfn, ptt);
		if (rc)
			qed_schedule_iov(hwfn, QED_IOV_WQ_FLR_FLAG);

		qed_ptt_release(hwfn, ptt);
	}

	if (test_and_clear_bit(QED_IOV_WQ_MSG_FLAG, &hwfn->iov_task_flags))
		qed_handle_vf_msg(hwfn);

	if (test_and_clear_bit(QED_IOV_WQ_SET_UNICAST_FILTER_FLAG,
			       &hwfn->iov_task_flags))
		qed_handle_pf_set_vf_unicast(hwfn);

	if (test_and_clear_bit(QED_IOV_WQ_BULLETIN_UPDATE_FLAG,
			       &hwfn->iov_task_flags))
		qed_handle_bulletin_post(hwfn);

	if (test_and_clear_bit(QED_IOV_WQ_TRUST_FLAG, &hwfn->iov_task_flags))
		qed_iov_handle_trust_change(hwfn);
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

		/* PFs needs a dedicated workqueue only if they support IOV.
		 * VFs always require one.
		 */
		if (IS_PF(p_hwfn->cdev) && !IS_PF_SRIOV(p_hwfn))
			continue;

		snprintf(name, NAME_SIZE, "iov-%02x:%02x.%02x",
			 cdev->pdev->bus->number,
			 PCI_SLOT(cdev->pdev->devfn), p_hwfn->abs_pf_id);

		p_hwfn->iov_wq = create_singlethread_workqueue(name);
		if (!p_hwfn->iov_wq) {
			DP_NOTICE(p_hwfn, "Cannot create iov workqueue\n");
			return -ENOMEM;
		}

		if (IS_PF(cdev))
			INIT_DELAYED_WORK(&p_hwfn->iov_task, qed_iov_pf_task);
		else
			INIT_DELAYED_WORK(&p_hwfn->iov_task, qed_iov_vf_task);
	}

	return 0;
}

const struct qed_iov_hv_ops qed_iov_ops_pass = {
	.configure = &qed_sriov_configure,
	.set_mac = &qed_sriov_pf_set_mac,
	.set_vlan = &qed_sriov_pf_set_vlan,
	.get_config = &qed_get_vf_config,
	.set_link_state = &qed_set_vf_link_state,
	.set_spoof = &qed_spoof_configure,
	.set_rate = &qed_set_vf_rate,
	.set_trust = &qed_set_vf_trust,
};
