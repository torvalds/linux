/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
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
 *
 */

#include "en_accel/tls.h"
#include "en_accel/tls_rxtx.h"
#include "accel/accel.h"

#include <net/inet6_hashtables.h>
#include <linux/ipv6.h>

#define SYNDROM_DECRYPTED  0x30
#define SYNDROM_RESYNC_REQUEST 0x31
#define SYNDROM_AUTH_FAILED 0x32

#define SYNDROME_OFFLOAD_REQUIRED 32
#define SYNDROME_SYNC 33

struct sync_info {
	u64 rcd_sn;
	s32 sync_len;
	int nr_frags;
	skb_frag_t frags[MAX_SKB_FRAGS];
};

struct recv_metadata_content {
	u8 syndrome;
	u8 reserved;
	__be32 sync_seq;
} __packed;

struct send_metadata_content {
	/* One byte of syndrome followed by 3 bytes of swid */
	__be32 syndrome_swid;
	__be16 first_seq;
} __packed;

struct mlx5e_tls_metadata {
	union {
		/* from fpga to host */
		struct recv_metadata_content recv;
		/* from host to fpga */
		struct send_metadata_content send;
		unsigned char raw[6];
	} __packed content;
	/* packet type ID field	*/
	__be16 ethertype;
} __packed;

static int mlx5e_tls_add_metadata(struct sk_buff *skb, __be32 swid)
{
	struct mlx5e_tls_metadata *pet;
	struct ethhdr *eth;

	if (skb_cow_head(skb, sizeof(struct mlx5e_tls_metadata)))
		return -ENOMEM;

	eth = (struct ethhdr *)skb_push(skb, sizeof(struct mlx5e_tls_metadata));
	skb->mac_header -= sizeof(struct mlx5e_tls_metadata);
	pet = (struct mlx5e_tls_metadata *)(eth + 1);

	memmove(skb->data, skb->data + sizeof(struct mlx5e_tls_metadata),
		2 * ETH_ALEN);

	eth->h_proto = cpu_to_be16(MLX5E_METADATA_ETHER_TYPE);
	pet->content.send.syndrome_swid =
		htonl(SYNDROME_OFFLOAD_REQUIRED << 24) | swid;

	return 0;
}

static int mlx5e_tls_get_sync_data(struct mlx5e_tls_offload_context_tx *context,
				   u32 tcp_seq, struct sync_info *info)
{
	int remaining, i = 0, ret = -EINVAL;
	struct tls_record_info *record;
	unsigned long flags;
	s32 sync_size;

	spin_lock_irqsave(&context->base.lock, flags);
	record = tls_get_record(&context->base, tcp_seq, &info->rcd_sn);

	if (unlikely(!record))
		goto out;

	sync_size = tcp_seq - tls_record_start_seq(record);
	info->sync_len = sync_size;
	if (unlikely(sync_size < 0)) {
		if (tls_record_is_start_marker(record))
			goto done;

		goto out;
	}

	remaining = sync_size;
	while (remaining > 0) {
		info->frags[i] = record->frags[i];
		__skb_frag_ref(&info->frags[i]);
		remaining -= skb_frag_size(&info->frags[i]);

		if (remaining < 0)
			skb_frag_size_add(&info->frags[i], remaining);

		i++;
	}
	info->nr_frags = i;
done:
	ret = 0;
out:
	spin_unlock_irqrestore(&context->base.lock, flags);
	return ret;
}

static void mlx5e_tls_complete_sync_skb(struct sk_buff *skb,
					struct sk_buff *nskb, u32 tcp_seq,
					int headln, __be64 rcd_sn)
{
	struct mlx5e_tls_metadata *pet;
	u8 syndrome = SYNDROME_SYNC;
	struct iphdr *iph;
	struct tcphdr *th;
	int data_len, mss;

	nskb->dev = skb->dev;
	skb_reset_mac_header(nskb);
	skb_set_network_header(nskb, skb_network_offset(skb));
	skb_set_transport_header(nskb, skb_transport_offset(skb));
	memcpy(nskb->data, skb->data, headln);
	memcpy(nskb->data + headln, &rcd_sn, sizeof(rcd_sn));

	iph = ip_hdr(nskb);
	iph->tot_len = htons(nskb->len - skb_network_offset(nskb));
	th = tcp_hdr(nskb);
	data_len = nskb->len - headln;
	tcp_seq -= data_len;
	th->seq = htonl(tcp_seq);

	mss = nskb->dev->mtu - (headln - skb_network_offset(nskb));
	skb_shinfo(nskb)->gso_size = 0;
	if (data_len > mss) {
		skb_shinfo(nskb)->gso_size = mss;
		skb_shinfo(nskb)->gso_segs = DIV_ROUND_UP(data_len, mss);
	}
	skb_shinfo(nskb)->gso_type = skb_shinfo(skb)->gso_type;

	pet = (struct mlx5e_tls_metadata *)(nskb->data + sizeof(struct ethhdr));
	memcpy(pet, &syndrome, sizeof(syndrome));
	pet->content.send.first_seq = htons(tcp_seq);

	/* MLX5 devices don't care about the checksum partial start, offset
	 * and pseudo header
	 */
	nskb->ip_summed = CHECKSUM_PARTIAL;

	nskb->queue_mapping = skb->queue_mapping;
}

static bool mlx5e_tls_handle_ooo(struct mlx5e_tls_offload_context_tx *context,
				 struct mlx5e_txqsq *sq, struct sk_buff *skb,
				 struct mlx5e_tls *tls)
{
	u32 tcp_seq = ntohl(tcp_hdr(skb)->seq);
	struct mlx5e_tx_wqe *wqe;
	struct sync_info info;
	struct sk_buff *nskb;
	int linear_len = 0;
	int headln;
	u16 pi;
	int i;

	sq->stats->tls_ooo++;

	if (mlx5e_tls_get_sync_data(context, tcp_seq, &info)) {
		/* We might get here if a retransmission reaches the driver
		 * after the relevant record is acked.
		 * It should be safe to drop the packet in this case
		 */
		atomic64_inc(&tls->sw_stats.tx_tls_drop_no_sync_data);
		goto err_out;
	}

	if (unlikely(info.sync_len < 0)) {
		u32 payload;

		headln = skb_transport_offset(skb) + tcp_hdrlen(skb);
		payload = skb->len - headln;
		if (likely(payload <= -info.sync_len))
			/* SKB payload doesn't require offload
			 */
			return true;

		atomic64_inc(&tls->sw_stats.tx_tls_drop_bypass_required);
		goto err_out;
	}

	if (unlikely(mlx5e_tls_add_metadata(skb, context->swid))) {
		atomic64_inc(&tls->sw_stats.tx_tls_drop_metadata);
		goto err_out;
	}

	headln = skb_transport_offset(skb) + tcp_hdrlen(skb);
	linear_len += headln + sizeof(info.rcd_sn);
	nskb = alloc_skb(linear_len, GFP_ATOMIC);
	if (unlikely(!nskb)) {
		atomic64_inc(&tls->sw_stats.tx_tls_drop_resync_alloc);
		goto err_out;
	}

	context->expected_seq = tcp_seq + skb->len - headln;
	skb_put(nskb, linear_len);
	for (i = 0; i < info.nr_frags; i++)
		skb_shinfo(nskb)->frags[i] = info.frags[i];

	skb_shinfo(nskb)->nr_frags = info.nr_frags;
	nskb->data_len = info.sync_len;
	nskb->len += info.sync_len;
	sq->stats->tls_resync_bytes += nskb->len;
	mlx5e_tls_complete_sync_skb(skb, nskb, tcp_seq, headln,
				    cpu_to_be64(info.rcd_sn));
	pi = mlx5_wq_cyc_ctr2ix(&sq->wq, sq->pc);
	wqe = MLX5E_TX_FETCH_WQE(sq, pi);
	mlx5e_sq_xmit(sq, nskb, wqe, pi, true);

	return true;

err_out:
	dev_kfree_skb_any(skb);
	return false;
}

bool mlx5e_tls_handle_tx_skb(struct net_device *netdev, struct mlx5e_txqsq *sq,
			     struct sk_buff *skb, struct mlx5e_accel_tx_tls_state *state)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_tls_offload_context_tx *context;
	struct tls_context *tls_ctx;
	u32 expected_seq;
	int datalen;
	u32 skb_seq;

	if (!skb->sk || !tls_is_sk_tx_device_offloaded(skb->sk))
		return true;

	datalen = skb->len - (skb_transport_offset(skb) + tcp_hdrlen(skb));
	if (!datalen)
		return true;

	tls_ctx = tls_get_ctx(skb->sk);
	if (WARN_ON_ONCE(tls_ctx->netdev != netdev))
		goto err_out;

	if (MLX5_CAP_GEN(sq->channel->mdev, tls_tx))
		return mlx5e_ktls_handle_tx_skb(tls_ctx, sq, skb, datalen, state);

	skb_seq = ntohl(tcp_hdr(skb)->seq);
	context = mlx5e_get_tls_tx_context(tls_ctx);
	expected_seq = context->expected_seq;

	if (unlikely(expected_seq != skb_seq))
		return mlx5e_tls_handle_ooo(context, sq, skb, priv->tls);

	if (unlikely(mlx5e_tls_add_metadata(skb, context->swid))) {
		atomic64_inc(&priv->tls->sw_stats.tx_tls_drop_metadata);
		dev_kfree_skb_any(skb);
		return false;
	}

	context->expected_seq = skb_seq + datalen;
	return true;

err_out:
	dev_kfree_skb_any(skb);
	return false;
}

void mlx5e_tls_handle_tx_wqe(struct mlx5e_txqsq *sq, struct mlx5_wqe_ctrl_seg *cseg,
			     struct mlx5e_accel_tx_tls_state *state)
{
	cseg->tisn = cpu_to_be32(state->tls_tisn << 8);
}

static int tls_update_resync_sn(struct net_device *netdev,
				struct sk_buff *skb,
				struct mlx5e_tls_metadata *mdata)
{
	struct sock *sk = NULL;
	struct iphdr *iph;
	struct tcphdr *th;
	__be32 seq;

	if (mdata->ethertype != htons(ETH_P_IP))
		return -EINVAL;

	iph = (struct iphdr *)(mdata + 1);

	th = ((void *)iph) + iph->ihl * 4;

	if (iph->version == 4) {
		sk = inet_lookup_established(dev_net(netdev), &tcp_hashinfo,
					     iph->saddr, th->source, iph->daddr,
					     th->dest, netdev->ifindex);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		struct ipv6hdr *ipv6h = (struct ipv6hdr *)iph;

		sk = __inet6_lookup_established(dev_net(netdev), &tcp_hashinfo,
						&ipv6h->saddr, th->source,
						&ipv6h->daddr, ntohs(th->dest),
						netdev->ifindex, 0);
#endif
	}
	if (!sk || sk->sk_state == TCP_TIME_WAIT) {
		struct mlx5e_priv *priv = netdev_priv(netdev);

		atomic64_inc(&priv->tls->sw_stats.rx_tls_drop_resync_request);
		goto out;
	}

	skb->sk = sk;
	skb->destructor = sock_edemux;

	memcpy(&seq, &mdata->content.recv.sync_seq, sizeof(seq));
	tls_offload_rx_resync_request(sk, seq);
out:
	return 0;
}

void mlx5e_tls_handle_rx_skb(struct net_device *netdev, struct sk_buff *skb,
			     u32 *cqe_bcnt)
{
	struct mlx5e_tls_metadata *mdata;
	struct mlx5e_priv *priv;

	if (!is_metadata_hdr_valid(skb))
		return;

	/* Use the metadata */
	mdata = (struct mlx5e_tls_metadata *)(skb->data + ETH_HLEN);
	switch (mdata->content.recv.syndrome) {
	case SYNDROM_DECRYPTED:
		skb->decrypted = 1;
		break;
	case SYNDROM_RESYNC_REQUEST:
		tls_update_resync_sn(netdev, skb, mdata);
		priv = netdev_priv(netdev);
		atomic64_inc(&priv->tls->sw_stats.rx_tls_resync_request);
		break;
	case SYNDROM_AUTH_FAILED:
		/* Authentication failure will be observed and verified by kTLS */
		priv = netdev_priv(netdev);
		atomic64_inc(&priv->tls->sw_stats.rx_tls_auth_fail);
		break;
	default:
		/* Bypass the metadata header to others */
		return;
	}

	remove_metadata_hdr(skb);
	*cqe_bcnt -= MLX5E_METADATA_ETHER_LEN;
}
