/*
   drbd_req.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2001-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 1999-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2002-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#include <linux/module.h>

#include <linux/slab.h>
#include <linux/drbd.h>
#include "drbd_int.h"
#include "drbd_req.h"


static bool drbd_may_do_local_read(struct drbd_device *device, sector_t sector, int size);

/* Update disk stats at start of I/O request */
static void _drbd_start_io_acct(struct drbd_device *device, struct drbd_request *req)
{
	const int rw = bio_data_dir(req->master_bio);
	int cpu;
	cpu = part_stat_lock();
	part_round_stats(cpu, &device->vdisk->part0);
	part_stat_inc(cpu, &device->vdisk->part0, ios[rw]);
	part_stat_add(cpu, &device->vdisk->part0, sectors[rw], req->i.size >> 9);
	(void) cpu; /* The macro invocations above want the cpu argument, I do not like
		       the compiler warning about cpu only assigned but never used... */
	part_inc_in_flight(&device->vdisk->part0, rw);
	part_stat_unlock();
}

/* Update disk stats when completing request upwards */
static void _drbd_end_io_acct(struct drbd_device *device, struct drbd_request *req)
{
	int rw = bio_data_dir(req->master_bio);
	unsigned long duration = jiffies - req->start_jif;
	int cpu;
	cpu = part_stat_lock();
	part_stat_add(cpu, &device->vdisk->part0, ticks[rw], duration);
	part_round_stats(cpu, &device->vdisk->part0);
	part_dec_in_flight(&device->vdisk->part0, rw);
	part_stat_unlock();
}

static struct drbd_request *drbd_req_new(struct drbd_device *device,
					       struct bio *bio_src)
{
	struct drbd_request *req;

	req = mempool_alloc(drbd_request_mempool, GFP_NOIO | __GFP_ZERO);
	if (!req)
		return NULL;

	drbd_req_make_private_bio(req, bio_src);
	req->rq_state    = bio_data_dir(bio_src) == WRITE ? RQ_WRITE : 0;
	req->device   = device;
	req->master_bio  = bio_src;
	req->epoch       = 0;

	drbd_clear_interval(&req->i);
	req->i.sector     = bio_src->bi_iter.bi_sector;
	req->i.size      = bio_src->bi_iter.bi_size;
	req->i.local = true;
	req->i.waiting = false;

	INIT_LIST_HEAD(&req->tl_requests);
	INIT_LIST_HEAD(&req->w.list);

	/* one reference to be put by __drbd_make_request */
	atomic_set(&req->completion_ref, 1);
	/* one kref as long as completion_ref > 0 */
	kref_init(&req->kref);
	return req;
}

static void drbd_remove_request_interval(struct rb_root *root,
					 struct drbd_request *req)
{
	struct drbd_device *device = req->device;
	struct drbd_interval *i = &req->i;

	drbd_remove_interval(root, i);

	/* Wake up any processes waiting for this request to complete.  */
	if (i->waiting)
		wake_up(&device->misc_wait);
}

void drbd_req_destroy(struct kref *kref)
{
	struct drbd_request *req = container_of(kref, struct drbd_request, kref);
	struct drbd_device *device = req->device;
	const unsigned s = req->rq_state;

	if ((req->master_bio && !(s & RQ_POSTPONED)) ||
		atomic_read(&req->completion_ref) ||
		(s & RQ_LOCAL_PENDING) ||
		((s & RQ_NET_MASK) && !(s & RQ_NET_DONE))) {
		drbd_err(device, "drbd_req_destroy: Logic BUG rq_state = 0x%x, completion_ref = %d\n",
				s, atomic_read(&req->completion_ref));
		return;
	}

	/* remove it from the transfer log.
	 * well, only if it had been there in the first
	 * place... if it had not (local only or conflicting
	 * and never sent), it should still be "empty" as
	 * initialized in drbd_req_new(), so we can list_del() it
	 * here unconditionally */
	list_del_init(&req->tl_requests);

	/* finally remove the request from the conflict detection
	 * respective block_id verification interval tree. */
	if (!drbd_interval_empty(&req->i)) {
		struct rb_root *root;

		if (s & RQ_WRITE)
			root = &device->write_requests;
		else
			root = &device->read_requests;
		drbd_remove_request_interval(root, req);
	} else if (s & (RQ_NET_MASK & ~RQ_NET_DONE) && req->i.size != 0)
		drbd_err(device, "drbd_req_destroy: Logic BUG: interval empty, but: rq_state=0x%x, sect=%llu, size=%u\n",
			s, (unsigned long long)req->i.sector, req->i.size);

	/* if it was a write, we may have to set the corresponding
	 * bit(s) out-of-sync first. If it had a local part, we need to
	 * release the reference to the activity log. */
	if (s & RQ_WRITE) {
		/* Set out-of-sync unless both OK flags are set
		 * (local only or remote failed).
		 * Other places where we set out-of-sync:
		 * READ with local io-error */

		/* There is a special case:
		 * we may notice late that IO was suspended,
		 * and postpone, or schedule for retry, a write,
		 * before it even was submitted or sent.
		 * In that case we do not want to touch the bitmap at all.
		 */
		if ((s & (RQ_POSTPONED|RQ_LOCAL_MASK|RQ_NET_MASK)) != RQ_POSTPONED) {
			if (!(s & RQ_NET_OK) || !(s & RQ_LOCAL_OK))
				drbd_set_out_of_sync(device, req->i.sector, req->i.size);

			if ((s & RQ_NET_OK) && (s & RQ_LOCAL_OK) && (s & RQ_NET_SIS))
				drbd_set_in_sync(device, req->i.sector, req->i.size);
		}

		/* one might be tempted to move the drbd_al_complete_io
		 * to the local io completion callback drbd_request_endio.
		 * but, if this was a mirror write, we may only
		 * drbd_al_complete_io after this is RQ_NET_DONE,
		 * otherwise the extent could be dropped from the al
		 * before it has actually been written on the peer.
		 * if we crash before our peer knows about the request,
		 * but after the extent has been dropped from the al,
		 * we would forget to resync the corresponding extent.
		 */
		if (s & RQ_IN_ACT_LOG) {
			if (get_ldev_if_state(device, D_FAILED)) {
				drbd_al_complete_io(device, &req->i);
				put_ldev(device);
			} else if (__ratelimit(&drbd_ratelimit_state)) {
				drbd_warn(device, "Should have called drbd_al_complete_io(, %llu, %u), "
					 "but my Disk seems to have failed :(\n",
					 (unsigned long long) req->i.sector, req->i.size);
			}
		}
	}

	mempool_free(req, drbd_request_mempool);
}

static void wake_all_senders(struct drbd_connection *connection)
{
	wake_up(&connection->sender_work.q_wait);
}

/* must hold resource->req_lock */
void start_new_tl_epoch(struct drbd_connection *connection)
{
	/* no point closing an epoch, if it is empty, anyways. */
	if (connection->current_tle_writes == 0)
		return;

	connection->current_tle_writes = 0;
	atomic_inc(&connection->current_tle_nr);
	wake_all_senders(connection);
}

void complete_master_bio(struct drbd_device *device,
		struct bio_and_error *m)
{
	bio_endio(m->bio, m->error);
	dec_ap_bio(device);
}


/* Helper for __req_mod().
 * Set m->bio to the master bio, if it is fit to be completed,
 * or leave it alone (it is initialized to NULL in __req_mod),
 * if it has already been completed, or cannot be completed yet.
 * If m->bio is set, the error status to be returned is placed in m->error.
 */
static
void drbd_req_complete(struct drbd_request *req, struct bio_and_error *m)
{
	const unsigned s = req->rq_state;
	struct drbd_device *device = req->device;
	int rw;
	int error, ok;

	/* we must not complete the master bio, while it is
	 *	still being processed by _drbd_send_zc_bio (drbd_send_dblock)
	 *	not yet acknowledged by the peer
	 *	not yet completed by the local io subsystem
	 * these flags may get cleared in any order by
	 *	the worker,
	 *	the receiver,
	 *	the bio_endio completion callbacks.
	 */
	if ((s & RQ_LOCAL_PENDING && !(s & RQ_LOCAL_ABORTED)) ||
	    (s & RQ_NET_QUEUED) || (s & RQ_NET_PENDING) ||
	    (s & RQ_COMPLETION_SUSP)) {
		drbd_err(device, "drbd_req_complete: Logic BUG rq_state = 0x%x\n", s);
		return;
	}

	if (!req->master_bio) {
		drbd_err(device, "drbd_req_complete: Logic BUG, master_bio == NULL!\n");
		return;
	}

	rw = bio_rw(req->master_bio);

	/*
	 * figure out whether to report success or failure.
	 *
	 * report success when at least one of the operations succeeded.
	 * or, to put the other way,
	 * only report failure, when both operations failed.
	 *
	 * what to do about the failures is handled elsewhere.
	 * what we need to do here is just: complete the master_bio.
	 *
	 * local completion error, if any, has been stored as ERR_PTR
	 * in private_bio within drbd_request_endio.
	 */
	ok = (s & RQ_LOCAL_OK) || (s & RQ_NET_OK);
	error = PTR_ERR(req->private_bio);

	/* Before we can signal completion to the upper layers,
	 * we may need to close the current transfer log epoch.
	 * We are within the request lock, so we can simply compare
	 * the request epoch number with the current transfer log
	 * epoch number.  If they match, increase the current_tle_nr,
	 * and reset the transfer log epoch write_cnt.
	 */
	if (rw == WRITE &&
	    req->epoch == atomic_read(&first_peer_device(device)->connection->current_tle_nr))
		start_new_tl_epoch(first_peer_device(device)->connection);

	/* Update disk stats */
	_drbd_end_io_acct(device, req);

	/* If READ failed,
	 * have it be pushed back to the retry work queue,
	 * so it will re-enter __drbd_make_request(),
	 * and be re-assigned to a suitable local or remote path,
	 * or failed if we do not have access to good data anymore.
	 *
	 * Unless it was failed early by __drbd_make_request(),
	 * because no path was available, in which case
	 * it was not even added to the transfer_log.
	 *
	 * READA may fail, and will not be retried.
	 *
	 * WRITE should have used all available paths already.
	 */
	if (!ok && rw == READ && !list_empty(&req->tl_requests))
		req->rq_state |= RQ_POSTPONED;

	if (!(req->rq_state & RQ_POSTPONED)) {
		m->error = ok ? 0 : (error ?: -EIO);
		m->bio = req->master_bio;
		req->master_bio = NULL;
		/* We leave it in the tree, to be able to verify later
		 * write-acks in protocol != C during resync.
		 * But we mark it as "complete", so it won't be counted as
		 * conflict in a multi-primary setup. */
		req->i.completed = true;
	}

	if (req->i.waiting)
		wake_up(&device->misc_wait);
}

static int drbd_req_put_completion_ref(struct drbd_request *req, struct bio_and_error *m, int put)
{
	struct drbd_device *device = req->device;
	D_ASSERT(device, m || (req->rq_state & RQ_POSTPONED));

	if (!atomic_sub_and_test(put, &req->completion_ref))
		return 0;

	drbd_req_complete(req, m);

	if (req->rq_state & RQ_POSTPONED) {
		/* don't destroy the req object just yet,
		 * but queue it for retry */
		drbd_restart_request(req);
		return 0;
	}

	return 1;
}

/* I'd like this to be the only place that manipulates
 * req->completion_ref and req->kref. */
static void mod_rq_state(struct drbd_request *req, struct bio_and_error *m,
		int clear, int set)
{
	struct drbd_device *device = req->device;
	unsigned s = req->rq_state;
	int c_put = 0;
	int k_put = 0;

	if (drbd_suspended(device) && !((s | clear) & RQ_COMPLETION_SUSP))
		set |= RQ_COMPLETION_SUSP;

	/* apply */

	req->rq_state &= ~clear;
	req->rq_state |= set;

	/* no change? */
	if (req->rq_state == s)
		return;

	/* intent: get references */

	if (!(s & RQ_LOCAL_PENDING) && (set & RQ_LOCAL_PENDING))
		atomic_inc(&req->completion_ref);

	if (!(s & RQ_NET_PENDING) && (set & RQ_NET_PENDING)) {
		inc_ap_pending(device);
		atomic_inc(&req->completion_ref);
	}

	if (!(s & RQ_NET_QUEUED) && (set & RQ_NET_QUEUED)) {
		atomic_inc(&req->completion_ref);
	}

	if (!(s & RQ_EXP_BARR_ACK) && (set & RQ_EXP_BARR_ACK))
		kref_get(&req->kref); /* wait for the DONE */

	if (!(s & RQ_NET_SENT) && (set & RQ_NET_SENT)) {
		/* potentially already completed in the asender thread */
		if (!(s & RQ_NET_DONE))
			atomic_add(req->i.size >> 9, &device->ap_in_flight);
	}

	if (!(s & RQ_COMPLETION_SUSP) && (set & RQ_COMPLETION_SUSP))
		atomic_inc(&req->completion_ref);

	/* progress: put references */

	if ((s & RQ_COMPLETION_SUSP) && (clear & RQ_COMPLETION_SUSP))
		++c_put;

	if (!(s & RQ_LOCAL_ABORTED) && (set & RQ_LOCAL_ABORTED)) {
		D_ASSERT(device, req->rq_state & RQ_LOCAL_PENDING);
		/* local completion may still come in later,
		 * we need to keep the req object around. */
		kref_get(&req->kref);
		++c_put;
	}

	if ((s & RQ_LOCAL_PENDING) && (clear & RQ_LOCAL_PENDING)) {
		if (req->rq_state & RQ_LOCAL_ABORTED)
			++k_put;
		else
			++c_put;
	}

	if ((s & RQ_NET_PENDING) && (clear & RQ_NET_PENDING)) {
		dec_ap_pending(device);
		++c_put;
		req->acked_jif = jiffies;
	}

	if ((s & RQ_NET_QUEUED) && (clear & RQ_NET_QUEUED))
		++c_put;

	if (!(s & RQ_NET_DONE) && (set & RQ_NET_DONE)) {
		if (s & RQ_NET_SENT)
			atomic_sub(req->i.size >> 9, &device->ap_in_flight);
		if (s & RQ_EXP_BARR_ACK)
			++k_put;
		req->net_done_jif = jiffies;
	}

	/* potentially complete and destroy */

	if (k_put || c_put) {
		/* Completion does it's own kref_put.  If we are going to
		 * kref_sub below, we need req to be still around then. */
		int at_least = k_put + !!c_put;
		int refcount = atomic_read(&req->kref.refcount);
		if (refcount < at_least)
			drbd_err(device,
				"mod_rq_state: Logic BUG: %x -> %x: refcount = %d, should be >= %d\n",
				s, req->rq_state, refcount, at_least);
	}

	/* If we made progress, retry conflicting peer requests, if any. */
	if (req->i.waiting)
		wake_up(&device->misc_wait);

	if (c_put)
		k_put += drbd_req_put_completion_ref(req, m, c_put);
	if (k_put)
		kref_sub(&req->kref, k_put, drbd_req_destroy);
}

static void drbd_report_io_error(struct drbd_device *device, struct drbd_request *req)
{
        char b[BDEVNAME_SIZE];

	if (!__ratelimit(&drbd_ratelimit_state))
		return;

	drbd_warn(device, "local %s IO error sector %llu+%u on %s\n",
			(req->rq_state & RQ_WRITE) ? "WRITE" : "READ",
			(unsigned long long)req->i.sector,
			req->i.size >> 9,
			bdevname(device->ldev->backing_bdev, b));
}

/* Helper for HANDED_OVER_TO_NETWORK.
 * Is this a protocol A write (neither WRITE_ACK nor RECEIVE_ACK expected)?
 * Is it also still "PENDING"?
 * --> If so, clear PENDING and set NET_OK below.
 * If it is a protocol A write, but not RQ_PENDING anymore, neg-ack was faster
 * (and we must not set RQ_NET_OK) */
static inline bool is_pending_write_protocol_A(struct drbd_request *req)
{
	return (req->rq_state &
		   (RQ_WRITE|RQ_NET_PENDING|RQ_EXP_WRITE_ACK|RQ_EXP_RECEIVE_ACK))
		== (RQ_WRITE|RQ_NET_PENDING);
}

/* obviously this could be coded as many single functions
 * instead of one huge switch,
 * or by putting the code directly in the respective locations
 * (as it has been before).
 *
 * but having it this way
 *  enforces that it is all in this one place, where it is easier to audit,
 *  it makes it obvious that whatever "event" "happens" to a request should
 *  happen "atomically" within the req_lock,
 *  and it enforces that we have to think in a very structured manner
 *  about the "events" that may happen to a request during its life time ...
 */
int __req_mod(struct drbd_request *req, enum drbd_req_event what,
		struct bio_and_error *m)
{
	struct drbd_device *const device = req->device;
	struct drbd_peer_device *const peer_device = first_peer_device(device);
	struct drbd_connection *const connection = peer_device ? peer_device->connection : NULL;
	struct net_conf *nc;
	int p, rv = 0;

	if (m)
		m->bio = NULL;

	switch (what) {
	default:
		drbd_err(device, "LOGIC BUG in %s:%u\n", __FILE__ , __LINE__);
		break;

	/* does not happen...
	 * initialization done in drbd_req_new
	case CREATED:
		break;
		*/

	case TO_BE_SENT: /* via network */
		/* reached via __drbd_make_request
		 * and from w_read_retry_remote */
		D_ASSERT(device, !(req->rq_state & RQ_NET_MASK));
		rcu_read_lock();
		nc = rcu_dereference(connection->net_conf);
		p = nc->wire_protocol;
		rcu_read_unlock();
		req->rq_state |=
			p == DRBD_PROT_C ? RQ_EXP_WRITE_ACK :
			p == DRBD_PROT_B ? RQ_EXP_RECEIVE_ACK : 0;
		mod_rq_state(req, m, 0, RQ_NET_PENDING);
		break;

	case TO_BE_SUBMITTED: /* locally */
		/* reached via __drbd_make_request */
		D_ASSERT(device, !(req->rq_state & RQ_LOCAL_MASK));
		mod_rq_state(req, m, 0, RQ_LOCAL_PENDING);
		break;

	case COMPLETED_OK:
		if (req->rq_state & RQ_WRITE)
			device->writ_cnt += req->i.size >> 9;
		else
			device->read_cnt += req->i.size >> 9;

		mod_rq_state(req, m, RQ_LOCAL_PENDING,
				RQ_LOCAL_COMPLETED|RQ_LOCAL_OK);
		break;

	case ABORT_DISK_IO:
		mod_rq_state(req, m, 0, RQ_LOCAL_ABORTED);
		break;

	case WRITE_COMPLETED_WITH_ERROR:
		drbd_report_io_error(device, req);
		__drbd_chk_io_error(device, DRBD_WRITE_ERROR);
		mod_rq_state(req, m, RQ_LOCAL_PENDING, RQ_LOCAL_COMPLETED);
		break;

	case READ_COMPLETED_WITH_ERROR:
		drbd_set_out_of_sync(device, req->i.sector, req->i.size);
		drbd_report_io_error(device, req);
		__drbd_chk_io_error(device, DRBD_READ_ERROR);
		/* fall through. */
	case READ_AHEAD_COMPLETED_WITH_ERROR:
		/* it is legal to fail READA, no __drbd_chk_io_error in that case. */
		mod_rq_state(req, m, RQ_LOCAL_PENDING, RQ_LOCAL_COMPLETED);
		break;

	case DISCARD_COMPLETED_NOTSUPP:
	case DISCARD_COMPLETED_WITH_ERROR:
		/* I'd rather not detach from local disk just because it
		 * failed a REQ_DISCARD. */
		mod_rq_state(req, m, RQ_LOCAL_PENDING, RQ_LOCAL_COMPLETED);
		break;

	case QUEUE_FOR_NET_READ:
		/* READ or READA, and
		 * no local disk,
		 * or target area marked as invalid,
		 * or just got an io-error. */
		/* from __drbd_make_request
		 * or from bio_endio during read io-error recovery */

		/* So we can verify the handle in the answer packet.
		 * Corresponding drbd_remove_request_interval is in
		 * drbd_req_complete() */
		D_ASSERT(device, drbd_interval_empty(&req->i));
		drbd_insert_interval(&device->read_requests, &req->i);

		set_bit(UNPLUG_REMOTE, &device->flags);

		D_ASSERT(device, req->rq_state & RQ_NET_PENDING);
		D_ASSERT(device, (req->rq_state & RQ_LOCAL_MASK) == 0);
		mod_rq_state(req, m, 0, RQ_NET_QUEUED);
		req->w.cb = w_send_read_req;
		drbd_queue_work(&connection->sender_work,
				&req->w);
		break;

	case QUEUE_FOR_NET_WRITE:
		/* assert something? */
		/* from __drbd_make_request only */

		/* Corresponding drbd_remove_request_interval is in
		 * drbd_req_complete() */
		D_ASSERT(device, drbd_interval_empty(&req->i));
		drbd_insert_interval(&device->write_requests, &req->i);

		/* NOTE
		 * In case the req ended up on the transfer log before being
		 * queued on the worker, it could lead to this request being
		 * missed during cleanup after connection loss.
		 * So we have to do both operations here,
		 * within the same lock that protects the transfer log.
		 *
		 * _req_add_to_epoch(req); this has to be after the
		 * _maybe_start_new_epoch(req); which happened in
		 * __drbd_make_request, because we now may set the bit
		 * again ourselves to close the current epoch.
		 *
		 * Add req to the (now) current epoch (barrier). */

		/* otherwise we may lose an unplug, which may cause some remote
		 * io-scheduler timeout to expire, increasing maximum latency,
		 * hurting performance. */
		set_bit(UNPLUG_REMOTE, &device->flags);

		/* queue work item to send data */
		D_ASSERT(device, req->rq_state & RQ_NET_PENDING);
		mod_rq_state(req, m, 0, RQ_NET_QUEUED|RQ_EXP_BARR_ACK);
		req->w.cb =  w_send_dblock;
		drbd_queue_work(&connection->sender_work,
				&req->w);

		/* close the epoch, in case it outgrew the limit */
		rcu_read_lock();
		nc = rcu_dereference(connection->net_conf);
		p = nc->max_epoch_size;
		rcu_read_unlock();
		if (connection->current_tle_writes >= p)
			start_new_tl_epoch(connection);

		break;

	case QUEUE_FOR_SEND_OOS:
		mod_rq_state(req, m, 0, RQ_NET_QUEUED);
		req->w.cb =  w_send_out_of_sync;
		drbd_queue_work(&connection->sender_work,
				&req->w);
		break;

	case READ_RETRY_REMOTE_CANCELED:
	case SEND_CANCELED:
	case SEND_FAILED:
		/* real cleanup will be done from tl_clear.  just update flags
		 * so it is no longer marked as on the worker queue */
		mod_rq_state(req, m, RQ_NET_QUEUED, 0);
		break;

	case HANDED_OVER_TO_NETWORK:
		/* assert something? */
		if (is_pending_write_protocol_A(req))
			/* this is what is dangerous about protocol A:
			 * pretend it was successfully written on the peer. */
			mod_rq_state(req, m, RQ_NET_QUEUED|RQ_NET_PENDING,
						RQ_NET_SENT|RQ_NET_OK);
		else
			mod_rq_state(req, m, RQ_NET_QUEUED, RQ_NET_SENT);
		/* It is still not yet RQ_NET_DONE until the
		 * corresponding epoch barrier got acked as well,
		 * so we know what to dirty on connection loss. */
		break;

	case OOS_HANDED_TO_NETWORK:
		/* Was not set PENDING, no longer QUEUED, so is now DONE
		 * as far as this connection is concerned. */
		mod_rq_state(req, m, RQ_NET_QUEUED, RQ_NET_DONE);
		break;

	case CONNECTION_LOST_WHILE_PENDING:
		/* transfer log cleanup after connection loss */
		mod_rq_state(req, m,
				RQ_NET_OK|RQ_NET_PENDING|RQ_COMPLETION_SUSP,
				RQ_NET_DONE);
		break;

	case CONFLICT_RESOLVED:
		/* for superseded conflicting writes of multiple primaries,
		 * there is no need to keep anything in the tl, potential
		 * node crashes are covered by the activity log.
		 *
		 * If this request had been marked as RQ_POSTPONED before,
		 * it will actually not be completed, but "restarted",
		 * resubmitted from the retry worker context. */
		D_ASSERT(device, req->rq_state & RQ_NET_PENDING);
		D_ASSERT(device, req->rq_state & RQ_EXP_WRITE_ACK);
		mod_rq_state(req, m, RQ_NET_PENDING, RQ_NET_DONE|RQ_NET_OK);
		break;

	case WRITE_ACKED_BY_PEER_AND_SIS:
		req->rq_state |= RQ_NET_SIS;
	case WRITE_ACKED_BY_PEER:
		/* Normal operation protocol C: successfully written on peer.
		 * During resync, even in protocol != C,
		 * we requested an explicit write ack anyways.
		 * Which means we cannot even assert anything here.
		 * Nothing more to do here.
		 * We want to keep the tl in place for all protocols, to cater
		 * for volatile write-back caches on lower level devices. */
		goto ack_common;
	case RECV_ACKED_BY_PEER:
		D_ASSERT(device, req->rq_state & RQ_EXP_RECEIVE_ACK);
		/* protocol B; pretends to be successfully written on peer.
		 * see also notes above in HANDED_OVER_TO_NETWORK about
		 * protocol != C */
	ack_common:
		mod_rq_state(req, m, RQ_NET_PENDING, RQ_NET_OK);
		break;

	case POSTPONE_WRITE:
		D_ASSERT(device, req->rq_state & RQ_EXP_WRITE_ACK);
		/* If this node has already detected the write conflict, the
		 * worker will be waiting on misc_wait.  Wake it up once this
		 * request has completed locally.
		 */
		D_ASSERT(device, req->rq_state & RQ_NET_PENDING);
		req->rq_state |= RQ_POSTPONED;
		if (req->i.waiting)
			wake_up(&device->misc_wait);
		/* Do not clear RQ_NET_PENDING. This request will make further
		 * progress via restart_conflicting_writes() or
		 * fail_postponed_requests(). Hopefully. */
		break;

	case NEG_ACKED:
		mod_rq_state(req, m, RQ_NET_OK|RQ_NET_PENDING, 0);
		break;

	case FAIL_FROZEN_DISK_IO:
		if (!(req->rq_state & RQ_LOCAL_COMPLETED))
			break;
		mod_rq_state(req, m, RQ_COMPLETION_SUSP, 0);
		break;

	case RESTART_FROZEN_DISK_IO:
		if (!(req->rq_state & RQ_LOCAL_COMPLETED))
			break;

		mod_rq_state(req, m,
				RQ_COMPLETION_SUSP|RQ_LOCAL_COMPLETED,
				RQ_LOCAL_PENDING);

		rv = MR_READ;
		if (bio_data_dir(req->master_bio) == WRITE)
			rv = MR_WRITE;

		get_ldev(device); /* always succeeds in this call path */
		req->w.cb = w_restart_disk_io;
		drbd_queue_work(&connection->sender_work,
				&req->w);
		break;

	case RESEND:
		/* Simply complete (local only) READs. */
		if (!(req->rq_state & RQ_WRITE) && !req->w.cb) {
			mod_rq_state(req, m, RQ_COMPLETION_SUSP, 0);
			break;
		}

		/* If RQ_NET_OK is already set, we got a P_WRITE_ACK or P_RECV_ACK
		   before the connection loss (B&C only); only P_BARRIER_ACK
		   (or the local completion?) was missing when we suspended.
		   Throwing them out of the TL here by pretending we got a BARRIER_ACK.
		   During connection handshake, we ensure that the peer was not rebooted. */
		if (!(req->rq_state & RQ_NET_OK)) {
			/* FIXME could this possibly be a req->dw.cb == w_send_out_of_sync?
			 * in that case we must not set RQ_NET_PENDING. */

			mod_rq_state(req, m, RQ_COMPLETION_SUSP, RQ_NET_QUEUED|RQ_NET_PENDING);
			if (req->w.cb) {
				/* w.cb expected to be w_send_dblock, or w_send_read_req */
				drbd_queue_work(&connection->sender_work,
						&req->w);
				rv = req->rq_state & RQ_WRITE ? MR_WRITE : MR_READ;
			} /* else: FIXME can this happen? */
			break;
		}
		/* else, fall through to BARRIER_ACKED */

	case BARRIER_ACKED:
		/* barrier ack for READ requests does not make sense */
		if (!(req->rq_state & RQ_WRITE))
			break;

		if (req->rq_state & RQ_NET_PENDING) {
			/* barrier came in before all requests were acked.
			 * this is bad, because if the connection is lost now,
			 * we won't be able to clean them up... */
			drbd_err(device, "FIXME (BARRIER_ACKED but pending)\n");
		}
		/* Allowed to complete requests, even while suspended.
		 * As this is called for all requests within a matching epoch,
		 * we need to filter, and only set RQ_NET_DONE for those that
		 * have actually been on the wire. */
		mod_rq_state(req, m, RQ_COMPLETION_SUSP,
				(req->rq_state & RQ_NET_MASK) ? RQ_NET_DONE : 0);
		break;

	case DATA_RECEIVED:
		D_ASSERT(device, req->rq_state & RQ_NET_PENDING);
		mod_rq_state(req, m, RQ_NET_PENDING, RQ_NET_OK|RQ_NET_DONE);
		break;

	case QUEUE_AS_DRBD_BARRIER:
		start_new_tl_epoch(connection);
		mod_rq_state(req, m, 0, RQ_NET_OK|RQ_NET_DONE);
		break;
	};

	return rv;
}

/* we may do a local read if:
 * - we are consistent (of course),
 * - or we are generally inconsistent,
 *   BUT we are still/already IN SYNC for this area.
 *   since size may be bigger than BM_BLOCK_SIZE,
 *   we may need to check several bits.
 */
static bool drbd_may_do_local_read(struct drbd_device *device, sector_t sector, int size)
{
	unsigned long sbnr, ebnr;
	sector_t esector, nr_sectors;

	if (device->state.disk == D_UP_TO_DATE)
		return true;
	if (device->state.disk != D_INCONSISTENT)
		return false;
	esector = sector + (size >> 9) - 1;
	nr_sectors = drbd_get_capacity(device->this_bdev);
	D_ASSERT(device, sector  < nr_sectors);
	D_ASSERT(device, esector < nr_sectors);

	sbnr = BM_SECT_TO_BIT(sector);
	ebnr = BM_SECT_TO_BIT(esector);

	return drbd_bm_count_bits(device, sbnr, ebnr) == 0;
}

static bool remote_due_to_read_balancing(struct drbd_device *device, sector_t sector,
		enum drbd_read_balancing rbm)
{
	struct backing_dev_info *bdi;
	int stripe_shift;

	switch (rbm) {
	case RB_CONGESTED_REMOTE:
		bdi = &device->ldev->backing_bdev->bd_disk->queue->backing_dev_info;
		return bdi_read_congested(bdi);
	case RB_LEAST_PENDING:
		return atomic_read(&device->local_cnt) >
			atomic_read(&device->ap_pending_cnt) + atomic_read(&device->rs_pending_cnt);
	case RB_32K_STRIPING:  /* stripe_shift = 15 */
	case RB_64K_STRIPING:
	case RB_128K_STRIPING:
	case RB_256K_STRIPING:
	case RB_512K_STRIPING:
	case RB_1M_STRIPING:   /* stripe_shift = 20 */
		stripe_shift = (rbm - RB_32K_STRIPING + 15);
		return (sector >> (stripe_shift - 9)) & 1;
	case RB_ROUND_ROBIN:
		return test_and_change_bit(READ_BALANCE_RR, &device->flags);
	case RB_PREFER_REMOTE:
		return true;
	case RB_PREFER_LOCAL:
	default:
		return false;
	}
}

/*
 * complete_conflicting_writes  -  wait for any conflicting write requests
 *
 * The write_requests tree contains all active write requests which we
 * currently know about.  Wait for any requests to complete which conflict with
 * the new one.
 *
 * Only way out: remove the conflicting intervals from the tree.
 */
static void complete_conflicting_writes(struct drbd_request *req)
{
	DEFINE_WAIT(wait);
	struct drbd_device *device = req->device;
	struct drbd_interval *i;
	sector_t sector = req->i.sector;
	int size = req->i.size;

	i = drbd_find_overlap(&device->write_requests, sector, size);
	if (!i)
		return;

	for (;;) {
		prepare_to_wait(&device->misc_wait, &wait, TASK_UNINTERRUPTIBLE);
		i = drbd_find_overlap(&device->write_requests, sector, size);
		if (!i)
			break;
		/* Indicate to wake up device->misc_wait on progress.  */
		i->waiting = true;
		spin_unlock_irq(&device->resource->req_lock);
		schedule();
		spin_lock_irq(&device->resource->req_lock);
	}
	finish_wait(&device->misc_wait, &wait);
}

/* called within req_lock and rcu_read_lock() */
static void maybe_pull_ahead(struct drbd_device *device)
{
	struct drbd_connection *connection = first_peer_device(device)->connection;
	struct net_conf *nc;
	bool congested = false;
	enum drbd_on_congestion on_congestion;

	rcu_read_lock();
	nc = rcu_dereference(connection->net_conf);
	on_congestion = nc ? nc->on_congestion : OC_BLOCK;
	rcu_read_unlock();
	if (on_congestion == OC_BLOCK ||
	    connection->agreed_pro_version < 96)
		return;

	if (on_congestion == OC_PULL_AHEAD && device->state.conn == C_AHEAD)
		return; /* nothing to do ... */

	/* If I don't even have good local storage, we can not reasonably try
	 * to pull ahead of the peer. We also need the local reference to make
	 * sure device->act_log is there.
	 */
	if (!get_ldev_if_state(device, D_UP_TO_DATE))
		return;

	if (nc->cong_fill &&
	    atomic_read(&device->ap_in_flight) >= nc->cong_fill) {
		drbd_info(device, "Congestion-fill threshold reached\n");
		congested = true;
	}

	if (device->act_log->used >= nc->cong_extents) {
		drbd_info(device, "Congestion-extents threshold reached\n");
		congested = true;
	}

	if (congested) {
		/* start a new epoch for non-mirrored writes */
		start_new_tl_epoch(first_peer_device(device)->connection);

		if (on_congestion == OC_PULL_AHEAD)
			_drbd_set_state(_NS(device, conn, C_AHEAD), 0, NULL);
		else  /*nc->on_congestion == OC_DISCONNECT */
			_drbd_set_state(_NS(device, conn, C_DISCONNECTING), 0, NULL);
	}
	put_ldev(device);
}

/* If this returns false, and req->private_bio is still set,
 * this should be submitted locally.
 *
 * If it returns false, but req->private_bio is not set,
 * we do not have access to good data :(
 *
 * Otherwise, this destroys req->private_bio, if any,
 * and returns true.
 */
static bool do_remote_read(struct drbd_request *req)
{
	struct drbd_device *device = req->device;
	enum drbd_read_balancing rbm;

	if (req->private_bio) {
		if (!drbd_may_do_local_read(device,
					req->i.sector, req->i.size)) {
			bio_put(req->private_bio);
			req->private_bio = NULL;
			put_ldev(device);
		}
	}

	if (device->state.pdsk != D_UP_TO_DATE)
		return false;

	if (req->private_bio == NULL)
		return true;

	/* TODO: improve read balancing decisions, take into account drbd
	 * protocol, pending requests etc. */

	rcu_read_lock();
	rbm = rcu_dereference(device->ldev->disk_conf)->read_balancing;
	rcu_read_unlock();

	if (rbm == RB_PREFER_LOCAL && req->private_bio)
		return false; /* submit locally */

	if (remote_due_to_read_balancing(device, req->i.sector, rbm)) {
		if (req->private_bio) {
			bio_put(req->private_bio);
			req->private_bio = NULL;
			put_ldev(device);
		}
		return true;
	}

	return false;
}

/* returns number of connections (== 1, for drbd 8.4)
 * expected to actually write this data,
 * which does NOT include those that we are L_AHEAD for. */
static int drbd_process_write_request(struct drbd_request *req)
{
	struct drbd_device *device = req->device;
	int remote, send_oos;

	remote = drbd_should_do_remote(device->state);
	send_oos = drbd_should_send_out_of_sync(device->state);

	/* Need to replicate writes.  Unless it is an empty flush,
	 * which is better mapped to a DRBD P_BARRIER packet,
	 * also for drbd wire protocol compatibility reasons.
	 * If this was a flush, just start a new epoch.
	 * Unless the current epoch was empty anyways, or we are not currently
	 * replicating, in which case there is no point. */
	if (unlikely(req->i.size == 0)) {
		/* The only size==0 bios we expect are empty flushes. */
		D_ASSERT(device, req->master_bio->bi_rw & REQ_FLUSH);
		if (remote)
			_req_mod(req, QUEUE_AS_DRBD_BARRIER);
		return remote;
	}

	if (!remote && !send_oos)
		return 0;

	D_ASSERT(device, !(remote && send_oos));

	if (remote) {
		_req_mod(req, TO_BE_SENT);
		_req_mod(req, QUEUE_FOR_NET_WRITE);
	} else if (drbd_set_out_of_sync(device, req->i.sector, req->i.size))
		_req_mod(req, QUEUE_FOR_SEND_OOS);

	return remote;
}

static void
drbd_submit_req_private_bio(struct drbd_request *req)
{
	struct drbd_device *device = req->device;
	struct bio *bio = req->private_bio;
	const int rw = bio_rw(bio);

	bio->bi_bdev = device->ldev->backing_bdev;

	/* State may have changed since we grabbed our reference on the
	 * ->ldev member. Double check, and short-circuit to endio.
	 * In case the last activity log transaction failed to get on
	 * stable storage, and this is a WRITE, we may not even submit
	 * this bio. */
	if (get_ldev(device)) {
		req->pre_submit_jif = jiffies;
		if (drbd_insert_fault(device,
				      rw == WRITE ? DRBD_FAULT_DT_WR
				    : rw == READ  ? DRBD_FAULT_DT_RD
				    :               DRBD_FAULT_DT_RA))
			bio_endio(bio, -EIO);
		else
			generic_make_request(bio);
		put_ldev(device);
	} else
		bio_endio(bio, -EIO);
}

static void drbd_queue_write(struct drbd_device *device, struct drbd_request *req)
{
	spin_lock(&device->submit.lock);
	list_add_tail(&req->tl_requests, &device->submit.writes);
	spin_unlock(&device->submit.lock);
	queue_work(device->submit.wq, &device->submit.worker);
}

/* returns the new drbd_request pointer, if the caller is expected to
 * drbd_send_and_submit() it (to save latency), or NULL if we queued the
 * request on the submitter thread.
 * Returns ERR_PTR(-ENOMEM) if we cannot allocate a drbd_request.
 */
static struct drbd_request *
drbd_request_prepare(struct drbd_device *device, struct bio *bio, unsigned long start_jif)
{
	const int rw = bio_data_dir(bio);
	struct drbd_request *req;

	/* allocate outside of all locks; */
	req = drbd_req_new(device, bio);
	if (!req) {
		dec_ap_bio(device);
		/* only pass the error to the upper layers.
		 * if user cannot handle io errors, that's not our business. */
		drbd_err(device, "could not kmalloc() req\n");
		bio_endio(bio, -ENOMEM);
		return ERR_PTR(-ENOMEM);
	}
	req->start_jif = start_jif;

	if (!get_ldev(device)) {
		bio_put(req->private_bio);
		req->private_bio = NULL;
	}

	/* Update disk stats */
	_drbd_start_io_acct(device, req);

	if (rw == WRITE && req->private_bio && req->i.size
	&& !test_bit(AL_SUSPENDED, &device->flags)) {
		if (!drbd_al_begin_io_fastpath(device, &req->i)) {
			drbd_queue_write(device, req);
			return NULL;
		}
		req->rq_state |= RQ_IN_ACT_LOG;
		req->in_actlog_jif = jiffies;
	}

	return req;
}

static void drbd_send_and_submit(struct drbd_device *device, struct drbd_request *req)
{
	struct drbd_resource *resource = device->resource;
	const int rw = bio_rw(req->master_bio);
	struct bio_and_error m = { NULL, };
	bool no_remote = false;
	bool submit_private_bio = false;

	spin_lock_irq(&resource->req_lock);
	if (rw == WRITE) {
		/* This may temporarily give up the req_lock,
		 * but will re-aquire it before it returns here.
		 * Needs to be before the check on drbd_suspended() */
		complete_conflicting_writes(req);
		/* no more giving up req_lock from now on! */

		/* check for congestion, and potentially stop sending
		 * full data updates, but start sending "dirty bits" only. */
		maybe_pull_ahead(device);
	}


	if (drbd_suspended(device)) {
		/* push back and retry: */
		req->rq_state |= RQ_POSTPONED;
		if (req->private_bio) {
			bio_put(req->private_bio);
			req->private_bio = NULL;
			put_ldev(device);
		}
		goto out;
	}

	/* We fail READ/READA early, if we can not serve it.
	 * We must do this before req is registered on any lists.
	 * Otherwise, drbd_req_complete() will queue failed READ for retry. */
	if (rw != WRITE) {
		if (!do_remote_read(req) && !req->private_bio)
			goto nodata;
	}

	/* which transfer log epoch does this belong to? */
	req->epoch = atomic_read(&first_peer_device(device)->connection->current_tle_nr);

	/* no point in adding empty flushes to the transfer log,
	 * they are mapped to drbd barriers already. */
	if (likely(req->i.size!=0)) {
		if (rw == WRITE)
			first_peer_device(device)->connection->current_tle_writes++;

		list_add_tail(&req->tl_requests, &first_peer_device(device)->connection->transfer_log);
	}

	if (rw == WRITE) {
		if (!drbd_process_write_request(req))
			no_remote = true;
	} else {
		/* We either have a private_bio, or we can read from remote.
		 * Otherwise we had done the goto nodata above. */
		if (req->private_bio == NULL) {
			_req_mod(req, TO_BE_SENT);
			_req_mod(req, QUEUE_FOR_NET_READ);
		} else
			no_remote = true;
	}

	if (req->private_bio) {
		/* needs to be marked within the same spinlock */
		_req_mod(req, TO_BE_SUBMITTED);
		/* but we need to give up the spinlock to submit */
		submit_private_bio = true;
	} else if (no_remote) {
nodata:
		if (__ratelimit(&drbd_ratelimit_state))
			drbd_err(device, "IO ERROR: neither local nor remote data, sector %llu+%u\n",
					(unsigned long long)req->i.sector, req->i.size >> 9);
		/* A write may have been queued for send_oos, however.
		 * So we can not simply free it, we must go through drbd_req_put_completion_ref() */
	}

out:
	if (drbd_req_put_completion_ref(req, &m, 1))
		kref_put(&req->kref, drbd_req_destroy);
	spin_unlock_irq(&resource->req_lock);

	/* Even though above is a kref_put(), this is safe.
	 * As long as we still need to submit our private bio,
	 * we hold a completion ref, and the request cannot disappear.
	 * If however this request did not even have a private bio to submit
	 * (e.g. remote read), req may already be invalid now.
	 * That's why we cannot check on req->private_bio. */
	if (submit_private_bio)
		drbd_submit_req_private_bio(req);
	if (m.bio)
		complete_master_bio(device, &m);
}

void __drbd_make_request(struct drbd_device *device, struct bio *bio, unsigned long start_jif)
{
	struct drbd_request *req = drbd_request_prepare(device, bio, start_jif);
	if (IS_ERR_OR_NULL(req))
		return;
	drbd_send_and_submit(device, req);
}

static void submit_fast_path(struct drbd_device *device, struct list_head *incoming)
{
	struct drbd_request *req, *tmp;
	list_for_each_entry_safe(req, tmp, incoming, tl_requests) {
		const int rw = bio_data_dir(req->master_bio);

		if (rw == WRITE /* rw != WRITE should not even end up here! */
		&& req->private_bio && req->i.size
		&& !test_bit(AL_SUSPENDED, &device->flags)) {
			if (!drbd_al_begin_io_fastpath(device, &req->i))
				continue;

			req->rq_state |= RQ_IN_ACT_LOG;
			req->in_actlog_jif = jiffies;
		}

		list_del_init(&req->tl_requests);
		drbd_send_and_submit(device, req);
	}
}

static bool prepare_al_transaction_nonblock(struct drbd_device *device,
					    struct list_head *incoming,
					    struct list_head *pending)
{
	struct drbd_request *req, *tmp;
	int wake = 0;
	int err;

	spin_lock_irq(&device->al_lock);
	list_for_each_entry_safe(req, tmp, incoming, tl_requests) {
		err = drbd_al_begin_io_nonblock(device, &req->i);
		if (err == -EBUSY)
			wake = 1;
		if (err)
			continue;
		list_move_tail(&req->tl_requests, pending);
	}
	spin_unlock_irq(&device->al_lock);
	if (wake)
		wake_up(&device->al_wait);

	return !list_empty(pending);
}

void do_submit(struct work_struct *ws)
{
	struct drbd_device *device = container_of(ws, struct drbd_device, submit.worker);
	LIST_HEAD(incoming);
	LIST_HEAD(pending);
	struct drbd_request *req, *tmp;

	for (;;) {
		spin_lock(&device->submit.lock);
		list_splice_tail_init(&device->submit.writes, &incoming);
		spin_unlock(&device->submit.lock);

		submit_fast_path(device, &incoming);
		if (list_empty(&incoming))
			break;

skip_fast_path:
		wait_event(device->al_wait, prepare_al_transaction_nonblock(device, &incoming, &pending));
		/* Maybe more was queued, while we prepared the transaction?
		 * Try to stuff them into this transaction as well.
		 * Be strictly non-blocking here, no wait_event, we already
		 * have something to commit.
		 * Stop if we don't make any more progres.
		 */
		for (;;) {
			LIST_HEAD(more_pending);
			LIST_HEAD(more_incoming);
			bool made_progress;

			/* It is ok to look outside the lock,
			 * it's only an optimization anyways */
			if (list_empty(&device->submit.writes))
				break;

			spin_lock(&device->submit.lock);
			list_splice_tail_init(&device->submit.writes, &more_incoming);
			spin_unlock(&device->submit.lock);

			if (list_empty(&more_incoming))
				break;

			made_progress = prepare_al_transaction_nonblock(device, &more_incoming, &more_pending);

			list_splice_tail_init(&more_pending, &pending);
			list_splice_tail_init(&more_incoming, &incoming);

			if (!made_progress)
				break;
		}
		drbd_al_begin_io_commit(device);

		list_for_each_entry_safe(req, tmp, &pending, tl_requests) {
			req->rq_state |= RQ_IN_ACT_LOG;
			req->in_actlog_jif = jiffies;
			list_del_init(&req->tl_requests);
			drbd_send_and_submit(device, req);
		}

		/* If all currently hot activity log extents are kept busy by
		 * incoming requests, we still must not totally starve new
		 * requests to cold extents. In that case, prepare one request
		 * in blocking mode. */
		list_for_each_entry_safe(req, tmp, &incoming, tl_requests) {
			bool was_cold;
			list_del_init(&req->tl_requests);
			was_cold = drbd_al_begin_io_prepare(device, &req->i);
			if (!was_cold) {
				req->rq_state |= RQ_IN_ACT_LOG;
				req->in_actlog_jif = jiffies;
				/* Corresponding extent was hot after all? */
				drbd_send_and_submit(device, req);
			} else {
				/* Found a request to a cold extent.
				 * Put on "pending" list,
				 * and try to cumulate with more. */
				list_add(&req->tl_requests, &pending);
				goto skip_fast_path;
			}
		}
	}
}

void drbd_make_request(struct request_queue *q, struct bio *bio)
{
	struct drbd_device *device = (struct drbd_device *) q->queuedata;
	unsigned long start_jif;

	start_jif = jiffies;

	/*
	 * what we "blindly" assume:
	 */
	D_ASSERT(device, IS_ALIGNED(bio->bi_iter.bi_size, 512));

	inc_ap_bio(device);
	__drbd_make_request(device, bio, start_jif);
}

/* This is called by bio_add_page().
 *
 * q->max_hw_sectors and other global limits are already enforced there.
 *
 * We need to call down to our lower level device,
 * in case it has special restrictions.
 *
 * We also may need to enforce configured max-bio-bvecs limits.
 *
 * As long as the BIO is empty we have to allow at least one bvec,
 * regardless of size and offset, so no need to ask lower levels.
 */
int drbd_merge_bvec(struct request_queue *q, struct bvec_merge_data *bvm, struct bio_vec *bvec)
{
	struct drbd_device *device = (struct drbd_device *) q->queuedata;
	unsigned int bio_size = bvm->bi_size;
	int limit = DRBD_MAX_BIO_SIZE;
	int backing_limit;

	if (bio_size && get_ldev(device)) {
		unsigned int max_hw_sectors = queue_max_hw_sectors(q);
		struct request_queue * const b =
			device->ldev->backing_bdev->bd_disk->queue;
		if (b->merge_bvec_fn) {
			backing_limit = b->merge_bvec_fn(b, bvm, bvec);
			limit = min(limit, backing_limit);
		}
		put_ldev(device);
		if ((limit >> 9) > max_hw_sectors)
			limit = max_hw_sectors << 9;
	}
	return limit;
}

static void find_oldest_requests(
		struct drbd_connection *connection,
		struct drbd_device *device,
		struct drbd_request **oldest_req_waiting_for_peer,
		struct drbd_request **oldest_req_waiting_for_disk)
{
	struct drbd_request *r;
	*oldest_req_waiting_for_peer = NULL;
	*oldest_req_waiting_for_disk = NULL;
	list_for_each_entry(r, &connection->transfer_log, tl_requests) {
		const unsigned s = r->rq_state;
		if (!*oldest_req_waiting_for_peer
		&& ((s & RQ_NET_MASK) && !(s & RQ_NET_DONE)))
			*oldest_req_waiting_for_peer = r;

		if (!*oldest_req_waiting_for_disk
		&& (s & RQ_LOCAL_PENDING) && r->device == device)
			*oldest_req_waiting_for_disk = r;

		if (*oldest_req_waiting_for_peer && *oldest_req_waiting_for_disk)
			break;
	}
}

void request_timer_fn(unsigned long data)
{
	struct drbd_device *device = (struct drbd_device *) data;
	struct drbd_connection *connection = first_peer_device(device)->connection;
	struct drbd_request *req_disk, *req_peer; /* oldest request */
	struct net_conf *nc;
	unsigned long ent = 0, dt = 0, et, nt; /* effective timeout = ko_count * timeout */
	unsigned long now;

	rcu_read_lock();
	nc = rcu_dereference(connection->net_conf);
	if (nc && device->state.conn >= C_WF_REPORT_PARAMS)
		ent = nc->timeout * HZ/10 * nc->ko_count;

	if (get_ldev(device)) { /* implicit state.disk >= D_INCONSISTENT */
		dt = rcu_dereference(device->ldev->disk_conf)->disk_timeout * HZ / 10;
		put_ldev(device);
	}
	rcu_read_unlock();

	et = min_not_zero(dt, ent);

	if (!et)
		return; /* Recurring timer stopped */

	now = jiffies;

	spin_lock_irq(&device->resource->req_lock);
	find_oldest_requests(connection, device, &req_peer, &req_disk);
	if (req_peer == NULL && req_disk == NULL) {
		spin_unlock_irq(&device->resource->req_lock);
		mod_timer(&device->request_timer, now + et);
		return;
	}

	/* The request is considered timed out, if
	 * - we have some effective timeout from the configuration,
	 *   with above state restrictions applied,
	 * - the oldest request is waiting for a response from the network
	 *   resp. the local disk,
	 * - the oldest request is in fact older than the effective timeout,
	 * - the connection was established (resp. disk was attached)
	 *   for longer than the timeout already.
	 * Note that for 32bit jiffies and very stable connections/disks,
	 * we may have a wrap around, which is catched by
	 *   !time_in_range(now, last_..._jif, last_..._jif + timeout).
	 *
	 * Side effect: once per 32bit wrap-around interval, which means every
	 * ~198 days with 250 HZ, we have a window where the timeout would need
	 * to expire twice (worst case) to become effective. Good enough.
	 */
	if (ent && req_peer &&
		 time_after(now, req_peer->start_jif + ent) &&
		!time_in_range(now, connection->last_reconnect_jif, connection->last_reconnect_jif + ent)) {
		drbd_warn(device, "Remote failed to finish a request within ko-count * timeout\n");
		_drbd_set_state(_NS(device, conn, C_TIMEOUT), CS_VERBOSE | CS_HARD, NULL);
	}
	if (dt && req_disk &&
		 time_after(now, req_disk->start_jif + dt) &&
		!time_in_range(now, device->last_reattach_jif, device->last_reattach_jif + dt)) {
		drbd_warn(device, "Local backing device failed to meet the disk-timeout\n");
		__drbd_chk_io_error(device, DRBD_FORCE_DETACH);
	}

	/* Reschedule timer for the nearest not already expired timeout.
	 * Fallback to now + min(effective network timeout, disk timeout). */
	ent = (ent && req_peer && time_before(now, req_peer->start_jif + ent))
		? req_peer->start_jif + ent : now + et;
	dt = (dt && req_disk && time_before(now, req_disk->start_jif + dt))
		? req_disk->start_jif + dt : now + et;
	nt = time_before(ent, dt) ? ent : dt;
	spin_unlock_irq(&connection->resource->req_lock);
	mod_timer(&device->request_timer, nt);
}
