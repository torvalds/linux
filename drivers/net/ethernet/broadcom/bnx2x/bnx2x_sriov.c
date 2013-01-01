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
#include "bnx2x_cmn.h"
#include "bnx2x_sriov.h"

/* General service functions */
static void storm_memset_vf_to_pf(struct bnx2x *bp, u16 abs_fid,
					 u16 pf_id)
{
	REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
	REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_VF_TO_PF_OFFSET(abs_fid),
		pf_id);
}

static void storm_memset_func_en(struct bnx2x *bp, u16 abs_fid,
					u8 enable)
{
	REG_WR8(bp, BAR_XSTRORM_INTMEM + XSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_CSTRORM_INTMEM + CSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_TSTRORM_INTMEM + TSTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
	REG_WR8(bp, BAR_USTRORM_INTMEM + USTORM_FUNC_EN_OFFSET(abs_fid),
		enable);
}

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

static void bnx2x_vf_igu_ack_sb(struct bnx2x *bp, struct bnx2x_virtf *vf,
				u8 igu_sb_id, u8 segment, u16 index, u8 op,
				u8 update)
{
	/* acking a VF sb through the PF - use the GRC */
	u32 ctl;
	u32 igu_addr_data = IGU_REG_COMMAND_REG_32LSB_DATA;
	u32 igu_addr_ctl = IGU_REG_COMMAND_REG_CTRL;
	u32 func_encode = vf->abs_vfid;
	u32 addr_encode = IGU_CMD_E2_PROD_UPD_BASE + igu_sb_id;
	struct igu_regular cmd_data = {0};

	cmd_data.sb_id_and_flags =
			((index << IGU_REGULAR_SB_INDEX_SHIFT) |
			 (segment << IGU_REGULAR_SEGMENT_ACCESS_SHIFT) |
			 (update << IGU_REGULAR_BUPDATE_SHIFT) |
			 (op << IGU_REGULAR_ENABLE_INT_SHIFT));

	ctl = addr_encode << IGU_CTRL_REG_ADDRESS_SHIFT		|
	      func_encode << IGU_CTRL_REG_FID_SHIFT		|
	      IGU_CTRL_CMD_TYPE_WR << IGU_CTRL_REG_TYPE_SHIFT;

	DP(NETIF_MSG_HW, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
	   cmd_data.sb_id_and_flags, igu_addr_data);
	REG_WR(bp, igu_addr_data, cmd_data.sb_id_and_flags);
	mmiowb();
	barrier();

	DP(NETIF_MSG_HW, "write 0x%08x to IGU(via GRC) addr 0x%x\n",
	   ctl, igu_addr_ctl);
	REG_WR(bp, igu_addr_ctl, ctl);
	mmiowb();
	barrier();
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
/* VF enable primitives
 * when pretend is required the caller is responsible
 * for calling pretend prior to calling these routines
 */

/* called only on E1H or E2.
 * When pretending to be PF, the pretend value is the function number 0...7
 * When pretending to be VF, the pretend val is the PF-num:VF-valid:ABS-VFID
 * combination
 */
int bnx2x_pretend_func(struct bnx2x *bp, u16 pretend_func_val)
{
	u32 pretend_reg;

	if (CHIP_IS_E1H(bp) && pretend_func_val > E1H_FUNC_MAX)
		return -1;

	/* get my own pretend register */
	pretend_reg = bnx2x_get_pretend_reg(bp);
	REG_WR(bp, pretend_reg, pretend_func_val);
	REG_RD(bp, pretend_reg);
	return 0;
}

/* internal vf enable - until vf is enabled internally all transactions
 * are blocked. this routine should always be called last with pretend.
 */
static void bnx2x_vf_enable_internal(struct bnx2x *bp, u8 enable)
{
	REG_WR(bp, PGLUE_B_REG_INTERNAL_VFID_ENABLE, enable ? 1 : 0);
}

/* clears vf error in all semi blocks */
static void bnx2x_vf_semi_clear_err(struct bnx2x *bp, u8 abs_vfid)
{
	REG_WR(bp, TSEM_REG_VFPF_ERR_NUM, abs_vfid);
	REG_WR(bp, USEM_REG_VFPF_ERR_NUM, abs_vfid);
	REG_WR(bp, CSEM_REG_VFPF_ERR_NUM, abs_vfid);
	REG_WR(bp, XSEM_REG_VFPF_ERR_NUM, abs_vfid);
}

static void bnx2x_vf_pglue_clear_err(struct bnx2x *bp, u8 abs_vfid)
{
	u32 was_err_group = (2 * BP_PATH(bp) + abs_vfid) >> 5;
	u32 was_err_reg = 0;

	switch (was_err_group) {
	case 0:
	    was_err_reg = PGLUE_B_REG_WAS_ERROR_VF_31_0_CLR;
	    break;
	case 1:
	    was_err_reg = PGLUE_B_REG_WAS_ERROR_VF_63_32_CLR;
	    break;
	case 2:
	    was_err_reg = PGLUE_B_REG_WAS_ERROR_VF_95_64_CLR;
	    break;
	case 3:
	    was_err_reg = PGLUE_B_REG_WAS_ERROR_VF_127_96_CLR;
	    break;
	}
	REG_WR(bp, was_err_reg, 1 << (abs_vfid & 0x1f));
}

static void bnx2x_vf_igu_reset(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int i;
	u32 val;

	/* Set VF masks and configuration - pretend */
	bnx2x_pretend_func(bp, HW_VF_HANDLE(bp, vf->abs_vfid));

	REG_WR(bp, IGU_REG_SB_INT_BEFORE_MASK_LSB, 0);
	REG_WR(bp, IGU_REG_SB_INT_BEFORE_MASK_MSB, 0);
	REG_WR(bp, IGU_REG_SB_MASK_LSB, 0);
	REG_WR(bp, IGU_REG_SB_MASK_MSB, 0);
	REG_WR(bp, IGU_REG_PBA_STATUS_LSB, 0);
	REG_WR(bp, IGU_REG_PBA_STATUS_MSB, 0);

	val = REG_RD(bp, IGU_REG_VF_CONFIGURATION);
	val |= (IGU_VF_CONF_FUNC_EN | IGU_VF_CONF_MSI_MSIX_EN);
	if (vf->cfg_flags & VF_CFG_INT_SIMD)
		val |= IGU_VF_CONF_SINGLE_ISR_EN;
	val &= ~IGU_VF_CONF_PARENT_MASK;
	val |= BP_FUNC(bp) << IGU_VF_CONF_PARENT_SHIFT;	/* parent PF */
	REG_WR(bp, IGU_REG_VF_CONFIGURATION, val);

	DP(BNX2X_MSG_IOV,
	   "value in IGU_REG_VF_CONFIGURATION of vf %d after write %x\n",
	   vf->abs_vfid, REG_RD(bp, IGU_REG_VF_CONFIGURATION));

	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));

	/* iterate over all queues, clear sb consumer */
	for (i = 0; i < vf_sb_count(vf); i++) {
		u8 igu_sb_id = vf_igu_sb(vf, i);

		/* zero prod memory */
		REG_WR(bp, IGU_REG_PROD_CONS_MEMORY + igu_sb_id * 4, 0);

		/* clear sb state machine */
		bnx2x_igu_clear_sb_gen(bp, vf->abs_vfid, igu_sb_id,
				       false /* VF */);

		/* disable + update */
		bnx2x_vf_igu_ack_sb(bp, vf, igu_sb_id, USTORM_ID, 0,
				    IGU_INT_DISABLE, 1);
	}
}

void bnx2x_vf_enable_access(struct bnx2x *bp, u8 abs_vfid)
{
	/* set the VF-PF association in the FW */
	storm_memset_vf_to_pf(bp, FW_VF_HANDLE(abs_vfid), BP_FUNC(bp));
	storm_memset_func_en(bp, FW_VF_HANDLE(abs_vfid), 1);

	/* clear vf errors*/
	bnx2x_vf_semi_clear_err(bp, abs_vfid);
	bnx2x_vf_pglue_clear_err(bp, abs_vfid);

	/* internal vf-enable - pretend */
	bnx2x_pretend_func(bp, HW_VF_HANDLE(bp, abs_vfid));
	DP(BNX2X_MSG_IOV, "enabling internal access for vf %x\n", abs_vfid);
	bnx2x_vf_enable_internal(bp, true);
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));
}

static void bnx2x_vf_enable_traffic(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	/* Reset vf in IGU  interrupts are still disabled */
	bnx2x_vf_igu_reset(bp, vf);

	/* pretend to enable the vf with the PBF */
	bnx2x_pretend_func(bp, HW_VF_HANDLE(bp, vf->abs_vfid));
	REG_WR(bp, PBF_REG_DISABLE_VF, 0);
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));
}

static u8 bnx2x_vf_is_pcie_pending(struct bnx2x *bp, u8 abs_vfid)
{
	struct pci_dev *dev;
	struct bnx2x_virtf *vf = bnx2x_vf_by_abs_fid(bp, abs_vfid);

	if (!vf)
		goto unknown_dev;

	dev = pci_get_bus_and_slot(vf->bus, vf->devfn);
	if (dev)
		return bnx2x_is_pcie_pending(dev);

unknown_dev:
	BNX2X_ERR("Unknown device\n");
	return false;
}

int bnx2x_vf_flr_clnup_epilog(struct bnx2x *bp, u8 abs_vfid)
{
	/* Wait 100ms */
	msleep(100);

	/* Verify no pending pci transactions */
	if (bnx2x_vf_is_pcie_pending(bp, abs_vfid))
		BNX2X_ERR("PCIE Transactions still pending\n");

	return 0;
}

/* must be called after the number of PF queues and the number of VFs are
 * both known
 */
static void
bnx2x_iov_static_resc(struct bnx2x *bp, struct vf_pf_resc_request *resc)
{
	u16 vlan_count = 0;

	/* will be set only during VF-ACQUIRE */
	resc->num_rxqs = 0;
	resc->num_txqs = 0;

	/* no credit calculcis for macs (just yet) */
	resc->num_mac_filters = 1;

	/* divvy up vlan rules */
	vlan_count = bp->vlans_pool.check(&bp->vlans_pool);
	vlan_count = 1 << ilog2(vlan_count);
	resc->num_vlan_filters = vlan_count / BNX2X_NR_VIRTFN(bp);

	/* no real limitation */
	resc->num_mc_filters = 0;

	/* num_sbs already set */
}

/* IOV global initialization routines  */
void bnx2x_iov_init_dq(struct bnx2x *bp)
{
	if (!IS_SRIOV(bp))
		return;

	/* Set the DQ such that the CID reflect the abs_vfid */
	REG_WR(bp, DORQ_REG_VF_NORM_VF_BASE, 0);
	REG_WR(bp, DORQ_REG_MAX_RVFID_SIZE, ilog2(BNX2X_MAX_NUM_OF_VFS));

	/* Set VFs starting CID. If its > 0 the preceding CIDs are belong to
	 * the PF L2 queues
	 */
	REG_WR(bp, DORQ_REG_VF_NORM_CID_BASE, BNX2X_FIRST_VF_CID);

	/* The VF window size is the log2 of the max number of CIDs per VF */
	REG_WR(bp, DORQ_REG_VF_NORM_CID_WND_SIZE, BNX2X_VF_CID_WND);

	/* The VF doorbell size  0 - *B, 4 - 128B. We set it here to match
	 * the Pf doorbell size although the 2 are independent.
	 */
	REG_WR(bp, DORQ_REG_VF_NORM_CID_OFST,
	       BNX2X_DB_SHIFT - BNX2X_DB_MIN_SHIFT);

	/* No security checks for now -
	 * configure single rule (out of 16) mask = 0x1, value = 0x0,
	 * CID range 0 - 0x1ffff
	 */
	REG_WR(bp, DORQ_REG_VF_TYPE_MASK_0, 1);
	REG_WR(bp, DORQ_REG_VF_TYPE_VALUE_0, 0);
	REG_WR(bp, DORQ_REG_VF_TYPE_MIN_MCID_0, 0);
	REG_WR(bp, DORQ_REG_VF_TYPE_MAX_MCID_0, 0x1ffff);

	/* set the number of VF alllowed doorbells to the full DQ range */
	REG_WR(bp, DORQ_REG_VF_NORM_MAX_CID_COUNT, 0x20000);

	/* set the VF doorbell threshold */
	REG_WR(bp, DORQ_REG_VF_USAGE_CT_LIMIT, 4);
}

void bnx2x_iov_init_dmae(struct bnx2x *bp)
{
	DP(BNX2X_MSG_IOV, "SRIOV is %s\n", IS_SRIOV(bp) ? "ON" : "OFF");
	if (!IS_SRIOV(bp))
		return;

	REG_WR(bp, DMAE_REG_BACKWARD_COMP_EN, 0);
}

static int bnx2x_vf_bus(struct bnx2x *bp, int vfid)
{
	struct pci_dev *dev = bp->pdev;
	struct bnx2x_sriov *iov = &bp->vfdb->sriov;

	return dev->bus->number + ((dev->devfn + iov->offset +
				    iov->stride * vfid) >> 8);
}

static int bnx2x_vf_devfn(struct bnx2x *bp, int vfid)
{
	struct pci_dev *dev = bp->pdev;
	struct bnx2x_sriov *iov = &bp->vfdb->sriov;

	return (dev->devfn + iov->offset + iov->stride * vfid) & 0xff;
}

static void bnx2x_vf_set_bars(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	int i, n;
	struct pci_dev *dev = bp->pdev;
	struct bnx2x_sriov *iov = &bp->vfdb->sriov;

	for (i = 0, n = 0; i < PCI_SRIOV_NUM_BARS; i += 2, n++) {
		u64 start = pci_resource_start(dev, PCI_IOV_RESOURCES + i);
		u32 size = pci_resource_len(dev, PCI_IOV_RESOURCES + i);

		do_div(size, iov->total);
		vf->bars[n].bar = start + size * vf->abs_vfid;
		vf->bars[n].size = size;
	}
}

void bnx2x_iov_remove_one(struct bnx2x *bp)
{
	/* if SRIOV is not enabled there's nothing to do */
	if (!IS_SRIOV(bp))
		return;

	/* free vf database */
	__bnx2x_iov_free_vfdb(bp);
}

void bnx2x_iov_free_mem(struct bnx2x *bp)
{
	int i;

	if (!IS_SRIOV(bp))
		return;

	/* free vfs hw contexts */
	for (i = 0; i < BNX2X_VF_CIDS/ILT_PAGE_CIDS; i++) {
		struct hw_dma *cxt = &bp->vfdb->context[i];
		BNX2X_PCI_FREE(cxt->addr, cxt->mapping, cxt->size);
	}

	BNX2X_PCI_FREE(BP_VFDB(bp)->sp_dma.addr,
		       BP_VFDB(bp)->sp_dma.mapping,
		       BP_VFDB(bp)->sp_dma.size);

	BNX2X_PCI_FREE(BP_VF_MBX_DMA(bp)->addr,
		       BP_VF_MBX_DMA(bp)->mapping,
		       BP_VF_MBX_DMA(bp)->size);
}

int bnx2x_iov_alloc_mem(struct bnx2x *bp)
{
	size_t tot_size;
	int i, rc = 0;

	if (!IS_SRIOV(bp))
		return rc;

	/* allocate vfs hw contexts */
	tot_size = (BP_VFDB(bp)->sriov.first_vf_in_pf + BNX2X_NR_VIRTFN(bp)) *
		BNX2X_CIDS_PER_VF * sizeof(union cdu_context);

	for (i = 0; i < BNX2X_VF_CIDS/ILT_PAGE_CIDS; i++) {
		struct hw_dma *cxt = BP_VF_CXT_PAGE(bp, i);
		cxt->size = min_t(size_t, tot_size, CDU_ILT_PAGE_SZ);

		if (cxt->size) {
			BNX2X_PCI_ALLOC(cxt->addr, &cxt->mapping, cxt->size);
		} else {
			cxt->addr = NULL;
			cxt->mapping = 0;
		}
		tot_size -= cxt->size;
	}

	/* allocate vfs ramrods dma memory - client_init and set_mac */
	tot_size = BNX2X_NR_VIRTFN(bp) * sizeof(struct bnx2x_vf_sp);
	BNX2X_PCI_ALLOC(BP_VFDB(bp)->sp_dma.addr, &BP_VFDB(bp)->sp_dma.mapping,
			tot_size);
	BP_VFDB(bp)->sp_dma.size = tot_size;

	/* allocate mailboxes */
	tot_size = BNX2X_NR_VIRTFN(bp) * MBX_MSG_ALIGNED_SIZE;
	BNX2X_PCI_ALLOC(BP_VF_MBX_DMA(bp)->addr, &BP_VF_MBX_DMA(bp)->mapping,
			tot_size);
	BP_VF_MBX_DMA(bp)->size = tot_size;

	return 0;

alloc_mem_err:
	return -ENOMEM;
}

static void bnx2x_vfq_init(struct bnx2x *bp, struct bnx2x_virtf *vf,
			   struct bnx2x_vf_queue *q)
{
	u8 cl_id = vfq_cl_id(vf, q);
	u8 func_id = FW_VF_HANDLE(vf->abs_vfid);
	unsigned long q_type = 0;

	set_bit(BNX2X_Q_TYPE_HAS_TX, &q_type);
	set_bit(BNX2X_Q_TYPE_HAS_RX, &q_type);

	/* Queue State object */
	bnx2x_init_queue_obj(bp, &q->sp_obj,
			     cl_id, &q->cid, 1, func_id,
			     bnx2x_vf_sp(bp, vf, q_data),
			     bnx2x_vf_sp_map(bp, vf, q_data),
			     q_type);

	DP(BNX2X_MSG_IOV,
	   "initialized vf %d's queue object. func id set to %d\n",
	   vf->abs_vfid, q->sp_obj.func_id);

	/* mac/vlan objects are per queue, but only those
	 * that belong to the leading queue are initialized
	 */
	if (vfq_is_leading(q)) {
		/* mac */
		bnx2x_init_mac_obj(bp, &q->mac_obj,
				   cl_id, q->cid, func_id,
				   bnx2x_vf_sp(bp, vf, mac_rdata),
				   bnx2x_vf_sp_map(bp, vf, mac_rdata),
				   BNX2X_FILTER_MAC_PENDING,
				   &vf->filter_state,
				   BNX2X_OBJ_TYPE_RX_TX,
				   &bp->macs_pool);
		/* vlan */
		bnx2x_init_vlan_obj(bp, &q->vlan_obj,
				    cl_id, q->cid, func_id,
				    bnx2x_vf_sp(bp, vf, vlan_rdata),
				    bnx2x_vf_sp_map(bp, vf, vlan_rdata),
				    BNX2X_FILTER_VLAN_PENDING,
				    &vf->filter_state,
				    BNX2X_OBJ_TYPE_RX_TX,
				    &bp->vlans_pool);

		/* mcast */
		bnx2x_init_mcast_obj(bp, &vf->mcast_obj, cl_id,
				     q->cid, func_id, func_id,
				     bnx2x_vf_sp(bp, vf, mcast_rdata),
				     bnx2x_vf_sp_map(bp, vf, mcast_rdata),
				     BNX2X_FILTER_MCAST_PENDING,
				     &vf->filter_state,
				     BNX2X_OBJ_TYPE_RX_TX);

		vf->leading_rss = cl_id;
	}
}

/* called by bnx2x_nic_load */
int bnx2x_iov_nic_init(struct bnx2x *bp)
{
	int vfid, qcount, i;

	if (!IS_SRIOV(bp)) {
		DP(BNX2X_MSG_IOV, "vfdb was not allocated\n");
		return 0;
	}

	DP(BNX2X_MSG_IOV, "num of vfs: %d\n", (bp)->vfdb->sriov.nr_virtfn);

	/* initialize vf database */
	for_each_vf(bp, vfid) {
		struct bnx2x_virtf *vf = BP_VF(bp, vfid);

		int base_vf_cid = (BP_VFDB(bp)->sriov.first_vf_in_pf + vfid) *
			BNX2X_CIDS_PER_VF;

		union cdu_context *base_cxt = (union cdu_context *)
			BP_VF_CXT_PAGE(bp, base_vf_cid/ILT_PAGE_CIDS)->addr +
			(base_vf_cid & (ILT_PAGE_CIDS-1));

		DP(BNX2X_MSG_IOV,
		   "VF[%d] Max IGU SBs: %d, base vf cid 0x%x, base cid 0x%x, base cxt %p\n",
		   vf->abs_vfid, vf_sb_count(vf), base_vf_cid,
		   BNX2X_FIRST_VF_CID + base_vf_cid, base_cxt);

		/* init statically provisioned resources */
		bnx2x_iov_static_resc(bp, &vf->alloc_resc);

		/* queues are initialized during VF-ACQUIRE */

		/* reserve the vf vlan credit */
		bp->vlans_pool.get(&bp->vlans_pool, vf_vlan_rules_cnt(vf));

		vf->filter_state = 0;
		vf->sp_cl_id = bnx2x_fp(bp, 0, cl_id);

		/*  init mcast object - This object will be re-initialized
		 *  during VF-ACQUIRE with the proper cl_id and cid.
		 *  It needs to be initialized here so that it can be safely
		 *  handled by a subsequent FLR flow.
		 */
		bnx2x_init_mcast_obj(bp, &vf->mcast_obj, 0xFF,
				     0xFF, 0xFF, 0xFF,
				     bnx2x_vf_sp(bp, vf, mcast_rdata),
				     bnx2x_vf_sp_map(bp, vf, mcast_rdata),
				     BNX2X_FILTER_MCAST_PENDING,
				     &vf->filter_state,
				     BNX2X_OBJ_TYPE_RX_TX);

		/* set the mailbox message addresses */
		BP_VF_MBX(bp, vfid)->msg = (struct bnx2x_vf_mbx_msg *)
			(((u8 *)BP_VF_MBX_DMA(bp)->addr) + vfid *
			MBX_MSG_ALIGNED_SIZE);

		BP_VF_MBX(bp, vfid)->msg_mapping = BP_VF_MBX_DMA(bp)->mapping +
			vfid * MBX_MSG_ALIGNED_SIZE;

		/* Enable vf mailbox */
		bnx2x_vf_enable_mbx(bp, vf->abs_vfid);
	}

	/* Final VF init */
	qcount = 0;
	for_each_vf(bp, i) {
		struct bnx2x_virtf *vf = BP_VF(bp, i);

		/* fill in the BDF and bars */
		vf->bus = bnx2x_vf_bus(bp, i);
		vf->devfn = bnx2x_vf_devfn(bp, i);
		bnx2x_vf_set_bars(bp, vf);

		DP(BNX2X_MSG_IOV,
		   "VF info[%d]: bus 0x%x, devfn 0x%x, bar0 [0x%x, %d], bar1 [0x%x, %d], bar2 [0x%x, %d]\n",
		   vf->abs_vfid, vf->bus, vf->devfn,
		   (unsigned)vf->bars[0].bar, vf->bars[0].size,
		   (unsigned)vf->bars[1].bar, vf->bars[1].size,
		   (unsigned)vf->bars[2].bar, vf->bars[2].size);

		/* set local queue arrays */
		vf->vfqs = &bp->vfdb->vfqs[qcount];
		qcount += bnx2x_vf(bp, i, alloc_resc.num_sbs);
	}

	return 0;
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

static u8 bnx2x_iov_is_vf_cid(struct bnx2x *bp, u16 cid)
{
	return ((cid >= BNX2X_FIRST_VF_CID) &&
		((cid - BNX2X_FIRST_VF_CID) < BNX2X_VF_CIDS));
}

static
void bnx2x_vf_handle_classification_eqe(struct bnx2x *bp,
					struct bnx2x_vf_queue *vfq,
					union event_ring_elem *elem)
{
	unsigned long ramrod_flags = 0;
	int rc = 0;

	/* Always push next commands out, don't wait here */
	set_bit(RAMROD_CONT, &ramrod_flags);

	switch (elem->message.data.eth_event.echo >> BNX2X_SWCID_SHIFT) {
	case BNX2X_FILTER_MAC_PENDING:
		rc = vfq->mac_obj.complete(bp, &vfq->mac_obj, elem,
					   &ramrod_flags);
		break;
	case BNX2X_FILTER_VLAN_PENDING:
		rc = vfq->vlan_obj.complete(bp, &vfq->vlan_obj, elem,
					    &ramrod_flags);
		break;
	default:
		BNX2X_ERR("Unsupported classification command: %d\n",
			  elem->message.data.eth_event.echo);
		return;
	}
	if (rc < 0)
		BNX2X_ERR("Failed to schedule new commands: %d\n", rc);
	else if (rc > 0)
		DP(BNX2X_MSG_IOV, "Scheduled next pending commands...\n");
}

static
void bnx2x_vf_handle_mcast_eqe(struct bnx2x *bp,
			       struct bnx2x_virtf *vf)
{
	struct bnx2x_mcast_ramrod_params rparam = {NULL};
	int rc;

	rparam.mcast_obj = &vf->mcast_obj;
	vf->mcast_obj.raw.clear_pending(&vf->mcast_obj.raw);

	/* If there are pending mcast commands - send them */
	if (vf->mcast_obj.check_pending(&vf->mcast_obj)) {
		rc = bnx2x_config_mcast(bp, &rparam, BNX2X_MCAST_CMD_CONT);
		if (rc < 0)
			BNX2X_ERR("Failed to send pending mcast commands: %d\n",
				  rc);
	}
}

static
void bnx2x_vf_handle_filters_eqe(struct bnx2x *bp,
				 struct bnx2x_virtf *vf)
{
	smp_mb__before_clear_bit();
	clear_bit(BNX2X_FILTER_RX_MODE_PENDING, &vf->filter_state);
	smp_mb__after_clear_bit();
}

int bnx2x_iov_eq_sp_event(struct bnx2x *bp, union event_ring_elem *elem)
{
	struct bnx2x_virtf *vf;
	int qidx = 0, abs_vfid;
	u8 opcode;
	u16 cid = 0xffff;

	if (!IS_SRIOV(bp))
		return 1;

	/* first get the cid - the only events we handle here are cfc-delete
	 * and set-mac completion
	 */
	opcode = elem->message.opcode;

	switch (opcode) {
	case EVENT_RING_OPCODE_CFC_DEL:
		cid = SW_CID((__force __le32)
			     elem->message.data.cfc_del_event.cid);
		DP(BNX2X_MSG_IOV, "checking cfc-del comp cid=%d\n", cid);
		break;
	case EVENT_RING_OPCODE_CLASSIFICATION_RULES:
	case EVENT_RING_OPCODE_MULTICAST_RULES:
	case EVENT_RING_OPCODE_FILTERS_RULES:
		cid = (elem->message.data.eth_event.echo &
		       BNX2X_SWCID_MASK);
		DP(BNX2X_MSG_IOV, "checking filtering comp cid=%d\n", cid);
		break;
	case EVENT_RING_OPCODE_VF_FLR:
		abs_vfid = elem->message.data.vf_flr_event.vf_id;
		DP(BNX2X_MSG_IOV, "Got VF FLR notification abs_vfid=%d\n",
		   abs_vfid);
		goto get_vf;
	case EVENT_RING_OPCODE_MALICIOUS_VF:
		abs_vfid = elem->message.data.malicious_vf_event.vf_id;
		DP(BNX2X_MSG_IOV, "Got VF MALICIOUS notification abs_vfid=%d\n",
		   abs_vfid);
		goto get_vf;
	default:
		return 1;
	}

	/* check if the cid is the VF range */
	if (!bnx2x_iov_is_vf_cid(bp, cid)) {
		DP(BNX2X_MSG_IOV, "cid is outside vf range: %d\n", cid);
		return 1;
	}

	/* extract vf and rxq index from vf_cid - relies on the following:
	 * 1. vfid on cid reflects the true abs_vfid
	 * 2. the max number of VFs (per path) is 64
	 */
	qidx = cid & ((1 << BNX2X_VF_CID_WND)-1);
	abs_vfid = (cid >> BNX2X_VF_CID_WND) & (BNX2X_MAX_NUM_OF_VFS-1);
get_vf:
	vf = bnx2x_vf_by_abs_fid(bp, abs_vfid);

	if (!vf) {
		BNX2X_ERR("EQ completion for unknown VF, cid %d, abs_vfid %d\n",
			  cid, abs_vfid);
		return 0;
	}

	switch (opcode) {
	case EVENT_RING_OPCODE_CFC_DEL:
		DP(BNX2X_MSG_IOV, "got VF [%d:%d] cfc delete ramrod\n",
		   vf->abs_vfid, qidx);
		vfq_get(vf, qidx)->sp_obj.complete_cmd(bp,
						       &vfq_get(vf,
								qidx)->sp_obj,
						       BNX2X_Q_CMD_CFC_DEL);
		break;
	case EVENT_RING_OPCODE_CLASSIFICATION_RULES:
		DP(BNX2X_MSG_IOV, "got VF [%d:%d] set mac/vlan ramrod\n",
		   vf->abs_vfid, qidx);
		bnx2x_vf_handle_classification_eqe(bp, vfq_get(vf, qidx), elem);
		break;
	case EVENT_RING_OPCODE_MULTICAST_RULES:
		DP(BNX2X_MSG_IOV, "got VF [%d:%d] set mcast ramrod\n",
		   vf->abs_vfid, qidx);
		bnx2x_vf_handle_mcast_eqe(bp, vf);
		break;
	case EVENT_RING_OPCODE_FILTERS_RULES:
		DP(BNX2X_MSG_IOV, "got VF [%d:%d] set rx-mode ramrod\n",
		   vf->abs_vfid, qidx);
		bnx2x_vf_handle_filters_eqe(bp, vf);
		break;
	case EVENT_RING_OPCODE_VF_FLR:
		DP(BNX2X_MSG_IOV, "got VF [%d] FLR notification\n",
		   vf->abs_vfid);
		/* Do nothing for now */
		break;
	case EVENT_RING_OPCODE_MALICIOUS_VF:
		DP(BNX2X_MSG_IOV, "got VF [%d] MALICIOUS notification\n",
		   vf->abs_vfid);
		/* Do nothing for now */
		break;
	}
	/* SRIOV: reschedule any 'in_progress' operations */
	bnx2x_iov_sp_event(bp, cid, false);

	return 0;
}

static struct bnx2x_virtf *bnx2x_vf_by_cid(struct bnx2x *bp, int vf_cid)
{
	/* extract the vf from vf_cid - relies on the following:
	 * 1. vfid on cid reflects the true abs_vfid
	 * 2. the max number of VFs (per path) is 64
	 */
	int abs_vfid = (vf_cid >> BNX2X_VF_CID_WND) & (BNX2X_MAX_NUM_OF_VFS-1);
	return bnx2x_vf_by_abs_fid(bp, abs_vfid);
}

void bnx2x_iov_set_queue_sp_obj(struct bnx2x *bp, int vf_cid,
				struct bnx2x_queue_sp_obj **q_obj)
{
	struct bnx2x_virtf *vf;

	if (!IS_SRIOV(bp))
		return;

	vf = bnx2x_vf_by_cid(bp, vf_cid);

	if (vf) {
		/* extract queue index from vf_cid - relies on the following:
		 * 1. vfid on cid reflects the true abs_vfid
		 * 2. the max number of VFs (per path) is 64
		 */
		int q_index = vf_cid & ((1 << BNX2X_VF_CID_WND)-1);
		*q_obj = &bnx2x_vfq(vf, q_index, sp_obj);
	} else {
		BNX2X_ERR("No vf matching cid %d\n", vf_cid);
	}
}

void bnx2x_iov_sp_event(struct bnx2x *bp, int vf_cid, bool queue_work)
{
	struct bnx2x_virtf *vf;

	/* check if the cid is the VF range */
	if (!IS_SRIOV(bp) || !bnx2x_iov_is_vf_cid(bp, vf_cid))
		return;

	vf = bnx2x_vf_by_cid(bp, vf_cid);
	if (vf) {
		/* set in_progress flag */
		atomic_set(&vf->op_in_progress, 1);
		if (queue_work)
			queue_delayed_work(bnx2x_wq, &bp->sp_task, 0);
	}
}

void bnx2x_iov_adjust_stats_req(struct bnx2x *bp)
{
	int i;
	int first_queue_query_index, num_queues_req;
	dma_addr_t cur_data_offset;
	struct stats_query_entry *cur_query_entry;
	u8 stats_count = 0;
	bool is_fcoe = false;

	if (!IS_SRIOV(bp))
		return;

	if (!NO_FCOE(bp))
		is_fcoe = true;

	/* fcoe adds one global request and one queue request */
	num_queues_req = BNX2X_NUM_ETH_QUEUES(bp) + is_fcoe;
	first_queue_query_index = BNX2X_FIRST_QUEUE_QUERY_IDX -
		(is_fcoe ? 0 : 1);

	DP(BNX2X_MSG_IOV,
	   "BNX2X_NUM_ETH_QUEUES %d, is_fcoe %d, first_queue_query_index %d => determined the last non virtual statistics query index is %d. Will add queries on top of that\n",
	   BNX2X_NUM_ETH_QUEUES(bp), is_fcoe, first_queue_query_index,
	   first_queue_query_index + num_queues_req);

	cur_data_offset = bp->fw_stats_data_mapping +
		offsetof(struct bnx2x_fw_stats_data, queue_stats) +
		num_queues_req * sizeof(struct per_queue_stats);

	cur_query_entry = &bp->fw_stats_req->
		query[first_queue_query_index + num_queues_req];

	for_each_vf(bp, i) {
		int j;
		struct bnx2x_virtf *vf = BP_VF(bp, i);

		if (vf->state != VF_ENABLED) {
			DP(BNX2X_MSG_IOV,
			   "vf %d not enabled so no stats for it\n",
			   vf->abs_vfid);
			continue;
		}

		DP(BNX2X_MSG_IOV, "add addresses for vf %d\n", vf->abs_vfid);
		for_each_vfq(vf, j) {
			struct bnx2x_vf_queue *rxq = vfq_get(vf, j);

			/* collect stats fro active queues only */
			if (bnx2x_get_q_logical_state(bp, &rxq->sp_obj) ==
			    BNX2X_Q_LOGICAL_STATE_STOPPED)
				continue;

			/* create stats query entry for this queue */
			cur_query_entry->kind = STATS_TYPE_QUEUE;
			cur_query_entry->index = vfq_cl_id(vf, rxq);
			cur_query_entry->funcID =
				cpu_to_le16(FW_VF_HANDLE(vf->abs_vfid));
			cur_query_entry->address.hi =
				cpu_to_le32(U64_HI(vf->fw_stat_map));
			cur_query_entry->address.lo =
				cpu_to_le32(U64_LO(vf->fw_stat_map));
			DP(BNX2X_MSG_IOV,
			   "added address %x %x for vf %d queue %d client %d\n",
			   cur_query_entry->address.hi,
			   cur_query_entry->address.lo, cur_query_entry->funcID,
			   j, cur_query_entry->index);
			cur_query_entry++;
			cur_data_offset += sizeof(struct per_queue_stats);
			stats_count++;
		}
	}
	bp->fw_stats_req->hdr.cmd_num = bp->fw_stats_num + stats_count;
}

void bnx2x_iov_sp_task(struct bnx2x *bp)
{
	int i;

	if (!IS_SRIOV(bp))
		return;
	/* Iterate over all VFs and invoke state transition for VFs with
	 * 'in-progress' slow-path operations
	 */
	DP(BNX2X_MSG_IOV, "searching for pending vf operations\n");
	for_each_vf(bp, i) {
		struct bnx2x_virtf *vf = BP_VF(bp, i);

		if (!list_empty(&vf->op_list_head) &&
		    atomic_read(&vf->op_in_progress)) {
			DP(BNX2X_MSG_IOV, "running pending op for vf %d\n", i);
			bnx2x_vfop_cur(bp, vf)->transition(bp, vf);
		}
	}
}

static inline
struct bnx2x_virtf *__vf_from_stat_id(struct bnx2x *bp, u8 stat_id)
{
	int i;
	struct bnx2x_virtf *vf = NULL;

	for_each_vf(bp, i) {
		vf = BP_VF(bp, i);
		if (stat_id >= vf->igu_base_id &&
		    stat_id < vf->igu_base_id + vf_sb_count(vf))
			break;
	}
	return vf;
}

/* VF API helpers */
static void bnx2x_vf_qtbl_set_q(struct bnx2x *bp, u8 abs_vfid, u8 qid,
				u8 enable)
{
	u32 reg = PXP_REG_HST_ZONE_PERMISSION_TABLE + qid * 4;
	u32 val = enable ? (abs_vfid | (1 << 6)) : 0;

	REG_WR(bp, reg, val);
}

u8 bnx2x_vf_max_queue_cnt(struct bnx2x *bp, struct bnx2x_virtf *vf)
{
	return min_t(u8, min_t(u8, vf_sb_count(vf), BNX2X_CIDS_PER_VF),
		     BNX2X_VF_MAX_QUEUES);
}

static
int bnx2x_vf_chk_avail_resc(struct bnx2x *bp, struct bnx2x_virtf *vf,
			    struct vf_pf_resc_request *req_resc)
{
	u8 rxq_cnt = vf_rxq_count(vf) ? : bnx2x_vf_max_queue_cnt(bp, vf);
	u8 txq_cnt = vf_txq_count(vf) ? : bnx2x_vf_max_queue_cnt(bp, vf);

	return ((req_resc->num_rxqs <= rxq_cnt) &&
		(req_resc->num_txqs <= txq_cnt) &&
		(req_resc->num_sbs <= vf_sb_count(vf))   &&
		(req_resc->num_mac_filters <= vf_mac_rules_cnt(vf)) &&
		(req_resc->num_vlan_filters <= vf_vlan_rules_cnt(vf)));
}

/* CORE VF API */
int bnx2x_vf_acquire(struct bnx2x *bp, struct bnx2x_virtf *vf,
		     struct vf_pf_resc_request *resc)
{
	int base_vf_cid = (BP_VFDB(bp)->sriov.first_vf_in_pf + vf->index) *
		BNX2X_CIDS_PER_VF;

	union cdu_context *base_cxt = (union cdu_context *)
		BP_VF_CXT_PAGE(bp, base_vf_cid/ILT_PAGE_CIDS)->addr +
		(base_vf_cid & (ILT_PAGE_CIDS-1));
	int i;

	/* if state is 'acquired' the VF was not released or FLR'd, in
	 * this case the returned resources match the acquired already
	 * acquired resources. Verify that the requested numbers do
	 * not exceed the already acquired numbers.
	 */
	if (vf->state == VF_ACQUIRED) {
		DP(BNX2X_MSG_IOV, "VF[%d] Trying to re-acquire resources (VF was not released or FLR'd)\n",
		   vf->abs_vfid);

		if (!bnx2x_vf_chk_avail_resc(bp, vf, resc)) {
			BNX2X_ERR("VF[%d] When re-acquiring resources, requested numbers must be <= then previously acquired numbers\n",
				  vf->abs_vfid);
			return -EINVAL;
		}
		return 0;
	}

	/* Otherwise vf state must be 'free' or 'reset' */
	if (vf->state != VF_FREE && vf->state != VF_RESET) {
		BNX2X_ERR("VF[%d] Can not acquire a VF with state %d\n",
			  vf->abs_vfid, vf->state);
		return -EINVAL;
	}

	/* static allocation:
	 * the global maximum number are fixed per VF. fail the request if
	 * requested number exceed these globals
	 */
	if (!bnx2x_vf_chk_avail_resc(bp, vf, resc)) {
		DP(BNX2X_MSG_IOV,
		   "cannot fulfill vf resource request. Placing maximal available values in response\n");
		/* set the max resource in the vf */
		return -ENOMEM;
	}

	/* Set resources counters - 0 request means max available */
	vf_sb_count(vf) = resc->num_sbs;
	vf_rxq_count(vf) = resc->num_rxqs ? : bnx2x_vf_max_queue_cnt(bp, vf);
	vf_txq_count(vf) = resc->num_txqs ? : bnx2x_vf_max_queue_cnt(bp, vf);
	if (resc->num_mac_filters)
		vf_mac_rules_cnt(vf) = resc->num_mac_filters;
	if (resc->num_vlan_filters)
		vf_vlan_rules_cnt(vf) = resc->num_vlan_filters;

	DP(BNX2X_MSG_IOV,
	   "Fulfilling vf request: sb count %d, tx_count %d, rx_count %d, mac_rules_count %d, vlan_rules_count %d\n",
	   vf_sb_count(vf), vf_rxq_count(vf),
	   vf_txq_count(vf), vf_mac_rules_cnt(vf),
	   vf_vlan_rules_cnt(vf));

	/* Initialize the queues */
	if (!vf->vfqs) {
		DP(BNX2X_MSG_IOV, "vf->vfqs was not allocated\n");
		return -EINVAL;
	}

	for_each_vfq(vf, i) {
		struct bnx2x_vf_queue *q = vfq_get(vf, i);

		if (!q) {
			DP(BNX2X_MSG_IOV, "q number %d was not allocated\n", i);
			return -EINVAL;
		}

		q->index = i;
		q->cxt = &((base_cxt + i)->eth);
		q->cid = BNX2X_FIRST_VF_CID + base_vf_cid + i;

		DP(BNX2X_MSG_IOV, "VFQ[%d:%d]: index %d, cid 0x%x, cxt %p\n",
		   vf->abs_vfid, i, q->index, q->cid, q->cxt);

		/* init SP objects */
		bnx2x_vfq_init(bp, vf, q);
	}
	vf->state = VF_ACQUIRED;
	return 0;
}

int bnx2x_vf_init(struct bnx2x *bp, struct bnx2x_virtf *vf, dma_addr_t *sb_map)
{
	struct bnx2x_func_init_params func_init = {0};
	u16 flags = 0;
	int i;

	/* the sb resources are initialized at this point, do the
	 * FW/HW initializations
	 */
	for_each_vf_sb(vf, i)
		bnx2x_init_sb(bp, (dma_addr_t)sb_map[i], vf->abs_vfid, true,
			      vf_igu_sb(vf, i), vf_igu_sb(vf, i));

	/* Sanity checks */
	if (vf->state != VF_ACQUIRED) {
		DP(BNX2X_MSG_IOV, "VF[%d] is not in VF_ACQUIRED, but %d\n",
		   vf->abs_vfid, vf->state);
		return -EINVAL;
	}
	/* FLR cleanup epilogue */
	if (bnx2x_vf_flr_clnup_epilog(bp, vf->abs_vfid))
		return -EBUSY;

	/* reset IGU VF statistics: MSIX */
	REG_WR(bp, IGU_REG_STATISTIC_NUM_MESSAGE_SENT + vf->abs_vfid * 4 , 0);

	/* vf init */
	if (vf->cfg_flags & VF_CFG_STATS)
		flags |= (FUNC_FLG_STATS | FUNC_FLG_SPQ);

	if (vf->cfg_flags & VF_CFG_TPA)
		flags |= FUNC_FLG_TPA;

	if (is_vf_multi(vf))
		flags |= FUNC_FLG_RSS;

	/* function setup */
	func_init.func_flgs = flags;
	func_init.pf_id = BP_FUNC(bp);
	func_init.func_id = FW_VF_HANDLE(vf->abs_vfid);
	func_init.fw_stat_map = vf->fw_stat_map;
	func_init.spq_map = vf->spq_map;
	func_init.spq_prod = 0;
	bnx2x_func_init(bp, &func_init);

	/* Enable the vf */
	bnx2x_vf_enable_access(bp, vf->abs_vfid);
	bnx2x_vf_enable_traffic(bp, vf);

	/* queue protection table */
	for_each_vfq(vf, i)
		bnx2x_vf_qtbl_set_q(bp, vf->abs_vfid,
				    vfq_qzone_id(vf, vfq_get(vf, i)), true);

	vf->state = VF_ENABLED;

	return 0;
}

void bnx2x_lock_vf_pf_channel(struct bnx2x *bp, struct bnx2x_virtf *vf,
			      enum channel_tlvs tlv)
{
	/* lock the channel */
	mutex_lock(&vf->op_mutex);

	/* record the locking op */
	vf->op_current = tlv;

	/* log the lock */
	DP(BNX2X_MSG_IOV, "VF[%d]: vf pf channel locked by %d\n",
	   vf->abs_vfid, tlv);
}

void bnx2x_unlock_vf_pf_channel(struct bnx2x *bp, struct bnx2x_virtf *vf,
				enum channel_tlvs expected_tlv)
{
	WARN(expected_tlv != vf->op_current,
	     "lock mismatch: expected %d found %d", expected_tlv,
	     vf->op_current);

	/* lock the channel */
	mutex_unlock(&vf->op_mutex);

	/* log the unlock */
	DP(BNX2X_MSG_IOV, "VF[%d]: vf pf channel unlocked by %d\n",
	   vf->abs_vfid, vf->op_current);

	/* record the locking op */
	vf->op_current = CHANNEL_TLV_NONE;
}
