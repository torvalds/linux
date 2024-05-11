// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Intel Corporation */

#include <net/libeth/rx.h>

/* Rx buffer management */

/**
 * libeth_rx_hw_len - get the actual buffer size to be passed to HW
 * @pp: &page_pool_params of the netdev to calculate the size for
 * @max_len: maximum buffer size for a single descriptor
 *
 * Return: HW-writeable length per one buffer to pass it to the HW accounting:
 * MTU the @dev has, HW required alignment, minimum and maximum allowed values,
 * and system's page size.
 */
static u32 libeth_rx_hw_len(const struct page_pool_params *pp, u32 max_len)
{
	u32 len;

	len = READ_ONCE(pp->netdev->mtu) + LIBETH_RX_LL_LEN;
	len = ALIGN(len, LIBETH_RX_BUF_STRIDE);
	len = min3(len, ALIGN_DOWN(max_len ? : U32_MAX, LIBETH_RX_BUF_STRIDE),
		   pp->max_len);

	return len;
}

/**
 * libeth_rx_fq_create - create a PP with the default libeth settings
 * @fq: buffer queue struct to fill
 * @napi: &napi_struct covering this PP (no usage outside its poll loops)
 *
 * Return: %0 on success, -%errno on failure.
 */
int libeth_rx_fq_create(struct libeth_fq *fq, struct napi_struct *napi)
{
	struct page_pool_params pp = {
		.flags		= PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.order		= LIBETH_RX_PAGE_ORDER,
		.pool_size	= fq->count,
		.nid		= fq->nid,
		.dev		= napi->dev->dev.parent,
		.netdev		= napi->dev,
		.napi		= napi,
		.dma_dir	= DMA_FROM_DEVICE,
		.offset		= LIBETH_SKB_HEADROOM,
	};
	struct libeth_fqe *fqes;
	struct page_pool *pool;

	/* HW-writeable / syncable length per one page */
	pp.max_len = LIBETH_RX_PAGE_LEN(pp.offset);

	/* HW-writeable length per buffer */
	fq->buf_len = libeth_rx_hw_len(&pp, fq->buf_len);
	/* Buffer size to allocate */
	fq->truesize = roundup_pow_of_two(SKB_HEAD_ALIGN(pp.offset +
							 fq->buf_len));

	pool = page_pool_create(&pp);
	if (IS_ERR(pool))
		return PTR_ERR(pool);

	fqes = kvcalloc_node(fq->count, sizeof(*fqes), GFP_KERNEL, fq->nid);
	if (!fqes)
		goto err_buf;

	fq->fqes = fqes;
	fq->pp = pool;

	return 0;

err_buf:
	page_pool_destroy(pool);

	return -ENOMEM;
}
EXPORT_SYMBOL_NS_GPL(libeth_rx_fq_create, LIBETH);

/**
 * libeth_rx_fq_destroy - destroy a &page_pool created by libeth
 * @fq: buffer queue to process
 */
void libeth_rx_fq_destroy(struct libeth_fq *fq)
{
	kvfree(fq->fqes);
	page_pool_destroy(fq->pp);
}
EXPORT_SYMBOL_NS_GPL(libeth_rx_fq_destroy, LIBETH);

/**
 * libeth_rx_recycle_slow - recycle a libeth page from the NAPI context
 * @page: page to recycle
 *
 * To be used on exceptions or rare cases not requiring fast inline recycling.
 */
void libeth_rx_recycle_slow(struct page *page)
{
	page_pool_recycle_direct(page->pp, page);
}
EXPORT_SYMBOL_NS_GPL(libeth_rx_recycle_slow, LIBETH);

/* Converting abstract packet type numbers into a software structure with
 * the packet parameters to do O(1) lookup on Rx.
 */

static const u16 libeth_rx_pt_xdp_oip[] = {
	[LIBETH_RX_PT_OUTER_L2]		= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_OUTER_IPV4]	= XDP_RSS_L3_IPV4,
	[LIBETH_RX_PT_OUTER_IPV6]	= XDP_RSS_L3_IPV6,
};

static const u16 libeth_rx_pt_xdp_iprot[] = {
	[LIBETH_RX_PT_INNER_NONE]	= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_INNER_UDP]	= XDP_RSS_L4_UDP,
	[LIBETH_RX_PT_INNER_TCP]	= XDP_RSS_L4_TCP,
	[LIBETH_RX_PT_INNER_SCTP]	= XDP_RSS_L4_SCTP,
	[LIBETH_RX_PT_INNER_ICMP]	= XDP_RSS_L4_ICMP,
	[LIBETH_RX_PT_INNER_TIMESYNC]	= XDP_RSS_TYPE_NONE,
};

static const u16 libeth_rx_pt_xdp_pl[] = {
	[LIBETH_RX_PT_PAYLOAD_NONE]	= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_PAYLOAD_L2]	= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_PAYLOAD_L3]	= XDP_RSS_TYPE_NONE,
	[LIBETH_RX_PT_PAYLOAD_L4]	= XDP_RSS_L4,
};

/**
 * libeth_rx_pt_gen_hash_type - generate an XDP RSS hash type for a PT
 * @pt: PT structure to evaluate
 *
 * Generates ```hash_type``` field with XDP RSS type values from the parsed
 * packet parameters if they're obtained dynamically at runtime.
 */
void libeth_rx_pt_gen_hash_type(struct libeth_rx_pt *pt)
{
	pt->hash_type = 0;
	pt->hash_type |= libeth_rx_pt_xdp_oip[pt->outer_ip];
	pt->hash_type |= libeth_rx_pt_xdp_iprot[pt->inner_prot];
	pt->hash_type |= libeth_rx_pt_xdp_pl[pt->payload_layer];
}
EXPORT_SYMBOL_NS_GPL(libeth_rx_pt_gen_hash_type, LIBETH);

/* Module */

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Common Ethernet library");
MODULE_LICENSE("GPL");
