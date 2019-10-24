// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/keyslot-manager.h>

static int num_prealloc_crypt_ctxs = 128;
static struct kmem_cache *bio_crypt_ctx_cache;
static mempool_t *bio_crypt_ctx_pool;

int bio_crypt_ctx_init(void)
{
	bio_crypt_ctx_cache = KMEM_CACHE(bio_crypt_ctx, 0);
	if (!bio_crypt_ctx_cache)
		return -ENOMEM;

	bio_crypt_ctx_pool = mempool_create_slab_pool(
					num_prealloc_crypt_ctxs,
					bio_crypt_ctx_cache);

	if (!bio_crypt_ctx_pool)
		return -ENOMEM;

	return 0;
}

struct bio_crypt_ctx *bio_crypt_alloc_ctx(gfp_t gfp_mask)
{
	return mempool_alloc(bio_crypt_ctx_pool, gfp_mask);
}
EXPORT_SYMBOL(bio_crypt_alloc_ctx);

void bio_crypt_free_ctx(struct bio *bio)
{
	mempool_free(bio->bi_crypt_context, bio_crypt_ctx_pool);
	bio->bi_crypt_context = NULL;
}
EXPORT_SYMBOL(bio_crypt_free_ctx);

int bio_crypt_clone(struct bio *dst, struct bio *src, gfp_t gfp_mask)
{
	if (!bio_has_crypt_ctx(src))
		return 0;

	dst->bi_crypt_context = bio_crypt_alloc_ctx(gfp_mask);
	if (!dst->bi_crypt_context)
		return -ENOMEM;

	*dst->bi_crypt_context = *src->bi_crypt_context;

	if (bio_crypt_has_keyslot(src))
		keyslot_manager_get_slot(src->bi_crypt_context->processing_ksm,
					 src->bi_crypt_context->keyslot);

	return 0;
}
EXPORT_SYMBOL(bio_crypt_clone);

bool bio_crypt_should_process(struct bio *bio, struct request_queue *q)
{
	if (!bio_has_crypt_ctx(bio))
		return false;

	WARN_ON(!bio_crypt_has_keyslot(bio));
	return q->ksm == bio->bi_crypt_context->processing_ksm;
}
EXPORT_SYMBOL(bio_crypt_should_process);

/*
 * Checks that two bio crypt contexts are compatible - i.e. that
 * they are mergeable except for data_unit_num continuity.
 */
bool bio_crypt_ctx_compatible(struct bio *b_1, struct bio *b_2)
{
	struct bio_crypt_ctx *bc1 = b_1->bi_crypt_context;
	struct bio_crypt_ctx *bc2 = b_2->bi_crypt_context;

	if (bio_has_crypt_ctx(b_1) != bio_has_crypt_ctx(b_2))
		return false;

	if (!bio_has_crypt_ctx(b_1))
		return true;

	return bc1->keyslot == bc2->keyslot &&
	       bc1->data_unit_size_bits == bc2->data_unit_size_bits;
}

/*
 * Checks that two bio crypt contexts are compatible, and also
 * that their data_unit_nums are continuous (and can hence be merged)
 */
bool bio_crypt_ctx_back_mergeable(struct bio *b_1,
				  unsigned int b1_sectors,
				  struct bio *b_2)
{
	struct bio_crypt_ctx *bc1 = b_1->bi_crypt_context;
	struct bio_crypt_ctx *bc2 = b_2->bi_crypt_context;

	if (!bio_crypt_ctx_compatible(b_1, b_2))
		return false;

	return !bio_has_crypt_ctx(b_1) ||
		(bc1->data_unit_num +
		(b1_sectors >> (bc1->data_unit_size_bits - 9)) ==
		bc2->data_unit_num);
}

void bio_crypt_ctx_release_keyslot(struct bio *bio)
{
	struct bio_crypt_ctx *crypt_ctx = bio->bi_crypt_context;

	keyslot_manager_put_slot(crypt_ctx->processing_ksm, crypt_ctx->keyslot);
	bio->bi_crypt_context->processing_ksm = NULL;
	bio->bi_crypt_context->keyslot = -1;
}

int bio_crypt_ctx_acquire_keyslot(struct bio *bio, struct keyslot_manager *ksm)
{
	int slot;
	enum blk_crypto_mode_num crypto_mode = bio_crypto_mode(bio);

	if (!ksm)
		return -ENOMEM;

	slot = keyslot_manager_get_slot_for_key(ksm,
			bio_crypt_raw_key(bio), crypto_mode,
			1 << bio->bi_crypt_context->data_unit_size_bits);
	if (slot < 0)
		return slot;

	bio_crypt_set_keyslot(bio, slot, ksm);
	return 0;
}
