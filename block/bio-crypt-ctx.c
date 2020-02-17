// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/keyslot-manager.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "blk-crypto-internal.h"

static int num_prealloc_crypt_ctxs = 128;

module_param(num_prealloc_crypt_ctxs, int, 0444);
MODULE_PARM_DESC(num_prealloc_crypt_ctxs,
		"Number of bio crypto contexts to preallocate");

static struct kmem_cache *bio_crypt_ctx_cache;
static mempool_t *bio_crypt_ctx_pool;

int __init bio_crypt_ctx_init(void)
{
	size_t i;

	bio_crypt_ctx_cache = KMEM_CACHE(bio_crypt_ctx, 0);
	if (!bio_crypt_ctx_cache)
		return -ENOMEM;

	bio_crypt_ctx_pool = mempool_create_slab_pool(num_prealloc_crypt_ctxs,
						      bio_crypt_ctx_cache);
	if (!bio_crypt_ctx_pool)
		return -ENOMEM;

	/* This is assumed in various places. */
	BUILD_BUG_ON(BLK_ENCRYPTION_MODE_INVALID != 0);

	/* Sanity check that no algorithm exceeds the defined limits. */
	for (i = 0; i < BLK_ENCRYPTION_MODE_MAX; i++) {
		BUG_ON(blk_crypto_modes[i].keysize > BLK_CRYPTO_MAX_KEY_SIZE);
		BUG_ON(blk_crypto_modes[i].ivsize > BLK_CRYPTO_MAX_IV_SIZE);
	}

	return 0;
}

struct bio_crypt_ctx *bio_crypt_alloc_ctx(gfp_t gfp_mask)
{
	return mempool_alloc(bio_crypt_ctx_pool, gfp_mask);
}
EXPORT_SYMBOL_GPL(bio_crypt_alloc_ctx);

void bio_crypt_free_ctx(struct bio *bio)
{
	mempool_free(bio->bi_crypt_context, bio_crypt_ctx_pool);
	bio->bi_crypt_context = NULL;
}

void bio_crypt_clone(struct bio *dst, struct bio *src, gfp_t gfp_mask)
{
	const struct bio_crypt_ctx *src_bc = src->bi_crypt_context;

	bio_clone_skip_dm_default_key(dst, src);

	/*
	 * If a bio is fallback_crypted, then it will be decrypted when
	 * bio_endio is called. As we only want the data to be decrypted once,
	 * copies of the bio must not have have a crypt context.
	 */
	if (!src_bc || bio_crypt_fallback_crypted(src_bc))
		return;

	dst->bi_crypt_context = bio_crypt_alloc_ctx(gfp_mask);
	*dst->bi_crypt_context = *src_bc;

	if (src_bc->bc_keyslot >= 0)
		keyslot_manager_get_slot(src_bc->bc_ksm, src_bc->bc_keyslot);
}
EXPORT_SYMBOL_GPL(bio_crypt_clone);

bool bio_crypt_should_process(struct request *rq)
{
	struct bio *bio = rq->bio;

	if (!bio || !bio->bi_crypt_context)
		return false;

	return rq->q->ksm == bio->bi_crypt_context->bc_ksm;
}
EXPORT_SYMBOL_GPL(bio_crypt_should_process);

/*
 * Checks that two bio crypt contexts are compatible - i.e. that
 * they are mergeable except for data_unit_num continuity.
 */
bool bio_crypt_ctx_compatible(struct bio *b_1, struct bio *b_2)
{
	struct bio_crypt_ctx *bc1 = b_1->bi_crypt_context;
	struct bio_crypt_ctx *bc2 = b_2->bi_crypt_context;

	if (!bc1)
		return !bc2;
	return bc2 && bc1->bc_key == bc2->bc_key;
}

/*
 * Checks that two bio crypt contexts are compatible, and also
 * that their data_unit_nums are continuous (and can hence be merged)
 * in the order b_1 followed by b_2.
 */
bool bio_crypt_ctx_mergeable(struct bio *b_1, unsigned int b1_bytes,
			     struct bio *b_2)
{
	struct bio_crypt_ctx *bc1 = b_1->bi_crypt_context;
	struct bio_crypt_ctx *bc2 = b_2->bi_crypt_context;

	if (!bio_crypt_ctx_compatible(b_1, b_2))
		return false;

	return !bc1 || bio_crypt_dun_is_contiguous(bc1, b1_bytes, bc2->bc_dun);
}

void bio_crypt_ctx_release_keyslot(struct bio_crypt_ctx *bc)
{
	keyslot_manager_put_slot(bc->bc_ksm, bc->bc_keyslot);
	bc->bc_ksm = NULL;
	bc->bc_keyslot = -1;
}

int bio_crypt_ctx_acquire_keyslot(struct bio_crypt_ctx *bc,
				  struct keyslot_manager *ksm)
{
	int slot = keyslot_manager_get_slot_for_key(ksm, bc->bc_key);

	if (slot < 0)
		return slot;

	bc->bc_keyslot = slot;
	bc->bc_ksm = ksm;
	return 0;
}
