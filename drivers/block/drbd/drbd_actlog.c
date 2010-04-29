/*
   drbd_actlog.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2003-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 2003-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2003-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

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

#include <linux/slab.h>
#include <linux/drbd.h>
#include "drbd_int.h"
#include "drbd_wrappers.h"

/* We maintain a trivial check sum in our on disk activity log.
 * With that we can ensure correct operation even when the storage
 * device might do a partial (last) sector write while loosing power.
 */
struct __packed al_transaction {
	u32       magic;
	u32       tr_number;
	struct __packed {
		u32 pos;
		u32 extent; } updates[1 + AL_EXTENTS_PT];
	u32       xor_sum;
};

struct update_odbm_work {
	struct drbd_work w;
	unsigned int enr;
};

struct update_al_work {
	struct drbd_work w;
	struct lc_element *al_ext;
	struct completion event;
	unsigned int enr;
	/* if old_enr != LC_FREE, write corresponding bitmap sector, too */
	unsigned int old_enr;
};

struct drbd_atodb_wait {
	atomic_t           count;
	struct completion  io_done;
	struct drbd_conf   *mdev;
	int                error;
};


int w_al_write_transaction(struct drbd_conf *, struct drbd_work *, int);

static int _drbd_md_sync_page_io(struct drbd_conf *mdev,
				 struct drbd_backing_dev *bdev,
				 struct page *page, sector_t sector,
				 int rw, int size)
{
	struct bio *bio;
	struct drbd_md_io md_io;
	int ok;

	md_io.mdev = mdev;
	init_completion(&md_io.event);
	md_io.error = 0;

	if ((rw & WRITE) && !test_bit(MD_NO_BARRIER, &mdev->flags))
		rw |= (1 << BIO_RW_BARRIER);
	rw |= ((1<<BIO_RW_UNPLUG) | (1<<BIO_RW_SYNCIO));

 retry:
	bio = bio_alloc(GFP_NOIO, 1);
	bio->bi_bdev = bdev->md_bdev;
	bio->bi_sector = sector;
	ok = (bio_add_page(bio, page, size, 0) == size);
	if (!ok)
		goto out;
	bio->bi_private = &md_io;
	bio->bi_end_io = drbd_md_io_complete;
	bio->bi_rw = rw;

	if (FAULT_ACTIVE(mdev, (rw & WRITE) ? DRBD_FAULT_MD_WR : DRBD_FAULT_MD_RD))
		bio_endio(bio, -EIO);
	else
		submit_bio(rw, bio);
	wait_for_completion(&md_io.event);
	ok = bio_flagged(bio, BIO_UPTODATE) && md_io.error == 0;

	/* check for unsupported barrier op.
	 * would rather check on EOPNOTSUPP, but that is not reliable.
	 * don't try again for ANY return value != 0 */
	if (unlikely(bio_rw_flagged(bio, BIO_RW_BARRIER) && !ok)) {
		/* Try again with no barrier */
		dev_warn(DEV, "Barriers not supported on meta data device - disabling\n");
		set_bit(MD_NO_BARRIER, &mdev->flags);
		rw &= ~(1 << BIO_RW_BARRIER);
		bio_put(bio);
		goto retry;
	}
 out:
	bio_put(bio);
	return ok;
}

int drbd_md_sync_page_io(struct drbd_conf *mdev, struct drbd_backing_dev *bdev,
			 sector_t sector, int rw)
{
	int logical_block_size, mask, ok;
	int offset = 0;
	struct page *iop = mdev->md_io_page;

	D_ASSERT(mutex_is_locked(&mdev->md_io_mutex));

	BUG_ON(!bdev->md_bdev);

	logical_block_size = bdev_logical_block_size(bdev->md_bdev);
	if (logical_block_size == 0)
		logical_block_size = MD_SECTOR_SIZE;

	/* in case logical_block_size != 512 [ s390 only? ] */
	if (logical_block_size != MD_SECTOR_SIZE) {
		mask = (logical_block_size / MD_SECTOR_SIZE) - 1;
		D_ASSERT(mask == 1 || mask == 3 || mask == 7);
		D_ASSERT(logical_block_size == (mask+1) * MD_SECTOR_SIZE);
		offset = sector & mask;
		sector = sector & ~mask;
		iop = mdev->md_io_tmpp;

		if (rw & WRITE) {
			/* these are GFP_KERNEL pages, pre-allocated
			 * on device initialization */
			void *p = page_address(mdev->md_io_page);
			void *hp = page_address(mdev->md_io_tmpp);

			ok = _drbd_md_sync_page_io(mdev, bdev, iop, sector,
					READ, logical_block_size);

			if (unlikely(!ok)) {
				dev_err(DEV, "drbd_md_sync_page_io(,%llus,"
				    "READ [logical_block_size!=512]) failed!\n",
				    (unsigned long long)sector);
				return 0;
			}

			memcpy(hp + offset*MD_SECTOR_SIZE, p, MD_SECTOR_SIZE);
		}
	}

	if (sector < drbd_md_first_sector(bdev) ||
	    sector > drbd_md_last_sector(bdev))
		dev_alert(DEV, "%s [%d]:%s(,%llus,%s) out of range md access!\n",
		     current->comm, current->pid, __func__,
		     (unsigned long long)sector, (rw & WRITE) ? "WRITE" : "READ");

	ok = _drbd_md_sync_page_io(mdev, bdev, iop, sector, rw, logical_block_size);
	if (unlikely(!ok)) {
		dev_err(DEV, "drbd_md_sync_page_io(,%llus,%s) failed!\n",
		    (unsigned long long)sector, (rw & WRITE) ? "WRITE" : "READ");
		return 0;
	}

	if (logical_block_size != MD_SECTOR_SIZE && !(rw & WRITE)) {
		void *p = page_address(mdev->md_io_page);
		void *hp = page_address(mdev->md_io_tmpp);

		memcpy(p, hp + offset*MD_SECTOR_SIZE, MD_SECTOR_SIZE);
	}

	return ok;
}

static struct lc_element *_al_get(struct drbd_conf *mdev, unsigned int enr)
{
	struct lc_element *al_ext;
	struct lc_element *tmp;
	unsigned long     al_flags = 0;

	spin_lock_irq(&mdev->al_lock);
	tmp = lc_find(mdev->resync, enr/AL_EXT_PER_BM_SECT);
	if (unlikely(tmp != NULL)) {
		struct bm_extent  *bm_ext = lc_entry(tmp, struct bm_extent, lce);
		if (test_bit(BME_NO_WRITES, &bm_ext->flags)) {
			spin_unlock_irq(&mdev->al_lock);
			return NULL;
		}
	}
	al_ext   = lc_get(mdev->act_log, enr);
	al_flags = mdev->act_log->flags;
	spin_unlock_irq(&mdev->al_lock);

	/*
	if (!al_ext) {
		if (al_flags & LC_STARVING)
			dev_warn(DEV, "Have to wait for LRU element (AL too small?)\n");
		if (al_flags & LC_DIRTY)
			dev_warn(DEV, "Ongoing AL update (AL device too slow?)\n");
	}
	*/

	return al_ext;
}

void drbd_al_begin_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = (sector >> (AL_EXTENT_SHIFT-9));
	struct lc_element *al_ext;
	struct update_al_work al_work;

	D_ASSERT(atomic_read(&mdev->local_cnt) > 0);

	wait_event(mdev->al_wait, (al_ext = _al_get(mdev, enr)));

	if (al_ext->lc_number != enr) {
		/* drbd_al_write_transaction(mdev,al_ext,enr);
		 * recurses into generic_make_request(), which
		 * disallows recursion, bios being serialized on the
		 * current->bio_tail list now.
		 * we have to delegate updates to the activity log
		 * to the worker thread. */
		init_completion(&al_work.event);
		al_work.al_ext = al_ext;
		al_work.enr = enr;
		al_work.old_enr = al_ext->lc_number;
		al_work.w.cb = w_al_write_transaction;
		drbd_queue_work_front(&mdev->data.work, &al_work.w);
		wait_for_completion(&al_work.event);

		mdev->al_writ_cnt++;

		spin_lock_irq(&mdev->al_lock);
		lc_changed(mdev->act_log, al_ext);
		spin_unlock_irq(&mdev->al_lock);
		wake_up(&mdev->al_wait);
	}
}

void drbd_al_complete_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = (sector >> (AL_EXTENT_SHIFT-9));
	struct lc_element *extent;
	unsigned long flags;

	spin_lock_irqsave(&mdev->al_lock, flags);

	extent = lc_find(mdev->act_log, enr);

	if (!extent) {
		spin_unlock_irqrestore(&mdev->al_lock, flags);
		dev_err(DEV, "al_complete_io() called on inactive extent %u\n", enr);
		return;
	}

	if (lc_put(mdev->act_log, extent) == 0)
		wake_up(&mdev->al_wait);

	spin_unlock_irqrestore(&mdev->al_lock, flags);
}

int
w_al_write_transaction(struct drbd_conf *mdev, struct drbd_work *w, int unused)
{
	struct update_al_work *aw = container_of(w, struct update_al_work, w);
	struct lc_element *updated = aw->al_ext;
	const unsigned int new_enr = aw->enr;
	const unsigned int evicted = aw->old_enr;
	struct al_transaction *buffer;
	sector_t sector;
	int i, n, mx;
	unsigned int extent_nr;
	u32 xor_sum = 0;

	if (!get_ldev(mdev)) {
		dev_err(DEV, "get_ldev() failed in w_al_write_transaction\n");
		complete(&((struct update_al_work *)w)->event);
		return 1;
	}
	/* do we have to do a bitmap write, first?
	 * TODO reduce maximum latency:
	 * submit both bios, then wait for both,
	 * instead of doing two synchronous sector writes. */
	if (mdev->state.conn < C_CONNECTED && evicted != LC_FREE)
		drbd_bm_write_sect(mdev, evicted/AL_EXT_PER_BM_SECT);

	mutex_lock(&mdev->md_io_mutex); /* protects md_io_page, al_tr_cycle, ... */
	buffer = (struct al_transaction *)page_address(mdev->md_io_page);

	buffer->magic = __constant_cpu_to_be32(DRBD_MAGIC);
	buffer->tr_number = cpu_to_be32(mdev->al_tr_number);

	n = lc_index_of(mdev->act_log, updated);

	buffer->updates[0].pos = cpu_to_be32(n);
	buffer->updates[0].extent = cpu_to_be32(new_enr);

	xor_sum ^= new_enr;

	mx = min_t(int, AL_EXTENTS_PT,
		   mdev->act_log->nr_elements - mdev->al_tr_cycle);
	for (i = 0; i < mx; i++) {
		unsigned idx = mdev->al_tr_cycle + i;
		extent_nr = lc_element_by_index(mdev->act_log, idx)->lc_number;
		buffer->updates[i+1].pos = cpu_to_be32(idx);
		buffer->updates[i+1].extent = cpu_to_be32(extent_nr);
		xor_sum ^= extent_nr;
	}
	for (; i < AL_EXTENTS_PT; i++) {
		buffer->updates[i+1].pos = __constant_cpu_to_be32(-1);
		buffer->updates[i+1].extent = __constant_cpu_to_be32(LC_FREE);
		xor_sum ^= LC_FREE;
	}
	mdev->al_tr_cycle += AL_EXTENTS_PT;
	if (mdev->al_tr_cycle >= mdev->act_log->nr_elements)
		mdev->al_tr_cycle = 0;

	buffer->xor_sum = cpu_to_be32(xor_sum);

	sector =  mdev->ldev->md.md_offset
		+ mdev->ldev->md.al_offset + mdev->al_tr_pos;

	if (!drbd_md_sync_page_io(mdev, mdev->ldev, sector, WRITE))
		drbd_chk_io_error(mdev, 1, TRUE);

	if (++mdev->al_tr_pos >
	    div_ceil(mdev->act_log->nr_elements, AL_EXTENTS_PT))
		mdev->al_tr_pos = 0;

	D_ASSERT(mdev->al_tr_pos < MD_AL_MAX_SIZE);
	mdev->al_tr_number++;

	mutex_unlock(&mdev->md_io_mutex);

	complete(&((struct update_al_work *)w)->event);
	put_ldev(mdev);

	return 1;
}

/**
 * drbd_al_read_tr() - Read a single transaction from the on disk activity log
 * @mdev:	DRBD device.
 * @bdev:	Block device to read form.
 * @b:		pointer to an al_transaction.
 * @index:	On disk slot of the transaction to read.
 *
 * Returns -1 on IO error, 0 on checksum error and 1 upon success.
 */
static int drbd_al_read_tr(struct drbd_conf *mdev,
			   struct drbd_backing_dev *bdev,
			   struct al_transaction *b,
			   int index)
{
	sector_t sector;
	int rv, i;
	u32 xor_sum = 0;

	sector = bdev->md.md_offset + bdev->md.al_offset + index;

	/* Dont process error normally,
	 * as this is done before disk is attached! */
	if (!drbd_md_sync_page_io(mdev, bdev, sector, READ))
		return -1;

	rv = (be32_to_cpu(b->magic) == DRBD_MAGIC);

	for (i = 0; i < AL_EXTENTS_PT + 1; i++)
		xor_sum ^= be32_to_cpu(b->updates[i].extent);
	rv &= (xor_sum == be32_to_cpu(b->xor_sum));

	return rv;
}

/**
 * drbd_al_read_log() - Restores the activity log from its on disk representation.
 * @mdev:	DRBD device.
 * @bdev:	Block device to read form.
 *
 * Returns 1 on success, returns 0 when reading the log failed due to IO errors.
 */
int drbd_al_read_log(struct drbd_conf *mdev, struct drbd_backing_dev *bdev)
{
	struct al_transaction *buffer;
	int i;
	int rv;
	int mx;
	int active_extents = 0;
	int transactions = 0;
	int found_valid = 0;
	int from = 0;
	int to = 0;
	u32 from_tnr = 0;
	u32 to_tnr = 0;
	u32 cnr;

	mx = div_ceil(mdev->act_log->nr_elements, AL_EXTENTS_PT);

	/* lock out all other meta data io for now,
	 * and make sure the page is mapped.
	 */
	mutex_lock(&mdev->md_io_mutex);
	buffer = page_address(mdev->md_io_page);

	/* Find the valid transaction in the log */
	for (i = 0; i <= mx; i++) {
		rv = drbd_al_read_tr(mdev, bdev, buffer, i);
		if (rv == 0)
			continue;
		if (rv == -1) {
			mutex_unlock(&mdev->md_io_mutex);
			return 0;
		}
		cnr = be32_to_cpu(buffer->tr_number);

		if (++found_valid == 1) {
			from = i;
			to = i;
			from_tnr = cnr;
			to_tnr = cnr;
			continue;
		}
		if ((int)cnr - (int)from_tnr < 0) {
			D_ASSERT(from_tnr - cnr + i - from == mx+1);
			from = i;
			from_tnr = cnr;
		}
		if ((int)cnr - (int)to_tnr > 0) {
			D_ASSERT(cnr - to_tnr == i - to);
			to = i;
			to_tnr = cnr;
		}
	}

	if (!found_valid) {
		dev_warn(DEV, "No usable activity log found.\n");
		mutex_unlock(&mdev->md_io_mutex);
		return 1;
	}

	/* Read the valid transactions.
	 * dev_info(DEV, "Reading from %d to %d.\n",from,to); */
	i = from;
	while (1) {
		int j, pos;
		unsigned int extent_nr;
		unsigned int trn;

		rv = drbd_al_read_tr(mdev, bdev, buffer, i);
		ERR_IF(rv == 0) goto cancel;
		if (rv == -1) {
			mutex_unlock(&mdev->md_io_mutex);
			return 0;
		}

		trn = be32_to_cpu(buffer->tr_number);

		spin_lock_irq(&mdev->al_lock);

		/* This loop runs backwards because in the cyclic
		   elements there might be an old version of the
		   updated element (in slot 0). So the element in slot 0
		   can overwrite old versions. */
		for (j = AL_EXTENTS_PT; j >= 0; j--) {
			pos = be32_to_cpu(buffer->updates[j].pos);
			extent_nr = be32_to_cpu(buffer->updates[j].extent);

			if (extent_nr == LC_FREE)
				continue;

			lc_set(mdev->act_log, extent_nr, pos);
			active_extents++;
		}
		spin_unlock_irq(&mdev->al_lock);

		transactions++;

cancel:
		if (i == to)
			break;
		i++;
		if (i > mx)
			i = 0;
	}

	mdev->al_tr_number = to_tnr+1;
	mdev->al_tr_pos = to;
	if (++mdev->al_tr_pos >
	    div_ceil(mdev->act_log->nr_elements, AL_EXTENTS_PT))
		mdev->al_tr_pos = 0;

	/* ok, we are done with it */
	mutex_unlock(&mdev->md_io_mutex);

	dev_info(DEV, "Found %d transactions (%d active extents) in activity log.\n",
	     transactions, active_extents);

	return 1;
}

static void atodb_endio(struct bio *bio, int error)
{
	struct drbd_atodb_wait *wc = bio->bi_private;
	struct drbd_conf *mdev = wc->mdev;
	struct page *page;
	int uptodate = bio_flagged(bio, BIO_UPTODATE);

	/* strange behavior of some lower level drivers...
	 * fail the request by clearing the uptodate flag,
	 * but do not return any error?! */
	if (!error && !uptodate)
		error = -EIO;

	drbd_chk_io_error(mdev, error, TRUE);
	if (error && wc->error == 0)
		wc->error = error;

	if (atomic_dec_and_test(&wc->count))
		complete(&wc->io_done);

	page = bio->bi_io_vec[0].bv_page;
	put_page(page);
	bio_put(bio);
	mdev->bm_writ_cnt++;
	put_ldev(mdev);
}

/* sector to word */
#define S2W(s)	((s)<<(BM_EXT_SHIFT-BM_BLOCK_SHIFT-LN2_BPL))

/* activity log to on disk bitmap -- prepare bio unless that sector
 * is already covered by previously prepared bios */
static int atodb_prepare_unless_covered(struct drbd_conf *mdev,
					struct bio **bios,
					unsigned int enr,
					struct drbd_atodb_wait *wc) __must_hold(local)
{
	struct bio *bio;
	struct page *page;
	sector_t on_disk_sector;
	unsigned int page_offset = PAGE_SIZE;
	int offset;
	int i = 0;
	int err = -ENOMEM;

	/* We always write aligned, full 4k blocks,
	 * so we can ignore the logical_block_size (for now) */
	enr &= ~7U;
	on_disk_sector = enr + mdev->ldev->md.md_offset
			     + mdev->ldev->md.bm_offset;

	D_ASSERT(!(on_disk_sector & 7U));

	/* Check if that enr is already covered by an already created bio.
	 * Caution, bios[] is not NULL terminated,
	 * but only initialized to all NULL.
	 * For completely scattered activity log,
	 * the last invocation iterates over all bios,
	 * and finds the last NULL entry.
	 */
	while ((bio = bios[i])) {
		if (bio->bi_sector == on_disk_sector)
			return 0;
		i++;
	}
	/* bios[i] == NULL, the next not yet used slot */

	/* GFP_KERNEL, we are not in the write-out path */
	bio = bio_alloc(GFP_KERNEL, 1);
	if (bio == NULL)
		return -ENOMEM;

	if (i > 0) {
		const struct bio_vec *prev_bv = bios[i-1]->bi_io_vec;
		page_offset = prev_bv->bv_offset + prev_bv->bv_len;
		page = prev_bv->bv_page;
	}
	if (page_offset == PAGE_SIZE) {
		page = alloc_page(__GFP_HIGHMEM);
		if (page == NULL)
			goto out_bio_put;
		page_offset = 0;
	} else {
		get_page(page);
	}

	offset = S2W(enr);
	drbd_bm_get_lel(mdev, offset,
			min_t(size_t, S2W(8), drbd_bm_words(mdev) - offset),
			kmap(page) + page_offset);
	kunmap(page);

	bio->bi_private = wc;
	bio->bi_end_io = atodb_endio;
	bio->bi_bdev = mdev->ldev->md_bdev;
	bio->bi_sector = on_disk_sector;

	if (bio_add_page(bio, page, 4096, page_offset) != 4096)
		goto out_put_page;

	atomic_inc(&wc->count);
	/* we already know that we may do this...
	 * get_ldev_if_state(mdev,D_ATTACHING);
	 * just get the extra reference, so that the local_cnt reflects
	 * the number of pending IO requests DRBD at its backing device.
	 */
	atomic_inc(&mdev->local_cnt);

	bios[i] = bio;

	return 0;

out_put_page:
	err = -EINVAL;
	put_page(page);
out_bio_put:
	bio_put(bio);
	return err;
}

/**
 * drbd_al_to_on_disk_bm() -  * Writes bitmap parts covered by active AL extents
 * @mdev:	DRBD device.
 *
 * Called when we detach (unconfigure) local storage,
 * or when we go from R_PRIMARY to R_SECONDARY role.
 */
void drbd_al_to_on_disk_bm(struct drbd_conf *mdev)
{
	int i, nr_elements;
	unsigned int enr;
	struct bio **bios;
	struct drbd_atodb_wait wc;

	ERR_IF (!get_ldev_if_state(mdev, D_ATTACHING))
		return; /* sorry, I don't have any act_log etc... */

	wait_event(mdev->al_wait, lc_try_lock(mdev->act_log));

	nr_elements = mdev->act_log->nr_elements;

	/* GFP_KERNEL, we are not in anyone's write-out path */
	bios = kzalloc(sizeof(struct bio *) * nr_elements, GFP_KERNEL);
	if (!bios)
		goto submit_one_by_one;

	atomic_set(&wc.count, 0);
	init_completion(&wc.io_done);
	wc.mdev = mdev;
	wc.error = 0;

	for (i = 0; i < nr_elements; i++) {
		enr = lc_element_by_index(mdev->act_log, i)->lc_number;
		if (enr == LC_FREE)
			continue;
		/* next statement also does atomic_inc wc.count and local_cnt */
		if (atodb_prepare_unless_covered(mdev, bios,
						enr/AL_EXT_PER_BM_SECT,
						&wc))
			goto free_bios_submit_one_by_one;
	}

	/* unnecessary optimization? */
	lc_unlock(mdev->act_log);
	wake_up(&mdev->al_wait);

	/* all prepared, submit them */
	for (i = 0; i < nr_elements; i++) {
		if (bios[i] == NULL)
			break;
		if (FAULT_ACTIVE(mdev, DRBD_FAULT_MD_WR)) {
			bios[i]->bi_rw = WRITE;
			bio_endio(bios[i], -EIO);
		} else {
			submit_bio(WRITE, bios[i]);
		}
	}

	drbd_blk_run_queue(bdev_get_queue(mdev->ldev->md_bdev));

	/* always (try to) flush bitmap to stable storage */
	drbd_md_flush(mdev);

	/* In case we did not submit a single IO do not wait for
	 * them to complete. ( Because we would wait forever here. )
	 *
	 * In case we had IOs and they are already complete, there
	 * is not point in waiting anyways.
	 * Therefore this if () ... */
	if (atomic_read(&wc.count))
		wait_for_completion(&wc.io_done);

	put_ldev(mdev);

	kfree(bios);
	return;

 free_bios_submit_one_by_one:
	/* free everything by calling the endio callback directly. */
	for (i = 0; i < nr_elements && bios[i]; i++)
		bio_endio(bios[i], 0);

	kfree(bios);

 submit_one_by_one:
	dev_warn(DEV, "Using the slow drbd_al_to_on_disk_bm()\n");

	for (i = 0; i < mdev->act_log->nr_elements; i++) {
		enr = lc_element_by_index(mdev->act_log, i)->lc_number;
		if (enr == LC_FREE)
			continue;
		/* Really slow: if we have al-extents 16..19 active,
		 * sector 4 will be written four times! Synchronous! */
		drbd_bm_write_sect(mdev, enr/AL_EXT_PER_BM_SECT);
	}

	lc_unlock(mdev->act_log);
	wake_up(&mdev->al_wait);
	put_ldev(mdev);
}

/**
 * drbd_al_apply_to_bm() - Sets the bitmap to diry(1) where covered ba active AL extents
 * @mdev:	DRBD device.
 */
void drbd_al_apply_to_bm(struct drbd_conf *mdev)
{
	unsigned int enr;
	unsigned long add = 0;
	char ppb[10];
	int i;

	wait_event(mdev->al_wait, lc_try_lock(mdev->act_log));

	for (i = 0; i < mdev->act_log->nr_elements; i++) {
		enr = lc_element_by_index(mdev->act_log, i)->lc_number;
		if (enr == LC_FREE)
			continue;
		add += drbd_bm_ALe_set_all(mdev, enr);
	}

	lc_unlock(mdev->act_log);
	wake_up(&mdev->al_wait);

	dev_info(DEV, "Marked additional %s as out-of-sync based on AL.\n",
	     ppsize(ppb, Bit2KB(add)));
}

static int _try_lc_del(struct drbd_conf *mdev, struct lc_element *al_ext)
{
	int rv;

	spin_lock_irq(&mdev->al_lock);
	rv = (al_ext->refcnt == 0);
	if (likely(rv))
		lc_del(mdev->act_log, al_ext);
	spin_unlock_irq(&mdev->al_lock);

	return rv;
}

/**
 * drbd_al_shrink() - Removes all active extents form the activity log
 * @mdev:	DRBD device.
 *
 * Removes all active extents form the activity log, waiting until
 * the reference count of each entry dropped to 0 first, of course.
 *
 * You need to lock mdev->act_log with lc_try_lock() / lc_unlock()
 */
void drbd_al_shrink(struct drbd_conf *mdev)
{
	struct lc_element *al_ext;
	int i;

	D_ASSERT(test_bit(__LC_DIRTY, &mdev->act_log->flags));

	for (i = 0; i < mdev->act_log->nr_elements; i++) {
		al_ext = lc_element_by_index(mdev->act_log, i);
		if (al_ext->lc_number == LC_FREE)
			continue;
		wait_event(mdev->al_wait, _try_lc_del(mdev, al_ext));
	}

	wake_up(&mdev->al_wait);
}

static int w_update_odbm(struct drbd_conf *mdev, struct drbd_work *w, int unused)
{
	struct update_odbm_work *udw = container_of(w, struct update_odbm_work, w);

	if (!get_ldev(mdev)) {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_warn(DEV, "Can not update on disk bitmap, local IO disabled.\n");
		kfree(udw);
		return 1;
	}

	drbd_bm_write_sect(mdev, udw->enr);
	put_ldev(mdev);

	kfree(udw);

	if (drbd_bm_total_weight(mdev) <= mdev->rs_failed) {
		switch (mdev->state.conn) {
		case C_SYNC_SOURCE:  case C_SYNC_TARGET:
		case C_PAUSED_SYNC_S: case C_PAUSED_SYNC_T:
			drbd_resync_finished(mdev);
		default:
			/* nothing to do */
			break;
		}
	}
	drbd_bcast_sync_progress(mdev);

	return 1;
}


/* ATTENTION. The AL's extents are 4MB each, while the extents in the
 * resync LRU-cache are 16MB each.
 * The caller of this function has to hold an get_ldev() reference.
 *
 * TODO will be obsoleted once we have a caching lru of the on disk bitmap
 */
static void drbd_try_clear_on_disk_bm(struct drbd_conf *mdev, sector_t sector,
				      int count, int success)
{
	struct lc_element *e;
	struct update_odbm_work *udw;

	unsigned int enr;

	D_ASSERT(atomic_read(&mdev->local_cnt));

	/* I simply assume that a sector/size pair never crosses
	 * a 16 MB extent border. (Currently this is true...) */
	enr = BM_SECT_TO_EXT(sector);

	e = lc_get(mdev->resync, enr);
	if (e) {
		struct bm_extent *ext = lc_entry(e, struct bm_extent, lce);
		if (ext->lce.lc_number == enr) {
			if (success)
				ext->rs_left -= count;
			else
				ext->rs_failed += count;
			if (ext->rs_left < ext->rs_failed) {
				dev_err(DEV, "BAD! sector=%llus enr=%u rs_left=%d "
				    "rs_failed=%d count=%d\n",
				     (unsigned long long)sector,
				     ext->lce.lc_number, ext->rs_left,
				     ext->rs_failed, count);
				dump_stack();

				lc_put(mdev->resync, &ext->lce);
				drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
				return;
			}
		} else {
			/* Normally this element should be in the cache,
			 * since drbd_rs_begin_io() pulled it already in.
			 *
			 * But maybe an application write finished, and we set
			 * something outside the resync lru_cache in sync.
			 */
			int rs_left = drbd_bm_e_weight(mdev, enr);
			if (ext->flags != 0) {
				dev_warn(DEV, "changing resync lce: %d[%u;%02lx]"
				     " -> %d[%u;00]\n",
				     ext->lce.lc_number, ext->rs_left,
				     ext->flags, enr, rs_left);
				ext->flags = 0;
			}
			if (ext->rs_failed) {
				dev_warn(DEV, "Kicking resync_lru element enr=%u "
				     "out with rs_failed=%d\n",
				     ext->lce.lc_number, ext->rs_failed);
				set_bit(WRITE_BM_AFTER_RESYNC, &mdev->flags);
			}
			ext->rs_left = rs_left;
			ext->rs_failed = success ? 0 : count;
			lc_changed(mdev->resync, &ext->lce);
		}
		lc_put(mdev->resync, &ext->lce);
		/* no race, we are within the al_lock! */

		if (ext->rs_left == ext->rs_failed) {
			ext->rs_failed = 0;

			udw = kmalloc(sizeof(*udw), GFP_ATOMIC);
			if (udw) {
				udw->enr = ext->lce.lc_number;
				udw->w.cb = w_update_odbm;
				drbd_queue_work_front(&mdev->data.work, &udw->w);
			} else {
				dev_warn(DEV, "Could not kmalloc an udw\n");
				set_bit(WRITE_BM_AFTER_RESYNC, &mdev->flags);
			}
		}
	} else {
		dev_err(DEV, "lc_get() failed! locked=%d/%d flags=%lu\n",
		    mdev->resync_locked,
		    mdev->resync->nr_elements,
		    mdev->resync->flags);
	}
}

/* clear the bit corresponding to the piece of storage in question:
 * size byte of data starting from sector.  Only clear a bits of the affected
 * one ore more _aligned_ BM_BLOCK_SIZE blocks.
 *
 * called by worker on C_SYNC_TARGET and receiver on SyncSource.
 *
 */
void __drbd_set_in_sync(struct drbd_conf *mdev, sector_t sector, int size,
		       const char *file, const unsigned int line)
{
	/* Is called from worker and receiver context _only_ */
	unsigned long sbnr, ebnr, lbnr;
	unsigned long count = 0;
	sector_t esector, nr_sectors;
	int wake_up = 0;
	unsigned long flags;

	if (size <= 0 || (size & 0x1ff) != 0 || size > DRBD_MAX_SEGMENT_SIZE) {
		dev_err(DEV, "drbd_set_in_sync: sector=%llus size=%d nonsense!\n",
				(unsigned long long)sector, size);
		return;
	}
	nr_sectors = drbd_get_capacity(mdev->this_bdev);
	esector = sector + (size >> 9) - 1;

	ERR_IF(sector >= nr_sectors) return;
	ERR_IF(esector >= nr_sectors) esector = (nr_sectors-1);

	lbnr = BM_SECT_TO_BIT(nr_sectors-1);

	/* we clear it (in sync).
	 * round up start sector, round down end sector.  we make sure we only
	 * clear full, aligned, BM_BLOCK_SIZE (4K) blocks */
	if (unlikely(esector < BM_SECT_PER_BIT-1))
		return;
	if (unlikely(esector == (nr_sectors-1)))
		ebnr = lbnr;
	else
		ebnr = BM_SECT_TO_BIT(esector - (BM_SECT_PER_BIT-1));
	sbnr = BM_SECT_TO_BIT(sector + BM_SECT_PER_BIT-1);

	if (sbnr > ebnr)
		return;

	/*
	 * ok, (capacity & 7) != 0 sometimes, but who cares...
	 * we count rs_{total,left} in bits, not sectors.
	 */
	spin_lock_irqsave(&mdev->al_lock, flags);
	count = drbd_bm_clear_bits(mdev, sbnr, ebnr);
	if (count) {
		/* we need the lock for drbd_try_clear_on_disk_bm */
		if (jiffies - mdev->rs_mark_time > HZ*10) {
			/* should be rolling marks,
			 * but we estimate only anyways. */
			if (mdev->rs_mark_left != drbd_bm_total_weight(mdev) &&
			    mdev->state.conn != C_PAUSED_SYNC_T &&
			    mdev->state.conn != C_PAUSED_SYNC_S) {
				mdev->rs_mark_time = jiffies;
				mdev->rs_mark_left = drbd_bm_total_weight(mdev);
			}
		}
		if (get_ldev(mdev)) {
			drbd_try_clear_on_disk_bm(mdev, sector, count, TRUE);
			put_ldev(mdev);
		}
		/* just wake_up unconditional now, various lc_chaged(),
		 * lc_put() in drbd_try_clear_on_disk_bm(). */
		wake_up = 1;
	}
	spin_unlock_irqrestore(&mdev->al_lock, flags);
	if (wake_up)
		wake_up(&mdev->al_wait);
}

/*
 * this is intended to set one request worth of data out of sync.
 * affects at least 1 bit,
 * and at most 1+DRBD_MAX_SEGMENT_SIZE/BM_BLOCK_SIZE bits.
 *
 * called by tl_clear and drbd_send_dblock (==drbd_make_request).
 * so this can be _any_ process.
 */
void __drbd_set_out_of_sync(struct drbd_conf *mdev, sector_t sector, int size,
			    const char *file, const unsigned int line)
{
	unsigned long sbnr, ebnr, lbnr, flags;
	sector_t esector, nr_sectors;
	unsigned int enr, count;
	struct lc_element *e;

	if (size <= 0 || (size & 0x1ff) != 0 || size > DRBD_MAX_SEGMENT_SIZE) {
		dev_err(DEV, "sector: %llus, size: %d\n",
			(unsigned long long)sector, size);
		return;
	}

	if (!get_ldev(mdev))
		return; /* no disk, no metadata, no bitmap to set bits in */

	nr_sectors = drbd_get_capacity(mdev->this_bdev);
	esector = sector + (size >> 9) - 1;

	ERR_IF(sector >= nr_sectors)
		goto out;
	ERR_IF(esector >= nr_sectors)
		esector = (nr_sectors-1);

	lbnr = BM_SECT_TO_BIT(nr_sectors-1);

	/* we set it out of sync,
	 * we do not need to round anything here */
	sbnr = BM_SECT_TO_BIT(sector);
	ebnr = BM_SECT_TO_BIT(esector);

	/* ok, (capacity & 7) != 0 sometimes, but who cares...
	 * we count rs_{total,left} in bits, not sectors.  */
	spin_lock_irqsave(&mdev->al_lock, flags);
	count = drbd_bm_set_bits(mdev, sbnr, ebnr);

	enr = BM_SECT_TO_EXT(sector);
	e = lc_find(mdev->resync, enr);
	if (e)
		lc_entry(e, struct bm_extent, lce)->rs_left += count;
	spin_unlock_irqrestore(&mdev->al_lock, flags);

out:
	put_ldev(mdev);
}

static
struct bm_extent *_bme_get(struct drbd_conf *mdev, unsigned int enr)
{
	struct lc_element *e;
	struct bm_extent *bm_ext;
	int wakeup = 0;
	unsigned long rs_flags;

	spin_lock_irq(&mdev->al_lock);
	if (mdev->resync_locked > mdev->resync->nr_elements/2) {
		spin_unlock_irq(&mdev->al_lock);
		return NULL;
	}
	e = lc_get(mdev->resync, enr);
	bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
	if (bm_ext) {
		if (bm_ext->lce.lc_number != enr) {
			bm_ext->rs_left = drbd_bm_e_weight(mdev, enr);
			bm_ext->rs_failed = 0;
			lc_changed(mdev->resync, &bm_ext->lce);
			wakeup = 1;
		}
		if (bm_ext->lce.refcnt == 1)
			mdev->resync_locked++;
		set_bit(BME_NO_WRITES, &bm_ext->flags);
	}
	rs_flags = mdev->resync->flags;
	spin_unlock_irq(&mdev->al_lock);
	if (wakeup)
		wake_up(&mdev->al_wait);

	if (!bm_ext) {
		if (rs_flags & LC_STARVING)
			dev_warn(DEV, "Have to wait for element"
			     " (resync LRU too small?)\n");
		BUG_ON(rs_flags & LC_DIRTY);
	}

	return bm_ext;
}

static int _is_in_al(struct drbd_conf *mdev, unsigned int enr)
{
	struct lc_element *al_ext;
	int rv = 0;

	spin_lock_irq(&mdev->al_lock);
	if (unlikely(enr == mdev->act_log->new_number))
		rv = 1;
	else {
		al_ext = lc_find(mdev->act_log, enr);
		if (al_ext) {
			if (al_ext->refcnt)
				rv = 1;
		}
	}
	spin_unlock_irq(&mdev->al_lock);

	/*
	if (unlikely(rv)) {
		dev_info(DEV, "Delaying sync read until app's write is done\n");
	}
	*/
	return rv;
}

/**
 * drbd_rs_begin_io() - Gets an extent in the resync LRU cache and sets it to BME_LOCKED
 * @mdev:	DRBD device.
 * @sector:	The sector number.
 *
 * This functions sleeps on al_wait. Returns 1 on success, 0 if interrupted.
 */
int drbd_rs_begin_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = BM_SECT_TO_EXT(sector);
	struct bm_extent *bm_ext;
	int i, sig;

	sig = wait_event_interruptible(mdev->al_wait,
			(bm_ext = _bme_get(mdev, enr)));
	if (sig)
		return 0;

	if (test_bit(BME_LOCKED, &bm_ext->flags))
		return 1;

	for (i = 0; i < AL_EXT_PER_BM_SECT; i++) {
		sig = wait_event_interruptible(mdev->al_wait,
				!_is_in_al(mdev, enr * AL_EXT_PER_BM_SECT + i));
		if (sig) {
			spin_lock_irq(&mdev->al_lock);
			if (lc_put(mdev->resync, &bm_ext->lce) == 0) {
				clear_bit(BME_NO_WRITES, &bm_ext->flags);
				mdev->resync_locked--;
				wake_up(&mdev->al_wait);
			}
			spin_unlock_irq(&mdev->al_lock);
			return 0;
		}
	}

	set_bit(BME_LOCKED, &bm_ext->flags);

	return 1;
}

/**
 * drbd_try_rs_begin_io() - Gets an extent in the resync LRU cache, does not sleep
 * @mdev:	DRBD device.
 * @sector:	The sector number.
 *
 * Gets an extent in the resync LRU cache, sets it to BME_NO_WRITES, then
 * tries to set it to BME_LOCKED. Returns 0 upon success, and -EAGAIN
 * if there is still application IO going on in this area.
 */
int drbd_try_rs_begin_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = BM_SECT_TO_EXT(sector);
	const unsigned int al_enr = enr*AL_EXT_PER_BM_SECT;
	struct lc_element *e;
	struct bm_extent *bm_ext;
	int i;

	spin_lock_irq(&mdev->al_lock);
	if (mdev->resync_wenr != LC_FREE && mdev->resync_wenr != enr) {
		/* in case you have very heavy scattered io, it may
		 * stall the syncer undefined if we give up the ref count
		 * when we try again and requeue.
		 *
		 * if we don't give up the refcount, but the next time
		 * we are scheduled this extent has been "synced" by new
		 * application writes, we'd miss the lc_put on the
		 * extent we keep the refcount on.
		 * so we remembered which extent we had to try again, and
		 * if the next requested one is something else, we do
		 * the lc_put here...
		 * we also have to wake_up
		 */
		e = lc_find(mdev->resync, mdev->resync_wenr);
		bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
		if (bm_ext) {
			D_ASSERT(!test_bit(BME_LOCKED, &bm_ext->flags));
			D_ASSERT(test_bit(BME_NO_WRITES, &bm_ext->flags));
			clear_bit(BME_NO_WRITES, &bm_ext->flags);
			mdev->resync_wenr = LC_FREE;
			if (lc_put(mdev->resync, &bm_ext->lce) == 0)
				mdev->resync_locked--;
			wake_up(&mdev->al_wait);
		} else {
			dev_alert(DEV, "LOGIC BUG\n");
		}
	}
	/* TRY. */
	e = lc_try_get(mdev->resync, enr);
	bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
	if (bm_ext) {
		if (test_bit(BME_LOCKED, &bm_ext->flags))
			goto proceed;
		if (!test_and_set_bit(BME_NO_WRITES, &bm_ext->flags)) {
			mdev->resync_locked++;
		} else {
			/* we did set the BME_NO_WRITES,
			 * but then could not set BME_LOCKED,
			 * so we tried again.
			 * drop the extra reference. */
			bm_ext->lce.refcnt--;
			D_ASSERT(bm_ext->lce.refcnt > 0);
		}
		goto check_al;
	} else {
		/* do we rather want to try later? */
		if (mdev->resync_locked > mdev->resync->nr_elements-3)
			goto try_again;
		/* Do or do not. There is no try. -- Yoda */
		e = lc_get(mdev->resync, enr);
		bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
		if (!bm_ext) {
			const unsigned long rs_flags = mdev->resync->flags;
			if (rs_flags & LC_STARVING)
				dev_warn(DEV, "Have to wait for element"
				     " (resync LRU too small?)\n");
			BUG_ON(rs_flags & LC_DIRTY);
			goto try_again;
		}
		if (bm_ext->lce.lc_number != enr) {
			bm_ext->rs_left = drbd_bm_e_weight(mdev, enr);
			bm_ext->rs_failed = 0;
			lc_changed(mdev->resync, &bm_ext->lce);
			wake_up(&mdev->al_wait);
			D_ASSERT(test_bit(BME_LOCKED, &bm_ext->flags) == 0);
		}
		set_bit(BME_NO_WRITES, &bm_ext->flags);
		D_ASSERT(bm_ext->lce.refcnt == 1);
		mdev->resync_locked++;
		goto check_al;
	}
check_al:
	for (i = 0; i < AL_EXT_PER_BM_SECT; i++) {
		if (unlikely(al_enr+i == mdev->act_log->new_number))
			goto try_again;
		if (lc_is_used(mdev->act_log, al_enr+i))
			goto try_again;
	}
	set_bit(BME_LOCKED, &bm_ext->flags);
proceed:
	mdev->resync_wenr = LC_FREE;
	spin_unlock_irq(&mdev->al_lock);
	return 0;

try_again:
	if (bm_ext)
		mdev->resync_wenr = enr;
	spin_unlock_irq(&mdev->al_lock);
	return -EAGAIN;
}

void drbd_rs_complete_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = BM_SECT_TO_EXT(sector);
	struct lc_element *e;
	struct bm_extent *bm_ext;
	unsigned long flags;

	spin_lock_irqsave(&mdev->al_lock, flags);
	e = lc_find(mdev->resync, enr);
	bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
	if (!bm_ext) {
		spin_unlock_irqrestore(&mdev->al_lock, flags);
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "drbd_rs_complete_io() called, but extent not found\n");
		return;
	}

	if (bm_ext->lce.refcnt == 0) {
		spin_unlock_irqrestore(&mdev->al_lock, flags);
		dev_err(DEV, "drbd_rs_complete_io(,%llu [=%u]) called, "
		    "but refcnt is 0!?\n",
		    (unsigned long long)sector, enr);
		return;
	}

	if (lc_put(mdev->resync, &bm_ext->lce) == 0) {
		clear_bit(BME_LOCKED, &bm_ext->flags);
		clear_bit(BME_NO_WRITES, &bm_ext->flags);
		mdev->resync_locked--;
		wake_up(&mdev->al_wait);
	}

	spin_unlock_irqrestore(&mdev->al_lock, flags);
}

/**
 * drbd_rs_cancel_all() - Removes all extents from the resync LRU (even BME_LOCKED)
 * @mdev:	DRBD device.
 */
void drbd_rs_cancel_all(struct drbd_conf *mdev)
{
	spin_lock_irq(&mdev->al_lock);

	if (get_ldev_if_state(mdev, D_FAILED)) { /* Makes sure ->resync is there. */
		lc_reset(mdev->resync);
		put_ldev(mdev);
	}
	mdev->resync_locked = 0;
	mdev->resync_wenr = LC_FREE;
	spin_unlock_irq(&mdev->al_lock);
	wake_up(&mdev->al_wait);
}

/**
 * drbd_rs_del_all() - Gracefully remove all extents from the resync LRU
 * @mdev:	DRBD device.
 *
 * Returns 0 upon success, -EAGAIN if at least one reference count was
 * not zero.
 */
int drbd_rs_del_all(struct drbd_conf *mdev)
{
	struct lc_element *e;
	struct bm_extent *bm_ext;
	int i;

	spin_lock_irq(&mdev->al_lock);

	if (get_ldev_if_state(mdev, D_FAILED)) {
		/* ok, ->resync is there. */
		for (i = 0; i < mdev->resync->nr_elements; i++) {
			e = lc_element_by_index(mdev->resync, i);
			bm_ext = lc_entry(e, struct bm_extent, lce);
			if (bm_ext->lce.lc_number == LC_FREE)
				continue;
			if (bm_ext->lce.lc_number == mdev->resync_wenr) {
				dev_info(DEV, "dropping %u in drbd_rs_del_all, apparently"
				     " got 'synced' by application io\n",
				     mdev->resync_wenr);
				D_ASSERT(!test_bit(BME_LOCKED, &bm_ext->flags));
				D_ASSERT(test_bit(BME_NO_WRITES, &bm_ext->flags));
				clear_bit(BME_NO_WRITES, &bm_ext->flags);
				mdev->resync_wenr = LC_FREE;
				lc_put(mdev->resync, &bm_ext->lce);
			}
			if (bm_ext->lce.refcnt != 0) {
				dev_info(DEV, "Retrying drbd_rs_del_all() later. "
				     "refcnt=%d\n", bm_ext->lce.refcnt);
				put_ldev(mdev);
				spin_unlock_irq(&mdev->al_lock);
				return -EAGAIN;
			}
			D_ASSERT(!test_bit(BME_LOCKED, &bm_ext->flags));
			D_ASSERT(!test_bit(BME_NO_WRITES, &bm_ext->flags));
			lc_del(mdev->resync, &bm_ext->lce);
		}
		D_ASSERT(mdev->resync->used == 0);
		put_ldev(mdev);
	}
	spin_unlock_irq(&mdev->al_lock);

	return 0;
}

/**
 * drbd_rs_failed_io() - Record information on a failure to resync the specified blocks
 * @mdev:	DRBD device.
 * @sector:	The sector number.
 * @size:	Size of failed IO operation, in byte.
 */
void drbd_rs_failed_io(struct drbd_conf *mdev, sector_t sector, int size)
{
	/* Is called from worker and receiver context _only_ */
	unsigned long sbnr, ebnr, lbnr;
	unsigned long count;
	sector_t esector, nr_sectors;
	int wake_up = 0;

	if (size <= 0 || (size & 0x1ff) != 0 || size > DRBD_MAX_SEGMENT_SIZE) {
		dev_err(DEV, "drbd_rs_failed_io: sector=%llus size=%d nonsense!\n",
				(unsigned long long)sector, size);
		return;
	}
	nr_sectors = drbd_get_capacity(mdev->this_bdev);
	esector = sector + (size >> 9) - 1;

	ERR_IF(sector >= nr_sectors) return;
	ERR_IF(esector >= nr_sectors) esector = (nr_sectors-1);

	lbnr = BM_SECT_TO_BIT(nr_sectors-1);

	/*
	 * round up start sector, round down end sector.  we make sure we only
	 * handle full, aligned, BM_BLOCK_SIZE (4K) blocks */
	if (unlikely(esector < BM_SECT_PER_BIT-1))
		return;
	if (unlikely(esector == (nr_sectors-1)))
		ebnr = lbnr;
	else
		ebnr = BM_SECT_TO_BIT(esector - (BM_SECT_PER_BIT-1));
	sbnr = BM_SECT_TO_BIT(sector + BM_SECT_PER_BIT-1);

	if (sbnr > ebnr)
		return;

	/*
	 * ok, (capacity & 7) != 0 sometimes, but who cares...
	 * we count rs_{total,left} in bits, not sectors.
	 */
	spin_lock_irq(&mdev->al_lock);
	count = drbd_bm_count_bits(mdev, sbnr, ebnr);
	if (count) {
		mdev->rs_failed += count;

		if (get_ldev(mdev)) {
			drbd_try_clear_on_disk_bm(mdev, sector, count, FALSE);
			put_ldev(mdev);
		}

		/* just wake_up unconditional now, various lc_chaged(),
		 * lc_put() in drbd_try_clear_on_disk_bm(). */
		wake_up = 1;
	}
	spin_unlock_irq(&mdev->al_lock);
	if (wake_up)
		wake_up(&mdev->al_wait);
}
