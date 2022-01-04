// SPDX-License-Identifier: GPL-2.0+
/*
 * SSH packet transport layer.
 *
 * Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <asm/unaligned.h>
#include <linux/atomic.h>
#include <linux/error-injection.h>
#include <linux/jiffies.h>
#include <linux/kfifo.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <linux/surface_aggregator/serial_hub.h>

#include "ssh_msgb.h"
#include "ssh_packet_layer.h"
#include "ssh_parser.h"

#include "trace.h"

/*
 * To simplify reasoning about the code below, we define a few concepts. The
 * system below is similar to a state-machine for packets, however, there are
 * too many states to explicitly write them down. To (somewhat) manage the
 * states and packets we rely on flags, reference counting, and some simple
 * concepts. State transitions are triggered by actions.
 *
 * >> Actions <<
 *
 * - submit
 * - transmission start (process next item in queue)
 * - transmission finished (guaranteed to never be parallel to transmission
 *   start)
 * - ACK received
 * - NAK received (this is equivalent to issuing re-submit for all pending
 *   packets)
 * - timeout (this is equivalent to re-issuing a submit or canceling)
 * - cancel (non-pending and pending)
 *
 * >> Data Structures, Packet Ownership, General Overview <<
 *
 * The code below employs two main data structures: The packet queue,
 * containing all packets scheduled for transmission, and the set of pending
 * packets, containing all packets awaiting an ACK.
 *
 * Shared ownership of a packet is controlled via reference counting. Inside
 * the transport system are a total of five packet owners:
 *
 * - the packet queue,
 * - the pending set,
 * - the transmitter thread,
 * - the receiver thread (via ACKing), and
 * - the timeout work item.
 *
 * Normal operation is as follows: The initial reference of the packet is
 * obtained by submitting the packet and queuing it. The receiver thread takes
 * packets from the queue. By doing this, it does not increment the refcount
 * but takes over the reference (removing it from the queue). If the packet is
 * sequenced (i.e. needs to be ACKed by the client), the transmitter thread
 * sets-up the timeout and adds the packet to the pending set before starting
 * to transmit it. As the timeout is handled by a reaper task, no additional
 * reference for it is needed. After the transmit is done, the reference held
 * by the transmitter thread is dropped. If the packet is unsequenced (i.e.
 * does not need an ACK), the packet is completed by the transmitter thread
 * before dropping that reference.
 *
 * On receival of an ACK, the receiver thread removes and obtains the
 * reference to the packet from the pending set. The receiver thread will then
 * complete the packet and drop its reference.
 *
 * On receival of a NAK, the receiver thread re-submits all currently pending
 * packets.
 *
 * Packet timeouts are detected by the timeout reaper. This is a task,
 * scheduled depending on the earliest packet timeout expiration date,
 * checking all currently pending packets if their timeout has expired. If the
 * timeout of a packet has expired, it is re-submitted and the number of tries
 * of this packet is incremented. If this number reaches its limit, the packet
 * will be completed with a failure.
 *
 * On transmission failure (such as repeated packet timeouts), the completion
 * callback is immediately run by on thread on which the error was detected.
 *
 * To ensure that a packet eventually leaves the system it is marked as
 * "locked" directly before it is going to be completed or when it is
 * canceled. Marking a packet as "locked" has the effect that passing and
 * creating new references of the packet is disallowed. This means that the
 * packet cannot be added to the queue, the pending set, and the timeout, or
 * be picked up by the transmitter thread or receiver thread. To remove a
 * packet from the system it has to be marked as locked and subsequently all
 * references from the data structures (queue, pending) have to be removed.
 * References held by threads will eventually be dropped automatically as
 * their execution progresses.
 *
 * Note that the packet completion callback is, in case of success and for a
 * sequenced packet, guaranteed to run on the receiver thread, thus providing
 * a way to reliably identify responses to the packet. The packet completion
 * callback is only run once and it does not indicate that the packet has
 * fully left the system (for this, one should rely on the release method,
 * triggered when the reference count of the packet reaches zero). In case of
 * re-submission (and with somewhat unlikely timing), it may be possible that
 * the packet is being re-transmitted while the completion callback runs.
 * Completion will occur both on success and internal error, as well as when
 * the packet is canceled.
 *
 * >> Flags <<
 *
 * Flags are used to indicate the state and progression of a packet. Some flags
 * have stricter guarantees than other:
 *
 * - locked
 *   Indicates if the packet is locked. If the packet is locked, passing and/or
 *   creating additional references to the packet is forbidden. The packet thus
 *   may not be queued, dequeued, or removed or added to the pending set. Note
 *   that the packet state flags may still change (e.g. it may be marked as
 *   ACKed, transmitted, ...).
 *
 * - completed
 *   Indicates if the packet completion callback has been executed or is about
 *   to be executed. This flag is used to ensure that the packet completion
 *   callback is only run once.
 *
 * - queued
 *   Indicates if a packet is present in the submission queue or not. This flag
 *   must only be modified with the queue lock held, and must be coherent to the
 *   presence of the packet in the queue.
 *
 * - pending
 *   Indicates if a packet is present in the set of pending packets or not.
 *   This flag must only be modified with the pending lock held, and must be
 *   coherent to the presence of the packet in the pending set.
 *
 * - transmitting
 *   Indicates if the packet is currently transmitting. In case of
 *   re-transmissions, it is only safe to wait on the "transmitted" completion
 *   after this flag has been set. The completion will be set both in success
 *   and error case.
 *
 * - transmitted
 *   Indicates if the packet has been transmitted. This flag is not cleared by
 *   the system, thus it indicates the first transmission only.
 *
 * - acked
 *   Indicates if the packet has been acknowledged by the client. There are no
 *   other guarantees given. For example, the packet may still be canceled
 *   and/or the completion may be triggered an error even though this bit is
 *   set. Rely on the status provided to the completion callback instead.
 *
 * - canceled
 *   Indicates if the packet has been canceled from the outside. There are no
 *   other guarantees given. Specifically, the packet may be completed by
 *   another part of the system before the cancellation attempts to complete it.
 *
 * >> General Notes <<
 *
 * - To avoid deadlocks, if both queue and pending locks are required, the
 *   pending lock must be acquired before the queue lock.
 *
 * - The packet priority must be accessed only while holding the queue lock.
 *
 * - The packet timestamp must be accessed only while holding the pending
 *   lock.
 */

/*
 * SSH_PTL_MAX_PACKET_TRIES - Maximum transmission attempts for packet.
 *
 * Maximum number of transmission attempts per sequenced packet in case of
 * time-outs. Must be smaller than 16. If the packet times out after this
 * amount of tries, the packet will be completed with %-ETIMEDOUT as status
 * code.
 */
#define SSH_PTL_MAX_PACKET_TRIES		3

/*
 * SSH_PTL_TX_TIMEOUT - Packet transmission timeout.
 *
 * Timeout in jiffies for packet transmission via the underlying serial
 * device. If transmitting the packet takes longer than this timeout, the
 * packet will be completed with -ETIMEDOUT. It will not be re-submitted.
 */
#define SSH_PTL_TX_TIMEOUT			HZ

/*
 * SSH_PTL_PACKET_TIMEOUT - Packet response timeout.
 *
 * Timeout as ktime_t delta for ACKs. If we have not received an ACK in this
 * time-frame after starting transmission, the packet will be re-submitted.
 */
#define SSH_PTL_PACKET_TIMEOUT			ms_to_ktime(1000)

/*
 * SSH_PTL_PACKET_TIMEOUT_RESOLUTION - Packet timeout granularity.
 *
 * Time-resolution for timeouts. Should be larger than one jiffy to avoid
 * direct re-scheduling of reaper work_struct.
 */
#define SSH_PTL_PACKET_TIMEOUT_RESOLUTION	ms_to_ktime(max(2000 / HZ, 50))

/*
 * SSH_PTL_MAX_PENDING - Maximum number of pending packets.
 *
 * Maximum number of sequenced packets concurrently waiting for an ACK.
 * Packets marked as blocking will not be transmitted while this limit is
 * reached.
 */
#define SSH_PTL_MAX_PENDING			1

/*
 * SSH_PTL_RX_BUF_LEN - Evaluation-buffer size in bytes.
 */
#define SSH_PTL_RX_BUF_LEN			4096

/*
 * SSH_PTL_RX_FIFO_LEN - Fifo input-buffer size in bytes.
 */
#define SSH_PTL_RX_FIFO_LEN			4096

#ifdef CONFIG_SURFACE_AGGREGATOR_ERROR_INJECTION

/**
 * ssh_ptl_should_drop_ack_packet() - Error injection hook to drop ACK packets.
 *
 * Useful to test detection and handling of automated re-transmits by the EC.
 * Specifically of packets that the EC considers not-ACKed but the driver
 * already considers ACKed (due to dropped ACK). In this case, the EC
 * re-transmits the packet-to-be-ACKed and the driver should detect it as
 * duplicate/already handled. Note that the driver should still send an ACK
 * for the re-transmitted packet.
 */
static noinline bool ssh_ptl_should_drop_ack_packet(void)
{
	return false;
}
ALLOW_ERROR_INJECTION(ssh_ptl_should_drop_ack_packet, TRUE);

/**
 * ssh_ptl_should_drop_nak_packet() - Error injection hook to drop NAK packets.
 *
 * Useful to test/force automated (timeout-based) re-transmit by the EC.
 * Specifically, packets that have not reached the driver completely/with valid
 * checksums. Only useful in combination with receival of (injected) bad data.
 */
static noinline bool ssh_ptl_should_drop_nak_packet(void)
{
	return false;
}
ALLOW_ERROR_INJECTION(ssh_ptl_should_drop_nak_packet, TRUE);

/**
 * ssh_ptl_should_drop_dsq_packet() - Error injection hook to drop sequenced
 * data packet.
 *
 * Useful to test re-transmit timeout of the driver. If the data packet has not
 * been ACKed after a certain time, the driver should re-transmit the packet up
 * to limited number of times defined in SSH_PTL_MAX_PACKET_TRIES.
 */
static noinline bool ssh_ptl_should_drop_dsq_packet(void)
{
	return false;
}
ALLOW_ERROR_INJECTION(ssh_ptl_should_drop_dsq_packet, TRUE);

/**
 * ssh_ptl_should_fail_write() - Error injection hook to make
 * serdev_device_write() fail.
 *
 * Hook to simulate errors in serdev_device_write when transmitting packets.
 */
static noinline int ssh_ptl_should_fail_write(void)
{
	return 0;
}
ALLOW_ERROR_INJECTION(ssh_ptl_should_fail_write, ERRNO);

/**
 * ssh_ptl_should_corrupt_tx_data() - Error injection hook to simulate invalid
 * data being sent to the EC.
 *
 * Hook to simulate corrupt/invalid data being sent from host (driver) to EC.
 * Causes the packet data to be actively corrupted by overwriting it with
 * pre-defined values, such that it becomes invalid, causing the EC to respond
 * with a NAK packet. Useful to test handling of NAK packets received by the
 * driver.
 */
static noinline bool ssh_ptl_should_corrupt_tx_data(void)
{
	return false;
}
ALLOW_ERROR_INJECTION(ssh_ptl_should_corrupt_tx_data, TRUE);

/**
 * ssh_ptl_should_corrupt_rx_syn() - Error injection hook to simulate invalid
 * data being sent by the EC.
 *
 * Hook to simulate invalid SYN bytes, i.e. an invalid start of messages and
 * test handling thereof in the driver.
 */
static noinline bool ssh_ptl_should_corrupt_rx_syn(void)
{
	return false;
}
ALLOW_ERROR_INJECTION(ssh_ptl_should_corrupt_rx_syn, TRUE);

/**
 * ssh_ptl_should_corrupt_rx_data() - Error injection hook to simulate invalid
 * data being sent by the EC.
 *
 * Hook to simulate invalid data/checksum of the message frame and test handling
 * thereof in the driver.
 */
static noinline bool ssh_ptl_should_corrupt_rx_data(void)
{
	return false;
}
ALLOW_ERROR_INJECTION(ssh_ptl_should_corrupt_rx_data, TRUE);

static bool __ssh_ptl_should_drop_ack_packet(struct ssh_packet *packet)
{
	if (likely(!ssh_ptl_should_drop_ack_packet()))
		return false;

	trace_ssam_ei_tx_drop_ack_packet(packet);
	ptl_info(packet->ptl, "packet error injection: dropping ACK packet %p\n",
		 packet);

	return true;
}

static bool __ssh_ptl_should_drop_nak_packet(struct ssh_packet *packet)
{
	if (likely(!ssh_ptl_should_drop_nak_packet()))
		return false;

	trace_ssam_ei_tx_drop_nak_packet(packet);
	ptl_info(packet->ptl, "packet error injection: dropping NAK packet %p\n",
		 packet);

	return true;
}

static bool __ssh_ptl_should_drop_dsq_packet(struct ssh_packet *packet)
{
	if (likely(!ssh_ptl_should_drop_dsq_packet()))
		return false;

	trace_ssam_ei_tx_drop_dsq_packet(packet);
	ptl_info(packet->ptl,
		 "packet error injection: dropping sequenced data packet %p\n",
		 packet);

	return true;
}

static bool ssh_ptl_should_drop_packet(struct ssh_packet *packet)
{
	/* Ignore packets that don't carry any data (i.e. flush). */
	if (!packet->data.ptr || !packet->data.len)
		return false;

	switch (packet->data.ptr[SSH_MSGOFFSET_FRAME(type)]) {
	case SSH_FRAME_TYPE_ACK:
		return __ssh_ptl_should_drop_ack_packet(packet);

	case SSH_FRAME_TYPE_NAK:
		return __ssh_ptl_should_drop_nak_packet(packet);

	case SSH_FRAME_TYPE_DATA_SEQ:
		return __ssh_ptl_should_drop_dsq_packet(packet);

	default:
		return false;
	}
}

static int ssh_ptl_write_buf(struct ssh_ptl *ptl, struct ssh_packet *packet,
			     const unsigned char *buf, size_t count)
{
	int status;

	status = ssh_ptl_should_fail_write();
	if (unlikely(status)) {
		trace_ssam_ei_tx_fail_write(packet, status);
		ptl_info(packet->ptl,
			 "packet error injection: simulating transmit error %d, packet %p\n",
			 status, packet);

		return status;
	}

	return serdev_device_write_buf(ptl->serdev, buf, count);
}

static void ssh_ptl_tx_inject_invalid_data(struct ssh_packet *packet)
{
	/* Ignore packets that don't carry any data (i.e. flush). */
	if (!packet->data.ptr || !packet->data.len)
		return;

	/* Only allow sequenced data packets to be modified. */
	if (packet->data.ptr[SSH_MSGOFFSET_FRAME(type)] != SSH_FRAME_TYPE_DATA_SEQ)
		return;

	if (likely(!ssh_ptl_should_corrupt_tx_data()))
		return;

	trace_ssam_ei_tx_corrupt_data(packet);
	ptl_info(packet->ptl,
		 "packet error injection: simulating invalid transmit data on packet %p\n",
		 packet);

	/*
	 * NB: The value 0xb3 has been chosen more or less randomly so that it
	 * doesn't have any (major) overlap with the SYN bytes (aa 55) and is
	 * non-trivial (i.e. non-zero, non-0xff).
	 */
	memset(packet->data.ptr, 0xb3, packet->data.len);
}

static void ssh_ptl_rx_inject_invalid_syn(struct ssh_ptl *ptl,
					  struct ssam_span *data)
{
	struct ssam_span frame;

	/* Check if there actually is something to corrupt. */
	if (!sshp_find_syn(data, &frame))
		return;

	if (likely(!ssh_ptl_should_corrupt_rx_syn()))
		return;

	trace_ssam_ei_rx_corrupt_syn(data->len);

	data->ptr[1] = 0xb3;	/* Set second byte of SYN to "random" value. */
}

static void ssh_ptl_rx_inject_invalid_data(struct ssh_ptl *ptl,
					   struct ssam_span *frame)
{
	size_t payload_len, message_len;
	struct ssh_frame *sshf;

	/* Ignore incomplete messages, will get handled once it's complete. */
	if (frame->len < SSH_MESSAGE_LENGTH(0))
		return;

	/* Ignore incomplete messages, part 2. */
	payload_len = get_unaligned_le16(&frame->ptr[SSH_MSGOFFSET_FRAME(len)]);
	message_len = SSH_MESSAGE_LENGTH(payload_len);
	if (frame->len < message_len)
		return;

	if (likely(!ssh_ptl_should_corrupt_rx_data()))
		return;

	sshf = (struct ssh_frame *)&frame->ptr[SSH_MSGOFFSET_FRAME(type)];
	trace_ssam_ei_rx_corrupt_data(sshf);

	/*
	 * Flip bits in first byte of payload checksum. This is basically
	 * equivalent to a payload/frame data error without us having to worry
	 * about (the, arguably pretty small, probability of) accidental
	 * checksum collisions.
	 */
	frame->ptr[frame->len - 2] = ~frame->ptr[frame->len - 2];
}

#else /* CONFIG_SURFACE_AGGREGATOR_ERROR_INJECTION */

static inline bool ssh_ptl_should_drop_packet(struct ssh_packet *packet)
{
	return false;
}

static inline int ssh_ptl_write_buf(struct ssh_ptl *ptl,
				    struct ssh_packet *packet,
				    const unsigned char *buf,
				    size_t count)
{
	return serdev_device_write_buf(ptl->serdev, buf, count);
}

static inline void ssh_ptl_tx_inject_invalid_data(struct ssh_packet *packet)
{
}

static inline void ssh_ptl_rx_inject_invalid_syn(struct ssh_ptl *ptl,
						 struct ssam_span *data)
{
}

static inline void ssh_ptl_rx_inject_invalid_data(struct ssh_ptl *ptl,
						  struct ssam_span *frame)
{
}

#endif /* CONFIG_SURFACE_AGGREGATOR_ERROR_INJECTION */

static void __ssh_ptl_packet_release(struct kref *kref)
{
	struct ssh_packet *p = container_of(kref, struct ssh_packet, refcnt);

	trace_ssam_packet_release(p);

	ptl_dbg_cond(p->ptl, "ptl: releasing packet %p\n", p);
	p->ops->release(p);
}

/**
 * ssh_packet_get() - Increment reference count of packet.
 * @packet: The packet to increment the reference count of.
 *
 * Increments the reference count of the given packet. See ssh_packet_put()
 * for the counter-part of this function.
 *
 * Return: Returns the packet provided as input.
 */
struct ssh_packet *ssh_packet_get(struct ssh_packet *packet)
{
	if (packet)
		kref_get(&packet->refcnt);
	return packet;
}
EXPORT_SYMBOL_GPL(ssh_packet_get);

/**
 * ssh_packet_put() - Decrement reference count of packet.
 * @packet: The packet to decrement the reference count of.
 *
 * If the reference count reaches zero, the ``release`` callback specified in
 * the packet's &struct ssh_packet_ops, i.e. ``packet->ops->release``, will be
 * called.
 *
 * See ssh_packet_get() for the counter-part of this function.
 */
void ssh_packet_put(struct ssh_packet *packet)
{
	if (packet)
		kref_put(&packet->refcnt, __ssh_ptl_packet_release);
}
EXPORT_SYMBOL_GPL(ssh_packet_put);

static u8 ssh_packet_get_seq(struct ssh_packet *packet)
{
	return packet->data.ptr[SSH_MSGOFFSET_FRAME(seq)];
}

/**
 * ssh_packet_init() - Initialize SSH packet.
 * @packet:   The packet to initialize.
 * @type:     Type-flags of the packet.
 * @priority: Priority of the packet. See SSH_PACKET_PRIORITY() for details.
 * @ops:      Packet operations.
 *
 * Initializes the given SSH packet. Sets the transmission buffer pointer to
 * %NULL and the transmission buffer length to zero. For data-type packets,
 * this buffer has to be set separately via ssh_packet_set_data() before
 * submission, and must contain a valid SSH message, i.e. frame with optional
 * payload of any type.
 */
void ssh_packet_init(struct ssh_packet *packet, unsigned long type,
		     u8 priority, const struct ssh_packet_ops *ops)
{
	kref_init(&packet->refcnt);

	packet->ptl = NULL;
	INIT_LIST_HEAD(&packet->queue_node);
	INIT_LIST_HEAD(&packet->pending_node);

	packet->state = type & SSH_PACKET_FLAGS_TY_MASK;
	packet->priority = priority;
	packet->timestamp = KTIME_MAX;

	packet->data.ptr = NULL;
	packet->data.len = 0;

	packet->ops = ops;
}

static struct kmem_cache *ssh_ctrl_packet_cache;

/**
 * ssh_ctrl_packet_cache_init() - Initialize the control packet cache.
 */
int ssh_ctrl_packet_cache_init(void)
{
	const unsigned int size = sizeof(struct ssh_packet) + SSH_MSG_LEN_CTRL;
	const unsigned int align = __alignof__(struct ssh_packet);
	struct kmem_cache *cache;

	cache = kmem_cache_create("ssam_ctrl_packet", size, align, 0, NULL);
	if (!cache)
		return -ENOMEM;

	ssh_ctrl_packet_cache = cache;
	return 0;
}

/**
 * ssh_ctrl_packet_cache_destroy() - Deinitialize the control packet cache.
 */
void ssh_ctrl_packet_cache_destroy(void)
{
	kmem_cache_destroy(ssh_ctrl_packet_cache);
	ssh_ctrl_packet_cache = NULL;
}

/**
 * ssh_ctrl_packet_alloc() - Allocate packet from control packet cache.
 * @packet: Where the pointer to the newly allocated packet should be stored.
 * @buffer: The buffer corresponding to this packet.
 * @flags:  Flags used for allocation.
 *
 * Allocates a packet and corresponding transport buffer from the control
 * packet cache. Sets the packet's buffer reference to the allocated buffer.
 * The packet must be freed via ssh_ctrl_packet_free(), which will also free
 * the corresponding buffer. The corresponding buffer must not be freed
 * separately. Intended to be used with %ssh_ptl_ctrl_packet_ops as packet
 * operations.
 *
 * Return: Returns zero on success, %-ENOMEM if the allocation failed.
 */
static int ssh_ctrl_packet_alloc(struct ssh_packet **packet,
				 struct ssam_span *buffer, gfp_t flags)
{
	*packet = kmem_cache_alloc(ssh_ctrl_packet_cache, flags);
	if (!*packet)
		return -ENOMEM;

	buffer->ptr = (u8 *)(*packet + 1);
	buffer->len = SSH_MSG_LEN_CTRL;

	trace_ssam_ctrl_packet_alloc(*packet, buffer->len);
	return 0;
}

/**
 * ssh_ctrl_packet_free() - Free packet allocated from control packet cache.
 * @p: The packet to free.
 */
static void ssh_ctrl_packet_free(struct ssh_packet *p)
{
	trace_ssam_ctrl_packet_free(p);
	kmem_cache_free(ssh_ctrl_packet_cache, p);
}

static const struct ssh_packet_ops ssh_ptl_ctrl_packet_ops = {
	.complete = NULL,
	.release = ssh_ctrl_packet_free,
};

static void ssh_ptl_timeout_reaper_mod(struct ssh_ptl *ptl, ktime_t now,
				       ktime_t expires)
{
	unsigned long delta = msecs_to_jiffies(ktime_ms_delta(expires, now));
	ktime_t aexp = ktime_add(expires, SSH_PTL_PACKET_TIMEOUT_RESOLUTION);

	spin_lock(&ptl->rtx_timeout.lock);

	/* Re-adjust / schedule reaper only if it is above resolution delta. */
	if (ktime_before(aexp, ptl->rtx_timeout.expires)) {
		ptl->rtx_timeout.expires = expires;
		mod_delayed_work(system_wq, &ptl->rtx_timeout.reaper, delta);
	}

	spin_unlock(&ptl->rtx_timeout.lock);
}

/* Must be called with queue lock held. */
static void ssh_packet_next_try(struct ssh_packet *p)
{
	u8 base = ssh_packet_priority_get_base(p->priority);
	u8 try = ssh_packet_priority_get_try(p->priority);

	lockdep_assert_held(&p->ptl->queue.lock);

	/*
	 * Ensure that we write the priority in one go via WRITE_ONCE() so we
	 * can access it via READ_ONCE() for tracing. Note that other access
	 * is guarded by the queue lock, so no need to use READ_ONCE() there.
	 */
	WRITE_ONCE(p->priority, __SSH_PACKET_PRIORITY(base, try + 1));
}

/* Must be called with queue lock held. */
static struct list_head *__ssh_ptl_queue_find_entrypoint(struct ssh_packet *p)
{
	struct list_head *head;
	struct ssh_packet *q;

	lockdep_assert_held(&p->ptl->queue.lock);

	/*
	 * We generally assume that there are less control (ACK/NAK) packets
	 * and re-submitted data packets as there are normal data packets (at
	 * least in situations in which many packets are queued; if there
	 * aren't many packets queued the decision on how to iterate should be
	 * basically irrelevant; the number of control/data packets is more or
	 * less limited via the maximum number of pending packets). Thus, when
	 * inserting a control or re-submitted data packet, (determined by
	 * their priority), we search from front to back. Normal data packets
	 * are, usually queued directly at the tail of the queue, so for those
	 * search from back to front.
	 */

	if (p->priority > SSH_PACKET_PRIORITY(DATA, 0)) {
		list_for_each(head, &p->ptl->queue.head) {
			q = list_entry(head, struct ssh_packet, queue_node);

			if (q->priority < p->priority)
				break;
		}
	} else {
		list_for_each_prev(head, &p->ptl->queue.head) {
			q = list_entry(head, struct ssh_packet, queue_node);

			if (q->priority >= p->priority) {
				head = head->next;
				break;
			}
		}
	}

	return head;
}

/* Must be called with queue lock held. */
static int __ssh_ptl_queue_push(struct ssh_packet *packet)
{
	struct ssh_ptl *ptl = packet->ptl;
	struct list_head *head;

	lockdep_assert_held(&ptl->queue.lock);

	if (test_bit(SSH_PTL_SF_SHUTDOWN_BIT, &ptl->state))
		return -ESHUTDOWN;

	/* Avoid further transitions when canceling/completing. */
	if (test_bit(SSH_PACKET_SF_LOCKED_BIT, &packet->state))
		return -EINVAL;

	/* If this packet has already been queued, do not add it. */
	if (test_and_set_bit(SSH_PACKET_SF_QUEUED_BIT, &packet->state))
		return -EALREADY;

	head = __ssh_ptl_queue_find_entrypoint(packet);

	list_add_tail(&ssh_packet_get(packet)->queue_node, head);
	return 0;
}

static int ssh_ptl_queue_push(struct ssh_packet *packet)
{
	int status;

	spin_lock(&packet->ptl->queue.lock);
	status = __ssh_ptl_queue_push(packet);
	spin_unlock(&packet->ptl->queue.lock);

	return status;
}

static void ssh_ptl_queue_remove(struct ssh_packet *packet)
{
	struct ssh_ptl *ptl = packet->ptl;

	spin_lock(&ptl->queue.lock);

	if (!test_and_clear_bit(SSH_PACKET_SF_QUEUED_BIT, &packet->state)) {
		spin_unlock(&ptl->queue.lock);
		return;
	}

	list_del(&packet->queue_node);

	spin_unlock(&ptl->queue.lock);
	ssh_packet_put(packet);
}

static void ssh_ptl_pending_push(struct ssh_packet *p)
{
	struct ssh_ptl *ptl = p->ptl;
	const ktime_t timestamp = ktime_get_coarse_boottime();
	const ktime_t timeout = ptl->rtx_timeout.timeout;

	/*
	 * Note: We can get the time for the timestamp before acquiring the
	 * lock as this is the only place we're setting it and this function
	 * is called only from the transmitter thread. Thus it is not possible
	 * to overwrite the timestamp with an outdated value below.
	 */

	spin_lock(&ptl->pending.lock);

	/* If we are canceling/completing this packet, do not add it. */
	if (test_bit(SSH_PACKET_SF_LOCKED_BIT, &p->state)) {
		spin_unlock(&ptl->pending.lock);
		return;
	}

	/*
	 * On re-submission, the packet has already been added the pending
	 * set. We still need to update the timestamp as the packet timeout is
	 * reset for each (re-)submission.
	 */
	p->timestamp = timestamp;

	/* In case it is already pending (e.g. re-submission), do not add it. */
	if (!test_and_set_bit(SSH_PACKET_SF_PENDING_BIT, &p->state)) {
		atomic_inc(&ptl->pending.count);
		list_add_tail(&ssh_packet_get(p)->pending_node, &ptl->pending.head);
	}

	spin_unlock(&ptl->pending.lock);

	/* Arm/update timeout reaper. */
	ssh_ptl_timeout_reaper_mod(ptl, timestamp, timestamp + timeout);
}

static void ssh_ptl_pending_remove(struct ssh_packet *packet)
{
	struct ssh_ptl *ptl = packet->ptl;

	spin_lock(&ptl->pending.lock);

	if (!test_and_clear_bit(SSH_PACKET_SF_PENDING_BIT, &packet->state)) {
		spin_unlock(&ptl->pending.lock);
		return;
	}

	list_del(&packet->pending_node);
	atomic_dec(&ptl->pending.count);

	spin_unlock(&ptl->pending.lock);

	ssh_packet_put(packet);
}

/* Warning: Does not check/set "completed" bit. */
static void __ssh_ptl_complete(struct ssh_packet *p, int status)
{
	struct ssh_ptl *ptl = READ_ONCE(p->ptl);

	trace_ssam_packet_complete(p, status);
	ptl_dbg_cond(ptl, "ptl: completing packet %p (status: %d)\n", p, status);

	if (p->ops->complete)
		p->ops->complete(p, status);
}

static void ssh_ptl_remove_and_complete(struct ssh_packet *p, int status)
{
	/*
	 * A call to this function should in general be preceded by
	 * set_bit(SSH_PACKET_SF_LOCKED_BIT, &p->flags) to avoid re-adding the
	 * packet to the structures it's going to be removed from.
	 *
	 * The set_bit call does not need explicit memory barriers as the
	 * implicit barrier of the test_and_set_bit() call below ensure that the
	 * flag is visible before we actually attempt to remove the packet.
	 */

	if (test_and_set_bit(SSH_PACKET_SF_COMPLETED_BIT, &p->state))
		return;

	ssh_ptl_queue_remove(p);
	ssh_ptl_pending_remove(p);

	__ssh_ptl_complete(p, status);
}

static bool ssh_ptl_tx_can_process(struct ssh_packet *packet)
{
	struct ssh_ptl *ptl = packet->ptl;

	if (test_bit(SSH_PACKET_TY_FLUSH_BIT, &packet->state))
		return !atomic_read(&ptl->pending.count);

	/* We can always process non-blocking packets. */
	if (!test_bit(SSH_PACKET_TY_BLOCKING_BIT, &packet->state))
		return true;

	/* If we are already waiting for this packet, send it again. */
	if (test_bit(SSH_PACKET_SF_PENDING_BIT, &packet->state))
		return true;

	/* Otherwise: Check if we have the capacity to send. */
	return atomic_read(&ptl->pending.count) < SSH_PTL_MAX_PENDING;
}

static struct ssh_packet *ssh_ptl_tx_pop(struct ssh_ptl *ptl)
{
	struct ssh_packet *packet = ERR_PTR(-ENOENT);
	struct ssh_packet *p, *n;

	spin_lock(&ptl->queue.lock);
	list_for_each_entry_safe(p, n, &ptl->queue.head, queue_node) {
		/*
		 * If we are canceling or completing this packet, ignore it.
		 * It's going to be removed from this queue shortly.
		 */
		if (test_bit(SSH_PACKET_SF_LOCKED_BIT, &p->state))
			continue;

		/*
		 * Packets should be ordered non-blocking/to-be-resent first.
		 * If we cannot process this packet, assume that we can't
		 * process any following packet either and abort.
		 */
		if (!ssh_ptl_tx_can_process(p)) {
			packet = ERR_PTR(-EBUSY);
			break;
		}

		/*
		 * We are allowed to change the state now. Remove it from the
		 * queue and mark it as being transmitted.
		 */

		list_del(&p->queue_node);

		set_bit(SSH_PACKET_SF_TRANSMITTING_BIT, &p->state);
		/* Ensure that state never gets zero. */
		smp_mb__before_atomic();
		clear_bit(SSH_PACKET_SF_QUEUED_BIT, &p->state);

		/*
		 * Update number of tries. This directly influences the
		 * priority in case the packet is re-submitted (e.g. via
		 * timeout/NAK). Note that all reads and writes to the
		 * priority after the first submission are guarded by the
		 * queue lock.
		 */
		ssh_packet_next_try(p);

		packet = p;
		break;
	}
	spin_unlock(&ptl->queue.lock);

	return packet;
}

static struct ssh_packet *ssh_ptl_tx_next(struct ssh_ptl *ptl)
{
	struct ssh_packet *p;

	p = ssh_ptl_tx_pop(ptl);
	if (IS_ERR(p))
		return p;

	if (test_bit(SSH_PACKET_TY_SEQUENCED_BIT, &p->state)) {
		ptl_dbg(ptl, "ptl: transmitting sequenced packet %p\n", p);
		ssh_ptl_pending_push(p);
	} else {
		ptl_dbg(ptl, "ptl: transmitting non-sequenced packet %p\n", p);
	}

	return p;
}

static void ssh_ptl_tx_compl_success(struct ssh_packet *packet)
{
	struct ssh_ptl *ptl = packet->ptl;

	ptl_dbg(ptl, "ptl: successfully transmitted packet %p\n", packet);

	/* Transition state to "transmitted". */
	set_bit(SSH_PACKET_SF_TRANSMITTED_BIT, &packet->state);
	/* Ensure that state never gets zero. */
	smp_mb__before_atomic();
	clear_bit(SSH_PACKET_SF_TRANSMITTING_BIT, &packet->state);

	/* If the packet is unsequenced, we're done: Lock and complete. */
	if (!test_bit(SSH_PACKET_TY_SEQUENCED_BIT, &packet->state)) {
		set_bit(SSH_PACKET_SF_LOCKED_BIT, &packet->state);
		ssh_ptl_remove_and_complete(packet, 0);
	}

	/*
	 * Notify that a packet transmission has finished. In general we're only
	 * waiting for one packet (if any), so wake_up_all should be fine.
	 */
	wake_up_all(&ptl->tx.packet_wq);
}

static void ssh_ptl_tx_compl_error(struct ssh_packet *packet, int status)
{
	/* Transmission failure: Lock the packet and try to complete it. */
	set_bit(SSH_PACKET_SF_LOCKED_BIT, &packet->state);
	/* Ensure that state never gets zero. */
	smp_mb__before_atomic();
	clear_bit(SSH_PACKET_SF_TRANSMITTING_BIT, &packet->state);

	ptl_err(packet->ptl, "ptl: transmission error: %d\n", status);
	ptl_dbg(packet->ptl, "ptl: failed to transmit packet: %p\n", packet);

	ssh_ptl_remove_and_complete(packet, status);

	/*
	 * Notify that a packet transmission has finished. In general we're only
	 * waiting for one packet (if any), so wake_up_all should be fine.
	 */
	wake_up_all(&packet->ptl->tx.packet_wq);
}

static long ssh_ptl_tx_wait_packet(struct ssh_ptl *ptl)
{
	int status;

	status = wait_for_completion_interruptible(&ptl->tx.thread_cplt_pkt);
	reinit_completion(&ptl->tx.thread_cplt_pkt);

	/*
	 * Ensure completion is cleared before continuing to avoid lost update
	 * problems.
	 */
	smp_mb__after_atomic();

	return status;
}

static long ssh_ptl_tx_wait_transfer(struct ssh_ptl *ptl, long timeout)
{
	long status;

	status = wait_for_completion_interruptible_timeout(&ptl->tx.thread_cplt_tx,
							   timeout);
	reinit_completion(&ptl->tx.thread_cplt_tx);

	/*
	 * Ensure completion is cleared before continuing to avoid lost update
	 * problems.
	 */
	smp_mb__after_atomic();

	return status;
}

static int ssh_ptl_tx_packet(struct ssh_ptl *ptl, struct ssh_packet *packet)
{
	long timeout = SSH_PTL_TX_TIMEOUT;
	size_t offset = 0;

	/* Note: Flush-packets don't have any data. */
	if (unlikely(!packet->data.ptr))
		return 0;

	/* Error injection: drop packet to simulate transmission problem. */
	if (ssh_ptl_should_drop_packet(packet))
		return 0;

	/* Error injection: simulate invalid packet data. */
	ssh_ptl_tx_inject_invalid_data(packet);

	ptl_dbg(ptl, "tx: sending data (length: %zu)\n", packet->data.len);
	print_hex_dump_debug("tx: ", DUMP_PREFIX_OFFSET, 16, 1,
			     packet->data.ptr, packet->data.len, false);

	do {
		ssize_t status, len;
		u8 *buf;

		buf = packet->data.ptr + offset;
		len = packet->data.len - offset;

		status = ssh_ptl_write_buf(ptl, packet, buf, len);
		if (status < 0)
			return status;

		if (status == len)
			return 0;

		offset += status;

		timeout = ssh_ptl_tx_wait_transfer(ptl, timeout);
		if (kthread_should_stop() || !atomic_read(&ptl->tx.running))
			return -ESHUTDOWN;

		if (timeout < 0)
			return -EINTR;

		if (timeout == 0)
			return -ETIMEDOUT;
	} while (true);
}

static int ssh_ptl_tx_threadfn(void *data)
{
	struct ssh_ptl *ptl = data;

	while (!kthread_should_stop() && atomic_read(&ptl->tx.running)) {
		struct ssh_packet *packet;
		int status;

		/* Try to get the next packet. */
		packet = ssh_ptl_tx_next(ptl);

		/* If no packet can be processed, we are done. */
		if (IS_ERR(packet)) {
			ssh_ptl_tx_wait_packet(ptl);
			continue;
		}

		/* Transfer and complete packet. */
		status = ssh_ptl_tx_packet(ptl, packet);
		if (status)
			ssh_ptl_tx_compl_error(packet, status);
		else
			ssh_ptl_tx_compl_success(packet);

		ssh_packet_put(packet);
	}

	return 0;
}

/**
 * ssh_ptl_tx_wakeup_packet() - Wake up packet transmitter thread for new
 * packet.
 * @ptl: The packet transport layer.
 *
 * Wakes up the packet transmitter thread, notifying it that a new packet has
 * arrived and is ready for transfer. If the packet transport layer has been
 * shut down, calls to this function will be ignored.
 */
static void ssh_ptl_tx_wakeup_packet(struct ssh_ptl *ptl)
{
	if (test_bit(SSH_PTL_SF_SHUTDOWN_BIT, &ptl->state))
		return;

	complete(&ptl->tx.thread_cplt_pkt);
}

/**
 * ssh_ptl_tx_start() - Start packet transmitter thread.
 * @ptl: The packet transport layer.
 *
 * Return: Returns zero on success, a negative error code on failure.
 */
int ssh_ptl_tx_start(struct ssh_ptl *ptl)
{
	atomic_set_release(&ptl->tx.running, 1);

	ptl->tx.thread = kthread_run(ssh_ptl_tx_threadfn, ptl, "ssam_serial_hub-tx");
	if (IS_ERR(ptl->tx.thread))
		return PTR_ERR(ptl->tx.thread);

	return 0;
}

/**
 * ssh_ptl_tx_stop() - Stop packet transmitter thread.
 * @ptl: The packet transport layer.
 *
 * Return: Returns zero on success, a negative error code on failure.
 */
int ssh_ptl_tx_stop(struct ssh_ptl *ptl)
{
	int status = 0;

	if (!IS_ERR_OR_NULL(ptl->tx.thread)) {
		/* Tell thread to stop. */
		atomic_set_release(&ptl->tx.running, 0);

		/*
		 * Wake up thread in case it is paused. Do not use wakeup
		 * helpers as this may be called when the shutdown bit has
		 * already been set.
		 */
		complete(&ptl->tx.thread_cplt_pkt);
		complete(&ptl->tx.thread_cplt_tx);

		/* Finally, wait for thread to stop. */
		status = kthread_stop(ptl->tx.thread);
		ptl->tx.thread = NULL;
	}

	return status;
}

static struct ssh_packet *ssh_ptl_ack_pop(struct ssh_ptl *ptl, u8 seq_id)
{
	struct ssh_packet *packet = ERR_PTR(-ENOENT);
	struct ssh_packet *p, *n;

	spin_lock(&ptl->pending.lock);
	list_for_each_entry_safe(p, n, &ptl->pending.head, pending_node) {
		/*
		 * We generally expect packets to be in order, so first packet
		 * to be added to pending is first to be sent, is first to be
		 * ACKed.
		 */
		if (unlikely(ssh_packet_get_seq(p) != seq_id))
			continue;

		/*
		 * In case we receive an ACK while handling a transmission
		 * error completion. The packet will be removed shortly.
		 */
		if (unlikely(test_bit(SSH_PACKET_SF_LOCKED_BIT, &p->state))) {
			packet = ERR_PTR(-EPERM);
			break;
		}

		/*
		 * Mark the packet as ACKed and remove it from pending by
		 * removing its node and decrementing the pending counter.
		 */
		set_bit(SSH_PACKET_SF_ACKED_BIT, &p->state);
		/* Ensure that state never gets zero. */
		smp_mb__before_atomic();
		clear_bit(SSH_PACKET_SF_PENDING_BIT, &p->state);

		atomic_dec(&ptl->pending.count);
		list_del(&p->pending_node);
		packet = p;

		break;
	}
	spin_unlock(&ptl->pending.lock);

	return packet;
}

static void ssh_ptl_wait_until_transmitted(struct ssh_packet *packet)
{
	wait_event(packet->ptl->tx.packet_wq,
		   test_bit(SSH_PACKET_SF_TRANSMITTED_BIT, &packet->state) ||
		   test_bit(SSH_PACKET_SF_LOCKED_BIT, &packet->state));
}

static void ssh_ptl_acknowledge(struct ssh_ptl *ptl, u8 seq)
{
	struct ssh_packet *p;

	p = ssh_ptl_ack_pop(ptl, seq);
	if (IS_ERR(p)) {
		if (PTR_ERR(p) == -ENOENT) {
			/*
			 * The packet has not been found in the set of pending
			 * packets.
			 */
			ptl_warn(ptl, "ptl: received ACK for non-pending packet\n");
		} else {
			/*
			 * The packet is pending, but we are not allowed to take
			 * it because it has been locked.
			 */
			WARN_ON(PTR_ERR(p) != -EPERM);
		}
		return;
	}

	ptl_dbg(ptl, "ptl: received ACK for packet %p\n", p);

	/*
	 * It is possible that the packet has been transmitted, but the state
	 * has not been updated from "transmitting" to "transmitted" yet.
	 * In that case, we need to wait for this transition to occur in order
	 * to determine between success or failure.
	 *
	 * On transmission failure, the packet will be locked after this call.
	 * On success, the transmitted bit will be set.
	 */
	ssh_ptl_wait_until_transmitted(p);

	/*
	 * The packet will already be locked in case of a transmission error or
	 * cancellation. Let the transmitter or cancellation issuer complete the
	 * packet.
	 */
	if (unlikely(test_and_set_bit(SSH_PACKET_SF_LOCKED_BIT, &p->state))) {
		if (unlikely(!test_bit(SSH_PACKET_SF_TRANSMITTED_BIT, &p->state)))
			ptl_err(ptl, "ptl: received ACK before packet had been fully transmitted\n");

		ssh_packet_put(p);
		return;
	}

	ssh_ptl_remove_and_complete(p, 0);
	ssh_packet_put(p);

	if (atomic_read(&ptl->pending.count) < SSH_PTL_MAX_PENDING)
		ssh_ptl_tx_wakeup_packet(ptl);
}

/**
 * ssh_ptl_submit() - Submit a packet to the transport layer.
 * @ptl: The packet transport layer to submit the packet to.
 * @p:   The packet to submit.
 *
 * Submits a new packet to the transport layer, queuing it to be sent. This
 * function should not be used for re-submission.
 *
 * Return: Returns zero on success, %-EINVAL if a packet field is invalid or
 * the packet has been canceled prior to submission, %-EALREADY if the packet
 * has already been submitted, or %-ESHUTDOWN if the packet transport layer
 * has been shut down.
 */
int ssh_ptl_submit(struct ssh_ptl *ptl, struct ssh_packet *p)
{
	struct ssh_ptl *ptl_old;
	int status;

	trace_ssam_packet_submit(p);

	/* Validate packet fields. */
	if (test_bit(SSH_PACKET_TY_FLUSH_BIT, &p->state)) {
		if (p->data.ptr || test_bit(SSH_PACKET_TY_SEQUENCED_BIT, &p->state))
			return -EINVAL;
	} else if (!p->data.ptr) {
		return -EINVAL;
	}

	/*
	 * The ptl reference only gets set on or before the first submission.
	 * After the first submission, it has to be read-only.
	 *
	 * Note that ptl may already be set from upper-layer request
	 * submission, thus we cannot expect it to be NULL.
	 */
	ptl_old = READ_ONCE(p->ptl);
	if (!ptl_old)
		WRITE_ONCE(p->ptl, ptl);
	else if (WARN_ON(ptl_old != ptl))
		return -EALREADY;	/* Submitted on different PTL. */

	status = ssh_ptl_queue_push(p);
	if (status)
		return status;

	if (!test_bit(SSH_PACKET_TY_BLOCKING_BIT, &p->state) ||
	    (atomic_read(&ptl->pending.count) < SSH_PTL_MAX_PENDING))
		ssh_ptl_tx_wakeup_packet(ptl);

	return 0;
}

/*
 * __ssh_ptl_resubmit() - Re-submit a packet to the transport layer.
 * @packet: The packet to re-submit.
 *
 * Re-submits the given packet: Checks if it can be re-submitted and queues it
 * if it can, resetting the packet timestamp in the process. Must be called
 * with the pending lock held.
 *
 * Return: Returns %-ECANCELED if the packet has exceeded its number of tries,
 * %-EINVAL if the packet has been locked, %-EALREADY if the packet is already
 * on the queue, and %-ESHUTDOWN if the transmission layer has been shut down.
 */
static int __ssh_ptl_resubmit(struct ssh_packet *packet)
{
	int status;
	u8 try;

	lockdep_assert_held(&packet->ptl->pending.lock);

	trace_ssam_packet_resubmit(packet);

	spin_lock(&packet->ptl->queue.lock);

	/* Check if the packet is out of tries. */
	try = ssh_packet_priority_get_try(packet->priority);
	if (try >= SSH_PTL_MAX_PACKET_TRIES) {
		spin_unlock(&packet->ptl->queue.lock);
		return -ECANCELED;
	}

	status = __ssh_ptl_queue_push(packet);
	if (status) {
		/*
		 * An error here indicates that the packet has either already
		 * been queued, been locked, or the transport layer is being
		 * shut down. In all cases: Ignore the error.
		 */
		spin_unlock(&packet->ptl->queue.lock);
		return status;
	}

	packet->timestamp = KTIME_MAX;

	spin_unlock(&packet->ptl->queue.lock);
	return 0;
}

static void ssh_ptl_resubmit_pending(struct ssh_ptl *ptl)
{
	struct ssh_packet *p;
	bool resub = false;

	/*
	 * Note: We deliberately do not remove/attempt to cancel and complete
	 * packets that are out of tires in this function. The packet will be
	 * eventually canceled and completed by the timeout. Removing the packet
	 * here could lead to overly eager cancellation if the packet has not
	 * been re-transmitted yet but the tries-counter already updated (i.e
	 * ssh_ptl_tx_next() removed the packet from the queue and updated the
	 * counter, but re-transmission for the last try has not actually
	 * started yet).
	 */

	spin_lock(&ptl->pending.lock);

	/* Re-queue all pending packets. */
	list_for_each_entry(p, &ptl->pending.head, pending_node) {
		/*
		 * Re-submission fails if the packet is out of tries, has been
		 * locked, is already queued, or the layer is being shut down.
		 * No need to re-schedule tx-thread in those cases.
		 */
		if (!__ssh_ptl_resubmit(p))
			resub = true;
	}

	spin_unlock(&ptl->pending.lock);

	if (resub)
		ssh_ptl_tx_wakeup_packet(ptl);
}

/**
 * ssh_ptl_cancel() - Cancel a packet.
 * @p: The packet to cancel.
 *
 * Cancels a packet. There are no guarantees on when completion and release
 * callbacks will be called. This may occur during execution of this function
 * or may occur at any point later.
 *
 * Note that it is not guaranteed that the packet will actually be canceled if
 * the packet is concurrently completed by another process. The only guarantee
 * of this function is that the packet will be completed (with success,
 * failure, or cancellation) and released from the transport layer in a
 * reasonable time-frame.
 *
 * May be called before the packet has been submitted, in which case any later
 * packet submission fails.
 */
void ssh_ptl_cancel(struct ssh_packet *p)
{
	if (test_and_set_bit(SSH_PACKET_SF_CANCELED_BIT, &p->state))
		return;

	trace_ssam_packet_cancel(p);

	/*
	 * Lock packet and commit with memory barrier. If this packet has
	 * already been locked, it's going to be removed and completed by
	 * another party, which should have precedence.
	 */
	if (test_and_set_bit(SSH_PACKET_SF_LOCKED_BIT, &p->state))
		return;

	/*
	 * By marking the packet as locked and employing the implicit memory
	 * barrier of test_and_set_bit, we have guaranteed that, at this point,
	 * the packet cannot be added to the queue any more.
	 *
	 * In case the packet has never been submitted, packet->ptl is NULL. If
	 * the packet is currently being submitted, packet->ptl may be NULL or
	 * non-NULL. Due marking the packet as locked above and committing with
	 * the memory barrier, we have guaranteed that, if packet->ptl is NULL,
	 * the packet will never be added to the queue. If packet->ptl is
	 * non-NULL, we don't have any guarantees.
	 */

	if (READ_ONCE(p->ptl)) {
		ssh_ptl_remove_and_complete(p, -ECANCELED);

		if (atomic_read(&p->ptl->pending.count) < SSH_PTL_MAX_PENDING)
			ssh_ptl_tx_wakeup_packet(p->ptl);

	} else if (!test_and_set_bit(SSH_PACKET_SF_COMPLETED_BIT, &p->state)) {
		__ssh_ptl_complete(p, -ECANCELED);
	}
}

/* Must be called with pending lock held */
static ktime_t ssh_packet_get_expiration(struct ssh_packet *p, ktime_t timeout)
{
	lockdep_assert_held(&p->ptl->pending.lock);

	if (p->timestamp != KTIME_MAX)
		return ktime_add(p->timestamp, timeout);
	else
		return KTIME_MAX;
}

static void ssh_ptl_timeout_reap(struct work_struct *work)
{
	struct ssh_ptl *ptl = to_ssh_ptl(work, rtx_timeout.reaper.work);
	struct ssh_packet *p, *n;
	LIST_HEAD(claimed);
	ktime_t now = ktime_get_coarse_boottime();
	ktime_t timeout = ptl->rtx_timeout.timeout;
	ktime_t next = KTIME_MAX;
	bool resub = false;
	int status;

	trace_ssam_ptl_timeout_reap(atomic_read(&ptl->pending.count));

	/*
	 * Mark reaper as "not pending". This is done before checking any
	 * packets to avoid lost-update type problems.
	 */
	spin_lock(&ptl->rtx_timeout.lock);
	ptl->rtx_timeout.expires = KTIME_MAX;
	spin_unlock(&ptl->rtx_timeout.lock);

	spin_lock(&ptl->pending.lock);

	list_for_each_entry_safe(p, n, &ptl->pending.head, pending_node) {
		ktime_t expires = ssh_packet_get_expiration(p, timeout);

		/*
		 * Check if the timeout hasn't expired yet. Find out next
		 * expiration date to be handled after this run.
		 */
		if (ktime_after(expires, now)) {
			next = ktime_before(expires, next) ? expires : next;
			continue;
		}

		trace_ssam_packet_timeout(p);

		status = __ssh_ptl_resubmit(p);

		/*
		 * Re-submission fails if the packet is out of tries, has been
		 * locked, is already queued, or the layer is being shut down.
		 * No need to re-schedule tx-thread in those cases.
		 */
		if (!status)
			resub = true;

		/* Go to next packet if this packet is not out of tries. */
		if (status != -ECANCELED)
			continue;

		/* No more tries left: Cancel the packet. */

		/*
		 * If someone else has locked the packet already, don't use it
		 * and let the other party complete it.
		 */
		if (test_and_set_bit(SSH_PACKET_SF_LOCKED_BIT, &p->state))
			continue;

		/*
		 * We have now marked the packet as locked. Thus it cannot be
		 * added to the pending list again after we've removed it here.
		 * We can therefore re-use the pending_node of this packet
		 * temporarily.
		 */

		clear_bit(SSH_PACKET_SF_PENDING_BIT, &p->state);

		atomic_dec(&ptl->pending.count);
		list_move_tail(&p->pending_node, &claimed);
	}

	spin_unlock(&ptl->pending.lock);

	/* Cancel and complete the packet. */
	list_for_each_entry_safe(p, n, &claimed, pending_node) {
		if (!test_and_set_bit(SSH_PACKET_SF_COMPLETED_BIT, &p->state)) {
			ssh_ptl_queue_remove(p);
			__ssh_ptl_complete(p, -ETIMEDOUT);
		}

		/*
		 * Drop the reference we've obtained by removing it from
		 * the pending set.
		 */
		list_del(&p->pending_node);
		ssh_packet_put(p);
	}

	/* Ensure that reaper doesn't run again immediately. */
	next = max(next, ktime_add(now, SSH_PTL_PACKET_TIMEOUT_RESOLUTION));
	if (next != KTIME_MAX)
		ssh_ptl_timeout_reaper_mod(ptl, now, next);

	if (resub)
		ssh_ptl_tx_wakeup_packet(ptl);
}

static bool ssh_ptl_rx_retransmit_check(struct ssh_ptl *ptl, u8 seq)
{
	int i;

	/*
	 * Check if SEQ has been seen recently (i.e. packet was
	 * re-transmitted and we should ignore it).
	 */
	for (i = 0; i < ARRAY_SIZE(ptl->rx.blocked.seqs); i++) {
		if (likely(ptl->rx.blocked.seqs[i] != seq))
			continue;

		ptl_dbg(ptl, "ptl: ignoring repeated data packet\n");
		return true;
	}

	/* Update list of blocked sequence IDs. */
	ptl->rx.blocked.seqs[ptl->rx.blocked.offset] = seq;
	ptl->rx.blocked.offset = (ptl->rx.blocked.offset + 1)
				  % ARRAY_SIZE(ptl->rx.blocked.seqs);

	return false;
}

static void ssh_ptl_rx_dataframe(struct ssh_ptl *ptl,
				 const struct ssh_frame *frame,
				 const struct ssam_span *payload)
{
	if (ssh_ptl_rx_retransmit_check(ptl, frame->seq))
		return;

	ptl->ops.data_received(ptl, payload);
}

static void ssh_ptl_send_ack(struct ssh_ptl *ptl, u8 seq)
{
	struct ssh_packet *packet;
	struct ssam_span buf;
	struct msgbuf msgb;
	int status;

	status = ssh_ctrl_packet_alloc(&packet, &buf, GFP_KERNEL);
	if (status) {
		ptl_err(ptl, "ptl: failed to allocate ACK packet\n");
		return;
	}

	ssh_packet_init(packet, 0, SSH_PACKET_PRIORITY(ACK, 0),
			&ssh_ptl_ctrl_packet_ops);

	msgb_init(&msgb, buf.ptr, buf.len);
	msgb_push_ack(&msgb, seq);
	ssh_packet_set_data(packet, msgb.begin, msgb_bytes_used(&msgb));

	ssh_ptl_submit(ptl, packet);
	ssh_packet_put(packet);
}

static void ssh_ptl_send_nak(struct ssh_ptl *ptl)
{
	struct ssh_packet *packet;
	struct ssam_span buf;
	struct msgbuf msgb;
	int status;

	status = ssh_ctrl_packet_alloc(&packet, &buf, GFP_KERNEL);
	if (status) {
		ptl_err(ptl, "ptl: failed to allocate NAK packet\n");
		return;
	}

	ssh_packet_init(packet, 0, SSH_PACKET_PRIORITY(NAK, 0),
			&ssh_ptl_ctrl_packet_ops);

	msgb_init(&msgb, buf.ptr, buf.len);
	msgb_push_nak(&msgb);
	ssh_packet_set_data(packet, msgb.begin, msgb_bytes_used(&msgb));

	ssh_ptl_submit(ptl, packet);
	ssh_packet_put(packet);
}

static size_t ssh_ptl_rx_eval(struct ssh_ptl *ptl, struct ssam_span *source)
{
	struct ssh_frame *frame;
	struct ssam_span payload;
	struct ssam_span aligned;
	bool syn_found;
	int status;

	/* Error injection: Modify data to simulate corrupt SYN bytes. */
	ssh_ptl_rx_inject_invalid_syn(ptl, source);

	/* Find SYN. */
	syn_found = sshp_find_syn(source, &aligned);

	if (unlikely(aligned.ptr != source->ptr)) {
		/*
		 * We expect aligned.ptr == source->ptr. If this is not the
		 * case, then aligned.ptr > source->ptr and we've encountered
		 * some unexpected data where we'd expect the start of a new
		 * message (i.e. the SYN sequence).
		 *
		 * This can happen when a CRC check for the previous message
		 * failed and we start actively searching for the next one
		 * (via the call to sshp_find_syn() above), or the first bytes
		 * of a message got dropped or corrupted.
		 *
		 * In any case, we issue a warning, send a NAK to the EC to
		 * request re-transmission of any data we haven't acknowledged
		 * yet, and finally, skip everything up to the next SYN
		 * sequence.
		 */

		ptl_warn(ptl, "rx: parser: invalid start of frame, skipping\n");

		/*
		 * Notes:
		 * - This might send multiple NAKs in case the communication
		 *   starts with an invalid SYN and is broken down into multiple
		 *   pieces. This should generally be handled fine, we just
		 *   might receive duplicate data in this case, which is
		 *   detected when handling data frames.
		 * - This path will also be executed on invalid CRCs: When an
		 *   invalid CRC is encountered, the code below will skip data
		 *   until directly after the SYN. This causes the search for
		 *   the next SYN, which is generally not placed directly after
		 *   the last one.
		 *
		 *   Open question: Should we send this in case of invalid
		 *   payload CRCs if the frame-type is non-sequential (current
		 *   implementation) or should we drop that frame without
		 *   telling the EC?
		 */
		ssh_ptl_send_nak(ptl);
	}

	if (unlikely(!syn_found))
		return aligned.ptr - source->ptr;

	/* Error injection: Modify data to simulate corruption. */
	ssh_ptl_rx_inject_invalid_data(ptl, &aligned);

	/* Parse and validate frame. */
	status = sshp_parse_frame(&ptl->serdev->dev, &aligned, &frame, &payload,
				  SSH_PTL_RX_BUF_LEN);
	if (status)	/* Invalid frame: skip to next SYN. */
		return aligned.ptr - source->ptr + sizeof(u16);
	if (!frame)	/* Not enough data. */
		return aligned.ptr - source->ptr;

	trace_ssam_rx_frame_received(frame);

	switch (frame->type) {
	case SSH_FRAME_TYPE_ACK:
		ssh_ptl_acknowledge(ptl, frame->seq);
		break;

	case SSH_FRAME_TYPE_NAK:
		ssh_ptl_resubmit_pending(ptl);
		break;

	case SSH_FRAME_TYPE_DATA_SEQ:
		ssh_ptl_send_ack(ptl, frame->seq);
		fallthrough;

	case SSH_FRAME_TYPE_DATA_NSQ:
		ssh_ptl_rx_dataframe(ptl, frame, &payload);
		break;

	default:
		ptl_warn(ptl, "ptl: received frame with unknown type %#04x\n",
			 frame->type);
		break;
	}

	return aligned.ptr - source->ptr + SSH_MESSAGE_LENGTH(payload.len);
}

static int ssh_ptl_rx_threadfn(void *data)
{
	struct ssh_ptl *ptl = data;

	while (true) {
		struct ssam_span span;
		size_t offs = 0;
		size_t n;

		wait_event_interruptible(ptl->rx.wq,
					 !kfifo_is_empty(&ptl->rx.fifo) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;

		/* Copy from fifo to evaluation buffer. */
		n = sshp_buf_read_from_fifo(&ptl->rx.buf, &ptl->rx.fifo);

		ptl_dbg(ptl, "rx: received data (size: %zu)\n", n);
		print_hex_dump_debug("rx: ", DUMP_PREFIX_OFFSET, 16, 1,
				     ptl->rx.buf.ptr + ptl->rx.buf.len - n,
				     n, false);

		/* Parse until we need more bytes or buffer is empty. */
		while (offs < ptl->rx.buf.len) {
			sshp_buf_span_from(&ptl->rx.buf, offs, &span);
			n = ssh_ptl_rx_eval(ptl, &span);
			if (n == 0)
				break;	/* Need more bytes. */

			offs += n;
		}

		/* Throw away the evaluated parts. */
		sshp_buf_drop(&ptl->rx.buf, offs);
	}

	return 0;
}

static void ssh_ptl_rx_wakeup(struct ssh_ptl *ptl)
{
	wake_up(&ptl->rx.wq);
}

/**
 * ssh_ptl_rx_start() - Start packet transport layer receiver thread.
 * @ptl: The packet transport layer.
 *
 * Return: Returns zero on success, a negative error code on failure.
 */
int ssh_ptl_rx_start(struct ssh_ptl *ptl)
{
	if (ptl->rx.thread)
		return 0;

	ptl->rx.thread = kthread_run(ssh_ptl_rx_threadfn, ptl,
				     "ssam_serial_hub-rx");
	if (IS_ERR(ptl->rx.thread))
		return PTR_ERR(ptl->rx.thread);

	return 0;
}

/**
 * ssh_ptl_rx_stop() - Stop packet transport layer receiver thread.
 * @ptl: The packet transport layer.
 *
 * Return: Returns zero on success, a negative error code on failure.
 */
int ssh_ptl_rx_stop(struct ssh_ptl *ptl)
{
	int status = 0;

	if (ptl->rx.thread) {
		status = kthread_stop(ptl->rx.thread);
		ptl->rx.thread = NULL;
	}

	return status;
}

/**
 * ssh_ptl_rx_rcvbuf() - Push data from lower-layer transport to the packet
 * layer.
 * @ptl: The packet transport layer.
 * @buf: Pointer to the data to push to the layer.
 * @n:   Size of the data to push to the layer, in bytes.
 *
 * Pushes data from a lower-layer transport to the receiver fifo buffer of the
 * packet layer and notifies the receiver thread. Calls to this function are
 * ignored once the packet layer has been shut down.
 *
 * Return: Returns the number of bytes transferred (positive or zero) on
 * success. Returns %-ESHUTDOWN if the packet layer has been shut down.
 */
int ssh_ptl_rx_rcvbuf(struct ssh_ptl *ptl, const u8 *buf, size_t n)
{
	int used;

	if (test_bit(SSH_PTL_SF_SHUTDOWN_BIT, &ptl->state))
		return -ESHUTDOWN;

	used = kfifo_in(&ptl->rx.fifo, buf, n);
	if (used)
		ssh_ptl_rx_wakeup(ptl);

	return used;
}

/**
 * ssh_ptl_shutdown() - Shut down the packet transport layer.
 * @ptl: The packet transport layer.
 *
 * Shuts down the packet transport layer, removing and canceling all queued
 * and pending packets. Packets canceled by this operation will be completed
 * with %-ESHUTDOWN as status. Receiver and transmitter threads will be
 * stopped.
 *
 * As a result of this function, the transport layer will be marked as shut
 * down. Submission of packets after the transport layer has been shut down
 * will fail with %-ESHUTDOWN.
 */
void ssh_ptl_shutdown(struct ssh_ptl *ptl)
{
	LIST_HEAD(complete_q);
	LIST_HEAD(complete_p);
	struct ssh_packet *p, *n;
	int status;

	/* Ensure that no new packets (including ACK/NAK) can be submitted. */
	set_bit(SSH_PTL_SF_SHUTDOWN_BIT, &ptl->state);
	/*
	 * Ensure that the layer gets marked as shut-down before actually
	 * stopping it. In combination with the check in ssh_ptl_queue_push(),
	 * this guarantees that no new packets can be added and all already
	 * queued packets are properly canceled. In combination with the check
	 * in ssh_ptl_rx_rcvbuf(), this guarantees that received data is
	 * properly cut off.
	 */
	smp_mb__after_atomic();

	status = ssh_ptl_rx_stop(ptl);
	if (status)
		ptl_err(ptl, "ptl: failed to stop receiver thread\n");

	status = ssh_ptl_tx_stop(ptl);
	if (status)
		ptl_err(ptl, "ptl: failed to stop transmitter thread\n");

	cancel_delayed_work_sync(&ptl->rtx_timeout.reaper);

	/*
	 * At this point, all threads have been stopped. This means that the
	 * only references to packets from inside the system are in the queue
	 * and pending set.
	 *
	 * Note: We still need locks here because someone could still be
	 * canceling packets.
	 *
	 * Note 2: We can re-use queue_node (or pending_node) if we mark the
	 * packet as locked an then remove it from the queue (or pending set
	 * respectively). Marking the packet as locked avoids re-queuing
	 * (which should already be prevented by having stopped the treads...)
	 * and not setting QUEUED_BIT (or PENDING_BIT) prevents removal from a
	 * new list via other threads (e.g. cancellation).
	 *
	 * Note 3: There may be overlap between complete_p and complete_q.
	 * This is handled via test_and_set_bit() on the "completed" flag
	 * (also handles cancellation).
	 */

	/* Mark queued packets as locked and move them to complete_q. */
	spin_lock(&ptl->queue.lock);
	list_for_each_entry_safe(p, n, &ptl->queue.head, queue_node) {
		set_bit(SSH_PACKET_SF_LOCKED_BIT, &p->state);
		/* Ensure that state does not get zero. */
		smp_mb__before_atomic();
		clear_bit(SSH_PACKET_SF_QUEUED_BIT, &p->state);

		list_move_tail(&p->queue_node, &complete_q);
	}
	spin_unlock(&ptl->queue.lock);

	/* Mark pending packets as locked and move them to complete_p. */
	spin_lock(&ptl->pending.lock);
	list_for_each_entry_safe(p, n, &ptl->pending.head, pending_node) {
		set_bit(SSH_PACKET_SF_LOCKED_BIT, &p->state);
		/* Ensure that state does not get zero. */
		smp_mb__before_atomic();
		clear_bit(SSH_PACKET_SF_PENDING_BIT, &p->state);

		list_move_tail(&p->pending_node, &complete_q);
	}
	atomic_set(&ptl->pending.count, 0);
	spin_unlock(&ptl->pending.lock);

	/* Complete and drop packets on complete_q. */
	list_for_each_entry(p, &complete_q, queue_node) {
		if (!test_and_set_bit(SSH_PACKET_SF_COMPLETED_BIT, &p->state))
			__ssh_ptl_complete(p, -ESHUTDOWN);

		ssh_packet_put(p);
	}

	/* Complete and drop packets on complete_p. */
	list_for_each_entry(p, &complete_p, pending_node) {
		if (!test_and_set_bit(SSH_PACKET_SF_COMPLETED_BIT, &p->state))
			__ssh_ptl_complete(p, -ESHUTDOWN);

		ssh_packet_put(p);
	}

	/*
	 * At this point we have guaranteed that the system doesn't reference
	 * any packets any more.
	 */
}

/**
 * ssh_ptl_init() - Initialize packet transport layer.
 * @ptl:    The packet transport layer to initialize.
 * @serdev: The underlying serial device, i.e. the lower-level transport.
 * @ops:    Packet layer operations.
 *
 * Initializes the given packet transport layer. Transmitter and receiver
 * threads must be started separately via ssh_ptl_tx_start() and
 * ssh_ptl_rx_start(), after the packet-layer has been initialized and the
 * lower-level transport layer has been set up.
 *
 * Return: Returns zero on success and a nonzero error code on failure.
 */
int ssh_ptl_init(struct ssh_ptl *ptl, struct serdev_device *serdev,
		 struct ssh_ptl_ops *ops)
{
	int i, status;

	ptl->serdev = serdev;
	ptl->state = 0;

	spin_lock_init(&ptl->queue.lock);
	INIT_LIST_HEAD(&ptl->queue.head);

	spin_lock_init(&ptl->pending.lock);
	INIT_LIST_HEAD(&ptl->pending.head);
	atomic_set_release(&ptl->pending.count, 0);

	ptl->tx.thread = NULL;
	atomic_set(&ptl->tx.running, 0);
	init_completion(&ptl->tx.thread_cplt_pkt);
	init_completion(&ptl->tx.thread_cplt_tx);
	init_waitqueue_head(&ptl->tx.packet_wq);

	ptl->rx.thread = NULL;
	init_waitqueue_head(&ptl->rx.wq);

	spin_lock_init(&ptl->rtx_timeout.lock);
	ptl->rtx_timeout.timeout = SSH_PTL_PACKET_TIMEOUT;
	ptl->rtx_timeout.expires = KTIME_MAX;
	INIT_DELAYED_WORK(&ptl->rtx_timeout.reaper, ssh_ptl_timeout_reap);

	ptl->ops = *ops;

	/* Initialize list of recent/blocked SEQs with invalid sequence IDs. */
	for (i = 0; i < ARRAY_SIZE(ptl->rx.blocked.seqs); i++)
		ptl->rx.blocked.seqs[i] = U16_MAX;
	ptl->rx.blocked.offset = 0;

	status = kfifo_alloc(&ptl->rx.fifo, SSH_PTL_RX_FIFO_LEN, GFP_KERNEL);
	if (status)
		return status;

	status = sshp_buf_alloc(&ptl->rx.buf, SSH_PTL_RX_BUF_LEN, GFP_KERNEL);
	if (status)
		kfifo_free(&ptl->rx.fifo);

	return status;
}

/**
 * ssh_ptl_destroy() - Deinitialize packet transport layer.
 * @ptl: The packet transport layer to deinitialize.
 *
 * Deinitializes the given packet transport layer and frees resources
 * associated with it. If receiver and/or transmitter threads have been
 * started, the layer must first be shut down via ssh_ptl_shutdown() before
 * this function can be called.
 */
void ssh_ptl_destroy(struct ssh_ptl *ptl)
{
	kfifo_free(&ptl->rx.fifo);
	sshp_buf_free(&ptl->rx.buf);
}
