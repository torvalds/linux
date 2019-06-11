// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <linux/bitfield.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>
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

	opcode = NFP_NET_CRYPTO_OP_TLS_1_2_AES_GCM_128_ENC;
	nn->ktls_tx_conn_cnt += add;
	cnt = nn->ktls_tx_conn_cnt;
	nn->dp.ktls_tx = !!nn->ktls_tx_conn_cnt;

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

	err = nfp_ccm_mbox_communicate(nn, skb, type,
				       sizeof(*reply), sizeof(*reply));
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

static struct nfp_crypto_req_add_back *
nfp_net_tls_set_ipv4(struct nfp_crypto_req_add_v4 *req, struct sock *sk,
		     int direction)
{
	struct inet_sock *inet = inet_sk(sk);

	req->front.key_len += sizeof(__be32) * 2;
	req->front.ipver_vlan = cpu_to_be16(FIELD_PREP(NFP_NET_TLS_IPVER, 4) |
					    FIELD_PREP(NFP_NET_TLS_VLAN,
						       NFP_NET_TLS_VLAN_UNUSED));

	if (direction == TLS_OFFLOAD_CTX_DIR_TX) {
		req->src_ip = inet->inet_saddr;
		req->dst_ip = inet->inet_daddr;
	} else {
		req->src_ip = inet->inet_daddr;
		req->dst_ip = inet->inet_saddr;
	}

	return &req->back;
}

static struct nfp_crypto_req_add_back *
nfp_net_tls_set_ipv6(struct nfp_crypto_req_add_v6 *req, struct sock *sk,
		     int direction)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6_pinfo *np = inet6_sk(sk);

	req->front.key_len += sizeof(struct in6_addr) * 2;
	req->front.ipver_vlan = cpu_to_be16(FIELD_PREP(NFP_NET_TLS_IPVER, 6) |
					    FIELD_PREP(NFP_NET_TLS_VLAN,
						       NFP_NET_TLS_VLAN_UNUSED));

	if (direction == TLS_OFFLOAD_CTX_DIR_TX) {
		memcpy(req->src_ip, &np->saddr, sizeof(req->src_ip));
		memcpy(req->dst_ip, &sk->sk_v6_daddr, sizeof(req->dst_ip));
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
		back->src_port = inet->inet_sport;
		back->dst_port = inet->inet_dport;
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
			return false;
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
	bool ipv6;
	int err;

	BUILD_BUG_ON(sizeof(struct nfp_net_tls_offload_ctx) >
		     TLS_DRIVER_STATE_SIZE_TX);

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
	front->key_len = 8;
	front->opcode = nfp_tls_1_2_dir_to_opcode(direction);
	memset(front->resv, 0, sizeof(front->resv));

	if (ipv6)
		back = nfp_net_tls_set_ipv6((void *)skb->data, sk, direction);
	else
		back = nfp_net_tls_set_ipv4((void *)skb->data, sk, direction);

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

	err = nfp_ccm_mbox_communicate(nn, skb, NFP_CCM_TYPE_CRYPTO_ADD,
				       sizeof(*reply), sizeof(*reply));
	if (err) {
		nn_dp_warn(&nn->dp, "failed to add TLS: %d\n", err);
		/* communicate frees skb on error */
		goto err_conn_remove;
	}

	reply = (void *)skb->data;
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
		goto err_fw_remove;
	}

	ntls = tls_driver_ctx(sk, direction);
	memcpy(ntls->fw_handle, reply->handle, sizeof(ntls->fw_handle));
	ntls->next_seq = start_offload_tcp_sn;
	dev_consume_skb_any(skb);

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

static const struct tlsdev_ops nfp_net_tls_ops = {
	.tls_dev_add = nfp_net_tls_add,
	.tls_dev_del = nfp_net_tls_del,
};

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

	if (nn->tlv_caps.crypto_ops & NFP_NET_TLS_OPCODE_MASK_TX) {
		netdev->hw_features |= NETIF_F_HW_TLS_TX;
		netdev->features |= NETIF_F_HW_TLS_TX;
	}

	netdev->tlsdev_ops = &nfp_net_tls_ops;

	return 0;
}
