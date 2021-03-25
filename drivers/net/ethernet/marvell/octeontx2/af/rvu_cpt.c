// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include <linux/pci.h>
#include "rvu_struct.h"
#include "rvu_reg.h"
#include "mbox.h"
#include "rvu.h"

/* CPT PF device id */
#define	PCI_DEVID_OTX2_CPT_PF	0xA0FD

static int get_cpt_pf_num(struct rvu *rvu)
{
	int i, domain_nr, cpt_pf_num = -1;
	struct pci_dev *pdev;

	domain_nr = pci_domain_nr(rvu->pdev->bus);
	for (i = 0; i < rvu->hw->total_pfs; i++) {
		pdev = pci_get_domain_bus_and_slot(domain_nr, i + 1, 0);
		if (!pdev)
			continue;

		if (pdev->device == PCI_DEVID_OTX2_CPT_PF) {
			cpt_pf_num = i;
			put_device(&pdev->dev);
			break;
		}
		put_device(&pdev->dev);
	}
	return cpt_pf_num;
}

static bool is_cpt_pf(struct rvu *rvu, u16 pcifunc)
{
	int cpt_pf_num = get_cpt_pf_num(rvu);

	if (rvu_get_pf(pcifunc) != cpt_pf_num)
		return false;
	if (pcifunc & RVU_PFVF_FUNC_MASK)
		return false;

	return true;
}

static bool is_cpt_vf(struct rvu *rvu, u16 pcifunc)
{
	int cpt_pf_num = get_cpt_pf_num(rvu);

	if (rvu_get_pf(pcifunc) != cpt_pf_num)
		return false;
	if (!(pcifunc & RVU_PFVF_FUNC_MASK))
		return false;

	return true;
}

int rvu_mbox_handler_cpt_lf_alloc(struct rvu *rvu,
				  struct cpt_lf_alloc_req_msg *req,
				  struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_block *block;
	int cptlf, blkaddr;
	int num_lfs, slot;
	u64 val;

	blkaddr = req->blkaddr ? req->blkaddr : BLKADDR_CPT0;
	if (blkaddr != BLKADDR_CPT0 && blkaddr != BLKADDR_CPT1)
		return -ENODEV;

	if (req->eng_grpmsk == 0x0)
		return CPT_AF_ERR_GRP_INVALID;

	block = &rvu->hw->block[blkaddr];
	num_lfs = rvu_get_rsrc_mapcount(rvu_get_pfvf(rvu, pcifunc),
					block->addr);
	if (!num_lfs)
		return CPT_AF_ERR_LF_INVALID;

	/* Check if requested 'CPTLF <=> NIXLF' mapping is valid */
	if (req->nix_pf_func) {
		/* If default, use 'this' CPTLF's PFFUNC */
		if (req->nix_pf_func == RVU_DEFAULT_PF_FUNC)
			req->nix_pf_func = pcifunc;
		if (!is_pffunc_map_valid(rvu, req->nix_pf_func, BLKTYPE_NIX))
			return CPT_AF_ERR_NIX_PF_FUNC_INVALID;
	}

	/* Check if requested 'CPTLF <=> SSOLF' mapping is valid */
	if (req->sso_pf_func) {
		/* If default, use 'this' CPTLF's PFFUNC */
		if (req->sso_pf_func == RVU_DEFAULT_PF_FUNC)
			req->sso_pf_func = pcifunc;
		if (!is_pffunc_map_valid(rvu, req->sso_pf_func, BLKTYPE_SSO))
			return CPT_AF_ERR_SSO_PF_FUNC_INVALID;
	}

	for (slot = 0; slot < num_lfs; slot++) {
		cptlf = rvu_get_lf(rvu, block, pcifunc, slot);
		if (cptlf < 0)
			return CPT_AF_ERR_LF_INVALID;

		/* Set CPT LF group and priority */
		val = (u64)req->eng_grpmsk << 48 | 1;
		rvu_write64(rvu, blkaddr, CPT_AF_LFX_CTL(cptlf), val);

		/* Set CPT LF NIX_PF_FUNC and SSO_PF_FUNC */
		val = (u64)req->nix_pf_func << 48 |
		      (u64)req->sso_pf_func << 32;
		rvu_write64(rvu, blkaddr, CPT_AF_LFX_CTL2(cptlf), val);
	}

	return 0;
}

static int cpt_lf_free(struct rvu *rvu, struct msg_req *req, int blkaddr)
{
	u16 pcifunc = req->hdr.pcifunc;
	int num_lfs, cptlf, slot;
	struct rvu_block *block;

	block = &rvu->hw->block[blkaddr];
	num_lfs = rvu_get_rsrc_mapcount(rvu_get_pfvf(rvu, pcifunc),
					block->addr);
	if (!num_lfs)
		return 0;

	for (slot = 0; slot < num_lfs; slot++) {
		cptlf = rvu_get_lf(rvu, block, pcifunc, slot);
		if (cptlf < 0)
			return CPT_AF_ERR_LF_INVALID;

		/* Reset CPT LF group and priority */
		rvu_write64(rvu, blkaddr, CPT_AF_LFX_CTL(cptlf), 0x0);
		/* Reset CPT LF NIX_PF_FUNC and SSO_PF_FUNC */
		rvu_write64(rvu, blkaddr, CPT_AF_LFX_CTL2(cptlf), 0x0);
	}

	return 0;
}

int rvu_mbox_handler_cpt_lf_free(struct rvu *rvu, struct msg_req *req,
				 struct msg_rsp *rsp)
{
	int ret;

	ret = cpt_lf_free(rvu, req, BLKADDR_CPT0);
	if (ret)
		return ret;

	if (is_block_implemented(rvu->hw, BLKADDR_CPT1))
		ret = cpt_lf_free(rvu, req, BLKADDR_CPT1);

	return ret;
}

static bool is_valid_offset(struct rvu *rvu, struct cpt_rd_wr_reg_msg *req)
{
	u64 offset = req->reg_offset;
	int blkaddr, num_lfs, lf;
	struct rvu_block *block;
	struct rvu_pfvf *pfvf;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_CPT, 0);

	/* Registers that can be accessed from PF/VF */
	if ((offset & 0xFF000) ==  CPT_AF_LFX_CTL(0) ||
	    (offset & 0xFF000) ==  CPT_AF_LFX_CTL2(0)) {
		if (offset & 7)
			return false;

		lf = (offset & 0xFFF) >> 3;
		block = &rvu->hw->block[blkaddr];
		pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);
		num_lfs = rvu_get_rsrc_mapcount(pfvf, block->addr);
		if (lf >= num_lfs)
			/* Slot is not valid for that PF/VF */
			return false;

		/* Translate local LF used by VFs to global CPT LF */
		lf = rvu_get_lf(rvu, &rvu->hw->block[blkaddr],
				req->hdr.pcifunc, lf);
		if (lf < 0)
			return false;

		return true;
	} else if (!(req->hdr.pcifunc & RVU_PFVF_FUNC_MASK)) {
		/* Registers that can be accessed from PF */
		switch (offset) {
		case CPT_AF_CTL:
		case CPT_AF_PF_FUNC:
		case CPT_AF_BLK_RST:
		case CPT_AF_CONSTANTS1:
			return true;
		}

		switch (offset & 0xFF000) {
		case CPT_AF_EXEX_STS(0):
		case CPT_AF_EXEX_CTL(0):
		case CPT_AF_EXEX_CTL2(0):
		case CPT_AF_EXEX_UCODE_BASE(0):
			if (offset & 7)
				return false;
			break;
		default:
			return false;
		}
		return true;
	}
	return false;
}

int rvu_mbox_handler_cpt_rd_wr_register(struct rvu *rvu,
					struct cpt_rd_wr_reg_msg *req,
					struct cpt_rd_wr_reg_msg *rsp)
{
	int blkaddr;

	blkaddr = req->blkaddr ? req->blkaddr : BLKADDR_CPT0;
	if (blkaddr != BLKADDR_CPT0 && blkaddr != BLKADDR_CPT1)
		return -ENODEV;

	/* This message is accepted only if sent from CPT PF/VF */
	if (!is_cpt_pf(rvu, req->hdr.pcifunc) &&
	    !is_cpt_vf(rvu, req->hdr.pcifunc))
		return CPT_AF_ERR_ACCESS_DENIED;

	rsp->reg_offset = req->reg_offset;
	rsp->ret_val = req->ret_val;
	rsp->is_write = req->is_write;

	if (!is_valid_offset(rvu, req))
		return CPT_AF_ERR_ACCESS_DENIED;

	if (req->is_write)
		rvu_write64(rvu, blkaddr, req->reg_offset, req->val);
	else
		rsp->val = rvu_read64(rvu, blkaddr, req->reg_offset);

	return 0;
}

#define INPROG_INFLIGHT(reg)    ((reg) & 0x1FF)
#define INPROG_GRB_PARTIAL(reg) ((reg) & BIT_ULL(31))
#define INPROG_GRB(reg)         (((reg) >> 32) & 0xFF)
#define INPROG_GWB(reg)         (((reg) >> 40) & 0xFF)

static void cpt_lf_disable_iqueue(struct rvu *rvu, int blkaddr, int slot)
{
	int i = 0, hard_lp_ctr = 100000;
	u64 inprog, grp_ptr;
	u16 nq_ptr, dq_ptr;

	/* Disable instructions enqueuing */
	rvu_write64(rvu, blkaddr, CPT_AF_BAR2_ALIASX(slot, CPT_LF_CTL), 0x0);

	/* Disable executions in the LF's queue */
	inprog = rvu_read64(rvu, blkaddr,
			    CPT_AF_BAR2_ALIASX(slot, CPT_LF_INPROG));
	inprog &= ~BIT_ULL(16);
	rvu_write64(rvu, blkaddr,
		    CPT_AF_BAR2_ALIASX(slot, CPT_LF_INPROG), inprog);

	/* Wait for CPT queue to become execution-quiescent */
	do {
		inprog = rvu_read64(rvu, blkaddr,
				    CPT_AF_BAR2_ALIASX(slot, CPT_LF_INPROG));
		if (INPROG_GRB_PARTIAL(inprog)) {
			i = 0;
			hard_lp_ctr--;
		} else {
			i++;
		}

		grp_ptr = rvu_read64(rvu, blkaddr,
				     CPT_AF_BAR2_ALIASX(slot,
							CPT_LF_Q_GRP_PTR));
		nq_ptr = (grp_ptr >> 32) & 0x7FFF;
		dq_ptr = grp_ptr & 0x7FFF;

	} while (hard_lp_ctr && (i < 10) && (nq_ptr != dq_ptr));

	if (hard_lp_ctr == 0)
		dev_warn(rvu->dev, "CPT FLR hits hard loop counter\n");

	i = 0;
	hard_lp_ctr = 100000;
	do {
		inprog = rvu_read64(rvu, blkaddr,
				    CPT_AF_BAR2_ALIASX(slot, CPT_LF_INPROG));

		if ((INPROG_INFLIGHT(inprog) == 0) &&
		    (INPROG_GWB(inprog) < 40) &&
		    ((INPROG_GRB(inprog) == 0) ||
		     (INPROG_GRB((inprog)) == 40))) {
			i++;
		} else {
			i = 0;
			hard_lp_ctr--;
		}
	} while (hard_lp_ctr && (i < 10));

	if (hard_lp_ctr == 0)
		dev_warn(rvu->dev, "CPT FLR hits hard loop counter\n");
}

int rvu_cpt_lf_teardown(struct rvu *rvu, u16 pcifunc, int lf, int slot)
{
	int blkaddr;
	u64 reg;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_CPT, pcifunc);
	if (blkaddr != BLKADDR_CPT0 && blkaddr != BLKADDR_CPT1)
		return -EINVAL;

	/* Enable BAR2 ALIAS for this pcifunc. */
	reg = BIT_ULL(16) | pcifunc;
	rvu_write64(rvu, blkaddr, CPT_AF_BAR2_SEL, reg);

	cpt_lf_disable_iqueue(rvu, blkaddr, slot);

	/* Set group drop to help clear out hardware */
	reg = rvu_read64(rvu, blkaddr, CPT_AF_BAR2_ALIASX(slot, CPT_LF_INPROG));
	reg |= BIT_ULL(17);
	rvu_write64(rvu, blkaddr, CPT_AF_BAR2_ALIASX(slot, CPT_LF_INPROG), reg);

	rvu_write64(rvu, blkaddr, CPT_AF_BAR2_SEL, 0);

	return 0;
}
