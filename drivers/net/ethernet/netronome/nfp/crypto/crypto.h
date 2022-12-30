/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef NFP_CRYPTO_H
#define NFP_CRYPTO_H 1

struct net_device;
struct nfp_net;
struct nfp_net_tls_resync_req;

struct nfp_net_tls_offload_ctx {
	__be32 fw_handle[2];

	u8 rx_end[0];
	/* Tx only fields follow - Rx side does not have enough driver state
	 * to fit these
	 */

	u32 next_seq;
};

#ifdef CONFIG_TLS_DEVICE
int nfp_net_tls_init(struct nfp_net *nn);
int nfp_net_tls_rx_resync_req(struct net_device *netdev,
			      struct nfp_net_tls_resync_req *req,
			      void *pkt, unsigned int pkt_len);
#else
static inline int nfp_net_tls_init(struct nfp_net *nn)
{
	return 0;
}

static inline int
nfp_net_tls_rx_resync_req(struct net_device *netdev,
			  struct nfp_net_tls_resync_req *req,
			  void *pkt, unsigned int pkt_len)
{
	return -EOPNOTSUPP;
}
#endif

/* IPsec related structures and functions */
struct nfp_ipsec_offload {
	u32 seq_hi;
	u32 seq_low;
	u32 handle;
};

#ifndef CONFIG_NFP_NET_IPSEC
static inline void nfp_net_ipsec_init(struct nfp_net *nn)
{
}

static inline void nfp_net_ipsec_clean(struct nfp_net *nn)
{
}
#else
void nfp_net_ipsec_init(struct nfp_net *nn);
void nfp_net_ipsec_clean(struct nfp_net *nn);
bool nfp_net_ipsec_tx_prep(struct nfp_net_dp *dp, struct sk_buff *skb,
			   struct nfp_ipsec_offload *offload_info);
int nfp_net_ipsec_rx(struct nfp_meta_parsed *meta, struct sk_buff *skb);
#endif

#endif
