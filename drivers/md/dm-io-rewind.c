// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 Red Hat, Inc.
 */

#include <linux/bio.h>
#include <linux/blk-crypto.h>
#include <linux/blk-integrity.h>

#include "dm-core.h"

static inline bool dm_bvec_iter_rewind(const struct bio_vec *bv,
				       struct bvec_iter *iter,
				       unsigned int bytes)
{
	int idx;

	iter->bi_size += bytes;
	if (bytes <= iter->bi_bvec_done) {
		iter->bi_bvec_done -= bytes;
		return true;
	}

	bytes -= iter->bi_bvec_done;
	idx = iter->bi_idx - 1;

	while (idx >= 0 && bytes && bytes > bv[idx].bv_len) {
		bytes -= bv[idx].bv_len;
		idx--;
	}

	if (WARN_ONCE(idx < 0 && bytes,
		      "Attempted to rewind iter beyond bvec's boundaries\n")) {
		iter->bi_size -= bytes;
		iter->bi_bvec_done = 0;
		iter->bi_idx = 0;
		return false;
	}

	iter->bi_idx = idx;
	iter->bi_bvec_done = bv[idx].bv_len - bytes;
	return true;
}

#if defined(CONFIG_BLK_DEV_INTEGRITY)

/**
 * dm_bio_integrity_rewind - Rewind integrity vector
 * @bio:	bio whose integrity vector to update
 * @bytes_done:	number of data bytes to rewind
 *
 * Description: This function calculates how many integrity bytes the
 * number of completed data bytes correspond to and rewind the
 * integrity vector accordingly.
 */
static void dm_bio_integrity_rewind(struct bio *bio, unsigned int bytes_done)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct blk_integrity *bi = blk_get_integrity(bio->bi_bdev->bd_disk);
	unsigned int bytes = bio_integrity_bytes(bi, bytes_done >> 9);

	bip->bip_iter.bi_sector -= bio_integrity_intervals(bi, bytes_done >> 9);
	dm_bvec_iter_rewind(bip->bip_vec, &bip->bip_iter, bytes);
}

#else /* CONFIG_BLK_DEV_INTEGRITY */

static inline void dm_bio_integrity_rewind(struct bio *bio,
					   unsigned int bytes_done)
{
}

#endif

#if defined(CONFIG_BLK_INLINE_ENCRYPTION)

/* Decrements @dun by @dec, treating @dun as a multi-limb integer. */
static void dm_bio_crypt_dun_decrement(u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE],
				       unsigned int dec)
{
	int i;

	for (i = 0; dec && i < BLK_CRYPTO_DUN_ARRAY_SIZE; i++) {
		u64 prev = dun[i];

		dun[i] -= dec;
		if (dun[i] > prev)
			dec = 1;
		else
			dec = 0;
	}
}

static void dm_bio_crypt_rewind(struct bio *bio, unsigned int bytes)
{
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;

	dm_bio_crypt_dun_decrement(bc->bc_dun,
				   bytes >> bc->bc_key->data_unit_size_bits);
}

#else /* CONFIG_BLK_INLINE_ENCRYPTION */

static inline void dm_bio_crypt_rewind(struct bio *bio, unsigned int bytes)
{
}

#endif

static inline void dm_bio_rewind_iter(const struct bio *bio,
				      struct bvec_iter *iter, unsigned int bytes)
{
	iter->bi_sector -= bytes >> 9;

	/* No advance means no rewind */
	if (bio_no_advance_iter(bio))
		iter->bi_size += bytes;
	else
		dm_bvec_iter_rewind(bio->bi_io_vec, iter, bytes);
}

/**
 * dm_bio_rewind - update ->bi_iter of @bio by rewinding @bytes.
 * @bio: bio to rewind
 * @bytes: how many bytes to rewind
 *
 * WARNING:
 * Caller must ensure that @bio has a fixed end sector, to allow
 * rewinding from end of bio and restoring its original position.
 * Caller is also responsibile for restoring bio's size.
 */
static void dm_bio_rewind(struct bio *bio, unsigned int bytes)
{
	if (bio_integrity(bio))
		dm_bio_integrity_rewind(bio, bytes);

	if (bio_has_crypt_ctx(bio))
		dm_bio_crypt_rewind(bio, bytes);

	dm_bio_rewind_iter(bio, &bio->bi_iter, bytes);
}

void dm_io_rewind(struct dm_io *io, struct bio_set *bs)
{
	struct bio *orig = io->orig_bio;
	struct bio *new_orig = bio_alloc_clone(orig->bi_bdev, orig,
					       GFP_NOIO, bs);
	/*
	 * dm_bio_rewind can restore to previous position since the
	 * end sector is fixed for original bio, but we still need
	 * to restore bio's size manually (using io->sectors).
	 */
	dm_bio_rewind(new_orig, ((io->sector_offset << 9) -
				 orig->bi_iter.bi_size));
	bio_trim(new_orig, 0, io->sectors);

	bio_chain(new_orig, orig);
	/*
	 * __bi_remaining was increased (by dm_split_and_process_bio),
	 * so must drop the one added in bio_chain.
	 */
	atomic_dec(&orig->__bi_remaining);
	io->orig_bio = new_orig;
}
