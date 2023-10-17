// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <net/ip6_checksum.h>
#include <net/page_pool/helpers.h>
#include <net/inet_ecn.h>
#include <linux/iopoll.h>
#include <linux/sctp.h>
#include <linux/pci.h>
#include <net/tcp.h>
#include <net/ip.h>

#include "wx_type.h"
#include "wx_lib.h"
#include "wx_hw.h"

/* Lookup table mapping the HW PTYPE to the bit field for decoding */
static struct wx_dec_ptype wx_ptype_lookup[256] = {
	/* L2: mac */
	[0x11] = WX_PTT(L2, NONE, NONE, NONE, NONE, PAY2),
	[0x12] = WX_PTT(L2, NONE, NONE, NONE, TS,   PAY2),
	[0x13] = WX_PTT(L2, NONE, NONE, NONE, NONE, PAY2),
	[0x14] = WX_PTT(L2, NONE, NONE, NONE, NONE, PAY2),
	[0x15] = WX_PTT(L2, NONE, NONE, NONE, NONE, NONE),
	[0x16] = WX_PTT(L2, NONE, NONE, NONE, NONE, PAY2),
	[0x17] = WX_PTT(L2, NONE, NONE, NONE, NONE, NONE),

	/* L2: ethertype filter */
	[0x18 ... 0x1F] = WX_PTT(L2, NONE, NONE, NONE, NONE, NONE),

	/* L3: ip non-tunnel */
	[0x21] = WX_PTT(IP, FGV4, NONE, NONE, NONE, PAY3),
	[0x22] = WX_PTT(IP, IPV4, NONE, NONE, NONE, PAY3),
	[0x23] = WX_PTT(IP, IPV4, NONE, NONE, UDP,  PAY4),
	[0x24] = WX_PTT(IP, IPV4, NONE, NONE, TCP,  PAY4),
	[0x25] = WX_PTT(IP, IPV4, NONE, NONE, SCTP, PAY4),
	[0x29] = WX_PTT(IP, FGV6, NONE, NONE, NONE, PAY3),
	[0x2A] = WX_PTT(IP, IPV6, NONE, NONE, NONE, PAY3),
	[0x2B] = WX_PTT(IP, IPV6, NONE, NONE, UDP,  PAY3),
	[0x2C] = WX_PTT(IP, IPV6, NONE, NONE, TCP,  PAY4),
	[0x2D] = WX_PTT(IP, IPV6, NONE, NONE, SCTP, PAY4),

	/* L2: fcoe */
	[0x30 ... 0x34] = WX_PTT(FCOE, NONE, NONE, NONE, NONE, PAY3),
	[0x38 ... 0x3C] = WX_PTT(FCOE, NONE, NONE, NONE, NONE, PAY3),

	/* IPv4 --> IPv4/IPv6 */
	[0x81] = WX_PTT(IP, IPV4, IPIP, FGV4, NONE, PAY3),
	[0x82] = WX_PTT(IP, IPV4, IPIP, IPV4, NONE, PAY3),
	[0x83] = WX_PTT(IP, IPV4, IPIP, IPV4, UDP,  PAY4),
	[0x84] = WX_PTT(IP, IPV4, IPIP, IPV4, TCP,  PAY4),
	[0x85] = WX_PTT(IP, IPV4, IPIP, IPV4, SCTP, PAY4),
	[0x89] = WX_PTT(IP, IPV4, IPIP, FGV6, NONE, PAY3),
	[0x8A] = WX_PTT(IP, IPV4, IPIP, IPV6, NONE, PAY3),
	[0x8B] = WX_PTT(IP, IPV4, IPIP, IPV6, UDP,  PAY4),
	[0x8C] = WX_PTT(IP, IPV4, IPIP, IPV6, TCP,  PAY4),
	[0x8D] = WX_PTT(IP, IPV4, IPIP, IPV6, SCTP, PAY4),

	/* IPv4 --> GRE/NAT --> NONE/IPv4/IPv6 */
	[0x90] = WX_PTT(IP, IPV4, IG, NONE, NONE, PAY3),
	[0x91] = WX_PTT(IP, IPV4, IG, FGV4, NONE, PAY3),
	[0x92] = WX_PTT(IP, IPV4, IG, IPV4, NONE, PAY3),
	[0x93] = WX_PTT(IP, IPV4, IG, IPV4, UDP,  PAY4),
	[0x94] = WX_PTT(IP, IPV4, IG, IPV4, TCP,  PAY4),
	[0x95] = WX_PTT(IP, IPV4, IG, IPV4, SCTP, PAY4),
	[0x99] = WX_PTT(IP, IPV4, IG, FGV6, NONE, PAY3),
	[0x9A] = WX_PTT(IP, IPV4, IG, IPV6, NONE, PAY3),
	[0x9B] = WX_PTT(IP, IPV4, IG, IPV6, UDP,  PAY4),
	[0x9C] = WX_PTT(IP, IPV4, IG, IPV6, TCP,  PAY4),
	[0x9D] = WX_PTT(IP, IPV4, IG, IPV6, SCTP, PAY4),

	/* IPv4 --> GRE/NAT --> MAC --> NONE/IPv4/IPv6 */
	[0xA0] = WX_PTT(IP, IPV4, IGM, NONE, NONE, PAY3),
	[0xA1] = WX_PTT(IP, IPV4, IGM, FGV4, NONE, PAY3),
	[0xA2] = WX_PTT(IP, IPV4, IGM, IPV4, NONE, PAY3),
	[0xA3] = WX_PTT(IP, IPV4, IGM, IPV4, UDP,  PAY4),
	[0xA4] = WX_PTT(IP, IPV4, IGM, IPV4, TCP,  PAY4),
	[0xA5] = WX_PTT(IP, IPV4, IGM, IPV4, SCTP, PAY4),
	[0xA9] = WX_PTT(IP, IPV4, IGM, FGV6, NONE, PAY3),
	[0xAA] = WX_PTT(IP, IPV4, IGM, IPV6, NONE, PAY3),
	[0xAB] = WX_PTT(IP, IPV4, IGM, IPV6, UDP,  PAY4),
	[0xAC] = WX_PTT(IP, IPV4, IGM, IPV6, TCP,  PAY4),
	[0xAD] = WX_PTT(IP, IPV4, IGM, IPV6, SCTP, PAY4),

	/* IPv4 --> GRE/NAT --> MAC+VLAN --> NONE/IPv4/IPv6 */
	[0xB0] = WX_PTT(IP, IPV4, IGMV, NONE, NONE, PAY3),
	[0xB1] = WX_PTT(IP, IPV4, IGMV, FGV4, NONE, PAY3),
	[0xB2] = WX_PTT(IP, IPV4, IGMV, IPV4, NONE, PAY3),
	[0xB3] = WX_PTT(IP, IPV4, IGMV, IPV4, UDP,  PAY4),
	[0xB4] = WX_PTT(IP, IPV4, IGMV, IPV4, TCP,  PAY4),
	[0xB5] = WX_PTT(IP, IPV4, IGMV, IPV4, SCTP, PAY4),
	[0xB9] = WX_PTT(IP, IPV4, IGMV, FGV6, NONE, PAY3),
	[0xBA] = WX_PTT(IP, IPV4, IGMV, IPV6, NONE, PAY3),
	[0xBB] = WX_PTT(IP, IPV4, IGMV, IPV6, UDP,  PAY4),
	[0xBC] = WX_PTT(IP, IPV4, IGMV, IPV6, TCP,  PAY4),
	[0xBD] = WX_PTT(IP, IPV4, IGMV, IPV6, SCTP, PAY4),

	/* IPv6 --> IPv4/IPv6 */
	[0xC1] = WX_PTT(IP, IPV6, IPIP, FGV4, NONE, PAY3),
	[0xC2] = WX_PTT(IP, IPV6, IPIP, IPV4, NONE, PAY3),
	[0xC3] = WX_PTT(IP, IPV6, IPIP, IPV4, UDP,  PAY4),
	[0xC4] = WX_PTT(IP, IPV6, IPIP, IPV4, TCP,  PAY4),
	[0xC5] = WX_PTT(IP, IPV6, IPIP, IPV4, SCTP, PAY4),
	[0xC9] = WX_PTT(IP, IPV6, IPIP, FGV6, NONE, PAY3),
	[0xCA] = WX_PTT(IP, IPV6, IPIP, IPV6, NONE, PAY3),
	[0xCB] = WX_PTT(IP, IPV6, IPIP, IPV6, UDP,  PAY4),
	[0xCC] = WX_PTT(IP, IPV6, IPIP, IPV6, TCP,  PAY4),
	[0xCD] = WX_PTT(IP, IPV6, IPIP, IPV6, SCTP, PAY4),

	/* IPv6 --> GRE/NAT -> NONE/IPv4/IPv6 */
	[0xD0] = WX_PTT(IP, IPV6, IG, NONE, NONE, PAY3),
	[0xD1] = WX_PTT(IP, IPV6, IG, FGV4, NONE, PAY3),
	[0xD2] = WX_PTT(IP, IPV6, IG, IPV4, NONE, PAY3),
	[0xD3] = WX_PTT(IP, IPV6, IG, IPV4, UDP,  PAY4),
	[0xD4] = WX_PTT(IP, IPV6, IG, IPV4, TCP,  PAY4),
	[0xD5] = WX_PTT(IP, IPV6, IG, IPV4, SCTP, PAY4),
	[0xD9] = WX_PTT(IP, IPV6, IG, FGV6, NONE, PAY3),
	[0xDA] = WX_PTT(IP, IPV6, IG, IPV6, NONE, PAY3),
	[0xDB] = WX_PTT(IP, IPV6, IG, IPV6, UDP,  PAY4),
	[0xDC] = WX_PTT(IP, IPV6, IG, IPV6, TCP,  PAY4),
	[0xDD] = WX_PTT(IP, IPV6, IG, IPV6, SCTP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC -> NONE/IPv4/IPv6 */
	[0xE0] = WX_PTT(IP, IPV6, IGM, NONE, NONE, PAY3),
	[0xE1] = WX_PTT(IP, IPV6, IGM, FGV4, NONE, PAY3),
	[0xE2] = WX_PTT(IP, IPV6, IGM, IPV4, NONE, PAY3),
	[0xE3] = WX_PTT(IP, IPV6, IGM, IPV4, UDP,  PAY4),
	[0xE4] = WX_PTT(IP, IPV6, IGM, IPV4, TCP,  PAY4),
	[0xE5] = WX_PTT(IP, IPV6, IGM, IPV4, SCTP, PAY4),
	[0xE9] = WX_PTT(IP, IPV6, IGM, FGV6, NONE, PAY3),
	[0xEA] = WX_PTT(IP, IPV6, IGM, IPV6, NONE, PAY3),
	[0xEB] = WX_PTT(IP, IPV6, IGM, IPV6, UDP,  PAY4),
	[0xEC] = WX_PTT(IP, IPV6, IGM, IPV6, TCP,  PAY4),
	[0xED] = WX_PTT(IP, IPV6, IGM, IPV6, SCTP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC--> NONE/IPv */
	[0xF0] = WX_PTT(IP, IPV6, IGMV, NONE, NONE, PAY3),
	[0xF1] = WX_PTT(IP, IPV6, IGMV, FGV4, NONE, PAY3),
	[0xF2] = WX_PTT(IP, IPV6, IGMV, IPV4, NONE, PAY3),
	[0xF3] = WX_PTT(IP, IPV6, IGMV, IPV4, UDP,  PAY4),
	[0xF4] = WX_PTT(IP, IPV6, IGMV, IPV4, TCP,  PAY4),
	[0xF5] = WX_PTT(IP, IPV6, IGMV, IPV4, SCTP, PAY4),
	[0xF9] = WX_PTT(IP, IPV6, IGMV, FGV6, NONE, PAY3),
	[0xFA] = WX_PTT(IP, IPV6, IGMV, IPV6, NONE, PAY3),
	[0xFB] = WX_PTT(IP, IPV6, IGMV, IPV6, UDP,  PAY4),
	[0xFC] = WX_PTT(IP, IPV6, IGMV, IPV6, TCP,  PAY4),
	[0xFD] = WX_PTT(IP, IPV6, IGMV, IPV6, SCTP, PAY4),
};

static struct wx_dec_ptype wx_decode_ptype(const u8 ptype)
{
	return wx_ptype_lookup[ptype];
}

/* wx_test_staterr - tests bits in Rx descriptor status and error fields */
static __le32 wx_test_staterr(union wx_rx_desc *rx_desc,
			      const u32 stat_err_bits)
{
	return rx_desc->wb.upper.status_error & cpu_to_le32(stat_err_bits);
}

static bool wx_can_reuse_rx_page(struct wx_rx_buffer *rx_buffer,
				 int rx_buffer_pgcnt)
{
	unsigned int pagecnt_bias = rx_buffer->pagecnt_bias;
	struct page *page = rx_buffer->page;

	/* avoid re-using remote and pfmemalloc pages */
	if (!dev_page_is_reusable(page))
		return false;

#if (PAGE_SIZE < 8192)
	/* if we are only owner of page we can reuse it */
	if (unlikely((rx_buffer_pgcnt - pagecnt_bias) > 1))
		return false;
#endif

	/* If we have drained the page fragment pool we need to update
	 * the pagecnt_bias and page count so that we fully restock the
	 * number of references the driver holds.
	 */
	if (unlikely(pagecnt_bias == 1)) {
		page_ref_add(page, USHRT_MAX - 1);
		rx_buffer->pagecnt_bias = USHRT_MAX;
	}

	return true;
}

/**
 * wx_reuse_rx_page - page flip buffer and store it back on the ring
 * @rx_ring: rx descriptor ring to store buffers on
 * @old_buff: donor buffer to have page reused
 *
 * Synchronizes page for reuse by the adapter
 **/
static void wx_reuse_rx_page(struct wx_ring *rx_ring,
			     struct wx_rx_buffer *old_buff)
{
	u16 nta = rx_ring->next_to_alloc;
	struct wx_rx_buffer *new_buff;

	new_buff = &rx_ring->rx_buffer_info[nta];

	/* update, and store next to alloc */
	nta++;
	rx_ring->next_to_alloc = (nta < rx_ring->count) ? nta : 0;

	/* transfer page from old buffer to new buffer */
	new_buff->page = old_buff->page;
	new_buff->page_dma = old_buff->page_dma;
	new_buff->page_offset = old_buff->page_offset;
	new_buff->pagecnt_bias	= old_buff->pagecnt_bias;
}

static void wx_dma_sync_frag(struct wx_ring *rx_ring,
			     struct wx_rx_buffer *rx_buffer)
{
	struct sk_buff *skb = rx_buffer->skb;
	skb_frag_t *frag = &skb_shinfo(skb)->frags[0];

	dma_sync_single_range_for_cpu(rx_ring->dev,
				      WX_CB(skb)->dma,
				      skb_frag_off(frag),
				      skb_frag_size(frag),
				      DMA_FROM_DEVICE);

	/* If the page was released, just unmap it. */
	if (unlikely(WX_CB(skb)->page_released))
		page_pool_put_full_page(rx_ring->page_pool, rx_buffer->page, false);
}

static struct wx_rx_buffer *wx_get_rx_buffer(struct wx_ring *rx_ring,
					     union wx_rx_desc *rx_desc,
					     struct sk_buff **skb,
					     int *rx_buffer_pgcnt)
{
	struct wx_rx_buffer *rx_buffer;
	unsigned int size;

	rx_buffer = &rx_ring->rx_buffer_info[rx_ring->next_to_clean];
	size = le16_to_cpu(rx_desc->wb.upper.length);

#if (PAGE_SIZE < 8192)
	*rx_buffer_pgcnt = page_count(rx_buffer->page);
#else
	*rx_buffer_pgcnt = 0;
#endif

	prefetchw(rx_buffer->page);
	*skb = rx_buffer->skb;

	/* Delay unmapping of the first packet. It carries the header
	 * information, HW may still access the header after the writeback.
	 * Only unmap it when EOP is reached
	 */
	if (!wx_test_staterr(rx_desc, WX_RXD_STAT_EOP)) {
		if (!*skb)
			goto skip_sync;
	} else {
		if (*skb)
			wx_dma_sync_frag(rx_ring, rx_buffer);
	}

	/* we are reusing so sync this buffer for CPU use */
	dma_sync_single_range_for_cpu(rx_ring->dev,
				      rx_buffer->dma,
				      rx_buffer->page_offset,
				      size,
				      DMA_FROM_DEVICE);
skip_sync:
	rx_buffer->pagecnt_bias--;

	return rx_buffer;
}

static void wx_put_rx_buffer(struct wx_ring *rx_ring,
			     struct wx_rx_buffer *rx_buffer,
			     struct sk_buff *skb,
			     int rx_buffer_pgcnt)
{
	if (wx_can_reuse_rx_page(rx_buffer, rx_buffer_pgcnt)) {
		/* hand second half of page back to the ring */
		wx_reuse_rx_page(rx_ring, rx_buffer);
	} else {
		if (!IS_ERR(skb) && WX_CB(skb)->dma == rx_buffer->dma)
			/* the page has been released from the ring */
			WX_CB(skb)->page_released = true;
		else
			page_pool_put_full_page(rx_ring->page_pool, rx_buffer->page, false);

		__page_frag_cache_drain(rx_buffer->page,
					rx_buffer->pagecnt_bias);
	}

	/* clear contents of rx_buffer */
	rx_buffer->page = NULL;
	rx_buffer->skb = NULL;
}

static struct sk_buff *wx_build_skb(struct wx_ring *rx_ring,
				    struct wx_rx_buffer *rx_buffer,
				    union wx_rx_desc *rx_desc)
{
	unsigned int size = le16_to_cpu(rx_desc->wb.upper.length);
#if (PAGE_SIZE < 8192)
	unsigned int truesize = WX_RX_BUFSZ;
#else
	unsigned int truesize = ALIGN(size, L1_CACHE_BYTES);
#endif
	struct sk_buff *skb = rx_buffer->skb;

	if (!skb) {
		void *page_addr = page_address(rx_buffer->page) +
				  rx_buffer->page_offset;

		/* prefetch first cache line of first page */
		prefetch(page_addr);
#if L1_CACHE_BYTES < 128
		prefetch(page_addr + L1_CACHE_BYTES);
#endif

		/* allocate a skb to store the frags */
		skb = napi_alloc_skb(&rx_ring->q_vector->napi, WX_RXBUFFER_256);
		if (unlikely(!skb))
			return NULL;

		/* we will be copying header into skb->data in
		 * pskb_may_pull so it is in our interest to prefetch
		 * it now to avoid a possible cache miss
		 */
		prefetchw(skb->data);

		if (size <= WX_RXBUFFER_256) {
			memcpy(__skb_put(skb, size), page_addr,
			       ALIGN(size, sizeof(long)));
			rx_buffer->pagecnt_bias++;

			return skb;
		}

		if (!wx_test_staterr(rx_desc, WX_RXD_STAT_EOP))
			WX_CB(skb)->dma = rx_buffer->dma;

		skb_add_rx_frag(skb, 0, rx_buffer->page,
				rx_buffer->page_offset,
				size, truesize);
		goto out;

	} else {
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, rx_buffer->page,
				rx_buffer->page_offset, size, truesize);
	}

out:
#if (PAGE_SIZE < 8192)
	/* flip page offset to other buffer */
	rx_buffer->page_offset ^= truesize;
#else
	/* move offset up to the next cache line */
	rx_buffer->page_offset += truesize;
#endif

	return skb;
}

static bool wx_alloc_mapped_page(struct wx_ring *rx_ring,
				 struct wx_rx_buffer *bi)
{
	struct page *page = bi->page;
	dma_addr_t dma;

	/* since we are recycling buffers we should seldom need to alloc */
	if (likely(page))
		return true;

	page = page_pool_dev_alloc_pages(rx_ring->page_pool);
	WARN_ON(!page);
	dma = page_pool_get_dma_addr(page);

	bi->page_dma = dma;
	bi->page = page;
	bi->page_offset = 0;
	page_ref_add(page, USHRT_MAX - 1);
	bi->pagecnt_bias = USHRT_MAX;

	return true;
}

/**
 * wx_alloc_rx_buffers - Replace used receive buffers
 * @rx_ring: ring to place buffers on
 * @cleaned_count: number of buffers to replace
 **/
void wx_alloc_rx_buffers(struct wx_ring *rx_ring, u16 cleaned_count)
{
	u16 i = rx_ring->next_to_use;
	union wx_rx_desc *rx_desc;
	struct wx_rx_buffer *bi;

	/* nothing to do */
	if (!cleaned_count)
		return;

	rx_desc = WX_RX_DESC(rx_ring, i);
	bi = &rx_ring->rx_buffer_info[i];
	i -= rx_ring->count;

	do {
		if (!wx_alloc_mapped_page(rx_ring, bi))
			break;

		/* sync the buffer for use by the device */
		dma_sync_single_range_for_device(rx_ring->dev, bi->dma,
						 bi->page_offset,
						 WX_RX_BUFSZ,
						 DMA_FROM_DEVICE);

		rx_desc->read.pkt_addr =
			cpu_to_le64(bi->page_dma + bi->page_offset);

		rx_desc++;
		bi++;
		i++;
		if (unlikely(!i)) {
			rx_desc = WX_RX_DESC(rx_ring, 0);
			bi = rx_ring->rx_buffer_info;
			i -= rx_ring->count;
		}

		/* clear the status bits for the next_to_use descriptor */
		rx_desc->wb.upper.status_error = 0;

		cleaned_count--;
	} while (cleaned_count);

	i += rx_ring->count;

	if (rx_ring->next_to_use != i) {
		rx_ring->next_to_use = i;
		/* update next to alloc since we have filled the ring */
		rx_ring->next_to_alloc = i;

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		writel(i, rx_ring->tail);
	}
}

u16 wx_desc_unused(struct wx_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - 1;
}

/**
 * wx_is_non_eop - process handling of non-EOP buffers
 * @rx_ring: Rx ring being processed
 * @rx_desc: Rx descriptor for current buffer
 * @skb: Current socket buffer containing buffer in progress
 *
 * This function updates next to clean. If the buffer is an EOP buffer
 * this function exits returning false, otherwise it will place the
 * sk_buff in the next buffer to be chained and return true indicating
 * that this is in fact a non-EOP buffer.
 **/
static bool wx_is_non_eop(struct wx_ring *rx_ring,
			  union wx_rx_desc *rx_desc,
			  struct sk_buff *skb)
{
	u32 ntc = rx_ring->next_to_clean + 1;

	/* fetch, update, and store next to clean */
	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;

	prefetch(WX_RX_DESC(rx_ring, ntc));

	/* if we are the last buffer then there is nothing else to do */
	if (likely(wx_test_staterr(rx_desc, WX_RXD_STAT_EOP)))
		return false;

	rx_ring->rx_buffer_info[ntc].skb = skb;

	return true;
}

static void wx_pull_tail(struct sk_buff *skb)
{
	skb_frag_t *frag = &skb_shinfo(skb)->frags[0];
	unsigned int pull_len;
	unsigned char *va;

	/* it is valid to use page_address instead of kmap since we are
	 * working with pages allocated out of the lomem pool per
	 * alloc_page(GFP_ATOMIC)
	 */
	va = skb_frag_address(frag);

	/* we need the header to contain the greater of either ETH_HLEN or
	 * 60 bytes if the skb->len is less than 60 for skb_pad.
	 */
	pull_len = eth_get_headlen(skb->dev, va, WX_RXBUFFER_256);

	/* align pull length to size of long to optimize memcpy performance */
	skb_copy_to_linear_data(skb, va, ALIGN(pull_len, sizeof(long)));

	/* update all of the pointers */
	skb_frag_size_sub(frag, pull_len);
	skb_frag_off_add(frag, pull_len);
	skb->data_len -= pull_len;
	skb->tail += pull_len;
}

/**
 * wx_cleanup_headers - Correct corrupted or empty headers
 * @rx_ring: rx descriptor ring packet is being transacted on
 * @rx_desc: pointer to the EOP Rx descriptor
 * @skb: pointer to current skb being fixed
 *
 * Check for corrupted packet headers caused by senders on the local L2
 * embedded NIC switch not setting up their Tx Descriptors right.  These
 * should be very rare.
 *
 * Also address the case where we are pulling data in on pages only
 * and as such no data is present in the skb header.
 *
 * In addition if skb is not at least 60 bytes we need to pad it so that
 * it is large enough to qualify as a valid Ethernet frame.
 *
 * Returns true if an error was encountered and skb was freed.
 **/
static bool wx_cleanup_headers(struct wx_ring *rx_ring,
			       union wx_rx_desc *rx_desc,
			       struct sk_buff *skb)
{
	struct net_device *netdev = rx_ring->netdev;

	/* verify that the packet does not have any known errors */
	if (!netdev ||
	    unlikely(wx_test_staterr(rx_desc, WX_RXD_ERR_RXE) &&
		     !(netdev->features & NETIF_F_RXALL))) {
		dev_kfree_skb_any(skb);
		return true;
	}

	/* place header in linear portion of buffer */
	if (!skb_headlen(skb))
		wx_pull_tail(skb);

	/* if eth_skb_pad returns an error the skb was freed */
	if (eth_skb_pad(skb))
		return true;

	return false;
}

static void wx_rx_hash(struct wx_ring *ring,
		       union wx_rx_desc *rx_desc,
		       struct sk_buff *skb)
{
	u16 rss_type;

	if (!(ring->netdev->features & NETIF_F_RXHASH))
		return;

	rss_type = le16_to_cpu(rx_desc->wb.lower.lo_dword.hs_rss.pkt_info) &
			       WX_RXD_RSSTYPE_MASK;

	if (!rss_type)
		return;

	skb_set_hash(skb, le32_to_cpu(rx_desc->wb.lower.hi_dword.rss),
		     (WX_RSS_L4_TYPES_MASK & (1ul << rss_type)) ?
		     PKT_HASH_TYPE_L4 : PKT_HASH_TYPE_L3);
}

/**
 * wx_rx_checksum - indicate in skb if hw indicated a good cksum
 * @ring: structure containing ring specific data
 * @rx_desc: current Rx descriptor being processed
 * @skb: skb currently being received and modified
 **/
static void wx_rx_checksum(struct wx_ring *ring,
			   union wx_rx_desc *rx_desc,
			   struct sk_buff *skb)
{
	struct wx_dec_ptype dptype = wx_decode_ptype(WX_RXD_PKTTYPE(rx_desc));

	skb_checksum_none_assert(skb);
	/* Rx csum disabled */
	if (!(ring->netdev->features & NETIF_F_RXCSUM))
		return;

	/* if IPv4 header checksum error */
	if ((wx_test_staterr(rx_desc, WX_RXD_STAT_IPCS) &&
	     wx_test_staterr(rx_desc, WX_RXD_ERR_IPE)) ||
	    (wx_test_staterr(rx_desc, WX_RXD_STAT_OUTERIPCS) &&
	     wx_test_staterr(rx_desc, WX_RXD_ERR_OUTERIPER))) {
		ring->rx_stats.csum_err++;
		return;
	}

	/* L4 checksum offload flag must set for the below code to work */
	if (!wx_test_staterr(rx_desc, WX_RXD_STAT_L4CS))
		return;

	/* Hardware can't guarantee csum if IPv6 Dest Header found */
	if (dptype.prot != WX_DEC_PTYPE_PROT_SCTP && WX_RXD_IPV6EX(rx_desc))
		return;

	/* if L4 checksum error */
	if (wx_test_staterr(rx_desc, WX_RXD_ERR_TCPE)) {
		ring->rx_stats.csum_err++;
		return;
	}

	/* It must be a TCP or UDP or SCTP packet with a valid checksum */
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	/* If there is an outer header present that might contain a checksum
	 * we need to bump the checksum level by 1 to reflect the fact that
	 * we are indicating we validated the inner checksum.
	 */
	if (dptype.etype >= WX_DEC_PTYPE_ETYPE_IG)
		__skb_incr_checksum_unnecessary(skb);
	ring->rx_stats.csum_good_cnt++;
}

static void wx_rx_vlan(struct wx_ring *ring, union wx_rx_desc *rx_desc,
		       struct sk_buff *skb)
{
	u16 ethertype;
	u8 idx = 0;

	if ((ring->netdev->features &
	     (NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_STAG_RX)) &&
	    wx_test_staterr(rx_desc, WX_RXD_STAT_VP)) {
		idx = (le16_to_cpu(rx_desc->wb.lower.lo_dword.hs_rss.pkt_info) &
		       0x1c0) >> 6;
		ethertype = ring->q_vector->wx->tpid[idx];
		__vlan_hwaccel_put_tag(skb, htons(ethertype),
				       le16_to_cpu(rx_desc->wb.upper.vlan));
	}
}

/**
 * wx_process_skb_fields - Populate skb header fields from Rx descriptor
 * @rx_ring: rx descriptor ring packet is being transacted on
 * @rx_desc: pointer to the EOP Rx descriptor
 * @skb: pointer to current skb being populated
 *
 * This function checks the ring, descriptor, and packet information in
 * order to populate the hash, checksum, protocol, and
 * other fields within the skb.
 **/
static void wx_process_skb_fields(struct wx_ring *rx_ring,
				  union wx_rx_desc *rx_desc,
				  struct sk_buff *skb)
{
	wx_rx_hash(rx_ring, rx_desc, skb);
	wx_rx_checksum(rx_ring, rx_desc, skb);
	wx_rx_vlan(rx_ring, rx_desc, skb);
	skb_record_rx_queue(skb, rx_ring->queue_index);
	skb->protocol = eth_type_trans(skb, rx_ring->netdev);
}

/**
 * wx_clean_rx_irq - Clean completed descriptors from Rx ring - bounce buf
 * @q_vector: structure containing interrupt and ring information
 * @rx_ring: rx descriptor ring to transact packets on
 * @budget: Total limit on number of packets to process
 *
 * This function provides a "bounce buffer" approach to Rx interrupt
 * processing.  The advantage to this is that on systems that have
 * expensive overhead for IOMMU access this provides a means of avoiding
 * it by maintaining the mapping of the page to the system.
 *
 * Returns amount of work completed.
 **/
static int wx_clean_rx_irq(struct wx_q_vector *q_vector,
			   struct wx_ring *rx_ring,
			   int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;
	u16 cleaned_count = wx_desc_unused(rx_ring);

	do {
		struct wx_rx_buffer *rx_buffer;
		union wx_rx_desc *rx_desc;
		struct sk_buff *skb;
		int rx_buffer_pgcnt;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= WX_RX_BUFFER_WRITE) {
			wx_alloc_rx_buffers(rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		rx_desc = WX_RX_DESC(rx_ring, rx_ring->next_to_clean);
		if (!wx_test_staterr(rx_desc, WX_RXD_STAT_DD))
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we know the
		 * descriptor has been written back
		 */
		dma_rmb();

		rx_buffer = wx_get_rx_buffer(rx_ring, rx_desc, &skb, &rx_buffer_pgcnt);

		/* retrieve a buffer from the ring */
		skb = wx_build_skb(rx_ring, rx_buffer, rx_desc);

		/* exit if we failed to retrieve a buffer */
		if (!skb) {
			rx_buffer->pagecnt_bias++;
			break;
		}

		wx_put_rx_buffer(rx_ring, rx_buffer, skb, rx_buffer_pgcnt);
		cleaned_count++;

		/* place incomplete frames back on ring for completion */
		if (wx_is_non_eop(rx_ring, rx_desc, skb))
			continue;

		/* verify the packet layout is correct */
		if (wx_cleanup_headers(rx_ring, rx_desc, skb))
			continue;

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += skb->len;

		/* populate checksum, timestamp, VLAN, and protocol */
		wx_process_skb_fields(rx_ring, rx_desc, skb);
		napi_gro_receive(&q_vector->napi, skb);

		/* update budget accounting */
		total_rx_packets++;
	} while (likely(total_rx_packets < budget));

	u64_stats_update_begin(&rx_ring->syncp);
	rx_ring->stats.packets += total_rx_packets;
	rx_ring->stats.bytes += total_rx_bytes;
	u64_stats_update_end(&rx_ring->syncp);
	q_vector->rx.total_packets += total_rx_packets;
	q_vector->rx.total_bytes += total_rx_bytes;

	return total_rx_packets;
}

static struct netdev_queue *wx_txring_txq(const struct wx_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->queue_index);
}

/**
 * wx_clean_tx_irq - Reclaim resources after transmit completes
 * @q_vector: structure containing interrupt and ring information
 * @tx_ring: tx ring to clean
 * @napi_budget: Used to determine if we are in netpoll
 **/
static bool wx_clean_tx_irq(struct wx_q_vector *q_vector,
			    struct wx_ring *tx_ring, int napi_budget)
{
	unsigned int budget = q_vector->wx->tx_work_limit;
	unsigned int total_bytes = 0, total_packets = 0;
	unsigned int i = tx_ring->next_to_clean;
	struct wx_tx_buffer *tx_buffer;
	union wx_tx_desc *tx_desc;

	if (!netif_carrier_ok(tx_ring->netdev))
		return true;

	tx_buffer = &tx_ring->tx_buffer_info[i];
	tx_desc = WX_TX_DESC(tx_ring, i);
	i -= tx_ring->count;

	do {
		union wx_tx_desc *eop_desc = tx_buffer->next_to_watch;

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		/* prevent any other reads prior to eop_desc */
		smp_rmb();

		/* if DD is not set pending work has not been completed */
		if (!(eop_desc->wb.status & cpu_to_le32(WX_TXD_STAT_DD)))
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buffer->next_to_watch = NULL;

		/* update the statistics for this packet */
		total_bytes += tx_buffer->bytecount;
		total_packets += tx_buffer->gso_segs;

		/* free the skb */
		napi_consume_skb(tx_buffer->skb, napi_budget);

		/* unmap skb header data */
		dma_unmap_single(tx_ring->dev,
				 dma_unmap_addr(tx_buffer, dma),
				 dma_unmap_len(tx_buffer, len),
				 DMA_TO_DEVICE);

		/* clear tx_buffer data */
		dma_unmap_len_set(tx_buffer, len, 0);

		/* unmap remaining buffers */
		while (tx_desc != eop_desc) {
			tx_buffer++;
			tx_desc++;
			i++;
			if (unlikely(!i)) {
				i -= tx_ring->count;
				tx_buffer = tx_ring->tx_buffer_info;
				tx_desc = WX_TX_DESC(tx_ring, 0);
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
			tx_buffer = tx_ring->tx_buffer_info;
			tx_desc = WX_TX_DESC(tx_ring, 0);
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

	netdev_tx_completed_queue(wx_txring_txq(tx_ring),
				  total_packets, total_bytes);

#define TX_WAKE_THRESHOLD (DESC_NEEDED * 2)
	if (unlikely(total_packets && netif_carrier_ok(tx_ring->netdev) &&
		     (wx_desc_unused(tx_ring) >= TX_WAKE_THRESHOLD))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();

		if (__netif_subqueue_stopped(tx_ring->netdev,
					     tx_ring->queue_index) &&
		    netif_running(tx_ring->netdev))
			netif_wake_subqueue(tx_ring->netdev,
					    tx_ring->queue_index);
	}

	return !!budget;
}

/**
 * wx_poll - NAPI polling RX/TX cleanup routine
 * @napi: napi struct with our devices info in it
 * @budget: amount of work driver is allowed to do this pass, in packets
 *
 * This function will clean all queues associated with a q_vector.
 **/
static int wx_poll(struct napi_struct *napi, int budget)
{
	struct wx_q_vector *q_vector = container_of(napi, struct wx_q_vector, napi);
	int per_ring_budget, work_done = 0;
	struct wx *wx = q_vector->wx;
	bool clean_complete = true;
	struct wx_ring *ring;

	wx_for_each_ring(ring, q_vector->tx) {
		if (!wx_clean_tx_irq(q_vector, ring, budget))
			clean_complete = false;
	}

	/* Exit if we are called by netpoll */
	if (budget <= 0)
		return budget;

	/* attempt to distribute budget to each queue fairly, but don't allow
	 * the budget to go below 1 because we'll exit polling
	 */
	if (q_vector->rx.count > 1)
		per_ring_budget = max(budget / q_vector->rx.count, 1);
	else
		per_ring_budget = budget;

	wx_for_each_ring(ring, q_vector->rx) {
		int cleaned = wx_clean_rx_irq(q_vector, ring, per_ring_budget);

		work_done += cleaned;
		if (cleaned >= per_ring_budget)
			clean_complete = false;
	}

	/* If all work not completed, return budget and keep polling */
	if (!clean_complete)
		return budget;

	/* all work done, exit the polling mode */
	if (likely(napi_complete_done(napi, work_done))) {
		if (netif_running(wx->netdev))
			wx_intr_enable(wx, WX_INTR_Q(q_vector->v_idx));
	}

	return min(work_done, budget - 1);
}

static int wx_maybe_stop_tx(struct wx_ring *tx_ring, u16 size)
{
	if (likely(wx_desc_unused(tx_ring) >= size))
		return 0;

	netif_stop_subqueue(tx_ring->netdev, tx_ring->queue_index);

	/* For the next check */
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available.
	 */
	if (likely(wx_desc_unused(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! - use start_queue because it doesn't call schedule */
	netif_start_subqueue(tx_ring->netdev, tx_ring->queue_index);

	return 0;
}

static u32 wx_tx_cmd_type(u32 tx_flags)
{
	/* set type for advanced descriptor with frame checksum insertion */
	u32 cmd_type = WX_TXD_DTYP_DATA | WX_TXD_IFCS;

	/* set HW vlan bit if vlan is present */
	cmd_type |= WX_SET_FLAG(tx_flags, WX_TX_FLAGS_HW_VLAN, WX_TXD_VLE);
	/* set segmentation enable bits for TSO/FSO */
	cmd_type |= WX_SET_FLAG(tx_flags, WX_TX_FLAGS_TSO, WX_TXD_TSE);
	/* set timestamp bit if present */
	cmd_type |= WX_SET_FLAG(tx_flags, WX_TX_FLAGS_TSTAMP, WX_TXD_MAC_TSTAMP);
	cmd_type |= WX_SET_FLAG(tx_flags, WX_TX_FLAGS_LINKSEC, WX_TXD_LINKSEC);

	return cmd_type;
}

static void wx_tx_olinfo_status(union wx_tx_desc *tx_desc,
				u32 tx_flags, unsigned int paylen)
{
	u32 olinfo_status = paylen << WX_TXD_PAYLEN_SHIFT;

	/* enable L4 checksum for TSO and TX checksum offload */
	olinfo_status |= WX_SET_FLAG(tx_flags, WX_TX_FLAGS_CSUM, WX_TXD_L4CS);
	/* enable IPv4 checksum for TSO */
	olinfo_status |= WX_SET_FLAG(tx_flags, WX_TX_FLAGS_IPV4, WX_TXD_IIPCS);
	/* enable outer IPv4 checksum for TSO */
	olinfo_status |= WX_SET_FLAG(tx_flags, WX_TX_FLAGS_OUTER_IPV4,
				     WX_TXD_EIPCS);
	/* Check Context must be set if Tx switch is enabled, which it
	 * always is for case where virtual functions are running
	 */
	olinfo_status |= WX_SET_FLAG(tx_flags, WX_TX_FLAGS_CC, WX_TXD_CC);
	olinfo_status |= WX_SET_FLAG(tx_flags, WX_TX_FLAGS_IPSEC,
				     WX_TXD_IPSEC);
	tx_desc->read.olinfo_status = cpu_to_le32(olinfo_status);
}

static void wx_tx_map(struct wx_ring *tx_ring,
		      struct wx_tx_buffer *first,
		      const u8 hdr_len)
{
	struct sk_buff *skb = first->skb;
	struct wx_tx_buffer *tx_buffer;
	u32 tx_flags = first->tx_flags;
	u16 i = tx_ring->next_to_use;
	unsigned int data_len, size;
	union wx_tx_desc *tx_desc;
	skb_frag_t *frag;
	dma_addr_t dma;
	u32 cmd_type;

	cmd_type = wx_tx_cmd_type(tx_flags);
	tx_desc = WX_TX_DESC(tx_ring, i);
	wx_tx_olinfo_status(tx_desc, tx_flags, skb->len - hdr_len);

	size = skb_headlen(skb);
	data_len = skb->data_len;
	dma = dma_map_single(tx_ring->dev, skb->data, size, DMA_TO_DEVICE);

	tx_buffer = first;

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		if (dma_mapping_error(tx_ring->dev, dma))
			goto dma_error;

		/* record length, and DMA address */
		dma_unmap_len_set(tx_buffer, len, size);
		dma_unmap_addr_set(tx_buffer, dma, dma);

		tx_desc->read.buffer_addr = cpu_to_le64(dma);

		while (unlikely(size > WX_MAX_DATA_PER_TXD)) {
			tx_desc->read.cmd_type_len =
				cpu_to_le32(cmd_type ^ WX_MAX_DATA_PER_TXD);

			i++;
			tx_desc++;
			if (i == tx_ring->count) {
				tx_desc = WX_TX_DESC(tx_ring, 0);
				i = 0;
			}
			tx_desc->read.olinfo_status = 0;

			dma += WX_MAX_DATA_PER_TXD;
			size -= WX_MAX_DATA_PER_TXD;

			tx_desc->read.buffer_addr = cpu_to_le64(dma);
		}

		if (likely(!data_len))
			break;

		tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type ^ size);

		i++;
		tx_desc++;
		if (i == tx_ring->count) {
			tx_desc = WX_TX_DESC(tx_ring, 0);
			i = 0;
		}
		tx_desc->read.olinfo_status = 0;

		size = skb_frag_size(frag);

		data_len -= size;

		dma = skb_frag_dma_map(tx_ring->dev, frag, 0, size,
				       DMA_TO_DEVICE);

		tx_buffer = &tx_ring->tx_buffer_info[i];
	}

	/* write last descriptor with RS and EOP bits */
	cmd_type |= size | WX_TXD_EOP | WX_TXD_RS;
	tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type);

	netdev_tx_sent_queue(wx_txring_txq(tx_ring), first->bytecount);

	skb_tx_timestamp(skb);

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

	i++;
	if (i == tx_ring->count)
		i = 0;

	tx_ring->next_to_use = i;

	wx_maybe_stop_tx(tx_ring, DESC_NEEDED);

	if (netif_xmit_stopped(wx_txring_txq(tx_ring)) || !netdev_xmit_more())
		writel(i, tx_ring->tail);

	return;
dma_error:
	dev_err(tx_ring->dev, "TX DMA map failed\n");

	/* clear dma mappings for failed tx_buffer_info map */
	for (;;) {
		tx_buffer = &tx_ring->tx_buffer_info[i];
		if (dma_unmap_len(tx_buffer, len))
			dma_unmap_page(tx_ring->dev,
				       dma_unmap_addr(tx_buffer, dma),
				       dma_unmap_len(tx_buffer, len),
				       DMA_TO_DEVICE);
		dma_unmap_len_set(tx_buffer, len, 0);
		if (tx_buffer == first)
			break;
		if (i == 0)
			i += tx_ring->count;
		i--;
	}

	dev_kfree_skb_any(first->skb);
	first->skb = NULL;

	tx_ring->next_to_use = i;
}

static void wx_tx_ctxtdesc(struct wx_ring *tx_ring, u32 vlan_macip_lens,
			   u32 fcoe_sof_eof, u32 type_tucmd, u32 mss_l4len_idx)
{
	struct wx_tx_context_desc *context_desc;
	u16 i = tx_ring->next_to_use;

	context_desc = WX_TX_CTXTDESC(tx_ring, i);
	i++;
	tx_ring->next_to_use = (i < tx_ring->count) ? i : 0;

	/* set bits to identify this as an advanced context descriptor */
	type_tucmd |= WX_TXD_DTYP_CTXT;
	context_desc->vlan_macip_lens   = cpu_to_le32(vlan_macip_lens);
	context_desc->seqnum_seed       = cpu_to_le32(fcoe_sof_eof);
	context_desc->type_tucmd_mlhl   = cpu_to_le32(type_tucmd);
	context_desc->mss_l4len_idx     = cpu_to_le32(mss_l4len_idx);
}

static void wx_get_ipv6_proto(struct sk_buff *skb, int offset, u8 *nexthdr)
{
	struct ipv6hdr *hdr = (struct ipv6hdr *)(skb->data + offset);

	*nexthdr = hdr->nexthdr;
	offset += sizeof(struct ipv6hdr);
	while (ipv6_ext_hdr(*nexthdr)) {
		struct ipv6_opt_hdr _hdr, *hp;

		if (*nexthdr == NEXTHDR_NONE)
			return;
		hp = skb_header_pointer(skb, offset, sizeof(_hdr), &_hdr);
		if (!hp)
			return;
		if (*nexthdr == NEXTHDR_FRAGMENT)
			break;
		*nexthdr = hp->nexthdr;
	}
}

union network_header {
	struct iphdr *ipv4;
	struct ipv6hdr *ipv6;
	void *raw;
};

static u8 wx_encode_tx_desc_ptype(const struct wx_tx_buffer *first)
{
	u8 tun_prot = 0, l4_prot = 0, ptype = 0;
	struct sk_buff *skb = first->skb;

	if (skb->encapsulation) {
		union network_header hdr;

		switch (first->protocol) {
		case htons(ETH_P_IP):
			tun_prot = ip_hdr(skb)->protocol;
			ptype = WX_PTYPE_TUN_IPV4;
			break;
		case htons(ETH_P_IPV6):
			wx_get_ipv6_proto(skb, skb_network_offset(skb), &tun_prot);
			ptype = WX_PTYPE_TUN_IPV6;
			break;
		default:
			return ptype;
		}

		if (tun_prot == IPPROTO_IPIP) {
			hdr.raw = (void *)inner_ip_hdr(skb);
			ptype |= WX_PTYPE_PKT_IPIP;
		} else if (tun_prot == IPPROTO_UDP) {
			hdr.raw = (void *)inner_ip_hdr(skb);
			if (skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
			    skb->inner_protocol != htons(ETH_P_TEB)) {
				ptype |= WX_PTYPE_PKT_IG;
			} else {
				if (((struct ethhdr *)skb_inner_mac_header(skb))->h_proto
				     == htons(ETH_P_8021Q))
					ptype |= WX_PTYPE_PKT_IGMV;
				else
					ptype |= WX_PTYPE_PKT_IGM;
			}

		} else if (tun_prot == IPPROTO_GRE) {
			hdr.raw = (void *)inner_ip_hdr(skb);
			if (skb->inner_protocol ==  htons(ETH_P_IP) ||
			    skb->inner_protocol ==  htons(ETH_P_IPV6)) {
				ptype |= WX_PTYPE_PKT_IG;
			} else {
				if (((struct ethhdr *)skb_inner_mac_header(skb))->h_proto
				    == htons(ETH_P_8021Q))
					ptype |= WX_PTYPE_PKT_IGMV;
				else
					ptype |= WX_PTYPE_PKT_IGM;
			}
		} else {
			return ptype;
		}

		switch (hdr.ipv4->version) {
		case IPVERSION:
			l4_prot = hdr.ipv4->protocol;
			break;
		case 6:
			wx_get_ipv6_proto(skb, skb_inner_network_offset(skb), &l4_prot);
			ptype |= WX_PTYPE_PKT_IPV6;
			break;
		default:
			return ptype;
		}
	} else {
		switch (first->protocol) {
		case htons(ETH_P_IP):
			l4_prot = ip_hdr(skb)->protocol;
			ptype = WX_PTYPE_PKT_IP;
			break;
		case htons(ETH_P_IPV6):
			wx_get_ipv6_proto(skb, skb_network_offset(skb), &l4_prot);
			ptype = WX_PTYPE_PKT_IP | WX_PTYPE_PKT_IPV6;
			break;
		default:
			return WX_PTYPE_PKT_MAC | WX_PTYPE_TYP_MAC;
		}
	}
	switch (l4_prot) {
	case IPPROTO_TCP:
		ptype |= WX_PTYPE_TYP_TCP;
		break;
	case IPPROTO_UDP:
		ptype |= WX_PTYPE_TYP_UDP;
		break;
	case IPPROTO_SCTP:
		ptype |= WX_PTYPE_TYP_SCTP;
		break;
	default:
		ptype |= WX_PTYPE_TYP_IP;
		break;
	}

	return ptype;
}

static int wx_tso(struct wx_ring *tx_ring, struct wx_tx_buffer *first,
		  u8 *hdr_len, u8 ptype)
{
	u32 vlan_macip_lens, type_tucmd, mss_l4len_idx;
	struct net_device *netdev = tx_ring->netdev;
	u32 l4len, tunhdr_eiplen_tunlen = 0;
	struct sk_buff *skb = first->skb;
	bool enc = skb->encapsulation;
	struct ipv6hdr *ipv6h;
	struct tcphdr *tcph;
	struct iphdr *iph;
	u8 tun_prot = 0;
	int err;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (!skb_is_gso(skb))
		return 0;

	err = skb_cow_head(skb, 0);
	if (err < 0)
		return err;

	/* indicates the inner headers in the skbuff are valid. */
	iph = enc ? inner_ip_hdr(skb) : ip_hdr(skb);
	if (iph->version == 4) {
		tcph = enc ? inner_tcp_hdr(skb) : tcp_hdr(skb);
		iph->tot_len = 0;
		iph->check = 0;
		tcph->check = ~csum_tcpudp_magic(iph->saddr,
						 iph->daddr, 0,
						 IPPROTO_TCP, 0);
		first->tx_flags |= WX_TX_FLAGS_TSO |
				   WX_TX_FLAGS_CSUM |
				   WX_TX_FLAGS_IPV4 |
				   WX_TX_FLAGS_CC;
	} else if (iph->version == 6 && skb_is_gso_v6(skb)) {
		ipv6h = enc ? inner_ipv6_hdr(skb) : ipv6_hdr(skb);
		tcph = enc ? inner_tcp_hdr(skb) : tcp_hdr(skb);
		ipv6h->payload_len = 0;
		tcph->check = ~csum_ipv6_magic(&ipv6h->saddr,
					       &ipv6h->daddr, 0,
					       IPPROTO_TCP, 0);
		first->tx_flags |= WX_TX_FLAGS_TSO |
				   WX_TX_FLAGS_CSUM |
				   WX_TX_FLAGS_CC;
	}

	/* compute header lengths */
	l4len = enc ? inner_tcp_hdrlen(skb) : tcp_hdrlen(skb);
	*hdr_len = enc ? (skb_inner_transport_header(skb) - skb->data) :
			 skb_transport_offset(skb);
	*hdr_len += l4len;

	/* update gso size and bytecount with header size */
	first->gso_segs = skb_shinfo(skb)->gso_segs;
	first->bytecount += (first->gso_segs - 1) * *hdr_len;

	/* mss_l4len_id: use 0 as index for TSO */
	mss_l4len_idx = l4len << WX_TXD_L4LEN_SHIFT;
	mss_l4len_idx |= skb_shinfo(skb)->gso_size << WX_TXD_MSS_SHIFT;

	/* vlan_macip_lens: HEADLEN, MACLEN, VLAN tag */
	if (enc) {
		switch (first->protocol) {
		case htons(ETH_P_IP):
			tun_prot = ip_hdr(skb)->protocol;
			first->tx_flags |= WX_TX_FLAGS_OUTER_IPV4;
			break;
		case htons(ETH_P_IPV6):
			tun_prot = ipv6_hdr(skb)->nexthdr;
			break;
		default:
			break;
		}
		switch (tun_prot) {
		case IPPROTO_UDP:
			tunhdr_eiplen_tunlen = WX_TXD_TUNNEL_UDP;
			tunhdr_eiplen_tunlen |= ((skb_network_header_len(skb) >> 2) <<
						 WX_TXD_OUTER_IPLEN_SHIFT) |
						(((skb_inner_mac_header(skb) -
						skb_transport_header(skb)) >> 1) <<
						WX_TXD_TUNNEL_LEN_SHIFT);
			break;
		case IPPROTO_GRE:
			tunhdr_eiplen_tunlen = WX_TXD_TUNNEL_GRE;
			tunhdr_eiplen_tunlen |= ((skb_network_header_len(skb) >> 2) <<
						 WX_TXD_OUTER_IPLEN_SHIFT) |
						(((skb_inner_mac_header(skb) -
						skb_transport_header(skb)) >> 1) <<
						WX_TXD_TUNNEL_LEN_SHIFT);
			break;
		case IPPROTO_IPIP:
			tunhdr_eiplen_tunlen = (((char *)inner_ip_hdr(skb) -
						(char *)ip_hdr(skb)) >> 2) <<
						WX_TXD_OUTER_IPLEN_SHIFT;
			break;
		default:
			break;
		}
		vlan_macip_lens = skb_inner_network_header_len(skb) >> 1;
	} else {
		vlan_macip_lens = skb_network_header_len(skb) >> 1;
	}

	vlan_macip_lens |= skb_network_offset(skb) << WX_TXD_MACLEN_SHIFT;
	vlan_macip_lens |= first->tx_flags & WX_TX_FLAGS_VLAN_MASK;

	type_tucmd = ptype << 24;
	if (skb->vlan_proto == htons(ETH_P_8021AD) &&
	    netdev->features & NETIF_F_HW_VLAN_STAG_TX)
		type_tucmd |= WX_SET_FLAG(first->tx_flags,
					  WX_TX_FLAGS_HW_VLAN,
					  0x1 << WX_TXD_TAG_TPID_SEL_SHIFT);
	wx_tx_ctxtdesc(tx_ring, vlan_macip_lens, tunhdr_eiplen_tunlen,
		       type_tucmd, mss_l4len_idx);

	return 1;
}

static void wx_tx_csum(struct wx_ring *tx_ring, struct wx_tx_buffer *first,
		       u8 ptype)
{
	u32 tunhdr_eiplen_tunlen = 0, vlan_macip_lens = 0;
	struct net_device *netdev = tx_ring->netdev;
	u32 mss_l4len_idx = 0, type_tucmd;
	struct sk_buff *skb = first->skb;
	u8 tun_prot = 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		if (!(first->tx_flags & WX_TX_FLAGS_HW_VLAN) &&
		    !(first->tx_flags & WX_TX_FLAGS_CC))
			return;
		vlan_macip_lens = skb_network_offset(skb) <<
				  WX_TXD_MACLEN_SHIFT;
	} else {
		u8 l4_prot = 0;
		union {
			struct iphdr *ipv4;
			struct ipv6hdr *ipv6;
			u8 *raw;
		} network_hdr;
		union {
			struct tcphdr *tcphdr;
			u8 *raw;
		} transport_hdr;

		if (skb->encapsulation) {
			network_hdr.raw = skb_inner_network_header(skb);
			transport_hdr.raw = skb_inner_transport_header(skb);
			vlan_macip_lens = skb_network_offset(skb) <<
					  WX_TXD_MACLEN_SHIFT;
			switch (first->protocol) {
			case htons(ETH_P_IP):
				tun_prot = ip_hdr(skb)->protocol;
				break;
			case htons(ETH_P_IPV6):
				tun_prot = ipv6_hdr(skb)->nexthdr;
				break;
			default:
				return;
			}
			switch (tun_prot) {
			case IPPROTO_UDP:
				tunhdr_eiplen_tunlen = WX_TXD_TUNNEL_UDP;
				tunhdr_eiplen_tunlen |=
					((skb_network_header_len(skb) >> 2) <<
					WX_TXD_OUTER_IPLEN_SHIFT) |
					(((skb_inner_mac_header(skb) -
					skb_transport_header(skb)) >> 1) <<
					WX_TXD_TUNNEL_LEN_SHIFT);
				break;
			case IPPROTO_GRE:
				tunhdr_eiplen_tunlen = WX_TXD_TUNNEL_GRE;
				tunhdr_eiplen_tunlen |= ((skb_network_header_len(skb) >> 2) <<
							 WX_TXD_OUTER_IPLEN_SHIFT) |
							 (((skb_inner_mac_header(skb) -
							    skb_transport_header(skb)) >> 1) <<
							  WX_TXD_TUNNEL_LEN_SHIFT);
				break;
			case IPPROTO_IPIP:
				tunhdr_eiplen_tunlen = (((char *)inner_ip_hdr(skb) -
							(char *)ip_hdr(skb)) >> 2) <<
							WX_TXD_OUTER_IPLEN_SHIFT;
				break;
			default:
				break;
			}

		} else {
			network_hdr.raw = skb_network_header(skb);
			transport_hdr.raw = skb_transport_header(skb);
			vlan_macip_lens = skb_network_offset(skb) <<
					  WX_TXD_MACLEN_SHIFT;
		}

		switch (network_hdr.ipv4->version) {
		case IPVERSION:
			vlan_macip_lens |= (transport_hdr.raw - network_hdr.raw) >> 1;
			l4_prot = network_hdr.ipv4->protocol;
			break;
		case 6:
			vlan_macip_lens |= (transport_hdr.raw - network_hdr.raw) >> 1;
			l4_prot = network_hdr.ipv6->nexthdr;
			break;
		default:
			break;
		}

		switch (l4_prot) {
		case IPPROTO_TCP:
		mss_l4len_idx = (transport_hdr.tcphdr->doff * 4) <<
				WX_TXD_L4LEN_SHIFT;
			break;
		case IPPROTO_SCTP:
			mss_l4len_idx = sizeof(struct sctphdr) <<
					WX_TXD_L4LEN_SHIFT;
			break;
		case IPPROTO_UDP:
			mss_l4len_idx = sizeof(struct udphdr) <<
					WX_TXD_L4LEN_SHIFT;
			break;
		default:
			break;
		}

		/* update TX checksum flag */
		first->tx_flags |= WX_TX_FLAGS_CSUM;
	}
	first->tx_flags |= WX_TX_FLAGS_CC;
	/* vlan_macip_lens: MACLEN, VLAN tag */
	vlan_macip_lens |= first->tx_flags & WX_TX_FLAGS_VLAN_MASK;

	type_tucmd = ptype << 24;
	if (skb->vlan_proto == htons(ETH_P_8021AD) &&
	    netdev->features & NETIF_F_HW_VLAN_STAG_TX)
		type_tucmd |= WX_SET_FLAG(first->tx_flags,
					  WX_TX_FLAGS_HW_VLAN,
					  0x1 << WX_TXD_TAG_TPID_SEL_SHIFT);
	wx_tx_ctxtdesc(tx_ring, vlan_macip_lens, tunhdr_eiplen_tunlen,
		       type_tucmd, mss_l4len_idx);
}

static netdev_tx_t wx_xmit_frame_ring(struct sk_buff *skb,
				      struct wx_ring *tx_ring)
{
	u16 count = TXD_USE_COUNT(skb_headlen(skb));
	struct wx_tx_buffer *first;
	u8 hdr_len = 0, ptype;
	unsigned short f;
	u32 tx_flags = 0;
	int tso;

	/* need: 1 descriptor per page * PAGE_SIZE/WX_MAX_DATA_PER_TXD,
	 *       + 1 desc for skb_headlen/WX_MAX_DATA_PER_TXD,
	 *       + 2 desc gap to keep tail from touching head,
	 *       + 1 desc for context descriptor,
	 * otherwise try next time
	 */
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
		count += TXD_USE_COUNT(skb_frag_size(&skb_shinfo(skb)->
						     frags[f]));

	if (wx_maybe_stop_tx(tx_ring, count + 3))
		return NETDEV_TX_BUSY;

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_buffer_info[tx_ring->next_to_use];
	first->skb = skb;
	first->bytecount = skb->len;
	first->gso_segs = 1;

	/* if we have a HW VLAN tag being added default to the HW one */
	if (skb_vlan_tag_present(skb)) {
		tx_flags |= skb_vlan_tag_get(skb) << WX_TX_FLAGS_VLAN_SHIFT;
		tx_flags |= WX_TX_FLAGS_HW_VLAN;
	}

	/* record initial flags and protocol */
	first->tx_flags = tx_flags;
	first->protocol = vlan_get_protocol(skb);

	ptype = wx_encode_tx_desc_ptype(first);

	tso = wx_tso(tx_ring, first, &hdr_len, ptype);
	if (tso < 0)
		goto out_drop;
	else if (!tso)
		wx_tx_csum(tx_ring, first, ptype);
	wx_tx_map(tx_ring, first, hdr_len);

	return NETDEV_TX_OK;
out_drop:
	dev_kfree_skb_any(first->skb);
	first->skb = NULL;

	return NETDEV_TX_OK;
}

netdev_tx_t wx_xmit_frame(struct sk_buff *skb,
			  struct net_device *netdev)
{
	unsigned int r_idx = skb->queue_mapping;
	struct wx *wx = netdev_priv(netdev);
	struct wx_ring *tx_ring;

	if (!netif_carrier_ok(netdev)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* The minimum packet size for olinfo paylen is 17 so pad the skb
	 * in order to meet this minimum size requirement.
	 */
	if (skb_put_padto(skb, 17))
		return NETDEV_TX_OK;

	if (r_idx >= wx->num_tx_queues)
		r_idx = r_idx % wx->num_tx_queues;
	tx_ring = wx->tx_ring[r_idx];

	return wx_xmit_frame_ring(skb, tx_ring);
}
EXPORT_SYMBOL(wx_xmit_frame);

void wx_napi_enable_all(struct wx *wx)
{
	struct wx_q_vector *q_vector;
	int q_idx;

	for (q_idx = 0; q_idx < wx->num_q_vectors; q_idx++) {
		q_vector = wx->q_vector[q_idx];
		napi_enable(&q_vector->napi);
	}
}
EXPORT_SYMBOL(wx_napi_enable_all);

void wx_napi_disable_all(struct wx *wx)
{
	struct wx_q_vector *q_vector;
	int q_idx;

	for (q_idx = 0; q_idx < wx->num_q_vectors; q_idx++) {
		q_vector = wx->q_vector[q_idx];
		napi_disable(&q_vector->napi);
	}
}
EXPORT_SYMBOL(wx_napi_disable_all);

/**
 * wx_set_rss_queues: Allocate queues for RSS
 * @wx: board private structure to initialize
 *
 * This is our "base" multiqueue mode.  RSS (Receive Side Scaling) will try
 * to allocate one Rx queue per CPU, and if available, one Tx queue per CPU.
 *
 **/
static void wx_set_rss_queues(struct wx *wx)
{
	wx->num_rx_queues = wx->mac.max_rx_queues;
	wx->num_tx_queues = wx->mac.max_tx_queues;
}

static void wx_set_num_queues(struct wx *wx)
{
	/* Start with base case */
	wx->num_rx_queues = 1;
	wx->num_tx_queues = 1;
	wx->queues_per_pool = 1;

	wx_set_rss_queues(wx);
}

/**
 * wx_acquire_msix_vectors - acquire MSI-X vectors
 * @wx: board private structure
 *
 * Attempts to acquire a suitable range of MSI-X vector interrupts. Will
 * return a negative error code if unable to acquire MSI-X vectors for any
 * reason.
 */
static int wx_acquire_msix_vectors(struct wx *wx)
{
	struct irq_affinity affd = {0, };
	int nvecs, i;

	nvecs = min_t(int, num_online_cpus(), wx->mac.max_msix_vectors);

	wx->msix_entries = kcalloc(nvecs,
				   sizeof(struct msix_entry),
				   GFP_KERNEL);
	if (!wx->msix_entries)
		return -ENOMEM;

	nvecs = pci_alloc_irq_vectors_affinity(wx->pdev, nvecs,
					       nvecs,
					       PCI_IRQ_MSIX | PCI_IRQ_AFFINITY,
					       &affd);
	if (nvecs < 0) {
		wx_err(wx, "Failed to allocate MSI-X interrupts. Err: %d\n", nvecs);
		kfree(wx->msix_entries);
		wx->msix_entries = NULL;
		return nvecs;
	}

	for (i = 0; i < nvecs; i++) {
		wx->msix_entries[i].entry = i;
		wx->msix_entries[i].vector = pci_irq_vector(wx->pdev, i);
	}

	/* one for msix_other */
	nvecs -= 1;
	wx->num_q_vectors = nvecs;
	wx->num_rx_queues = nvecs;
	wx->num_tx_queues = nvecs;

	return 0;
}

/**
 * wx_set_interrupt_capability - set MSI-X or MSI if supported
 * @wx: board private structure to initialize
 *
 * Attempt to configure the interrupts using the best available
 * capabilities of the hardware and the kernel.
 **/
static int wx_set_interrupt_capability(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;
	int nvecs, ret;

	/* We will try to get MSI-X interrupts first */
	ret = wx_acquire_msix_vectors(wx);
	if (ret == 0 || (ret == -ENOMEM))
		return ret;

	wx->num_rx_queues = 1;
	wx->num_tx_queues = 1;
	wx->num_q_vectors = 1;

	/* minmum one for queue, one for misc*/
	nvecs = 1;
	nvecs = pci_alloc_irq_vectors(pdev, nvecs,
				      nvecs, PCI_IRQ_MSI | PCI_IRQ_LEGACY);
	if (nvecs == 1) {
		if (pdev->msi_enabled)
			wx_err(wx, "Fallback to MSI.\n");
		else
			wx_err(wx, "Fallback to LEGACY.\n");
	} else {
		wx_err(wx, "Failed to allocate MSI/LEGACY interrupts. Error: %d\n", nvecs);
		return nvecs;
	}

	pdev->irq = pci_irq_vector(pdev, 0);

	return 0;
}

/**
 * wx_cache_ring_rss - Descriptor ring to register mapping for RSS
 * @wx: board private structure to initialize
 *
 * Cache the descriptor ring offsets for RSS, ATR, FCoE, and SR-IOV.
 *
 **/
static void wx_cache_ring_rss(struct wx *wx)
{
	u16 i;

	for (i = 0; i < wx->num_rx_queues; i++)
		wx->rx_ring[i]->reg_idx = i;

	for (i = 0; i < wx->num_tx_queues; i++)
		wx->tx_ring[i]->reg_idx = i;
}

static void wx_add_ring(struct wx_ring *ring, struct wx_ring_container *head)
{
	ring->next = head->ring;
	head->ring = ring;
	head->count++;
}

/**
 * wx_alloc_q_vector - Allocate memory for a single interrupt vector
 * @wx: board private structure to initialize
 * @v_count: q_vectors allocated on wx, used for ring interleaving
 * @v_idx: index of vector in wx struct
 * @txr_count: total number of Tx rings to allocate
 * @txr_idx: index of first Tx ring to allocate
 * @rxr_count: total number of Rx rings to allocate
 * @rxr_idx: index of first Rx ring to allocate
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 **/
static int wx_alloc_q_vector(struct wx *wx,
			     unsigned int v_count, unsigned int v_idx,
			     unsigned int txr_count, unsigned int txr_idx,
			     unsigned int rxr_count, unsigned int rxr_idx)
{
	struct wx_q_vector *q_vector;
	int ring_count, default_itr;
	struct wx_ring *ring;

	/* note this will allocate space for the ring structure as well! */
	ring_count = txr_count + rxr_count;

	q_vector = kzalloc(struct_size(q_vector, ring, ring_count),
			   GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	/* initialize NAPI */
	netif_napi_add(wx->netdev, &q_vector->napi,
		       wx_poll);

	/* tie q_vector and wx together */
	wx->q_vector[v_idx] = q_vector;
	q_vector->wx = wx;
	q_vector->v_idx = v_idx;
	if (cpu_online(v_idx))
		q_vector->numa_node = cpu_to_node(v_idx);

	/* initialize pointer to rings */
	ring = q_vector->ring;

	if (wx->mac.type == wx_mac_sp)
		default_itr = WX_12K_ITR;
	else
		default_itr = WX_7K_ITR;
	/* initialize ITR */
	if (txr_count && !rxr_count)
		/* tx only vector */
		q_vector->itr = wx->tx_itr_setting ?
				default_itr : wx->tx_itr_setting;
	else
		/* rx or rx/tx vector */
		q_vector->itr = wx->rx_itr_setting ?
				default_itr : wx->rx_itr_setting;

	while (txr_count) {
		/* assign generic ring traits */
		ring->dev = &wx->pdev->dev;
		ring->netdev = wx->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* update q_vector Tx values */
		wx_add_ring(ring, &q_vector->tx);

		/* apply Tx specific ring traits */
		ring->count = wx->tx_ring_count;

		ring->queue_index = txr_idx;

		/* assign ring to wx */
		wx->tx_ring[txr_idx] = ring;

		/* update count and index */
		txr_count--;
		txr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	while (rxr_count) {
		/* assign generic ring traits */
		ring->dev = &wx->pdev->dev;
		ring->netdev = wx->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* update q_vector Rx values */
		wx_add_ring(ring, &q_vector->rx);

		/* apply Rx specific ring traits */
		ring->count = wx->rx_ring_count;
		ring->queue_index = rxr_idx;

		/* assign ring to wx */
		wx->rx_ring[rxr_idx] = ring;

		/* update count and index */
		rxr_count--;
		rxr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	return 0;
}

/**
 * wx_free_q_vector - Free memory allocated for specific interrupt vector
 * @wx: board private structure to initialize
 * @v_idx: Index of vector to be freed
 *
 * This function frees the memory allocated to the q_vector.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void wx_free_q_vector(struct wx *wx, int v_idx)
{
	struct wx_q_vector *q_vector = wx->q_vector[v_idx];
	struct wx_ring *ring;

	wx_for_each_ring(ring, q_vector->tx)
		wx->tx_ring[ring->queue_index] = NULL;

	wx_for_each_ring(ring, q_vector->rx)
		wx->rx_ring[ring->queue_index] = NULL;

	wx->q_vector[v_idx] = NULL;
	netif_napi_del(&q_vector->napi);
	kfree_rcu(q_vector, rcu);
}

/**
 * wx_alloc_q_vectors - Allocate memory for interrupt vectors
 * @wx: board private structure to initialize
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int wx_alloc_q_vectors(struct wx *wx)
{
	unsigned int rxr_idx = 0, txr_idx = 0, v_idx = 0;
	unsigned int rxr_remaining = wx->num_rx_queues;
	unsigned int txr_remaining = wx->num_tx_queues;
	unsigned int q_vectors = wx->num_q_vectors;
	int rqpv, tqpv;
	int err;

	for (; v_idx < q_vectors; v_idx++) {
		rqpv = DIV_ROUND_UP(rxr_remaining, q_vectors - v_idx);
		tqpv = DIV_ROUND_UP(txr_remaining, q_vectors - v_idx);
		err = wx_alloc_q_vector(wx, q_vectors, v_idx,
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
	wx->num_tx_queues = 0;
	wx->num_rx_queues = 0;
	wx->num_q_vectors = 0;

	while (v_idx--)
		wx_free_q_vector(wx, v_idx);

	return -ENOMEM;
}

/**
 * wx_free_q_vectors - Free memory allocated for interrupt vectors
 * @wx: board private structure to initialize
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void wx_free_q_vectors(struct wx *wx)
{
	int v_idx = wx->num_q_vectors;

	wx->num_tx_queues = 0;
	wx->num_rx_queues = 0;
	wx->num_q_vectors = 0;

	while (v_idx--)
		wx_free_q_vector(wx, v_idx);
}

void wx_reset_interrupt_capability(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;

	if (!pdev->msi_enabled && !pdev->msix_enabled)
		return;

	pci_free_irq_vectors(wx->pdev);
	if (pdev->msix_enabled) {
		kfree(wx->msix_entries);
		wx->msix_entries = NULL;
	}
}
EXPORT_SYMBOL(wx_reset_interrupt_capability);

/**
 * wx_clear_interrupt_scheme - Clear the current interrupt scheme settings
 * @wx: board private structure to clear interrupt scheme on
 *
 * We go through and clear interrupt specific resources and reset the structure
 * to pre-load conditions
 **/
void wx_clear_interrupt_scheme(struct wx *wx)
{
	wx_free_q_vectors(wx);
	wx_reset_interrupt_capability(wx);
}
EXPORT_SYMBOL(wx_clear_interrupt_scheme);

int wx_init_interrupt_scheme(struct wx *wx)
{
	int ret;

	/* Number of supported queues */
	wx_set_num_queues(wx);

	/* Set interrupt mode */
	ret = wx_set_interrupt_capability(wx);
	if (ret) {
		wx_err(wx, "Allocate irq vectors for failed.\n");
		return ret;
	}

	/* Allocate memory for queues */
	ret = wx_alloc_q_vectors(wx);
	if (ret) {
		wx_err(wx, "Unable to allocate memory for queue vectors.\n");
		wx_reset_interrupt_capability(wx);
		return ret;
	}

	wx_cache_ring_rss(wx);

	return 0;
}
EXPORT_SYMBOL(wx_init_interrupt_scheme);

irqreturn_t wx_msix_clean_rings(int __always_unused irq, void *data)
{
	struct wx_q_vector *q_vector = data;

	/* EIAM disabled interrupts (on this vector) for us */
	if (q_vector->rx.ring || q_vector->tx.ring)
		napi_schedule_irqoff(&q_vector->napi);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(wx_msix_clean_rings);

void wx_free_irq(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;
	int vector;

	if (!(pdev->msix_enabled)) {
		free_irq(pdev->irq, wx);
		return;
	}

	for (vector = 0; vector < wx->num_q_vectors; vector++) {
		struct wx_q_vector *q_vector = wx->q_vector[vector];
		struct msix_entry *entry = &wx->msix_entries[vector];

		/* free only the irqs that were actually requested */
		if (!q_vector->rx.ring && !q_vector->tx.ring)
			continue;

		free_irq(entry->vector, q_vector);
	}

	if (wx->mac.type == wx_mac_em)
		free_irq(wx->msix_entries[vector].vector, wx);
}
EXPORT_SYMBOL(wx_free_irq);

/**
 * wx_setup_isb_resources - allocate interrupt status resources
 * @wx: board private structure
 *
 * Return 0 on success, negative on failure
 **/
int wx_setup_isb_resources(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;

	wx->isb_mem = dma_alloc_coherent(&pdev->dev,
					 sizeof(u32) * 4,
					 &wx->isb_dma,
					 GFP_KERNEL);
	if (!wx->isb_mem) {
		wx_err(wx, "Alloc isb_mem failed\n");
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL(wx_setup_isb_resources);

/**
 * wx_free_isb_resources - allocate all queues Rx resources
 * @wx: board private structure
 *
 * Return 0 on success, negative on failure
 **/
void wx_free_isb_resources(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;

	dma_free_coherent(&pdev->dev, sizeof(u32) * 4,
			  wx->isb_mem, wx->isb_dma);
	wx->isb_mem = NULL;
}
EXPORT_SYMBOL(wx_free_isb_resources);

u32 wx_misc_isb(struct wx *wx, enum wx_isb_idx idx)
{
	u32 cur_tag = 0;

	cur_tag = wx->isb_mem[WX_ISB_HEADER];
	wx->isb_tag[idx] = cur_tag;

	return (__force u32)cpu_to_le32(wx->isb_mem[idx]);
}
EXPORT_SYMBOL(wx_misc_isb);

/**
 * wx_set_ivar - set the IVAR registers, mapping interrupt causes to vectors
 * @wx: pointer to wx struct
 * @direction: 0 for Rx, 1 for Tx, -1 for other causes
 * @queue: queue to map the corresponding interrupt to
 * @msix_vector: the vector to map to the corresponding queue
 *
 **/
static void wx_set_ivar(struct wx *wx, s8 direction,
			u16 queue, u16 msix_vector)
{
	u32 ivar, index;

	if (direction == -1) {
		/* other causes */
		msix_vector |= WX_PX_IVAR_ALLOC_VAL;
		index = 0;
		ivar = rd32(wx, WX_PX_MISC_IVAR);
		ivar &= ~(0xFF << index);
		ivar |= (msix_vector << index);
		wr32(wx, WX_PX_MISC_IVAR, ivar);
	} else {
		/* tx or rx causes */
		msix_vector |= WX_PX_IVAR_ALLOC_VAL;
		index = ((16 * (queue & 1)) + (8 * direction));
		ivar = rd32(wx, WX_PX_IVAR(queue >> 1));
		ivar &= ~(0xFF << index);
		ivar |= (msix_vector << index);
		wr32(wx, WX_PX_IVAR(queue >> 1), ivar);
	}
}

/**
 * wx_write_eitr - write EITR register in hardware specific way
 * @q_vector: structure containing interrupt and ring information
 *
 * This function is made to be called by ethtool and by the driver
 * when it needs to update EITR registers at runtime.  Hardware
 * specific quirks/differences are taken care of here.
 */
static void wx_write_eitr(struct wx_q_vector *q_vector)
{
	struct wx *wx = q_vector->wx;
	int v_idx = q_vector->v_idx;
	u32 itr_reg;

	if (wx->mac.type == wx_mac_sp)
		itr_reg = q_vector->itr & WX_SP_MAX_EITR;
	else
		itr_reg = q_vector->itr & WX_EM_MAX_EITR;

	itr_reg |= WX_PX_ITR_CNT_WDIS;

	wr32(wx, WX_PX_ITR(v_idx), itr_reg);
}

/**
 * wx_configure_vectors - Configure vectors for hardware
 * @wx: board private structure
 *
 * wx_configure_vectors sets up the hardware to properly generate MSI-X/MSI/LEGACY
 * interrupts.
 **/
void wx_configure_vectors(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;
	u32 eitrsel = 0;
	u16 v_idx;

	if (pdev->msix_enabled) {
		/* Populate MSIX to EITR Select */
		wr32(wx, WX_PX_ITRSEL, eitrsel);
		/* use EIAM to auto-mask when MSI-X interrupt is asserted
		 * this saves a register write for every interrupt
		 */
		wr32(wx, WX_PX_GPIE, WX_PX_GPIE_MODEL);
	} else {
		/* legacy interrupts, use EIAM to auto-mask when reading EICR,
		 * specifically only auto mask tx and rx interrupts.
		 */
		wr32(wx, WX_PX_GPIE, 0);
	}

	/* Populate the IVAR table and set the ITR values to the
	 * corresponding register.
	 */
	for (v_idx = 0; v_idx < wx->num_q_vectors; v_idx++) {
		struct wx_q_vector *q_vector = wx->q_vector[v_idx];
		struct wx_ring *ring;

		wx_for_each_ring(ring, q_vector->rx)
			wx_set_ivar(wx, 0, ring->reg_idx, v_idx);

		wx_for_each_ring(ring, q_vector->tx)
			wx_set_ivar(wx, 1, ring->reg_idx, v_idx);

		wx_write_eitr(q_vector);
	}

	wx_set_ivar(wx, -1, 0, v_idx);
	if (pdev->msix_enabled)
		wr32(wx, WX_PX_ITR(v_idx), 1950);
}
EXPORT_SYMBOL(wx_configure_vectors);

/**
 * wx_clean_rx_ring - Free Rx Buffers per Queue
 * @rx_ring: ring to free buffers from
 **/
static void wx_clean_rx_ring(struct wx_ring *rx_ring)
{
	struct wx_rx_buffer *rx_buffer;
	u16 i = rx_ring->next_to_clean;

	rx_buffer = &rx_ring->rx_buffer_info[i];

	/* Free all the Rx ring sk_buffs */
	while (i != rx_ring->next_to_alloc) {
		if (rx_buffer->skb) {
			struct sk_buff *skb = rx_buffer->skb;

			if (WX_CB(skb)->page_released)
				page_pool_put_full_page(rx_ring->page_pool, rx_buffer->page, false);

			dev_kfree_skb(skb);
		}

		/* Invalidate cache lines that may have been written to by
		 * device so that we avoid corrupting memory.
		 */
		dma_sync_single_range_for_cpu(rx_ring->dev,
					      rx_buffer->dma,
					      rx_buffer->page_offset,
					      WX_RX_BUFSZ,
					      DMA_FROM_DEVICE);

		/* free resources associated with mapping */
		page_pool_put_full_page(rx_ring->page_pool, rx_buffer->page, false);
		__page_frag_cache_drain(rx_buffer->page,
					rx_buffer->pagecnt_bias);

		i++;
		rx_buffer++;
		if (i == rx_ring->count) {
			i = 0;
			rx_buffer = rx_ring->rx_buffer_info;
		}
	}

	rx_ring->next_to_alloc = 0;
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
}

/**
 * wx_clean_all_rx_rings - Free Rx Buffers for all queues
 * @wx: board private structure
 **/
void wx_clean_all_rx_rings(struct wx *wx)
{
	int i;

	for (i = 0; i < wx->num_rx_queues; i++)
		wx_clean_rx_ring(wx->rx_ring[i]);
}
EXPORT_SYMBOL(wx_clean_all_rx_rings);

/**
 * wx_free_rx_resources - Free Rx Resources
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
static void wx_free_rx_resources(struct wx_ring *rx_ring)
{
	wx_clean_rx_ring(rx_ring);
	kvfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!rx_ring->desc)
		return;

	dma_free_coherent(rx_ring->dev, rx_ring->size,
			  rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;

	if (rx_ring->page_pool) {
		page_pool_destroy(rx_ring->page_pool);
		rx_ring->page_pool = NULL;
	}
}

/**
 * wx_free_all_rx_resources - Free Rx Resources for All Queues
 * @wx: pointer to hardware structure
 *
 * Free all receive software resources
 **/
static void wx_free_all_rx_resources(struct wx *wx)
{
	int i;

	for (i = 0; i < wx->num_rx_queues; i++)
		wx_free_rx_resources(wx->rx_ring[i]);
}

/**
 * wx_clean_tx_ring - Free Tx Buffers
 * @tx_ring: ring to be cleaned
 **/
static void wx_clean_tx_ring(struct wx_ring *tx_ring)
{
	struct wx_tx_buffer *tx_buffer;
	u16 i = tx_ring->next_to_clean;

	tx_buffer = &tx_ring->tx_buffer_info[i];

	while (i != tx_ring->next_to_use) {
		union wx_tx_desc *eop_desc, *tx_desc;

		/* Free all the Tx ring sk_buffs */
		dev_kfree_skb_any(tx_buffer->skb);

		/* unmap skb header data */
		dma_unmap_single(tx_ring->dev,
				 dma_unmap_addr(tx_buffer, dma),
				 dma_unmap_len(tx_buffer, len),
				 DMA_TO_DEVICE);

		/* check for eop_desc to determine the end of the packet */
		eop_desc = tx_buffer->next_to_watch;
		tx_desc = WX_TX_DESC(tx_ring, i);

		/* unmap remaining buffers */
		while (tx_desc != eop_desc) {
			tx_buffer++;
			tx_desc++;
			i++;
			if (unlikely(i == tx_ring->count)) {
				i = 0;
				tx_buffer = tx_ring->tx_buffer_info;
				tx_desc = WX_TX_DESC(tx_ring, 0);
			}

			/* unmap any remaining paged data */
			if (dma_unmap_len(tx_buffer, len))
				dma_unmap_page(tx_ring->dev,
					       dma_unmap_addr(tx_buffer, dma),
					       dma_unmap_len(tx_buffer, len),
					       DMA_TO_DEVICE);
		}

		/* move us one more past the eop_desc for start of next pkt */
		tx_buffer++;
		i++;
		if (unlikely(i == tx_ring->count)) {
			i = 0;
			tx_buffer = tx_ring->tx_buffer_info;
		}
	}

	netdev_tx_reset_queue(wx_txring_txq(tx_ring));

	/* reset next_to_use and next_to_clean */
	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
}

/**
 * wx_clean_all_tx_rings - Free Tx Buffers for all queues
 * @wx: board private structure
 **/
void wx_clean_all_tx_rings(struct wx *wx)
{
	int i;

	for (i = 0; i < wx->num_tx_queues; i++)
		wx_clean_tx_ring(wx->tx_ring[i]);
}
EXPORT_SYMBOL(wx_clean_all_tx_rings);

/**
 * wx_free_tx_resources - Free Tx Resources per Queue
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
static void wx_free_tx_resources(struct wx_ring *tx_ring)
{
	wx_clean_tx_ring(tx_ring);
	kvfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!tx_ring->desc)
		return;

	dma_free_coherent(tx_ring->dev, tx_ring->size,
			  tx_ring->desc, tx_ring->dma);
	tx_ring->desc = NULL;
}

/**
 * wx_free_all_tx_resources - Free Tx Resources for All Queues
 * @wx: pointer to hardware structure
 *
 * Free all transmit software resources
 **/
static void wx_free_all_tx_resources(struct wx *wx)
{
	int i;

	for (i = 0; i < wx->num_tx_queues; i++)
		wx_free_tx_resources(wx->tx_ring[i]);
}

void wx_free_resources(struct wx *wx)
{
	wx_free_isb_resources(wx);
	wx_free_all_rx_resources(wx);
	wx_free_all_tx_resources(wx);
}
EXPORT_SYMBOL(wx_free_resources);

static int wx_alloc_page_pool(struct wx_ring *rx_ring)
{
	int ret = 0;

	struct page_pool_params pp_params = {
		.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.order = 0,
		.pool_size = rx_ring->size,
		.nid = dev_to_node(rx_ring->dev),
		.dev = rx_ring->dev,
		.dma_dir = DMA_FROM_DEVICE,
		.offset = 0,
		.max_len = PAGE_SIZE,
	};

	rx_ring->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(rx_ring->page_pool)) {
		ret = PTR_ERR(rx_ring->page_pool);
		rx_ring->page_pool = NULL;
	}

	return ret;
}

/**
 * wx_setup_rx_resources - allocate Rx resources (Descriptors)
 * @rx_ring: rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/
static int wx_setup_rx_resources(struct wx_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	int orig_node = dev_to_node(dev);
	int numa_node = NUMA_NO_NODE;
	int size, ret;

	size = sizeof(struct wx_rx_buffer) * rx_ring->count;

	if (rx_ring->q_vector)
		numa_node = rx_ring->q_vector->numa_node;

	rx_ring->rx_buffer_info = kvmalloc_node(size, GFP_KERNEL, numa_node);
	if (!rx_ring->rx_buffer_info)
		rx_ring->rx_buffer_info = kvmalloc(size, GFP_KERNEL);
	if (!rx_ring->rx_buffer_info)
		goto err;

	/* Round up to nearest 4K */
	rx_ring->size = rx_ring->count * sizeof(union wx_rx_desc);
	rx_ring->size = ALIGN(rx_ring->size, 4096);

	set_dev_node(dev, numa_node);
	rx_ring->desc = dma_alloc_coherent(dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);
	if (!rx_ring->desc) {
		set_dev_node(dev, orig_node);
		rx_ring->desc = dma_alloc_coherent(dev, rx_ring->size,
						   &rx_ring->dma, GFP_KERNEL);
	}

	if (!rx_ring->desc)
		goto err;

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	ret = wx_alloc_page_pool(rx_ring);
	if (ret < 0) {
		dev_err(rx_ring->dev, "Page pool creation failed: %d\n", ret);
		goto err_desc;
	}

	return 0;

err_desc:
	dma_free_coherent(dev, rx_ring->size, rx_ring->desc, rx_ring->dma);
err:
	kvfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;
	dev_err(dev, "Unable to allocate memory for the Rx descriptor ring\n");
	return -ENOMEM;
}

/**
 * wx_setup_all_rx_resources - allocate all queues Rx resources
 * @wx: pointer to hardware structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int wx_setup_all_rx_resources(struct wx *wx)
{
	int i, err = 0;

	for (i = 0; i < wx->num_rx_queues; i++) {
		err = wx_setup_rx_resources(wx->rx_ring[i]);
		if (!err)
			continue;

		wx_err(wx, "Allocation for Rx Queue %u failed\n", i);
		goto err_setup_rx;
	}

	return 0;
err_setup_rx:
	/* rewind the index freeing the rings as we go */
	while (i--)
		wx_free_rx_resources(wx->rx_ring[i]);
	return err;
}

/**
 * wx_setup_tx_resources - allocate Tx resources (Descriptors)
 * @tx_ring: tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/
static int wx_setup_tx_resources(struct wx_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int orig_node = dev_to_node(dev);
	int numa_node = NUMA_NO_NODE;
	int size;

	size = sizeof(struct wx_tx_buffer) * tx_ring->count;

	if (tx_ring->q_vector)
		numa_node = tx_ring->q_vector->numa_node;

	tx_ring->tx_buffer_info = kvmalloc_node(size, GFP_KERNEL, numa_node);
	if (!tx_ring->tx_buffer_info)
		tx_ring->tx_buffer_info = kvmalloc(size, GFP_KERNEL);
	if (!tx_ring->tx_buffer_info)
		goto err;

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(union wx_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);

	set_dev_node(dev, numa_node);
	tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);
	if (!tx_ring->desc) {
		set_dev_node(dev, orig_node);
		tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size,
						   &tx_ring->dma, GFP_KERNEL);
	}

	if (!tx_ring->desc)
		goto err;

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	return 0;

err:
	kvfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;
	dev_err(dev, "Unable to allocate memory for the Tx descriptor ring\n");
	return -ENOMEM;
}

/**
 * wx_setup_all_tx_resources - allocate all queues Tx resources
 * @wx: pointer to private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int wx_setup_all_tx_resources(struct wx *wx)
{
	int i, err = 0;

	for (i = 0; i < wx->num_tx_queues; i++) {
		err = wx_setup_tx_resources(wx->tx_ring[i]);
		if (!err)
			continue;

		wx_err(wx, "Allocation for Tx Queue %u failed\n", i);
		goto err_setup_tx;
	}

	return 0;
err_setup_tx:
	/* rewind the index freeing the rings as we go */
	while (i--)
		wx_free_tx_resources(wx->tx_ring[i]);
	return err;
}

int wx_setup_resources(struct wx *wx)
{
	int err;

	/* allocate transmit descriptors */
	err = wx_setup_all_tx_resources(wx);
	if (err)
		return err;

	/* allocate receive descriptors */
	err = wx_setup_all_rx_resources(wx);
	if (err)
		goto err_free_tx;

	err = wx_setup_isb_resources(wx);
	if (err)
		goto err_free_rx;

	return 0;

err_free_rx:
	wx_free_all_rx_resources(wx);
err_free_tx:
	wx_free_all_tx_resources(wx);

	return err;
}
EXPORT_SYMBOL(wx_setup_resources);

/**
 * wx_get_stats64 - Get System Network Statistics
 * @netdev: network interface device structure
 * @stats: storage space for 64bit statistics
 */
void wx_get_stats64(struct net_device *netdev,
		    struct rtnl_link_stats64 *stats)
{
	struct wx *wx = netdev_priv(netdev);
	int i;

	rcu_read_lock();
	for (i = 0; i < wx->num_rx_queues; i++) {
		struct wx_ring *ring = READ_ONCE(wx->rx_ring[i]);
		u64 bytes, packets;
		unsigned int start;

		if (ring) {
			do {
				start = u64_stats_fetch_begin(&ring->syncp);
				packets = ring->stats.packets;
				bytes   = ring->stats.bytes;
			} while (u64_stats_fetch_retry(&ring->syncp, start));
			stats->rx_packets += packets;
			stats->rx_bytes   += bytes;
		}
	}

	for (i = 0; i < wx->num_tx_queues; i++) {
		struct wx_ring *ring = READ_ONCE(wx->tx_ring[i]);
		u64 bytes, packets;
		unsigned int start;

		if (ring) {
			do {
				start = u64_stats_fetch_begin(&ring->syncp);
				packets = ring->stats.packets;
				bytes   = ring->stats.bytes;
			} while (u64_stats_fetch_retry(&ring->syncp,
							   start));
			stats->tx_packets += packets;
			stats->tx_bytes   += bytes;
		}
	}

	rcu_read_unlock();
}
EXPORT_SYMBOL(wx_get_stats64);

int wx_set_features(struct net_device *netdev, netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;
	struct wx *wx = netdev_priv(netdev);

	if (changed & NETIF_F_RXHASH)
		wr32m(wx, WX_RDB_RA_CTL, WX_RDB_RA_CTL_RSS_EN,
		      WX_RDB_RA_CTL_RSS_EN);
	else
		wr32m(wx, WX_RDB_RA_CTL, WX_RDB_RA_CTL_RSS_EN, 0);

	if (changed &
	    (NETIF_F_HW_VLAN_CTAG_RX |
	     NETIF_F_HW_VLAN_STAG_RX))
		wx_set_rx_mode(netdev);

	return 1;
}
EXPORT_SYMBOL(wx_set_features);

MODULE_LICENSE("GPL");
