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
#include <linux/pci.h>

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"
#include "npc.h"
#include "cgx.h"

static int rvu_nix_get_bpid(struct rvu *rvu, struct nix_bp_cfg_req *req,
			    int type, int chan_id);

enum mc_tbl_sz {
	MC_TBL_SZ_256,
	MC_TBL_SZ_512,
	MC_TBL_SZ_1K,
	MC_TBL_SZ_2K,
	MC_TBL_SZ_4K,
	MC_TBL_SZ_8K,
	MC_TBL_SZ_16K,
	MC_TBL_SZ_32K,
	MC_TBL_SZ_64K,
};

enum mc_buf_cnt {
	MC_BUF_CNT_8,
	MC_BUF_CNT_16,
	MC_BUF_CNT_32,
	MC_BUF_CNT_64,
	MC_BUF_CNT_128,
	MC_BUF_CNT_256,
	MC_BUF_CNT_512,
	MC_BUF_CNT_1024,
	MC_BUF_CNT_2048,
};

enum nix_makr_fmt_indexes {
	NIX_MARK_CFG_IP_DSCP_RED,
	NIX_MARK_CFG_IP_DSCP_YELLOW,
	NIX_MARK_CFG_IP_DSCP_YELLOW_RED,
	NIX_MARK_CFG_IP_ECN_RED,
	NIX_MARK_CFG_IP_ECN_YELLOW,
	NIX_MARK_CFG_IP_ECN_YELLOW_RED,
	NIX_MARK_CFG_VLAN_DEI_RED,
	NIX_MARK_CFG_VLAN_DEI_YELLOW,
	NIX_MARK_CFG_VLAN_DEI_YELLOW_RED,
	NIX_MARK_CFG_MAX,
};

/* For now considering MC resources needed for broadcast
 * pkt replication only. i.e 256 HWVFs + 12 PFs.
 */
#define MC_TBL_SIZE	MC_TBL_SZ_512
#define MC_BUF_CNT	MC_BUF_CNT_128

struct mce {
	struct hlist_node	node;
	u16			pcifunc;
};

bool is_nixlf_attached(struct rvu *rvu, u16 pcifunc)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return false;
	return true;
}

int rvu_get_nixlf_count(struct rvu *rvu)
{
	struct rvu_block *block;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return 0;
	block = &rvu->hw->block[blkaddr];
	return block->lf.max;
}

int nix_get_nixlf(struct rvu *rvu, u16 pcifunc, int *nixlf, int *nix_blkaddr)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	struct rvu_hwinfo *hw = rvu->hw;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	*nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (*nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	if (nix_blkaddr)
		*nix_blkaddr = blkaddr;

	return 0;
}

static void nix_mce_list_init(struct nix_mce_list *list, int max)
{
	INIT_HLIST_HEAD(&list->head);
	list->count = 0;
	list->max = max;
}

static u16 nix_alloc_mce_list(struct nix_mcast *mcast, int count)
{
	int idx;

	if (!mcast)
		return 0;

	idx = mcast->next_free_mce;
	mcast->next_free_mce += count;
	return idx;
}

static inline struct nix_hw *get_nix_hw(struct rvu_hwinfo *hw, int blkaddr)
{
	if (blkaddr == BLKADDR_NIX0 && hw->nix0)
		return hw->nix0;

	return NULL;
}

static void nix_rx_sync(struct rvu *rvu, int blkaddr)
{
	int err;

	/*Sync all in flight RX packets to LLC/DRAM */
	rvu_write64(rvu, blkaddr, NIX_AF_RX_SW_SYNC, BIT_ULL(0));
	err = rvu_poll_reg(rvu, blkaddr, NIX_AF_RX_SW_SYNC, BIT_ULL(0), true);
	if (err)
		dev_err(rvu->dev, "NIX RX software sync failed\n");
}

static bool is_valid_txschq(struct rvu *rvu, int blkaddr,
			    int lvl, u16 pcifunc, u16 schq)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct nix_txsch *txsch;
	struct nix_hw *nix_hw;
	u16 map_func;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return false;

	txsch = &nix_hw->txsch[lvl];
	/* Check out of bounds */
	if (schq >= txsch->schq.max)
		return false;

	mutex_lock(&rvu->rsrc_lock);
	map_func = TXSCH_MAP_FUNC(txsch->pfvf_map[schq]);
	mutex_unlock(&rvu->rsrc_lock);

	/* TLs aggegating traffic are shared across PF and VFs */
	if (lvl >= hw->cap.nix_tx_aggr_lvl) {
		if (rvu_get_pf(map_func) != rvu_get_pf(pcifunc))
			return false;
		else
			return true;
	}

	if (map_func != pcifunc)
		return false;

	return true;
}

static int nix_interface_init(struct rvu *rvu, u16 pcifunc, int type, int nixlf)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	u8 cgx_id, lmac_id;
	int pkind, pf, vf;
	int err;

	pf = rvu_get_pf(pcifunc);
	if (!is_pf_cgxmapped(rvu, pf) && type != NIX_INTF_TYPE_LBK)
		return 0;

	switch (type) {
	case NIX_INTF_TYPE_CGX:
		pfvf->cgx_lmac = rvu->pf2cgxlmac_map[pf];
		rvu_get_cgx_lmac_id(pfvf->cgx_lmac, &cgx_id, &lmac_id);

		pkind = rvu_npc_get_pkind(rvu, pf);
		if (pkind < 0) {
			dev_err(rvu->dev,
				"PF_Func 0x%x: Invalid pkind\n", pcifunc);
			return -EINVAL;
		}
		pfvf->rx_chan_base = NIX_CHAN_CGX_LMAC_CHX(cgx_id, lmac_id, 0);
		pfvf->tx_chan_base = pfvf->rx_chan_base;
		pfvf->rx_chan_cnt = 1;
		pfvf->tx_chan_cnt = 1;
		cgx_set_pkind(rvu_cgx_pdata(cgx_id, rvu), lmac_id, pkind);
		rvu_npc_set_pkind(rvu, pkind, pfvf);

		/* By default we enable pause frames */
		if ((pcifunc & RVU_PFVF_FUNC_MASK) == 0)
			cgx_lmac_set_pause_frm(rvu_cgx_pdata(cgx_id, rvu),
					       lmac_id, true, true);
		break;
	case NIX_INTF_TYPE_LBK:
		vf = (pcifunc & RVU_PFVF_FUNC_MASK) - 1;

		/* Note that AF's VFs work in pairs and talk over consecutive
		 * loopback channels.Therefore if odd number of AF VFs are
		 * enabled then the last VF remains with no pair.
		 */
		pfvf->rx_chan_base = NIX_CHAN_LBK_CHX(0, vf);
		pfvf->tx_chan_base = vf & 0x1 ? NIX_CHAN_LBK_CHX(0, vf - 1) :
						NIX_CHAN_LBK_CHX(0, vf + 1);
		pfvf->rx_chan_cnt = 1;
		pfvf->tx_chan_cnt = 1;
		rvu_npc_install_promisc_entry(rvu, pcifunc, nixlf,
					      pfvf->rx_chan_base, false);
		break;
	}

	/* Add a UCAST forwarding rule in MCAM with this NIXLF attached
	 * RVU PF/VF's MAC address.
	 */
	rvu_npc_install_ucast_entry(rvu, pcifunc, nixlf,
				    pfvf->rx_chan_base, pfvf->mac_addr);

	/* Add this PF_FUNC to bcast pkt replication list */
	err = nix_update_bcast_mce_list(rvu, pcifunc, true);
	if (err) {
		dev_err(rvu->dev,
			"Bcast list, failed to enable PF_FUNC 0x%x\n",
			pcifunc);
		return err;
	}

	rvu_npc_install_bcast_match_entry(rvu, pcifunc,
					  nixlf, pfvf->rx_chan_base);
	pfvf->maxlen = NIC_HW_MIN_FRS;
	pfvf->minlen = NIC_HW_MIN_FRS;

	return 0;
}

static void nix_interface_deinit(struct rvu *rvu, u16 pcifunc, u8 nixlf)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	int err;

	pfvf->maxlen = 0;
	pfvf->minlen = 0;
	pfvf->rxvlan = false;

	/* Remove this PF_FUNC from bcast pkt replication list */
	err = nix_update_bcast_mce_list(rvu, pcifunc, false);
	if (err) {
		dev_err(rvu->dev,
			"Bcast list, failed to disable PF_FUNC 0x%x\n",
			pcifunc);
	}

	/* Free and disable any MCAM entries used by this NIX LF */
	rvu_npc_disable_mcam_entries(rvu, pcifunc, nixlf);
}

int rvu_mbox_handler_nix_bp_disable(struct rvu *rvu,
				    struct nix_bp_cfg_req *req,
				    struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_pfvf *pfvf;
	int blkaddr, pf, type;
	u16 chan_base, chan;
	u64 cfg;

	pf = rvu_get_pf(pcifunc);
	type = is_afvf(pcifunc) ? NIX_INTF_TYPE_LBK : NIX_INTF_TYPE_CGX;
	if (!is_pf_cgxmapped(rvu, pf) && type != NIX_INTF_TYPE_LBK)
		return 0;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);

	chan_base = pfvf->rx_chan_base + req->chan_base;
	for (chan = chan_base; chan < (chan_base + req->chan_cnt); chan++) {
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_RX_CHANX_CFG(chan));
		rvu_write64(rvu, blkaddr, NIX_AF_RX_CHANX_CFG(chan),
			    cfg & ~BIT_ULL(16));
	}
	return 0;
}

static int rvu_nix_get_bpid(struct rvu *rvu, struct nix_bp_cfg_req *req,
			    int type, int chan_id)
{
	int bpid, blkaddr, lmac_chan_cnt;
	struct rvu_hwinfo *hw = rvu->hw;
	u16 cgx_bpid_cnt, lbk_bpid_cnt;
	struct rvu_pfvf *pfvf;
	u8 cgx_id, lmac_id;
	u64 cfg;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, req->hdr.pcifunc);
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST);
	lmac_chan_cnt = cfg & 0xFF;

	cgx_bpid_cnt = hw->cgx_links * lmac_chan_cnt;
	lbk_bpid_cnt = hw->lbk_links * ((cfg >> 16) & 0xFF);

	pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);

	/* Backpressure IDs range division
	 * CGX channles are mapped to (0 - 191) BPIDs
	 * LBK channles are mapped to (192 - 255) BPIDs
	 * SDP channles are mapped to (256 - 511) BPIDs
	 *
	 * Lmac channles and bpids mapped as follows
	 * cgx(0)_lmac(0)_chan(0 - 15) = bpid(0 - 15)
	 * cgx(0)_lmac(1)_chan(0 - 15) = bpid(16 - 31) ....
	 * cgx(1)_lmac(0)_chan(0 - 15) = bpid(64 - 79) ....
	 */
	switch (type) {
	case NIX_INTF_TYPE_CGX:
		if ((req->chan_base + req->chan_cnt) > 15)
			return -EINVAL;
		rvu_get_cgx_lmac_id(pfvf->cgx_lmac, &cgx_id, &lmac_id);
		/* Assign bpid based on cgx, lmac and chan id */
		bpid = (cgx_id * hw->lmac_per_cgx * lmac_chan_cnt) +
			(lmac_id * lmac_chan_cnt) + req->chan_base;

		if (req->bpid_per_chan)
			bpid += chan_id;
		if (bpid > cgx_bpid_cnt)
			return -EINVAL;
		break;

	case NIX_INTF_TYPE_LBK:
		if ((req->chan_base + req->chan_cnt) > 63)
			return -EINVAL;
		bpid = cgx_bpid_cnt + req->chan_base;
		if (req->bpid_per_chan)
			bpid += chan_id;
		if (bpid > (cgx_bpid_cnt + lbk_bpid_cnt))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	return bpid;
}

int rvu_mbox_handler_nix_bp_enable(struct rvu *rvu,
				   struct nix_bp_cfg_req *req,
				   struct nix_bp_cfg_rsp *rsp)
{
	int blkaddr, pf, type, chan_id = 0;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_pfvf *pfvf;
	u16 chan_base, chan;
	s16 bpid, bpid_base;
	u64 cfg;

	pf = rvu_get_pf(pcifunc);
	type = is_afvf(pcifunc) ? NIX_INTF_TYPE_LBK : NIX_INTF_TYPE_CGX;

	/* Enable backpressure only for CGX mapped PFs and LBK interface */
	if (!is_pf_cgxmapped(rvu, pf) && type != NIX_INTF_TYPE_LBK)
		return 0;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);

	bpid_base = rvu_nix_get_bpid(rvu, req, type, chan_id);
	chan_base = pfvf->rx_chan_base + req->chan_base;
	bpid = bpid_base;

	for (chan = chan_base; chan < (chan_base + req->chan_cnt); chan++) {
		if (bpid < 0) {
			dev_warn(rvu->dev, "Fail to enable backpressure\n");
			return -EINVAL;
		}

		cfg = rvu_read64(rvu, blkaddr, NIX_AF_RX_CHANX_CFG(chan));
		rvu_write64(rvu, blkaddr, NIX_AF_RX_CHANX_CFG(chan),
			    cfg | (bpid & 0xFF) | BIT_ULL(16));
		chan_id++;
		bpid = rvu_nix_get_bpid(rvu, req, type, chan_id);
	}

	for (chan = 0; chan < req->chan_cnt; chan++) {
		/* Map channel and bpid assign to it */
		rsp->chan_bpid[chan] = ((req->chan_base + chan) & 0x7F) << 10 |
					(bpid_base & 0x3FF);
		if (req->bpid_per_chan)
			bpid_base++;
	}
	rsp->chan_cnt = req->chan_cnt;

	return 0;
}

static void nix_setup_lso_tso_l3(struct rvu *rvu, int blkaddr,
				 u64 format, bool v4, u64 *fidx)
{
	struct nix_lso_format field = {0};

	/* IP's Length field */
	field.layer = NIX_TXLAYER_OL3;
	/* In ipv4, length field is at offset 2 bytes, for ipv6 it's 4 */
	field.offset = v4 ? 2 : 4;
	field.sizem1 = 1; /* i.e 2 bytes */
	field.alg = NIX_LSOALG_ADD_PAYLEN;
	rvu_write64(rvu, blkaddr,
		    NIX_AF_LSO_FORMATX_FIELDX(format, (*fidx)++),
		    *(u64 *)&field);

	/* No ID field in IPv6 header */
	if (!v4)
		return;

	/* IP's ID field */
	field.layer = NIX_TXLAYER_OL3;
	field.offset = 4;
	field.sizem1 = 1; /* i.e 2 bytes */
	field.alg = NIX_LSOALG_ADD_SEGNUM;
	rvu_write64(rvu, blkaddr,
		    NIX_AF_LSO_FORMATX_FIELDX(format, (*fidx)++),
		    *(u64 *)&field);
}

static void nix_setup_lso_tso_l4(struct rvu *rvu, int blkaddr,
				 u64 format, u64 *fidx)
{
	struct nix_lso_format field = {0};

	/* TCP's sequence number field */
	field.layer = NIX_TXLAYER_OL4;
	field.offset = 4;
	field.sizem1 = 3; /* i.e 4 bytes */
	field.alg = NIX_LSOALG_ADD_OFFSET;
	rvu_write64(rvu, blkaddr,
		    NIX_AF_LSO_FORMATX_FIELDX(format, (*fidx)++),
		    *(u64 *)&field);

	/* TCP's flags field */
	field.layer = NIX_TXLAYER_OL4;
	field.offset = 12;
	field.sizem1 = 1; /* 2 bytes */
	field.alg = NIX_LSOALG_TCP_FLAGS;
	rvu_write64(rvu, blkaddr,
		    NIX_AF_LSO_FORMATX_FIELDX(format, (*fidx)++),
		    *(u64 *)&field);
}

static void nix_setup_lso(struct rvu *rvu, struct nix_hw *nix_hw, int blkaddr)
{
	u64 cfg, idx, fidx = 0;

	/* Get max HW supported format indices */
	cfg = (rvu_read64(rvu, blkaddr, NIX_AF_CONST1) >> 48) & 0xFF;
	nix_hw->lso.total = cfg;

	/* Enable LSO */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_LSO_CFG);
	/* For TSO, set first and middle segment flags to
	 * mask out PSH, RST & FIN flags in TCP packet
	 */
	cfg &= ~((0xFFFFULL << 32) | (0xFFFFULL << 16));
	cfg |= (0xFFF2ULL << 32) | (0xFFF2ULL << 16);
	rvu_write64(rvu, blkaddr, NIX_AF_LSO_CFG, cfg | BIT_ULL(63));

	/* Setup default static LSO formats
	 *
	 * Configure format fields for TCPv4 segmentation offload
	 */
	idx = NIX_LSO_FORMAT_IDX_TSOV4;
	nix_setup_lso_tso_l3(rvu, blkaddr, idx, true, &fidx);
	nix_setup_lso_tso_l4(rvu, blkaddr, idx, &fidx);

	/* Set rest of the fields to NOP */
	for (; fidx < 8; fidx++) {
		rvu_write64(rvu, blkaddr,
			    NIX_AF_LSO_FORMATX_FIELDX(idx, fidx), 0x0ULL);
	}
	nix_hw->lso.in_use++;

	/* Configure format fields for TCPv6 segmentation offload */
	idx = NIX_LSO_FORMAT_IDX_TSOV6;
	fidx = 0;
	nix_setup_lso_tso_l3(rvu, blkaddr, idx, false, &fidx);
	nix_setup_lso_tso_l4(rvu, blkaddr, idx, &fidx);

	/* Set rest of the fields to NOP */
	for (; fidx < 8; fidx++) {
		rvu_write64(rvu, blkaddr,
			    NIX_AF_LSO_FORMATX_FIELDX(idx, fidx), 0x0ULL);
	}
	nix_hw->lso.in_use++;
}

static void nix_ctx_free(struct rvu *rvu, struct rvu_pfvf *pfvf)
{
	kfree(pfvf->rq_bmap);
	kfree(pfvf->sq_bmap);
	kfree(pfvf->cq_bmap);
	if (pfvf->rq_ctx)
		qmem_free(rvu->dev, pfvf->rq_ctx);
	if (pfvf->sq_ctx)
		qmem_free(rvu->dev, pfvf->sq_ctx);
	if (pfvf->cq_ctx)
		qmem_free(rvu->dev, pfvf->cq_ctx);
	if (pfvf->rss_ctx)
		qmem_free(rvu->dev, pfvf->rss_ctx);
	if (pfvf->nix_qints_ctx)
		qmem_free(rvu->dev, pfvf->nix_qints_ctx);
	if (pfvf->cq_ints_ctx)
		qmem_free(rvu->dev, pfvf->cq_ints_ctx);

	pfvf->rq_bmap = NULL;
	pfvf->cq_bmap = NULL;
	pfvf->sq_bmap = NULL;
	pfvf->rq_ctx = NULL;
	pfvf->sq_ctx = NULL;
	pfvf->cq_ctx = NULL;
	pfvf->rss_ctx = NULL;
	pfvf->nix_qints_ctx = NULL;
	pfvf->cq_ints_ctx = NULL;
}

static int nixlf_rss_ctx_init(struct rvu *rvu, int blkaddr,
			      struct rvu_pfvf *pfvf, int nixlf,
			      int rss_sz, int rss_grps, int hwctx_size,
			      u64 way_mask)
{
	int err, grp, num_indices;

	/* RSS is not requested for this NIXLF */
	if (!rss_sz)
		return 0;
	num_indices = rss_sz * rss_grps;

	/* Alloc NIX RSS HW context memory and config the base */
	err = qmem_alloc(rvu->dev, &pfvf->rss_ctx, num_indices, hwctx_size);
	if (err)
		return err;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RSS_BASE(nixlf),
		    (u64)pfvf->rss_ctx->iova);

	/* Config full RSS table size, enable RSS and caching */
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RSS_CFG(nixlf),
		    BIT_ULL(36) | BIT_ULL(4) |
		    ilog2(num_indices / MAX_RSS_INDIR_TBL_SIZE) |
		    way_mask << 20);
	/* Config RSS group offset and sizes */
	for (grp = 0; grp < rss_grps; grp++)
		rvu_write64(rvu, blkaddr, NIX_AF_LFX_RSS_GRPX(nixlf, grp),
			    ((ilog2(rss_sz) - 1) << 16) | (rss_sz * grp));
	return 0;
}

static int nix_aq_enqueue_wait(struct rvu *rvu, struct rvu_block *block,
			       struct nix_aq_inst_s *inst)
{
	struct admin_queue *aq = block->aq;
	struct nix_aq_res_s *result;
	int timeout = 1000;
	u64 reg, head;

	result = (struct nix_aq_res_s *)aq->res->base;

	/* Get current head pointer where to append this instruction */
	reg = rvu_read64(rvu, block->addr, NIX_AF_AQ_STATUS);
	head = (reg >> 4) & AQ_PTR_MASK;

	memcpy((void *)(aq->inst->base + (head * aq->inst->entry_sz)),
	       (void *)inst, aq->inst->entry_sz);
	memset(result, 0, sizeof(*result));
	/* sync into memory */
	wmb();

	/* Ring the doorbell and wait for result */
	rvu_write64(rvu, block->addr, NIX_AF_AQ_DOOR, 1);
	while (result->compcode == NIX_AQ_COMP_NOTDONE) {
		cpu_relax();
		udelay(1);
		timeout--;
		if (!timeout)
			return -EBUSY;
	}

	if (result->compcode != NIX_AQ_COMP_GOOD)
		/* TODO: Replace this with some error code */
		return -EBUSY;

	return 0;
}

static int rvu_nix_aq_enq_inst(struct rvu *rvu, struct nix_aq_enq_req *req,
			       struct nix_aq_enq_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	int nixlf, blkaddr, rc = 0;
	struct nix_aq_inst_s inst;
	struct rvu_block *block;
	struct admin_queue *aq;
	struct rvu_pfvf *pfvf;
	void *ctx, *mask;
	bool ena;
	u64 cfg;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	aq = block->aq;
	if (!aq) {
		dev_warn(rvu->dev, "%s: NIX AQ not initialized\n", __func__);
		return NIX_AF_ERR_AQ_ENQUEUE;
	}

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	nixlf = rvu_get_lf(rvu, block, pcifunc, 0);

	/* Skip NIXLF check for broadcast MCE entry init */
	if (!(!rsp && req->ctype == NIX_AQ_CTYPE_MCE)) {
		if (!pfvf->nixlf || nixlf < 0)
			return NIX_AF_ERR_AF_LF_INVALID;
	}

	switch (req->ctype) {
	case NIX_AQ_CTYPE_RQ:
		/* Check if index exceeds max no of queues */
		if (!pfvf->rq_ctx || req->qidx >= pfvf->rq_ctx->qsize)
			rc = NIX_AF_ERR_AQ_ENQUEUE;
		break;
	case NIX_AQ_CTYPE_SQ:
		if (!pfvf->sq_ctx || req->qidx >= pfvf->sq_ctx->qsize)
			rc = NIX_AF_ERR_AQ_ENQUEUE;
		break;
	case NIX_AQ_CTYPE_CQ:
		if (!pfvf->cq_ctx || req->qidx >= pfvf->cq_ctx->qsize)
			rc = NIX_AF_ERR_AQ_ENQUEUE;
		break;
	case NIX_AQ_CTYPE_RSS:
		/* Check if RSS is enabled and qidx is within range */
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_LFX_RSS_CFG(nixlf));
		if (!(cfg & BIT_ULL(4)) || !pfvf->rss_ctx ||
		    (req->qidx >= (256UL << (cfg & 0xF))))
			rc = NIX_AF_ERR_AQ_ENQUEUE;
		break;
	case NIX_AQ_CTYPE_MCE:
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_RX_MCAST_CFG);
		/* Check if index exceeds MCE list length */
		if (!hw->nix0->mcast.mce_ctx ||
		    (req->qidx >= (256UL << (cfg & 0xF))))
			rc = NIX_AF_ERR_AQ_ENQUEUE;

		/* Adding multicast lists for requests from PF/VFs is not
		 * yet supported, so ignore this.
		 */
		if (rsp)
			rc = NIX_AF_ERR_AQ_ENQUEUE;
		break;
	default:
		rc = NIX_AF_ERR_AQ_ENQUEUE;
	}

	if (rc)
		return rc;

	/* Check if SQ pointed SMQ belongs to this PF/VF or not */
	if (req->ctype == NIX_AQ_CTYPE_SQ &&
	    ((req->op == NIX_AQ_INSTOP_INIT && req->sq.ena) ||
	     (req->op == NIX_AQ_INSTOP_WRITE &&
	      req->sq_mask.ena && req->sq_mask.smq && req->sq.ena))) {
		if (!is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_SMQ,
				     pcifunc, req->sq.smq))
			return NIX_AF_ERR_AQ_ENQUEUE;
	}

	memset(&inst, 0, sizeof(struct nix_aq_inst_s));
	inst.lf = nixlf;
	inst.cindex = req->qidx;
	inst.ctype = req->ctype;
	inst.op = req->op;
	/* Currently we are not supporting enqueuing multiple instructions,
	 * so always choose first entry in result memory.
	 */
	inst.res_addr = (u64)aq->res->iova;

	/* Hardware uses same aq->res->base for updating result of
	 * previous instruction hence wait here till it is done.
	 */
	spin_lock(&aq->lock);

	/* Clean result + context memory */
	memset(aq->res->base, 0, aq->res->entry_sz);
	/* Context needs to be written at RES_ADDR + 128 */
	ctx = aq->res->base + 128;
	/* Mask needs to be written at RES_ADDR + 256 */
	mask = aq->res->base + 256;

	switch (req->op) {
	case NIX_AQ_INSTOP_WRITE:
		if (req->ctype == NIX_AQ_CTYPE_RQ)
			memcpy(mask, &req->rq_mask,
			       sizeof(struct nix_rq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_SQ)
			memcpy(mask, &req->sq_mask,
			       sizeof(struct nix_sq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_CQ)
			memcpy(mask, &req->cq_mask,
			       sizeof(struct nix_cq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_RSS)
			memcpy(mask, &req->rss_mask,
			       sizeof(struct nix_rsse_s));
		else if (req->ctype == NIX_AQ_CTYPE_MCE)
			memcpy(mask, &req->mce_mask,
			       sizeof(struct nix_rx_mce_s));
		fallthrough;
	case NIX_AQ_INSTOP_INIT:
		if (req->ctype == NIX_AQ_CTYPE_RQ)
			memcpy(ctx, &req->rq, sizeof(struct nix_rq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_SQ)
			memcpy(ctx, &req->sq, sizeof(struct nix_sq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_CQ)
			memcpy(ctx, &req->cq, sizeof(struct nix_cq_ctx_s));
		else if (req->ctype == NIX_AQ_CTYPE_RSS)
			memcpy(ctx, &req->rss, sizeof(struct nix_rsse_s));
		else if (req->ctype == NIX_AQ_CTYPE_MCE)
			memcpy(ctx, &req->mce, sizeof(struct nix_rx_mce_s));
		break;
	case NIX_AQ_INSTOP_NOP:
	case NIX_AQ_INSTOP_READ:
	case NIX_AQ_INSTOP_LOCK:
	case NIX_AQ_INSTOP_UNLOCK:
		break;
	default:
		rc = NIX_AF_ERR_AQ_ENQUEUE;
		spin_unlock(&aq->lock);
		return rc;
	}

	/* Submit the instruction to AQ */
	rc = nix_aq_enqueue_wait(rvu, block, &inst);
	if (rc) {
		spin_unlock(&aq->lock);
		return rc;
	}

	/* Set RQ/SQ/CQ bitmap if respective queue hw context is enabled */
	if (req->op == NIX_AQ_INSTOP_INIT) {
		if (req->ctype == NIX_AQ_CTYPE_RQ && req->rq.ena)
			__set_bit(req->qidx, pfvf->rq_bmap);
		if (req->ctype == NIX_AQ_CTYPE_SQ && req->sq.ena)
			__set_bit(req->qidx, pfvf->sq_bmap);
		if (req->ctype == NIX_AQ_CTYPE_CQ && req->cq.ena)
			__set_bit(req->qidx, pfvf->cq_bmap);
	}

	if (req->op == NIX_AQ_INSTOP_WRITE) {
		if (req->ctype == NIX_AQ_CTYPE_RQ) {
			ena = (req->rq.ena & req->rq_mask.ena) |
				(test_bit(req->qidx, pfvf->rq_bmap) &
				~req->rq_mask.ena);
			if (ena)
				__set_bit(req->qidx, pfvf->rq_bmap);
			else
				__clear_bit(req->qidx, pfvf->rq_bmap);
		}
		if (req->ctype == NIX_AQ_CTYPE_SQ) {
			ena = (req->rq.ena & req->sq_mask.ena) |
				(test_bit(req->qidx, pfvf->sq_bmap) &
				~req->sq_mask.ena);
			if (ena)
				__set_bit(req->qidx, pfvf->sq_bmap);
			else
				__clear_bit(req->qidx, pfvf->sq_bmap);
		}
		if (req->ctype == NIX_AQ_CTYPE_CQ) {
			ena = (req->rq.ena & req->cq_mask.ena) |
				(test_bit(req->qidx, pfvf->cq_bmap) &
				~req->cq_mask.ena);
			if (ena)
				__set_bit(req->qidx, pfvf->cq_bmap);
			else
				__clear_bit(req->qidx, pfvf->cq_bmap);
		}
	}

	if (rsp) {
		/* Copy read context into mailbox */
		if (req->op == NIX_AQ_INSTOP_READ) {
			if (req->ctype == NIX_AQ_CTYPE_RQ)
				memcpy(&rsp->rq, ctx,
				       sizeof(struct nix_rq_ctx_s));
			else if (req->ctype == NIX_AQ_CTYPE_SQ)
				memcpy(&rsp->sq, ctx,
				       sizeof(struct nix_sq_ctx_s));
			else if (req->ctype == NIX_AQ_CTYPE_CQ)
				memcpy(&rsp->cq, ctx,
				       sizeof(struct nix_cq_ctx_s));
			else if (req->ctype == NIX_AQ_CTYPE_RSS)
				memcpy(&rsp->rss, ctx,
				       sizeof(struct nix_rsse_s));
			else if (req->ctype == NIX_AQ_CTYPE_MCE)
				memcpy(&rsp->mce, ctx,
				       sizeof(struct nix_rx_mce_s));
		}
	}

	spin_unlock(&aq->lock);
	return 0;
}

static const char *nix_get_ctx_name(int ctype)
{
	switch (ctype) {
	case NIX_AQ_CTYPE_CQ:
		return "CQ";
	case NIX_AQ_CTYPE_SQ:
		return "SQ";
	case NIX_AQ_CTYPE_RQ:
		return "RQ";
	case NIX_AQ_CTYPE_RSS:
		return "RSS";
	}
	return "";
}

static int nix_lf_hwctx_disable(struct rvu *rvu, struct hwctx_disable_req *req)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);
	struct nix_aq_enq_req aq_req;
	unsigned long *bmap;
	int qidx, q_cnt = 0;
	int err = 0, rc;

	if (!pfvf->cq_ctx || !pfvf->sq_ctx || !pfvf->rq_ctx)
		return NIX_AF_ERR_AQ_ENQUEUE;

	memset(&aq_req, 0, sizeof(struct nix_aq_enq_req));
	aq_req.hdr.pcifunc = req->hdr.pcifunc;

	if (req->ctype == NIX_AQ_CTYPE_CQ) {
		aq_req.cq.ena = 0;
		aq_req.cq_mask.ena = 1;
		aq_req.cq.bp_ena = 0;
		aq_req.cq_mask.bp_ena = 1;
		q_cnt = pfvf->cq_ctx->qsize;
		bmap = pfvf->cq_bmap;
	}
	if (req->ctype == NIX_AQ_CTYPE_SQ) {
		aq_req.sq.ena = 0;
		aq_req.sq_mask.ena = 1;
		q_cnt = pfvf->sq_ctx->qsize;
		bmap = pfvf->sq_bmap;
	}
	if (req->ctype == NIX_AQ_CTYPE_RQ) {
		aq_req.rq.ena = 0;
		aq_req.rq_mask.ena = 1;
		q_cnt = pfvf->rq_ctx->qsize;
		bmap = pfvf->rq_bmap;
	}

	aq_req.ctype = req->ctype;
	aq_req.op = NIX_AQ_INSTOP_WRITE;

	for (qidx = 0; qidx < q_cnt; qidx++) {
		if (!test_bit(qidx, bmap))
			continue;
		aq_req.qidx = qidx;
		rc = rvu_nix_aq_enq_inst(rvu, &aq_req, NULL);
		if (rc) {
			err = rc;
			dev_err(rvu->dev, "Failed to disable %s:%d context\n",
				nix_get_ctx_name(req->ctype), qidx);
		}
	}

	return err;
}

#ifdef CONFIG_NDC_DIS_DYNAMIC_CACHING
static int nix_lf_hwctx_lockdown(struct rvu *rvu, struct nix_aq_enq_req *req)
{
	struct nix_aq_enq_req lock_ctx_req;
	int err;

	if (req->op != NIX_AQ_INSTOP_INIT)
		return 0;

	if (req->ctype == NIX_AQ_CTYPE_MCE ||
	    req->ctype == NIX_AQ_CTYPE_DYNO)
		return 0;

	memset(&lock_ctx_req, 0, sizeof(struct nix_aq_enq_req));
	lock_ctx_req.hdr.pcifunc = req->hdr.pcifunc;
	lock_ctx_req.ctype = req->ctype;
	lock_ctx_req.op = NIX_AQ_INSTOP_LOCK;
	lock_ctx_req.qidx = req->qidx;
	err = rvu_nix_aq_enq_inst(rvu, &lock_ctx_req, NULL);
	if (err)
		dev_err(rvu->dev,
			"PFUNC 0x%x: Failed to lock NIX %s:%d context\n",
			req->hdr.pcifunc,
			nix_get_ctx_name(req->ctype), req->qidx);
	return err;
}

int rvu_mbox_handler_nix_aq_enq(struct rvu *rvu,
				struct nix_aq_enq_req *req,
				struct nix_aq_enq_rsp *rsp)
{
	int err;

	err = rvu_nix_aq_enq_inst(rvu, req, rsp);
	if (!err)
		err = nix_lf_hwctx_lockdown(rvu, req);
	return err;
}
#else

int rvu_mbox_handler_nix_aq_enq(struct rvu *rvu,
				struct nix_aq_enq_req *req,
				struct nix_aq_enq_rsp *rsp)
{
	return rvu_nix_aq_enq_inst(rvu, req, rsp);
}
#endif

int rvu_mbox_handler_nix_hwctx_disable(struct rvu *rvu,
				       struct hwctx_disable_req *req,
				       struct msg_rsp *rsp)
{
	return nix_lf_hwctx_disable(rvu, req);
}

int rvu_mbox_handler_nix_lf_alloc(struct rvu *rvu,
				  struct nix_lf_alloc_req *req,
				  struct nix_lf_alloc_rsp *rsp)
{
	int nixlf, qints, hwctx_size, intf, err, rc = 0;
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_block *block;
	struct rvu_pfvf *pfvf;
	u64 cfg, ctx_cfg;
	int blkaddr;

	if (!req->rq_cnt || !req->sq_cnt || !req->cq_cnt)
		return NIX_AF_ERR_PARAM;

	if (req->way_mask)
		req->way_mask &= 0xFFFF;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	nixlf = rvu_get_lf(rvu, block, pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	/* Check if requested 'NIXLF <=> NPALF' mapping is valid */
	if (req->npa_func) {
		/* If default, use 'this' NIXLF's PFFUNC */
		if (req->npa_func == RVU_DEFAULT_PF_FUNC)
			req->npa_func = pcifunc;
		if (!is_pffunc_map_valid(rvu, req->npa_func, BLKTYPE_NPA))
			return NIX_AF_INVAL_NPA_PF_FUNC;
	}

	/* Check if requested 'NIXLF <=> SSOLF' mapping is valid */
	if (req->sso_func) {
		/* If default, use 'this' NIXLF's PFFUNC */
		if (req->sso_func == RVU_DEFAULT_PF_FUNC)
			req->sso_func = pcifunc;
		if (!is_pffunc_map_valid(rvu, req->sso_func, BLKTYPE_SSO))
			return NIX_AF_INVAL_SSO_PF_FUNC;
	}

	/* If RSS is being enabled, check if requested config is valid.
	 * RSS table size should be power of two, otherwise
	 * RSS_GRP::OFFSET + adder might go beyond that group or
	 * won't be able to use entire table.
	 */
	if (req->rss_sz && (req->rss_sz > MAX_RSS_INDIR_TBL_SIZE ||
			    !is_power_of_2(req->rss_sz)))
		return NIX_AF_ERR_RSS_SIZE_INVALID;

	if (req->rss_sz &&
	    (!req->rss_grps || req->rss_grps > MAX_RSS_GROUPS))
		return NIX_AF_ERR_RSS_GRPS_INVALID;

	/* Reset this NIX LF */
	err = rvu_lf_reset(rvu, block, nixlf);
	if (err) {
		dev_err(rvu->dev, "Failed to reset NIX%d LF%d\n",
			block->addr - BLKADDR_NIX0, nixlf);
		return NIX_AF_ERR_LF_RESET;
	}

	ctx_cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST3);

	/* Alloc NIX RQ HW context memory and config the base */
	hwctx_size = 1UL << ((ctx_cfg >> 4) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->rq_ctx, req->rq_cnt, hwctx_size);
	if (err)
		goto free_mem;

	pfvf->rq_bmap = kcalloc(req->rq_cnt, sizeof(long), GFP_KERNEL);
	if (!pfvf->rq_bmap)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RQS_BASE(nixlf),
		    (u64)pfvf->rq_ctx->iova);

	/* Set caching and queue count in HW */
	cfg = BIT_ULL(36) | (req->rq_cnt - 1) | req->way_mask << 20;
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RQS_CFG(nixlf), cfg);

	/* Alloc NIX SQ HW context memory and config the base */
	hwctx_size = 1UL << (ctx_cfg & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->sq_ctx, req->sq_cnt, hwctx_size);
	if (err)
		goto free_mem;

	pfvf->sq_bmap = kcalloc(req->sq_cnt, sizeof(long), GFP_KERNEL);
	if (!pfvf->sq_bmap)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_SQS_BASE(nixlf),
		    (u64)pfvf->sq_ctx->iova);

	cfg = BIT_ULL(36) | (req->sq_cnt - 1) | req->way_mask << 20;
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_SQS_CFG(nixlf), cfg);

	/* Alloc NIX CQ HW context memory and config the base */
	hwctx_size = 1UL << ((ctx_cfg >> 8) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->cq_ctx, req->cq_cnt, hwctx_size);
	if (err)
		goto free_mem;

	pfvf->cq_bmap = kcalloc(req->cq_cnt, sizeof(long), GFP_KERNEL);
	if (!pfvf->cq_bmap)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CQS_BASE(nixlf),
		    (u64)pfvf->cq_ctx->iova);

	cfg = BIT_ULL(36) | (req->cq_cnt - 1) | req->way_mask << 20;
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CQS_CFG(nixlf), cfg);

	/* Initialize receive side scaling (RSS) */
	hwctx_size = 1UL << ((ctx_cfg >> 12) & 0xF);
	err = nixlf_rss_ctx_init(rvu, blkaddr, pfvf, nixlf, req->rss_sz,
				 req->rss_grps, hwctx_size, req->way_mask);
	if (err)
		goto free_mem;

	/* Alloc memory for CQINT's HW contexts */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST2);
	qints = (cfg >> 24) & 0xFFF;
	hwctx_size = 1UL << ((ctx_cfg >> 24) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->cq_ints_ctx, qints, hwctx_size);
	if (err)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CINTS_BASE(nixlf),
		    (u64)pfvf->cq_ints_ctx->iova);

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CINTS_CFG(nixlf),
		    BIT_ULL(36) | req->way_mask << 20);

	/* Alloc memory for QINT's HW contexts */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST2);
	qints = (cfg >> 12) & 0xFFF;
	hwctx_size = 1UL << ((ctx_cfg >> 20) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->nix_qints_ctx, qints, hwctx_size);
	if (err)
		goto free_mem;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_QINTS_BASE(nixlf),
		    (u64)pfvf->nix_qints_ctx->iova);
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_QINTS_CFG(nixlf),
		    BIT_ULL(36) | req->way_mask << 20);

	/* Setup VLANX TPID's.
	 * Use VLAN1 for 802.1Q
	 * and VLAN0 for 802.1AD.
	 */
	cfg = (0x8100ULL << 16) | 0x88A8ULL;
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_TX_CFG(nixlf), cfg);

	/* Enable LMTST for this NIX LF */
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_TX_CFG2(nixlf), BIT_ULL(0));

	/* Set CQE/WQE size, NPA_PF_FUNC for SQBs and also SSO_PF_FUNC */
	if (req->npa_func)
		cfg = req->npa_func;
	if (req->sso_func)
		cfg |= (u64)req->sso_func << 16;

	cfg |= (u64)req->xqe_sz << 33;
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CFG(nixlf), cfg);

	/* Config Rx pkt length, csum checks and apad  enable / disable */
	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RX_CFG(nixlf), req->rx_cfg);

	intf = is_afvf(pcifunc) ? NIX_INTF_TYPE_LBK : NIX_INTF_TYPE_CGX;
	err = nix_interface_init(rvu, pcifunc, intf, nixlf);
	if (err)
		goto free_mem;

	/* Disable NPC entries as NIXLF's contexts are not initialized yet */
	rvu_npc_disable_default_entries(rvu, pcifunc, nixlf);

	goto exit;

free_mem:
	nix_ctx_free(rvu, pfvf);
	rc = -ENOMEM;

exit:
	/* Set macaddr of this PF/VF */
	ether_addr_copy(rsp->mac_addr, pfvf->mac_addr);

	/* set SQB size info */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_SQ_CONST);
	rsp->sqb_size = (cfg >> 34) & 0xFFFF;
	rsp->rx_chan_base = pfvf->rx_chan_base;
	rsp->tx_chan_base = pfvf->tx_chan_base;
	rsp->rx_chan_cnt = pfvf->rx_chan_cnt;
	rsp->tx_chan_cnt = pfvf->tx_chan_cnt;
	rsp->lso_tsov4_idx = NIX_LSO_FORMAT_IDX_TSOV4;
	rsp->lso_tsov6_idx = NIX_LSO_FORMAT_IDX_TSOV6;
	/* Get HW supported stat count */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST1);
	rsp->lf_rx_stats = ((cfg >> 32) & 0xFF);
	rsp->lf_tx_stats = ((cfg >> 24) & 0xFF);
	/* Get count of CQ IRQs and error IRQs supported per LF */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST2);
	rsp->qints = ((cfg >> 12) & 0xFFF);
	rsp->cints = ((cfg >> 24) & 0xFFF);
	return rc;
}

int rvu_mbox_handler_nix_lf_free(struct rvu *rvu, struct msg_req *req,
				 struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_block *block;
	int blkaddr, nixlf, err;
	struct rvu_pfvf *pfvf;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	nixlf = rvu_get_lf(rvu, block, pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_interface_deinit(rvu, pcifunc, nixlf);

	/* Reset this NIX LF */
	err = rvu_lf_reset(rvu, block, nixlf);
	if (err) {
		dev_err(rvu->dev, "Failed to reset NIX%d LF%d\n",
			block->addr - BLKADDR_NIX0, nixlf);
		return NIX_AF_ERR_LF_RESET;
	}

	nix_ctx_free(rvu, pfvf);

	return 0;
}

int rvu_mbox_handler_nix_mark_format_cfg(struct rvu *rvu,
					 struct nix_mark_format_cfg  *req,
					 struct nix_mark_format_cfg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct nix_hw *nix_hw;
	struct rvu_pfvf *pfvf;
	int blkaddr, rc;
	u32 cfg;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	cfg = (((u32)req->offset & 0x7) << 16) |
	      (((u32)req->y_mask & 0xF) << 12) |
	      (((u32)req->y_val & 0xF) << 8) |
	      (((u32)req->r_mask & 0xF) << 4) | ((u32)req->r_val & 0xF);

	rc = rvu_nix_reserve_mark_format(rvu, nix_hw, blkaddr, cfg);
	if (rc < 0) {
		dev_err(rvu->dev, "No mark_format_ctl for (pf:%d, vf:%d)",
			rvu_get_pf(pcifunc), pcifunc & RVU_PFVF_FUNC_MASK);
		return NIX_AF_ERR_MARK_CFG_FAIL;
	}

	rsp->mark_format_idx = rc;
	return 0;
}

/* Disable shaping of pkts by a scheduler queue
 * at a given scheduler level.
 */
static void nix_reset_tx_shaping(struct rvu *rvu, int blkaddr,
				 int lvl, int schq)
{
	u64  cir_reg = 0, pir_reg = 0;
	u64  cfg;

	switch (lvl) {
	case NIX_TXSCH_LVL_TL1:
		cir_reg = NIX_AF_TL1X_CIR(schq);
		pir_reg = 0; /* PIR not available at TL1 */
		break;
	case NIX_TXSCH_LVL_TL2:
		cir_reg = NIX_AF_TL2X_CIR(schq);
		pir_reg = NIX_AF_TL2X_PIR(schq);
		break;
	case NIX_TXSCH_LVL_TL3:
		cir_reg = NIX_AF_TL3X_CIR(schq);
		pir_reg = NIX_AF_TL3X_PIR(schq);
		break;
	case NIX_TXSCH_LVL_TL4:
		cir_reg = NIX_AF_TL4X_CIR(schq);
		pir_reg = NIX_AF_TL4X_PIR(schq);
		break;
	}

	if (!cir_reg)
		return;
	cfg = rvu_read64(rvu, blkaddr, cir_reg);
	rvu_write64(rvu, blkaddr, cir_reg, cfg & ~BIT_ULL(0));

	if (!pir_reg)
		return;
	cfg = rvu_read64(rvu, blkaddr, pir_reg);
	rvu_write64(rvu, blkaddr, pir_reg, cfg & ~BIT_ULL(0));
}

static void nix_reset_tx_linkcfg(struct rvu *rvu, int blkaddr,
				 int lvl, int schq)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int link;

	if (lvl >= hw->cap.nix_tx_aggr_lvl)
		return;

	/* Reset TL4's SDP link config */
	if (lvl == NIX_TXSCH_LVL_TL4)
		rvu_write64(rvu, blkaddr, NIX_AF_TL4X_SDP_LINK_CFG(schq), 0x00);

	if (lvl != NIX_TXSCH_LVL_TL2)
		return;

	/* Reset TL2's CGX or LBK link config */
	for (link = 0; link < (hw->cgx_links + hw->lbk_links); link++)
		rvu_write64(rvu, blkaddr,
			    NIX_AF_TL3_TL2X_LINKX_CFG(schq, link), 0x00);
}

static int nix_get_tx_link(struct rvu *rvu, u16 pcifunc)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int pf = rvu_get_pf(pcifunc);
	u8 cgx_id = 0, lmac_id = 0;

	if (is_afvf(pcifunc)) {/* LBK links */
		return hw->cgx_links;
	} else if (is_pf_cgxmapped(rvu, pf)) {
		rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
		return (cgx_id * hw->lmac_per_cgx) + lmac_id;
	}

	/* SDP link */
	return hw->cgx_links + hw->lbk_links;
}

static void nix_get_txschq_range(struct rvu *rvu, u16 pcifunc,
				 int link, int *start, int *end)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int pf = rvu_get_pf(pcifunc);

	if (is_afvf(pcifunc)) { /* LBK links */
		*start = hw->cap.nix_txsch_per_cgx_lmac * link;
		*end = *start + hw->cap.nix_txsch_per_lbk_lmac;
	} else if (is_pf_cgxmapped(rvu, pf)) { /* CGX links */
		*start = hw->cap.nix_txsch_per_cgx_lmac * link;
		*end = *start + hw->cap.nix_txsch_per_cgx_lmac;
	} else { /* SDP link */
		*start = (hw->cap.nix_txsch_per_cgx_lmac * hw->cgx_links) +
			(hw->cap.nix_txsch_per_lbk_lmac * hw->lbk_links);
		*end = *start + hw->cap.nix_txsch_per_sdp_lmac;
	}
}

static int nix_check_txschq_alloc_req(struct rvu *rvu, int lvl, u16 pcifunc,
				      struct nix_hw *nix_hw,
				      struct nix_txsch_alloc_req *req)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int schq, req_schq, free_cnt;
	struct nix_txsch *txsch;
	int link, start, end;

	txsch = &nix_hw->txsch[lvl];
	req_schq = req->schq_contig[lvl] + req->schq[lvl];

	if (!req_schq)
		return 0;

	link = nix_get_tx_link(rvu, pcifunc);

	/* For traffic aggregating scheduler level, one queue is enough */
	if (lvl >= hw->cap.nix_tx_aggr_lvl) {
		if (req_schq != 1)
			return NIX_AF_ERR_TLX_ALLOC_FAIL;
		return 0;
	}

	/* Get free SCHQ count and check if request can be accomodated */
	if (hw->cap.nix_fixed_txschq_mapping) {
		nix_get_txschq_range(rvu, pcifunc, link, &start, &end);
		schq = start + (pcifunc & RVU_PFVF_FUNC_MASK);
		if (end <= txsch->schq.max && schq < end &&
		    !test_bit(schq, txsch->schq.bmap))
			free_cnt = 1;
		else
			free_cnt = 0;
	} else {
		free_cnt = rvu_rsrc_free_count(&txsch->schq);
	}

	if (free_cnt < req_schq || req_schq > MAX_TXSCHQ_PER_FUNC)
		return NIX_AF_ERR_TLX_ALLOC_FAIL;

	/* If contiguous queues are needed, check for availability */
	if (!hw->cap.nix_fixed_txschq_mapping && req->schq_contig[lvl] &&
	    !rvu_rsrc_check_contig(&txsch->schq, req->schq_contig[lvl]))
		return NIX_AF_ERR_TLX_ALLOC_FAIL;

	return 0;
}

static void nix_txsch_alloc(struct rvu *rvu, struct nix_txsch *txsch,
			    struct nix_txsch_alloc_rsp *rsp,
			    int lvl, int start, int end)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = rsp->hdr.pcifunc;
	int idx, schq;

	/* For traffic aggregating levels, queue alloc is based
	 * on transmit link to which PF_FUNC is mapped to.
	 */
	if (lvl >= hw->cap.nix_tx_aggr_lvl) {
		/* A single TL queue is allocated */
		if (rsp->schq_contig[lvl]) {
			rsp->schq_contig[lvl] = 1;
			rsp->schq_contig_list[lvl][0] = start;
		}

		/* Both contig and non-contig reqs doesn't make sense here */
		if (rsp->schq_contig[lvl])
			rsp->schq[lvl] = 0;

		if (rsp->schq[lvl]) {
			rsp->schq[lvl] = 1;
			rsp->schq_list[lvl][0] = start;
		}
		return;
	}

	/* Adjust the queue request count if HW supports
	 * only one queue per level configuration.
	 */
	if (hw->cap.nix_fixed_txschq_mapping) {
		idx = pcifunc & RVU_PFVF_FUNC_MASK;
		schq = start + idx;
		if (idx >= (end - start) || test_bit(schq, txsch->schq.bmap)) {
			rsp->schq_contig[lvl] = 0;
			rsp->schq[lvl] = 0;
			return;
		}

		if (rsp->schq_contig[lvl]) {
			rsp->schq_contig[lvl] = 1;
			set_bit(schq, txsch->schq.bmap);
			rsp->schq_contig_list[lvl][0] = schq;
			rsp->schq[lvl] = 0;
		} else if (rsp->schq[lvl]) {
			rsp->schq[lvl] = 1;
			set_bit(schq, txsch->schq.bmap);
			rsp->schq_list[lvl][0] = schq;
		}
		return;
	}

	/* Allocate contiguous queue indices requesty first */
	if (rsp->schq_contig[lvl]) {
		schq = bitmap_find_next_zero_area(txsch->schq.bmap,
						  txsch->schq.max, start,
						  rsp->schq_contig[lvl], 0);
		if (schq >= end)
			rsp->schq_contig[lvl] = 0;
		for (idx = 0; idx < rsp->schq_contig[lvl]; idx++) {
			set_bit(schq, txsch->schq.bmap);
			rsp->schq_contig_list[lvl][idx] = schq;
			schq++;
		}
	}

	/* Allocate non-contiguous queue indices */
	if (rsp->schq[lvl]) {
		idx = 0;
		for (schq = start; schq < end; schq++) {
			if (!test_bit(schq, txsch->schq.bmap)) {
				set_bit(schq, txsch->schq.bmap);
				rsp->schq_list[lvl][idx++] = schq;
			}
			if (idx == rsp->schq[lvl])
				break;
		}
		/* Update how many were allocated */
		rsp->schq[lvl] = idx;
	}
}

int rvu_mbox_handler_nix_txsch_alloc(struct rvu *rvu,
				     struct nix_txsch_alloc_req *req,
				     struct nix_txsch_alloc_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	int link, blkaddr, rc = 0;
	int lvl, idx, start, end;
	struct nix_txsch *txsch;
	struct rvu_pfvf *pfvf;
	struct nix_hw *nix_hw;
	u32 *pfvf_map;
	u16 schq;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	mutex_lock(&rvu->rsrc_lock);

	/* Check if request is valid as per HW capabilities
	 * and can be accomodated.
	 */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		rc = nix_check_txschq_alloc_req(rvu, lvl, pcifunc, nix_hw, req);
		if (rc)
			goto err;
	}

	/* Allocate requested Tx scheduler queues */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		txsch = &nix_hw->txsch[lvl];
		pfvf_map = txsch->pfvf_map;

		if (!req->schq[lvl] && !req->schq_contig[lvl])
			continue;

		rsp->schq[lvl] = req->schq[lvl];
		rsp->schq_contig[lvl] = req->schq_contig[lvl];

		link = nix_get_tx_link(rvu, pcifunc);

		if (lvl >= hw->cap.nix_tx_aggr_lvl) {
			start = link;
			end = link;
		} else if (hw->cap.nix_fixed_txschq_mapping) {
			nix_get_txschq_range(rvu, pcifunc, link, &start, &end);
		} else {
			start = 0;
			end = txsch->schq.max;
		}

		nix_txsch_alloc(rvu, txsch, rsp, lvl, start, end);

		/* Reset queue config */
		for (idx = 0; idx < req->schq_contig[lvl]; idx++) {
			schq = rsp->schq_contig_list[lvl][idx];
			if (!(TXSCH_MAP_FLAGS(pfvf_map[schq]) &
			    NIX_TXSCHQ_CFG_DONE))
				pfvf_map[schq] = TXSCH_MAP(pcifunc, 0);
			nix_reset_tx_linkcfg(rvu, blkaddr, lvl, schq);
			nix_reset_tx_shaping(rvu, blkaddr, lvl, schq);
		}

		for (idx = 0; idx < req->schq[lvl]; idx++) {
			schq = rsp->schq_list[lvl][idx];
			if (!(TXSCH_MAP_FLAGS(pfvf_map[schq]) &
			    NIX_TXSCHQ_CFG_DONE))
				pfvf_map[schq] = TXSCH_MAP(pcifunc, 0);
			nix_reset_tx_linkcfg(rvu, blkaddr, lvl, schq);
			nix_reset_tx_shaping(rvu, blkaddr, lvl, schq);
		}
	}

	rsp->aggr_level = hw->cap.nix_tx_aggr_lvl;
	rsp->aggr_lvl_rr_prio = TXSCH_TL1_DFLT_RR_PRIO;
	rsp->link_cfg_lvl = rvu_read64(rvu, blkaddr,
				       NIX_AF_PSE_CHANNEL_LEVEL) & 0x01 ?
				       NIX_TXSCH_LVL_TL3 : NIX_TXSCH_LVL_TL2;
	goto exit;
err:
	rc = NIX_AF_ERR_TLX_ALLOC_FAIL;
exit:
	mutex_unlock(&rvu->rsrc_lock);
	return rc;
}

static void nix_smq_flush(struct rvu *rvu, int blkaddr,
			  int smq, u16 pcifunc, int nixlf)
{
	int pf = rvu_get_pf(pcifunc);
	u8 cgx_id = 0, lmac_id = 0;
	int err, restore_tx_en = 0;
	u64 cfg;

	/* enable cgx tx if disabled */
	if (is_pf_cgxmapped(rvu, pf)) {
		rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
		restore_tx_en = !cgx_lmac_tx_enable(rvu_cgx_pdata(cgx_id, rvu),
						    lmac_id, true);
	}

	cfg = rvu_read64(rvu, blkaddr, NIX_AF_SMQX_CFG(smq));
	/* Do SMQ flush and set enqueue xoff */
	cfg |= BIT_ULL(50) | BIT_ULL(49);
	rvu_write64(rvu, blkaddr, NIX_AF_SMQX_CFG(smq), cfg);

	/* Disable backpressure from physical link,
	 * otherwise SMQ flush may stall.
	 */
	rvu_cgx_enadis_rx_bp(rvu, pf, false);

	/* Wait for flush to complete */
	err = rvu_poll_reg(rvu, blkaddr,
			   NIX_AF_SMQX_CFG(smq), BIT_ULL(49), true);
	if (err)
		dev_err(rvu->dev,
			"NIXLF%d: SMQ%d flush failed\n", nixlf, smq);

	rvu_cgx_enadis_rx_bp(rvu, pf, true);
	/* restore cgx tx state */
	if (restore_tx_en)
		cgx_lmac_tx_enable(rvu_cgx_pdata(cgx_id, rvu), lmac_id, false);
}

static int nix_txschq_free(struct rvu *rvu, u16 pcifunc)
{
	int blkaddr, nixlf, lvl, schq, err;
	struct rvu_hwinfo *hw = rvu->hw;
	struct nix_txsch *txsch;
	struct nix_hw *nix_hw;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	/* Disable TL2/3 queue links before SMQ flush*/
	mutex_lock(&rvu->rsrc_lock);
	for (lvl = NIX_TXSCH_LVL_TL4; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		if (lvl != NIX_TXSCH_LVL_TL2 && lvl != NIX_TXSCH_LVL_TL4)
			continue;

		txsch = &nix_hw->txsch[lvl];
		for (schq = 0; schq < txsch->schq.max; schq++) {
			if (TXSCH_MAP_FUNC(txsch->pfvf_map[schq]) != pcifunc)
				continue;
			nix_reset_tx_linkcfg(rvu, blkaddr, lvl, schq);
		}
	}

	/* Flush SMQs */
	txsch = &nix_hw->txsch[NIX_TXSCH_LVL_SMQ];
	for (schq = 0; schq < txsch->schq.max; schq++) {
		if (TXSCH_MAP_FUNC(txsch->pfvf_map[schq]) != pcifunc)
			continue;
		nix_smq_flush(rvu, blkaddr, schq, pcifunc, nixlf);
	}

	/* Now free scheduler queues to free pool */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		 /* TLs above aggregation level are shared across all PF
		  * and it's VFs, hence skip freeing them.
		  */
		if (lvl >= hw->cap.nix_tx_aggr_lvl)
			continue;

		txsch = &nix_hw->txsch[lvl];
		for (schq = 0; schq < txsch->schq.max; schq++) {
			if (TXSCH_MAP_FUNC(txsch->pfvf_map[schq]) != pcifunc)
				continue;
			rvu_free_rsrc(&txsch->schq, schq);
			txsch->pfvf_map[schq] = TXSCH_MAP(0, NIX_TXSCHQ_FREE);
		}
	}
	mutex_unlock(&rvu->rsrc_lock);

	/* Sync cached info for this LF in NDC-TX to LLC/DRAM */
	rvu_write64(rvu, blkaddr, NIX_AF_NDC_TX_SYNC, BIT_ULL(12) | nixlf);
	err = rvu_poll_reg(rvu, blkaddr, NIX_AF_NDC_TX_SYNC, BIT_ULL(12), true);
	if (err)
		dev_err(rvu->dev, "NDC-TX sync failed for NIXLF %d\n", nixlf);

	return 0;
}

static int nix_txschq_free_one(struct rvu *rvu,
			       struct nix_txsch_free_req *req)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	int lvl, schq, nixlf, blkaddr;
	struct nix_txsch *txsch;
	struct nix_hw *nix_hw;
	u32 *pfvf_map;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	nixlf = rvu_get_lf(rvu, &hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	lvl = req->schq_lvl;
	schq = req->schq;
	txsch = &nix_hw->txsch[lvl];

	if (lvl >= hw->cap.nix_tx_aggr_lvl || schq >= txsch->schq.max)
		return 0;

	pfvf_map = txsch->pfvf_map;
	mutex_lock(&rvu->rsrc_lock);

	if (TXSCH_MAP_FUNC(pfvf_map[schq]) != pcifunc) {
		mutex_unlock(&rvu->rsrc_lock);
		goto err;
	}

	/* Flush if it is a SMQ. Onus of disabling
	 * TL2/3 queue links before SMQ flush is on user
	 */
	if (lvl == NIX_TXSCH_LVL_SMQ)
		nix_smq_flush(rvu, blkaddr, schq, pcifunc, nixlf);

	/* Free the resource */
	rvu_free_rsrc(&txsch->schq, schq);
	txsch->pfvf_map[schq] = TXSCH_MAP(0, NIX_TXSCHQ_FREE);
	mutex_unlock(&rvu->rsrc_lock);
	return 0;
err:
	return NIX_AF_ERR_TLX_INVALID;
}

int rvu_mbox_handler_nix_txsch_free(struct rvu *rvu,
				    struct nix_txsch_free_req *req,
				    struct msg_rsp *rsp)
{
	if (req->flags & TXSCHQ_FREE_ALL)
		return nix_txschq_free(rvu, req->hdr.pcifunc);
	else
		return nix_txschq_free_one(rvu, req);
}

static bool is_txschq_hierarchy_valid(struct rvu *rvu, u16 pcifunc, int blkaddr,
				      int lvl, u64 reg, u64 regval)
{
	u64 regbase = reg & 0xFFFF;
	u16 schq, parent;

	if (!rvu_check_valid_reg(TXSCHQ_HWREGMAP, lvl, reg))
		return false;

	schq = TXSCHQ_IDX(reg, TXSCHQ_IDX_SHIFT);
	/* Check if this schq belongs to this PF/VF or not */
	if (!is_valid_txschq(rvu, blkaddr, lvl, pcifunc, schq))
		return false;

	parent = (regval >> 16) & 0x1FF;
	/* Validate MDQ's TL4 parent */
	if (regbase == NIX_AF_MDQX_PARENT(0) &&
	    !is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_TL4, pcifunc, parent))
		return false;

	/* Validate TL4's TL3 parent */
	if (regbase == NIX_AF_TL4X_PARENT(0) &&
	    !is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_TL3, pcifunc, parent))
		return false;

	/* Validate TL3's TL2 parent */
	if (regbase == NIX_AF_TL3X_PARENT(0) &&
	    !is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_TL2, pcifunc, parent))
		return false;

	/* Validate TL2's TL1 parent */
	if (regbase == NIX_AF_TL2X_PARENT(0) &&
	    !is_valid_txschq(rvu, blkaddr, NIX_TXSCH_LVL_TL1, pcifunc, parent))
		return false;

	return true;
}

static bool is_txschq_shaping_valid(struct rvu_hwinfo *hw, int lvl, u64 reg)
{
	u64 regbase;

	if (hw->cap.nix_shaping)
		return true;

	/* If shaping and coloring is not supported, then
	 * *_CIR and *_PIR registers should not be configured.
	 */
	regbase = reg & 0xFFFF;

	switch (lvl) {
	case NIX_TXSCH_LVL_TL1:
		if (regbase == NIX_AF_TL1X_CIR(0))
			return false;
		break;
	case NIX_TXSCH_LVL_TL2:
		if (regbase == NIX_AF_TL2X_CIR(0) ||
		    regbase == NIX_AF_TL2X_PIR(0))
			return false;
		break;
	case NIX_TXSCH_LVL_TL3:
		if (regbase == NIX_AF_TL3X_CIR(0) ||
		    regbase == NIX_AF_TL3X_PIR(0))
			return false;
		break;
	case NIX_TXSCH_LVL_TL4:
		if (regbase == NIX_AF_TL4X_CIR(0) ||
		    regbase == NIX_AF_TL4X_PIR(0))
			return false;
		break;
	}
	return true;
}

static void nix_tl1_default_cfg(struct rvu *rvu, struct nix_hw *nix_hw,
				u16 pcifunc, int blkaddr)
{
	u32 *pfvf_map;
	int schq;

	schq = nix_get_tx_link(rvu, pcifunc);
	pfvf_map = nix_hw->txsch[NIX_TXSCH_LVL_TL1].pfvf_map;
	/* Skip if PF has already done the config */
	if (TXSCH_MAP_FLAGS(pfvf_map[schq]) & NIX_TXSCHQ_CFG_DONE)
		return;
	rvu_write64(rvu, blkaddr, NIX_AF_TL1X_TOPOLOGY(schq),
		    (TXSCH_TL1_DFLT_RR_PRIO << 1));
	rvu_write64(rvu, blkaddr, NIX_AF_TL1X_SCHEDULE(schq),
		    TXSCH_TL1_DFLT_RR_QTM);
	rvu_write64(rvu, blkaddr, NIX_AF_TL1X_CIR(schq), 0x00);
	pfvf_map[schq] = TXSCH_SET_FLAG(pfvf_map[schq], NIX_TXSCHQ_CFG_DONE);
}

int rvu_mbox_handler_nix_txschq_cfg(struct rvu *rvu,
				    struct nix_txschq_config *req,
				    struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	u64 reg, regval, schq_regbase;
	struct nix_txsch *txsch;
	struct nix_hw *nix_hw;
	int blkaddr, idx, err;
	int nixlf, schq;
	u32 *pfvf_map;

	if (req->lvl >= NIX_TXSCH_LVL_CNT ||
	    req->num_regs > MAX_REGS_PER_MBOX_MSG)
		return NIX_AF_INVAL_TXSCHQ_CFG;

	err = nix_get_nixlf(rvu, pcifunc, &nixlf, &blkaddr);
	if (err)
		return err;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	txsch = &nix_hw->txsch[req->lvl];
	pfvf_map = txsch->pfvf_map;

	if (req->lvl >= hw->cap.nix_tx_aggr_lvl &&
	    pcifunc & RVU_PFVF_FUNC_MASK) {
		mutex_lock(&rvu->rsrc_lock);
		if (req->lvl == NIX_TXSCH_LVL_TL1)
			nix_tl1_default_cfg(rvu, nix_hw, pcifunc, blkaddr);
		mutex_unlock(&rvu->rsrc_lock);
		return 0;
	}

	for (idx = 0; idx < req->num_regs; idx++) {
		reg = req->reg[idx];
		regval = req->regval[idx];
		schq_regbase = reg & 0xFFFF;

		if (!is_txschq_hierarchy_valid(rvu, pcifunc, blkaddr,
					       txsch->lvl, reg, regval))
			return NIX_AF_INVAL_TXSCHQ_CFG;

		/* Check if shaping and coloring is supported */
		if (!is_txschq_shaping_valid(hw, req->lvl, reg))
			continue;

		/* Replace PF/VF visible NIXLF slot with HW NIXLF id */
		if (schq_regbase == NIX_AF_SMQX_CFG(0)) {
			nixlf = rvu_get_lf(rvu, &hw->block[blkaddr],
					   pcifunc, 0);
			regval &= ~(0x7FULL << 24);
			regval |= ((u64)nixlf << 24);
		}

		/* Clear 'BP_ENA' config, if it's not allowed */
		if (!hw->cap.nix_tx_link_bp) {
			if (schq_regbase == NIX_AF_TL4X_SDP_LINK_CFG(0) ||
			    (schq_regbase & 0xFF00) ==
			    NIX_AF_TL3_TL2X_LINKX_CFG(0, 0))
				regval &= ~BIT_ULL(13);
		}

		/* Mark config as done for TL1 by PF */
		if (schq_regbase >= NIX_AF_TL1X_SCHEDULE(0) &&
		    schq_regbase <= NIX_AF_TL1X_GREEN_BYTES(0)) {
			schq = TXSCHQ_IDX(reg, TXSCHQ_IDX_SHIFT);
			mutex_lock(&rvu->rsrc_lock);
			pfvf_map[schq] = TXSCH_SET_FLAG(pfvf_map[schq],
							NIX_TXSCHQ_CFG_DONE);
			mutex_unlock(&rvu->rsrc_lock);
		}

		/* SMQ flush is special hence split register writes such
		 * that flush first and write rest of the bits later.
		 */
		if (schq_regbase == NIX_AF_SMQX_CFG(0) &&
		    (regval & BIT_ULL(49))) {
			schq = TXSCHQ_IDX(reg, TXSCHQ_IDX_SHIFT);
			nix_smq_flush(rvu, blkaddr, schq, pcifunc, nixlf);
			regval &= ~BIT_ULL(49);
		}
		rvu_write64(rvu, blkaddr, reg, regval);
	}

	return 0;
}

static int nix_rx_vtag_cfg(struct rvu *rvu, int nixlf, int blkaddr,
			   struct nix_vtag_config *req)
{
	u64 regval = req->vtag_size;

	if (req->rx.vtag_type > 7 || req->vtag_size > VTAGSIZE_T8)
		return -EINVAL;

	if (req->rx.capture_vtag)
		regval |= BIT_ULL(5);
	if (req->rx.strip_vtag)
		regval |= BIT_ULL(4);

	rvu_write64(rvu, blkaddr,
		    NIX_AF_LFX_RX_VTAG_TYPEX(nixlf, req->rx.vtag_type), regval);
	return 0;
}

int rvu_mbox_handler_nix_vtag_cfg(struct rvu *rvu,
				  struct nix_vtag_config *req,
				  struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, nixlf, err;

	err = nix_get_nixlf(rvu, pcifunc, &nixlf, &blkaddr);
	if (err)
		return err;

	if (req->cfg_type) {
		err = nix_rx_vtag_cfg(rvu, nixlf, blkaddr, req);
		if (err)
			return NIX_AF_ERR_PARAM;
	} else {
		/* TODO: handle tx vtag configuration */
		return 0;
	}

	return 0;
}

static int nix_setup_mce(struct rvu *rvu, int mce, u8 op,
			 u16 pcifunc, int next, bool eol)
{
	struct nix_aq_enq_req aq_req;
	int err;

	aq_req.hdr.pcifunc = 0;
	aq_req.ctype = NIX_AQ_CTYPE_MCE;
	aq_req.op = op;
	aq_req.qidx = mce;

	/* Forward bcast pkts to RQ0, RSS not needed */
	aq_req.mce.op = 0;
	aq_req.mce.index = 0;
	aq_req.mce.eol = eol;
	aq_req.mce.pf_func = pcifunc;
	aq_req.mce.next = next;

	/* All fields valid */
	*(u64 *)(&aq_req.mce_mask) = ~0ULL;

	err = rvu_nix_aq_enq_inst(rvu, &aq_req, NULL);
	if (err) {
		dev_err(rvu->dev, "Failed to setup Bcast MCE for PF%d:VF%d\n",
			rvu_get_pf(pcifunc), pcifunc & RVU_PFVF_FUNC_MASK);
		return err;
	}
	return 0;
}

static int nix_update_mce_list(struct nix_mce_list *mce_list,
			       u16 pcifunc, bool add)
{
	struct mce *mce, *tail = NULL;
	bool delete = false;

	/* Scan through the current list */
	hlist_for_each_entry(mce, &mce_list->head, node) {
		/* If already exists, then delete */
		if (mce->pcifunc == pcifunc && !add) {
			delete = true;
			break;
		}
		tail = mce;
	}

	if (delete) {
		hlist_del(&mce->node);
		kfree(mce);
		mce_list->count--;
		return 0;
	}

	if (!add)
		return 0;

	/* Add a new one to the list, at the tail */
	mce = kzalloc(sizeof(*mce), GFP_KERNEL);
	if (!mce)
		return -ENOMEM;
	mce->pcifunc = pcifunc;
	if (!tail)
		hlist_add_head(&mce->node, &mce_list->head);
	else
		hlist_add_behind(&mce->node, &tail->node);
	mce_list->count++;
	return 0;
}

int nix_update_bcast_mce_list(struct rvu *rvu, u16 pcifunc, bool add)
{
	int err = 0, idx, next_idx, last_idx;
	struct nix_mce_list *mce_list;
	struct nix_mcast *mcast;
	struct nix_hw *nix_hw;
	struct rvu_pfvf *pfvf;
	struct mce *mce;
	int blkaddr;

	/* Broadcast pkt replication is not needed for AF's VFs, hence skip */
	if (is_afvf(pcifunc))
		return 0;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return 0;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return 0;

	mcast = &nix_hw->mcast;

	/* Get this PF/VF func's MCE index */
	pfvf = rvu_get_pfvf(rvu, pcifunc & ~RVU_PFVF_FUNC_MASK);
	idx = pfvf->bcast_mce_idx + (pcifunc & RVU_PFVF_FUNC_MASK);

	mce_list = &pfvf->bcast_mce_list;
	if (idx > (pfvf->bcast_mce_idx + mce_list->max)) {
		dev_err(rvu->dev,
			"%s: Idx %d > max MCE idx %d, for PF%d bcast list\n",
			__func__, idx, mce_list->max,
			pcifunc >> RVU_PFVF_PF_SHIFT);
		return -EINVAL;
	}

	mutex_lock(&mcast->mce_lock);

	err = nix_update_mce_list(mce_list, pcifunc, add);
	if (err)
		goto end;

	/* Disable MCAM entry in NPC */
	if (!mce_list->count) {
		rvu_npc_enable_bcast_entry(rvu, pcifunc, false);
		goto end;
	}

	/* Dump the updated list to HW */
	idx = pfvf->bcast_mce_idx;
	last_idx = idx + mce_list->count - 1;
	hlist_for_each_entry(mce, &mce_list->head, node) {
		if (idx > last_idx)
			break;

		next_idx = idx + 1;
		/* EOL should be set in last MCE */
		err = nix_setup_mce(rvu, idx, NIX_AQ_INSTOP_WRITE,
				    mce->pcifunc, next_idx,
				    (next_idx > last_idx) ? true : false);
		if (err)
			goto end;
		idx++;
	}

end:
	mutex_unlock(&mcast->mce_lock);
	return err;
}

static int nix_setup_bcast_tables(struct rvu *rvu, struct nix_hw *nix_hw)
{
	struct nix_mcast *mcast = &nix_hw->mcast;
	int err, pf, numvfs, idx;
	struct rvu_pfvf *pfvf;
	u16 pcifunc;
	u64 cfg;

	/* Skip PF0 (i.e AF) */
	for (pf = 1; pf < (rvu->cgx_mapped_pfs + 1); pf++) {
		cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_PFX_CFG(pf));
		/* If PF is not enabled, nothing to do */
		if (!((cfg >> 20) & 0x01))
			continue;
		/* Get numVFs attached to this PF */
		numvfs = (cfg >> 12) & 0xFF;

		pfvf = &rvu->pf[pf];
		/* Save the start MCE */
		pfvf->bcast_mce_idx = nix_alloc_mce_list(mcast, numvfs + 1);

		nix_mce_list_init(&pfvf->bcast_mce_list, numvfs + 1);

		for (idx = 0; idx < (numvfs + 1); idx++) {
			/* idx-0 is for PF, followed by VFs */
			pcifunc = (pf << RVU_PFVF_PF_SHIFT);
			pcifunc |= idx;
			/* Add dummy entries now, so that we don't have to check
			 * for whether AQ_OP should be INIT/WRITE later on.
			 * Will be updated when a NIXLF is attached/detached to
			 * these PF/VFs.
			 */
			err = nix_setup_mce(rvu, pfvf->bcast_mce_idx + idx,
					    NIX_AQ_INSTOP_INIT,
					    pcifunc, 0, true);
			if (err)
				return err;
		}
	}
	return 0;
}

static int nix_setup_mcast(struct rvu *rvu, struct nix_hw *nix_hw, int blkaddr)
{
	struct nix_mcast *mcast = &nix_hw->mcast;
	struct rvu_hwinfo *hw = rvu->hw;
	int err, size;

	size = (rvu_read64(rvu, blkaddr, NIX_AF_CONST3) >> 16) & 0x0F;
	size = (1ULL << size);

	/* Alloc memory for multicast/mirror replication entries */
	err = qmem_alloc(rvu->dev, &mcast->mce_ctx,
			 (256UL << MC_TBL_SIZE), size);
	if (err)
		return -ENOMEM;

	rvu_write64(rvu, blkaddr, NIX_AF_RX_MCAST_BASE,
		    (u64)mcast->mce_ctx->iova);

	/* Set max list length equal to max no of VFs per PF  + PF itself */
	rvu_write64(rvu, blkaddr, NIX_AF_RX_MCAST_CFG,
		    BIT_ULL(36) | (hw->max_vfs_per_pf << 4) | MC_TBL_SIZE);

	/* Alloc memory for multicast replication buffers */
	size = rvu_read64(rvu, blkaddr, NIX_AF_MC_MIRROR_CONST) & 0xFFFF;
	err = qmem_alloc(rvu->dev, &mcast->mcast_buf,
			 (8UL << MC_BUF_CNT), size);
	if (err)
		return -ENOMEM;

	rvu_write64(rvu, blkaddr, NIX_AF_RX_MCAST_BUF_BASE,
		    (u64)mcast->mcast_buf->iova);

	/* Alloc pkind for NIX internal RX multicast/mirror replay */
	mcast->replay_pkind = rvu_alloc_rsrc(&hw->pkind.rsrc);

	rvu_write64(rvu, blkaddr, NIX_AF_RX_MCAST_BUF_CFG,
		    BIT_ULL(63) | (mcast->replay_pkind << 24) |
		    BIT_ULL(20) | MC_BUF_CNT);

	mutex_init(&mcast->mce_lock);

	return nix_setup_bcast_tables(rvu, nix_hw);
}

static int nix_setup_txschq(struct rvu *rvu, struct nix_hw *nix_hw, int blkaddr)
{
	struct nix_txsch *txsch;
	int err, lvl, schq;
	u64 cfg, reg;

	/* Get scheduler queue count of each type and alloc
	 * bitmap for each for alloc/free/attach operations.
	 */
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		txsch = &nix_hw->txsch[lvl];
		txsch->lvl = lvl;
		switch (lvl) {
		case NIX_TXSCH_LVL_SMQ:
			reg = NIX_AF_MDQ_CONST;
			break;
		case NIX_TXSCH_LVL_TL4:
			reg = NIX_AF_TL4_CONST;
			break;
		case NIX_TXSCH_LVL_TL3:
			reg = NIX_AF_TL3_CONST;
			break;
		case NIX_TXSCH_LVL_TL2:
			reg = NIX_AF_TL2_CONST;
			break;
		case NIX_TXSCH_LVL_TL1:
			reg = NIX_AF_TL1_CONST;
			break;
		}
		cfg = rvu_read64(rvu, blkaddr, reg);
		txsch->schq.max = cfg & 0xFFFF;
		err = rvu_alloc_bitmap(&txsch->schq);
		if (err)
			return err;

		/* Allocate memory for scheduler queues to
		 * PF/VF pcifunc mapping info.
		 */
		txsch->pfvf_map = devm_kcalloc(rvu->dev, txsch->schq.max,
					       sizeof(u32), GFP_KERNEL);
		if (!txsch->pfvf_map)
			return -ENOMEM;
		for (schq = 0; schq < txsch->schq.max; schq++)
			txsch->pfvf_map[schq] = TXSCH_MAP(0, NIX_TXSCHQ_FREE);
	}
	return 0;
}

int rvu_nix_reserve_mark_format(struct rvu *rvu, struct nix_hw *nix_hw,
				int blkaddr, u32 cfg)
{
	int fmt_idx;

	for (fmt_idx = 0; fmt_idx < nix_hw->mark_format.in_use; fmt_idx++) {
		if (nix_hw->mark_format.cfg[fmt_idx] == cfg)
			return fmt_idx;
	}
	if (fmt_idx >= nix_hw->mark_format.total)
		return -ERANGE;

	rvu_write64(rvu, blkaddr, NIX_AF_MARK_FORMATX_CTL(fmt_idx), cfg);
	nix_hw->mark_format.cfg[fmt_idx] = cfg;
	nix_hw->mark_format.in_use++;
	return fmt_idx;
}

static int nix_af_mark_format_setup(struct rvu *rvu, struct nix_hw *nix_hw,
				    int blkaddr)
{
	u64 cfgs[] = {
		[NIX_MARK_CFG_IP_DSCP_RED]         = 0x10003,
		[NIX_MARK_CFG_IP_DSCP_YELLOW]      = 0x11200,
		[NIX_MARK_CFG_IP_DSCP_YELLOW_RED]  = 0x11203,
		[NIX_MARK_CFG_IP_ECN_RED]          = 0x6000c,
		[NIX_MARK_CFG_IP_ECN_YELLOW]       = 0x60c00,
		[NIX_MARK_CFG_IP_ECN_YELLOW_RED]   = 0x60c0c,
		[NIX_MARK_CFG_VLAN_DEI_RED]        = 0x30008,
		[NIX_MARK_CFG_VLAN_DEI_YELLOW]     = 0x30800,
		[NIX_MARK_CFG_VLAN_DEI_YELLOW_RED] = 0x30808,
	};
	int i, rc;
	u64 total;

	total = (rvu_read64(rvu, blkaddr, NIX_AF_PSE_CONST) & 0xFF00) >> 8;
	nix_hw->mark_format.total = (u8)total;
	nix_hw->mark_format.cfg = devm_kcalloc(rvu->dev, total, sizeof(u32),
					       GFP_KERNEL);
	if (!nix_hw->mark_format.cfg)
		return -ENOMEM;
	for (i = 0; i < NIX_MARK_CFG_MAX; i++) {
		rc = rvu_nix_reserve_mark_format(rvu, nix_hw, blkaddr, cfgs[i]);
		if (rc < 0)
			dev_err(rvu->dev, "Err %d in setup mark format %d\n",
				i, rc);
	}

	return 0;
}

int rvu_mbox_handler_nix_stats_rst(struct rvu *rvu, struct msg_req *req,
				   struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	int i, nixlf, blkaddr, err;
	u64 stats;

	err = nix_get_nixlf(rvu, pcifunc, &nixlf, &blkaddr);
	if (err)
		return err;

	/* Get stats count supported by HW */
	stats = rvu_read64(rvu, blkaddr, NIX_AF_CONST1);

	/* Reset tx stats */
	for (i = 0; i < ((stats >> 24) & 0xFF); i++)
		rvu_write64(rvu, blkaddr, NIX_AF_LFX_TX_STATX(nixlf, i), 0);

	/* Reset rx stats */
	for (i = 0; i < ((stats >> 32) & 0xFF); i++)
		rvu_write64(rvu, blkaddr, NIX_AF_LFX_RX_STATX(nixlf, i), 0);

	return 0;
}

/* Returns the ALG index to be set into NPC_RX_ACTION */
static int get_flowkey_alg_idx(struct nix_hw *nix_hw, u32 flow_cfg)
{
	int i;

	/* Scan over exiting algo entries to find a match */
	for (i = 0; i < nix_hw->flowkey.in_use; i++)
		if (nix_hw->flowkey.flowkey[i] == flow_cfg)
			return i;

	return -ERANGE;
}

static int set_flowkey_fields(struct nix_rx_flowkey_alg *alg, u32 flow_cfg)
{
	int idx, nr_field, key_off, field_marker, keyoff_marker;
	int max_key_off, max_bit_pos, group_member;
	struct nix_rx_flowkey_alg *field;
	struct nix_rx_flowkey_alg tmp;
	u32 key_type, valid_key;

	if (!alg)
		return -EINVAL;

#define FIELDS_PER_ALG  5
#define MAX_KEY_OFF	40
	/* Clear all fields */
	memset(alg, 0, sizeof(uint64_t) * FIELDS_PER_ALG);

	/* Each of the 32 possible flow key algorithm definitions should
	 * fall into above incremental config (except ALG0). Otherwise a
	 * single NPC MCAM entry is not sufficient for supporting RSS.
	 *
	 * If a different definition or combination needed then NPC MCAM
	 * has to be programmed to filter such pkts and it's action should
	 * point to this definition to calculate flowtag or hash.
	 *
	 * The `for loop` goes over _all_ protocol field and the following
	 * variables depicts the state machine forward progress logic.
	 *
	 * keyoff_marker - Enabled when hash byte length needs to be accounted
	 * in field->key_offset update.
	 * field_marker - Enabled when a new field needs to be selected.
	 * group_member - Enabled when protocol is part of a group.
	 */

	keyoff_marker = 0; max_key_off = 0; group_member = 0;
	nr_field = 0; key_off = 0; field_marker = 1;
	field = &tmp; max_bit_pos = fls(flow_cfg);
	for (idx = 0;
	     idx < max_bit_pos && nr_field < FIELDS_PER_ALG &&
	     key_off < MAX_KEY_OFF; idx++) {
		key_type = BIT(idx);
		valid_key = flow_cfg & key_type;
		/* Found a field marker, reset the field values */
		if (field_marker)
			memset(&tmp, 0, sizeof(tmp));

		field_marker = true;
		keyoff_marker = true;
		switch (key_type) {
		case NIX_FLOW_KEY_TYPE_PORT:
			field->sel_chan = true;
			/* This should be set to 1, when SEL_CHAN is set */
			field->bytesm1 = 1;
			break;
		case NIX_FLOW_KEY_TYPE_IPV4:
		case NIX_FLOW_KEY_TYPE_INNR_IPV4:
			field->lid = NPC_LID_LC;
			field->ltype_match = NPC_LT_LC_IP;
			if (key_type == NIX_FLOW_KEY_TYPE_INNR_IPV4) {
				field->lid = NPC_LID_LG;
				field->ltype_match = NPC_LT_LG_TU_IP;
			}
			field->hdr_offset = 12; /* SIP offset */
			field->bytesm1 = 7; /* SIP + DIP, 8 bytes */
			field->ltype_mask = 0xF; /* Match only IPv4 */
			keyoff_marker = false;
			break;
		case NIX_FLOW_KEY_TYPE_IPV6:
		case NIX_FLOW_KEY_TYPE_INNR_IPV6:
			field->lid = NPC_LID_LC;
			field->ltype_match = NPC_LT_LC_IP6;
			if (key_type == NIX_FLOW_KEY_TYPE_INNR_IPV6) {
				field->lid = NPC_LID_LG;
				field->ltype_match = NPC_LT_LG_TU_IP6;
			}
			field->hdr_offset = 8; /* SIP offset */
			field->bytesm1 = 31; /* SIP + DIP, 32 bytes */
			field->ltype_mask = 0xF; /* Match only IPv6 */
			break;
		case NIX_FLOW_KEY_TYPE_TCP:
		case NIX_FLOW_KEY_TYPE_UDP:
		case NIX_FLOW_KEY_TYPE_SCTP:
		case NIX_FLOW_KEY_TYPE_INNR_TCP:
		case NIX_FLOW_KEY_TYPE_INNR_UDP:
		case NIX_FLOW_KEY_TYPE_INNR_SCTP:
			field->lid = NPC_LID_LD;
			if (key_type == NIX_FLOW_KEY_TYPE_INNR_TCP ||
			    key_type == NIX_FLOW_KEY_TYPE_INNR_UDP ||
			    key_type == NIX_FLOW_KEY_TYPE_INNR_SCTP)
				field->lid = NPC_LID_LH;
			field->bytesm1 = 3; /* Sport + Dport, 4 bytes */

			/* Enum values for NPC_LID_LD and NPC_LID_LG are same,
			 * so no need to change the ltype_match, just change
			 * the lid for inner protocols
			 */
			BUILD_BUG_ON((int)NPC_LT_LD_TCP !=
				     (int)NPC_LT_LH_TU_TCP);
			BUILD_BUG_ON((int)NPC_LT_LD_UDP !=
				     (int)NPC_LT_LH_TU_UDP);
			BUILD_BUG_ON((int)NPC_LT_LD_SCTP !=
				     (int)NPC_LT_LH_TU_SCTP);

			if ((key_type == NIX_FLOW_KEY_TYPE_TCP ||
			     key_type == NIX_FLOW_KEY_TYPE_INNR_TCP) &&
			    valid_key) {
				field->ltype_match |= NPC_LT_LD_TCP;
				group_member = true;
			} else if ((key_type == NIX_FLOW_KEY_TYPE_UDP ||
				    key_type == NIX_FLOW_KEY_TYPE_INNR_UDP) &&
				   valid_key) {
				field->ltype_match |= NPC_LT_LD_UDP;
				group_member = true;
			} else if ((key_type == NIX_FLOW_KEY_TYPE_SCTP ||
				    key_type == NIX_FLOW_KEY_TYPE_INNR_SCTP) &&
				   valid_key) {
				field->ltype_match |= NPC_LT_LD_SCTP;
				group_member = true;
			}
			field->ltype_mask = ~field->ltype_match;
			if (key_type == NIX_FLOW_KEY_TYPE_SCTP ||
			    key_type == NIX_FLOW_KEY_TYPE_INNR_SCTP) {
				/* Handle the case where any of the group item
				 * is enabled in the group but not the final one
				 */
				if (group_member) {
					valid_key = true;
					group_member = false;
				}
			} else {
				field_marker = false;
				keyoff_marker = false;
			}
			break;
		case NIX_FLOW_KEY_TYPE_NVGRE:
			field->lid = NPC_LID_LD;
			field->hdr_offset = 4; /* VSID offset */
			field->bytesm1 = 2;
			field->ltype_match = NPC_LT_LD_NVGRE;
			field->ltype_mask = 0xF;
			break;
		case NIX_FLOW_KEY_TYPE_VXLAN:
		case NIX_FLOW_KEY_TYPE_GENEVE:
			field->lid = NPC_LID_LE;
			field->bytesm1 = 2;
			field->hdr_offset = 4;
			field->ltype_mask = 0xF;
			field_marker = false;
			keyoff_marker = false;

			if (key_type == NIX_FLOW_KEY_TYPE_VXLAN && valid_key) {
				field->ltype_match |= NPC_LT_LE_VXLAN;
				group_member = true;
			}

			if (key_type == NIX_FLOW_KEY_TYPE_GENEVE && valid_key) {
				field->ltype_match |= NPC_LT_LE_GENEVE;
				group_member = true;
			}

			if (key_type == NIX_FLOW_KEY_TYPE_GENEVE) {
				if (group_member) {
					field->ltype_mask = ~field->ltype_match;
					field_marker = true;
					keyoff_marker = true;
					valid_key = true;
					group_member = false;
				}
			}
			break;
		case NIX_FLOW_KEY_TYPE_ETH_DMAC:
		case NIX_FLOW_KEY_TYPE_INNR_ETH_DMAC:
			field->lid = NPC_LID_LA;
			field->ltype_match = NPC_LT_LA_ETHER;
			if (key_type == NIX_FLOW_KEY_TYPE_INNR_ETH_DMAC) {
				field->lid = NPC_LID_LF;
				field->ltype_match = NPC_LT_LF_TU_ETHER;
			}
			field->hdr_offset = 0;
			field->bytesm1 = 5; /* DMAC 6 Byte */
			field->ltype_mask = 0xF;
			break;
		case NIX_FLOW_KEY_TYPE_IPV6_EXT:
			field->lid = NPC_LID_LC;
			field->hdr_offset = 40; /* IPV6 hdr */
			field->bytesm1 = 0; /* 1 Byte ext hdr*/
			field->ltype_match = NPC_LT_LC_IP6_EXT;
			field->ltype_mask = 0xF;
			break;
		case NIX_FLOW_KEY_TYPE_GTPU:
			field->lid = NPC_LID_LE;
			field->hdr_offset = 4;
			field->bytesm1 = 3; /* 4 bytes TID*/
			field->ltype_match = NPC_LT_LE_GTPU;
			field->ltype_mask = 0xF;
			break;
		case NIX_FLOW_KEY_TYPE_VLAN:
			field->lid = NPC_LID_LB;
			field->hdr_offset = 2; /* Skip TPID (2-bytes) */
			field->bytesm1 = 1; /* 2 Bytes (Actually 12 bits) */
			field->ltype_match = NPC_LT_LB_CTAG;
			field->ltype_mask = 0xF;
			field->fn_mask = 1; /* Mask out the first nibble */
			break;
		}
		field->ena = 1;

		/* Found a valid flow key type */
		if (valid_key) {
			field->key_offset = key_off;
			memcpy(&alg[nr_field], field, sizeof(*field));
			max_key_off = max(max_key_off, field->bytesm1 + 1);

			/* Found a field marker, get the next field */
			if (field_marker)
				nr_field++;
		}

		/* Found a keyoff marker, update the new key_off */
		if (keyoff_marker) {
			key_off += max_key_off;
			max_key_off = 0;
		}
	}
	/* Processed all the flow key types */
	if (idx == max_bit_pos && key_off <= MAX_KEY_OFF)
		return 0;
	else
		return NIX_AF_ERR_RSS_NOSPC_FIELD;
}

static int reserve_flowkey_alg_idx(struct rvu *rvu, int blkaddr, u32 flow_cfg)
{
	u64 field[FIELDS_PER_ALG];
	struct nix_hw *hw;
	int fid, rc;

	hw = get_nix_hw(rvu->hw, blkaddr);
	if (!hw)
		return -EINVAL;

	/* No room to add new flow hash algoritham */
	if (hw->flowkey.in_use >= NIX_FLOW_KEY_ALG_MAX)
		return NIX_AF_ERR_RSS_NOSPC_ALGO;

	/* Generate algo fields for the given flow_cfg */
	rc = set_flowkey_fields((struct nix_rx_flowkey_alg *)field, flow_cfg);
	if (rc)
		return rc;

	/* Update ALGX_FIELDX register with generated fields */
	for (fid = 0; fid < FIELDS_PER_ALG; fid++)
		rvu_write64(rvu, blkaddr,
			    NIX_AF_RX_FLOW_KEY_ALGX_FIELDX(hw->flowkey.in_use,
							   fid), field[fid]);

	/* Store the flow_cfg for futher lookup */
	rc = hw->flowkey.in_use;
	hw->flowkey.flowkey[rc] = flow_cfg;
	hw->flowkey.in_use++;

	return rc;
}

int rvu_mbox_handler_nix_rss_flowkey_cfg(struct rvu *rvu,
					 struct nix_rss_flowkey_cfg *req,
					 struct nix_rss_flowkey_cfg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	int alg_idx, nixlf, blkaddr;
	struct nix_hw *nix_hw;
	int err;

	err = nix_get_nixlf(rvu, pcifunc, &nixlf, &blkaddr);
	if (err)
		return err;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	alg_idx = get_flowkey_alg_idx(nix_hw, req->flowkey_cfg);
	/* Failed to get algo index from the exiting list, reserve new  */
	if (alg_idx < 0) {
		alg_idx = reserve_flowkey_alg_idx(rvu, blkaddr,
						  req->flowkey_cfg);
		if (alg_idx < 0)
			return alg_idx;
	}
	rsp->alg_idx = alg_idx;
	rvu_npc_update_flowkey_alg_idx(rvu, pcifunc, nixlf, req->group,
				       alg_idx, req->mcam_index);
	return 0;
}

static int nix_rx_flowkey_alg_cfg(struct rvu *rvu, int blkaddr)
{
	u32 flowkey_cfg, minkey_cfg;
	int alg, fid, rc;

	/* Disable all flow key algx fieldx */
	for (alg = 0; alg < NIX_FLOW_KEY_ALG_MAX; alg++) {
		for (fid = 0; fid < FIELDS_PER_ALG; fid++)
			rvu_write64(rvu, blkaddr,
				    NIX_AF_RX_FLOW_KEY_ALGX_FIELDX(alg, fid),
				    0);
	}

	/* IPv4/IPv6 SIP/DIPs */
	flowkey_cfg = NIX_FLOW_KEY_TYPE_IPV4 | NIX_FLOW_KEY_TYPE_IPV6;
	rc = reserve_flowkey_alg_idx(rvu, blkaddr, flowkey_cfg);
	if (rc < 0)
		return rc;

	/* TCPv4/v6 4-tuple, SIP, DIP, Sport, Dport */
	minkey_cfg = flowkey_cfg;
	flowkey_cfg = minkey_cfg | NIX_FLOW_KEY_TYPE_TCP;
	rc = reserve_flowkey_alg_idx(rvu, blkaddr, flowkey_cfg);
	if (rc < 0)
		return rc;

	/* UDPv4/v6 4-tuple, SIP, DIP, Sport, Dport */
	flowkey_cfg = minkey_cfg | NIX_FLOW_KEY_TYPE_UDP;
	rc = reserve_flowkey_alg_idx(rvu, blkaddr, flowkey_cfg);
	if (rc < 0)
		return rc;

	/* SCTPv4/v6 4-tuple, SIP, DIP, Sport, Dport */
	flowkey_cfg = minkey_cfg | NIX_FLOW_KEY_TYPE_SCTP;
	rc = reserve_flowkey_alg_idx(rvu, blkaddr, flowkey_cfg);
	if (rc < 0)
		return rc;

	/* TCP/UDP v4/v6 4-tuple, rest IP pkts 2-tuple */
	flowkey_cfg = minkey_cfg | NIX_FLOW_KEY_TYPE_TCP |
			NIX_FLOW_KEY_TYPE_UDP;
	rc = reserve_flowkey_alg_idx(rvu, blkaddr, flowkey_cfg);
	if (rc < 0)
		return rc;

	/* TCP/SCTP v4/v6 4-tuple, rest IP pkts 2-tuple */
	flowkey_cfg = minkey_cfg | NIX_FLOW_KEY_TYPE_TCP |
			NIX_FLOW_KEY_TYPE_SCTP;
	rc = reserve_flowkey_alg_idx(rvu, blkaddr, flowkey_cfg);
	if (rc < 0)
		return rc;

	/* UDP/SCTP v4/v6 4-tuple, rest IP pkts 2-tuple */
	flowkey_cfg = minkey_cfg | NIX_FLOW_KEY_TYPE_UDP |
			NIX_FLOW_KEY_TYPE_SCTP;
	rc = reserve_flowkey_alg_idx(rvu, blkaddr, flowkey_cfg);
	if (rc < 0)
		return rc;

	/* TCP/UDP/SCTP v4/v6 4-tuple, rest IP pkts 2-tuple */
	flowkey_cfg = minkey_cfg | NIX_FLOW_KEY_TYPE_TCP |
		      NIX_FLOW_KEY_TYPE_UDP | NIX_FLOW_KEY_TYPE_SCTP;
	rc = reserve_flowkey_alg_idx(rvu, blkaddr, flowkey_cfg);
	if (rc < 0)
		return rc;

	return 0;
}

int rvu_mbox_handler_nix_set_mac_addr(struct rvu *rvu,
				      struct nix_set_mac_addr *req,
				      struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, nixlf, err;
	struct rvu_pfvf *pfvf;

	err = nix_get_nixlf(rvu, pcifunc, &nixlf, &blkaddr);
	if (err)
		return err;

	pfvf = rvu_get_pfvf(rvu, pcifunc);

	ether_addr_copy(pfvf->mac_addr, req->mac_addr);

	rvu_npc_install_ucast_entry(rvu, pcifunc, nixlf,
				    pfvf->rx_chan_base, req->mac_addr);

	rvu_npc_update_rxvlan(rvu, pcifunc, nixlf);

	return 0;
}

int rvu_mbox_handler_nix_get_mac_addr(struct rvu *rvu,
				      struct msg_req *req,
				      struct nix_get_mac_addr_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_pfvf *pfvf;

	if (!is_nixlf_attached(rvu, pcifunc))
		return NIX_AF_ERR_AF_LF_INVALID;

	pfvf = rvu_get_pfvf(rvu, pcifunc);

	ether_addr_copy(rsp->mac_addr, pfvf->mac_addr);

	return 0;
}

int rvu_mbox_handler_nix_set_rx_mode(struct rvu *rvu, struct nix_rx_mode *req,
				     struct msg_rsp *rsp)
{
	bool allmulti = false, disable_promisc = false;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, nixlf, err;
	struct rvu_pfvf *pfvf;

	err = nix_get_nixlf(rvu, pcifunc, &nixlf, &blkaddr);
	if (err)
		return err;

	pfvf = rvu_get_pfvf(rvu, pcifunc);

	if (req->mode & NIX_RX_MODE_PROMISC)
		allmulti = false;
	else if (req->mode & NIX_RX_MODE_ALLMULTI)
		allmulti = true;
	else
		disable_promisc = true;

	if (disable_promisc)
		rvu_npc_disable_promisc_entry(rvu, pcifunc, nixlf);
	else
		rvu_npc_install_promisc_entry(rvu, pcifunc, nixlf,
					      pfvf->rx_chan_base, allmulti);

	rvu_npc_update_rxvlan(rvu, pcifunc, nixlf);

	return 0;
}

static void nix_find_link_frs(struct rvu *rvu,
			      struct nix_frs_cfg *req, u16 pcifunc)
{
	int pf = rvu_get_pf(pcifunc);
	struct rvu_pfvf *pfvf;
	int maxlen, minlen;
	int numvfs, hwvf;
	int vf;

	/* Update with requester's min/max lengths */
	pfvf = rvu_get_pfvf(rvu, pcifunc);
	pfvf->maxlen = req->maxlen;
	if (req->update_minlen)
		pfvf->minlen = req->minlen;

	maxlen = req->maxlen;
	minlen = req->update_minlen ? req->minlen : 0;

	/* Get this PF's numVFs and starting hwvf */
	rvu_get_pf_numvfs(rvu, pf, &numvfs, &hwvf);

	/* For each VF, compare requested max/minlen */
	for (vf = 0; vf < numvfs; vf++) {
		pfvf =  &rvu->hwvf[hwvf + vf];
		if (pfvf->maxlen > maxlen)
			maxlen = pfvf->maxlen;
		if (req->update_minlen &&
		    pfvf->minlen && pfvf->minlen < minlen)
			minlen = pfvf->minlen;
	}

	/* Compare requested max/minlen with PF's max/minlen */
	pfvf = &rvu->pf[pf];
	if (pfvf->maxlen > maxlen)
		maxlen = pfvf->maxlen;
	if (req->update_minlen &&
	    pfvf->minlen && pfvf->minlen < minlen)
		minlen = pfvf->minlen;

	/* Update the request with max/min PF's and it's VF's max/min */
	req->maxlen = maxlen;
	if (req->update_minlen)
		req->minlen = minlen;
}

int rvu_mbox_handler_nix_set_hw_frs(struct rvu *rvu, struct nix_frs_cfg *req,
				    struct msg_rsp *rsp)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc = req->hdr.pcifunc;
	int pf = rvu_get_pf(pcifunc);
	int blkaddr, schq, link = -1;
	struct nix_txsch *txsch;
	u64 cfg, lmac_fifo_len;
	struct nix_hw *nix_hw;
	u8 cgx = 0, lmac = 0;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	if (!req->sdp_link && req->maxlen > NIC_HW_MAX_FRS)
		return NIX_AF_ERR_FRS_INVALID;

	if (req->update_minlen && req->minlen < NIC_HW_MIN_FRS)
		return NIX_AF_ERR_FRS_INVALID;

	/* Check if requester wants to update SMQ's */
	if (!req->update_smq)
		goto rx_frscfg;

	/* Update min/maxlen in each of the SMQ attached to this PF/VF */
	txsch = &nix_hw->txsch[NIX_TXSCH_LVL_SMQ];
	mutex_lock(&rvu->rsrc_lock);
	for (schq = 0; schq < txsch->schq.max; schq++) {
		if (TXSCH_MAP_FUNC(txsch->pfvf_map[schq]) != pcifunc)
			continue;
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_SMQX_CFG(schq));
		cfg = (cfg & ~(0xFFFFULL << 8)) | ((u64)req->maxlen << 8);
		if (req->update_minlen)
			cfg = (cfg & ~0x7FULL) | ((u64)req->minlen & 0x7F);
		rvu_write64(rvu, blkaddr, NIX_AF_SMQX_CFG(schq), cfg);
	}
	mutex_unlock(&rvu->rsrc_lock);

rx_frscfg:
	/* Check if config is for SDP link */
	if (req->sdp_link) {
		if (!hw->sdp_links)
			return NIX_AF_ERR_RX_LINK_INVALID;
		link = hw->cgx_links + hw->lbk_links;
		goto linkcfg;
	}

	/* Check if the request is from CGX mapped RVU PF */
	if (is_pf_cgxmapped(rvu, pf)) {
		/* Get CGX and LMAC to which this PF is mapped and find link */
		rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx, &lmac);
		link = (cgx * hw->lmac_per_cgx) + lmac;
	} else if (pf == 0) {
		/* For VFs of PF0 ingress is LBK port, so config LBK link */
		link = hw->cgx_links;
	}

	if (link < 0)
		return NIX_AF_ERR_RX_LINK_INVALID;

	nix_find_link_frs(rvu, req, pcifunc);

linkcfg:
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_RX_LINKX_CFG(link));
	cfg = (cfg & ~(0xFFFFULL << 16)) | ((u64)req->maxlen << 16);
	if (req->update_minlen)
		cfg = (cfg & ~0xFFFFULL) | req->minlen;
	rvu_write64(rvu, blkaddr, NIX_AF_RX_LINKX_CFG(link), cfg);

	if (req->sdp_link || pf == 0)
		return 0;

	/* Update transmit credits for CGX links */
	lmac_fifo_len =
		CGX_FIFO_LEN / cgx_get_lmac_cnt(rvu_cgx_pdata(cgx, rvu));
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_TX_LINKX_NORM_CREDIT(link));
	cfg &= ~(0xFFFFFULL << 12);
	cfg |=  ((lmac_fifo_len - req->maxlen) / 16) << 12;
	rvu_write64(rvu, blkaddr, NIX_AF_TX_LINKX_NORM_CREDIT(link), cfg);
	return 0;
}

int rvu_mbox_handler_nix_rxvlan_alloc(struct rvu *rvu, struct msg_req *req,
				      struct msg_rsp *rsp)
{
	struct npc_mcam_alloc_entry_req alloc_req = { };
	struct npc_mcam_alloc_entry_rsp alloc_rsp = { };
	struct npc_mcam_free_entry_req free_req = { };
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, nixlf, err;
	struct rvu_pfvf *pfvf;

	/* LBK VFs do not have separate MCAM UCAST entry hence
	 * skip allocating rxvlan for them
	 */
	if (is_afvf(pcifunc))
		return 0;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	if (pfvf->rxvlan)
		return 0;

	/* alloc new mcam entry */
	alloc_req.hdr.pcifunc = pcifunc;
	alloc_req.count = 1;

	err = rvu_mbox_handler_npc_mcam_alloc_entry(rvu, &alloc_req,
						    &alloc_rsp);
	if (err)
		return err;

	/* update entry to enable rxvlan offload */
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0) {
		err = NIX_AF_ERR_AF_LF_INVALID;
		goto free_entry;
	}

	nixlf = rvu_get_lf(rvu, &rvu->hw->block[blkaddr], pcifunc, 0);
	if (nixlf < 0) {
		err = NIX_AF_ERR_AF_LF_INVALID;
		goto free_entry;
	}

	pfvf->rxvlan_index = alloc_rsp.entry_list[0];
	/* all it means is that rxvlan_index is valid */
	pfvf->rxvlan = true;

	err = rvu_npc_update_rxvlan(rvu, pcifunc, nixlf);
	if (err)
		goto free_entry;

	return 0;
free_entry:
	free_req.hdr.pcifunc = pcifunc;
	free_req.entry = alloc_rsp.entry_list[0];
	rvu_mbox_handler_npc_mcam_free_entry(rvu, &free_req, rsp);
	pfvf->rxvlan = false;
	return err;
}

int rvu_mbox_handler_nix_set_rx_cfg(struct rvu *rvu, struct nix_rx_cfg *req,
				    struct msg_rsp *rsp)
{
	int nixlf, blkaddr, err;
	u64 cfg;

	err = nix_get_nixlf(rvu, req->hdr.pcifunc, &nixlf, &blkaddr);
	if (err)
		return err;

	cfg = rvu_read64(rvu, blkaddr, NIX_AF_LFX_RX_CFG(nixlf));
	/* Set the interface configuration */
	if (req->len_verify & BIT(0))
		cfg |= BIT_ULL(41);
	else
		cfg &= ~BIT_ULL(41);

	if (req->len_verify & BIT(1))
		cfg |= BIT_ULL(40);
	else
		cfg &= ~BIT_ULL(40);

	if (req->csum_verify & BIT(0))
		cfg |= BIT_ULL(37);
	else
		cfg &= ~BIT_ULL(37);

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_RX_CFG(nixlf), cfg);

	return 0;
}

static void nix_link_config(struct rvu *rvu, int blkaddr)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int cgx, lmac_cnt, slink, link;
	u64 tx_credits;

	/* Set default min/max packet lengths allowed on NIX Rx links.
	 *
	 * With HW reset minlen value of 60byte, HW will treat ARP pkts
	 * as undersize and report them to SW as error pkts, hence
	 * setting it to 40 bytes.
	 */
	for (link = 0; link < (hw->cgx_links + hw->lbk_links); link++) {
		rvu_write64(rvu, blkaddr, NIX_AF_RX_LINKX_CFG(link),
			    NIC_HW_MAX_FRS << 16 | NIC_HW_MIN_FRS);
	}

	if (hw->sdp_links) {
		link = hw->cgx_links + hw->lbk_links;
		rvu_write64(rvu, blkaddr, NIX_AF_RX_LINKX_CFG(link),
			    SDP_HW_MAX_FRS << 16 | NIC_HW_MIN_FRS);
	}

	/* Set credits for Tx links assuming max packet length allowed.
	 * This will be reconfigured based on MTU set for PF/VF.
	 */
	for (cgx = 0; cgx < hw->cgx; cgx++) {
		lmac_cnt = cgx_get_lmac_cnt(rvu_cgx_pdata(cgx, rvu));
		tx_credits = ((CGX_FIFO_LEN / lmac_cnt) - NIC_HW_MAX_FRS) / 16;
		/* Enable credits and set credit pkt count to max allowed */
		tx_credits =  (tx_credits << 12) | (0x1FF << 2) | BIT_ULL(1);
		slink = cgx * hw->lmac_per_cgx;
		for (link = slink; link < (slink + lmac_cnt); link++) {
			rvu_write64(rvu, blkaddr,
				    NIX_AF_TX_LINKX_NORM_CREDIT(link),
				    tx_credits);
		}
	}

	/* Set Tx credits for LBK link */
	slink = hw->cgx_links;
	for (link = slink; link < (slink + hw->lbk_links); link++) {
		tx_credits = 1000; /* 10 * max LBK datarate = 10 * 100Gbps */
		/* Enable credits and set credit pkt count to max allowed */
		tx_credits =  (tx_credits << 12) | (0x1FF << 2) | BIT_ULL(1);
		rvu_write64(rvu, blkaddr,
			    NIX_AF_TX_LINKX_NORM_CREDIT(link), tx_credits);
	}
}

static int nix_calibrate_x2p(struct rvu *rvu, int blkaddr)
{
	int idx, err;
	u64 status;

	/* Start X2P bus calibration */
	rvu_write64(rvu, blkaddr, NIX_AF_CFG,
		    rvu_read64(rvu, blkaddr, NIX_AF_CFG) | BIT_ULL(9));
	/* Wait for calibration to complete */
	err = rvu_poll_reg(rvu, blkaddr,
			   NIX_AF_STATUS, BIT_ULL(10), false);
	if (err) {
		dev_err(rvu->dev, "NIX X2P bus calibration failed\n");
		return err;
	}

	status = rvu_read64(rvu, blkaddr, NIX_AF_STATUS);
	/* Check if CGX devices are ready */
	for (idx = 0; idx < rvu->cgx_cnt_max; idx++) {
		/* Skip when cgx port is not available */
		if (!rvu_cgx_pdata(idx, rvu) ||
		    (status & (BIT_ULL(16 + idx))))
			continue;
		dev_err(rvu->dev,
			"CGX%d didn't respond to NIX X2P calibration\n", idx);
		err = -EBUSY;
	}

	/* Check if LBK is ready */
	if (!(status & BIT_ULL(19))) {
		dev_err(rvu->dev,
			"LBK didn't respond to NIX X2P calibration\n");
		err = -EBUSY;
	}

	/* Clear 'calibrate_x2p' bit */
	rvu_write64(rvu, blkaddr, NIX_AF_CFG,
		    rvu_read64(rvu, blkaddr, NIX_AF_CFG) & ~BIT_ULL(9));
	if (err || (status & 0x3FFULL))
		dev_err(rvu->dev,
			"NIX X2P calibration failed, status 0x%llx\n", status);
	if (err)
		return err;
	return 0;
}

static int nix_aq_init(struct rvu *rvu, struct rvu_block *block)
{
	u64 cfg;
	int err;

	/* Set admin queue endianness */
	cfg = rvu_read64(rvu, block->addr, NIX_AF_CFG);
#ifdef __BIG_ENDIAN
	cfg |= BIT_ULL(8);
	rvu_write64(rvu, block->addr, NIX_AF_CFG, cfg);
#else
	cfg &= ~BIT_ULL(8);
	rvu_write64(rvu, block->addr, NIX_AF_CFG, cfg);
#endif

	/* Do not bypass NDC cache */
	cfg = rvu_read64(rvu, block->addr, NIX_AF_NDC_CFG);
	cfg &= ~0x3FFEULL;
#ifdef CONFIG_NDC_DIS_DYNAMIC_CACHING
	/* Disable caching of SQB aka SQEs */
	cfg |= 0x04ULL;
#endif
	rvu_write64(rvu, block->addr, NIX_AF_NDC_CFG, cfg);

	/* Result structure can be followed by RQ/SQ/CQ context at
	 * RES + 128bytes and a write mask at RES + 256 bytes, depending on
	 * operation type. Alloc sufficient result memory for all operations.
	 */
	err = rvu_aq_alloc(rvu, &block->aq,
			   Q_COUNT(AQ_SIZE), sizeof(struct nix_aq_inst_s),
			   ALIGN(sizeof(struct nix_aq_res_s), 128) + 256);
	if (err)
		return err;

	rvu_write64(rvu, block->addr, NIX_AF_AQ_CFG, AQ_SIZE);
	rvu_write64(rvu, block->addr,
		    NIX_AF_AQ_BASE, (u64)block->aq->inst->iova);
	return 0;
}

int rvu_nix_init(struct rvu *rvu)
{
	const struct npc_lt_def_cfg *ltdefs;
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int blkaddr, err;
	u64 cfg;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return 0;
	block = &hw->block[blkaddr];

	if (is_rvu_96xx_B0(rvu)) {
		/* As per a HW errata in 96xx A0/B0 silicon, NIX may corrupt
		 * internal state when conditional clocks are turned off.
		 * Hence enable them.
		 */
		rvu_write64(rvu, blkaddr, NIX_AF_CFG,
			    rvu_read64(rvu, blkaddr, NIX_AF_CFG) | 0x40ULL);

		/* Set chan/link to backpressure TL3 instead of TL2 */
		rvu_write64(rvu, blkaddr, NIX_AF_PSE_CHANNEL_LEVEL, 0x01);

		/* Disable SQ manager's sticky mode operation (set TM6 = 0)
		 * This sticky mode is known to cause SQ stalls when multiple
		 * SQs are mapped to same SMQ and transmitting pkts at a time.
		 */
		cfg = rvu_read64(rvu, blkaddr, NIX_AF_SQM_DBG_CTL_STATUS);
		cfg &= ~BIT_ULL(15);
		rvu_write64(rvu, blkaddr, NIX_AF_SQM_DBG_CTL_STATUS, cfg);
	}

	ltdefs = rvu->kpu.lt_def;
	/* Calibrate X2P bus to check if CGX/LBK links are fine */
	err = nix_calibrate_x2p(rvu, blkaddr);
	if (err)
		return err;

	/* Set num of links of each type */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST);
	hw->cgx = (cfg >> 12) & 0xF;
	hw->lmac_per_cgx = (cfg >> 8) & 0xF;
	hw->cgx_links = hw->cgx * hw->lmac_per_cgx;
	hw->lbk_links = 1;
	hw->sdp_links = 1;

	/* Initialize admin queue */
	err = nix_aq_init(rvu, block);
	if (err)
		return err;

	/* Restore CINT timer delay to HW reset values */
	rvu_write64(rvu, blkaddr, NIX_AF_CINT_DELAY, 0x0ULL);

	if (blkaddr == BLKADDR_NIX0) {
		hw->nix0 = devm_kzalloc(rvu->dev,
					sizeof(struct nix_hw), GFP_KERNEL);
		if (!hw->nix0)
			return -ENOMEM;

		err = nix_setup_txschq(rvu, hw->nix0, blkaddr);
		if (err)
			return err;

		err = nix_af_mark_format_setup(rvu, hw->nix0, blkaddr);
		if (err)
			return err;

		err = nix_setup_mcast(rvu, hw->nix0, blkaddr);
		if (err)
			return err;

		/* Configure segmentation offload formats */
		nix_setup_lso(rvu, hw->nix0, blkaddr);

		/* Config Outer/Inner L2, IP, TCP, UDP and SCTP NPC layer info.
		 * This helps HW protocol checker to identify headers
		 * and validate length and checksums.
		 */
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OL2,
			    (ltdefs->rx_ol2.lid << 8) | (ltdefs->rx_ol2.ltype_match << 4) |
			    ltdefs->rx_ol2.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OIP4,
			    (ltdefs->rx_oip4.lid << 8) | (ltdefs->rx_oip4.ltype_match << 4) |
			    ltdefs->rx_oip4.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_IIP4,
			    (ltdefs->rx_iip4.lid << 8) | (ltdefs->rx_iip4.ltype_match << 4) |
			    ltdefs->rx_iip4.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OIP6,
			    (ltdefs->rx_oip6.lid << 8) | (ltdefs->rx_oip6.ltype_match << 4) |
			    ltdefs->rx_oip6.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_IIP6,
			    (ltdefs->rx_iip6.lid << 8) | (ltdefs->rx_iip6.ltype_match << 4) |
			    ltdefs->rx_iip6.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OTCP,
			    (ltdefs->rx_otcp.lid << 8) | (ltdefs->rx_otcp.ltype_match << 4) |
			    ltdefs->rx_otcp.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_ITCP,
			    (ltdefs->rx_itcp.lid << 8) | (ltdefs->rx_itcp.ltype_match << 4) |
			    ltdefs->rx_itcp.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OUDP,
			    (ltdefs->rx_oudp.lid << 8) | (ltdefs->rx_oudp.ltype_match << 4) |
			    ltdefs->rx_oudp.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_IUDP,
			    (ltdefs->rx_iudp.lid << 8) | (ltdefs->rx_iudp.ltype_match << 4) |
			    ltdefs->rx_iudp.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_OSCTP,
			    (ltdefs->rx_osctp.lid << 8) | (ltdefs->rx_osctp.ltype_match << 4) |
			    ltdefs->rx_osctp.ltype_mask);
		rvu_write64(rvu, blkaddr, NIX_AF_RX_DEF_ISCTP,
			    (ltdefs->rx_isctp.lid << 8) | (ltdefs->rx_isctp.ltype_match << 4) |
			    ltdefs->rx_isctp.ltype_mask);

		err = nix_rx_flowkey_alg_cfg(rvu, blkaddr);
		if (err)
			return err;

		/* Initialize CGX/LBK/SDP link credits, min/max pkt lengths */
		nix_link_config(rvu, blkaddr);

		/* Enable Channel backpressure */
		rvu_write64(rvu, blkaddr, NIX_AF_RX_CFG, BIT_ULL(0));
	}
	return 0;
}

void rvu_nix_freemem(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	struct nix_txsch *txsch;
	struct nix_mcast *mcast;
	struct nix_hw *nix_hw;
	int blkaddr, lvl;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, 0);
	if (blkaddr < 0)
		return;

	block = &hw->block[blkaddr];
	rvu_aq_free(rvu, block->aq);

	if (blkaddr == BLKADDR_NIX0) {
		nix_hw = get_nix_hw(rvu->hw, blkaddr);
		if (!nix_hw)
			return;

		for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
			txsch = &nix_hw->txsch[lvl];
			kfree(txsch->schq.bmap);
		}

		mcast = &nix_hw->mcast;
		qmem_free(rvu->dev, mcast->mce_ctx);
		qmem_free(rvu->dev, mcast->mcast_buf);
		mutex_destroy(&mcast->mce_lock);
	}
}

int rvu_mbox_handler_nix_lf_start_rx(struct rvu *rvu, struct msg_req *req,
				     struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	int nixlf, err;

	err = nix_get_nixlf(rvu, pcifunc, &nixlf, NULL);
	if (err)
		return err;

	rvu_npc_enable_default_entries(rvu, pcifunc, nixlf);

	return rvu_cgx_start_stop_io(rvu, pcifunc, true);
}

int rvu_mbox_handler_nix_lf_stop_rx(struct rvu *rvu, struct msg_req *req,
				    struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	int nixlf, err;

	err = nix_get_nixlf(rvu, pcifunc, &nixlf, NULL);
	if (err)
		return err;

	rvu_npc_disable_default_entries(rvu, pcifunc, nixlf);

	return rvu_cgx_start_stop_io(rvu, pcifunc, false);
}

void rvu_nix_lf_teardown(struct rvu *rvu, u16 pcifunc, int blkaddr, int nixlf)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	struct hwctx_disable_req ctx_req;
	int err;

	ctx_req.hdr.pcifunc = pcifunc;

	/* Cleanup NPC MCAM entries, free Tx scheduler queues being used */
	nix_interface_deinit(rvu, pcifunc, nixlf);
	nix_rx_sync(rvu, blkaddr);
	nix_txschq_free(rvu, pcifunc);

	rvu_cgx_start_stop_io(rvu, pcifunc, false);

	if (pfvf->sq_ctx) {
		ctx_req.ctype = NIX_AQ_CTYPE_SQ;
		err = nix_lf_hwctx_disable(rvu, &ctx_req);
		if (err)
			dev_err(rvu->dev, "SQ ctx disable failed\n");
	}

	if (pfvf->rq_ctx) {
		ctx_req.ctype = NIX_AQ_CTYPE_RQ;
		err = nix_lf_hwctx_disable(rvu, &ctx_req);
		if (err)
			dev_err(rvu->dev, "RQ ctx disable failed\n");
	}

	if (pfvf->cq_ctx) {
		ctx_req.ctype = NIX_AQ_CTYPE_CQ;
		err = nix_lf_hwctx_disable(rvu, &ctx_req);
		if (err)
			dev_err(rvu->dev, "CQ ctx disable failed\n");
	}

	nix_ctx_free(rvu, pfvf);
}

#define NIX_AF_LFX_TX_CFG_PTP_EN	BIT_ULL(32)

static int rvu_nix_lf_ptp_tx_cfg(struct rvu *rvu, u16 pcifunc, bool enable)
{
	struct rvu_hwinfo *hw = rvu->hw;
	struct rvu_block *block;
	int blkaddr;
	int nixlf;
	u64 cfg;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	block = &hw->block[blkaddr];
	nixlf = rvu_get_lf(rvu, block, pcifunc, 0);
	if (nixlf < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	cfg = rvu_read64(rvu, blkaddr, NIX_AF_LFX_TX_CFG(nixlf));

	if (enable)
		cfg |= NIX_AF_LFX_TX_CFG_PTP_EN;
	else
		cfg &= ~NIX_AF_LFX_TX_CFG_PTP_EN;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_TX_CFG(nixlf), cfg);

	return 0;
}

int rvu_mbox_handler_nix_lf_ptp_tx_enable(struct rvu *rvu, struct msg_req *req,
					  struct msg_rsp *rsp)
{
	return rvu_nix_lf_ptp_tx_cfg(rvu, req->hdr.pcifunc, true);
}

int rvu_mbox_handler_nix_lf_ptp_tx_disable(struct rvu *rvu, struct msg_req *req,
					   struct msg_rsp *rsp)
{
	return rvu_nix_lf_ptp_tx_cfg(rvu, req->hdr.pcifunc, false);
}

int rvu_mbox_handler_nix_lso_format_cfg(struct rvu *rvu,
					struct nix_lso_format_cfg *req,
					struct nix_lso_format_cfg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct nix_hw *nix_hw;
	struct rvu_pfvf *pfvf;
	int blkaddr, idx, f;
	u64 reg;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NIX, pcifunc);
	if (!pfvf->nixlf || blkaddr < 0)
		return NIX_AF_ERR_AF_LF_INVALID;

	nix_hw = get_nix_hw(rvu->hw, blkaddr);
	if (!nix_hw)
		return -EINVAL;

	/* Find existing matching LSO format, if any */
	for (idx = 0; idx < nix_hw->lso.in_use; idx++) {
		for (f = 0; f < NIX_LSO_FIELD_MAX; f++) {
			reg = rvu_read64(rvu, blkaddr,
					 NIX_AF_LSO_FORMATX_FIELDX(idx, f));
			if (req->fields[f] != (reg & req->field_mask))
				break;
		}

		if (f == NIX_LSO_FIELD_MAX)
			break;
	}

	if (idx < nix_hw->lso.in_use) {
		/* Match found */
		rsp->lso_format_idx = idx;
		return 0;
	}

	if (nix_hw->lso.in_use == nix_hw->lso.total)
		return NIX_AF_ERR_LSO_CFG_FAIL;

	rsp->lso_format_idx = nix_hw->lso.in_use++;

	for (f = 0; f < NIX_LSO_FIELD_MAX; f++)
		rvu_write64(rvu, blkaddr,
			    NIX_AF_LSO_FORMATX_FIELDX(rsp->lso_format_idx, f),
			    req->fields[f]);

	return 0;
}
