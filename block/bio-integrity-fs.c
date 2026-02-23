// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Christoph Hellwig.
 */
#include <linux/blk-integrity.h>
#include <linux/bio-integrity.h>
#include "blk.h"

struct fs_bio_integrity_buf {
	struct bio_integrity_payload	bip;
	struct bio_vec			bvec;
};

static struct kmem_cache *fs_bio_integrity_cache;
static mempool_t fs_bio_integrity_pool;

unsigned int fs_bio_integrity_alloc(struct bio *bio)
{
	struct fs_bio_integrity_buf *iib;
	unsigned int action;

	action = bio_integrity_action(bio);
	if (!action)
		return 0;

	iib = mempool_alloc(&fs_bio_integrity_pool, GFP_NOIO);
	bio_integrity_init(bio, &iib->bip, &iib->bvec, 1);

	bio_integrity_alloc_buf(bio, action & BI_ACT_ZERO);
	if (action & BI_ACT_CHECK)
		bio_integrity_setup_default(bio);
	return action;
}

void fs_bio_integrity_free(struct bio *bio)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);

	bio_integrity_free_buf(bip);
	mempool_free(container_of(bip, struct fs_bio_integrity_buf, bip),
			&fs_bio_integrity_pool);

	bio->bi_integrity = NULL;
	bio->bi_opf &= ~REQ_INTEGRITY;
}

void fs_bio_integrity_generate(struct bio *bio)
{
	if (fs_bio_integrity_alloc(bio))
		bio_integrity_generate(bio);
}
EXPORT_SYMBOL_GPL(fs_bio_integrity_generate);

int fs_bio_integrity_verify(struct bio *bio, sector_t sector, unsigned int size)
{
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);
	struct bio_integrity_payload *bip = bio_integrity(bio);

	/*
	 * Reinitialize bip->bip_iter.
	 *
	 * This is for use in the submitter after the driver is done with the
	 * bio.  Requires the submitter to remember the sector and the size.
	 */
	memset(&bip->bip_iter, 0, sizeof(bip->bip_iter));
	bip->bip_iter.bi_sector = sector;
	bip->bip_iter.bi_size = bio_integrity_bytes(bi, size >> SECTOR_SHIFT);
	return blk_status_to_errno(bio_integrity_verify(bio, &bip->bip_iter));
}

static int __init fs_bio_integrity_init(void)
{
	fs_bio_integrity_cache = kmem_cache_create("fs_bio_integrity",
			sizeof(struct fs_bio_integrity_buf), 0,
			SLAB_HWCACHE_ALIGN | SLAB_PANIC, NULL);
	if (mempool_init_slab_pool(&fs_bio_integrity_pool, BIO_POOL_SIZE,
			fs_bio_integrity_cache))
		panic("fs_bio_integrity: can't create pool\n");
	return 0;
}
fs_initcall(fs_bio_integrity_init);
