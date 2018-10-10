// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/sysfs.h>

#include "rvu.h"
#include "rvu_reg.h"

#define DRV_NAME	"octeontx2-af"
#define DRV_STRING      "Marvell OcteonTX2 RVU Admin Function Driver"
#define DRV_VERSION	"1.0"

static int rvu_get_hwvf(struct rvu *rvu, int pcifunc);

/* Supported devices */
static const struct pci_device_id rvu_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_RVU_AF) },
	{ 0, }  /* end of table */
};

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION(DRV_STRING);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, rvu_id_table);

/* Poll a RVU block's register 'offset', for a 'zero'
 * or 'nonzero' at bits specified by 'mask'
 */
int rvu_poll_reg(struct rvu *rvu, u64 block, u64 offset, u64 mask, bool zero)
{
	void __iomem *reg;
	int timeout = 100;
	u64 reg_val;

	reg = rvu->afreg_base + ((block << 28) | offset);
	while (timeout) {
		reg_val = readq(reg);
		if (zero && !(reg_val & mask))
			return 0;
		if (!zero && (reg_val & mask))
			return 0;
		usleep_range(1, 2);
		timeout--;
	}
	return -EBUSY;
}

int rvu_alloc_bitmap(struct rsrc_bmap *rsrc)
{
	rsrc->bmap = kcalloc(BITS_TO_LONGS(rsrc->max),
			     sizeof(long), GFP_KERNEL);
	if (!rsrc->bmap)
		return -ENOMEM;
	return 0;
}

static void rvu_update_rsrc_map(struct rvu *rvu, struct rvu_pfvf *pfvf,
				struct rvu_block *block, u16 pcifunc,
				u16 lf, bool attach)
{
	int devnum, num_lfs = 0;
	bool is_pf;
	u64 reg;

	if (lf >= block->lf.max) {
		dev_err(&rvu->pdev->dev,
			"%s: FATAL: LF %d is >= %s's max lfs i.e %d\n",
			__func__, lf, block->name, block->lf.max);
		return;
	}

	/* Check if this is for a RVU PF or VF */
	if (pcifunc & RVU_PFVF_FUNC_MASK) {
		is_pf = false;
		devnum = rvu_get_hwvf(rvu, pcifunc);
	} else {
		is_pf = true;
		devnum = rvu_get_pf(pcifunc);
	}

	block->fn_map[lf] = attach ? pcifunc : 0;

	switch (block->type) {
	case BLKTYPE_NPA:
		pfvf->npalf = attach ? true : false;
		num_lfs = pfvf->npalf;
		break;
	case BLKTYPE_NIX:
		pfvf->nixlf = attach ? true : false;
		num_lfs = pfvf->nixlf;
		break;
	case BLKTYPE_SSO:
		attach ? pfvf->sso++ : pfvf->sso--;
		num_lfs = pfvf->sso;
		break;
	case BLKTYPE_SSOW:
		attach ? pfvf->ssow++ : pfvf->ssow--;
		num_lfs = pfvf->ssow;
		break;
	case BLKTYPE_TIM:
		attach ? pfvf->timlfs++ : pfvf->timlfs--;
		num_lfs = pfvf->timlfs;
		break;
	case BLKTYPE_CPT:
		attach ? pfvf->cptlfs++ : pfvf->cptlfs--;
		num_lfs = pfvf->cptlfs;
		break;
	}

	reg = is_pf ? block->pf_lfcnt_reg : block->vf_lfcnt_reg;
	rvu_write64(rvu, BLKADDR_RVUM, reg | (devnum << 16), num_lfs);
}

inline int rvu_get_pf(u16 pcifunc)
{
	return (pcifunc >> RVU_PFVF_PF_SHIFT) & RVU_PFVF_PF_MASK;
}

static int rvu_get_hwvf(struct rvu *rvu, int pcifunc)
{
	int pf, func;
	u64 cfg;

	pf = rvu_get_pf(pcifunc);
	func = pcifunc & RVU_PFVF_FUNC_MASK;

	/* Get first HWVF attached to this PF */
	cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_PFX_CFG(pf));

	return ((cfg & 0xFFF) + func - 1);
}

struct rvu_pfvf *rvu_get_pfvf(struct rvu *rvu, int pcifunc)
{
	/* Check if it is a PF or VF */
	if (pcifunc & RVU_PFVF_FUNC_MASK)
		return &rvu->hwvf[rvu_get_hwvf(rvu, pcifunc)];
	else
		return &rvu->pf[rvu_get_pf(pcifunc)];
}

static void rvu_check_block_implemented(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int blkid;
	u64 cfg;

	/* For each block check if 'implemented' bit is set */
	for (blkid = 0; blkid < BLK_COUNT; blkid++) {
		block = &hw->block[blkid];
		cfg = rvupf_read64(rvu, RVU_PF_BLOCK_ADDRX_DISC(blkid));
		if (cfg & BIT_ULL(11))
			block->implemented = true;
	}
}

static void rvu_block_reset(struct rvu *rvu, int blkaddr, u64 rst_reg)
{
	struct rvu_block *block = &rvu->hw->block[blkaddr];

	if (!block->implemented)
		return;

	rvu_write64(rvu, blkaddr, rst_reg, BIT_ULL(0));
	rvu_poll_reg(rvu, blkaddr, rst_reg, BIT_ULL(63), true);
}

static void rvu_reset_all_blocks(struct rvu *rvu)
{
	/* Do a HW reset of all RVU blocks */
	rvu_block_reset(rvu, BLKADDR_NPA, NPA_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NIX0, NIX_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NPC, NPC_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_SSO, SSO_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_TIM, TIM_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_CPT0, CPT_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NDC0, NDC_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NDC1, NDC_AF_BLK_RST);
	rvu_block_reset(rvu, BLKADDR_NDC2, NDC_AF_BLK_RST);
}

static void rvu_scan_block(struct rvu *rvu, struct rvu_block *block)
{
	struct rvu_pfvf *pfvf;
	u64 cfg;
	int lf;

	for (lf = 0; lf < block->lf.max; lf++) {
		cfg = rvu_read64(rvu, block->addr,
				 block->lfcfg_reg | (lf << block->lfshift));
		if (!(cfg & BIT_ULL(63)))
			continue;

		/* Set this resource as being used */
		__set_bit(lf, block->lf.bmap);

		/* Get, to whom this LF is attached */
		pfvf = rvu_get_pfvf(rvu, (cfg >> 8) & 0xFFFF);
		rvu_update_rsrc_map(rvu, pfvf, block,
				    (cfg >> 8) & 0xFFFF, lf, true);
	}
}

static void rvu_free_hw_resources(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int id;

	/* Free all bitmaps */
	for (id = 0; id < BLK_COUNT; id++) {
		block = &hw->block[id];
		kfree(block->lf.bmap);
	}
}

static int rvu_setup_hw_resources(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int blkid, err;
	u64 cfg;

	/* Get HW supported max RVU PF & VF count */
	cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_CONST);
	hw->total_pfs = (cfg >> 32) & 0xFF;
	hw->total_vfs = (cfg >> 20) & 0xFFF;
	hw->max_vfs_per_pf = (cfg >> 40) & 0xFF;

	/* Init NPA LF's bitmap */
	block = &hw->block[BLKADDR_NPA];
	if (!block->implemented)
		goto nix;
	cfg = rvu_read64(rvu, BLKADDR_NPA, NPA_AF_CONST);
	block->lf.max = (cfg >> 16) & 0xFFF;
	block->addr = BLKADDR_NPA;
	block->type = BLKTYPE_NPA;
	block->lfshift = 8;
	block->lookup_reg = NPA_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_NPA_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_NPA_CFG;
	block->lfcfg_reg = NPA_PRIV_LFX_CFG;
	block->msixcfg_reg = NPA_PRIV_LFX_INT_CFG;
	block->lfreset_reg = NPA_AF_LF_RST;
	sprintf(block->name, "NPA");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

nix:
	/* Init NIX LF's bitmap */
	block = &hw->block[BLKADDR_NIX0];
	if (!block->implemented)
		goto sso;
	cfg = rvu_read64(rvu, BLKADDR_NIX0, NIX_AF_CONST2);
	block->lf.max = cfg & 0xFFF;
	block->addr = BLKADDR_NIX0;
	block->type = BLKTYPE_NIX;
	block->lfshift = 8;
	block->lookup_reg = NIX_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_NIX_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_NIX_CFG;
	block->lfcfg_reg = NIX_PRIV_LFX_CFG;
	block->msixcfg_reg = NIX_PRIV_LFX_INT_CFG;
	block->lfreset_reg = NIX_AF_LF_RST;
	sprintf(block->name, "NIX");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

sso:
	/* Init SSO group's bitmap */
	block = &hw->block[BLKADDR_SSO];
	if (!block->implemented)
		goto ssow;
	cfg = rvu_read64(rvu, BLKADDR_SSO, SSO_AF_CONST);
	block->lf.max = cfg & 0xFFFF;
	block->addr = BLKADDR_SSO;
	block->type = BLKTYPE_SSO;
	block->multislot = true;
	block->lfshift = 3;
	block->lookup_reg = SSO_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_SSO_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_SSO_CFG;
	block->lfcfg_reg = SSO_PRIV_LFX_HWGRP_CFG;
	block->msixcfg_reg = SSO_PRIV_LFX_HWGRP_INT_CFG;
	block->lfreset_reg = SSO_AF_LF_HWGRP_RST;
	sprintf(block->name, "SSO GROUP");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

ssow:
	/* Init SSO workslot's bitmap */
	block = &hw->block[BLKADDR_SSOW];
	if (!block->implemented)
		goto tim;
	block->lf.max = (cfg >> 56) & 0xFF;
	block->addr = BLKADDR_SSOW;
	block->type = BLKTYPE_SSOW;
	block->multislot = true;
	block->lfshift = 3;
	block->lookup_reg = SSOW_AF_RVU_LF_HWS_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_SSOW_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_SSOW_CFG;
	block->lfcfg_reg = SSOW_PRIV_LFX_HWS_CFG;
	block->msixcfg_reg = SSOW_PRIV_LFX_HWS_INT_CFG;
	block->lfreset_reg = SSOW_AF_LF_HWS_RST;
	sprintf(block->name, "SSOWS");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

tim:
	/* Init TIM LF's bitmap */
	block = &hw->block[BLKADDR_TIM];
	if (!block->implemented)
		goto cpt;
	cfg = rvu_read64(rvu, BLKADDR_TIM, TIM_AF_CONST);
	block->lf.max = cfg & 0xFFFF;
	block->addr = BLKADDR_TIM;
	block->type = BLKTYPE_TIM;
	block->multislot = true;
	block->lfshift = 3;
	block->lookup_reg = TIM_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_TIM_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_TIM_CFG;
	block->lfcfg_reg = TIM_PRIV_LFX_CFG;
	block->msixcfg_reg = TIM_PRIV_LFX_INT_CFG;
	block->lfreset_reg = TIM_AF_LF_RST;
	sprintf(block->name, "TIM");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

cpt:
	/* Init CPT LF's bitmap */
	block = &hw->block[BLKADDR_CPT0];
	if (!block->implemented)
		goto init;
	cfg = rvu_read64(rvu, BLKADDR_CPT0, CPT_AF_CONSTANTS0);
	block->lf.max = cfg & 0xFF;
	block->addr = BLKADDR_CPT0;
	block->type = BLKTYPE_CPT;
	block->multislot = true;
	block->lfshift = 3;
	block->lookup_reg = CPT_AF_RVU_LF_CFG_DEBUG;
	block->pf_lfcnt_reg = RVU_PRIV_PFX_CPT_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_CPT_CFG;
	block->lfcfg_reg = CPT_PRIV_LFX_CFG;
	block->msixcfg_reg = CPT_PRIV_LFX_INT_CFG;
	block->lfreset_reg = CPT_AF_LF_RST;
	sprintf(block->name, "CPT");
	err = rvu_alloc_bitmap(&block->lf);
	if (err)
		return err;

init:
	/* Allocate memory for PFVF data */
	rvu->pf = devm_kcalloc(rvu->dev, hw->total_pfs,
			       sizeof(struct rvu_pfvf), GFP_KERNEL);
	if (!rvu->pf)
		return -ENOMEM;

	rvu->hwvf = devm_kcalloc(rvu->dev, hw->total_vfs,
				 sizeof(struct rvu_pfvf), GFP_KERNEL);
	if (!rvu->hwvf)
		return -ENOMEM;

	for (blkid = 0; blkid < BLK_COUNT; blkid++) {
		block = &hw->block[blkid];
		if (!block->lf.bmap)
			continue;

		/* Allocate memory for block LF/slot to pcifunc mapping info */
		block->fn_map = devm_kcalloc(rvu->dev, block->lf.max,
					     sizeof(u16), GFP_KERNEL);
		if (!block->fn_map)
			return -ENOMEM;

		/* Scan all blocks to check if low level firmware has
		 * already provisioned any of the resources to a PF/VF.
		 */
		rvu_scan_block(rvu, block);
	}

	return 0;
}

static int rvu_mbox_handler_READY(struct rvu *rvu, struct msg_req *req,
				  struct ready_msg_rsp *rsp)
{
	return 0;
}

static int rvu_process_mbox_msg(struct rvu *rvu, int devid,
				struct mbox_msghdr *req)
{
	/* Check if valid, if not reply with a invalid msg */
	if (req->sig != OTX2_MBOX_REQ_SIG)
		goto bad_message;

	switch (req->id) {
#define M(_name, _id, _req_type, _rsp_type)				\
	case _id: {							\
		struct _rsp_type *rsp;					\
		int err;						\
									\
		rsp = (struct _rsp_type *)otx2_mbox_alloc_msg(		\
			&rvu->mbox, devid,				\
			sizeof(struct _rsp_type));			\
		if (rsp) {						\
			rsp->hdr.id = _id;				\
			rsp->hdr.sig = OTX2_MBOX_RSP_SIG;		\
			rsp->hdr.pcifunc = req->pcifunc;		\
			rsp->hdr.rc = 0;				\
		}							\
									\
		err = rvu_mbox_handler_ ## _name(rvu,			\
						 (struct _req_type *)req, \
						 rsp);			\
		if (rsp && err)						\
			rsp->hdr.rc = err;				\
									\
		return rsp ? err : -ENOMEM;				\
	}
MBOX_MESSAGES
#undef M
		break;
bad_message:
	default:
		otx2_reply_invalid_msg(&rvu->mbox, devid, req->pcifunc,
				       req->id);
		return -ENODEV;
	}
}

static void rvu_mbox_handler(struct work_struct *work)
{
	struct rvu_work *mwork = container_of(work, struct rvu_work, work);
	struct rvu *rvu = mwork->rvu;
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *req_hdr;
	struct mbox_msghdr *msg;
	struct otx2_mbox *mbox;
	int offset, id, err;
	u16 pf;

	mbox = &rvu->mbox;
	pf = mwork - rvu->mbox_wrk;
	mdev = &mbox->dev[pf];

	/* Process received mbox messages */
	req_hdr = mdev->mbase + mbox->rx_start;
	if (req_hdr->num_msgs == 0)
		return;

	offset = mbox->rx_start + ALIGN(sizeof(*req_hdr), MBOX_MSG_ALIGN);

	for (id = 0; id < req_hdr->num_msgs; id++) {
		msg = mdev->mbase + offset;

		/* Set which PF sent this message based on mbox IRQ */
		msg->pcifunc &= ~(RVU_PFVF_PF_MASK << RVU_PFVF_PF_SHIFT);
		msg->pcifunc |= (pf << RVU_PFVF_PF_SHIFT);
		err = rvu_process_mbox_msg(rvu, pf, msg);
		if (!err) {
			offset = mbox->rx_start + msg->next_msgoff;
			continue;
		}

		if (msg->pcifunc & RVU_PFVF_FUNC_MASK)
			dev_warn(rvu->dev, "Error %d when processing message %s (0x%x) from PF%d:VF%d\n",
				 err, otx2_mbox_id2name(msg->id), msg->id, pf,
				 (msg->pcifunc & RVU_PFVF_FUNC_MASK) - 1);
		else
			dev_warn(rvu->dev, "Error %d when processing message %s (0x%x) from PF%d\n",
				 err, otx2_mbox_id2name(msg->id), msg->id, pf);
	}

	/* Send mbox responses to PF */
	otx2_mbox_msg_send(mbox, pf);
}

static int rvu_mbox_init(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	void __iomem *hwbase = NULL;
	struct rvu_work *mwork;
	u64 bar4_addr;
	int err, pf;

	rvu->mbox_wq = alloc_workqueue("rvu_afpf_mailbox",
				       WQ_UNBOUND | WQ_HIGHPRI | WQ_MEM_RECLAIM,
				       hw->total_pfs);
	if (!rvu->mbox_wq)
		return -ENOMEM;

	rvu->mbox_wrk = devm_kcalloc(rvu->dev, hw->total_pfs,
				     sizeof(struct rvu_work), GFP_KERNEL);
	if (!rvu->mbox_wrk) {
		err = -ENOMEM;
		goto exit;
	}

	/* Map mbox region shared with PFs */
	bar4_addr = rvu_read64(rvu, BLKADDR_RVUM, RVU_AF_PF_BAR4_ADDR);
	/* Mailbox is a reserved memory (in RAM) region shared between
	 * RVU devices, shouldn't be mapped as device memory to allow
	 * unaligned accesses.
	 */
	hwbase = ioremap_wc(bar4_addr, MBOX_SIZE * hw->total_pfs);
	if (!hwbase) {
		dev_err(rvu->dev, "Unable to map mailbox region\n");
		err = -ENOMEM;
		goto exit;
	}

	err = otx2_mbox_init(&rvu->mbox, hwbase, rvu->pdev, rvu->afreg_base,
			     MBOX_DIR_AFPF, hw->total_pfs);
	if (err)
		goto exit;

	for (pf = 0; pf < hw->total_pfs; pf++) {
		mwork = &rvu->mbox_wrk[pf];
		mwork->rvu = rvu;
		INIT_WORK(&mwork->work, rvu_mbox_handler);
	}

	return 0;
exit:
	if (hwbase)
		iounmap((void __iomem *)hwbase);
	destroy_workqueue(rvu->mbox_wq);
	return err;
}

static void rvu_mbox_destroy(struct rvu *rvu)
{
	if (rvu->mbox_wq) {
		flush_workqueue(rvu->mbox_wq);
		destroy_workqueue(rvu->mbox_wq);
		rvu->mbox_wq = NULL;
	}

	if (rvu->mbox.hwbase)
		iounmap((void __iomem *)rvu->mbox.hwbase);

	otx2_mbox_destroy(&rvu->mbox);
}

static irqreturn_t rvu_mbox_intr_handler(int irq, void *rvu_irq)
{
	struct rvu *rvu = (struct rvu *)rvu_irq;
	struct otx2_mbox_dev *mdev;
	struct otx2_mbox *mbox;
	struct mbox_hdr *hdr;
	u64 intr;
	u8  pf;

	intr = rvu_read64(rvu, BLKADDR_RVUM, RVU_AF_PFAF_MBOX_INT);
	/* Clear interrupts */
	rvu_write64(rvu, BLKADDR_RVUM, RVU_AF_PFAF_MBOX_INT, intr);

	/* Sync with mbox memory region */
	smp_wmb();

	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		if (intr & (1ULL << pf)) {
			mbox = &rvu->mbox;
			mdev = &mbox->dev[pf];
			hdr = mdev->mbase + mbox->rx_start;
			if (hdr->num_msgs)
				queue_work(rvu->mbox_wq,
					   &rvu->mbox_wrk[pf].work);
		}
	}

	return IRQ_HANDLED;
}

static void rvu_enable_mbox_intr(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;

	/* Clear spurious irqs, if any */
	rvu_write64(rvu, BLKADDR_RVUM,
		    RVU_AF_PFAF_MBOX_INT, INTR_MASK(hw->total_pfs));

	/* Enable mailbox interrupt for all PFs except PF0 i.e AF itself */
	rvu_write64(rvu, BLKADDR_RVUM, RVU_AF_PFAF_MBOX_INT_ENA_W1S,
		    INTR_MASK(hw->total_pfs) & ~1ULL);
}

static void rvu_unregister_interrupts(struct rvu *rvu)
{
	int irq;

	/* Disable the Mbox interrupt */
	rvu_write64(rvu, BLKADDR_RVUM, RVU_AF_PFAF_MBOX_INT_ENA_W1C,
		    INTR_MASK(rvu->hw->total_pfs) & ~1ULL);

	for (irq = 0; irq < rvu->num_vec; irq++) {
		if (rvu->irq_allocated[irq])
			free_irq(pci_irq_vector(rvu->pdev, irq), rvu);
	}

	pci_free_irq_vectors(rvu->pdev);
	rvu->num_vec = 0;
}

static int rvu_register_interrupts(struct rvu *rvu)
{
	int ret;

	rvu->num_vec = pci_msix_vec_count(rvu->pdev);

	rvu->irq_name = devm_kmalloc_array(rvu->dev, rvu->num_vec,
					   NAME_SIZE, GFP_KERNEL);
	if (!rvu->irq_name)
		return -ENOMEM;

	rvu->irq_allocated = devm_kcalloc(rvu->dev, rvu->num_vec,
					  sizeof(bool), GFP_KERNEL);
	if (!rvu->irq_allocated)
		return -ENOMEM;

	/* Enable MSI-X */
	ret = pci_alloc_irq_vectors(rvu->pdev, rvu->num_vec,
				    rvu->num_vec, PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(rvu->dev,
			"RVUAF: Request for %d msix vectors failed, ret %d\n",
			rvu->num_vec, ret);
		return ret;
	}

	/* Register mailbox interrupt handler */
	sprintf(&rvu->irq_name[RVU_AF_INT_VEC_MBOX * NAME_SIZE], "RVUAF Mbox");
	ret = request_irq(pci_irq_vector(rvu->pdev, RVU_AF_INT_VEC_MBOX),
			  rvu_mbox_intr_handler, 0,
			  &rvu->irq_name[RVU_AF_INT_VEC_MBOX * NAME_SIZE], rvu);
	if (ret) {
		dev_err(rvu->dev,
			"RVUAF: IRQ registration failed for mbox irq\n");
		goto fail;
	}

	rvu->irq_allocated[RVU_AF_INT_VEC_MBOX] = true;

	/* Enable mailbox interrupts from all PFs */
	rvu_enable_mbox_intr(rvu);

	return 0;

fail:
	pci_free_irq_vectors(rvu->pdev);
	return ret;
}

static int rvu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct rvu *rvu;
	int    err;

	rvu = devm_kzalloc(dev, sizeof(*rvu), GFP_KERNEL);
	if (!rvu)
		return -ENOMEM;

	rvu->hw = devm_kzalloc(dev, sizeof(struct rvu_hwinfo), GFP_KERNEL);
	if (!rvu->hw) {
		devm_kfree(dev, rvu);
		return -ENOMEM;
	}

	pci_set_drvdata(pdev, rvu);
	rvu->pdev = pdev;
	rvu->dev = &pdev->dev;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		goto err_freemem;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto err_disable_device;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to set DMA mask\n");
		goto err_release_regions;
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to set consistent DMA mask\n");
		goto err_release_regions;
	}

	/* Map Admin function CSRs */
	rvu->afreg_base = pcim_iomap(pdev, PCI_AF_REG_BAR_NUM, 0);
	rvu->pfreg_base = pcim_iomap(pdev, PCI_PF_REG_BAR_NUM, 0);
	if (!rvu->afreg_base || !rvu->pfreg_base) {
		dev_err(dev, "Unable to map admin function CSRs, aborting\n");
		err = -ENOMEM;
		goto err_release_regions;
	}

	/* Check which blocks the HW supports */
	rvu_check_block_implemented(rvu);

	rvu_reset_all_blocks(rvu);

	err = rvu_setup_hw_resources(rvu);
	if (err)
		goto err_release_regions;

	err = rvu_mbox_init(rvu);
	if (err)
		goto err_hwsetup;

	err = rvu_register_interrupts(rvu);
	if (err)
		goto err_mbox;

	return 0;

err_mbox:
	rvu_mbox_destroy(rvu);
err_hwsetup:
	rvu_reset_all_blocks(rvu);
	rvu_free_hw_resources(rvu);
err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
err_freemem:
	pci_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, rvu->hw);
	devm_kfree(dev, rvu);
	return err;
}

static void rvu_remove(struct pci_dev *pdev)
{
	struct rvu *rvu = pci_get_drvdata(pdev);

	rvu_unregister_interrupts(rvu);
	rvu_mbox_destroy(rvu);
	rvu_reset_all_blocks(rvu);
	rvu_free_hw_resources(rvu);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	devm_kfree(&pdev->dev, rvu->hw);
	devm_kfree(&pdev->dev, rvu);
}

static struct pci_driver rvu_driver = {
	.name = DRV_NAME,
	.id_table = rvu_id_table,
	.probe = rvu_probe,
	.remove = rvu_remove,
};

static int __init rvu_init_module(void)
{
	pr_info("%s: %s\n", DRV_NAME, DRV_STRING);

	return pci_register_driver(&rvu_driver);
}

static void __exit rvu_cleanup_module(void)
{
	pci_unregister_driver(&rvu_driver);
}

module_init(rvu_init_module);
module_exit(rvu_cleanup_module);
