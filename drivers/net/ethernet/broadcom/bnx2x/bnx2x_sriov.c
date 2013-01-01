/* bnx2x_sriov.c: Broadcom Everest network driver.
 *
 * Copyright 2009-2012 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Shmulik Ravid <shmulikr@broadcom.com>
 *	       Ariel Elior <ariele@broadcom.com>
 *
 */
#include "bnx2x.h"
#include "bnx2x_init.h"
#include "bnx2x_sriov.h"
int bnx2x_vf_idx_by_abs_fid(struct bnx2x *bp, u16 abs_vfid)
{
	int idx;

	for_each_vf(bp, idx)
		if (bnx2x_vf(bp, idx, abs_vfid) == abs_vfid)
			break;
	return idx;
}

static
struct bnx2x_virtf *bnx2x_vf_by_abs_fid(struct bnx2x *bp, u16 abs_vfid)
{
	u16 idx =  (u16)bnx2x_vf_idx_by_abs_fid(bp, abs_vfid);
	return (idx < BNX2X_NR_VIRTFN(bp)) ? BP_VF(bp, idx) : NULL;
}

static int bnx2x_ari_enabled(struct pci_dev *dev)
{
	return dev->bus->self && dev->bus->self->ari_enabled;
}

static void
bnx2x_vf_set_igu_info(struct bnx2x *bp, u8 igu_sb_id, u8 abs_vfid)
{
	struct bnx2x_virtf *vf = bnx2x_vf_by_abs_fid(bp, abs_vfid);
	if (vf) {
		if (!vf_sb_count(vf))
			vf->igu_base_id = igu_sb_id;
		++vf_sb_count(vf);
	}
}

static void
bnx2x_get_vf_igu_cam_info(struct bnx2x *bp)
{
	int sb_id;
	u32 val;
	u8 fid;

	/* IGU in normal mode - read CAM */
	for (sb_id = 0; sb_id < IGU_REG_MAPPING_MEMORY_SIZE; sb_id++) {
		val = REG_RD(bp, IGU_REG_MAPPING_MEMORY + sb_id * 4);
		if (!(val & IGU_REG_MAPPING_MEMORY_VALID))
			continue;
		fid = GET_FIELD((val), IGU_REG_MAPPING_MEMORY_FID);
		if (!(fid & IGU_FID_ENCODE_IS_PF))
			bnx2x_vf_set_igu_info(bp, sb_id,
					      (fid & IGU_FID_VF_NUM_MASK));

		DP(BNX2X_MSG_IOV, "%s[%d], igu_sb_id=%d, msix=%d\n",
		   ((fid & IGU_FID_ENCODE_IS_PF) ? "PF" : "VF"),
		   ((fid & IGU_FID_ENCODE_IS_PF) ? (fid & IGU_FID_PF_NUM_MASK) :
		   (fid & IGU_FID_VF_NUM_MASK)), sb_id,
		   GET_FIELD((val), IGU_REG_MAPPING_MEMORY_VECTOR));
	}
}

static void __bnx2x_iov_free_vfdb(struct bnx2x *bp)
{
	if (bp->vfdb) {
		kfree(bp->vfdb->vfqs);
		kfree(bp->vfdb->vfs);
		kfree(bp->vfdb);
	}
	bp->vfdb = NULL;
}

static int bnx2x_sriov_pci_cfg_info(struct bnx2x *bp, struct bnx2x_sriov *iov)
{
	int pos;
	struct pci_dev *dev = bp->pdev;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV);
	if (!pos) {
		BNX2X_ERR("failed to find SRIOV capability in device\n");
		return -ENODEV;
	}

	iov->pos = pos;
	DP(BNX2X_MSG_IOV, "sriov ext pos %d\n", pos);
	pci_read_config_word(dev, pos + PCI_SRIOV_CTRL, &iov->ctrl);
	pci_read_config_word(dev, pos + PCI_SRIOV_TOTAL_VF, &iov->total);
	pci_read_config_word(dev, pos + PCI_SRIOV_INITIAL_VF, &iov->initial);
	pci_read_config_word(dev, pos + PCI_SRIOV_VF_OFFSET, &iov->offset);
	pci_read_config_word(dev, pos + PCI_SRIOV_VF_STRIDE, &iov->stride);
	pci_read_config_dword(dev, pos + PCI_SRIOV_SUP_PGSIZE, &iov->pgsz);
	pci_read_config_dword(dev, pos + PCI_SRIOV_CAP, &iov->cap);
	pci_read_config_byte(dev, pos + PCI_SRIOV_FUNC_LINK, &iov->link);

	return 0;
}

static int bnx2x_sriov_info(struct bnx2x *bp, struct bnx2x_sriov *iov)
{
	u32 val;

	/* read the SRIOV capability structure
	 * The fields can be read via configuration read or
	 * directly from the device (starting at offset PCICFG_OFFSET)
	 */
	if (bnx2x_sriov_pci_cfg_info(bp, iov))
		return -ENODEV;

	/* get the number of SRIOV bars */
	iov->nres = 0;

	/* read the first_vfid */
	val = REG_RD(bp, PCICFG_OFFSET + GRC_CONFIG_REG_PF_INIT_VF);
	iov->first_vf_in_pf = ((val & GRC_CR_PF_INIT_VF_PF_FIRST_VF_NUM_MASK)
			       * 8) - (BNX2X_MAX_NUM_OF_VFS * BP_PATH(bp));

	DP(BNX2X_MSG_IOV,
	   "IOV info[%d]: first vf %d, nres %d, cap 0x%x, ctrl 0x%x, total %d, initial %d, num vfs %d, offset %d, stride %d, page size 0x%x\n",
	   BP_FUNC(bp),
	   iov->first_vf_in_pf, iov->nres, iov->cap, iov->ctrl, iov->total,
	   iov->initial, iov->nr_virtfn, iov->offset, iov->stride, iov->pgsz);

	return 0;
}

static u8 bnx2x_iov_get_max_queue_count(struct bnx2x *bp)
{
	int i;
	u8 queue_count = 0;

	if (IS_SRIOV(bp))
		for_each_vf(bp, i)
			queue_count += bnx2x_vf(bp, i, alloc_resc.num_sbs);

	return queue_count;
}

/* must be called after PF bars are mapped */
int bnx2x_iov_init_one(struct bnx2x *bp, int int_mode_param,
				 int num_vfs_param)
{
	int err, i, qcount;
	struct bnx2x_sriov *iov;
	struct pci_dev *dev = bp->pdev;

	bp->vfdb = NULL;

	/* verify sriov capability is present in configuration space */
	if (!pci_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV)) {
		DP(BNX2X_MSG_IOV, "no sriov - capability not found\n");
		return 0;
	}

	/* verify is pf */
	if (IS_VF(bp))
		return 0;

	/* verify chip revision */
	if (CHIP_IS_E1x(bp))
		return 0;

	/* check if SRIOV support is turned off */
	if (!num_vfs_param)
		return 0;

	/* SRIOV assumes that num of PF CIDs < BNX2X_FIRST_VF_CID */
	if (BNX2X_L2_MAX_CID(bp) >= BNX2X_FIRST_VF_CID) {
		BNX2X_ERR("PF cids %d are overspilling into vf space (starts at %d). Abort SRIOV\n",
			  BNX2X_L2_MAX_CID(bp), BNX2X_FIRST_VF_CID);
		return 0;
	}

	/* SRIOV can be enabled only with MSIX */
	if (int_mode_param == BNX2X_INT_MODE_MSI ||
	    int_mode_param == BNX2X_INT_MODE_INTX) {
		BNX2X_ERR("Forced MSI/INTx mode is incompatible with SRIOV\n");
		return 0;
	}

	/* verify ari is enabled */
	if (!bnx2x_ari_enabled(bp->pdev)) {
		BNX2X_ERR("ARI not supported, SRIOV can not be enabled\n");
		return 0;
	}

	/* verify igu is in normal mode */
	if (CHIP_INT_MODE_IS_BC(bp)) {
		BNX2X_ERR("IGU not normal mode,  SRIOV can not be enabled\n");
		return 0;
	}

	/* allocate the vfs database */
	bp->vfdb = kzalloc(sizeof(*(bp->vfdb)), GFP_KERNEL);
	if (!bp->vfdb) {
		BNX2X_ERR("failed to allocate vf database\n");
		err = -ENOMEM;
		goto failed;
	}

	/* get the sriov info - Linux already collected all the pertinent
	 * information, however the sriov structure is for the private use
	 * of the pci module. Also we want this information regardless
	 * of the hyper-visor.
	 */
	iov = &(bp->vfdb->sriov);
	err = bnx2x_sriov_info(bp, iov);
	if (err)
		goto failed;

	/* SR-IOV capability was enabled but there are no VFs*/
	if (iov->total == 0)
		goto failed;

	/* calcuate the actual number of VFs */
	iov->nr_virtfn = min_t(u16, iov->total, (u16)num_vfs_param);

	/* allcate the vf array */
	bp->vfdb->vfs = kzalloc(sizeof(struct bnx2x_virtf) *
				BNX2X_NR_VIRTFN(bp), GFP_KERNEL);
	if (!bp->vfdb->vfs) {
		BNX2X_ERR("failed to allocate vf array\n");
		err = -ENOMEM;
		goto failed;
	}

	/* Initial VF init - index and abs_vfid - nr_virtfn must be set */
	for_each_vf(bp, i) {
		bnx2x_vf(bp, i, index) = i;
		bnx2x_vf(bp, i, abs_vfid) = iov->first_vf_in_pf + i;
		bnx2x_vf(bp, i, state) = VF_FREE;
		INIT_LIST_HEAD(&bnx2x_vf(bp, i, op_list_head));
		mutex_init(&bnx2x_vf(bp, i, op_mutex));
		bnx2x_vf(bp, i, op_current) = CHANNEL_TLV_NONE;
	}

	/* re-read the IGU CAM for VFs - index and abs_vfid must be set */
	bnx2x_get_vf_igu_cam_info(bp);

	/* get the total queue count and allocate the global queue arrays */
	qcount = bnx2x_iov_get_max_queue_count(bp);

	/* allocate the queue arrays for all VFs */
	bp->vfdb->vfqs = kzalloc(qcount * sizeof(struct bnx2x_vf_queue),
				 GFP_KERNEL);
	if (!bp->vfdb->vfqs) {
		BNX2X_ERR("failed to allocate vf queue array\n");
		err = -ENOMEM;
		goto failed;
	}

	return 0;
failed:
	DP(BNX2X_MSG_IOV, "Failed err=%d\n", err);
	__bnx2x_iov_free_vfdb(bp);
	return err;
}

/* called by bnx2x_init_hw_func, returns the next ilt line */
int bnx2x_iov_init_ilt(struct bnx2x *bp, u16 line)
{
	int i;
	struct bnx2x_ilt *ilt = BP_ILT(bp);

	if (!IS_SRIOV(bp))
		return line;

	/* set vfs ilt lines */
	for (i = 0; i < BNX2X_VF_CIDS/ILT_PAGE_CIDS; i++) {
		struct hw_dma *hw_cxt = BP_VF_CXT_PAGE(bp, i);

		ilt->lines[line+i].page = hw_cxt->addr;
		ilt->lines[line+i].page_mapping = hw_cxt->mapping;
		ilt->lines[line+i].size = hw_cxt->size; /* doesn't matter */
	}
	return line + i;
}

void bnx2x_iov_remove_one(struct bnx2x *bp)
{
	/* if SRIOV is not enabled there's nothing to do */
	if (!IS_SRIOV(bp))
		return;

	/* free vf database */
	__bnx2x_iov_free_vfdb(bp);
}
