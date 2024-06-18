// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018 Chelsio Communications, Inc.
 *
 * Written by: Atul Gupta (atul.gupta@chelsio.com)
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sched/signal.h>
#include <net/tcp.h>
#include <net/busy_poll.h>
#include <crypto/aes.h>

#include "chtls.h"
#include "chtls_cm.h"

static bool is_tls_tx(struct chtls_sock *csk)
{
	return csk->tlshws.txkey >= 0;
}

static bool is_tls_rx(struct chtls_sock *csk)
{
	return csk->tlshws.rxkey >= 0;
}

static int data_sgl_len(const struct sk_buff *skb)
{
	unsigned int cnt;

	cnt = skb_shinfo(skb)->nr_frags;
	return sgl_len(cnt) * 8;
}

static int nos_ivs(struct sock *sk, unsigned int size)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);

	return DIV_ROUND_UP(size, csk->tlshws.mfs);
}

static int set_ivs_imm(struct sock *sk, const struct sk_buff *skb)
{
	int ivs_size = nos_ivs(sk, skb->len) * CIPHER_BLOCK_SIZE;
	int hlen = TLS_WR_CPL_LEN + data_sgl_len(skb);

	if ((hlen + KEY_ON_MEM_SZ + ivs_size) <
	    MAX_IMM_OFLD_TX_DATA_WR_LEN) {
		ULP_SKB_CB(skb)->ulp.tls.iv = 1;
		return 1;
	}
	ULP_SKB_CB(skb)->ulp.tls.iv = 0;
	return 0;
}

static int max_ivs_size(struct sock *sk, int size)
{
	return nos_ivs(sk, size) * CIPHER_BLOCK_SIZE;
}

static int ivs_size(struct sock *sk, const struct sk_buff *skb)
{
	return set_ivs_imm(sk, skb) ? (nos_ivs(sk, skb->len) *
		 CIPHER_BLOCK_SIZE) : 0;
}

static int flowc_wr_credits(int nparams, int *flowclenp)
{
	int flowclen16, flowclen;

	flowclen = offsetof(struct fw_flowc_wr, mnemval[nparams]);
	flowclen16 = DIV_ROUND_UP(flowclen, 16);
	flowclen = flowclen16 * 16;

	if (flowclenp)
		*flowclenp = flowclen;

	return flowclen16;
}

static struct sk_buff *create_flowc_wr_skb(struct sock *sk,
					   struct fw_flowc_wr *flowc,
					   int flowclen)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct sk_buff *skb;

	skb = alloc_skb(flowclen, GFP_ATOMIC);
	if (!skb)
		return NULL;

	__skb_put_data(skb, flowc, flowclen);
	skb_set_queue_mapping(skb, (csk->txq_idx << 1) | CPL_PRIORITY_DATA);

	return skb;
}

static int send_flowc_wr(struct sock *sk, struct fw_flowc_wr *flowc,
			 int flowclen)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;
	int flowclen16;
	int ret;

	flowclen16 = flowclen / 16;

	if (csk_flag(sk, CSK_TX_DATA_SENT)) {
		skb = create_flowc_wr_skb(sk, flowc, flowclen);
		if (!skb)
			return -ENOMEM;

		skb_entail(sk, skb,
			   ULPCB_FLAG_NO_HDR | ULPCB_FLAG_NO_APPEND);
		return 0;
	}

	ret = cxgb4_immdata_send(csk->egress_dev,
				 csk->txq_idx,
				 flowc, flowclen);
	if (!ret)
		return flowclen16;
	skb = create_flowc_wr_skb(sk, flowc, flowclen);
	if (!skb)
		return -ENOMEM;
	send_or_defer(sk, tp, skb, 0);
	return flowclen16;
}

static u8 tcp_state_to_flowc_state(u8 state)
{
	switch (state) {
	case TCP_ESTABLISHED:
		return FW_FLOWC_MNEM_TCPSTATE_ESTABLISHED;
	case TCP_CLOSE_WAIT:
		return FW_FLOWC_MNEM_TCPSTATE_CLOSEWAIT;
	case TCP_FIN_WAIT1:
		return FW_FLOWC_MNEM_TCPSTATE_FINWAIT1;
	case TCP_CLOSING:
		return FW_FLOWC_MNEM_TCPSTATE_CLOSING;
	case TCP_LAST_ACK:
		return FW_FLOWC_MNEM_TCPSTATE_LASTACK;
	case TCP_FIN_WAIT2:
		return FW_FLOWC_MNEM_TCPSTATE_FINWAIT2;
	}

	return FW_FLOWC_MNEM_TCPSTATE_ESTABLISHED;
}

int send_tx_flowc_wr(struct sock *sk, int compl,
		     u32 snd_nxt, u32 rcv_nxt)
{
	struct flowc_packed {
		struct fw_flowc_wr fc;
		struct fw_flowc_mnemval mnemval[FW_FLOWC_MNEM_MAX];
	} __packed sflowc;
	int nparams, paramidx, flowclen16, flowclen;
	struct fw_flowc_wr *flowc;
	struct chtls_sock *csk;
	struct tcp_sock *tp;

	csk = rcu_dereference_sk_user_data(sk);
	tp = tcp_sk(sk);
	memset(&sflowc, 0, sizeof(sflowc));
	flowc = &sflowc.fc;

#define FLOWC_PARAM(__m, __v) \
	do { \
		flowc->mnemval[paramidx].mnemonic = FW_FLOWC_MNEM_##__m; \
		flowc->mnemval[paramidx].val = cpu_to_be32(__v); \
		paramidx++; \
	} while (0)

	paramidx = 0;

	FLOWC_PARAM(PFNVFN, FW_PFVF_CMD_PFN_V(csk->cdev->lldi->pf));
	FLOWC_PARAM(CH, csk->tx_chan);
	FLOWC_PARAM(PORT, csk->tx_chan);
	FLOWC_PARAM(IQID, csk->rss_qid);
	FLOWC_PARAM(SNDNXT, tp->snd_nxt);
	FLOWC_PARAM(RCVNXT, tp->rcv_nxt);
	FLOWC_PARAM(SNDBUF, csk->sndbuf);
	FLOWC_PARAM(MSS, tp->mss_cache);
	FLOWC_PARAM(TCPSTATE, tcp_state_to_flowc_state(sk->sk_state));

	if (SND_WSCALE(tp))
		FLOWC_PARAM(RCV_SCALE, SND_WSCALE(tp));

	if (csk->ulp_mode == ULP_MODE_TLS)
		FLOWC_PARAM(ULD_MODE, ULP_MODE_TLS);

	if (csk->tlshws.fcplenmax)
		FLOWC_PARAM(TXDATAPLEN_MAX, csk->tlshws.fcplenmax);

	nparams = paramidx;
#undef FLOWC_PARAM

	flowclen16 = flowc_wr_credits(nparams, &flowclen);
	flowc->op_to_nparams =
		cpu_to_be32(FW_WR_OP_V(FW_FLOWC_WR) |
			    FW_WR_COMPL_V(compl) |
			    FW_FLOWC_WR_NPARAMS_V(nparams));
	flowc->flowid_len16 = cpu_to_be32(FW_WR_LEN16_V(flowclen16) |
					  FW_WR_FLOWID_V(csk->tid));

	return send_flowc_wr(sk, flowc, flowclen);
}

/* Copy IVs to WR */
static int tls_copy_ivs(struct sock *sk, struct sk_buff *skb)

{
	struct chtls_sock *csk;
	unsigned char *iv_loc;
	struct chtls_hws *hws;
	unsigned char *ivs;
	u16 number_of_ivs;
	struct page *page;
	int err = 0;

	csk = rcu_dereference_sk_user_data(sk);
	hws = &csk->tlshws;
	number_of_ivs = nos_ivs(sk, skb->len);

	if (number_of_ivs > MAX_IVS_PAGE) {
		pr_warn("MAX IVs in PAGE exceeded %d\n", number_of_ivs);
		return -ENOMEM;
	}

	/* generate the  IVs */
	ivs = kmalloc_array(CIPHER_BLOCK_SIZE, number_of_ivs, GFP_ATOMIC);
	if (!ivs)
		return -ENOMEM;
	get_random_bytes(ivs, number_of_ivs * CIPHER_BLOCK_SIZE);

	if (skb_ulp_tls_iv_imm(skb)) {
		/* send the IVs as immediate data in the WR */
		iv_loc = (unsigned char *)__skb_push(skb, number_of_ivs *
						CIPHER_BLOCK_SIZE);
		if (iv_loc)
			memcpy(iv_loc, ivs, number_of_ivs * CIPHER_BLOCK_SIZE);

		hws->ivsize = number_of_ivs * CIPHER_BLOCK_SIZE;
	} else {
		/* Send the IVs as sgls */
		/* Already accounted IV DSGL for credits */
		skb_shinfo(skb)->nr_frags--;
		page = alloc_pages(sk->sk_allocation | __GFP_COMP, 0);
		if (!page) {
			pr_info("%s : Page allocation for IVs failed\n",
				__func__);
			err = -ENOMEM;
			goto out;
		}
		memcpy(page_address(page), ivs, number_of_ivs *
		       CIPHER_BLOCK_SIZE);
		skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags, page, 0,
				   number_of_ivs * CIPHER_BLOCK_SIZE);
		hws->ivsize = 0;
	}
out:
	kfree(ivs);
	return err;
}

/* Copy Key to WR */
static void tls_copy_tx_key(struct sock *sk, struct sk_buff *skb)
{
	struct ulptx_sc_memrd *sc_memrd;
	struct chtls_sock *csk;
	struct chtls_dev *cdev;
	struct ulptx_idata *sc;
	struct chtls_hws *hws;
	u32 immdlen;
	int kaddr;

	csk = rcu_dereference_sk_user_data(sk);
	hws = &csk->tlshws;
	cdev = csk->cdev;

	immdlen = sizeof(*sc) + sizeof(*sc_memrd);
	kaddr = keyid_to_addr(cdev->kmap.start, hws->txkey);
	sc = (struct ulptx_idata *)__skb_push(skb, immdlen);
	if (sc) {
		sc->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_NOOP));
		sc->len = htonl(0);
		sc_memrd = (struct ulptx_sc_memrd *)(sc + 1);
		sc_memrd->cmd_to_len =
				htonl(ULPTX_CMD_V(ULP_TX_SC_MEMRD) |
				ULP_TX_SC_MORE_V(1) |
				ULPTX_LEN16_V(hws->keylen >> 4));
		sc_memrd->addr = htonl(kaddr);
	}
}

static u64 tlstx_incr_seqnum(struct chtls_hws *hws)
{
	return hws->tx_seq_no++;
}

static bool is_sg_request(const struct sk_buff *skb)
{
	return skb->peeked ||
		(skb->len > MAX_IMM_ULPTX_WR_LEN);
}

/*
 * Returns true if an sk_buff carries urgent data.
 */
static bool skb_urgent(struct sk_buff *skb)
{
	return ULP_SKB_CB(skb)->flags & ULPCB_FLAG_URG;
}

/* TLS content type for CPL SFO */
static unsigned char tls_content_type(unsigned char content_type)
{
	switch (content_type) {
	case TLS_HDR_TYPE_CCS:
		return CPL_TX_TLS_SFO_TYPE_CCS;
	case TLS_HDR_TYPE_ALERT:
		return CPL_TX_TLS_SFO_TYPE_ALERT;
	case TLS_HDR_TYPE_HANDSHAKE:
		return CPL_TX_TLS_SFO_TYPE_HANDSHAKE;
	case TLS_HDR_TYPE_HEARTBEAT:
		return CPL_TX_TLS_SFO_TYPE_HEARTBEAT;
	}
	return CPL_TX_TLS_SFO_TYPE_DATA;
}

static void tls_tx_data_wr(struct sock *sk, struct sk_buff *skb,
			   int dlen, int tls_immd, u32 credits,
			   int expn, int pdus)
{
	struct fw_tlstx_data_wr *req_wr;
	struct cpl_tx_tls_sfo *req_cpl;
	unsigned int wr_ulp_mode_force;
	struct tls_scmd *updated_scmd;
	unsigned char data_type;
	struct chtls_sock *csk;
	struct net_device *dev;
	struct chtls_hws *hws;
	struct tls_scmd *scmd;
	struct adapter *adap;
	unsigned char *req;
	int immd_len;
	int iv_imm;
	int len;

	csk = rcu_dereference_sk_user_data(sk);
	iv_imm = skb_ulp_tls_iv_imm(skb);
	dev = csk->egress_dev;
	adap = netdev2adap(dev);
	hws = &csk->tlshws;
	scmd = &hws->scmd;
	len = dlen + expn;

	dlen = (dlen < hws->mfs) ? dlen : hws->mfs;
	atomic_inc(&adap->chcr_stats.tls_pdu_tx);

	updated_scmd = scmd;
	updated_scmd->seqno_numivs &= 0xffffff80;
	updated_scmd->seqno_numivs |= SCMD_NUM_IVS_V(pdus);
	hws->scmd = *updated_scmd;

	req = (unsigned char *)__skb_push(skb, sizeof(struct cpl_tx_tls_sfo));
	req_cpl = (struct cpl_tx_tls_sfo *)req;
	req = (unsigned char *)__skb_push(skb, (sizeof(struct
				fw_tlstx_data_wr)));

	req_wr = (struct fw_tlstx_data_wr *)req;
	immd_len = (tls_immd ? dlen : 0);
	req_wr->op_to_immdlen =
		htonl(FW_WR_OP_V(FW_TLSTX_DATA_WR) |
		FW_TLSTX_DATA_WR_COMPL_V(1) |
		FW_TLSTX_DATA_WR_IMMDLEN_V(immd_len));
	req_wr->flowid_len16 = htonl(FW_TLSTX_DATA_WR_FLOWID_V(csk->tid) |
				     FW_TLSTX_DATA_WR_LEN16_V(credits));
	wr_ulp_mode_force = TX_ULP_MODE_V(ULP_MODE_TLS);

	if (is_sg_request(skb))
		wr_ulp_mode_force |= FW_OFLD_TX_DATA_WR_ALIGNPLD_F |
			((tcp_sk(sk)->nonagle & TCP_NAGLE_OFF) ? 0 :
			FW_OFLD_TX_DATA_WR_SHOVE_F);

	req_wr->lsodisable_to_flags =
			htonl(TX_ULP_MODE_V(ULP_MODE_TLS) |
			      TX_URG_V(skb_urgent(skb)) |
			      T6_TX_FORCE_F | wr_ulp_mode_force |
			      TX_SHOVE_V((!csk_flag(sk, CSK_TX_MORE_DATA)) &&
					 skb_queue_empty(&csk->txq)));

	req_wr->ctxloc_to_exp =
			htonl(FW_TLSTX_DATA_WR_NUMIVS_V(pdus) |
			      FW_TLSTX_DATA_WR_EXP_V(expn) |
			      FW_TLSTX_DATA_WR_CTXLOC_V(CHTLS_KEY_CONTEXT_DDR) |
			      FW_TLSTX_DATA_WR_IVDSGL_V(!iv_imm) |
			      FW_TLSTX_DATA_WR_KEYSIZE_V(hws->keylen >> 4));

	/* Fill in the length */
	req_wr->plen = htonl(len);
	req_wr->mfs = htons(hws->mfs);
	req_wr->adjustedplen_pkd =
		htons(FW_TLSTX_DATA_WR_ADJUSTEDPLEN_V(hws->adjustlen));
	req_wr->expinplenmax_pkd =
		htons(FW_TLSTX_DATA_WR_EXPINPLENMAX_V(hws->expansion));
	req_wr->pdusinplenmax_pkd =
		FW_TLSTX_DATA_WR_PDUSINPLENMAX_V(hws->pdus);
	req_wr->r10 = 0;

	data_type = tls_content_type(ULP_SKB_CB(skb)->ulp.tls.type);
	req_cpl->op_to_seg_len = htonl(CPL_TX_TLS_SFO_OPCODE_V(CPL_TX_TLS_SFO) |
				       CPL_TX_TLS_SFO_DATA_TYPE_V(data_type) |
				       CPL_TX_TLS_SFO_CPL_LEN_V(2) |
				       CPL_TX_TLS_SFO_SEG_LEN_V(dlen));
	req_cpl->pld_len = htonl(len - expn);

	req_cpl->type_protover = htonl(CPL_TX_TLS_SFO_TYPE_V
		((data_type == CPL_TX_TLS_SFO_TYPE_HEARTBEAT) ?
		TLS_HDR_TYPE_HEARTBEAT : 0) |
		CPL_TX_TLS_SFO_PROTOVER_V(0));

	/* create the s-command */
	req_cpl->r1_lo = 0;
	req_cpl->seqno_numivs  = cpu_to_be32(hws->scmd.seqno_numivs);
	req_cpl->ivgen_hdrlen = cpu_to_be32(hws->scmd.ivgen_hdrlen);
	req_cpl->scmd1 = cpu_to_be64(tlstx_incr_seqnum(hws));
}

/*
 * Calculate the TLS data expansion size
 */
static int chtls_expansion_size(struct sock *sk, int data_len,
				int fullpdu,
				unsigned short *pducnt)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct chtls_hws *hws = &csk->tlshws;
	struct tls_scmd *scmd = &hws->scmd;
	int fragsize = hws->mfs;
	int expnsize = 0;
	int fragleft;
	int fragcnt;
	int expppdu;

	if (SCMD_CIPH_MODE_G(scmd->seqno_numivs) ==
	    SCMD_CIPH_MODE_AES_GCM) {
		expppdu = GCM_TAG_SIZE + AEAD_EXPLICIT_DATA_SIZE +
			  TLS_HEADER_LENGTH;

		if (fullpdu) {
			*pducnt = data_len / (expppdu + fragsize);
			if (*pducnt > 32)
				*pducnt = 32;
			else if (!*pducnt)
				*pducnt = 1;
			expnsize = (*pducnt) * expppdu;
			return expnsize;
		}
		fragcnt = (data_len / fragsize);
		expnsize =  fragcnt * expppdu;
		fragleft = data_len % fragsize;
		if (fragleft > 0)
			expnsize += expppdu;
	}
	return expnsize;
}

/* WR with IV, KEY and CPL SFO added */
static void make_tlstx_data_wr(struct sock *sk, struct sk_buff *skb,
			       int tls_tx_imm, int tls_len, u32 credits)
{
	unsigned short pdus_per_ulp = 0;
	struct chtls_sock *csk;
	struct chtls_hws *hws;
	int expn_sz;
	int pdus;

	csk = rcu_dereference_sk_user_data(sk);
	hws = &csk->tlshws;
	pdus = DIV_ROUND_UP(tls_len, hws->mfs);
	expn_sz = chtls_expansion_size(sk, tls_len, 0, NULL);
	if (!hws->compute) {
		hws->expansion = chtls_expansion_size(sk,
						      hws->fcplenmax,
						      1, &pdus_per_ulp);
		hws->pdus = pdus_per_ulp;
		hws->adjustlen = hws->pdus *
			((hws->expansion / hws->pdus) + hws->mfs);
		hws->compute = 1;
	}
	if (tls_copy_ivs(sk, skb))
		return;
	tls_copy_tx_key(sk, skb);
	tls_tx_data_wr(sk, skb, tls_len, tls_tx_imm, credits, expn_sz, pdus);
	hws->tx_seq_no += (pdus - 1);
}

static void make_tx_data_wr(struct sock *sk, struct sk_buff *skb,
			    unsigned int immdlen, int len,
			    u32 credits, u32 compl)
{
	struct fw_ofld_tx_data_wr *req;
	unsigned int wr_ulp_mode_force;
	struct chtls_sock *csk;
	unsigned int opcode;

	csk = rcu_dereference_sk_user_data(sk);
	opcode = FW_OFLD_TX_DATA_WR;

	req = (struct fw_ofld_tx_data_wr *)__skb_push(skb, sizeof(*req));
	req->op_to_immdlen = htonl(WR_OP_V(opcode) |
				FW_WR_COMPL_V(compl) |
				FW_WR_IMMDLEN_V(immdlen));
	req->flowid_len16 = htonl(FW_WR_FLOWID_V(csk->tid) |
				FW_WR_LEN16_V(credits));

	wr_ulp_mode_force = TX_ULP_MODE_V(csk->ulp_mode);
	if (is_sg_request(skb))
		wr_ulp_mode_force |= FW_OFLD_TX_DATA_WR_ALIGNPLD_F |
			((tcp_sk(sk)->nonagle & TCP_NAGLE_OFF) ? 0 :
				FW_OFLD_TX_DATA_WR_SHOVE_F);

	req->tunnel_to_proxy = htonl(wr_ulp_mode_force |
			TX_URG_V(skb_urgent(skb)) |
			TX_SHOVE_V((!csk_flag(sk, CSK_TX_MORE_DATA)) &&
				   skb_queue_empty(&csk->txq)));
	req->plen = htonl(len);
}

static int chtls_wr_size(struct chtls_sock *csk, const struct sk_buff *skb,
			 bool size)
{
	int wr_size;

	wr_size = TLS_WR_CPL_LEN;
	wr_size += KEY_ON_MEM_SZ;
	wr_size += ivs_size(csk->sk, skb);

	if (size)
		return wr_size;

	/* frags counted for IV dsgl */
	if (!skb_ulp_tls_iv_imm(skb))
		skb_shinfo(skb)->nr_frags++;

	return wr_size;
}

static bool is_ofld_imm(struct chtls_sock *csk, const struct sk_buff *skb)
{
	int length = skb->len;

	if (skb->peeked || skb->len > MAX_IMM_ULPTX_WR_LEN)
		return false;

	if (likely(ULP_SKB_CB(skb)->flags & ULPCB_FLAG_NEED_HDR)) {
		/* Check TLS header len for Immediate */
		if (csk->ulp_mode == ULP_MODE_TLS &&
		    skb_ulp_tls_inline(skb))
			length += chtls_wr_size(csk, skb, true);
		else
			length += sizeof(struct fw_ofld_tx_data_wr);

		return length <= MAX_IMM_OFLD_TX_DATA_WR_LEN;
	}
	return true;
}

static unsigned int calc_tx_flits(const struct sk_buff *skb,
				  unsigned int immdlen)
{
	unsigned int flits, cnt;

	flits = immdlen / 8;   /* headers */
	cnt = skb_shinfo(skb)->nr_frags;
	if (skb_tail_pointer(skb) != skb_transport_header(skb))
		cnt++;
	return flits + sgl_len(cnt);
}

static void arp_failure_discard(void *handle, struct sk_buff *skb)
{
	kfree_skb(skb);
}

int chtls_push_frames(struct chtls_sock *csk, int comp)
{
	struct chtls_hws *hws = &csk->tlshws;
	struct tcp_sock *tp;
	struct sk_buff *skb;
	int total_size = 0;
	struct sock *sk;
	int wr_size;

	wr_size = sizeof(struct fw_ofld_tx_data_wr);
	sk = csk->sk;
	tp = tcp_sk(sk);

	if (unlikely(sk_in_state(sk, TCPF_SYN_SENT | TCPF_CLOSE)))
		return 0;

	if (unlikely(csk_flag(sk, CSK_ABORT_SHUTDOWN)))
		return 0;

	while (csk->wr_credits && (skb = skb_peek(&csk->txq)) &&
	       (!(ULP_SKB_CB(skb)->flags & ULPCB_FLAG_HOLD) ||
		skb_queue_len(&csk->txq) > 1)) {
		unsigned int credit_len = skb->len;
		unsigned int credits_needed;
		unsigned int completion = 0;
		int tls_len = skb->len;/* TLS data len before IV/key */
		unsigned int immdlen;
		int len = skb->len;    /* length [ulp bytes] inserted by hw */
		int flowclen16 = 0;
		int tls_tx_imm = 0;

		immdlen = skb->len;
		if (!is_ofld_imm(csk, skb)) {
			immdlen = skb_transport_offset(skb);
			if (skb_ulp_tls_inline(skb))
				wr_size = chtls_wr_size(csk, skb, false);
			credit_len = 8 * calc_tx_flits(skb, immdlen);
		} else {
			if (skb_ulp_tls_inline(skb)) {
				wr_size = chtls_wr_size(csk, skb, false);
				tls_tx_imm = 1;
			}
		}
		if (likely(ULP_SKB_CB(skb)->flags & ULPCB_FLAG_NEED_HDR))
			credit_len += wr_size;
		credits_needed = DIV_ROUND_UP(credit_len, 16);
		if (!csk_flag_nochk(csk, CSK_TX_DATA_SENT)) {
			flowclen16 = send_tx_flowc_wr(sk, 1, tp->snd_nxt,
						      tp->rcv_nxt);
			if (flowclen16 <= 0)
				break;
			csk->wr_credits -= flowclen16;
			csk->wr_unacked += flowclen16;
			csk->wr_nondata += flowclen16;
			csk_set_flag(csk, CSK_TX_DATA_SENT);
		}

		if (csk->wr_credits < credits_needed) {
			if (skb_ulp_tls_inline(skb) &&
			    !skb_ulp_tls_iv_imm(skb))
				skb_shinfo(skb)->nr_frags--;
			break;
		}

		__skb_unlink(skb, &csk->txq);
		skb_set_queue_mapping(skb, (csk->txq_idx << 1) |
				      CPL_PRIORITY_DATA);
		if (hws->ofld)
			hws->txqid = (skb->queue_mapping >> 1);
		skb->csum = (__force __wsum)(credits_needed + csk->wr_nondata);
		csk->wr_credits -= credits_needed;
		csk->wr_unacked += credits_needed;
		csk->wr_nondata = 0;
		enqueue_wr(csk, skb);

		if (likely(ULP_SKB_CB(skb)->flags & ULPCB_FLAG_NEED_HDR)) {
			if ((comp && csk->wr_unacked == credits_needed) ||
			    (ULP_SKB_CB(skb)->flags & ULPCB_FLAG_COMPL) ||
			    csk->wr_unacked >= csk->wr_max_credits / 2) {
				completion = 1;
				csk->wr_unacked = 0;
			}
			if (skb_ulp_tls_inline(skb))
				make_tlstx_data_wr(sk, skb, tls_tx_imm,
						   tls_len, credits_needed);
			else
				make_tx_data_wr(sk, skb, immdlen, len,
						credits_needed, completion);
			tp->snd_nxt += len;
			tp->lsndtime = tcp_jiffies32;
			if (completion)
				ULP_SKB_CB(skb)->flags &= ~ULPCB_FLAG_NEED_HDR;
		} else {
			struct cpl_close_con_req *req = cplhdr(skb);
			unsigned int cmd  = CPL_OPCODE_G(ntohl
					     (OPCODE_TID(req)));

			if (cmd == CPL_CLOSE_CON_REQ)
				csk_set_flag(csk,
					     CSK_CLOSE_CON_REQUESTED);

			if ((ULP_SKB_CB(skb)->flags & ULPCB_FLAG_COMPL) &&
			    (csk->wr_unacked >= csk->wr_max_credits / 2)) {
				req->wr.wr_hi |= htonl(FW_WR_COMPL_F);
				csk->wr_unacked = 0;
			}
		}
		total_size += skb->truesize;
		if (ULP_SKB_CB(skb)->flags & ULPCB_FLAG_BARRIER)
			csk_set_flag(csk, CSK_TX_WAIT_IDLE);
		t4_set_arp_err_handler(skb, NULL, arp_failure_discard);
		cxgb4_l2t_send(csk->egress_dev, skb, csk->l2t_entry);
	}
	sk->sk_wmem_queued -= total_size;
	return total_size;
}

static void mark_urg(struct tcp_sock *tp, int flags,
		     struct sk_buff *skb)
{
	if (unlikely(flags & MSG_OOB)) {
		tp->snd_up = tp->write_seq;
		ULP_SKB_CB(skb)->flags = ULPCB_FLAG_URG |
					 ULPCB_FLAG_BARRIER |
					 ULPCB_FLAG_NO_APPEND |
					 ULPCB_FLAG_NEED_HDR;
	}
}

/*
 * Returns true if a connection should send more data to TCP engine
 */
static bool should_push(struct sock *sk)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct chtls_dev *cdev = csk->cdev;
	struct tcp_sock *tp = tcp_sk(sk);

	/*
	 * If we've released our offload resources there's nothing to do ...
	 */
	if (!cdev)
		return false;

	/*
	 * If there aren't any work requests in flight, or there isn't enough
	 * data in flight, or Nagle is off then send the current TX_DATA
	 * otherwise hold it and wait to accumulate more data.
	 */
	return csk->wr_credits == csk->wr_max_credits ||
		(tp->nonagle & TCP_NAGLE_OFF);
}

/*
 * Returns true if a TCP socket is corked.
 */
static bool corked(const struct tcp_sock *tp, int flags)
{
	return (flags & MSG_MORE) || (tp->nonagle & TCP_NAGLE_CORK);
}

/*
 * Returns true if a send should try to push new data.
 */
static bool send_should_push(struct sock *sk, int flags)
{
	return should_push(sk) && !corked(tcp_sk(sk), flags);
}

void chtls_tcp_push(struct sock *sk, int flags)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	int qlen = skb_queue_len(&csk->txq);

	if (likely(qlen)) {
		struct sk_buff *skb = skb_peek_tail(&csk->txq);
		struct tcp_sock *tp = tcp_sk(sk);

		mark_urg(tp, flags, skb);

		if (!(ULP_SKB_CB(skb)->flags & ULPCB_FLAG_NO_APPEND) &&
		    corked(tp, flags)) {
			ULP_SKB_CB(skb)->flags |= ULPCB_FLAG_HOLD;
			return;
		}

		ULP_SKB_CB(skb)->flags &= ~ULPCB_FLAG_HOLD;
		if (qlen == 1 &&
		    ((ULP_SKB_CB(skb)->flags & ULPCB_FLAG_NO_APPEND) ||
		     should_push(sk)))
			chtls_push_frames(csk, 1);
	}
}

/*
 * Calculate the size for a new send sk_buff.  It's maximum size so we can
 * pack lots of data into it, unless we plan to send it immediately, in which
 * case we size it more tightly.
 *
 * Note: we don't bother compensating for MSS < PAGE_SIZE because it doesn't
 * arise in normal cases and when it does we are just wasting memory.
 */
static int select_size(struct sock *sk, int io_len, int flags, int len)
{
	const int pgbreak = SKB_MAX_HEAD(len);

	/*
	 * If the data wouldn't fit in the main body anyway, put only the
	 * header in the main body so it can use immediate data and place all
	 * the payload in page fragments.
	 */
	if (io_len > pgbreak)
		return 0;

	/*
	 * If we will be accumulating payload get a large main body.
	 */
	if (!send_should_push(sk, flags))
		return pgbreak;

	return io_len;
}

void skb_entail(struct sock *sk, struct sk_buff *skb, int flags)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	ULP_SKB_CB(skb)->seq = tp->write_seq;
	ULP_SKB_CB(skb)->flags = flags;
	__skb_queue_tail(&csk->txq, skb);
	sk->sk_wmem_queued += skb->truesize;

	if (TCP_PAGE(sk) && TCP_OFF(sk)) {
		put_page(TCP_PAGE(sk));
		TCP_PAGE(sk) = NULL;
		TCP_OFF(sk) = 0;
	}
}

static struct sk_buff *get_tx_skb(struct sock *sk, int size)
{
	struct sk_buff *skb;

	skb = alloc_skb(size + TX_HEADER_LEN, sk->sk_allocation);
	if (likely(skb)) {
		skb_reserve(skb, TX_HEADER_LEN);
		skb_entail(sk, skb, ULPCB_FLAG_NEED_HDR);
		skb_reset_transport_header(skb);
	}
	return skb;
}

static struct sk_buff *get_record_skb(struct sock *sk, int size, bool zcopy)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct sk_buff *skb;

	skb = alloc_skb(((zcopy ? 0 : size) + TX_TLSHDR_LEN +
			KEY_ON_MEM_SZ + max_ivs_size(sk, size)),
			sk->sk_allocation);
	if (likely(skb)) {
		skb_reserve(skb, (TX_TLSHDR_LEN +
			    KEY_ON_MEM_SZ + max_ivs_size(sk, size)));
		skb_entail(sk, skb, ULPCB_FLAG_NEED_HDR);
		skb_reset_transport_header(skb);
		ULP_SKB_CB(skb)->ulp.tls.ofld = 1;
		ULP_SKB_CB(skb)->ulp.tls.type = csk->tlshws.type;
	}
	return skb;
}

static void tx_skb_finalize(struct sk_buff *skb)
{
	struct ulp_skb_cb *cb = ULP_SKB_CB(skb);

	if (!(cb->flags & ULPCB_FLAG_NO_HDR))
		cb->flags = ULPCB_FLAG_NEED_HDR;
	cb->flags |= ULPCB_FLAG_NO_APPEND;
}

static void push_frames_if_head(struct sock *sk)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);

	if (skb_queue_len(&csk->txq) == 1)
		chtls_push_frames(csk, 1);
}

static int chtls_skb_copy_to_page_nocache(struct sock *sk,
					  struct iov_iter *from,
					  struct sk_buff *skb,
					  struct page *page,
					  int off, int copy)
{
	int err;

	err = skb_do_copy_data_nocache(sk, skb, from, page_address(page) +
				       off, copy, skb->len);
	if (err)
		return err;

	skb->len             += copy;
	skb->data_len        += copy;
	skb->truesize        += copy;
	sk->sk_wmem_queued   += copy;
	return 0;
}

static bool csk_mem_free(struct chtls_dev *cdev, struct sock *sk)
{
	return (cdev->max_host_sndbuf - sk->sk_wmem_queued > 0);
}

static int csk_wait_memory(struct chtls_dev *cdev,
			   struct sock *sk, long *timeo_p)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int ret, err = 0;
	long current_timeo;
	long vm_wait = 0;
	bool noblock;

	current_timeo = *timeo_p;
	noblock = (*timeo_p ? false : true);
	if (csk_mem_free(cdev, sk)) {
		current_timeo = get_random_u32_below(HZ / 5) + 2;
		vm_wait = get_random_u32_below(HZ / 5) + 2;
	}

	add_wait_queue(sk_sleep(sk), &wait);
	while (1) {
		sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);

		if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN))
			goto do_error;
		if (!*timeo_p) {
			if (noblock)
				set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
			goto do_nonblock;
		}
		if (signal_pending(current))
			goto do_interrupted;
		sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);
		if (csk_mem_free(cdev, sk) && !vm_wait)
			break;

		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		sk->sk_write_pending++;
		ret = sk_wait_event(sk, &current_timeo, sk->sk_err ||
				    (sk->sk_shutdown & SEND_SHUTDOWN) ||
				    (csk_mem_free(cdev, sk) && !vm_wait),
				    &wait);
		sk->sk_write_pending--;
		if (ret < 0)
			goto do_error;

		if (vm_wait) {
			vm_wait -= current_timeo;
			current_timeo = *timeo_p;
			if (current_timeo != MAX_SCHEDULE_TIMEOUT) {
				current_timeo -= vm_wait;
				if (current_timeo < 0)
					current_timeo = 0;
			}
			vm_wait = 0;
		}
		*timeo_p = current_timeo;
	}
do_rm_wq:
	remove_wait_queue(sk_sleep(sk), &wait);
	return err;
do_error:
	err = -EPIPE;
	goto do_rm_wq;
do_nonblock:
	err = -EAGAIN;
	goto do_rm_wq;
do_interrupted:
	err = sock_intr_errno(*timeo_p);
	goto do_rm_wq;
}

static int chtls_proccess_cmsg(struct sock *sk, struct msghdr *msg,
			       unsigned char *record_type)
{
	struct cmsghdr *cmsg;
	int rc = -EINVAL;

	for_each_cmsghdr(cmsg, msg) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;
		if (cmsg->cmsg_level != SOL_TLS)
			continue;

		switch (cmsg->cmsg_type) {
		case TLS_SET_RECORD_TYPE:
			if (cmsg->cmsg_len < CMSG_LEN(sizeof(*record_type)))
				return -EINVAL;

			if (msg->msg_flags & MSG_MORE)
				return -EINVAL;

			*record_type = *(unsigned char *)CMSG_DATA(cmsg);
			rc = 0;
			break;
		default:
			return -EINVAL;
		}
	}

	return rc;
}

int chtls_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct chtls_dev *cdev = csk->cdev;
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;
	int mss, flags, err;
	int recordsz = 0;
	int copied = 0;
	long timeo;

	lock_sock(sk);
	flags = msg->msg_flags;
	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);

	if (!sk_in_state(sk, TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)) {
		err = sk_stream_wait_connect(sk, &timeo);
		if (err)
			goto out_err;
	}

	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);
	err = -EPIPE;
	if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN))
		goto out_err;

	mss = csk->mss;
	csk_set_flag(csk, CSK_TX_MORE_DATA);

	while (msg_data_left(msg)) {
		int copy = 0;

		skb = skb_peek_tail(&csk->txq);
		if (skb) {
			copy = mss - skb->len;
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		}
		if (!csk_mem_free(cdev, sk))
			goto wait_for_sndbuf;

		if (is_tls_tx(csk) && !csk->tlshws.txleft) {
			unsigned char record_type = TLS_RECORD_TYPE_DATA;

			if (unlikely(msg->msg_controllen)) {
				err = chtls_proccess_cmsg(sk, msg,
							  &record_type);
				if (err)
					goto out_err;

				/* Avoid appending tls handshake, alert to tls data */
				if (skb)
					tx_skb_finalize(skb);
			}

			recordsz = size;
			csk->tlshws.txleft = recordsz;
			csk->tlshws.type = record_type;
		}

		if (!skb || (ULP_SKB_CB(skb)->flags & ULPCB_FLAG_NO_APPEND) ||
		    copy <= 0) {
new_buf:
			if (skb) {
				tx_skb_finalize(skb);
				push_frames_if_head(sk);
			}

			if (is_tls_tx(csk)) {
				skb = get_record_skb(sk,
						     select_size(sk,
								 recordsz,
								 flags,
								 TX_TLSHDR_LEN),
								 false);
			} else {
				skb = get_tx_skb(sk,
						 select_size(sk, size, flags,
							     TX_HEADER_LEN));
			}
			if (unlikely(!skb))
				goto wait_for_memory;

			skb->ip_summed = CHECKSUM_UNNECESSARY;
			copy = mss;
		}
		if (copy > size)
			copy = size;

		if (msg->msg_flags & MSG_SPLICE_PAGES) {
			err = skb_splice_from_iter(skb, &msg->msg_iter, copy,
						   sk->sk_allocation);
			if (err < 0) {
				if (err == -EMSGSIZE)
					goto new_buf;
				goto do_fault;
			}
			copy = err;
			sk_wmem_queued_add(sk, copy);
		} else if (skb_tailroom(skb) > 0) {
			copy = min(copy, skb_tailroom(skb));
			if (is_tls_tx(csk))
				copy = min_t(int, copy, csk->tlshws.txleft);
			err = skb_add_data_nocache(sk, skb,
						   &msg->msg_iter, copy);
			if (err)
				goto do_fault;
		} else {
			int i = skb_shinfo(skb)->nr_frags;
			struct page *page = TCP_PAGE(sk);
			int pg_size = PAGE_SIZE;
			int off = TCP_OFF(sk);
			bool merge;

			if (page)
				pg_size = page_size(page);
			if (off < pg_size &&
			    skb_can_coalesce(skb, i, page, off)) {
				merge = true;
				goto copy;
			}
			merge = false;
			if (i == (is_tls_tx(csk) ? (MAX_SKB_FRAGS - 1) :
			    MAX_SKB_FRAGS))
				goto new_buf;

			if (page && off == pg_size) {
				put_page(page);
				TCP_PAGE(sk) = page = NULL;
				pg_size = PAGE_SIZE;
			}

			if (!page) {
				gfp_t gfp = sk->sk_allocation;
				int order = cdev->send_page_order;

				if (order) {
					page = alloc_pages(gfp | __GFP_COMP |
							   __GFP_NOWARN |
							   __GFP_NORETRY,
							   order);
					if (page)
						pg_size <<= order;
				}
				if (!page) {
					page = alloc_page(gfp);
					pg_size = PAGE_SIZE;
				}
				if (!page)
					goto wait_for_memory;
				off = 0;
			}
copy:
			if (copy > pg_size - off)
				copy = pg_size - off;
			if (is_tls_tx(csk))
				copy = min_t(int, copy, csk->tlshws.txleft);

			err = chtls_skb_copy_to_page_nocache(sk, &msg->msg_iter,
							     skb, page,
							     off, copy);
			if (unlikely(err)) {
				if (!TCP_PAGE(sk)) {
					TCP_PAGE(sk) = page;
					TCP_OFF(sk) = 0;
				}
				goto do_fault;
			}
			/* Update the skb. */
			if (merge) {
				skb_frag_size_add(
						&skb_shinfo(skb)->frags[i - 1],
						copy);
			} else {
				skb_fill_page_desc(skb, i, page, off, copy);
				if (off + copy < pg_size) {
					/* space left keep page */
					get_page(page);
					TCP_PAGE(sk) = page;
				} else {
					TCP_PAGE(sk) = NULL;
				}
			}
			TCP_OFF(sk) = off + copy;
		}
		if (unlikely(skb->len == mss))
			tx_skb_finalize(skb);
		tp->write_seq += copy;
		copied += copy;
		size -= copy;

		if (is_tls_tx(csk))
			csk->tlshws.txleft -= copy;

		if (corked(tp, flags) &&
		    (sk_stream_wspace(sk) < sk_stream_min_wspace(sk)))
			ULP_SKB_CB(skb)->flags |= ULPCB_FLAG_NO_APPEND;

		if (size == 0)
			goto out;

		if (ULP_SKB_CB(skb)->flags & ULPCB_FLAG_NO_APPEND)
			push_frames_if_head(sk);
		continue;
wait_for_sndbuf:
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
wait_for_memory:
		err = csk_wait_memory(cdev, sk, &timeo);
		if (err)
			goto do_error;
	}
out:
	csk_reset_flag(csk, CSK_TX_MORE_DATA);
	if (copied)
		chtls_tcp_push(sk, flags);
done:
	release_sock(sk);
	return copied;
do_fault:
	if (!skb->len) {
		__skb_unlink(skb, &csk->txq);
		sk->sk_wmem_queued -= skb->truesize;
		__kfree_skb(skb);
	}
do_error:
	if (copied)
		goto out;
out_err:
	if (csk_conn_inline(csk))
		csk_reset_flag(csk, CSK_TX_MORE_DATA);
	copied = sk_stream_error(sk, flags, err);
	goto done;
}

void chtls_splice_eof(struct socket *sock)
{
	struct sock *sk = sock->sk;

	lock_sock(sk);
	chtls_tcp_push(sk, 0);
	release_sock(sk);
}

static void chtls_select_window(struct sock *sk)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	unsigned int wnd = tp->rcv_wnd;

	wnd = max_t(unsigned int, wnd, tcp_full_space(sk));
	wnd = max_t(unsigned int, MIN_RCV_WND, wnd);

	if (wnd > MAX_RCV_WND)
		wnd = MAX_RCV_WND;

/*
 * Check if we need to grow the receive window in response to an increase in
 * the socket's receive buffer size.  Some applications increase the buffer
 * size dynamically and rely on the window to grow accordingly.
 */

	if (wnd > tp->rcv_wnd) {
		tp->rcv_wup -= wnd - tp->rcv_wnd;
		tp->rcv_wnd = wnd;
		/* Mark the receive window as updated */
		csk_reset_flag(csk, CSK_UPDATE_RCV_WND);
	}
}

/*
 * Send RX credits through an RX_DATA_ACK CPL message.  We are permitted
 * to return without sending the message in case we cannot allocate
 * an sk_buff.  Returns the number of credits sent.
 */
static u32 send_rx_credits(struct chtls_sock *csk, u32 credits)
{
	struct cpl_rx_data_ack *req;
	struct sk_buff *skb;

	skb = alloc_skb(sizeof(*req), GFP_ATOMIC);
	if (!skb)
		return 0;
	__skb_put(skb, sizeof(*req));
	req = (struct cpl_rx_data_ack *)skb->head;

	set_wr_txq(skb, CPL_PRIORITY_ACK, csk->port_id);
	INIT_TP_WR(req, csk->tid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_RX_DATA_ACK,
						    csk->tid));
	req->credit_dack = cpu_to_be32(RX_CREDITS_V(credits) |
				       RX_FORCE_ACK_F);
	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);
	return credits;
}

#define CREDIT_RETURN_STATE (TCPF_ESTABLISHED | \
			     TCPF_FIN_WAIT1 | \
			     TCPF_FIN_WAIT2)

/*
 * Called after some received data has been read.  It returns RX credits
 * to the HW for the amount of data processed.
 */
static void chtls_cleanup_rbuf(struct sock *sk, int copied)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct tcp_sock *tp;
	int must_send;
	u32 credits;
	u32 thres;

	thres = 15 * 1024;

	if (!sk_in_state(sk, CREDIT_RETURN_STATE))
		return;

	chtls_select_window(sk);
	tp = tcp_sk(sk);
	credits = tp->copied_seq - tp->rcv_wup;
	if (unlikely(!credits))
		return;

/*
 * For coalescing to work effectively ensure the receive window has
 * at least 16KB left.
 */
	must_send = credits + 16384 >= tp->rcv_wnd;

	if (must_send || credits >= thres)
		tp->rcv_wup += send_rx_credits(csk, credits);
}

static int chtls_pt_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			    int flags, int *addr_len)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct chtls_hws *hws = &csk->tlshws;
	struct net_device *dev = csk->egress_dev;
	struct adapter *adap = netdev2adap(dev);
	struct tcp_sock *tp = tcp_sk(sk);
	unsigned long avail;
	int buffers_freed;
	int copied = 0;
	int target;
	long timeo;
	int ret;

	buffers_freed = 0;

	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	if (unlikely(csk_flag(sk, CSK_UPDATE_RCV_WND)))
		chtls_cleanup_rbuf(sk, copied);

	do {
		struct sk_buff *skb;
		u32 offset = 0;

		if (unlikely(tp->urg_data &&
			     tp->urg_seq == tp->copied_seq)) {
			if (copied)
				break;
			if (signal_pending(current)) {
				copied = timeo ? sock_intr_errno(timeo) :
					-EAGAIN;
				break;
			}
		}
		skb = skb_peek(&sk->sk_receive_queue);
		if (skb)
			goto found_ok_skb;
		if (csk->wr_credits &&
		    skb_queue_len(&csk->txq) &&
		    chtls_push_frames(csk, csk->wr_credits ==
				      csk->wr_max_credits))
			sk->sk_write_space(sk);

		if (copied >= target && !READ_ONCE(sk->sk_backlog.tail))
			break;

		if (copied) {
			if (sk->sk_err || sk->sk_state == TCP_CLOSE ||
			    (sk->sk_shutdown & RCV_SHUTDOWN) ||
			    signal_pending(current))
				break;

			if (!timeo)
				break;
		} else {
			if (sock_flag(sk, SOCK_DONE))
				break;
			if (sk->sk_err) {
				copied = sock_error(sk);
				break;
			}
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;
			if (sk->sk_state == TCP_CLOSE) {
				copied = -ENOTCONN;
				break;
			}
			if (!timeo) {
				copied = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				copied = sock_intr_errno(timeo);
				break;
			}
		}
		if (READ_ONCE(sk->sk_backlog.tail)) {
			release_sock(sk);
			lock_sock(sk);
			chtls_cleanup_rbuf(sk, copied);
			continue;
		}

		if (copied >= target)
			break;
		chtls_cleanup_rbuf(sk, copied);
		ret = sk_wait_data(sk, &timeo, NULL);
		if (ret < 0) {
			copied = copied ? : ret;
			goto unlock;
		}
		continue;
found_ok_skb:
		if (!skb->len) {
			skb_dst_set(skb, NULL);
			__skb_unlink(skb, &sk->sk_receive_queue);
			kfree_skb(skb);

			if (!copied && !timeo) {
				copied = -EAGAIN;
				break;
			}

			if (copied < target) {
				release_sock(sk);
				lock_sock(sk);
				continue;
			}
			break;
		}
		offset = hws->copied_seq;
		avail = skb->len - offset;
		if (len < avail)
			avail = len;

		if (unlikely(tp->urg_data)) {
			u32 urg_offset = tp->urg_seq - tp->copied_seq;

			if (urg_offset < avail) {
				if (urg_offset) {
					avail = urg_offset;
				} else if (!sock_flag(sk, SOCK_URGINLINE)) {
					/* First byte is urgent, skip */
					tp->copied_seq++;
					offset++;
					avail--;
					if (!avail)
						goto skip_copy;
				}
			}
		}
		/* Set record type if not already done. For a non-data record,
		 * do not proceed if record type could not be copied.
		 */
		if (ULP_SKB_CB(skb)->flags & ULPCB_FLAG_TLS_HDR) {
			struct tls_hdr *thdr = (struct tls_hdr *)skb->data;
			int cerr = 0;

			cerr = put_cmsg(msg, SOL_TLS, TLS_GET_RECORD_TYPE,
					sizeof(thdr->type), &thdr->type);

			if (cerr && thdr->type != TLS_RECORD_TYPE_DATA) {
				copied = -EIO;
				break;
			}
			/*  don't send tls header, skip copy */
			goto skip_copy;
		}

		if (skb_copy_datagram_msg(skb, offset, msg, avail)) {
			if (!copied) {
				copied = -EFAULT;
				break;
			}
		}

		copied += avail;
		len -= avail;
		hws->copied_seq += avail;
skip_copy:
		if (tp->urg_data && after(tp->copied_seq, tp->urg_seq))
			tp->urg_data = 0;

		if ((avail + offset) >= skb->len) {
			struct sk_buff *next_skb;
			if (ULP_SKB_CB(skb)->flags & ULPCB_FLAG_TLS_HDR) {
				tp->copied_seq += skb->len;
				hws->rcvpld = skb->hdr_len;
			} else {
				atomic_inc(&adap->chcr_stats.tls_pdu_rx);
				tp->copied_seq += hws->rcvpld;
			}
			chtls_free_skb(sk, skb);
			buffers_freed++;
			hws->copied_seq = 0;
			next_skb = skb_peek(&sk->sk_receive_queue);
			if (copied >= target && !next_skb)
				break;
			if (ULP_SKB_CB(next_skb)->flags & ULPCB_FLAG_TLS_HDR)
				break;
		}
	} while (len > 0);

	if (buffers_freed)
		chtls_cleanup_rbuf(sk, copied);

unlock:
	release_sock(sk);
	return copied;
}

/*
 * Peek at data in a socket's receive buffer.
 */
static int peekmsg(struct sock *sk, struct msghdr *msg,
		   size_t len, int flags)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u32 peek_seq, offset;
	struct sk_buff *skb;
	int copied = 0;
	size_t avail;          /* amount of available data in current skb */
	long timeo;
	int ret;

	lock_sock(sk);
	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);
	peek_seq = tp->copied_seq;

	do {
		if (unlikely(tp->urg_data && tp->urg_seq == peek_seq)) {
			if (copied)
				break;
			if (signal_pending(current)) {
				copied = timeo ? sock_intr_errno(timeo) :
				-EAGAIN;
				break;
			}
		}

		skb_queue_walk(&sk->sk_receive_queue, skb) {
			offset = peek_seq - ULP_SKB_CB(skb)->seq;
			if (offset < skb->len)
				goto found_ok_skb;
		}

		/* empty receive queue */
		if (copied)
			break;
		if (sock_flag(sk, SOCK_DONE))
			break;
		if (sk->sk_err) {
			copied = sock_error(sk);
			break;
		}
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			break;
		if (sk->sk_state == TCP_CLOSE) {
			copied = -ENOTCONN;
			break;
		}
		if (!timeo) {
			copied = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			copied = sock_intr_errno(timeo);
			break;
		}

		if (READ_ONCE(sk->sk_backlog.tail)) {
			/* Do not sleep, just process backlog. */
			release_sock(sk);
			lock_sock(sk);
		} else {
			ret = sk_wait_data(sk, &timeo, NULL);
			if (ret < 0) {
				/* here 'copied' is 0 due to previous checks */
				copied = ret;
				break;
			}
		}

		if (unlikely(peek_seq != tp->copied_seq)) {
			if (net_ratelimit())
				pr_info("TCP(%s:%d), race in MSG_PEEK.\n",
					current->comm, current->pid);
			peek_seq = tp->copied_seq;
		}
		continue;

found_ok_skb:
		avail = skb->len - offset;
		if (len < avail)
			avail = len;
		/*
		 * Do we have urgent data here?  We need to skip over the
		 * urgent byte.
		 */
		if (unlikely(tp->urg_data)) {
			u32 urg_offset = tp->urg_seq - peek_seq;

			if (urg_offset < avail) {
				/*
				 * The amount of data we are preparing to copy
				 * contains urgent data.
				 */
				if (!urg_offset) { /* First byte is urgent */
					if (!sock_flag(sk, SOCK_URGINLINE)) {
						peek_seq++;
						offset++;
						avail--;
					}
					if (!avail)
						continue;
				} else {
					/* stop short of the urgent data */
					avail = urg_offset;
				}
			}
		}

		/*
		 * If MSG_TRUNC is specified the data is discarded.
		 */
		if (likely(!(flags & MSG_TRUNC)))
			if (skb_copy_datagram_msg(skb, offset, msg, len)) {
				if (!copied) {
					copied = -EFAULT;
					break;
				}
			}
		peek_seq += avail;
		copied += avail;
		len -= avail;
	} while (len > 0);

	release_sock(sk);
	return copied;
}

int chtls_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
		  int flags, int *addr_len)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct chtls_sock *csk;
	unsigned long avail;    /* amount of available data in current skb */
	int buffers_freed;
	int copied = 0;
	long timeo;
	int target;             /* Read at least this many bytes */
	int ret;

	buffers_freed = 0;

	if (unlikely(flags & MSG_OOB))
		return tcp_prot.recvmsg(sk, msg, len, flags, addr_len);

	if (unlikely(flags & MSG_PEEK))
		return peekmsg(sk, msg, len, flags);

	if (sk_can_busy_loop(sk) &&
	    skb_queue_empty_lockless(&sk->sk_receive_queue) &&
	    sk->sk_state == TCP_ESTABLISHED)
		sk_busy_loop(sk, flags & MSG_DONTWAIT);

	lock_sock(sk);
	csk = rcu_dereference_sk_user_data(sk);

	if (is_tls_rx(csk))
		return chtls_pt_recvmsg(sk, msg, len, flags, addr_len);

	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	if (unlikely(csk_flag(sk, CSK_UPDATE_RCV_WND)))
		chtls_cleanup_rbuf(sk, copied);

	do {
		struct sk_buff *skb;
		u32 offset;

		if (unlikely(tp->urg_data && tp->urg_seq == tp->copied_seq)) {
			if (copied)
				break;
			if (signal_pending(current)) {
				copied = timeo ? sock_intr_errno(timeo) :
					-EAGAIN;
				break;
			}
		}

		skb = skb_peek(&sk->sk_receive_queue);
		if (skb)
			goto found_ok_skb;

		if (csk->wr_credits &&
		    skb_queue_len(&csk->txq) &&
		    chtls_push_frames(csk, csk->wr_credits ==
				      csk->wr_max_credits))
			sk->sk_write_space(sk);

		if (copied >= target && !READ_ONCE(sk->sk_backlog.tail))
			break;

		if (copied) {
			if (sk->sk_err || sk->sk_state == TCP_CLOSE ||
			    (sk->sk_shutdown & RCV_SHUTDOWN) ||
			    signal_pending(current))
				break;
		} else {
			if (sock_flag(sk, SOCK_DONE))
				break;
			if (sk->sk_err) {
				copied = sock_error(sk);
				break;
			}
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;
			if (sk->sk_state == TCP_CLOSE) {
				copied = -ENOTCONN;
				break;
			}
			if (!timeo) {
				copied = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				copied = sock_intr_errno(timeo);
				break;
			}
		}

		if (READ_ONCE(sk->sk_backlog.tail)) {
			release_sock(sk);
			lock_sock(sk);
			chtls_cleanup_rbuf(sk, copied);
			continue;
		}

		if (copied >= target)
			break;
		chtls_cleanup_rbuf(sk, copied);
		ret = sk_wait_data(sk, &timeo, NULL);
		if (ret < 0) {
			copied = copied ? : ret;
			goto unlock;
		}
		continue;

found_ok_skb:
		if (!skb->len) {
			chtls_kfree_skb(sk, skb);
			if (!copied && !timeo) {
				copied = -EAGAIN;
				break;
			}

			if (copied < target)
				continue;

			break;
		}

		offset = tp->copied_seq - ULP_SKB_CB(skb)->seq;
		avail = skb->len - offset;
		if (len < avail)
			avail = len;

		if (unlikely(tp->urg_data)) {
			u32 urg_offset = tp->urg_seq - tp->copied_seq;

			if (urg_offset < avail) {
				if (urg_offset) {
					avail = urg_offset;
				} else if (!sock_flag(sk, SOCK_URGINLINE)) {
					tp->copied_seq++;
					offset++;
					avail--;
					if (!avail)
						goto skip_copy;
				}
			}
		}

		if (likely(!(flags & MSG_TRUNC))) {
			if (skb_copy_datagram_msg(skb, offset,
						  msg, avail)) {
				if (!copied) {
					copied = -EFAULT;
					break;
				}
			}
		}

		tp->copied_seq += avail;
		copied += avail;
		len -= avail;

skip_copy:
		if (tp->urg_data && after(tp->copied_seq, tp->urg_seq))
			tp->urg_data = 0;

		if (avail + offset >= skb->len) {
			chtls_free_skb(sk, skb);
			buffers_freed++;

			if  (copied >= target &&
			     !skb_peek(&sk->sk_receive_queue))
				break;
		}
	} while (len > 0);

	if (buffers_freed)
		chtls_cleanup_rbuf(sk, copied);

unlock:
	release_sock(sk);
	return copied;
}
