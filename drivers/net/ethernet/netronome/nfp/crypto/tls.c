// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

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

static struct sk_buff *
nfp_net_tls_alloc_simple(struct nfp_net *nn, size_t req_sz, gfp_t flags)
{
	return nfp_ccm_mbox_alloc(nn, req_sz,
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

static int
nfp_net_tls_add(struct net_device *netdev, struct sock *sk,
		enum tls_offload_ctx_dir direction,
		struct tls_crypto_info *crypto_info,
		u32 start_offload_tcp_sn)
{
	return -EOPNOTSUPP;
}

static void
nfp_net_tls_del(struct net_device *netdev, struct tls_context *tls_ctx,
		enum tls_offload_ctx_dir direction)
{
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

	netdev->tlsdev_ops = &nfp_net_tls_ops;

	return 0;
}
