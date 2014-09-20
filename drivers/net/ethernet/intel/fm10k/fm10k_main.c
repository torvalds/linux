/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include <linux/types.h>
#include <linux/module.h>
#include <net/ipv6.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <linux/if_macvlan.h>
#include <linux/prefetch.h>

#include "fm10k.h"

#define DRV_VERSION	"0.12.2-k"
const char fm10k_driver_version[] = DRV_VERSION;
char fm10k_driver_name[] = "fm10k";
static const char fm10k_driver_string[] =
	"Intel(R) Ethernet Switch Host Interface Driver";
static const char fm10k_copyright[] =
	"Copyright (c) 2013 Intel Corporation.";

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) Ethernet Switch Host Interface Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/**
 * fm10k_init_module - Driver Registration Routine
 *
 * fm10k_init_module is the first routine called when the driver is
 * loaded.  All it does is register with the PCI subsystem.
 **/
static int __init fm10k_init_module(void)
{
	pr_info("%s - version %s\n", fm10k_driver_string, fm10k_driver_version);
	pr_info("%s\n", fm10k_copyright);

	return fm10k_register_pci_driver();
}
module_init(fm10k_init_module);

/**
 * fm10k_exit_module - Driver Exit Cleanup Routine
 *
 * fm10k_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit fm10k_exit_module(void)
{
	fm10k_unregister_pci_driver();
}
module_exit(fm10k_exit_module);

static bool fm10k_alloc_mapped_page(struct fm10k_ring *rx_ring,
				    struct fm10k_rx_buffer *bi)
{
	struct page *page = bi->page;
	dma_addr_t dma;

	/* Only page will be NULL if buffer was consumed */
	if (likely(page))
		return true;

	/* alloc new page for storage */
	page = alloc_page(GFP_ATOMIC | __GFP_COLD);
	if (unlikely(!page)) {
		rx_ring->rx_stats.alloc_failed++;
		return false;
	}

	/* map page for use */
	dma = dma_map_page(rx_ring->dev, page, 0, PAGE_SIZE, DMA_FROM_DEVICE);

	/* if mapping failed free memory back to system since
	 * there isn't much point in holding memory we can't use
	 */
	if (dma_mapping_error(rx_ring->dev, dma)) {
		__free_page(page);
		bi->page = NULL;

		rx_ring->rx_stats.alloc_failed++;
		return false;
	}

	bi->dma = dma;
	bi->page = page;
	bi->page_offset = 0;

	return true;
}

/**
 * fm10k_alloc_rx_buffers - Replace used receive buffers
 * @rx_ring: ring to place buffers on
 * @cleaned_count: number of buffers to replace
 **/
void fm10k_alloc_rx_buffers(struct fm10k_ring *rx_ring, u16 cleaned_count)
{
	union fm10k_rx_desc *rx_desc;
	struct fm10k_rx_buffer *bi;
	u16 i = rx_ring->next_to_use;

	/* nothing to do */
	if (!cleaned_count)
		return;

	rx_desc = FM10K_RX_DESC(rx_ring, i);
	bi = &rx_ring->rx_buffer[i];
	i -= rx_ring->count;

	do {
		if (!fm10k_alloc_mapped_page(rx_ring, bi))
			break;

		/* Refresh the desc even if buffer_addrs didn't change
		 * because each write-back erases this info.
		 */
		rx_desc->q.pkt_addr = cpu_to_le64(bi->dma + bi->page_offset);

		rx_desc++;
		bi++;
		i++;
		if (unlikely(!i)) {
			rx_desc = FM10K_RX_DESC(rx_ring, 0);
			bi = rx_ring->rx_buffer;
			i -= rx_ring->count;
		}

		/* clear the hdr_addr for the next_to_use descriptor */
		rx_desc->q.hdr_addr = 0;

		cleaned_count--;
	} while (cleaned_count);

	i += rx_ring->count;

	if (rx_ring->next_to_use != i) {
		/* record the next descriptor to use */
		rx_ring->next_to_use = i;

		/* update next to alloc since we have filled the ring */
		rx_ring->next_to_alloc = i;

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();

		/* notify hardware of new descriptors */
		writel(i, rx_ring->tail);
	}
}

/**
 * fm10k_reuse_rx_page - page flip buffer and store it back on the ring
 * @rx_ring: rx descriptor ring to store buffers on
 * @old_buff: donor buffer to have page reused
 *
 * Synchronizes page for reuse by the interface
 **/
static void fm10k_reuse_rx_page(struct fm10k_ring *rx_ring,
				struct fm10k_rx_buffer *old_buff)
{
	struct fm10k_rx_buffer *new_buff;
	u16 nta = rx_ring->next_to_alloc;

	new_buff = &rx_ring->rx_buffer[nta];

	/* update, and store next to alloc */
	nta++;
	rx_ring->next_to_alloc = (nta < rx_ring->count) ? nta : 0;

	/* transfer page from old buffer to new buffer */
	memcpy(new_buff, old_buff, sizeof(struct fm10k_rx_buffer));

	/* sync the buffer for use by the device */
	dma_sync_single_range_for_device(rx_ring->dev, old_buff->dma,
					 old_buff->page_offset,
					 FM10K_RX_BUFSZ,
					 DMA_FROM_DEVICE);
}

static bool fm10k_can_reuse_rx_page(struct fm10k_rx_buffer *rx_buffer,
				    struct page *page,
				    unsigned int truesize)
{
	/* avoid re-using remote pages */
	if (unlikely(page_to_nid(page) != numa_mem_id()))
		return false;

#if (PAGE_SIZE < 8192)
	/* if we are only owner of page we can reuse it */
	if (unlikely(page_count(page) != 1))
		return false;

	/* flip page offset to other buffer */
	rx_buffer->page_offset ^= FM10K_RX_BUFSZ;

	/* since we are the only owner of the page and we need to
	 * increment it, just set the value to 2 in order to avoid
	 * an unnecessary locked operation
	 */
	atomic_set(&page->_count, 2);
#else
	/* move offset up to the next cache line */
	rx_buffer->page_offset += truesize;

	if (rx_buffer->page_offset > (PAGE_SIZE - FM10K_RX_BUFSZ))
		return false;

	/* bump ref count on page before it is given to the stack */
	get_page(page);
#endif

	return true;
}

/**
 * fm10k_add_rx_frag - Add contents of Rx buffer to sk_buff
 * @rx_ring: rx descriptor ring to transact packets on
 * @rx_buffer: buffer containing page to add
 * @rx_desc: descriptor containing length of buffer written by hardware
 * @skb: sk_buff to place the data into
 *
 * This function will add the data contained in rx_buffer->page to the skb.
 * This is done either through a direct copy if the data in the buffer is
 * less than the skb header size, otherwise it will just attach the page as
 * a frag to the skb.
 *
 * The function will then update the page offset if necessary and return
 * true if the buffer can be reused by the interface.
 **/
static bool fm10k_add_rx_frag(struct fm10k_ring *rx_ring,
			      struct fm10k_rx_buffer *rx_buffer,
			      union fm10k_rx_desc *rx_desc,
			      struct sk_buff *skb)
{
	struct page *page = rx_buffer->page;
	unsigned int size = le16_to_cpu(rx_desc->w.length);
#if (PAGE_SIZE < 8192)
	unsigned int truesize = FM10K_RX_BUFSZ;
#else
	unsigned int truesize = ALIGN(size, L1_CACHE_BYTES);
#endif

	if ((size <= FM10K_RX_HDR_LEN) && !skb_is_nonlinear(skb)) {
		unsigned char *va = page_address(page) + rx_buffer->page_offset;

		memcpy(__skb_put(skb, size), va, ALIGN(size, sizeof(long)));

		/* we can reuse buffer as-is, just make sure it is local */
		if (likely(page_to_nid(page) == numa_mem_id()))
			return true;

		/* this page cannot be reused so discard it */
		put_page(page);
		return false;
	}

	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page,
			rx_buffer->page_offset, size, truesize);

	return fm10k_can_reuse_rx_page(rx_buffer, page, truesize);
}

static struct sk_buff *fm10k_fetch_rx_buffer(struct fm10k_ring *rx_ring,
					     union fm10k_rx_desc *rx_desc,
					     struct sk_buff *skb)
{
	struct fm10k_rx_buffer *rx_buffer;
	struct page *page;

	rx_buffer = &rx_ring->rx_buffer[rx_ring->next_to_clean];

	page = rx_buffer->page;
	prefetchw(page);

	if (likely(!skb)) {
		void *page_addr = page_address(page) +
				  rx_buffer->page_offset;

		/* prefetch first cache line of first page */
		prefetch(page_addr);
#if L1_CACHE_BYTES < 128
		prefetch(page_addr + L1_CACHE_BYTES);
#endif

		/* allocate a skb to store the frags */
		skb = netdev_alloc_skb_ip_align(rx_ring->netdev,
						FM10K_RX_HDR_LEN);
		if (unlikely(!skb)) {
			rx_ring->rx_stats.alloc_failed++;
			return NULL;
		}

		/* we will be copying header into skb->data in
		 * pskb_may_pull so it is in our interest to prefetch
		 * it now to avoid a possible cache miss
		 */
		prefetchw(skb->data);
	}

	/* we are reusing so sync this buffer for CPU use */
	dma_sync_single_range_for_cpu(rx_ring->dev,
				      rx_buffer->dma,
				      rx_buffer->page_offset,
				      FM10K_RX_BUFSZ,
				      DMA_FROM_DEVICE);

	/* pull page into skb */
	if (fm10k_add_rx_frag(rx_ring, rx_buffer, rx_desc, skb)) {
		/* hand second half of page back to the ring */
		fm10k_reuse_rx_page(rx_ring, rx_buffer);
	} else {
		/* we are not reusing the buffer so unmap it */
		dma_unmap_page(rx_ring->dev, rx_buffer->dma,
			       PAGE_SIZE, DMA_FROM_DEVICE);
	}

	/* clear contents of rx_buffer */
	rx_buffer->page = NULL;

	return skb;
}

static inline void fm10k_rx_checksum(struct fm10k_ring *ring,
				     union fm10k_rx_desc *rx_desc,
				     struct sk_buff *skb)
{
	skb_checksum_none_assert(skb);

	/* Rx checksum disabled via ethtool */
	if (!(ring->netdev->features & NETIF_F_RXCSUM))
		return;

	/* TCP/UDP checksum error bit is set */
	if (fm10k_test_staterr(rx_desc,
			       FM10K_RXD_STATUS_L4E |
			       FM10K_RXD_STATUS_L4E2 |
			       FM10K_RXD_STATUS_IPE |
			       FM10K_RXD_STATUS_IPE2)) {
		ring->rx_stats.csum_err++;
		return;
	}

	/* It must be a TCP or UDP packet with a valid checksum */
	if (fm10k_test_staterr(rx_desc, FM10K_RXD_STATUS_L4CS2))
		skb->encapsulation = true;
	else if (!fm10k_test_staterr(rx_desc, FM10K_RXD_STATUS_L4CS))
		return;

	skb->ip_summed = CHECKSUM_UNNECESSARY;
}

#define FM10K_RSS_L4_TYPES_MASK \
	((1ul << FM10K_RSSTYPE_IPV4_TCP) | \
	 (1ul << FM10K_RSSTYPE_IPV4_UDP) | \
	 (1ul << FM10K_RSSTYPE_IPV6_TCP) | \
	 (1ul << FM10K_RSSTYPE_IPV6_UDP))

static inline void fm10k_rx_hash(struct fm10k_ring *ring,
				 union fm10k_rx_desc *rx_desc,
				 struct sk_buff *skb)
{
	u16 rss_type;

	if (!(ring->netdev->features & NETIF_F_RXHASH))
		return;

	rss_type = le16_to_cpu(rx_desc->w.pkt_info) & FM10K_RXD_RSSTYPE_MASK;
	if (!rss_type)
		return;

	skb_set_hash(skb, le32_to_cpu(rx_desc->d.rss),
		     (FM10K_RSS_L4_TYPES_MASK & (1ul << rss_type)) ?
		     PKT_HASH_TYPE_L4 : PKT_HASH_TYPE_L3);
}

/**
 * fm10k_process_skb_fields - Populate skb header fields from Rx descriptor
 * @rx_ring: rx descriptor ring packet is being transacted on
 * @rx_desc: pointer to the EOP Rx descriptor
 * @skb: pointer to current skb being populated
 *
 * This function checks the ring, descriptor, and packet information in
 * order to populate the hash, checksum, VLAN, timestamp, protocol, and
 * other fields within the skb.
 **/
static unsigned int fm10k_process_skb_fields(struct fm10k_ring *rx_ring,
					     union fm10k_rx_desc *rx_desc,
					     struct sk_buff *skb)
{
	unsigned int len = skb->len;

	fm10k_rx_hash(rx_ring, rx_desc, skb);

	fm10k_rx_checksum(rx_ring, rx_desc, skb);

	FM10K_CB(skb)->fi.w.vlan = rx_desc->w.vlan;

	skb_record_rx_queue(skb, rx_ring->queue_index);

	FM10K_CB(skb)->fi.d.glort = rx_desc->d.glort;

	if (rx_desc->w.vlan) {
		u16 vid = le16_to_cpu(rx_desc->w.vlan);

		if (vid != rx_ring->vid)
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);
	}

	skb->protocol = eth_type_trans(skb, rx_ring->netdev);

	return len;
}

/**
 * fm10k_is_non_eop - process handling of non-EOP buffers
 * @rx_ring: Rx ring being processed
 * @rx_desc: Rx descriptor for current buffer
 *
 * This function updates next to clean.  If the buffer is an EOP buffer
 * this function exits returning false, otherwise it will place the
 * sk_buff in the next buffer to be chained and return true indicating
 * that this is in fact a non-EOP buffer.
 **/
static bool fm10k_is_non_eop(struct fm10k_ring *rx_ring,
			     union fm10k_rx_desc *rx_desc)
{
	u32 ntc = rx_ring->next_to_clean + 1;

	/* fetch, update, and store next to clean */
	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;

	prefetch(FM10K_RX_DESC(rx_ring, ntc));

	if (likely(fm10k_test_staterr(rx_desc, FM10K_RXD_STATUS_EOP)))
		return false;

	return true;
}

/**
 * fm10k_pull_tail - fm10k specific version of skb_pull_tail
 * @rx_ring: rx descriptor ring packet is being transacted on
 * @rx_desc: pointer to the EOP Rx descriptor
 * @skb: pointer to current skb being adjusted
 *
 * This function is an fm10k specific version of __pskb_pull_tail.  The
 * main difference between this version and the original function is that
 * this function can make several assumptions about the state of things
 * that allow for significant optimizations versus the standard function.
 * As a result we can do things like drop a frag and maintain an accurate
 * truesize for the skb.
 */
static void fm10k_pull_tail(struct fm10k_ring *rx_ring,
			    union fm10k_rx_desc *rx_desc,
			    struct sk_buff *skb)
{
	struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[0];
	unsigned char *va;
	unsigned int pull_len;

	/* it is valid to use page_address instead of kmap since we are
	 * working with pages allocated out of the lomem pool per
	 * alloc_page(GFP_ATOMIC)
	 */
	va = skb_frag_address(frag);

	/* we need the header to contain the greater of either ETH_HLEN or
	 * 60 bytes if the skb->len is less than 60 for skb_pad.
	 */
	pull_len = eth_get_headlen(va, FM10K_RX_HDR_LEN);

	/* align pull length to size of long to optimize memcpy performance */
	skb_copy_to_linear_data(skb, va, ALIGN(pull_len, sizeof(long)));

	/* update all of the pointers */
	skb_frag_size_sub(frag, pull_len);
	frag->page_offset += pull_len;
	skb->data_len -= pull_len;
	skb->tail += pull_len;
}

/**
 * fm10k_cleanup_headers - Correct corrupted or empty headers
 * @rx_ring: rx descriptor ring packet is being transacted on
 * @rx_desc: pointer to the EOP Rx descriptor
 * @skb: pointer to current skb being fixed
 *
 * Address the case where we are pulling data in on pages only
 * and as such no data is present in the skb header.
 *
 * In addition if skb is not at least 60 bytes we need to pad it so that
 * it is large enough to qualify as a valid Ethernet frame.
 *
 * Returns true if an error was encountered and skb was freed.
 **/
static bool fm10k_cleanup_headers(struct fm10k_ring *rx_ring,
				  union fm10k_rx_desc *rx_desc,
				  struct sk_buff *skb)
{
	if (unlikely((fm10k_test_staterr(rx_desc,
					 FM10K_RXD_STATUS_RXE)))) {
		dev_kfree_skb_any(skb);
		rx_ring->rx_stats.errors++;
		return true;
	}

	/* place header in linear portion of buffer */
	if (skb_is_nonlinear(skb))
		fm10k_pull_tail(rx_ring, rx_desc, skb);

	/* if skb_pad returns an error the skb was freed */
	if (unlikely(skb->len < 60)) {
		int pad_len = 60 - skb->len;

		if (skb_pad(skb, pad_len))
			return true;
		__skb_put(skb, pad_len);
	}

	return false;
}

/**
 * fm10k_receive_skb - helper function to handle rx indications
 * @q_vector: structure containing interrupt and ring information
 * @skb: packet to send up
 **/
static void fm10k_receive_skb(struct fm10k_q_vector *q_vector,
			      struct sk_buff *skb)
{
	napi_gro_receive(&q_vector->napi, skb);
}

static bool fm10k_clean_rx_irq(struct fm10k_q_vector *q_vector,
			       struct fm10k_ring *rx_ring,
			       int budget)
{
	struct sk_buff *skb = rx_ring->skb;
	unsigned int total_bytes = 0, total_packets = 0;
	u16 cleaned_count = fm10k_desc_unused(rx_ring);

	do {
		union fm10k_rx_desc *rx_desc;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= FM10K_RX_BUFFER_WRITE) {
			fm10k_alloc_rx_buffers(rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		rx_desc = FM10K_RX_DESC(rx_ring, rx_ring->next_to_clean);

		if (!fm10k_test_staterr(rx_desc, FM10K_RXD_STATUS_DD))
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we know the
		 * RXD_STATUS_DD bit is set
		 */
		rmb();

		/* retrieve a buffer from the ring */
		skb = fm10k_fetch_rx_buffer(rx_ring, rx_desc, skb);

		/* exit if we failed to retrieve a buffer */
		if (!skb)
			break;

		cleaned_count++;

		/* fetch next buffer in frame if non-eop */
		if (fm10k_is_non_eop(rx_ring, rx_desc))
			continue;

		/* verify the packet layout is correct */
		if (fm10k_cleanup_headers(rx_ring, rx_desc, skb)) {
			skb = NULL;
			continue;
		}

		/* populate checksum, timestamp, VLAN, and protocol */
		total_bytes += fm10k_process_skb_fields(rx_ring, rx_desc, skb);

		fm10k_receive_skb(q_vector, skb);

		/* reset skb pointer */
		skb = NULL;

		/* update budget accounting */
		total_packets++;
	} while (likely(total_packets < budget));

	/* place incomplete frames back on ring for completion */
	rx_ring->skb = skb;

	u64_stats_update_begin(&rx_ring->syncp);
	rx_ring->stats.packets += total_packets;
	rx_ring->stats.bytes += total_bytes;
	u64_stats_update_end(&rx_ring->syncp);
	q_vector->rx.total_packets += total_packets;
	q_vector->rx.total_bytes += total_bytes;

	return total_packets < budget;
}

#define VXLAN_HLEN (sizeof(struct udphdr) + 8)
static struct ethhdr *fm10k_port_is_vxlan(struct sk_buff *skb)
{
	struct fm10k_intfc *interface = netdev_priv(skb->dev);
	struct fm10k_vxlan_port *vxlan_port;

	/* we can only offload a vxlan if we recognize it as such */
	vxlan_port = list_first_entry_or_null(&interface->vxlan_port,
					      struct fm10k_vxlan_port, list);

	if (!vxlan_port)
		return NULL;
	if (vxlan_port->port != udp_hdr(skb)->dest)
		return NULL;

	/* return offset of udp_hdr plus 8 bytes for VXLAN header */
	return (struct ethhdr *)(skb_transport_header(skb) + VXLAN_HLEN);
}

#define FM10K_NVGRE_RESERVED0_FLAGS htons(0x9FFF)
#define NVGRE_TNI htons(0x2000)
struct fm10k_nvgre_hdr {
	__be16 flags;
	__be16 proto;
	__be32 tni;
};

static struct ethhdr *fm10k_gre_is_nvgre(struct sk_buff *skb)
{
	struct fm10k_nvgre_hdr *nvgre_hdr;
	int hlen = ip_hdrlen(skb);

	/* currently only IPv4 is supported due to hlen above */
	if (vlan_get_protocol(skb) != htons(ETH_P_IP))
		return NULL;

	/* our transport header should be NVGRE */
	nvgre_hdr = (struct fm10k_nvgre_hdr *)(skb_network_header(skb) + hlen);

	/* verify all reserved flags are 0 */
	if (nvgre_hdr->flags & FM10K_NVGRE_RESERVED0_FLAGS)
		return NULL;

	/* verify protocol is transparent Ethernet bridging */
	if (nvgre_hdr->proto != htons(ETH_P_TEB))
		return NULL;

	/* report start of ethernet header */
	if (nvgre_hdr->flags & NVGRE_TNI)
		return (struct ethhdr *)(nvgre_hdr + 1);

	return (struct ethhdr *)(&nvgre_hdr->tni);
}

static __be16 fm10k_tx_encap_offload(struct sk_buff *skb)
{
	struct ethhdr *eth_hdr;
	u8 l4_hdr = 0;

	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		l4_hdr = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		l4_hdr = ipv6_hdr(skb)->nexthdr;
		break;
	default:
		return 0;
	}

	switch (l4_hdr) {
	case IPPROTO_UDP:
		eth_hdr = fm10k_port_is_vxlan(skb);
		break;
	case IPPROTO_GRE:
		eth_hdr = fm10k_gre_is_nvgre(skb);
		break;
	default:
		return 0;
	}

	if (!eth_hdr)
		return 0;

	switch (eth_hdr->h_proto) {
	case htons(ETH_P_IP):
	case htons(ETH_P_IPV6):
		break;
	default:
		return 0;
	}

	return eth_hdr->h_proto;
}

static int fm10k_tso(struct fm10k_ring *tx_ring,
		     struct fm10k_tx_buffer *first)
{
	struct sk_buff *skb = first->skb;
	struct fm10k_tx_desc *tx_desc;
	unsigned char *th;
	u8 hdrlen;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (!skb_is_gso(skb))
		return 0;

	/* compute header lengths */
	if (skb->encapsulation) {
		if (!fm10k_tx_encap_offload(skb))
			goto err_vxlan;
		th = skb_inner_transport_header(skb);
	} else {
		th = skb_transport_header(skb);
	}

	/* compute offset from SOF to transport header and add header len */
	hdrlen = (th - skb->data) + (((struct tcphdr *)th)->doff << 2);

	first->tx_flags |= FM10K_TX_FLAGS_CSUM;

	/* update gso size and bytecount with header size */
	first->gso_segs = skb_shinfo(skb)->gso_segs;
	first->bytecount += (first->gso_segs - 1) * hdrlen;

	/* populate Tx descriptor header size and mss */
	tx_desc = FM10K_TX_DESC(tx_ring, tx_ring->next_to_use);
	tx_desc->hdrlen = hdrlen;
	tx_desc->mss = cpu_to_le16(skb_shinfo(skb)->gso_size);

	return 1;
err_vxlan:
	tx_ring->netdev->features &= ~NETIF_F_GSO_UDP_TUNNEL;
	if (!net_ratelimit())
		netdev_err(tx_ring->netdev,
			   "TSO requested for unsupported tunnel, disabling offload\n");
	return -1;
}

static void fm10k_tx_csum(struct fm10k_ring *tx_ring,
			  struct fm10k_tx_buffer *first)
{
	struct sk_buff *skb = first->skb;
	struct fm10k_tx_desc *tx_desc;
	union {
		struct iphdr *ipv4;
		struct ipv6hdr *ipv6;
		u8 *raw;
	} network_hdr;
	__be16 protocol;
	u8 l4_hdr = 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		goto no_csum;

	if (skb->encapsulation) {
		protocol = fm10k_tx_encap_offload(skb);
		if (!protocol) {
			if (skb_checksum_help(skb)) {
				dev_warn(tx_ring->dev,
					 "failed to offload encap csum!\n");
				tx_ring->tx_stats.csum_err++;
			}
			goto no_csum;
		}
		network_hdr.raw = skb_inner_network_header(skb);
	} else {
		protocol = vlan_get_protocol(skb);
		network_hdr.raw = skb_network_header(skb);
	}

	switch (protocol) {
	case htons(ETH_P_IP):
		l4_hdr = network_hdr.ipv4->protocol;
		break;
	case htons(ETH_P_IPV6):
		l4_hdr = network_hdr.ipv6->nexthdr;
		break;
	default:
		if (unlikely(net_ratelimit())) {
			dev_warn(tx_ring->dev,
				 "partial checksum but ip version=%x!\n",
				 protocol);
		}
		tx_ring->tx_stats.csum_err++;
		goto no_csum;
	}

	switch (l4_hdr) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		break;
	case IPPROTO_GRE:
		if (skb->encapsulation)
			break;
	default:
		if (unlikely(net_ratelimit())) {
			dev_warn(tx_ring->dev,
				 "partial checksum but l4 proto=%x!\n",
				 l4_hdr);
		}
		tx_ring->tx_stats.csum_err++;
		goto no_csum;
	}

	/* update TX checksum flag */
	first->tx_flags |= FM10K_TX_FLAGS_CSUM;

no_csum:
	/* populate Tx descriptor header size and mss */
	tx_desc = FM10K_TX_DESC(tx_ring, tx_ring->next_to_use);
	tx_desc->hdrlen = 0;
	tx_desc->mss = 0;
}

#define FM10K_SET_FLAG(_input, _flag, _result) \
	((_flag <= _result) ? \
	 ((u32)(_input & _flag) * (_result / _flag)) : \
	 ((u32)(_input & _flag) / (_flag / _result)))

static u8 fm10k_tx_desc_flags(struct sk_buff *skb, u32 tx_flags)
{
	/* set type for advanced descriptor with frame checksum insertion */
	u32 desc_flags = 0;

	/* set checksum offload bits */
	desc_flags |= FM10K_SET_FLAG(tx_flags, FM10K_TX_FLAGS_CSUM,
				     FM10K_TXD_FLAG_CSUM);

	return desc_flags;
}

static bool fm10k_tx_desc_push(struct fm10k_ring *tx_ring,
			       struct fm10k_tx_desc *tx_desc, u16 i,
			       dma_addr_t dma, unsigned int size, u8 desc_flags)
{
	/* set RS and INT for last frame in a cache line */
	if ((++i & (FM10K_TXD_WB_FIFO_SIZE - 1)) == 0)
		desc_flags |= FM10K_TXD_FLAG_RS | FM10K_TXD_FLAG_INT;

	/* record values to descriptor */
	tx_desc->buffer_addr = cpu_to_le64(dma);
	tx_desc->flags = desc_flags;
	tx_desc->buflen = cpu_to_le16(size);

	/* return true if we just wrapped the ring */
	return i == tx_ring->count;
}

static void fm10k_tx_map(struct fm10k_ring *tx_ring,
			 struct fm10k_tx_buffer *first)
{
	struct sk_buff *skb = first->skb;
	struct fm10k_tx_buffer *tx_buffer;
	struct fm10k_tx_desc *tx_desc;
	struct skb_frag_struct *frag;
	unsigned char *data;
	dma_addr_t dma;
	unsigned int data_len, size;
	u32 tx_flags = first->tx_flags;
	u16 i = tx_ring->next_to_use;
	u8 flags = fm10k_tx_desc_flags(skb, tx_flags);

	tx_desc = FM10K_TX_DESC(tx_ring, i);

	/* add HW VLAN tag */
	if (vlan_tx_tag_present(skb))
		tx_desc->vlan = cpu_to_le16(vlan_tx_tag_get(skb));
	else
		tx_desc->vlan = 0;

	size = skb_headlen(skb);
	data = skb->data;

	dma = dma_map_single(tx_ring->dev, data, size, DMA_TO_DEVICE);

	data_len = skb->data_len;
	tx_buffer = first;

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		if (dma_mapping_error(tx_ring->dev, dma))
			goto dma_error;

		/* record length, and DMA address */
		dma_unmap_len_set(tx_buffer, len, size);
		dma_unmap_addr_set(tx_buffer, dma, dma);

		while (unlikely(size > FM10K_MAX_DATA_PER_TXD)) {
			if (fm10k_tx_desc_push(tx_ring, tx_desc++, i++, dma,
					       FM10K_MAX_DATA_PER_TXD, flags)) {
				tx_desc = FM10K_TX_DESC(tx_ring, 0);
				i = 0;
			}

			dma += FM10K_MAX_DATA_PER_TXD;
			size -= FM10K_MAX_DATA_PER_TXD;
		}

		if (likely(!data_len))
			break;

		if (fm10k_tx_desc_push(tx_ring, tx_desc++, i++,
				       dma, size, flags)) {
			tx_desc = FM10K_TX_DESC(tx_ring, 0);
			i = 0;
		}

		size = skb_frag_size(frag);
		data_len -= size;

		dma = skb_frag_dma_map(tx_ring->dev, frag, 0, size,
				       DMA_TO_DEVICE);

		tx_buffer = &tx_ring->tx_buffer[i];
	}

	/* write last descriptor with LAST bit set */
	flags |= FM10K_TXD_FLAG_LAST;

	if (fm10k_tx_desc_push(tx_ring, tx_desc, i++, dma, size, flags))
		i = 0;

	/* record bytecount for BQL */
	netdev_tx_sent_queue(txring_txq(tx_ring), first->bytecount);

	/* record SW timestamp if HW timestamp is not available */
	skb_tx_timestamp(first->skb);

	/* Force memory writes to complete before letting h/w know there
	 * are new descriptors to fetch.  (Only applicable for weak-ordered
	 * memory model archs, such as IA-64).
	 *
	 * We also need this memory barrier to make certain all of the
	 * status bits have been updated before next_to_watch is written.
	 */
	wmb();

	/* set next_to_watch value indicating a packet is present */
	first->next_to_watch = tx_desc;

	tx_ring->next_to_use = i;

	/* notify HW of packet */
	writel(i, tx_ring->tail);

	/* we need this if more than one processor can write to our tail
	 * at a time, it synchronizes IO on IA64/Altix systems
	 */
	mmiowb();

	return;
dma_error:
	dev_err(tx_ring->dev, "TX DMA map failed\n");

	/* clear dma mappings for failed tx_buffer map */
	for (;;) {
		tx_buffer = &tx_ring->tx_buffer[i];
		fm10k_unmap_and_free_tx_resource(tx_ring, tx_buffer);
		if (tx_buffer == first)
			break;
		if (i == 0)
			i = tx_ring->count;
		i--;
	}

	tx_ring->next_to_use = i;
}

static int __fm10k_maybe_stop_tx(struct fm10k_ring *tx_ring, u16 size)
{
	netif_stop_subqueue(tx_ring->netdev, tx_ring->queue_index);

	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available. */
	if (likely(fm10k_desc_unused(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! - use start_queue because it doesn't call schedule */
	netif_start_subqueue(tx_ring->netdev, tx_ring->queue_index);
	++tx_ring->tx_stats.restart_queue;
	return 0;
}

static inline int fm10k_maybe_stop_tx(struct fm10k_ring *tx_ring, u16 size)
{
	if (likely(fm10k_desc_unused(tx_ring) >= size))
		return 0;
	return __fm10k_maybe_stop_tx(tx_ring, size);
}

netdev_tx_t fm10k_xmit_frame_ring(struct sk_buff *skb,
				  struct fm10k_ring *tx_ring)
{
	struct fm10k_tx_buffer *first;
	int tso;
	u32 tx_flags = 0;
#if PAGE_SIZE > FM10K_MAX_DATA_PER_TXD
	unsigned short f;
#endif
	u16 count = TXD_USE_COUNT(skb_headlen(skb));

	/* need: 1 descriptor per page * PAGE_SIZE/FM10K_MAX_DATA_PER_TXD,
	 *       + 1 desc for skb_headlen/FM10K_MAX_DATA_PER_TXD,
	 *       + 2 desc gap to keep tail from touching head
	 * otherwise try next time
	 */
#if PAGE_SIZE > FM10K_MAX_DATA_PER_TXD
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
		count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size);
#else
	count += skb_shinfo(skb)->nr_frags;
#endif
	if (fm10k_maybe_stop_tx(tx_ring, count + 3)) {
		tx_ring->tx_stats.tx_busy++;
		return NETDEV_TX_BUSY;
	}

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_buffer[tx_ring->next_to_use];
	first->skb = skb;
	first->bytecount = max_t(unsigned int, skb->len, ETH_ZLEN);
	first->gso_segs = 1;

	/* record initial flags and protocol */
	first->tx_flags = tx_flags;

	tso = fm10k_tso(tx_ring, first);
	if (tso < 0)
		goto out_drop;
	else if (!tso)
		fm10k_tx_csum(tx_ring, first);

	fm10k_tx_map(tx_ring, first);

	fm10k_maybe_stop_tx(tx_ring, DESC_NEEDED);

	return NETDEV_TX_OK;

out_drop:
	dev_kfree_skb_any(first->skb);
	first->skb = NULL;

	return NETDEV_TX_OK;
}

static u64 fm10k_get_tx_completed(struct fm10k_ring *ring)
{
	return ring->stats.packets;
}

static u64 fm10k_get_tx_pending(struct fm10k_ring *ring)
{
	/* use SW head and tail until we have real hardware */
	u32 head = ring->next_to_clean;
	u32 tail = ring->next_to_use;

	return ((head <= tail) ? tail : tail + ring->count) - head;
}

bool fm10k_check_tx_hang(struct fm10k_ring *tx_ring)
{
	u32 tx_done = fm10k_get_tx_completed(tx_ring);
	u32 tx_done_old = tx_ring->tx_stats.tx_done_old;
	u32 tx_pending = fm10k_get_tx_pending(tx_ring);

	clear_check_for_tx_hang(tx_ring);

	/* Check for a hung queue, but be thorough. This verifies
	 * that a transmit has been completed since the previous
	 * check AND there is at least one packet pending. By
	 * requiring this to fail twice we avoid races with
	 * clearing the ARMED bit and conditions where we
	 * run the check_tx_hang logic with a transmit completion
	 * pending but without time to complete it yet.
	 */
	if (!tx_pending || (tx_done_old != tx_done)) {
		/* update completed stats and continue */
		tx_ring->tx_stats.tx_done_old = tx_done;
		/* reset the countdown */
		clear_bit(__FM10K_HANG_CHECK_ARMED, &tx_ring->state);

		return false;
	}

	/* make sure it is true for two checks in a row */
	return test_and_set_bit(__FM10K_HANG_CHECK_ARMED, &tx_ring->state);
}

/**
 * fm10k_tx_timeout_reset - initiate reset due to Tx timeout
 * @interface: driver private struct
 **/
void fm10k_tx_timeout_reset(struct fm10k_intfc *interface)
{
	/* Do the reset outside of interrupt context */
	if (!test_bit(__FM10K_DOWN, &interface->state)) {
		netdev_err(interface->netdev, "Reset interface\n");
		interface->tx_timeout_count++;
		interface->flags |= FM10K_FLAG_RESET_REQUESTED;
		fm10k_service_event_schedule(interface);
	}
}

/**
 * fm10k_clean_tx_irq - Reclaim resources after transmit completes
 * @q_vector: structure containing interrupt and ring information
 * @tx_ring: tx ring to clean
 **/
static bool fm10k_clean_tx_irq(struct fm10k_q_vector *q_vector,
			       struct fm10k_ring *tx_ring)
{
	struct fm10k_intfc *interface = q_vector->interface;
	struct fm10k_tx_buffer *tx_buffer;
	struct fm10k_tx_desc *tx_desc;
	unsigned int total_bytes = 0, total_packets = 0;
	unsigned int budget = q_vector->tx.work_limit;
	unsigned int i = tx_ring->next_to_clean;

	if (test_bit(__FM10K_DOWN, &interface->state))
		return true;

	tx_buffer = &tx_ring->tx_buffer[i];
	tx_desc = FM10K_TX_DESC(tx_ring, i);
	i -= tx_ring->count;

	do {
		struct fm10k_tx_desc *eop_desc = tx_buffer->next_to_watch;

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		/* prevent any other reads prior to eop_desc */
		read_barrier_depends();

		/* if DD is not set pending work has not been completed */
		if (!(eop_desc->flags & FM10K_TXD_FLAG_DONE))
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buffer->next_to_watch = NULL;

		/* update the statistics for this packet */
		total_bytes += tx_buffer->bytecount;
		total_packets += tx_buffer->gso_segs;

		/* free the skb */
		dev_consume_skb_any(tx_buffer->skb);

		/* unmap skb header data */
		dma_unmap_single(tx_ring->dev,
				 dma_unmap_addr(tx_buffer, dma),
				 dma_unmap_len(tx_buffer, len),
				 DMA_TO_DEVICE);

		/* clear tx_buffer data */
		tx_buffer->skb = NULL;
		dma_unmap_len_set(tx_buffer, len, 0);

		/* unmap remaining buffers */
		while (tx_desc != eop_desc) {
			tx_buffer++;
			tx_desc++;
			i++;
			if (unlikely(!i)) {
				i -= tx_ring->count;
				tx_buffer = tx_ring->tx_buffer;
				tx_desc = FM10K_TX_DESC(tx_ring, 0);
			}

			/* unmap any remaining paged data */
			if (dma_unmap_len(tx_buffer, len)) {
				dma_unmap_page(tx_ring->dev,
					       dma_unmap_addr(tx_buffer, dma),
					       dma_unmap_len(tx_buffer, len),
					       DMA_TO_DEVICE);
				dma_unmap_len_set(tx_buffer, len, 0);
			}
		}

		/* move us one more past the eop_desc for start of next pkt */
		tx_buffer++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buffer = tx_ring->tx_buffer;
			tx_desc = FM10K_TX_DESC(tx_ring, 0);
		}

		/* issue prefetch for next Tx descriptor */
		prefetch(tx_desc);

		/* update budget accounting */
		budget--;
	} while (likely(budget));

	i += tx_ring->count;
	tx_ring->next_to_clean = i;
	u64_stats_update_begin(&tx_ring->syncp);
	tx_ring->stats.bytes += total_bytes;
	tx_ring->stats.packets += total_packets;
	u64_stats_update_end(&tx_ring->syncp);
	q_vector->tx.total_bytes += total_bytes;
	q_vector->tx.total_packets += total_packets;

	if (check_for_tx_hang(tx_ring) && fm10k_check_tx_hang(tx_ring)) {
		/* schedule immediate reset if we believe we hung */
		struct fm10k_hw *hw = &interface->hw;

		netif_err(interface, drv, tx_ring->netdev,
			  "Detected Tx Unit Hang\n"
			  "  Tx Queue             <%d>\n"
			  "  TDH, TDT             <%x>, <%x>\n"
			  "  next_to_use          <%x>\n"
			  "  next_to_clean        <%x>\n",
			  tx_ring->queue_index,
			  fm10k_read_reg(hw, FM10K_TDH(tx_ring->reg_idx)),
			  fm10k_read_reg(hw, FM10K_TDT(tx_ring->reg_idx)),
			  tx_ring->next_to_use, i);

		netif_stop_subqueue(tx_ring->netdev,
				    tx_ring->queue_index);

		netif_info(interface, probe, tx_ring->netdev,
			   "tx hang %d detected on queue %d, resetting interface\n",
			   interface->tx_timeout_count + 1,
			   tx_ring->queue_index);

		fm10k_tx_timeout_reset(interface);

		/* the netdev is about to reset, no point in enabling stuff */
		return true;
	}

	/* notify netdev of completed buffers */
	netdev_tx_completed_queue(txring_txq(tx_ring),
				  total_packets, total_bytes);

#define TX_WAKE_THRESHOLD min_t(u16, FM10K_MIN_TXD - 1, DESC_NEEDED * 2)
	if (unlikely(total_packets && netif_carrier_ok(tx_ring->netdev) &&
		     (fm10k_desc_unused(tx_ring) >= TX_WAKE_THRESHOLD))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (__netif_subqueue_stopped(tx_ring->netdev,
					     tx_ring->queue_index) &&
		    !test_bit(__FM10K_DOWN, &interface->state)) {
			netif_wake_subqueue(tx_ring->netdev,
					    tx_ring->queue_index);
			++tx_ring->tx_stats.restart_queue;
		}
	}

	return !!budget;
}

/**
 * fm10k_update_itr - update the dynamic ITR value based on packet size
 *
 *      Stores a new ITR value based on strictly on packet size.  The
 *      divisors and thresholds used by this function were determined based
 *      on theoretical maximum wire speed and testing data, in order to
 *      minimize response time while increasing bulk throughput.
 *
 * @ring_container: Container for rings to have ITR updated
 **/
static void fm10k_update_itr(struct fm10k_ring_container *ring_container)
{
	unsigned int avg_wire_size, packets;

	/* Only update ITR if we are using adaptive setting */
	if (!(ring_container->itr & FM10K_ITR_ADAPTIVE))
		goto clear_counts;

	packets = ring_container->total_packets;
	if (!packets)
		goto clear_counts;

	avg_wire_size = ring_container->total_bytes / packets;

	/* Add 24 bytes to size to account for CRC, preamble, and gap */
	avg_wire_size += 24;

	/* Don't starve jumbo frames */
	if (avg_wire_size > 3000)
		avg_wire_size = 3000;

	/* Give a little boost to mid-size frames */
	if ((avg_wire_size > 300) && (avg_wire_size < 1200))
		avg_wire_size /= 3;
	else
		avg_wire_size /= 2;

	/* write back value and retain adaptive flag */
	ring_container->itr = avg_wire_size | FM10K_ITR_ADAPTIVE;

clear_counts:
	ring_container->total_bytes = 0;
	ring_container->total_packets = 0;
}

static void fm10k_qv_enable(struct fm10k_q_vector *q_vector)
{
	/* Enable auto-mask and clear the current mask */
	u32 itr = FM10K_ITR_ENABLE;

	/* Update Tx ITR */
	fm10k_update_itr(&q_vector->tx);

	/* Update Rx ITR */
	fm10k_update_itr(&q_vector->rx);

	/* Store Tx itr in timer slot 0 */
	itr |= (q_vector->tx.itr & FM10K_ITR_MAX);

	/* Shift Rx itr to timer slot 1 */
	itr |= (q_vector->rx.itr & FM10K_ITR_MAX) << FM10K_ITR_INTERVAL1_SHIFT;

	/* Write the final value to the ITR register */
	writel(itr, q_vector->itr);
}

static int fm10k_poll(struct napi_struct *napi, int budget)
{
	struct fm10k_q_vector *q_vector =
			       container_of(napi, struct fm10k_q_vector, napi);
	struct fm10k_ring *ring;
	int per_ring_budget;
	bool clean_complete = true;

	fm10k_for_each_ring(ring, q_vector->tx)
		clean_complete &= fm10k_clean_tx_irq(q_vector, ring);

	/* attempt to distribute budget to each queue fairly, but don't
	 * allow the budget to go below 1 because we'll exit polling
	 */
	if (q_vector->rx.count > 1)
		per_ring_budget = max(budget/q_vector->rx.count, 1);
	else
		per_ring_budget = budget;

	fm10k_for_each_ring(ring, q_vector->rx)
		clean_complete &= fm10k_clean_rx_irq(q_vector, ring,
						     per_ring_budget);

	/* If all work not completed, return budget and keep polling */
	if (!clean_complete)
		return budget;

	/* all work done, exit the polling mode */
	napi_complete(napi);

	/* re-enable the q_vector */
	fm10k_qv_enable(q_vector);

	return 0;
}

/**
 * fm10k_set_qos_queues: Allocate queues for a QOS-enabled device
 * @interface: board private structure to initialize
 *
 * When QoS (Quality of Service) is enabled, allocate queues for
 * each traffic class.  If multiqueue isn't available,then abort QoS
 * initialization.
 *
 * This function handles all combinations of Qos and RSS.
 *
 **/
static bool fm10k_set_qos_queues(struct fm10k_intfc *interface)
{
	struct net_device *dev = interface->netdev;
	struct fm10k_ring_feature *f;
	int rss_i, i;
	int pcs;

	/* Map queue offset and counts onto allocated tx queues */
	pcs = netdev_get_num_tc(dev);

	if (pcs <= 1)
		return false;

	/* set QoS mask and indices */
	f = &interface->ring_feature[RING_F_QOS];
	f->indices = pcs;
	f->mask = (1 << fls(pcs - 1)) - 1;

	/* determine the upper limit for our current DCB mode */
	rss_i = interface->hw.mac.max_queues / pcs;
	rss_i = 1 << (fls(rss_i) - 1);

	/* set RSS mask and indices */
	f = &interface->ring_feature[RING_F_RSS];
	rss_i = min_t(u16, rss_i, f->limit);
	f->indices = rss_i;
	f->mask = (1 << fls(rss_i - 1)) - 1;

	/* configure pause class to queue mapping */
	for (i = 0; i < pcs; i++)
		netdev_set_tc_queue(dev, i, rss_i, rss_i * i);

	interface->num_rx_queues = rss_i * pcs;
	interface->num_tx_queues = rss_i * pcs;

	return true;
}

/**
 * fm10k_set_rss_queues: Allocate queues for RSS
 * @interface: board private structure to initialize
 *
 * This is our "base" multiqueue mode.  RSS (Receive Side Scaling) will try
 * to allocate one Rx queue per CPU, and if available, one Tx queue per CPU.
 *
 **/
static bool fm10k_set_rss_queues(struct fm10k_intfc *interface)
{
	struct fm10k_ring_feature *f;
	u16 rss_i;

	f = &interface->ring_feature[RING_F_RSS];
	rss_i = min_t(u16, interface->hw.mac.max_queues, f->limit);

	/* record indices and power of 2 mask for RSS */
	f->indices = rss_i;
	f->mask = (1 << fls(rss_i - 1)) - 1;

	interface->num_rx_queues = rss_i;
	interface->num_tx_queues = rss_i;

	return true;
}

/**
 * fm10k_set_num_queues: Allocate queues for device, feature dependent
 * @interface: board private structure to initialize
 *
 * This is the top level queue allocation routine.  The order here is very
 * important, starting with the "most" number of features turned on at once,
 * and ending with the smallest set of features.  This way large combinations
 * can be allocated if they're turned on, and smaller combinations are the
 * fallthrough conditions.
 *
 **/
static void fm10k_set_num_queues(struct fm10k_intfc *interface)
{
	/* Start with base case */
	interface->num_rx_queues = 1;
	interface->num_tx_queues = 1;

	if (fm10k_set_qos_queues(interface))
		return;

	fm10k_set_rss_queues(interface);
}

/**
 * fm10k_alloc_q_vector - Allocate memory for a single interrupt vector
 * @interface: board private structure to initialize
 * @v_count: q_vectors allocated on interface, used for ring interleaving
 * @v_idx: index of vector in interface struct
 * @txr_count: total number of Tx rings to allocate
 * @txr_idx: index of first Tx ring to allocate
 * @rxr_count: total number of Rx rings to allocate
 * @rxr_idx: index of first Rx ring to allocate
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 **/
static int fm10k_alloc_q_vector(struct fm10k_intfc *interface,
				unsigned int v_count, unsigned int v_idx,
				unsigned int txr_count, unsigned int txr_idx,
				unsigned int rxr_count, unsigned int rxr_idx)
{
	struct fm10k_q_vector *q_vector;
	struct fm10k_ring *ring;
	int ring_count, size;

	ring_count = txr_count + rxr_count;
	size = sizeof(struct fm10k_q_vector) +
	       (sizeof(struct fm10k_ring) * ring_count);

	/* allocate q_vector and rings */
	q_vector = kzalloc(size, GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	/* initialize NAPI */
	netif_napi_add(interface->netdev, &q_vector->napi,
		       fm10k_poll, NAPI_POLL_WEIGHT);

	/* tie q_vector and interface together */
	interface->q_vector[v_idx] = q_vector;
	q_vector->interface = interface;
	q_vector->v_idx = v_idx;

	/* initialize pointer to rings */
	ring = q_vector->ring;

	/* save Tx ring container info */
	q_vector->tx.ring = ring;
	q_vector->tx.work_limit = FM10K_DEFAULT_TX_WORK;
	q_vector->tx.itr = interface->tx_itr;
	q_vector->tx.count = txr_count;

	while (txr_count) {
		/* assign generic ring traits */
		ring->dev = &interface->pdev->dev;
		ring->netdev = interface->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* apply Tx specific ring traits */
		ring->count = interface->tx_ring_count;
		ring->queue_index = txr_idx;

		/* assign ring to interface */
		interface->tx_ring[txr_idx] = ring;

		/* update count and index */
		txr_count--;
		txr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	/* save Rx ring container info */
	q_vector->rx.ring = ring;
	q_vector->rx.itr = interface->rx_itr;
	q_vector->rx.count = rxr_count;

	while (rxr_count) {
		/* assign generic ring traits */
		ring->dev = &interface->pdev->dev;
		ring->netdev = interface->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* apply Rx specific ring traits */
		ring->count = interface->rx_ring_count;
		ring->queue_index = rxr_idx;

		/* assign ring to interface */
		interface->rx_ring[rxr_idx] = ring;

		/* update count and index */
		rxr_count--;
		rxr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	return 0;
}

/**
 * fm10k_free_q_vector - Free memory allocated for specific interrupt vector
 * @interface: board private structure to initialize
 * @v_idx: Index of vector to be freed
 *
 * This function frees the memory allocated to the q_vector.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void fm10k_free_q_vector(struct fm10k_intfc *interface, int v_idx)
{
	struct fm10k_q_vector *q_vector = interface->q_vector[v_idx];
	struct fm10k_ring *ring;

	fm10k_for_each_ring(ring, q_vector->tx)
		interface->tx_ring[ring->queue_index] = NULL;

	fm10k_for_each_ring(ring, q_vector->rx)
		interface->rx_ring[ring->queue_index] = NULL;

	interface->q_vector[v_idx] = NULL;
	netif_napi_del(&q_vector->napi);
	kfree_rcu(q_vector, rcu);
}

/**
 * fm10k_alloc_q_vectors - Allocate memory for interrupt vectors
 * @interface: board private structure to initialize
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int fm10k_alloc_q_vectors(struct fm10k_intfc *interface)
{
	unsigned int q_vectors = interface->num_q_vectors;
	unsigned int rxr_remaining = interface->num_rx_queues;
	unsigned int txr_remaining = interface->num_tx_queues;
	unsigned int rxr_idx = 0, txr_idx = 0, v_idx = 0;
	int err;

	if (q_vectors >= (rxr_remaining + txr_remaining)) {
		for (; rxr_remaining; v_idx++) {
			err = fm10k_alloc_q_vector(interface, q_vectors, v_idx,
						   0, 0, 1, rxr_idx);
			if (err)
				goto err_out;

			/* update counts and index */
			rxr_remaining--;
			rxr_idx++;
		}
	}

	for (; v_idx < q_vectors; v_idx++) {
		int rqpv = DIV_ROUND_UP(rxr_remaining, q_vectors - v_idx);
		int tqpv = DIV_ROUND_UP(txr_remaining, q_vectors - v_idx);

		err = fm10k_alloc_q_vector(interface, q_vectors, v_idx,
					   tqpv, txr_idx,
					   rqpv, rxr_idx);

		if (err)
			goto err_out;

		/* update counts and index */
		rxr_remaining -= rqpv;
		txr_remaining -= tqpv;
		rxr_idx++;
		txr_idx++;
	}

	return 0;

err_out:
	interface->num_tx_queues = 0;
	interface->num_rx_queues = 0;
	interface->num_q_vectors = 0;

	while (v_idx--)
		fm10k_free_q_vector(interface, v_idx);

	return -ENOMEM;
}

/**
 * fm10k_free_q_vectors - Free memory allocated for interrupt vectors
 * @interface: board private structure to initialize
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void fm10k_free_q_vectors(struct fm10k_intfc *interface)
{
	int v_idx = interface->num_q_vectors;

	interface->num_tx_queues = 0;
	interface->num_rx_queues = 0;
	interface->num_q_vectors = 0;

	while (v_idx--)
		fm10k_free_q_vector(interface, v_idx);
}

/**
 * f10k_reset_msix_capability - reset MSI-X capability
 * @interface: board private structure to initialize
 *
 * Reset the MSI-X capability back to its starting state
 **/
static void fm10k_reset_msix_capability(struct fm10k_intfc *interface)
{
	pci_disable_msix(interface->pdev);
	kfree(interface->msix_entries);
	interface->msix_entries = NULL;
}

/**
 * f10k_init_msix_capability - configure MSI-X capability
 * @interface: board private structure to initialize
 *
 * Attempt to configure the interrupts using the best available
 * capabilities of the hardware and the kernel.
 **/
static int fm10k_init_msix_capability(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	int v_budget, vector;

	/* It's easy to be greedy for MSI-X vectors, but it really
	 * doesn't do us much good if we have a lot more vectors
	 * than CPU's.  So let's be conservative and only ask for
	 * (roughly) the same number of vectors as there are CPU's.
	 * the default is to use pairs of vectors
	 */
	v_budget = max(interface->num_rx_queues, interface->num_tx_queues);
	v_budget = min_t(u16, v_budget, num_online_cpus());

	/* account for vectors not related to queues */
	v_budget += NON_Q_VECTORS(hw);

	/* At the same time, hardware can only support a maximum of
	 * hw.mac->max_msix_vectors vectors.  With features
	 * such as RSS and VMDq, we can easily surpass the number of Rx and Tx
	 * descriptor queues supported by our device.  Thus, we cap it off in
	 * those rare cases where the cpu count also exceeds our vector limit.
	 */
	v_budget = min_t(int, v_budget, hw->mac.max_msix_vectors);

	/* A failure in MSI-X entry allocation is fatal. */
	interface->msix_entries = kcalloc(v_budget, sizeof(struct msix_entry),
					  GFP_KERNEL);
	if (!interface->msix_entries)
		return -ENOMEM;

	/* populate entry values */
	for (vector = 0; vector < v_budget; vector++)
		interface->msix_entries[vector].entry = vector;

	/* Attempt to enable MSI-X with requested value */
	v_budget = pci_enable_msix_range(interface->pdev,
					 interface->msix_entries,
					 MIN_MSIX_COUNT(hw),
					 v_budget);
	if (v_budget < 0) {
		kfree(interface->msix_entries);
		interface->msix_entries = NULL;
		return -ENOMEM;
	}

	/* record the number of queues available for q_vectors */
	interface->num_q_vectors = v_budget - NON_Q_VECTORS(hw);

	return 0;
}

/**
 * fm10k_cache_ring_qos - Descriptor ring to register mapping for QoS
 * @interface: Interface structure continaining rings and devices
 *
 * Cache the descriptor ring offsets for Qos
 **/
static bool fm10k_cache_ring_qos(struct fm10k_intfc *interface)
{
	struct net_device *dev = interface->netdev;
	int pc, offset, rss_i, i, q_idx;
	u16 pc_stride = interface->ring_feature[RING_F_QOS].mask + 1;
	u8 num_pcs = netdev_get_num_tc(dev);

	if (num_pcs <= 1)
		return false;

	rss_i = interface->ring_feature[RING_F_RSS].indices;

	for (pc = 0, offset = 0; pc < num_pcs; pc++, offset += rss_i) {
		q_idx = pc;
		for (i = 0; i < rss_i; i++) {
			interface->tx_ring[offset + i]->reg_idx = q_idx;
			interface->tx_ring[offset + i]->qos_pc = pc;
			interface->rx_ring[offset + i]->reg_idx = q_idx;
			interface->rx_ring[offset + i]->qos_pc = pc;
			q_idx += pc_stride;
		}
	}

	return true;
}

/**
 * fm10k_cache_ring_rss - Descriptor ring to register mapping for RSS
 * @interface: Interface structure continaining rings and devices
 *
 * Cache the descriptor ring offsets for RSS
 **/
static void fm10k_cache_ring_rss(struct fm10k_intfc *interface)
{
	int i;

	for (i = 0; i < interface->num_rx_queues; i++)
		interface->rx_ring[i]->reg_idx = i;

	for (i = 0; i < interface->num_tx_queues; i++)
		interface->tx_ring[i]->reg_idx = i;
}

/**
 * fm10k_assign_rings - Map rings to network devices
 * @interface: Interface structure containing rings and devices
 *
 * This function is meant to go though and configure both the network
 * devices so that they contain rings, and configure the rings so that
 * they function with their network devices.
 **/
static void fm10k_assign_rings(struct fm10k_intfc *interface)
{
	if (fm10k_cache_ring_qos(interface))
		return;

	fm10k_cache_ring_rss(interface);
}

static void fm10k_init_reta(struct fm10k_intfc *interface)
{
	u16 i, rss_i = interface->ring_feature[RING_F_RSS].indices;
	u32 reta, base;

	/* If the netdev is initialized we have to maintain table if possible */
	if (interface->netdev->reg_state) {
		for (i = FM10K_RETA_SIZE; i--;) {
			reta = interface->reta[i];
			if ((((reta << 24) >> 24) < rss_i) &&
			    (((reta << 16) >> 24) < rss_i) &&
			    (((reta <<  8) >> 24) < rss_i) &&
			    (((reta)       >> 24) < rss_i))
				continue;
			goto repopulate_reta;
		}

		/* do nothing if all of the elements are in bounds */
		return;
	}

repopulate_reta:
	/* Populate the redirection table 4 entries at a time.  To do this
	 * we are generating the results for n and n+2 and then interleaving
	 * those with the results with n+1 and n+3.
	 */
	for (i = FM10K_RETA_SIZE; i--;) {
		/* first pass generates n and n+2 */
		base = ((i * 0x00040004) + 0x00020000) * rss_i;
		reta = (base & 0x3F803F80) >> 7;

		/* second pass generates n+1 and n+3 */
		base += 0x00010001 * rss_i;
		reta |= (base & 0x3F803F80) << 1;

		interface->reta[i] = reta;
	}
}

/**
 * fm10k_init_queueing_scheme - Determine proper queueing scheme
 * @interface: board private structure to initialize
 *
 * We determine which queueing scheme to use based on...
 * - Hardware queue count (num_*_queues)
 *   - defined by miscellaneous hardware support/features (RSS, etc.)
 **/
int fm10k_init_queueing_scheme(struct fm10k_intfc *interface)
{
	int err;

	/* Number of supported queues */
	fm10k_set_num_queues(interface);

	/* Configure MSI-X capability */
	err = fm10k_init_msix_capability(interface);
	if (err) {
		dev_err(&interface->pdev->dev,
			"Unable to initialize MSI-X capability\n");
		return err;
	}

	/* Allocate memory for queues */
	err = fm10k_alloc_q_vectors(interface);
	if (err)
		return err;

	/* Map rings to devices, and map devices to physical queues */
	fm10k_assign_rings(interface);

	/* Initialize RSS redirection table */
	fm10k_init_reta(interface);

	return 0;
}

/**
 * fm10k_clear_queueing_scheme - Clear the current queueing scheme settings
 * @interface: board private structure to clear queueing scheme on
 *
 * We go through and clear queueing specific resources and reset the structure
 * to pre-load conditions
 **/
void fm10k_clear_queueing_scheme(struct fm10k_intfc *interface)
{
	fm10k_free_q_vectors(interface);
	fm10k_reset_msix_capability(interface);
}
