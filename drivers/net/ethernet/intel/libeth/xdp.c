// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#define DEFAULT_SYMBOL_NAMESPACE	"LIBETH_XDP"

#include <linux/export.h>

#include <net/libeth/xdp.h>

#include "priv.h"

/* XDPSQ sharing */

DEFINE_STATIC_KEY_FALSE(libeth_xdpsq_share);
EXPORT_SYMBOL_GPL(libeth_xdpsq_share);

void __libeth_xdpsq_get(struct libeth_xdpsq_lock *lock,
			const struct net_device *dev)
{
	bool warn;

	spin_lock_init(&lock->lock);
	lock->share = true;

	warn = !static_key_enabled(&libeth_xdpsq_share);
	static_branch_inc(&libeth_xdpsq_share);

	if (warn && net_ratelimit())
		netdev_warn(dev, "XDPSQ sharing enabled, possible XDP Tx slowdown\n");
}
EXPORT_SYMBOL_GPL(__libeth_xdpsq_get);

void __libeth_xdpsq_put(struct libeth_xdpsq_lock *lock,
			const struct net_device *dev)
{
	static_branch_dec(&libeth_xdpsq_share);

	if (!static_key_enabled(&libeth_xdpsq_share) && net_ratelimit())
		netdev_notice(dev, "XDPSQ sharing disabled\n");

	lock->share = false;
}
EXPORT_SYMBOL_GPL(__libeth_xdpsq_put);

void __acquires(&lock->lock)
__libeth_xdpsq_lock(struct libeth_xdpsq_lock *lock)
{
	spin_lock(&lock->lock);
}
EXPORT_SYMBOL_GPL(__libeth_xdpsq_lock);

void __releases(&lock->lock)
__libeth_xdpsq_unlock(struct libeth_xdpsq_lock *lock)
{
	spin_unlock(&lock->lock);
}
EXPORT_SYMBOL_GPL(__libeth_xdpsq_unlock);

/* XDPSQ clean-up timers */

/**
 * libeth_xdpsq_init_timer - initialize an XDPSQ clean-up timer
 * @timer: timer to initialize
 * @xdpsq: queue this timer belongs to
 * @lock: corresponding XDPSQ lock
 * @poll: queue polling/completion function
 *
 * XDPSQ clean-up timers must be set up before using at the queue configuration
 * time. Set the required pointers and the cleaning callback.
 */
void libeth_xdpsq_init_timer(struct libeth_xdpsq_timer *timer, void *xdpsq,
			     struct libeth_xdpsq_lock *lock,
			     void (*poll)(struct work_struct *work))
{
	timer->xdpsq = xdpsq;
	timer->lock = lock;

	INIT_DELAYED_WORK(&timer->dwork, poll);
}
EXPORT_SYMBOL_GPL(libeth_xdpsq_init_timer);

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
 * @flags: internal libeth_xdp flags (XSk, .ndo_xdp_xmit etc.)
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

	if (flags & LIBETH_XDP_TX_XSK)
		libeth_xsk_tx_return_bulk(pos, left);
	else if (!(flags & LIBETH_XDP_TX_NDO))
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
 * libeth_xdp_load_stash - recreate an &xdp_buff from libeth_xdp buffer stash
 * @dst: target &libeth_xdp_buff to initialize
 * @src: source stash
 *
 * External helper used by libeth_xdp_init_buff(), do not call directly.
 * Recreate an onstack &libeth_xdp_buff using the stash saved earlier.
 * The only field untouched (rxq) is initialized later in the
 * abovementioned function.
 */
void libeth_xdp_load_stash(struct libeth_xdp_buff *dst,
			   const struct libeth_xdp_buff_stash *src)
{
	dst->data = src->data;
	dst->base.data_end = src->data + src->len;
	dst->base.data_meta = src->data;
	dst->base.data_hard_start = src->data - src->headroom;

	dst->base.frame_sz = src->frame_sz;
	dst->base.flags = src->flags;
}
EXPORT_SYMBOL_GPL(libeth_xdp_load_stash);

/**
 * libeth_xdp_save_stash - convert &xdp_buff to a libeth_xdp buffer stash
 * @dst: target &libeth_xdp_buff_stash to initialize
 * @src: source XDP buffer
 *
 * External helper used by libeth_xdp_save_buff(), do not call directly.
 * Use the fields from the passed XDP buffer to initialize the stash on the
 * queue, so that a partially received frame can be finished later during
 * the next NAPI poll.
 */
void libeth_xdp_save_stash(struct libeth_xdp_buff_stash *dst,
			   const struct libeth_xdp_buff *src)
{
	dst->data = src->data;
	dst->headroom = src->data - src->base.data_hard_start;
	dst->len = src->base.data_end - src->data;

	dst->frame_sz = src->base.frame_sz;
	dst->flags = src->base.flags;

	WARN_ON_ONCE(dst->flags != src->base.flags);
}
EXPORT_SYMBOL_GPL(libeth_xdp_save_stash);

void __libeth_xdp_return_stash(struct libeth_xdp_buff_stash *stash)
{
	LIBETH_XDP_ONSTACK_BUFF(xdp);

	libeth_xdp_load_stash(xdp, stash);
	libeth_xdp_return_buff_slow(xdp);

	stash->data = NULL;
}
EXPORT_SYMBOL_GPL(__libeth_xdp_return_stash);

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

/**
 * libeth_xdp_buff_add_frag - add frag to XDP buffer
 * @xdp: head XDP buffer
 * @fqe: Rx buffer containing the frag
 * @len: frag length reported by HW
 *
 * External helper used by libeth_xdp_process_buff(), do not call directly.
 * Frees both head and frag buffers on error.
 *
 * Return: true success, false on error (no space for a new frag).
 */
bool libeth_xdp_buff_add_frag(struct libeth_xdp_buff *xdp,
			      const struct libeth_fqe *fqe,
			      u32 len)
{
	netmem_ref netmem = fqe->netmem;

	if (!xdp_buff_add_frag(&xdp->base, netmem,
			       fqe->offset + netmem_get_pp(netmem)->p.offset,
			       len, fqe->truesize))
		goto recycle;

	return true;

recycle:
	libeth_rx_recycle_slow(netmem);
	libeth_xdp_return_buff_slow(xdp);

	return false;
}
EXPORT_SYMBOL_GPL(libeth_xdp_buff_add_frag);

/**
 * libeth_xdp_prog_exception - handle XDP prog exceptions
 * @bq: XDP Tx bulk
 * @xdp: buffer to process
 * @act: original XDP prog verdict
 * @ret: error code if redirect failed
 *
 * External helper used by __libeth_xdp_run_prog() and
 * __libeth_xsk_run_prog_slow(), do not call directly.
 * Reports invalid @act, XDP exception trace event and frees the buffer.
 *
 * Return: libeth_xdp XDP prog verdict.
 */
u32 __cold libeth_xdp_prog_exception(const struct libeth_xdp_tx_bulk *bq,
				     struct libeth_xdp_buff *xdp,
				     enum xdp_action act, int ret)
{
	if (act > XDP_REDIRECT)
		bpf_warn_invalid_xdp_action(bq->dev, bq->prog, act);

	libeth_trace_xdp_exception(bq->dev, bq->prog, act);

	if (xdp->base.rxq->mem.type == MEM_TYPE_XSK_BUFF_POOL)
		return libeth_xsk_prog_exception(xdp, act, ret);

	libeth_xdp_return_buff_slow(xdp);

	return LIBETH_XDP_DROP;
}
EXPORT_SYMBOL_GPL(libeth_xdp_prog_exception);

/* Tx buffer completion */

static void libeth_xdp_put_netmem_bulk(netmem_ref netmem,
				       struct xdp_frame_bulk *bq)
{
	if (unlikely(bq->count == XDP_BULK_QUEUE_SIZE))
		xdp_flush_frame_bulk(bq);

	bq->q[bq->count++] = netmem;
}

/**
 * libeth_xdp_return_buff_bulk - free &xdp_buff as part of a bulk
 * @sinfo: shared info corresponding to the buffer
 * @bq: XDP frame bulk to store the buffer
 * @frags: whether the buffer has frags
 *
 * Same as xdp_return_frame_bulk(), but for &libeth_xdp_buff, speeds up Tx
 * completion of ``XDP_TX`` buffers and allows to free them in same bulks
 * with &xdp_frame buffers.
 */
void libeth_xdp_return_buff_bulk(const struct skb_shared_info *sinfo,
				 struct xdp_frame_bulk *bq, bool frags)
{
	if (!frags)
		goto head;

	for (u32 i = 0; i < sinfo->nr_frags; i++)
		libeth_xdp_put_netmem_bulk(skb_frag_netmem(&sinfo->frags[i]),
					   bq);

head:
	libeth_xdp_put_netmem_bulk(virt_to_netmem(sinfo), bq);
}
EXPORT_SYMBOL_GPL(libeth_xdp_return_buff_bulk);

/* Misc */

/**
 * libeth_xdp_queue_threshold - calculate XDP queue clean/refill threshold
 * @count: number of descriptors in the queue
 *
 * The threshold is the limit at which RQs start to refill (when the number of
 * empty buffers exceeds it) and SQs get cleaned up (when the number of free
 * descriptors goes below it). To speed up hotpath processing, threshold is
 * always pow-2, closest to 1/4 of the queue length.
 * Don't call it on hotpath, calculate and cache the threshold during the
 * queue initialization.
 *
 * Return: the calculated threshold.
 */
u32 libeth_xdp_queue_threshold(u32 count)
{
	u32 quarter, low, high;

	if (likely(is_power_of_2(count)))
		return count >> 2;

	quarter = DIV_ROUND_CLOSEST(count, 4);
	low = rounddown_pow_of_two(quarter);
	high = roundup_pow_of_two(quarter);

	return high - quarter <= quarter - low ? high : low;
}
EXPORT_SYMBOL_GPL(libeth_xdp_queue_threshold);

/**
 * __libeth_xdp_set_features - set XDP features for netdev
 * @dev: &net_device to configure
 * @xmo: XDP metadata ops (Rx hints)
 * @zc_segs: maximum number of S/G frags the HW can transmit
 * @tmo: XSk Tx metadata ops (Tx hints)
 *
 * Set all the features libeth_xdp supports. Only the first argument is
 * necessary; without the third one (zero), XSk support won't be advertised.
 * Use the non-underscored versions in drivers instead.
 */
void __libeth_xdp_set_features(struct net_device *dev,
			       const struct xdp_metadata_ops *xmo,
			       u32 zc_segs,
			       const struct xsk_tx_metadata_ops *tmo)
{
	xdp_set_features_flag(dev,
			      NETDEV_XDP_ACT_BASIC |
			      NETDEV_XDP_ACT_REDIRECT |
			      NETDEV_XDP_ACT_NDO_XMIT |
			      (zc_segs ? NETDEV_XDP_ACT_XSK_ZEROCOPY : 0) |
			      NETDEV_XDP_ACT_RX_SG |
			      NETDEV_XDP_ACT_NDO_XMIT_SG);
	dev->xdp_metadata_ops = xmo;

	tmo = tmo == libeth_xsktmo ? &libeth_xsktmo_slow : tmo;

	dev->xdp_zc_max_segs = zc_segs ? : 1;
	dev->xsk_tx_metadata_ops = zc_segs ? tmo : NULL;
}
EXPORT_SYMBOL_GPL(__libeth_xdp_set_features);

/**
 * libeth_xdp_set_redirect - toggle the XDP redirect feature
 * @dev: &net_device to configure
 * @enable: whether XDP is enabled
 *
 * Use this when XDPSQs are not always available to dynamically enable
 * and disable redirect feature.
 */
void libeth_xdp_set_redirect(struct net_device *dev, bool enable)
{
	if (enable)
		xdp_features_set_redirect_target(dev, true);
	else
		xdp_features_clear_redirect_target(dev);
}
EXPORT_SYMBOL_GPL(libeth_xdp_set_redirect);

/* Module */

static const struct libeth_xdp_ops xdp_ops __initconst = {
	.bulk	= libeth_xdp_return_buff_bulk,
	.xsk	= libeth_xsk_buff_free_slow,
};

static int __init libeth_xdp_module_init(void)
{
	libeth_attach_xdp(&xdp_ops);

	return 0;
}
module_init(libeth_xdp_module_init);

static void __exit libeth_xdp_module_exit(void)
{
	libeth_detach_xdp();
}
module_exit(libeth_xdp_module_exit);

MODULE_DESCRIPTION("Common Ethernet library - XDP infra");
MODULE_IMPORT_NS("LIBETH");
MODULE_LICENSE("GPL");
