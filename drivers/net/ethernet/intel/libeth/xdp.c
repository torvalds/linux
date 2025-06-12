// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#define DEFAULT_SYMBOL_NAMESPACE	"LIBETH_XDP"

#include <linux/export.h>

#include <net/libeth/xdp.h>

/* ``XDP_TX`` bulking */

static void __cold
libeth_xdp_tx_return_one(const struct libeth_xdp_tx_frame *frm)
{
	if (frm->len_fl & LIBETH_XDP_TX_MULTI)
		libeth_xdp_return_frags(frm->data + frm->soff, true);

	libeth_xdp_return_va(frm->data, true);
}

static void __cold
libeth_xdp_tx_return_bulk(const struct libeth_xdp_tx_frame *bq, u32 count)
{
	for (u32 i = 0; i < count; i++) {
		const struct libeth_xdp_tx_frame *frm = &bq[i];

		if (!(frm->len_fl & LIBETH_XDP_TX_FIRST))
			continue;

		libeth_xdp_tx_return_one(frm);
	}
}

static void __cold libeth_trace_xdp_exception(const struct net_device *dev,
					      const struct bpf_prog *prog,
					      u32 act)
{
	trace_xdp_exception(dev, prog, act);
}

/**
 * libeth_xdp_tx_exception - handle Tx exceptions of XDP frames
 * @bq: XDP Tx frame bulk
 * @sent: number of frames sent successfully (from this bulk)
 * @flags: internal libeth_xdp flags (.ndo_xdp_xmit etc.)
 *
 * Cold helper used by __libeth_xdp_tx_flush_bulk(), do not call directly.
 * Reports XDP Tx exceptions, frees the frames that won't be sent or adjust
 * the Tx bulk to try again later.
 */
void __cold libeth_xdp_tx_exception(struct libeth_xdp_tx_bulk *bq, u32 sent,
				    u32 flags)
{
	const struct libeth_xdp_tx_frame *pos = &bq->bulk[sent];
	u32 left = bq->count - sent;

	if (!(flags & LIBETH_XDP_TX_NDO))
		libeth_trace_xdp_exception(bq->dev, bq->prog, XDP_TX);

	if (!(flags & LIBETH_XDP_TX_DROP)) {
		memmove(bq->bulk, pos, left * sizeof(*bq->bulk));
		bq->count = left;

		return;
	}

	if (!(flags & LIBETH_XDP_TX_NDO))
		libeth_xdp_tx_return_bulk(pos, left);
	else
		libeth_xdp_xmit_return_bulk(pos, left, bq->dev);

	bq->count = 0;
}
EXPORT_SYMBOL_GPL(libeth_xdp_tx_exception);

/* .ndo_xdp_xmit() implementation */

u32 __cold libeth_xdp_xmit_return_bulk(const struct libeth_xdp_tx_frame *bq,
				       u32 count, const struct net_device *dev)
{
	u32 n = 0;

	for (u32 i = 0; i < count; i++) {
		const struct libeth_xdp_tx_frame *frm = &bq[i];
		dma_addr_t dma;

		if (frm->flags & LIBETH_XDP_TX_FIRST)
			dma = *libeth_xdp_xmit_frame_dma(frm->xdpf);
		else
			dma = dma_unmap_addr(frm, dma);

		dma_unmap_page(dev->dev.parent, dma, dma_unmap_len(frm, len),
			       DMA_TO_DEVICE);

		/* Actual xdp_frames are freed by the core */
		n += !!(frm->flags & LIBETH_XDP_TX_FIRST);
	}

	return n;
}
EXPORT_SYMBOL_GPL(libeth_xdp_xmit_return_bulk);

/* Rx polling path */

/**
 * libeth_xdp_return_buff_slow - free &libeth_xdp_buff
 * @xdp: buffer to free/return
 *
 * Slowpath version of libeth_xdp_return_buff() to be called on exceptions,
 * queue clean-ups etc., without unwanted inlining.
 */
void __cold libeth_xdp_return_buff_slow(struct libeth_xdp_buff *xdp)
{
	__libeth_xdp_return_buff(xdp, false);
}
EXPORT_SYMBOL_GPL(libeth_xdp_return_buff_slow);

MODULE_DESCRIPTION("Common Ethernet library - XDP infra");
MODULE_IMPORT_NS("LIBETH");
MODULE_LICENSE("GPL");
