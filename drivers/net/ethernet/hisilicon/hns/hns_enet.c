/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/cpumask.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>

#include "hnae.h"
#include "hns_enet.h"

#define NIC_MAX_Q_PER_VF 16
#define HNS_NIC_TX_TIMEOUT (5 * HZ)

#define SERVICE_TIMER_HZ (1 * HZ)

#define NIC_TX_CLEAN_MAX_NUM 256
#define NIC_RX_CLEAN_MAX_NUM 64

#define RCB_IRQ_NOT_INITED 0
#define RCB_IRQ_INITED 1
#define HNS_BUFFER_SIZE_2048 2048

#define BD_MAX_SEND_SIZE 8191
#define SKB_TMP_LEN(SKB) \
	(((SKB)->transport_header - (SKB)->mac_header) + tcp_hdrlen(SKB))

static void fill_v2_desc(struct hnae_ring *ring, void *priv,
			 int size, dma_addr_t dma, int frag_end,
			 int buf_num, enum hns_desc_type type, int mtu)
{
	struct hnae_desc *desc = &ring->desc[ring->next_to_use];
	struct hnae_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	struct iphdr *iphdr;
	struct ipv6hdr *ipv6hdr;
	struct sk_buff *skb;
	__be16 protocol;
	u8 bn_pid = 0;
	u8 rrcfv = 0;
	u8 ip_offset = 0;
	u8 tvsvsn = 0;
	u16 mss = 0;
	u8 l4_len = 0;
	u16 paylen = 0;

	desc_cb->priv = priv;
	desc_cb->length = size;
	desc_cb->dma = dma;
	desc_cb->type = type;

	desc->addr = cpu_to_le64(dma);
	desc->tx.send_size = cpu_to_le16((u16)size);

	/* config bd buffer end */
	hnae_set_bit(rrcfv, HNSV2_TXD_VLD_B, 1);
	hnae_set_field(bn_pid, HNSV2_TXD_BUFNUM_M, 0, buf_num - 1);

	/* fill port_id in the tx bd for sending management pkts */
	hnae_set_field(bn_pid, HNSV2_TXD_PORTID_M,
		       HNSV2_TXD_PORTID_S, ring->q->handle->dport_id);

	if (type == DESC_TYPE_SKB) {
		skb = (struct sk_buff *)priv;

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			skb_reset_mac_len(skb);
			protocol = skb->protocol;
			ip_offset = ETH_HLEN;

			if (protocol == htons(ETH_P_8021Q)) {
				ip_offset += VLAN_HLEN;
				protocol = vlan_get_protocol(skb);
				skb->protocol = protocol;
			}

			if (skb->protocol == htons(ETH_P_IP)) {
				iphdr = ip_hdr(skb);
				hnae_set_bit(rrcfv, HNSV2_TXD_L3CS_B, 1);
				hnae_set_bit(rrcfv, HNSV2_TXD_L4CS_B, 1);

				/* check for tcp/udp header */
				if (iphdr->protocol == IPPROTO_TCP &&
				    skb_is_gso(skb)) {
					hnae_set_bit(tvsvsn,
						     HNSV2_TXD_TSE_B, 1);
					l4_len = tcp_hdrlen(skb);
					mss = skb_shinfo(skb)->gso_size;
					paylen = skb->len - SKB_TMP_LEN(skb);
				}
			} else if (skb->protocol == htons(ETH_P_IPV6)) {
				hnae_set_bit(tvsvsn, HNSV2_TXD_IPV6_B, 1);
				ipv6hdr = ipv6_hdr(skb);
				hnae_set_bit(rrcfv, HNSV2_TXD_L4CS_B, 1);

				/* check for tcp/udp header */
				if (ipv6hdr->nexthdr == IPPROTO_TCP &&
				    skb_is_gso(skb) && skb_is_gso_v6(skb)) {
					hnae_set_bit(tvsvsn,
						     HNSV2_TXD_TSE_B, 1);
					l4_len = tcp_hdrlen(skb);
					mss = skb_shinfo(skb)->gso_size;
					paylen = skb->len - SKB_TMP_LEN(skb);
				}
			}
			desc->tx.ip_offset = ip_offset;
			desc->tx.tse_vlan_snap_v6_sctp_nth = tvsvsn;
			desc->tx.mss = cpu_to_le16(mss);
			desc->tx.l4_len = l4_len;
			desc->tx.paylen = cpu_to_le16(paylen);
		}
	}

	hnae_set_bit(rrcfv, HNSV2_TXD_FE_B, frag_end);

	desc->tx.bn_pid = bn_pid;
	desc->tx.ra_ri_cs_fe_vld = rrcfv;

	ring_ptr_move_fw(ring, next_to_use);
}

static const struct acpi_device_id hns_enet_acpi_match[] = {
	{ "HISI00C1", 0 },
	{ "HISI00C2", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, hns_enet_acpi_match);

static void fill_desc(struct hnae_ring *ring, void *priv,
		      int size, dma_addr_t dma, int frag_end,
		      int buf_num, enum hns_desc_type type, int mtu)
{
	struct hnae_desc *desc = &ring->desc[ring->next_to_use];
	struct hnae_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	struct sk_buff *skb;
	__be16 protocol;
	u32 ip_offset;
	u32 asid_bufnum_pid = 0;
	u32 flag_ipoffset = 0;

	desc_cb->priv = priv;
	desc_cb->length = size;
	desc_cb->dma = dma;
	desc_cb->type = type;

	desc->addr = cpu_to_le64(dma);
	desc->tx.send_size = cpu_to_le16((u16)size);

	/*config bd buffer end */
	flag_ipoffset |= 1 << HNS_TXD_VLD_B;

	asid_bufnum_pid |= buf_num << HNS_TXD_BUFNUM_S;

	if (type == DESC_TYPE_SKB) {
		skb = (struct sk_buff *)priv;

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			protocol = skb->protocol;
			ip_offset = ETH_HLEN;

			/*if it is a SW VLAN check the next protocol*/
			if (protocol == htons(ETH_P_8021Q)) {
				ip_offset += VLAN_HLEN;
				protocol = vlan_get_protocol(skb);
				skb->protocol = protocol;
			}

			if (skb->protocol == htons(ETH_P_IP)) {
				flag_ipoffset |= 1 << HNS_TXD_L3CS_B;
				/* check for tcp/udp header */
				flag_ipoffset |= 1 << HNS_TXD_L4CS_B;

			} else if (skb->protocol == htons(ETH_P_IPV6)) {
				/* ipv6 has not l3 cs, check for L4 header */
				flag_ipoffset |= 1 << HNS_TXD_L4CS_B;
			}

			flag_ipoffset |= ip_offset << HNS_TXD_IPOFFSET_S;
		}
	}

	flag_ipoffset |= frag_end << HNS_TXD_FE_B;

	desc->tx.asid_bufnum_pid = cpu_to_le16(asid_bufnum_pid);
	desc->tx.flag_ipoffset = cpu_to_le32(flag_ipoffset);

	ring_ptr_move_fw(ring, next_to_use);
}

static void unfill_desc(struct hnae_ring *ring)
{
	ring_ptr_move_bw(ring, next_to_use);
}

static int hns_nic_maybe_stop_tx(
	struct sk_buff **out_skb, int *bnum, struct hnae_ring *ring)
{
	struct sk_buff *skb = *out_skb;
	struct sk_buff *new_skb = NULL;
	int buf_num;

	/* no. of segments (plus a header) */
	buf_num = skb_shinfo(skb)->nr_frags + 1;

	if (unlikely(buf_num > ring->max_desc_num_per_pkt)) {
		if (ring_space(ring) < 1)
			return -EBUSY;

		new_skb = skb_copy(skb, GFP_ATOMIC);
		if (!new_skb)
			return -ENOMEM;

		dev_kfree_skb_any(skb);
		*out_skb = new_skb;
		buf_num = 1;
	} else if (buf_num > ring_space(ring)) {
		return -EBUSY;
	}

	*bnum = buf_num;
	return 0;
}

static int hns_nic_maybe_stop_tso(
	struct sk_buff **out_skb, int *bnum, struct hnae_ring *ring)
{
	int i;
	int size;
	int buf_num;
	int frag_num;
	struct sk_buff *skb = *out_skb;
	struct sk_buff *new_skb = NULL;
	struct skb_frag_struct *frag;

	size = skb_headlen(skb);
	buf_num = (size + BD_MAX_SEND_SIZE - 1) / BD_MAX_SEND_SIZE;

	frag_num = skb_shinfo(skb)->nr_frags;
	for (i = 0; i < frag_num; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		size = skb_frag_size(frag);
		buf_num += (size + BD_MAX_SEND_SIZE - 1) / BD_MAX_SEND_SIZE;
	}

	if (unlikely(buf_num > ring->max_desc_num_per_pkt)) {
		buf_num = (skb->len + BD_MAX_SEND_SIZE - 1) / BD_MAX_SEND_SIZE;
		if (ring_space(ring) < buf_num)
			return -EBUSY;
		/* manual split the send packet */
		new_skb = skb_copy(skb, GFP_ATOMIC);
		if (!new_skb)
			return -ENOMEM;
		dev_kfree_skb_any(skb);
		*out_skb = new_skb;

	} else if (ring_space(ring) < buf_num) {
		return -EBUSY;
	}

	*bnum = buf_num;
	return 0;
}

static void fill_tso_desc(struct hnae_ring *ring, void *priv,
			  int size, dma_addr_t dma, int frag_end,
			  int buf_num, enum hns_desc_type type, int mtu)
{
	int frag_buf_num;
	int sizeoflast;
	int k;

	frag_buf_num = (size + BD_MAX_SEND_SIZE - 1) / BD_MAX_SEND_SIZE;
	sizeoflast = size % BD_MAX_SEND_SIZE;
	sizeoflast = sizeoflast ? sizeoflast : BD_MAX_SEND_SIZE;

	/* when the frag size is bigger than hardware, split this frag */
	for (k = 0; k < frag_buf_num; k++)
		fill_v2_desc(ring, priv,
			     (k == frag_buf_num - 1) ?
					sizeoflast : BD_MAX_SEND_SIZE,
			     dma + BD_MAX_SEND_SIZE * k,
			     frag_end && (k == frag_buf_num - 1) ? 1 : 0,
			     buf_num,
			     (type == DESC_TYPE_SKB && !k) ?
					DESC_TYPE_SKB : DESC_TYPE_PAGE,
			     mtu);
}

int hns_nic_net_xmit_hw(struct net_device *ndev,
			struct sk_buff *skb,
			struct hns_nic_ring_data *ring_data)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct device *dev = priv->dev;
	struct hnae_ring *ring = ring_data->ring;
	struct netdev_queue *dev_queue;
	struct skb_frag_struct *frag;
	int buf_num;
	int seg_num;
	dma_addr_t dma;
	int size, next_to_use;
	int i;

	switch (priv->ops.maybe_stop_tx(&skb, &buf_num, ring)) {
	case -EBUSY:
		ring->stats.tx_busy++;
		goto out_net_tx_busy;
	case -ENOMEM:
		ring->stats.sw_err_cnt++;
		netdev_err(ndev, "no memory to xmit!\n");
		goto out_err_tx_ok;
	default:
		break;
	}

	/* no. of segments (plus a header) */
	seg_num = skb_shinfo(skb)->nr_frags + 1;
	next_to_use = ring->next_to_use;

	/* fill the first part */
	size = skb_headlen(skb);
	dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma)) {
		netdev_err(ndev, "TX head DMA map failed\n");
		ring->stats.sw_err_cnt++;
		goto out_err_tx_ok;
	}
	priv->ops.fill_desc(ring, skb, size, dma, seg_num == 1 ? 1 : 0,
			    buf_num, DESC_TYPE_SKB, ndev->mtu);

	/* fill the fragments */
	for (i = 1; i < seg_num; i++) {
		frag = &skb_shinfo(skb)->frags[i - 1];
		size = skb_frag_size(frag);
		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma)) {
			netdev_err(ndev, "TX frag(%d) DMA map failed\n", i);
			ring->stats.sw_err_cnt++;
			goto out_map_frag_fail;
		}
		priv->ops.fill_desc(ring, skb_frag_page(frag), size, dma,
				    seg_num - 1 == i ? 1 : 0, buf_num,
				    DESC_TYPE_PAGE, ndev->mtu);
	}

	/*complete translate all packets*/
	dev_queue = netdev_get_tx_queue(ndev, skb->queue_mapping);
	netdev_tx_sent_queue(dev_queue, skb->len);

	wmb(); /* commit all data before submit */
	assert(skb->queue_mapping < priv->ae_handle->q_num);
	hnae_queue_xmit(priv->ae_handle->qs[skb->queue_mapping], buf_num);
	ring->stats.tx_pkts++;
	ring->stats.tx_bytes += skb->len;

	return NETDEV_TX_OK;

out_map_frag_fail:

	while (ring->next_to_use != next_to_use) {
		unfill_desc(ring);
		if (ring->next_to_use != next_to_use)
			dma_unmap_page(dev,
				       ring->desc_cb[ring->next_to_use].dma,
				       ring->desc_cb[ring->next_to_use].length,
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(dev,
					 ring->desc_cb[next_to_use].dma,
					 ring->desc_cb[next_to_use].length,
					 DMA_TO_DEVICE);
	}

out_err_tx_ok:

	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;

out_net_tx_busy:

	netif_stop_subqueue(ndev, skb->queue_mapping);

	/* Herbert's original patch had:
	 *  smp_mb__after_netif_stop_queue();
	 * but since that doesn't exist yet, just open code it.
	 */
	smp_mb();
	return NETDEV_TX_BUSY;
}

/**
 * hns_nic_get_headlen - determine size of header for RSC/LRO/GRO/FCOE
 * @data: pointer to the start of the headers
 * @max: total length of section to find headers in
 *
 * This function is meant to determine the length of headers that will
 * be recognized by hardware for LRO, GRO, and RSC offloads.  The main
 * motivation of doing this is to only perform one pull for IPv4 TCP
 * packets so that we can do basic things like calculating the gso_size
 * based on the average data per packet.
 **/
static unsigned int hns_nic_get_headlen(unsigned char *data, u32 flag,
					unsigned int max_size)
{
	unsigned char *network;
	u8 hlen;

	/* this should never happen, but better safe than sorry */
	if (max_size < ETH_HLEN)
		return max_size;

	/* initialize network frame pointer */
	network = data;

	/* set first protocol and move network header forward */
	network += ETH_HLEN;

	/* handle any vlan tag if present */
	if (hnae_get_field(flag, HNS_RXD_VLAN_M, HNS_RXD_VLAN_S)
		== HNS_RX_FLAG_VLAN_PRESENT) {
		if ((typeof(max_size))(network - data) > (max_size - VLAN_HLEN))
			return max_size;

		network += VLAN_HLEN;
	}

	/* handle L3 protocols */
	if (hnae_get_field(flag, HNS_RXD_L3ID_M, HNS_RXD_L3ID_S)
		== HNS_RX_FLAG_L3ID_IPV4) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct iphdr)))
			return max_size;

		/* access ihl as a u8 to avoid unaligned access on ia64 */
		hlen = (network[0] & 0x0F) << 2;

		/* verify hlen meets minimum size requirements */
		if (hlen < sizeof(struct iphdr))
			return network - data;

		/* record next protocol if header is present */
	} else if (hnae_get_field(flag, HNS_RXD_L3ID_M, HNS_RXD_L3ID_S)
		== HNS_RX_FLAG_L3ID_IPV6) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct ipv6hdr)))
			return max_size;

		/* record next protocol */
		hlen = sizeof(struct ipv6hdr);
	} else {
		return network - data;
	}

	/* relocate pointer to start of L4 header */
	network += hlen;

	/* finally sort out TCP/UDP */
	if (hnae_get_field(flag, HNS_RXD_L4ID_M, HNS_RXD_L4ID_S)
		== HNS_RX_FLAG_L4ID_TCP) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct tcphdr)))
			return max_size;

		/* access doff as a u8 to avoid unaligned access on ia64 */
		hlen = (network[12] & 0xF0) >> 2;

		/* verify hlen meets minimum size requirements */
		if (hlen < sizeof(struct tcphdr))
			return network - data;

		network += hlen;
	} else if (hnae_get_field(flag, HNS_RXD_L4ID_M, HNS_RXD_L4ID_S)
		== HNS_RX_FLAG_L4ID_UDP) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct udphdr)))
			return max_size;

		network += sizeof(struct udphdr);
	}

	/* If everything has gone correctly network should be the
	 * data section of the packet and will be the end of the header.
	 * If not then it probably represents the end of the last recognized
	 * header.
	 */
	if ((typeof(max_size))(network - data) < max_size)
		return network - data;
	else
		return max_size;
}

static void hns_nic_reuse_page(struct sk_buff *skb, int i,
			       struct hnae_ring *ring, int pull_len,
			       struct hnae_desc_cb *desc_cb)
{
	struct hnae_desc *desc;
	int truesize, size;
	int last_offset;
	bool twobufs;

	twobufs = ((PAGE_SIZE < 8192) && hnae_buf_size(ring) == HNS_BUFFER_SIZE_2048);

	desc = &ring->desc[ring->next_to_clean];
	size = le16_to_cpu(desc->rx.size);

	if (twobufs) {
		truesize = hnae_buf_size(ring);
	} else {
		truesize = ALIGN(size, L1_CACHE_BYTES);
		last_offset = hnae_page_size(ring) - hnae_buf_size(ring);
	}

	skb_add_rx_frag(skb, i, desc_cb->priv, desc_cb->page_offset + pull_len,
			size - pull_len, truesize - pull_len);

	 /* avoid re-using remote pages,flag default unreuse */
	if (unlikely(page_to_nid(desc_cb->priv) != numa_node_id()))
		return;

	if (twobufs) {
		/* if we are only owner of page we can reuse it */
		if (likely(page_count(desc_cb->priv) == 1)) {
			/* flip page offset to other buffer */
			desc_cb->page_offset ^= truesize;

			desc_cb->reuse_flag = 1;
			/* bump ref count on page before it is given*/
			get_page(desc_cb->priv);
		}
		return;
	}

	/* move offset up to the next cache line */
	desc_cb->page_offset += truesize;

	if (desc_cb->page_offset <= last_offset) {
		desc_cb->reuse_flag = 1;
		/* bump ref count on page before it is given*/
		get_page(desc_cb->priv);
	}
}

static void get_v2rx_desc_bnum(u32 bnum_flag, int *out_bnum)
{
	*out_bnum = hnae_get_field(bnum_flag,
				   HNS_RXD_BUFNUM_M, HNS_RXD_BUFNUM_S) + 1;
}

static void get_rx_desc_bnum(u32 bnum_flag, int *out_bnum)
{
	*out_bnum = hnae_get_field(bnum_flag,
				   HNS_RXD_BUFNUM_M, HNS_RXD_BUFNUM_S);
}

static int hns_nic_poll_rx_skb(struct hns_nic_ring_data *ring_data,
			       struct sk_buff **out_skb, int *out_bnum)
{
	struct hnae_ring *ring = ring_data->ring;
	struct net_device *ndev = ring_data->napi.dev;
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb;
	struct hnae_desc *desc;
	struct hnae_desc_cb *desc_cb;
	struct ethhdr *eh;
	unsigned char *va;
	int bnum, length, i;
	int pull_len;
	u32 bnum_flag;

	desc = &ring->desc[ring->next_to_clean];
	desc_cb = &ring->desc_cb[ring->next_to_clean];

	prefetch(desc);

	va = (unsigned char *)desc_cb->buf + desc_cb->page_offset;

	/* prefetch first cache line of first page */
	prefetch(va);
#if L1_CACHE_BYTES < 128
	prefetch(va + L1_CACHE_BYTES);
#endif

	skb = *out_skb = napi_alloc_skb(&ring_data->napi,
					HNS_RX_HEAD_SIZE);
	if (unlikely(!skb)) {
		netdev_err(ndev, "alloc rx skb fail\n");
		ring->stats.sw_err_cnt++;
		return -ENOMEM;
	}
	skb_reset_mac_header(skb);

	prefetchw(skb->data);
	length = le16_to_cpu(desc->rx.pkt_len);
	bnum_flag = le32_to_cpu(desc->rx.ipoff_bnum_pid_flag);
	priv->ops.get_rxd_bnum(bnum_flag, &bnum);
	*out_bnum = bnum;

	if (length <= HNS_RX_HEAD_SIZE) {
		memcpy(__skb_put(skb, length), va, ALIGN(length, sizeof(long)));

		/* we can reuse buffer as-is, just make sure it is local */
		if (likely(page_to_nid(desc_cb->priv) == numa_node_id()))
			desc_cb->reuse_flag = 1;
		else /* this page cannot be reused so discard it */
			put_page(desc_cb->priv);

		ring_ptr_move_fw(ring, next_to_clean);

		if (unlikely(bnum != 1)) { /* check err*/
			*out_bnum = 1;
			goto out_bnum_err;
		}
	} else {
		ring->stats.seg_pkt_cnt++;

		pull_len = hns_nic_get_headlen(va, bnum_flag, HNS_RX_HEAD_SIZE);
		memcpy(__skb_put(skb, pull_len), va,
		       ALIGN(pull_len, sizeof(long)));

		hns_nic_reuse_page(skb, 0, ring, pull_len, desc_cb);
		ring_ptr_move_fw(ring, next_to_clean);

		if (unlikely(bnum >= (int)MAX_SKB_FRAGS)) { /* check err*/
			*out_bnum = 1;
			goto out_bnum_err;
		}
		for (i = 1; i < bnum; i++) {
			desc = &ring->desc[ring->next_to_clean];
			desc_cb = &ring->desc_cb[ring->next_to_clean];

			hns_nic_reuse_page(skb, i, ring, 0, desc_cb);
			ring_ptr_move_fw(ring, next_to_clean);
		}
	}

	/* check except process, free skb and jump the desc */
	if (unlikely((!bnum) || (bnum > ring->max_desc_num_per_pkt))) {
out_bnum_err:
		*out_bnum = *out_bnum ? *out_bnum : 1; /* ntc moved,cannot 0*/
		netdev_err(ndev, "invalid bnum(%d,%d,%d,%d),%016llx,%016llx\n",
			   bnum, ring->max_desc_num_per_pkt,
			   length, (int)MAX_SKB_FRAGS,
			   ((u64 *)desc)[0], ((u64 *)desc)[1]);
		ring->stats.err_bd_num++;
		dev_kfree_skb_any(skb);
		return -EDOM;
	}

	bnum_flag = le32_to_cpu(desc->rx.ipoff_bnum_pid_flag);

	if (unlikely(!hnae_get_bit(bnum_flag, HNS_RXD_VLD_B))) {
		netdev_err(ndev, "no valid bd,%016llx,%016llx\n",
			   ((u64 *)desc)[0], ((u64 *)desc)[1]);
		ring->stats.non_vld_descs++;
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	if (unlikely((!desc->rx.pkt_len) ||
		     hnae_get_bit(bnum_flag, HNS_RXD_DROP_B))) {
		ring->stats.err_pkt_len++;
		dev_kfree_skb_any(skb);
		return -EFAULT;
	}

	if (unlikely(hnae_get_bit(bnum_flag, HNS_RXD_L2E_B))) {
		ring->stats.l2_err++;
		dev_kfree_skb_any(skb);
		return -EFAULT;
	}

	/* filter out multicast pkt with the same src mac as this port */
	eh = eth_hdr(skb);
	if (unlikely(is_multicast_ether_addr(eh->h_dest) &&
		     ether_addr_equal(ndev->dev_addr, eh->h_source))) {
		dev_kfree_skb_any(skb);
		return -EFAULT;
	}

	ring->stats.rx_pkts++;
	ring->stats.rx_bytes += skb->len;

	if (unlikely(hnae_get_bit(bnum_flag, HNS_RXD_L3E_B) ||
		     hnae_get_bit(bnum_flag, HNS_RXD_L4E_B))) {
		ring->stats.l3l4_csum_err++;
		return 0;
	}

	skb->ip_summed = CHECKSUM_UNNECESSARY;

	return 0;
}

static void
hns_nic_alloc_rx_buffers(struct hns_nic_ring_data *ring_data, int cleand_count)
{
	int i, ret;
	struct hnae_desc_cb res_cbs;
	struct hnae_desc_cb *desc_cb;
	struct hnae_ring *ring = ring_data->ring;
	struct net_device *ndev = ring_data->napi.dev;

	for (i = 0; i < cleand_count; i++) {
		desc_cb = &ring->desc_cb[ring->next_to_use];
		if (desc_cb->reuse_flag) {
			ring->stats.reuse_pg_cnt++;
			hnae_reuse_buffer(ring, ring->next_to_use);
		} else {
			ret = hnae_reserve_buffer_map(ring, &res_cbs);
			if (ret) {
				ring->stats.sw_err_cnt++;
				netdev_err(ndev, "hnae reserve buffer map failed.\n");
				break;
			}
			hnae_replace_buffer(ring, ring->next_to_use, &res_cbs);
		}

		ring_ptr_move_fw(ring, next_to_use);
	}

	wmb(); /* make all data has been write before submit */
	writel_relaxed(i, ring->io_base + RCB_REG_HEAD);
}

/* return error number for error or number of desc left to take
 */
static void hns_nic_rx_up_pro(struct hns_nic_ring_data *ring_data,
			      struct sk_buff *skb)
{
	struct net_device *ndev = ring_data->napi.dev;

	skb->protocol = eth_type_trans(skb, ndev);
	(void)napi_gro_receive(&ring_data->napi, skb);
	ndev->last_rx = jiffies;
}

static int hns_nic_rx_poll_one(struct hns_nic_ring_data *ring_data,
			       int budget, void *v)
{
	struct hnae_ring *ring = ring_data->ring;
	struct sk_buff *skb;
	int num, bnum, ex_num;
#define RCB_NOF_ALLOC_RX_BUFF_ONCE 16
	int recv_pkts, recv_bds, clean_count, err;

	num = readl_relaxed(ring->io_base + RCB_REG_FBDNUM);
	rmb(); /* make sure num taken effect before the other data is touched */

	recv_pkts = 0, recv_bds = 0, clean_count = 0;
recv:
	while (recv_pkts < budget && recv_bds < num) {
		/* reuse or realloc buffers */
		if (clean_count >= RCB_NOF_ALLOC_RX_BUFF_ONCE) {
			hns_nic_alloc_rx_buffers(ring_data, clean_count);
			clean_count = 0;
		}

		/* poll one pkt */
		err = hns_nic_poll_rx_skb(ring_data, &skb, &bnum);
		if (unlikely(!skb)) /* this fault cannot be repaired */
			goto out;

		recv_bds += bnum;
		clean_count += bnum;
		if (unlikely(err)) {  /* do jump the err */
			recv_pkts++;
			continue;
		}

		/* do update ip stack process*/
		((void (*)(struct hns_nic_ring_data *, struct sk_buff *))v)(
							ring_data, skb);
		recv_pkts++;
	}

	/* make all data has been write before submit */
	if (recv_pkts < budget) {
		ex_num = readl_relaxed(ring->io_base + RCB_REG_FBDNUM);

		if (ex_num > clean_count) {
			num += ex_num - clean_count;
			rmb(); /*complete read rx ring bd number*/
			goto recv;
		}
	}

out:
	/* make all data has been write before submit */
	if (clean_count > 0)
		hns_nic_alloc_rx_buffers(ring_data, clean_count);

	return recv_pkts;
}

static void hns_nic_rx_fini_pro(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	int num = 0;

	/* for hardware bug fixed */
	num = readl_relaxed(ring->io_base + RCB_REG_FBDNUM);

	if (num > 0) {
		ring_data->ring->q->handle->dev->ops->toggle_ring_irq(
			ring_data->ring, 1);

		napi_schedule(&ring_data->napi);
	}
}

static inline void hns_nic_reclaim_one_desc(struct hnae_ring *ring,
					    int *bytes, int *pkts)
{
	struct hnae_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_clean];

	(*pkts) += (desc_cb->type == DESC_TYPE_SKB);
	(*bytes) += desc_cb->length;
	/* desc_cb will be cleaned, after hnae_free_buffer_detach*/
	hnae_free_buffer_detach(ring, ring->next_to_clean);

	ring_ptr_move_fw(ring, next_to_clean);
}

static int is_valid_clean_head(struct hnae_ring *ring, int h)
{
	int u = ring->next_to_use;
	int c = ring->next_to_clean;

	if (unlikely(h > ring->desc_num))
		return 0;

	assert(u > 0 && u < ring->desc_num);
	assert(c > 0 && c < ring->desc_num);
	assert(u != c && h != c); /* must be checked before call this func */

	return u > c ? (h > c && h <= u) : (h > c || h <= u);
}

/* netif_tx_lock will turn down the performance, set only when necessary */
#ifdef CONFIG_NET_POLL_CONTROLLER
#define NETIF_TX_LOCK(ndev) netif_tx_lock(ndev)
#define NETIF_TX_UNLOCK(ndev) netif_tx_unlock(ndev)
#else
#define NETIF_TX_LOCK(ndev)
#define NETIF_TX_UNLOCK(ndev)
#endif
/* reclaim all desc in one budget
 * return error or number of desc left
 */
static int hns_nic_tx_poll_one(struct hns_nic_ring_data *ring_data,
			       int budget, void *v)
{
	struct hnae_ring *ring = ring_data->ring;
	struct net_device *ndev = ring_data->napi.dev;
	struct netdev_queue *dev_queue;
	struct hns_nic_priv *priv = netdev_priv(ndev);
	int head;
	int bytes, pkts;

	NETIF_TX_LOCK(ndev);

	head = readl_relaxed(ring->io_base + RCB_REG_HEAD);
	rmb(); /* make sure head is ready before touch any data */

	if (is_ring_empty(ring) || head == ring->next_to_clean) {
		NETIF_TX_UNLOCK(ndev);
		return 0; /* no data to poll */
	}

	if (!is_valid_clean_head(ring, head)) {
		netdev_err(ndev, "wrong head (%d, %d-%d)\n", head,
			   ring->next_to_use, ring->next_to_clean);
		ring->stats.io_err_cnt++;
		NETIF_TX_UNLOCK(ndev);
		return -EIO;
	}

	bytes = 0;
	pkts = 0;
	while (head != ring->next_to_clean) {
		hns_nic_reclaim_one_desc(ring, &bytes, &pkts);
		/* issue prefetch for next Tx descriptor */
		prefetch(&ring->desc_cb[ring->next_to_clean]);
	}

	NETIF_TX_UNLOCK(ndev);

	dev_queue = netdev_get_tx_queue(ndev, ring_data->queue_index);
	netdev_tx_completed_queue(dev_queue, pkts, bytes);

	if (unlikely(priv->link && !netif_carrier_ok(ndev)))
		netif_carrier_on(ndev);

	if (unlikely(pkts && netif_carrier_ok(ndev) &&
		     (ring_space(ring) >= ring->max_desc_num_per_pkt * 2))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (netif_tx_queue_stopped(dev_queue) &&
		    !test_bit(NIC_STATE_DOWN, &priv->state)) {
			netif_tx_wake_queue(dev_queue);
			ring->stats.restart_queue++;
		}
	}
	return 0;
}

static void hns_nic_tx_fini_pro(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	int head = readl_relaxed(ring->io_base + RCB_REG_HEAD);

	if (head != ring->next_to_clean) {
		ring_data->ring->q->handle->dev->ops->toggle_ring_irq(
			ring_data->ring, 1);

		napi_schedule(&ring_data->napi);
	}
}

static void hns_nic_tx_clr_all_bufs(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	struct net_device *ndev = ring_data->napi.dev;
	struct netdev_queue *dev_queue;
	int head;
	int bytes, pkts;

	NETIF_TX_LOCK(ndev);

	head = ring->next_to_use; /* ntu :soft setted ring position*/
	bytes = 0;
	pkts = 0;
	while (head != ring->next_to_clean)
		hns_nic_reclaim_one_desc(ring, &bytes, &pkts);

	NETIF_TX_UNLOCK(ndev);

	dev_queue = netdev_get_tx_queue(ndev, ring_data->queue_index);
	netdev_tx_reset_queue(dev_queue);
}

static int hns_nic_common_poll(struct napi_struct *napi, int budget)
{
	struct hns_nic_ring_data *ring_data =
		container_of(napi, struct hns_nic_ring_data, napi);
	int clean_complete = ring_data->poll_one(
				ring_data, budget, ring_data->ex_process);

	if (clean_complete >= 0 && clean_complete < budget) {
		napi_complete(napi);
		ring_data->ring->q->handle->dev->ops->toggle_ring_irq(
			ring_data->ring, 0);
		if (ring_data->fini_process)
			ring_data->fini_process(ring_data);
		return 0;
	}

	return clean_complete;
}

static irqreturn_t hns_irq_handle(int irq, void *dev)
{
	struct hns_nic_ring_data *ring_data = (struct hns_nic_ring_data *)dev;

	ring_data->ring->q->handle->dev->ops->toggle_ring_irq(
		ring_data->ring, 1);
	napi_schedule(&ring_data->napi);

	return IRQ_HANDLED;
}

/**
 *hns_nic_adjust_link - adjust net work mode by the phy stat or new param
 *@ndev: net device
 */
static void hns_nic_adjust_link(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	int state = 1;

	if (ndev->phydev) {
		h->dev->ops->adjust_link(h, ndev->phydev->speed,
					 ndev->phydev->duplex);
		state = ndev->phydev->link;
	}
	state = state && h->dev->ops->get_status(h);

	if (state != priv->link) {
		if (state) {
			netif_carrier_on(ndev);
			netif_tx_wake_all_queues(ndev);
			netdev_info(ndev, "link up\n");
		} else {
			netif_carrier_off(ndev);
			netdev_info(ndev, "link down\n");
		}
		priv->link = state;
	}
}

/**
 *hns_nic_init_phy - init phy
 *@ndev: net device
 *@h: ae handle
 * Return 0 on success, negative on failure
 */
int hns_nic_init_phy(struct net_device *ndev, struct hnae_handle *h)
{
	struct phy_device *phy_dev = h->phy_dev;
	int ret;

	if (!h->phy_dev)
		return 0;

	if (h->phy_if != PHY_INTERFACE_MODE_XGMII) {
		phy_dev->dev_flags = 0;

		ret = phy_connect_direct(ndev, phy_dev, hns_nic_adjust_link,
					 h->phy_if);
	} else {
		ret = phy_attach_direct(ndev, phy_dev, 0, h->phy_if);
	}
	if (unlikely(ret))
		return -ENODEV;

	phy_dev->supported &= h->if_support;
	phy_dev->advertising = phy_dev->supported;

	if (h->phy_if == PHY_INTERFACE_MODE_XGMII)
		phy_dev->autoneg = false;

	return 0;
}

static int hns_nic_ring_open(struct net_device *netdev, int idx)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;

	napi_enable(&priv->ring_data[idx].napi);

	enable_irq(priv->ring_data[idx].ring->irq);
	h->dev->ops->toggle_ring_irq(priv->ring_data[idx].ring, 0);

	return 0;
}

static int hns_nic_net_set_mac_address(struct net_device *ndev, void *p)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	struct sockaddr *mac_addr = p;
	int ret;

	if (!mac_addr || !is_valid_ether_addr((const u8 *)mac_addr->sa_data))
		return -EADDRNOTAVAIL;

	ret = h->dev->ops->set_mac_addr(h, mac_addr->sa_data);
	if (ret) {
		netdev_err(ndev, "set_mac_address fail, ret=%d!\n", ret);
		return ret;
	}

	memcpy(ndev->dev_addr, mac_addr->sa_data, ndev->addr_len);

	return 0;
}

void hns_nic_update_stats(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;

	h->dev->ops->update_stats(h, &netdev->stats);
}

/* set mac addr if it is configed. or leave it to the AE driver */
static void hns_init_mac_addr(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);

	if (!device_get_mac_address(priv->dev, ndev->dev_addr, ETH_ALEN)) {
		eth_hw_addr_random(ndev);
		dev_warn(priv->dev, "No valid mac, use random mac %pM",
			 ndev->dev_addr);
	}
}

static void hns_nic_ring_close(struct net_device *netdev, int idx)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;

	h->dev->ops->toggle_ring_irq(priv->ring_data[idx].ring, 1);
	disable_irq(priv->ring_data[idx].ring->irq);

	napi_disable(&priv->ring_data[idx].napi);
}

static void hns_set_irq_affinity(struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;
	struct hns_nic_ring_data *rd;
	int i;
	int cpu;
	cpumask_t mask;

	/*diffrent irq banlance for 16core and 32core*/
	if (h->q_num == num_possible_cpus()) {
		for (i = 0; i < h->q_num * 2; i++) {
			rd = &priv->ring_data[i];
			if (cpu_online(rd->queue_index)) {
				cpumask_clear(&mask);
				cpu = rd->queue_index;
				cpumask_set_cpu(cpu, &mask);
				(void)irq_set_affinity_hint(rd->ring->irq,
							    &mask);
			}
		}
	} else {
		for (i = 0; i < h->q_num; i++) {
			rd = &priv->ring_data[i];
			if (cpu_online(rd->queue_index * 2)) {
				cpumask_clear(&mask);
				cpu = rd->queue_index * 2;
				cpumask_set_cpu(cpu, &mask);
				(void)irq_set_affinity_hint(rd->ring->irq,
							    &mask);
			}
		}

		for (i = h->q_num; i < h->q_num * 2; i++) {
			rd = &priv->ring_data[i];
			if (cpu_online(rd->queue_index * 2 + 1)) {
				cpumask_clear(&mask);
				cpu = rd->queue_index * 2 + 1;
				cpumask_set_cpu(cpu, &mask);
				(void)irq_set_affinity_hint(rd->ring->irq,
							    &mask);
			}
		}
	}
}

static int hns_nic_init_irq(struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;
	struct hns_nic_ring_data *rd;
	int i;
	int ret;

	for (i = 0; i < h->q_num * 2; i++) {
		rd = &priv->ring_data[i];

		if (rd->ring->irq_init_flag == RCB_IRQ_INITED)
			break;

		snprintf(rd->ring->ring_name, RCB_RING_NAME_LEN,
			 "%s-%s%d", priv->netdev->name,
			 (i < h->q_num ? "tx" : "rx"), rd->queue_index);

		rd->ring->ring_name[RCB_RING_NAME_LEN - 1] = '\0';

		ret = request_irq(rd->ring->irq,
				  hns_irq_handle, 0, rd->ring->ring_name, rd);
		if (ret) {
			netdev_err(priv->netdev, "request irq(%d) fail\n",
				   rd->ring->irq);
			return ret;
		}
		disable_irq(rd->ring->irq);
		rd->ring->irq_init_flag = RCB_IRQ_INITED;
	}

	/*set cpu affinity*/
	hns_set_irq_affinity(priv);

	return 0;
}

static int hns_nic_net_up(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	int i, j;
	int ret;

	ret = hns_nic_init_irq(priv);
	if (ret != 0) {
		netdev_err(ndev, "hns init irq failed! ret=%d\n", ret);
		return ret;
	}

	for (i = 0; i < h->q_num * 2; i++) {
		ret = hns_nic_ring_open(ndev, i);
		if (ret)
			goto out_has_some_queues;
	}

	ret = h->dev->ops->set_mac_addr(h, ndev->dev_addr);
	if (ret)
		goto out_set_mac_addr_err;

	ret = h->dev->ops->start ? h->dev->ops->start(h) : 0;
	if (ret)
		goto out_start_err;

	if (ndev->phydev)
		phy_start(ndev->phydev);

	clear_bit(NIC_STATE_DOWN, &priv->state);
	(void)mod_timer(&priv->service_timer, jiffies + SERVICE_TIMER_HZ);

	return 0;

out_start_err:
	netif_stop_queue(ndev);
out_set_mac_addr_err:
out_has_some_queues:
	for (j = i - 1; j >= 0; j--)
		hns_nic_ring_close(ndev, j);

	set_bit(NIC_STATE_DOWN, &priv->state);

	return ret;
}

static void hns_nic_net_down(struct net_device *ndev)
{
	int i;
	struct hnae_ae_ops *ops;
	struct hns_nic_priv *priv = netdev_priv(ndev);

	if (test_and_set_bit(NIC_STATE_DOWN, &priv->state))
		return;

	(void)del_timer_sync(&priv->service_timer);
	netif_tx_stop_all_queues(ndev);
	netif_carrier_off(ndev);
	netif_tx_disable(ndev);
	priv->link = 0;

	if (ndev->phydev)
		phy_stop(ndev->phydev);

	ops = priv->ae_handle->dev->ops;

	if (ops->stop)
		ops->stop(priv->ae_handle);

	netif_tx_stop_all_queues(ndev);

	for (i = priv->ae_handle->q_num - 1; i >= 0; i--) {
		hns_nic_ring_close(ndev, i);
		hns_nic_ring_close(ndev, i + priv->ae_handle->q_num);

		/* clean tx buffers*/
		hns_nic_tx_clr_all_bufs(priv->ring_data + i);
	}
}

void hns_nic_net_reset(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *handle = priv->ae_handle;

	while (test_and_set_bit(NIC_STATE_RESETTING, &priv->state))
		usleep_range(1000, 2000);

	(void)hnae_reinit_handle(handle);

	clear_bit(NIC_STATE_RESETTING, &priv->state);
}

void hns_nic_net_reinit(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	netif_trans_update(priv->netdev);
	while (test_and_set_bit(NIC_STATE_REINITING, &priv->state))
		usleep_range(1000, 2000);

	hns_nic_net_down(netdev);
	hns_nic_net_reset(netdev);
	(void)hns_nic_net_up(netdev);
	clear_bit(NIC_STATE_REINITING, &priv->state);
}

static int hns_nic_net_open(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	int ret;

	if (test_bit(NIC_STATE_TESTING, &priv->state))
		return -EBUSY;

	priv->link = 0;
	netif_carrier_off(ndev);

	ret = netif_set_real_num_tx_queues(ndev, h->q_num);
	if (ret < 0) {
		netdev_err(ndev, "netif_set_real_num_tx_queues fail, ret=%d!\n",
			   ret);
		return ret;
	}

	ret = netif_set_real_num_rx_queues(ndev, h->q_num);
	if (ret < 0) {
		netdev_err(ndev,
			   "netif_set_real_num_rx_queues fail, ret=%d!\n", ret);
		return ret;
	}

	ret = hns_nic_net_up(ndev);
	if (ret) {
		netdev_err(ndev,
			   "hns net up fail, ret=%d!\n", ret);
		return ret;
	}

	return 0;
}

static int hns_nic_net_stop(struct net_device *ndev)
{
	hns_nic_net_down(ndev);

	return 0;
}

static void hns_tx_timeout_reset(struct hns_nic_priv *priv);
static void hns_nic_net_timeout(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);

	hns_tx_timeout_reset(priv);
}

static int hns_nic_do_ioctl(struct net_device *netdev, struct ifreq *ifr,
			    int cmd)
{
	struct phy_device *phy_dev = netdev->phydev;

	if (!netif_running(netdev))
		return -EINVAL;

	if (!phy_dev)
		return -ENOTSUPP;

	return phy_mii_ioctl(phy_dev, ifr, cmd);
}

/* use only for netconsole to poll with the device without interrupt */
#ifdef CONFIG_NET_POLL_CONTROLLER
void hns_nic_poll_controller(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	unsigned long flags;
	int i;

	local_irq_save(flags);
	for (i = 0; i < priv->ae_handle->q_num * 2; i++)
		napi_schedule(&priv->ring_data[i].napi);
	local_irq_restore(flags);
}
#endif

static netdev_tx_t hns_nic_net_xmit(struct sk_buff *skb,
				    struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	int ret;

	assert(skb->queue_mapping < ndev->ae_handle->q_num);
	ret = hns_nic_net_xmit_hw(ndev, skb,
				  &tx_ring_data(priv, skb->queue_mapping));
	if (ret == NETDEV_TX_OK) {
		netif_trans_update(ndev);
		ndev->stats.tx_bytes += skb->len;
		ndev->stats.tx_packets++;
	}
	return (netdev_tx_t)ret;
}

static int hns_nic_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	int ret;

	/* MTU < 68 is an error and causes problems on some kernels */
	if (new_mtu < 68)
		return -EINVAL;

	if (!h->dev->ops->set_mtu)
		return -ENOTSUPP;

	if (netif_running(ndev)) {
		(void)hns_nic_net_stop(ndev);
		msleep(100);

		ret = h->dev->ops->set_mtu(h, new_mtu);
		if (ret)
			netdev_err(ndev, "set mtu fail, return value %d\n",
				   ret);

		if (hns_nic_net_open(ndev))
			netdev_err(ndev, "hns net open fail\n");
	} else {
		ret = h->dev->ops->set_mtu(h, new_mtu);
	}

	if (!ret)
		ndev->mtu = new_mtu;

	return ret;
}

static int hns_nic_set_features(struct net_device *netdev,
				netdev_features_t features)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	switch (priv->enet_ver) {
	case AE_VERSION_1:
		if (features & (NETIF_F_TSO | NETIF_F_TSO6))
			netdev_info(netdev, "enet v1 do not support tso!\n");
		break;
	default:
		if (features & (NETIF_F_TSO | NETIF_F_TSO6)) {
			priv->ops.fill_desc = fill_tso_desc;
			priv->ops.maybe_stop_tx = hns_nic_maybe_stop_tso;
			/* The chip only support 7*4096 */
			netif_set_gso_max_size(netdev, 7 * 4096);
		} else {
			priv->ops.fill_desc = fill_v2_desc;
			priv->ops.maybe_stop_tx = hns_nic_maybe_stop_tx;
		}
		break;
	}
	netdev->features = features;
	return 0;
}

static netdev_features_t hns_nic_fix_features(
		struct net_device *netdev, netdev_features_t features)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	switch (priv->enet_ver) {
	case AE_VERSION_1:
		features &= ~(NETIF_F_TSO | NETIF_F_TSO6 |
				NETIF_F_HW_VLAN_CTAG_FILTER);
		break;
	default:
		break;
	}
	return features;
}

/**
 * nic_set_multicast_list - set mutl mac address
 * @netdev: net device
 * @p: mac address
 *
 * return void
 */
void hns_set_multicast_list(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	struct netdev_hw_addr *ha = NULL;

	if (!h)	{
		netdev_err(ndev, "hnae handle is null\n");
		return;
	}

	if (h->dev->ops->set_mc_addr) {
		netdev_for_each_mc_addr(ha, ndev)
			if (h->dev->ops->set_mc_addr(h, ha->addr))
				netdev_err(ndev, "set multicast fail\n");
	}
}

void hns_nic_set_rx_mode(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;

	if (h->dev->ops->set_promisc_mode) {
		if (ndev->flags & IFF_PROMISC)
			h->dev->ops->set_promisc_mode(h, 1);
		else
			h->dev->ops->set_promisc_mode(h, 0);
	}

	hns_set_multicast_list(ndev);
}

struct rtnl_link_stats64 *hns_nic_get_stats64(struct net_device *ndev,
					      struct rtnl_link_stats64 *stats)
{
	int idx = 0;
	u64 tx_bytes = 0;
	u64 rx_bytes = 0;
	u64 tx_pkts = 0;
	u64 rx_pkts = 0;
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;

	for (idx = 0; idx < h->q_num; idx++) {
		tx_bytes += h->qs[idx]->tx_ring.stats.tx_bytes;
		tx_pkts += h->qs[idx]->tx_ring.stats.tx_pkts;
		rx_bytes += h->qs[idx]->rx_ring.stats.rx_bytes;
		rx_pkts += h->qs[idx]->rx_ring.stats.rx_pkts;
	}

	stats->tx_bytes = tx_bytes;
	stats->tx_packets = tx_pkts;
	stats->rx_bytes = rx_bytes;
	stats->rx_packets = rx_pkts;

	stats->rx_errors = ndev->stats.rx_errors;
	stats->multicast = ndev->stats.multicast;
	stats->rx_length_errors = ndev->stats.rx_length_errors;
	stats->rx_crc_errors = ndev->stats.rx_crc_errors;
	stats->rx_missed_errors = ndev->stats.rx_missed_errors;

	stats->tx_errors = ndev->stats.tx_errors;
	stats->rx_dropped = ndev->stats.rx_dropped;
	stats->tx_dropped = ndev->stats.tx_dropped;
	stats->collisions = ndev->stats.collisions;
	stats->rx_over_errors = ndev->stats.rx_over_errors;
	stats->rx_frame_errors = ndev->stats.rx_frame_errors;
	stats->rx_fifo_errors = ndev->stats.rx_fifo_errors;
	stats->tx_aborted_errors = ndev->stats.tx_aborted_errors;
	stats->tx_carrier_errors = ndev->stats.tx_carrier_errors;
	stats->tx_fifo_errors = ndev->stats.tx_fifo_errors;
	stats->tx_heartbeat_errors = ndev->stats.tx_heartbeat_errors;
	stats->tx_window_errors = ndev->stats.tx_window_errors;
	stats->rx_compressed = ndev->stats.rx_compressed;
	stats->tx_compressed = ndev->stats.tx_compressed;

	return stats;
}

static const struct net_device_ops hns_nic_netdev_ops = {
	.ndo_open = hns_nic_net_open,
	.ndo_stop = hns_nic_net_stop,
	.ndo_start_xmit = hns_nic_net_xmit,
	.ndo_tx_timeout = hns_nic_net_timeout,
	.ndo_set_mac_address = hns_nic_net_set_mac_address,
	.ndo_change_mtu = hns_nic_change_mtu,
	.ndo_do_ioctl = hns_nic_do_ioctl,
	.ndo_set_features = hns_nic_set_features,
	.ndo_fix_features = hns_nic_fix_features,
	.ndo_get_stats64 = hns_nic_get_stats64,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = hns_nic_poll_controller,
#endif
	.ndo_set_rx_mode = hns_nic_set_rx_mode,
};

static void hns_nic_update_link_status(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	struct hnae_handle *h = priv->ae_handle;

	if (h->phy_dev) {
		if (h->phy_if != PHY_INTERFACE_MODE_XGMII)
			return;

		(void)genphy_read_status(h->phy_dev);
	}
	hns_nic_adjust_link(netdev);
}

/* for dumping key regs*/
static void hns_nic_dump(struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;
	struct hnae_ae_ops *ops = h->dev->ops;
	u32 *data, reg_num, i;

	if (ops->get_regs_len && ops->get_regs) {
		reg_num = ops->get_regs_len(priv->ae_handle);
		reg_num = (reg_num + 3ul) & ~3ul;
		data = kcalloc(reg_num, sizeof(u32), GFP_KERNEL);
		if (data) {
			ops->get_regs(priv->ae_handle, data);
			for (i = 0; i < reg_num; i += 4)
				pr_info("0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
					i, data[i], data[i + 1],
					data[i + 2], data[i + 3]);
			kfree(data);
		}
	}

	for (i = 0; i < h->q_num; i++) {
		pr_info("tx_queue%d_next_to_clean:%d\n",
			i, h->qs[i]->tx_ring.next_to_clean);
		pr_info("tx_queue%d_next_to_use:%d\n",
			i, h->qs[i]->tx_ring.next_to_use);
		pr_info("rx_queue%d_next_to_clean:%d\n",
			i, h->qs[i]->rx_ring.next_to_clean);
		pr_info("rx_queue%d_next_to_use:%d\n",
			i, h->qs[i]->rx_ring.next_to_use);
	}
}

/* for resetting subtask */
static void hns_nic_reset_subtask(struct hns_nic_priv *priv)
{
	enum hnae_port_type type = priv->ae_handle->port_type;

	if (!test_bit(NIC_STATE2_RESET_REQUESTED, &priv->state))
		return;
	clear_bit(NIC_STATE2_RESET_REQUESTED, &priv->state);

	/* If we're already down, removing or resetting, just bail */
	if (test_bit(NIC_STATE_DOWN, &priv->state) ||
	    test_bit(NIC_STATE_REMOVING, &priv->state) ||
	    test_bit(NIC_STATE_RESETTING, &priv->state))
		return;

	hns_nic_dump(priv);
	netdev_info(priv->netdev, "try to reset %s port!\n",
		    (type == HNAE_PORT_DEBUG ? "debug" : "service"));

	rtnl_lock();
	/* put off any impending NetWatchDogTimeout */
	netif_trans_update(priv->netdev);

	if (type == HNAE_PORT_DEBUG) {
		hns_nic_net_reinit(priv->netdev);
	} else {
		netif_carrier_off(priv->netdev);
		netif_tx_disable(priv->netdev);
	}
	rtnl_unlock();
}

/* for doing service complete*/
static void hns_nic_service_event_complete(struct hns_nic_priv *priv)
{
	WARN_ON(!test_bit(NIC_STATE_SERVICE_SCHED, &priv->state));

	smp_mb__before_atomic();
	clear_bit(NIC_STATE_SERVICE_SCHED, &priv->state);
}

static void hns_nic_service_task(struct work_struct *work)
{
	struct hns_nic_priv *priv
		= container_of(work, struct hns_nic_priv, service_task);
	struct hnae_handle *h = priv->ae_handle;

	hns_nic_update_link_status(priv->netdev);
	h->dev->ops->update_led_status(h);
	hns_nic_update_stats(priv->netdev);

	hns_nic_reset_subtask(priv);
	hns_nic_service_event_complete(priv);
}

static void hns_nic_task_schedule(struct hns_nic_priv *priv)
{
	if (!test_bit(NIC_STATE_DOWN, &priv->state) &&
	    !test_bit(NIC_STATE_REMOVING, &priv->state) &&
	    !test_and_set_bit(NIC_STATE_SERVICE_SCHED, &priv->state))
		(void)schedule_work(&priv->service_task);
}

static void hns_nic_service_timer(unsigned long data)
{
	struct hns_nic_priv *priv = (struct hns_nic_priv *)data;

	(void)mod_timer(&priv->service_timer, jiffies + SERVICE_TIMER_HZ);

	hns_nic_task_schedule(priv);
}

/**
 * hns_tx_timeout_reset - initiate reset due to Tx timeout
 * @priv: driver private struct
 **/
static void hns_tx_timeout_reset(struct hns_nic_priv *priv)
{
	/* Do the reset outside of interrupt context */
	if (!test_bit(NIC_STATE_DOWN, &priv->state)) {
		set_bit(NIC_STATE2_RESET_REQUESTED, &priv->state);
		netdev_warn(priv->netdev,
			    "initiating reset due to tx timeout(%llu,0x%lx)\n",
			    priv->tx_timeout_count, priv->state);
		priv->tx_timeout_count++;
		hns_nic_task_schedule(priv);
	}
}

static int hns_nic_init_ring_data(struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;
	struct hns_nic_ring_data *rd;
	bool is_ver1 = AE_IS_VER1(priv->enet_ver);
	int i;

	if (h->q_num > NIC_MAX_Q_PER_VF) {
		netdev_err(priv->netdev, "too much queue (%d)\n", h->q_num);
		return -EINVAL;
	}

	priv->ring_data = kzalloc(h->q_num * sizeof(*priv->ring_data) * 2,
				  GFP_KERNEL);
	if (!priv->ring_data)
		return -ENOMEM;

	for (i = 0; i < h->q_num; i++) {
		rd = &priv->ring_data[i];
		rd->queue_index = i;
		rd->ring = &h->qs[i]->tx_ring;
		rd->poll_one = hns_nic_tx_poll_one;
		rd->fini_process = is_ver1 ? hns_nic_tx_fini_pro : NULL;

		netif_napi_add(priv->netdev, &rd->napi,
			       hns_nic_common_poll, NIC_TX_CLEAN_MAX_NUM);
		rd->ring->irq_init_flag = RCB_IRQ_NOT_INITED;
	}
	for (i = h->q_num; i < h->q_num * 2; i++) {
		rd = &priv->ring_data[i];
		rd->queue_index = i - h->q_num;
		rd->ring = &h->qs[i - h->q_num]->rx_ring;
		rd->poll_one = hns_nic_rx_poll_one;
		rd->ex_process = hns_nic_rx_up_pro;
		rd->fini_process = is_ver1 ? hns_nic_rx_fini_pro : NULL;

		netif_napi_add(priv->netdev, &rd->napi,
			       hns_nic_common_poll, NIC_RX_CLEAN_MAX_NUM);
		rd->ring->irq_init_flag = RCB_IRQ_NOT_INITED;
	}

	return 0;
}

static void hns_nic_uninit_ring_data(struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;
	int i;

	for (i = 0; i < h->q_num * 2; i++) {
		netif_napi_del(&priv->ring_data[i].napi);
		if (priv->ring_data[i].ring->irq_init_flag == RCB_IRQ_INITED) {
			(void)irq_set_affinity_hint(
				priv->ring_data[i].ring->irq,
				NULL);
			free_irq(priv->ring_data[i].ring->irq,
				 &priv->ring_data[i]);
		}

		priv->ring_data[i].ring->irq_init_flag = RCB_IRQ_NOT_INITED;
	}
	kfree(priv->ring_data);
}

static void hns_nic_set_priv_ops(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;

	if (AE_IS_VER1(priv->enet_ver)) {
		priv->ops.fill_desc = fill_desc;
		priv->ops.get_rxd_bnum = get_rx_desc_bnum;
		priv->ops.maybe_stop_tx = hns_nic_maybe_stop_tx;
	} else {
		priv->ops.get_rxd_bnum = get_v2rx_desc_bnum;
		if ((netdev->features & NETIF_F_TSO) ||
		    (netdev->features & NETIF_F_TSO6)) {
			priv->ops.fill_desc = fill_tso_desc;
			priv->ops.maybe_stop_tx = hns_nic_maybe_stop_tso;
			/* This chip only support 7*4096 */
			netif_set_gso_max_size(netdev, 7 * 4096);
		} else {
			priv->ops.fill_desc = fill_v2_desc;
			priv->ops.maybe_stop_tx = hns_nic_maybe_stop_tx;
		}
		/* enable tso when init
		 * control tso on/off through TSE bit in bd
		 */
		h->dev->ops->set_tso_stats(h, 1);
	}
}

static int hns_nic_try_get_ae(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h;
	int ret;

	h = hnae_get_handle(&priv->netdev->dev,
			    priv->fwnode, priv->port_id, NULL);
	if (IS_ERR_OR_NULL(h)) {
		ret = -ENODEV;
		dev_dbg(priv->dev, "has not handle, register notifier!\n");
		goto out;
	}
	priv->ae_handle = h;

	ret = hns_nic_init_phy(ndev, h);
	if (ret) {
		dev_err(priv->dev, "probe phy device fail!\n");
		goto out_init_phy;
	}

	ret = hns_nic_init_ring_data(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_init_ring_data;
	}

	hns_nic_set_priv_ops(ndev);

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(priv->dev, "probe register netdev fail!\n");
		goto out_reg_ndev_fail;
	}
	return 0;

out_reg_ndev_fail:
	hns_nic_uninit_ring_data(priv);
	priv->ring_data = NULL;
out_init_phy:
out_init_ring_data:
	hnae_put_handle(priv->ae_handle);
	priv->ae_handle = NULL;
out:
	return ret;
}

static int hns_nic_notifier_action(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	struct hns_nic_priv *priv =
		container_of(nb, struct hns_nic_priv, notifier_block);

	assert(action == HNAE_AE_REGISTER);

	if (!hns_nic_try_get_ae(priv->netdev)) {
		hnae_unregister_notifier(&priv->notifier_block);
		priv->notifier_block.notifier_call = NULL;
	}
	return 0;
}

static int hns_nic_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct net_device *ndev;
	struct hns_nic_priv *priv;
	u32 port_id;
	int ret;

	ndev = alloc_etherdev_mq(sizeof(struct hns_nic_priv), NIC_MAX_Q_PER_VF);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	priv = netdev_priv(ndev);
	priv->dev = dev;
	priv->netdev = ndev;

	if (dev_of_node(dev)) {
		struct device_node *ae_node;

		if (of_device_is_compatible(dev->of_node,
					    "hisilicon,hns-nic-v1"))
			priv->enet_ver = AE_VERSION_1;
		else
			priv->enet_ver = AE_VERSION_2;

		ae_node = of_parse_phandle(dev->of_node, "ae-handle", 0);
		if (IS_ERR_OR_NULL(ae_node)) {
			ret = PTR_ERR(ae_node);
			dev_err(dev, "not find ae-handle\n");
			goto out_read_prop_fail;
		}
		priv->fwnode = &ae_node->fwnode;
	} else if (is_acpi_node(dev->fwnode)) {
		struct acpi_reference_args args;

		if (acpi_dev_found(hns_enet_acpi_match[0].id))
			priv->enet_ver = AE_VERSION_1;
		else if (acpi_dev_found(hns_enet_acpi_match[1].id))
			priv->enet_ver = AE_VERSION_2;
		else
			return -ENXIO;

		/* try to find port-idx-in-ae first */
		ret = acpi_node_get_property_reference(dev->fwnode,
						       "ae-handle", 0, &args);
		if (ret) {
			dev_err(dev, "not find ae-handle\n");
			goto out_read_prop_fail;
		}
		priv->fwnode = acpi_fwnode_handle(args.adev);
	} else {
		dev_err(dev, "cannot read cfg data from OF or acpi\n");
		return -ENXIO;
	}

	ret = device_property_read_u32(dev, "port-idx-in-ae", &port_id);
	if (ret) {
		/* only for old code compatible */
		ret = device_property_read_u32(dev, "port-id", &port_id);
		if (ret)
			goto out_read_prop_fail;
		/* for old dts, we need to caculate the port offset */
		port_id = port_id < HNS_SRV_OFFSET ? port_id + HNS_DEBUG_OFFSET
			: port_id - HNS_SRV_OFFSET;
	}
	priv->port_id = port_id;

	hns_init_mac_addr(ndev);

	ndev->watchdog_timeo = HNS_NIC_TX_TIMEOUT;
	ndev->priv_flags |= IFF_UNICAST_FLT;
	ndev->netdev_ops = &hns_nic_netdev_ops;
	hns_ethtool_set_ops(ndev);

	ndev->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
		NETIF_F_GRO;
	ndev->vlan_features |=
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM;
	ndev->vlan_features |= NETIF_F_SG | NETIF_F_GSO | NETIF_F_GRO;

	switch (priv->enet_ver) {
	case AE_VERSION_2:
		ndev->features |= NETIF_F_TSO | NETIF_F_TSO6;
		ndev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
			NETIF_F_GRO | NETIF_F_TSO | NETIF_F_TSO6;
		break;
	default:
		break;
	}

	SET_NETDEV_DEV(ndev, dev);

	if (!dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64)))
		dev_dbg(dev, "set mask to 64bit\n");
	else
		dev_err(dev, "set mask to 64bit fail!\n");

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(ndev);

	setup_timer(&priv->service_timer, hns_nic_service_timer,
		    (unsigned long)priv);
	INIT_WORK(&priv->service_task, hns_nic_service_task);

	set_bit(NIC_STATE_SERVICE_INITED, &priv->state);
	clear_bit(NIC_STATE_SERVICE_SCHED, &priv->state);
	set_bit(NIC_STATE_DOWN, &priv->state);

	if (hns_nic_try_get_ae(priv->netdev)) {
		priv->notifier_block.notifier_call = hns_nic_notifier_action;
		ret = hnae_register_notifier(&priv->notifier_block);
		if (ret) {
			dev_err(dev, "register notifier fail!\n");
			goto out_notify_fail;
		}
		dev_dbg(dev, "has not handle, register notifier!\n");
	}

	return 0;

out_notify_fail:
	(void)cancel_work_sync(&priv->service_task);
out_read_prop_fail:
	free_netdev(ndev);
	return ret;
}

static int hns_nic_dev_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct hns_nic_priv *priv = netdev_priv(ndev);

	if (ndev->reg_state != NETREG_UNINITIALIZED)
		unregister_netdev(ndev);

	if (priv->ring_data)
		hns_nic_uninit_ring_data(priv);
	priv->ring_data = NULL;

	if (ndev->phydev)
		phy_disconnect(ndev->phydev);

	if (!IS_ERR_OR_NULL(priv->ae_handle))
		hnae_put_handle(priv->ae_handle);
	priv->ae_handle = NULL;
	if (priv->notifier_block.notifier_call)
		hnae_unregister_notifier(&priv->notifier_block);
	priv->notifier_block.notifier_call = NULL;

	set_bit(NIC_STATE_REMOVING, &priv->state);
	(void)cancel_work_sync(&priv->service_task);

	free_netdev(ndev);
	return 0;
}

static const struct of_device_id hns_enet_of_match[] = {
	{.compatible = "hisilicon,hns-nic-v1",},
	{.compatible = "hisilicon,hns-nic-v2",},
	{},
};

MODULE_DEVICE_TABLE(of, hns_enet_of_match);

static struct platform_driver hns_nic_dev_driver = {
	.driver = {
		.name = "hns-nic",
		.of_match_table = hns_enet_of_match,
		.acpi_match_table = ACPI_PTR(hns_enet_acpi_match),
	},
	.probe = hns_nic_dev_probe,
	.remove = hns_nic_dev_remove,
};

module_platform_driver(hns_nic_dev_driver);

MODULE_DESCRIPTION("HISILICON HNS Ethernet driver");
MODULE_AUTHOR("Hisilicon, Inc.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hns-nic");
