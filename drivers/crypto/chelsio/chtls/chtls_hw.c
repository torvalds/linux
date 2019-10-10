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
#include <linux/tls.h>
#include <net/tls.h>

#include "chtls.h"
#include "chtls_cm.h"

static void __set_tcb_field_direct(struct chtls_sock *csk,
				   struct cpl_set_tcb_field *req, u16 word,
				   u64 mask, u64 val, u8 cookie, int no_reply)
{
	struct ulptx_idata *sc;

	INIT_TP_WR_CPL(req, CPL_SET_TCB_FIELD, csk->tid);
	req->wr.wr_mid |= htonl(FW_WR_FLOWID_V(csk->tid));
	req->reply_ctrl = htons(NO_REPLY_V(no_reply) |
				QUEUENO_V(csk->rss_qid));
	req->word_cookie = htons(TCB_WORD_V(word) | TCB_COOKIE_V(cookie));
	req->mask = cpu_to_be64(mask);
	req->val = cpu_to_be64(val);
	sc = (struct ulptx_idata *)(req + 1);
	sc->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_NOOP));
	sc->len = htonl(0);
}

static void __set_tcb_field(struct sock *sk, struct sk_buff *skb, u16 word,
			    u64 mask, u64 val, u8 cookie, int no_reply)
{
	struct cpl_set_tcb_field *req;
	struct chtls_sock *csk;
	struct ulptx_idata *sc;
	unsigned int wrlen;

	wrlen = roundup(sizeof(*req) + sizeof(*sc), 16);
	csk = rcu_dereference_sk_user_data(sk);

	req = (struct cpl_set_tcb_field *)__skb_put(skb, wrlen);
	__set_tcb_field_direct(csk, req, word, mask, val, cookie, no_reply);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, csk->port_id);
}

/*
 * Send control message to HW, message go as immediate data and packet
 * is freed immediately.
 */
static int chtls_set_tcb_field(struct sock *sk, u16 word, u64 mask, u64 val)
{
	struct cpl_set_tcb_field *req;
	unsigned int credits_needed;
	struct chtls_sock *csk;
	struct ulptx_idata *sc;
	struct sk_buff *skb;
	unsigned int wrlen;
	int ret;

	wrlen = roundup(sizeof(*req) + sizeof(*sc), 16);

	skb = alloc_skb(wrlen, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	credits_needed = DIV_ROUND_UP(wrlen, 16);
	csk = rcu_dereference_sk_user_data(sk);

	__set_tcb_field(sk, skb, word, mask, val, 0, 1);
	skb_set_queue_mapping(skb, (csk->txq_idx << 1) | CPL_PRIORITY_DATA);
	csk->wr_credits -= credits_needed;
	csk->wr_unacked += credits_needed;
	enqueue_wr(csk, skb);
	ret = cxgb4_ofld_send(csk->egress_dev, skb);
	if (ret < 0)
		kfree_skb(skb);
	return ret < 0 ? ret : 0;
}

/*
 * Set one of the t_flags bits in the TCB.
 */
int chtls_set_tcb_tflag(struct sock *sk, unsigned int bit_pos, int val)
{
	return chtls_set_tcb_field(sk, 1, 1ULL << bit_pos,
				   (u64)val << bit_pos);
}

static int chtls_set_tcb_keyid(struct sock *sk, int keyid)
{
	return chtls_set_tcb_field(sk, 31, 0xFFFFFFFFULL, keyid);
}

static int chtls_set_tcb_seqno(struct sock *sk)
{
	return chtls_set_tcb_field(sk, 28, ~0ULL, 0);
}

static int chtls_set_tcb_quiesce(struct sock *sk, int val)
{
	return chtls_set_tcb_field(sk, 1, (1ULL << TF_RX_QUIESCE_S),
				   TF_RX_QUIESCE_V(val));
}

/* TLS Key bitmap processing */
int chtls_init_kmap(struct chtls_dev *cdev, struct cxgb4_lld_info *lldi)
{
	unsigned int num_key_ctx, bsize;
	int ksize;

	num_key_ctx = (lldi->vr->key.size / TLS_KEY_CONTEXT_SZ);
	bsize = BITS_TO_LONGS(num_key_ctx);

	cdev->kmap.size = num_key_ctx;
	cdev->kmap.available = bsize;
	ksize = sizeof(*cdev->kmap.addr) * bsize;
	cdev->kmap.addr = kvzalloc(ksize, GFP_KERNEL);
	if (!cdev->kmap.addr)
		return -ENOMEM;

	cdev->kmap.start = lldi->vr->key.start;
	spin_lock_init(&cdev->kmap.lock);
	return 0;
}

static int get_new_keyid(struct chtls_sock *csk, u32 optname)
{
	struct net_device *dev = csk->egress_dev;
	struct chtls_dev *cdev = csk->cdev;
	struct chtls_hws *hws;
	struct adapter *adap;
	int keyid;

	adap = netdev2adap(dev);
	hws = &csk->tlshws;

	spin_lock_bh(&cdev->kmap.lock);
	keyid = find_first_zero_bit(cdev->kmap.addr, cdev->kmap.size);
	if (keyid < cdev->kmap.size) {
		__set_bit(keyid, cdev->kmap.addr);
		if (optname == TLS_RX)
			hws->rxkey = keyid;
		else
			hws->txkey = keyid;
		atomic_inc(&adap->chcr_stats.tls_key);
	} else {
		keyid = -1;
	}
	spin_unlock_bh(&cdev->kmap.lock);
	return keyid;
}

void free_tls_keyid(struct sock *sk)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct net_device *dev = csk->egress_dev;
	struct chtls_dev *cdev = csk->cdev;
	struct chtls_hws *hws;
	struct adapter *adap;

	if (!cdev->kmap.addr)
		return;

	adap = netdev2adap(dev);
	hws = &csk->tlshws;

	spin_lock_bh(&cdev->kmap.lock);
	if (hws->rxkey >= 0) {
		__clear_bit(hws->rxkey, cdev->kmap.addr);
		atomic_dec(&adap->chcr_stats.tls_key);
		hws->rxkey = -1;
	}
	if (hws->txkey >= 0) {
		__clear_bit(hws->txkey, cdev->kmap.addr);
		atomic_dec(&adap->chcr_stats.tls_key);
		hws->txkey = -1;
	}
	spin_unlock_bh(&cdev->kmap.lock);
}

unsigned int keyid_to_addr(int start_addr, int keyid)
{
	return (start_addr + (keyid * TLS_KEY_CONTEXT_SZ)) >> 5;
}

static void chtls_rxkey_ivauth(struct _key_ctx *kctx)
{
	kctx->iv_to_auth = cpu_to_be64(KEYCTX_TX_WR_IV_V(6ULL) |
				  KEYCTX_TX_WR_AAD_V(1ULL) |
				  KEYCTX_TX_WR_AADST_V(5ULL) |
				  KEYCTX_TX_WR_CIPHER_V(14ULL) |
				  KEYCTX_TX_WR_CIPHERST_V(0ULL) |
				  KEYCTX_TX_WR_AUTH_V(14ULL) |
				  KEYCTX_TX_WR_AUTHST_V(16ULL) |
				  KEYCTX_TX_WR_AUTHIN_V(16ULL));
}

static int chtls_key_info(struct chtls_sock *csk,
			  struct _key_ctx *kctx,
			  u32 keylen, u32 optname)
{
	unsigned char key[AES_KEYSIZE_128];
	struct tls12_crypto_info_aes_gcm_128 *gcm_ctx;
	unsigned char ghash_h[AEAD_H_SIZE];
	int ck_size, key_ctx_size;
	struct crypto_aes_ctx aes;
	int ret;

	gcm_ctx = (struct tls12_crypto_info_aes_gcm_128 *)
		  &csk->tlshws.crypto_info;

	key_ctx_size = sizeof(struct _key_ctx) +
		       roundup(keylen, 16) + AEAD_H_SIZE;

	if (keylen == AES_KEYSIZE_128) {
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
	} else {
		pr_err("GCM: Invalid key length %d\n", keylen);
		return -EINVAL;
	}
	memcpy(key, gcm_ctx->key, keylen);

	/* Calculate the H = CIPH(K, 0 repeated 16 times).
	 * It will go in key context
	 */
	ret = aes_expandkey(&aes, key, keylen);
	if (ret)
		return ret;

	memset(ghash_h, 0, AEAD_H_SIZE);
	aes_encrypt(&aes, ghash_h, ghash_h);
	memzero_explicit(&aes, sizeof(aes));
	csk->tlshws.keylen = key_ctx_size;

	/* Copy the Key context */
	if (optname == TLS_RX) {
		int key_ctx;

		key_ctx = ((key_ctx_size >> 4) << 3);
		kctx->ctx_hdr = FILL_KEY_CRX_HDR(ck_size,
						 CHCR_KEYCTX_MAC_KEY_SIZE_128,
						 0, 0, key_ctx);
		chtls_rxkey_ivauth(kctx);
	} else {
		kctx->ctx_hdr = FILL_KEY_CTX_HDR(ck_size,
						 CHCR_KEYCTX_MAC_KEY_SIZE_128,
						 0, 0, key_ctx_size >> 4);
	}

	memcpy(kctx->salt, gcm_ctx->salt, TLS_CIPHER_AES_GCM_128_SALT_SIZE);
	memcpy(kctx->key, gcm_ctx->key, keylen);
	memcpy(kctx->key + keylen, ghash_h, AEAD_H_SIZE);
	/* erase key info from driver */
	memset(gcm_ctx->key, 0, keylen);

	return 0;
}

static void chtls_set_scmd(struct chtls_sock *csk)
{
	struct chtls_hws *hws = &csk->tlshws;

	hws->scmd.seqno_numivs =
		SCMD_SEQ_NO_CTRL_V(3) |
		SCMD_PROTO_VERSION_V(0) |
		SCMD_ENC_DEC_CTRL_V(0) |
		SCMD_CIPH_AUTH_SEQ_CTRL_V(1) |
		SCMD_CIPH_MODE_V(2) |
		SCMD_AUTH_MODE_V(4) |
		SCMD_HMAC_CTRL_V(0) |
		SCMD_IV_SIZE_V(4) |
		SCMD_NUM_IVS_V(1);

	hws->scmd.ivgen_hdrlen =
		SCMD_IV_GEN_CTRL_V(1) |
		SCMD_KEY_CTX_INLINE_V(0) |
		SCMD_TLS_FRAG_ENABLE_V(1);
}

int chtls_setkey(struct chtls_sock *csk, u32 keylen, u32 optname)
{
	struct tls_key_req *kwr;
	struct chtls_dev *cdev;
	struct _key_ctx *kctx;
	int wrlen, klen, len;
	struct sk_buff *skb;
	struct sock *sk;
	int keyid;
	int kaddr;
	int ret;

	cdev = csk->cdev;
	sk = csk->sk;

	klen = roundup((keylen + AEAD_H_SIZE) + sizeof(*kctx), 32);
	wrlen = roundup(sizeof(*kwr), 16);
	len = klen + wrlen;

	/* Flush out-standing data before new key takes effect */
	if (optname == TLS_TX) {
		lock_sock(sk);
		if (skb_queue_len(&csk->txq))
			chtls_push_frames(csk, 0);
		release_sock(sk);
	}

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	keyid = get_new_keyid(csk, optname);
	if (keyid < 0) {
		ret = -ENOSPC;
		goto out_nokey;
	}

	kaddr = keyid_to_addr(cdev->kmap.start, keyid);
	kwr = (struct tls_key_req *)__skb_put_zero(skb, len);
	kwr->wr.op_to_compl =
		cpu_to_be32(FW_WR_OP_V(FW_ULPTX_WR) | FW_WR_COMPL_F |
		      FW_WR_ATOMIC_V(1U));
	kwr->wr.flowid_len16 =
		cpu_to_be32(FW_WR_LEN16_V(DIV_ROUND_UP(len, 16) |
			    FW_WR_FLOWID_V(csk->tid)));
	kwr->wr.protocol = 0;
	kwr->wr.mfs = htons(TLS_MFS);
	kwr->wr.reneg_to_write_rx = optname;

	/* ulptx command */
	kwr->req.cmd = cpu_to_be32(ULPTX_CMD_V(ULP_TX_MEM_WRITE) |
			    T5_ULP_MEMIO_ORDER_V(1) |
			    T5_ULP_MEMIO_IMM_V(1));
	kwr->req.len16 = cpu_to_be32((csk->tid << 8) |
			      DIV_ROUND_UP(len - sizeof(kwr->wr), 16));
	kwr->req.dlen = cpu_to_be32(ULP_MEMIO_DATA_LEN_V(klen >> 5));
	kwr->req.lock_addr = cpu_to_be32(ULP_MEMIO_ADDR_V(kaddr));

	/* sub command */
	kwr->sc_imm.cmd_more = cpu_to_be32(ULPTX_CMD_V(ULP_TX_SC_IMM));
	kwr->sc_imm.len = cpu_to_be32(klen);

	/* key info */
	kctx = (struct _key_ctx *)(kwr + 1);
	ret = chtls_key_info(csk, kctx, keylen, optname);
	if (ret)
		goto out_notcb;

	set_wr_txq(skb, CPL_PRIORITY_DATA, csk->tlshws.txqid);
	csk->wr_credits -= DIV_ROUND_UP(len, 16);
	csk->wr_unacked += DIV_ROUND_UP(len, 16);
	enqueue_wr(csk, skb);
	cxgb4_ofld_send(csk->egress_dev, skb);

	chtls_set_scmd(csk);
	/* Clear quiesce for Rx key */
	if (optname == TLS_RX) {
		ret = chtls_set_tcb_keyid(sk, keyid);
		if (ret)
			goto out_notcb;
		ret = chtls_set_tcb_field(sk, 0,
					  TCB_ULP_RAW_V(TCB_ULP_RAW_M),
					  TCB_ULP_RAW_V((TF_TLS_KEY_SIZE_V(1) |
					  TF_TLS_CONTROL_V(1) |
					  TF_TLS_ACTIVE_V(1) |
					  TF_TLS_ENABLE_V(1))));
		if (ret)
			goto out_notcb;
		ret = chtls_set_tcb_seqno(sk);
		if (ret)
			goto out_notcb;
		ret = chtls_set_tcb_quiesce(sk, 0);
		if (ret)
			goto out_notcb;
		csk->tlshws.rxkey = keyid;
	} else {
		csk->tlshws.tx_seq_no = 0;
		csk->tlshws.txkey = keyid;
	}

	return ret;
out_notcb:
	free_tls_keyid(sk);
out_nokey:
	kfree_skb(skb);
	return ret;
}
