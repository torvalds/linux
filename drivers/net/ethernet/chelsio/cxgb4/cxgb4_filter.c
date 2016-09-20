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

#include "cxgb4.h"
#include "l2t.h"
#include "t4fw_api.h"
#include "cxgb4_filter.h"

/* Delete the filter at a specified index. */
static int del_filter_wr(struct adapter *adapter, int fidx)
{
	struct filter_entry *f = &adapter->tids.ftid_tab[fidx];
	struct fw_filter_wr *fwr;
	unsigned int len, ftid;
	struct sk_buff *skb;

	len = sizeof(*fwr);
	ftid = adapter->tids.ftid_base + fidx;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	fwr = (struct fw_filter_wr *)__skb_put(skb, len);
	t4_mk_filtdelwr(ftid, fwr, adapter->sge.fw_evtq.abs_id);

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
	struct fw_filter_wr *fwr;
	struct sk_buff *skb;
	unsigned int ftid;

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

	ftid = adapter->tids.ftid_base + fidx;

	fwr = (struct fw_filter_wr *)__skb_put(skb, sizeof(*fwr));
	memset(fwr, 0, sizeof(*fwr));

	/* It would be nice to put most of the following in t4_hw.c but most
	 * of the work is translating the cxgbtool ch_filter_specification
	 * into the Work Request and the definition of that structure is
	 * currently in cxgbtool.h which isn't appropriate to pull into the
	 * common code.  We may eventually try to come up with a more neutral
	 * filter specification structure but for now it's easiest to simply
	 * put this fairly direct code in line ...
	 */
	fwr->op_pkd = htonl(FW_WR_OP_V(FW_FILTER_WR));
	fwr->len16_pkd = htonl(FW_WR_LEN16_V(sizeof(*fwr) / 16));
	fwr->tid_to_iq =
		htonl(FW_FILTER_WR_TID_V(ftid) |
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
	fwr->smac_sel = 0;
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
	if (f->fs.newsmac)
		memcpy(fwr->sma, f->fs.smac, sizeof(fwr->sma));

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
	/* If the new or old filter have loopback rewriteing rules then we'll
	 * need to free any existing Layer Two Table (L2T) entries of the old
	 * filter rule.  The firmware will handle freeing up any Source MAC
	 * Table (SMT) entries used for rewriting Source MAC Addresses in
	 * loopback rules.
	 */
	if (f->l2t)
		cxgb4_l2t_release(f->l2t);

	/* The zeroing of the filter rule below clears the filter valid,
	 * pending, locked flags, l2t pointer, etc. so it's all we need for
	 * this operation.
	 */
	memset(f, 0, sizeof(*f));
}

/* Handle a filter write/deletion reply. */
void filter_rpl(struct adapter *adap, const struct cpl_set_tcb_rpl *rpl)
{
	unsigned int idx = GET_TID(rpl);
	unsigned int nidx = idx - adap->tids.ftid_base;
	struct filter_entry *f;
	unsigned int ret;

	if (idx >= adap->tids.ftid_base && nidx <
	   (adap->tids.nftids + adap->tids.nsftids)) {
		idx = nidx;
		ret = TCB_COOKIE_G(rpl->cookie);
		f = &adap->tids.ftid_tab[idx];

		if (ret == FW_FILTER_WR_FLT_DELETED) {
			/* Clear the filter when we get confirmation from the
			 * hardware that the filter has been deleted.
			 */
			clear_filter(adap, f);
		} else if (ret == FW_FILTER_WR_SMT_TBL_FULL) {
			dev_err(adap->pdev_dev, "filter %u setup failed due to full SMT\n",
				idx);
			clear_filter(adap, f);
		} else if (ret == FW_FILTER_WR_FLT_ADDED) {
			f->smtidx = (be64_to_cpu(rpl->oldval) >> 24) & 0xff;
			f->pending = 0;  /* asynchronous setup completed */
			f->valid = 1;
		} else {
			/* Something went wrong.  Issue a warning about the
			 * problem and clear everything out.
			 */
			dev_err(adap->pdev_dev, "filter %u setup failed with error %u\n",
				idx, ret);
			clear_filter(adap, f);
		}
	}
}
