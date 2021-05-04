// SPDX-License-Identifier: GPL-2.0
/*  Marvell RPM CN10K driver
 *
 * Copyright (C) 2020 Marvell.
 */

#include <linux/bitfield.h>
#include <linux/pci.h>
#include "rvu.h"
#include "cgx.h"
#include "rvu_reg.h"

int rvu_set_channels_base(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 cpt_chan_base;
	u64 nix_const;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return blkaddr;

	nix_const = rvu_read64(rvu, blkaddr, NIX_AF_CONST);

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
	 * LBK, SDP, CGX and CPT.
	 */
	hw->sdp_chan_base = hw->lbk_chan_base + hw->lbk_links *
				((nix_const >> 16) & 0xFFULL);
	hw->cgx_chan_base = hw->sdp_chan_base + hw->sdp_links * SDP_CHANNELS;

	cpt_chan_base = hw->cgx_chan_base + hw->cgx_links *
				(nix_const & 0xFFULL);

	/* Out of 4096 channels start CPT from 2048 so
	 * that MSB for CPT channels is always set
	 */
	if (cpt_chan_base <= 0x800) {
		hw->cpt_chan_base = 0x800;
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
	u64 nix_const = rvu_read64(rvu, blkaddr, NIX_AF_CONST);
	u16 cgx_chans, lbk_chans, sdp_chans, cpt_chans;
	struct rvu_hwinfo *hw = rvu->hw;
	int link, nix_link = 0;
	u16 start;
	u64 cfg;

	cgx_chans = nix_const & 0xFFULL;
	lbk_chans = (nix_const >> 16) & 0xFFULL;
	sdp_chans = SDP_CHANNELS;
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
