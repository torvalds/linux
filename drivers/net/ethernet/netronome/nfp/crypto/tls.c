// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <linux/bitfield.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <net/inet6_hashtables.h>
#include <net/tls.h>

#include "../ccm.h"
#include "../nfp_net.h"
#include "crypto.h"
#include "fw.h"

#define NFP_NET_TLS_CCM_MBOX_OPS_MASK		\
	(BIT(NFP_CCM_TYPE_CRYPTO_RESET) |	\
	 BIT(NFP_CCM_TYPE_CRYPTO_ADD) |		\
	 BIT(NFP_CCM_TYPE_CRYPTO_DEL) |		\
	 BIT(NFP_CCM_TYPE_CRYPTO_UPDATE))

#define NFP_NET_TLS_OPCODE_MASK_RX			\
	BIT(NFP_NET_CRYPTO_OP_TLS_1_2_AES_GCM_128_DEC)

#define NFP_NET_TLS_OPCODE_MASK_TX			\
	BIT(NFP_NET_CRYPTO_OP_TLS_1_2_AES_GCM_128_ENC)

#define NFP_NET_TLS_OPCODE_MASK						\
	(NFP_NET_TLS_OPCODE_MASK_RX | NFP_NET_TLS_OPCODE_MASK_TX)

static void nfp_net_crypto_set_op(struct nfp_net *nn, u8 opcode, bool on)
{
	u32 off, val;

	off = nn->tlv_caps.crypto_enable_off + round_down(opcode / 8, 4);

	val = nn_readl(nn, off);
	if (on)
		val |= BIT(opcode & 31);
	else
		val &= ~BIT(opcode & 31);
	nn_writel(nn, off, val);
}

static bool
__nfp_net_tls_conn_cnt_changed(struct nfp_net *nn, int add,
			       enum tls_offload_ctx_dir direction)
{
	u8 opcode;
	int cnt;

	if (direction == TLS_OFFLOAD_CTX_DIR_TX) {
		opcode = NFP_NET_CRYPTO_OP_TLS_1_2_AES_GCM_128_ENC;
		nn->ktls_tx_conn_cnt += add;
		cnt = nn->ktls_tx_conn_cnt;
		nn->dp.ktls_tx = !!nn->ktls_tx_conn_cnt;
	} else {
		opcode = NFP_NET_CRYPTO_OP_TLS_1_2_AES_GCM_128_DEC;
		nn->ktls_rx_conn_cnt += add;
		cnt = nn->ktls_rx_conn_cnt;
	}

	/* Care only about 0 -> 1 and 1 -> 0 transitions */
	if (cnt > 1)
		return false;

	nfp_net_crypto_set_op(nn, opcode, cnt);
	return true;
}

static int
nfp_net_tls_conn_cnt_changed(struct nfp_net *nn, int add,
			     enum tls_offload_ctx_dir direction)
{
	int ret = 0;

	/* Use the BAR lock to protect the connection counts */
	nn_ctrl_bar_lock(nn);
	if (__nfp_net_tls_conn_cnt_changed(nn, add, direction)) {
		ret = __nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_CRYPTO);
		/* Undo the cnt adjustment if failed */
		if (ret)
			__nfp_net_tls_conn_cnt_changed(nn, -add, direction);
	}
	nn_ctrl_bar_unlock(nn);

	return ret;
}

static int
nfp_net_tls_conn_add(struct nfp_net *nn, enum tls_offload_ctx_dir direction)
{
	return nfp_net_tls_conn_cnt_changed(nn, 1, direction);
}

static int
nfp_net_tls_conn_remove(struct nfp_net *nn, enum tls_offload_ctx_dir direction)
{
	return nfp_net_tls_conn_cnt_changed(nn, -1, direction);
}

static struct sk_buff *
nfp_net_tls_alloc_simple(struct nfp_net *nn, size_t req_sz, gfp_t flags)
{
	return nfp_ccm_mbox_msg_alloc(nn, req_sz,
				      sizeof(struct nfp_crypto_reply_simple),
				      flags);
}

static int
nfp_net_tls_communicate_simple(struct nfp_net *nn, struct sk_buff *skb,
			       const char *name, enum nfp_ccm_type type)
{
	struct nfp_crypto_reply_simple *reply;
	int err;

	err = __nfp_ccm_mbox_communicate(nn, skb, type,
					 sizeof(*reply), sizeof(*reply),
					 type == NFP_CCM_TYPE_CRYPTO_DEL);
	if (err) {
		nn_dp_warn(&nn->dp, "failed to %s TLS: %d\n", name, err);
		return err;
	}

	reply = (void *)skb->data;
	err = -be32_to_cpu(reply->error);
	if (err)
		nn_dp_warn(&nn->dp, "failed to %s TLS, fw replied: %d\n",
			   name, err);
	dev_consume_skb_any(skb);

	return err;
}

static void nfp_net_tls_del_fw(struct nfp_net *nn, __be32 *fw_handle)
{
	struct nfp_crypto_req_del *req;
	struct sk_buff *skb;

	skb = nfp_net_tls_alloc_simple(nn, sizeof(*req), GFP_KERNEL);
	if (!skb)
		return;

	req = (void *)skb->data;
	req->ep_id = 0;
	memcpy(req->handle, fw_handle, sizeof(req->handle));

	nfp_net_tls_communicate_simple(nn, skb, "delete",
				       NFP_CCM_TYPE_CRYPTO_DEL);
}

static void
nfp_net_tls_set_ipver_vlan(struct nfp_crypto_req_add_front *front, u8 ipver)
{
	front->ipver_vlan = cpu_to_be16(FIELD_PREP(NFP_NET_TLS_IPVER, ipver) |
					FIELD_PREP(NFP_NET_TLS_VLAN,
						   NFP_NET_TLS_VLAN_UNUSED));
}

static void
nfp_net_tls_assign_conn_id(struct nfp_net *nn,
			   struct nfp_crypto_req_add_front *front)
{
	u32 len;
	u64 id;

	id = atomic64_inc_return(&nn->ktls_conn_id_gen);
	len = front->key_len - NFP_NET_TLS_NON_ADDR_KEY_LEN;

	memcpy(front->l3_addrs, &id, sizeof(id));
	memset(front->l3_addrs + sizeof(id), 0, len - sizeof(id));
}

static struct nfp_crypto_req_add_back *
nfp_net_tls_set_ipv4(struct nfp_net *nn, struct nfp_crypto_req_add_v4 *req,
		     struct sock *sk, int direction)
{
	struct inet_sock *inet = inet_sk(sk);

	req->front.key_len += sizeof(__be32) * 2;

	if (direction == TLS_OFFLOAD_CTX_DIR_TX) {
		nfp_net_tls_assign_conn_id(nn, &req->front);
	} else {
		req->src_ip = inet->inet_daddr;
		req->dst_ip = inet->inet_saddr;
	}

	return &req->back;
}

static struct nfp_crypto_req_add_back *
nfp_net_tls_set_ipv6(struct nfp_net *nn, struct nfp_crypto_req_add_v6 *req,
		     struct sock *sk, int direction)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6_pinfo *np = inet6_sk(sk);

	req->front.key_len += sizeof(struct in6_addr) * 2;

	if (direction == TLS_OFFLOAD_CTX_DIR_TX) {
		nfp_net_tls_assign_conn_id(nn, &req->front);
	} else {
		memcpy(req->src_ip, &sk->sk_v6_daddr, sizeof(req->src_ip));
		memcpy(req->dst_ip, &np->saddr, sizeof(req->dst_ip));
	}

#endif
	return &req->back;
}

static void
nfp_net_tls_set_l4(struct nfp_crypto_req_add_front *front,
		   struct nfp_crypto_req_add_back *back, struct sock *sk,
		   int direction)
{
	struct inet_sock *inet = inet_sk(sk);

	front->l4_proto = IPPROTO_TCP;

	if (direction == TLS_OFFLOAD_CTX_DIR_TX) {
		back->src_port = 0;
		back->dst_port = 0;
	} else {
		back->src_port = inet->inet_dport;
		back->dst_port = inet->inet_sport;
	}
}

static u8 nfp_tls_1_2_dir_to_opcode(enum tls_offload_ctx_dir direction)
{
	switch (direction) {
	case TLS_OFFLOAD_CTX_DIR_TX:
		return NFP_NET_CRYPTO_OP_TLS_1_2_AES_GCM_128_ENC;
	case TLS_OFFLOAD_CTX_DIR_RX:
		return NFP_NET_CRYPTO_OP_TLS_1_2_AES_GCM_128_DEC;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

static bool
nfp_net_cipher_supported(struct nfp_net *nn, u16 cipher_type,
			 enum tls_offload_ctx_dir direction)
{
	u8 bit;

	switch (cipher_type) {
	case TLS_CIPHER_AES_GCM_128:
		if (direction == TLS_OFFLOAD_CTX_DIR_TX)
			bit = NFP_NET_CRYPTO_OP_TLS_1_2_AES_GCM_128_ENC;
		else
			bit = NFP_NET_CRYPTO_OP_TLS_1_2_AES_GCM_128_DEC;
		break;
	default:
		return false;
	}

	return nn->tlv_caps.crypto_ops & BIT(bit);
}

static int
nfp_net_tls_add(struct net_device *netdev, struct sock *sk,
		enum tls_offload_ctx_dir direction,
		struct tls_crypto_info *crypto_info,
		u32 start_offload_tcp_sn)
{
	struct tls12_crypto_info_aes_gcm_128 *tls_ci;
	struct nfp_net *nn = netdev_priv(netdev);
	struct nfp_crypto_req_add_front *front;
	struct nfp_net_tls_offload_ctx *ntls;
	struct nfp_crypto_req_add_back *back;
	struct nfp_crypto_reply_add *reply;
	struct sk_buff *skb;
	size_t req_sz;
	void *req;
	bool ipv6;
	int err;

	BUILD_BUG_ON(sizeof(struct nfp_net_tls_offload_ctx) >
		     TLS_DRIVER_STATE_SIZE_TX);
	BUILD_BUG_ON(offsetof(struct nfp_net_tls_offload_ctx, rx_end) >
		     TLS_DRIVER_STATE_SIZE_RX);

	if (!nfp_net_cipher_supported(nn, crypto_info->cipher_type, direction))
		return -EOPNOTSUPP;

	switch (sk->sk_family) {
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		if (sk->sk_ipv6only ||
		    ipv6_addr_type(&sk->sk_v6_daddr) != IPV6_ADDR_MAPPED) {
			req_sz = sizeof(struct nfp_crypto_req_add_v6);
			ipv6 = true;
			break;
		}
#endif
		/* fall through */
	case AF_INET:
		req_sz = sizeof(struct nfp_crypto_req_add_v4);
		ipv6 = false;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = nfp_net_tls_conn_add(nn, direction);
	if (err)
		return err;

	skb = nfp_ccm_mbox_msg_alloc(nn, req_sz, sizeof(*reply), GFP_KERNEL);
	if (!skb) {
		err = -ENOMEM;
		goto err_conn_remove;
	}

	front = (void *)skb->data;
	front->ep_id = 0;
	front->key_len = NFP_NET_TLS_NON_ADDR_KEY_LEN;
	front->opcode = nfp_tls_1_2_dir_to_opcode(direction);
	memset(front->resv, 0, sizeof(front->resv));

	nfp_net_tls_set_ipver_vlan(front, ipv6 ? 6 : 4);

	req = (void *)skb->data;
	if (ipv6)
		back = nfp_net_tls_set_ipv6(nn, req, sk, direction);
	else
		back = nfp_net_tls_set_ipv4(nn, req, sk, direction);

	nfp_net_tls_set_l4(front, back, sk, direction);

	back->counter = 0;
	back->tcp_seq = cpu_to_be32(start_offload_tcp_sn);

	tls_ci = (struct tls12_crypto_info_aes_gcm_128 *)crypto_info;
	memcpy(back->key, tls_ci->key, TLS_CIPHER_AES_GCM_128_KEY_SIZE);
	memset(&back->key[TLS_CIPHER_AES_GCM_128_KEY_SIZE / 4], 0,
	       sizeof(back->key) - TLS_CIPHER_AES_GCM_128_KEY_SIZE);
	memcpy(back->iv, tls_ci->iv, TLS_CIPHER_AES_GCM_128_IV_SIZE);
	memcpy(&back->salt, tls_ci->salt, TLS_CIPHER_AES_GCM_128_SALT_SIZE);
	memcpy(back->rec_no, tls_ci->rec_seq, sizeof(tls_ci->rec_seq));

	/* Get an extra ref on the skb so we can wipe the key after */
	skb_get(skb);

	err = nfp_ccm_mbox_communicate(nn, skb, NFP_CCM_TYPE_CRYPTO_ADD,
				       sizeof(*reply), sizeof(*reply));
	reply = (void *)skb->data;

	/* We depend on CCM MBOX code not reallocating skb we sent
	 * so we can clear the key material out of the memory.
	 */
	if (!WARN_ON_ONCE((u8 *)back < skb->head ||
			  (u8 *)back > skb_end_pointer(skb)) &&
	    !WARN_ON_ONCE((u8 *)&reply[1] > (u8 *)back))
		memzero_explicit(back, sizeof(*back));
	dev_consume_skb_any(skb); /* the extra ref from skb_get() above */

	if (err) {
		nn_dp_warn(&nn->dp, "failed to add TLS: %d (%d)\n",
			   err, direction == TLS_OFFLOAD_CTX_DIR_TX);
		/* communicate frees skb on error */
		goto err_conn_remove;
	}

	err = -be32_to_cpu(reply->error);
	if (err) {
		if (err == -ENOSPC) {
			if (!atomic_fetch_inc(&nn->ktls_no_space))
				nn_info(nn, "HW TLS table full\n");
		} else {
			nn_dp_warn(&nn->dp,
				   "failed to add TLS, FW replied: %d\n", err);
		}
		goto err_free_skb;
	}

	if (!reply->handle[0] && !reply->handle[1]) {
		nn_dp_warn(&nn->dp, "FW returned NULL handle\n");
		err = -EINVAL;
		goto err_fw_remove;
	}

	ntls = tls_driver_ctx(sk, direction);
	memcpy(ntls->fw_handle, reply->handle, sizeof(ntls->fw_handle));
	if (direction == TLS_OFFLOAD_CTX_DIR_TX)
		ntls->next_seq = start_offload_tcp_sn;
	dev_consume_skb_any(skb);

	if (direction == TLS_OFFLOAD_CTX_DIR_TX)
		return 0;

	if (!nn->tlv_caps.tls_resync_ss)
		tls_offload_rx_resync_set_type(sk, TLS_OFFLOAD_SYNC_TYPE_CORE_NEXT_HINT);

	return 0;

err_fw_remove:
	nfp_net_tls_del_fw(nn, reply->handle);
err_free_skb:
	dev_consume_skb_any(skb);
err_conn_remove:
	nfp_net_tls_conn_remove(nn, direction);
	return err;
}

static void
nfp_net_tls_del(struct net_device *netdev, struct tls_context *tls_ctx,
		enum tls_offload_ctx_dir direction)
{
	struct nfp_net *nn = netdev_priv(netdev);
	struct nfp_net_tls_offload_ctx *ntls;

	nfp_net_tls_conn_remove(nn, direction);

	ntls = __tls_driver_ctx(tls_ctx, direction);
	nfp_net_tls_del_fw(nn, ntls->fw_handle);
}

static int
nfp_net_tls_resync(struct net_device *netdev, struct sock *sk, u32 seq,
		   u8 *rcd_sn, enum tls_offload_ctx_dir direction)
{
	struct nfp_net *nn = netdev_priv(netdev);
	struct nfp_net_tls_offload_ctx *ntls;
	struct nfp_crypto_req_update *req;
	enum nfp_ccm_type type;
	struct sk_buff *skb;
	gfp_t flags;
	int err;

	flags = direction == TLS_OFFLOAD_CTX_DIR_TX ? GFP_KERNEL : GFP_ATOMIC;
	skb = nfp_net_tls_alloc_simple(nn, sizeof(*req), flags);
	if (!skb)
		return -ENOMEM;

	ntls = tls_driver_ctx(sk, direction);
	req = (void *)skb->data;
	req->ep_id = 0;
	req->opcode = nfp_tls_1_2_dir_to_opcode(direction);
	memset(req->resv, 0, sizeof(req->resv));
	memcpy(req->handle, ntls->fw_handle, sizeof(ntls->fw_handle));
	req->tcp_seq = cpu_to_be32(seq);
	memcpy(req->rec_no, rcd_sn, sizeof(req->rec_no));

	type = NFP_CCM_TYPE_CRYPTO_UPDATE;
	if (direction == TLS_OFFLOAD_CTX_DIR_TX) {
		err = nfp_net_tls_communicate_simple(nn, skb, "sync", type);
		if (err)
			return err;
		ntls->next_seq = seq;
	} else {
		if (nn->tlv_caps.tls_resync_ss)
			type = NFP_CCM_TYPE_CRYPTO_RESYNC;
		nfp_ccm_mbox_post(nn, skb, type,
				  sizeof(struct nfp_crypto_reply_simple));
		atomic_inc(&nn->ktls_rx_resync_sent);
	}

	return 0;
}

static const struct tlsdev_ops nfp_net_tls_ops = {
	.tls_dev_add = nfp_net_tls_add,
	.tls_dev_del = nfp_net_tls_del,
	.tls_dev_resync = nfp_net_tls_resync,
};

int nfp_net_tls_rx_resync_req(struct net_device *netdev,
			      struct nfp_net_tls_resync_req *req,
			      void *pkt, unsigned int pkt_len)
{
	struct nfp_net *nn = netdev_priv(netdev);
	struct nfp_net_tls_offload_ctx *ntls;
	struct ipv6hdr *ipv6h;
	struct tcphdr *th;
	struct iphdr *iph;
	struct sock *sk;
	__be32 tcp_seq;
	int err;

	iph = pkt + req->l3_offset;
	ipv6h = pkt + req->l3_offset;
	th = pkt + req->l4_offset;

	if ((u8 *)&th[1] > (u8 *)pkt + pkt_len) {
		netdev_warn_once(netdev, "invalid TLS RX resync request (l3_off: %hhu l4_off: %hhu pkt_len: %u)\n",
				 req->l3_offset, req->l4_offset, pkt_len);
		err = -EINVAL;
		goto err_cnt_ign;
	}

	switch (iph->version) {
	case 4:
		sk = inet_lookup_established(dev_net(netdev), &tcp_hashinfo,
					     iph->saddr, th->source, iph->daddr,
					     th->dest, netdev->ifindex);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case 6:
		sk = __inet6_lookup_established(dev_net(netdev), &tcp_hashinfo,
						&ipv6h->saddr, th->source,
						&ipv6h->daddr, ntohs(th->dest),
						netdev->ifindex, 0);
		break;
#endif
	default:
		netdev_warn_once(netdev, "invalid TLS RX resync request (l3_off: %hhu l4_off: %hhu ipver: %u)\n",
				 req->l3_offset, req->l4_offset, iph->version);
		err = -EINVAL;
		goto err_cnt_ign;
	}

	err = 0;
	if (!sk)
		goto err_cnt_ign;
	if (!tls_is_sk_rx_device_offloaded(sk) ||
	    sk->sk_shutdown & RCV_SHUTDOWN)
		goto err_put_sock;

	ntls = tls_driver_ctx(sk, TLS_OFFLOAD_CTX_DIR_RX);
	/* some FW versions can't report the handle and report 0s */
	if (memchr_inv(&req->fw_handle, 0, sizeof(req->fw_handle)) &&
	    memcmp(&req->fw_handle, &ntls->fw_handle, sizeof(ntls->fw_handle)))
		goto err_put_sock;

	/* copy to ensure alignment */
	memcpy(&tcp_seq, &req->tcp_seq, sizeof(tcp_seq));
	tls_offload_rx_resync_request(sk, tcp_seq);
	atomic_inc(&nn->ktls_rx_resync_req);

	sock_gen_put(sk);
	return 0;

err_put_sock:
	sock_gen_put(sk);
err_cnt_ign:
	atomic_inc(&nn->ktls_rx_resync_ign);
	return err;
}

static int nfp_net_tls_reset(struct nfp_net *nn)
{
	struct nfp_crypto_req_reset *req;
	struct sk_buff *skb;

	skb = nfp_net_tls_alloc_simple(nn, sizeof(*req), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	req = (void *)skb->data;
	req->ep_id = 0;

	return nfp_net_tls_communicate_simple(nn, skb, "reset",
					      NFP_CCM_TYPE_CRYPTO_RESET);
}

int nfp_net_tls_init(struct nfp_net *nn)
{
	struct net_device *netdev = nn->dp.netdev;
	int err;

	if (!(nn->tlv_caps.crypto_ops & NFP_NET_TLS_OPCODE_MASK))
		return 0;

	if ((nn->tlv_caps.mbox_cmsg_types & NFP_NET_TLS_CCM_MBOX_OPS_MASK) !=
	    NFP_NET_TLS_CCM_MBOX_OPS_MASK)
		return 0;

	if (!nfp_ccm_mbox_fits(nn, sizeof(struct nfp_crypto_req_add_v6))) {
		nn_warn(nn, "disabling TLS offload - mbox too small: %d\n",
			nn->tlv_caps.mbox_len);
		return 0;
	}

	err = nfp_net_tls_reset(nn);
	if (err)
		return err;

	nn_ctrl_bar_lock(nn);
	nn_writel(nn, nn->tlv_caps.crypto_enable_off, 0);
	err = __nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_CRYPTO);
	nn_ctrl_bar_unlock(nn);
	if (err)
		return err;

	if (nn->tlv_caps.crypto_ops & NFP_NET_TLS_OPCODE_MASK_RX) {
		netdev->hw_features |= NETIF_F_HW_TLS_RX;
		netdev->features |= NETIF_F_HW_TLS_RX;
	}
	if (nn->tlv_caps.crypto_ops & NFP_NET_TLS_OPCODE_MASK_TX) {
		netdev->hw_features |= NETIF_F_HW_TLS_TX;
		netdev->features |= NETIF_F_HW_TLS_TX;
	}

	netdev->tlsdev_ops = &nfp_net_tls_ops;

	return 0;
}
