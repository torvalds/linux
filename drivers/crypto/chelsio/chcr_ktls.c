// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Chelsio Communications.  All rights reserved. */

#ifdef CONFIG_CHELSIO_TLS_DEVICE
#include "chcr_ktls.h"

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
	tx_info->ip_family = sk->sk_family;

	if (sk->sk_family == AF_INET ||
	    (sk->sk_family == AF_INET6 && !sk->sk_ipv6only &&
	     ipv6_addr_type(&sk->sk_v6_daddr) == IPV6_ADDR_MAPPED)) {
		tx_info->ip_family = AF_INET;
		ret = chcr_ktls_act_open_req(sk, tx_info, atid);
	} else {
		tx_info->ip_family = AF_INET6;
		ret = -EOPNOTSUPP;
	}

	/* if return type is NET_XMIT_CN, msg will be sent but delayed, mark ret
	 * success, if any other return type clear atid and return that failure.
	 */
	if (ret) {
		if (ret == NET_XMIT_CN)
			ret = 0;
		else
			cxgb4_free_atid(t, atid);
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
 * chcr_ktls_mark_tcb_close: mark tcb state to CLOSE
 * @tx_info - driver specific tls info.
 * return: NET_TX_OK/NET_XMIT_DROP.
 */
static int chcr_ktls_mark_tcb_close(struct chcr_ktls_info *tx_info)
{
	return chcr_set_tcb_field(tx_info, TCB_T_STATE_W,
				  TCB_T_STATE_V(TCB_T_STATE_M),
				  CHCR_TCB_STATE_CLOSED, 1);
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

	if (!tx_info)
		return;

	spin_lock(&tx_info->lock);
	tx_info->connection_state = KTLS_CONN_CLOSED;
	spin_unlock(&tx_info->lock);

	if (tx_info->l2te)
		cxgb4_l2t_release(tx_info->l2te);

	if (tx_info->tid != -1) {
		/* clear tcb state and then release tid */
		chcr_ktls_mark_tcb_close(tx_info);
		cxgb4_remove_tid(&tx_info->adap->tids, tx_info->tx_chan,
				 tx_info->tid, tx_info->ip_family);
	}
	kvfree(tx_info);
	tx_ctx->chcr_info = NULL;
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
	struct chcr_ktls_ofld_ctx_tx *tx_ctx;
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
	if (direction == TLS_OFFLOAD_CTX_DIR_RX) {
		pr_err("not expecting for RX direction\n");
		ret = -EINVAL;
		goto out;
	}
	if (tx_ctx->chcr_info) {
		ret = -EINVAL;
		goto out;
	}

	tx_info = kvzalloc(sizeof(*tx_info), GFP_KERNEL);
	if (!tx_info) {
		ret = -ENOMEM;
		goto out;
	}

	spin_lock_init(&tx_info->lock);

	/* clear connection state */
	spin_lock(&tx_info->lock);
	tx_info->connection_state = KTLS_CONN_CLOSED;
	spin_unlock(&tx_info->lock);

	tx_info->sk = sk;
	/* initialize tid and atid to -1, 0 is a also a valid id. */
	tx_info->tid = -1;
	tx_info->atid = -1;

	tx_info->adap = adap;
	tx_info->netdev = netdev;
	tx_info->tx_chan = pi->tx_chan;
	tx_info->smt_idx = pi->smt_idx;
	tx_info->port_id = pi->port_id;

	tx_info->rx_qid = chcr_get_first_rx_qid(adap);
	if (unlikely(tx_info->rx_qid < 0))
		goto out2;

	tx_info->prev_seq = start_offload_tcp_sn;
	tx_info->tcp_start_seq_number = start_offload_tcp_sn;

	/* get peer ip */
	if (sk->sk_family == AF_INET ||
	    (sk->sk_family == AF_INET6 && !sk->sk_ipv6only &&
	     ipv6_addr_type(&sk->sk_v6_daddr) == IPV6_ADDR_MAPPED)) {
		memcpy(daaddr, &sk->sk_daddr, 4);
	} else {
		goto out2;
	}

	/* get the l2t index */
	dst = sk_dst_get(sk);
	if (!dst) {
		pr_err("DST entry not found\n");
		goto out2;
	}
	n = dst_neigh_lookup(dst, daaddr);
	if (!n || !n->dev) {
		pr_err("neighbour not found\n");
		dst_release(dst);
		goto out2;
	}
	tx_info->l2te  = cxgb4_l2t_get(adap->l2t, n, n->dev, 0);

	neigh_release(n);
	dst_release(dst);

	if (!tx_info->l2te) {
		pr_err("l2t entry not found\n");
		goto out2;
	}

	tx_ctx->chcr_info = tx_info;

	/* create a filter and call cxgb4_l2t_send to send the packet out, which
	 * will take care of updating l2t entry in hw if not already done.
	 */
	ret = chcr_setup_connection(sk, tx_info);
	if (ret)
		goto out2;

	return 0;
out2:
	kvfree(tx_info);
out:
	return ret;
}

static const struct tlsdev_ops chcr_ktls_ops = {
	.tls_dev_add = chcr_ktls_dev_add,
	.tls_dev_del = chcr_ktls_dev_del,
};

/*
 * chcr_enable_ktls:  add NETIF_F_HW_TLS_TX flag in all the ports.
 */
void chcr_enable_ktls(struct adapter *adap)
{
	struct net_device *netdev;
	int i;

	for_each_port(adap, i) {
		netdev = adap->port[i];
		netdev->features |= NETIF_F_HW_TLS_TX;
		netdev->hw_features |= NETIF_F_HW_TLS_TX;
		netdev->tlsdev_ops = &chcr_ktls_ops;
	}
}

/*
 * chcr_disable_ktls:  remove NETIF_F_HW_TLS_TX flag from all the ports.
 */
void chcr_disable_ktls(struct adapter *adap)
{
	struct net_device *netdev;
	int i;

	for_each_port(adap, i) {
		netdev = adap->port[i];
		netdev->features &= ~NETIF_F_HW_TLS_TX;
		netdev->hw_features &= ~NETIF_F_HW_TLS_TX;
		netdev->tlsdev_ops = NULL;
	}
}
#endif /* CONFIG_CHELSIO_TLS_DEVICE */
