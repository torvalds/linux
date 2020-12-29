/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <net/ipv6.h>

#include "cxgb4.h"
#include "t4_regs.h"
#include "t4_tcb.h"
#include "t4_values.h"
#include "clip_tbl.h"
#include "l2t.h"
#include "smt.h"
#include "t4fw_api.h"
#include "cxgb4_filter.h"

static inline bool is_field_set(u32 val, u32 mask)
{
	return val || mask;
}

static inline bool unsupported(u32 conf, u32 conf_mask, u32 val, u32 mask)
{
	return !(conf & conf_mask) && is_field_set(val, mask);
}

static int set_tcb_field(struct adapter *adap, struct filter_entry *f,
			 unsigned int ftid,  u16 word, u64 mask, u64 val,
			 int no_reply)
{
	struct cpl_set_tcb_field *req;
	struct sk_buff *skb;

	skb = alloc_skb(sizeof(struct cpl_set_tcb_field), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	req = (struct cpl_set_tcb_field *)__skb_put_zero(skb, sizeof(*req));
	INIT_TP_WR_CPL(req, CPL_SET_TCB_FIELD, ftid);
	req->reply_ctrl = htons(REPLY_CHAN_V(0) |
				QUEUENO_V(adap->sge.fw_evtq.abs_id) |
				NO_REPLY_V(no_reply));
	req->word_cookie = htons(TCB_WORD_V(word) | TCB_COOKIE_V(ftid));
	req->mask = cpu_to_be64(mask);
	req->val = cpu_to_be64(val);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, f->fs.val.iport & 0x3);
	t4_ofld_send(adap, skb);
	return 0;
}

/* Set one of the t_flags bits in the TCB.
 */
static int set_tcb_tflag(struct adapter *adap, struct filter_entry *f,
			 unsigned int ftid, unsigned int bit_pos,
			 unsigned int val, int no_reply)
{
	return set_tcb_field(adap, f, ftid,  TCB_T_FLAGS_W, 1ULL << bit_pos,
			     (unsigned long long)val << bit_pos, no_reply);
}

static void mk_abort_req_ulp(struct cpl_abort_req *abort_req, unsigned int tid)
{
	struct ulp_txpkt *txpkt = (struct ulp_txpkt *)abort_req;
	struct ulptx_idata *sc = (struct ulptx_idata *)(txpkt + 1);

	txpkt->cmd_dest = htonl(ULPTX_CMD_V(ULP_TX_PKT) | ULP_TXPKT_DEST_V(0));
	txpkt->len = htonl(DIV_ROUND_UP(sizeof(*abort_req), 16));
	sc->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_IMM));
	sc->len = htonl(sizeof(*abort_req) - sizeof(struct work_request_hdr));
	OPCODE_TID(abort_req) = htonl(MK_OPCODE_TID(CPL_ABORT_REQ, tid));
	abort_req->rsvd0 = htonl(0);
	abort_req->rsvd1 = 0;
	abort_req->cmd = CPL_ABORT_NO_RST;
}

static void mk_abort_rpl_ulp(struct cpl_abort_rpl *abort_rpl, unsigned int tid)
{
	struct ulp_txpkt *txpkt = (struct ulp_txpkt *)abort_rpl;
	struct ulptx_idata *sc = (struct ulptx_idata *)(txpkt + 1);

	txpkt->cmd_dest = htonl(ULPTX_CMD_V(ULP_TX_PKT) | ULP_TXPKT_DEST_V(0));
	txpkt->len = htonl(DIV_ROUND_UP(sizeof(*abort_rpl), 16));
	sc->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_IMM));
	sc->len = htonl(sizeof(*abort_rpl) - sizeof(struct work_request_hdr));
	OPCODE_TID(abort_rpl) = htonl(MK_OPCODE_TID(CPL_ABORT_RPL, tid));
	abort_rpl->rsvd0 = htonl(0);
	abort_rpl->rsvd1 = 0;
	abort_rpl->cmd = CPL_ABORT_NO_RST;
}

static void mk_set_tcb_ulp(struct filter_entry *f,
			   struct cpl_set_tcb_field *req,
			   unsigned int word, u64 mask, u64 val,
			   u8 cookie, int no_reply)
{
	struct ulp_txpkt *txpkt = (struct ulp_txpkt *)req;
	struct ulptx_idata *sc = (struct ulptx_idata *)(txpkt + 1);

	txpkt->cmd_dest = htonl(ULPTX_CMD_V(ULP_TX_PKT) | ULP_TXPKT_DEST_V(0));
	txpkt->len = htonl(DIV_ROUND_UP(sizeof(*req), 16));
	sc->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_IMM));
	sc->len = htonl(sizeof(*req) - sizeof(struct work_request_hdr));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, f->tid));
	req->reply_ctrl = htons(NO_REPLY_V(no_reply) | REPLY_CHAN_V(0) |
				QUEUENO_V(0));
	req->word_cookie = htons(TCB_WORD_V(word) | TCB_COOKIE_V(cookie));
	req->mask = cpu_to_be64(mask);
	req->val = cpu_to_be64(val);
	sc = (struct ulptx_idata *)(req + 1);
	sc->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_NOOP));
	sc->len = htonl(0);
}

static int configure_filter_smac(struct adapter *adap, struct filter_entry *f)
{
	int err;

	/* do a set-tcb for smac-sel and CWR bit.. */
	err = set_tcb_field(adap, f, f->tid, TCB_SMAC_SEL_W,
			    TCB_SMAC_SEL_V(TCB_SMAC_SEL_M),
			    TCB_SMAC_SEL_V(f->smt->idx), 1);
	if (err)
		goto smac_err;

	err = set_tcb_tflag(adap, f, f->tid, TF_CCTRL_CWR_S, 1, 1);
	if (!err)
		return 0;

smac_err:
	dev_err(adap->pdev_dev, "filter %u smac config failed with error %u\n",
		f->tid, err);
	return err;
}

static void set_nat_params(struct adapter *adap, struct filter_entry *f,
			   unsigned int tid, bool dip, bool sip, bool dp,
			   bool sp)
{
	if (dip) {
		if (f->fs.type) {
			set_tcb_field(adap, f, tid, TCB_SND_UNA_RAW_W,
				      WORD_MASK, f->fs.nat_lip[15] |
				      f->fs.nat_lip[14] << 8 |
				      f->fs.nat_lip[13] << 16 |
				      f->fs.nat_lip[12] << 24, 1);

			set_tcb_field(adap, f, tid, TCB_SND_UNA_RAW_W + 1,
				      WORD_MASK, f->fs.nat_lip[11] |
				      f->fs.nat_lip[10] << 8 |
				      f->fs.nat_lip[9] << 16 |
				      f->fs.nat_lip[8] << 24, 1);

			set_tcb_field(adap, f, tid, TCB_SND_UNA_RAW_W + 2,
				      WORD_MASK, f->fs.nat_lip[7] |
				      f->fs.nat_lip[6] << 8 |
				      f->fs.nat_lip[5] << 16 |
				      f->fs.nat_lip[4] << 24, 1);

			set_tcb_field(adap, f, tid, TCB_SND_UNA_RAW_W + 3,
				      WORD_MASK, f->fs.nat_lip[3] |
				      f->fs.nat_lip[2] << 8 |
				      f->fs.nat_lip[1] << 16 |
				      f->fs.nat_lip[0] << 24, 1);
		} else {
			set_tcb_field(adap, f, tid, TCB_RX_FRAG3_LEN_RAW_W,
				      WORD_MASK, f->fs.nat_lip[3] |
				      f->fs.nat_lip[2] << 8 |
				      f->fs.nat_lip[1] << 16 |
				      f->fs.nat_lip[0] << 24, 1);
		}
	}

	if (sip) {
		if (f->fs.type) {
			set_tcb_field(adap, f, tid, TCB_RX_FRAG2_PTR_RAW_W,
				      WORD_MASK, f->fs.nat_fip[15] |
				      f->fs.nat_fip[14] << 8 |
				      f->fs.nat_fip[13] << 16 |
				      f->fs.nat_fip[12] << 24, 1);

			set_tcb_field(adap, f, tid, TCB_RX_FRAG2_PTR_RAW_W + 1,
				      WORD_MASK, f->fs.nat_fip[11] |
				      f->fs.nat_fip[10] << 8 |
				      f->fs.nat_fip[9] << 16 |
				      f->fs.nat_fip[8] << 24, 1);

			set_tcb_field(adap, f, tid, TCB_RX_FRAG2_PTR_RAW_W + 2,
				      WORD_MASK, f->fs.nat_fip[7] |
				      f->fs.nat_fip[6] << 8 |
				      f->fs.nat_fip[5] << 16 |
				      f->fs.nat_fip[4] << 24, 1);

			set_tcb_field(adap, f, tid, TCB_RX_FRAG2_PTR_RAW_W + 3,
				      WORD_MASK, f->fs.nat_fip[3] |
				      f->fs.nat_fip[2] << 8 |
				      f->fs.nat_fip[1] << 16 |
				      f->fs.nat_fip[0] << 24, 1);

		} else {
			set_tcb_field(adap, f, tid,
				      TCB_RX_FRAG3_START_IDX_OFFSET_RAW_W,
				      WORD_MASK, f->fs.nat_fip[3] |
				      f->fs.nat_fip[2] << 8 |
				      f->fs.nat_fip[1] << 16 |
				      f->fs.nat_fip[0] << 24, 1);
		}
	}

	set_tcb_field(adap, f, tid, TCB_PDU_HDR_LEN_W, WORD_MASK,
		      (dp ? f->fs.nat_lport : 0) |
		      (sp ? f->fs.nat_fport << 16 : 0), 1);
}

/* Validate filter spec against configuration done on the card. */
static int validate_filter(struct net_device *dev,
			   struct ch_filter_specification *fs)
{
	struct adapter *adapter = netdev2adap(dev);
	u32 fconf, iconf;

	/* Check for unconfigured fields being used. */
	fconf = adapter->params.tp.vlan_pri_map;
	iconf = adapter->params.tp.ingress_config;

	if (unsupported(fconf, FCOE_F, fs->val.fcoe, fs->mask.fcoe) ||
	    unsupported(fconf, PORT_F, fs->val.iport, fs->mask.iport) ||
	    unsupported(fconf, TOS_F, fs->val.tos, fs->mask.tos) ||
	    unsupported(fconf, ETHERTYPE_F, fs->val.ethtype,
			fs->mask.ethtype) ||
	    unsupported(fconf, MACMATCH_F, fs->val.macidx, fs->mask.macidx) ||
	    unsupported(fconf, MPSHITTYPE_F, fs->val.matchtype,
			fs->mask.matchtype) ||
	    unsupported(fconf, FRAGMENTATION_F, fs->val.frag, fs->mask.frag) ||
	    unsupported(fconf, PROTOCOL_F, fs->val.proto, fs->mask.proto) ||
	    unsupported(fconf, VNIC_ID_F, fs->val.pfvf_vld,
			fs->mask.pfvf_vld) ||
	    unsupported(fconf, VNIC_ID_F, fs->val.ovlan_vld,
			fs->mask.ovlan_vld) ||
	    unsupported(fconf, VNIC_ID_F, fs->val.encap_vld,
			fs->mask.encap_vld) ||
	    unsupported(fconf, VLAN_F, fs->val.ivlan_vld, fs->mask.ivlan_vld))
		return -EOPNOTSUPP;

	/* T4 inconveniently uses the same FT_VNIC_ID_W bits for both the Outer
	 * VLAN Tag and PF/VF/VFvld fields based on VNIC_F being set
	 * in TP_INGRESS_CONFIG.  Hense the somewhat crazy checks
	 * below.  Additionally, since the T4 firmware interface also
	 * carries that overlap, we need to translate any PF/VF
	 * specification into that internal format below.
	 */
	if ((is_field_set(fs->val.pfvf_vld, fs->mask.pfvf_vld) &&
	     is_field_set(fs->val.ovlan_vld, fs->mask.ovlan_vld)) ||
	    (is_field_set(fs->val.pfvf_vld, fs->mask.pfvf_vld) &&
	     is_field_set(fs->val.encap_vld, fs->mask.encap_vld)) ||
	    (is_field_set(fs->val.ovlan_vld, fs->mask.ovlan_vld) &&
	     is_field_set(fs->val.encap_vld, fs->mask.encap_vld)))
		return -EOPNOTSUPP;
	if (unsupported(iconf, VNIC_F, fs->val.pfvf_vld, fs->mask.pfvf_vld) ||
	    (is_field_set(fs->val.ovlan_vld, fs->mask.ovlan_vld) &&
	     (iconf & VNIC_F)))
		return -EOPNOTSUPP;
	if (fs->val.pf > 0x7 || fs->val.vf > 0x7f)
		return -ERANGE;
	fs->mask.pf &= 0x7;
	fs->mask.vf &= 0x7f;

	/* If the user is requesting that the filter action loop
	 * matching packets back out one of our ports, make sure that
	 * the egress port is in range.
	 */
	if (fs->action == FILTER_SWITCH &&
	    fs->eport >= adapter->params.nports)
		return -ERANGE;

	/* Don't allow various trivially obvious bogus out-of-range values... */
	if (fs->val.iport >= adapter->params.nports)
		return -ERANGE;

	/* T4 doesn't support removing VLAN Tags for loop back filters. */
	if (is_t4(adapter->params.chip) &&
	    fs->action == FILTER_SWITCH &&
	    (fs->newvlan == VLAN_REMOVE ||
	     fs->newvlan == VLAN_REWRITE))
		return -EOPNOTSUPP;

	if (fs->val.encap_vld &&
	    CHELSIO_CHIP_VERSION(adapter->params.chip) < CHELSIO_T6)
		return -EOPNOTSUPP;
	return 0;
}

static int get_filter_steerq(struct net_device *dev,
			     struct ch_filter_specification *fs)
{
	struct adapter *adapter = netdev2adap(dev);
	int iq;

	/* If the user has requested steering matching Ingress Packets
	 * to a specific Queue Set, we need to make sure it's in range
	 * for the port and map that into the Absolute Queue ID of the
	 * Queue Set's Response Queue.
	 */
	if (!fs->dirsteer) {
		if (fs->iq)
			return -EINVAL;
		iq = 0;
	} else {
		struct port_info *pi = netdev_priv(dev);

		/* If the iq id is greater than the number of qsets,
		 * then assume it is an absolute qid.
		 */
		if (fs->iq < pi->nqsets)
			iq = adapter->sge.ethrxq[pi->first_qset +
						 fs->iq].rspq.abs_id;
		else
			iq = fs->iq;
	}

	return iq;
}

static int get_filter_count(struct adapter *adapter, unsigned int fidx,
			    u64 *pkts, u64 *bytes, bool hash)
{
	unsigned int tcb_base, tcbaddr;
	unsigned int word_offset;
	struct filter_entry *f;
	__be64 be64_byte_count;
	int ret;

	tcb_base = t4_read_reg(adapter, TP_CMM_TCB_BASE_A);
	if (is_hashfilter(adapter) && hash) {
		if (fidx < adapter->tids.ntids) {
			f = adapter->tids.tid_tab[fidx];
			if (!f)
				return -EINVAL;
		} else {
			return -E2BIG;
		}
	} else {
		if ((fidx != (adapter->tids.nftids +
			      adapter->tids.nsftids - 1)) &&
		    fidx >= adapter->tids.nftids)
			return -E2BIG;

		f = &adapter->tids.ftid_tab[fidx];
		if (!f->valid)
			return -EINVAL;
	}
	tcbaddr = tcb_base + f->tid * TCB_SIZE;

	spin_lock(&adapter->win0_lock);
	if (is_t4(adapter->params.chip)) {
		__be64 be64_count;

		/* T4 doesn't maintain byte counts in hw */
		*bytes = 0;

		/* Get pkts */
		word_offset = 4;
		ret = t4_memory_rw(adapter, MEMWIN_NIC, MEM_EDC0,
				   tcbaddr + (word_offset * sizeof(__be32)),
				   sizeof(be64_count),
				   (__be32 *)&be64_count,
				   T4_MEMORY_READ);
		if (ret < 0)
			goto out;
		*pkts = be64_to_cpu(be64_count);
	} else {
		__be32 be32_count;

		/* Get bytes */
		word_offset = 4;
		ret = t4_memory_rw(adapter, MEMWIN_NIC, MEM_EDC0,
				   tcbaddr + (word_offset * sizeof(__be32)),
				   sizeof(be64_byte_count),
				   &be64_byte_count,
				   T4_MEMORY_READ);
		if (ret < 0)
			goto out;
		*bytes = be64_to_cpu(be64_byte_count);

		/* Get pkts */
		word_offset = 6;
		ret = t4_memory_rw(adapter, MEMWIN_NIC, MEM_EDC0,
				   tcbaddr + (word_offset * sizeof(__be32)),
				   sizeof(be32_count),
				   &be32_count,
				   T4_MEMORY_READ);
		if (ret < 0)
			goto out;
		*pkts = (u64)be32_to_cpu(be32_count);
	}

out:
	spin_unlock(&adapter->win0_lock);
	return ret;
}

int cxgb4_get_filter_counters(struct net_device *dev, unsigned int fidx,
			      u64 *hitcnt, u64 *bytecnt, bool hash)
{
	struct adapter *adapter = netdev2adap(dev);

	return get_filter_count(adapter, fidx, hitcnt, bytecnt, hash);
}

int cxgb4_get_free_ftid(struct net_device *dev, int family)
{
	struct adapter *adap = netdev2adap(dev);
	struct tid_info *t = &adap->tids;
	int ftid;

	spin_lock_bh(&t->ftid_lock);
	if (family == PF_INET) {
		ftid = find_first_zero_bit(t->ftid_bmap, t->nftids);
		if (ftid >= t->nftids)
			ftid = -1;
	} else {
		if (is_t6(adap->params.chip)) {
			ftid = bitmap_find_free_region(t->ftid_bmap,
						       t->nftids, 1);
			if (ftid < 0)
				goto out_unlock;

			/* this is only a lookup, keep the found region
			 * unallocated
			 */
			bitmap_release_region(t->ftid_bmap, ftid, 1);
		} else {
			ftid = bitmap_find_free_region(t->ftid_bmap,
						       t->nftids, 2);
			if (ftid < 0)
				goto out_unlock;

			bitmap_release_region(t->ftid_bmap, ftid, 2);
		}
	}
out_unlock:
	spin_unlock_bh(&t->ftid_lock);
	return ftid;
}

static int cxgb4_set_ftid(struct tid_info *t, int fidx, int family,
			  unsigned int chip_ver)
{
	spin_lock_bh(&t->ftid_lock);

	if (test_bit(fidx, t->ftid_bmap)) {
		spin_unlock_bh(&t->ftid_lock);
		return -EBUSY;
	}

	if (family == PF_INET) {
		__set_bit(fidx, t->ftid_bmap);
	} else {
		if (chip_ver < CHELSIO_T6)
			bitmap_allocate_region(t->ftid_bmap, fidx, 2);
		else
			bitmap_allocate_region(t->ftid_bmap, fidx, 1);
	}

	spin_unlock_bh(&t->ftid_lock);
	return 0;
}

static void cxgb4_clear_ftid(struct tid_info *t, int fidx, int family,
			     unsigned int chip_ver)
{
	spin_lock_bh(&t->ftid_lock);
	if (family == PF_INET) {
		__clear_bit(fidx, t->ftid_bmap);
	} else {
		if (chip_ver < CHELSIO_T6)
			bitmap_release_region(t->ftid_bmap, fidx, 2);
		else
			bitmap_release_region(t->ftid_bmap, fidx, 1);
	}
	spin_unlock_bh(&t->ftid_lock);
}

/* Delete the filter at a specified index. */
static int del_filter_wr(struct adapter *adapter, int fidx)
{
	struct filter_entry *f = &adapter->tids.ftid_tab[fidx];
	struct fw_filter_wr *fwr;
	struct sk_buff *skb;
	unsigned int len;

	len = sizeof(*fwr);

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	fwr = __skb_put(skb, len);
	t4_mk_filtdelwr(f->tid, fwr, (adapter->flags & SHUTTING_DOWN) ? -1
			: adapter->sge.fw_evtq.abs_id);

	/* Mark the filter as "pending" and ship off the Filter Work Request.
	 * When we get the Work Request Reply we'll clear the pending status.
	 */
	f->pending = 1;
	t4_mgmt_tx(adapter, skb);
	return 0;
}

/* Send a Work Request to write the filter at a specified index.  We construct
 * a Firmware Filter Work Request to have the work done and put the indicated
 * filter into "pending" mode which will prevent any further actions against
 * it till we get a reply from the firmware on the completion status of the
 * request.
 */
int set_filter_wr(struct adapter *adapter, int fidx)
{
	struct filter_entry *f = &adapter->tids.ftid_tab[fidx];
	struct fw_filter2_wr *fwr;
	struct sk_buff *skb;

	skb = alloc_skb(sizeof(*fwr), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	/* If the new filter requires loopback Destination MAC and/or VLAN
	 * rewriting then we need to allocate a Layer 2 Table (L2T) entry for
	 * the filter.
	 */
	if (f->fs.newdmac || f->fs.newvlan) {
		/* allocate L2T entry for new filter */
		f->l2t = t4_l2t_alloc_switching(adapter, f->fs.vlan,
						f->fs.eport, f->fs.dmac);
		if (!f->l2t) {
			kfree_skb(skb);
			return -ENOMEM;
		}
	}

	/* If the new filter requires loopback Source MAC rewriting then
	 * we need to allocate a SMT entry for the filter.
	 */
	if (f->fs.newsmac) {
		f->smt = cxgb4_smt_alloc_switching(f->dev, f->fs.smac);
		if (!f->smt) {
			if (f->l2t) {
				cxgb4_l2t_release(f->l2t);
				f->l2t = NULL;
			}
			kfree_skb(skb);
			return -ENOMEM;
		}
	}

	fwr = __skb_put_zero(skb, sizeof(*fwr));

	/* It would be nice to put most of the following in t4_hw.c but most
	 * of the work is translating the cxgbtool ch_filter_specification
	 * into the Work Request and the definition of that structure is
	 * currently in cxgbtool.h which isn't appropriate to pull into the
	 * common code.  We may eventually try to come up with a more neutral
	 * filter specification structure but for now it's easiest to simply
	 * put this fairly direct code in line ...
	 */
	if (adapter->params.filter2_wr_support)
		fwr->op_pkd = htonl(FW_WR_OP_V(FW_FILTER2_WR));
	else
		fwr->op_pkd = htonl(FW_WR_OP_V(FW_FILTER_WR));
	fwr->len16_pkd = htonl(FW_WR_LEN16_V(sizeof(*fwr) / 16));
	fwr->tid_to_iq =
		htonl(FW_FILTER_WR_TID_V(f->tid) |
		      FW_FILTER_WR_RQTYPE_V(f->fs.type) |
		      FW_FILTER_WR_NOREPLY_V(0) |
		      FW_FILTER_WR_IQ_V(f->fs.iq));
	fwr->del_filter_to_l2tix =
		htonl(FW_FILTER_WR_RPTTID_V(f->fs.rpttid) |
		      FW_FILTER_WR_DROP_V(f->fs.action == FILTER_DROP) |
		      FW_FILTER_WR_DIRSTEER_V(f->fs.dirsteer) |
		      FW_FILTER_WR_MASKHASH_V(f->fs.maskhash) |
		      FW_FILTER_WR_DIRSTEERHASH_V(f->fs.dirsteerhash) |
		      FW_FILTER_WR_LPBK_V(f->fs.action == FILTER_SWITCH) |
		      FW_FILTER_WR_DMAC_V(f->fs.newdmac) |
		      FW_FILTER_WR_SMAC_V(f->fs.newsmac) |
		      FW_FILTER_WR_INSVLAN_V(f->fs.newvlan == VLAN_INSERT ||
					     f->fs.newvlan == VLAN_REWRITE) |
		      FW_FILTER_WR_RMVLAN_V(f->fs.newvlan == VLAN_REMOVE ||
					    f->fs.newvlan == VLAN_REWRITE) |
		      FW_FILTER_WR_HITCNTS_V(f->fs.hitcnts) |
		      FW_FILTER_WR_TXCHAN_V(f->fs.eport) |
		      FW_FILTER_WR_PRIO_V(f->fs.prio) |
		      FW_FILTER_WR_L2TIX_V(f->l2t ? f->l2t->idx : 0));
	fwr->ethtype = htons(f->fs.val.ethtype);
	fwr->ethtypem = htons(f->fs.mask.ethtype);
	fwr->frag_to_ovlan_vldm =
		(FW_FILTER_WR_FRAG_V(f->fs.val.frag) |
		 FW_FILTER_WR_FRAGM_V(f->fs.mask.frag) |
		 FW_FILTER_WR_IVLAN_VLD_V(f->fs.val.ivlan_vld) |
		 FW_FILTER_WR_OVLAN_VLD_V(f->fs.val.ovlan_vld) |
		 FW_FILTER_WR_IVLAN_VLDM_V(f->fs.mask.ivlan_vld) |
		 FW_FILTER_WR_OVLAN_VLDM_V(f->fs.mask.ovlan_vld));
	if (f->fs.newsmac)
		fwr->smac_sel = f->smt->idx;
	fwr->rx_chan_rx_rpl_iq =
		htons(FW_FILTER_WR_RX_CHAN_V(0) |
		      FW_FILTER_WR_RX_RPL_IQ_V(adapter->sge.fw_evtq.abs_id));
	fwr->maci_to_matchtypem =
		htonl(FW_FILTER_WR_MACI_V(f->fs.val.macidx) |
		      FW_FILTER_WR_MACIM_V(f->fs.mask.macidx) |
		      FW_FILTER_WR_FCOE_V(f->fs.val.fcoe) |
		      FW_FILTER_WR_FCOEM_V(f->fs.mask.fcoe) |
		      FW_FILTER_WR_PORT_V(f->fs.val.iport) |
		      FW_FILTER_WR_PORTM_V(f->fs.mask.iport) |
		      FW_FILTER_WR_MATCHTYPE_V(f->fs.val.matchtype) |
		      FW_FILTER_WR_MATCHTYPEM_V(f->fs.mask.matchtype));
	fwr->ptcl = f->fs.val.proto;
	fwr->ptclm = f->fs.mask.proto;
	fwr->ttyp = f->fs.val.tos;
	fwr->ttypm = f->fs.mask.tos;
	fwr->ivlan = htons(f->fs.val.ivlan);
	fwr->ivlanm = htons(f->fs.mask.ivlan);
	fwr->ovlan = htons(f->fs.val.ovlan);
	fwr->ovlanm = htons(f->fs.mask.ovlan);
	memcpy(fwr->lip, f->fs.val.lip, sizeof(fwr->lip));
	memcpy(fwr->lipm, f->fs.mask.lip, sizeof(fwr->lipm));
	memcpy(fwr->fip, f->fs.val.fip, sizeof(fwr->fip));
	memcpy(fwr->fipm, f->fs.mask.fip, sizeof(fwr->fipm));
	fwr->lp = htons(f->fs.val.lport);
	fwr->lpm = htons(f->fs.mask.lport);
	fwr->fp = htons(f->fs.val.fport);
	fwr->fpm = htons(f->fs.mask.fport);

	if (adapter->params.filter2_wr_support) {
		fwr->natmode_to_ulp_type =
			FW_FILTER2_WR_ULP_TYPE_V(f->fs.nat_mode ?
						 ULP_MODE_TCPDDP :
						 ULP_MODE_NONE) |
			FW_FILTER2_WR_NATMODE_V(f->fs.nat_mode);
		memcpy(fwr->newlip, f->fs.nat_lip, sizeof(fwr->newlip));
		memcpy(fwr->newfip, f->fs.nat_fip, sizeof(fwr->newfip));
		fwr->newlport = htons(f->fs.nat_lport);
		fwr->newfport = htons(f->fs.nat_fport);
	}

	/* Mark the filter as "pending" and ship off the Filter Work Request.
	 * When we get the Work Request Reply we'll clear the pending status.
	 */
	f->pending = 1;
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, f->fs.val.iport & 0x3);
	t4_ofld_send(adapter, skb);
	return 0;
}

/* Return an error number if the indicated filter isn't writable ... */
int writable_filter(struct filter_entry *f)
{
	if (f->locked)
		return -EPERM;
	if (f->pending)
		return -EBUSY;

	return 0;
}

/* Delete the filter at the specified index (if valid).  The checks for all
 * the common problems with doing this like the filter being locked, currently
 * pending in another operation, etc.
 */
int delete_filter(struct adapter *adapter, unsigned int fidx)
{
	struct filter_entry *f;
	int ret;

	if (fidx >= adapter->tids.nftids + adapter->tids.nsftids)
		return -EINVAL;

	f = &adapter->tids.ftid_tab[fidx];
	ret = writable_filter(f);
	if (ret)
		return ret;
	if (f->valid)
		return del_filter_wr(adapter, fidx);

	return 0;
}

/* Clear a filter and release any of its resources that we own.  This also
 * clears the filter's "pending" status.
 */
void clear_filter(struct adapter *adap, struct filter_entry *f)
{
	struct port_info *pi = netdev_priv(f->dev);

	/* If the new or old filter have loopback rewriteing rules then we'll
	 * need to free any existing L2T, SMT, CLIP entries of filter
	 * rule.
	 */
	if (f->l2t)
		cxgb4_l2t_release(f->l2t);

	if (f->smt)
		cxgb4_smt_release(f->smt);

	if (f->fs.val.encap_vld && f->fs.val.ovlan_vld)
		if (atomic_dec_and_test(&adap->mps_encap[f->fs.val.ovlan &
							 0x1ff].refcnt))
			t4_free_encap_mac_filt(adap, pi->viid,
					       f->fs.val.ovlan & 0x1ff, 0);

	if ((f->fs.hash || is_t6(adap->params.chip)) && f->fs.type)
		cxgb4_clip_release(f->dev, (const u32 *)&f->fs.val.lip, 1);

	/* The zeroing of the filter rule below clears the filter valid,
	 * pending, locked flags, l2t pointer, etc. so it's all we need for
	 * this operation.
	 */
	memset(f, 0, sizeof(*f));
}

void clear_all_filters(struct adapter *adapter)
{
	unsigned int i;

	if (adapter->tids.ftid_tab) {
		struct filter_entry *f = &adapter->tids.ftid_tab[0];
		unsigned int max_ftid = adapter->tids.nftids +
					adapter->tids.nsftids;

		for (i = 0; i < max_ftid; i++, f++)
			if (f->valid || f->pending)
				clear_filter(adapter, f);
	}
}

/* Fill up default masks for set match fields. */
static void fill_default_mask(struct ch_filter_specification *fs)
{
	unsigned int lip = 0, lip_mask = 0;
	unsigned int fip = 0, fip_mask = 0;
	unsigned int i;

	if (fs->val.iport && !fs->mask.iport)
		fs->mask.iport |= ~0;
	if (fs->val.fcoe && !fs->mask.fcoe)
		fs->mask.fcoe |= ~0;
	if (fs->val.matchtype && !fs->mask.matchtype)
		fs->mask.matchtype |= ~0;
	if (fs->val.macidx && !fs->mask.macidx)
		fs->mask.macidx |= ~0;
	if (fs->val.ethtype && !fs->mask.ethtype)
		fs->mask.ethtype |= ~0;
	if (fs->val.ivlan && !fs->mask.ivlan)
		fs->mask.ivlan |= ~0;
	if (fs->val.ovlan && !fs->mask.ovlan)
		fs->mask.ovlan |= ~0;
	if (fs->val.frag && !fs->mask.frag)
		fs->mask.frag |= ~0;
	if (fs->val.tos && !fs->mask.tos)
		fs->mask.tos |= ~0;
	if (fs->val.proto && !fs->mask.proto)
		fs->mask.proto |= ~0;

	for (i = 0; i < ARRAY_SIZE(fs->val.lip); i++) {
		lip |= fs->val.lip[i];
		lip_mask |= fs->mask.lip[i];
		fip |= fs->val.fip[i];
		fip_mask |= fs->mask.fip[i];
	}

	if (lip && !lip_mask)
		memset(fs->mask.lip, ~0, sizeof(fs->mask.lip));

	if (fip && !fip_mask)
		memset(fs->mask.fip, ~0, sizeof(fs->mask.lip));

	if (fs->val.lport && !fs->mask.lport)
		fs->mask.lport = ~0;
	if (fs->val.fport && !fs->mask.fport)
		fs->mask.fport = ~0;
}

static bool is_addr_all_mask(u8 *ipmask, int family)
{
	if (family == AF_INET) {
		struct in_addr *addr;

		addr = (struct in_addr *)ipmask;
		if (addr->s_addr == htonl(0xffffffff))
			return true;
	} else if (family == AF_INET6) {
		struct in6_addr *addr6;

		addr6 = (struct in6_addr *)ipmask;
		if (addr6->s6_addr32[0] == htonl(0xffffffff) &&
		    addr6->s6_addr32[1] == htonl(0xffffffff) &&
		    addr6->s6_addr32[2] == htonl(0xffffffff) &&
		    addr6->s6_addr32[3] == htonl(0xffffffff))
			return true;
	}
	return false;
}

static bool is_inaddr_any(u8 *ip, int family)
{
	int addr_type;

	if (family == AF_INET) {
		struct in_addr *addr;

		addr = (struct in_addr *)ip;
		if (addr->s_addr == htonl(INADDR_ANY))
			return true;
	} else if (family == AF_INET6) {
		struct in6_addr *addr6;

		addr6 = (struct in6_addr *)ip;
		addr_type = ipv6_addr_type((const struct in6_addr *)
					   &addr6);
		if (addr_type == IPV6_ADDR_ANY)
			return true;
	}
	return false;
}

bool is_filter_exact_match(struct adapter *adap,
			   struct ch_filter_specification *fs)
{
	struct tp_params *tp = &adap->params.tp;
	u64 hash_filter_mask = tp->hash_filter_mask;
	u64 ntuple_mask = 0;

	if (!is_hashfilter(adap))
		return false;

	 /* Keep tunnel VNI match disabled for hash-filters for now */
	if (fs->mask.encap_vld)
		return false;

	if (fs->type) {
		if (is_inaddr_any(fs->val.fip, AF_INET6) ||
		    !is_addr_all_mask(fs->mask.fip, AF_INET6))
			return false;

		if (is_inaddr_any(fs->val.lip, AF_INET6) ||
		    !is_addr_all_mask(fs->mask.lip, AF_INET6))
			return false;
	} else {
		if (is_inaddr_any(fs->val.fip, AF_INET) ||
		    !is_addr_all_mask(fs->mask.fip, AF_INET))
			return false;

		if (is_inaddr_any(fs->val.lip, AF_INET) ||
		    !is_addr_all_mask(fs->mask.lip, AF_INET))
			return false;
	}

	if (!fs->val.lport || fs->mask.lport != 0xffff)
		return false;

	if (!fs->val.fport || fs->mask.fport != 0xffff)
		return false;

	/* calculate tuple mask and compare with mask configured in hw */
	if (tp->fcoe_shift >= 0)
		ntuple_mask |= (u64)fs->mask.fcoe << tp->fcoe_shift;

	if (tp->port_shift >= 0)
		ntuple_mask |= (u64)fs->mask.iport << tp->port_shift;

	if (tp->vnic_shift >= 0) {
		if ((adap->params.tp.ingress_config & VNIC_F))
			ntuple_mask |= (u64)fs->mask.pfvf_vld << tp->vnic_shift;
		else
			ntuple_mask |= (u64)fs->mask.ovlan_vld <<
				tp->vnic_shift;
	}

	if (tp->vlan_shift >= 0)
		ntuple_mask |= (u64)fs->mask.ivlan << tp->vlan_shift;

	if (tp->tos_shift >= 0)
		ntuple_mask |= (u64)fs->mask.tos << tp->tos_shift;

	if (tp->protocol_shift >= 0)
		ntuple_mask |= (u64)fs->mask.proto << tp->protocol_shift;

	if (tp->ethertype_shift >= 0)
		ntuple_mask |= (u64)fs->mask.ethtype << tp->ethertype_shift;

	if (tp->macmatch_shift >= 0)
		ntuple_mask |= (u64)fs->mask.macidx << tp->macmatch_shift;

	if (tp->matchtype_shift >= 0)
		ntuple_mask |= (u64)fs->mask.matchtype << tp->matchtype_shift;

	if (tp->frag_shift >= 0)
		ntuple_mask |= (u64)fs->mask.frag << tp->frag_shift;

	if (ntuple_mask != hash_filter_mask)
		return false;

	return true;
}

static u64 hash_filter_ntuple(struct ch_filter_specification *fs,
			      struct net_device *dev)
{
	struct adapter *adap = netdev2adap(dev);
	struct tp_params *tp = &adap->params.tp;
	u64 ntuple = 0;

	/* Initialize each of the fields which we care about which are present
	 * in the Compressed Filter Tuple.
	 */
	if (tp->vlan_shift >= 0 && fs->mask.ivlan)
		ntuple |= (FT_VLAN_VLD_F | fs->val.ivlan) << tp->vlan_shift;

	if (tp->port_shift >= 0 && fs->mask.iport)
		ntuple |= (u64)fs->val.iport << tp->port_shift;

	if (tp->protocol_shift >= 0) {
		if (!fs->val.proto)
			ntuple |= (u64)IPPROTO_TCP << tp->protocol_shift;
		else
			ntuple |= (u64)fs->val.proto << tp->protocol_shift;
	}

	if (tp->tos_shift >= 0 && fs->mask.tos)
		ntuple |= (u64)(fs->val.tos) << tp->tos_shift;

	if (tp->vnic_shift >= 0) {
		if ((adap->params.tp.ingress_config & USE_ENC_IDX_F) &&
		    fs->mask.encap_vld)
			ntuple |= (u64)((fs->val.encap_vld << 16) |
					(fs->val.ovlan)) << tp->vnic_shift;
		else if ((adap->params.tp.ingress_config & VNIC_F) &&
			 fs->mask.pfvf_vld)
			ntuple |= (u64)((fs->val.pfvf_vld << 16) |
					(fs->val.pf << 13) |
					(fs->val.vf)) << tp->vnic_shift;
		else
			ntuple |= (u64)((fs->val.ovlan_vld << 16) |
					(fs->val.ovlan)) << tp->vnic_shift;
	}

	if (tp->macmatch_shift >= 0 && fs->mask.macidx)
		ntuple |= (u64)(fs->val.macidx) << tp->macmatch_shift;

	if (tp->ethertype_shift >= 0 && fs->mask.ethtype)
		ntuple |= (u64)(fs->val.ethtype) << tp->ethertype_shift;

	if (tp->matchtype_shift >= 0 && fs->mask.matchtype)
		ntuple |= (u64)(fs->val.matchtype) << tp->matchtype_shift;

	if (tp->frag_shift >= 0 && fs->mask.frag)
		ntuple |= (u64)(fs->val.frag) << tp->frag_shift;

	if (tp->fcoe_shift >= 0 && fs->mask.fcoe)
		ntuple |= (u64)(fs->val.fcoe) << tp->fcoe_shift;
	return ntuple;
}

static void mk_act_open_req6(struct filter_entry *f, struct sk_buff *skb,
			     unsigned int qid_filterid, struct adapter *adap)
{
	struct cpl_t6_act_open_req6 *t6req = NULL;
	struct cpl_act_open_req6 *req = NULL;

	t6req = (struct cpl_t6_act_open_req6 *)__skb_put(skb, sizeof(*t6req));
	INIT_TP_WR(t6req, 0);
	req = (struct cpl_act_open_req6 *)t6req;
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ACT_OPEN_REQ6, qid_filterid));
	req->local_port = cpu_to_be16(f->fs.val.lport);
	req->peer_port = cpu_to_be16(f->fs.val.fport);
	req->local_ip_hi = *(__be64 *)(&f->fs.val.lip);
	req->local_ip_lo = *(((__be64 *)&f->fs.val.lip) + 1);
	req->peer_ip_hi = *(__be64 *)(&f->fs.val.fip);
	req->peer_ip_lo = *(((__be64 *)&f->fs.val.fip) + 1);
	req->opt0 = cpu_to_be64(NAGLE_V(f->fs.newvlan == VLAN_REMOVE ||
					f->fs.newvlan == VLAN_REWRITE) |
				DELACK_V(f->fs.hitcnts) |
				L2T_IDX_V(f->l2t ? f->l2t->idx : 0) |
				SMAC_SEL_V((cxgb4_port_viid(f->dev) &
					    0x7F) << 1) |
				TX_CHAN_V(f->fs.eport) |
				NO_CONG_V(f->fs.rpttid) |
				ULP_MODE_V(f->fs.nat_mode ?
					   ULP_MODE_TCPDDP : ULP_MODE_NONE) |
				TCAM_BYPASS_F | NON_OFFLOAD_F);
	t6req->params = cpu_to_be64(FILTER_TUPLE_V(hash_filter_ntuple(&f->fs,
								      f->dev)));
	t6req->opt2 = htonl(RSS_QUEUE_VALID_F |
			    RSS_QUEUE_V(f->fs.iq) |
			    TX_QUEUE_V(f->fs.nat_mode) |
			    T5_OPT_2_VALID_F |
			    RX_CHANNEL_F |
			    PACE_V((f->fs.maskhash) |
				   ((f->fs.dirsteerhash) << 1)));
}

static void mk_act_open_req(struct filter_entry *f, struct sk_buff *skb,
			    unsigned int qid_filterid, struct adapter *adap)
{
	struct cpl_t6_act_open_req *t6req = NULL;
	struct cpl_act_open_req *req = NULL;

	t6req = (struct cpl_t6_act_open_req *)__skb_put(skb, sizeof(*t6req));
	INIT_TP_WR(t6req, 0);
	req = (struct cpl_act_open_req *)t6req;
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ACT_OPEN_REQ, qid_filterid));
	req->local_port = cpu_to_be16(f->fs.val.lport);
	req->peer_port = cpu_to_be16(f->fs.val.fport);
	memcpy(&req->local_ip, f->fs.val.lip, 4);
	memcpy(&req->peer_ip, f->fs.val.fip, 4);
	req->opt0 = cpu_to_be64(NAGLE_V(f->fs.newvlan == VLAN_REMOVE ||
					f->fs.newvlan == VLAN_REWRITE) |
				DELACK_V(f->fs.hitcnts) |
				L2T_IDX_V(f->l2t ? f->l2t->idx : 0) |
				SMAC_SEL_V((cxgb4_port_viid(f->dev) &
					    0x7F) << 1) |
				TX_CHAN_V(f->fs.eport) |
				NO_CONG_V(f->fs.rpttid) |
				ULP_MODE_V(f->fs.nat_mode ?
					   ULP_MODE_TCPDDP : ULP_MODE_NONE) |
				TCAM_BYPASS_F | NON_OFFLOAD_F);

	t6req->params = cpu_to_be64(FILTER_TUPLE_V(hash_filter_ntuple(&f->fs,
								      f->dev)));
	t6req->opt2 = htonl(RSS_QUEUE_VALID_F |
			    RSS_QUEUE_V(f->fs.iq) |
			    TX_QUEUE_V(f->fs.nat_mode) |
			    T5_OPT_2_VALID_F |
			    RX_CHANNEL_F |
			    PACE_V((f->fs.maskhash) |
				   ((f->fs.dirsteerhash) << 1)));
}

static int cxgb4_set_hash_filter(struct net_device *dev,
				 struct ch_filter_specification *fs,
				 struct filter_ctx *ctx)
{
	struct adapter *adapter = netdev2adap(dev);
	struct port_info *pi = netdev_priv(dev);
	struct tid_info *t = &adapter->tids;
	struct filter_entry *f;
	struct sk_buff *skb;
	int iq, atid, size;
	int ret = 0;
	u32 iconf;

	fill_default_mask(fs);
	ret = validate_filter(dev, fs);
	if (ret)
		return ret;

	iq = get_filter_steerq(dev, fs);
	if (iq < 0)
		return iq;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	f->fs = *fs;
	f->ctx = ctx;
	f->dev = dev;
	f->fs.iq = iq;

	/* If the new filter requires loopback Destination MAC and/or VLAN
	 * rewriting then we need to allocate a Layer 2 Table (L2T) entry for
	 * the filter.
	 */
	if (f->fs.newdmac || f->fs.newvlan) {
		/* allocate L2T entry for new filter */
		f->l2t = t4_l2t_alloc_switching(adapter, f->fs.vlan,
						f->fs.eport, f->fs.dmac);
		if (!f->l2t) {
			ret = -ENOMEM;
			goto out_err;
		}
	}

	/* If the new filter requires loopback Source MAC rewriting then
	 * we need to allocate a SMT entry for the filter.
	 */
	if (f->fs.newsmac) {
		f->smt = cxgb4_smt_alloc_switching(f->dev, f->fs.smac);
		if (!f->smt) {
			if (f->l2t) {
				cxgb4_l2t_release(f->l2t);
				f->l2t = NULL;
			}
			ret = -ENOMEM;
			goto free_l2t;
		}
	}

	atid = cxgb4_alloc_atid(t, f);
	if (atid < 0) {
		ret = atid;
		goto free_smt;
	}

	iconf = adapter->params.tp.ingress_config;
	if (iconf & VNIC_F) {
		f->fs.val.ovlan = (fs->val.pf << 13) | fs->val.vf;
		f->fs.mask.ovlan = (fs->mask.pf << 13) | fs->mask.vf;
		f->fs.val.ovlan_vld = fs->val.pfvf_vld;
		f->fs.mask.ovlan_vld = fs->mask.pfvf_vld;
	} else if (iconf & USE_ENC_IDX_F) {
		if (f->fs.val.encap_vld) {
			struct port_info *pi = netdev_priv(f->dev);
			u8 match_all_mac[] = { 0, 0, 0, 0, 0, 0 };

			/* allocate MPS TCAM entry */
			ret = t4_alloc_encap_mac_filt(adapter, pi->viid,
						      match_all_mac,
						      match_all_mac,
						      f->fs.val.vni,
						      f->fs.mask.vni,
						      0, 1, 1);
			if (ret < 0)
				goto free_atid;

			atomic_inc(&adapter->mps_encap[ret].refcnt);
			f->fs.val.ovlan = ret;
			f->fs.mask.ovlan = 0xffff;
			f->fs.val.ovlan_vld = 1;
			f->fs.mask.ovlan_vld = 1;
		}
	}

	size = sizeof(struct cpl_t6_act_open_req);
	if (f->fs.type) {
		ret = cxgb4_clip_get(f->dev, (const u32 *)&f->fs.val.lip, 1);
		if (ret)
			goto free_mps;

		skb = alloc_skb(size, GFP_KERNEL);
		if (!skb) {
			ret = -ENOMEM;
			goto free_clip;
		}

		mk_act_open_req6(f, skb,
				 ((adapter->sge.fw_evtq.abs_id << 14) | atid),
				 adapter);
	} else {
		skb = alloc_skb(size, GFP_KERNEL);
		if (!skb) {
			ret = -ENOMEM;
			goto free_mps;
		}

		mk_act_open_req(f, skb,
				((adapter->sge.fw_evtq.abs_id << 14) | atid),
				adapter);
	}

	f->pending = 1;
	set_wr_txq(skb, CPL_PRIORITY_SETUP, f->fs.val.iport & 0x3);
	t4_ofld_send(adapter, skb);
	return 0;

free_clip:
	cxgb4_clip_release(f->dev, (const u32 *)&f->fs.val.lip, 1);

free_mps:
	if (f->fs.val.encap_vld && f->fs.val.ovlan_vld)
		t4_free_encap_mac_filt(adapter, pi->viid, f->fs.val.ovlan, 1);

free_atid:
	cxgb4_free_atid(t, atid);

free_smt:
	if (f->smt) {
		cxgb4_smt_release(f->smt);
		f->smt = NULL;
	}

free_l2t:
	if (f->l2t) {
		cxgb4_l2t_release(f->l2t);
		f->l2t = NULL;
	}

out_err:
	kfree(f);
	return ret;
}

/* Check a Chelsio Filter Request for validity, convert it into our internal
 * format and send it to the hardware.  Return 0 on success, an error number
 * otherwise.  We attach any provided filter operation context to the internal
 * filter specification in order to facilitate signaling completion of the
 * operation.
 */
int __cxgb4_set_filter(struct net_device *dev, int filter_id,
		       struct ch_filter_specification *fs,
		       struct filter_ctx *ctx)
{
	struct adapter *adapter = netdev2adap(dev);
	unsigned int chip_ver = CHELSIO_CHIP_VERSION(adapter->params.chip);
	unsigned int max_fidx, fidx;
	struct filter_entry *f;
	u32 iconf;
	int iq, ret;

	if (fs->hash) {
		if (is_hashfilter(adapter))
			return cxgb4_set_hash_filter(dev, fs, ctx);
		netdev_err(dev, "%s: Exact-match filters only supported with Hash Filter configuration\n",
			   __func__);
		return -EINVAL;
	}

	max_fidx = adapter->tids.nftids;
	if (filter_id != (max_fidx + adapter->tids.nsftids - 1) &&
	    filter_id >= max_fidx)
		return -E2BIG;

	fill_default_mask(fs);

	ret = validate_filter(dev, fs);
	if (ret)
		return ret;

	iq = get_filter_steerq(dev, fs);
	if (iq < 0)
		return iq;

	/* IPv6 filters occupy four slots and must be aligned on
	 * four-slot boundaries.  IPv4 filters only occupy a single
	 * slot and have no alignment requirements but writing a new
	 * IPv4 filter into the middle of an existing IPv6 filter
	 * requires clearing the old IPv6 filter and hence we prevent
	 * insertion.
	 */
	if (fs->type == 0) { /* IPv4 */
		/* For T6, If our IPv4 filter isn't being written to a
		 * multiple of two filter index and there's an IPv6
		 * filter at the multiple of 2 base slot, then we need
		 * to delete that IPv6 filter ...
		 * For adapters below T6, IPv6 filter occupies 4 entries.
		 * Hence we need to delete the filter in multiple of 4 slot.
		 */
		if (chip_ver < CHELSIO_T6)
			fidx = filter_id & ~0x3;
		else
			fidx = filter_id & ~0x1;

		if (fidx != filter_id &&
		    adapter->tids.ftid_tab[fidx].fs.type) {
			f = &adapter->tids.ftid_tab[fidx];
			if (f->valid) {
				dev_err(adapter->pdev_dev,
					"Invalid location. IPv6 requires 4 slots and is occupying slots %u to %u\n",
					fidx, fidx + 3);
				return -EINVAL;
			}
		}
	} else { /* IPv6 */
		if (chip_ver < CHELSIO_T6) {
			/* Ensure that the IPv6 filter is aligned on a
			 * multiple of 4 boundary.
			 */
			if (filter_id & 0x3) {
				dev_err(adapter->pdev_dev,
					"Invalid location. IPv6 must be aligned on a 4-slot boundary\n");
				return -EINVAL;
			}

			/* Check all except the base overlapping IPv4 filter
			 * slots.
			 */
			for (fidx = filter_id + 1; fidx < filter_id + 4;
			     fidx++) {
				f = &adapter->tids.ftid_tab[fidx];
				if (f->valid) {
					dev_err(adapter->pdev_dev,
						"Invalid location.  IPv6 requires 4 slots and an IPv4 filter exists at %u\n",
						fidx);
					return -EBUSY;
				}
			}
		} else {
			/* For T6, CLIP being enabled, IPv6 filter would occupy
			 * 2 entries.
			 */
			if (filter_id & 0x1)
				return -EINVAL;
			/* Check overlapping IPv4 filter slot */
			fidx = filter_id + 1;
			f = &adapter->tids.ftid_tab[fidx];
			if (f->valid) {
				pr_err("%s: IPv6 filter requires 2 indices. IPv4 filter already present at %d. Please remove IPv4 filter first.\n",
				       __func__, fidx);
				return -EBUSY;
			}
		}
	}

	/* Check to make sure that provided filter index is not
	 * already in use by someone else
	 */
	f = &adapter->tids.ftid_tab[filter_id];
	if (f->valid)
		return -EBUSY;

	fidx = filter_id + adapter->tids.ftid_base;
	ret = cxgb4_set_ftid(&adapter->tids, filter_id,
			     fs->type ? PF_INET6 : PF_INET,
			     chip_ver);
	if (ret)
		return ret;

	/* Check t  make sure the filter requested is writable ... */
	ret = writable_filter(f);
	if (ret) {
		/* Clear the bits we have set above */
		cxgb4_clear_ftid(&adapter->tids, filter_id,
				 fs->type ? PF_INET6 : PF_INET,
				 chip_ver);
		return ret;
	}

	if (is_t6(adapter->params.chip) && fs->type &&
	    ipv6_addr_type((const struct in6_addr *)fs->val.lip) !=
	    IPV6_ADDR_ANY) {
		ret = cxgb4_clip_get(dev, (const u32 *)&fs->val.lip, 1);
		if (ret) {
			cxgb4_clear_ftid(&adapter->tids, filter_id, PF_INET6,
					 chip_ver);
			return ret;
		}
	}

	/* Convert the filter specification into our internal format.
	 * We copy the PF/VF specification into the Outer VLAN field
	 * here so the rest of the code -- including the interface to
	 * the firmware -- doesn't have to constantly do these checks.
	 */
	f->fs = *fs;
	f->fs.iq = iq;
	f->dev = dev;

	iconf = adapter->params.tp.ingress_config;
	if (iconf & VNIC_F) {
		f->fs.val.ovlan = (fs->val.pf << 13) | fs->val.vf;
		f->fs.mask.ovlan = (fs->mask.pf << 13) | fs->mask.vf;
		f->fs.val.ovlan_vld = fs->val.pfvf_vld;
		f->fs.mask.ovlan_vld = fs->mask.pfvf_vld;
	} else if (iconf & USE_ENC_IDX_F) {
		if (f->fs.val.encap_vld) {
			struct port_info *pi = netdev_priv(f->dev);
			u8 match_all_mac[] = { 0, 0, 0, 0, 0, 0 };

			/* allocate MPS TCAM entry */
			ret = t4_alloc_encap_mac_filt(adapter, pi->viid,
						      match_all_mac,
						      match_all_mac,
						      f->fs.val.vni,
						      f->fs.mask.vni,
						      0, 1, 1);
			if (ret < 0)
				goto free_clip;

			atomic_inc(&adapter->mps_encap[ret].refcnt);
			f->fs.val.ovlan = ret;
			f->fs.mask.ovlan = 0x1ff;
			f->fs.val.ovlan_vld = 1;
			f->fs.mask.ovlan_vld = 1;
		}
	}

	/* Attempt to set the filter.  If we don't succeed, we clear
	 * it and return the failure.
	 */
	f->ctx = ctx;
	f->tid = fidx; /* Save the actual tid */
	ret = set_filter_wr(adapter, filter_id);
	if (ret) {
		cxgb4_clear_ftid(&adapter->tids, filter_id,
				 fs->type ? PF_INET6 : PF_INET,
				 chip_ver);
		clear_filter(adapter, f);
	}

	return ret;

free_clip:
	if (is_t6(adapter->params.chip) && f->fs.type)
		cxgb4_clip_release(f->dev, (const u32 *)&f->fs.val.lip, 1);
	cxgb4_clear_ftid(&adapter->tids, filter_id,
			 fs->type ? PF_INET6 : PF_INET, chip_ver);
	return ret;
}

static int cxgb4_del_hash_filter(struct net_device *dev, int filter_id,
				 struct filter_ctx *ctx)
{
	struct adapter *adapter = netdev2adap(dev);
	struct tid_info *t = &adapter->tids;
	struct cpl_abort_req *abort_req;
	struct cpl_abort_rpl *abort_rpl;
	struct cpl_set_tcb_field *req;
	struct ulptx_idata *aligner;
	struct work_request_hdr *wr;
	struct filter_entry *f;
	struct sk_buff *skb;
	unsigned int wrlen;
	int ret;

	netdev_dbg(dev, "%s: filter_id = %d ; nftids = %d\n",
		   __func__, filter_id, adapter->tids.nftids);

	if (filter_id > adapter->tids.ntids)
		return -E2BIG;

	f = lookup_tid(t, filter_id);
	if (!f) {
		netdev_err(dev, "%s: no filter entry for filter_id = %d",
			   __func__, filter_id);
		return -EINVAL;
	}

	ret = writable_filter(f);
	if (ret)
		return ret;

	if (!f->valid)
		return -EINVAL;

	f->ctx = ctx;
	f->pending = 1;
	wrlen = roundup(sizeof(*wr) + (sizeof(*req) + sizeof(*aligner))
			+ sizeof(*abort_req) + sizeof(*abort_rpl), 16);
	skb = alloc_skb(wrlen, GFP_KERNEL);
	if (!skb) {
		netdev_err(dev, "%s: could not allocate skb ..\n", __func__);
		return -ENOMEM;
	}
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, f->fs.val.iport & 0x3);
	req = (struct cpl_set_tcb_field *)__skb_put(skb, wrlen);
	INIT_ULPTX_WR(req, wrlen, 0, 0);
	wr = (struct work_request_hdr *)req;
	wr++;
	req = (struct cpl_set_tcb_field *)wr;
	mk_set_tcb_ulp(f, req, TCB_RSS_INFO_W, TCB_RSS_INFO_V(TCB_RSS_INFO_M),
		       TCB_RSS_INFO_V(adapter->sge.fw_evtq.abs_id), 0, 1);
	aligner = (struct ulptx_idata *)(req + 1);
	abort_req = (struct cpl_abort_req *)(aligner + 1);
	mk_abort_req_ulp(abort_req, f->tid);
	abort_rpl = (struct cpl_abort_rpl *)(abort_req + 1);
	mk_abort_rpl_ulp(abort_rpl, f->tid);
	t4_ofld_send(adapter, skb);
	return 0;
}

/* Check a delete filter request for validity and send it to the hardware.
 * Return 0 on success, an error number otherwise.  We attach any provided
 * filter operation context to the internal filter specification in order to
 * facilitate signaling completion of the operation.
 */
int __cxgb4_del_filter(struct net_device *dev, int filter_id,
		       struct ch_filter_specification *fs,
		       struct filter_ctx *ctx)
{
	struct adapter *adapter = netdev2adap(dev);
	unsigned int chip_ver = CHELSIO_CHIP_VERSION(adapter->params.chip);
	struct filter_entry *f;
	unsigned int max_fidx;
	int ret;

	if (fs && fs->hash) {
		if (is_hashfilter(adapter))
			return cxgb4_del_hash_filter(dev, filter_id, ctx);
		netdev_err(dev, "%s: Exact-match filters only supported with Hash Filter configuration\n",
			   __func__);
		return -EINVAL;
	}

	max_fidx = adapter->tids.nftids;
	if (filter_id != (max_fidx + adapter->tids.nsftids - 1) &&
	    filter_id >= max_fidx)
		return -E2BIG;

	f = &adapter->tids.ftid_tab[filter_id];
	ret = writable_filter(f);
	if (ret)
		return ret;

	if (f->valid) {
		f->ctx = ctx;
		cxgb4_clear_ftid(&adapter->tids, filter_id,
				 f->fs.type ? PF_INET6 : PF_INET,
				 chip_ver);
		return del_filter_wr(adapter, filter_id);
	}

	/* If the caller has passed in a Completion Context then we need to
	 * mark it as a successful completion so they don't stall waiting
	 * for it.
	 */
	if (ctx) {
		ctx->result = 0;
		complete(&ctx->completion);
	}
	return ret;
}

int cxgb4_set_filter(struct net_device *dev, int filter_id,
		     struct ch_filter_specification *fs)
{
	struct filter_ctx ctx;
	int ret;

	init_completion(&ctx.completion);

	ret = __cxgb4_set_filter(dev, filter_id, fs, &ctx);
	if (ret)
		goto out;

	/* Wait for reply */
	ret = wait_for_completion_timeout(&ctx.completion, 10 * HZ);
	if (!ret)
		return -ETIMEDOUT;

	ret = ctx.result;
out:
	return ret;
}

int cxgb4_del_filter(struct net_device *dev, int filter_id,
		     struct ch_filter_specification *fs)
{
	struct filter_ctx ctx;
	int ret;

	/* If we are shutting down the adapter do not wait for completion */
	if (netdev2adap(dev)->flags & SHUTTING_DOWN)
		return __cxgb4_del_filter(dev, filter_id, fs, NULL);

	init_completion(&ctx.completion);

	ret = __cxgb4_del_filter(dev, filter_id, fs, &ctx);
	if (ret)
		goto out;

	/* Wait for reply */
	ret = wait_for_completion_timeout(&ctx.completion, 10 * HZ);
	if (!ret)
		return -ETIMEDOUT;

	ret = ctx.result;
out:
	return ret;
}

static int configure_filter_tcb(struct adapter *adap, unsigned int tid,
				struct filter_entry *f)
{
	if (f->fs.hitcnts) {
		set_tcb_field(adap, f, tid, TCB_TIMESTAMP_W,
			      TCB_TIMESTAMP_V(TCB_TIMESTAMP_M),
			      TCB_TIMESTAMP_V(0ULL),
			      1);
		set_tcb_field(adap, f, tid, TCB_RTT_TS_RECENT_AGE_W,
			      TCB_RTT_TS_RECENT_AGE_V(TCB_RTT_TS_RECENT_AGE_M),
			      TCB_RTT_TS_RECENT_AGE_V(0ULL),
			      1);
	}

	if (f->fs.newdmac)
		set_tcb_tflag(adap, f, tid, TF_CCTRL_ECE_S, 1,
			      1);

	if (f->fs.newvlan == VLAN_INSERT ||
	    f->fs.newvlan == VLAN_REWRITE)
		set_tcb_tflag(adap, f, tid, TF_CCTRL_RFR_S, 1,
			      1);
	if (f->fs.newsmac)
		configure_filter_smac(adap, f);

	if (f->fs.nat_mode) {
		switch (f->fs.nat_mode) {
		case NAT_MODE_DIP:
			set_nat_params(adap, f, tid, true, false, false, false);
			break;

		case NAT_MODE_DIP_DP:
			set_nat_params(adap, f, tid, true, false, true, false);
			break;

		case NAT_MODE_DIP_DP_SIP:
			set_nat_params(adap, f, tid, true, true, true, false);
			break;
		case NAT_MODE_DIP_DP_SP:
			set_nat_params(adap, f, tid, true, false, true, true);
			break;

		case NAT_MODE_SIP_SP:
			set_nat_params(adap, f, tid, false, true, false, true);
			break;

		case NAT_MODE_DIP_SIP_SP:
			set_nat_params(adap, f, tid, true, true, false, true);
			break;

		case NAT_MODE_ALL:
			set_nat_params(adap, f, tid, true, true, true, true);
			break;

		default:
			pr_err("%s: Invalid NAT mode: %d\n",
			       __func__, f->fs.nat_mode);
			return -EINVAL;
		}
	}
	return 0;
}

void hash_del_filter_rpl(struct adapter *adap,
			 const struct cpl_abort_rpl_rss *rpl)
{
	unsigned int status = rpl->status;
	struct tid_info *t = &adap->tids;
	unsigned int tid = GET_TID(rpl);
	struct filter_ctx *ctx = NULL;
	struct filter_entry *f;

	dev_dbg(adap->pdev_dev, "%s: status = %u; tid = %u\n",
		__func__, status, tid);

	f = lookup_tid(t, tid);
	if (!f) {
		dev_err(adap->pdev_dev, "%s:could not find filter entry",
			__func__);
		return;
	}
	ctx = f->ctx;
	f->ctx = NULL;
	clear_filter(adap, f);
	cxgb4_remove_tid(t, 0, tid, 0);
	kfree(f);
	if (ctx) {
		ctx->result = 0;
		complete(&ctx->completion);
	}
}

void hash_filter_rpl(struct adapter *adap, const struct cpl_act_open_rpl *rpl)
{
	unsigned int ftid = TID_TID_G(AOPEN_ATID_G(ntohl(rpl->atid_status)));
	unsigned int status  = AOPEN_STATUS_G(ntohl(rpl->atid_status));
	struct tid_info *t = &adap->tids;
	unsigned int tid = GET_TID(rpl);
	struct filter_ctx *ctx = NULL;
	struct filter_entry *f;

	dev_dbg(adap->pdev_dev, "%s: tid = %u; atid = %u; status = %u\n",
		__func__, tid, ftid, status);

	f = lookup_atid(t, ftid);
	if (!f) {
		dev_err(adap->pdev_dev, "%s:could not find filter entry",
			__func__);
		return;
	}
	ctx = f->ctx;
	f->ctx = NULL;

	switch (status) {
	case CPL_ERR_NONE:
		f->tid = tid;
		f->pending = 0;
		f->valid = 1;
		cxgb4_insert_tid(t, f, f->tid, 0);
		cxgb4_free_atid(t, ftid);
		if (ctx) {
			ctx->tid = f->tid;
			ctx->result = 0;
		}
		if (configure_filter_tcb(adap, tid, f)) {
			clear_filter(adap, f);
			cxgb4_remove_tid(t, 0, tid, 0);
			kfree(f);
			if (ctx) {
				ctx->result = -EINVAL;
				complete(&ctx->completion);
			}
			return;
		}
		switch (f->fs.action) {
		case FILTER_PASS:
			if (f->fs.dirsteer)
				set_tcb_tflag(adap, f, tid,
					      TF_DIRECT_STEER_S, 1, 1);
			break;
		case FILTER_DROP:
			set_tcb_tflag(adap, f, tid, TF_DROP_S, 1, 1);
			break;
		case FILTER_SWITCH:
			set_tcb_tflag(adap, f, tid, TF_LPBK_S, 1, 1);
			break;
		}

		break;

	default:
		dev_err(adap->pdev_dev, "%s: filter creation PROBLEM; status = %u\n",
			__func__, status);

		if (ctx) {
			if (status == CPL_ERR_TCAM_FULL)
				ctx->result = -EAGAIN;
			else
				ctx->result = -EINVAL;
		}
		clear_filter(adap, f);
		cxgb4_free_atid(t, ftid);
		kfree(f);
	}
	if (ctx)
		complete(&ctx->completion);
}

/* Handle a filter write/deletion reply. */
void filter_rpl(struct adapter *adap, const struct cpl_set_tcb_rpl *rpl)
{
	unsigned int tid = GET_TID(rpl);
	struct filter_entry *f = NULL;
	unsigned int max_fidx;
	int idx;

	max_fidx = adap->tids.nftids + adap->tids.nsftids;
	/* Get the corresponding filter entry for this tid */
	if (adap->tids.ftid_tab) {
		/* Check this in normal filter region */
		idx = tid - adap->tids.ftid_base;
		if (idx >= max_fidx)
			return;
		f = &adap->tids.ftid_tab[idx];
		if (f->tid != tid)
			return;
	}

	/* We found the filter entry for this tid */
	if (f) {
		unsigned int ret = TCB_COOKIE_G(rpl->cookie);
		struct filter_ctx *ctx;

		/* Pull off any filter operation context attached to the
		 * filter.
		 */
		ctx = f->ctx;
		f->ctx = NULL;

		if (ret == FW_FILTER_WR_FLT_DELETED) {
			/* Clear the filter when we get confirmation from the
			 * hardware that the filter has been deleted.
			 */
			clear_filter(adap, f);
			if (ctx)
				ctx->result = 0;
		} else if (ret == FW_FILTER_WR_FLT_ADDED) {
			f->pending = 0;  /* async setup completed */
			f->valid = 1;
			if (ctx) {
				ctx->result = 0;
				ctx->tid = idx;
			}
		} else {
			/* Something went wrong.  Issue a warning about the
			 * problem and clear everything out.
			 */
			dev_err(adap->pdev_dev, "filter %u setup failed with error %u\n",
				idx, ret);
			clear_filter(adap, f);
			if (ctx)
				ctx->result = -EINVAL;
		}
		if (ctx)
			complete(&ctx->completion);
	}
}

int init_hash_filter(struct adapter *adap)
{
	/* On T6, verify the necessary register configs and warn the user in
	 * case of improper config
	 */
	if (is_t6(adap->params.chip)) {
		if (TCAM_ACTV_HIT_G(t4_read_reg(adap, LE_DB_RSP_CODE_0_A)) != 4)
			goto err;

		if (HASH_ACTV_HIT_G(t4_read_reg(adap, LE_DB_RSP_CODE_1_A)) != 4)
			goto err;
	} else {
		dev_err(adap->pdev_dev, "Hash filter supported only on T6\n");
		return -EINVAL;
	}
	adap->params.hash_filter = 1;
	return 0;
err:
	dev_warn(adap->pdev_dev, "Invalid hash filter config!\n");
	return -EINVAL;
}
