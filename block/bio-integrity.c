/*
 * bio-integrity.c - bio data integrity extensions
 *
 * Copyright (C) 2007, 2008, 2009 Oracle Corporation
 * Written by: Martin K. Petersen <martin.petersen@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#include <linux/blkdev.h>
#include <linux/mempool.h>
#include <linux/export.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include "blk.h"

#define BIP_INLINE_VECS	4

static struct kmem_cache *bip_slab;
static struct workqueue_struct *kintegrityd_wq;

void blk_flush_integrity(void)
{
	flush_workqueue(kintegrityd_wq);
}

/**
 * bio_integrity_alloc - Allocate integrity payload and attach it to bio
 * @bio:	bio to attach integrity metadata to
 * @gfp_mask:	Memory allocation mask
 * @nr_vecs:	Number of integrity metadata scatter-gather elements
 *
 * Description: This function prepares a bio for attaching integrity
 * metadata.  nr_vecs specifies the maximum number of pages containing
 * integrity metadata that can be attached.
 */
struct bio_integrity_payload *bio_integrity_alloc(struct bio *bio,
						  gfp_t gfp_mask,
						  unsigned int nr_vecs)
{
	struct bio_integrity_payload *bip;
	struct bio_set *bs = bio->bi_pool;
	unsigned inline_vecs;

	if (!bs || !mempool_initialized(&bs->bio_integrity_pool)) {
		bip = kmalloc(sizeof(struct bio_integrity_payload) +
			      sizeof(struct bio_vec) * nr_vecs, gfp_mask);
		inline_vecs = nr_vecs;
	} else {
		bip = mempool_alloc(&bs->bio_integrity_pool, gfp_mask);
		inline_vecs = BIP_INLINE_VECS;
	}

	if (unlikely(!bip))
		return ERR_PTR(-ENOMEM);

	memset(bip, 0, sizeof(*bip));

	if (nr_vecs > inline_vecs) {
		unsigned long idx = 0;

		bip->bip_vec = bvec_alloc(gfp_mask, nr_vecs, &idx,
					  &bs->bvec_integrity_pool);
		if (!bip->bip_vec)
			goto err;
		bip->bip_max_vcnt = bvec_nr_vecs(idx);
		bip->bip_slab = idx;
	} else {
		bip->bip_vec = bip->bip_inline_vecs;
		bip->bip_max_vcnt = inline_vecs;
	}

	bip->bip_bio = bio;
	bio->bi_integrity = bip;
	bio->bi_opf |= REQ_INTEGRITY;

	return bip;
err:
	mempool_free(bip, &bs->bio_integrity_pool);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(bio_integrity_alloc);

/**
 * bio_integrity_free - Free bio integrity payload
 * @bio:	bio containing bip to be freed
 *
 * Description: Used to free the integrity portion of a bio. Usually
 * called from bio_free().
 */
static void bio_integrity_free(struct bio *bio)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct bio_set *bs = bio->bi_pool;

	if (bip->bip_flags & BIP_BLOCK_INTEGRITY)
		kfree(page_address(bip->bip_vec->bv_page) +
		      bip->bip_vec->bv_offset);

	if (bs && mempool_initialized(&bs->bio_integrity_pool)) {
		bvec_free(&bs->bvec_integrity_pool, bip->bip_vec, bip->bip_slab);

		mempool_free(bip, &bs->bio_integrity_pool);
	} else {
		kfree(bip);
	}

	bio->bi_integrity = NULL;
	bio->bi_opf &= ~REQ_INTEGRITY;
}

/**
 * bio_integrity_add_page - Attach integrity metadata
 * @bio:	bio to update
 * @page:	page containing integrity metadata
 * @len:	number of bytes of integrity metadata in page
 * @offset:	start offset within page
 *
 * Description: Attach a page containing integrity metadata to bio.
 */
int bio_integrity_add_page(struct bio *bio, struct page *page,
			   unsigned int len, unsigned int offset)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct bio_vec *iv;

	if (bip->bip_vcnt >= bip->bip_max_vcnt) {
		printk(KERN_ERR "%s: bip_vec full\n", __func__);
		return 0;
	}

	iv = bip->bip_vec + bip->bip_vcnt;

	if (bip->bip_vcnt &&
	    bvec_gap_to_prev(bio->bi_disk->queue,
			     &bip->bip_vec[bip->bip_vcnt - 1], offset))
		return 0;

	iv->bv_page = page;
	iv->bv_len = len;
	iv->bv_offset = offset;
	bip->bip_vcnt++;

	return len;
}
EXPORT_SYMBOL(bio_integrity_add_page);

/**
 * bio_integrity_process - Process integrity metadata for a bio
 * @bio:	bio to generate/verify integrity metadata for
 * @proc_iter:  iterator to process
 * @proc_fn:	Pointer to the relevant processing function
 */
static blk_status_t bio_integrity_process(struct bio *bio,
		struct bvec_iter *proc_iter, integrity_processing_fn *proc_fn)
{
	struct blk_integrity *bi = blk_get_integrity(bio->bi_disk);
	struct blk_integrity_iter iter;
	struct bvec_iter bviter;
	struct bio_vec bv;
	struct bio_integrity_payload *bip = bio_integrity(bio);
	blk_status_t ret = BLK_STS_OK;
	void *prot_buf = page_address(bip->bip_vec->bv_page) +
		bip->bip_vec->bv_offset;

	iter.disk_name = bio->bi_disk->disk_name;
	iter.interval = 1 << bi->interval_exp;
	iter.seed = proc_iter->bi_sector;
	iter.prot_buf = prot_buf;

	__bio_for_each_segment(bv, bio, bviter, *proc_iter) {
		void *kaddr = kmap_atomic(bv.bv_page);

		iter.data_buf = kaddr + bv.bv_offset;
		iter.data_size = bv.bv_len;

		ret = proc_fn(&iter);
		if (ret) {
			kunmap_atomic(kaddr);
			return ret;
		}

		kunmap_atomic(kaddr);
	}
	return ret;
}

/**
 * bio_integrity_prep - Prepare bio for integrity I/O
 * @bio:	bio to prepare
 *
 * Description:  Checks if the bio already has an integrity payload attached.
 * If it does, the payload has been generated by another kernel subsystem,
 * and we just pass it through. Otherwise allocates integrity payload.
 * The bio must have data direction, target device and start sector set priot
 * to calling.  In the WRITE case, integrity metadata will be generated using
 * the block device's integrity function.  In the READ case, the buffer
 * will be prepared for DMA and a suitable end_io handler set up.
 */
bool bio_integrity_prep(struct bio *bio)
{
	struct bio_integrity_payload *bip;
	struct blk_integrity *bi = blk_get_integrity(bio->bi_disk);
	struct request_queue *q = bio->bi_disk->queue;
	void *buf;
	unsigned long start, end;
	unsigned int len, nr_pages;
	unsigned int bytes, offset, i;
	unsigned int intervals;
	blk_status_t status;

	if (!bi)
		return true;

	if (bio_op(bio) != REQ_OP_READ && bio_op(bio) != REQ_OP_WRITE)
		return true;

	if (!bio_sectors(bio))
		return true;

	/* Already protected? */
	if (bio_integrity(bio))
		return true;

	if (bio_data_dir(bio) == READ) {
		if (!bi->profile->verify_fn ||
		    !(bi->flags & BLK_INTEGRITY_VERIFY))
			return true;
	} else {
		if (!bi->profile->generate_fn ||
		    !(bi->flags & BLK_INTEGRITY_GENERATE))
			return true;
	}
	intervals = bio_integrity_intervals(bi, bio_sectors(bio));

	/* Allocate kernel buffer for protection data */
	len = intervals * bi->tuple_size;
	buf = kmalloc(len, GFP_NOIO | q->bounce_gfp);
	status = BLK_STS_RESOURCE;
	if (unlikely(buf == NULL)) {
		printk(KERN_ERR "could not allocate integrity buffer\n");
		goto err_end_io;
	}

	end = (((unsigned long) buf) + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	start = ((unsigned long) buf) >> PAGE_SHIFT;
	nr_pages = end - start;

	/* Allocate bio integrity payload and integrity vectors */
	bip = bio_integrity_alloc(bio, GFP_NOIO, nr_pages);
	if (IS_ERR(bip)) {
		printk(KERN_ERR "could not allocate data integrity bioset\n");
		kfree(buf);
		status = BLK_STS_RESOURCE;
		goto err_end_io;
	}

	bip->bip_flags |= BIP_BLOCK_INTEGRITY;
	bip->bip_iter.bi_size = len;
	bip_set_seed(bip, bio->bi_iter.bi_sector);

	if (bi->flags & BLK_INTEGRITY_IP_CHECKSUM)
		bip->bip_flags |= BIP_IP_CHECKSUM;

	/* Map it */
	offset = offset_in_page(buf);
	for (i = 0 ; i < nr_pages ; i++) {
		int ret;
		bytes = PAGE_SIZE - offset;

		if (len <= 0)
			break;

		if (bytes > len)
			bytes = len;

		ret = bio_integrity_add_page(bio, virt_to_page(buf),
					     bytes, offset);

		if (ret == 0) {
			printk(KERN_ERR "could not attach integrity payload\n");
			kfree(buf);
			status = BLK_STS_RESOURCE;
			goto err_end_io;
		}

		if (ret < bytes)
			break;

		buf += bytes;
		len -= bytes;
		offset = 0;
	}

	/* Auto-generate integrity metadata if this is a write */
	if (bio_data_dir(bio) == WRITE) {
		bio_integrity_process(bio, &bio->bi_iter,
				      bi->profile->generate_fn);
	}
	return true;

err_end_io:
	bio->bi_status = status;
	bio_endio(bio);
	return false;

}
EXPORT_SYMBOL(bio_integrity_prep);

/**
 * bio_integrity_verify_fn - Integrity I/O completion worker
 * @work:	Work struct stored in bio to be verified
 *
 * Description: This workqueue function is called to complete a READ
 * request.  The function verifies the transferred integrity metadata
 * and then calls the original bio end_io function.
 */
static void bio_integrity_verify_fn(struct work_struct *work)
{
	struct bio_integrity_payload *bip =
		container_of(work, struct bio_integrity_payload, bip_work);
	struct bio *bio = bip->bip_bio;
	struct blk_integrity *bi = blk_get_integrity(bio->bi_disk);
	struct bvec_iter iter = bio->bi_iter;

	/*
	 * At the moment verify is called bio's iterator was advanced
	 * during split and completion, we need to rewind iterator to
	 * it's original position.
	 */
	if (bio_rewind_iter(bio, &iter, iter.bi_done)) {
		bio->bi_status = bio_integrity_process(bio, &iter,
						       bi->profile->verify_fn);
	} else {
		bio->bi_status = BLK_STS_IOERR;
	}

	bio_integrity_free(bio);
	bio_endio(bio);
}

/**
 * __bio_integrity_endio - Integrity I/O completion function
 * @bio:	Protected bio
 *
 * Description: Completion for integrity I/O
 *
 * Normally I/O completion is done in interrupt context.  However,
 * verifying I/O integrity is a time-consuming task which must be run
 * in process context.	This function postpones completion
 * accordingly.
 */
bool __bio_integrity_endio(struct bio *bio)
{
	struct blk_integrity *bi = blk_get_integrity(bio->bi_disk);
	struct bio_integrity_payload *bip = bio_integrity(bio);

	if (bio_op(bio) == REQ_OP_READ && !bio->bi_status &&
	    (bip->bip_flags & BIP_BLOCK_INTEGRITY) && bi->profile->verify_fn) {
		INIT_WORK(&bip->bip_work, bio_integrity_verify_fn);
		queue_work(kintegrityd_wq, &bip->bip_work);
		return false;
	}

	bio_integrity_free(bio);
	return true;
}

/**
 * bio_integrity_advance - Advance integrity vector
 * @bio:	bio whose integrity vector to update
 * @bytes_done:	number of data bytes that have been completed
 *
 * Description: This function calculates how many integrity bytes the
 * number of completed data bytes correspond to and advances the
 * integrity vector accordingly.
 */
void bio_integrity_advance(struct bio *bio, unsigned int bytes_done)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct blk_integrity *bi = blk_get_integrity(bio->bi_disk);
	unsigned bytes = bio_integrity_bytes(bi, bytes_done >> 9);

	bip->bip_iter.bi_sector += bytes_done >> 9;
	bvec_iter_advance(bip->bip_vec, &bip->bip_iter, bytes);
}
EXPORT_SYMBOL(bio_integrity_advance);

/**
 * bio_integrity_trim - Trim integrity vector
 * @bio:	bio whose integrity vector to update
 *
 * Description: Used to trim the integrity vector in a cloned bio.
 */
void bio_integrity_trim(struct bio *bio)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct blk_integrity *bi = blk_get_integrity(bio->bi_disk);

	bip->bip_iter.bi_size = bio_integrity_bytes(bi, bio_sectors(bio));
}
EXPORT_SYMBOL(bio_integrity_trim);

/**
 * bio_integrity_clone - Callback for cloning bios with integrity metadata
 * @bio:	New bio
 * @bio_src:	Original bio
 * @gfp_mask:	Memory allocation mask
 *
 * Description:	Called to allocate a bip when cloning a bio
 */
int bio_integrity_clone(struct bio *bio, struct bio *bio_src,
			gfp_t gfp_mask)
{
	struct bio_integrity_payload *bip_src = bio_integrity(bio_src);
	struct bio_integrity_payload *bip;

	BUG_ON(bip_src == NULL);

	bip = bio_integrity_alloc(bio, gfp_mask, bip_src->bip_vcnt);
	if (IS_ERR(bip))
		return PTR_ERR(bip);

	memcpy(bip->bip_vec, bip_src->bip_vec,
	       bip_src->bip_vcnt * sizeof(struct bio_vec));

	bip->bip_vcnt = bip_src->bip_vcnt;
	bip->bip_iter = bip_src->bip_iter;

	return 0;
}
EXPORT_SYMBOL(bio_integrity_clone);

int bioset_integrity_create(struct bio_set *bs, int pool_size)
{
	if (mempool_initialized(&bs->bio_integrity_pool))
		return 0;

	if (mempool_init_slab_pool(&bs->bio_integrity_pool,
				   pool_size, bip_slab))
		return -1;

	if (biovec_init_pool(&bs->bvec_integrity_pool, pool_size)) {
		mempool_exit(&bs->bio_integrity_pool);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(bioset_integrity_create);

void bioset_integrity_free(struct bio_set *bs)
{
	mempool_exit(&bs->bio_integrity_pool);
	mempool_exit(&bs->bvec_integrity_pool);
}
EXPORT_SYMBOL(bioset_integrity_free);

void __init bio_integrity_init(void)
{
	/*
	 * kintegrityd won't block much but may burn a lot of CPU cycles.
	 * Make it highpri CPU intensive wq with max concurrency of 1.
	 */
	kintegrityd_wq = alloc_workqueue("kintegrityd", WQ_MEM_RECLAIM |
					 WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!kintegrityd_wq)
		panic("Failed to create kintegrityd\n");

	bip_slab = kmem_cache_create("bio_integrity_payload",
				     sizeof(struct bio_integrity_payload) +
				     sizeof(struct bio_vec) * BIP_INLINE_VECS,
				     0, SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);
}
