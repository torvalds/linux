// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007, 2008, 2009 Oracle Corporation
 * Written by: Martin K. Petersen <martin.petersen@oracle.com>
 *
 * Automatically generate and verify integrity data on PI capable devices if the
 * bio submitter didn't provide PI itself.  This ensures that kernel verifies
 * data integrity even if the file system (or other user of the block device) is
 * not aware of PI.
 */
#include <linux/blk-integrity.h>
#include <linux/t10-pi.h>
#include <linux/workqueue.h>
#include "blk.h"

struct bio_integrity_data {
	struct bio			*bio;
	struct bvec_iter		saved_bio_iter;
	struct work_struct		work;
	struct bio_integrity_payload	bip;
	struct bio_vec			bvec;
};

static struct kmem_cache *bid_slab;
static mempool_t bid_pool;
static struct workqueue_struct *kintegrityd_wq;

static void bio_integrity_finish(struct bio_integrity_data *bid)
{
	bid->bio->bi_integrity = NULL;
	bid->bio->bi_opf &= ~REQ_INTEGRITY;
	bio_integrity_free_buf(&bid->bip);
	mempool_free(bid, &bid_pool);
}

static void bio_integrity_verify_fn(struct work_struct *work)
{
	struct bio_integrity_data *bid =
		container_of(work, struct bio_integrity_data, work);
	struct bio *bio = bid->bio;

	bio->bi_status = bio_integrity_verify(bio, &bid->saved_bio_iter);
	bio_integrity_finish(bid);
	bio_endio(bio);
}

#define BIP_CHECK_FLAGS (BIP_CHECK_GUARD | BIP_CHECK_REFTAG | BIP_CHECK_APPTAG)
static bool bip_should_check(struct bio_integrity_payload *bip)
{
	return bip->bip_flags & BIP_CHECK_FLAGS;
}

/**
 * __bio_integrity_endio - Integrity I/O completion function
 * @bio:	Protected bio
 *
 * Normally I/O completion is done in interrupt context.  However, verifying I/O
 * integrity is a time-consuming task which must be run in process context.
 *
 * This function postpones completion accordingly.
 */
bool __bio_integrity_endio(struct bio *bio)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct bio_integrity_data *bid =
		container_of(bip, struct bio_integrity_data, bip);

	if (bio_op(bio) == REQ_OP_READ && !bio->bi_status &&
	    bip_should_check(bip)) {
		INIT_WORK(&bid->work, bio_integrity_verify_fn);
		queue_work(kintegrityd_wq, &bid->work);
		return false;
	}

	bio_integrity_finish(bid);
	return true;
}

/**
 * bio_integrity_prep - Prepare bio for integrity I/O
 * @bio:	bio to prepare
 * @action:	preparation action needed (BI_ACT_*)
 *
 * Allocate the integrity payload.  For writes, generate the integrity metadata
 * and for reads, setup the completion handler to verify the metadata.
 *
 * This is used for bios that do not have user integrity payloads attached.
 */
void bio_integrity_prep(struct bio *bio, unsigned int action)
{
	struct bio_integrity_data *bid;

	bid = mempool_alloc(&bid_pool, GFP_NOIO);
	bio_integrity_init(bio, &bid->bip, &bid->bvec, 1);
	bid->bio = bio;
	bid->bip.bip_flags |= BIP_BLOCK_INTEGRITY;
	bio_integrity_alloc_buf(bio, action & BI_ACT_ZERO);
	if (action & BI_ACT_CHECK)
		bio_integrity_setup_default(bio);

	/* Auto-generate integrity metadata if this is a write */
	if (bio_data_dir(bio) == WRITE && bip_should_check(&bid->bip))
		bio_integrity_generate(bio);
	else
		bid->saved_bio_iter = bio->bi_iter;
}
EXPORT_SYMBOL(bio_integrity_prep);

void blk_flush_integrity(void)
{
	flush_workqueue(kintegrityd_wq);
}

static int __init blk_integrity_auto_init(void)
{
	bid_slab = kmem_cache_create("bio_integrity_data",
			sizeof(struct bio_integrity_data), 0,
			SLAB_HWCACHE_ALIGN | SLAB_PANIC, NULL);

	if (mempool_init_slab_pool(&bid_pool, BIO_POOL_SIZE, bid_slab))
		panic("bio: can't create integrity pool\n");

	/*
	 * kintegrityd won't block much but may burn a lot of CPU cycles.
	 * Make it highpri CPU intensive wq with max concurrency of 1.
	 */
	kintegrityd_wq = alloc_workqueue("kintegrityd", WQ_MEM_RECLAIM |
					 WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!kintegrityd_wq)
		panic("Failed to create kintegrityd\n");
	return 0;
}
subsys_initcall(blk_integrity_auto_init);
