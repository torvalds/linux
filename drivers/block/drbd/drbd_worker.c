/*
   drbd_worker.c

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
#include <linux/drbd.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/mm_inline.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/scatterlist.h>

#include "drbd_int.h"
#include "drbd_req.h"

#define SLEEP_TIME (HZ/10)

static int w_make_ov_request(struct drbd_conf *mdev, struct drbd_work *w, int cancel);



/* defined here:
   drbd_md_io_complete
   drbd_endio_sec
   drbd_endio_pri

 * more endio handlers:
   atodb_endio in drbd_actlog.c
   drbd_bm_async_io_complete in drbd_bitmap.c

 * For all these callbacks, note the following:
 * The callbacks will be called in irq context by the IDE drivers,
 * and in Softirqs/Tasklets/BH context by the SCSI drivers.
 * Try to get the locking right :)
 *
 */


/* About the global_state_lock
   Each state transition on an device holds a read lock. In case we have
   to evaluate the sync after dependencies, we grab a write lock, because
   we need stable states on all devices for that.  */
rwlock_t global_state_lock;

/* used for synchronous meta data and bitmap IO
 * submitted by drbd_md_sync_page_io()
 */
void drbd_md_io_complete(struct bio *bio, int error)
{
	struct drbd_md_io *md_io;

	md_io = (struct drbd_md_io *)bio->bi_private;
	md_io->error = error;

	complete(&md_io->event);
}

/* reads on behalf of the partner,
 * "submitted" by the receiver
 */
void drbd_endio_read_sec_final(struct drbd_epoch_entry *e) __releases(local)
{
	unsigned long flags = 0;
	struct drbd_conf *mdev = e->mdev;

	D_ASSERT(e->block_id != ID_VACANT);

	spin_lock_irqsave(&mdev->req_lock, flags);
	mdev->read_cnt += e->size >> 9;
	list_del(&e->w.list);
	if (list_empty(&mdev->read_ee))
		wake_up(&mdev->ee_wait);
	if (test_bit(__EE_WAS_ERROR, &e->flags))
		__drbd_chk_io_error(mdev, FALSE);
	spin_unlock_irqrestore(&mdev->req_lock, flags);

	drbd_queue_work(&mdev->data.work, &e->w);
	put_ldev(mdev);
}

static int is_failed_barrier(int ee_flags)
{
	return (ee_flags & (EE_IS_BARRIER|EE_WAS_ERROR|EE_RESUBMITTED))
			== (EE_IS_BARRIER|EE_WAS_ERROR);
}

/* writes on behalf of the partner, or resync writes,
 * "submitted" by the receiver, final stage.  */
static void drbd_endio_write_sec_final(struct drbd_epoch_entry *e) __releases(local)
{
	unsigned long flags = 0;
	struct drbd_conf *mdev = e->mdev;
	sector_t e_sector;
	int do_wake;
	int is_syncer_req;
	int do_al_complete_io;

	/* if this is a failed barrier request, disable use of barriers,
	 * and schedule for resubmission */
	if (is_failed_barrier(e->flags)) {
		drbd_bump_write_ordering(mdev, WO_bdev_flush);
		spin_lock_irqsave(&mdev->req_lock, flags);
		list_del(&e->w.list);
		e->flags = (e->flags & ~EE_WAS_ERROR) | EE_RESUBMITTED;
		e->w.cb = w_e_reissue;
		/* put_ldev actually happens below, once we come here again. */
		__release(local);
		spin_unlock_irqrestore(&mdev->req_lock, flags);
		drbd_queue_work(&mdev->data.work, &e->w);
		return;
	}

	D_ASSERT(e->block_id != ID_VACANT);

	/* after we moved e to done_ee,
	 * we may no longer access it,
	 * it may be freed/reused already!
	 * (as soon as we release the req_lock) */
	e_sector = e->sector;
	do_al_complete_io = e->flags & EE_CALL_AL_COMPLETE_IO;
	is_syncer_req = is_syncer_block_id(e->block_id);

	spin_lock_irqsave(&mdev->req_lock, flags);
	mdev->writ_cnt += e->size >> 9;
	list_del(&e->w.list); /* has been on active_ee or sync_ee */
	list_add_tail(&e->w.list, &mdev->done_ee);

	/* No hlist_del_init(&e->colision) here, we did not send the Ack yet,
	 * neither did we wake possibly waiting conflicting requests.
	 * done from "drbd_process_done_ee" within the appropriate w.cb
	 * (e_end_block/e_end_resync_block) or from _drbd_clear_done_ee */

	do_wake = is_syncer_req
		? list_empty(&mdev->sync_ee)
		: list_empty(&mdev->active_ee);

	if (test_bit(__EE_WAS_ERROR, &e->flags))
		__drbd_chk_io_error(mdev, FALSE);
	spin_unlock_irqrestore(&mdev->req_lock, flags);

	if (is_syncer_req)
		drbd_rs_complete_io(mdev, e_sector);

	if (do_wake)
		wake_up(&mdev->ee_wait);

	if (do_al_complete_io)
		drbd_al_complete_io(mdev, e_sector);

	wake_asender(mdev);
	put_ldev(mdev);
}

/* writes on behalf of the partner, or resync writes,
 * "submitted" by the receiver.
 */
void drbd_endio_sec(struct bio *bio, int error)
{
	struct drbd_epoch_entry *e = bio->bi_private;
	struct drbd_conf *mdev = e->mdev;
	int uptodate = bio_flagged(bio, BIO_UPTODATE);
	int is_write = bio_data_dir(bio) == WRITE;

	if (error)
		dev_warn(DEV, "%s: error=%d s=%llus\n",
				is_write ? "write" : "read", error,
				(unsigned long long)e->sector);
	if (!error && !uptodate) {
		dev_warn(DEV, "%s: setting error to -EIO s=%llus\n",
				is_write ? "write" : "read",
				(unsigned long long)e->sector);
		/* strange behavior of some lower level drivers...
		 * fail the request by clearing the uptodate flag,
		 * but do not return any error?! */
		error = -EIO;
	}

	if (error)
		set_bit(__EE_WAS_ERROR, &e->flags);

	bio_put(bio); /* no need for the bio anymore */
	if (atomic_dec_and_test(&e->pending_bios)) {
		if (is_write)
			drbd_endio_write_sec_final(e);
		else
			drbd_endio_read_sec_final(e);
	}
}

/* read, readA or write requests on R_PRIMARY coming from drbd_make_request
 */
void drbd_endio_pri(struct bio *bio, int error)
{
	unsigned long flags;
	struct drbd_request *req = bio->bi_private;
	struct drbd_conf *mdev = req->mdev;
	struct bio_and_error m;
	enum drbd_req_event what;
	int uptodate = bio_flagged(bio, BIO_UPTODATE);

	if (!error && !uptodate) {
		dev_warn(DEV, "p %s: setting error to -EIO\n",
			 bio_data_dir(bio) == WRITE ? "write" : "read");
		/* strange behavior of some lower level drivers...
		 * fail the request by clearing the uptodate flag,
		 * but do not return any error?! */
		error = -EIO;
	}

	/* to avoid recursion in __req_mod */
	if (unlikely(error)) {
		what = (bio_data_dir(bio) == WRITE)
			? write_completed_with_error
			: (bio_rw(bio) == READ)
			  ? read_completed_with_error
			  : read_ahead_completed_with_error;
	} else
		what = completed_ok;

	bio_put(req->private_bio);
	req->private_bio = ERR_PTR(error);

	spin_lock_irqsave(&mdev->req_lock, flags);
	__req_mod(req, what, &m);
	spin_unlock_irqrestore(&mdev->req_lock, flags);

	if (m.bio)
		complete_master_bio(mdev, &m);
}

int w_read_retry_remote(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_request *req = container_of(w, struct drbd_request, w);

	/* We should not detach for read io-error,
	 * but try to WRITE the P_DATA_REPLY to the failed location,
	 * to give the disk the chance to relocate that block */

	spin_lock_irq(&mdev->req_lock);
	if (cancel || mdev->state.pdsk != D_UP_TO_DATE) {
		_req_mod(req, read_retry_remote_canceled);
		spin_unlock_irq(&mdev->req_lock);
		return 1;
	}
	spin_unlock_irq(&mdev->req_lock);

	return w_send_read_req(mdev, w, 0);
}

int w_resync_inactive(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	ERR_IF(cancel) return 1;
	dev_err(DEV, "resync inactive, but callback triggered??\n");
	return 1; /* Simply ignore this! */
}

void drbd_csum_ee(struct drbd_conf *mdev, struct crypto_hash *tfm, struct drbd_epoch_entry *e, void *digest)
{
	struct hash_desc desc;
	struct scatterlist sg;
	struct page *page = e->pages;
	struct page *tmp;
	unsigned len;

	desc.tfm = tfm;
	desc.flags = 0;

	sg_init_table(&sg, 1);
	crypto_hash_init(&desc);

	while ((tmp = page_chain_next(page))) {
		/* all but the last page will be fully used */
		sg_set_page(&sg, page, PAGE_SIZE, 0);
		crypto_hash_update(&desc, &sg, sg.length);
		page = tmp;
	}
	/* and now the last, possibly only partially used page */
	len = e->size & (PAGE_SIZE - 1);
	sg_set_page(&sg, page, len ?: PAGE_SIZE, 0);
	crypto_hash_update(&desc, &sg, sg.length);
	crypto_hash_final(&desc, digest);
}

void drbd_csum_bio(struct drbd_conf *mdev, struct crypto_hash *tfm, struct bio *bio, void *digest)
{
	struct hash_desc desc;
	struct scatterlist sg;
	struct bio_vec *bvec;
	int i;

	desc.tfm = tfm;
	desc.flags = 0;

	sg_init_table(&sg, 1);
	crypto_hash_init(&desc);

	__bio_for_each_segment(bvec, bio, i, 0) {
		sg_set_page(&sg, bvec->bv_page, bvec->bv_len, bvec->bv_offset);
		crypto_hash_update(&desc, &sg, sg.length);
	}
	crypto_hash_final(&desc, digest);
}

static int w_e_send_csum(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_epoch_entry *e = container_of(w, struct drbd_epoch_entry, w);
	int digest_size;
	void *digest;
	int ok;

	D_ASSERT(e->block_id == DRBD_MAGIC + 0xbeef);

	if (unlikely(cancel)) {
		drbd_free_ee(mdev, e);
		return 1;
	}

	if (likely((e->flags & EE_WAS_ERROR) == 0)) {
		digest_size = crypto_hash_digestsize(mdev->csums_tfm);
		digest = kmalloc(digest_size, GFP_NOIO);
		if (digest) {
			drbd_csum_ee(mdev, mdev->csums_tfm, e, digest);

			inc_rs_pending(mdev);
			ok = drbd_send_drequest_csum(mdev,
						     e->sector,
						     e->size,
						     digest,
						     digest_size,
						     P_CSUM_RS_REQUEST);
			kfree(digest);
		} else {
			dev_err(DEV, "kmalloc() of digest failed.\n");
			ok = 0;
		}
	} else
		ok = 1;

	drbd_free_ee(mdev, e);

	if (unlikely(!ok))
		dev_err(DEV, "drbd_send_drequest(..., csum) failed\n");
	return ok;
}

#define GFP_TRY	(__GFP_HIGHMEM | __GFP_NOWARN)

static int read_for_csum(struct drbd_conf *mdev, sector_t sector, int size)
{
	struct drbd_epoch_entry *e;

	if (!get_ldev(mdev))
		return 0;

	/* GFP_TRY, because if there is no memory available right now, this may
	 * be rescheduled for later. It is "only" background resync, after all. */
	e = drbd_alloc_ee(mdev, DRBD_MAGIC+0xbeef, sector, size, GFP_TRY);
	if (!e)
		goto fail;

	spin_lock_irq(&mdev->req_lock);
	list_add(&e->w.list, &mdev->read_ee);
	spin_unlock_irq(&mdev->req_lock);

	e->w.cb = w_e_send_csum;
	if (drbd_submit_ee(mdev, e, READ, DRBD_FAULT_RS_RD) == 0)
		return 1;

	drbd_free_ee(mdev, e);
fail:
	put_ldev(mdev);
	return 2;
}

void resync_timer_fn(unsigned long data)
{
	unsigned long flags;
	struct drbd_conf *mdev = (struct drbd_conf *) data;
	int queue;

	spin_lock_irqsave(&mdev->req_lock, flags);

	if (likely(!test_and_clear_bit(STOP_SYNC_TIMER, &mdev->flags))) {
		queue = 1;
		if (mdev->state.conn == C_VERIFY_S)
			mdev->resync_work.cb = w_make_ov_request;
		else
			mdev->resync_work.cb = w_make_resync_request;
	} else {
		queue = 0;
		mdev->resync_work.cb = w_resync_inactive;
	}

	spin_unlock_irqrestore(&mdev->req_lock, flags);

	/* harmless race: list_empty outside data.work.q_lock */
	if (list_empty(&mdev->resync_work.list) && queue)
		drbd_queue_work(&mdev->data.work, &mdev->resync_work);
}

int w_make_resync_request(struct drbd_conf *mdev,
		struct drbd_work *w, int cancel)
{
	unsigned long bit;
	sector_t sector;
	const sector_t capacity = drbd_get_capacity(mdev->this_bdev);
	int max_segment_size;
	int number, i, size, pe, mx;
	int align, queued, sndbuf;

	if (unlikely(cancel))
		return 1;

	if (unlikely(mdev->state.conn < C_CONNECTED)) {
		dev_err(DEV, "Confused in w_make_resync_request()! cstate < Connected");
		return 0;
	}

	if (mdev->state.conn != C_SYNC_TARGET)
		dev_err(DEV, "%s in w_make_resync_request\n",
			drbd_conn_str(mdev->state.conn));

	if (!get_ldev(mdev)) {
		/* Since we only need to access mdev->rsync a
		   get_ldev_if_state(mdev,D_FAILED) would be sufficient, but
		   to continue resync with a broken disk makes no sense at
		   all */
		dev_err(DEV, "Disk broke down during resync!\n");
		mdev->resync_work.cb = w_resync_inactive;
		return 1;
	}

	/* starting with drbd 8.3.8, we can handle multi-bio EEs,
	 * if it should be necessary */
	max_segment_size = mdev->agreed_pro_version < 94 ?
		queue_max_segment_size(mdev->rq_queue) : DRBD_MAX_SEGMENT_SIZE;

	number = SLEEP_TIME * mdev->sync_conf.rate  / ((BM_BLOCK_SIZE / 1024) * HZ);
	pe = atomic_read(&mdev->rs_pending_cnt);

	mutex_lock(&mdev->data.mutex);
	if (mdev->data.socket)
		mx = mdev->data.socket->sk->sk_rcvbuf / sizeof(struct p_block_req);
	else
		mx = 1;
	mutex_unlock(&mdev->data.mutex);

	/* For resync rates >160MB/sec, allow more pending RS requests */
	if (number > mx)
		mx = number;

	/* Limit the number of pending RS requests to no more than the peer's receive buffer */
	if ((pe + number) > mx) {
		number = mx - pe;
	}

	for (i = 0; i < number; i++) {
		/* Stop generating RS requests, when half of the send buffer is filled */
		mutex_lock(&mdev->data.mutex);
		if (mdev->data.socket) {
			queued = mdev->data.socket->sk->sk_wmem_queued;
			sndbuf = mdev->data.socket->sk->sk_sndbuf;
		} else {
			queued = 1;
			sndbuf = 0;
		}
		mutex_unlock(&mdev->data.mutex);
		if (queued > sndbuf / 2)
			goto requeue;

next_sector:
		size = BM_BLOCK_SIZE;
		bit  = drbd_bm_find_next(mdev, mdev->bm_resync_fo);

		if (bit == -1UL) {
			mdev->bm_resync_fo = drbd_bm_bits(mdev);
			mdev->resync_work.cb = w_resync_inactive;
			put_ldev(mdev);
			return 1;
		}

		sector = BM_BIT_TO_SECT(bit);

		if (drbd_try_rs_begin_io(mdev, sector)) {
			mdev->bm_resync_fo = bit;
			goto requeue;
		}
		mdev->bm_resync_fo = bit + 1;

		if (unlikely(drbd_bm_test_bit(mdev, bit) == 0)) {
			drbd_rs_complete_io(mdev, sector);
			goto next_sector;
		}

#if DRBD_MAX_SEGMENT_SIZE > BM_BLOCK_SIZE
		/* try to find some adjacent bits.
		 * we stop if we have already the maximum req size.
		 *
		 * Additionally always align bigger requests, in order to
		 * be prepared for all stripe sizes of software RAIDs.
		 */
		align = 1;
		for (;;) {
			if (size + BM_BLOCK_SIZE > max_segment_size)
				break;

			/* Be always aligned */
			if (sector & ((1<<(align+3))-1))
				break;

			/* do not cross extent boundaries */
			if (((bit+1) & BM_BLOCKS_PER_BM_EXT_MASK) == 0)
				break;
			/* now, is it actually dirty, after all?
			 * caution, drbd_bm_test_bit is tri-state for some
			 * obscure reason; ( b == 0 ) would get the out-of-band
			 * only accidentally right because of the "oddly sized"
			 * adjustment below */
			if (drbd_bm_test_bit(mdev, bit+1) != 1)
				break;
			bit++;
			size += BM_BLOCK_SIZE;
			if ((BM_BLOCK_SIZE << align) <= size)
				align++;
			i++;
		}
		/* if we merged some,
		 * reset the offset to start the next drbd_bm_find_next from */
		if (size > BM_BLOCK_SIZE)
			mdev->bm_resync_fo = bit + 1;
#endif

		/* adjust very last sectors, in case we are oddly sized */
		if (sector + (size>>9) > capacity)
			size = (capacity-sector)<<9;
		if (mdev->agreed_pro_version >= 89 && mdev->csums_tfm) {
			switch (read_for_csum(mdev, sector, size)) {
			case 0: /* Disk failure*/
				put_ldev(mdev);
				return 0;
			case 2: /* Allocation failed */
				drbd_rs_complete_io(mdev, sector);
				mdev->bm_resync_fo = BM_SECT_TO_BIT(sector);
				goto requeue;
			/* case 1: everything ok */
			}
		} else {
			inc_rs_pending(mdev);
			if (!drbd_send_drequest(mdev, P_RS_DATA_REQUEST,
					       sector, size, ID_SYNCER)) {
				dev_err(DEV, "drbd_send_drequest() failed, aborting...\n");
				dec_rs_pending(mdev);
				put_ldev(mdev);
				return 0;
			}
		}
	}

	if (mdev->bm_resync_fo >= drbd_bm_bits(mdev)) {
		/* last syncer _request_ was sent,
		 * but the P_RS_DATA_REPLY not yet received.  sync will end (and
		 * next sync group will resume), as soon as we receive the last
		 * resync data block, and the last bit is cleared.
		 * until then resync "work" is "inactive" ...
		 */
		mdev->resync_work.cb = w_resync_inactive;
		put_ldev(mdev);
		return 1;
	}

 requeue:
	mod_timer(&mdev->resync_timer, jiffies + SLEEP_TIME);
	put_ldev(mdev);
	return 1;
}

static int w_make_ov_request(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	int number, i, size;
	sector_t sector;
	const sector_t capacity = drbd_get_capacity(mdev->this_bdev);

	if (unlikely(cancel))
		return 1;

	if (unlikely(mdev->state.conn < C_CONNECTED)) {
		dev_err(DEV, "Confused in w_make_ov_request()! cstate < Connected");
		return 0;
	}

	number = SLEEP_TIME*mdev->sync_conf.rate / ((BM_BLOCK_SIZE/1024)*HZ);
	if (atomic_read(&mdev->rs_pending_cnt) > number)
		goto requeue;

	number -= atomic_read(&mdev->rs_pending_cnt);

	sector = mdev->ov_position;
	for (i = 0; i < number; i++) {
		if (sector >= capacity) {
			mdev->resync_work.cb = w_resync_inactive;
			return 1;
		}

		size = BM_BLOCK_SIZE;

		if (drbd_try_rs_begin_io(mdev, sector)) {
			mdev->ov_position = sector;
			goto requeue;
		}

		if (sector + (size>>9) > capacity)
			size = (capacity-sector)<<9;

		inc_rs_pending(mdev);
		if (!drbd_send_ov_request(mdev, sector, size)) {
			dec_rs_pending(mdev);
			return 0;
		}
		sector += BM_SECT_PER_BIT;
	}
	mdev->ov_position = sector;

 requeue:
	mod_timer(&mdev->resync_timer, jiffies + SLEEP_TIME);
	return 1;
}


int w_ov_finished(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	kfree(w);
	ov_oos_print(mdev);
	drbd_resync_finished(mdev);

	return 1;
}

static int w_resync_finished(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	kfree(w);

	drbd_resync_finished(mdev);

	return 1;
}

int drbd_resync_finished(struct drbd_conf *mdev)
{
	unsigned long db, dt, dbdt;
	unsigned long n_oos;
	union drbd_state os, ns;
	struct drbd_work *w;
	char *khelper_cmd = NULL;

	/* Remove all elements from the resync LRU. Since future actions
	 * might set bits in the (main) bitmap, then the entries in the
	 * resync LRU would be wrong. */
	if (drbd_rs_del_all(mdev)) {
		/* In case this is not possible now, most probably because
		 * there are P_RS_DATA_REPLY Packets lingering on the worker's
		 * queue (or even the read operations for those packets
		 * is not finished by now).   Retry in 100ms. */

		drbd_kick_lo(mdev);
		__set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 10);
		w = kmalloc(sizeof(struct drbd_work), GFP_ATOMIC);
		if (w) {
			w->cb = w_resync_finished;
			drbd_queue_work(&mdev->data.work, w);
			return 1;
		}
		dev_err(DEV, "Warn failed to drbd_rs_del_all() and to kmalloc(w).\n");
	}

	dt = (jiffies - mdev->rs_start - mdev->rs_paused) / HZ;
	if (dt <= 0)
		dt = 1;
	db = mdev->rs_total;
	dbdt = Bit2KB(db/dt);
	mdev->rs_paused /= HZ;

	if (!get_ldev(mdev))
		goto out;

	spin_lock_irq(&mdev->req_lock);
	os = mdev->state;

	/* This protects us against multiple calls (that can happen in the presence
	   of application IO), and against connectivity loss just before we arrive here. */
	if (os.conn <= C_CONNECTED)
		goto out_unlock;

	ns = os;
	ns.conn = C_CONNECTED;

	dev_info(DEV, "%s done (total %lu sec; paused %lu sec; %lu K/sec)\n",
	     (os.conn == C_VERIFY_S || os.conn == C_VERIFY_T) ?
	     "Online verify " : "Resync",
	     dt + mdev->rs_paused, mdev->rs_paused, dbdt);

	n_oos = drbd_bm_total_weight(mdev);

	if (os.conn == C_VERIFY_S || os.conn == C_VERIFY_T) {
		if (n_oos) {
			dev_alert(DEV, "Online verify found %lu %dk block out of sync!\n",
			      n_oos, Bit2KB(1));
			khelper_cmd = "out-of-sync";
		}
	} else {
		D_ASSERT((n_oos - mdev->rs_failed) == 0);

		if (os.conn == C_SYNC_TARGET || os.conn == C_PAUSED_SYNC_T)
			khelper_cmd = "after-resync-target";

		if (mdev->csums_tfm && mdev->rs_total) {
			const unsigned long s = mdev->rs_same_csum;
			const unsigned long t = mdev->rs_total;
			const int ratio =
				(t == 0)     ? 0 :
			(t < 100000) ? ((s*100)/t) : (s/(t/100));
			dev_info(DEV, "%u %% had equal check sums, eliminated: %luK; "
			     "transferred %luK total %luK\n",
			     ratio,
			     Bit2KB(mdev->rs_same_csum),
			     Bit2KB(mdev->rs_total - mdev->rs_same_csum),
			     Bit2KB(mdev->rs_total));
		}
	}

	if (mdev->rs_failed) {
		dev_info(DEV, "            %lu failed blocks\n", mdev->rs_failed);

		if (os.conn == C_SYNC_TARGET || os.conn == C_PAUSED_SYNC_T) {
			ns.disk = D_INCONSISTENT;
			ns.pdsk = D_UP_TO_DATE;
		} else {
			ns.disk = D_UP_TO_DATE;
			ns.pdsk = D_INCONSISTENT;
		}
	} else {
		ns.disk = D_UP_TO_DATE;
		ns.pdsk = D_UP_TO_DATE;

		if (os.conn == C_SYNC_TARGET || os.conn == C_PAUSED_SYNC_T) {
			if (mdev->p_uuid) {
				int i;
				for (i = UI_BITMAP ; i <= UI_HISTORY_END ; i++)
					_drbd_uuid_set(mdev, i, mdev->p_uuid[i]);
				drbd_uuid_set(mdev, UI_BITMAP, mdev->ldev->md.uuid[UI_CURRENT]);
				_drbd_uuid_set(mdev, UI_CURRENT, mdev->p_uuid[UI_CURRENT]);
			} else {
				dev_err(DEV, "mdev->p_uuid is NULL! BUG\n");
			}
		}

		drbd_uuid_set_bm(mdev, 0UL);

		if (mdev->p_uuid) {
			/* Now the two UUID sets are equal, update what we
			 * know of the peer. */
			int i;
			for (i = UI_CURRENT ; i <= UI_HISTORY_END ; i++)
				mdev->p_uuid[i] = mdev->ldev->md.uuid[i];
		}
	}

	_drbd_set_state(mdev, ns, CS_VERBOSE, NULL);
out_unlock:
	spin_unlock_irq(&mdev->req_lock);
	put_ldev(mdev);
out:
	mdev->rs_total  = 0;
	mdev->rs_failed = 0;
	mdev->rs_paused = 0;
	mdev->ov_start_sector = 0;

	if (test_and_clear_bit(WRITE_BM_AFTER_RESYNC, &mdev->flags)) {
		dev_warn(DEV, "Writing the whole bitmap, due to failed kmalloc\n");
		drbd_queue_bitmap_io(mdev, &drbd_bm_write, NULL, "write from resync_finished");
	}

	if (khelper_cmd)
		drbd_khelper(mdev, khelper_cmd);

	return 1;
}

/* helper */
static void move_to_net_ee_or_free(struct drbd_conf *mdev, struct drbd_epoch_entry *e)
{
	if (drbd_ee_has_active_page(e)) {
		/* This might happen if sendpage() has not finished */
		spin_lock_irq(&mdev->req_lock);
		list_add_tail(&e->w.list, &mdev->net_ee);
		spin_unlock_irq(&mdev->req_lock);
	} else
		drbd_free_ee(mdev, e);
}

/**
 * w_e_end_data_req() - Worker callback, to send a P_DATA_REPLY packet in response to a P_DATA_REQUEST
 * @mdev:	DRBD device.
 * @w:		work object.
 * @cancel:	The connection will be closed anyways
 */
int w_e_end_data_req(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_epoch_entry *e = container_of(w, struct drbd_epoch_entry, w);
	int ok;

	if (unlikely(cancel)) {
		drbd_free_ee(mdev, e);
		dec_unacked(mdev);
		return 1;
	}

	if (likely((e->flags & EE_WAS_ERROR) == 0)) {
		ok = drbd_send_block(mdev, P_DATA_REPLY, e);
	} else {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "Sending NegDReply. sector=%llus.\n",
			    (unsigned long long)e->sector);

		ok = drbd_send_ack(mdev, P_NEG_DREPLY, e);
	}

	dec_unacked(mdev);

	move_to_net_ee_or_free(mdev, e);

	if (unlikely(!ok))
		dev_err(DEV, "drbd_send_block() failed\n");
	return ok;
}

/**
 * w_e_end_rsdata_req() - Worker callback to send a P_RS_DATA_REPLY packet in response to a P_RS_DATA_REQUESTRS
 * @mdev:	DRBD device.
 * @w:		work object.
 * @cancel:	The connection will be closed anyways
 */
int w_e_end_rsdata_req(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_epoch_entry *e = container_of(w, struct drbd_epoch_entry, w);
	int ok;

	if (unlikely(cancel)) {
		drbd_free_ee(mdev, e);
		dec_unacked(mdev);
		return 1;
	}

	if (get_ldev_if_state(mdev, D_FAILED)) {
		drbd_rs_complete_io(mdev, e->sector);
		put_ldev(mdev);
	}

	if (likely((e->flags & EE_WAS_ERROR) == 0)) {
		if (likely(mdev->state.pdsk >= D_INCONSISTENT)) {
			inc_rs_pending(mdev);
			ok = drbd_send_block(mdev, P_RS_DATA_REPLY, e);
		} else {
			if (__ratelimit(&drbd_ratelimit_state))
				dev_err(DEV, "Not sending RSDataReply, "
				    "partner DISKLESS!\n");
			ok = 1;
		}
	} else {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "Sending NegRSDReply. sector %llus.\n",
			    (unsigned long long)e->sector);

		ok = drbd_send_ack(mdev, P_NEG_RS_DREPLY, e);

		/* update resync data with failure */
		drbd_rs_failed_io(mdev, e->sector, e->size);
	}

	dec_unacked(mdev);

	move_to_net_ee_or_free(mdev, e);

	if (unlikely(!ok))
		dev_err(DEV, "drbd_send_block() failed\n");
	return ok;
}

int w_e_end_csum_rs_req(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_epoch_entry *e = container_of(w, struct drbd_epoch_entry, w);
	struct digest_info *di;
	int digest_size;
	void *digest = NULL;
	int ok, eq = 0;

	if (unlikely(cancel)) {
		drbd_free_ee(mdev, e);
		dec_unacked(mdev);
		return 1;
	}

	drbd_rs_complete_io(mdev, e->sector);

	di = (struct digest_info *)(unsigned long)e->block_id;

	if (likely((e->flags & EE_WAS_ERROR) == 0)) {
		/* quick hack to try to avoid a race against reconfiguration.
		 * a real fix would be much more involved,
		 * introducing more locking mechanisms */
		if (mdev->csums_tfm) {
			digest_size = crypto_hash_digestsize(mdev->csums_tfm);
			D_ASSERT(digest_size == di->digest_size);
			digest = kmalloc(digest_size, GFP_NOIO);
		}
		if (digest) {
			drbd_csum_ee(mdev, mdev->csums_tfm, e, digest);
			eq = !memcmp(digest, di->digest, digest_size);
			kfree(digest);
		}

		if (eq) {
			drbd_set_in_sync(mdev, e->sector, e->size);
			/* rs_same_csums unit is BM_BLOCK_SIZE */
			mdev->rs_same_csum += e->size >> BM_BLOCK_SHIFT;
			ok = drbd_send_ack(mdev, P_RS_IS_IN_SYNC, e);
		} else {
			inc_rs_pending(mdev);
			e->block_id = ID_SYNCER;
			ok = drbd_send_block(mdev, P_RS_DATA_REPLY, e);
		}
	} else {
		ok = drbd_send_ack(mdev, P_NEG_RS_DREPLY, e);
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "Sending NegDReply. I guess it gets messy.\n");
	}

	dec_unacked(mdev);

	kfree(di);

	move_to_net_ee_or_free(mdev, e);

	if (unlikely(!ok))
		dev_err(DEV, "drbd_send_block/ack() failed\n");
	return ok;
}

int w_e_end_ov_req(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_epoch_entry *e = container_of(w, struct drbd_epoch_entry, w);
	int digest_size;
	void *digest;
	int ok = 1;

	if (unlikely(cancel))
		goto out;

	if (unlikely((e->flags & EE_WAS_ERROR) != 0))
		goto out;

	digest_size = crypto_hash_digestsize(mdev->verify_tfm);
	/* FIXME if this allocation fails, online verify will not terminate! */
	digest = kmalloc(digest_size, GFP_NOIO);
	if (digest) {
		drbd_csum_ee(mdev, mdev->verify_tfm, e, digest);
		inc_rs_pending(mdev);
		ok = drbd_send_drequest_csum(mdev, e->sector, e->size,
					     digest, digest_size, P_OV_REPLY);
		if (!ok)
			dec_rs_pending(mdev);
		kfree(digest);
	}

out:
	drbd_free_ee(mdev, e);

	dec_unacked(mdev);

	return ok;
}

void drbd_ov_oos_found(struct drbd_conf *mdev, sector_t sector, int size)
{
	if (mdev->ov_last_oos_start + mdev->ov_last_oos_size == sector) {
		mdev->ov_last_oos_size += size>>9;
	} else {
		mdev->ov_last_oos_start = sector;
		mdev->ov_last_oos_size = size>>9;
	}
	drbd_set_out_of_sync(mdev, sector, size);
	set_bit(WRITE_BM_AFTER_RESYNC, &mdev->flags);
}

int w_e_end_ov_reply(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_epoch_entry *e = container_of(w, struct drbd_epoch_entry, w);
	struct digest_info *di;
	int digest_size;
	void *digest;
	int ok, eq = 0;

	if (unlikely(cancel)) {
		drbd_free_ee(mdev, e);
		dec_unacked(mdev);
		return 1;
	}

	/* after "cancel", because after drbd_disconnect/drbd_rs_cancel_all
	 * the resync lru has been cleaned up already */
	drbd_rs_complete_io(mdev, e->sector);

	di = (struct digest_info *)(unsigned long)e->block_id;

	if (likely((e->flags & EE_WAS_ERROR) == 0)) {
		digest_size = crypto_hash_digestsize(mdev->verify_tfm);
		digest = kmalloc(digest_size, GFP_NOIO);
		if (digest) {
			drbd_csum_ee(mdev, mdev->verify_tfm, e, digest);

			D_ASSERT(digest_size == di->digest_size);
			eq = !memcmp(digest, di->digest, digest_size);
			kfree(digest);
		}
	} else {
		ok = drbd_send_ack(mdev, P_NEG_RS_DREPLY, e);
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "Sending NegDReply. I guess it gets messy.\n");
	}

	dec_unacked(mdev);

	kfree(di);

	if (!eq)
		drbd_ov_oos_found(mdev, e->sector, e->size);
	else
		ov_oos_print(mdev);

	ok = drbd_send_ack_ex(mdev, P_OV_RESULT, e->sector, e->size,
			      eq ? ID_IN_SYNC : ID_OUT_OF_SYNC);

	drbd_free_ee(mdev, e);

	if (--mdev->ov_left == 0) {
		ov_oos_print(mdev);
		drbd_resync_finished(mdev);
	}

	return ok;
}

int w_prev_work_done(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_wq_barrier *b = container_of(w, struct drbd_wq_barrier, w);
	complete(&b->done);
	return 1;
}

int w_send_barrier(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_tl_epoch *b = container_of(w, struct drbd_tl_epoch, w);
	struct p_barrier *p = &mdev->data.sbuf.barrier;
	int ok = 1;

	/* really avoid racing with tl_clear.  w.cb may have been referenced
	 * just before it was reassigned and re-queued, so double check that.
	 * actually, this race was harmless, since we only try to send the
	 * barrier packet here, and otherwise do nothing with the object.
	 * but compare with the head of w_clear_epoch */
	spin_lock_irq(&mdev->req_lock);
	if (w->cb != w_send_barrier || mdev->state.conn < C_CONNECTED)
		cancel = 1;
	spin_unlock_irq(&mdev->req_lock);
	if (cancel)
		return 1;

	if (!drbd_get_data_sock(mdev))
		return 0;
	p->barrier = b->br_number;
	/* inc_ap_pending was done where this was queued.
	 * dec_ap_pending will be done in got_BarrierAck
	 * or (on connection loss) in w_clear_epoch.  */
	ok = _drbd_send_cmd(mdev, mdev->data.socket, P_BARRIER,
				(struct p_header *)p, sizeof(*p), 0);
	drbd_put_data_sock(mdev);

	return ok;
}

int w_send_write_hint(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	if (cancel)
		return 1;
	return drbd_send_short_cmd(mdev, P_UNPLUG_REMOTE);
}

/**
 * w_send_dblock() - Worker callback to send a P_DATA packet in order to mirror a write request
 * @mdev:	DRBD device.
 * @w:		work object.
 * @cancel:	The connection will be closed anyways
 */
int w_send_dblock(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_request *req = container_of(w, struct drbd_request, w);
	int ok;

	if (unlikely(cancel)) {
		req_mod(req, send_canceled);
		return 1;
	}

	ok = drbd_send_dblock(mdev, req);
	req_mod(req, ok ? handed_over_to_network : send_failed);

	return ok;
}

/**
 * w_send_read_req() - Worker callback to send a read request (P_DATA_REQUEST) packet
 * @mdev:	DRBD device.
 * @w:		work object.
 * @cancel:	The connection will be closed anyways
 */
int w_send_read_req(struct drbd_conf *mdev, struct drbd_work *w, int cancel)
{
	struct drbd_request *req = container_of(w, struct drbd_request, w);
	int ok;

	if (unlikely(cancel)) {
		req_mod(req, send_canceled);
		return 1;
	}

	ok = drbd_send_drequest(mdev, P_DATA_REQUEST, req->sector, req->size,
				(unsigned long)req);

	if (!ok) {
		/* ?? we set C_TIMEOUT or C_BROKEN_PIPE in drbd_send();
		 * so this is probably redundant */
		if (mdev->state.conn >= C_CONNECTED)
			drbd_force_state(mdev, NS(conn, C_NETWORK_FAILURE));
	}
	req_mod(req, ok ? handed_over_to_network : send_failed);

	return ok;
}

static int _drbd_may_sync_now(struct drbd_conf *mdev)
{
	struct drbd_conf *odev = mdev;

	while (1) {
		if (odev->sync_conf.after == -1)
			return 1;
		odev = minor_to_mdev(odev->sync_conf.after);
		ERR_IF(!odev) return 1;
		if ((odev->state.conn >= C_SYNC_SOURCE &&
		     odev->state.conn <= C_PAUSED_SYNC_T) ||
		    odev->state.aftr_isp || odev->state.peer_isp ||
		    odev->state.user_isp)
			return 0;
	}
}

/**
 * _drbd_pause_after() - Pause resync on all devices that may not resync now
 * @mdev:	DRBD device.
 *
 * Called from process context only (admin command and after_state_ch).
 */
static int _drbd_pause_after(struct drbd_conf *mdev)
{
	struct drbd_conf *odev;
	int i, rv = 0;

	for (i = 0; i < minor_count; i++) {
		odev = minor_to_mdev(i);
		if (!odev)
			continue;
		if (odev->state.conn == C_STANDALONE && odev->state.disk == D_DISKLESS)
			continue;
		if (!_drbd_may_sync_now(odev))
			rv |= (__drbd_set_state(_NS(odev, aftr_isp, 1), CS_HARD, NULL)
			       != SS_NOTHING_TO_DO);
	}

	return rv;
}

/**
 * _drbd_resume_next() - Resume resync on all devices that may resync now
 * @mdev:	DRBD device.
 *
 * Called from process context only (admin command and worker).
 */
static int _drbd_resume_next(struct drbd_conf *mdev)
{
	struct drbd_conf *odev;
	int i, rv = 0;

	for (i = 0; i < minor_count; i++) {
		odev = minor_to_mdev(i);
		if (!odev)
			continue;
		if (odev->state.conn == C_STANDALONE && odev->state.disk == D_DISKLESS)
			continue;
		if (odev->state.aftr_isp) {
			if (_drbd_may_sync_now(odev))
				rv |= (__drbd_set_state(_NS(odev, aftr_isp, 0),
							CS_HARD, NULL)
				       != SS_NOTHING_TO_DO) ;
		}
	}
	return rv;
}

void resume_next_sg(struct drbd_conf *mdev)
{
	write_lock_irq(&global_state_lock);
	_drbd_resume_next(mdev);
	write_unlock_irq(&global_state_lock);
}

void suspend_other_sg(struct drbd_conf *mdev)
{
	write_lock_irq(&global_state_lock);
	_drbd_pause_after(mdev);
	write_unlock_irq(&global_state_lock);
}

static int sync_after_error(struct drbd_conf *mdev, int o_minor)
{
	struct drbd_conf *odev;

	if (o_minor == -1)
		return NO_ERROR;
	if (o_minor < -1 || minor_to_mdev(o_minor) == NULL)
		return ERR_SYNC_AFTER;

	/* check for loops */
	odev = minor_to_mdev(o_minor);
	while (1) {
		if (odev == mdev)
			return ERR_SYNC_AFTER_CYCLE;

		/* dependency chain ends here, no cycles. */
		if (odev->sync_conf.after == -1)
			return NO_ERROR;

		/* follow the dependency chain */
		odev = minor_to_mdev(odev->sync_conf.after);
	}
}

int drbd_alter_sa(struct drbd_conf *mdev, int na)
{
	int changes;
	int retcode;

	write_lock_irq(&global_state_lock);
	retcode = sync_after_error(mdev, na);
	if (retcode == NO_ERROR) {
		mdev->sync_conf.after = na;
		do {
			changes  = _drbd_pause_after(mdev);
			changes |= _drbd_resume_next(mdev);
		} while (changes);
	}
	write_unlock_irq(&global_state_lock);
	return retcode;
}

static void ping_peer(struct drbd_conf *mdev)
{
	clear_bit(GOT_PING_ACK, &mdev->flags);
	request_ping(mdev);
	wait_event(mdev->misc_wait,
		   test_bit(GOT_PING_ACK, &mdev->flags) || mdev->state.conn < C_CONNECTED);
}

/**
 * drbd_start_resync() - Start the resync process
 * @mdev:	DRBD device.
 * @side:	Either C_SYNC_SOURCE or C_SYNC_TARGET
 *
 * This function might bring you directly into one of the
 * C_PAUSED_SYNC_* states.
 */
void drbd_start_resync(struct drbd_conf *mdev, enum drbd_conns side)
{
	union drbd_state ns;
	int r;

	if (mdev->state.conn >= C_SYNC_SOURCE) {
		dev_err(DEV, "Resync already running!\n");
		return;
	}

	/* In case a previous resync run was aborted by an IO error/detach on the peer. */
	drbd_rs_cancel_all(mdev);

	if (side == C_SYNC_TARGET) {
		/* Since application IO was locked out during C_WF_BITMAP_T and
		   C_WF_SYNC_UUID we are still unmodified. Before going to C_SYNC_TARGET
		   we check that we might make the data inconsistent. */
		r = drbd_khelper(mdev, "before-resync-target");
		r = (r >> 8) & 0xff;
		if (r > 0) {
			dev_info(DEV, "before-resync-target handler returned %d, "
			     "dropping connection.\n", r);
			drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
			return;
		}
	}

	drbd_state_lock(mdev);

	if (!get_ldev_if_state(mdev, D_NEGOTIATING)) {
		drbd_state_unlock(mdev);
		return;
	}

	if (side == C_SYNC_TARGET) {
		mdev->bm_resync_fo = 0;
	} else /* side == C_SYNC_SOURCE */ {
		u64 uuid;

		get_random_bytes(&uuid, sizeof(u64));
		drbd_uuid_set(mdev, UI_BITMAP, uuid);
		drbd_send_sync_uuid(mdev, uuid);

		D_ASSERT(mdev->state.disk == D_UP_TO_DATE);
	}

	write_lock_irq(&global_state_lock);
	ns = mdev->state;

	ns.aftr_isp = !_drbd_may_sync_now(mdev);

	ns.conn = side;

	if (side == C_SYNC_TARGET)
		ns.disk = D_INCONSISTENT;
	else /* side == C_SYNC_SOURCE */
		ns.pdsk = D_INCONSISTENT;

	r = __drbd_set_state(mdev, ns, CS_VERBOSE, NULL);
	ns = mdev->state;

	if (ns.conn < C_CONNECTED)
		r = SS_UNKNOWN_ERROR;

	if (r == SS_SUCCESS) {
		mdev->rs_total     =
		mdev->rs_mark_left = drbd_bm_total_weight(mdev);
		mdev->rs_failed    = 0;
		mdev->rs_paused    = 0;
		mdev->rs_start     =
		mdev->rs_mark_time = jiffies;
		mdev->rs_same_csum = 0;
		_drbd_pause_after(mdev);
	}
	write_unlock_irq(&global_state_lock);
	put_ldev(mdev);

	if (r == SS_SUCCESS) {
		dev_info(DEV, "Began resync as %s (will sync %lu KB [%lu bits set]).\n",
		     drbd_conn_str(ns.conn),
		     (unsigned long) mdev->rs_total << (BM_BLOCK_SHIFT-10),
		     (unsigned long) mdev->rs_total);

		if (mdev->rs_total == 0) {
			/* Peer still reachable? Beware of failing before-resync-target handlers! */
			ping_peer(mdev);
			drbd_resync_finished(mdev);
		}

		/* ns.conn may already be != mdev->state.conn,
		 * we may have been paused in between, or become paused until
		 * the timer triggers.
		 * No matter, that is handled in resync_timer_fn() */
		if (ns.conn == C_SYNC_TARGET)
			mod_timer(&mdev->resync_timer, jiffies);

		drbd_md_sync(mdev);
	}
	drbd_state_unlock(mdev);
}

int drbd_worker(struct drbd_thread *thi)
{
	struct drbd_conf *mdev = thi->mdev;
	struct drbd_work *w = NULL;
	LIST_HEAD(work_list);
	int intr = 0, i;

	sprintf(current->comm, "drbd%d_worker", mdev_to_minor(mdev));

	while (get_t_state(thi) == Running) {
		drbd_thread_current_set_cpu(mdev);

		if (down_trylock(&mdev->data.work.s)) {
			mutex_lock(&mdev->data.mutex);
			if (mdev->data.socket && !mdev->net_conf->no_cork)
				drbd_tcp_uncork(mdev->data.socket);
			mutex_unlock(&mdev->data.mutex);

			intr = down_interruptible(&mdev->data.work.s);

			mutex_lock(&mdev->data.mutex);
			if (mdev->data.socket  && !mdev->net_conf->no_cork)
				drbd_tcp_cork(mdev->data.socket);
			mutex_unlock(&mdev->data.mutex);
		}

		if (intr) {
			D_ASSERT(intr == -EINTR);
			flush_signals(current);
			ERR_IF (get_t_state(thi) == Running)
				continue;
			break;
		}

		if (get_t_state(thi) != Running)
			break;
		/* With this break, we have done a down() but not consumed
		   the entry from the list. The cleanup code takes care of
		   this...   */

		w = NULL;
		spin_lock_irq(&mdev->data.work.q_lock);
		ERR_IF(list_empty(&mdev->data.work.q)) {
			/* something terribly wrong in our logic.
			 * we were able to down() the semaphore,
			 * but the list is empty... doh.
			 *
			 * what is the best thing to do now?
			 * try again from scratch, restarting the receiver,
			 * asender, whatnot? could break even more ugly,
			 * e.g. when we are primary, but no good local data.
			 *
			 * I'll try to get away just starting over this loop.
			 */
			spin_unlock_irq(&mdev->data.work.q_lock);
			continue;
		}
		w = list_entry(mdev->data.work.q.next, struct drbd_work, list);
		list_del_init(&w->list);
		spin_unlock_irq(&mdev->data.work.q_lock);

		if (!w->cb(mdev, w, mdev->state.conn < C_CONNECTED)) {
			/* dev_warn(DEV, "worker: a callback failed! \n"); */
			if (mdev->state.conn >= C_CONNECTED)
				drbd_force_state(mdev,
						NS(conn, C_NETWORK_FAILURE));
		}
	}
	D_ASSERT(test_bit(DEVICE_DYING, &mdev->flags));
	D_ASSERT(test_bit(CONFIG_PENDING, &mdev->flags));

	spin_lock_irq(&mdev->data.work.q_lock);
	i = 0;
	while (!list_empty(&mdev->data.work.q)) {
		list_splice_init(&mdev->data.work.q, &work_list);
		spin_unlock_irq(&mdev->data.work.q_lock);

		while (!list_empty(&work_list)) {
			w = list_entry(work_list.next, struct drbd_work, list);
			list_del_init(&w->list);
			w->cb(mdev, w, 1);
			i++; /* dead debugging code */
		}

		spin_lock_irq(&mdev->data.work.q_lock);
	}
	sema_init(&mdev->data.work.s, 0);
	/* DANGEROUS race: if someone did queue his work within the spinlock,
	 * but up() ed outside the spinlock, we could get an up() on the
	 * semaphore without corresponding list entry.
	 * So don't do that.
	 */
	spin_unlock_irq(&mdev->data.work.q_lock);

	D_ASSERT(mdev->state.disk == D_DISKLESS && mdev->state.conn == C_STANDALONE);
	/* _drbd_set_state only uses stop_nowait.
	 * wait here for the Exiting receiver. */
	drbd_thread_stop(&mdev->receiver);
	drbd_mdev_cleanup(mdev);

	dev_info(DEV, "worker terminated\n");

	clear_bit(DEVICE_DYING, &mdev->flags);
	clear_bit(CONFIG_PENDING, &mdev->flags);
	wake_up(&mdev->state_wait);

	return 0;
}
