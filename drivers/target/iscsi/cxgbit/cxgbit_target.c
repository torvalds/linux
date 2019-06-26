/*
 * Copyright (c) 2016 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>

#include <asm/unaligned.h>
#include <net/tcp.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include "cxgbit.h"

struct sge_opaque_hdr {
	void *dev;
	dma_addr_t addr[MAX_SKB_FRAGS + 1];
};

static const u8 cxgbit_digest_len[] = {0, 4, 4, 8};

#define TX_HDR_LEN (sizeof(struct sge_opaque_hdr) + \
		    sizeof(struct fw_ofld_tx_data_wr))

static struct sk_buff *
__cxgbit_alloc_skb(struct cxgbit_sock *csk, u32 len, bool iso)
{
	struct sk_buff *skb = NULL;
	u8 submode = 0;
	int errcode;
	static const u32 hdr_len = TX_HDR_LEN + ISCSI_HDR_LEN;

	if (len) {
		skb = alloc_skb_with_frags(hdr_len, len,
					   0, &errcode,
					   GFP_KERNEL);
		if (!skb)
			return NULL;

		skb_reserve(skb, TX_HDR_LEN);
		skb_reset_transport_header(skb);
		__skb_put(skb, ISCSI_HDR_LEN);
		skb->data_len = len;
		skb->len += len;
		submode |= (csk->submode & CXGBIT_SUBMODE_DCRC);

	} else {
		u32 iso_len = iso ? sizeof(struct cpl_tx_data_iso) : 0;

		skb = alloc_skb(hdr_len + iso_len, GFP_KERNEL);
		if (!skb)
			return NULL;

		skb_reserve(skb, TX_HDR_LEN + iso_len);
		skb_reset_transport_header(skb);
		__skb_put(skb, ISCSI_HDR_LEN);
	}

	submode |= (csk->submode & CXGBIT_SUBMODE_HCRC);
	cxgbit_skcb_submode(skb) = submode;
	cxgbit_skcb_tx_extralen(skb) = cxgbit_digest_len[submode];
	cxgbit_skcb_flags(skb) |= SKCBF_TX_NEED_HDR;
	return skb;
}

static struct sk_buff *cxgbit_alloc_skb(struct cxgbit_sock *csk, u32 len)
{
	return __cxgbit_alloc_skb(csk, len, false);
}

/*
 * cxgbit_is_ofld_imm - check whether a packet can be sent as immediate data
 * @skb: the packet
 *
 * Returns true if a packet can be sent as an offload WR with immediate
 * data.  We currently use the same limit as for Ethernet packets.
 */
static int cxgbit_is_ofld_imm(const struct sk_buff *skb)
{
	int length = skb->len;

	if (likely(cxgbit_skcb_flags(skb) & SKCBF_TX_NEED_HDR))
		length += sizeof(struct fw_ofld_tx_data_wr);

	if (likely(cxgbit_skcb_flags(skb) & SKCBF_TX_ISO))
		length += sizeof(struct cpl_tx_data_iso);

#define MAX_IMM_TX_PKT_LEN	256
	return length <= MAX_IMM_TX_PKT_LEN;
}

/*
 * cxgbit_sgl_len - calculates the size of an SGL of the given capacity
 * @n: the number of SGL entries
 * Calculates the number of flits needed for a scatter/gather list that
 * can hold the given number of entries.
 */
static inline unsigned int cxgbit_sgl_len(unsigned int n)
{
	n--;
	return (3 * n) / 2 + (n & 1) + 2;
}

/*
 * cxgbit_calc_tx_flits_ofld - calculate # of flits for an offload packet
 * @skb: the packet
 *
 * Returns the number of flits needed for the given offload packet.
 * These packets are already fully constructed and no additional headers
 * will be added.
 */
static unsigned int cxgbit_calc_tx_flits_ofld(const struct sk_buff *skb)
{
	unsigned int flits, cnt;

	if (cxgbit_is_ofld_imm(skb))
		return DIV_ROUND_UP(skb->len, 8);
	flits = skb_transport_offset(skb) / 8;
	cnt = skb_shinfo(skb)->nr_frags;
	if (skb_tail_pointer(skb) != skb_transport_header(skb))
		cnt++;
	return flits + cxgbit_sgl_len(cnt);
}

#define CXGBIT_ISO_FSLICE 0x1
#define CXGBIT_ISO_LSLICE 0x2
static void
cxgbit_cpl_tx_data_iso(struct sk_buff *skb, struct cxgbit_iso_info *iso_info)
{
	struct cpl_tx_data_iso *cpl;
	unsigned int submode = cxgbit_skcb_submode(skb);
	unsigned int fslice = !!(iso_info->flags & CXGBIT_ISO_FSLICE);
	unsigned int lslice = !!(iso_info->flags & CXGBIT_ISO_LSLICE);

	cpl = __skb_push(skb, sizeof(*cpl));

	cpl->op_to_scsi = htonl(CPL_TX_DATA_ISO_OP_V(CPL_TX_DATA_ISO) |
			CPL_TX_DATA_ISO_FIRST_V(fslice) |
			CPL_TX_DATA_ISO_LAST_V(lslice) |
			CPL_TX_DATA_ISO_CPLHDRLEN_V(0) |
			CPL_TX_DATA_ISO_HDRCRC_V(submode & 1) |
			CPL_TX_DATA_ISO_PLDCRC_V(((submode >> 1) & 1)) |
			CPL_TX_DATA_ISO_IMMEDIATE_V(0) |
			CPL_TX_DATA_ISO_SCSI_V(2));

	cpl->ahs_len = 0;
	cpl->mpdu = htons(DIV_ROUND_UP(iso_info->mpdu, 4));
	cpl->burst_size = htonl(DIV_ROUND_UP(iso_info->burst_len, 4));
	cpl->len = htonl(iso_info->len);
	cpl->reserved2_seglen_offset = htonl(0);
	cpl->datasn_offset = htonl(0);
	cpl->buffer_offset = htonl(0);
	cpl->reserved3 = 0;

	__skb_pull(skb, sizeof(*cpl));
}

static void
cxgbit_tx_data_wr(struct cxgbit_sock *csk, struct sk_buff *skb, u32 dlen,
		  u32 len, u32 credits, u32 compl)
{
	struct fw_ofld_tx_data_wr *req;
	const struct cxgb4_lld_info *lldi = &csk->com.cdev->lldi;
	u32 submode = cxgbit_skcb_submode(skb);
	u32 wr_ulp_mode = 0;
	u32 hdr_size = sizeof(*req);
	u32 opcode = FW_OFLD_TX_DATA_WR;
	u32 immlen = 0;
	u32 force = is_t5(lldi->adapter_type) ? TX_FORCE_V(!submode) :
		    T6_TX_FORCE_F;

	if (cxgbit_skcb_flags(skb) & SKCBF_TX_ISO) {
		opcode = FW_ISCSI_TX_DATA_WR;
		immlen += sizeof(struct cpl_tx_data_iso);
		hdr_size += sizeof(struct cpl_tx_data_iso);
		submode |= 8;
	}

	if (cxgbit_is_ofld_imm(skb))
		immlen += dlen;

	req = __skb_push(skb, hdr_size);
	req->op_to_immdlen = cpu_to_be32(FW_WR_OP_V(opcode) |
					FW_WR_COMPL_V(compl) |
					FW_WR_IMMDLEN_V(immlen));
	req->flowid_len16 = cpu_to_be32(FW_WR_FLOWID_V(csk->tid) |
					FW_WR_LEN16_V(credits));
	req->plen = htonl(len);
	wr_ulp_mode = FW_OFLD_TX_DATA_WR_ULPMODE_V(ULP_MODE_ISCSI) |
				FW_OFLD_TX_DATA_WR_ULPSUBMODE_V(submode);

	req->tunnel_to_proxy = htonl((wr_ulp_mode) | force |
		 FW_OFLD_TX_DATA_WR_SHOVE_V(skb_peek(&csk->txq) ? 0 : 1));
}

static void cxgbit_arp_failure_skb_discard(void *handle, struct sk_buff *skb)
{
	kfree_skb(skb);
}

void cxgbit_push_tx_frames(struct cxgbit_sock *csk)
{
	struct sk_buff *skb;

	while (csk->wr_cred && ((skb = skb_peek(&csk->txq)) != NULL)) {
		u32 dlen = skb->len;
		u32 len = skb->len;
		u32 credits_needed;
		u32 compl = 0;
		u32 flowclen16 = 0;
		u32 iso_cpl_len = 0;

		if (cxgbit_skcb_flags(skb) & SKCBF_TX_ISO)
			iso_cpl_len = sizeof(struct cpl_tx_data_iso);

		if (cxgbit_is_ofld_imm(skb))
			credits_needed = DIV_ROUND_UP(dlen + iso_cpl_len, 16);
		else
			credits_needed = DIV_ROUND_UP((8 *
					cxgbit_calc_tx_flits_ofld(skb)) +
					iso_cpl_len, 16);

		if (likely(cxgbit_skcb_flags(skb) & SKCBF_TX_NEED_HDR))
			credits_needed += DIV_ROUND_UP(
				sizeof(struct fw_ofld_tx_data_wr), 16);
		/*
		 * Assumes the initial credits is large enough to support
		 * fw_flowc_wr plus largest possible first payload
		 */

		if (!test_and_set_bit(CSK_TX_DATA_SENT, &csk->com.flags)) {
			flowclen16 = cxgbit_send_tx_flowc_wr(csk);
			csk->wr_cred -= flowclen16;
			csk->wr_una_cred += flowclen16;
		}

		if (csk->wr_cred < credits_needed) {
			pr_debug("csk 0x%p, skb %u/%u, wr %d < %u.\n",
				 csk, skb->len, skb->data_len,
				 credits_needed, csk->wr_cred);
			break;
		}
		__skb_unlink(skb, &csk->txq);
		set_wr_txq(skb, CPL_PRIORITY_DATA, csk->txq_idx);
		skb->csum = (__force __wsum)(credits_needed + flowclen16);
		csk->wr_cred -= credits_needed;
		csk->wr_una_cred += credits_needed;

		pr_debug("csk 0x%p, skb %u/%u, wr %d, left %u, unack %u.\n",
			 csk, skb->len, skb->data_len, credits_needed,
			 csk->wr_cred, csk->wr_una_cred);

		if (likely(cxgbit_skcb_flags(skb) & SKCBF_TX_NEED_HDR)) {
			len += cxgbit_skcb_tx_extralen(skb);

			if ((csk->wr_una_cred >= (csk->wr_max_cred / 2)) ||
			    (!before(csk->write_seq,
				     csk->snd_una + csk->snd_win))) {
				compl = 1;
				csk->wr_una_cred = 0;
			}

			cxgbit_tx_data_wr(csk, skb, dlen, len, credits_needed,
					  compl);
			csk->snd_nxt += len;

		} else if ((cxgbit_skcb_flags(skb) & SKCBF_TX_FLAG_COMPL) ||
			   (csk->wr_una_cred >= (csk->wr_max_cred / 2))) {
			struct cpl_close_con_req *req =
				(struct cpl_close_con_req *)skb->data;
			req->wr.wr_hi |= htonl(FW_WR_COMPL_F);
			csk->wr_una_cred = 0;
		}

		cxgbit_sock_enqueue_wr(csk, skb);
		t4_set_arp_err_handler(skb, csk,
				       cxgbit_arp_failure_skb_discard);

		pr_debug("csk 0x%p,%u, skb 0x%p, %u.\n",
			 csk, csk->tid, skb, len);

		cxgbit_l2t_send(csk->com.cdev, skb, csk->l2t);
	}
}

static bool cxgbit_lock_sock(struct cxgbit_sock *csk)
{
	spin_lock_bh(&csk->lock);

	if (before(csk->write_seq, csk->snd_una + csk->snd_win))
		csk->lock_owner = true;

	spin_unlock_bh(&csk->lock);

	return csk->lock_owner;
}

static void cxgbit_unlock_sock(struct cxgbit_sock *csk)
{
	struct sk_buff_head backlogq;
	struct sk_buff *skb;
	void (*fn)(struct cxgbit_sock *, struct sk_buff *);

	skb_queue_head_init(&backlogq);

	spin_lock_bh(&csk->lock);
	while (skb_queue_len(&csk->backlogq)) {
		skb_queue_splice_init(&csk->backlogq, &backlogq);
		spin_unlock_bh(&csk->lock);

		while ((skb = __skb_dequeue(&backlogq))) {
			fn = cxgbit_skcb_rx_backlog_fn(skb);
			fn(csk, skb);
		}

		spin_lock_bh(&csk->lock);
	}

	csk->lock_owner = false;
	spin_unlock_bh(&csk->lock);
}

static int cxgbit_queue_skb(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	int ret = 0;

	wait_event_interruptible(csk->ack_waitq, cxgbit_lock_sock(csk));

	if (unlikely((csk->com.state != CSK_STATE_ESTABLISHED) ||
		     signal_pending(current))) {
		__kfree_skb(skb);
		__skb_queue_purge(&csk->ppodq);
		ret = -1;
		spin_lock_bh(&csk->lock);
		if (csk->lock_owner) {
			spin_unlock_bh(&csk->lock);
			goto unlock;
		}
		spin_unlock_bh(&csk->lock);
		return ret;
	}

	csk->write_seq += skb->len +
			  cxgbit_skcb_tx_extralen(skb);

	skb_queue_splice_tail_init(&csk->ppodq, &csk->txq);
	__skb_queue_tail(&csk->txq, skb);
	cxgbit_push_tx_frames(csk);

unlock:
	cxgbit_unlock_sock(csk);
	return ret;
}

static int
cxgbit_map_skb(struct iscsi_cmd *cmd, struct sk_buff *skb, u32 data_offset,
	       u32 data_length)
{
	u32 i = 0, nr_frags = MAX_SKB_FRAGS;
	u32 padding = ((-data_length) & 3);
	struct scatterlist *sg;
	struct page *page;
	unsigned int page_off;

	if (padding)
		nr_frags--;

	/*
	 * We know each entry in t_data_sg contains a page.
	 */
	sg = &cmd->se_cmd.t_data_sg[data_offset / PAGE_SIZE];
	page_off = (data_offset % PAGE_SIZE);

	while (data_length && (i < nr_frags)) {
		u32 cur_len = min_t(u32, data_length, sg->length - page_off);

		page = sg_page(sg);

		get_page(page);
		skb_fill_page_desc(skb, i, page, sg->offset + page_off,
				   cur_len);
		skb->data_len += cur_len;
		skb->len += cur_len;
		skb->truesize += cur_len;

		data_length -= cur_len;
		page_off = 0;
		sg = sg_next(sg);
		i++;
	}

	if (data_length)
		return -1;

	if (padding) {
		page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!page)
			return -1;
		skb_fill_page_desc(skb, i, page, 0, padding);
		skb->data_len += padding;
		skb->len += padding;
		skb->truesize += padding;
	}

	return 0;
}

static int
cxgbit_tx_datain_iso(struct cxgbit_sock *csk, struct iscsi_cmd *cmd,
		     struct iscsi_datain_req *dr)
{
	struct iscsi_conn *conn = csk->conn;
	struct sk_buff *skb;
	struct iscsi_datain datain;
	struct cxgbit_iso_info iso_info;
	u32 data_length = cmd->se_cmd.data_length;
	u32 mrdsl = conn->conn_ops->MaxRecvDataSegmentLength;
	u32 num_pdu, plen, tx_data = 0;
	bool task_sense = !!(cmd->se_cmd.se_cmd_flags &
		SCF_TRANSPORT_TASK_SENSE);
	bool set_statsn = false;
	int ret = -1;

	while (data_length) {
		num_pdu = (data_length + mrdsl - 1) / mrdsl;
		if (num_pdu > csk->max_iso_npdu)
			num_pdu = csk->max_iso_npdu;

		plen = num_pdu * mrdsl;
		if (plen > data_length)
			plen = data_length;

		skb = __cxgbit_alloc_skb(csk, 0, true);
		if (unlikely(!skb))
			return -ENOMEM;

		memset(skb->data, 0, ISCSI_HDR_LEN);
		cxgbit_skcb_flags(skb) |= SKCBF_TX_ISO;
		cxgbit_skcb_submode(skb) |= (csk->submode &
				CXGBIT_SUBMODE_DCRC);
		cxgbit_skcb_tx_extralen(skb) = (num_pdu *
				cxgbit_digest_len[cxgbit_skcb_submode(skb)]) +
						((num_pdu - 1) * ISCSI_HDR_LEN);

		memset(&datain, 0, sizeof(struct iscsi_datain));
		memset(&iso_info, 0, sizeof(iso_info));

		if (!tx_data)
			iso_info.flags |= CXGBIT_ISO_FSLICE;

		if (!(data_length - plen)) {
			iso_info.flags |= CXGBIT_ISO_LSLICE;
			if (!task_sense) {
				datain.flags = ISCSI_FLAG_DATA_STATUS;
				iscsit_increment_maxcmdsn(cmd, conn->sess);
				cmd->stat_sn = conn->stat_sn++;
				set_statsn = true;
			}
		}

		iso_info.burst_len = num_pdu * mrdsl;
		iso_info.mpdu = mrdsl;
		iso_info.len = ISCSI_HDR_LEN + plen;

		cxgbit_cpl_tx_data_iso(skb, &iso_info);

		datain.offset = tx_data;
		datain.data_sn = cmd->data_sn - 1;

		iscsit_build_datain_pdu(cmd, conn, &datain,
					(struct iscsi_data_rsp *)skb->data,
					set_statsn);

		ret = cxgbit_map_skb(cmd, skb, tx_data, plen);
		if (unlikely(ret)) {
			__kfree_skb(skb);
			goto out;
		}

		ret = cxgbit_queue_skb(csk, skb);
		if (unlikely(ret))
			goto out;

		tx_data += plen;
		data_length -= plen;

		cmd->read_data_done += plen;
		cmd->data_sn += num_pdu;
	}

	dr->dr_complete = DATAIN_COMPLETE_NORMAL;

	return 0;

out:
	return ret;
}

static int
cxgbit_tx_datain(struct cxgbit_sock *csk, struct iscsi_cmd *cmd,
		 const struct iscsi_datain *datain)
{
	struct sk_buff *skb;
	int ret = 0;

	skb = cxgbit_alloc_skb(csk, 0);
	if (unlikely(!skb))
		return -ENOMEM;

	memcpy(skb->data, cmd->pdu, ISCSI_HDR_LEN);

	if (datain->length) {
		cxgbit_skcb_submode(skb) |= (csk->submode &
				CXGBIT_SUBMODE_DCRC);
		cxgbit_skcb_tx_extralen(skb) =
				cxgbit_digest_len[cxgbit_skcb_submode(skb)];
	}

	ret = cxgbit_map_skb(cmd, skb, datain->offset, datain->length);
	if (ret < 0) {
		__kfree_skb(skb);
		return ret;
	}

	return cxgbit_queue_skb(csk, skb);
}

static int
cxgbit_xmit_datain_pdu(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
		       struct iscsi_datain_req *dr,
		       const struct iscsi_datain *datain)
{
	struct cxgbit_sock *csk = conn->context;
	u32 data_length = cmd->se_cmd.data_length;
	u32 padding = ((-data_length) & 3);
	u32 mrdsl = conn->conn_ops->MaxRecvDataSegmentLength;

	if ((data_length > mrdsl) && (!dr->recovery) &&
	    (!padding) && (!datain->offset) && csk->max_iso_npdu) {
		atomic_long_add(data_length - datain->length,
				&conn->sess->tx_data_octets);
		return cxgbit_tx_datain_iso(csk, cmd, dr);
	}

	return cxgbit_tx_datain(csk, cmd, datain);
}

static int
cxgbit_xmit_nondatain_pdu(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
			  const void *data_buf, u32 data_buf_len)
{
	struct cxgbit_sock *csk = conn->context;
	struct sk_buff *skb;
	u32 padding = ((-data_buf_len) & 3);

	skb = cxgbit_alloc_skb(csk, data_buf_len + padding);
	if (unlikely(!skb))
		return -ENOMEM;

	memcpy(skb->data, cmd->pdu, ISCSI_HDR_LEN);

	if (data_buf_len) {
		u32 pad_bytes = 0;

		skb_store_bits(skb, ISCSI_HDR_LEN, data_buf, data_buf_len);

		if (padding)
			skb_store_bits(skb, ISCSI_HDR_LEN + data_buf_len,
				       &pad_bytes, padding);
	}

	cxgbit_skcb_tx_extralen(skb) = cxgbit_digest_len[
				       cxgbit_skcb_submode(skb)];

	return cxgbit_queue_skb(csk, skb);
}

int
cxgbit_xmit_pdu(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
		struct iscsi_datain_req *dr, const void *buf, u32 buf_len)
{
	if (dr)
		return cxgbit_xmit_datain_pdu(conn, cmd, dr, buf);
	else
		return cxgbit_xmit_nondatain_pdu(conn, cmd, buf, buf_len);
}

int cxgbit_validate_params(struct iscsi_conn *conn)
{
	struct cxgbit_sock *csk = conn->context;
	struct cxgbit_device *cdev = csk->com.cdev;
	struct iscsi_param *param;
	u32 max_xmitdsl;

	param = iscsi_find_param_from_key(MAXXMITDATASEGMENTLENGTH,
					  conn->param_list);
	if (!param)
		return -1;

	if (kstrtou32(param->value, 0, &max_xmitdsl) < 0)
		return -1;

	if (max_xmitdsl > cdev->mdsl) {
		if (iscsi_change_param_sprintf(
			conn, "MaxXmitDataSegmentLength=%u", cdev->mdsl))
			return -1;
	}

	return 0;
}

static int cxgbit_set_digest(struct cxgbit_sock *csk)
{
	struct iscsi_conn *conn = csk->conn;
	struct iscsi_param *param;

	param = iscsi_find_param_from_key(HEADERDIGEST, conn->param_list);
	if (!param) {
		pr_err("param not found key %s\n", HEADERDIGEST);
		return -1;
	}

	if (!strcmp(param->value, CRC32C))
		csk->submode |= CXGBIT_SUBMODE_HCRC;

	param = iscsi_find_param_from_key(DATADIGEST, conn->param_list);
	if (!param) {
		csk->submode = 0;
		pr_err("param not found key %s\n", DATADIGEST);
		return -1;
	}

	if (!strcmp(param->value, CRC32C))
		csk->submode |= CXGBIT_SUBMODE_DCRC;

	if (cxgbit_setup_conn_digest(csk)) {
		csk->submode = 0;
		return -1;
	}

	return 0;
}

static int cxgbit_set_iso_npdu(struct cxgbit_sock *csk)
{
	struct iscsi_conn *conn = csk->conn;
	struct iscsi_conn_ops *conn_ops = conn->conn_ops;
	struct iscsi_param *param;
	u32 mrdsl, mbl;
	u32 max_npdu, max_iso_npdu;
	u32 max_iso_payload;

	if (conn->login->leading_connection) {
		param = iscsi_find_param_from_key(MAXBURSTLENGTH,
						  conn->param_list);
		if (!param) {
			pr_err("param not found key %s\n", MAXBURSTLENGTH);
			return -1;
		}

		if (kstrtou32(param->value, 0, &mbl) < 0)
			return -1;
	} else {
		mbl = conn->sess->sess_ops->MaxBurstLength;
	}

	mrdsl = conn_ops->MaxRecvDataSegmentLength;
	max_npdu = mbl / mrdsl;

	max_iso_payload = rounddown(CXGBIT_MAX_ISO_PAYLOAD, csk->emss);

	max_iso_npdu = max_iso_payload /
		       (ISCSI_HDR_LEN + mrdsl +
			cxgbit_digest_len[csk->submode]);

	csk->max_iso_npdu = min(max_npdu, max_iso_npdu);

	if (csk->max_iso_npdu <= 1)
		csk->max_iso_npdu = 0;

	return 0;
}

/*
 * cxgbit_seq_pdu_inorder()
 * @csk: pointer to cxgbit socket structure
 *
 * This function checks whether data sequence and data
 * pdu are in order.
 *
 * Return: returns -1 on error, 0 if data sequence and
 * data pdu are in order, 1 if data sequence or data pdu
 * is not in order.
 */
static int cxgbit_seq_pdu_inorder(struct cxgbit_sock *csk)
{
	struct iscsi_conn *conn = csk->conn;
	struct iscsi_param *param;

	if (conn->login->leading_connection) {
		param = iscsi_find_param_from_key(DATASEQUENCEINORDER,
						  conn->param_list);
		if (!param) {
			pr_err("param not found key %s\n", DATASEQUENCEINORDER);
			return -1;
		}

		if (strcmp(param->value, YES))
			return 1;

		param = iscsi_find_param_from_key(DATAPDUINORDER,
						  conn->param_list);
		if (!param) {
			pr_err("param not found key %s\n", DATAPDUINORDER);
			return -1;
		}

		if (strcmp(param->value, YES))
			return 1;

	} else {
		if (!conn->sess->sess_ops->DataSequenceInOrder)
			return 1;
		if (!conn->sess->sess_ops->DataPDUInOrder)
			return 1;
	}

	return 0;
}

static int cxgbit_set_params(struct iscsi_conn *conn)
{
	struct cxgbit_sock *csk = conn->context;
	struct cxgbit_device *cdev = csk->com.cdev;
	struct cxgbi_ppm *ppm = *csk->com.cdev->lldi.iscsi_ppm;
	struct iscsi_conn_ops *conn_ops = conn->conn_ops;
	struct iscsi_param *param;
	u8 erl;

	if (conn_ops->MaxRecvDataSegmentLength > cdev->mdsl)
		conn_ops->MaxRecvDataSegmentLength = cdev->mdsl;

	if (cxgbit_set_digest(csk))
		return -1;

	if (conn->login->leading_connection) {
		param = iscsi_find_param_from_key(ERRORRECOVERYLEVEL,
						  conn->param_list);
		if (!param) {
			pr_err("param not found key %s\n", ERRORRECOVERYLEVEL);
			return -1;
		}
		if (kstrtou8(param->value, 0, &erl) < 0)
			return -1;
	} else {
		erl = conn->sess->sess_ops->ErrorRecoveryLevel;
	}

	if (!erl) {
		int ret;

		ret = cxgbit_seq_pdu_inorder(csk);
		if (ret < 0) {
			return -1;
		} else if (ret > 0) {
			if (is_t5(cdev->lldi.adapter_type))
				goto enable_ddp;
			else
				return 0;
		}

		if (test_bit(CDEV_ISO_ENABLE, &cdev->flags)) {
			if (cxgbit_set_iso_npdu(csk))
				return -1;
		}

enable_ddp:
		if (test_bit(CDEV_DDP_ENABLE, &cdev->flags)) {
			if (cxgbit_setup_conn_pgidx(csk,
						    ppm->tformat.pgsz_idx_dflt))
				return -1;
			set_bit(CSK_DDP_ENABLE, &csk->com.flags);
		}
	}

	return 0;
}

int
cxgbit_put_login_tx(struct iscsi_conn *conn, struct iscsi_login *login,
		    u32 length)
{
	struct cxgbit_sock *csk = conn->context;
	struct sk_buff *skb;
	u32 padding_buf = 0;
	u8 padding = ((-length) & 3);

	skb = cxgbit_alloc_skb(csk, length + padding);
	if (!skb)
		return -ENOMEM;
	skb_store_bits(skb, 0, login->rsp, ISCSI_HDR_LEN);
	skb_store_bits(skb, ISCSI_HDR_LEN, login->rsp_buf, length);

	if (padding)
		skb_store_bits(skb, ISCSI_HDR_LEN + length,
			       &padding_buf, padding);

	if (login->login_complete) {
		if (cxgbit_set_params(conn)) {
			kfree_skb(skb);
			return -1;
		}

		set_bit(CSK_LOGIN_DONE, &csk->com.flags);
	}

	if (cxgbit_queue_skb(csk, skb))
		return -1;

	if ((!login->login_complete) && (!login->login_failed))
		schedule_delayed_work(&conn->login_work, 0);

	return 0;
}

static void
cxgbit_skb_copy_to_sg(struct sk_buff *skb, struct scatterlist *sg,
		      unsigned int nents, u32 skip)
{
	struct skb_seq_state st;
	const u8 *buf;
	unsigned int consumed = 0, buf_len;
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_rx_pdu_cb(skb);

	skb_prepare_seq_read(skb, pdu_cb->doffset,
			     pdu_cb->doffset + pdu_cb->dlen,
			     &st);

	while (true) {
		buf_len = skb_seq_read(consumed, &buf, &st);
		if (!buf_len) {
			skb_abort_seq_read(&st);
			break;
		}

		consumed += sg_pcopy_from_buffer(sg, nents, (void *)buf,
						 buf_len, skip + consumed);
	}
}

static struct iscsi_cmd *cxgbit_allocate_cmd(struct cxgbit_sock *csk)
{
	struct iscsi_conn *conn = csk->conn;
	struct cxgbi_ppm *ppm = cdev2ppm(csk->com.cdev);
	struct cxgbit_cmd *ccmd;
	struct iscsi_cmd *cmd;

	cmd = iscsit_allocate_cmd(conn, TASK_INTERRUPTIBLE);
	if (!cmd) {
		pr_err("Unable to allocate iscsi_cmd + cxgbit_cmd\n");
		return NULL;
	}

	ccmd = iscsit_priv_cmd(cmd);
	ccmd->ttinfo.tag = ppm->tformat.no_ddp_mask;
	ccmd->setup_ddp = true;

	return cmd;
}

static int
cxgbit_handle_immediate_data(struct iscsi_cmd *cmd, struct iscsi_scsi_req *hdr,
			     u32 length)
{
	struct iscsi_conn *conn = cmd->conn;
	struct cxgbit_sock *csk = conn->context;
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_rx_pdu_cb(csk->skb);

	if (pdu_cb->flags & PDUCBF_RX_DCRC_ERR) {
		pr_err("ImmediateData CRC32C DataDigest error\n");
		if (!conn->sess->sess_ops->ErrorRecoveryLevel) {
			pr_err("Unable to recover from"
			       " Immediate Data digest failure while"
			       " in ERL=0.\n");
			iscsit_reject_cmd(cmd, ISCSI_REASON_DATA_DIGEST_ERROR,
					  (unsigned char *)hdr);
			return IMMEDIATE_DATA_CANNOT_RECOVER;
		}

		iscsit_reject_cmd(cmd, ISCSI_REASON_DATA_DIGEST_ERROR,
				  (unsigned char *)hdr);
		return IMMEDIATE_DATA_ERL1_CRC_FAILURE;
	}

	if (cmd->se_cmd.se_cmd_flags & SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC) {
		struct cxgbit_cmd *ccmd = iscsit_priv_cmd(cmd);
		struct skb_shared_info *ssi = skb_shinfo(csk->skb);
		skb_frag_t *dfrag = &ssi->frags[pdu_cb->dfrag_idx];

		sg_init_table(&ccmd->sg, 1);
		sg_set_page(&ccmd->sg, dfrag->page.p, skb_frag_size(dfrag),
			    dfrag->page_offset);
		get_page(dfrag->page.p);

		cmd->se_cmd.t_data_sg = &ccmd->sg;
		cmd->se_cmd.t_data_nents = 1;

		ccmd->release = true;
	} else {
		struct scatterlist *sg = &cmd->se_cmd.t_data_sg[0];
		u32 sg_nents = max(1UL, DIV_ROUND_UP(pdu_cb->dlen, PAGE_SIZE));

		cxgbit_skb_copy_to_sg(csk->skb, sg, sg_nents, 0);
	}

	cmd->write_data_done += pdu_cb->dlen;

	if (cmd->write_data_done == cmd->se_cmd.data_length) {
		spin_lock_bh(&cmd->istate_lock);
		cmd->cmd_flags |= ICF_GOT_LAST_DATAOUT;
		cmd->i_state = ISTATE_RECEIVED_LAST_DATAOUT;
		spin_unlock_bh(&cmd->istate_lock);
	}

	return IMMEDIATE_DATA_NORMAL_OPERATION;
}

static int
cxgbit_get_immediate_data(struct iscsi_cmd *cmd, struct iscsi_scsi_req *hdr,
			  bool dump_payload)
{
	struct iscsi_conn *conn = cmd->conn;
	int cmdsn_ret = 0, immed_ret = IMMEDIATE_DATA_NORMAL_OPERATION;
	/*
	 * Special case for Unsupported SAM WRITE Opcodes and ImmediateData=Yes.
	 */
	if (dump_payload)
		goto after_immediate_data;

	immed_ret = cxgbit_handle_immediate_data(cmd, hdr,
						 cmd->first_burst_len);
after_immediate_data:
	if (immed_ret == IMMEDIATE_DATA_NORMAL_OPERATION) {
		/*
		 * A PDU/CmdSN carrying Immediate Data passed
		 * DataCRC, check against ExpCmdSN/MaxCmdSN if
		 * Immediate Bit is not set.
		 */
		cmdsn_ret = iscsit_sequence_cmd(conn, cmd,
						(unsigned char *)hdr,
						hdr->cmdsn);
		if (cmdsn_ret == CMDSN_ERROR_CANNOT_RECOVER)
			return -1;

		if (cmd->sense_reason || cmdsn_ret == CMDSN_LOWER_THAN_EXP) {
			target_put_sess_cmd(&cmd->se_cmd);
			return 0;
		} else if (cmd->unsolicited_data) {
			iscsit_set_unsolicited_dataout(cmd);
		}

	} else if (immed_ret == IMMEDIATE_DATA_ERL1_CRC_FAILURE) {
		/*
		 * Immediate Data failed DataCRC and ERL>=1,
		 * silently drop this PDU and let the initiator
		 * plug the CmdSN gap.
		 *
		 * FIXME: Send Unsolicited NOPIN with reserved
		 * TTT here to help the initiator figure out
		 * the missing CmdSN, although they should be
		 * intelligent enough to determine the missing
		 * CmdSN and issue a retry to plug the sequence.
		 */
		cmd->i_state = ISTATE_REMOVE;
		iscsit_add_cmd_to_immediate_queue(cmd, conn, cmd->i_state);
	} else /* immed_ret == IMMEDIATE_DATA_CANNOT_RECOVER */
		return -1;

	return 0;
}

static int
cxgbit_handle_scsi_cmd(struct cxgbit_sock *csk, struct iscsi_cmd *cmd)
{
	struct iscsi_conn *conn = csk->conn;
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_rx_pdu_cb(csk->skb);
	struct iscsi_scsi_req *hdr = (struct iscsi_scsi_req *)pdu_cb->hdr;
	int rc;
	bool dump_payload = false;

	rc = iscsit_setup_scsi_cmd(conn, cmd, (unsigned char *)hdr);
	if (rc < 0)
		return rc;

	if (pdu_cb->dlen && (pdu_cb->dlen == cmd->se_cmd.data_length) &&
	    (pdu_cb->nr_dfrags == 1))
		cmd->se_cmd.se_cmd_flags |= SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC;

	rc = iscsit_process_scsi_cmd(conn, cmd, hdr);
	if (rc < 0)
		return 0;
	else if (rc > 0)
		dump_payload = true;

	if (!pdu_cb->dlen)
		return 0;

	return cxgbit_get_immediate_data(cmd, hdr, dump_payload);
}

static int cxgbit_handle_iscsi_dataout(struct cxgbit_sock *csk)
{
	struct scatterlist *sg_start;
	struct iscsi_conn *conn = csk->conn;
	struct iscsi_cmd *cmd = NULL;
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_rx_pdu_cb(csk->skb);
	struct iscsi_data *hdr = (struct iscsi_data *)pdu_cb->hdr;
	u32 data_offset = be32_to_cpu(hdr->offset);
	u32 data_len = pdu_cb->dlen;
	int rc, sg_nents, sg_off;
	bool dcrc_err = false;

	if (pdu_cb->flags & PDUCBF_RX_DDP_CMP) {
		u32 offset = be32_to_cpu(hdr->offset);
		u32 ddp_data_len;
		u32 payload_length = ntoh24(hdr->dlength);
		bool success = false;

		cmd = iscsit_find_cmd_from_itt_or_dump(conn, hdr->itt, 0);
		if (!cmd)
			return 0;

		ddp_data_len = offset - cmd->write_data_done;
		atomic_long_add(ddp_data_len, &conn->sess->rx_data_octets);

		cmd->write_data_done = offset;
		cmd->next_burst_len = ddp_data_len;
		cmd->data_sn = be32_to_cpu(hdr->datasn);

		rc = __iscsit_check_dataout_hdr(conn, (unsigned char *)hdr,
						cmd, payload_length, &success);
		if (rc < 0)
			return rc;
		else if (!success)
			return 0;
	} else {
		rc = iscsit_check_dataout_hdr(conn, (unsigned char *)hdr, &cmd);
		if (rc < 0)
			return rc;
		else if (!cmd)
			return 0;
	}

	if (pdu_cb->flags & PDUCBF_RX_DCRC_ERR) {
		pr_err("ITT: 0x%08x, Offset: %u, Length: %u,"
		       " DataSN: 0x%08x\n",
		       hdr->itt, hdr->offset, data_len,
		       hdr->datasn);

		dcrc_err = true;
		goto check_payload;
	}

	pr_debug("DataOut data_len: %u, "
		"write_data_done: %u, data_length: %u\n",
		  data_len,  cmd->write_data_done,
		  cmd->se_cmd.data_length);

	if (!(pdu_cb->flags & PDUCBF_RX_DATA_DDPD)) {
		u32 skip = data_offset % PAGE_SIZE;

		sg_off = data_offset / PAGE_SIZE;
		sg_start = &cmd->se_cmd.t_data_sg[sg_off];
		sg_nents = max(1UL, DIV_ROUND_UP(skip + data_len, PAGE_SIZE));

		cxgbit_skb_copy_to_sg(csk->skb, sg_start, sg_nents, skip);
	}

check_payload:

	rc = iscsit_check_dataout_payload(cmd, hdr, dcrc_err);
	if (rc < 0)
		return rc;

	return 0;
}

static int cxgbit_handle_nop_out(struct cxgbit_sock *csk, struct iscsi_cmd *cmd)
{
	struct iscsi_conn *conn = csk->conn;
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_rx_pdu_cb(csk->skb);
	struct iscsi_nopout *hdr = (struct iscsi_nopout *)pdu_cb->hdr;
	unsigned char *ping_data = NULL;
	u32 payload_length = pdu_cb->dlen;
	int ret;

	ret = iscsit_setup_nop_out(conn, cmd, hdr);
	if (ret < 0)
		return 0;

	if (pdu_cb->flags & PDUCBF_RX_DCRC_ERR) {
		if (!conn->sess->sess_ops->ErrorRecoveryLevel) {
			pr_err("Unable to recover from"
			       " NOPOUT Ping DataCRC failure while in"
			       " ERL=0.\n");
			ret = -1;
			goto out;
		} else {
			/*
			 * drop this PDU and let the
			 * initiator plug the CmdSN gap.
			 */
			pr_info("Dropping NOPOUT"
				" Command CmdSN: 0x%08x due to"
				" DataCRC error.\n", hdr->cmdsn);
			ret = 0;
			goto out;
		}
	}

	/*
	 * Handle NOP-OUT payload for traditional iSCSI sockets
	 */
	if (payload_length && hdr->ttt == cpu_to_be32(0xFFFFFFFF)) {
		ping_data = kzalloc(payload_length + 1, GFP_KERNEL);
		if (!ping_data) {
			pr_err("Unable to allocate memory for"
				" NOPOUT ping data.\n");
			ret = -1;
			goto out;
		}

		skb_copy_bits(csk->skb, pdu_cb->doffset,
			      ping_data, payload_length);

		ping_data[payload_length] = '\0';
		/*
		 * Attach ping data to struct iscsi_cmd->buf_ptr.
		 */
		cmd->buf_ptr = ping_data;
		cmd->buf_ptr_size = payload_length;

		pr_debug("Got %u bytes of NOPOUT ping"
			" data.\n", payload_length);
		pr_debug("Ping Data: \"%s\"\n", ping_data);
	}

	return iscsit_process_nop_out(conn, cmd, hdr);
out:
	if (cmd)
		iscsit_free_cmd(cmd, false);
	return ret;
}

static int
cxgbit_handle_text_cmd(struct cxgbit_sock *csk, struct iscsi_cmd *cmd)
{
	struct iscsi_conn *conn = csk->conn;
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_rx_pdu_cb(csk->skb);
	struct iscsi_text *hdr = (struct iscsi_text *)pdu_cb->hdr;
	u32 payload_length = pdu_cb->dlen;
	int rc;
	unsigned char *text_in = NULL;

	rc = iscsit_setup_text_cmd(conn, cmd, hdr);
	if (rc < 0)
		return rc;

	if (pdu_cb->flags & PDUCBF_RX_DCRC_ERR) {
		if (!conn->sess->sess_ops->ErrorRecoveryLevel) {
			pr_err("Unable to recover from"
			       " Text Data digest failure while in"
			       " ERL=0.\n");
			goto reject;
		} else {
			/*
			 * drop this PDU and let the
			 * initiator plug the CmdSN gap.
			 */
			pr_info("Dropping Text"
				" Command CmdSN: 0x%08x due to"
				" DataCRC error.\n", hdr->cmdsn);
			return 0;
		}
	}

	if (payload_length) {
		text_in = kzalloc(payload_length, GFP_KERNEL);
		if (!text_in) {
			pr_err("Unable to allocate text_in of payload_length: %u\n",
			       payload_length);
			return -ENOMEM;
		}
		skb_copy_bits(csk->skb, pdu_cb->doffset,
			      text_in, payload_length);

		text_in[payload_length - 1] = '\0';

		cmd->text_in_ptr = text_in;
	}

	return iscsit_process_text_cmd(conn, cmd, hdr);

reject:
	return iscsit_reject_cmd(cmd, ISCSI_REASON_PROTOCOL_ERROR,
				 pdu_cb->hdr);
}

static int cxgbit_target_rx_opcode(struct cxgbit_sock *csk)
{
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_rx_pdu_cb(csk->skb);
	struct iscsi_hdr *hdr = (struct iscsi_hdr *)pdu_cb->hdr;
	struct iscsi_conn *conn = csk->conn;
	struct iscsi_cmd *cmd = NULL;
	u8 opcode = (hdr->opcode & ISCSI_OPCODE_MASK);
	int ret = -EINVAL;

	switch (opcode) {
	case ISCSI_OP_SCSI_CMD:
		cmd = cxgbit_allocate_cmd(csk);
		if (!cmd)
			goto reject;

		ret = cxgbit_handle_scsi_cmd(csk, cmd);
		break;
	case ISCSI_OP_SCSI_DATA_OUT:
		ret = cxgbit_handle_iscsi_dataout(csk);
		break;
	case ISCSI_OP_NOOP_OUT:
		if (hdr->ttt == cpu_to_be32(0xFFFFFFFF)) {
			cmd = cxgbit_allocate_cmd(csk);
			if (!cmd)
				goto reject;
		}

		ret = cxgbit_handle_nop_out(csk, cmd);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		cmd = cxgbit_allocate_cmd(csk);
		if (!cmd)
			goto reject;

		ret = iscsit_handle_task_mgt_cmd(conn, cmd,
						 (unsigned char *)hdr);
		break;
	case ISCSI_OP_TEXT:
		if (hdr->ttt != cpu_to_be32(0xFFFFFFFF)) {
			cmd = iscsit_find_cmd_from_itt(conn, hdr->itt);
			if (!cmd)
				goto reject;
		} else {
			cmd = cxgbit_allocate_cmd(csk);
			if (!cmd)
				goto reject;
		}

		ret = cxgbit_handle_text_cmd(csk, cmd);
		break;
	case ISCSI_OP_LOGOUT:
		cmd = cxgbit_allocate_cmd(csk);
		if (!cmd)
			goto reject;

		ret = iscsit_handle_logout_cmd(conn, cmd, (unsigned char *)hdr);
		if (ret > 0)
			wait_for_completion_timeout(&conn->conn_logout_comp,
						    SECONDS_FOR_LOGOUT_COMP
						    * HZ);
		break;
	case ISCSI_OP_SNACK:
		ret = iscsit_handle_snack(conn, (unsigned char *)hdr);
		break;
	default:
		pr_err("Got unknown iSCSI OpCode: 0x%02x\n", opcode);
		dump_stack();
		break;
	}

	return ret;

reject:
	return iscsit_add_reject(conn, ISCSI_REASON_BOOKMARK_NO_RESOURCES,
				 (unsigned char *)hdr);
	return ret;
}

static int cxgbit_rx_opcode(struct cxgbit_sock *csk)
{
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_rx_pdu_cb(csk->skb);
	struct iscsi_conn *conn = csk->conn;
	struct iscsi_hdr *hdr = pdu_cb->hdr;
	u8 opcode;

	if (pdu_cb->flags & PDUCBF_RX_HCRC_ERR) {
		atomic_long_inc(&conn->sess->conn_digest_errors);
		goto transport_err;
	}

	if (conn->conn_state == TARG_CONN_STATE_IN_LOGOUT)
		goto transport_err;

	opcode = hdr->opcode & ISCSI_OPCODE_MASK;

	if (conn->sess->sess_ops->SessionType &&
	    ((!(opcode & ISCSI_OP_TEXT)) ||
	     (!(opcode & ISCSI_OP_LOGOUT)))) {
		pr_err("Received illegal iSCSI Opcode: 0x%02x"
			" while in Discovery Session, rejecting.\n", opcode);
		iscsit_add_reject(conn, ISCSI_REASON_PROTOCOL_ERROR,
				  (unsigned char *)hdr);
		goto transport_err;
	}

	if (cxgbit_target_rx_opcode(csk) < 0)
		goto transport_err;

	return 0;

transport_err:
	return -1;
}

static int cxgbit_rx_login_pdu(struct cxgbit_sock *csk)
{
	struct iscsi_conn *conn = csk->conn;
	struct iscsi_login *login = conn->login;
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_rx_pdu_cb(csk->skb);
	struct iscsi_login_req *login_req;

	login_req = (struct iscsi_login_req *)login->req;
	memcpy(login_req, pdu_cb->hdr, sizeof(*login_req));

	pr_debug("Got Login Command, Flags 0x%02x, ITT: 0x%08x,"
		" CmdSN: 0x%08x, ExpStatSN: 0x%08x, CID: %hu, Length: %u\n",
		login_req->flags, login_req->itt, login_req->cmdsn,
		login_req->exp_statsn, login_req->cid, pdu_cb->dlen);
	/*
	 * Setup the initial iscsi_login values from the leading
	 * login request PDU.
	 */
	if (login->first_request) {
		login_req = (struct iscsi_login_req *)login->req;
		login->leading_connection = (!login_req->tsih) ? 1 : 0;
		login->current_stage	= ISCSI_LOGIN_CURRENT_STAGE(
				login_req->flags);
		login->version_min	= login_req->min_version;
		login->version_max	= login_req->max_version;
		memcpy(login->isid, login_req->isid, 6);
		login->cmd_sn		= be32_to_cpu(login_req->cmdsn);
		login->init_task_tag	= login_req->itt;
		login->initial_exp_statsn = be32_to_cpu(login_req->exp_statsn);
		login->cid		= be16_to_cpu(login_req->cid);
		login->tsih		= be16_to_cpu(login_req->tsih);
	}

	if (iscsi_target_check_login_request(conn, login) < 0)
		return -1;

	memset(login->req_buf, 0, MAX_KEY_VALUE_PAIRS);
	skb_copy_bits(csk->skb, pdu_cb->doffset, login->req_buf, pdu_cb->dlen);

	return 0;
}

static int
cxgbit_process_iscsi_pdu(struct cxgbit_sock *csk, struct sk_buff *skb, int idx)
{
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_skb_lro_pdu_cb(skb, idx);
	int ret;

	cxgbit_rx_pdu_cb(skb) = pdu_cb;

	csk->skb = skb;

	if (!test_bit(CSK_LOGIN_DONE, &csk->com.flags)) {
		ret = cxgbit_rx_login_pdu(csk);
		set_bit(CSK_LOGIN_PDU_DONE, &csk->com.flags);
	} else {
		ret = cxgbit_rx_opcode(csk);
	}

	return ret;
}

static void cxgbit_lro_skb_dump(struct sk_buff *skb)
{
	struct skb_shared_info *ssi = skb_shinfo(skb);
	struct cxgbit_lro_cb *lro_cb = cxgbit_skb_lro_cb(skb);
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_skb_lro_pdu_cb(skb, 0);
	u8 i;

	pr_info("skb 0x%p, head 0x%p, 0x%p, len %u,%u, frags %u.\n",
		skb, skb->head, skb->data, skb->len, skb->data_len,
		ssi->nr_frags);
	pr_info("skb 0x%p, lro_cb, csk 0x%p, pdu %u, %u.\n",
		skb, lro_cb->csk, lro_cb->pdu_idx, lro_cb->pdu_totallen);

	for (i = 0; i < lro_cb->pdu_idx; i++, pdu_cb++)
		pr_info("skb 0x%p, pdu %d, %u, f 0x%x, seq 0x%x, dcrc 0x%x, "
			"frags %u.\n",
			skb, i, pdu_cb->pdulen, pdu_cb->flags, pdu_cb->seq,
			pdu_cb->ddigest, pdu_cb->frags);
	for (i = 0; i < ssi->nr_frags; i++)
		pr_info("skb 0x%p, frag %d, off %u, sz %u.\n",
			skb, i, ssi->frags[i].page_offset, ssi->frags[i].size);
}

static void cxgbit_lro_hskb_reset(struct cxgbit_sock *csk)
{
	struct sk_buff *skb = csk->lro_hskb;
	struct skb_shared_info *ssi = skb_shinfo(skb);
	u8 i;

	memset(skb->data, 0, LRO_SKB_MIN_HEADROOM);
	for (i = 0; i < ssi->nr_frags; i++)
		put_page(skb_frag_page(&ssi->frags[i]));
	ssi->nr_frags = 0;
	skb->data_len = 0;
	skb->truesize -= skb->len;
	skb->len = 0;
}

static void
cxgbit_lro_skb_merge(struct cxgbit_sock *csk, struct sk_buff *skb, u8 pdu_idx)
{
	struct sk_buff *hskb = csk->lro_hskb;
	struct cxgbit_lro_pdu_cb *hpdu_cb = cxgbit_skb_lro_pdu_cb(hskb, 0);
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_skb_lro_pdu_cb(skb, pdu_idx);
	struct skb_shared_info *hssi = skb_shinfo(hskb);
	struct skb_shared_info *ssi = skb_shinfo(skb);
	unsigned int len = 0;

	if (pdu_cb->flags & PDUCBF_RX_HDR) {
		u8 hfrag_idx = hssi->nr_frags;

		hpdu_cb->flags |= pdu_cb->flags;
		hpdu_cb->seq = pdu_cb->seq;
		hpdu_cb->hdr = pdu_cb->hdr;
		hpdu_cb->hlen = pdu_cb->hlen;

		memcpy(&hssi->frags[hfrag_idx], &ssi->frags[pdu_cb->hfrag_idx],
		       sizeof(skb_frag_t));

		get_page(skb_frag_page(&hssi->frags[hfrag_idx]));
		hssi->nr_frags++;
		hpdu_cb->frags++;
		hpdu_cb->hfrag_idx = hfrag_idx;

		len = hssi->frags[hfrag_idx].size;
		hskb->len += len;
		hskb->data_len += len;
		hskb->truesize += len;
	}

	if (pdu_cb->flags & PDUCBF_RX_DATA) {
		u8 dfrag_idx = hssi->nr_frags, i;

		hpdu_cb->flags |= pdu_cb->flags;
		hpdu_cb->dfrag_idx = dfrag_idx;

		len = 0;
		for (i = 0; i < pdu_cb->nr_dfrags; dfrag_idx++, i++) {
			memcpy(&hssi->frags[dfrag_idx],
			       &ssi->frags[pdu_cb->dfrag_idx + i],
			       sizeof(skb_frag_t));

			get_page(skb_frag_page(&hssi->frags[dfrag_idx]));

			len += hssi->frags[dfrag_idx].size;

			hssi->nr_frags++;
			hpdu_cb->frags++;
		}

		hpdu_cb->dlen = pdu_cb->dlen;
		hpdu_cb->doffset = hpdu_cb->hlen;
		hpdu_cb->nr_dfrags = pdu_cb->nr_dfrags;
		hskb->len += len;
		hskb->data_len += len;
		hskb->truesize += len;
	}

	if (pdu_cb->flags & PDUCBF_RX_STATUS) {
		hpdu_cb->flags |= pdu_cb->flags;

		if (hpdu_cb->flags & PDUCBF_RX_DATA)
			hpdu_cb->flags &= ~PDUCBF_RX_DATA_DDPD;

		hpdu_cb->ddigest = pdu_cb->ddigest;
		hpdu_cb->pdulen = pdu_cb->pdulen;
	}
}

static int cxgbit_process_lro_skb(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	struct cxgbit_lro_cb *lro_cb = cxgbit_skb_lro_cb(skb);
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_skb_lro_pdu_cb(skb, 0);
	u8 pdu_idx = 0, last_idx = 0;
	int ret = 0;

	if (!pdu_cb->complete) {
		cxgbit_lro_skb_merge(csk, skb, 0);

		if (pdu_cb->flags & PDUCBF_RX_STATUS) {
			struct sk_buff *hskb = csk->lro_hskb;

			ret = cxgbit_process_iscsi_pdu(csk, hskb, 0);

			cxgbit_lro_hskb_reset(csk);

			if (ret < 0)
				goto out;
		}

		pdu_idx = 1;
	}

	if (lro_cb->pdu_idx)
		last_idx = lro_cb->pdu_idx - 1;

	for (; pdu_idx <= last_idx; pdu_idx++) {
		ret = cxgbit_process_iscsi_pdu(csk, skb, pdu_idx);
		if (ret < 0)
			goto out;
	}

	if ((!lro_cb->complete) && lro_cb->pdu_idx)
		cxgbit_lro_skb_merge(csk, skb, lro_cb->pdu_idx);

out:
	return ret;
}

static int cxgbit_rx_lro_skb(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	struct cxgbit_lro_cb *lro_cb = cxgbit_skb_lro_cb(skb);
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_skb_lro_pdu_cb(skb, 0);
	int ret = -1;

	if ((pdu_cb->flags & PDUCBF_RX_HDR) &&
	    (pdu_cb->seq != csk->rcv_nxt)) {
		pr_info("csk 0x%p, tid 0x%x, seq 0x%x != 0x%x.\n",
			csk, csk->tid, pdu_cb->seq, csk->rcv_nxt);
		cxgbit_lro_skb_dump(skb);
		return ret;
	}

	csk->rcv_nxt += lro_cb->pdu_totallen;

	ret = cxgbit_process_lro_skb(csk, skb);

	csk->rx_credits += lro_cb->pdu_totallen;

	if (csk->rx_credits >= (csk->rcv_win / 4))
		cxgbit_rx_data_ack(csk);

	return ret;
}

static int cxgbit_rx_skb(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	struct cxgb4_lld_info *lldi = &csk->com.cdev->lldi;
	int ret = -1;

	if (likely(cxgbit_skcb_flags(skb) & SKCBF_RX_LRO)) {
		if (is_t5(lldi->adapter_type))
			ret = cxgbit_rx_lro_skb(csk, skb);
		else
			ret = cxgbit_process_lro_skb(csk, skb);
	}

	__kfree_skb(skb);
	return ret;
}

static bool cxgbit_rxq_len(struct cxgbit_sock *csk, struct sk_buff_head *rxq)
{
	spin_lock_bh(&csk->rxq.lock);
	if (skb_queue_len(&csk->rxq)) {
		skb_queue_splice_init(&csk->rxq, rxq);
		spin_unlock_bh(&csk->rxq.lock);
		return true;
	}
	spin_unlock_bh(&csk->rxq.lock);
	return false;
}

static int cxgbit_wait_rxq(struct cxgbit_sock *csk)
{
	struct sk_buff *skb;
	struct sk_buff_head rxq;

	skb_queue_head_init(&rxq);

	wait_event_interruptible(csk->waitq, cxgbit_rxq_len(csk, &rxq));

	if (signal_pending(current))
		goto out;

	while ((skb = __skb_dequeue(&rxq))) {
		if (cxgbit_rx_skb(csk, skb))
			goto out;
	}

	return 0;
out:
	__skb_queue_purge(&rxq);
	return -1;
}

int cxgbit_get_login_rx(struct iscsi_conn *conn, struct iscsi_login *login)
{
	struct cxgbit_sock *csk = conn->context;
	int ret = -1;

	while (!test_and_clear_bit(CSK_LOGIN_PDU_DONE, &csk->com.flags)) {
		ret = cxgbit_wait_rxq(csk);
		if (ret) {
			clear_bit(CSK_LOGIN_PDU_DONE, &csk->com.flags);
			break;
		}
	}

	return ret;
}

void cxgbit_get_rx_pdu(struct iscsi_conn *conn)
{
	struct cxgbit_sock *csk = conn->context;

	while (!kthread_should_stop()) {
		iscsit_thread_check_cpumask(conn, current, 0);
		if (cxgbit_wait_rxq(csk))
			return;
	}
}
