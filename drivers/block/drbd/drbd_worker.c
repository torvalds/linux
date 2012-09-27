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

static int w_make_ov_request(struct drbd_work *w, int cancel);


/* endio handlers:
 *   drbd_md_io_complete (defined here)
 *   drbd_request_endio (defined here)
 *   drbd_peer_request_endio (defined here)
 *   bm_async_io_complete (defined in drbd_bitmap.c)
 *
 * For all these callbacks, note the following:
 * The callbacks will be called in irq context by the IDE drivers,
 * and in Softirqs/Tasklets/BH context by the SCSI drivers.
 * Try to get the locking right :)
 *
 */


/* About the global_state_lock
   Each state transition on an device holds a read lock. In case we have
   to evaluate the resync after dependencies, we grab a write lock, because
   we need stable states on all devices for that.  */
rwlock_t global_state_lock;

/* used for synchronous meta data and bitmap IO
 * submitted by drbd_md_sync_page_io()
 */
void drbd_md_io_complete(struct bio *bio, int error)
{
	struct drbd_md_io *md_io;
	struct drbd_conf *mdev;

	md_io = (struct drbd_md_io *)bio->bi_private;
	mdev = container_of(md_io, struct drbd_conf, md_io);

	md_io->error = error;

	/* We grabbed an extra reference in _drbd_md_sync_page_io() to be able
	 * to timeout on the lower level device, and eventually detach from it.
	 * If this io completion runs after that timeout expired, this
	 * drbd_md_put_buffer() may allow us to finally try and re-attach.
	 * During normal operation, this only puts that extra reference
	 * down to 1 again.
	 * Make sure we first drop the reference, and only then signal
	 * completion, or we may (in drbd_al_read_log()) cycle so fast into the
	 * next drbd_md_sync_page_io(), that we trigger the
	 * ASSERT(atomic_read(&mdev->md_io_in_use) == 1) there.
	 */
	drbd_md_put_buffer(mdev);
	md_io->done = 1;
	wake_up(&mdev->misc_wait);
	bio_put(bio);
	put_ldev(mdev);
}

/* reads on behalf of the partner,
 * "submitted" by the receiver
 */
void drbd_endio_read_sec_final(struct drbd_peer_request *peer_req) __releases(local)
{
	unsigned long flags = 0;
	struct drbd_conf *mdev = peer_req->w.mdev;

	spin_lock_irqsave(&mdev->tconn->req_lock, flags);
	mdev->read_cnt += peer_req->i.size >> 9;
	list_del(&peer_req->w.list);
	if (list_empty(&mdev->read_ee))
		wake_up(&mdev->ee_wait);
	if (test_bit(__EE_WAS_ERROR, &peer_req->flags))
		__drbd_chk_io_error(mdev, DRBD_READ_ERROR);
	spin_unlock_irqrestore(&mdev->tconn->req_lock, flags);

	drbd_queue_work(&mdev->tconn->sender_work, &peer_req->w);
	put_ldev(mdev);
}

/* writes on behalf of the partner, or resync writes,
 * "submitted" by the receiver, final stage.  */
static void drbd_endio_write_sec_final(struct drbd_peer_request *peer_req) __releases(local)
{
	unsigned long flags = 0;
	struct drbd_conf *mdev = peer_req->w.mdev;
	struct drbd_interval i;
	int do_wake;
	u64 block_id;
	int do_al_complete_io;

	/* after we moved peer_req to done_ee,
	 * we may no longer access it,
	 * it may be freed/reused already!
	 * (as soon as we release the req_lock) */
	i = peer_req->i;
	do_al_complete_io = peer_req->flags & EE_CALL_AL_COMPLETE_IO;
	block_id = peer_req->block_id;

	spin_lock_irqsave(&mdev->tconn->req_lock, flags);
	mdev->writ_cnt += peer_req->i.size >> 9;
	list_move_tail(&peer_req->w.list, &mdev->done_ee);

	/*
	 * Do not remove from the write_requests tree here: we did not send the
	 * Ack yet and did not wake possibly waiting conflicting requests.
	 * Removed from the tree from "drbd_process_done_ee" within the
	 * appropriate w.cb (e_end_block/e_end_resync_block) or from
	 * _drbd_clear_done_ee.
	 */

	do_wake = list_empty(block_id == ID_SYNCER ? &mdev->sync_ee : &mdev->active_ee);

	if (test_bit(__EE_WAS_ERROR, &peer_req->flags))
		__drbd_chk_io_error(mdev, DRBD_WRITE_ERROR);
	spin_unlock_irqrestore(&mdev->tconn->req_lock, flags);

	if (block_id == ID_SYNCER)
		drbd_rs_complete_io(mdev, i.sector);

	if (do_wake)
		wake_up(&mdev->ee_wait);

	if (do_al_complete_io)
		drbd_al_complete_io(mdev, &i);

	wake_asender(mdev->tconn);
	put_ldev(mdev);
}

/* writes on behalf of the partner, or resync writes,
 * "submitted" by the receiver.
 */
void drbd_peer_request_endio(struct bio *bio, int error)
{
	struct drbd_peer_request *peer_req = bio->bi_private;
	struct drbd_conf *mdev = peer_req->w.mdev;
	int uptodate = bio_flagged(bio, BIO_UPTODATE);
	int is_write = bio_data_dir(bio) == WRITE;

	if (error && __ratelimit(&drbd_ratelimit_state))
		dev_warn(DEV, "%s: error=%d s=%llus\n",
				is_write ? "write" : "read", error,
				(unsigned long long)peer_req->i.sector);
	if (!error && !uptodate) {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_warn(DEV, "%s: setting error to -EIO s=%llus\n",
					is_write ? "write" : "read",
					(unsigned long long)peer_req->i.sector);
		/* strange behavior of some lower level drivers...
		 * fail the request by clearing the uptodate flag,
		 * but do not return any error?! */
		error = -EIO;
	}

	if (error)
		set_bit(__EE_WAS_ERROR, &peer_req->flags);

	bio_put(bio); /* no need for the bio anymore */
	if (atomic_dec_and_test(&peer_req->pending_bios)) {
		if (is_write)
			drbd_endio_write_sec_final(peer_req);
		else
			drbd_endio_read_sec_final(peer_req);
	}
}

/* read, readA or write requests on R_PRIMARY coming from drbd_make_request
 */
void drbd_request_endio(struct bio *bio, int error)
{
	unsigned long flags;
	struct drbd_request *req = bio->bi_private;
	struct drbd_conf *mdev = req->w.mdev;
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


	/* If this request was aborted locally before,
	 * but now was completed "successfully",
	 * chances are that this caused arbitrary data corruption.
	 *
	 * "aborting" requests, or force-detaching the disk, is intended for
	 * completely blocked/hung local backing devices which do no longer
	 * complete requests at all, not even do error completions.  In this
	 * situation, usually a hard-reset and failover is the only way out.
	 *
	 * By "aborting", basically faking a local error-completion,
	 * we allow for a more graceful swichover by cleanly migrating services.
	 * Still the affected node has to be rebooted "soon".
	 *
	 * By completing these requests, we allow the upper layers to re-use
	 * the associated data pages.
	 *
	 * If later the local backing device "recovers", and now DMAs some data
	 * from disk into the original request pages, in the best case it will
	 * just put random data into unused pages; but typically it will corrupt
	 * meanwhile completely unrelated data, causing all sorts of damage.
	 *
	 * Which means delayed successful completion,
	 * especially for READ requests,
	 * is a reason to panic().
	 *
	 * We assume that a delayed *error* completion is OK,
	 * though we still will complain noisily about it.
	 */
	if (unlikely(req->rq_state & RQ_LOCAL_ABORTED)) {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_emerg(DEV, "delayed completion of aborted local request; disk-timeout may be too aggressive\n");

		if (!error)
			panic("possible random memory corruption caused by delayed completion of aborted local request\n");
	}

	/* to avoid recursion in __req_mod */
	if (unlikely(error)) {
		what = (bio_data_dir(bio) == WRITE)
			? WRITE_COMPLETED_WITH_ERROR
			: (bio_rw(bio) == READ)
			  ? READ_COMPLETED_WITH_ERROR
			  : READ_AHEAD_COMPLETED_WITH_ERROR;
	} else
		what = COMPLETED_OK;

	bio_put(req->private_bio);
	req->private_bio = ERR_PTR(error);

	/* not req_mod(), we need irqsave here! */
	spin_lock_irqsave(&mdev->tconn->req_lock, flags);
	__req_mod(req, what, &m);
	spin_unlock_irqrestore(&mdev->tconn->req_lock, flags);
	put_ldev(mdev);

	if (m.bio)
		complete_master_bio(mdev, &m);
}

void drbd_csum_ee(struct drbd_conf *mdev, struct crypto_hash *tfm,
		  struct drbd_peer_request *peer_req, void *digest)
{
	struct hash_desc desc;
	struct scatterlist sg;
	struct page *page = peer_req->pages;
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
	len = peer_req->i.size & (PAGE_SIZE - 1);
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

	bio_for_each_segment(bvec, bio, i) {
		sg_set_page(&sg, bvec->bv_page, bvec->bv_len, bvec->bv_offset);
		crypto_hash_update(&desc, &sg, sg.length);
	}
	crypto_hash_final(&desc, digest);
}

/* MAYBE merge common code with w_e_end_ov_req */
static int w_e_send_csum(struct drbd_work *w, int cancel)
{
	struct drbd_peer_request *peer_req = container_of(w, struct drbd_peer_request, w);
	struct drbd_conf *mdev = w->mdev;
	int digest_size;
	void *digest;
	int err = 0;

	if (unlikely(cancel))
		goto out;

	if (unlikely((peer_req->flags & EE_WAS_ERROR) != 0))
		goto out;

	digest_size = crypto_hash_digestsize(mdev->tconn->csums_tfm);
	digest = kmalloc(digest_size, GFP_NOIO);
	if (digest) {
		sector_t sector = peer_req->i.sector;
		unsigned int size = peer_req->i.size;
		drbd_csum_ee(mdev, mdev->tconn->csums_tfm, peer_req, digest);
		/* Free peer_req and pages before send.
		 * In case we block on congestion, we could otherwise run into
		 * some distributed deadlock, if the other side blocks on
		 * congestion as well, because our receiver blocks in
		 * drbd_alloc_pages due to pp_in_use > max_buffers. */
		drbd_free_peer_req(mdev, peer_req);
		peer_req = NULL;
		inc_rs_pending(mdev);
		err = drbd_send_drequest_csum(mdev, sector, size,
					      digest, digest_size,
					      P_CSUM_RS_REQUEST);
		kfree(digest);
	} else {
		dev_err(DEV, "kmalloc() of digest failed.\n");
		err = -ENOMEM;
	}

out:
	if (peer_req)
		drbd_free_peer_req(mdev, peer_req);

	if (unlikely(err))
		dev_err(DEV, "drbd_send_drequest(..., csum) failed\n");
	return err;
}

#define GFP_TRY	(__GFP_HIGHMEM | __GFP_NOWARN)

static int read_for_csum(struct drbd_conf *mdev, sector_t sector, int size)
{
	struct drbd_peer_request *peer_req;

	if (!get_ldev(mdev))
		return -EIO;

	if (drbd_rs_should_slow_down(mdev, sector))
		goto defer;

	/* GFP_TRY, because if there is no memory available right now, this may
	 * be rescheduled for later. It is "only" background resync, after all. */
	peer_req = drbd_alloc_peer_req(mdev, ID_SYNCER /* unused */, sector,
				       size, GFP_TRY);
	if (!peer_req)
		goto defer;

	peer_req->w.cb = w_e_send_csum;
	spin_lock_irq(&mdev->tconn->req_lock);
	list_add(&peer_req->w.list, &mdev->read_ee);
	spin_unlock_irq(&mdev->tconn->req_lock);

	atomic_add(size >> 9, &mdev->rs_sect_ev);
	if (drbd_submit_peer_request(mdev, peer_req, READ, DRBD_FAULT_RS_RD) == 0)
		return 0;

	/* If it failed because of ENOMEM, retry should help.  If it failed
	 * because bio_add_page failed (probably broken lower level driver),
	 * retry may or may not help.
	 * If it does not, you may need to force disconnect. */
	spin_lock_irq(&mdev->tconn->req_lock);
	list_del(&peer_req->w.list);
	spin_unlock_irq(&mdev->tconn->req_lock);

	drbd_free_peer_req(mdev, peer_req);
defer:
	put_ldev(mdev);
	return -EAGAIN;
}

int w_resync_timer(struct drbd_work *w, int cancel)
{
	struct drbd_conf *mdev = w->mdev;
	switch (mdev->state.conn) {
	case C_VERIFY_S:
		w_make_ov_request(w, cancel);
		break;
	case C_SYNC_TARGET:
		w_make_resync_request(w, cancel);
		break;
	}

	return 0;
}

void resync_timer_fn(unsigned long data)
{
	struct drbd_conf *mdev = (struct drbd_conf *) data;

	if (list_empty(&mdev->resync_work.list))
		drbd_queue_work(&mdev->tconn->sender_work, &mdev->resync_work);
}

static void fifo_set(struct fifo_buffer *fb, int value)
{
	int i;

	for (i = 0; i < fb->size; i++)
		fb->values[i] = value;
}

static int fifo_push(struct fifo_buffer *fb, int value)
{
	int ov;

	ov = fb->values[fb->head_index];
	fb->values[fb->head_index++] = value;

	if (fb->head_index >= fb->size)
		fb->head_index = 0;

	return ov;
}

static void fifo_add_val(struct fifo_buffer *fb, int value)
{
	int i;

	for (i = 0; i < fb->size; i++)
		fb->values[i] += value;
}

struct fifo_buffer *fifo_alloc(int fifo_size)
{
	struct fifo_buffer *fb;

	fb = kzalloc(sizeof(struct fifo_buffer) + sizeof(int) * fifo_size, GFP_NOIO);
	if (!fb)
		return NULL;

	fb->head_index = 0;
	fb->size = fifo_size;
	fb->total = 0;

	return fb;
}

static int drbd_rs_controller(struct drbd_conf *mdev)
{
	struct disk_conf *dc;
	unsigned int sect_in;  /* Number of sectors that came in since the last turn */
	unsigned int want;     /* The number of sectors we want in the proxy */
	int req_sect; /* Number of sectors to request in this turn */
	int correction; /* Number of sectors more we need in the proxy*/
	int cps; /* correction per invocation of drbd_rs_controller() */
	int steps; /* Number of time steps to plan ahead */
	int curr_corr;
	int max_sect;
	struct fifo_buffer *plan;

	sect_in = atomic_xchg(&mdev->rs_sect_in, 0); /* Number of sectors that came in */
	mdev->rs_in_flight -= sect_in;

	dc = rcu_dereference(mdev->ldev->disk_conf);
	plan = rcu_dereference(mdev->rs_plan_s);

	steps = plan->size; /* (dc->c_plan_ahead * 10 * SLEEP_TIME) / HZ; */

	if (mdev->rs_in_flight + sect_in == 0) { /* At start of resync */
		want = ((dc->resync_rate * 2 * SLEEP_TIME) / HZ) * steps;
	} else { /* normal path */
		want = dc->c_fill_target ? dc->c_fill_target :
			sect_in * dc->c_delay_target * HZ / (SLEEP_TIME * 10);
	}

	correction = want - mdev->rs_in_flight - plan->total;

	/* Plan ahead */
	cps = correction / steps;
	fifo_add_val(plan, cps);
	plan->total += cps * steps;

	/* What we do in this step */
	curr_corr = fifo_push(plan, 0);
	plan->total -= curr_corr;

	req_sect = sect_in + curr_corr;
	if (req_sect < 0)
		req_sect = 0;

	max_sect = (dc->c_max_rate * 2 * SLEEP_TIME) / HZ;
	if (req_sect > max_sect)
		req_sect = max_sect;

	/*
	dev_warn(DEV, "si=%u if=%d wa=%u co=%d st=%d cps=%d pl=%d cc=%d rs=%d\n",
		 sect_in, mdev->rs_in_flight, want, correction,
		 steps, cps, mdev->rs_planed, curr_corr, req_sect);
	*/

	return req_sect;
}

static int drbd_rs_number_requests(struct drbd_conf *mdev)
{
	int number;

	rcu_read_lock();
	if (rcu_dereference(mdev->rs_plan_s)->size) {
		number = drbd_rs_controller(mdev) >> (BM_BLOCK_SHIFT - 9);
		mdev->c_sync_rate = number * HZ * (BM_BLOCK_SIZE / 1024) / SLEEP_TIME;
	} else {
		mdev->c_sync_rate = rcu_dereference(mdev->ldev->disk_conf)->resync_rate;
		number = SLEEP_TIME * mdev->c_sync_rate  / ((BM_BLOCK_SIZE / 1024) * HZ);
	}
	rcu_read_unlock();

	/* ignore the amount of pending requests, the resync controller should
	 * throttle down to incoming reply rate soon enough anyways. */
	return number;
}

int w_make_resync_request(struct drbd_work *w, int cancel)
{
	struct drbd_conf *mdev = w->mdev;
	unsigned long bit;
	sector_t sector;
	const sector_t capacity = drbd_get_capacity(mdev->this_bdev);
	int max_bio_size;
	int number, rollback_i, size;
	int align, queued, sndbuf;
	int i = 0;

	if (unlikely(cancel))
		return 0;

	if (mdev->rs_total == 0) {
		/* empty resync? */
		drbd_resync_finished(mdev);
		return 0;
	}

	if (!get_ldev(mdev)) {
		/* Since we only need to access mdev->rsync a
		   get_ldev_if_state(mdev,D_FAILED) would be sufficient, but
		   to continue resync with a broken disk makes no sense at
		   all */
		dev_err(DEV, "Disk broke down during resync!\n");
		return 0;
	}

	max_bio_size = queue_max_hw_sectors(mdev->rq_queue) << 9;
	number = drbd_rs_number_requests(mdev);
	if (number == 0)
		goto requeue;

	for (i = 0; i < number; i++) {
		/* Stop generating RS requests, when half of the send buffer is filled */
		mutex_lock(&mdev->tconn->data.mutex);
		if (mdev->tconn->data.socket) {
			queued = mdev->tconn->data.socket->sk->sk_wmem_queued;
			sndbuf = mdev->tconn->data.socket->sk->sk_sndbuf;
		} else {
			queued = 1;
			sndbuf = 0;
		}
		mutex_unlock(&mdev->tconn->data.mutex);
		if (queued > sndbuf / 2)
			goto requeue;

next_sector:
		size = BM_BLOCK_SIZE;
		bit  = drbd_bm_find_next(mdev, mdev->bm_resync_fo);

		if (bit == DRBD_END_OF_BITMAP) {
			mdev->bm_resync_fo = drbd_bm_bits(mdev);
			put_ldev(mdev);
			return 0;
		}

		sector = BM_BIT_TO_SECT(bit);

		if (drbd_rs_should_slow_down(mdev, sector) ||
		    drbd_try_rs_begin_io(mdev, sector)) {
			mdev->bm_resync_fo = bit;
			goto requeue;
		}
		mdev->bm_resync_fo = bit + 1;

		if (unlikely(drbd_bm_test_bit(mdev, bit) == 0)) {
			drbd_rs_complete_io(mdev, sector);
			goto next_sector;
		}

#if DRBD_MAX_BIO_SIZE > BM_BLOCK_SIZE
		/* try to find some adjacent bits.
		 * we stop if we have already the maximum req size.
		 *
		 * Additionally always align bigger requests, in order to
		 * be prepared for all stripe sizes of software RAIDs.
		 */
		align = 1;
		rollback_i = i;
		for (;;) {
			if (size + BM_BLOCK_SIZE > max_bio_size)
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
		if (mdev->tconn->agreed_pro_version >= 89 && mdev->tconn->csums_tfm) {
			switch (read_for_csum(mdev, sector, size)) {
			case -EIO: /* Disk failure */
				put_ldev(mdev);
				return -EIO;
			case -EAGAIN: /* allocation failed, or ldev busy */
				drbd_rs_complete_io(mdev, sector);
				mdev->bm_resync_fo = BM_SECT_TO_BIT(sector);
				i = rollback_i;
				goto requeue;
			case 0:
				/* everything ok */
				break;
			default:
				BUG();
			}
		} else {
			int err;

			inc_rs_pending(mdev);
			err = drbd_send_drequest(mdev, P_RS_DATA_REQUEST,
						 sector, size, ID_SYNCER);
			if (err) {
				dev_err(DEV, "drbd_send_drequest() failed, aborting...\n");
				dec_rs_pending(mdev);
				put_ldev(mdev);
				return err;
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
		put_ldev(mdev);
		return 0;
	}

 requeue:
	mdev->rs_in_flight += (i << (BM_BLOCK_SHIFT - 9));
	mod_timer(&mdev->resync_timer, jiffies + SLEEP_TIME);
	put_ldev(mdev);
	return 0;
}

static int w_make_ov_request(struct drbd_work *w, int cancel)
{
	struct drbd_conf *mdev = w->mdev;
	int number, i, size;
	sector_t sector;
	const sector_t capacity = drbd_get_capacity(mdev->this_bdev);
	bool stop_sector_reached = false;

	if (unlikely(cancel))
		return 1;

	number = drbd_rs_number_requests(mdev);

	sector = mdev->ov_position;
	for (i = 0; i < number; i++) {
		if (sector >= capacity)
			return 1;

		/* We check for "finished" only in the reply path:
		 * w_e_end_ov_reply().
		 * We need to send at least one request out. */
		stop_sector_reached = i > 0
			&& verify_can_do_stop_sector(mdev)
			&& sector >= mdev->ov_stop_sector;
		if (stop_sector_reached)
			break;

		size = BM_BLOCK_SIZE;

		if (drbd_rs_should_slow_down(mdev, sector) ||
		    drbd_try_rs_begin_io(mdev, sector)) {
			mdev->ov_position = sector;
			goto requeue;
		}

		if (sector + (size>>9) > capacity)
			size = (capacity-sector)<<9;

		inc_rs_pending(mdev);
		if (drbd_send_ov_request(mdev, sector, size)) {
			dec_rs_pending(mdev);
			return 0;
		}
		sector += BM_SECT_PER_BIT;
	}
	mdev->ov_position = sector;

 requeue:
	mdev->rs_in_flight += (i << (BM_BLOCK_SHIFT - 9));
	if (i == 0 || !stop_sector_reached)
		mod_timer(&mdev->resync_timer, jiffies + SLEEP_TIME);
	return 1;
}

int w_ov_finished(struct drbd_work *w, int cancel)
{
	struct drbd_conf *mdev = w->mdev;
	kfree(w);
	ov_out_of_sync_print(mdev);
	drbd_resync_finished(mdev);

	return 0;
}

static int w_resync_finished(struct drbd_work *w, int cancel)
{
	struct drbd_conf *mdev = w->mdev;
	kfree(w);

	drbd_resync_finished(mdev);

	return 0;
}

static void ping_peer(struct drbd_conf *mdev)
{
	struct drbd_tconn *tconn = mdev->tconn;

	clear_bit(GOT_PING_ACK, &tconn->flags);
	request_ping(tconn);
	wait_event(tconn->ping_wait,
		   test_bit(GOT_PING_ACK, &tconn->flags) || mdev->state.conn < C_CONNECTED);
}

int drbd_resync_finished(struct drbd_conf *mdev)
{
	unsigned long db, dt, dbdt;
	unsigned long n_oos;
	union drbd_state os, ns;
	struct drbd_work *w;
	char *khelper_cmd = NULL;
	int verify_done = 0;

	/* Remove all elements from the resync LRU. Since future actions
	 * might set bits in the (main) bitmap, then the entries in the
	 * resync LRU would be wrong. */
	if (drbd_rs_del_all(mdev)) {
		/* In case this is not possible now, most probably because
		 * there are P_RS_DATA_REPLY Packets lingering on the worker's
		 * queue (or even the read operations for those packets
		 * is not finished by now).   Retry in 100ms. */

		schedule_timeout_interruptible(HZ / 10);
		w = kmalloc(sizeof(struct drbd_work), GFP_ATOMIC);
		if (w) {
			w->cb = w_resync_finished;
			w->mdev = mdev;
			drbd_queue_work(&mdev->tconn->sender_work, w);
			return 1;
		}
		dev_err(DEV, "Warn failed to drbd_rs_del_all() and to kmalloc(w).\n");
	}

	dt = (jiffies - mdev->rs_start - mdev->rs_paused) / HZ;
	if (dt <= 0)
		dt = 1;
	
	db = mdev->rs_total;
	/* adjust for verify start and stop sectors, respective reached position */
	if (mdev->state.conn == C_VERIFY_S || mdev->state.conn == C_VERIFY_T)
		db -= mdev->ov_left;

	dbdt = Bit2KB(db/dt);
	mdev->rs_paused /= HZ;

	if (!get_ldev(mdev))
		goto out;

	ping_peer(mdev);

	spin_lock_irq(&mdev->tconn->req_lock);
	os = drbd_read_state(mdev);

	verify_done = (os.conn == C_VERIFY_S || os.conn == C_VERIFY_T);

	/* This protects us against multiple calls (that can happen in the presence
	   of application IO), and against connectivity loss just before we arrive here. */
	if (os.conn <= C_CONNECTED)
		goto out_unlock;

	ns = os;
	ns.conn = C_CONNECTED;

	dev_info(DEV, "%s done (total %lu sec; paused %lu sec; %lu K/sec)\n",
	     verify_done ? "Online verify" : "Resync",
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

		if (mdev->tconn->csums_tfm && mdev->rs_total) {
			const unsigned long s = mdev->rs_same_csum;
			const unsigned long t = mdev->rs_total;
			const int ratio =
				(t == 0)     ? 0 :
			(t < 100000) ? ((s*100)/t) : (s/(t/100));
			dev_info(DEV, "%u %% had equal checksums, eliminated: %luK; "
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

		if (!(os.conn == C_VERIFY_S || os.conn == C_VERIFY_T)) {
			/* for verify runs, we don't update uuids here,
			 * so there would be nothing to report. */
			drbd_uuid_set_bm(mdev, 0UL);
			drbd_print_uuids(mdev, "updated UUIDs");
			if (mdev->p_uuid) {
				/* Now the two UUID sets are equal, update what we
				 * know of the peer. */
				int i;
				for (i = UI_CURRENT ; i <= UI_HISTORY_END ; i++)
					mdev->p_uuid[i] = mdev->ldev->md.uuid[i];
			}
		}
	}

	_drbd_set_state(mdev, ns, CS_VERBOSE, NULL);
out_unlock:
	spin_unlock_irq(&mdev->tconn->req_lock);
	put_ldev(mdev);
out:
	mdev->rs_total  = 0;
	mdev->rs_failed = 0;
	mdev->rs_paused = 0;

	/* reset start sector, if we reached end of device */
	if (verify_done && mdev->ov_left == 0)
		mdev->ov_start_sector = 0;

	drbd_md_sync(mdev);

	if (khelper_cmd)
		drbd_khelper(mdev, khelper_cmd);

	return 1;
}

/* helper */
static void move_to_net_ee_or_free(struct drbd_conf *mdev, struct drbd_peer_request *peer_req)
{
	if (drbd_peer_req_has_active_page(peer_req)) {
		/* This might happen if sendpage() has not finished */
		int i = (peer_req->i.size + PAGE_SIZE -1) >> PAGE_SHIFT;
		atomic_add(i, &mdev->pp_in_use_by_net);
		atomic_sub(i, &mdev->pp_in_use);
		spin_lock_irq(&mdev->tconn->req_lock);
		list_add_tail(&peer_req->w.list, &mdev->net_ee);
		spin_unlock_irq(&mdev->tconn->req_lock);
		wake_up(&drbd_pp_wait);
	} else
		drbd_free_peer_req(mdev, peer_req);
}

/**
 * w_e_end_data_req() - Worker callback, to send a P_DATA_REPLY packet in response to a P_DATA_REQUEST
 * @mdev:	DRBD device.
 * @w:		work object.
 * @cancel:	The connection will be closed anyways
 */
int w_e_end_data_req(struct drbd_work *w, int cancel)
{
	struct drbd_peer_request *peer_req = container_of(w, struct drbd_peer_request, w);
	struct drbd_conf *mdev = w->mdev;
	int err;

	if (unlikely(cancel)) {
		drbd_free_peer_req(mdev, peer_req);
		dec_unacked(mdev);
		return 0;
	}

	if (likely((peer_req->flags & EE_WAS_ERROR) == 0)) {
		err = drbd_send_block(mdev, P_DATA_REPLY, peer_req);
	} else {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "Sending NegDReply. sector=%llus.\n",
			    (unsigned long long)peer_req->i.sector);

		err = drbd_send_ack(mdev, P_NEG_DREPLY, peer_req);
	}

	dec_unacked(mdev);

	move_to_net_ee_or_free(mdev, peer_req);

	if (unlikely(err))
		dev_err(DEV, "drbd_send_block() failed\n");
	return err;
}

/**
 * w_e_end_rsdata_req() - Worker callback to send a P_RS_DATA_REPLY packet in response to a P_RS_DATA_REQUEST
 * @mdev:	DRBD device.
 * @w:		work object.
 * @cancel:	The connection will be closed anyways
 */
int w_e_end_rsdata_req(struct drbd_work *w, int cancel)
{
	struct drbd_peer_request *peer_req = container_of(w, struct drbd_peer_request, w);
	struct drbd_conf *mdev = w->mdev;
	int err;

	if (unlikely(cancel)) {
		drbd_free_peer_req(mdev, peer_req);
		dec_unacked(mdev);
		return 0;
	}

	if (get_ldev_if_state(mdev, D_FAILED)) {
		drbd_rs_complete_io(mdev, peer_req->i.sector);
		put_ldev(mdev);
	}

	if (mdev->state.conn == C_AHEAD) {
		err = drbd_send_ack(mdev, P_RS_CANCEL, peer_req);
	} else if (likely((peer_req->flags & EE_WAS_ERROR) == 0)) {
		if (likely(mdev->state.pdsk >= D_INCONSISTENT)) {
			inc_rs_pending(mdev);
			err = drbd_send_block(mdev, P_RS_DATA_REPLY, peer_req);
		} else {
			if (__ratelimit(&drbd_ratelimit_state))
				dev_err(DEV, "Not sending RSDataReply, "
				    "partner DISKLESS!\n");
			err = 0;
		}
	} else {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "Sending NegRSDReply. sector %llus.\n",
			    (unsigned long long)peer_req->i.sector);

		err = drbd_send_ack(mdev, P_NEG_RS_DREPLY, peer_req);

		/* update resync data with failure */
		drbd_rs_failed_io(mdev, peer_req->i.sector, peer_req->i.size);
	}

	dec_unacked(mdev);

	move_to_net_ee_or_free(mdev, peer_req);

	if (unlikely(err))
		dev_err(DEV, "drbd_send_block() failed\n");
	return err;
}

int w_e_end_csum_rs_req(struct drbd_work *w, int cancel)
{
	struct drbd_peer_request *peer_req = container_of(w, struct drbd_peer_request, w);
	struct drbd_conf *mdev = w->mdev;
	struct digest_info *di;
	int digest_size;
	void *digest = NULL;
	int err, eq = 0;

	if (unlikely(cancel)) {
		drbd_free_peer_req(mdev, peer_req);
		dec_unacked(mdev);
		return 0;
	}

	if (get_ldev(mdev)) {
		drbd_rs_complete_io(mdev, peer_req->i.sector);
		put_ldev(mdev);
	}

	di = peer_req->digest;

	if (likely((peer_req->flags & EE_WAS_ERROR) == 0)) {
		/* quick hack to try to avoid a race against reconfiguration.
		 * a real fix would be much more involved,
		 * introducing more locking mechanisms */
		if (mdev->tconn->csums_tfm) {
			digest_size = crypto_hash_digestsize(mdev->tconn->csums_tfm);
			D_ASSERT(digest_size == di->digest_size);
			digest = kmalloc(digest_size, GFP_NOIO);
		}
		if (digest) {
			drbd_csum_ee(mdev, mdev->tconn->csums_tfm, peer_req, digest);
			eq = !memcmp(digest, di->digest, digest_size);
			kfree(digest);
		}

		if (eq) {
			drbd_set_in_sync(mdev, peer_req->i.sector, peer_req->i.size);
			/* rs_same_csums unit is BM_BLOCK_SIZE */
			mdev->rs_same_csum += peer_req->i.size >> BM_BLOCK_SHIFT;
			err = drbd_send_ack(mdev, P_RS_IS_IN_SYNC, peer_req);
		} else {
			inc_rs_pending(mdev);
			peer_req->block_id = ID_SYNCER; /* By setting block_id, digest pointer becomes invalid! */
			peer_req->flags &= ~EE_HAS_DIGEST; /* This peer request no longer has a digest pointer */
			kfree(di);
			err = drbd_send_block(mdev, P_RS_DATA_REPLY, peer_req);
		}
	} else {
		err = drbd_send_ack(mdev, P_NEG_RS_DREPLY, peer_req);
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "Sending NegDReply. I guess it gets messy.\n");
	}

	dec_unacked(mdev);
	move_to_net_ee_or_free(mdev, peer_req);

	if (unlikely(err))
		dev_err(DEV, "drbd_send_block/ack() failed\n");
	return err;
}

int w_e_end_ov_req(struct drbd_work *w, int cancel)
{
	struct drbd_peer_request *peer_req = container_of(w, struct drbd_peer_request, w);
	struct drbd_conf *mdev = w->mdev;
	sector_t sector = peer_req->i.sector;
	unsigned int size = peer_req->i.size;
	int digest_size;
	void *digest;
	int err = 0;

	if (unlikely(cancel))
		goto out;

	digest_size = crypto_hash_digestsize(mdev->tconn->verify_tfm);
	digest = kmalloc(digest_size, GFP_NOIO);
	if (!digest) {
		err = 1;	/* terminate the connection in case the allocation failed */
		goto out;
	}

	if (likely(!(peer_req->flags & EE_WAS_ERROR)))
		drbd_csum_ee(mdev, mdev->tconn->verify_tfm, peer_req, digest);
	else
		memset(digest, 0, digest_size);

	/* Free e and pages before send.
	 * In case we block on congestion, we could otherwise run into
	 * some distributed deadlock, if the other side blocks on
	 * congestion as well, because our receiver blocks in
	 * drbd_alloc_pages due to pp_in_use > max_buffers. */
	drbd_free_peer_req(mdev, peer_req);
	peer_req = NULL;
	inc_rs_pending(mdev);
	err = drbd_send_drequest_csum(mdev, sector, size, digest, digest_size, P_OV_REPLY);
	if (err)
		dec_rs_pending(mdev);
	kfree(digest);

out:
	if (peer_req)
		drbd_free_peer_req(mdev, peer_req);
	dec_unacked(mdev);
	return err;
}

void drbd_ov_out_of_sync_found(struct drbd_conf *mdev, sector_t sector, int size)
{
	if (mdev->ov_last_oos_start + mdev->ov_last_oos_size == sector) {
		mdev->ov_last_oos_size += size>>9;
	} else {
		mdev->ov_last_oos_start = sector;
		mdev->ov_last_oos_size = size>>9;
	}
	drbd_set_out_of_sync(mdev, sector, size);
}

int w_e_end_ov_reply(struct drbd_work *w, int cancel)
{
	struct drbd_peer_request *peer_req = container_of(w, struct drbd_peer_request, w);
	struct drbd_conf *mdev = w->mdev;
	struct digest_info *di;
	void *digest;
	sector_t sector = peer_req->i.sector;
	unsigned int size = peer_req->i.size;
	int digest_size;
	int err, eq = 0;
	bool stop_sector_reached = false;

	if (unlikely(cancel)) {
		drbd_free_peer_req(mdev, peer_req);
		dec_unacked(mdev);
		return 0;
	}

	/* after "cancel", because after drbd_disconnect/drbd_rs_cancel_all
	 * the resync lru has been cleaned up already */
	if (get_ldev(mdev)) {
		drbd_rs_complete_io(mdev, peer_req->i.sector);
		put_ldev(mdev);
	}

	di = peer_req->digest;

	if (likely((peer_req->flags & EE_WAS_ERROR) == 0)) {
		digest_size = crypto_hash_digestsize(mdev->tconn->verify_tfm);
		digest = kmalloc(digest_size, GFP_NOIO);
		if (digest) {
			drbd_csum_ee(mdev, mdev->tconn->verify_tfm, peer_req, digest);

			D_ASSERT(digest_size == di->digest_size);
			eq = !memcmp(digest, di->digest, digest_size);
			kfree(digest);
		}
	}

	/* Free peer_req and pages before send.
	 * In case we block on congestion, we could otherwise run into
	 * some distributed deadlock, if the other side blocks on
	 * congestion as well, because our receiver blocks in
	 * drbd_alloc_pages due to pp_in_use > max_buffers. */
	drbd_free_peer_req(mdev, peer_req);
	if (!eq)
		drbd_ov_out_of_sync_found(mdev, sector, size);
	else
		ov_out_of_sync_print(mdev);

	err = drbd_send_ack_ex(mdev, P_OV_RESULT, sector, size,
			       eq ? ID_IN_SYNC : ID_OUT_OF_SYNC);

	dec_unacked(mdev);

	--mdev->ov_left;

	/* let's advance progress step marks only for every other megabyte */
	if ((mdev->ov_left & 0x200) == 0x200)
		drbd_advance_rs_marks(mdev, mdev->ov_left);

	stop_sector_reached = verify_can_do_stop_sector(mdev) &&
		(sector + (size>>9)) >= mdev->ov_stop_sector;

	if (mdev->ov_left == 0 || stop_sector_reached) {
		ov_out_of_sync_print(mdev);
		drbd_resync_finished(mdev);
	}

	return err;
}

int w_prev_work_done(struct drbd_work *w, int cancel)
{
	struct drbd_wq_barrier *b = container_of(w, struct drbd_wq_barrier, w);

	complete(&b->done);
	return 0;
}

/* FIXME
 * We need to track the number of pending barrier acks,
 * and to be able to wait for them.
 * See also comment in drbd_adm_attach before drbd_suspend_io.
 */
int drbd_send_barrier(struct drbd_tconn *tconn)
{
	struct p_barrier *p;
	struct drbd_socket *sock;

	sock = &tconn->data;
	p = conn_prepare_command(tconn, sock);
	if (!p)
		return -EIO;
	p->barrier = tconn->send.current_epoch_nr;
	p->pad = 0;
	tconn->send.current_epoch_writes = 0;

	return conn_send_command(tconn, sock, P_BARRIER, sizeof(*p), NULL, 0);
}

int w_send_write_hint(struct drbd_work *w, int cancel)
{
	struct drbd_conf *mdev = w->mdev;
	struct drbd_socket *sock;

	if (cancel)
		return 0;
	sock = &mdev->tconn->data;
	if (!drbd_prepare_command(mdev, sock))
		return -EIO;
	return drbd_send_command(mdev, sock, P_UNPLUG_REMOTE, 0, NULL, 0);
}

static void re_init_if_first_write(struct drbd_tconn *tconn, unsigned int epoch)
{
	if (!tconn->send.seen_any_write_yet) {
		tconn->send.seen_any_write_yet = true;
		tconn->send.current_epoch_nr = epoch;
		tconn->send.current_epoch_writes = 0;
	}
}

static void maybe_send_barrier(struct drbd_tconn *tconn, unsigned int epoch)
{
	/* re-init if first write on this connection */
	if (!tconn->send.seen_any_write_yet)
		return;
	if (tconn->send.current_epoch_nr != epoch) {
		if (tconn->send.current_epoch_writes)
			drbd_send_barrier(tconn);
		tconn->send.current_epoch_nr = epoch;
	}
}

int w_send_out_of_sync(struct drbd_work *w, int cancel)
{
	struct drbd_request *req = container_of(w, struct drbd_request, w);
	struct drbd_conf *mdev = w->mdev;
	struct drbd_tconn *tconn = mdev->tconn;
	int err;

	if (unlikely(cancel)) {
		req_mod(req, SEND_CANCELED);
		return 0;
	}

	/* this time, no tconn->send.current_epoch_writes++;
	 * If it was sent, it was the closing barrier for the last
	 * replicated epoch, before we went into AHEAD mode.
	 * No more barriers will be sent, until we leave AHEAD mode again. */
	maybe_send_barrier(tconn, req->epoch);

	err = drbd_send_out_of_sync(mdev, req);
	req_mod(req, OOS_HANDED_TO_NETWORK);

	return err;
}

/**
 * w_send_dblock() - Worker callback to send a P_DATA packet in order to mirror a write request
 * @mdev:	DRBD device.
 * @w:		work object.
 * @cancel:	The connection will be closed anyways
 */
int w_send_dblock(struct drbd_work *w, int cancel)
{
	struct drbd_request *req = container_of(w, struct drbd_request, w);
	struct drbd_conf *mdev = w->mdev;
	struct drbd_tconn *tconn = mdev->tconn;
	int err;

	if (unlikely(cancel)) {
		req_mod(req, SEND_CANCELED);
		return 0;
	}

	re_init_if_first_write(tconn, req->epoch);
	maybe_send_barrier(tconn, req->epoch);
	tconn->send.current_epoch_writes++;

	err = drbd_send_dblock(mdev, req);
	req_mod(req, err ? SEND_FAILED : HANDED_OVER_TO_NETWORK);

	return err;
}

/**
 * w_send_read_req() - Worker callback to send a read request (P_DATA_REQUEST) packet
 * @mdev:	DRBD device.
 * @w:		work object.
 * @cancel:	The connection will be closed anyways
 */
int w_send_read_req(struct drbd_work *w, int cancel)
{
	struct drbd_request *req = container_of(w, struct drbd_request, w);
	struct drbd_conf *mdev = w->mdev;
	struct drbd_tconn *tconn = mdev->tconn;
	int err;

	if (unlikely(cancel)) {
		req_mod(req, SEND_CANCELED);
		return 0;
	}

	/* Even read requests may close a write epoch,
	 * if there was any yet. */
	maybe_send_barrier(tconn, req->epoch);

	err = drbd_send_drequest(mdev, P_DATA_REQUEST, req->i.sector, req->i.size,
				 (unsigned long)req);

	req_mod(req, err ? SEND_FAILED : HANDED_OVER_TO_NETWORK);

	return err;
}

int w_restart_disk_io(struct drbd_work *w, int cancel)
{
	struct drbd_request *req = container_of(w, struct drbd_request, w);
	struct drbd_conf *mdev = w->mdev;

	if (bio_data_dir(req->master_bio) == WRITE && req->rq_state & RQ_IN_ACT_LOG)
		drbd_al_begin_io(mdev, &req->i);

	drbd_req_make_private_bio(req, req->master_bio);
	req->private_bio->bi_bdev = mdev->ldev->backing_bdev;
	generic_make_request(req->private_bio);

	return 0;
}

static int _drbd_may_sync_now(struct drbd_conf *mdev)
{
	struct drbd_conf *odev = mdev;
	int resync_after;

	while (1) {
		if (!odev->ldev)
			return 1;
		rcu_read_lock();
		resync_after = rcu_dereference(odev->ldev->disk_conf)->resync_after;
		rcu_read_unlock();
		if (resync_after == -1)
			return 1;
		odev = minor_to_mdev(resync_after);
		if (!expect(odev))
			return 1;
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

	rcu_read_lock();
	idr_for_each_entry(&minors, odev, i) {
		if (odev->state.conn == C_STANDALONE && odev->state.disk == D_DISKLESS)
			continue;
		if (!_drbd_may_sync_now(odev))
			rv |= (__drbd_set_state(_NS(odev, aftr_isp, 1), CS_HARD, NULL)
			       != SS_NOTHING_TO_DO);
	}
	rcu_read_unlock();

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

	rcu_read_lock();
	idr_for_each_entry(&minors, odev, i) {
		if (odev->state.conn == C_STANDALONE && odev->state.disk == D_DISKLESS)
			continue;
		if (odev->state.aftr_isp) {
			if (_drbd_may_sync_now(odev))
				rv |= (__drbd_set_state(_NS(odev, aftr_isp, 0),
							CS_HARD, NULL)
				       != SS_NOTHING_TO_DO) ;
		}
	}
	rcu_read_unlock();
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

/* caller must hold global_state_lock */
enum drbd_ret_code drbd_resync_after_valid(struct drbd_conf *mdev, int o_minor)
{
	struct drbd_conf *odev;
	int resync_after;

	if (o_minor == -1)
		return NO_ERROR;
	if (o_minor < -1 || minor_to_mdev(o_minor) == NULL)
		return ERR_RESYNC_AFTER;

	/* check for loops */
	odev = minor_to_mdev(o_minor);
	while (1) {
		if (odev == mdev)
			return ERR_RESYNC_AFTER_CYCLE;

		rcu_read_lock();
		resync_after = rcu_dereference(odev->ldev->disk_conf)->resync_after;
		rcu_read_unlock();
		/* dependency chain ends here, no cycles. */
		if (resync_after == -1)
			return NO_ERROR;

		/* follow the dependency chain */
		odev = minor_to_mdev(resync_after);
	}
}

/* caller must hold global_state_lock */
void drbd_resync_after_changed(struct drbd_conf *mdev)
{
	int changes;

	do {
		changes  = _drbd_pause_after(mdev);
		changes |= _drbd_resume_next(mdev);
	} while (changes);
}

void drbd_rs_controller_reset(struct drbd_conf *mdev)
{
	struct fifo_buffer *plan;

	atomic_set(&mdev->rs_sect_in, 0);
	atomic_set(&mdev->rs_sect_ev, 0);
	mdev->rs_in_flight = 0;

	/* Updating the RCU protected object in place is necessary since
	   this function gets called from atomic context.
	   It is valid since all other updates also lead to an completely
	   empty fifo */
	rcu_read_lock();
	plan = rcu_dereference(mdev->rs_plan_s);
	plan->total = 0;
	fifo_set(plan, 0);
	rcu_read_unlock();
}

void start_resync_timer_fn(unsigned long data)
{
	struct drbd_conf *mdev = (struct drbd_conf *) data;

	drbd_queue_work(&mdev->tconn->sender_work, &mdev->start_resync_work);
}

int w_start_resync(struct drbd_work *w, int cancel)
{
	struct drbd_conf *mdev = w->mdev;

	if (atomic_read(&mdev->unacked_cnt) || atomic_read(&mdev->rs_pending_cnt)) {
		dev_warn(DEV, "w_start_resync later...\n");
		mdev->start_resync_timer.expires = jiffies + HZ/10;
		add_timer(&mdev->start_resync_timer);
		return 0;
	}

	drbd_start_resync(mdev, C_SYNC_SOURCE);
	clear_bit(AHEAD_TO_SYNC_SOURCE, &mdev->flags);
	return 0;
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

	if (mdev->state.conn >= C_SYNC_SOURCE && mdev->state.conn < C_AHEAD) {
		dev_err(DEV, "Resync already running!\n");
		return;
	}

	if (!test_bit(B_RS_H_DONE, &mdev->flags)) {
		if (side == C_SYNC_TARGET) {
			/* Since application IO was locked out during C_WF_BITMAP_T and
			   C_WF_SYNC_UUID we are still unmodified. Before going to C_SYNC_TARGET
			   we check that we might make the data inconsistent. */
			r = drbd_khelper(mdev, "before-resync-target");
			r = (r >> 8) & 0xff;
			if (r > 0) {
				dev_info(DEV, "before-resync-target handler returned %d, "
					 "dropping connection.\n", r);
				conn_request_state(mdev->tconn, NS(conn, C_DISCONNECTING), CS_HARD);
				return;
			}
		} else /* C_SYNC_SOURCE */ {
			r = drbd_khelper(mdev, "before-resync-source");
			r = (r >> 8) & 0xff;
			if (r > 0) {
				if (r == 3) {
					dev_info(DEV, "before-resync-source handler returned %d, "
						 "ignoring. Old userland tools?", r);
				} else {
					dev_info(DEV, "before-resync-source handler returned %d, "
						 "dropping connection.\n", r);
					conn_request_state(mdev->tconn, NS(conn, C_DISCONNECTING), CS_HARD);
					return;
				}
			}
		}
	}

	if (current == mdev->tconn->worker.task) {
		/* The worker should not sleep waiting for state_mutex,
		   that can take long */
		if (!mutex_trylock(mdev->state_mutex)) {
			set_bit(B_RS_H_DONE, &mdev->flags);
			mdev->start_resync_timer.expires = jiffies + HZ/5;
			add_timer(&mdev->start_resync_timer);
			return;
		}
	} else {
		mutex_lock(mdev->state_mutex);
	}
	clear_bit(B_RS_H_DONE, &mdev->flags);

	write_lock_irq(&global_state_lock);
	if (!get_ldev_if_state(mdev, D_NEGOTIATING)) {
		write_unlock_irq(&global_state_lock);
		mutex_unlock(mdev->state_mutex);
		return;
	}

	ns = drbd_read_state(mdev);

	ns.aftr_isp = !_drbd_may_sync_now(mdev);

	ns.conn = side;

	if (side == C_SYNC_TARGET)
		ns.disk = D_INCONSISTENT;
	else /* side == C_SYNC_SOURCE */
		ns.pdsk = D_INCONSISTENT;

	r = __drbd_set_state(mdev, ns, CS_VERBOSE, NULL);
	ns = drbd_read_state(mdev);

	if (ns.conn < C_CONNECTED)
		r = SS_UNKNOWN_ERROR;

	if (r == SS_SUCCESS) {
		unsigned long tw = drbd_bm_total_weight(mdev);
		unsigned long now = jiffies;
		int i;

		mdev->rs_failed    = 0;
		mdev->rs_paused    = 0;
		mdev->rs_same_csum = 0;
		mdev->rs_last_events = 0;
		mdev->rs_last_sect_ev = 0;
		mdev->rs_total     = tw;
		mdev->rs_start     = now;
		for (i = 0; i < DRBD_SYNC_MARKS; i++) {
			mdev->rs_mark_left[i] = tw;
			mdev->rs_mark_time[i] = now;
		}
		_drbd_pause_after(mdev);
	}
	write_unlock_irq(&global_state_lock);

	if (r == SS_SUCCESS) {
		dev_info(DEV, "Began resync as %s (will sync %lu KB [%lu bits set]).\n",
		     drbd_conn_str(ns.conn),
		     (unsigned long) mdev->rs_total << (BM_BLOCK_SHIFT-10),
		     (unsigned long) mdev->rs_total);
		if (side == C_SYNC_TARGET)
			mdev->bm_resync_fo = 0;

		/* Since protocol 96, we must serialize drbd_gen_and_send_sync_uuid
		 * with w_send_oos, or the sync target will get confused as to
		 * how much bits to resync.  We cannot do that always, because for an
		 * empty resync and protocol < 95, we need to do it here, as we call
		 * drbd_resync_finished from here in that case.
		 * We drbd_gen_and_send_sync_uuid here for protocol < 96,
		 * and from after_state_ch otherwise. */
		if (side == C_SYNC_SOURCE && mdev->tconn->agreed_pro_version < 96)
			drbd_gen_and_send_sync_uuid(mdev);

		if (mdev->tconn->agreed_pro_version < 95 && mdev->rs_total == 0) {
			/* This still has a race (about when exactly the peers
			 * detect connection loss) that can lead to a full sync
			 * on next handshake. In 8.3.9 we fixed this with explicit
			 * resync-finished notifications, but the fix
			 * introduces a protocol change.  Sleeping for some
			 * time longer than the ping interval + timeout on the
			 * SyncSource, to give the SyncTarget the chance to
			 * detect connection loss, then waiting for a ping
			 * response (implicit in drbd_resync_finished) reduces
			 * the race considerably, but does not solve it. */
			if (side == C_SYNC_SOURCE) {
				struct net_conf *nc;
				int timeo;

				rcu_read_lock();
				nc = rcu_dereference(mdev->tconn->net_conf);
				timeo = nc->ping_int * HZ + nc->ping_timeo * HZ / 9;
				rcu_read_unlock();
				schedule_timeout_interruptible(timeo);
			}
			drbd_resync_finished(mdev);
		}

		drbd_rs_controller_reset(mdev);
		/* ns.conn may already be != mdev->state.conn,
		 * we may have been paused in between, or become paused until
		 * the timer triggers.
		 * No matter, that is handled in resync_timer_fn() */
		if (ns.conn == C_SYNC_TARGET)
			mod_timer(&mdev->resync_timer, jiffies);

		drbd_md_sync(mdev);
	}
	put_ldev(mdev);
	mutex_unlock(mdev->state_mutex);
}

/* If the resource already closed the current epoch, but we did not
 * (because we have not yet seen new requests), we should send the
 * corresponding barrier now.  Must be checked within the same spinlock
 * that is used to check for new requests. */
bool need_to_send_barrier(struct drbd_tconn *connection)
{
	if (!connection->send.seen_any_write_yet)
		return false;

	/* Skip barriers that do not contain any writes.
	 * This may happen during AHEAD mode. */
	if (!connection->send.current_epoch_writes)
		return false;

	/* ->req_lock is held when requests are queued on
	 * connection->sender_work, and put into ->transfer_log.
	 * It is also held when ->current_tle_nr is increased.
	 * So either there are already new requests queued,
	 * and corresponding barriers will be send there.
	 * Or nothing new is queued yet, so the difference will be 1.
	 */
	if (atomic_read(&connection->current_tle_nr) !=
	    connection->send.current_epoch_nr + 1)
		return false;

	return true;
}

bool dequeue_work_batch(struct drbd_work_queue *queue, struct list_head *work_list)
{
	spin_lock_irq(&queue->q_lock);
	list_splice_init(&queue->q, work_list);
	spin_unlock_irq(&queue->q_lock);
	return !list_empty(work_list);
}

bool dequeue_work_item(struct drbd_work_queue *queue, struct list_head *work_list)
{
	spin_lock_irq(&queue->q_lock);
	if (!list_empty(&queue->q))
		list_move(queue->q.next, work_list);
	spin_unlock_irq(&queue->q_lock);
	return !list_empty(work_list);
}

void wait_for_work(struct drbd_tconn *connection, struct list_head *work_list)
{
	DEFINE_WAIT(wait);
	struct net_conf *nc;
	int uncork, cork;

	dequeue_work_item(&connection->sender_work, work_list);
	if (!list_empty(work_list))
		return;

	/* Still nothing to do?
	 * Maybe we still need to close the current epoch,
	 * even if no new requests are queued yet.
	 *
	 * Also, poke TCP, just in case.
	 * Then wait for new work (or signal). */
	rcu_read_lock();
	nc = rcu_dereference(connection->net_conf);
	uncork = nc ? nc->tcp_cork : 0;
	rcu_read_unlock();
	if (uncork) {
		mutex_lock(&connection->data.mutex);
		if (connection->data.socket)
			drbd_tcp_uncork(connection->data.socket);
		mutex_unlock(&connection->data.mutex);
	}

	for (;;) {
		int send_barrier;
		prepare_to_wait(&connection->sender_work.q_wait, &wait, TASK_INTERRUPTIBLE);
		spin_lock_irq(&connection->req_lock);
		spin_lock(&connection->sender_work.q_lock);	/* FIXME get rid of this one? */
		/* dequeue single item only,
		 * we still use drbd_queue_work_front() in some places */
		if (!list_empty(&connection->sender_work.q))
			list_move(connection->sender_work.q.next, work_list);
		spin_unlock(&connection->sender_work.q_lock);	/* FIXME get rid of this one? */
		if (!list_empty(work_list) || signal_pending(current)) {
			spin_unlock_irq(&connection->req_lock);
			break;
		}
		send_barrier = need_to_send_barrier(connection);
		spin_unlock_irq(&connection->req_lock);
		if (send_barrier) {
			drbd_send_barrier(connection);
			connection->send.current_epoch_nr++;
		}
		schedule();
		/* may be woken up for other things but new work, too,
		 * e.g. if the current epoch got closed.
		 * In which case we send the barrier above. */
	}
	finish_wait(&connection->sender_work.q_wait, &wait);

	/* someone may have changed the config while we have been waiting above. */
	rcu_read_lock();
	nc = rcu_dereference(connection->net_conf);
	cork = nc ? nc->tcp_cork : 0;
	rcu_read_unlock();
	mutex_lock(&connection->data.mutex);
	if (connection->data.socket) {
		if (cork)
			drbd_tcp_cork(connection->data.socket);
		else if (!uncork)
			drbd_tcp_uncork(connection->data.socket);
	}
	mutex_unlock(&connection->data.mutex);
}

int drbd_worker(struct drbd_thread *thi)
{
	struct drbd_tconn *tconn = thi->tconn;
	struct drbd_work *w = NULL;
	struct drbd_conf *mdev;
	LIST_HEAD(work_list);
	int vnr;

	while (get_t_state(thi) == RUNNING) {
		drbd_thread_current_set_cpu(thi);

		/* as long as we use drbd_queue_work_front(),
		 * we may only dequeue single work items here, not batches. */
		if (list_empty(&work_list))
			wait_for_work(tconn, &work_list);

		if (signal_pending(current)) {
			flush_signals(current);
			if (get_t_state(thi) == RUNNING) {
				conn_warn(tconn, "Worker got an unexpected signal\n");
				continue;
			}
			break;
		}

		if (get_t_state(thi) != RUNNING)
			break;

		while (!list_empty(&work_list)) {
			w = list_first_entry(&work_list, struct drbd_work, list);
			list_del_init(&w->list);
			if (w->cb(w, tconn->cstate < C_WF_REPORT_PARAMS) == 0)
				continue;
			if (tconn->cstate >= C_WF_REPORT_PARAMS)
				conn_request_state(tconn, NS(conn, C_NETWORK_FAILURE), CS_HARD);
		}
	}

	do {
		while (!list_empty(&work_list)) {
			w = list_first_entry(&work_list, struct drbd_work, list);
			list_del_init(&w->list);
			w->cb(w, 1);
		}
		dequeue_work_batch(&tconn->sender_work, &work_list);
	} while (!list_empty(&work_list));

	rcu_read_lock();
	idr_for_each_entry(&tconn->volumes, mdev, vnr) {
		D_ASSERT(mdev->state.disk == D_DISKLESS && mdev->state.conn == C_STANDALONE);
		kref_get(&mdev->kref);
		rcu_read_unlock();
		drbd_mdev_cleanup(mdev);
		kref_put(&mdev->kref, &drbd_minor_destroy);
		rcu_read_lock();
	}
	rcu_read_unlock();

	return 0;
}
