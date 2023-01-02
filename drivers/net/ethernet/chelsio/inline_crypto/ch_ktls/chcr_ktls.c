// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Chelsio Communications.  All rights reserved. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/ip.h>
#include <net/ipv6.h>
#include <linux/netdevice.h>
#include <crypto/aes.h>
#include "chcr_ktls.h"

static LIST_HEAD(uld_ctx_list);
static DEFINE_MUTEX(dev_mutex);

/* chcr_get_nfrags_to_send: get the remaining nfrags after start offset
 * @skb: skb
 * @start: start offset.
 * @len: how much data to send after @start
 */
static int chcr_get_nfrags_to_send(struct sk_buff *skb, u32 start, u32 len)
{
	struct skb_shared_info *si = skb_shinfo(skb);
	u32 frag_size, skb_linear_data_len = skb_headlen(skb);
	u8 nfrags = 0, frag_idx = 0;
	skb_frag_t *frag;

	/* if its a linear skb then return 1 */
	if (!skb_is_nonlinear(skb))
		return 1;

	if (unlikely(start < skb_linear_data_len)) {
		frag_size = min(len, skb_linear_data_len - start);
	} else {
		start -= skb_linear_data_len;

		frag = &si->frags[frag_idx];
		frag_size = skb_frag_size(frag);
		while (start >= frag_size) {
			start -= frag_size;
			frag_idx++;
			frag = &si->frags[frag_idx];
			frag_size = skb_frag_size(frag);
		}
		frag_size = min(len, skb_frag_size(frag) - start);
	}
	len -= frag_size;
	nfrags++;

	while (len) {
		frag_size = min(len, skb_frag_size(&si->frags[frag_idx]));
		len -= frag_size;
		nfrags++;
		frag_idx++;
	}
	return nfrags;
}

static int chcr_init_tcb_fields(struct chcr_ktls_info *tx_info);
static void clear_conn_resources(struct chcr_ktls_info *tx_info);
/*
 * chcr_ktls_save_keys: calculate and save crypto keys.
 * @tx_info - driver specific tls info.
 * @crypto_info - tls crypto information.
 * @direction - TX/RX direction.
 * return - SUCCESS/FAILURE.
 */
static int chcr_ktls_save_keys(struct chcr_ktls_info *tx_info,
			       struct tls_crypto_info *crypto_info,
			       enum tls_offload_ctx_dir direction)
{
	int ck_size, key_ctx_size, mac_key_size, keylen, ghash_size, ret;
	unsigned char ghash_h[TLS_CIPHER_AES_GCM_256_TAG_SIZE];
	struct tls12_crypto_info_aes_gcm_128 *info_128_gcm;
	struct ktls_key_ctx *kctx = &tx_info->key_ctx;
	struct crypto_aes_ctx aes_ctx;
	unsigned char *key, *salt;

	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128:
		info_128_gcm =
			(struct tls12_crypto_info_aes_gcm_128 *)crypto_info;
		keylen = TLS_CIPHER_AES_GCM_128_KEY_SIZE;
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
		tx_info->salt_size = TLS_CIPHER_AES_GCM_128_SALT_SIZE;
		mac_key_size = CHCR_KEYCTX_MAC_KEY_SIZE_128;
		tx_info->iv_size = TLS_CIPHER_AES_GCM_128_IV_SIZE;
		tx_info->iv = be64_to_cpu(*(__be64 *)info_128_gcm->iv);

		ghash_size = TLS_CIPHER_AES_GCM_128_TAG_SIZE;
		key = info_128_gcm->key;
		salt = info_128_gcm->salt;
		tx_info->record_no = *(u64 *)info_128_gcm->rec_seq;

		/* The SCMD fields used when encrypting a full TLS
		 * record. Its a one time calculation till the
		 * connection exists.
		 */
		tx_info->scmd0_seqno_numivs =
			SCMD_SEQ_NO_CTRL_V(CHCR_SCMD_SEQ_NO_CTRL_64BIT) |
			SCMD_CIPH_AUTH_SEQ_CTRL_F |
			SCMD_PROTO_VERSION_V(CHCR_SCMD_PROTO_VERSION_TLS) |
			SCMD_CIPH_MODE_V(CHCR_SCMD_CIPHER_MODE_AES_GCM) |
			SCMD_AUTH_MODE_V(CHCR_SCMD_AUTH_MODE_GHASH) |
			SCMD_IV_SIZE_V(TLS_CIPHER_AES_GCM_128_IV_SIZE >> 1) |
			SCMD_NUM_IVS_V(1);

		/* keys will be sent inline. */
		tx_info->scmd0_ivgen_hdrlen = SCMD_KEY_CTX_INLINE_F;

		/* The SCMD fields used when encrypting a partial TLS
		 * record (no trailer and possibly a truncated payload).
		 */
		tx_info->scmd0_short_seqno_numivs =
			SCMD_CIPH_AUTH_SEQ_CTRL_F |
			SCMD_PROTO_VERSION_V(CHCR_SCMD_PROTO_VERSION_GENERIC) |
			SCMD_CIPH_MODE_V(CHCR_SCMD_CIPHER_MODE_AES_CTR) |
			SCMD_IV_SIZE_V(AES_BLOCK_LEN >> 1);

		tx_info->scmd0_short_ivgen_hdrlen =
			tx_info->scmd0_ivgen_hdrlen | SCMD_AADIVDROP_F;

		break;

	default:
		pr_err("GCM: cipher type 0x%x not supported\n",
		       crypto_info->cipher_type);
		ret = -EINVAL;
		goto out;
	}

	key_ctx_size = CHCR_KTLS_KEY_CTX_LEN +
		       roundup(keylen, 16) + ghash_size;
	/* Calculate the H = CIPH(K, 0 repeated 16 times).
	 * It will go in key context
	 */

	ret = aes_expandkey(&aes_ctx, key, keylen);
	if (ret)
		goto out;

	memset(ghash_h, 0, ghash_size);
	aes_encrypt(&aes_ctx, ghash_h, ghash_h);
	memzero_explicit(&aes_ctx, sizeof(aes_ctx));

	/* fill the Key context */
	if (direction == TLS_OFFLOAD_CTX_DIR_TX) {
		kctx->ctx_hdr = FILL_KEY_CTX_HDR(ck_size,
						 mac_key_size,
						 key_ctx_size >> 4);
	} else {
		ret = -EINVAL;
		goto out;
	}

	memcpy(kctx->salt, salt, tx_info->salt_size);
	memcpy(kctx->key, key, keylen);
	memcpy(kctx->key + keylen, ghash_h, ghash_size);
	tx_info->key_ctx_len = key_ctx_size;

out:
	return ret;
}

/*
 * chcr_ktls_act_open_req: creates TCB entry for ipv4 connection.
 * @sk - tcp socket.
 * @tx_info - driver specific tls info.
 * @atid - connection active tid.
 * return - send success/failure.
 */
static int chcr_ktls_act_open_req(struct sock *sk,
				  struct chcr_ktls_info *tx_info,
				  int atid)
{
	struct inet_sock *inet = inet_sk(sk);
	struct cpl_t6_act_open_req *cpl6;
	struct cpl_act_open_req *cpl;
	struct sk_buff *skb;
	unsigned int len;
	int qid_atid;
	u64 options;

	len = sizeof(*cpl6);
	skb = alloc_skb(len, GFP_KERNEL);
	if (unlikely(!skb))
		return -ENOMEM;
	/* mark it a control pkt */
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, tx_info->port_id);

	cpl6 = __skb_put_zero(skb, len);
	cpl = (struct cpl_act_open_req *)cpl6;
	INIT_TP_WR(cpl6, 0);
	qid_atid = TID_QID_V(tx_info->rx_qid) |
		   TID_TID_V(atid);
	OPCODE_TID(cpl) = htonl(MK_OPCODE_TID(CPL_ACT_OPEN_REQ, qid_atid));
	cpl->local_port = inet->inet_sport;
	cpl->peer_port = inet->inet_dport;
	cpl->local_ip = inet->inet_rcv_saddr;
	cpl->peer_ip = inet->inet_daddr;

	/* fill first 64 bit option field. */
	options = TCAM_BYPASS_F | ULP_MODE_V(ULP_MODE_NONE) | NON_OFFLOAD_F |
		  SMAC_SEL_V(tx_info->smt_idx) | TX_CHAN_V(tx_info->tx_chan);
	cpl->opt0 = cpu_to_be64(options);

	/* next 64 bit option field. */
	options =
		TX_QUEUE_V(tx_info->adap->params.tp.tx_modq[tx_info->tx_chan]);
	cpl->opt2 = htonl(options);

	return cxgb4_l2t_send(tx_info->netdev, skb, tx_info->l2te);
}

#if IS_ENABLED(CONFIG_IPV6)
/*
 * chcr_ktls_act_open_req6: creates TCB entry for ipv6 connection.
 * @sk - tcp socket.
 * @tx_info - driver specific tls info.
 * @atid - connection active tid.
 * return - send success/failure.
 */
static int chcr_ktls_act_open_req6(struct sock *sk,
				   struct chcr_ktls_info *tx_info,
				   int atid)
{
	struct inet_sock *inet = inet_sk(sk);
	struct cpl_t6_act_open_req6 *cpl6;
	struct cpl_act_open_req6 *cpl;
	struct sk_buff *skb;
	unsigned int len;
	int qid_atid;
	u64 options;

	len = sizeof(*cpl6);
	skb = alloc_skb(len, GFP_KERNEL);
	if (unlikely(!skb))
		return -ENOMEM;
	/* mark it a control pkt */
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, tx_info->port_id);

	cpl6 = __skb_put_zero(skb, len);
	cpl = (struct cpl_act_open_req6 *)cpl6;
	INIT_TP_WR(cpl6, 0);
	qid_atid = TID_QID_V(tx_info->rx_qid) | TID_TID_V(atid);
	OPCODE_TID(cpl) = htonl(MK_OPCODE_TID(CPL_ACT_OPEN_REQ6, qid_atid));
	cpl->local_port = inet->inet_sport;
	cpl->peer_port = inet->inet_dport;
	cpl->local_ip_hi = *(__be64 *)&sk->sk_v6_rcv_saddr.in6_u.u6_addr8[0];
	cpl->local_ip_lo = *(__be64 *)&sk->sk_v6_rcv_saddr.in6_u.u6_addr8[8];
	cpl->peer_ip_hi = *(__be64 *)&sk->sk_v6_daddr.in6_u.u6_addr8[0];
	cpl->peer_ip_lo = *(__be64 *)&sk->sk_v6_daddr.in6_u.u6_addr8[8];

	/* first 64 bit option field. */
	options = TCAM_BYPASS_F | ULP_MODE_V(ULP_MODE_NONE) | NON_OFFLOAD_F |
		  SMAC_SEL_V(tx_info->smt_idx) | TX_CHAN_V(tx_info->tx_chan);
	cpl->opt0 = cpu_to_be64(options);
	/* next 64 bit option field. */
	options =
		TX_QUEUE_V(tx_info->adap->params.tp.tx_modq[tx_info->tx_chan]);
	cpl->opt2 = htonl(options);

	return cxgb4_l2t_send(tx_info->netdev, skb, tx_info->l2te);
}
#endif /* #if IS_ENABLED(CONFIG_IPV6) */

/*
 * chcr_setup_connection:  create a TCB entry so that TP will form tcp packets.
 * @sk - tcp socket.
 * @tx_info - driver specific tls info.
 * return: NET_TX_OK/NET_XMIT_DROP
 */
static int chcr_setup_connection(struct sock *sk,
				 struct chcr_ktls_info *tx_info)
{
	struct tid_info *t = &tx_info->adap->tids;
	int atid, ret = 0;

	atid = cxgb4_alloc_atid(t, tx_info);
	if (atid == -1)
		return -EINVAL;

	tx_info->atid = atid;

	if (tx_info->ip_family == AF_INET) {
		ret = chcr_ktls_act_open_req(sk, tx_info, atid);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		ret = cxgb4_clip_get(tx_info->netdev, (const u32 *)
				     &sk->sk_v6_rcv_saddr,
				     1);
		if (ret)
			return ret;
		ret = chcr_ktls_act_open_req6(sk, tx_info, atid);
#endif
	}

	/* if return type is NET_XMIT_CN, msg will be sent but delayed, mark ret
	 * success, if any other return type clear atid and return that failure.
	 */
	if (ret) {
		if (ret == NET_XMIT_CN) {
			ret = 0;
		} else {
#if IS_ENABLED(CONFIG_IPV6)
			/* clear clip entry */
			if (tx_info->ip_family == AF_INET6)
				cxgb4_clip_release(tx_info->netdev,
						   (const u32 *)
						   &sk->sk_v6_rcv_saddr,
						   1);
#endif
			cxgb4_free_atid(t, atid);
		}
	}

	return ret;
}

/*
 * chcr_set_tcb_field: update tcb fields.
 * @tx_info - driver specific tls info.
 * @word - TCB word.
 * @mask - TCB word related mask.
 * @val - TCB word related value.
 * @no_reply - set 1 if not looking for TP response.
 */
static int chcr_set_tcb_field(struct chcr_ktls_info *tx_info, u16 word,
			      u64 mask, u64 val, int no_reply)
{
	struct cpl_set_tcb_field *req;
	struct sk_buff *skb;

	skb = alloc_skb(sizeof(struct cpl_set_tcb_field), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	req = (struct cpl_set_tcb_field *)__skb_put_zero(skb, sizeof(*req));
	INIT_TP_WR_CPL(req, CPL_SET_TCB_FIELD, tx_info->tid);
	req->reply_ctrl = htons(QUEUENO_V(tx_info->rx_qid) |
				NO_REPLY_V(no_reply));
	req->word_cookie = htons(TCB_WORD_V(word));
	req->mask = cpu_to_be64(mask);
	req->val = cpu_to_be64(val);

	set_wr_txq(skb, CPL_PRIORITY_CONTROL, tx_info->port_id);
	return cxgb4_ofld_send(tx_info->netdev, skb);
}

/*
 * chcr_ktls_dev_del:  call back for tls_dev_del.
 * Remove the tid and l2t entry and close the connection.
 * it per connection basis.
 * @netdev - net device.
 * @tls_cts - tls context.
 * @direction - TX/RX crypto direction
 */
static void chcr_ktls_dev_del(struct net_device *netdev,
			      struct tls_context *tls_ctx,
			      enum tls_offload_ctx_dir direction)
{
	struct chcr_ktls_ofld_ctx_tx *tx_ctx =
				chcr_get_ktls_tx_context(tls_ctx);
	struct chcr_ktls_info *tx_info = tx_ctx->chcr_info;
	struct ch_ktls_port_stats_debug *port_stats;
	struct chcr_ktls_uld_ctx *u_ctx;

	if (!tx_info)
		return;

	u_ctx = tx_info->adap->uld[CXGB4_ULD_KTLS].handle;
	if (u_ctx && u_ctx->detach)
		return;
	/* clear l2t entry */
	if (tx_info->l2te)
		cxgb4_l2t_release(tx_info->l2te);

#if IS_ENABLED(CONFIG_IPV6)
	/* clear clip entry */
	if (tx_info->ip_family == AF_INET6)
		cxgb4_clip_release(netdev, (const u32 *)
				   &tx_info->sk->sk_v6_rcv_saddr,
				   1);
#endif

	/* clear tid */
	if (tx_info->tid != -1) {
		cxgb4_remove_tid(&tx_info->adap->tids, tx_info->tx_chan,
				 tx_info->tid, tx_info->ip_family);

		xa_erase(&u_ctx->tid_list, tx_info->tid);
	}

	port_stats = &tx_info->adap->ch_ktls_stats.ktls_port[tx_info->port_id];
	atomic64_inc(&port_stats->ktls_tx_connection_close);
	kvfree(tx_info);
	tx_ctx->chcr_info = NULL;
	/* release module refcount */
	module_put(THIS_MODULE);
}

/*
 * chcr_ktls_dev_add:  call back for tls_dev_add.
 * Create a tcb entry for TP. Also add l2t entry for the connection. And
 * generate keys & save those keys locally.
 * @netdev - net device.
 * @tls_cts - tls context.
 * @direction - TX/RX crypto direction
 * return: SUCCESS/FAILURE.
 */
static int chcr_ktls_dev_add(struct net_device *netdev, struct sock *sk,
			     enum tls_offload_ctx_dir direction,
			     struct tls_crypto_info *crypto_info,
			     u32 start_offload_tcp_sn)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct ch_ktls_port_stats_debug *port_stats;
	struct chcr_ktls_ofld_ctx_tx *tx_ctx;
	struct chcr_ktls_uld_ctx *u_ctx;
	struct chcr_ktls_info *tx_info;
	struct dst_entry *dst;
	struct adapter *adap;
	struct port_info *pi;
	struct neighbour *n;
	u8 daaddr[16];
	int ret = -1;

	tx_ctx = chcr_get_ktls_tx_context(tls_ctx);

	pi = netdev_priv(netdev);
	adap = pi->adapter;
	port_stats = &adap->ch_ktls_stats.ktls_port[pi->port_id];
	atomic64_inc(&port_stats->ktls_tx_connection_open);
	u_ctx = adap->uld[CXGB4_ULD_KTLS].handle;

	if (direction == TLS_OFFLOAD_CTX_DIR_RX) {
		pr_err("not expecting for RX direction\n");
		goto out;
	}

	if (tx_ctx->chcr_info)
		goto out;

	if (u_ctx && u_ctx->detach)
		goto out;

	tx_info = kvzalloc(sizeof(*tx_info), GFP_KERNEL);
	if (!tx_info)
		goto out;

	tx_info->sk = sk;
	spin_lock_init(&tx_info->lock);
	/* initialize tid and atid to -1, 0 is a also a valid id. */
	tx_info->tid = -1;
	tx_info->atid = -1;

	tx_info->adap = adap;
	tx_info->netdev = netdev;
	tx_info->first_qset = pi->first_qset;
	tx_info->tx_chan = pi->tx_chan;
	tx_info->smt_idx = pi->smt_idx;
	tx_info->port_id = pi->port_id;
	tx_info->prev_ack = 0;
	tx_info->prev_win = 0;

	tx_info->rx_qid = chcr_get_first_rx_qid(adap);
	if (unlikely(tx_info->rx_qid < 0))
		goto free_tx_info;

	tx_info->prev_seq = start_offload_tcp_sn;
	tx_info->tcp_start_seq_number = start_offload_tcp_sn;

	/* save crypto keys */
	ret = chcr_ktls_save_keys(tx_info, crypto_info, direction);
	if (ret < 0)
		goto free_tx_info;

	/* get peer ip */
	if (sk->sk_family == AF_INET) {
		memcpy(daaddr, &sk->sk_daddr, 4);
		tx_info->ip_family = AF_INET;
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		if (!ipv6_only_sock(sk) &&
		    ipv6_addr_type(&sk->sk_v6_daddr) == IPV6_ADDR_MAPPED) {
			memcpy(daaddr, &sk->sk_daddr, 4);
			tx_info->ip_family = AF_INET;
		} else {
			memcpy(daaddr, sk->sk_v6_daddr.in6_u.u6_addr8, 16);
			tx_info->ip_family = AF_INET6;
		}
#endif
	}

	/* get the l2t index */
	dst = sk_dst_get(sk);
	if (!dst) {
		pr_err("DST entry not found\n");
		goto free_tx_info;
	}
	n = dst_neigh_lookup(dst, daaddr);
	if (!n || !n->dev) {
		pr_err("neighbour not found\n");
		dst_release(dst);
		goto free_tx_info;
	}
	tx_info->l2te  = cxgb4_l2t_get(adap->l2t, n, n->dev, 0);

	neigh_release(n);
	dst_release(dst);

	if (!tx_info->l2te) {
		pr_err("l2t entry not found\n");
		goto free_tx_info;
	}

	/* Driver shouldn't be removed until any single connection exists */
	if (!try_module_get(THIS_MODULE))
		goto free_l2t;

	init_completion(&tx_info->completion);
	/* create a filter and call cxgb4_l2t_send to send the packet out, which
	 * will take care of updating l2t entry in hw if not already done.
	 */
	tx_info->open_state = CH_KTLS_OPEN_PENDING;

	if (chcr_setup_connection(sk, tx_info))
		goto put_module;

	/* Wait for reply */
	wait_for_completion_timeout(&tx_info->completion, 30 * HZ);
	spin_lock_bh(&tx_info->lock);
	if (tx_info->open_state) {
		/* need to wait for hw response, can't free tx_info yet. */
		if (tx_info->open_state == CH_KTLS_OPEN_PENDING)
			tx_info->pending_close = true;
		else
			spin_unlock_bh(&tx_info->lock);
		/* if in pending close, free the lock after the cleanup */
		goto put_module;
	}
	spin_unlock_bh(&tx_info->lock);

	/* initialize tcb */
	reinit_completion(&tx_info->completion);
	/* mark it pending for hw response */
	tx_info->open_state = CH_KTLS_OPEN_PENDING;

	if (chcr_init_tcb_fields(tx_info))
		goto free_tid;

	/* Wait for reply */
	wait_for_completion_timeout(&tx_info->completion, 30 * HZ);
	spin_lock_bh(&tx_info->lock);
	if (tx_info->open_state) {
		/* need to wait for hw response, can't free tx_info yet. */
		tx_info->pending_close = true;
		/* free the lock after cleanup */
		goto free_tid;
	}
	spin_unlock_bh(&tx_info->lock);

	if (!cxgb4_check_l2t_valid(tx_info->l2te))
		goto free_tid;

	atomic64_inc(&port_stats->ktls_tx_ctx);
	tx_ctx->chcr_info = tx_info;

	return 0;

free_tid:
#if IS_ENABLED(CONFIG_IPV6)
	/* clear clip entry */
	if (tx_info->ip_family == AF_INET6)
		cxgb4_clip_release(netdev, (const u32 *)
				   &sk->sk_v6_rcv_saddr,
				   1);
#endif
	cxgb4_remove_tid(&tx_info->adap->tids, tx_info->tx_chan,
			 tx_info->tid, tx_info->ip_family);

	xa_erase(&u_ctx->tid_list, tx_info->tid);

put_module:
	/* release module refcount */
	module_put(THIS_MODULE);
free_l2t:
	cxgb4_l2t_release(tx_info->l2te);
free_tx_info:
	if (tx_info->pending_close)
		spin_unlock_bh(&tx_info->lock);
	else
		kvfree(tx_info);
out:
	atomic64_inc(&port_stats->ktls_tx_connection_fail);
	return -1;
}

/*
 * chcr_init_tcb_fields:  Initialize tcb fields to handle TCP seq number
 *			  handling.
 * @tx_info - driver specific tls info.
 * return: NET_TX_OK/NET_XMIT_DROP
 */
static int chcr_init_tcb_fields(struct chcr_ktls_info *tx_info)
{
	int  ret = 0;

	/* set tcb in offload and bypass */
	ret =
	chcr_set_tcb_field(tx_info, TCB_T_FLAGS_W,
			   TCB_T_FLAGS_V(TF_CORE_BYPASS_F | TF_NON_OFFLOAD_F),
			   TCB_T_FLAGS_V(TF_CORE_BYPASS_F), 1);
	if (ret)
		return ret;
	/* reset snd_una and snd_next fields in tcb */
	ret = chcr_set_tcb_field(tx_info, TCB_SND_UNA_RAW_W,
				 TCB_SND_NXT_RAW_V(TCB_SND_NXT_RAW_M) |
				 TCB_SND_UNA_RAW_V(TCB_SND_UNA_RAW_M),
				 0, 1);
	if (ret)
		return ret;

	/* reset send max */
	ret = chcr_set_tcb_field(tx_info, TCB_SND_MAX_RAW_W,
				 TCB_SND_MAX_RAW_V(TCB_SND_MAX_RAW_M),
				 0, 1);
	if (ret)
		return ret;

	/* update l2t index and request for tp reply to confirm tcb is
	 * initialised to handle tx traffic.
	 */
	ret = chcr_set_tcb_field(tx_info, TCB_L2T_IX_W,
				 TCB_L2T_IX_V(TCB_L2T_IX_M),
				 TCB_L2T_IX_V(tx_info->l2te->idx), 0);
	return ret;
}

/*
 * chcr_ktls_cpl_act_open_rpl: connection reply received from TP.
 */
static int chcr_ktls_cpl_act_open_rpl(struct adapter *adap,
				      unsigned char *input)
{
	const struct cpl_act_open_rpl *p = (void *)input;
	struct chcr_ktls_info *tx_info = NULL;
	struct chcr_ktls_ofld_ctx_tx *tx_ctx;
	struct chcr_ktls_uld_ctx *u_ctx;
	unsigned int atid, tid, status;
	struct tls_context *tls_ctx;
	struct tid_info *t;
	int ret = 0;

	tid = GET_TID(p);
	status = AOPEN_STATUS_G(ntohl(p->atid_status));
	atid = TID_TID_G(AOPEN_ATID_G(ntohl(p->atid_status)));

	t = &adap->tids;
	tx_info = lookup_atid(t, atid);

	if (!tx_info || tx_info->atid != atid) {
		pr_err("%s: incorrect tx_info or atid\n", __func__);
		return -1;
	}

	cxgb4_free_atid(t, atid);
	tx_info->atid = -1;

	spin_lock(&tx_info->lock);
	/* HW response is very close, finish pending cleanup */
	if (tx_info->pending_close) {
		spin_unlock(&tx_info->lock);
		if (!status) {
			cxgb4_remove_tid(&tx_info->adap->tids, tx_info->tx_chan,
					 tid, tx_info->ip_family);
		}
		kvfree(tx_info);
		return 0;
	}

	if (!status) {
		tx_info->tid = tid;
		cxgb4_insert_tid(t, tx_info, tx_info->tid, tx_info->ip_family);
		/* Adding tid */
		tls_ctx = tls_get_ctx(tx_info->sk);
		tx_ctx = chcr_get_ktls_tx_context(tls_ctx);
		u_ctx = adap->uld[CXGB4_ULD_KTLS].handle;
		if (u_ctx) {
			ret = xa_insert_bh(&u_ctx->tid_list, tid, tx_ctx,
					   GFP_NOWAIT);
			if (ret < 0) {
				pr_err("%s: Failed to allocate tid XA entry = %d\n",
				       __func__, tx_info->tid);
				tx_info->open_state = CH_KTLS_OPEN_FAILURE;
				goto out;
			}
		}
		tx_info->open_state = CH_KTLS_OPEN_SUCCESS;
	} else {
		tx_info->open_state = CH_KTLS_OPEN_FAILURE;
	}
out:
	spin_unlock(&tx_info->lock);

	complete(&tx_info->completion);
	return ret;
}

/*
 * chcr_ktls_cpl_set_tcb_rpl: TCB reply received from TP.
 */
static int chcr_ktls_cpl_set_tcb_rpl(struct adapter *adap, unsigned char *input)
{
	const struct cpl_set_tcb_rpl *p = (void *)input;
	struct chcr_ktls_info *tx_info = NULL;
	struct tid_info *t;
	u32 tid;

	tid = GET_TID(p);

	t = &adap->tids;
	tx_info = lookup_tid(t, tid);

	if (!tx_info || tx_info->tid != tid) {
		pr_err("%s: incorrect tx_info or tid\n", __func__);
		return -1;
	}

	spin_lock(&tx_info->lock);
	if (tx_info->pending_close) {
		spin_unlock(&tx_info->lock);
		kvfree(tx_info);
		return 0;
	}
	tx_info->open_state = CH_KTLS_OPEN_SUCCESS;
	spin_unlock(&tx_info->lock);

	complete(&tx_info->completion);
	return 0;
}

static void *__chcr_write_cpl_set_tcb_ulp(struct chcr_ktls_info *tx_info,
					u32 tid, void *pos, u16 word,
					struct sge_eth_txq *q, u64 mask,
					u64 val, u32 reply)
{
	struct cpl_set_tcb_field_core *cpl;
	struct ulptx_idata *idata;
	struct ulp_txpkt *txpkt;

	/* ULP_TXPKT */
	txpkt = pos;
	txpkt->cmd_dest = htonl(ULPTX_CMD_V(ULP_TX_PKT) |
				ULP_TXPKT_CHANNELID_V(tx_info->port_id) |
				ULP_TXPKT_FID_V(q->q.cntxt_id) |
				ULP_TXPKT_RO_F);
	txpkt->len = htonl(DIV_ROUND_UP(CHCR_SET_TCB_FIELD_LEN, 16));

	/* ULPTX_IDATA sub-command */
	idata = (struct ulptx_idata *)(txpkt + 1);
	idata->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_IMM));
	idata->len = htonl(sizeof(*cpl));
	pos = idata + 1;

	cpl = pos;
	/* CPL_SET_TCB_FIELD */
	OPCODE_TID(cpl) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, tid));
	cpl->reply_ctrl = htons(QUEUENO_V(tx_info->rx_qid) |
			NO_REPLY_V(!reply));
	cpl->word_cookie = htons(TCB_WORD_V(word));
	cpl->mask = cpu_to_be64(mask);
	cpl->val = cpu_to_be64(val);

	/* ULPTX_NOOP */
	idata = (struct ulptx_idata *)(cpl + 1);
	idata->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_NOOP));
	idata->len = htonl(0);
	pos = idata + 1;

	return pos;
}


/*
 * chcr_write_cpl_set_tcb_ulp: update tcb values.
 * TCB is responsible to create tcp headers, so all the related values
 * should be correctly updated.
 * @tx_info - driver specific tls info.
 * @q - tx queue on which packet is going out.
 * @tid - TCB identifier.
 * @pos - current index where should we start writing.
 * @word - TCB word.
 * @mask - TCB word related mask.
 * @val - TCB word related value.
 * @reply - set 1 if looking for TP response.
 * return - next position to write.
 */
static void *chcr_write_cpl_set_tcb_ulp(struct chcr_ktls_info *tx_info,
					struct sge_eth_txq *q, u32 tid,
					void *pos, u16 word, u64 mask,
					u64 val, u32 reply)
{
	int left = (void *)q->q.stat - pos;

	if (unlikely(left < CHCR_SET_TCB_FIELD_LEN)) {
		if (!left) {
			pos = q->q.desc;
		} else {
			u8 buf[48] = {0};

			__chcr_write_cpl_set_tcb_ulp(tx_info, tid, buf, word, q,
						     mask, val, reply);

			return chcr_copy_to_txd(buf, &q->q, pos,
						CHCR_SET_TCB_FIELD_LEN);
		}
	}

	pos = __chcr_write_cpl_set_tcb_ulp(tx_info, tid, pos, word, q,
					   mask, val, reply);

	/* check again if we are at the end of the queue */
	if (left == CHCR_SET_TCB_FIELD_LEN)
		pos = q->q.desc;

	return pos;
}

/*
 * chcr_ktls_xmit_tcb_cpls: update tcb entry so that TP will create the header
 * with updated values like tcp seq, ack, window etc.
 * @tx_info - driver specific tls info.
 * @q - TX queue.
 * @tcp_seq
 * @tcp_ack
 * @tcp_win
 * return: NETDEV_TX_BUSY/NET_TX_OK.
 */
static int chcr_ktls_xmit_tcb_cpls(struct chcr_ktls_info *tx_info,
				   struct sge_eth_txq *q, u64 tcp_seq,
				   u64 tcp_ack, u64 tcp_win, bool offset)
{
	bool first_wr = ((tx_info->prev_ack == 0) && (tx_info->prev_win == 0));
	struct ch_ktls_port_stats_debug *port_stats;
	u32 len, cpl = 0, ndesc, wr_len, wr_mid = 0;
	struct fw_ulptx_wr *wr;
	int credits;
	void *pos;

	wr_len = sizeof(*wr);
	/* there can be max 4 cpls, check if we have enough credits */
	len = wr_len + 4 * roundup(CHCR_SET_TCB_FIELD_LEN, 16);
	ndesc = DIV_ROUND_UP(len, 64);

	credits = chcr_txq_avail(&q->q) - ndesc;
	if (unlikely(credits < 0)) {
		chcr_eth_txq_stop(q);
		return NETDEV_TX_BUSY;
	}

	if (unlikely(credits < ETHTXQ_STOP_THRES)) {
		chcr_eth_txq_stop(q);
		wr_mid |= FW_WR_EQUEQ_F | FW_WR_EQUIQ_F;
	}

	pos = &q->q.desc[q->q.pidx];
	/* make space for WR, we'll fill it later when we know all the cpls
	 * being sent out and have complete length.
	 */
	wr = pos;
	pos += wr_len;
	/* update tx_max if its a re-transmit or the first wr */
	if (first_wr || tcp_seq != tx_info->prev_seq) {
		pos = chcr_write_cpl_set_tcb_ulp(tx_info, q, tx_info->tid, pos,
						 TCB_TX_MAX_W,
						 TCB_TX_MAX_V(TCB_TX_MAX_M),
						 TCB_TX_MAX_V(tcp_seq), 0);
		cpl++;
	}
	/* reset snd una if it's a re-transmit pkt */
	if (tcp_seq != tx_info->prev_seq || offset) {
		/* reset snd_una */
		port_stats =
			&tx_info->adap->ch_ktls_stats.ktls_port[tx_info->port_id];
		pos = chcr_write_cpl_set_tcb_ulp(tx_info, q, tx_info->tid, pos,
						 TCB_SND_UNA_RAW_W,
						 TCB_SND_UNA_RAW_V
						 (TCB_SND_UNA_RAW_M),
						 TCB_SND_UNA_RAW_V(0), 0);
		if (tcp_seq != tx_info->prev_seq)
			atomic64_inc(&port_stats->ktls_tx_ooo);
		cpl++;
	}
	/* update ack */
	if (first_wr || tx_info->prev_ack != tcp_ack) {
		pos = chcr_write_cpl_set_tcb_ulp(tx_info, q, tx_info->tid, pos,
						 TCB_RCV_NXT_W,
						 TCB_RCV_NXT_V(TCB_RCV_NXT_M),
						 TCB_RCV_NXT_V(tcp_ack), 0);
		tx_info->prev_ack = tcp_ack;
		cpl++;
	}
	/* update receive window */
	if (first_wr || tx_info->prev_win != tcp_win) {
		chcr_write_cpl_set_tcb_ulp(tx_info, q, tx_info->tid, pos,
					   TCB_RCV_WND_W,
					   TCB_RCV_WND_V(TCB_RCV_WND_M),
					   TCB_RCV_WND_V(tcp_win), 0);
		tx_info->prev_win = tcp_win;
		cpl++;
	}

	if (cpl) {
		/* get the actual length */
		len = wr_len + cpl * roundup(CHCR_SET_TCB_FIELD_LEN, 16);
		/* ULPTX wr */
		wr->op_to_compl = htonl(FW_WR_OP_V(FW_ULPTX_WR));
		wr->cookie = 0;
		/* fill len in wr field */
		wr->flowid_len16 = htonl(wr_mid |
					 FW_WR_LEN16_V(DIV_ROUND_UP(len, 16)));

		ndesc = DIV_ROUND_UP(len, 64);
		chcr_txq_advance(&q->q, ndesc);
		cxgb4_ring_tx_db(tx_info->adap, &q->q, ndesc);
	}
	return 0;
}

/*
 * chcr_ktls_get_tx_flits
 * returns number of flits to be sent out, it includes key context length, WR
 * size and skb fragments.
 */
static unsigned int
chcr_ktls_get_tx_flits(u32 nr_frags, unsigned int key_ctx_len)
{
	return chcr_sgl_len(nr_frags) +
	       DIV_ROUND_UP(key_ctx_len + CHCR_KTLS_WR_SIZE, 8);
}

/*
 * chcr_ktls_check_tcp_options: To check if there is any TCP option available
 * other than timestamp.
 * @skb - skb contains partial record..
 * return: 1 / 0
 */
static int
chcr_ktls_check_tcp_options(struct tcphdr *tcp)
{
	int cnt, opt, optlen;
	u_char *cp;

	cp = (u_char *)(tcp + 1);
	cnt = (tcp->doff << 2) - sizeof(struct tcphdr);
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[0];
		if (opt == TCPOPT_EOL)
			break;
		if (opt == TCPOPT_NOP) {
			optlen = 1;
		} else {
			if (cnt < 2)
				break;
			optlen = cp[1];
			if (optlen < 2 || optlen > cnt)
				break;
		}
		switch (opt) {
		case TCPOPT_NOP:
			break;
		default:
			return 1;
		}
	}
	return 0;
}

/*
 * chcr_ktls_write_tcp_options : TP can't send out all the options, we need to
 * send out separately.
 * @tx_info - driver specific tls info.
 * @skb - skb contains partial record..
 * @q - TX queue.
 * @tx_chan - channel number.
 * return: NETDEV_TX_OK/NETDEV_TX_BUSY.
 */
static int
chcr_ktls_write_tcp_options(struct chcr_ktls_info *tx_info, struct sk_buff *skb,
			    struct sge_eth_txq *q, uint32_t tx_chan)
{
	struct fw_eth_tx_pkt_wr *wr;
	struct cpl_tx_pkt_core *cpl;
	u32 ctrl, iplen, maclen;
	struct ipv6hdr *ip6;
	unsigned int ndesc;
	struct tcphdr *tcp;
	int len16, pktlen;
	struct iphdr *ip;
	u32 wr_mid = 0;
	int credits;
	u8 buf[150];
	u64 cntrl1;
	void *pos;

	iplen = skb_network_header_len(skb);
	maclen = skb_mac_header_len(skb);

	/* packet length = eth hdr len + ip hdr len + tcp hdr len
	 * (including options).
	 */
	pktlen = skb_tcp_all_headers(skb);

	ctrl = sizeof(*cpl) + pktlen;
	len16 = DIV_ROUND_UP(sizeof(*wr) + ctrl, 16);
	/* check how many descriptors needed */
	ndesc = DIV_ROUND_UP(len16, 4);

	credits = chcr_txq_avail(&q->q) - ndesc;
	if (unlikely(credits < 0)) {
		chcr_eth_txq_stop(q);
		return NETDEV_TX_BUSY;
	}

	if (unlikely(credits < ETHTXQ_STOP_THRES)) {
		chcr_eth_txq_stop(q);
		wr_mid |= FW_WR_EQUEQ_F | FW_WR_EQUIQ_F;
	}

	pos = &q->q.desc[q->q.pidx];
	wr = pos;

	/* Firmware work request header */
	wr->op_immdlen = htonl(FW_WR_OP_V(FW_ETH_TX_PKT_WR) |
			       FW_WR_IMMDLEN_V(ctrl));

	wr->equiq_to_len16 = htonl(wr_mid | FW_WR_LEN16_V(len16));
	wr->r3 = 0;

	cpl = (void *)(wr + 1);

	/* CPL header */
	cpl->ctrl0 = htonl(TXPKT_OPCODE_V(CPL_TX_PKT) | TXPKT_INTF_V(tx_chan) |
			   TXPKT_PF_V(tx_info->adap->pf));
	cpl->pack = 0;
	cpl->len = htons(pktlen);

	memcpy(buf, skb->data, pktlen);
	if (!IS_ENABLED(CONFIG_IPV6) || tx_info->ip_family == AF_INET) {
		/* we need to correct ip header len */
		ip = (struct iphdr *)(buf + maclen);
		ip->tot_len = htons(pktlen - maclen);
		cntrl1 = TXPKT_CSUM_TYPE_V(TX_CSUM_TCPIP);
	} else {
		ip6 = (struct ipv6hdr *)(buf + maclen);
		ip6->payload_len = htons(pktlen - maclen - iplen);
		cntrl1 = TXPKT_CSUM_TYPE_V(TX_CSUM_TCPIP6);
	}

	cntrl1 |= T6_TXPKT_ETHHDR_LEN_V(maclen - ETH_HLEN) |
		  TXPKT_IPHDR_LEN_V(iplen);
	/* checksum offload */
	cpl->ctrl1 = cpu_to_be64(cntrl1);

	pos = cpl + 1;

	/* now take care of the tcp header, if fin is not set then clear push
	 * bit as well, and if fin is set, it will be sent at the last so we
	 * need to update the tcp sequence number as per the last packet.
	 */
	tcp = (struct tcphdr *)(buf + maclen + iplen);

	if (!tcp->fin)
		tcp->psh = 0;
	else
		tcp->seq = htonl(tx_info->prev_seq);

	chcr_copy_to_txd(buf, &q->q, pos, pktlen);

	chcr_txq_advance(&q->q, ndesc);
	cxgb4_ring_tx_db(tx_info->adap, &q->q, ndesc);
	return 0;
}

/*
 * chcr_ktls_xmit_wr_complete: This sends out the complete record. If an skb
 * received has partial end part of the record, send out the complete record, so
 * that crypto block will be able to generate TAG/HASH.
 * @skb - segment which has complete or partial end part.
 * @tx_info - driver specific tls info.
 * @q - TX queue.
 * @tcp_seq
 * @tcp_push - tcp push bit.
 * @mss - segment size.
 * return: NETDEV_TX_BUSY/NET_TX_OK.
 */
static int chcr_ktls_xmit_wr_complete(struct sk_buff *skb,
				      struct chcr_ktls_info *tx_info,
				      struct sge_eth_txq *q, u32 tcp_seq,
				      bool is_last_wr, u32 data_len,
				      u32 skb_offset, u32 nfrags,
				      bool tcp_push, u32 mss)
{
	u32 len16, wr_mid = 0, flits = 0, ndesc, cipher_start;
	struct adapter *adap = tx_info->adap;
	int credits, left, last_desc;
	struct tx_sw_desc *sgl_sdesc;
	struct cpl_tx_data *tx_data;
	struct cpl_tx_sec_pdu *cpl;
	struct ulptx_idata *idata;
	struct ulp_txpkt *ulptx;
	struct fw_ulptx_wr *wr;
	void *pos;
	u64 *end;

	/* get the number of flits required */
	flits = chcr_ktls_get_tx_flits(nfrags, tx_info->key_ctx_len);
	/* number of descriptors */
	ndesc = chcr_flits_to_desc(flits);
	/* check if enough credits available */
	credits = chcr_txq_avail(&q->q) - ndesc;
	if (unlikely(credits < 0)) {
		chcr_eth_txq_stop(q);
		return NETDEV_TX_BUSY;
	}

	if (unlikely(credits < ETHTXQ_STOP_THRES)) {
		/* Credits are below the threshold values, stop the queue after
		 * injecting the Work Request for this packet.
		 */
		chcr_eth_txq_stop(q);
		wr_mid |= FW_WR_EQUEQ_F | FW_WR_EQUIQ_F;
	}

	last_desc = q->q.pidx + ndesc - 1;
	if (last_desc >= q->q.size)
		last_desc -= q->q.size;
	sgl_sdesc = &q->q.sdesc[last_desc];

	if (unlikely(cxgb4_map_skb(adap->pdev_dev, skb, sgl_sdesc->addr) < 0)) {
		memset(sgl_sdesc->addr, 0, sizeof(sgl_sdesc->addr));
		q->mapping_err++;
		return NETDEV_TX_BUSY;
	}

	if (!is_last_wr)
		skb_get(skb);

	pos = &q->q.desc[q->q.pidx];
	end = (u64 *)pos + flits;
	/* FW_ULPTX_WR */
	wr = pos;
	/* WR will need len16 */
	len16 = DIV_ROUND_UP(flits, 2);
	wr->op_to_compl = htonl(FW_WR_OP_V(FW_ULPTX_WR));
	wr->flowid_len16 = htonl(wr_mid | FW_WR_LEN16_V(len16));
	wr->cookie = 0;
	pos += sizeof(*wr);
	/* ULP_TXPKT */
	ulptx = pos;
	ulptx->cmd_dest = htonl(ULPTX_CMD_V(ULP_TX_PKT) |
				ULP_TXPKT_CHANNELID_V(tx_info->port_id) |
				ULP_TXPKT_FID_V(q->q.cntxt_id) |
				ULP_TXPKT_RO_F);
	ulptx->len = htonl(len16 - 1);
	/* ULPTX_IDATA sub-command */
	idata = (struct ulptx_idata *)(ulptx + 1);
	idata->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_IMM) | ULP_TX_SC_MORE_F);
	/* idata length will include cpl_tx_sec_pdu + key context size +
	 * cpl_tx_data header.
	 */
	idata->len = htonl(sizeof(*cpl) + tx_info->key_ctx_len +
			   sizeof(*tx_data));
	/* SEC CPL */
	cpl = (struct cpl_tx_sec_pdu *)(idata + 1);
	cpl->op_ivinsrtofst =
		htonl(CPL_TX_SEC_PDU_OPCODE_V(CPL_TX_SEC_PDU) |
		      CPL_TX_SEC_PDU_CPLLEN_V(CHCR_CPL_TX_SEC_PDU_LEN_64BIT) |
		      CPL_TX_SEC_PDU_PLACEHOLDER_V(1) |
		      CPL_TX_SEC_PDU_IVINSRTOFST_V(TLS_HEADER_SIZE + 1));
	cpl->pldlen = htonl(data_len);

	/* encryption should start after tls header size + iv size */
	cipher_start = TLS_HEADER_SIZE + tx_info->iv_size + 1;

	cpl->aadstart_cipherstop_hi =
		htonl(CPL_TX_SEC_PDU_AADSTART_V(1) |
		      CPL_TX_SEC_PDU_AADSTOP_V(TLS_HEADER_SIZE) |
		      CPL_TX_SEC_PDU_CIPHERSTART_V(cipher_start));

	/* authentication will also start after tls header + iv size */
	cpl->cipherstop_lo_authinsert =
	htonl(CPL_TX_SEC_PDU_AUTHSTART_V(cipher_start) |
	      CPL_TX_SEC_PDU_AUTHSTOP_V(TLS_CIPHER_AES_GCM_128_TAG_SIZE) |
	      CPL_TX_SEC_PDU_AUTHINSERT_V(TLS_CIPHER_AES_GCM_128_TAG_SIZE));

	/* These two flits are actually a CPL_TLS_TX_SCMD_FMT. */
	cpl->seqno_numivs = htonl(tx_info->scmd0_seqno_numivs);
	cpl->ivgen_hdrlen = htonl(tx_info->scmd0_ivgen_hdrlen);
	cpl->scmd1 = cpu_to_be64(tx_info->record_no);

	pos = cpl + 1;
	/* check if space left to fill the keys */
	left = (void *)q->q.stat - pos;
	if (!left) {
		left = (void *)end - (void *)q->q.stat;
		pos = q->q.desc;
		end = pos + left;
	}

	pos = chcr_copy_to_txd(&tx_info->key_ctx, &q->q, pos,
			       tx_info->key_ctx_len);
	left = (void *)q->q.stat - pos;

	if (!left) {
		left = (void *)end - (void *)q->q.stat;
		pos = q->q.desc;
		end = pos + left;
	}
	/* CPL_TX_DATA */
	tx_data = (void *)pos;
	OPCODE_TID(tx_data) = htonl(MK_OPCODE_TID(CPL_TX_DATA, tx_info->tid));
	tx_data->len = htonl(TX_DATA_MSS_V(mss) | TX_LENGTH_V(data_len));

	tx_data->rsvd = htonl(tcp_seq);

	tx_data->flags = htonl(TX_BYPASS_F);
	if (tcp_push)
		tx_data->flags |= htonl(TX_PUSH_F | TX_SHOVE_F);

	/* check left again, it might go beyond queue limit */
	pos = tx_data + 1;
	left = (void *)q->q.stat - pos;

	/* check the position again */
	if (!left) {
		left = (void *)end - (void *)q->q.stat;
		pos = q->q.desc;
		end = pos + left;
	}

	/* send the complete packet except the header */
	cxgb4_write_partial_sgl(skb, &q->q, pos, end, sgl_sdesc->addr,
				skb_offset, data_len);
	sgl_sdesc->skb = skb;

	chcr_txq_advance(&q->q, ndesc);
	cxgb4_ring_tx_db(adap, &q->q, ndesc);
	atomic64_inc(&adap->ch_ktls_stats.ktls_tx_send_records);

	return 0;
}

/*
 * chcr_ktls_xmit_wr_short: This is to send out partial records. If its
 * a middle part of a record, fetch the prior data to make it 16 byte aligned
 * and then only send it out.
 *
 * @skb - skb contains partial record..
 * @tx_info - driver specific tls info.
 * @q - TX queue.
 * @tcp_seq
 * @tcp_push - tcp push bit.
 * @mss - segment size.
 * @tls_rec_offset - offset from start of the tls record.
 * @perior_data - data before the current segment, required to make this record
 *		  16 byte aligned.
 * @prior_data_len - prior_data length (less than 16)
 * return: NETDEV_TX_BUSY/NET_TX_OK.
 */
static int chcr_ktls_xmit_wr_short(struct sk_buff *skb,
				   struct chcr_ktls_info *tx_info,
				   struct sge_eth_txq *q,
				   u32 tcp_seq, bool tcp_push, u32 mss,
				   u32 tls_rec_offset, u8 *prior_data,
				   u32 prior_data_len, u32 data_len,
				   u32 skb_offset)
{
	u32 len16, wr_mid = 0, cipher_start, nfrags;
	struct adapter *adap = tx_info->adap;
	unsigned int flits = 0, ndesc;
	int credits, left, last_desc;
	struct tx_sw_desc *sgl_sdesc;
	struct cpl_tx_data *tx_data;
	struct cpl_tx_sec_pdu *cpl;
	struct ulptx_idata *idata;
	struct ulp_txpkt *ulptx;
	struct fw_ulptx_wr *wr;
	__be64 iv_record;
	void *pos;
	u64 *end;

	nfrags = chcr_get_nfrags_to_send(skb, skb_offset, data_len);
	/* get the number of flits required, it's a partial record so 2 flits
	 * (AES_BLOCK_SIZE) will be added.
	 */
	flits = chcr_ktls_get_tx_flits(nfrags, tx_info->key_ctx_len) + 2;
	/* get the correct 8 byte IV of this record */
	iv_record = cpu_to_be64(tx_info->iv + tx_info->record_no);
	/* If it's a middle record and not 16 byte aligned to run AES CTR, need
	 * to make it 16 byte aligned. So atleadt 2 extra flits of immediate
	 * data will be added.
	 */
	if (prior_data_len)
		flits += 2;
	/* number of descriptors */
	ndesc = chcr_flits_to_desc(flits);
	/* check if enough credits available */
	credits = chcr_txq_avail(&q->q) - ndesc;
	if (unlikely(credits < 0)) {
		chcr_eth_txq_stop(q);
		return NETDEV_TX_BUSY;
	}

	if (unlikely(credits < ETHTXQ_STOP_THRES)) {
		chcr_eth_txq_stop(q);
		wr_mid |= FW_WR_EQUEQ_F | FW_WR_EQUIQ_F;
	}

	last_desc = q->q.pidx + ndesc - 1;
	if (last_desc >= q->q.size)
		last_desc -= q->q.size;
	sgl_sdesc = &q->q.sdesc[last_desc];

	if (unlikely(cxgb4_map_skb(adap->pdev_dev, skb, sgl_sdesc->addr) < 0)) {
		memset(sgl_sdesc->addr, 0, sizeof(sgl_sdesc->addr));
		q->mapping_err++;
		return NETDEV_TX_BUSY;
	}

	pos = &q->q.desc[q->q.pidx];
	end = (u64 *)pos + flits;
	/* FW_ULPTX_WR */
	wr = pos;
	/* WR will need len16 */
	len16 = DIV_ROUND_UP(flits, 2);
	wr->op_to_compl = htonl(FW_WR_OP_V(FW_ULPTX_WR));
	wr->flowid_len16 = htonl(wr_mid | FW_WR_LEN16_V(len16));
	wr->cookie = 0;
	pos += sizeof(*wr);
	/* ULP_TXPKT */
	ulptx = pos;
	ulptx->cmd_dest = htonl(ULPTX_CMD_V(ULP_TX_PKT) |
				ULP_TXPKT_CHANNELID_V(tx_info->port_id) |
				ULP_TXPKT_FID_V(q->q.cntxt_id) |
				ULP_TXPKT_RO_F);
	ulptx->len = htonl(len16 - 1);
	/* ULPTX_IDATA sub-command */
	idata = (struct ulptx_idata *)(ulptx + 1);
	idata->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_IMM) | ULP_TX_SC_MORE_F);
	/* idata length will include cpl_tx_sec_pdu + key context size +
	 * cpl_tx_data header.
	 */
	idata->len = htonl(sizeof(*cpl) + tx_info->key_ctx_len +
			   sizeof(*tx_data) + AES_BLOCK_LEN + prior_data_len);
	/* SEC CPL */
	cpl = (struct cpl_tx_sec_pdu *)(idata + 1);
	/* cipher start will have tls header + iv size extra if its a header
	 * part of tls record. else only 16 byte IV will be added.
	 */
	cipher_start =
		AES_BLOCK_LEN + 1 +
		(!tls_rec_offset ? TLS_HEADER_SIZE + tx_info->iv_size : 0);

	cpl->op_ivinsrtofst =
		htonl(CPL_TX_SEC_PDU_OPCODE_V(CPL_TX_SEC_PDU) |
		      CPL_TX_SEC_PDU_CPLLEN_V(CHCR_CPL_TX_SEC_PDU_LEN_64BIT) |
		      CPL_TX_SEC_PDU_IVINSRTOFST_V(1));
	cpl->pldlen = htonl(data_len + AES_BLOCK_LEN + prior_data_len);
	cpl->aadstart_cipherstop_hi =
		htonl(CPL_TX_SEC_PDU_CIPHERSTART_V(cipher_start));
	cpl->cipherstop_lo_authinsert = 0;
	/* These two flits are actually a CPL_TLS_TX_SCMD_FMT. */
	cpl->seqno_numivs = htonl(tx_info->scmd0_short_seqno_numivs);
	cpl->ivgen_hdrlen = htonl(tx_info->scmd0_short_ivgen_hdrlen);
	cpl->scmd1 = 0;

	pos = cpl + 1;
	/* check if space left to fill the keys */
	left = (void *)q->q.stat - pos;
	if (!left) {
		left = (void *)end - (void *)q->q.stat;
		pos = q->q.desc;
		end = pos + left;
	}

	pos = chcr_copy_to_txd(&tx_info->key_ctx, &q->q, pos,
			       tx_info->key_ctx_len);
	left = (void *)q->q.stat - pos;

	if (!left) {
		left = (void *)end - (void *)q->q.stat;
		pos = q->q.desc;
		end = pos + left;
	}
	/* CPL_TX_DATA */
	tx_data = (void *)pos;
	OPCODE_TID(tx_data) = htonl(MK_OPCODE_TID(CPL_TX_DATA, tx_info->tid));
	tx_data->len = htonl(TX_DATA_MSS_V(mss) |
			     TX_LENGTH_V(data_len + prior_data_len));
	tx_data->rsvd = htonl(tcp_seq);
	tx_data->flags = htonl(TX_BYPASS_F);
	if (tcp_push)
		tx_data->flags |= htonl(TX_PUSH_F | TX_SHOVE_F);

	/* check left again, it might go beyond queue limit */
	pos = tx_data + 1;
	left = (void *)q->q.stat - pos;

	/* check the position again */
	if (!left) {
		left = (void *)end - (void *)q->q.stat;
		pos = q->q.desc;
		end = pos + left;
	}
	/* copy the 16 byte IV for AES-CTR, which includes 4 bytes of salt, 8
	 * bytes of actual IV and 4 bytes of 16 byte-sequence.
	 */
	memcpy(pos, tx_info->key_ctx.salt, tx_info->salt_size);
	memcpy(pos + tx_info->salt_size, &iv_record, tx_info->iv_size);
	*(__be32 *)(pos + tx_info->salt_size + tx_info->iv_size) =
		htonl(2 + (tls_rec_offset ? ((tls_rec_offset -
		(TLS_HEADER_SIZE + tx_info->iv_size)) / AES_BLOCK_LEN) : 0));

	pos += 16;
	/* Prior_data_len will always be less than 16 bytes, fill the
	 * prio_data_len after AES_CTRL_BLOCK and clear the remaining length
	 * to 0.
	 */
	if (prior_data_len)
		pos = chcr_copy_to_txd(prior_data, &q->q, pos, 16);
	/* send the complete packet except the header */
	cxgb4_write_partial_sgl(skb, &q->q, pos, end, sgl_sdesc->addr,
				skb_offset, data_len);
	sgl_sdesc->skb = skb;

	chcr_txq_advance(&q->q, ndesc);
	cxgb4_ring_tx_db(adap, &q->q, ndesc);

	return 0;
}

/*
 * chcr_ktls_tx_plaintxt: This handler will take care of the records which has
 * only plain text (only tls header and iv)
 * @tx_info - driver specific tls info.
 * @skb - skb contains partial record..
 * @tcp_seq
 * @mss - segment size.
 * @tcp_push - tcp push bit.
 * @q - TX queue.
 * @port_id : port number
 * @perior_data - data before the current segment, required to make this record
 *		 16 byte aligned.
 * @prior_data_len - prior_data length (less than 16)
 * return: NETDEV_TX_BUSY/NET_TX_OK.
 */
static int chcr_ktls_tx_plaintxt(struct chcr_ktls_info *tx_info,
				 struct sk_buff *skb, u32 tcp_seq, u32 mss,
				 bool tcp_push, struct sge_eth_txq *q,
				 u32 port_id, u8 *prior_data,
				 u32 data_len, u32 skb_offset,
				 u32 prior_data_len)
{
	int credits, left, len16, last_desc;
	unsigned int flits = 0, ndesc;
	struct tx_sw_desc *sgl_sdesc;
	struct cpl_tx_data *tx_data;
	struct ulptx_idata *idata;
	struct ulp_txpkt *ulptx;
	struct fw_ulptx_wr *wr;
	u32 wr_mid = 0, nfrags;
	void *pos;
	u64 *end;

	flits = DIV_ROUND_UP(CHCR_PLAIN_TX_DATA_LEN, 8);
	nfrags = chcr_get_nfrags_to_send(skb, skb_offset, data_len);
	flits += chcr_sgl_len(nfrags);
	if (prior_data_len)
		flits += 2;

	/* WR will need len16 */
	len16 = DIV_ROUND_UP(flits, 2);
	/* check how many descriptors needed */
	ndesc = DIV_ROUND_UP(flits, 8);

	credits = chcr_txq_avail(&q->q) - ndesc;
	if (unlikely(credits < 0)) {
		chcr_eth_txq_stop(q);
		return NETDEV_TX_BUSY;
	}

	if (unlikely(credits < ETHTXQ_STOP_THRES)) {
		chcr_eth_txq_stop(q);
		wr_mid |= FW_WR_EQUEQ_F | FW_WR_EQUIQ_F;
	}

	last_desc = q->q.pidx + ndesc - 1;
	if (last_desc >= q->q.size)
		last_desc -= q->q.size;
	sgl_sdesc = &q->q.sdesc[last_desc];

	if (unlikely(cxgb4_map_skb(tx_info->adap->pdev_dev, skb,
				   sgl_sdesc->addr) < 0)) {
		memset(sgl_sdesc->addr, 0, sizeof(sgl_sdesc->addr));
		q->mapping_err++;
		return NETDEV_TX_BUSY;
	}

	pos = &q->q.desc[q->q.pidx];
	end = (u64 *)pos + flits;
	/* FW_ULPTX_WR */
	wr = pos;
	wr->op_to_compl = htonl(FW_WR_OP_V(FW_ULPTX_WR));
	wr->flowid_len16 = htonl(wr_mid | FW_WR_LEN16_V(len16));
	wr->cookie = 0;
	/* ULP_TXPKT */
	ulptx = (struct ulp_txpkt *)(wr + 1);
	ulptx->cmd_dest = htonl(ULPTX_CMD_V(ULP_TX_PKT) |
			ULP_TXPKT_DATAMODIFY_V(0) |
			ULP_TXPKT_CHANNELID_V(tx_info->port_id) |
			ULP_TXPKT_DEST_V(0) |
			ULP_TXPKT_FID_V(q->q.cntxt_id) | ULP_TXPKT_RO_V(1));
	ulptx->len = htonl(len16 - 1);
	/* ULPTX_IDATA sub-command */
	idata = (struct ulptx_idata *)(ulptx + 1);
	idata->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_IMM) | ULP_TX_SC_MORE_F);
	idata->len = htonl(sizeof(*tx_data) + prior_data_len);
	/* CPL_TX_DATA */
	tx_data = (struct cpl_tx_data *)(idata + 1);
	OPCODE_TID(tx_data) = htonl(MK_OPCODE_TID(CPL_TX_DATA, tx_info->tid));
	tx_data->len = htonl(TX_DATA_MSS_V(mss) |
			     TX_LENGTH_V(data_len + prior_data_len));
	/* set tcp seq number */
	tx_data->rsvd = htonl(tcp_seq);
	tx_data->flags = htonl(TX_BYPASS_F);
	if (tcp_push)
		tx_data->flags |= htonl(TX_PUSH_F | TX_SHOVE_F);

	pos = tx_data + 1;
	/* apart from prior_data_len, we should set remaining part of 16 bytes
	 * to be zero.
	 */
	if (prior_data_len)
		pos = chcr_copy_to_txd(prior_data, &q->q, pos, 16);

	/* check left again, it might go beyond queue limit */
	left = (void *)q->q.stat - pos;

	/* check the position again */
	if (!left) {
		left = (void *)end - (void *)q->q.stat;
		pos = q->q.desc;
		end = pos + left;
	}
	/* send the complete packet including the header */
	cxgb4_write_partial_sgl(skb, &q->q, pos, end, sgl_sdesc->addr,
				skb_offset, data_len);
	sgl_sdesc->skb = skb;

	chcr_txq_advance(&q->q, ndesc);
	cxgb4_ring_tx_db(tx_info->adap, &q->q, ndesc);
	return 0;
}

static int chcr_ktls_tunnel_pkt(struct chcr_ktls_info *tx_info,
				struct sk_buff *skb,
				struct sge_eth_txq *q)
{
	u32 ctrl, iplen, maclen, wr_mid = 0, len16;
	struct tx_sw_desc *sgl_sdesc;
	struct fw_eth_tx_pkt_wr *wr;
	struct cpl_tx_pkt_core *cpl;
	unsigned int flits, ndesc;
	int credits, last_desc;
	u64 cntrl1, *end;
	void *pos;

	ctrl = sizeof(*cpl);
	flits = DIV_ROUND_UP(sizeof(*wr) + ctrl, 8);

	flits += chcr_sgl_len(skb_shinfo(skb)->nr_frags + 1);
	len16 = DIV_ROUND_UP(flits, 2);
	/* check how many descriptors needed */
	ndesc = DIV_ROUND_UP(flits, 8);

	credits = chcr_txq_avail(&q->q) - ndesc;
	if (unlikely(credits < 0)) {
		chcr_eth_txq_stop(q);
		return -ENOMEM;
	}

	if (unlikely(credits < ETHTXQ_STOP_THRES)) {
		chcr_eth_txq_stop(q);
		wr_mid |= FW_WR_EQUEQ_F | FW_WR_EQUIQ_F;
	}

	last_desc = q->q.pidx + ndesc - 1;
	if (last_desc >= q->q.size)
		last_desc -= q->q.size;
	sgl_sdesc = &q->q.sdesc[last_desc];

	if (unlikely(cxgb4_map_skb(tx_info->adap->pdev_dev, skb,
				   sgl_sdesc->addr) < 0)) {
		memset(sgl_sdesc->addr, 0, sizeof(sgl_sdesc->addr));
		q->mapping_err++;
		return -ENOMEM;
	}

	iplen = skb_network_header_len(skb);
	maclen = skb_mac_header_len(skb);

	pos = &q->q.desc[q->q.pidx];
	end = (u64 *)pos + flits;
	wr = pos;

	/* Firmware work request header */
	wr->op_immdlen = htonl(FW_WR_OP_V(FW_ETH_TX_PKT_WR) |
			       FW_WR_IMMDLEN_V(ctrl));

	wr->equiq_to_len16 = htonl(wr_mid | FW_WR_LEN16_V(len16));
	wr->r3 = 0;

	cpl = (void *)(wr + 1);

	/* CPL header */
	cpl->ctrl0 = htonl(TXPKT_OPCODE_V(CPL_TX_PKT) |
			   TXPKT_INTF_V(tx_info->tx_chan) |
			   TXPKT_PF_V(tx_info->adap->pf));
	cpl->pack = 0;
	cntrl1 = TXPKT_CSUM_TYPE_V(tx_info->ip_family == AF_INET ?
				   TX_CSUM_TCPIP : TX_CSUM_TCPIP6);
	cntrl1 |= T6_TXPKT_ETHHDR_LEN_V(maclen - ETH_HLEN) |
		  TXPKT_IPHDR_LEN_V(iplen);
	/* checksum offload */
	cpl->ctrl1 = cpu_to_be64(cntrl1);
	cpl->len = htons(skb->len);

	pos = cpl + 1;

	cxgb4_write_sgl(skb, &q->q, pos, end, 0, sgl_sdesc->addr);
	sgl_sdesc->skb = skb;
	chcr_txq_advance(&q->q, ndesc);
	cxgb4_ring_tx_db(tx_info->adap, &q->q, ndesc);
	return 0;
}

/*
 * chcr_ktls_copy_record_in_skb
 * @nskb - new skb where the frags to be added.
 * @skb - old skb, to copy socket and destructor details.
 * @record - specific record which has complete 16k record in frags.
 */
static void chcr_ktls_copy_record_in_skb(struct sk_buff *nskb,
					 struct sk_buff *skb,
					 struct tls_record_info *record)
{
	int i = 0;

	for (i = 0; i < record->num_frags; i++) {
		skb_shinfo(nskb)->frags[i] = record->frags[i];
		/* increase the frag ref count */
		__skb_frag_ref(&skb_shinfo(nskb)->frags[i]);
	}

	skb_shinfo(nskb)->nr_frags = record->num_frags;
	nskb->data_len = record->len;
	nskb->len += record->len;
	nskb->truesize += record->len;
	nskb->sk = skb->sk;
	nskb->destructor = skb->destructor;
	refcount_add(nskb->truesize, &nskb->sk->sk_wmem_alloc);
}

/*
 * chcr_end_part_handler: This handler will handle the record which
 * is complete or if record's end part is received. T6 adapter has a issue that
 * it can't send out TAG with partial record so if its an end part then we have
 * to send TAG as well and for which we need to fetch the complete record and
 * send it to crypto module.
 * @tx_info - driver specific tls info.
 * @skb - skb contains partial record.
 * @record - complete record of 16K size.
 * @tcp_seq
 * @mss - segment size in which TP needs to chop a packet.
 * @tcp_push_no_fin - tcp push if fin is not set.
 * @q - TX queue.
 * @tls_end_offset - offset from end of the record.
 * @last wr : check if this is the last part of the skb going out.
 * return: NETDEV_TX_OK/NETDEV_TX_BUSY.
 */
static int chcr_end_part_handler(struct chcr_ktls_info *tx_info,
				 struct sk_buff *skb,
				 struct tls_record_info *record,
				 u32 tcp_seq, int mss, bool tcp_push_no_fin,
				 struct sge_eth_txq *q, u32 skb_offset,
				 u32 tls_end_offset, bool last_wr)
{
	bool free_skb_if_tx_fails = false;
	struct sk_buff *nskb = NULL;

	/* check if it is a complete record */
	if (tls_end_offset == record->len) {
		nskb = skb;
		atomic64_inc(&tx_info->adap->ch_ktls_stats.ktls_tx_complete_pkts);
	} else {
		nskb = alloc_skb(0, GFP_ATOMIC);
		if (!nskb) {
			dev_kfree_skb_any(skb);
			return NETDEV_TX_BUSY;
		}

		/* copy complete record in skb */
		chcr_ktls_copy_record_in_skb(nskb, skb, record);
		/* packet is being sent from the beginning, update the tcp_seq
		 * accordingly.
		 */
		tcp_seq = tls_record_start_seq(record);
		/* reset skb offset */
		skb_offset = 0;

		if (last_wr)
			dev_kfree_skb_any(skb);
		else
			free_skb_if_tx_fails = true;

		last_wr = true;

		atomic64_inc(&tx_info->adap->ch_ktls_stats.ktls_tx_end_pkts);
	}

	if (chcr_ktls_xmit_wr_complete(nskb, tx_info, q, tcp_seq,
				       last_wr, record->len, skb_offset,
				       record->num_frags,
				       (last_wr && tcp_push_no_fin),
				       mss)) {
		if (free_skb_if_tx_fails)
			dev_kfree_skb_any(skb);
		goto out;
	}
	tx_info->prev_seq = record->end_seq;
	return 0;
out:
	dev_kfree_skb_any(nskb);
	return NETDEV_TX_BUSY;
}

/*
 * chcr_short_record_handler: This handler will take care of the records which
 * doesn't have end part (1st part or the middle part(/s) of a record). In such
 * cases, AES CTR will be used in place of AES GCM to send out partial packet.
 * This partial record might be the first part of the record, or the middle
 * part. In case of middle record we should fetch the prior data to make it 16
 * byte aligned. If it has a partial tls header or iv then get to the start of
 * tls header. And if it has partial TAG, then remove the complete TAG and send
 * only the payload.
 * There is one more possibility that it gets a partial header, send that
 * portion as a plaintext.
 * @tx_info - driver specific tls info.
 * @skb - skb contains partial record..
 * @record - complete record of 16K size.
 * @tcp_seq
 * @mss - segment size in which TP needs to chop a packet.
 * @tcp_push_no_fin - tcp push if fin is not set.
 * @q - TX queue.
 * @tls_end_offset - offset from end of the record.
 * return: NETDEV_TX_OK/NETDEV_TX_BUSY.
 */
static int chcr_short_record_handler(struct chcr_ktls_info *tx_info,
				     struct sk_buff *skb,
				     struct tls_record_info *record,
				     u32 tcp_seq, int mss, bool tcp_push_no_fin,
				     u32 data_len, u32 skb_offset,
				     struct sge_eth_txq *q, u32 tls_end_offset)
{
	u32 tls_rec_offset = tcp_seq - tls_record_start_seq(record);
	u8 prior_data[16] = {0};
	u32 prior_data_len = 0;

	/* check if the skb is ending in middle of tag/HASH, its a big
	 * trouble, send the packet before the HASH.
	 */
	int remaining_record = tls_end_offset - data_len;

	if (remaining_record > 0 &&
	    remaining_record < TLS_CIPHER_AES_GCM_128_TAG_SIZE) {
		int trimmed_len = 0;

		if (tls_end_offset > TLS_CIPHER_AES_GCM_128_TAG_SIZE)
			trimmed_len = data_len -
				      (TLS_CIPHER_AES_GCM_128_TAG_SIZE -
				       remaining_record);
		if (!trimmed_len)
			return FALLBACK;

		WARN_ON(trimmed_len > data_len);

		data_len = trimmed_len;
		atomic64_inc(&tx_info->adap->ch_ktls_stats.ktls_tx_trimmed_pkts);
	}

	/* check if it is only the header part. */
	if (tls_rec_offset + data_len <= (TLS_HEADER_SIZE + tx_info->iv_size)) {
		if (chcr_ktls_tx_plaintxt(tx_info, skb, tcp_seq, mss,
					  tcp_push_no_fin, q,
					  tx_info->port_id, prior_data,
					  data_len, skb_offset, prior_data_len))
			goto out;

		tx_info->prev_seq = tcp_seq + data_len;
		return 0;
	}

	/* check if the middle record's start point is 16 byte aligned. CTR
	 * needs 16 byte aligned start point to start encryption.
	 */
	if (tls_rec_offset) {
		/* there is an offset from start, means its a middle record */
		int remaining = 0;

		if (tls_rec_offset < (TLS_HEADER_SIZE + tx_info->iv_size)) {
			prior_data_len = tls_rec_offset;
			tls_rec_offset = 0;
			remaining = 0;
		} else {
			prior_data_len =
				(tls_rec_offset -
				(TLS_HEADER_SIZE + tx_info->iv_size))
				% AES_BLOCK_LEN;
			remaining = tls_rec_offset - prior_data_len;
		}

		/* if prior_data_len is not zero, means we need to fetch prior
		 * data to make this record 16 byte aligned, or we need to reach
		 * to start offset.
		 */
		if (prior_data_len) {
			int i = 0;
			skb_frag_t *f;
			int frag_size = 0, frag_delta = 0;

			while (remaining > 0) {
				frag_size = skb_frag_size(&record->frags[i]);
				if (remaining < frag_size)
					break;

				remaining -= frag_size;
				i++;
			}
			f = &record->frags[i];
			frag_delta = skb_frag_size(f) - remaining;

			if (frag_delta >= prior_data_len) {
				memcpy_from_page(prior_data, skb_frag_page(f),
						 skb_frag_off(f) + remaining,
						 prior_data_len);
			} else {
				memcpy_from_page(prior_data, skb_frag_page(f),
						 skb_frag_off(f) + remaining,
						 frag_delta);

				/* get the next page */
				f = &record->frags[i + 1];

				memcpy_from_page(prior_data + frag_delta,
						 skb_frag_page(f),
						 skb_frag_off(f),
						 prior_data_len - frag_delta);
			}
			/* reset tcp_seq as per the prior_data_required len */
			tcp_seq -= prior_data_len;
		}
		atomic64_inc(&tx_info->adap->ch_ktls_stats.ktls_tx_middle_pkts);
	} else {
		atomic64_inc(&tx_info->adap->ch_ktls_stats.ktls_tx_start_pkts);
	}

	if (chcr_ktls_xmit_wr_short(skb, tx_info, q, tcp_seq, tcp_push_no_fin,
				    mss, tls_rec_offset, prior_data,
				    prior_data_len, data_len, skb_offset)) {
		goto out;
	}

	tx_info->prev_seq = tcp_seq + data_len + prior_data_len;
	return 0;
out:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_BUSY;
}

static int chcr_ktls_sw_fallback(struct sk_buff *skb,
				 struct chcr_ktls_info *tx_info,
				 struct sge_eth_txq *q)
{
	u32 data_len, skb_offset;
	struct sk_buff *nskb;
	struct tcphdr *th;

	nskb = tls_encrypt_skb(skb);

	if (!nskb)
		return 0;

	th = tcp_hdr(nskb);
	skb_offset = skb_tcp_all_headers(nskb);
	data_len = nskb->len - skb_offset;
	skb_tx_timestamp(nskb);

	if (chcr_ktls_tunnel_pkt(tx_info, nskb, q))
		goto out;

	tx_info->prev_seq = ntohl(th->seq) + data_len;
	atomic64_inc(&tx_info->adap->ch_ktls_stats.ktls_tx_fallback);
	return 0;
out:
	dev_kfree_skb_any(nskb);
	return 0;
}
/* nic tls TX handler */
static int chcr_ktls_xmit(struct sk_buff *skb, struct net_device *dev)
{
	u32 tls_end_offset, tcp_seq, skb_data_len, skb_offset;
	struct ch_ktls_port_stats_debug *port_stats;
	struct chcr_ktls_ofld_ctx_tx *tx_ctx;
	struct ch_ktls_stats_debug *stats;
	struct tcphdr *th = tcp_hdr(skb);
	int data_len, qidx, ret = 0, mss;
	struct tls_record_info *record;
	struct chcr_ktls_info *tx_info;
	struct net_device *tls_netdev;
	struct tls_context *tls_ctx;
	struct sge_eth_txq *q;
	struct adapter *adap;
	unsigned long flags;

	tcp_seq = ntohl(th->seq);
	skb_offset = skb_tcp_all_headers(skb);
	skb_data_len = skb->len - skb_offset;
	data_len = skb_data_len;

	mss = skb_is_gso(skb) ? skb_shinfo(skb)->gso_size : data_len;

	tls_ctx = tls_get_ctx(skb->sk);
	tls_netdev = rcu_dereference_bh(tls_ctx->netdev);
	/* Don't quit on NULL: if tls_device_down is running in parallel,
	 * netdev might become NULL, even if tls_is_sk_tx_device_offloaded was
	 * true. Rather continue processing this packet.
	 */
	if (unlikely(tls_netdev && tls_netdev != dev))
		goto out;

	tx_ctx = chcr_get_ktls_tx_context(tls_ctx);
	tx_info = tx_ctx->chcr_info;

	if (unlikely(!tx_info))
		goto out;

	adap = tx_info->adap;
	stats = &adap->ch_ktls_stats;
	port_stats = &stats->ktls_port[tx_info->port_id];

	qidx = skb->queue_mapping;
	q = &adap->sge.ethtxq[qidx + tx_info->first_qset];
	cxgb4_reclaim_completed_tx(adap, &q->q, true);
	/* if tcp options are set but finish is not send the options first */
	if (!th->fin && chcr_ktls_check_tcp_options(th)) {
		ret = chcr_ktls_write_tcp_options(tx_info, skb, q,
						  tx_info->tx_chan);
		if (ret)
			return NETDEV_TX_BUSY;
	}

	/* TCP segments can be in received either complete or partial.
	 * chcr_end_part_handler will handle cases if complete record or end
	 * part of the record is received. In case of partial end part of record,
	 * we will send the complete record again.
	 */

	spin_lock_irqsave(&tx_ctx->base.lock, flags);

	do {

		cxgb4_reclaim_completed_tx(adap, &q->q, true);
		/* fetch the tls record */
		record = tls_get_record(&tx_ctx->base, tcp_seq,
					&tx_info->record_no);
		/* By the time packet reached to us, ACK is received, and record
		 * won't be found in that case, handle it gracefully.
		 */
		if (unlikely(!record)) {
			spin_unlock_irqrestore(&tx_ctx->base.lock, flags);
			atomic64_inc(&port_stats->ktls_tx_drop_no_sync_data);
			goto out;
		}

		tls_end_offset = record->end_seq - tcp_seq;

		pr_debug("seq 0x%x, end_seq 0x%x prev_seq 0x%x, datalen 0x%x\n",
			 tcp_seq, record->end_seq, tx_info->prev_seq, data_len);
		/* update tcb for the skb */
		if (skb_data_len == data_len) {
			u32 tx_max = tcp_seq;

			if (!tls_record_is_start_marker(record) &&
			    tls_end_offset < TLS_CIPHER_AES_GCM_128_TAG_SIZE)
				tx_max = record->end_seq -
					TLS_CIPHER_AES_GCM_128_TAG_SIZE;

			ret = chcr_ktls_xmit_tcb_cpls(tx_info, q, tx_max,
						      ntohl(th->ack_seq),
						      ntohs(th->window),
						      tls_end_offset !=
						      record->len);
			if (ret) {
				spin_unlock_irqrestore(&tx_ctx->base.lock,
						       flags);
				goto out;
			}

			if (th->fin)
				skb_get(skb);
		}

		if (unlikely(tls_record_is_start_marker(record))) {
			atomic64_inc(&port_stats->ktls_tx_skip_no_sync_data);
			/* If tls_end_offset < data_len, means there is some
			 * data after start marker, which needs encryption, send
			 * plaintext first and take skb refcount. else send out
			 * complete pkt as plaintext.
			 */
			if (tls_end_offset < data_len)
				skb_get(skb);
			else
				tls_end_offset = data_len;

			ret = chcr_ktls_tx_plaintxt(tx_info, skb, tcp_seq, mss,
						    (!th->fin && th->psh), q,
						    tx_info->port_id, NULL,
						    tls_end_offset, skb_offset,
						    0);

			if (ret) {
				/* free the refcount taken earlier */
				if (tls_end_offset < data_len)
					dev_kfree_skb_any(skb);
				spin_unlock_irqrestore(&tx_ctx->base.lock, flags);
				goto out;
			}

			data_len -= tls_end_offset;
			tcp_seq = record->end_seq;
			skb_offset += tls_end_offset;
			continue;
		}

		/* if a tls record is finishing in this SKB */
		if (tls_end_offset <= data_len) {
			ret = chcr_end_part_handler(tx_info, skb, record,
						    tcp_seq, mss,
						    (!th->fin && th->psh), q,
						    skb_offset,
						    tls_end_offset,
						    skb_offset +
						    tls_end_offset == skb->len);

			data_len -= tls_end_offset;
			/* tcp_seq increment is required to handle next record.
			 */
			tcp_seq += tls_end_offset;
			skb_offset += tls_end_offset;
		} else {
			ret = chcr_short_record_handler(tx_info, skb,
							record, tcp_seq, mss,
							(!th->fin && th->psh),
							data_len, skb_offset,
							q, tls_end_offset);
			data_len = 0;
		}

		/* if any failure, come out from the loop. */
		if (ret) {
			spin_unlock_irqrestore(&tx_ctx->base.lock, flags);
			if (th->fin)
				dev_kfree_skb_any(skb);

			if (ret == FALLBACK)
				return chcr_ktls_sw_fallback(skb, tx_info, q);

			return NETDEV_TX_OK;
		}

		/* length should never be less than 0 */
		WARN_ON(data_len < 0);

	} while (data_len > 0);

	spin_unlock_irqrestore(&tx_ctx->base.lock, flags);
	atomic64_inc(&port_stats->ktls_tx_encrypted_packets);
	atomic64_add(skb_data_len, &port_stats->ktls_tx_encrypted_bytes);

	/* tcp finish is set, send a separate tcp msg including all the options
	 * as well.
	 */
	if (th->fin) {
		chcr_ktls_write_tcp_options(tx_info, skb, q, tx_info->tx_chan);
		dev_kfree_skb_any(skb);
	}

	return NETDEV_TX_OK;
out:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void *chcr_ktls_uld_add(const struct cxgb4_lld_info *lldi)
{
	struct chcr_ktls_uld_ctx *u_ctx;

	pr_info_once("%s - version %s\n", CHCR_KTLS_DRV_DESC,
		     CHCR_KTLS_DRV_VERSION);
	u_ctx = kzalloc(sizeof(*u_ctx), GFP_KERNEL);
	if (!u_ctx) {
		u_ctx = ERR_PTR(-ENOMEM);
		goto out;
	}
	u_ctx->lldi = *lldi;
	u_ctx->detach = false;
	xa_init_flags(&u_ctx->tid_list, XA_FLAGS_LOCK_BH);
out:
	return u_ctx;
}

static const struct tlsdev_ops chcr_ktls_ops = {
	.tls_dev_add = chcr_ktls_dev_add,
	.tls_dev_del = chcr_ktls_dev_del,
};

static chcr_handler_func work_handlers[NUM_CPL_CMDS] = {
	[CPL_ACT_OPEN_RPL] = chcr_ktls_cpl_act_open_rpl,
	[CPL_SET_TCB_RPL] = chcr_ktls_cpl_set_tcb_rpl,
};

static int chcr_ktls_uld_rx_handler(void *handle, const __be64 *rsp,
				    const struct pkt_gl *pgl)
{
	const struct cpl_act_open_rpl *rpl = (struct cpl_act_open_rpl *)rsp;
	struct chcr_ktls_uld_ctx *u_ctx = handle;
	u8 opcode = rpl->ot.opcode;
	struct adapter *adap;

	adap = pci_get_drvdata(u_ctx->lldi.pdev);

	if (!work_handlers[opcode]) {
		pr_err("Unsupported opcode %d received\n", opcode);
		return 0;
	}

	work_handlers[opcode](adap, (unsigned char *)&rsp[1]);
	return 0;
}

static void clear_conn_resources(struct chcr_ktls_info *tx_info)
{
	/* clear l2t entry */
	if (tx_info->l2te)
		cxgb4_l2t_release(tx_info->l2te);

#if IS_ENABLED(CONFIG_IPV6)
	/* clear clip entry */
	if (tx_info->ip_family == AF_INET6)
		cxgb4_clip_release(tx_info->netdev, (const u32 *)
				   &tx_info->sk->sk_v6_rcv_saddr,
				   1);
#endif

	/* clear tid */
	if (tx_info->tid != -1)
		cxgb4_remove_tid(&tx_info->adap->tids, tx_info->tx_chan,
				 tx_info->tid, tx_info->ip_family);
}

static void ch_ktls_reset_all_conn(struct chcr_ktls_uld_ctx *u_ctx)
{
	struct ch_ktls_port_stats_debug *port_stats;
	struct chcr_ktls_ofld_ctx_tx *tx_ctx;
	struct chcr_ktls_info *tx_info;
	unsigned long index;

	xa_for_each(&u_ctx->tid_list, index, tx_ctx) {
		tx_info = tx_ctx->chcr_info;
		clear_conn_resources(tx_info);
		port_stats = &tx_info->adap->ch_ktls_stats.ktls_port[tx_info->port_id];
		atomic64_inc(&port_stats->ktls_tx_connection_close);
		kvfree(tx_info);
		tx_ctx->chcr_info = NULL;
		/* release module refcount */
		module_put(THIS_MODULE);
	}
}

static int chcr_ktls_uld_state_change(void *handle, enum cxgb4_state new_state)
{
	struct chcr_ktls_uld_ctx *u_ctx = handle;

	switch (new_state) {
	case CXGB4_STATE_UP:
		pr_info("%s: Up\n", pci_name(u_ctx->lldi.pdev));
		mutex_lock(&dev_mutex);
		list_add_tail(&u_ctx->entry, &uld_ctx_list);
		mutex_unlock(&dev_mutex);
		break;
	case CXGB4_STATE_START_RECOVERY:
	case CXGB4_STATE_DOWN:
	case CXGB4_STATE_DETACH:
		pr_info("%s: Down\n", pci_name(u_ctx->lldi.pdev));
		mutex_lock(&dev_mutex);
		u_ctx->detach = true;
		list_del(&u_ctx->entry);
		ch_ktls_reset_all_conn(u_ctx);
		xa_destroy(&u_ctx->tid_list);
		mutex_unlock(&dev_mutex);
		break;
	default:
		break;
	}

	return 0;
}

static struct cxgb4_uld_info chcr_ktls_uld_info = {
	.name = CHCR_KTLS_DRV_MODULE_NAME,
	.nrxq = 1,
	.rxq_size = 1024,
	.add = chcr_ktls_uld_add,
	.tx_handler = chcr_ktls_xmit,
	.rx_handler = chcr_ktls_uld_rx_handler,
	.state_change = chcr_ktls_uld_state_change,
	.tlsdev_ops = &chcr_ktls_ops,
};

static int __init chcr_ktls_init(void)
{
	cxgb4_register_uld(CXGB4_ULD_KTLS, &chcr_ktls_uld_info);
	return 0;
}

static void __exit chcr_ktls_exit(void)
{
	struct chcr_ktls_uld_ctx *u_ctx, *tmp;
	struct adapter *adap;

	pr_info("driver unloaded\n");

	mutex_lock(&dev_mutex);
	list_for_each_entry_safe(u_ctx, tmp, &uld_ctx_list, entry) {
		adap = pci_get_drvdata(u_ctx->lldi.pdev);
		memset(&adap->ch_ktls_stats, 0, sizeof(adap->ch_ktls_stats));
		list_del(&u_ctx->entry);
		xa_destroy(&u_ctx->tid_list);
		kfree(u_ctx);
	}
	mutex_unlock(&dev_mutex);
	cxgb4_unregister_uld(CXGB4_ULD_KTLS);
}

module_init(chcr_ktls_init);
module_exit(chcr_ktls_exit);

MODULE_DESCRIPTION("Chelsio NIC TLS ULD driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chelsio Communications");
MODULE_VERSION(CHCR_KTLS_DRV_VERSION);
