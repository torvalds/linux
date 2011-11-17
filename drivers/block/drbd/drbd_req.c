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


static bool drbd_may_do_local_read(struct drbd_conf *mdev, sector_t sector, int size);

/* Update disk stats at start of I/O request */
static void _drbd_start_io_acct(struct drbd_conf *mdev, struct drbd_request *req, struct bio *bio)
{
	const int rw = bio_data_dir(bio);
	int cpu;
	cpu = part_stat_lock();
	part_round_stats(cpu, &mdev->vdisk->part0);
	part_stat_inc(cpu, &mdev->vdisk->part0, ios[rw]);
	part_stat_add(cpu, &mdev->vdisk->part0, sectors[rw], bio_sectors(bio));
	(void) cpu; /* The macro invocations above want the cpu argument, I do not like
		       the compiler warning about cpu only assigned but never used... */
	part_inc_in_flight(&mdev->vdisk->part0, rw);
	part_stat_unlock();
}

/* Update disk stats when completing request upwards */
static void _drbd_end_io_acct(struct drbd_conf *mdev, struct drbd_request *req)
{
	int rw = bio_data_dir(req->master_bio);
	unsigned long duration = jiffies - req->start_time;
	int cpu;
	cpu = part_stat_lock();
	part_stat_add(cpu, &mdev->vdisk->part0, ticks[rw], duration);
	part_round_stats(cpu, &mdev->vdisk->part0);
	part_dec_in_flight(&mdev->vdisk->part0, rw);
	part_stat_unlock();
}

static struct drbd_request *drbd_req_new(struct drbd_conf *mdev,
					       struct bio *bio_src)
{
	struct drbd_request *req;

	req = mempool_alloc(drbd_request_mempool, GFP_NOIO);
	if (!req)
		return NULL;

	drbd_req_make_private_bio(req, bio_src);
	req->rq_state    = bio_data_dir(bio_src) == WRITE ? RQ_WRITE : 0;
	req->w.mdev      = mdev;
	req->master_bio  = bio_src;
	req->epoch       = 0;

	drbd_clear_interval(&req->i);
	req->i.sector     = bio_src->bi_sector;
	req->i.size      = bio_src->bi_size;
	req->i.local = true;
	req->i.waiting = false;

	INIT_LIST_HEAD(&req->tl_requests);
	INIT_LIST_HEAD(&req->w.list);

	return req;
}

static void drbd_req_free(struct drbd_request *req)
{
	mempool_free(req, drbd_request_mempool);
}

/* rw is bio_data_dir(), only READ or WRITE */
static void _req_is_done(struct drbd_conf *mdev, struct drbd_request *req, const int rw)
{
	const unsigned long s = req->rq_state;

	/* remove it from the transfer log.
	 * well, only if it had been there in the first
	 * place... if it had not (local only or conflicting
	 * and never sent), it should still be "empty" as
	 * initialized in drbd_req_new(), so we can list_del() it
	 * here unconditionally */
	list_del_init(&req->tl_requests);

	/* if it was a write, we may have to set the corresponding
	 * bit(s) out-of-sync first. If it had a local part, we need to
	 * release the reference to the activity log. */
	if (rw == WRITE) {
		/* Set out-of-sync unless both OK flags are set
		 * (local only or remote failed).
		 * Other places where we set out-of-sync:
		 * READ with local io-error */
		if (!(s & RQ_NET_OK) || !(s & RQ_LOCAL_OK))
			drbd_set_out_of_sync(mdev, req->i.sector, req->i.size);

		if ((s & RQ_NET_OK) && (s & RQ_LOCAL_OK) && (s & RQ_NET_SIS))
			drbd_set_in_sync(mdev, req->i.sector, req->i.size);

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
		if (s & RQ_LOCAL_MASK) {
			if (get_ldev_if_state(mdev, D_FAILED)) {
				if (s & RQ_IN_ACT_LOG)
					drbd_al_complete_io(mdev, &req->i);
				put_ldev(mdev);
			} else if (__ratelimit(&drbd_ratelimit_state)) {
				dev_warn(DEV, "Should have called drbd_al_complete_io(, %llu, %u), "
					 "but my Disk seems to have failed :(\n",
					 (unsigned long long) req->i.sector, req->i.size);
			}
		}
	}

	if (s & RQ_POSTPONED)
		drbd_restart_request(req);
	else
		drbd_req_free(req);
}

static void queue_barrier(struct drbd_conf *mdev)
{
	struct drbd_tl_epoch *b;
	struct drbd_tconn *tconn = mdev->tconn;

	/* We are within the req_lock. Once we queued the barrier for sending,
	 * we set the CREATE_BARRIER bit. It is cleared as soon as a new
	 * barrier/epoch object is added. This is the only place this bit is
	 * set. It indicates that the barrier for this epoch is already queued,
	 * and no new epoch has been created yet. */
	if (test_bit(CREATE_BARRIER, &tconn->flags))
		return;

	b = tconn->newest_tle;
	b->w.cb = w_send_barrier;
	b->w.mdev = mdev;
	/* inc_ap_pending done here, so we won't
	 * get imbalanced on connection loss.
	 * dec_ap_pending will be done in got_BarrierAck
	 * or (on connection loss) in tl_clear.  */
	inc_ap_pending(mdev);
	drbd_queue_work(&tconn->data.work, &b->w);
	set_bit(CREATE_BARRIER, &tconn->flags);
}

static void _about_to_complete_local_write(struct drbd_conf *mdev,
	struct drbd_request *req)
{
	const unsigned long s = req->rq_state;

	/* Before we can signal completion to the upper layers,
	 * we may need to close the current epoch.
	 * We can skip this, if this request has not even been sent, because we
	 * did not have a fully established connection yet/anymore, during
	 * bitmap exchange, or while we are C_AHEAD due to congestion policy.
	 */
	if (mdev->state.conn >= C_CONNECTED &&
	    (s & RQ_NET_SENT) != 0 &&
	    req->epoch == atomic_read(&mdev->tconn->current_tle_nr))
		queue_barrier(mdev);
}

void complete_master_bio(struct drbd_conf *mdev,
		struct bio_and_error *m)
{
	bio_endio(m->bio, m->error);
	dec_ap_bio(mdev);
}


static void drbd_remove_request_interval(struct rb_root *root,
					 struct drbd_request *req)
{
	struct drbd_conf *mdev = req->w.mdev;
	struct drbd_interval *i = &req->i;

	drbd_remove_interval(root, i);

	/* Wake up any processes waiting for this request to complete.  */
	if (i->waiting)
		wake_up(&mdev->misc_wait);
}

static void maybe_wakeup_conflicting_requests(struct drbd_request *req)
{
	const unsigned long s = req->rq_state;
	if (s & RQ_LOCAL_PENDING && !(s & RQ_LOCAL_ABORTED))
		return;
	if (req->i.waiting)
		/* Retry all conflicting peer requests.  */
		wake_up(&req->w.mdev->misc_wait);
}

static
void req_may_be_done(struct drbd_request *req)
{
	const unsigned long s = req->rq_state;
	struct drbd_conf *mdev = req->w.mdev;
	int rw = req->rq_state & RQ_WRITE ? WRITE : READ;

	/* req->master_bio still present means: Not yet completed.
	 *
	 * Unless this is RQ_POSTPONED, which will cause _req_is_done() to
	 * queue it on the retry workqueue instead of destroying it.
	 */
	if (req->master_bio && !(s & RQ_POSTPONED))
		return;

	/* Local still pending, even though master_bio is already completed?
	 * may happen for RQ_LOCAL_ABORTED requests. */
	if (s & RQ_LOCAL_PENDING)
		return;

	if ((s & RQ_NET_MASK) == 0 || (s & RQ_NET_DONE)) {
		/* this is disconnected (local only) operation,
		 * or protocol A, B, or C P_BARRIER_ACK,
		 * or killed from the transfer log due to connection loss. */
		_req_is_done(mdev, req, rw);
	}
	/* else: network part and not DONE yet. that is
	 * protocol A, B, or C, barrier ack still pending... */
}

/* Helper for __req_mod().
 * Set m->bio to the master bio, if it is fit to be completed,
 * or leave it alone (it is initialized to NULL in __req_mod),
 * if it has already been completed, or cannot be completed yet.
 * If m->bio is set, the error status to be returned is placed in m->error.
 */
static
void req_may_be_completed(struct drbd_request *req, struct bio_and_error *m)
{
	const unsigned long s = req->rq_state;
	struct drbd_conf *mdev = req->w.mdev;

	/* we must not complete the master bio, while it is
	 *	still being processed by _drbd_send_zc_bio (drbd_send_dblock)
	 *	not yet acknowledged by the peer
	 *	not yet completed by the local io subsystem
	 * these flags may get cleared in any order by
	 *	the worker,
	 *	the receiver,
	 *	the bio_endio completion callbacks.
	 */
	if (s & RQ_LOCAL_PENDING && !(s & RQ_LOCAL_ABORTED))
		return;
	if (s & RQ_NET_QUEUED)
		return;
	if (s & RQ_NET_PENDING)
		return;

	if (req->master_bio) {
		int rw = bio_rw(req->master_bio);

		/* this is DATA_RECEIVED (remote read)
		 * or protocol C P_WRITE_ACK
		 * or protocol B P_RECV_ACK
		 * or protocol A "HANDED_OVER_TO_NETWORK" (SendAck)
		 * or canceled or failed,
		 * or killed from the transfer log due to connection loss.
		 */

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
		int ok = (s & RQ_LOCAL_OK) || (s & RQ_NET_OK);
		int error = PTR_ERR(req->private_bio);

		/* remove the request from the conflict detection
		 * respective block_id verification hash */
		if (!drbd_interval_empty(&req->i)) {
			struct rb_root *root;

			if (rw == WRITE)
				root = &mdev->write_requests;
			else
				root = &mdev->read_requests;
			drbd_remove_request_interval(root, req);
		} else if (!(s & RQ_POSTPONED))
			D_ASSERT((s & (RQ_NET_MASK & ~RQ_NET_DONE)) == 0);

		/* for writes we need to do some extra housekeeping */
		if (rw == WRITE)
			_about_to_complete_local_write(mdev, req);

		/* Update disk stats */
		_drbd_end_io_acct(mdev, req);

		/* if READ failed,
		 * have it be pushed back to the retry work queue,
		 * so it will re-enter __drbd_make_request,
		 * and be re-assigned to a suitable local or remote path,
		 * or failed if we do not have access to good data anymore.
		 * READA may fail.
		 * WRITE should have used all available paths already.
		 */
		if (!ok && rw == READ)
			req->rq_state |= RQ_POSTPONED;

		if (!(req->rq_state & RQ_POSTPONED)) {
			m->error = ok ? 0 : (error ?: -EIO);
			m->bio = req->master_bio;
			req->master_bio = NULL;
		} else {
			/* Assert that this will be _req_is_done()
			 * with this very invokation. */
			/* FIXME:
			 * what about (RQ_LOCAL_PENDING | RQ_LOCAL_ABORTED)?
			 */
			D_ASSERT(!(s & RQ_LOCAL_PENDING));
			D_ASSERT((s & RQ_NET_MASK) == 0 || (s & RQ_NET_DONE));
		}
	}
	req_may_be_done(req);
}

static void req_may_be_completed_not_susp(struct drbd_request *req, struct bio_and_error *m)
{
	struct drbd_conf *mdev = req->w.mdev;

	if (!drbd_suspended(mdev))
		req_may_be_completed(req, m);
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
	struct drbd_conf *mdev = req->w.mdev;
	struct net_conf *nc;
	int p, rv = 0;

	if (m)
		m->bio = NULL;

	switch (what) {
	default:
		dev_err(DEV, "LOGIC BUG in %s:%u\n", __FILE__ , __LINE__);
		break;

	/* does not happen...
	 * initialization done in drbd_req_new
	case CREATED:
		break;
		*/

	case TO_BE_SENT: /* via network */
		/* reached via __drbd_make_request
		 * and from w_read_retry_remote */
		D_ASSERT(!(req->rq_state & RQ_NET_MASK));
		req->rq_state |= RQ_NET_PENDING;
		rcu_read_lock();
		nc = rcu_dereference(mdev->tconn->net_conf);
		p = nc->wire_protocol;
		rcu_read_unlock();
		req->rq_state |=
			p == DRBD_PROT_C ? RQ_EXP_WRITE_ACK :
			p == DRBD_PROT_B ? RQ_EXP_RECEIVE_ACK : 0;
		inc_ap_pending(mdev);
		break;

	case TO_BE_SUBMITTED: /* locally */
		/* reached via __drbd_make_request */
		D_ASSERT(!(req->rq_state & RQ_LOCAL_MASK));
		req->rq_state |= RQ_LOCAL_PENDING;
		break;

	case COMPLETED_OK:
		if (req->rq_state & RQ_WRITE)
			mdev->writ_cnt += req->i.size >> 9;
		else
			mdev->read_cnt += req->i.size >> 9;

		req->rq_state |= (RQ_LOCAL_COMPLETED|RQ_LOCAL_OK);
		req->rq_state &= ~RQ_LOCAL_PENDING;

		maybe_wakeup_conflicting_requests(req);
		req_may_be_completed_not_susp(req, m);
		break;

	case ABORT_DISK_IO:
		req->rq_state |= RQ_LOCAL_ABORTED;
		req_may_be_completed_not_susp(req, m);
		break;

	case WRITE_COMPLETED_WITH_ERROR:
		req->rq_state |= RQ_LOCAL_COMPLETED;
		req->rq_state &= ~RQ_LOCAL_PENDING;

		__drbd_chk_io_error(mdev, false);
		maybe_wakeup_conflicting_requests(req);
		req_may_be_completed_not_susp(req, m);
		break;

	case READ_AHEAD_COMPLETED_WITH_ERROR:
		/* it is legal to fail READA */
		req->rq_state |= RQ_LOCAL_COMPLETED;
		req->rq_state &= ~RQ_LOCAL_PENDING;
		req_may_be_completed_not_susp(req, m);
		break;

	case READ_COMPLETED_WITH_ERROR:
		drbd_set_out_of_sync(mdev, req->i.sector, req->i.size);

		req->rq_state |= RQ_LOCAL_COMPLETED;
		req->rq_state &= ~RQ_LOCAL_PENDING;

		D_ASSERT(!(req->rq_state & RQ_NET_MASK));

		__drbd_chk_io_error(mdev, false);
		req_may_be_completed_not_susp(req, m);
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
		 * req_may_be_completed() */
		D_ASSERT(drbd_interval_empty(&req->i));
		drbd_insert_interval(&mdev->read_requests, &req->i);

		set_bit(UNPLUG_REMOTE, &mdev->flags);

		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		D_ASSERT((req->rq_state & RQ_LOCAL_MASK) == 0);
		req->rq_state |= RQ_NET_QUEUED;
		req->w.cb = w_send_read_req;
		drbd_queue_work(&mdev->tconn->data.work, &req->w);
		break;

	case QUEUE_FOR_NET_WRITE:
		/* assert something? */
		/* from __drbd_make_request only */

		/* Corresponding drbd_remove_request_interval is in
		 * req_may_be_completed() */
		D_ASSERT(drbd_interval_empty(&req->i));
		drbd_insert_interval(&mdev->write_requests, &req->i);

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
		set_bit(UNPLUG_REMOTE, &mdev->flags);

		/* see __drbd_make_request,
		 * just after it grabs the req_lock */
		D_ASSERT(test_bit(CREATE_BARRIER, &mdev->tconn->flags) == 0);

		req->epoch = atomic_read(&mdev->tconn->current_tle_nr);

		/* increment size of current epoch */
		mdev->tconn->newest_tle->n_writes++;

		/* queue work item to send data */
		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		req->rq_state |= RQ_NET_QUEUED;
		req->w.cb =  w_send_dblock;
		drbd_queue_work(&mdev->tconn->data.work, &req->w);

		/* close the epoch, in case it outgrew the limit */
		rcu_read_lock();
		nc = rcu_dereference(mdev->tconn->net_conf);
		p = nc->max_epoch_size;
		rcu_read_unlock();
		if (mdev->tconn->newest_tle->n_writes >= p)
			queue_barrier(mdev);

		break;

	case QUEUE_FOR_SEND_OOS:
		req->rq_state |= RQ_NET_QUEUED;
		req->w.cb =  w_send_out_of_sync;
		drbd_queue_work(&mdev->tconn->data.work, &req->w);
		break;

	case READ_RETRY_REMOTE_CANCELED:
	case SEND_CANCELED:
	case SEND_FAILED:
		/* real cleanup will be done from tl_clear.  just update flags
		 * so it is no longer marked as on the worker queue */
		req->rq_state &= ~RQ_NET_QUEUED;
		/* if we did it right, tl_clear should be scheduled only after
		 * this, so this should not be necessary! */
		req_may_be_completed_not_susp(req, m);
		break;

	case HANDED_OVER_TO_NETWORK:
		/* assert something? */
		if (bio_data_dir(req->master_bio) == WRITE)
			atomic_add(req->i.size >> 9, &mdev->ap_in_flight);

		if (bio_data_dir(req->master_bio) == WRITE &&
		    !(req->rq_state & (RQ_EXP_RECEIVE_ACK | RQ_EXP_WRITE_ACK))) {
			/* this is what is dangerous about protocol A:
			 * pretend it was successfully written on the peer. */
			if (req->rq_state & RQ_NET_PENDING) {
				dec_ap_pending(mdev);
				req->rq_state &= ~RQ_NET_PENDING;
				req->rq_state |= RQ_NET_OK;
			} /* else: neg-ack was faster... */
			/* it is still not yet RQ_NET_DONE until the
			 * corresponding epoch barrier got acked as well,
			 * so we know what to dirty on connection loss */
		}
		req->rq_state &= ~RQ_NET_QUEUED;
		req->rq_state |= RQ_NET_SENT;
		req_may_be_completed_not_susp(req, m);
		break;

	case OOS_HANDED_TO_NETWORK:
		/* Was not set PENDING, no longer QUEUED, so is now DONE
		 * as far as this connection is concerned. */
		req->rq_state &= ~RQ_NET_QUEUED;
		req->rq_state |= RQ_NET_DONE;
		req_may_be_completed_not_susp(req, m);
		break;

	case CONNECTION_LOST_WHILE_PENDING:
		/* transfer log cleanup after connection loss */
		/* assert something? */
		if (req->rq_state & RQ_NET_PENDING)
			dec_ap_pending(mdev);

		p = !(req->rq_state & RQ_WRITE) && req->rq_state & RQ_NET_PENDING;

		req->rq_state &= ~(RQ_NET_OK|RQ_NET_PENDING);
		req->rq_state |= RQ_NET_DONE;
		if (req->rq_state & RQ_NET_SENT && req->rq_state & RQ_WRITE)
			atomic_sub(req->i.size >> 9, &mdev->ap_in_flight);

		req_may_be_completed(req, m); /* Allowed while state.susp */
		break;

	case DISCARD_WRITE:
		/* for discarded conflicting writes of multiple primaries,
		 * there is no need to keep anything in the tl, potential
		 * node crashes are covered by the activity log. */
		req->rq_state |= RQ_NET_DONE;
		/* fall through */
	case WRITE_ACKED_BY_PEER_AND_SIS:
	case WRITE_ACKED_BY_PEER:
		if (what == WRITE_ACKED_BY_PEER_AND_SIS)
			req->rq_state |= RQ_NET_SIS;
		D_ASSERT(req->rq_state & RQ_EXP_WRITE_ACK);
		/* protocol C; successfully written on peer.
		 * Nothing more to do here.
		 * We want to keep the tl in place for all protocols, to cater
		 * for volatile write-back caches on lower level devices. */

		goto ack_common;
	case RECV_ACKED_BY_PEER:
		D_ASSERT(req->rq_state & RQ_EXP_RECEIVE_ACK);
		/* protocol B; pretends to be successfully written on peer.
		 * see also notes above in HANDED_OVER_TO_NETWORK about
		 * protocol != C */
	ack_common:
		req->rq_state |= RQ_NET_OK;
		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		dec_ap_pending(mdev);
		atomic_sub(req->i.size >> 9, &mdev->ap_in_flight);
		req->rq_state &= ~RQ_NET_PENDING;
		maybe_wakeup_conflicting_requests(req);
		req_may_be_completed_not_susp(req, m);
		break;

	case POSTPONE_WRITE:
		D_ASSERT(req->rq_state & RQ_EXP_WRITE_ACK);
		/* If this node has already detected the write conflict, the
		 * worker will be waiting on misc_wait.  Wake it up once this
		 * request has completed locally.
		 */
		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		req->rq_state |= RQ_POSTPONED;
		maybe_wakeup_conflicting_requests(req);
		req_may_be_completed_not_susp(req, m);
		break;

	case NEG_ACKED:
		/* assert something? */
		if (req->rq_state & RQ_NET_PENDING) {
			dec_ap_pending(mdev);
			if (req->rq_state & RQ_WRITE)
				atomic_sub(req->i.size >> 9, &mdev->ap_in_flight);
		}
		req->rq_state &= ~(RQ_NET_OK|RQ_NET_PENDING);

		req->rq_state |= RQ_NET_DONE;

		maybe_wakeup_conflicting_requests(req);
		req_may_be_completed_not_susp(req, m);
		/* else: done by HANDED_OVER_TO_NETWORK */
		break;

	case FAIL_FROZEN_DISK_IO:
		if (!(req->rq_state & RQ_LOCAL_COMPLETED))
			break;

		req_may_be_completed(req, m); /* Allowed while state.susp */
		break;

	case RESTART_FROZEN_DISK_IO:
		if (!(req->rq_state & RQ_LOCAL_COMPLETED))
			break;

		req->rq_state &= ~RQ_LOCAL_COMPLETED;

		rv = MR_READ;
		if (bio_data_dir(req->master_bio) == WRITE)
			rv = MR_WRITE;

		get_ldev(mdev);
		req->w.cb = w_restart_disk_io;
		drbd_queue_work(&mdev->tconn->data.work, &req->w);
		break;

	case RESEND:
		/* If RQ_NET_OK is already set, we got a P_WRITE_ACK or P_RECV_ACK
		   before the connection loss (B&C only); only P_BARRIER_ACK was missing.
		   Throwing them out of the TL here by pretending we got a BARRIER_ACK.
		   During connection handshake, we ensure that the peer was not rebooted. */
		if (!(req->rq_state & RQ_NET_OK)) {
			if (req->w.cb) {
				drbd_queue_work(&mdev->tconn->data.work, &req->w);
				rv = req->rq_state & RQ_WRITE ? MR_WRITE : MR_READ;
			}
			break;
		}
		/* else, fall through to BARRIER_ACKED */

	case BARRIER_ACKED:
		if (!(req->rq_state & RQ_WRITE))
			break;

		if (req->rq_state & RQ_NET_PENDING) {
			/* barrier came in before all requests were acked.
			 * this is bad, because if the connection is lost now,
			 * we won't be able to clean them up... */
			dev_err(DEV, "FIXME (BARRIER_ACKED but pending)\n");
			list_move(&req->tl_requests, &mdev->tconn->out_of_sequence_requests);
		}
		if ((req->rq_state & RQ_NET_MASK) != 0) {
			req->rq_state |= RQ_NET_DONE;
			if (!(req->rq_state & (RQ_EXP_RECEIVE_ACK | RQ_EXP_WRITE_ACK)))
				atomic_sub(req->i.size>>9, &mdev->ap_in_flight);
		}
		req_may_be_done(req); /* Allowed while state.susp */
		break;

	case DATA_RECEIVED:
		D_ASSERT(req->rq_state & RQ_NET_PENDING);
		dec_ap_pending(mdev);
		req->rq_state &= ~RQ_NET_PENDING;
		req->rq_state |= (RQ_NET_OK|RQ_NET_DONE);
		req_may_be_completed_not_susp(req, m);
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
static bool drbd_may_do_local_read(struct drbd_conf *mdev, sector_t sector, int size)
{
	unsigned long sbnr, ebnr;
	sector_t esector, nr_sectors;

	if (mdev->state.disk == D_UP_TO_DATE)
		return true;
	if (mdev->state.disk != D_INCONSISTENT)
		return false;
	esector = sector + (size >> 9) - 1;
	nr_sectors = drbd_get_capacity(mdev->this_bdev);
	D_ASSERT(sector  < nr_sectors);
	D_ASSERT(esector < nr_sectors);

	sbnr = BM_SECT_TO_BIT(sector);
	ebnr = BM_SECT_TO_BIT(esector);

	return drbd_bm_count_bits(mdev, sbnr, ebnr) == 0;
}

static bool remote_due_to_read_balancing(struct drbd_conf *mdev, sector_t sector)
{
	enum drbd_read_balancing rbm;
	struct backing_dev_info *bdi;
	int stripe_shift;

	if (mdev->state.pdsk < D_UP_TO_DATE)
		return false;

	rcu_read_lock();
	rbm = rcu_dereference(mdev->ldev->disk_conf)->read_balancing;
	rcu_read_unlock();

	switch (rbm) {
	case RB_CONGESTED_REMOTE:
		bdi = &mdev->ldev->backing_bdev->bd_disk->queue->backing_dev_info;
		return bdi_read_congested(bdi);
	case RB_LEAST_PENDING:
		return atomic_read(&mdev->local_cnt) >
			atomic_read(&mdev->ap_pending_cnt) + atomic_read(&mdev->rs_pending_cnt);
	case RB_32K_STRIPING:  /* stripe_shift = 15 */
	case RB_64K_STRIPING:
	case RB_128K_STRIPING:
	case RB_256K_STRIPING:
	case RB_512K_STRIPING:
	case RB_1M_STRIPING:   /* stripe_shift = 20 */
		stripe_shift = (rbm - RB_32K_STRIPING + 15);
		return (sector >> (stripe_shift - 9)) & 1;
	case RB_ROUND_ROBIN:
		return test_and_change_bit(READ_BALANCE_RR, &mdev->flags);
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
	struct drbd_conf *mdev = req->w.mdev;
	struct drbd_interval *i;
	sector_t sector = req->i.sector;
	int size = req->i.size;

	i = drbd_find_overlap(&mdev->write_requests, sector, size);
	if (!i)
		return;

	for (;;) {
		prepare_to_wait(&mdev->misc_wait, &wait, TASK_UNINTERRUPTIBLE);
		i = drbd_find_overlap(&mdev->write_requests, sector, size);
		if (!i)
			break;
		/* Indicate to wake up device->misc_wait on progress.  */
		i->waiting = true;
		spin_unlock_irq(&mdev->tconn->req_lock);
		schedule();
		spin_lock_irq(&mdev->tconn->req_lock);
	}
	finish_wait(&mdev->misc_wait, &wait);
}

int __drbd_make_request(struct drbd_conf *mdev, struct bio *bio, unsigned long start_time)
{
	const int rw = bio_rw(bio);
	const int size = bio->bi_size;
	const sector_t sector = bio->bi_sector;
	struct drbd_tl_epoch *b = NULL;
	struct drbd_request *req;
	struct net_conf *nc;
	int local, remote, send_oos = 0;
	int err = 0;
	int ret = 0;
	union drbd_dev_state s;

	/* allocate outside of all locks; */
	req = drbd_req_new(mdev, bio);
	if (!req) {
		dec_ap_bio(mdev);
		/* only pass the error to the upper layers.
		 * if user cannot handle io errors, that's not our business. */
		dev_err(DEV, "could not kmalloc() req\n");
		bio_endio(bio, -ENOMEM);
		return 0;
	}
	req->start_time = start_time;

	local = get_ldev(mdev);
	if (!local) {
		bio_put(req->private_bio); /* or we get a bio leak */
		req->private_bio = NULL;
	}
	if (rw == WRITE) {
		remote = 1;
	} else {
		/* READ || READA */
		if (local) {
			if (!drbd_may_do_local_read(mdev, sector, size) ||
			    remote_due_to_read_balancing(mdev, sector)) {
				/* we could kick the syncer to
				 * sync this extent asap, wait for
				 * it, then continue locally.
				 * Or just issue the request remotely.
				 */
				local = 0;
				bio_put(req->private_bio);
				req->private_bio = NULL;
				put_ldev(mdev);
			}
		}
		remote = !local && mdev->state.pdsk >= D_UP_TO_DATE;
	}

	/* If we have a disk, but a READA request is mapped to remote,
	 * we are R_PRIMARY, D_INCONSISTENT, SyncTarget.
	 * Just fail that READA request right here.
	 *
	 * THINK: maybe fail all READA when not local?
	 *        or make this configurable...
	 *        if network is slow, READA won't do any good.
	 */
	if (rw == READA && mdev->state.disk >= D_INCONSISTENT && !local) {
		err = -EWOULDBLOCK;
		goto fail_and_free_req;
	}

	/* For WRITES going to the local disk, grab a reference on the target
	 * extent.  This waits for any resync activity in the corresponding
	 * resync extent to finish, and, if necessary, pulls in the target
	 * extent into the activity log, which involves further disk io because
	 * of transactional on-disk meta data updates. */
	if (rw == WRITE && local && !test_bit(AL_SUSPENDED, &mdev->flags)) {
		req->rq_state |= RQ_IN_ACT_LOG;
		drbd_al_begin_io(mdev, &req->i);
	}

	s = mdev->state;
	remote = remote && drbd_should_do_remote(s);
	send_oos = rw == WRITE && drbd_should_send_out_of_sync(s);
	D_ASSERT(!(remote && send_oos));

	if (!(local || remote) && !drbd_suspended(mdev)) {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "IO ERROR: neither local nor remote disk\n");
		err = -EIO;
		goto fail_free_complete;
	}

	/* For WRITE request, we have to make sure that we have an
	 * unused_spare_tle, in case we need to start a new epoch.
	 * I try to be smart and avoid to pre-allocate always "just in case",
	 * but there is a race between testing the bit and pointer outside the
	 * spinlock, and grabbing the spinlock.
	 * if we lost that race, we retry.  */
	if (rw == WRITE && (remote || send_oos) &&
	    mdev->tconn->unused_spare_tle == NULL &&
	    test_bit(CREATE_BARRIER, &mdev->tconn->flags)) {
allocate_barrier:
		b = kmalloc(sizeof(struct drbd_tl_epoch), GFP_NOIO);
		if (!b) {
			dev_err(DEV, "Failed to alloc barrier.\n");
			err = -ENOMEM;
			goto fail_free_complete;
		}
	}

	/* GOOD, everything prepared, grab the spin_lock */
	spin_lock_irq(&mdev->tconn->req_lock);

	if (rw == WRITE) {
		/* This may temporarily give up the req_lock,
		 * but will re-aquire it before it returns here.
		 * Needs to be before the check on drbd_suspended() */
		complete_conflicting_writes(req);
	}

	if (drbd_suspended(mdev)) {
		/* If we got suspended, use the retry mechanism in
		   drbd_make_request() to restart processing of this
		   bio. In the next call to drbd_make_request
		   we sleep in inc_ap_bio() */
		ret = 1;
		spin_unlock_irq(&mdev->tconn->req_lock);
		goto fail_free_complete;
	}

	if (remote || send_oos) {
		remote = drbd_should_do_remote(mdev->state);
		send_oos = rw == WRITE && drbd_should_send_out_of_sync(mdev->state);
		D_ASSERT(!(remote && send_oos));

		if (!(remote || send_oos))
			dev_warn(DEV, "lost connection while grabbing the req_lock!\n");
		if (!(local || remote)) {
			dev_err(DEV, "IO ERROR: neither local nor remote disk\n");
			spin_unlock_irq(&mdev->tconn->req_lock);
			err = -EIO;
			goto fail_free_complete;
		}
	}

	if (b && mdev->tconn->unused_spare_tle == NULL) {
		mdev->tconn->unused_spare_tle = b;
		b = NULL;
	}
	if (rw == WRITE && (remote || send_oos) &&
	    mdev->tconn->unused_spare_tle == NULL &&
	    test_bit(CREATE_BARRIER, &mdev->tconn->flags)) {
		/* someone closed the current epoch
		 * while we were grabbing the spinlock */
		spin_unlock_irq(&mdev->tconn->req_lock);
		goto allocate_barrier;
	}


	/* Update disk stats */
	_drbd_start_io_acct(mdev, req, bio);

	/* _maybe_start_new_epoch(mdev);
	 * If we need to generate a write barrier packet, we have to add the
	 * new epoch (barrier) object, and queue the barrier packet for sending,
	 * and queue the req's data after it _within the same lock_, otherwise
	 * we have race conditions were the reorder domains could be mixed up.
	 *
	 * Even read requests may start a new epoch and queue the corresponding
	 * barrier packet.  To get the write ordering right, we only have to
	 * make sure that, if this is a write request and it triggered a
	 * barrier packet, this request is queued within the same spinlock. */
	if ((remote || send_oos) && mdev->tconn->unused_spare_tle &&
	    test_and_clear_bit(CREATE_BARRIER, &mdev->tconn->flags)) {
		_tl_add_barrier(mdev->tconn, mdev->tconn->unused_spare_tle);
		mdev->tconn->unused_spare_tle = NULL;
	} else {
		D_ASSERT(!(remote && rw == WRITE &&
			   test_bit(CREATE_BARRIER, &mdev->tconn->flags)));
	}

	/* NOTE
	 * Actually, 'local' may be wrong here already, since we may have failed
	 * to write to the meta data, and may become wrong anytime because of
	 * local io-error for some other request, which would lead to us
	 * "detaching" the local disk.
	 *
	 * 'remote' may become wrong any time because the network could fail.
	 *
	 * This is a harmless race condition, though, since it is handled
	 * correctly at the appropriate places; so it just defers the failure
	 * of the respective operation.
	 */

	/* mark them early for readability.
	 * this just sets some state flags. */
	if (remote)
		_req_mod(req, TO_BE_SENT);
	if (local)
		_req_mod(req, TO_BE_SUBMITTED);

	list_add_tail(&req->tl_requests, &mdev->tconn->newest_tle->requests);

	/* NOTE remote first: to get the concurrent write detection right,
	 * we must register the request before start of local IO.  */
	if (remote) {
		/* either WRITE and C_CONNECTED,
		 * or READ, and no local disk,
		 * or READ, but not in sync.
		 */
		_req_mod(req, (rw == WRITE)
				? QUEUE_FOR_NET_WRITE
				: QUEUE_FOR_NET_READ);
	}
	if (send_oos && drbd_set_out_of_sync(mdev, sector, size))
		_req_mod(req, QUEUE_FOR_SEND_OOS);

	rcu_read_lock();
	nc = rcu_dereference(mdev->tconn->net_conf);
	if (remote &&
	    nc->on_congestion != OC_BLOCK && mdev->tconn->agreed_pro_version >= 96) {
		int congested = 0;

		if (nc->cong_fill &&
		    atomic_read(&mdev->ap_in_flight) >= nc->cong_fill) {
			dev_info(DEV, "Congestion-fill threshold reached\n");
			congested = 1;
		}

		if (mdev->act_log->used >= nc->cong_extents) {
			dev_info(DEV, "Congestion-extents threshold reached\n");
			congested = 1;
		}

		if (congested) {
			queue_barrier(mdev); /* last barrier, after mirrored writes */

			if (nc->on_congestion == OC_PULL_AHEAD)
				_drbd_set_state(_NS(mdev, conn, C_AHEAD), 0, NULL);
			else  /*nc->on_congestion == OC_DISCONNECT */
				_drbd_set_state(_NS(mdev, conn, C_DISCONNECTING), 0, NULL);
		}
	}
	rcu_read_unlock();

	spin_unlock_irq(&mdev->tconn->req_lock);
	kfree(b); /* if someone else has beaten us to it... */

	if (local) {
		req->private_bio->bi_bdev = mdev->ldev->backing_bdev;

		/* State may have changed since we grabbed our reference on the
		 * mdev->ldev member. Double check, and short-circuit to endio.
		 * In case the last activity log transaction failed to get on
		 * stable storage, and this is a WRITE, we may not even submit
		 * this bio. */
		if (get_ldev(mdev)) {
			if (drbd_insert_fault(mdev,   rw == WRITE ? DRBD_FAULT_DT_WR
						    : rw == READ  ? DRBD_FAULT_DT_RD
						    :               DRBD_FAULT_DT_RA))
				bio_endio(req->private_bio, -EIO);
			else
				generic_make_request(req->private_bio);
			put_ldev(mdev);
		} else
			bio_endio(req->private_bio, -EIO);
	}

	return 0;

fail_free_complete:
	if (req->rq_state & RQ_IN_ACT_LOG)
		drbd_al_complete_io(mdev, &req->i);
fail_and_free_req:
	if (local) {
		bio_put(req->private_bio);
		req->private_bio = NULL;
		put_ldev(mdev);
	}
	if (!ret)
		bio_endio(bio, err);

	drbd_req_free(req);
	dec_ap_bio(mdev);
	kfree(b);

	return ret;
}

int drbd_make_request(struct request_queue *q, struct bio *bio)
{
	struct drbd_conf *mdev = (struct drbd_conf *) q->queuedata;
	unsigned long start_time;

	start_time = jiffies;

	/*
	 * what we "blindly" assume:
	 */
	D_ASSERT(bio->bi_size > 0);
	D_ASSERT(IS_ALIGNED(bio->bi_size, 512));

	do {
		inc_ap_bio(mdev);
	} while (__drbd_make_request(mdev, bio, start_time));

	return 0;
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
	struct drbd_conf *mdev = (struct drbd_conf *) q->queuedata;
	unsigned int bio_size = bvm->bi_size;
	int limit = DRBD_MAX_BIO_SIZE;
	int backing_limit;

	if (bio_size && get_ldev(mdev)) {
		struct request_queue * const b =
			mdev->ldev->backing_bdev->bd_disk->queue;
		if (b->merge_bvec_fn) {
			backing_limit = b->merge_bvec_fn(b, bvm, bvec);
			limit = min(limit, backing_limit);
		}
		put_ldev(mdev);
	}
	return limit;
}

void request_timer_fn(unsigned long data)
{
	struct drbd_conf *mdev = (struct drbd_conf *) data;
	struct drbd_tconn *tconn = mdev->tconn;
	struct drbd_request *req; /* oldest request */
	struct list_head *le;
	struct net_conf *nc;
	unsigned long ent = 0, dt = 0, et, nt; /* effective timeout = ko_count * timeout */
	unsigned long now;

	rcu_read_lock();
	nc = rcu_dereference(tconn->net_conf);
	if (nc && mdev->state.conn >= C_WF_REPORT_PARAMS)
		ent = nc->timeout * HZ/10 * nc->ko_count;

	if (get_ldev(mdev)) { /* implicit state.disk >= D_INCONSISTENT */
		dt = rcu_dereference(mdev->ldev->disk_conf)->disk_timeout * HZ / 10;
		put_ldev(mdev);
	}
	rcu_read_unlock();

	et = min_not_zero(dt, ent);

	if (!et)
		return; /* Recurring timer stopped */

	now = jiffies;

	spin_lock_irq(&tconn->req_lock);
	le = &tconn->oldest_tle->requests;
	if (list_empty(le)) {
		spin_unlock_irq(&tconn->req_lock);
		mod_timer(&mdev->request_timer, now + et);
		return;
	}

	le = le->prev;
	req = list_entry(le, struct drbd_request, tl_requests);

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
	if (ent && req->rq_state & RQ_NET_PENDING &&
		 time_after(now, req->start_time + ent) &&
		!time_in_range(now, tconn->last_reconnect_jif, tconn->last_reconnect_jif + ent)) {
		dev_warn(DEV, "Remote failed to finish a request within ko-count * timeout\n");
		_drbd_set_state(_NS(mdev, conn, C_TIMEOUT), CS_VERBOSE | CS_HARD, NULL);
	}
	if (dt && req->rq_state & RQ_LOCAL_PENDING && req->w.mdev == mdev &&
		 time_after(now, req->start_time + dt) &&
		!time_in_range(now, mdev->last_reattach_jif, mdev->last_reattach_jif + dt)) {
		dev_warn(DEV, "Local backing device failed to meet the disk-timeout\n");
		__drbd_chk_io_error(mdev, 1);
	}
	nt = (time_after(now, req->start_time + et) ? now : req->start_time) + et;
	spin_unlock_irq(&tconn->req_lock);
	mod_timer(&mdev->request_timer, nt);
}
