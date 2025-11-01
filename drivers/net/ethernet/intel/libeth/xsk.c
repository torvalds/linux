// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#define DEFAULT_SYMBOL_NAMESPACE	"LIBETH_XDP"

#include <linux/export.h>

#include <net/libeth/xsk.h>

#include "priv.h"

/* ``XDP_TX`` bulking */

void __cold libeth_xsk_tx_return_bulk(const struct libeth_xdp_tx_frame *bq,
				      u32 count)
{
	for (u32 i = 0; i < count; i++)
		libeth_xsk_buff_free_slow(bq[i].xsk);
}

/* XSk TMO */

const struct xsk_tx_metadata_ops libeth_xsktmo_slow = {
	.tmo_request_checksum		= libeth_xsktmo_req_csum,
};

/* Rx polling path */

/**
 * libeth_xsk_buff_free_slow - free an XSk Rx buffer
 * @xdp: buffer to free
 *
 * Slowpath version of xsk_buff_free() to be used on exceptions, cleanups etc.
 * to avoid unwanted inlining.
 */
void libeth_xsk_buff_free_slow(struct libeth_xdp_buff *xdp)
{
	xsk_buff_free(&xdp->base);
}
EXPORT_SYMBOL_GPL(libeth_xsk_buff_free_slow);

/**
 * libeth_xsk_buff_add_frag - add frag to XSk Rx buffer
 * @head: head buffer
 * @xdp: frag buffer
 *
 * External helper used by libeth_xsk_process_buff(), do not call directly.
 * Frees both main and frag buffers on error.
 *
 * Return: main buffer with attached frag on success, %NULL on error (no space
 * for a new frag).
 */
struct libeth_xdp_buff *libeth_xsk_buff_add_frag(struct libeth_xdp_buff *head,
						 struct libeth_xdp_buff *xdp)
{
	if (!xsk_buff_add_frag(&head->base, &xdp->base))
		goto free;

	return head;

free:
	libeth_xsk_buff_free_slow(xdp);
	libeth_xsk_buff_free_slow(head);

	return NULL;
}
EXPORT_SYMBOL_GPL(libeth_xsk_buff_add_frag);

/**
 * libeth_xsk_buff_stats_frags - update onstack RQ stats with XSk frags info
 * @rs: onstack stats to update
 * @xdp: buffer to account
 *
 * External helper used by __libeth_xsk_run_pass(), do not call directly.
 * Adds buffer's frags count and total len to the onstack stats.
 */
void libeth_xsk_buff_stats_frags(struct libeth_rq_napi_stats *rs,
				 const struct libeth_xdp_buff *xdp)
{
	libeth_xdp_buff_stats_frags(rs, xdp);
}
EXPORT_SYMBOL_GPL(libeth_xsk_buff_stats_frags);

/**
 * __libeth_xsk_run_prog_slow - process the non-``XDP_REDIRECT`` verdicts
 * @xdp: buffer to process
 * @bq: Tx bulk for queueing on ``XDP_TX``
 * @act: verdict to process
 * @ret: error code if ``XDP_REDIRECT`` failed
 *
 * External helper used by __libeth_xsk_run_prog(), do not call directly.
 * ``XDP_REDIRECT`` is the most common and hottest verdict on XSk, thus
 * it is processed inline. The rest goes here for out-of-line processing,
 * together with redirect errors.
 *
 * Return: libeth_xdp XDP prog verdict.
 */
u32 __libeth_xsk_run_prog_slow(struct libeth_xdp_buff *xdp,
			       const struct libeth_xdp_tx_bulk *bq,
			       enum xdp_action act, int ret)
{
	switch (act) {
	case XDP_DROP:
		xsk_buff_free(&xdp->base);

		return LIBETH_XDP_DROP;
	case XDP_TX:
		return LIBETH_XDP_TX;
	case XDP_PASS:
		return LIBETH_XDP_PASS;
	default:
		break;
	}

	return libeth_xdp_prog_exception(bq, xdp, act, ret);
}
EXPORT_SYMBOL_GPL(__libeth_xsk_run_prog_slow);

/**
 * libeth_xsk_prog_exception - handle XDP prog exceptions on XSk
 * @xdp: buffer to process
 * @act: verdict returned by the prog
 * @ret: error code if ``XDP_REDIRECT`` failed
 *
 * Internal. Frees the buffer and, if the queue uses XSk wakeups, stop the
 * current NAPI poll when there are no free buffers left.
 *
 * Return: libeth_xdp's XDP prog verdict.
 */
u32 __cold libeth_xsk_prog_exception(struct libeth_xdp_buff *xdp,
				     enum xdp_action act, int ret)
{
	const struct xdp_buff_xsk *xsk;
	u32 __ret = LIBETH_XDP_DROP;

	if (act != XDP_REDIRECT)
		goto drop;

	xsk = container_of(&xdp->base, typeof(*xsk), xdp);
	if (xsk_uses_need_wakeup(xsk->pool) && ret == -ENOBUFS)
		__ret = LIBETH_XDP_ABORTED;

drop:
	libeth_xsk_buff_free_slow(xdp);

	return __ret;
}

/* Refill */

/**
 * libeth_xskfq_create - create an XSkFQ
 * @fq: fill queue to initialize
 *
 * Allocates the FQEs and initializes the fields used by libeth_xdp: number
 * of buffers to refill, refill threshold and buffer len.
 *
 * Return: %0 on success, -errno otherwise.
 */
int libeth_xskfq_create(struct libeth_xskfq *fq)
{
	fq->fqes = kvcalloc_node(fq->count, sizeof(*fq->fqes), GFP_KERNEL,
				 fq->nid);
	if (!fq->fqes)
		return -ENOMEM;

	fq->pending = fq->count;
	fq->thresh = libeth_xdp_queue_threshold(fq->count);
	fq->buf_len = xsk_pool_get_rx_frame_size(fq->pool);

	return 0;
}
EXPORT_SYMBOL_GPL(libeth_xskfq_create);

/**
 * libeth_xskfq_destroy - destroy an XSkFQ
 * @fq: fill queue to destroy
 *
 * Zeroes the used fields and frees the FQEs array.
 */
void libeth_xskfq_destroy(struct libeth_xskfq *fq)
{
	fq->buf_len = 0;
	fq->thresh = 0;
	fq->pending = 0;

	kvfree(fq->fqes);
}
EXPORT_SYMBOL_GPL(libeth_xskfq_destroy);

/* .ndo_xsk_wakeup */

static void libeth_xsk_napi_sched(void *info)
{
	__napi_schedule_irqoff(info);
}

/**
 * libeth_xsk_init_wakeup - initialize libeth XSk wakeup structure
 * @csd: struct to initialize
 * @napi: NAPI corresponding to this queue
 *
 * libeth_xdp uses inter-processor interrupts to perform XSk wakeups. In order
 * to do that, the corresponding CSDs must be initialized when creating the
 * queues.
 */
void libeth_xsk_init_wakeup(call_single_data_t *csd, struct napi_struct *napi)
{
	INIT_CSD(csd, libeth_xsk_napi_sched, napi);
}
EXPORT_SYMBOL_GPL(libeth_xsk_init_wakeup);

/**
 * libeth_xsk_wakeup - perform an XSk wakeup
 * @csd: CSD corresponding to the queue
 * @qid: the stack queue index
 *
 * Try to mark the NAPI as missed first, so that it could be rescheduled.
 * If it's not, schedule it on the corresponding CPU using IPIs (or directly
 * if already running on it).
 */
void libeth_xsk_wakeup(call_single_data_t *csd, u32 qid)
{
	struct napi_struct *napi = csd->info;

	if (napi_if_scheduled_mark_missed(napi) ||
	    unlikely(!napi_schedule_prep(napi)))
		return;

	if (unlikely(qid >= nr_cpu_ids))
		qid %= nr_cpu_ids;

	if (qid != raw_smp_processor_id() && cpu_online(qid))
		smp_call_function_single_async(qid, csd);
	else
		__napi_schedule(napi);
}
EXPORT_SYMBOL_GPL(libeth_xsk_wakeup);

/* Pool setup */

#define LIBETH_XSK_DMA_ATTR					\
	(DMA_ATTR_WEAK_ORDERING | DMA_ATTR_SKIP_CPU_SYNC)

/**
 * libeth_xsk_setup_pool - setup or destroy an XSk pool for a queue
 * @dev: target &net_device
 * @qid: stack queue index to configure
 * @enable: whether to enable or disable the pool
 *
 * Check that @qid is valid and then map or unmap the pool.
 *
 * Return: %0 on success, -errno otherwise.
 */
int libeth_xsk_setup_pool(struct net_device *dev, u32 qid, bool enable)
{
	struct xsk_buff_pool *pool;

	pool = xsk_get_pool_from_qid(dev, qid);
	if (!pool)
		return -EINVAL;

	if (enable)
		return xsk_pool_dma_map(pool, dev->dev.parent,
					LIBETH_XSK_DMA_ATTR);
	else
		xsk_pool_dma_unmap(pool, LIBETH_XSK_DMA_ATTR);

	return 0;
}
EXPORT_SYMBOL_GPL(libeth_xsk_setup_pool);
