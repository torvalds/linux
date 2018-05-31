#include "en_accel/rxtx.h"

static void mlx5e_udp_gso_prepare_last_skb(struct sk_buff *skb,
					   struct sk_buff *nskb,
					   int remaining)
{
	int bytes_needed = remaining, remaining_headlen, remaining_page_offset;
	int headlen = skb_transport_offset(skb) + sizeof(struct udphdr);
	int payload_len = remaining + sizeof(struct udphdr);
	int k = 0, i, j;

	skb_copy_bits(skb, 0, nskb->data, headlen);
	nskb->dev = skb->dev;
	skb_reset_mac_header(nskb);
	skb_set_network_header(nskb, skb_network_offset(skb));
	skb_set_transport_header(nskb, skb_transport_offset(skb));
	skb_set_tail_pointer(nskb, headlen);

	/* How many frags do we need? */
	for (i = skb_shinfo(skb)->nr_frags - 1; i >= 0; i--) {
		bytes_needed -= skb_frag_size(&skb_shinfo(skb)->frags[i]);
		k++;
		if (bytes_needed <= 0)
			break;
	}

	/* Fill the first frag and split it if necessary */
	j = skb_shinfo(skb)->nr_frags - k;
	remaining_page_offset = -bytes_needed;
	skb_fill_page_desc(nskb, 0,
			   skb_shinfo(skb)->frags[j].page.p,
			   skb_shinfo(skb)->frags[j].page_offset + remaining_page_offset,
			   skb_shinfo(skb)->frags[j].size - remaining_page_offset);

	skb_frag_ref(skb, j);

	/* Fill the rest of the frags */
	for (i = 1; i < k; i++) {
		j = skb_shinfo(skb)->nr_frags - k + i;

		skb_fill_page_desc(nskb, i,
				   skb_shinfo(skb)->frags[j].page.p,
				   skb_shinfo(skb)->frags[j].page_offset,
				   skb_shinfo(skb)->frags[j].size);
		skb_frag_ref(skb, j);
	}
	skb_shinfo(nskb)->nr_frags = k;

	remaining_headlen = remaining - skb->data_len;

	/* headlen contains remaining data? */
	if (remaining_headlen > 0)
		skb_copy_bits(skb, skb->len - remaining, nskb->data + headlen,
			      remaining_headlen);
	nskb->len = remaining + headlen;
	nskb->data_len =  payload_len - sizeof(struct udphdr) +
		max_t(int, 0, remaining_headlen);
	nskb->protocol = skb->protocol;
	if (nskb->protocol == htons(ETH_P_IP)) {
		ip_hdr(nskb)->id = htons(ntohs(ip_hdr(nskb)->id) +
					 skb_shinfo(skb)->gso_segs);
		ip_hdr(nskb)->tot_len =
			htons(payload_len + sizeof(struct iphdr));
	} else {
		ipv6_hdr(nskb)->payload_len = htons(payload_len);
	}
	udp_hdr(nskb)->len = htons(payload_len);
	skb_shinfo(nskb)->gso_size = 0;
	nskb->ip_summed = skb->ip_summed;
	nskb->csum_start = skb->csum_start;
	nskb->csum_offset = skb->csum_offset;
	nskb->queue_mapping = skb->queue_mapping;
}

/* might send skbs and update wqe and pi */
struct sk_buff *mlx5e_udp_gso_handle_tx_skb(struct net_device *netdev,
					    struct mlx5e_txqsq *sq,
					    struct sk_buff *skb,
					    struct mlx5e_tx_wqe **wqe,
					    u16 *pi)
{
	int payload_len = skb_shinfo(skb)->gso_size + sizeof(struct udphdr);
	int headlen = skb_transport_offset(skb) + sizeof(struct udphdr);
	int remaining = (skb->len - headlen) % skb_shinfo(skb)->gso_size;
	struct sk_buff *nskb;

	if (skb->protocol == htons(ETH_P_IP))
		ip_hdr(skb)->tot_len = htons(payload_len + sizeof(struct iphdr));
	else
		ipv6_hdr(skb)->payload_len = htons(payload_len);
	udp_hdr(skb)->len = htons(payload_len);
	if (!remaining)
		return skb;

	nskb = alloc_skb(max_t(int, headlen, headlen + remaining - skb->data_len), GFP_ATOMIC);
	if (unlikely(!nskb)) {
		sq->stats->dropped++;
		return NULL;
	}

	mlx5e_udp_gso_prepare_last_skb(skb, nskb, remaining);

	skb_shinfo(skb)->gso_segs--;
	pskb_trim(skb, skb->len - remaining);
	mlx5e_sq_xmit(sq, skb, *wqe, *pi);
	mlx5e_sq_fetch_wqe(sq, wqe, pi);
	return nskb;
}
