/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/etherdevice.h>
#include <linux/crc32.h>
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
		p_ramrod->personality = PERSONALITY_RDMA_AND_ETH;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown VF personality %d\n",
			  p_hwfn->hw_info.personality);
		return -EINVAL;
	}

	fp_minor = p_vf->acquire.vfdev_info.eth_fp_hsi_minor;
	if (fp_minor > ETH_HSI_VER_MINOR) {
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

static bool qed_iov_validate_rxq(struct qed_hwfn *p_hwfn,
				 struct qed_vf_info *p_vf, u16 rx_qid)
{
	if (rx_qid >= p_vf->num_rxqs)
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[0x%02x] - can't touch Rx queue[%04x]; Only 0x%04x are allocated\n",
			   p_vf->abs_vf_id, rx_qid, p_vf->num_rxqs);
	return rx_qid < p_vf->num_rxqs;
}

static bool qed_iov_validate_txq(struct qed_hwfn *p_hwfn,
				 struct qed_vf_info *p_vf, u16 tx_qid)
{
	if (tx_qid >= p_vf->num_txqs)
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[0x%02x] - can't touch Tx queue[%04x]; Only 0x%04x are allocated\n",
			   p_vf->abs_vf_id, tx_qid, p_vf->num_txqs);
	return tx_qid < p_vf->num_txqs;
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

int qed_iov_post_vf_bulletin(struct qed_hwfn *p_hwfn,
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
	params.flags = QED_DMAE_FLAG_VF_DST;
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
	if (IS_VF(p_hwfn->cdev) || !IS_QED_SRIOV(p_hwfn->cdev) ||
	    !IS_PF_SRIOV_ALLOC(p_hwfn))
		return false;

	/* Check VF validity */
	if (!qed_iov_is_valid_vfid(p_hwfn, vfid, true))
		return false;

	return true;
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

void qed_iov_set_vfs_to_disable(struct qed_dev *cdev, u8 to_disable)
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

static int qed_iov_enable_vf_access(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    struct qed_vf_info *vf)
{
	u32 igu_vf_conf = IGU_VF_CONF_FUNC_EN;
	int rc;

	if (vf->to_disable)
		return 0;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "Enable internal access for vf %x [abs %x]\n",
		   vf->abs_vf_id, QED_VF_ABS_ID(p_hwfn, vf));

	qed_iov_vf_pglue_clear_err(p_hwfn, p_ptt, QED_VF_ABS_ID(p_hwfn, vf));

	qed_iov_vf_igu_reset(p_hwfn, p_ptt, vf);

	rc = qed_mcp_config_vf_msix(p_hwfn, p_ptt, vf->abs_vf_id, vf->num_sbs);
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
 * @brief qed_iov_config_perm_table - configure the permission
 *      zone table.
 *      In E4, queue zone permission table size is 320x9. There
 *      are 320 VF queues for single engine device (256 for dual
 *      engine device), and each entry has the following format:
 *      {Valid, VF[7:0]}
 * @param p_hwfn
 * @param p_ptt
 * @param vf
 * @param enable
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
		val = enable ? (vf->abs_vf_id | (1 << 8)) : 0;
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
	struct qed_igu_block *igu_blocks;
	int qid = 0, igu_id = 0;
	u32 val = 0;

	igu_blocks = p_hwfn->hw_info.p_igu_info->igu_map.igu_blocks;

	if (num_rx_queues > p_hwfn->hw_info.p_igu_info->free_blks)
		num_rx_queues = p_hwfn->hw_info.p_igu_info->free_blks;
	p_hwfn->hw_info.p_igu_info->free_blks -= num_rx_queues;

	SET_FIELD(val, IGU_MAPPING_LINE_FUNCTION_NUMBER, vf->abs_vf_id);
	SET_FIELD(val, IGU_MAPPING_LINE_VALID, 1);
	SET_FIELD(val, IGU_MAPPING_LINE_PF_VALID, 0);

	while ((qid < num_rx_queues) &&
	       (igu_id < QED_MAPPING_MEMORY_SIZE(p_hwfn->cdev))) {
		if (igu_blocks[igu_id].status & QED_IGU_STATUS_FREE) {
			struct cau_sb_entry sb_entry;

			vf->igu_sbs[qid] = (u16)igu_id;
			igu_blocks[igu_id].status &= ~QED_IGU_STATUS_FREE;

			SET_FIELD(val, IGU_MAPPING_LINE_VECTOR_NUMBER, qid);

			qed_wr(p_hwfn, p_ptt,
			       IGU_REG_MAPPING_MEMORY + sizeof(u32) * igu_id,
			       val);

			/* Configure igu sb in CAU which were marked valid */
			qed_init_cau_sb_entry(p_hwfn, &sb_entry,
					      p_hwfn->rel_pf_id,
					      vf->abs_vf_id, 1);
			qed_dmae_host2grc(p_hwfn, p_ptt,
					  (u64)(uintptr_t)&sb_entry,
					  CAU_REG_SB_VAR_MEMORY +
					  igu_id * sizeof(u64), 2, 0);
			qid++;
		}
		igu_id++;
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

		p_info->igu_map.igu_blocks[igu_id].status |=
		    QED_IGU_STATUS_FREE;

		p_hwfn->hw_info.p_igu_info->free_blks++;
	}

	vf->num_sbs = 0;
}

static int qed_iov_init_hw_for_vf(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  u16 rel_vf_id, u16 num_rx_queues)
{
	u8 num_of_vf_avaiable_chains = 0;
	struct qed_vf_info *vf = NULL;
	int rc = 0;
	u32 cids;
	u8 i;

	vf = qed_iov_get_vf_info(p_hwfn, rel_vf_id, false);
	if (!vf) {
		DP_ERR(p_hwfn, "qed_iov_init_hw_for_vf : vf is NULL\n");
		return -EINVAL;
	}

	if (vf->b_init) {
		DP_NOTICE(p_hwfn, "VF[%d] is already active.\n", rel_vf_id);
		return -EINVAL;
	}

	/* Limit number of queues according to number of CIDs */
	qed_cxt_get_proto_cid_count(p_hwfn, PROTOCOLID_ETH, &cids);
	DP_VERBOSE(p_hwfn,
		   QED_MSG_IOV,
		   "VF[%d] - requesting to initialize for 0x%04x queues [0x%04x CIDs available]\n",
		   vf->relative_vf_id, num_rx_queues, (u16) cids);
	num_rx_queues = min_t(u16, num_rx_queues, ((u16) cids));

	num_of_vf_avaiable_chains = qed_iov_alloc_vf_igu_sbs(p_hwfn,
							     p_ptt,
							     vf,
							     num_rx_queues);
	if (!num_of_vf_avaiable_chains) {
		DP_ERR(p_hwfn, "no available igu sbs\n");
		return -ENOMEM;
	}

	/* Choose queue number and index ranges */
	vf->num_rxqs = num_of_vf_avaiable_chains;
	vf->num_txqs = num_of_vf_avaiable_chains;

	for (i = 0; i < vf->num_rxqs; i++) {
		u16 queue_id = qed_int_queue_id_from_sb_id(p_hwfn,
							   vf->igu_sbs[i]);

		if (queue_id > RESC_NUM(p_hwfn, QED_L2_QUEUE)) {
			DP_NOTICE(p_hwfn,
				  "VF[%d] will require utilizing of out-of-bounds queues - %04x\n",
				  vf->relative_vf_id, queue_id);
			return -EINVAL;
		}

		/* CIDs are per-VF, so no problem having them 0-based. */
		vf->vf_queues[i].fw_rx_qid = queue_id;
		vf->vf_queues[i].fw_tx_qid = queue_id;
		vf->vf_queues[i].fw_cid = i;

		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "VF[%d] - [%d] SB %04x, Tx/Rx queue %04x CID %04x\n",
			   vf->relative_vf_id, i, vf->igu_sbs[i], queue_id, i);
	}
	rc = qed_iov_enable_vf_access(p_hwfn, p_ptt, vf);
	if (!rc) {
		vf->b_init = true;

		if (IS_LEAD_HWFN(p_hwfn))
			p_hwfn->cdev->p_iov_info->num_vfs++;
	}

	return rc;
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
		if (!(tlvs_mask & (1 << i)))
			continue;

		resp = qed_add_tlv(p_hwfn, &p_mbx->offset,
				   qed_iov_vport_to_tlv(p_hwfn, i), size);

		if (tlvs_accepted & (1 << i))
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

struct qed_public_vf_info *qed_iov_get_public_vf_info(struct qed_hwfn *p_hwfn,
						      u16 relative_vf_id,
						      bool b_enabled_only)
{
	struct qed_vf_info *vf = NULL;

	vf = qed_iov_get_vf_info(p_hwfn, relative_vf_id, b_enabled_only);
	if (!vf)
		return NULL;

	return &vf->p_vf_info;
}

void qed_iov_clean_vf(struct qed_hwfn *p_hwfn, u8 vfid)
{
	struct qed_public_vf_info *vf_info;

	vf_info = qed_iov_get_public_vf_info(p_hwfn, vfid, false);

	if (!vf_info)
		return;

	/* Clear the VF mac */
	memset(vf_info->mac, 0, ETH_ALEN);
}

static void qed_iov_vf_cleanup(struct qed_hwfn *p_hwfn,
			       struct qed_vf_info *p_vf)
{
	u32 i;

	p_vf->vf_bulletin = 0;
	p_vf->vport_instance = 0;
	p_vf->configured_features = 0;

	/* If VF previously requested less resources, go back to default */
	p_vf->num_rxqs = p_vf->num_sbs;
	p_vf->num_txqs = p_vf->num_sbs;

	p_vf->num_active_rxqs = 0;

	for (i = 0; i < QED_MAX_VF_CHAINS_PER_PF; i++)
		p_vf->vf_queues[i].rxq_active = 0;

	memset(&p_vf->shadow_config, 0, sizeof(p_vf->shadow_config));
	memset(&p_vf->acquire, 0, sizeof(p_vf->acquire));
	qed_iov_clean_vf(p_hwfn, p_vf->relative_vf_id);
}

static u8 qed_iov_vf_mbx_acquire_resc(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      struct qed_vf_info *p_vf,
				      struct vf_pf_resc_request *p_req,
				      struct pf_vf_resc *p_resp)
{
	int i;

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
		p_resp->cid[i] = p_vf->vf_queues[i].fw_cid;
	}

	/* Filter related information */
	p_resp->num_mac_filters = min_t(u8, p_vf->num_mac_filters,
					p_req->num_mac_filters);
	p_resp->num_vlan_filters = min_t(u8, p_vf->num_vlan_filters,
					 p_req->num_vlan_filters);

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
	    p_resp->num_mc_filters < p_req->num_mc_filters) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "VF[%d] - Insufficient resources: rxq [%02x/%02x] txq [%02x/%02x] sbs [%02x/%02x] mac [%02x/%02x] vlan [%02x/%02x] mc [%02x/%02x]\n",
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
			   p_req->num_mc_filters, p_resp->num_mc_filters);
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

	/* Validate FW compatibility */
	if (req->vfdev_info.eth_fp_hsi_major != ETH_HSI_VER_MAJOR) {
		DP_INFO(p_hwfn,
			"VF[%d] needs fastpath HSI %02x.%02x, which is incompatible with loaded FW's faspath HSI %02x.%02x\n",
			vf->abs_vf_id,
			req->vfdev_info.eth_fp_hsi_major,
			req->vfdev_info.eth_fp_hsi_minor,
			ETH_HSI_VER_MAJOR, ETH_HSI_VER_MINOR);

		/* Write the PF version so that VF would know which version
		 * is supported.
		 */
		pfdev_info->major_fp_hsi = ETH_HSI_VER_MAJOR;
		pfdev_info->minor_fp_hsi = ETH_HSI_VER_MINOR;

		goto out;
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
	pfdev_info->indices_per_sb = PIS_PER_SB;

	pfdev_info->capabilities = PFVF_ACQUIRE_CAP_DEFAULT_UNTAGGED |
				   PFVF_ACQUIRE_CAP_POST_FW_OVERRIDE;
	if (p_hwfn->cdev->num_hwfns > 1)
		pfdev_info->capabilities |= PFVF_ACQUIRE_CAP_100G;

	qed_iov_vf_mbx_acquire_stats(p_hwfn, &pfdev_info->stats_info);

	memcpy(pfdev_info->port_mac, p_hwfn->hw_info.hw_mac_addr, ETH_ALEN);

	pfdev_info->fw_major = FW_MAJOR_VERSION;
	pfdev_info->fw_minor = FW_MINOR_VERSION;
	pfdev_info->fw_rev = FW_REVISION_VERSION;
	pfdev_info->fw_eng = FW_ENGINEERING_VERSION;
	pfdev_info->minor_fp_hsi = min_t(u8,
					 ETH_HSI_VER_MINOR,
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
	if (rc) {
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
		DP_VERBOSE(p_hwfn,
			   QED_MSG_IOV,
			   "Reconfiguring VLAN [0x%04x] for VF [%04x]\n",
			   filter.vlan, p_vf->relative_vf_id);
		rc = qed_sp_eth_filter_ucast(p_hwfn,
					     p_vf->opaque_fid,
					     &filter,
					     QED_SPQ_MODE_CB, NULL);
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

	if ((events & (1 << VLAN_ADDR_FORCED)) &&
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

	if (events & (1 << MAC_ADDR_FORCED)) {
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

		p_vf->configured_features |= 1 << MAC_ADDR_FORCED;
	}

	if (events & (1 << VLAN_ADDR_FORCED)) {
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
			u16 qid;

			if (!p_vf->vf_queues[i].rxq_active)
				continue;

			qid = p_vf->vf_queues[i].fw_rx_qid;

			rc = qed_sp_eth_rx_queues_update(p_hwfn, qid,
							 1, 0, 1,
							 QED_SPQ_MODE_EBLOCK,
							 NULL);
			if (rc) {
				DP_NOTICE(p_hwfn,
					  "Failed to send Rx update fo queue[0x%04x]\n",
					  qid);
				return rc;
			}
		}

		if (filter.vlan)
			p_vf->configured_features |= 1 << VLAN_ADDR_FORCED;
		else
			p_vf->configured_features &= ~(1 << VLAN_ADDR_FORCED);
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
				    vf->igu_sbs[sb_id],
				    vf->abs_vf_id, 1);
	}
	qed_iov_enable_vf_traffic(p_hwfn, p_ptt, vf);

	vf->mtu = start->mtu;
	vf->shadow_config.inner_vlan_removal = start->inner_vlan_removal;

	/* Take into consideration configuration forced by hypervisor;
	 * If none is configured, use the supplied VF values [for old
	 * vfs that would still be fine, since they passed '0' as padding].
	 */
	p_bitmap = &vf_info->bulletin.p_virt->valid_bitmap;
	if (!(*p_bitmap & (1 << VFPF_BULLETIN_UNTAGGED_DEFAULT_FORCED))) {
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

	rc = qed_sp_eth_vport_start(p_hwfn, &params);
	if (rc != 0) {
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

	rc = qed_sp_vport_stop(p_hwfn, vf->opaque_fid, vf->vport_id);
	if (rc != 0) {
		DP_ERR(p_hwfn, "qed_iov_vf_mbx_stop_vport returned error %d\n",
		       rc);
		status = PFVF_STATUS_FAILURE;
	}

	/* Forget the configuration on the vport */
	vf->configured_features = 0;
	memset(&vf->shadow_config, 0, sizeof(vf->shadow_config));

	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_VPORT_TEARDOWN,
			     sizeof(struct pfvf_def_resp_tlv), status);
}

static void qed_iov_vf_mbx_start_rxq_resp(struct qed_hwfn *p_hwfn,
					  struct qed_ptt *p_ptt,
					  struct qed_vf_info *vf, u8 status)
{
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct pfvf_start_queue_resp_tlv *p_tlv;
	struct vfpf_start_rxq_tlv *req;

	mbx->offset = (u8 *)mbx->reply_virt;

	p_tlv = qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_START_RXQ,
			    sizeof(*p_tlv));
	qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_LIST_END,
		    sizeof(struct channel_list_end_tlv));

	/* Update the TLV with the response */
	if (status == PFVF_STATUS_SUCCESS) {
		req = &mbx->req_virt->start_rxq;
		p_tlv->offset = PXP_VF_BAR0_START_MSDM_ZONE_B +
				offsetof(struct mstorm_vf_zone,
					 non_trigger.eth_rx_queue_producers) +
				sizeof(struct eth_rx_prod_data) * req->rx_qid;
	}

	qed_iov_send_response(p_hwfn, p_ptt, vf, sizeof(*p_tlv), status);
}

static void qed_iov_vf_mbx_start_rxq(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_vf_info *vf)
{
	struct qed_queue_start_common_params params;
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	u8 status = PFVF_STATUS_NO_RESOURCE;
	struct vfpf_start_rxq_tlv *req;
	int rc;

	memset(&params, 0, sizeof(params));
	req = &mbx->req_virt->start_rxq;

	if (!qed_iov_validate_rxq(p_hwfn, vf, req->rx_qid) ||
	    !qed_iov_validate_sb(p_hwfn, vf, req->hw_sb))
		goto out;

	params.queue_id =  vf->vf_queues[req->rx_qid].fw_rx_qid;
	params.vf_qid = req->rx_qid;
	params.vport_id = vf->vport_id;
	params.sb = req->hw_sb;
	params.sb_idx = req->sb_index;

	rc = qed_sp_eth_rxq_start_ramrod(p_hwfn, vf->opaque_fid,
					 vf->vf_queues[req->rx_qid].fw_cid,
					 &params,
					 vf->abs_vf_id + 0x10,
					 req->bd_max_bytes,
					 req->rxq_addr,
					 req->cqe_pbl_addr, req->cqe_pbl_size);

	if (rc) {
		status = PFVF_STATUS_FAILURE;
	} else {
		status = PFVF_STATUS_SUCCESS;
		vf->vf_queues[req->rx_qid].rxq_active = true;
		vf->num_active_rxqs++;
	}

out:
	qed_iov_vf_mbx_start_rxq_resp(p_hwfn, p_ptt, vf, status);
}

static void qed_iov_vf_mbx_start_txq_resp(struct qed_hwfn *p_hwfn,
					  struct qed_ptt *p_ptt,
					  struct qed_vf_info *p_vf, u8 status)
{
	struct qed_iov_vf_mbx *mbx = &p_vf->vf_mbx;
	struct pfvf_start_queue_resp_tlv *p_tlv;

	mbx->offset = (u8 *)mbx->reply_virt;

	p_tlv = qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_START_TXQ,
			    sizeof(*p_tlv));
	qed_add_tlv(p_hwfn, &mbx->offset, CHANNEL_TLV_LIST_END,
		    sizeof(struct channel_list_end_tlv));

	/* Update the TLV with the response */
	if (status == PFVF_STATUS_SUCCESS) {
		u16 qid = mbx->req_virt->start_txq.tx_qid;

		p_tlv->offset = qed_db_addr(p_vf->vf_queues[qid].fw_cid,
					    DQ_DEMS_LEGACY);
	}

	qed_iov_send_response(p_hwfn, p_ptt, p_vf, sizeof(*p_tlv), status);
}

static void qed_iov_vf_mbx_start_txq(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_vf_info *vf)
{
	struct qed_queue_start_common_params params;
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	u8 status = PFVF_STATUS_NO_RESOURCE;
	union qed_qm_pq_params pq_params;
	struct vfpf_start_txq_tlv *req;
	int rc;

	/* Prepare the parameters which would choose the right PQ */
	memset(&pq_params, 0, sizeof(pq_params));
	pq_params.eth.is_vf = 1;
	pq_params.eth.vf_id = vf->relative_vf_id;

	memset(&params, 0, sizeof(params));
	req = &mbx->req_virt->start_txq;

	if (!qed_iov_validate_txq(p_hwfn, vf, req->tx_qid) ||
	    !qed_iov_validate_sb(p_hwfn, vf, req->hw_sb))
		goto out;

	params.queue_id =  vf->vf_queues[req->tx_qid].fw_tx_qid;
	params.vport_id = vf->vport_id;
	params.sb = req->hw_sb;
	params.sb_idx = req->sb_index;

	rc = qed_sp_eth_txq_start_ramrod(p_hwfn,
					 vf->opaque_fid,
					 vf->vf_queues[req->tx_qid].fw_cid,
					 &params,
					 vf->abs_vf_id + 0x10,
					 req->pbl_addr,
					 req->pbl_size, &pq_params);

	if (rc) {
		status = PFVF_STATUS_FAILURE;
	} else {
		status = PFVF_STATUS_SUCCESS;
		vf->vf_queues[req->tx_qid].txq_active = true;
	}

out:
	qed_iov_vf_mbx_start_txq_resp(p_hwfn, p_ptt, vf, status);
}

static int qed_iov_vf_stop_rxqs(struct qed_hwfn *p_hwfn,
				struct qed_vf_info *vf,
				u16 rxq_id, u8 num_rxqs, bool cqe_completion)
{
	int rc = 0;
	int qid;

	if (rxq_id + num_rxqs > ARRAY_SIZE(vf->vf_queues))
		return -EINVAL;

	for (qid = rxq_id; qid < rxq_id + num_rxqs; qid++) {
		if (vf->vf_queues[qid].rxq_active) {
			rc = qed_sp_eth_rx_queue_stop(p_hwfn,
						      vf->vf_queues[qid].
						      fw_rx_qid, false,
						      cqe_completion);

			if (rc)
				return rc;
		}
		vf->vf_queues[qid].rxq_active = false;
		vf->num_active_rxqs--;
	}

	return rc;
}

static int qed_iov_vf_stop_txqs(struct qed_hwfn *p_hwfn,
				struct qed_vf_info *vf, u16 txq_id, u8 num_txqs)
{
	int rc = 0;
	int qid;

	if (txq_id + num_txqs > ARRAY_SIZE(vf->vf_queues))
		return -EINVAL;

	for (qid = txq_id; qid < txq_id + num_txqs; qid++) {
		if (vf->vf_queues[qid].txq_active) {
			rc = qed_sp_eth_tx_queue_stop(p_hwfn,
						      vf->vf_queues[qid].
						      fw_tx_qid);

			if (rc)
				return rc;
		}
		vf->vf_queues[qid].txq_active = false;
	}
	return rc;
}

static void qed_iov_vf_mbx_stop_rxqs(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_vf_info *vf)
{
	u16 length = sizeof(struct pfvf_def_resp_tlv);
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	u8 status = PFVF_STATUS_SUCCESS;
	struct vfpf_stop_rxqs_tlv *req;
	int rc;

	/* We give the option of starting from qid != 0, in this case we
	 * need to make sure that qid + num_qs doesn't exceed the actual
	 * amount of queues that exist.
	 */
	req = &mbx->req_virt->stop_rxqs;
	rc = qed_iov_vf_stop_rxqs(p_hwfn, vf, req->rx_qid,
				  req->num_rxqs, req->cqe_completion);
	if (rc)
		status = PFVF_STATUS_FAILURE;

	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_STOP_RXQS,
			     length, status);
}

static void qed_iov_vf_mbx_stop_txqs(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_vf_info *vf)
{
	u16 length = sizeof(struct pfvf_def_resp_tlv);
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	u8 status = PFVF_STATUS_SUCCESS;
	struct vfpf_stop_txqs_tlv *req;
	int rc;

	/* We give the option of starting from qid != 0, in this case we
	 * need to make sure that qid + num_qs doesn't exceed the actual
	 * amount of queues that exist.
	 */
	req = &mbx->req_virt->stop_txqs;
	rc = qed_iov_vf_stop_txqs(p_hwfn, vf, req->tx_qid, req->num_txqs);
	if (rc)
		status = PFVF_STATUS_FAILURE;

	qed_iov_prepare_resp(p_hwfn, p_ptt, vf, CHANNEL_TLV_STOP_TXQS,
			     length, status);
}

static void qed_iov_vf_mbx_update_rxqs(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       struct qed_vf_info *vf)
{
	u16 length = sizeof(struct pfvf_def_resp_tlv);
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct vfpf_update_rxq_tlv *req;
	u8 status = PFVF_STATUS_SUCCESS;
	u8 complete_event_flg;
	u8 complete_cqe_flg;
	u16 qid;
	int rc;
	u8 i;

	req = &mbx->req_virt->update_rxq;
	complete_cqe_flg = !!(req->flags & VFPF_RXQ_UPD_COMPLETE_CQE_FLAG);
	complete_event_flg = !!(req->flags & VFPF_RXQ_UPD_COMPLETE_EVENT_FLAG);

	for (i = 0; i < req->num_rxqs; i++) {
		qid = req->rx_qid + i;

		if (!vf->vf_queues[qid].rxq_active) {
			DP_NOTICE(p_hwfn, "VF rx_qid = %d isn`t active!\n",
				  qid);
			status = PFVF_STATUS_FAILURE;
			break;
		}

		rc = qed_sp_eth_rx_queues_update(p_hwfn,
						 vf->vf_queues[qid].fw_rx_qid,
						 1,
						 complete_cqe_flg,
						 complete_event_flg,
						 QED_SPQ_MODE_EBLOCK, NULL);

		if (rc) {
			status = PFVF_STATUS_FAILURE;
			break;
		}
	}

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
	if (!(p_vf->configured_features & (1 << VLAN_ADDR_FORCED))) {
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
	       sizeof(unsigned long) * ETH_MULTICAST_MAC_BINS_IN_REGS);
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
			    struct qed_iov_vf_mbx *p_mbx, u16 *tlvs_mask)
{
	struct vfpf_vport_update_rss_tlv *p_rss_tlv;
	u16 tlv = CHANNEL_TLV_VPORT_UPDATE_RSS;
	u16 i, q_idx, max_q_idx;
	u16 table_size;

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
	memcpy(p_rss->rss_ind_table, p_rss_tlv->rss_ind_table,
	       sizeof(p_rss->rss_ind_table));
	memcpy(p_rss->rss_key, p_rss_tlv->rss_key, sizeof(p_rss->rss_key));

	table_size = min_t(u16, ARRAY_SIZE(p_rss->rss_ind_table),
			   (1 << p_rss_tlv->rss_table_size_log));

	max_q_idx = ARRAY_SIZE(vf->vf_queues);

	for (i = 0; i < table_size; i++) {
		u16 index = vf->vf_queues[0].fw_rx_qid;

		q_idx = p_rss->rss_ind_table[i];
		if (q_idx >= max_q_idx)
			DP_NOTICE(p_hwfn,
				  "rss_ind_table[%d] = %d, rxq is out of range\n",
				  i, q_idx);
		else if (!vf->vf_queues[q_idx].rxq_active)
			DP_NOTICE(p_hwfn,
				  "rss_ind_table[%d] = %d, rxq is not active\n",
				  i, q_idx);
		else
			index = vf->vf_queues[q_idx].fw_rx_qid;
		p_rss->rss_ind_table[i] = index;
	}

	p_data->rss_params = p_rss;
	*tlvs_mask |= 1 << QED_IOV_VP_UPDATE_RSS;
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

static void qed_iov_vf_mbx_vport_update(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					struct qed_vf_info *vf)
{
	struct qed_sp_vport_update_params params;
	struct qed_iov_vf_mbx *mbx = &vf->vf_mbx;
	struct qed_sge_tpa_params sge_tpa_params;
	struct qed_rss_params rss_params;
	u8 status = PFVF_STATUS_SUCCESS;
	u16 tlvs_mask = 0;
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
	qed_iov_vp_update_rss_param(p_hwfn, vf, &params, &rss_params,
				    mbx, &tlvs_mask);
	qed_iov_vp_update_accept_any_vlan(p_hwfn, &params, mbx, &tlvs_mask);
	qed_iov_vp_update_sge_tpa_param(p_hwfn, vf, &params,
					&sge_tpa_params, mbx, &tlvs_mask);

	/* Just log a message if there is no single extended tlv in buffer.
	 * When all features of vport update ramrod would be requested by VF
	 * as extended TLVs in buffer then an error can be returned in response
	 * if there is no extended TLV present in buffer.
	 */
	if (!tlvs_mask) {
		DP_NOTICE(p_hwfn,
			  "No feature tlvs found for vport update\n");
		status = PFVF_STATUS_NOT_SUPPORTED;
		goto out;
	}

	rc = qed_sp_vport_update(p_hwfn, &params, QED_SPQ_MODE_EBLOCK, NULL);

	if (rc)
		status = PFVF_STATUS_FAILURE;

out:
	length = qed_iov_prep_vp_update_resp_tlvs(p_hwfn, vf, mbx, status,
						  tlvs_mask, tlvs_mask);
	qed_iov_send_response(p_hwfn, p_ptt, vf, length, status);
}

static int qed_iov_vf_update_unicast_shadow(struct qed_hwfn *p_hwfn,
					    struct qed_vf_info *p_vf,
					    struct qed_filter_ucast *p_params)
{
	int i;

	if (p_params->type == QED_FILTER_MAC)
		return 0;

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
	if (p_vf->bulletin.p_virt->valid_bitmap & (1 << VLAN_ADDR_FORCED))
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

int qed_iov_chk_ucast(struct qed_hwfn *hwfn,
		      int vfid, struct qed_filter_ucast *params)
{
	struct qed_public_vf_info *vf;

	vf = qed_iov_get_public_vf_info(hwfn, vfid, true);
	if (!vf)
		return -EINVAL;

	/* No real decision to make; Store the configured MAC */
	if (params->type == QED_FILTER_MAC ||
	    params->type == QED_FILTER_MAC_VLAN)
		ether_addr_copy(vf->mac, params->mac);

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
		   "VF[%d]: opcode 0x%02x type 0x%02x [%s %s] [vport 0x%02x] MAC %02x:%02x:%02x:%02x:%02x:%02x, vlan 0x%04x\n",
		   vf->abs_vf_id, params.opcode, params.type,
		   params.is_rx_filter ? "RX" : "",
		   params.is_tx_filter ? "TX" : "",
		   params.vport_to_add_to,
		   params.mac[0], params.mac[1],
		   params.mac[2], params.mac[3],
		   params.mac[4], params.mac[5], params.vlan);

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
	if ((p_bulletin->valid_bitmap & (1 << VLAN_ADDR_FORCED)) &&
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

	if ((p_bulletin->valid_bitmap & (1 << MAC_ADDR_FORCED)) &&
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
	u32 cons[MAX_NUM_VOQS], distance[MAX_NUM_VOQS];
	int i, cnt;

	/* Read initial consumers & producers */
	for (i = 0; i < MAX_NUM_VOQS; i++) {
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
		for (; i < MAX_NUM_VOQS; i++) {
			u32 tmp;

			tmp = qed_rd(p_hwfn, p_ptt,
				     PBF_REG_NUM_BLOCKS_ALLOCATED_CONS_VOQ0 +
				     i * 0x40);
			if (distance[i] > tmp - cons[i])
				break;
		}

		if (i == MAX_NUM_VOQS)
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
		ack_vfs[vfid / 32] |= (1 << (vfid % 32));
		p_hwfn->pf_iov_info->pending_flr[rel_vf_id / 64] &=
		    ~(1ULL << (rel_vf_id % 64));
		p_hwfn->pf_iov_info->pending_events[rel_vf_id / 64] &=
		    ~(1ULL << (rel_vf_id % 64));
	}

	return rc;
}

int qed_iov_vf_flr_cleanup(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
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

int qed_iov_mark_vf_flr(struct qed_hwfn *p_hwfn, u32 *p_disabled_vfs)
{
	u16 i, found = 0;

	DP_VERBOSE(p_hwfn, QED_MSG_IOV, "Marking FLR-ed VFs\n");
	for (i = 0; i < (VF_MAX_STATIC / 32); i++)
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "[%08x,...,%08x]: %08x\n",
			   i * 32, (i + 1) * 32 - 1, p_disabled_vfs[i]);

	if (!p_hwfn->cdev->p_iov_info) {
		DP_NOTICE(p_hwfn, "VF flr but no IOV\n");
		return 0;
	}

	/* Mark VFs */
	for (i = 0; i < p_hwfn->cdev->p_iov_info->total_vfs; i++) {
		struct qed_vf_info *p_vf;
		u8 vfid;

		p_vf = qed_iov_get_vf_info(p_hwfn, i, false);
		if (!p_vf)
			continue;

		vfid = p_vf->abs_vf_id;
		if ((1 << (vfid % 32)) & p_disabled_vfs[vfid / 32]) {
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
			found = 1;
		}
	}

	return found;
}

static void qed_iov_get_link(struct qed_hwfn *p_hwfn,
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
		return;

	p_bulletin = p_vf->bulletin.p_virt;

	if (p_params)
		__qed_vf_get_link_params(p_hwfn, p_params, p_bulletin);
	if (p_link)
		__qed_vf_get_link_state(p_hwfn, p_link, p_bulletin);
	if (p_caps)
		__qed_vf_get_link_caps(p_hwfn, p_caps, p_bulletin);
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
		}
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

	feature = 1 << MAC_ADDR_FORCED;
	memcpy(vf_info->bulletin.p_virt->mac, mac, ETH_ALEN);

	vf_info->bulletin.p_virt->valid_bitmap |= feature;
	/* Forced MAC will disable MAC_ADDR */
	vf_info->bulletin.p_virt->valid_bitmap &=
				~(1 << VFPF_BULLETIN_MAC_ADDR);

	qed_iov_configure_vport_forced(p_hwfn, vf_info, feature);
}

void qed_iov_bulletin_set_forced_vlan(struct qed_hwfn *p_hwfn,
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

	feature = 1 << VLAN_ADDR_FORCED;
	vf_info->bulletin.p_virt->pvid = pvid;
	if (pvid)
		vf_info->bulletin.p_virt->valid_bitmap |= feature;
	else
		vf_info->bulletin.p_virt->valid_bitmap &= ~feature;

	qed_iov_configure_vport_forced(p_hwfn, vf_info, feature);
}

static bool qed_iov_vf_has_vport_instance(struct qed_hwfn *p_hwfn, int vfid)
{
	struct qed_vf_info *p_vf_info;

	p_vf_info = qed_iov_get_vf_info(p_hwfn, (u16) vfid, true);
	if (!p_vf_info)
		return false;

	return !!p_vf_info->vport_instance;
}

bool qed_iov_is_vf_stopped(struct qed_hwfn *p_hwfn, int vfid)
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

int qed_iov_spoofchk_set(struct qed_hwfn *p_hwfn, int vfid, bool val)
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

static u8 *qed_iov_bulletin_get_forced_mac(struct qed_hwfn *p_hwfn,
					   u16 rel_vf_id)
{
	struct qed_vf_info *p_vf;

	p_vf = qed_iov_get_vf_info(p_hwfn, rel_vf_id, true);
	if (!p_vf || !p_vf->bulletin.p_virt)
		return NULL;

	if (!(p_vf->bulletin.p_virt->valid_bitmap & (1 << MAC_ADDR_FORCED)))
		return NULL;

	return p_vf->bulletin.p_virt->mac;
}

u16 qed_iov_bulletin_get_forced_vlan(struct qed_hwfn *p_hwfn, u16 rel_vf_id)
{
	struct qed_vf_info *p_vf;

	p_vf = qed_iov_get_vf_info(p_hwfn, rel_vf_id, true);
	if (!p_vf || !p_vf->bulletin.p_virt)
		return 0;

	if (!(p_vf->bulletin.p_virt->valid_bitmap & (1 << VLAN_ADDR_FORCED)))
		return 0;

	return p_vf->bulletin.p_virt->pvid;
}

static int qed_iov_configure_tx_rate(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt, int vfid, int val)
{
	struct qed_vf_info *vf;
	u8 abs_vp_id = 0;
	int rc;

	vf = qed_iov_get_vf_info(p_hwfn, (u16)vfid, true);
	if (!vf)
		return -EINVAL;

	rc = qed_fw_vport(p_hwfn, vf->vport_id, &abs_vp_id);
	if (rc)
		return rc;

	return qed_init_vport_rl(p_hwfn, p_ptt, abs_vp_id, (u32)val);
}

int qed_iov_configure_min_tx_rate(struct qed_dev *cdev, int vfid, u32 rate)
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

			if (!qed_iov_is_valid_vfid(hwfn, j, true))
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

	qed_iov_set_vfs_to_disable(cdev, false);

	return 0;
}

static int qed_sriov_enable(struct qed_dev *cdev, int num)
{
	struct qed_sb_cnt_info sb_cnt_info;
	int i, j, rc;

	if (num >= RESC_NUM(&cdev->hwfns[0], QED_VPORT)) {
		DP_NOTICE(cdev, "Can start at most %d VFs\n",
			  RESC_NUM(&cdev->hwfns[0], QED_VPORT) - 1);
		return -EINVAL;
	}

	/* Initialize HW for VF access */
	for_each_hwfn(cdev, j) {
		struct qed_hwfn *hwfn = &cdev->hwfns[j];
		struct qed_ptt *ptt = qed_ptt_acquire(hwfn);
		int num_sbs = 0, limit = 16;

		if (!ptt) {
			DP_ERR(hwfn, "Failed to acquire ptt\n");
			rc = -EBUSY;
			goto err;
		}

		if (IS_MF_DEFAULT(hwfn))
			limit = MAX_NUM_VFS_BB / hwfn->num_funcs_on_engine;

		memset(&sb_cnt_info, 0, sizeof(sb_cnt_info));
		qed_int_get_num_sbs(hwfn, &sb_cnt_info);
		num_sbs = min_t(int, sb_cnt_info.sb_free_blk, limit);

		for (i = 0; i < num; i++) {
			if (!qed_iov_is_valid_vfid(hwfn, i, false))
				continue;

			rc = qed_iov_init_hw_for_vf(hwfn,
						    ptt, i, num_sbs / num);
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

	if (!qed_iov_is_valid_vfid(&cdev->hwfns[0], vfid, true)) {
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

		/* Set the forced MAC, and schedule the IOV task */
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

	if (!qed_iov_is_valid_vfid(&cdev->hwfns[0], vfid, true)) {
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

	/* Sanitize request */
	if (IS_VF(cdev))
		return -EINVAL;

	if (!qed_iov_is_valid_vfid(&cdev->hwfns[0], vf_id, true)) {
		DP_VERBOSE(cdev, QED_MSG_IOV,
			   "VF index [%d] isn't active\n", vf_id);
		return -EINVAL;
	}

	vf_info = qed_iov_get_public_vf_info(hwfn, vf_id, true);

	qed_iov_get_link(hwfn, vf_id, NULL, &link, NULL);

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

	return 0;
}

void qed_inform_vf_link_state(struct qed_hwfn *hwfn)
{
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

		memcpy(&params, qed_mcp_get_link_params(hwfn), sizeof(params));
		memcpy(&link, qed_mcp_get_link_state(hwfn), sizeof(link));
		memcpy(&caps, qed_mcp_get_link_capabilities(hwfn),
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

	if (!qed_iov_is_valid_vfid(&cdev->hwfns[0], vf_id, true)) {
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
		mac = qed_iov_bulletin_get_forced_mac(hwfn, i);
		if (is_valid_ether_addr(info->forced_mac) &&
		    (!mac || !ether_addr_equal(mac, info->forced_mac))) {
			DP_VERBOSE(hwfn,
				   QED_MSG_IOV,
				   "Handling PF setting of VF MAC to VF 0x%02x [Abs 0x%02x]\n",
				   i,
				   hwfn->cdev->p_iov_info->first_vf_in_pf + i);

			/* Update bulletin board with forced MAC */
			qed_iov_bulletin_set_forced_mac(hwfn,
							info->forced_mac, i);
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

void qed_iov_pf_task(struct work_struct *work)
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
};
