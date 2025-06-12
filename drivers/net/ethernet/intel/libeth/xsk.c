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
