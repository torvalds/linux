/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <net/checksum.h>

#include "qlcnic.h"

#define TX_ETHER_PKT	0x01
#define TX_TCP_PKT	0x02
#define TX_UDP_PKT	0x03
#define TX_IP_PKT	0x04
#define TX_TCP_LSO	0x05
#define TX_TCP_LSO6	0x06
#define TX_TCPV6_PKT	0x0b
#define TX_UDPV6_PKT	0x0c
#define FLAGS_VLAN_TAGGED	0x10
#define FLAGS_VLAN_OOB		0x40

#define qlcnic_set_tx_vlan_tci(cmd_desc, v)	\
	(cmd_desc)->vlan_TCI = cpu_to_le16(v);
#define qlcnic_set_cmd_desc_port(cmd_desc, var)	\
	((cmd_desc)->port_ctxid |= ((var) & 0x0F))
#define qlcnic_set_cmd_desc_ctxid(cmd_desc, var)	\
	((cmd_desc)->port_ctxid |= ((var) << 4 & 0xF0))

#define qlcnic_set_tx_port(_desc, _port) \
	((_desc)->port_ctxid = ((_port) & 0xf) | (((_port) << 4) & 0xf0))

#define qlcnic_set_tx_flags_opcode(_desc, _flags, _opcode) \
	((_desc)->flags_opcode |= \
	cpu_to_le16(((_flags) & 0x7f) | (((_opcode) & 0x3f) << 7)))

#define qlcnic_set_tx_frags_len(_desc, _frags, _len) \
	((_desc)->nfrags__length = \
	cpu_to_le32(((_frags) & 0xff) | (((_len) & 0xffffff) << 8)))

/* owner bits of status_desc */
#define STATUS_OWNER_HOST	(0x1ULL << 56)
#define STATUS_OWNER_PHANTOM	(0x2ULL << 56)

/* Status descriptor:
   0-3 port, 4-7 status, 8-11 type, 12-27 total_length
   28-43 reference_handle, 44-47 protocol, 48-52 pkt_offset
   53-55 desc_cnt, 56-57 owner, 58-63 opcode
 */
#define qlcnic_get_sts_port(sts_data)	\
	((sts_data) & 0x0F)
#define qlcnic_get_sts_status(sts_data)	\
	(((sts_data) >> 4) & 0x0F)
#define qlcnic_get_sts_type(sts_data)	\
	(((sts_data) >> 8) & 0x0F)
#define qlcnic_get_sts_totallength(sts_data)	\
	(((sts_data) >> 12) & 0xFFFF)
#define qlcnic_get_sts_refhandle(sts_data)	\
	(((sts_data) >> 28) & 0xFFFF)
#define qlcnic_get_sts_prot(sts_data)	\
	(((sts_data) >> 44) & 0x0F)
#define qlcnic_get_sts_pkt_offset(sts_data)	\
	(((sts_data) >> 48) & 0x1F)
#define qlcnic_get_sts_desc_cnt(sts_data)	\
	(((sts_data) >> 53) & 0x7)
#define qlcnic_get_sts_opcode(sts_data)	\
	(((sts_data) >> 58) & 0x03F)

#define qlcnic_get_lro_sts_refhandle(sts_data) 	\
	((sts_data) & 0x07FFF)
#define qlcnic_get_lro_sts_length(sts_data)	\
	(((sts_data) >> 16) & 0x0FFFF)
#define qlcnic_get_lro_sts_l2_hdr_offset(sts_data)	\
	(((sts_data) >> 32) & 0x0FF)
#define qlcnic_get_lro_sts_l4_hdr_offset(sts_data)	\
	(((sts_data) >> 40) & 0x0FF)
#define qlcnic_get_lro_sts_timestamp(sts_data)	\
	(((sts_data) >> 48) & 0x1)
#define qlcnic_get_lro_sts_type(sts_data)	\
	(((sts_data) >> 49) & 0x7)
#define qlcnic_get_lro_sts_push_flag(sts_data)		\
	(((sts_data) >> 52) & 0x1)
#define qlcnic_get_lro_sts_seq_number(sts_data)		\
	((sts_data) & 0x0FFFFFFFF)
#define qlcnic_get_lro_sts_mss(sts_data1)		\
	((sts_data1 >> 32) & 0x0FFFF)

#define qlcnic_83xx_get_lro_sts_mss(sts) ((sts) & 0xffff)

/* opcode field in status_desc */
#define QLCNIC_SYN_OFFLOAD	0x03
#define QLCNIC_RXPKT_DESC  	0x04
#define QLCNIC_OLD_RXPKT_DESC	0x3f
#define QLCNIC_RESPONSE_DESC	0x05
#define QLCNIC_LRO_DESC  	0x12

#define QLCNIC_TX_POLL_BUDGET		128
#define QLCNIC_TCP_HDR_SIZE		20
#define QLCNIC_TCP_TS_OPTION_SIZE	12
#define QLCNIC_FETCH_RING_ID(handle)	((handle) >> 63)
#define QLCNIC_DESC_OWNER_FW		cpu_to_le64(STATUS_OWNER_PHANTOM)

#define QLCNIC_TCP_TS_HDR_SIZE (QLCNIC_TCP_HDR_SIZE + QLCNIC_TCP_TS_OPTION_SIZE)

/* for status field in status_desc */
#define STATUS_CKSUM_LOOP	0
#define STATUS_CKSUM_OK		2

#define qlcnic_83xx_pktln(sts)		((sts >> 32) & 0x3FFF)
#define qlcnic_83xx_hndl(sts)		((sts >> 48) & 0x7FFF)
#define qlcnic_83xx_csum_status(sts)	((sts >> 39) & 7)
#define qlcnic_83xx_opcode(sts)	((sts >> 42) & 0xF)
#define qlcnic_83xx_vlan_tag(sts)	(((sts) >> 48) & 0xFFFF)
#define qlcnic_83xx_lro_pktln(sts)	(((sts) >> 32) & 0x3FFF)
#define qlcnic_83xx_l2_hdr_off(sts)	(((sts) >> 16) & 0xFF)
#define qlcnic_83xx_l4_hdr_off(sts)	(((sts) >> 24) & 0xFF)
#define qlcnic_83xx_pkt_cnt(sts)	(((sts) >> 16) & 0x7)
#define qlcnic_83xx_is_tstamp(sts)	(((sts) >> 40) & 1)
#define qlcnic_83xx_is_psh_bit(sts)	(((sts) >> 41) & 1)
#define qlcnic_83xx_is_ip_align(sts)	(((sts) >> 46) & 1)
#define qlcnic_83xx_has_vlan_tag(sts)	(((sts) >> 47) & 1)

struct sk_buff *qlcnic_process_rxbuf(struct qlcnic_adapter *,
				     struct qlcnic_host_rds_ring *, u16, u16);

inline void qlcnic_enable_tx_intr(struct qlcnic_adapter *adapter,
				  struct qlcnic_host_tx_ring *tx_ring)
{
	if (qlcnic_check_multi_tx(adapter) &&
	    !adapter->ahw->diag_test)
		writel(0x0, tx_ring->crb_intr_mask);
}


static inline void qlcnic_disable_tx_int(struct qlcnic_adapter *adapter,
					 struct qlcnic_host_tx_ring *tx_ring)
{
	if (qlcnic_check_multi_tx(adapter) &&
	    !adapter->ahw->diag_test)
		writel(1, tx_ring->crb_intr_mask);
}

inline void qlcnic_83xx_enable_tx_intr(struct qlcnic_adapter *adapter,
				       struct qlcnic_host_tx_ring *tx_ring)
{
	writel(0, tx_ring->crb_intr_mask);
}

inline void qlcnic_83xx_disable_tx_intr(struct qlcnic_adapter *adapter,
					struct qlcnic_host_tx_ring *tx_ring)
{
	writel(1, tx_ring->crb_intr_mask);
}

static inline u8 qlcnic_mac_hash(u64 mac)
{
	return (u8)((mac & 0xff) ^ ((mac >> 40) & 0xff));
}

static inline u32 qlcnic_get_ref_handle(struct qlcnic_adapter *adapter,
					u16 handle, u8 ring_id)
{
	if (qlcnic_83xx_check(adapter))
		return handle | (ring_id << 15);
	else
		return handle;
}

static inline int qlcnic_82xx_is_lb_pkt(u64 sts_data)
{
	return (qlcnic_get_sts_status(sts_data) == STATUS_CKSUM_LOOP) ? 1 : 0;
}

static void qlcnic_delete_rx_list_mac(struct qlcnic_adapter *adapter,
				      struct qlcnic_filter *fil,
				      void *addr, u16 vlan_id)
{
	int ret;
	u8 op;

	op = vlan_id ? QLCNIC_MAC_VLAN_ADD : QLCNIC_MAC_ADD;
	ret = qlcnic_sre_macaddr_change(adapter, addr, vlan_id, op);
	if (ret)
		return;

	op = vlan_id ? QLCNIC_MAC_VLAN_DEL : QLCNIC_MAC_DEL;
	ret = qlcnic_sre_macaddr_change(adapter, addr, vlan_id, op);
	if (!ret) {
		hlist_del(&fil->fnode);
		adapter->rx_fhash.fnum--;
	}
}

static struct qlcnic_filter *qlcnic_find_mac_filter(struct hlist_head *head,
						    void *addr, u16 vlan_id)
{
	struct qlcnic_filter *tmp_fil = NULL;
	struct hlist_node *n;

	hlist_for_each_entry_safe(tmp_fil, n, head, fnode) {
		if (!memcmp(tmp_fil->faddr, addr, ETH_ALEN) &&
		    tmp_fil->vlan_id == vlan_id)
			return tmp_fil;
	}

	return NULL;
}

void qlcnic_add_lb_filter(struct qlcnic_adapter *adapter, struct sk_buff *skb,
			  int loopback_pkt, u16 vlan_id)
{
	struct ethhdr *phdr = (struct ethhdr *)(skb->data);
	struct qlcnic_filter *fil, *tmp_fil;
	struct hlist_head *head;
	unsigned long time;
	u64 src_addr = 0;
	u8 hindex, op;
	int ret;

	memcpy(&src_addr, phdr->h_source, ETH_ALEN);
	hindex = qlcnic_mac_hash(src_addr) &
		 (adapter->fhash.fbucket_size - 1);

	if (loopback_pkt) {
		if (adapter->rx_fhash.fnum >= adapter->rx_fhash.fmax)
			return;

		head = &(adapter->rx_fhash.fhead[hindex]);

		tmp_fil = qlcnic_find_mac_filter(head, &src_addr, vlan_id);
		if (tmp_fil) {
			time = tmp_fil->ftime;
			if (time_after(jiffies, QLCNIC_READD_AGE * HZ + time))
				tmp_fil->ftime = jiffies;
			return;
		}

		fil = kzalloc(sizeof(struct qlcnic_filter), GFP_ATOMIC);
		if (!fil)
			return;

		fil->ftime = jiffies;
		memcpy(fil->faddr, &src_addr, ETH_ALEN);
		fil->vlan_id = vlan_id;
		spin_lock(&adapter->rx_mac_learn_lock);
		hlist_add_head(&(fil->fnode), head);
		adapter->rx_fhash.fnum++;
		spin_unlock(&adapter->rx_mac_learn_lock);
	} else {
		head = &adapter->fhash.fhead[hindex];

		spin_lock(&adapter->mac_learn_lock);

		tmp_fil = qlcnic_find_mac_filter(head, &src_addr, vlan_id);
		if (tmp_fil) {
			op = vlan_id ? QLCNIC_MAC_VLAN_DEL : QLCNIC_MAC_DEL;
			ret = qlcnic_sre_macaddr_change(adapter,
							(u8 *)&src_addr,
							vlan_id, op);
			if (!ret) {
				hlist_del(&tmp_fil->fnode);
				adapter->fhash.fnum--;
			}

			spin_unlock(&adapter->mac_learn_lock);

			return;
		}

		spin_unlock(&adapter->mac_learn_lock);

		head = &adapter->rx_fhash.fhead[hindex];

		spin_lock(&adapter->rx_mac_learn_lock);

		tmp_fil = qlcnic_find_mac_filter(head, &src_addr, vlan_id);
		if (tmp_fil)
			qlcnic_delete_rx_list_mac(adapter, tmp_fil, &src_addr,
						  vlan_id);

		spin_unlock(&adapter->rx_mac_learn_lock);
	}
}

void qlcnic_82xx_change_filter(struct qlcnic_adapter *adapter, u64 *uaddr,
			       u16 vlan_id)
{
	struct cmd_desc_type0 *hwdesc;
	struct qlcnic_nic_req *req;
	struct qlcnic_mac_req *mac_req;
	struct qlcnic_vlan_req *vlan_req;
	struct qlcnic_host_tx_ring *tx_ring = adapter->tx_ring;
	u32 producer;
	u64 word;

	producer = tx_ring->producer;
	hwdesc = &tx_ring->desc_head[tx_ring->producer];

	req = (struct qlcnic_nic_req *)hwdesc;
	memset(req, 0, sizeof(struct qlcnic_nic_req));
	req->qhdr = cpu_to_le64(QLCNIC_REQUEST << 23);

	word = QLCNIC_MAC_EVENT | ((u64)(adapter->portnum) << 16);
	req->req_hdr = cpu_to_le64(word);

	mac_req = (struct qlcnic_mac_req *)&(req->words[0]);
	mac_req->op = vlan_id ? QLCNIC_MAC_VLAN_ADD : QLCNIC_MAC_ADD;
	memcpy(mac_req->mac_addr, uaddr, ETH_ALEN);

	vlan_req = (struct qlcnic_vlan_req *)&req->words[1];
	vlan_req->vlan_id = cpu_to_le16(vlan_id);

	tx_ring->producer = get_next_index(producer, tx_ring->num_desc);
	smp_mb();
}

static void qlcnic_send_filter(struct qlcnic_adapter *adapter,
			       struct cmd_desc_type0 *first_desc,
			       struct sk_buff *skb)
{
	struct qlcnic_filter *fil, *tmp_fil;
	struct hlist_node *n;
	struct hlist_head *head;
	struct net_device *netdev = adapter->netdev;
	struct ethhdr *phdr = (struct ethhdr *)(skb->data);
	u64 src_addr = 0;
	u16 vlan_id = 0;
	u8 hindex;

	if (ether_addr_equal(phdr->h_source, adapter->mac_addr))
		return;

	if (adapter->fhash.fnum >= adapter->fhash.fmax) {
		adapter->stats.mac_filter_limit_overrun++;
		netdev_info(netdev, "Can not add more than %d mac addresses\n",
			    adapter->fhash.fmax);
		return;
	}

	memcpy(&src_addr, phdr->h_source, ETH_ALEN);
	hindex = qlcnic_mac_hash(src_addr) & (adapter->fhash.fbucket_size - 1);
	head = &(adapter->fhash.fhead[hindex]);

	hlist_for_each_entry_safe(tmp_fil, n, head, fnode) {
		if (!memcmp(tmp_fil->faddr, &src_addr, ETH_ALEN) &&
		    tmp_fil->vlan_id == vlan_id) {
			if (jiffies > (QLCNIC_READD_AGE * HZ + tmp_fil->ftime))
				qlcnic_change_filter(adapter, &src_addr,
						     vlan_id);
			tmp_fil->ftime = jiffies;
			return;
		}
	}

	fil = kzalloc(sizeof(struct qlcnic_filter), GFP_ATOMIC);
	if (!fil)
		return;

	qlcnic_change_filter(adapter, &src_addr, vlan_id);
	fil->ftime = jiffies;
	fil->vlan_id = vlan_id;
	memcpy(fil->faddr, &src_addr, ETH_ALEN);
	spin_lock(&adapter->mac_learn_lock);
	hlist_add_head(&(fil->fnode), head);
	adapter->fhash.fnum++;
	spin_unlock(&adapter->mac_learn_lock);
}

static int qlcnic_tx_pkt(struct qlcnic_adapter *adapter,
			 struct cmd_desc_type0 *first_desc, struct sk_buff *skb,
			 struct qlcnic_host_tx_ring *tx_ring)
{
	u8 l4proto, opcode = 0, hdr_len = 0;
	u16 flags = 0, vlan_tci = 0;
	int copied, offset, copy_len, size;
	struct cmd_desc_type0 *hwdesc;
	struct vlan_ethhdr *vh;
	u16 protocol = ntohs(skb->protocol);
	u32 producer = tx_ring->producer;

	if (protocol == ETH_P_8021Q) {
		vh = (struct vlan_ethhdr *)skb->data;
		flags = FLAGS_VLAN_TAGGED;
		vlan_tci = ntohs(vh->h_vlan_TCI);
		protocol = ntohs(vh->h_vlan_encapsulated_proto);
	} else if (vlan_tx_tag_present(skb)) {
		flags = FLAGS_VLAN_OOB;
		vlan_tci = vlan_tx_tag_get(skb);
	}
	if (unlikely(adapter->tx_pvid)) {
		if (vlan_tci && !(adapter->flags & QLCNIC_TAGGING_ENABLED))
			return -EIO;
		if (vlan_tci && (adapter->flags & QLCNIC_TAGGING_ENABLED))
			goto set_flags;

		flags = FLAGS_VLAN_OOB;
		vlan_tci = adapter->tx_pvid;
	}
set_flags:
	qlcnic_set_tx_vlan_tci(first_desc, vlan_tci);
	qlcnic_set_tx_flags_opcode(first_desc, flags, opcode);

	if (*(skb->data) & BIT_0) {
		flags |= BIT_0;
		memcpy(&first_desc->eth_addr, skb->data, ETH_ALEN);
	}
	opcode = TX_ETHER_PKT;
	if (skb_is_gso(skb)) {
		hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		first_desc->mss = cpu_to_le16(skb_shinfo(skb)->gso_size);
		first_desc->total_hdr_length = hdr_len;
		opcode = (protocol == ETH_P_IPV6) ? TX_TCP_LSO6 : TX_TCP_LSO;

		/* For LSO, we need to copy the MAC/IP/TCP headers into
		* the descriptor ring */
		copied = 0;
		offset = 2;

		if (flags & FLAGS_VLAN_OOB) {
			first_desc->total_hdr_length += VLAN_HLEN;
			first_desc->tcp_hdr_offset = VLAN_HLEN;
			first_desc->ip_hdr_offset = VLAN_HLEN;

			/* Only in case of TSO on vlan device */
			flags |= FLAGS_VLAN_TAGGED;

			/* Create a TSO vlan header template for firmware */
			hwdesc = &tx_ring->desc_head[producer];
			tx_ring->cmd_buf_arr[producer].skb = NULL;

			copy_len = min((int)sizeof(struct cmd_desc_type0) -
				       offset, hdr_len + VLAN_HLEN);

			vh = (struct vlan_ethhdr *)((char *) hwdesc + 2);
			skb_copy_from_linear_data(skb, vh, 12);
			vh->h_vlan_proto = htons(ETH_P_8021Q);
			vh->h_vlan_TCI = htons(vlan_tci);

			skb_copy_from_linear_data_offset(skb, 12,
							 (char *)vh + 16,
							 copy_len - 16);
			copied = copy_len - VLAN_HLEN;
			offset = 0;
			producer = get_next_index(producer, tx_ring->num_desc);
		}

		while (copied < hdr_len) {
			size = (int)sizeof(struct cmd_desc_type0) - offset;
			copy_len = min(size, (hdr_len - copied));
			hwdesc = &tx_ring->desc_head[producer];
			tx_ring->cmd_buf_arr[producer].skb = NULL;
			skb_copy_from_linear_data_offset(skb, copied,
							 (char *)hwdesc +
							 offset, copy_len);
			copied += copy_len;
			offset = 0;
			producer = get_next_index(producer, tx_ring->num_desc);
		}

		tx_ring->producer = producer;
		smp_mb();
		adapter->stats.lso_frames++;

	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (protocol == ETH_P_IP) {
			l4proto = ip_hdr(skb)->protocol;

			if (l4proto == IPPROTO_TCP)
				opcode = TX_TCP_PKT;
			else if (l4proto == IPPROTO_UDP)
				opcode = TX_UDP_PKT;
		} else if (protocol == ETH_P_IPV6) {
			l4proto = ipv6_hdr(skb)->nexthdr;

			if (l4proto == IPPROTO_TCP)
				opcode = TX_TCPV6_PKT;
			else if (l4proto == IPPROTO_UDP)
				opcode = TX_UDPV6_PKT;
		}
	}
	first_desc->tcp_hdr_offset += skb_transport_offset(skb);
	first_desc->ip_hdr_offset += skb_network_offset(skb);
	qlcnic_set_tx_flags_opcode(first_desc, flags, opcode);

	return 0;
}

static int qlcnic_map_tx_skb(struct pci_dev *pdev, struct sk_buff *skb,
			     struct qlcnic_cmd_buffer *pbuf)
{
	struct qlcnic_skb_frag *nf;
	struct skb_frag_struct *frag;
	int i, nr_frags;
	dma_addr_t map;

	nr_frags = skb_shinfo(skb)->nr_frags;
	nf = &pbuf->frag_array[0];

	map = pci_map_single(pdev, skb->data, skb_headlen(skb),
			     PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(pdev, map))
		goto out_err;

	nf->dma = map;
	nf->length = skb_headlen(skb);

	for (i = 0; i < nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		nf = &pbuf->frag_array[i+1];
		map = skb_frag_dma_map(&pdev->dev, frag, 0, skb_frag_size(frag),
				       DMA_TO_DEVICE);
		if (dma_mapping_error(&pdev->dev, map))
			goto unwind;

		nf->dma = map;
		nf->length = skb_frag_size(frag);
	}

	return 0;

unwind:
	while (--i >= 0) {
		nf = &pbuf->frag_array[i+1];
		pci_unmap_page(pdev, nf->dma, nf->length, PCI_DMA_TODEVICE);
	}

	nf = &pbuf->frag_array[0];
	pci_unmap_single(pdev, nf->dma, skb_headlen(skb), PCI_DMA_TODEVICE);

out_err:
	return -ENOMEM;
}

static void qlcnic_unmap_buffers(struct pci_dev *pdev, struct sk_buff *skb,
				 struct qlcnic_cmd_buffer *pbuf)
{
	struct qlcnic_skb_frag *nf = &pbuf->frag_array[0];
	int i, nr_frags = skb_shinfo(skb)->nr_frags;

	for (i = 0; i < nr_frags; i++) {
		nf = &pbuf->frag_array[i+1];
		pci_unmap_page(pdev, nf->dma, nf->length, PCI_DMA_TODEVICE);
	}

	nf = &pbuf->frag_array[0];
	pci_unmap_single(pdev, nf->dma, skb_headlen(skb), PCI_DMA_TODEVICE);
	pbuf->skb = NULL;
}

static inline void qlcnic_clear_cmddesc(u64 *desc)
{
	desc[0] = 0ULL;
	desc[2] = 0ULL;
	desc[7] = 0ULL;
}

netdev_tx_t qlcnic_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_host_tx_ring *tx_ring;
	struct qlcnic_cmd_buffer *pbuf;
	struct qlcnic_skb_frag *buffrag;
	struct cmd_desc_type0 *hwdesc, *first_desc;
	struct pci_dev *pdev;
	struct ethhdr *phdr;
	int i, k, frag_count, delta = 0;
	u32 producer, num_txd;

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state)) {
		netif_tx_stop_all_queues(netdev);
		return NETDEV_TX_BUSY;
	}

	if (adapter->flags & QLCNIC_MACSPOOF) {
		phdr = (struct ethhdr *)skb->data;
		if (!ether_addr_equal(phdr->h_source, adapter->mac_addr))
			goto drop_packet;
	}

	tx_ring = &adapter->tx_ring[skb_get_queue_mapping(skb)];
	num_txd = tx_ring->num_desc;

	frag_count = skb_shinfo(skb)->nr_frags + 1;

	/* 14 frags supported for normal packet and
	 * 32 frags supported for TSO packet
	 */
	if (!skb_is_gso(skb) && frag_count > QLCNIC_MAX_FRAGS_PER_TX) {
		for (i = 0; i < (frag_count - QLCNIC_MAX_FRAGS_PER_TX); i++)
			delta += skb_frag_size(&skb_shinfo(skb)->frags[i]);

		if (!__pskb_pull_tail(skb, delta))
			goto drop_packet;

		frag_count = 1 + skb_shinfo(skb)->nr_frags;
	}

	if (unlikely(qlcnic_tx_avail(tx_ring) <= TX_STOP_THRESH)) {
		netif_tx_stop_queue(tx_ring->txq);
		if (qlcnic_tx_avail(tx_ring) > TX_STOP_THRESH) {
			netif_tx_start_queue(tx_ring->txq);
		} else {
			tx_ring->tx_stats.xmit_off++;
			return NETDEV_TX_BUSY;
		}
	}

	producer = tx_ring->producer;
	pbuf = &tx_ring->cmd_buf_arr[producer];
	pdev = adapter->pdev;
	first_desc = &tx_ring->desc_head[producer];
	hwdesc = &tx_ring->desc_head[producer];
	qlcnic_clear_cmddesc((u64 *)hwdesc);

	if (qlcnic_map_tx_skb(pdev, skb, pbuf)) {
		adapter->stats.tx_dma_map_error++;
		goto drop_packet;
	}

	pbuf->skb = skb;
	pbuf->frag_count = frag_count;

	qlcnic_set_tx_frags_len(first_desc, frag_count, skb->len);
	qlcnic_set_tx_port(first_desc, adapter->portnum);

	for (i = 0; i < frag_count; i++) {
		k = i % 4;

		if ((k == 0) && (i > 0)) {
			/* move to next desc.*/
			producer = get_next_index(producer, num_txd);
			hwdesc = &tx_ring->desc_head[producer];
			qlcnic_clear_cmddesc((u64 *)hwdesc);
			tx_ring->cmd_buf_arr[producer].skb = NULL;
		}

		buffrag = &pbuf->frag_array[i];
		hwdesc->buffer_length[k] = cpu_to_le16(buffrag->length);
		switch (k) {
		case 0:
			hwdesc->addr_buffer1 = cpu_to_le64(buffrag->dma);
			break;
		case 1:
			hwdesc->addr_buffer2 = cpu_to_le64(buffrag->dma);
			break;
		case 2:
			hwdesc->addr_buffer3 = cpu_to_le64(buffrag->dma);
			break;
		case 3:
			hwdesc->addr_buffer4 = cpu_to_le64(buffrag->dma);
			break;
		}
	}

	tx_ring->producer = get_next_index(producer, num_txd);
	smp_mb();

	if (unlikely(qlcnic_tx_pkt(adapter, first_desc, skb, tx_ring)))
		goto unwind_buff;

	if (adapter->drv_mac_learn)
		qlcnic_send_filter(adapter, first_desc, skb);

	tx_ring->tx_stats.tx_bytes += skb->len;
	tx_ring->tx_stats.xmit_called++;

	qlcnic_update_cmd_producer(tx_ring);

	return NETDEV_TX_OK;

unwind_buff:
	qlcnic_unmap_buffers(pdev, skb, pbuf);
drop_packet:
	adapter->stats.txdropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

void qlcnic_advert_link_change(struct qlcnic_adapter *adapter, int linkup)
{
	struct net_device *netdev = adapter->netdev;

	if (adapter->ahw->linkup && !linkup) {
		netdev_info(netdev, "NIC Link is down\n");
		adapter->ahw->linkup = 0;
		netif_carrier_off(netdev);
	} else if (!adapter->ahw->linkup && linkup) {
		/* Do not advertise Link up if the port is in loopback mode */
		if (qlcnic_83xx_check(adapter) && adapter->ahw->lb_mode)
			return;

		netdev_info(netdev, "NIC Link is up\n");
		adapter->ahw->linkup = 1;
		netif_carrier_on(netdev);
	}
}

static int qlcnic_alloc_rx_skb(struct qlcnic_adapter *adapter,
			       struct qlcnic_host_rds_ring *rds_ring,
			       struct qlcnic_rx_buffer *buffer)
{
	struct sk_buff *skb;
	dma_addr_t dma;
	struct pci_dev *pdev = adapter->pdev;

	skb = netdev_alloc_skb(adapter->netdev, rds_ring->skb_size);
	if (!skb) {
		adapter->stats.skb_alloc_failure++;
		return -ENOMEM;
	}

	skb_reserve(skb, NET_IP_ALIGN);
	dma = pci_map_single(pdev, skb->data,
			     rds_ring->dma_size, PCI_DMA_FROMDEVICE);

	if (pci_dma_mapping_error(pdev, dma)) {
		adapter->stats.rx_dma_map_error++;
		dev_kfree_skb_any(skb);
		return -ENOMEM;
	}

	buffer->skb = skb;
	buffer->dma = dma;

	return 0;
}

static void qlcnic_post_rx_buffers_nodb(struct qlcnic_adapter *adapter,
					struct qlcnic_host_rds_ring *rds_ring,
					u8 ring_id)
{
	struct rcv_desc *pdesc;
	struct qlcnic_rx_buffer *buffer;
	int  count = 0;
	uint32_t producer, handle;
	struct list_head *head;

	if (!spin_trylock(&rds_ring->lock))
		return;

	producer = rds_ring->producer;
	head = &rds_ring->free_list;
	while (!list_empty(head)) {
		buffer = list_entry(head->next, struct qlcnic_rx_buffer, list);

		if (!buffer->skb) {
			if (qlcnic_alloc_rx_skb(adapter, rds_ring, buffer))
				break;
		}
		count++;
		list_del(&buffer->list);

		/* make a rcv descriptor  */
		pdesc = &rds_ring->desc_head[producer];
		handle = qlcnic_get_ref_handle(adapter,
					       buffer->ref_handle, ring_id);
		pdesc->reference_handle = cpu_to_le16(handle);
		pdesc->buffer_length = cpu_to_le32(rds_ring->dma_size);
		pdesc->addr_buffer = cpu_to_le64(buffer->dma);
		producer = get_next_index(producer, rds_ring->num_desc);
	}
	if (count) {
		rds_ring->producer = producer;
		writel((producer - 1) & (rds_ring->num_desc - 1),
		       rds_ring->crb_rcv_producer);
	}
	spin_unlock(&rds_ring->lock);
}

static int qlcnic_process_cmd_ring(struct qlcnic_adapter *adapter,
				   struct qlcnic_host_tx_ring *tx_ring,
				   int budget)
{
	u32 sw_consumer, hw_consumer;
	int i, done, count = 0;
	struct qlcnic_cmd_buffer *buffer;
	struct pci_dev *pdev = adapter->pdev;
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_skb_frag *frag;

	if (!spin_trylock(&tx_ring->tx_clean_lock))
		return 1;

	sw_consumer = tx_ring->sw_consumer;
	hw_consumer = le32_to_cpu(*(tx_ring->hw_consumer));

	while (sw_consumer != hw_consumer) {
		buffer = &tx_ring->cmd_buf_arr[sw_consumer];
		if (buffer->skb) {
			frag = &buffer->frag_array[0];
			pci_unmap_single(pdev, frag->dma, frag->length,
					 PCI_DMA_TODEVICE);
			frag->dma = 0ULL;
			for (i = 1; i < buffer->frag_count; i++) {
				frag++;
				pci_unmap_page(pdev, frag->dma, frag->length,
					       PCI_DMA_TODEVICE);
				frag->dma = 0ULL;
			}
			tx_ring->tx_stats.xmit_finished++;
			dev_kfree_skb_any(buffer->skb);
			buffer->skb = NULL;
		}

		sw_consumer = get_next_index(sw_consumer, tx_ring->num_desc);
		if (++count >= budget)
			break;
	}

	tx_ring->sw_consumer = sw_consumer;

	if (count && netif_running(netdev)) {
		smp_mb();
		if (netif_tx_queue_stopped(tx_ring->txq) &&
		    netif_carrier_ok(netdev)) {
			if (qlcnic_tx_avail(tx_ring) > TX_STOP_THRESH) {
				netif_tx_wake_queue(tx_ring->txq);
				tx_ring->tx_stats.xmit_on++;
			}
		}
		adapter->tx_timeo_cnt = 0;
	}
	/*
	 * If everything is freed up to consumer then check if the ring is full
	 * If the ring is full then check if more needs to be freed and
	 * schedule the call back again.
	 *
	 * This happens when there are 2 CPUs. One could be freeing and the
	 * other filling it. If the ring is full when we get out of here and
	 * the card has already interrupted the host then the host can miss the
	 * interrupt.
	 *
	 * There is still a possible race condition and the host could miss an
	 * interrupt. The card has to take care of this.
	 */
	hw_consumer = le32_to_cpu(*(tx_ring->hw_consumer));
	done = (sw_consumer == hw_consumer);

	spin_unlock(&tx_ring->tx_clean_lock);

	return done;
}

static int qlcnic_poll(struct napi_struct *napi, int budget)
{
	int tx_complete, work_done;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_adapter *adapter;
	struct qlcnic_host_tx_ring *tx_ring;

	sds_ring = container_of(napi, struct qlcnic_host_sds_ring, napi);
	adapter = sds_ring->adapter;
	tx_ring = sds_ring->tx_ring;

	tx_complete = qlcnic_process_cmd_ring(adapter, tx_ring,
					      budget);
	work_done = qlcnic_process_rcv_ring(sds_ring, budget);
	if ((work_done < budget) && tx_complete) {
		napi_complete(&sds_ring->napi);
		if (test_bit(__QLCNIC_DEV_UP, &adapter->state)) {
			qlcnic_enable_int(sds_ring);
			qlcnic_enable_tx_intr(adapter, tx_ring);
		}
	}

	return work_done;
}

static int qlcnic_tx_poll(struct napi_struct *napi, int budget)
{
	struct qlcnic_host_tx_ring *tx_ring;
	struct qlcnic_adapter *adapter;
	int work_done;

	tx_ring = container_of(napi, struct qlcnic_host_tx_ring, napi);
	adapter = tx_ring->adapter;

	work_done = qlcnic_process_cmd_ring(adapter, tx_ring, budget);
	if (work_done) {
		napi_complete(&tx_ring->napi);
		if (test_bit(__QLCNIC_DEV_UP, &adapter->state))
			qlcnic_enable_tx_intr(adapter, tx_ring);
	}

	return work_done;
}

static int qlcnic_rx_poll(struct napi_struct *napi, int budget)
{
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_adapter *adapter;
	int work_done;

	sds_ring = container_of(napi, struct qlcnic_host_sds_ring, napi);
	adapter = sds_ring->adapter;

	work_done = qlcnic_process_rcv_ring(sds_ring, budget);

	if (work_done < budget) {
		napi_complete(&sds_ring->napi);
		if (test_bit(__QLCNIC_DEV_UP, &adapter->state))
			qlcnic_enable_int(sds_ring);
	}

	return work_done;
}

static void qlcnic_handle_linkevent(struct qlcnic_adapter *adapter,
				    struct qlcnic_fw_msg *msg)
{
	u32 cable_OUI;
	u16 cable_len, link_speed;
	u8  link_status, module, duplex, autoneg, lb_status = 0;
	struct net_device *netdev = adapter->netdev;

	adapter->ahw->has_link_events = 1;

	cable_OUI = msg->body[1] & 0xffffffff;
	cable_len = (msg->body[1] >> 32) & 0xffff;
	link_speed = (msg->body[1] >> 48) & 0xffff;

	link_status = msg->body[2] & 0xff;
	duplex = (msg->body[2] >> 16) & 0xff;
	autoneg = (msg->body[2] >> 24) & 0xff;
	lb_status = (msg->body[2] >> 32) & 0x3;

	module = (msg->body[2] >> 8) & 0xff;
	if (module == LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLE)
		dev_info(&netdev->dev,
			 "unsupported cable: OUI 0x%x, length %d\n",
			 cable_OUI, cable_len);
	else if (module == LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLELEN)
		dev_info(&netdev->dev, "unsupported cable length %d\n",
			 cable_len);

	if (!link_status && (lb_status == QLCNIC_ILB_MODE ||
	    lb_status == QLCNIC_ELB_MODE))
		adapter->ahw->loopback_state |= QLCNIC_LINKEVENT;

	qlcnic_advert_link_change(adapter, link_status);

	if (duplex == LINKEVENT_FULL_DUPLEX)
		adapter->ahw->link_duplex = DUPLEX_FULL;
	else
		adapter->ahw->link_duplex = DUPLEX_HALF;

	adapter->ahw->module_type = module;
	adapter->ahw->link_autoneg = autoneg;

	if (link_status) {
		adapter->ahw->link_speed = link_speed;
	} else {
		adapter->ahw->link_speed = SPEED_UNKNOWN;
		adapter->ahw->link_duplex = DUPLEX_UNKNOWN;
	}
}

static void qlcnic_handle_fw_message(int desc_cnt, int index,
				     struct qlcnic_host_sds_ring *sds_ring)
{
	struct qlcnic_fw_msg msg;
	struct status_desc *desc;
	struct qlcnic_adapter *adapter;
	struct device *dev;
	int i = 0, opcode, ret;

	while (desc_cnt > 0 && i < 8) {
		desc = &sds_ring->desc_head[index];
		msg.words[i++] = le64_to_cpu(desc->status_desc_data[0]);
		msg.words[i++] = le64_to_cpu(desc->status_desc_data[1]);

		index = get_next_index(index, sds_ring->num_desc);
		desc_cnt--;
	}

	adapter = sds_ring->adapter;
	dev = &adapter->pdev->dev;
	opcode = qlcnic_get_nic_msg_opcode(msg.body[0]);

	switch (opcode) {
	case QLCNIC_C2H_OPCODE_GET_LINKEVENT_RESPONSE:
		qlcnic_handle_linkevent(adapter, &msg);
		break;
	case QLCNIC_C2H_OPCODE_CONFIG_LOOPBACK:
		ret = (u32)(msg.body[1]);
		switch (ret) {
		case 0:
			adapter->ahw->loopback_state |= QLCNIC_LB_RESPONSE;
			break;
		case 1:
			dev_info(dev, "loopback already in progress\n");
			adapter->ahw->diag_cnt = -EINPROGRESS;
			break;
		case 2:
			dev_info(dev, "loopback cable is not connected\n");
			adapter->ahw->diag_cnt = -ENODEV;
			break;
		default:
			dev_info(dev,
				 "loopback configure request failed, err %x\n",
				 ret);
			adapter->ahw->diag_cnt = -EIO;
			break;
		}
		break;
	case QLCNIC_C2H_OPCODE_GET_DCB_AEN:
		qlcnic_dcb_aen_handler(adapter->dcb, (void *)&msg);
		break;
	default:
		break;
	}
}

struct sk_buff *qlcnic_process_rxbuf(struct qlcnic_adapter *adapter,
				     struct qlcnic_host_rds_ring *ring,
				     u16 index, u16 cksum)
{
	struct qlcnic_rx_buffer *buffer;
	struct sk_buff *skb;

	buffer = &ring->rx_buf_arr[index];
	if (unlikely(buffer->skb == NULL)) {
		WARN_ON(1);
		return NULL;
	}

	pci_unmap_single(adapter->pdev, buffer->dma, ring->dma_size,
			 PCI_DMA_FROMDEVICE);

	skb = buffer->skb;
	if (likely((adapter->netdev->features & NETIF_F_RXCSUM) &&
		   (cksum == STATUS_CKSUM_OK || cksum == STATUS_CKSUM_LOOP))) {
		adapter->stats.csummed++;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else {
		skb_checksum_none_assert(skb);
	}


	buffer->skb = NULL;

	return skb;
}

static inline int qlcnic_check_rx_tagging(struct qlcnic_adapter *adapter,
					  struct sk_buff *skb, u16 *vlan_tag)
{
	struct ethhdr *eth_hdr;

	if (!__vlan_get_tag(skb, vlan_tag)) {
		eth_hdr = (struct ethhdr *)skb->data;
		memmove(skb->data + VLAN_HLEN, eth_hdr, ETH_ALEN * 2);
		skb_pull(skb, VLAN_HLEN);
	}
	if (!adapter->rx_pvid)
		return 0;

	if (*vlan_tag == adapter->rx_pvid) {
		/* Outer vlan tag. Packet should follow non-vlan path */
		*vlan_tag = 0xffff;
		return 0;
	}
	if (adapter->flags & QLCNIC_TAGGING_ENABLED)
		return 0;

	return -EINVAL;
}

static struct qlcnic_rx_buffer *
qlcnic_process_rcv(struct qlcnic_adapter *adapter,
		   struct qlcnic_host_sds_ring *sds_ring, int ring,
		   u64 sts_data0)
{
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_rx_buffer *buffer;
	struct sk_buff *skb;
	struct qlcnic_host_rds_ring *rds_ring;
	int index, length, cksum, pkt_offset, is_lb_pkt;
	u16 vid = 0xffff, t_vid;

	if (unlikely(ring >= adapter->max_rds_rings))
		return NULL;

	rds_ring = &recv_ctx->rds_rings[ring];

	index = qlcnic_get_sts_refhandle(sts_data0);
	if (unlikely(index >= rds_ring->num_desc))
		return NULL;

	buffer = &rds_ring->rx_buf_arr[index];
	length = qlcnic_get_sts_totallength(sts_data0);
	cksum  = qlcnic_get_sts_status(sts_data0);
	pkt_offset = qlcnic_get_sts_pkt_offset(sts_data0);

	skb = qlcnic_process_rxbuf(adapter, rds_ring, index, cksum);
	if (!skb)
		return buffer;

	if (adapter->drv_mac_learn &&
	    (adapter->flags & QLCNIC_ESWITCH_ENABLED)) {
		t_vid = 0;
		is_lb_pkt = qlcnic_82xx_is_lb_pkt(sts_data0);
		qlcnic_add_lb_filter(adapter, skb, is_lb_pkt, t_vid);
	}

	if (length > rds_ring->skb_size)
		skb_put(skb, rds_ring->skb_size);
	else
		skb_put(skb, length);

	if (pkt_offset)
		skb_pull(skb, pkt_offset);

	if (unlikely(qlcnic_check_rx_tagging(adapter, skb, &vid))) {
		adapter->stats.rxdropped++;
		dev_kfree_skb(skb);
		return buffer;
	}

	skb->protocol = eth_type_trans(skb, netdev);

	if (vid != 0xffff)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);

	napi_gro_receive(&sds_ring->napi, skb);

	adapter->stats.rx_pkts++;
	adapter->stats.rxbytes += length;

	return buffer;
}

#define QLC_TCP_HDR_SIZE            20
#define QLC_TCP_TS_OPTION_SIZE      12
#define QLC_TCP_TS_HDR_SIZE         (QLC_TCP_HDR_SIZE + QLC_TCP_TS_OPTION_SIZE)

static struct qlcnic_rx_buffer *
qlcnic_process_lro(struct qlcnic_adapter *adapter,
		   int ring, u64 sts_data0, u64 sts_data1)
{
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_rx_buffer *buffer;
	struct sk_buff *skb;
	struct qlcnic_host_rds_ring *rds_ring;
	struct iphdr *iph;
	struct ipv6hdr *ipv6h;
	struct tcphdr *th;
	bool push, timestamp;
	int index, l2_hdr_offset, l4_hdr_offset, is_lb_pkt;
	u16 lro_length, length, data_offset, t_vid, vid = 0xffff;
	u32 seq_number;

	if (unlikely(ring > adapter->max_rds_rings))
		return NULL;

	rds_ring = &recv_ctx->rds_rings[ring];

	index = qlcnic_get_lro_sts_refhandle(sts_data0);
	if (unlikely(index > rds_ring->num_desc))
		return NULL;

	buffer = &rds_ring->rx_buf_arr[index];

	timestamp = qlcnic_get_lro_sts_timestamp(sts_data0);
	lro_length = qlcnic_get_lro_sts_length(sts_data0);
	l2_hdr_offset = qlcnic_get_lro_sts_l2_hdr_offset(sts_data0);
	l4_hdr_offset = qlcnic_get_lro_sts_l4_hdr_offset(sts_data0);
	push = qlcnic_get_lro_sts_push_flag(sts_data0);
	seq_number = qlcnic_get_lro_sts_seq_number(sts_data1);

	skb = qlcnic_process_rxbuf(adapter, rds_ring, index, STATUS_CKSUM_OK);
	if (!skb)
		return buffer;

	if (adapter->drv_mac_learn &&
	    (adapter->flags & QLCNIC_ESWITCH_ENABLED)) {
		t_vid = 0;
		is_lb_pkt = qlcnic_82xx_is_lb_pkt(sts_data0);
		qlcnic_add_lb_filter(adapter, skb, is_lb_pkt, t_vid);
	}

	if (timestamp)
		data_offset = l4_hdr_offset + QLC_TCP_TS_HDR_SIZE;
	else
		data_offset = l4_hdr_offset + QLC_TCP_HDR_SIZE;

	skb_put(skb, lro_length + data_offset);
	skb_pull(skb, l2_hdr_offset);

	if (unlikely(qlcnic_check_rx_tagging(adapter, skb, &vid))) {
		adapter->stats.rxdropped++;
		dev_kfree_skb(skb);
		return buffer;
	}

	skb->protocol = eth_type_trans(skb, netdev);

	if (ntohs(skb->protocol) == ETH_P_IPV6) {
		ipv6h = (struct ipv6hdr *)skb->data;
		th = (struct tcphdr *)(skb->data + sizeof(struct ipv6hdr));
		length = (th->doff << 2) + lro_length;
		ipv6h->payload_len = htons(length);
	} else {
		iph = (struct iphdr *)skb->data;
		th = (struct tcphdr *)(skb->data + (iph->ihl << 2));
		length = (iph->ihl << 2) + (th->doff << 2) + lro_length;
		csum_replace2(&iph->check, iph->tot_len, htons(length));
		iph->tot_len = htons(length);
	}

	th->psh = push;
	th->seq = htonl(seq_number);
	length = skb->len;

	if (adapter->flags & QLCNIC_FW_LRO_MSS_CAP) {
		skb_shinfo(skb)->gso_size = qlcnic_get_lro_sts_mss(sts_data1);
		if (skb->protocol == htons(ETH_P_IPV6))
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
		else
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
	}

	if (vid != 0xffff)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);
	netif_receive_skb(skb);

	adapter->stats.lro_pkts++;
	adapter->stats.lrobytes += length;

	return buffer;
}

int qlcnic_process_rcv_ring(struct qlcnic_host_sds_ring *sds_ring, int max)
{
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_adapter *adapter = sds_ring->adapter;
	struct list_head *cur;
	struct status_desc *desc;
	struct qlcnic_rx_buffer *rxbuf;
	int opcode, desc_cnt, count = 0;
	u64 sts_data0, sts_data1;
	u8 ring;
	u32 consumer = sds_ring->consumer;

	while (count < max) {
		desc = &sds_ring->desc_head[consumer];
		sts_data0 = le64_to_cpu(desc->status_desc_data[0]);

		if (!(sts_data0 & STATUS_OWNER_HOST))
			break;

		desc_cnt = qlcnic_get_sts_desc_cnt(sts_data0);
		opcode = qlcnic_get_sts_opcode(sts_data0);
		switch (opcode) {
		case QLCNIC_RXPKT_DESC:
		case QLCNIC_OLD_RXPKT_DESC:
		case QLCNIC_SYN_OFFLOAD:
			ring = qlcnic_get_sts_type(sts_data0);
			rxbuf = qlcnic_process_rcv(adapter, sds_ring, ring,
						   sts_data0);
			break;
		case QLCNIC_LRO_DESC:
			ring = qlcnic_get_lro_sts_type(sts_data0);
			sts_data1 = le64_to_cpu(desc->status_desc_data[1]);
			rxbuf = qlcnic_process_lro(adapter, ring, sts_data0,
						   sts_data1);
			break;
		case QLCNIC_RESPONSE_DESC:
			qlcnic_handle_fw_message(desc_cnt, consumer, sds_ring);
		default:
			goto skip;
		}
		WARN_ON(desc_cnt > 1);

		if (likely(rxbuf))
			list_add_tail(&rxbuf->list, &sds_ring->free_list[ring]);
		else
			adapter->stats.null_rxbuf++;
skip:
		for (; desc_cnt > 0; desc_cnt--) {
			desc = &sds_ring->desc_head[consumer];
			desc->status_desc_data[0] = QLCNIC_DESC_OWNER_FW;
			consumer = get_next_index(consumer, sds_ring->num_desc);
		}
		count++;
	}

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &adapter->recv_ctx->rds_rings[ring];
		if (!list_empty(&sds_ring->free_list[ring])) {
			list_for_each(cur, &sds_ring->free_list[ring]) {
				rxbuf = list_entry(cur, struct qlcnic_rx_buffer,
						   list);
				qlcnic_alloc_rx_skb(adapter, rds_ring, rxbuf);
			}
			spin_lock(&rds_ring->lock);
			list_splice_tail_init(&sds_ring->free_list[ring],
					      &rds_ring->free_list);
			spin_unlock(&rds_ring->lock);
		}

		qlcnic_post_rx_buffers_nodb(adapter, rds_ring, ring);
	}

	if (count) {
		sds_ring->consumer = consumer;
		writel(consumer, sds_ring->crb_sts_consumer);
	}

	return count;
}

void qlcnic_post_rx_buffers(struct qlcnic_adapter *adapter,
			    struct qlcnic_host_rds_ring *rds_ring, u8 ring_id)
{
	struct rcv_desc *pdesc;
	struct qlcnic_rx_buffer *buffer;
	int count = 0;
	u32 producer, handle;
	struct list_head *head;

	producer = rds_ring->producer;
	head = &rds_ring->free_list;

	while (!list_empty(head)) {

		buffer = list_entry(head->next, struct qlcnic_rx_buffer, list);

		if (!buffer->skb) {
			if (qlcnic_alloc_rx_skb(adapter, rds_ring, buffer))
				break;
		}

		count++;
		list_del(&buffer->list);

		/* make a rcv descriptor  */
		pdesc = &rds_ring->desc_head[producer];
		pdesc->addr_buffer = cpu_to_le64(buffer->dma);
		handle = qlcnic_get_ref_handle(adapter, buffer->ref_handle,
					       ring_id);
		pdesc->reference_handle = cpu_to_le16(handle);
		pdesc->buffer_length = cpu_to_le32(rds_ring->dma_size);
		producer = get_next_index(producer, rds_ring->num_desc);
	}

	if (count) {
		rds_ring->producer = producer;
		writel((producer-1) & (rds_ring->num_desc-1),
		       rds_ring->crb_rcv_producer);
	}
}

static void dump_skb(struct sk_buff *skb, struct qlcnic_adapter *adapter)
{
	int i;
	unsigned char *data = skb->data;

	pr_info(KERN_INFO "\n");
	for (i = 0; i < skb->len; i++) {
		QLCDB(adapter, DRV, "%02x ", data[i]);
		if ((i & 0x0f) == 8)
			pr_info(KERN_INFO "\n");
	}
}

static void qlcnic_process_rcv_diag(struct qlcnic_adapter *adapter, int ring,
				    u64 sts_data0)
{
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct sk_buff *skb;
	struct qlcnic_host_rds_ring *rds_ring;
	int index, length, cksum, pkt_offset;

	if (unlikely(ring >= adapter->max_rds_rings))
		return;

	rds_ring = &recv_ctx->rds_rings[ring];

	index = qlcnic_get_sts_refhandle(sts_data0);
	length = qlcnic_get_sts_totallength(sts_data0);
	if (unlikely(index >= rds_ring->num_desc))
		return;

	cksum  = qlcnic_get_sts_status(sts_data0);
	pkt_offset = qlcnic_get_sts_pkt_offset(sts_data0);

	skb = qlcnic_process_rxbuf(adapter, rds_ring, index, cksum);
	if (!skb)
		return;

	if (length > rds_ring->skb_size)
		skb_put(skb, rds_ring->skb_size);
	else
		skb_put(skb, length);

	if (pkt_offset)
		skb_pull(skb, pkt_offset);

	if (!qlcnic_check_loopback_buff(skb->data, adapter->mac_addr))
		adapter->ahw->diag_cnt++;
	else
		dump_skb(skb, adapter);

	dev_kfree_skb_any(skb);
	adapter->stats.rx_pkts++;
	adapter->stats.rxbytes += length;

	return;
}

void qlcnic_82xx_process_rcv_ring_diag(struct qlcnic_host_sds_ring *sds_ring)
{
	struct qlcnic_adapter *adapter = sds_ring->adapter;
	struct status_desc *desc;
	u64 sts_data0;
	int ring, opcode, desc_cnt;

	u32 consumer = sds_ring->consumer;

	desc = &sds_ring->desc_head[consumer];
	sts_data0 = le64_to_cpu(desc->status_desc_data[0]);

	if (!(sts_data0 & STATUS_OWNER_HOST))
		return;

	desc_cnt = qlcnic_get_sts_desc_cnt(sts_data0);
	opcode = qlcnic_get_sts_opcode(sts_data0);
	switch (opcode) {
	case QLCNIC_RESPONSE_DESC:
		qlcnic_handle_fw_message(desc_cnt, consumer, sds_ring);
		break;
	default:
		ring = qlcnic_get_sts_type(sts_data0);
		qlcnic_process_rcv_diag(adapter, ring, sts_data0);
		break;
	}

	for (; desc_cnt > 0; desc_cnt--) {
		desc = &sds_ring->desc_head[consumer];
		desc->status_desc_data[0] = cpu_to_le64(STATUS_OWNER_PHANTOM);
		consumer = get_next_index(consumer, sds_ring->num_desc);
	}

	sds_ring->consumer = consumer;
	writel(consumer, sds_ring->crb_sts_consumer);
}

int qlcnic_82xx_napi_add(struct qlcnic_adapter *adapter,
			 struct net_device *netdev)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_host_tx_ring *tx_ring;

	if (qlcnic_alloc_sds_rings(recv_ctx, adapter->drv_sds_rings))
		return -ENOMEM;

	for (ring = 0; ring < adapter->drv_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		if (qlcnic_check_multi_tx(adapter) &&
		    !adapter->ahw->diag_test &&
		    (adapter->drv_tx_rings > QLCNIC_SINGLE_RING)) {
			netif_napi_add(netdev, &sds_ring->napi, qlcnic_rx_poll,
				       NAPI_POLL_WEIGHT);
		} else {
			if (ring == (adapter->drv_sds_rings - 1))
				netif_napi_add(netdev, &sds_ring->napi,
					       qlcnic_poll,
					       NAPI_POLL_WEIGHT);
			else
				netif_napi_add(netdev, &sds_ring->napi,
					       qlcnic_rx_poll,
					       NAPI_POLL_WEIGHT);
		}
	}

	if (qlcnic_alloc_tx_rings(adapter, netdev)) {
		qlcnic_free_sds_rings(recv_ctx);
		return -ENOMEM;
	}

	if (qlcnic_check_multi_tx(adapter) && !adapter->ahw->diag_test) {
		for (ring = 0; ring < adapter->drv_tx_rings; ring++) {
			tx_ring = &adapter->tx_ring[ring];
			netif_napi_add(netdev, &tx_ring->napi, qlcnic_tx_poll,
				       NAPI_POLL_WEIGHT);
		}
	}

	return 0;
}

void qlcnic_82xx_napi_del(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_host_tx_ring *tx_ring;

	for (ring = 0; ring < adapter->drv_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		netif_napi_del(&sds_ring->napi);
	}

	qlcnic_free_sds_rings(adapter->recv_ctx);

	if (qlcnic_check_multi_tx(adapter) && !adapter->ahw->diag_test) {
		for (ring = 0; ring < adapter->drv_tx_rings; ring++) {
			tx_ring = &adapter->tx_ring[ring];
			netif_napi_del(&tx_ring->napi);
		}
	}

	qlcnic_free_tx_rings(adapter);
}

void qlcnic_82xx_napi_enable(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_host_tx_ring *tx_ring;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return;

	for (ring = 0; ring < adapter->drv_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		napi_enable(&sds_ring->napi);
		qlcnic_enable_int(sds_ring);
	}

	if (qlcnic_check_multi_tx(adapter) &&
	    (adapter->flags & QLCNIC_MSIX_ENABLED) &&
	    !adapter->ahw->diag_test &&
	    (adapter->drv_tx_rings > QLCNIC_SINGLE_RING)) {
		for (ring = 0; ring < adapter->drv_tx_rings; ring++) {
			tx_ring = &adapter->tx_ring[ring];
			napi_enable(&tx_ring->napi);
			qlcnic_enable_tx_intr(adapter, tx_ring);
		}
	}
}

void qlcnic_82xx_napi_disable(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_host_tx_ring *tx_ring;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return;

	for (ring = 0; ring < adapter->drv_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		qlcnic_disable_int(sds_ring);
		napi_synchronize(&sds_ring->napi);
		napi_disable(&sds_ring->napi);
	}

	if ((adapter->flags & QLCNIC_MSIX_ENABLED) &&
	    !adapter->ahw->diag_test &&
	    qlcnic_check_multi_tx(adapter)) {
		for (ring = 0; ring < adapter->drv_tx_rings; ring++) {
			tx_ring = &adapter->tx_ring[ring];
			qlcnic_disable_tx_int(adapter, tx_ring);
			napi_synchronize(&tx_ring->napi);
			napi_disable(&tx_ring->napi);
		}
	}
}

#define QLC_83XX_NORMAL_LB_PKT	(1ULL << 36)
#define QLC_83XX_LRO_LB_PKT	(1ULL << 46)

static inline int qlcnic_83xx_is_lb_pkt(u64 sts_data, int lro_pkt)
{
	if (lro_pkt)
		return (sts_data & QLC_83XX_LRO_LB_PKT) ? 1 : 0;
	else
		return (sts_data & QLC_83XX_NORMAL_LB_PKT) ? 1 : 0;
}

static struct qlcnic_rx_buffer *
qlcnic_83xx_process_rcv(struct qlcnic_adapter *adapter,
			struct qlcnic_host_sds_ring *sds_ring,
			u8 ring, u64 sts_data[])
{
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_rx_buffer *buffer;
	struct sk_buff *skb;
	struct qlcnic_host_rds_ring *rds_ring;
	int index, length, cksum, is_lb_pkt;
	u16 vid = 0xffff, t_vid;

	if (unlikely(ring >= adapter->max_rds_rings))
		return NULL;

	rds_ring = &recv_ctx->rds_rings[ring];

	index = qlcnic_83xx_hndl(sts_data[0]);
	if (unlikely(index >= rds_ring->num_desc))
		return NULL;

	buffer = &rds_ring->rx_buf_arr[index];
	length = qlcnic_83xx_pktln(sts_data[0]);
	cksum  = qlcnic_83xx_csum_status(sts_data[1]);
	skb = qlcnic_process_rxbuf(adapter, rds_ring, index, cksum);
	if (!skb)
		return buffer;

	if (adapter->drv_mac_learn &&
	    (adapter->flags & QLCNIC_ESWITCH_ENABLED)) {
		t_vid = 0;
		is_lb_pkt = qlcnic_83xx_is_lb_pkt(sts_data[1], 0);
		qlcnic_add_lb_filter(adapter, skb, is_lb_pkt, t_vid);
	}

	if (length > rds_ring->skb_size)
		skb_put(skb, rds_ring->skb_size);
	else
		skb_put(skb, length);

	if (unlikely(qlcnic_check_rx_tagging(adapter, skb, &vid))) {
		adapter->stats.rxdropped++;
		dev_kfree_skb(skb);
		return buffer;
	}

	skb->protocol = eth_type_trans(skb, netdev);

	if (vid != 0xffff)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);

	napi_gro_receive(&sds_ring->napi, skb);

	adapter->stats.rx_pkts++;
	adapter->stats.rxbytes += length;

	return buffer;
}

static struct qlcnic_rx_buffer *
qlcnic_83xx_process_lro(struct qlcnic_adapter *adapter,
			u8 ring, u64 sts_data[])
{
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_rx_buffer *buffer;
	struct sk_buff *skb;
	struct qlcnic_host_rds_ring *rds_ring;
	struct iphdr *iph;
	struct ipv6hdr *ipv6h;
	struct tcphdr *th;
	bool push;
	int l2_hdr_offset, l4_hdr_offset;
	int index, is_lb_pkt;
	u16 lro_length, length, data_offset, gso_size;
	u16 vid = 0xffff, t_vid;

	if (unlikely(ring > adapter->max_rds_rings))
		return NULL;

	rds_ring = &recv_ctx->rds_rings[ring];

	index = qlcnic_83xx_hndl(sts_data[0]);
	if (unlikely(index > rds_ring->num_desc))
		return NULL;

	buffer = &rds_ring->rx_buf_arr[index];

	lro_length = qlcnic_83xx_lro_pktln(sts_data[0]);
	l2_hdr_offset = qlcnic_83xx_l2_hdr_off(sts_data[1]);
	l4_hdr_offset = qlcnic_83xx_l4_hdr_off(sts_data[1]);
	push = qlcnic_83xx_is_psh_bit(sts_data[1]);

	skb = qlcnic_process_rxbuf(adapter, rds_ring, index, STATUS_CKSUM_OK);
	if (!skb)
		return buffer;

	if (adapter->drv_mac_learn &&
	    (adapter->flags & QLCNIC_ESWITCH_ENABLED)) {
		t_vid = 0;
		is_lb_pkt = qlcnic_83xx_is_lb_pkt(sts_data[1], 1);
		qlcnic_add_lb_filter(adapter, skb, is_lb_pkt, t_vid);
	}
	if (qlcnic_83xx_is_tstamp(sts_data[1]))
		data_offset = l4_hdr_offset + QLCNIC_TCP_TS_HDR_SIZE;
	else
		data_offset = l4_hdr_offset + QLCNIC_TCP_HDR_SIZE;

	skb_put(skb, lro_length + data_offset);
	skb_pull(skb, l2_hdr_offset);

	if (unlikely(qlcnic_check_rx_tagging(adapter, skb, &vid))) {
		adapter->stats.rxdropped++;
		dev_kfree_skb(skb);
		return buffer;
	}

	skb->protocol = eth_type_trans(skb, netdev);
	if (ntohs(skb->protocol) == ETH_P_IPV6) {
		ipv6h = (struct ipv6hdr *)skb->data;
		th = (struct tcphdr *)(skb->data + sizeof(struct ipv6hdr));

		length = (th->doff << 2) + lro_length;
		ipv6h->payload_len = htons(length);
	} else {
		iph = (struct iphdr *)skb->data;
		th = (struct tcphdr *)(skb->data + (iph->ihl << 2));
		length = (iph->ihl << 2) + (th->doff << 2) + lro_length;
		csum_replace2(&iph->check, iph->tot_len, htons(length));
		iph->tot_len = htons(length);
	}

	th->psh = push;
	length = skb->len;

	if (adapter->flags & QLCNIC_FW_LRO_MSS_CAP) {
		gso_size = qlcnic_83xx_get_lro_sts_mss(sts_data[0]);
		skb_shinfo(skb)->gso_size = gso_size;
		if (skb->protocol == htons(ETH_P_IPV6))
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
		else
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
	}

	if (vid != 0xffff)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);

	netif_receive_skb(skb);

	adapter->stats.lro_pkts++;
	adapter->stats.lrobytes += length;
	return buffer;
}

static int qlcnic_83xx_process_rcv_ring(struct qlcnic_host_sds_ring *sds_ring,
					int max)
{
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_adapter *adapter = sds_ring->adapter;
	struct list_head *cur;
	struct status_desc *desc;
	struct qlcnic_rx_buffer *rxbuf = NULL;
	u8 ring;
	u64 sts_data[2];
	int count = 0, opcode;
	u32 consumer = sds_ring->consumer;

	while (count < max) {
		desc = &sds_ring->desc_head[consumer];
		sts_data[1] = le64_to_cpu(desc->status_desc_data[1]);
		opcode = qlcnic_83xx_opcode(sts_data[1]);
		if (!opcode)
			break;
		sts_data[0] = le64_to_cpu(desc->status_desc_data[0]);
		ring = QLCNIC_FETCH_RING_ID(sts_data[0]);

		switch (opcode) {
		case QLC_83XX_REG_DESC:
			rxbuf = qlcnic_83xx_process_rcv(adapter, sds_ring,
							ring, sts_data);
			break;
		case QLC_83XX_LRO_DESC:
			rxbuf = qlcnic_83xx_process_lro(adapter, ring,
							sts_data);
			break;
		default:
			dev_info(&adapter->pdev->dev,
				 "Unknown opcode: 0x%x\n", opcode);
			goto skip;
		}

		if (likely(rxbuf))
			list_add_tail(&rxbuf->list, &sds_ring->free_list[ring]);
		else
			adapter->stats.null_rxbuf++;
skip:
		desc = &sds_ring->desc_head[consumer];
		/* Reset the descriptor */
		desc->status_desc_data[1] = 0;
		consumer = get_next_index(consumer, sds_ring->num_desc);
		count++;
	}
	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &adapter->recv_ctx->rds_rings[ring];
		if (!list_empty(&sds_ring->free_list[ring])) {
			list_for_each(cur, &sds_ring->free_list[ring]) {
				rxbuf = list_entry(cur, struct qlcnic_rx_buffer,
						   list);
				qlcnic_alloc_rx_skb(adapter, rds_ring, rxbuf);
			}
			spin_lock(&rds_ring->lock);
			list_splice_tail_init(&sds_ring->free_list[ring],
					      &rds_ring->free_list);
			spin_unlock(&rds_ring->lock);
		}
		qlcnic_post_rx_buffers_nodb(adapter, rds_ring, ring);
	}
	if (count) {
		sds_ring->consumer = consumer;
		writel(consumer, sds_ring->crb_sts_consumer);
	}
	return count;
}

static int qlcnic_83xx_msix_sriov_vf_poll(struct napi_struct *napi, int budget)
{
	int tx_complete;
	int work_done;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_adapter *adapter;
	struct qlcnic_host_tx_ring *tx_ring;

	sds_ring = container_of(napi, struct qlcnic_host_sds_ring, napi);
	adapter = sds_ring->adapter;
	/* tx ring count = 1 */
	tx_ring = adapter->tx_ring;

	tx_complete = qlcnic_process_cmd_ring(adapter, tx_ring, budget);
	work_done = qlcnic_83xx_process_rcv_ring(sds_ring, budget);
	if ((work_done < budget) && tx_complete) {
		napi_complete(&sds_ring->napi);
		qlcnic_83xx_enable_intr(adapter, sds_ring);
	}

	return work_done;
}

static int qlcnic_83xx_poll(struct napi_struct *napi, int budget)
{
	int tx_complete;
	int work_done;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_adapter *adapter;
	struct qlcnic_host_tx_ring *tx_ring;

	sds_ring = container_of(napi, struct qlcnic_host_sds_ring, napi);
	adapter = sds_ring->adapter;
	/* tx ring count = 1 */
	tx_ring = adapter->tx_ring;

	tx_complete = qlcnic_process_cmd_ring(adapter, tx_ring, budget);
	work_done = qlcnic_83xx_process_rcv_ring(sds_ring, budget);
	if ((work_done < budget) && tx_complete) {
		napi_complete(&sds_ring->napi);
		qlcnic_83xx_enable_intr(adapter, sds_ring);
	}

	return work_done;
}

static int qlcnic_83xx_msix_tx_poll(struct napi_struct *napi, int budget)
{
	int work_done;
	struct qlcnic_host_tx_ring *tx_ring;
	struct qlcnic_adapter *adapter;

	budget = QLCNIC_TX_POLL_BUDGET;
	tx_ring = container_of(napi, struct qlcnic_host_tx_ring, napi);
	adapter = tx_ring->adapter;
	work_done = qlcnic_process_cmd_ring(adapter, tx_ring, budget);
	if (work_done) {
		napi_complete(&tx_ring->napi);
		if (test_bit(__QLCNIC_DEV_UP , &adapter->state))
			qlcnic_83xx_enable_tx_intr(adapter, tx_ring);
	}

	return work_done;
}

static int qlcnic_83xx_rx_poll(struct napi_struct *napi, int budget)
{
	int work_done;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_adapter *adapter;

	sds_ring = container_of(napi, struct qlcnic_host_sds_ring, napi);
	adapter = sds_ring->adapter;
	work_done = qlcnic_83xx_process_rcv_ring(sds_ring, budget);
	if (work_done < budget) {
		napi_complete(&sds_ring->napi);
		if (test_bit(__QLCNIC_DEV_UP, &adapter->state))
			qlcnic_83xx_enable_intr(adapter, sds_ring);
	}

	return work_done;
}

void qlcnic_83xx_napi_enable(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_host_tx_ring *tx_ring;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return;

	for (ring = 0; ring < adapter->drv_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		napi_enable(&sds_ring->napi);
		if (adapter->flags & QLCNIC_MSIX_ENABLED)
			qlcnic_83xx_enable_intr(adapter, sds_ring);
	}

	if ((adapter->flags & QLCNIC_MSIX_ENABLED) &&
	    !(adapter->flags & QLCNIC_TX_INTR_SHARED)) {
		for (ring = 0; ring < adapter->drv_tx_rings; ring++) {
			tx_ring = &adapter->tx_ring[ring];
			napi_enable(&tx_ring->napi);
			qlcnic_83xx_enable_tx_intr(adapter, tx_ring);
		}
	}
}

void qlcnic_83xx_napi_disable(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_host_tx_ring *tx_ring;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return;

	for (ring = 0; ring < adapter->drv_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		if (adapter->flags & QLCNIC_MSIX_ENABLED)
			qlcnic_83xx_disable_intr(adapter, sds_ring);
		napi_synchronize(&sds_ring->napi);
		napi_disable(&sds_ring->napi);
	}

	if ((adapter->flags & QLCNIC_MSIX_ENABLED) &&
	    !(adapter->flags & QLCNIC_TX_INTR_SHARED)) {
		for (ring = 0; ring < adapter->drv_tx_rings; ring++) {
			tx_ring = &adapter->tx_ring[ring];
			qlcnic_83xx_disable_tx_intr(adapter, tx_ring);
			napi_synchronize(&tx_ring->napi);
			napi_disable(&tx_ring->napi);
		}
	}
}

int qlcnic_83xx_napi_add(struct qlcnic_adapter *adapter,
			 struct net_device *netdev)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_host_tx_ring *tx_ring;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;

	if (qlcnic_alloc_sds_rings(recv_ctx, adapter->drv_sds_rings))
		return -ENOMEM;

	for (ring = 0; ring < adapter->drv_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		if (adapter->flags & QLCNIC_MSIX_ENABLED) {
			if (!(adapter->flags & QLCNIC_TX_INTR_SHARED))
				netif_napi_add(netdev, &sds_ring->napi,
					       qlcnic_83xx_rx_poll,
					       NAPI_POLL_WEIGHT);
			else
				netif_napi_add(netdev, &sds_ring->napi,
					       qlcnic_83xx_msix_sriov_vf_poll,
					       NAPI_POLL_WEIGHT);

		} else {
			netif_napi_add(netdev, &sds_ring->napi,
				       qlcnic_83xx_poll,
				       NAPI_POLL_WEIGHT);
		}
	}

	if (qlcnic_alloc_tx_rings(adapter, netdev)) {
		qlcnic_free_sds_rings(recv_ctx);
		return -ENOMEM;
	}

	if ((adapter->flags & QLCNIC_MSIX_ENABLED) &&
	    !(adapter->flags & QLCNIC_TX_INTR_SHARED)) {
		for (ring = 0; ring < adapter->drv_tx_rings; ring++) {
			tx_ring = &adapter->tx_ring[ring];
			netif_napi_add(netdev, &tx_ring->napi,
				       qlcnic_83xx_msix_tx_poll,
				       NAPI_POLL_WEIGHT);
		}
	}

	return 0;
}

void qlcnic_83xx_napi_del(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_host_tx_ring *tx_ring;

	for (ring = 0; ring < adapter->drv_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		netif_napi_del(&sds_ring->napi);
	}

	qlcnic_free_sds_rings(adapter->recv_ctx);

	if ((adapter->flags & QLCNIC_MSIX_ENABLED) &&
	    !(adapter->flags & QLCNIC_TX_INTR_SHARED)) {
		for (ring = 0; ring < adapter->drv_tx_rings; ring++) {
			tx_ring = &adapter->tx_ring[ring];
			netif_napi_del(&tx_ring->napi);
		}
	}

	qlcnic_free_tx_rings(adapter);
}

void qlcnic_83xx_process_rcv_diag(struct qlcnic_adapter *adapter,
				  int ring, u64 sts_data[])
{
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct sk_buff *skb;
	struct qlcnic_host_rds_ring *rds_ring;
	int index, length;

	if (unlikely(ring >= adapter->max_rds_rings))
		return;

	rds_ring = &recv_ctx->rds_rings[ring];
	index = qlcnic_83xx_hndl(sts_data[0]);
	if (unlikely(index >= rds_ring->num_desc))
		return;

	length = qlcnic_83xx_pktln(sts_data[0]);

	skb = qlcnic_process_rxbuf(adapter, rds_ring, index, STATUS_CKSUM_OK);
	if (!skb)
		return;

	if (length > rds_ring->skb_size)
		skb_put(skb, rds_ring->skb_size);
	else
		skb_put(skb, length);

	if (!qlcnic_check_loopback_buff(skb->data, adapter->mac_addr))
		adapter->ahw->diag_cnt++;
	else
		dump_skb(skb, adapter);

	dev_kfree_skb_any(skb);
	return;
}

void qlcnic_83xx_process_rcv_ring_diag(struct qlcnic_host_sds_ring *sds_ring)
{
	struct qlcnic_adapter *adapter = sds_ring->adapter;
	struct status_desc *desc;
	u64 sts_data[2];
	int ring, opcode;
	u32 consumer = sds_ring->consumer;

	desc = &sds_ring->desc_head[consumer];
	sts_data[0] = le64_to_cpu(desc->status_desc_data[0]);
	sts_data[1] = le64_to_cpu(desc->status_desc_data[1]);
	opcode = qlcnic_83xx_opcode(sts_data[1]);
	if (!opcode)
		return;

	ring = QLCNIC_FETCH_RING_ID(qlcnic_83xx_hndl(sts_data[0]));
	qlcnic_83xx_process_rcv_diag(adapter, ring, sts_data);
	desc = &sds_ring->desc_head[consumer];
	desc->status_desc_data[0] = cpu_to_le64(STATUS_OWNER_PHANTOM);
	consumer = get_next_index(consumer, sds_ring->num_desc);
	sds_ring->consumer = consumer;
	writel(consumer, sds_ring->crb_sts_consumer);
}
