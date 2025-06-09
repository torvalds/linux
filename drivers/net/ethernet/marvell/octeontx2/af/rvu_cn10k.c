// SPDX-License-Identifier: GPL-2.0
/* Marvell RPM CN10K driver
 *
 * Copyright (C) 2020 Marvell.
 */

#include <linux/bitfield.h>
#include <linux/pci.h>
#include "rvu.h"
#include "cgx.h"
#include "rvu_reg.h"

/* RVU LMTST */
#define LMT_TBL_OP_READ		0
#define LMT_TBL_OP_WRITE	1
#define LMT_MAPTBL_ENTRY_SIZE	16
#define LMT_MAX_VFS		256

#define LMT_MAP_ENTRY_ENA      BIT_ULL(20)
#define LMT_MAP_ENTRY_LINES    GENMASK_ULL(18, 16)

/* Function to perform operations (read/write) on lmtst map table */
static int lmtst_map_table_ops(struct rvu *rvu, u32 index, u64 *val,
			       int lmt_tbl_op)
{
	void __iomem *lmt_map_base;
	u64 tbl_base, cfg;
	int pfs, vfs;

	tbl_base = rvu_read64(rvu, BLKADDR_APR, APR_AF_LMT_MAP_BASE);
	cfg  = rvu_read64(rvu, BLKADDR_APR, APR_AF_LMT_CFG);
	vfs = 1 << (cfg & 0xF);
	pfs = 1 << ((cfg >> 4) & 0x7);

	lmt_map_base = ioremap_wc(tbl_base, pfs * vfs * LMT_MAPTBL_ENTRY_SIZE);
	if (!lmt_map_base) {
		dev_err(rvu->dev, "Failed to setup lmt map table mapping!!\n");
		return -ENOMEM;
	}

	if (lmt_tbl_op == LMT_TBL_OP_READ) {
		*val = readq(lmt_map_base + index);
	} else {
		writeq((*val), (lmt_map_base + index));

		cfg = FIELD_PREP(LMT_MAP_ENTRY_ENA, 0x1);
		/* 2048 LMTLINES */
		cfg |= FIELD_PREP(LMT_MAP_ENTRY_LINES, 0x6);

		writeq(cfg, (lmt_map_base + (index + 8)));

		/* Flushing the AP interceptor cache to make APR_LMT_MAP_ENTRY_S
		 * changes effective. Write 1 for flush and read is being used as a
		 * barrier and sets up a data dependency. Write to 0 after a write
		 * to 1 to complete the flush.
		 */
		rvu_write64(rvu, BLKADDR_APR, APR_AF_LMT_CTL, BIT_ULL(0));
		rvu_read64(rvu, BLKADDR_APR, APR_AF_LMT_CTL);
		rvu_write64(rvu, BLKADDR_APR, APR_AF_LMT_CTL, 0x00);
	}

	iounmap(lmt_map_base);
	return 0;
}

#define LMT_MAP_TBL_W1_OFF  8
static u32 rvu_get_lmtst_tbl_index(struct rvu *rvu, u16 pcifunc)
{
	return ((rvu_get_pf(pcifunc) * LMT_MAX_VFS) +
		(pcifunc & RVU_PFVF_FUNC_MASK)) * LMT_MAPTBL_ENTRY_SIZE;
}

static int rvu_get_lmtaddr(struct rvu *rvu, u16 pcifunc,
			   u64 iova, u64 *lmt_addr)
{
	u64 pa, val, pf;
	int err = 0;

	if (!iova) {
		dev_err(rvu->dev, "%s Requested Null address for transulation\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&rvu->rsrc_lock);
	rvu_write64(rvu, BLKADDR_RVUM, RVU_AF_SMMU_ADDR_REQ, iova);
	pf = rvu_get_pf(pcifunc) & RVU_PFVF_PF_MASK;
	val = BIT_ULL(63) | BIT_ULL(14) | BIT_ULL(13) | pf << 8 |
	      ((pcifunc & RVU_PFVF_FUNC_MASK) & 0xFF);
	rvu_write64(rvu, BLKADDR_RVUM, RVU_AF_SMMU_TXN_REQ, val);

	err = rvu_poll_reg(rvu, BLKADDR_RVUM, RVU_AF_SMMU_ADDR_RSP_STS, BIT_ULL(0), false);
	if (err) {
		dev_err(rvu->dev, "%s LMTLINE iova transulation failed\n", __func__);
		goto exit;
	}
	val = rvu_read64(rvu, BLKADDR_RVUM, RVU_AF_SMMU_ADDR_RSP_STS);
	if (val & ~0x1ULL) {
		dev_err(rvu->dev, "%s LMTLINE iova transulation failed err:%llx\n", __func__, val);
		err = -EIO;
		goto exit;
	}
	/* PA[51:12] = RVU_AF_SMMU_TLN_FLIT0[57:18]
	 * PA[11:0] = IOVA[11:0]
	 */
	pa = rvu_read64(rvu, BLKADDR_RVUM, RVU_AF_SMMU_TLN_FLIT0) >> 18;
	pa &= GENMASK_ULL(39, 0);
	*lmt_addr = (pa << 12) | (iova  & 0xFFF);
exit:
	mutex_unlock(&rvu->rsrc_lock);
	return err;
}

static int rvu_update_lmtaddr(struct rvu *rvu, u16 pcifunc, u64 lmt_addr)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	u32 tbl_idx;
	int err = 0;
	u64 val;

	/* Read the current lmt addr of pcifunc */
	tbl_idx = rvu_get_lmtst_tbl_index(rvu, pcifunc);
	err = lmtst_map_table_ops(rvu, tbl_idx, &val, LMT_TBL_OP_READ);
	if (err) {
		dev_err(rvu->dev,
			"Failed to read LMT map table: index 0x%x err %d\n",
			tbl_idx, err);
		return err;
	}

	/* Storing the seondary's lmt base address as this needs to be
	 * reverted in FLR. Also making sure this default value doesn't
	 * get overwritten on multiple calls to this mailbox.
	 */
	if (!pfvf->lmt_base_addr)
		pfvf->lmt_base_addr = val;

	/* Update the LMT table with new addr */
	err = lmtst_map_table_ops(rvu, tbl_idx, &lmt_addr, LMT_TBL_OP_WRITE);
	if (err) {
		dev_err(rvu->dev,
			"Failed to update LMT map table: index 0x%x err %d\n",
			tbl_idx, err);
		return err;
	}
	return 0;
}

int rvu_mbox_handler_lmtst_tbl_setup(struct rvu *rvu,
				     struct lmtst_tbl_setup_req *req,
				     struct msg_rsp *rsp)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);
	u32 pri_tbl_idx, tbl_idx;
	u64 lmt_addr;
	int err = 0;
	u64 val;

	/* Check if PF_FUNC wants to use it's own local memory as LMTLINE
	 * region, if so, convert that IOVA to physical address and
	 * populate LMT table with that address
	 */
	if (req->use_local_lmt_region) {
		err = rvu_get_lmtaddr(rvu, req->hdr.pcifunc,
				      req->lmt_iova, &lmt_addr);
		if (err < 0)
			return err;

		/* Update the lmt addr for this PFFUNC in the LMT table */
		err = rvu_update_lmtaddr(rvu, req->hdr.pcifunc, lmt_addr);
		if (err)
			return err;
	}

	/* Reconfiguring lmtst map table in lmt region shared mode i.e. make
	 * multiple PF_FUNCs to share an LMTLINE region, so primary/base
	 * pcifunc (which is passed as an argument to mailbox) is the one
	 * whose lmt base address will be shared among other secondary
	 * pcifunc (will be the one who is calling this mailbox).
	 */
	if (req->base_pcifunc) {
		/* Calculating the LMT table index equivalent to primary
		 * pcifunc.
		 */
		pri_tbl_idx = rvu_get_lmtst_tbl_index(rvu, req->base_pcifunc);

		/* Read the base lmt addr of the primary pcifunc */
		err = lmtst_map_table_ops(rvu, pri_tbl_idx, &val,
					  LMT_TBL_OP_READ);
		if (err) {
			dev_err(rvu->dev,
				"Failed to read LMT map table: index 0x%x err %d\n",
				pri_tbl_idx, err);
			goto error;
		}

		/* Update the base lmt addr of secondary with primary's base
		 * lmt addr.
		 */
		err = rvu_update_lmtaddr(rvu, req->hdr.pcifunc, val);
		if (err)
			return err;
	}

	/* This mailbox can also be used to update word1 of APR_LMT_MAP_ENTRY_S
	 * like enabling scheduled LMTST, disable LMTLINE prefetch, disable
	 * early completion for ordered LMTST.
	 */
	if (req->sch_ena || req->dis_sched_early_comp || req->dis_line_pref) {
		tbl_idx = rvu_get_lmtst_tbl_index(rvu, req->hdr.pcifunc);
		err = lmtst_map_table_ops(rvu, tbl_idx + LMT_MAP_TBL_W1_OFF,
					  &val, LMT_TBL_OP_READ);
		if (err) {
			dev_err(rvu->dev,
				"Failed to read LMT map table: index 0x%x err %d\n",
				tbl_idx + LMT_MAP_TBL_W1_OFF, err);
			goto error;
		}

		/* Storing lmt map table entry word1 default value as this needs
		 * to be reverted in FLR. Also making sure this default value
		 * doesn't get overwritten on multiple calls to this mailbox.
		 */
		if (!pfvf->lmt_map_ent_w1)
			pfvf->lmt_map_ent_w1 = val;

		/* Disable early completion for Ordered LMTSTs. */
		if (req->dis_sched_early_comp)
			val |= (req->dis_sched_early_comp <<
				APR_LMT_MAP_ENT_DIS_SCH_CMP_SHIFT);
		/* Enable scheduled LMTST */
		if (req->sch_ena)
			val |= (req->sch_ena << APR_LMT_MAP_ENT_SCH_ENA_SHIFT) |
				req->ssow_pf_func;
		/* Disables LMTLINE prefetch before receiving store data. */
		if (req->dis_line_pref)
			val |= (req->dis_line_pref <<
				APR_LMT_MAP_ENT_DIS_LINE_PREF_SHIFT);

		err = lmtst_map_table_ops(rvu, tbl_idx + LMT_MAP_TBL_W1_OFF,
					  &val, LMT_TBL_OP_WRITE);
		if (err) {
			dev_err(rvu->dev,
				"Failed to update LMT map table: index 0x%x err %d\n",
				tbl_idx + LMT_MAP_TBL_W1_OFF, err);
			goto error;
		}
	}

error:
	return err;
}

/* Resetting the lmtst map table to original base addresses */
void rvu_reset_lmt_map_tbl(struct rvu *rvu, u16 pcifunc)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	u32 tbl_idx;
	int err;

	if (is_rvu_otx2(rvu))
		return;

	if (pfvf->lmt_base_addr || pfvf->lmt_map_ent_w1) {
		/* This corresponds to lmt map table index */
		tbl_idx = rvu_get_lmtst_tbl_index(rvu, pcifunc);
		/* Reverting back original lmt base addr for respective
		 * pcifunc.
		 */
		if (pfvf->lmt_base_addr) {
			err = lmtst_map_table_ops(rvu, tbl_idx,
						  &pfvf->lmt_base_addr,
						  LMT_TBL_OP_WRITE);
			if (err)
				dev_err(rvu->dev,
					"Failed to update LMT map table: index 0x%x err %d\n",
					tbl_idx, err);
			pfvf->lmt_base_addr = 0;
		}
		/* Reverting back to orginal word1 val of lmtst map table entry
		 * which underwent changes.
		 */
		if (pfvf->lmt_map_ent_w1) {
			err = lmtst_map_table_ops(rvu,
						  tbl_idx + LMT_MAP_TBL_W1_OFF,
						  &pfvf->lmt_map_ent_w1,
						  LMT_TBL_OP_WRITE);
			if (err)
				dev_err(rvu->dev,
					"Failed to update LMT map table: index 0x%x err %d\n",
					tbl_idx + LMT_MAP_TBL_W1_OFF, err);
			pfvf->lmt_map_ent_w1 = 0;
		}
	}
}

int rvu_set_channels_base(struct rvu *rvu)
{
	u16 nr_lbk_chans, nr_sdp_chans, nr_cgx_chans, nr_cpt_chans;
	u16 sdp_chan_base, cgx_chan_base, cpt_chan_base;
	struct rvu_hwinfo *hw = rvu->hw;
	u64 nix_const, nix_const1;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return blkaddr;

	nix_const = rvu_read64(rvu, blkaddr, NIX_AF_CONST);
	nix_const1 = rvu_read64(rvu, blkaddr, NIX_AF_CONST1);

	hw->cgx = (nix_const >> 12) & 0xFULL;
	hw->lmac_per_cgx = (nix_const >> 8) & 0xFULL;
	hw->cgx_links = hw->cgx * hw->lmac_per_cgx;
	hw->lbk_links = (nix_const >> 24) & 0xFULL;
	hw->cpt_links = (nix_const >> 44) & 0xFULL;
	hw->sdp_links = 1;

	hw->cgx_chan_base = NIX_CHAN_CGX_LMAC_CHX(0, 0, 0);
	hw->lbk_chan_base = NIX_CHAN_LBK_CHX(0, 0);
	hw->sdp_chan_base = NIX_CHAN_SDP_CH_START;

	/* No Programmable channels */
	if (!(nix_const & BIT_ULL(60)))
		return 0;

	hw->cap.programmable_chans = true;

	/* If programmable channels are present then configure
	 * channels such that all channel numbers are contiguous
	 * leaving no holes. This way the new CPT channels can be
	 * accomodated. The order of channel numbers assigned is
	 * LBK, SDP, CGX and CPT. Also the base channel number
	 * of a block must be multiple of number of channels
	 * of the block.
	 */
	nr_lbk_chans = (nix_const >> 16) & 0xFFULL;
	nr_sdp_chans = nix_const1 & 0xFFFULL;
	nr_cgx_chans = nix_const & 0xFFULL;
	nr_cpt_chans = (nix_const >> 32) & 0xFFFULL;

	sdp_chan_base = hw->lbk_chan_base + hw->lbk_links * nr_lbk_chans;
	/* Round up base channel to multiple of number of channels */
	hw->sdp_chan_base = ALIGN(sdp_chan_base, nr_sdp_chans);

	cgx_chan_base = hw->sdp_chan_base + hw->sdp_links * nr_sdp_chans;
	hw->cgx_chan_base = ALIGN(cgx_chan_base, nr_cgx_chans);

	cpt_chan_base = hw->cgx_chan_base + hw->cgx_links * nr_cgx_chans;
	hw->cpt_chan_base = ALIGN(cpt_chan_base, nr_cpt_chans);

	/* Out of 4096 channels start CPT from 2048 so
	 * that MSB for CPT channels is always set
	 */
	if (cpt_chan_base <= NIX_CHAN_CPT_CH_START) {
		hw->cpt_chan_base = NIX_CHAN_CPT_CH_START;
	} else {
		dev_err(rvu->dev,
			"CPT channels could not fit in the range 2048-4095\n");
		return -EINVAL;
	}

	return 0;
}

#define LBK_CONNECT_NIXX(a)		(0x0 + (a))

static void __rvu_lbk_set_chans(struct rvu *rvu, void __iomem *base,
				u64 offset, int lbkid, u16 chans)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u64 cfg;

	cfg = readq(base + offset);
	cfg &= ~(LBK_LINK_CFG_RANGE_MASK |
		 LBK_LINK_CFG_ID_MASK | LBK_LINK_CFG_BASE_MASK);
	cfg |=	FIELD_PREP(LBK_LINK_CFG_RANGE_MASK, ilog2(chans));
	cfg |=	FIELD_PREP(LBK_LINK_CFG_ID_MASK, lbkid);
	cfg |=	FIELD_PREP(LBK_LINK_CFG_BASE_MASK, hw->lbk_chan_base);

	writeq(cfg, base + offset);
}

static void rvu_lbk_set_channels(struct rvu *rvu)
{
	struct pci_dev *pdev = NULL;
	void __iomem *base;
	u64 lbk_const;
	u8 src, dst;
	u16 chans;

	/* To loopback packets between multiple NIX blocks
	 * mutliple LBK blocks are needed. With two NIX blocks,
	 * four LBK blocks are needed and each LBK block
	 * source and destination are as follows:
	 * LBK0 - source NIX0 and destination NIX1
	 * LBK1 - source NIX0 and destination NIX1
	 * LBK2 - source NIX1 and destination NIX0
	 * LBK3 - source NIX1 and destination NIX1
	 * As per the HRM channel numbers should be programmed as:
	 * P2X and X2P of LBK0 as same
	 * P2X and X2P of LBK3 as same
	 * P2X of LBK1 and X2P of LBK2 as same
	 * P2X of LBK2 and X2P of LBK1 as same
	 */
	while (true) {
		pdev = pci_get_device(PCI_VENDOR_ID_CAVIUM,
				      PCI_DEVID_OCTEONTX2_LBK, pdev);
		if (!pdev)
			return;

		base = pci_ioremap_bar(pdev, 0);
		if (!base)
			goto err_put;

		lbk_const = readq(base + LBK_CONST);
		chans = FIELD_GET(LBK_CONST_CHANS, lbk_const);
		dst = FIELD_GET(LBK_CONST_DST, lbk_const);
		src = FIELD_GET(LBK_CONST_SRC, lbk_const);

		if (src == dst) {
			if (src == LBK_CONNECT_NIXX(0)) { /* LBK0 */
				__rvu_lbk_set_chans(rvu, base, LBK_LINK_CFG_X2P,
						    0, chans);
				__rvu_lbk_set_chans(rvu, base, LBK_LINK_CFG_P2X,
						    0, chans);
			} else if (src == LBK_CONNECT_NIXX(1)) { /* LBK3 */
				__rvu_lbk_set_chans(rvu, base, LBK_LINK_CFG_X2P,
						    1, chans);
				__rvu_lbk_set_chans(rvu, base, LBK_LINK_CFG_P2X,
						    1, chans);
			}
		} else {
			if (src == LBK_CONNECT_NIXX(0)) { /* LBK1 */
				__rvu_lbk_set_chans(rvu, base, LBK_LINK_CFG_X2P,
						    0, chans);
				__rvu_lbk_set_chans(rvu, base, LBK_LINK_CFG_P2X,
						    1, chans);
			} else if (src == LBK_CONNECT_NIXX(1)) { /* LBK2 */
				__rvu_lbk_set_chans(rvu, base, LBK_LINK_CFG_X2P,
						    1, chans);
				__rvu_lbk_set_chans(rvu, base, LBK_LINK_CFG_P2X,
						    0, chans);
			}
		}
		iounmap(base);
	}
err_put:
	pci_dev_put(pdev);
}

static void __rvu_nix_set_channels(struct rvu *rvu, int blkaddr)
{
	u64 nix_const1 = rvu_read64(rvu, blkaddr, NIX_AF_CONST1);
	u64 nix_const = rvu_read64(rvu, blkaddr, NIX_AF_CONST);
	u16 cgx_chans, lbk_chans, sdp_chans, cpt_chans;
	struct rvu_hwinfo *hw = rvu->hw;
	int link, nix_link = 0;
	u16 start;
	u64 cfg;

	cgx_chans = nix_const & 0xFFULL;
	lbk_chans = (nix_const >> 16) & 0xFFULL;
	sdp_chans = nix_const1 & 0xFFFULL;
	cpt_chans = (nix_const >> 32) & 0xFFFULL;

	start = hw->cgx_chan_base;
	for (link = 0; link < hw->cgx_links; link++, nix_link++) {
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_LINKX_CFG(nix_link));
		cfg &= ~(NIX_AF_LINKX_BASE_MASK | NIX_AF_LINKX_RANGE_MASK);
		cfg |=	FIELD_PREP(NIX_AF_LINKX_RANGE_MASK, ilog2(cgx_chans));
		cfg |=	FIELD_PREP(NIX_AF_LINKX_BASE_MASK, start);
		rvu_write64(rvu, blkaddr, NIX_AF_LINKX_CFG(nix_link), cfg);
		start += cgx_chans;
	}

	start = hw->lbk_chan_base;
	for (link = 0; link < hw->lbk_links; link++, nix_link++) {
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_LINKX_CFG(nix_link));
		cfg &= ~(NIX_AF_LINKX_BASE_MASK | NIX_AF_LINKX_RANGE_MASK);
		cfg |=	FIELD_PREP(NIX_AF_LINKX_RANGE_MASK, ilog2(lbk_chans));
		cfg |=	FIELD_PREP(NIX_AF_LINKX_BASE_MASK, start);
		rvu_write64(rvu, blkaddr, NIX_AF_LINKX_CFG(nix_link), cfg);
		start += lbk_chans;
	}

	start = hw->sdp_chan_base;
	for (link = 0; link < hw->sdp_links; link++, nix_link++) {
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_LINKX_CFG(nix_link));
		cfg &= ~(NIX_AF_LINKX_BASE_MASK | NIX_AF_LINKX_RANGE_MASK);
		cfg |=	FIELD_PREP(NIX_AF_LINKX_RANGE_MASK, ilog2(sdp_chans));
		cfg |=	FIELD_PREP(NIX_AF_LINKX_BASE_MASK, start);
		rvu_write64(rvu, blkaddr, NIX_AF_LINKX_CFG(nix_link), cfg);
		start += sdp_chans;
	}

	start = hw->cpt_chan_base;
	for (link = 0; link < hw->cpt_links; link++, nix_link++) {
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_LINKX_CFG(nix_link));
		cfg &= ~(NIX_AF_LINKX_BASE_MASK | NIX_AF_LINKX_RANGE_MASK);
		cfg |=	FIELD_PREP(NIX_AF_LINKX_RANGE_MASK, ilog2(cpt_chans));
		cfg |=	FIELD_PREP(NIX_AF_LINKX_BASE_MASK, start);
		rvu_write64(rvu, blkaddr, NIX_AF_LINKX_CFG(nix_link), cfg);
		start += cpt_chans;
	}
}

static void rvu_nix_set_channels(struct rvu *rvu)
{
	int blkaddr = 0;

	blkaddr = rvu_get_next_nix_blkaddr(rvu, blkaddr);
	while (blkaddr) {
		__rvu_nix_set_channels(rvu, blkaddr);
		blkaddr = rvu_get_next_nix_blkaddr(rvu, blkaddr);
	}
}

static void __rvu_rpm_set_channels(int cgxid, int lmacid, u16 base)
{
	u64 cfg;

	cfg = cgx_lmac_read(cgxid, lmacid, RPMX_CMRX_LINK_CFG);
	cfg &= ~(RPMX_CMRX_LINK_BASE_MASK | RPMX_CMRX_LINK_RANGE_MASK);

	/* There is no read-only constant register to read
	 * the number of channels for LMAC and it is always 16.
	 */
	cfg |=	FIELD_PREP(RPMX_CMRX_LINK_RANGE_MASK, ilog2(16));
	cfg |=	FIELD_PREP(RPMX_CMRX_LINK_BASE_MASK, base);
	cgx_lmac_write(cgxid, lmacid, RPMX_CMRX_LINK_CFG, cfg);
}

static void rvu_rpm_set_channels(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 base = hw->cgx_chan_base;
	int cgx, lmac;

	for (cgx = 0; cgx < rvu->cgx_cnt_max; cgx++) {
		for (lmac = 0; lmac < hw->lmac_per_cgx; lmac++) {
			__rvu_rpm_set_channels(cgx, lmac, base);
			base += 16;
		}
	}
}

void rvu_program_channels(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;

	if (!hw->cap.programmable_chans)
		return;

	rvu_nix_set_channels(rvu);
	rvu_lbk_set_channels(rvu);
	rvu_rpm_set_channels(rvu);
}

void rvu_nix_block_cn10k_init(struct rvu *rvu, struct nix_hw *nix_hw)
{
	int blkaddr = nix_hw->blkaddr;
	u64 cfg;

	/* Set AF vWQE timer interval to a LF configurable range of
	 * 6.4us to 1.632ms.
	 */
	rvu_write64(rvu, blkaddr, NIX_AF_VWQE_TIMER, 0x3FULL);

	/* Enable NIX RX stream and global conditional clock to
	 * avoild multiple free of NPA buffers.
	 */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CFG);
	cfg |= BIT_ULL(1) | BIT_ULL(2);
	rvu_write64(rvu, blkaddr, NIX_AF_CFG, cfg);
}

void rvu_apr_block_cn10k_init(struct rvu *rvu)
{
	u64 reg;

	reg = rvu_read64(rvu, BLKADDR_APR, APR_AF_LMT_CFG);
	reg |=	FIELD_PREP(LMTST_THROTTLE_MASK, LMTST_WR_PEND_MAX);
	rvu_write64(rvu, BLKADDR_APR, APR_AF_LMT_CFG, reg);
}
