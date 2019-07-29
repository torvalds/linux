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

#include "cgx.h"
#include "rvu.h"
#include "rvu_reg.h"

#define DRV_NAME	"octeontx2-af"
#define DRV_STRING      "Marvell OcteonTX2 RVU Admin Function Driver"
#define DRV_VERSION	"1.0"

static int rvu_get_hwvf(struct rvu *rvu, int pcifunc);

static void rvu_set_msix_offset(struct rvu *rvu, struct rvu_pfvf *pfvf,
				struct rvu_block *block, int lf);
static void rvu_clear_msix_offset(struct rvu *rvu, struct rvu_pfvf *pfvf,
				  struct rvu_block *block, int lf);

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
	unsigned long timeout = jiffies + usecs_to_jiffies(100);
	void __iomem *reg;
	u64 reg_val;

	reg = rvu->afreg_base + ((block << 28) | offset);
	while (time_before(jiffies, timeout)) {
		reg_val = readq(reg);
		if (zero && !(reg_val & mask))
			return 0;
		if (!zero && (reg_val & mask))
			return 0;
		usleep_range(1, 5);
		timeout--;
	}
	return -EBUSY;
}

int rvu_alloc_rsrc(struct rsrc_bmap *rsrc)
{
	int id;

	if (!rsrc->bmap)
		return -EINVAL;

	id = find_first_zero_bit(rsrc->bmap, rsrc->max);
	if (id >= rsrc->max)
		return -ENOSPC;

	__set_bit(id, rsrc->bmap);

	return id;
}

int rvu_alloc_rsrc_contig(struct rsrc_bmap *rsrc, int nrsrc)
{
	int start;

	if (!rsrc->bmap)
		return -EINVAL;

	start = bitmap_find_next_zero_area(rsrc->bmap, rsrc->max, 0, nrsrc, 0);
	if (start >= rsrc->max)
		return -ENOSPC;

	bitmap_set(rsrc->bmap, start, nrsrc);
	return start;
}

static void rvu_free_rsrc_contig(struct rsrc_bmap *rsrc, int nrsrc, int start)
{
	if (!rsrc->bmap)
		return;
	if (start >= rsrc->max)
		return;

	bitmap_clear(rsrc->bmap, start, nrsrc);
}

bool rvu_rsrc_check_contig(struct rsrc_bmap *rsrc, int nrsrc)
{
	int start;

	if (!rsrc->bmap)
		return false;

	start = bitmap_find_next_zero_area(rsrc->bmap, rsrc->max, 0, nrsrc, 0);
	if (start >= rsrc->max)
		return false;

	return true;
}

void rvu_free_rsrc(struct rsrc_bmap *rsrc, int id)
{
	if (!rsrc->bmap)
		return;

	__clear_bit(id, rsrc->bmap);
}

int rvu_rsrc_free_count(struct rsrc_bmap *rsrc)
{
	int used;

	if (!rsrc->bmap)
		return 0;

	used = bitmap_weight(rsrc->bmap, rsrc->max);
	return (rsrc->max - used);
}

int rvu_alloc_bitmap(struct rsrc_bmap *rsrc)
{
	rsrc->bmap = kcalloc(BITS_TO_LONGS(rsrc->max),
			     sizeof(long), GFP_KERNEL);
	if (!rsrc->bmap)
		return -ENOMEM;
	return 0;
}

/* Get block LF's HW index from a PF_FUNC's block slot number */
int rvu_get_lf(struct rvu *rvu, struct rvu_block *block, u16 pcifunc, u16 slot)
{
	u16 match = 0;
	int lf;

	spin_lock(&rvu->rsrc_lock);
	for (lf = 0; lf < block->lf.max; lf++) {
		if (block->fn_map[lf] == pcifunc) {
			if (slot == match) {
				spin_unlock(&rvu->rsrc_lock);
				return lf;
			}
			match++;
		}
	}
	spin_unlock(&rvu->rsrc_lock);
	return -ENODEV;
}

/* Convert BLOCK_TYPE_E to a BLOCK_ADDR_E.
 * Some silicon variants of OcteonTX2 supports
 * multiple blocks of same type.
 *
 * @pcifunc has to be zero when no LF is yet attached.
 */
int rvu_get_blkaddr(struct rvu *rvu, int blktype, u16 pcifunc)
{
	int devnum, blkaddr = -ENODEV;
	u64 cfg, reg;
	bool is_pf;

	switch (blktype) {
	case BLKTYPE_NPC:
		blkaddr = BLKADDR_NPC;
		goto exit;
	case BLKTYPE_NPA:
		blkaddr = BLKADDR_NPA;
		goto exit;
	case BLKTYPE_NIX:
		/* For now assume NIX0 */
		if (!pcifunc) {
			blkaddr = BLKADDR_NIX0;
			goto exit;
		}
		break;
	case BLKTYPE_SSO:
		blkaddr = BLKADDR_SSO;
		goto exit;
	case BLKTYPE_SSOW:
		blkaddr = BLKADDR_SSOW;
		goto exit;
	case BLKTYPE_TIM:
		blkaddr = BLKADDR_TIM;
		goto exit;
	case BLKTYPE_CPT:
		/* For now assume CPT0 */
		if (!pcifunc) {
			blkaddr = BLKADDR_CPT0;
			goto exit;
		}
		break;
	}

	/* Check if this is a RVU PF or VF */
	if (pcifunc & RVU_PFVF_FUNC_MASK) {
		is_pf = false;
		devnum = rvu_get_hwvf(rvu, pcifunc);
	} else {
		is_pf = true;
		devnum = rvu_get_pf(pcifunc);
	}

	/* Check if the 'pcifunc' has a NIX LF from 'BLKADDR_NIX0' */
	if (blktype == BLKTYPE_NIX) {
		reg = is_pf ? RVU_PRIV_PFX_NIX0_CFG : RVU_PRIV_HWVFX_NIX0_CFG;
		cfg = rvu_read64(rvu, BLKADDR_RVUM, reg | (devnum << 16));
		if (cfg)
			blkaddr = BLKADDR_NIX0;
	}

	/* Check if the 'pcifunc' has a CPT LF from 'BLKADDR_CPT0' */
	if (blktype == BLKTYPE_CPT) {
		reg = is_pf ? RVU_PRIV_PFX_CPT0_CFG : RVU_PRIV_HWVFX_CPT0_CFG;
		cfg = rvu_read64(rvu, BLKADDR_RVUM, reg | (devnum << 16));
		if (cfg)
			blkaddr = BLKADDR_CPT0;
	}

exit:
	if (is_block_implemented(rvu->hw, blkaddr))
		return blkaddr;
	return -ENODEV;
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

void rvu_get_pf_numvfs(struct rvu *rvu, int pf, int *numvfs, int *hwvf)
{
	u64 cfg;

	/* Get numVFs attached to this PF and first HWVF */
	cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_PFX_CFG(pf));
	*numvfs = (cfg >> 12) & 0xFF;
	*hwvf = cfg & 0xFFF;
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

bool is_block_implemented(struct rvu_hwinfo *hw, int blkaddr)
{
	struct rvu_block *block;

	if (blkaddr < BLKADDR_RVUM || blkaddr >= BLK_COUNT)
		return false;

	block = &hw->block[blkaddr];
	return block->implemented;
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

int rvu_lf_reset(struct rvu *rvu, struct rvu_block *block, int lf)
{
	int err;

	if (!block->implemented)
		return 0;

	rvu_write64(rvu, block->addr, block->lfreset_reg, lf | BIT_ULL(12));
	err = rvu_poll_reg(rvu, block->addr, block->lfreset_reg, BIT_ULL(12),
			   true);
	return err;
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

		/* Set start MSIX vector for this LF within this PF/VF */
		rvu_set_msix_offset(rvu, pfvf, block, lf);
	}
}

static void rvu_check_min_msix_vec(struct rvu *rvu, int nvecs, int pf, int vf)
{
	int min_vecs;

	if (!vf)
		goto check_pf;

	if (!nvecs) {
		dev_warn(rvu->dev,
			 "PF%d:VF%d is configured with zero msix vectors, %d\n",
			 pf, vf - 1, nvecs);
	}
	return;

check_pf:
	if (pf == 0)
		min_vecs = RVU_AF_INT_VEC_CNT + RVU_PF_INT_VEC_CNT;
	else
		min_vecs = RVU_PF_INT_VEC_CNT;

	if (!(nvecs < min_vecs))
		return;
	dev_warn(rvu->dev,
		 "PF%d is configured with too few vectors, %d, min is %d\n",
		 pf, nvecs, min_vecs);
}

static int rvu_setup_msix_resources(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int pf, vf, numvfs, hwvf, err;
	int nvecs, offset, max_msix;
	struct rvu_pfvf *pfvf;
	u64 cfg, phy_addr;
	dma_addr_t iova;

	for (pf = 0; pf < hw->total_pfs; pf++) {
		cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_PFX_CFG(pf));
		/* If PF is not enabled, nothing to do */
		if (!((cfg >> 20) & 0x01))
			continue;

		rvu_get_pf_numvfs(rvu, pf, &numvfs, &hwvf);

		pfvf = &rvu->pf[pf];
		/* Get num of MSIX vectors attached to this PF */
		cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_PFX_MSIX_CFG(pf));
		pfvf->msix.max = ((cfg >> 32) & 0xFFF) + 1;
		rvu_check_min_msix_vec(rvu, pfvf->msix.max, pf, 0);

		/* Alloc msix bitmap for this PF */
		err = rvu_alloc_bitmap(&pfvf->msix);
		if (err)
			return err;

		/* Allocate memory for MSIX vector to RVU block LF mapping */
		pfvf->msix_lfmap = devm_kcalloc(rvu->dev, pfvf->msix.max,
						sizeof(u16), GFP_KERNEL);
		if (!pfvf->msix_lfmap)
			return -ENOMEM;

		/* For PF0 (AF) firmware will set msix vector offsets for
		 * AF, block AF and PF0_INT vectors, so jump to VFs.
		 */
		if (!pf)
			goto setup_vfmsix;

		/* Set MSIX offset for PF's 'RVU_PF_INT_VEC' vectors.
		 * These are allocated on driver init and never freed,
		 * so no need to set 'msix_lfmap' for these.
		 */
		cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_PFX_INT_CFG(pf));
		nvecs = (cfg >> 12) & 0xFF;
		cfg &= ~0x7FFULL;
		offset = rvu_alloc_rsrc_contig(&pfvf->msix, nvecs);
		rvu_write64(rvu, BLKADDR_RVUM,
			    RVU_PRIV_PFX_INT_CFG(pf), cfg | offset);
setup_vfmsix:
		/* Alloc msix bitmap for VFs */
		for (vf = 0; vf < numvfs; vf++) {
			pfvf =  &rvu->hwvf[hwvf + vf];
			/* Get num of MSIX vectors attached to this VF */
			cfg = rvu_read64(rvu, BLKADDR_RVUM,
					 RVU_PRIV_PFX_MSIX_CFG(pf));
			pfvf->msix.max = (cfg & 0xFFF) + 1;
			rvu_check_min_msix_vec(rvu, pfvf->msix.max, pf, vf + 1);

			/* Alloc msix bitmap for this VF */
			err = rvu_alloc_bitmap(&pfvf->msix);
			if (err)
				return err;

			pfvf->msix_lfmap =
				devm_kcalloc(rvu->dev, pfvf->msix.max,
					     sizeof(u16), GFP_KERNEL);
			if (!pfvf->msix_lfmap)
				return -ENOMEM;

			/* Set MSIX offset for HWVF's 'RVU_VF_INT_VEC' vectors.
			 * These are allocated on driver init and never freed,
			 * so no need to set 'msix_lfmap' for these.
			 */
			cfg = rvu_read64(rvu, BLKADDR_RVUM,
					 RVU_PRIV_HWVFX_INT_CFG(hwvf + vf));
			nvecs = (cfg >> 12) & 0xFF;
			cfg &= ~0x7FFULL;
			offset = rvu_alloc_rsrc_contig(&pfvf->msix, nvecs);
			rvu_write64(rvu, BLKADDR_RVUM,
				    RVU_PRIV_HWVFX_INT_CFG(hwvf + vf),
				    cfg | offset);
		}
	}

	/* HW interprets RVU_AF_MSIXTR_BASE address as an IOVA, hence
	 * create a IOMMU mapping for the physcial address configured by
	 * firmware and reconfig RVU_AF_MSIXTR_BASE with IOVA.
	 */
	cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_CONST);
	max_msix = cfg & 0xFFFFF;
	phy_addr = rvu_read64(rvu, BLKADDR_RVUM, RVU_AF_MSIXTR_BASE);
	iova = dma_map_resource(rvu->dev, phy_addr,
				max_msix * PCI_MSIX_ENTRY_SIZE,
				DMA_BIDIRECTIONAL, 0);

	if (dma_mapping_error(rvu->dev, iova))
		return -ENOMEM;

	rvu_write64(rvu, BLKADDR_RVUM, RVU_AF_MSIXTR_BASE, (u64)iova);
	rvu->msix_base_iova = iova;

	return 0;
}

static void rvu_free_hw_resources(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	struct rvu_pfvf  *pfvf;
	int id, max_msix;
	u64 cfg;

	rvu_npa_freemem(rvu);
	rvu_npc_freemem(rvu);
	rvu_nix_freemem(rvu);

	/* Free block LF bitmaps */
	for (id = 0; id < BLK_COUNT; id++) {
		block = &hw->block[id];
		kfree(block->lf.bmap);
	}

	/* Free MSIX bitmaps */
	for (id = 0; id < hw->total_pfs; id++) {
		pfvf = &rvu->pf[id];
		kfree(pfvf->msix.bmap);
	}

	for (id = 0; id < hw->total_vfs; id++) {
		pfvf = &rvu->hwvf[id];
		kfree(pfvf->msix.bmap);
	}

	/* Unmap MSIX vector base IOVA mapping */
	if (!rvu->msix_base_iova)
		return;
	cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_CONST);
	max_msix = cfg & 0xFFFFF;
	dma_unmap_resource(rvu->dev, rvu->msix_base_iova,
			   max_msix * PCI_MSIX_ENTRY_SIZE,
			   DMA_BIDIRECTIONAL, 0);
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
	block->pf_lfcnt_reg = RVU_PRIV_PFX_NIX0_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_NIX0_CFG;
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
	block->pf_lfcnt_reg = RVU_PRIV_PFX_CPT0_CFG;
	block->vf_lfcnt_reg = RVU_PRIV_HWVFX_CPT0_CFG;
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

	spin_lock_init(&rvu->rsrc_lock);

	err = rvu_setup_msix_resources(rvu);
	if (err)
		return err;

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

	err = rvu_npc_init(rvu);
	if (err)
		return err;

	err = rvu_npa_init(rvu);
	if (err)
		return err;

	err = rvu_nix_init(rvu);
	if (err)
		return err;

	return 0;
}

/* NPA and NIX admin queue APIs */
void rvu_aq_free(struct rvu *rvu, struct admin_queue *aq)
{
	if (!aq)
		return;

	qmem_free(rvu->dev, aq->inst);
	qmem_free(rvu->dev, aq->res);
	devm_kfree(rvu->dev, aq);
}

int rvu_aq_alloc(struct rvu *rvu, struct admin_queue **ad_queue,
		 int qsize, int inst_size, int res_size)
{
	struct admin_queue *aq;
	int err;

	*ad_queue = devm_kzalloc(rvu->dev, sizeof(*aq), GFP_KERNEL);
	if (!*ad_queue)
		return -ENOMEM;
	aq = *ad_queue;

	/* Alloc memory for instructions i.e AQ */
	err = qmem_alloc(rvu->dev, &aq->inst, qsize, inst_size);
	if (err) {
		devm_kfree(rvu->dev, aq);
		return err;
	}

	/* Alloc memory for results */
	err = qmem_alloc(rvu->dev, &aq->res, qsize, res_size);
	if (err) {
		rvu_aq_free(rvu, aq);
		return err;
	}

	spin_lock_init(&aq->lock);
	return 0;
}

static int rvu_mbox_handler_READY(struct rvu *rvu, struct msg_req *req,
				  struct ready_msg_rsp *rsp)
{
	return 0;
}

/* Get current count of a RVU block's LF/slots
 * provisioned to a given RVU func.
 */
static u16 rvu_get_rsrc_mapcount(struct rvu_pfvf *pfvf, int blktype)
{
	switch (blktype) {
	case BLKTYPE_NPA:
		return pfvf->npalf ? 1 : 0;
	case BLKTYPE_NIX:
		return pfvf->nixlf ? 1 : 0;
	case BLKTYPE_SSO:
		return pfvf->sso;
	case BLKTYPE_SSOW:
		return pfvf->ssow;
	case BLKTYPE_TIM:
		return pfvf->timlfs;
	case BLKTYPE_CPT:
		return pfvf->cptlfs;
	}
	return 0;
}

static int rvu_lookup_rsrc(struct rvu *rvu, struct rvu_block *block,
			   int pcifunc, int slot)
{
	u64 val;

	val = ((u64)pcifunc << 24) | (slot << 16) | (1ULL << 13);
	rvu_write64(rvu, block->addr, block->lookup_reg, val);
	/* Wait for the lookup to finish */
	/* TODO: put some timeout here */
	while (rvu_read64(rvu, block->addr, block->lookup_reg) & (1ULL << 13))
		;

	val = rvu_read64(rvu, block->addr, block->lookup_reg);

	/* Check LF valid bit */
	if (!(val & (1ULL << 12)))
		return -1;

	return (val & 0xFFF);
}

static void rvu_detach_block(struct rvu *rvu, int pcifunc, int blktype)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int slot, lf, num_lfs;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, blktype, pcifunc);
	if (blkaddr < 0)
		return;

	block = &hw->block[blkaddr];

	num_lfs = rvu_get_rsrc_mapcount(pfvf, block->type);
	if (!num_lfs)
		return;

	for (slot = 0; slot < num_lfs; slot++) {
		lf = rvu_lookup_rsrc(rvu, block, pcifunc, slot);
		if (lf < 0) /* This should never happen */
			continue;

		/* Disable the LF */
		rvu_write64(rvu, blkaddr, block->lfcfg_reg |
			    (lf << block->lfshift), 0x00ULL);

		/* Update SW maintained mapping info as well */
		rvu_update_rsrc_map(rvu, pfvf, block,
				    pcifunc, lf, false);

		/* Free the resource */
		rvu_free_rsrc(&block->lf, lf);

		/* Clear MSIX vector offset for this LF */
		rvu_clear_msix_offset(rvu, pfvf, block, lf);
	}
}

static int rvu_detach_rsrcs(struct rvu *rvu, struct rsrc_detach *detach,
			    u16 pcifunc)
{
	struct rvu_hwinfo *hw = rvu->hw;
	bool detach_all = true;
	struct rvu_block *block;
	int blkid;

	spin_lock(&rvu->rsrc_lock);

	/* Check for partial resource detach */
	if (detach && detach->partial)
		detach_all = false;

	/* Check for RVU block's LFs attached to this func,
	 * if so, detach them.
	 */
	for (blkid = 0; blkid < BLK_COUNT; blkid++) {
		block = &hw->block[blkid];
		if (!block->lf.bmap)
			continue;
		if (!detach_all && detach) {
			if (blkid == BLKADDR_NPA && !detach->npalf)
				continue;
			else if ((blkid == BLKADDR_NIX0) && !detach->nixlf)
				continue;
			else if ((blkid == BLKADDR_SSO) && !detach->sso)
				continue;
			else if ((blkid == BLKADDR_SSOW) && !detach->ssow)
				continue;
			else if ((blkid == BLKADDR_TIM) && !detach->timlfs)
				continue;
			else if ((blkid == BLKADDR_CPT0) && !detach->cptlfs)
				continue;
		}
		rvu_detach_block(rvu, pcifunc, block->type);
	}

	spin_unlock(&rvu->rsrc_lock);
	return 0;
}

static int rvu_mbox_handler_DETACH_RESOURCES(struct rvu *rvu,
					     struct rsrc_detach *detach,
					     struct msg_rsp *rsp)
{
	return rvu_detach_rsrcs(rvu, detach, detach->hdr.pcifunc);
}

static void rvu_attach_block(struct rvu *rvu, int pcifunc,
			     int blktype, int num_lfs)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int slot, lf;
	int blkaddr;
	u64 cfg;

	if (!num_lfs)
		return;

	blkaddr = rvu_get_blkaddr(rvu, blktype, 0);
	if (blkaddr < 0)
		return;

	block = &hw->block[blkaddr];
	if (!block->lf.bmap)
		return;

	for (slot = 0; slot < num_lfs; slot++) {
		/* Allocate the resource */
		lf = rvu_alloc_rsrc(&block->lf);
		if (lf < 0)
			return;

		cfg = (1ULL << 63) | (pcifunc << 8) | slot;
		rvu_write64(rvu, blkaddr, block->lfcfg_reg |
			    (lf << block->lfshift), cfg);
		rvu_update_rsrc_map(rvu, pfvf, block,
				    pcifunc, lf, true);

		/* Set start MSIX vector for this LF within this PF/VF */
		rvu_set_msix_offset(rvu, pfvf, block, lf);
	}
}

static int rvu_check_rsrc_availability(struct rvu *rvu,
				       struct rsrc_attach *req, u16 pcifunc)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int free_lfs, mappedlfs;

	/* Only one NPA LF can be attached */
	if (req->npalf && !rvu_get_rsrc_mapcount(pfvf, BLKTYPE_NPA)) {
		block = &hw->block[BLKADDR_NPA];
		free_lfs = rvu_rsrc_free_count(&block->lf);
		if (!free_lfs)
			goto fail;
	} else if (req->npalf) {
		dev_err(&rvu->pdev->dev,
			"Func 0x%x: Invalid req, already has NPA\n",
			 pcifunc);
		return -EINVAL;
	}

	/* Only one NIX LF can be attached */
	if (req->nixlf && !rvu_get_rsrc_mapcount(pfvf, BLKTYPE_NIX)) {
		block = &hw->block[BLKADDR_NIX0];
		free_lfs = rvu_rsrc_free_count(&block->lf);
		if (!free_lfs)
			goto fail;
	} else if (req->nixlf) {
		dev_err(&rvu->pdev->dev,
			"Func 0x%x: Invalid req, already has NIX\n",
			pcifunc);
		return -EINVAL;
	}

	if (req->sso) {
		block = &hw->block[BLKADDR_SSO];
		/* Is request within limits ? */
		if (req->sso > block->lf.max) {
			dev_err(&rvu->pdev->dev,
				"Func 0x%x: Invalid SSO req, %d > max %d\n",
				 pcifunc, req->sso, block->lf.max);
			return -EINVAL;
		}
		mappedlfs = rvu_get_rsrc_mapcount(pfvf, block->type);
		free_lfs = rvu_rsrc_free_count(&block->lf);
		/* Check if additional resources are available */
		if (req->sso > mappedlfs &&
		    ((req->sso - mappedlfs) > free_lfs))
			goto fail;
	}

	if (req->ssow) {
		block = &hw->block[BLKADDR_SSOW];
		if (req->ssow > block->lf.max) {
			dev_err(&rvu->pdev->dev,
				"Func 0x%x: Invalid SSOW req, %d > max %d\n",
				 pcifunc, req->sso, block->lf.max);
			return -EINVAL;
		}
		mappedlfs = rvu_get_rsrc_mapcount(pfvf, block->type);
		free_lfs = rvu_rsrc_free_count(&block->lf);
		if (req->ssow > mappedlfs &&
		    ((req->ssow - mappedlfs) > free_lfs))
			goto fail;
	}

	if (req->timlfs) {
		block = &hw->block[BLKADDR_TIM];
		if (req->timlfs > block->lf.max) {
			dev_err(&rvu->pdev->dev,
				"Func 0x%x: Invalid TIMLF req, %d > max %d\n",
				 pcifunc, req->timlfs, block->lf.max);
			return -EINVAL;
		}
		mappedlfs = rvu_get_rsrc_mapcount(pfvf, block->type);
		free_lfs = rvu_rsrc_free_count(&block->lf);
		if (req->timlfs > mappedlfs &&
		    ((req->timlfs - mappedlfs) > free_lfs))
			goto fail;
	}

	if (req->cptlfs) {
		block = &hw->block[BLKADDR_CPT0];
		if (req->cptlfs > block->lf.max) {
			dev_err(&rvu->pdev->dev,
				"Func 0x%x: Invalid CPTLF req, %d > max %d\n",
				 pcifunc, req->cptlfs, block->lf.max);
			return -EINVAL;
		}
		mappedlfs = rvu_get_rsrc_mapcount(pfvf, block->type);
		free_lfs = rvu_rsrc_free_count(&block->lf);
		if (req->cptlfs > mappedlfs &&
		    ((req->cptlfs - mappedlfs) > free_lfs))
			goto fail;
	}

	return 0;

fail:
	dev_info(rvu->dev, "Request for %s failed\n", block->name);
	return -ENOSPC;
}

static int rvu_mbox_handler_ATTACH_RESOURCES(struct rvu *rvu,
					     struct rsrc_attach *attach,
					     struct msg_rsp *rsp)
{
	u16 pcifunc = attach->hdr.pcifunc;
	int err;

	/* If first request, detach all existing attached resources */
	if (!attach->modify)
		rvu_detach_rsrcs(rvu, NULL, pcifunc);

	spin_lock(&rvu->rsrc_lock);

	/* Check if the request can be accommodated */
	err = rvu_check_rsrc_availability(rvu, attach, pcifunc);
	if (err)
		goto exit;

	/* Now attach the requested resources */
	if (attach->npalf)
		rvu_attach_block(rvu, pcifunc, BLKTYPE_NPA, 1);

	if (attach->nixlf)
		rvu_attach_block(rvu, pcifunc, BLKTYPE_NIX, 1);

	if (attach->sso) {
		/* RVU func doesn't know which exact LF or slot is attached
		 * to it, it always sees as slot 0,1,2. So for a 'modify'
		 * request, simply detach all existing attached LFs/slots
		 * and attach a fresh.
		 */
		if (attach->modify)
			rvu_detach_block(rvu, pcifunc, BLKTYPE_SSO);
		rvu_attach_block(rvu, pcifunc, BLKTYPE_SSO, attach->sso);
	}

	if (attach->ssow) {
		if (attach->modify)
			rvu_detach_block(rvu, pcifunc, BLKTYPE_SSOW);
		rvu_attach_block(rvu, pcifunc, BLKTYPE_SSOW, attach->ssow);
	}

	if (attach->timlfs) {
		if (attach->modify)
			rvu_detach_block(rvu, pcifunc, BLKTYPE_TIM);
		rvu_attach_block(rvu, pcifunc, BLKTYPE_TIM, attach->timlfs);
	}

	if (attach->cptlfs) {
		if (attach->modify)
			rvu_detach_block(rvu, pcifunc, BLKTYPE_CPT);
		rvu_attach_block(rvu, pcifunc, BLKTYPE_CPT, attach->cptlfs);
	}

exit:
	spin_unlock(&rvu->rsrc_lock);
	return err;
}

static u16 rvu_get_msix_offset(struct rvu *rvu, struct rvu_pfvf *pfvf,
			       int blkaddr, int lf)
{
	u16 vec;

	if (lf < 0)
		return MSIX_VECTOR_INVALID;

	for (vec = 0; vec < pfvf->msix.max; vec++) {
		if (pfvf->msix_lfmap[vec] == MSIX_BLKLF(blkaddr, lf))
			return vec;
	}
	return MSIX_VECTOR_INVALID;
}

static void rvu_set_msix_offset(struct rvu *rvu, struct rvu_pfvf *pfvf,
				struct rvu_block *block, int lf)
{
	u16 nvecs, vec, offset;
	u64 cfg;

	cfg = rvu_read64(rvu, block->addr, block->msixcfg_reg |
			 (lf << block->lfshift));
	nvecs = (cfg >> 12) & 0xFF;

	/* Check and alloc MSIX vectors, must be contiguous */
	if (!rvu_rsrc_check_contig(&pfvf->msix, nvecs))
		return;

	offset = rvu_alloc_rsrc_contig(&pfvf->msix, nvecs);

	/* Config MSIX offset in LF */
	rvu_write64(rvu, block->addr, block->msixcfg_reg |
		    (lf << block->lfshift), (cfg & ~0x7FFULL) | offset);

	/* Update the bitmap as well */
	for (vec = 0; vec < nvecs; vec++)
		pfvf->msix_lfmap[offset + vec] = MSIX_BLKLF(block->addr, lf);
}

static void rvu_clear_msix_offset(struct rvu *rvu, struct rvu_pfvf *pfvf,
				  struct rvu_block *block, int lf)
{
	u16 nvecs, vec, offset;
	u64 cfg;

	cfg = rvu_read64(rvu, block->addr, block->msixcfg_reg |
			 (lf << block->lfshift));
	nvecs = (cfg >> 12) & 0xFF;

	/* Clear MSIX offset in LF */
	rvu_write64(rvu, block->addr, block->msixcfg_reg |
		    (lf << block->lfshift), cfg & ~0x7FFULL);

	offset = rvu_get_msix_offset(rvu, pfvf, block->addr, lf);

	/* Update the mapping */
	for (vec = 0; vec < nvecs; vec++)
		pfvf->msix_lfmap[offset + vec] = 0;

	/* Free the same in MSIX bitmap */
	rvu_free_rsrc_contig(&pfvf->msix, nvecs, offset);
}

static int rvu_mbox_handler_MSIX_OFFSET(struct rvu *rvu, struct msg_req *req,
					struct msix_offset_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_pfvf *pfvf;
	int lf, slot;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	if (!pfvf->msix.bmap)
		return 0;

	/* Set MSIX offsets for each block's LFs attached to this PF/VF */
	lf = rvu_get_lf(rvu, &hw->block[BLKADDR_NPA], pcifunc, 0);
	rsp->npa_msixoff = rvu_get_msix_offset(rvu, pfvf, BLKADDR_NPA, lf);

	lf = rvu_get_lf(rvu, &hw->block[BLKADDR_NIX0], pcifunc, 0);
	rsp->nix_msixoff = rvu_get_msix_offset(rvu, pfvf, BLKADDR_NIX0, lf);

	rsp->sso = pfvf->sso;
	for (slot = 0; slot < rsp->sso; slot++) {
		lf = rvu_get_lf(rvu, &hw->block[BLKADDR_SSO], pcifunc, slot);
		rsp->sso_msixoff[slot] =
			rvu_get_msix_offset(rvu, pfvf, BLKADDR_SSO, lf);
	}

	rsp->ssow = pfvf->ssow;
	for (slot = 0; slot < rsp->ssow; slot++) {
		lf = rvu_get_lf(rvu, &hw->block[BLKADDR_SSOW], pcifunc, slot);
		rsp->ssow_msixoff[slot] =
			rvu_get_msix_offset(rvu, pfvf, BLKADDR_SSOW, lf);
	}

	rsp->timlfs = pfvf->timlfs;
	for (slot = 0; slot < rsp->timlfs; slot++) {
		lf = rvu_get_lf(rvu, &hw->block[BLKADDR_TIM], pcifunc, slot);
		rsp->timlf_msixoff[slot] =
			rvu_get_msix_offset(rvu, pfvf, BLKADDR_TIM, lf);
	}

	rsp->cptlfs = pfvf->cptlfs;
	for (slot = 0; slot < rsp->cptlfs; slot++) {
		lf = rvu_get_lf(rvu, &hw->block[BLKADDR_CPT0], pcifunc, slot);
		rsp->cptlf_msixoff[slot] =
			rvu_get_msix_offset(rvu, pfvf, BLKADDR_CPT0, lf);
	}
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

static void rvu_mbox_up_handler(struct work_struct *work)
{
	struct rvu_work *mwork = container_of(work, struct rvu_work, work);
	struct rvu *rvu = mwork->rvu;
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;
	struct otx2_mbox *mbox;
	int offset, id;
	u16 pf;

	mbox = &rvu->mbox_up;
	pf = mwork - rvu->mbox_wrk_up;
	mdev = &mbox->dev[pf];

	rsp_hdr = mdev->mbase + mbox->rx_start;
	if (rsp_hdr->num_msgs == 0) {
		dev_warn(rvu->dev, "mbox up handler: num_msgs = 0\n");
		return;
	}

	offset = mbox->rx_start + ALIGN(sizeof(*rsp_hdr), MBOX_MSG_ALIGN);

	for (id = 0; id < rsp_hdr->num_msgs; id++) {
		msg = mdev->mbase + offset;

		if (msg->id >= MBOX_MSG_MAX) {
			dev_err(rvu->dev,
				"Mbox msg with unknown ID 0x%x\n", msg->id);
			goto end;
		}

		if (msg->sig != OTX2_MBOX_RSP_SIG) {
			dev_err(rvu->dev,
				"Mbox msg with wrong signature %x, ID 0x%x\n",
				msg->sig, msg->id);
			goto end;
		}

		switch (msg->id) {
		case MBOX_MSG_CGX_LINK_EVENT:
			break;
		default:
			if (msg->rc)
				dev_err(rvu->dev,
					"Mbox msg response has err %d, ID 0x%x\n",
					msg->rc, msg->id);
			break;
		}
end:
		offset = mbox->rx_start + msg->next_msgoff;
		mdev->msgs_acked++;
	}

	otx2_mbox_reset(mbox, 0);
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

	rvu->mbox_wrk_up = devm_kcalloc(rvu->dev, hw->total_pfs,
					sizeof(struct rvu_work), GFP_KERNEL);
	if (!rvu->mbox_wrk_up) {
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

	err = otx2_mbox_init(&rvu->mbox_up, hwbase, rvu->pdev, rvu->afreg_base,
			     MBOX_DIR_AFPF_UP, hw->total_pfs);
	if (err)
		goto exit;

	for (pf = 0; pf < hw->total_pfs; pf++) {
		mwork = &rvu->mbox_wrk[pf];
		mwork->rvu = rvu;
		INIT_WORK(&mwork->work, rvu_mbox_handler);
	}

	for (pf = 0; pf < hw->total_pfs; pf++) {
		mwork = &rvu->mbox_wrk_up[pf];
		mwork->rvu = rvu;
		INIT_WORK(&mwork->work, rvu_mbox_up_handler);
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
	otx2_mbox_destroy(&rvu->mbox_up);
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
			mbox = &rvu->mbox_up;
			mdev = &mbox->dev[pf];
			hdr = mdev->mbase + mbox->rx_start;
			if (hdr->num_msgs)
				queue_work(rvu->mbox_wq,
					   &rvu->mbox_wrk_up[pf].work);
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

	err = rvu_cgx_probe(rvu);
	if (err)
		goto err_mbox;

	err = rvu_register_interrupts(rvu);
	if (err)
		goto err_cgx;

	return 0;
err_cgx:
	rvu_cgx_wq_destroy(rvu);
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
	rvu_cgx_wq_destroy(rvu);
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
	int err;

	pr_info("%s: %s\n", DRV_NAME, DRV_STRING);

	err = pci_register_driver(&cgx_driver);
	if (err < 0)
		return err;

	err =  pci_register_driver(&rvu_driver);
	if (err < 0)
		pci_unregister_driver(&cgx_driver);

	return err;
}

static void __exit rvu_cleanup_module(void)
{
	pci_unregister_driver(&rvu_driver);
	pci_unregister_driver(&cgx_driver);
}

module_init(rvu_init_module);
module_exit(rvu_cleanup_module);
