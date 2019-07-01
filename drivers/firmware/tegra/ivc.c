// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved.
 */

#include <soc/tegra/ivc.h>

#define TEGRA_IVC_ALIGN 64

/*
 * IVC channel reset protocol.
 *
 * Each end uses its tx_channel.state to indicate its synchronization state.
 */
enum tegra_ivc_state {
	/*
	 * This value is zero for backwards compatibility with services that
	 * assume channels to be initially zeroed. Such channels are in an
	 * initially valid state, but cannot be asynchronously reset, and must
	 * maintain a valid state at all times.
	 *
	 * The transmitting end can enter the established state from the sync or
	 * ack state when it observes the receiving endpoint in the ack or
	 * established state, indicating that has cleared the counters in our
	 * rx_channel.
	 */
	TEGRA_IVC_STATE_ESTABLISHED = 0,

	/*
	 * If an endpoint is observed in the sync state, the remote endpoint is
	 * allowed to clear the counters it owns asynchronously with respect to
	 * the current endpoint. Therefore, the current endpoint is no longer
	 * allowed to communicate.
	 */
	TEGRA_IVC_STATE_SYNC,

	/*
	 * When the transmitting end observes the receiving end in the sync
	 * state, it can clear the w_count and r_count and transition to the ack
	 * state. If the remote endpoint observes us in the ack state, it can
	 * return to the established state once it has cleared its counters.
	 */
	TEGRA_IVC_STATE_ACK
};

/*
 * This structure is divided into two-cache aligned parts, the first is only
 * written through the tx.channel pointer, while the second is only written
 * through the rx.channel pointer. This delineates ownership of the cache
 * lines, which is critical to performance and necessary in non-cache coherent
 * implementations.
 */
struct tegra_ivc_header {
	union {
		struct {
			/* fields owned by the transmitting end */
			u32 count;
			u32 state;
		};

		u8 pad[TEGRA_IVC_ALIGN];
	} tx;

	union {
		/* fields owned by the receiving end */
		u32 count;
		u8 pad[TEGRA_IVC_ALIGN];
	} rx;
};

static inline void tegra_ivc_invalidate(struct tegra_ivc *ivc, dma_addr_t phys)
{
	if (!ivc->peer)
		return;

	dma_sync_single_for_cpu(ivc->peer, phys, TEGRA_IVC_ALIGN,
				DMA_FROM_DEVICE);
}

static inline void tegra_ivc_flush(struct tegra_ivc *ivc, dma_addr_t phys)
{
	if (!ivc->peer)
		return;

	dma_sync_single_for_device(ivc->peer, phys, TEGRA_IVC_ALIGN,
				   DMA_TO_DEVICE);
}

static inline bool tegra_ivc_empty(struct tegra_ivc *ivc,
				   struct tegra_ivc_header *header)
{
	/*
	 * This function performs multiple checks on the same values with
	 * security implications, so create snapshots with READ_ONCE() to
	 * ensure that these checks use the same values.
	 */
	u32 tx = READ_ONCE(header->tx.count);
	u32 rx = READ_ONCE(header->rx.count);

	/*
	 * Perform an over-full check to prevent denial of service attacks
	 * where a server could be easily fooled into believing that there's
	 * an extremely large number of frames ready, since receivers are not
	 * expected to check for full or over-full conditions.
	 *
	 * Although the channel isn't empty, this is an invalid case caused by
	 * a potentially malicious peer, so returning empty is safer, because
	 * it gives the impression that the channel has gone silent.
	 */
	if (tx - rx > ivc->num_frames)
		return true;

	return tx == rx;
}

static inline bool tegra_ivc_full(struct tegra_ivc *ivc,
				  struct tegra_ivc_header *header)
{
	u32 tx = READ_ONCE(header->tx.count);
	u32 rx = READ_ONCE(header->rx.count);

	/*
	 * Invalid cases where the counters indicate that the queue is over
	 * capacity also appear full.
	 */
	return tx - rx >= ivc->num_frames;
}

static inline u32 tegra_ivc_available(struct tegra_ivc *ivc,
				      struct tegra_ivc_header *header)
{
	u32 tx = READ_ONCE(header->tx.count);
	u32 rx = READ_ONCE(header->rx.count);

	/*
	 * This function isn't expected to be used in scenarios where an
	 * over-full situation can lead to denial of service attacks. See the
	 * comment in tegra_ivc_empty() for an explanation about special
	 * over-full considerations.
	 */
	return tx - rx;
}

static inline void tegra_ivc_advance_tx(struct tegra_ivc *ivc)
{
	WRITE_ONCE(ivc->tx.channel->tx.count,
		   READ_ONCE(ivc->tx.channel->tx.count) + 1);

	if (ivc->tx.position == ivc->num_frames - 1)
		ivc->tx.position = 0;
	else
		ivc->tx.position++;
}

static inline void tegra_ivc_advance_rx(struct tegra_ivc *ivc)
{
	WRITE_ONCE(ivc->rx.channel->rx.count,
		   READ_ONCE(ivc->rx.channel->rx.count) + 1);

	if (ivc->rx.position == ivc->num_frames - 1)
		ivc->rx.position = 0;
	else
		ivc->rx.position++;
}

static inline int tegra_ivc_check_read(struct tegra_ivc *ivc)
{
	unsigned int offset = offsetof(struct tegra_ivc_header, tx.count);

	/*
	 * tx.channel->state is set locally, so it is not synchronized with
	 * state from the remote peer. The remote peer cannot reset its
	 * transmit counters until we've acknowledged its synchronization
	 * request, so no additional synchronization is required because an
	 * asynchronous transition of rx.channel->state to
	 * TEGRA_IVC_STATE_ACK is not allowed.
	 */
	if (ivc->tx.channel->tx.state != TEGRA_IVC_STATE_ESTABLISHED)
		return -ECONNRESET;

	/*
	 * Avoid unnecessary invalidations when performing repeated accesses
	 * to an IVC channel by checking the old queue pointers first.
	 *
	 * Synchronization is only necessary when these pointers indicate
	 * empty or full.
	 */
	if (!tegra_ivc_empty(ivc, ivc->rx.channel))
		return 0;

	tegra_ivc_invalidate(ivc, ivc->rx.phys + offset);

	if (tegra_ivc_empty(ivc, ivc->rx.channel))
		return -ENOSPC;

	return 0;
}

static inline int tegra_ivc_check_write(struct tegra_ivc *ivc)
{
	unsigned int offset = offsetof(struct tegra_ivc_header, rx.count);

	if (ivc->tx.channel->tx.state != TEGRA_IVC_STATE_ESTABLISHED)
		return -ECONNRESET;

	if (!tegra_ivc_full(ivc, ivc->tx.channel))
		return 0;

	tegra_ivc_invalidate(ivc, ivc->tx.phys + offset);

	if (tegra_ivc_full(ivc, ivc->tx.channel))
		return -ENOSPC;

	return 0;
}

static void *tegra_ivc_frame_virt(struct tegra_ivc *ivc,
				  struct tegra_ivc_header *header,
				  unsigned int frame)
{
	if (WARN_ON(frame >= ivc->num_frames))
		return ERR_PTR(-EINVAL);

	return (void *)(header + 1) + ivc->frame_size * frame;
}

static inline dma_addr_t tegra_ivc_frame_phys(struct tegra_ivc *ivc,
					      dma_addr_t phys,
					      unsigned int frame)
{
	unsigned long offset;

	offset = sizeof(struct tegra_ivc_header) + ivc->frame_size * frame;

	return phys + offset;
}

static inline void tegra_ivc_invalidate_frame(struct tegra_ivc *ivc,
					      dma_addr_t phys,
					      unsigned int frame,
					      unsigned int offset,
					      size_t size)
{
	if (!ivc->peer || WARN_ON(frame >= ivc->num_frames))
		return;

	phys = tegra_ivc_frame_phys(ivc, phys, frame) + offset;

	dma_sync_single_for_cpu(ivc->peer, phys, size, DMA_FROM_DEVICE);
}

static inline void tegra_ivc_flush_frame(struct tegra_ivc *ivc,
					 dma_addr_t phys,
					 unsigned int frame,
					 unsigned int offset,
					 size_t size)
{
	if (!ivc->peer || WARN_ON(frame >= ivc->num_frames))
		return;

	phys = tegra_ivc_frame_phys(ivc, phys, frame) + offset;

	dma_sync_single_for_device(ivc->peer, phys, size, DMA_TO_DEVICE);
}

/* directly peek at the next frame rx'ed */
void *tegra_ivc_read_get_next_frame(struct tegra_ivc *ivc)
{
	int err;

	if (WARN_ON(ivc == NULL))
		return ERR_PTR(-EINVAL);

	err = tegra_ivc_check_read(ivc);
	if (err < 0)
		return ERR_PTR(err);

	/*
	 * Order observation of ivc->rx.position potentially indicating new
	 * data before data read.
	 */
	smp_rmb();

	tegra_ivc_invalidate_frame(ivc, ivc->rx.phys, ivc->rx.position, 0,
				   ivc->frame_size);

	return tegra_ivc_frame_virt(ivc, ivc->rx.channel, ivc->rx.position);
}
EXPORT_SYMBOL(tegra_ivc_read_get_next_frame);

int tegra_ivc_read_advance(struct tegra_ivc *ivc)
{
	unsigned int rx = offsetof(struct tegra_ivc_header, rx.count);
	unsigned int tx = offsetof(struct tegra_ivc_header, tx.count);
	int err;

	/*
	 * No read barriers or synchronization here: the caller is expected to
	 * have already observed the channel non-empty. This check is just to
	 * catch programming errors.
	 */
	err = tegra_ivc_check_read(ivc);
	if (err < 0)
		return err;

	tegra_ivc_advance_rx(ivc);

	tegra_ivc_flush(ivc, ivc->rx.phys + rx);

	/*
	 * Ensure our write to ivc->rx.position occurs before our read from
	 * ivc->tx.position.
	 */
	smp_mb();

	/*
	 * Notify only upon transition from full to non-full. The available
	 * count can only asynchronously increase, so the worst possible
	 * side-effect will be a spurious notification.
	 */
	tegra_ivc_invalidate(ivc, ivc->rx.phys + tx);

	if (tegra_ivc_available(ivc, ivc->rx.channel) == ivc->num_frames - 1)
		ivc->notify(ivc, ivc->notify_data);

	return 0;
}
EXPORT_SYMBOL(tegra_ivc_read_advance);

/* directly poke at the next frame to be tx'ed */
void *tegra_ivc_write_get_next_frame(struct tegra_ivc *ivc)
{
	int err;

	err = tegra_ivc_check_write(ivc);
	if (err < 0)
		return ERR_PTR(err);

	return tegra_ivc_frame_virt(ivc, ivc->tx.channel, ivc->tx.position);
}
EXPORT_SYMBOL(tegra_ivc_write_get_next_frame);

/* advance the tx buffer */
int tegra_ivc_write_advance(struct tegra_ivc *ivc)
{
	unsigned int tx = offsetof(struct tegra_ivc_header, tx.count);
	unsigned int rx = offsetof(struct tegra_ivc_header, rx.count);
	int err;

	err = tegra_ivc_check_write(ivc);
	if (err < 0)
		return err;

	tegra_ivc_flush_frame(ivc, ivc->tx.phys, ivc->tx.position, 0,
			      ivc->frame_size);

	/*
	 * Order any possible stores to the frame before update of
	 * ivc->tx.position.
	 */
	smp_wmb();

	tegra_ivc_advance_tx(ivc);
	tegra_ivc_flush(ivc, ivc->tx.phys + tx);

	/*
	 * Ensure our write to ivc->tx.position occurs before our read from
	 * ivc->rx.position.
	 */
	smp_mb();

	/*
	 * Notify only upon transition from empty to non-empty. The available
	 * count can only asynchronously decrease, so the worst possible
	 * side-effect will be a spurious notification.
	 */
	tegra_ivc_invalidate(ivc, ivc->tx.phys + rx);

	if (tegra_ivc_available(ivc, ivc->tx.channel) == 1)
		ivc->notify(ivc, ivc->notify_data);

	return 0;
}
EXPORT_SYMBOL(tegra_ivc_write_advance);

void tegra_ivc_reset(struct tegra_ivc *ivc)
{
	unsigned int offset = offsetof(struct tegra_ivc_header, tx.count);

	ivc->tx.channel->tx.state = TEGRA_IVC_STATE_SYNC;
	tegra_ivc_flush(ivc, ivc->tx.phys + offset);
	ivc->notify(ivc, ivc->notify_data);
}
EXPORT_SYMBOL(tegra_ivc_reset);

/*
 * =======================================================
 *  IVC State Transition Table - see tegra_ivc_notified()
 * =======================================================
 *
 *	local	remote	action
 *	-----	------	-----------------------------------
 *	SYNC	EST	<none>
 *	SYNC	ACK	reset counters; move to EST; notify
 *	SYNC	SYNC	reset counters; move to ACK; notify
 *	ACK	EST	move to EST; notify
 *	ACK	ACK	move to EST; notify
 *	ACK	SYNC	reset counters; move to ACK; notify
 *	EST	EST	<none>
 *	EST	ACK	<none>
 *	EST	SYNC	reset counters; move to ACK; notify
 *
 * ===============================================================
 */

int tegra_ivc_notified(struct tegra_ivc *ivc)
{
	unsigned int offset = offsetof(struct tegra_ivc_header, tx.count);
	enum tegra_ivc_state state;

	/* Copy the receiver's state out of shared memory. */
	tegra_ivc_invalidate(ivc, ivc->rx.phys + offset);
	state = READ_ONCE(ivc->rx.channel->tx.state);

	if (state == TEGRA_IVC_STATE_SYNC) {
		offset = offsetof(struct tegra_ivc_header, tx.count);

		/*
		 * Order observation of TEGRA_IVC_STATE_SYNC before stores
		 * clearing tx.channel.
		 */
		smp_rmb();

		/*
		 * Reset tx.channel counters. The remote end is in the SYNC
		 * state and won't make progress until we change our state,
		 * so the counters are not in use at this time.
		 */
		ivc->tx.channel->tx.count = 0;
		ivc->rx.channel->rx.count = 0;

		ivc->tx.position = 0;
		ivc->rx.position = 0;

		/*
		 * Ensure that counters appear cleared before new state can be
		 * observed.
		 */
		smp_wmb();

		/*
		 * Move to ACK state. We have just cleared our counters, so it
		 * is now safe for the remote end to start using these values.
		 */
		ivc->tx.channel->tx.state = TEGRA_IVC_STATE_ACK;
		tegra_ivc_flush(ivc, ivc->tx.phys + offset);

		/*
		 * Notify remote end to observe state transition.
		 */
		ivc->notify(ivc, ivc->notify_data);

	} else if (ivc->tx.channel->tx.state == TEGRA_IVC_STATE_SYNC &&
		   state == TEGRA_IVC_STATE_ACK) {
		offset = offsetof(struct tegra_ivc_header, tx.count);

		/*
		 * Order observation of ivc_state_sync before stores clearing
		 * tx_channel.
		 */
		smp_rmb();

		/*
		 * Reset tx.channel counters. The remote end is in the ACK
		 * state and won't make progress until we change our state,
		 * so the counters are not in use at this time.
		 */
		ivc->tx.channel->tx.count = 0;
		ivc->rx.channel->rx.count = 0;

		ivc->tx.position = 0;
		ivc->rx.position = 0;

		/*
		 * Ensure that counters appear cleared before new state can be
		 * observed.
		 */
		smp_wmb();

		/*
		 * Move to ESTABLISHED state. We know that the remote end has
		 * already cleared its counters, so it is safe to start
		 * writing/reading on this channel.
		 */
		ivc->tx.channel->tx.state = TEGRA_IVC_STATE_ESTABLISHED;
		tegra_ivc_flush(ivc, ivc->tx.phys + offset);

		/*
		 * Notify remote end to observe state transition.
		 */
		ivc->notify(ivc, ivc->notify_data);

	} else if (ivc->tx.channel->tx.state == TEGRA_IVC_STATE_ACK) {
		offset = offsetof(struct tegra_ivc_header, tx.count);

		/*
		 * At this point, we have observed the peer to be in either
		 * the ACK or ESTABLISHED state. Next, order observation of
		 * peer state before storing to tx.channel.
		 */
		smp_rmb();

		/*
		 * Move to ESTABLISHED state. We know that we have previously
		 * cleared our counters, and we know that the remote end has
		 * cleared its counters, so it is safe to start writing/reading
		 * on this channel.
		 */
		ivc->tx.channel->tx.state = TEGRA_IVC_STATE_ESTABLISHED;
		tegra_ivc_flush(ivc, ivc->tx.phys + offset);

		/*
		 * Notify remote end to observe state transition.
		 */
		ivc->notify(ivc, ivc->notify_data);

	} else {
		/*
		 * There is no need to handle any further action. Either the
		 * channel is already fully established, or we are waiting for
		 * the remote end to catch up with our current state. Refer
		 * to the diagram in "IVC State Transition Table" above.
		 */
	}

	if (ivc->tx.channel->tx.state != TEGRA_IVC_STATE_ESTABLISHED)
		return -EAGAIN;

	return 0;
}
EXPORT_SYMBOL(tegra_ivc_notified);

size_t tegra_ivc_align(size_t size)
{
	return ALIGN(size, TEGRA_IVC_ALIGN);
}
EXPORT_SYMBOL(tegra_ivc_align);

unsigned tegra_ivc_total_queue_size(unsigned queue_size)
{
	if (!IS_ALIGNED(queue_size, TEGRA_IVC_ALIGN)) {
		pr_err("%s: queue_size (%u) must be %u-byte aligned\n",
		       __func__, queue_size, TEGRA_IVC_ALIGN);
		return 0;
	}

	return queue_size + sizeof(struct tegra_ivc_header);
}
EXPORT_SYMBOL(tegra_ivc_total_queue_size);

static int tegra_ivc_check_params(unsigned long rx, unsigned long tx,
				  unsigned int num_frames, size_t frame_size)
{
	BUILD_BUG_ON(!IS_ALIGNED(offsetof(struct tegra_ivc_header, tx.count),
				 TEGRA_IVC_ALIGN));
	BUILD_BUG_ON(!IS_ALIGNED(offsetof(struct tegra_ivc_header, rx.count),
				 TEGRA_IVC_ALIGN));
	BUILD_BUG_ON(!IS_ALIGNED(sizeof(struct tegra_ivc_header),
				 TEGRA_IVC_ALIGN));

	if ((uint64_t)num_frames * (uint64_t)frame_size >= 0x100000000UL) {
		pr_err("num_frames * frame_size overflows\n");
		return -EINVAL;
	}

	if (!IS_ALIGNED(frame_size, TEGRA_IVC_ALIGN)) {
		pr_err("frame size not adequately aligned: %zu\n", frame_size);
		return -EINVAL;
	}

	/*
	 * The headers must at least be aligned enough for counters
	 * to be accessed atomically.
	 */
	if (!IS_ALIGNED(rx, TEGRA_IVC_ALIGN)) {
		pr_err("IVC channel start not aligned: %#lx\n", rx);
		return -EINVAL;
	}

	if (!IS_ALIGNED(tx, TEGRA_IVC_ALIGN)) {
		pr_err("IVC channel start not aligned: %#lx\n", tx);
		return -EINVAL;
	}

	if (rx < tx) {
		if (rx + frame_size * num_frames > tx) {
			pr_err("queue regions overlap: %#lx + %zx > %#lx\n",
			       rx, frame_size * num_frames, tx);
			return -EINVAL;
		}
	} else {
		if (tx + frame_size * num_frames > rx) {
			pr_err("queue regions overlap: %#lx + %zx > %#lx\n",
			       tx, frame_size * num_frames, rx);
			return -EINVAL;
		}
	}

	return 0;
}

int tegra_ivc_init(struct tegra_ivc *ivc, struct device *peer, void *rx,
		   dma_addr_t rx_phys, void *tx, dma_addr_t tx_phys,
		   unsigned int num_frames, size_t frame_size,
		   void (*notify)(struct tegra_ivc *ivc, void *data),
		   void *data)
{
	size_t queue_size;
	int err;

	if (WARN_ON(!ivc || !notify))
		return -EINVAL;

	/*
	 * All sizes that can be returned by communication functions should
	 * fit in an int.
	 */
	if (frame_size > INT_MAX)
		return -E2BIG;

	err = tegra_ivc_check_params((unsigned long)rx, (unsigned long)tx,
				     num_frames, frame_size);
	if (err < 0)
		return err;

	queue_size = tegra_ivc_total_queue_size(num_frames * frame_size);

	if (peer) {
		ivc->rx.phys = dma_map_single(peer, rx, queue_size,
					      DMA_BIDIRECTIONAL);
		if (dma_mapping_error(peer, ivc->rx.phys))
			return -ENOMEM;

		ivc->tx.phys = dma_map_single(peer, tx, queue_size,
					      DMA_BIDIRECTIONAL);
		if (dma_mapping_error(peer, ivc->tx.phys)) {
			dma_unmap_single(peer, ivc->rx.phys, queue_size,
					 DMA_BIDIRECTIONAL);
			return -ENOMEM;
		}
	} else {
		ivc->rx.phys = rx_phys;
		ivc->tx.phys = tx_phys;
	}

	ivc->rx.channel = rx;
	ivc->tx.channel = tx;
	ivc->peer = peer;
	ivc->notify = notify;
	ivc->notify_data = data;
	ivc->frame_size = frame_size;
	ivc->num_frames = num_frames;

	/*
	 * These values aren't necessarily correct until the channel has been
	 * reset.
	 */
	ivc->tx.position = 0;
	ivc->rx.position = 0;

	return 0;
}
EXPORT_SYMBOL(tegra_ivc_init);

void tegra_ivc_cleanup(struct tegra_ivc *ivc)
{
	if (ivc->peer) {
		size_t size = tegra_ivc_total_queue_size(ivc->num_frames *
							 ivc->frame_size);

		dma_unmap_single(ivc->peer, ivc->rx.phys, size,
				 DMA_BIDIRECTIONAL);
		dma_unmap_single(ivc->peer, ivc->tx.phys, size,
				 DMA_BIDIRECTIONAL);
	}
}
EXPORT_SYMBOL(tegra_ivc_cleanup);
