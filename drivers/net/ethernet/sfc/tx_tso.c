/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2015 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/pci.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <net/ipv6.h>
#include <linux/if_ether.h>
#include <linux/highmem.h>
#include <linux/moduleparam.h>
#include <linux/cache.h>
#include "net_driver.h"
#include "efx.h"
#include "io.h"
#include "nic.h"
#include "tx.h"
#include "workarounds.h"
#include "ef10_regs.h"

/* Efx legacy TCP segmentation acceleration.
 *
 * Utilises firmware support to go faster than GSO (but not as fast as TSOv2).
 *
 * Requires TX checksum offload support.
 */

#define PTR_DIFF(p1, p2)  ((u8 *)(p1) - (u8 *)(p2))

/**
 * struct tso_state - TSO state for an SKB
 * @out_len: Remaining length in current segment
 * @seqnum: Current sequence number
 * @ipv4_id: Current IPv4 ID, host endian
 * @packet_space: Remaining space in current packet
 * @dma_addr: DMA address of current position
 * @in_len: Remaining length in current SKB fragment
 * @unmap_len: Length of SKB fragment
 * @unmap_addr: DMA address of SKB fragment
 * @protocol: Network protocol (after any VLAN header)
 * @ip_off: Offset of IP header
 * @tcp_off: Offset of TCP header
 * @header_len: Number of bytes of header
 * @ip_base_len: IPv4 tot_len or IPv6 payload_len, before TCP payload
 * @header_dma_addr: Header DMA address
 * @header_unmap_len: Header DMA mapped length
 *
 * The state used during segmentation.  It is put into this data structure
 * just to make it easy to pass into inline functions.
 */
struct tso_state {
	/* Output position */
	unsigned int out_len;
	unsigned int seqnum;
	u16 ipv4_id;
	unsigned int packet_space;

	/* Input position */
	dma_addr_t dma_addr;
	unsigned int in_len;
	unsigned int unmap_len;
	dma_addr_t unmap_addr;

	__be16 protocol;
	unsigned int ip_off;
	unsigned int tcp_off;
	unsigned int header_len;
	unsigned int ip_base_len;
	dma_addr_t header_dma_addr;
	unsigned int header_unmap_len;
};

static inline void prefetch_ptr(struct efx_tx_queue *tx_queue)
{
	unsigned int insert_ptr = efx_tx_queue_get_insert_index(tx_queue);
	char *ptr;

	ptr = (char *) (tx_queue->buffer + insert_ptr);
	prefetch(ptr);
	prefetch(ptr + 0x80);

	ptr = (char *) (((efx_qword_t *)tx_queue->txd.buf.addr) + insert_ptr);
	prefetch(ptr);
	prefetch(ptr + 0x80);
}

/**
 * efx_tx_queue_insert - push descriptors onto the TX queue
 * @tx_queue:		Efx TX queue
 * @dma_addr:		DMA address of fragment
 * @len:		Length of fragment
 * @final_buffer:	The final buffer inserted into the queue
 *
 * Push descriptors onto the TX queue.
 */
static void efx_tx_queue_insert(struct efx_tx_queue *tx_queue,
				dma_addr_t dma_addr, unsigned int len,
				struct efx_tx_buffer **final_buffer)
{
	struct efx_tx_buffer *buffer;
	unsigned int dma_len;

	EFX_WARN_ON_ONCE_PARANOID(len <= 0);

	while (1) {
		buffer = efx_tx_queue_get_insert_buffer(tx_queue);
		++tx_queue->insert_count;

		EFX_WARN_ON_ONCE_PARANOID(tx_queue->insert_count -
					  tx_queue->read_count >=
					  tx_queue->efx->txq_entries);

		buffer->dma_addr = dma_addr;

		dma_len = tx_queue->efx->type->tx_limit_len(tx_queue,
				dma_addr, len);

		/* If there's space for everything this is our last buffer. */
		if (dma_len >= len)
			break;

		buffer->len = dma_len;
		buffer->flags = EFX_TX_BUF_CONT;
		dma_addr += dma_len;
		len -= dma_len;
	}

	EFX_WARN_ON_ONCE_PARANOID(!len);
	buffer->len = len;
	*final_buffer = buffer;
}

/*
 * Verify that our various assumptions about sk_buffs and the conditions
 * under which TSO will be attempted hold true.  Return the protocol number.
 */
static __be16 efx_tso_check_protocol(struct sk_buff *skb)
{
	__be16 protocol = skb->protocol;

	EFX_WARN_ON_ONCE_PARANOID(((struct ethhdr *)skb->data)->h_proto !=
				  protocol);
	if (protocol == htons(ETH_P_8021Q)) {
		struct vlan_ethhdr *veh = (struct vlan_ethhdr *)skb->data;

		protocol = veh->h_vlan_encapsulated_proto;
	}

	if (protocol == htons(ETH_P_IP)) {
		EFX_WARN_ON_ONCE_PARANOID(ip_hdr(skb)->protocol != IPPROTO_TCP);
	} else {
		EFX_WARN_ON_ONCE_PARANOID(protocol != htons(ETH_P_IPV6));
		EFX_WARN_ON_ONCE_PARANOID(ipv6_hdr(skb)->nexthdr != NEXTHDR_TCP);
	}
	EFX_WARN_ON_ONCE_PARANOID((PTR_DIFF(tcp_hdr(skb), skb->data) +
				   (tcp_hdr(skb)->doff << 2u)) >
				  skb_headlen(skb));

	return protocol;
}

/* Parse the SKB header and initialise state. */
static int tso_start(struct tso_state *st, struct efx_nic *efx,
		     struct efx_tx_queue *tx_queue,
		     const struct sk_buff *skb)
{
	struct device *dma_dev = &efx->pci_dev->dev;
	unsigned int header_len, in_len;
	dma_addr_t dma_addr;

	st->ip_off = skb_network_header(skb) - skb->data;
	st->tcp_off = skb_transport_header(skb) - skb->data;
	header_len = st->tcp_off + (tcp_hdr(skb)->doff << 2u);
	in_len = skb_headlen(skb) - header_len;
	st->header_len = header_len;
	st->in_len = in_len;
	if (st->protocol == htons(ETH_P_IP)) {
		st->ip_base_len = st->header_len - st->ip_off;
		st->ipv4_id = ntohs(ip_hdr(skb)->id);
	} else {
		st->ip_base_len = st->header_len - st->tcp_off;
		st->ipv4_id = 0;
	}
	st->seqnum = ntohl(tcp_hdr(skb)->seq);

	EFX_WARN_ON_ONCE_PARANOID(tcp_hdr(skb)->urg);
	EFX_WARN_ON_ONCE_PARANOID(tcp_hdr(skb)->syn);
	EFX_WARN_ON_ONCE_PARANOID(tcp_hdr(skb)->rst);

	st->out_len = skb->len - header_len;

	dma_addr = dma_map_single(dma_dev, skb->data,
				  skb_headlen(skb), DMA_TO_DEVICE);
	st->header_dma_addr = dma_addr;
	st->header_unmap_len = skb_headlen(skb);
	st->dma_addr = dma_addr + header_len;
	st->unmap_len = 0;

	return unlikely(dma_mapping_error(dma_dev, dma_addr)) ? -ENOMEM : 0;
}

static int tso_get_fragment(struct tso_state *st, struct efx_nic *efx,
			    skb_frag_t *frag)
{
	st->unmap_addr = skb_frag_dma_map(&efx->pci_dev->dev, frag, 0,
					  skb_frag_size(frag), DMA_TO_DEVICE);
	if (likely(!dma_mapping_error(&efx->pci_dev->dev, st->unmap_addr))) {
		st->unmap_len = skb_frag_size(frag);
		st->in_len = skb_frag_size(frag);
		st->dma_addr = st->unmap_addr;
		return 0;
	}
	return -ENOMEM;
}


/**
 * tso_fill_packet_with_fragment - form descriptors for the current fragment
 * @tx_queue:		Efx TX queue
 * @skb:		Socket buffer
 * @st:			TSO state
 *
 * Form descriptors for the current fragment, until we reach the end
 * of fragment or end-of-packet.
 */
static void tso_fill_packet_with_fragment(struct efx_tx_queue *tx_queue,
					  const struct sk_buff *skb,
					  struct tso_state *st)
{
	struct efx_tx_buffer *buffer;
	int n;

	if (st->in_len == 0)
		return;
	if (st->packet_space == 0)
		return;

	EFX_WARN_ON_ONCE_PARANOID(st->in_len <= 0);
	EFX_WARN_ON_ONCE_PARANOID(st->packet_space <= 0);

	n = min(st->in_len, st->packet_space);

	st->packet_space -= n;
	st->out_len -= n;
	st->in_len -= n;

	efx_tx_queue_insert(tx_queue, st->dma_addr, n, &buffer);

	if (st->out_len == 0) {
		/* Transfer ownership of the skb */
		buffer->skb = skb;
		buffer->flags = EFX_TX_BUF_SKB;
	} else if (st->packet_space != 0) {
		buffer->flags = EFX_TX_BUF_CONT;
	}

	if (st->in_len == 0) {
		/* Transfer ownership of the DMA mapping */
		buffer->unmap_len = st->unmap_len;
		buffer->dma_offset = buffer->unmap_len - buffer->len;
		st->unmap_len = 0;
	}

	st->dma_addr += n;
}


#define TCP_FLAGS_OFFSET 13

/**
 * tso_start_new_packet - generate a new header and prepare for the new packet
 * @tx_queue:		Efx TX queue
 * @skb:		Socket buffer
 * @st:			TSO state
 *
 * Generate a new header and prepare for the new packet.  Return 0 on
 * success, or -%ENOMEM if failed to alloc header, or other negative error.
 */
static int tso_start_new_packet(struct efx_tx_queue *tx_queue,
				const struct sk_buff *skb,
				struct tso_state *st)
{
	struct efx_tx_buffer *buffer =
		efx_tx_queue_get_insert_buffer(tx_queue);
	bool is_last = st->out_len <= skb_shinfo(skb)->gso_size;
	u8 tcp_flags_mask, tcp_flags;

	if (!is_last) {
		st->packet_space = skb_shinfo(skb)->gso_size;
		tcp_flags_mask = 0x09; /* mask out FIN and PSH */
	} else {
		st->packet_space = st->out_len;
		tcp_flags_mask = 0x00;
	}

	if (WARN_ON(!st->header_unmap_len))
		return -EINVAL;
	/* Send the original headers with a TSO option descriptor
	 * in front
	 */
	tcp_flags = ((u8 *)tcp_hdr(skb))[TCP_FLAGS_OFFSET] & ~tcp_flags_mask;

	buffer->flags = EFX_TX_BUF_OPTION;
	buffer->len = 0;
	buffer->unmap_len = 0;
	EFX_POPULATE_QWORD_5(buffer->option,
			     ESF_DZ_TX_DESC_IS_OPT, 1,
			     ESF_DZ_TX_OPTION_TYPE,
			     ESE_DZ_TX_OPTION_DESC_TSO,
			     ESF_DZ_TX_TSO_TCP_FLAGS, tcp_flags,
			     ESF_DZ_TX_TSO_IP_ID, st->ipv4_id,
			     ESF_DZ_TX_TSO_TCP_SEQNO, st->seqnum);
	++tx_queue->insert_count;

	/* We mapped the headers in tso_start().  Unmap them
	 * when the last segment is completed.
	 */
	buffer = efx_tx_queue_get_insert_buffer(tx_queue);
	buffer->dma_addr = st->header_dma_addr;
	buffer->len = st->header_len;
	if (is_last) {
		buffer->flags = EFX_TX_BUF_CONT | EFX_TX_BUF_MAP_SINGLE;
		buffer->unmap_len = st->header_unmap_len;
		buffer->dma_offset = 0;
		/* Ensure we only unmap them once in case of a
		 * later DMA mapping error and rollback
		 */
		st->header_unmap_len = 0;
	} else {
		buffer->flags = EFX_TX_BUF_CONT;
		buffer->unmap_len = 0;
	}
	++tx_queue->insert_count;

	st->seqnum += skb_shinfo(skb)->gso_size;

	/* Linux leaves suitable gaps in the IP ID space for us to fill. */
	++st->ipv4_id;

	return 0;
}

/**
 * efx_enqueue_skb_tso - segment and transmit a TSO socket buffer
 * @tx_queue:		Efx TX queue
 * @skb:		Socket buffer
 * @data_mapped:        Did we map the data? Always set to true
 *                      by this on success.
 *
 * Context: You must hold netif_tx_lock() to call this function.
 *
 * Add socket buffer @skb to @tx_queue, doing TSO or return != 0 if
 * @skb was not enqueued.  @skb is consumed unless return value is
 * %EINVAL.
 */
int efx_enqueue_skb_tso(struct efx_tx_queue *tx_queue,
			struct sk_buff *skb,
			bool *data_mapped)
{
	struct efx_nic *efx = tx_queue->efx;
	int frag_i, rc;
	struct tso_state state;

	if (tx_queue->tso_version != 1)
		return -EINVAL;

	prefetch(skb->data);

	/* Find the packet protocol and sanity-check it */
	state.protocol = efx_tso_check_protocol(skb);

	EFX_WARN_ON_ONCE_PARANOID(tx_queue->write_count != tx_queue->insert_count);

	rc = tso_start(&state, efx, tx_queue, skb);
	if (rc)
		goto fail;

	if (likely(state.in_len == 0)) {
		/* Grab the first payload fragment. */
		EFX_WARN_ON_ONCE_PARANOID(skb_shinfo(skb)->nr_frags < 1);
		frag_i = 0;
		rc = tso_get_fragment(&state, efx,
				      skb_shinfo(skb)->frags + frag_i);
		if (rc)
			goto fail;
	} else {
		/* Payload starts in the header area. */
		frag_i = -1;
	}

	rc = tso_start_new_packet(tx_queue, skb, &state);
	if (rc)
		goto fail;

	prefetch_ptr(tx_queue);

	while (1) {
		tso_fill_packet_with_fragment(tx_queue, skb, &state);

		/* Move onto the next fragment? */
		if (state.in_len == 0) {
			if (++frag_i >= skb_shinfo(skb)->nr_frags)
				/* End of payload reached. */
				break;
			rc = tso_get_fragment(&state, efx,
					      skb_shinfo(skb)->frags + frag_i);
			if (rc)
				goto fail;
		}

		/* Start at new packet? */
		if (state.packet_space == 0) {
			rc = tso_start_new_packet(tx_queue, skb, &state);
			if (rc)
				goto fail;
		}
	}

	*data_mapped = true;

	return 0;

fail:
	if (rc == -ENOMEM)
		netif_err(efx, tx_err, efx->net_dev,
			  "Out of memory for TSO headers, or DMA mapping error\n");
	else
		netif_err(efx, tx_err, efx->net_dev, "TSO failed, rc = %d\n", rc);

	/* Free the DMA mapping we were in the process of writing out */
	if (state.unmap_len) {
		dma_unmap_page(&efx->pci_dev->dev, state.unmap_addr,
			       state.unmap_len, DMA_TO_DEVICE);
	}

	/* Free the header DMA mapping */
	if (state.header_unmap_len)
		dma_unmap_single(&efx->pci_dev->dev, state.header_dma_addr,
				 state.header_unmap_len, DMA_TO_DEVICE);

	return rc;
}
