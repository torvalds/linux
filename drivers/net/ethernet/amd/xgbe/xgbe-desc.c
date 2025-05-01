// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause)
/*
 * Copyright (c) 2014-2025, Advanced Micro Devices, Inc.
 * Copyright (c) 2014, Synopsys, Inc.
 * All rights reserved
 */

#include "xgbe.h"
#include "xgbe-common.h"

static void xgbe_unmap_rdata(struct xgbe_prv_data *, struct xgbe_ring_data *);

static void xgbe_free_ring(struct xgbe_prv_data *pdata,
			   struct xgbe_ring *ring)
{
	struct xgbe_ring_data *rdata;
	unsigned int i;

	if (!ring)
		return;

	if (ring->rdata) {
		for (i = 0; i < ring->rdesc_count; i++) {
			rdata = XGBE_GET_DESC_DATA(ring, i);
			xgbe_unmap_rdata(pdata, rdata);
		}

		kfree(ring->rdata);
		ring->rdata = NULL;
	}

	if (ring->rx_hdr_pa.pages) {
		dma_unmap_page(pdata->dev, ring->rx_hdr_pa.pages_dma,
			       ring->rx_hdr_pa.pages_len, DMA_FROM_DEVICE);
		put_page(ring->rx_hdr_pa.pages);

		ring->rx_hdr_pa.pages = NULL;
		ring->rx_hdr_pa.pages_len = 0;
		ring->rx_hdr_pa.pages_offset = 0;
		ring->rx_hdr_pa.pages_dma = 0;
	}

	if (ring->rx_buf_pa.pages) {
		dma_unmap_page(pdata->dev, ring->rx_buf_pa.pages_dma,
			       ring->rx_buf_pa.pages_len, DMA_FROM_DEVICE);
		put_page(ring->rx_buf_pa.pages);

		ring->rx_buf_pa.pages = NULL;
		ring->rx_buf_pa.pages_len = 0;
		ring->rx_buf_pa.pages_offset = 0;
		ring->rx_buf_pa.pages_dma = 0;
	}

	if (ring->rdesc) {
		dma_free_coherent(pdata->dev,
				  (sizeof(struct xgbe_ring_desc) *
				   ring->rdesc_count),
				  ring->rdesc, ring->rdesc_dma);
		ring->rdesc = NULL;
	}
}

static void xgbe_free_ring_resources(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	DBGPR("-->xgbe_free_ring_resources\n");

	for (i = 0; i < pdata->channel_count; i++) {
		channel = pdata->channel[i];
		xgbe_free_ring(pdata, channel->tx_ring);
		xgbe_free_ring(pdata, channel->rx_ring);
	}

	DBGPR("<--xgbe_free_ring_resources\n");
}

static void *xgbe_alloc_node(size_t size, int node)
{
	void *mem;

	mem = kzalloc_node(size, GFP_KERNEL, node);
	if (!mem)
		mem = kzalloc(size, GFP_KERNEL);

	return mem;
}

static void *xgbe_dma_alloc_node(struct device *dev, size_t size,
				 dma_addr_t *dma, int node)
{
	void *mem;
	int cur_node = dev_to_node(dev);

	set_dev_node(dev, node);
	mem = dma_alloc_coherent(dev, size, dma, GFP_KERNEL);
	set_dev_node(dev, cur_node);

	if (!mem)
		mem = dma_alloc_coherent(dev, size, dma, GFP_KERNEL);

	return mem;
}

static int xgbe_init_ring(struct xgbe_prv_data *pdata,
			  struct xgbe_ring *ring, unsigned int rdesc_count)
{
	size_t size;

	if (!ring)
		return 0;

	/* Descriptors */
	size = rdesc_count * sizeof(struct xgbe_ring_desc);

	ring->rdesc_count = rdesc_count;
	ring->rdesc = xgbe_dma_alloc_node(pdata->dev, size, &ring->rdesc_dma,
					  ring->node);
	if (!ring->rdesc)
		return -ENOMEM;

	/* Descriptor information */
	size = rdesc_count * sizeof(struct xgbe_ring_data);

	ring->rdata = xgbe_alloc_node(size, ring->node);
	if (!ring->rdata)
		return -ENOMEM;

	netif_dbg(pdata, drv, pdata->netdev,
		  "rdesc=%p, rdesc_dma=%pad, rdata=%p, node=%d\n",
		  ring->rdesc, &ring->rdesc_dma, ring->rdata, ring->node);

	return 0;
}

static int xgbe_alloc_ring_resources(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;
	int ret;

	for (i = 0; i < pdata->channel_count; i++) {
		channel = pdata->channel[i];
		netif_dbg(pdata, drv, pdata->netdev, "%s - Tx ring:\n",
			  channel->name);

		ret = xgbe_init_ring(pdata, channel->tx_ring,
				     pdata->tx_desc_count);
		if (ret) {
			netdev_alert(pdata->netdev,
				     "error initializing Tx ring\n");
			goto err_ring;
		}

		netif_dbg(pdata, drv, pdata->netdev, "%s - Rx ring:\n",
			  channel->name);

		ret = xgbe_init_ring(pdata, channel->rx_ring,
				     pdata->rx_desc_count);
		if (ret) {
			netdev_alert(pdata->netdev,
				     "error initializing Rx ring\n");
			goto err_ring;
		}
	}

	return 0;

err_ring:
	xgbe_free_ring_resources(pdata);

	return ret;
}

static int xgbe_alloc_pages(struct xgbe_prv_data *pdata,
			    struct xgbe_page_alloc *pa, int alloc_order,
			    int node)
{
	struct page *pages = NULL;
	dma_addr_t pages_dma;
	gfp_t gfp;
	int order;

again:
	order = alloc_order;

	/* Try to obtain pages, decreasing order if necessary */
	gfp = GFP_ATOMIC | __GFP_COMP | __GFP_NOWARN;
	while (order >= 0) {
		pages = alloc_pages_node(node, gfp, order);
		if (pages)
			break;

		order--;
	}

	/* If we couldn't get local pages, try getting from anywhere */
	if (!pages && (node != NUMA_NO_NODE)) {
		node = NUMA_NO_NODE;
		goto again;
	}

	if (!pages)
		return -ENOMEM;

	/* Map the pages */
	pages_dma = dma_map_page(pdata->dev, pages, 0,
				 PAGE_SIZE << order, DMA_FROM_DEVICE);
	if (dma_mapping_error(pdata->dev, pages_dma)) {
		put_page(pages);
		return -ENOMEM;
	}

	pa->pages = pages;
	pa->pages_len = PAGE_SIZE << order;
	pa->pages_offset = 0;
	pa->pages_dma = pages_dma;

	return 0;
}

static void xgbe_set_buffer_data(struct xgbe_buffer_data *bd,
				 struct xgbe_page_alloc *pa,
				 unsigned int len)
{
	get_page(pa->pages);
	bd->pa = *pa;

	bd->dma_base = pa->pages_dma;
	bd->dma_off = pa->pages_offset;
	bd->dma_len = len;

	pa->pages_offset += len;
	if ((pa->pages_offset + len) > pa->pages_len) {
		/* This data descriptor is responsible for unmapping page(s) */
		bd->pa_unmap = *pa;

		/* Get a new allocation next time */
		pa->pages = NULL;
		pa->pages_len = 0;
		pa->pages_offset = 0;
		pa->pages_dma = 0;
	}
}

static int xgbe_map_rx_buffer(struct xgbe_prv_data *pdata,
			      struct xgbe_ring *ring,
			      struct xgbe_ring_data *rdata)
{
	int ret;

	if (!ring->rx_hdr_pa.pages) {
		ret = xgbe_alloc_pages(pdata, &ring->rx_hdr_pa, 0, ring->node);
		if (ret)
			return ret;
	}

	if (!ring->rx_buf_pa.pages) {
		ret = xgbe_alloc_pages(pdata, &ring->rx_buf_pa,
				       PAGE_ALLOC_COSTLY_ORDER, ring->node);
		if (ret)
			return ret;
	}

	/* Set up the header page info */
	if (pdata->netdev->features & NETIF_F_RXCSUM) {
		xgbe_set_buffer_data(&rdata->rx.hdr, &ring->rx_hdr_pa,
				     XGBE_SKB_ALLOC_SIZE);
	} else {
		xgbe_set_buffer_data(&rdata->rx.hdr, &ring->rx_hdr_pa,
				     pdata->rx_buf_size);
	}

	/* Set up the buffer page info */
	xgbe_set_buffer_data(&rdata->rx.buf, &ring->rx_buf_pa,
			     pdata->rx_buf_size);

	return 0;
}

static void xgbe_wrapper_tx_descriptor_init(struct xgbe_prv_data *pdata)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_channel *channel;
	struct xgbe_ring *ring;
	struct xgbe_ring_data *rdata;
	struct xgbe_ring_desc *rdesc;
	dma_addr_t rdesc_dma;
	unsigned int i, j;

	DBGPR("-->xgbe_wrapper_tx_descriptor_init\n");

	for (i = 0; i < pdata->channel_count; i++) {
		channel = pdata->channel[i];
		ring = channel->tx_ring;
		if (!ring)
			break;

		rdesc = ring->rdesc;
		rdesc_dma = ring->rdesc_dma;

		for (j = 0; j < ring->rdesc_count; j++) {
			rdata = XGBE_GET_DESC_DATA(ring, j);

			rdata->rdesc = rdesc;
			rdata->rdesc_dma = rdesc_dma;

			rdesc++;
			rdesc_dma += sizeof(struct xgbe_ring_desc);
		}

		ring->cur = 0;
		ring->dirty = 0;
		memset(&ring->tx, 0, sizeof(ring->tx));

		hw_if->tx_desc_init(channel);
	}

	DBGPR("<--xgbe_wrapper_tx_descriptor_init\n");
}

static void xgbe_wrapper_rx_descriptor_init(struct xgbe_prv_data *pdata)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_channel *channel;
	struct xgbe_ring *ring;
	struct xgbe_ring_desc *rdesc;
	struct xgbe_ring_data *rdata;
	dma_addr_t rdesc_dma;
	unsigned int i, j;

	DBGPR("-->xgbe_wrapper_rx_descriptor_init\n");

	for (i = 0; i < pdata->channel_count; i++) {
		channel = pdata->channel[i];
		ring = channel->rx_ring;
		if (!ring)
			break;

		rdesc = ring->rdesc;
		rdesc_dma = ring->rdesc_dma;

		for (j = 0; j < ring->rdesc_count; j++) {
			rdata = XGBE_GET_DESC_DATA(ring, j);

			rdata->rdesc = rdesc;
			rdata->rdesc_dma = rdesc_dma;

			if (xgbe_map_rx_buffer(pdata, ring, rdata))
				break;

			rdesc++;
			rdesc_dma += sizeof(struct xgbe_ring_desc);
		}

		ring->cur = 0;
		ring->dirty = 0;

		hw_if->rx_desc_init(channel);
	}

	DBGPR("<--xgbe_wrapper_rx_descriptor_init\n");
}

static void xgbe_unmap_rdata(struct xgbe_prv_data *pdata,
			     struct xgbe_ring_data *rdata)
{
	if (rdata->skb_dma) {
		if (rdata->mapped_as_page) {
			dma_unmap_page(pdata->dev, rdata->skb_dma,
				       rdata->skb_dma_len, DMA_TO_DEVICE);
		} else {
			dma_unmap_single(pdata->dev, rdata->skb_dma,
					 rdata->skb_dma_len, DMA_TO_DEVICE);
		}
		rdata->skb_dma = 0;
		rdata->skb_dma_len = 0;
	}

	if (rdata->skb) {
		dev_kfree_skb_any(rdata->skb);
		rdata->skb = NULL;
	}

	if (rdata->rx.hdr.pa.pages)
		put_page(rdata->rx.hdr.pa.pages);

	if (rdata->rx.hdr.pa_unmap.pages) {
		dma_unmap_page(pdata->dev, rdata->rx.hdr.pa_unmap.pages_dma,
			       rdata->rx.hdr.pa_unmap.pages_len,
			       DMA_FROM_DEVICE);
		put_page(rdata->rx.hdr.pa_unmap.pages);
	}

	if (rdata->rx.buf.pa.pages)
		put_page(rdata->rx.buf.pa.pages);

	if (rdata->rx.buf.pa_unmap.pages) {
		dma_unmap_page(pdata->dev, rdata->rx.buf.pa_unmap.pages_dma,
			       rdata->rx.buf.pa_unmap.pages_len,
			       DMA_FROM_DEVICE);
		put_page(rdata->rx.buf.pa_unmap.pages);
	}

	memset(&rdata->tx, 0, sizeof(rdata->tx));
	memset(&rdata->rx, 0, sizeof(rdata->rx));

	rdata->mapped_as_page = 0;

	if (rdata->state_saved) {
		rdata->state_saved = 0;
		rdata->state.skb = NULL;
		rdata->state.len = 0;
		rdata->state.error = 0;
	}
}

static int xgbe_map_tx_skb(struct xgbe_channel *channel, struct sk_buff *skb)
{
	struct xgbe_prv_data *pdata = channel->pdata;
	struct xgbe_ring *ring = channel->tx_ring;
	struct xgbe_ring_data *rdata;
	struct xgbe_packet_data *packet;
	skb_frag_t *frag;
	dma_addr_t skb_dma;
	unsigned int start_index, cur_index;
	unsigned int offset, tso, vlan, datalen, len;
	unsigned int i;

	DBGPR("-->xgbe_map_tx_skb: cur = %d\n", ring->cur);

	offset = 0;
	start_index = ring->cur;
	cur_index = ring->cur;

	packet = &ring->packet_data;
	packet->rdesc_count = 0;
	packet->length = 0;

	tso = XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			     TSO_ENABLE);
	vlan = XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			      VLAN_CTAG);

	/* Save space for a context descriptor if needed */
	if ((tso && (packet->mss != ring->tx.cur_mss)) ||
	    (vlan && (packet->vlan_ctag != ring->tx.cur_vlan_ctag)))
		cur_index++;
	rdata = XGBE_GET_DESC_DATA(ring, cur_index);

	if (tso) {
		/* Map the TSO header */
		skb_dma = dma_map_single(pdata->dev, skb->data,
					 packet->header_len, DMA_TO_DEVICE);
		if (dma_mapping_error(pdata->dev, skb_dma)) {
			netdev_alert(pdata->netdev, "dma_map_single failed\n");
			goto err_out;
		}
		rdata->skb_dma = skb_dma;
		rdata->skb_dma_len = packet->header_len;
		netif_dbg(pdata, tx_queued, pdata->netdev,
			  "skb header: index=%u, dma=%pad, len=%u\n",
			  cur_index, &skb_dma, packet->header_len);

		offset = packet->header_len;

		packet->length += packet->header_len;

		cur_index++;
		rdata = XGBE_GET_DESC_DATA(ring, cur_index);
	}

	/* Map the (remainder of the) packet */
	for (datalen = skb_headlen(skb) - offset; datalen; ) {
		len = min_t(unsigned int, datalen, XGBE_TX_MAX_BUF_SIZE);

		skb_dma = dma_map_single(pdata->dev, skb->data + offset, len,
					 DMA_TO_DEVICE);
		if (dma_mapping_error(pdata->dev, skb_dma)) {
			netdev_alert(pdata->netdev, "dma_map_single failed\n");
			goto err_out;
		}
		rdata->skb_dma = skb_dma;
		rdata->skb_dma_len = len;
		netif_dbg(pdata, tx_queued, pdata->netdev,
			  "skb data: index=%u, dma=%pad, len=%u\n",
			  cur_index, &skb_dma, len);

		datalen -= len;
		offset += len;

		packet->length += len;

		cur_index++;
		rdata = XGBE_GET_DESC_DATA(ring, cur_index);
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		netif_dbg(pdata, tx_queued, pdata->netdev,
			  "mapping frag %u\n", i);

		frag = &skb_shinfo(skb)->frags[i];
		offset = 0;

		for (datalen = skb_frag_size(frag); datalen; ) {
			len = min_t(unsigned int, datalen,
				    XGBE_TX_MAX_BUF_SIZE);

			skb_dma = skb_frag_dma_map(pdata->dev, frag, offset,
						   len, DMA_TO_DEVICE);
			if (dma_mapping_error(pdata->dev, skb_dma)) {
				netdev_alert(pdata->netdev,
					     "skb_frag_dma_map failed\n");
				goto err_out;
			}
			rdata->skb_dma = skb_dma;
			rdata->skb_dma_len = len;
			rdata->mapped_as_page = 1;
			netif_dbg(pdata, tx_queued, pdata->netdev,
				  "skb frag: index=%u, dma=%pad, len=%u\n",
				  cur_index, &skb_dma, len);

			datalen -= len;
			offset += len;

			packet->length += len;

			cur_index++;
			rdata = XGBE_GET_DESC_DATA(ring, cur_index);
		}
	}

	/* Save the skb address in the last entry. We always have some data
	 * that has been mapped so rdata is always advanced past the last
	 * piece of mapped data - use the entry pointed to by cur_index - 1.
	 */
	rdata = XGBE_GET_DESC_DATA(ring, cur_index - 1);
	rdata->skb = skb;

	/* Save the number of descriptor entries used */
	packet->rdesc_count = cur_index - start_index;

	DBGPR("<--xgbe_map_tx_skb: count=%u\n", packet->rdesc_count);

	return packet->rdesc_count;

err_out:
	while (start_index < cur_index) {
		rdata = XGBE_GET_DESC_DATA(ring, start_index++);
		xgbe_unmap_rdata(pdata, rdata);
	}

	DBGPR("<--xgbe_map_tx_skb: count=0\n");

	return 0;
}

void xgbe_init_function_ptrs_desc(struct xgbe_desc_if *desc_if)
{
	DBGPR("-->xgbe_init_function_ptrs_desc\n");

	desc_if->alloc_ring_resources = xgbe_alloc_ring_resources;
	desc_if->free_ring_resources = xgbe_free_ring_resources;
	desc_if->map_tx_skb = xgbe_map_tx_skb;
	desc_if->map_rx_buffer = xgbe_map_rx_buffer;
	desc_if->unmap_rdata = xgbe_unmap_rdata;
	desc_if->wrapper_tx_desc_init = xgbe_wrapper_tx_descriptor_init;
	desc_if->wrapper_rx_desc_init = xgbe_wrapper_rx_descriptor_init;

	DBGPR("<--xgbe_init_function_ptrs_desc\n");
}
