// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Intel Corporation */

#include <net/libeth/rx.h>

/* Rx buffer management */

/**
 * libeth_rx_hw_len_mtu - get the actual buffer size to be passed to HW
 * @pp: &page_pool_params of the netdev to calculate the size for
 * @max_len: maximum buffer size for a single descriptor
 *
 * Return: HW-writeable length per one buffer to pass it to the HW accounting:
 * MTU the @dev has, HW required alignment, minimum and maximum allowed values,
 * and system's page size.
 */
static u32 libeth_rx_hw_len_mtu(const struct page_pool_params *pp, u32 max_len)
{
	u32 len;

	len = READ_ONCE(pp->netdev->mtu) + LIBETH_RX_LL_LEN;
	len = ALIGN(len, LIBETH_RX_BUF_STRIDE);
	len = min3(len, ALIGN_DOWN(max_len ? : U32_MAX, LIBETH_RX_BUF_STRIDE),
		   pp->max_len);

	return len;
}

/**
 * libeth_rx_hw_len_truesize - get the short buffer size to be passed to HW
 * @pp: &page_pool_params of the netdev to calculate the size for
 * @max_len: maximum buffer size for a single descriptor
 * @truesize: desired truesize for the buffers
 *
 * Return: HW-writeable length per one buffer to pass it to the HW ignoring the
 * MTU and closest to the passed truesize. Can be used for "short" buffer
 * queues to fragment pages more efficiently.
 */
static u32 libeth_rx_hw_len_truesize(const struct page_pool_params *pp,
				     u32 max_len, u32 truesize)
{
	u32 min, len;

	min = SKB_HEAD_ALIGN(pp->offset + LIBETH_RX_BUF_STRIDE);
	truesize = clamp(roundup_pow_of_two(truesize), roundup_pow_of_two(min),
			 PAGE_SIZE << LIBETH_RX_PAGE_ORDER);

	len = SKB_WITH_OVERHEAD(truesize - pp->offset);
	len = ALIGN_DOWN(len, LIBETH_RX_BUF_STRIDE) ? : LIBETH_RX_BUF_STRIDE;
	len = min3(len, ALIGN_DOWN(max_len ? : U32_MAX, LIBETH_RX_BUF_STRIDE),
		   pp->max_len);

	return len;
}

/**
 * libeth_rx_page_pool_params - calculate params with the stack overhead
 * @fq: buffer queue to calculate the size for
 * @pp: &page_pool_params of the netdev
 *
 * Set the PP params to will all needed stack overhead (headroom, tailroom) and
 * both the HW buffer length and the truesize for all types of buffers. For
 * "short" buffers, truesize never exceeds the "wanted" one; for the rest,
 * it can be up to the page size.
 *
 * Return: true on success, false on invalid input params.
 */
static bool libeth_rx_page_pool_params(struct libeth_fq *fq,
				       struct page_pool_params *pp)
{
	pp->offset = LIBETH_SKB_HEADROOM;
	/* HW-writeable / syncable length per one page */
	pp->max_len = LIBETH_RX_PAGE_LEN(pp->offset);

	/* HW-writeable length per buffer */
	switch (fq->type) {
	case LIBETH_FQE_MTU:
		fq->buf_len = libeth_rx_hw_len_mtu(pp, fq->buf_len);
		break;
	case LIBETH_FQE_SHORT:
		fq->buf_len = libeth_rx_hw_len_truesize(pp, fq->buf_len,
							fq->truesize);
		break;
	case LIBETH_FQE_HDR:
		fq->buf_len = ALIGN(LIBETH_MAX_HEAD, LIBETH_RX_BUF_STRIDE);
		break;
	default:
		return false;
	}

	/* Buffer size to allocate */
	fq->truesize = roundup_pow_of_two(SKB_HEAD_ALIGN(pp->offset +
							 fq->buf_len));

	return true;
}

/**
 * libeth_rx_page_pool_params_zc - calculate params without the stack overhead
 * @fq: buffer queue to calculate the size for
 * @pp: &page_pool_params of the netdev
 *
 * Set the PP params to exclude the stack overhead and both the buffer length
 * and the truesize, which are equal for the data buffers. Note that this
 * requires separate header buffers to be always active and account the
 * overhead.
 * With the MTU == ``PAGE_SIZE``, this allows the kernel to enable the zerocopy
 * mode.
 *
 * Return: true on success, false on invalid input params.
 */
static bool libeth_rx_page_pool_params_zc(struct libeth_fq *fq,
					  struct page_pool_params *pp)
{
	u32 mtu, max;

	pp->offset = 0;
	pp->max_len = PAGE_SIZE << LIBETH_RX_PAGE_ORDER;

	switch (fq->type) {
	case LIBETH_FQE_MTU:
		mtu = READ_ONCE(pp->netdev->mtu);
		break;
	case LIBETH_FQE_SHORT:
		mtu = fq->truesize;
		break;
	default:
		return false;
	}

	mtu = roundup_pow_of_two(mtu);
	max = min(rounddown_pow_of_two(fq->buf_len ? : U32_MAX),
		  pp->max_len);

	fq->buf_len = clamp(mtu, LIBETH_RX_BUF_STRIDE, max);
	fq->truesize = fq->buf_len;

	return true;
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
	};
	struct libeth_fqe *fqes;
	struct page_pool *pool;
	bool ret;

	if (!fq->hsplit)
		ret = libeth_rx_page_pool_params(fq, &pp);
	else
		ret = libeth_rx_page_pool_params_zc(fq, &pp);
	if (!ret)
		return -EINVAL;

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
EXPORT_SYMBOL_NS_GPL(libeth_rx_fq_create, "LIBETH");

/**
 * libeth_rx_fq_destroy - destroy a &page_pool created by libeth
 * @fq: buffer queue to process
 */
void libeth_rx_fq_destroy(struct libeth_fq *fq)
{
	kvfree(fq->fqes);
	page_pool_destroy(fq->pp);
}
EXPORT_SYMBOL_NS_GPL(libeth_rx_fq_destroy, "LIBETH");

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
EXPORT_SYMBOL_NS_GPL(libeth_rx_recycle_slow, "LIBETH");

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
EXPORT_SYMBOL_NS_GPL(libeth_rx_pt_gen_hash_type, "LIBETH");

/* Module */

MODULE_DESCRIPTION("Common Ethernet library");
MODULE_LICENSE("GPL");
