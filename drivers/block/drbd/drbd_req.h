/* SPDX-License-Identifier: GPL-2.0-only */
/*
   drbd_req.h

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2006-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 2006-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.
   Copyright (C) 2006-2008, Philipp Reisner <philipp.reisner@linbit.com>.

 */

#ifndef _DRBD_REQ_H
#define _DRBD_REQ_H

#include <linux/module.h>

#include <linux/slab.h>
#include <linux/drbd.h>
#include "drbd_int.h"

/* The request callbacks will be called in irq context by the IDE drivers,
   and in Softirqs/Tasklets/BH context by the SCSI drivers,
   and by the receiver and worker in kernel-thread context.
   Try to get the locking right :) */

/*
 * Objects of type struct drbd_request do only exist on a R_PRIMARY node, and are
 * associated with IO requests originating from the block layer above us.
 *
 * There are quite a few things that may happen to a drbd request
 * during its lifetime.
 *
 *  It will be created.
 *  It will be marked with the intention to be
 *    submitted to local disk and/or
 *    send via the network.
 *
 *  It has to be placed on the transfer log and other housekeeping lists,
 *  In case we have a network connection.
 *
 *  It may be identified as a concurrent (write) request
 *    and be handled accordingly.
 *
 *  It may me handed over to the local disk subsystem.
 *  It may be completed by the local disk subsystem,
 *    either successfully or with io-error.
 *  In case it is a READ request, and it failed locally,
 *    it may be retried remotely.
 *
 *  It may be queued for sending.
 *  It may be handed over to the network stack,
 *    which may fail.
 *  It may be acknowledged by the "peer" according to the wire_protocol in use.
 *    this may be a negative ack.
 *  It may receive a faked ack when the network connection is lost and the
 *  transfer log is cleaned up.
 *  Sending may be canceled due to network connection loss.
 *  When it finally has outlived its time,
 *    corresponding dirty bits in the resync-bitmap may be cleared or set,
 *    it will be destroyed,
 *    and completion will be signalled to the originator,
 *      with or without "success".
 */

enum drbd_req_event {
	CREATED,
	TO_BE_SENT,
	TO_BE_SUBMITTED,

	/* XXX yes, now I am inconsistent...
	 * these are not "events" but "actions"
	 * oh, well... */
	QUEUE_FOR_NET_WRITE,
	QUEUE_FOR_NET_READ,
	QUEUE_FOR_SEND_OOS,

	/* An empty flush is queued as P_BARRIER,
	 * which will cause it to complete "successfully",
	 * even if the local disk flush failed.
	 *
	 * Just like "real" requests, empty flushes (blkdev_issue_flush()) will
	 * only see an error if neither local nor remote data is reachable. */
	QUEUE_AS_DRBD_BARRIER,

	SEND_CANCELED,
	SEND_FAILED,
	HANDED_OVER_TO_NETWORK,
	OOS_HANDED_TO_NETWORK,
	CONNECTION_LOST_WHILE_PENDING,
	READ_RETRY_REMOTE_CANCELED,
	RECV_ACKED_BY_PEER,
	WRITE_ACKED_BY_PEER,
	WRITE_ACKED_BY_PEER_AND_SIS, /* and set_in_sync */
	CONFLICT_RESOLVED,
	POSTPONE_WRITE,
	NEG_ACKED,
	BARRIER_ACKED, /* in protocol A and B */
	DATA_RECEIVED, /* (remote read) */

	COMPLETED_OK,
	READ_COMPLETED_WITH_ERROR,
	READ_AHEAD_COMPLETED_WITH_ERROR,
	WRITE_COMPLETED_WITH_ERROR,
	DISCARD_COMPLETED_NOTSUPP,
	DISCARD_COMPLETED_WITH_ERROR,

	ABORT_DISK_IO,
	RESEND,
	FAIL_FROZEN_DISK_IO,
	RESTART_FROZEN_DISK_IO,
	NOTHING,
};

/* encoding of request states for now.  we don't actually need that many bits.
 * we don't need to do atomic bit operations either, since most of the time we
 * need to look at the connection state and/or manipulate some lists at the
 * same time, so we should hold the request lock anyways.
 */
enum drbd_req_state_bits {
	/* 3210
	 * 0000: no local possible
	 * 0001: to be submitted
	 *    UNUSED, we could map: 011: submitted, completion still pending
	 * 0110: completed ok
	 * 0010: completed with error
	 * 1001: Aborted (before completion)
	 * 1x10: Aborted and completed -> free
	 */
	__RQ_LOCAL_PENDING,
	__RQ_LOCAL_COMPLETED,
	__RQ_LOCAL_OK,
	__RQ_LOCAL_ABORTED,

	/* 87654
	 * 00000: no network possible
	 * 00001: to be send
	 * 00011: to be send, on worker queue
	 * 00101: sent, expecting recv_ack (B) or write_ack (C)
	 * 11101: sent,
	 *        recv_ack (B) or implicit "ack" (A),
	 *        still waiting for the barrier ack.
	 *        master_bio may already be completed and invalidated.
	 * 11100: write acked (C),
	 *        data received (for remote read, any protocol)
	 *        or finally the barrier ack has arrived (B,A)...
	 *        request can be freed
	 * 01100: neg-acked (write, protocol C)
	 *        or neg-d-acked (read, any protocol)
	 *        or killed from the transfer log
	 *        during cleanup after connection loss
	 *        request can be freed
	 * 01000: canceled or send failed...
	 *        request can be freed
	 */

	/* if "SENT" is not set, yet, this can still fail or be canceled.
	 * if "SENT" is set already, we still wait for an Ack packet.
	 * when cleared, the master_bio may be completed.
	 * in (B,A) the request object may still linger on the transaction log
	 * until the corresponding barrier ack comes in */
	__RQ_NET_PENDING,

	/* If it is QUEUED, and it is a WRITE, it is also registered in the
	 * transfer log. Currently we need this flag to avoid conflicts between
	 * worker canceling the request and tl_clear_barrier killing it from
	 * transfer log.  We should restructure the code so this conflict does
	 * no longer occur. */
	__RQ_NET_QUEUED,

	/* well, actually only "handed over to the network stack".
	 *
	 * TODO can potentially be dropped because of the similar meaning
	 * of RQ_NET_SENT and ~RQ_NET_QUEUED.
	 * however it is not exactly the same. before we drop it
	 * we must ensure that we can tell a request with network part
	 * from a request without, regardless of what happens to it. */
	__RQ_NET_SENT,

	/* when set, the request may be freed (if RQ_NET_QUEUED is clear).
	 * basically this means the corresponding P_BARRIER_ACK was received */
	__RQ_NET_DONE,

	/* whether or not we know (C) or pretend (B,A) that the write
	 * was successfully written on the peer.
	 */
	__RQ_NET_OK,

	/* peer called drbd_set_in_sync() for this write */
	__RQ_NET_SIS,

	/* keep this last, its for the RQ_NET_MASK */
	__RQ_NET_MAX,

	/* Set when this is a write, clear for a read */
	__RQ_WRITE,
	__RQ_WSAME,
	__RQ_UNMAP,
	__RQ_ZEROES,

	/* Should call drbd_al_complete_io() for this request... */
	__RQ_IN_ACT_LOG,

	/* This was the most recent request during some blk_finish_plug()
	 * or its implicit from-schedule equivalent.
	 * We may use it as hint to send a P_UNPLUG_REMOTE */
	__RQ_UNPLUG,

	/* The peer has sent a retry ACK */
	__RQ_POSTPONED,

	/* would have been completed,
	 * but was not, because of drbd_suspended() */
	__RQ_COMPLETION_SUSP,

	/* We expect a receive ACK (wire proto B) */
	__RQ_EXP_RECEIVE_ACK,

	/* We expect a write ACK (wite proto C) */
	__RQ_EXP_WRITE_ACK,

	/* waiting for a barrier ack, did an extra kref_get */
	__RQ_EXP_BARR_ACK,
};

#define RQ_LOCAL_PENDING   (1UL << __RQ_LOCAL_PENDING)
#define RQ_LOCAL_COMPLETED (1UL << __RQ_LOCAL_COMPLETED)
#define RQ_LOCAL_OK        (1UL << __RQ_LOCAL_OK)
#define RQ_LOCAL_ABORTED   (1UL << __RQ_LOCAL_ABORTED)

#define RQ_LOCAL_MASK      ((RQ_LOCAL_ABORTED << 1)-1)

#define RQ_NET_PENDING     (1UL << __RQ_NET_PENDING)
#define RQ_NET_QUEUED      (1UL << __RQ_NET_QUEUED)
#define RQ_NET_SENT        (1UL << __RQ_NET_SENT)
#define RQ_NET_DONE        (1UL << __RQ_NET_DONE)
#define RQ_NET_OK          (1UL << __RQ_NET_OK)
#define RQ_NET_SIS         (1UL << __RQ_NET_SIS)

#define RQ_NET_MASK        (((1UL << __RQ_NET_MAX)-1) & ~RQ_LOCAL_MASK)

#define RQ_WRITE           (1UL << __RQ_WRITE)
#define RQ_WSAME           (1UL << __RQ_WSAME)
#define RQ_UNMAP           (1UL << __RQ_UNMAP)
#define RQ_ZEROES          (1UL << __RQ_ZEROES)
#define RQ_IN_ACT_LOG      (1UL << __RQ_IN_ACT_LOG)
#define RQ_UNPLUG          (1UL << __RQ_UNPLUG)
#define RQ_POSTPONED	   (1UL << __RQ_POSTPONED)
#define RQ_COMPLETION_SUSP (1UL << __RQ_COMPLETION_SUSP)
#define RQ_EXP_RECEIVE_ACK (1UL << __RQ_EXP_RECEIVE_ACK)
#define RQ_EXP_WRITE_ACK   (1UL << __RQ_EXP_WRITE_ACK)
#define RQ_EXP_BARR_ACK    (1UL << __RQ_EXP_BARR_ACK)

/* For waking up the frozen transfer log mod_req() has to return if the request
   should be counted in the epoch object*/
#define MR_WRITE       1
#define MR_READ        2

/* Short lived temporary struct on the stack.
 * We could squirrel the error to be returned into
 * bio->bi_iter.bi_size, or similar. But that would be too ugly. */
struct bio_and_error {
	struct bio *bio;
	int error;
};

extern void start_new_tl_epoch(struct drbd_connection *connection);
extern void drbd_req_destroy(struct kref *kref);
extern int __req_mod(struct drbd_request *req, enum drbd_req_event what,
		struct bio_and_error *m);
extern void complete_master_bio(struct drbd_device *device,
		struct bio_and_error *m);
extern void request_timer_fn(struct timer_list *t);
extern void tl_restart(struct drbd_connection *connection, enum drbd_req_event what);
extern void _tl_restart(struct drbd_connection *connection, enum drbd_req_event what);
extern void tl_abort_disk_io(struct drbd_device *device);

/* this is in drbd_main.c */
extern void drbd_restart_request(struct drbd_request *req);

/* use this if you don't want to deal with calling complete_master_bio()
 * outside the spinlock, e.g. when walking some list on cleanup. */
static inline int _req_mod(struct drbd_request *req, enum drbd_req_event what)
{
	struct drbd_device *device = req->device;
	struct bio_and_error m;
	int rv;

	/* __req_mod possibly frees req, do not touch req after that! */
	rv = __req_mod(req, what, &m);
	if (m.bio)
		complete_master_bio(device, &m);

	return rv;
}

/* completion of master bio is outside of our spinlock.
 * We still may or may not be inside some irqs disabled section
 * of the lower level driver completion callback, so we need to
 * spin_lock_irqsave here. */
static inline int req_mod(struct drbd_request *req,
		enum drbd_req_event what)
{
	unsigned long flags;
	struct drbd_device *device = req->device;
	struct bio_and_error m;
	int rv;

	spin_lock_irqsave(&device->resource->req_lock, flags);
	rv = __req_mod(req, what, &m);
	spin_unlock_irqrestore(&device->resource->req_lock, flags);

	if (m.bio)
		complete_master_bio(device, &m);

	return rv;
}

extern bool drbd_should_do_remote(union drbd_dev_state);

#endif
