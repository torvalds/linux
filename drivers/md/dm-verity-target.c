// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Author: Mikulas Patocka <mpatocka@redhat.com>
 *
 * Based on Chromium dm-verity driver (C) 2011 The Chromium OS Authors
 *
 * In the file "/sys/module/dm_verity/parameters/prefetch_cluster" you can set
 * default prefetch value. Data are read in "prefetch_cluster" chunks from the
 * hash device. Setting this greatly improves performance when data and hash
 * are on the same disk on different partitions on devices with poor random
 * access behavior.
 */

#include "dm-verity.h"
#include "dm-verity-fec.h"
#include "dm-verity-verify-sig.h"
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/jump_label.h>

#define DM_MSG_PREFIX			"verity"

#define DM_VERITY_ENV_LENGTH		42
#define DM_VERITY_ENV_VAR_NAME		"DM_VERITY_ERR_BLOCK_NR"

#define DM_VERITY_DEFAULT_PREFETCH_SIZE	262144

#define DM_VERITY_MAX_CORRUPTED_ERRS	100

#define DM_VERITY_OPT_LOGGING		"ignore_corruption"
#define DM_VERITY_OPT_RESTART		"restart_on_corruption"
#define DM_VERITY_OPT_PANIC		"panic_on_corruption"
#define DM_VERITY_OPT_IGN_ZEROES	"ignore_zero_blocks"
#define DM_VERITY_OPT_AT_MOST_ONCE	"check_at_most_once"
#define DM_VERITY_OPT_TASKLET_VERIFY	"try_verify_in_tasklet"

#define DM_VERITY_OPTS_MAX		(4 + DM_VERITY_OPTS_FEC + \
					 DM_VERITY_ROOT_HASH_VERIFICATION_OPTS)

static unsigned dm_verity_prefetch_cluster = DM_VERITY_DEFAULT_PREFETCH_SIZE;

module_param_named(prefetch_cluster, dm_verity_prefetch_cluster, uint, S_IRUGO | S_IWUSR);

static DEFINE_STATIC_KEY_FALSE(use_tasklet_enabled);

struct dm_verity_prefetch_work {
	struct work_struct work;
	struct dm_verity *v;
	sector_t block;
	unsigned n_blocks;
};

/*
 * Auxiliary structure appended to each dm-bufio buffer. If the value
 * hash_verified is nonzero, hash of the block has been verified.
 *
 * The variable hash_verified is set to 0 when allocating the buffer, then
 * it can be changed to 1 and it is never reset to 0 again.
 *
 * There is no lock around this value, a race condition can at worst cause
 * that multiple processes verify the hash of the same buffer simultaneously
 * and write 1 to hash_verified simultaneously.
 * This condition is harmless, so we don't need locking.
 */
struct buffer_aux {
	int hash_verified;
};

/*
 * Initialize struct buffer_aux for a freshly created buffer.
 */
static void dm_bufio_alloc_callback(struct dm_buffer *buf)
{
	struct buffer_aux *aux = dm_bufio_get_aux_data(buf);

	aux->hash_verified = 0;
}

/*
 * Translate input sector number to the sector number on the target device.
 */
static sector_t verity_map_sector(struct dm_verity *v, sector_t bi_sector)
{
	return v->data_start + dm_target_offset(v->ti, bi_sector);
}

/*
 * Return hash position of a specified block at a specified tree level
 * (0 is the lowest level).
 * The lowest "hash_per_block_bits"-bits of the result denote hash position
 * inside a hash block. The remaining bits denote location of the hash block.
 */
static sector_t verity_position_at_level(struct dm_verity *v, sector_t block,
					 int level)
{
	return block >> (level * v->hash_per_block_bits);
}

static int verity_hash_update(struct dm_verity *v, struct ahash_request *req,
				const u8 *data, size_t len,
				struct crypto_wait *wait)
{
	struct scatterlist sg;

	if (likely(!is_vmalloc_addr(data))) {
		sg_init_one(&sg, data, len);
		ahash_request_set_crypt(req, &sg, NULL, len);
		return crypto_wait_req(crypto_ahash_update(req), wait);
	} else {
		do {
			int r;
			size_t this_step = min_t(size_t, len, PAGE_SIZE - offset_in_page(data));
			flush_kernel_vmap_range((void *)data, this_step);
			sg_init_table(&sg, 1);
			sg_set_page(&sg, vmalloc_to_page(data), this_step, offset_in_page(data));
			ahash_request_set_crypt(req, &sg, NULL, this_step);
			r = crypto_wait_req(crypto_ahash_update(req), wait);
			if (unlikely(r))
				return r;
			data += this_step;
			len -= this_step;
		} while (len);
		return 0;
	}
}

/*
 * Wrapper for crypto_ahash_init, which handles verity salting.
 */
static int verity_hash_init(struct dm_verity *v, struct ahash_request *req,
				struct crypto_wait *wait)
{
	int r;

	ahash_request_set_tfm(req, v->tfm);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP |
					CRYPTO_TFM_REQ_MAY_BACKLOG,
					crypto_req_done, (void *)wait);
	crypto_init_wait(wait);

	r = crypto_wait_req(crypto_ahash_init(req), wait);

	if (unlikely(r < 0)) {
		DMERR("crypto_ahash_init failed: %d", r);
		return r;
	}

	if (likely(v->salt_size && (v->version >= 1)))
		r = verity_hash_update(v, req, v->salt, v->salt_size, wait);

	return r;
}

static int verity_hash_final(struct dm_verity *v, struct ahash_request *req,
			     u8 *digest, struct crypto_wait *wait)
{
	int r;

	if (unlikely(v->salt_size && (!v->version))) {
		r = verity_hash_update(v, req, v->salt, v->salt_size, wait);

		if (r < 0) {
			DMERR("verity_hash_final failed updating salt: %d", r);
			goto out;
		}
	}

	ahash_request_set_crypt(req, NULL, digest, 0);
	r = crypto_wait_req(crypto_ahash_final(req), wait);
out:
	return r;
}

int verity_hash(struct dm_verity *v, struct ahash_request *req,
		const u8 *data, size_t len, u8 *digest)
{
	int r;
	struct crypto_wait wait;

	r = verity_hash_init(v, req, &wait);
	if (unlikely(r < 0))
		goto out;

	r = verity_hash_update(v, req, data, len, &wait);
	if (unlikely(r < 0))
		goto out;

	r = verity_hash_final(v, req, digest, &wait);

out:
	return r;
}

static void verity_hash_at_level(struct dm_verity *v, sector_t block, int level,
				 sector_t *hash_block, unsigned *offset)
{
	sector_t position = verity_position_at_level(v, block, level);
	unsigned idx;

	*hash_block = v->hash_level_block[level] + (position >> v->hash_per_block_bits);

	if (!offset)
		return;

	idx = position & ((1 << v->hash_per_block_bits) - 1);
	if (!v->version)
		*offset = idx * v->digest_size;
	else
		*offset = idx << (v->hash_dev_block_bits - v->hash_per_block_bits);
}

/*
 * Handle verification errors.
 */
static int verity_handle_err(struct dm_verity *v, enum verity_block_type type,
			     unsigned long long block)
{
	char verity_env[DM_VERITY_ENV_LENGTH];
	char *envp[] = { verity_env, NULL };
	const char *type_str = "";
	struct mapped_device *md = dm_table_get_md(v->ti->table);

	/* Corruption should be visible in device status in all modes */
	v->hash_failed = true;

	if (v->corrupted_errs >= DM_VERITY_MAX_CORRUPTED_ERRS)
		goto out;

	v->corrupted_errs++;

	switch (type) {
	case DM_VERITY_BLOCK_TYPE_DATA:
		type_str = "data";
		break;
	case DM_VERITY_BLOCK_TYPE_METADATA:
		type_str = "metadata";
		break;
	default:
		BUG();
	}

	DMERR_LIMIT("%s: %s block %llu is corrupted", v->data_dev->name,
		    type_str, block);

	if (v->corrupted_errs == DM_VERITY_MAX_CORRUPTED_ERRS)
		DMERR("%s: reached maximum errors", v->data_dev->name);

	snprintf(verity_env, DM_VERITY_ENV_LENGTH, "%s=%d,%llu",
		DM_VERITY_ENV_VAR_NAME, type, block);

	kobject_uevent_env(&disk_to_dev(dm_disk(md))->kobj, KOBJ_CHANGE, envp);

out:
	if (v->mode == DM_VERITY_MODE_LOGGING)
		return 0;

	if (v->mode == DM_VERITY_MODE_RESTART)
		kernel_restart("dm-verity device corrupted");

	if (v->mode == DM_VERITY_MODE_PANIC)
		panic("dm-verity device corrupted");

	return 1;
}

/*
 * Verify hash of a metadata block pertaining to the specified data block
 * ("block" argument) at a specified level ("level" argument).
 *
 * On successful return, verity_io_want_digest(v, io) contains the hash value
 * for a lower tree level or for the data block (if we're at the lowest level).
 *
 * If "skip_unverified" is true, unverified buffer is skipped and 1 is returned.
 * If "skip_unverified" is false, unverified buffer is hashed and verified
 * against current value of verity_io_want_digest(v, io).
 */
static int verity_verify_level(struct dm_verity *v, struct dm_verity_io *io,
			       sector_t block, int level, bool skip_unverified,
			       u8 *want_digest)
{
	struct dm_buffer *buf;
	struct buffer_aux *aux;
	u8 *data;
	int r;
	sector_t hash_block;
	unsigned offset;

	verity_hash_at_level(v, block, level, &hash_block, &offset);

	if (static_branch_unlikely(&use_tasklet_enabled) && io->in_tasklet) {
		data = dm_bufio_get(v->bufio, hash_block, &buf);
		if (data == NULL) {
			/*
			 * In tasklet and the hash was not in the bufio cache.
			 * Return early and resume execution from a work-queue
			 * to read the hash from disk.
			 */
			return -EAGAIN;
		}
	} else
		data = dm_bufio_read(v->bufio, hash_block, &buf);

	if (IS_ERR(data))
		return PTR_ERR(data);

	aux = dm_bufio_get_aux_data(buf);

	if (!aux->hash_verified) {
		if (skip_unverified) {
			r = 1;
			goto release_ret_r;
		}

		r = verity_hash(v, verity_io_hash_req(v, io),
				data, 1 << v->hash_dev_block_bits,
				verity_io_real_digest(v, io));
		if (unlikely(r < 0))
			goto release_ret_r;

		if (likely(memcmp(verity_io_real_digest(v, io), want_digest,
				  v->digest_size) == 0))
			aux->hash_verified = 1;
		else if (static_branch_unlikely(&use_tasklet_enabled) &&
			 io->in_tasklet) {
			/*
			 * Error handling code (FEC included) cannot be run in a
			 * tasklet since it may sleep, so fallback to work-queue.
			 */
			r = -EAGAIN;
			goto release_ret_r;
		}
		else if (verity_fec_decode(v, io,
					   DM_VERITY_BLOCK_TYPE_METADATA,
					   hash_block, data, NULL) == 0)
			aux->hash_verified = 1;
		else if (verity_handle_err(v,
					   DM_VERITY_BLOCK_TYPE_METADATA,
					   hash_block)) {
			r = -EIO;
			goto release_ret_r;
		}
	}

	data += offset;
	memcpy(want_digest, data, v->digest_size);
	r = 0;

release_ret_r:
	dm_bufio_release(buf);
	return r;
}

/*
 * Find a hash for a given block, write it to digest and verify the integrity
 * of the hash tree if necessary.
 */
int verity_hash_for_block(struct dm_verity *v, struct dm_verity_io *io,
			  sector_t block, u8 *digest, bool *is_zero)
{
	int r = 0, i;

	if (likely(v->levels)) {
		/*
		 * First, we try to get the requested hash for
		 * the current block. If the hash block itself is
		 * verified, zero is returned. If it isn't, this
		 * function returns 1 and we fall back to whole
		 * chain verification.
		 */
		r = verity_verify_level(v, io, block, 0, true, digest);
		if (likely(r <= 0))
			goto out;
	}

	memcpy(digest, v->root_digest, v->digest_size);

	for (i = v->levels - 1; i >= 0; i--) {
		r = verity_verify_level(v, io, block, i, false, digest);
		if (unlikely(r))
			goto out;
	}
out:
	if (!r && v->zero_digest)
		*is_zero = !memcmp(v->zero_digest, digest, v->digest_size);
	else
		*is_zero = false;

	return r;
}

/*
 * Calculates the digest for the given bio
 */
static int verity_for_io_block(struct dm_verity *v, struct dm_verity_io *io,
			       struct bvec_iter *iter, struct crypto_wait *wait)
{
	unsigned int todo = 1 << v->data_dev_block_bits;
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);
	struct scatterlist sg;
	struct ahash_request *req = verity_io_hash_req(v, io);

	do {
		int r;
		unsigned int len;
		struct bio_vec bv = bio_iter_iovec(bio, *iter);

		sg_init_table(&sg, 1);

		len = bv.bv_len;

		if (likely(len >= todo))
			len = todo;
		/*
		 * Operating on a single page at a time looks suboptimal
		 * until you consider the typical block size is 4,096B.
		 * Going through this loops twice should be very rare.
		 */
		sg_set_page(&sg, bv.bv_page, len, bv.bv_offset);
		ahash_request_set_crypt(req, &sg, NULL, len);
		r = crypto_wait_req(crypto_ahash_update(req), wait);

		if (unlikely(r < 0)) {
			DMERR("verity_for_io_block crypto op failed: %d", r);
			return r;
		}

		bio_advance_iter(bio, iter, len);
		todo -= len;
	} while (todo);

	return 0;
}

/*
 * Calls function process for 1 << v->data_dev_block_bits bytes in the bio_vec
 * starting from iter.
 */
int verity_for_bv_block(struct dm_verity *v, struct dm_verity_io *io,
			struct bvec_iter *iter,
			int (*process)(struct dm_verity *v,
				       struct dm_verity_io *io, u8 *data,
				       size_t len))
{
	unsigned todo = 1 << v->data_dev_block_bits;
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);

	do {
		int r;
		u8 *page;
		unsigned len;
		struct bio_vec bv = bio_iter_iovec(bio, *iter);

		page = bvec_kmap_local(&bv);
		len = bv.bv_len;

		if (likely(len >= todo))
			len = todo;

		r = process(v, io, page, len);
		kunmap_local(page);

		if (r < 0)
			return r;

		bio_advance_iter(bio, iter, len);
		todo -= len;
	} while (todo);

	return 0;
}

static int verity_bv_zero(struct dm_verity *v, struct dm_verity_io *io,
			  u8 *data, size_t len)
{
	memset(data, 0, len);
	return 0;
}

/*
 * Moves the bio iter one data block forward.
 */
static inline void verity_bv_skip_block(struct dm_verity *v,
					struct dm_verity_io *io,
					struct bvec_iter *iter)
{
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);

	bio_advance_iter(bio, iter, 1 << v->data_dev_block_bits);
}

/*
 * Verify one "dm_verity_io" structure.
 */
static int verity_verify_io(struct dm_verity_io *io)
{
	bool is_zero;
	struct dm_verity *v = io->v;
#if defined(CONFIG_DM_VERITY_FEC)
	struct bvec_iter start;
#endif
	struct bvec_iter iter_copy;
	struct bvec_iter *iter;
	struct crypto_wait wait;
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);
	unsigned int b;

	if (static_branch_unlikely(&use_tasklet_enabled) && io->in_tasklet) {
		/*
		 * Copy the iterator in case we need to restart
		 * verification in a work-queue.
		 */
		iter_copy = io->iter;
		iter = &iter_copy;
	} else
		iter = &io->iter;

	for (b = 0; b < io->n_blocks; b++) {
		int r;
		sector_t cur_block = io->block + b;
		struct ahash_request *req = verity_io_hash_req(v, io);

		if (v->validated_blocks &&
		    likely(test_bit(cur_block, v->validated_blocks))) {
			verity_bv_skip_block(v, io, iter);
			continue;
		}

		r = verity_hash_for_block(v, io, cur_block,
					  verity_io_want_digest(v, io),
					  &is_zero);
		if (unlikely(r < 0))
			return r;

		if (is_zero) {
			/*
			 * If we expect a zero block, don't validate, just
			 * return zeros.
			 */
			r = verity_for_bv_block(v, io, iter,
						verity_bv_zero);
			if (unlikely(r < 0))
				return r;

			continue;
		}

		r = verity_hash_init(v, req, &wait);
		if (unlikely(r < 0))
			return r;

#if defined(CONFIG_DM_VERITY_FEC)
		if (verity_fec_is_enabled(v))
			start = *iter;
#endif
		r = verity_for_io_block(v, io, iter, &wait);
		if (unlikely(r < 0))
			return r;

		r = verity_hash_final(v, req, verity_io_real_digest(v, io),
					&wait);
		if (unlikely(r < 0))
			return r;

		if (likely(memcmp(verity_io_real_digest(v, io),
				  verity_io_want_digest(v, io), v->digest_size) == 0)) {
			if (v->validated_blocks)
				set_bit(cur_block, v->validated_blocks);
			continue;
		} else if (static_branch_unlikely(&use_tasklet_enabled) &&
			   io->in_tasklet) {
			/*
			 * Error handling code (FEC included) cannot be run in a
			 * tasklet since it may sleep, so fallback to work-queue.
			 */
			return -EAGAIN;
#if defined(CONFIG_DM_VERITY_FEC)
		} else if (verity_fec_decode(v, io, DM_VERITY_BLOCK_TYPE_DATA,
					     cur_block, NULL, &start) == 0) {
			continue;
#endif
		} else {
			if (bio->bi_status) {
				/*
				 * Error correction failed; Just return error
				 */
				return -EIO;
			}
			if (verity_handle_err(v, DM_VERITY_BLOCK_TYPE_DATA,
					      cur_block))
				return -EIO;
		}
	}

	return 0;
}

/*
 * Skip verity work in response to I/O error when system is shutting down.
 */
static inline bool verity_is_system_shutting_down(void)
{
	return system_state == SYSTEM_HALT || system_state == SYSTEM_POWER_OFF
		|| system_state == SYSTEM_RESTART;
}

/*
 * End one "io" structure with a given error.
 */
static void verity_finish_io(struct dm_verity_io *io, blk_status_t status)
{
	struct dm_verity *v = io->v;
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);

	bio->bi_end_io = io->orig_bi_end_io;
	bio->bi_status = status;

	if (!static_branch_unlikely(&use_tasklet_enabled) || !io->in_tasklet)
		verity_fec_finish_io(io);

	bio_endio(bio);
}

static void verity_work(struct work_struct *w)
{
	struct dm_verity_io *io = container_of(w, struct dm_verity_io, work);

	io->in_tasklet = false;

	verity_fec_init_io(io);
	verity_finish_io(io, errno_to_blk_status(verity_verify_io(io)));
}

static void verity_tasklet(unsigned long data)
{
	struct dm_verity_io *io = (struct dm_verity_io *)data;
	int err;

	io->in_tasklet = true;
	err = verity_verify_io(io);
	if (err == -EAGAIN) {
		/* fallback to retrying with work-queue */
		INIT_WORK(&io->work, verity_work);
		queue_work(io->v->verify_wq, &io->work);
		return;
	}

	verity_finish_io(io, errno_to_blk_status(err));
}

static void verity_end_io(struct bio *bio)
{
	struct dm_verity_io *io = bio->bi_private;

	if (bio->bi_status &&
	    (!verity_fec_is_enabled(io->v) || verity_is_system_shutting_down())) {
		verity_finish_io(io, bio->bi_status);
		return;
	}

	if (static_branch_unlikely(&use_tasklet_enabled) && io->v->use_tasklet) {
		tasklet_init(&io->tasklet, verity_tasklet, (unsigned long)io);
		tasklet_schedule(&io->tasklet);
	} else {
		INIT_WORK(&io->work, verity_work);
		queue_work(io->v->verify_wq, &io->work);
	}
}

/*
 * Prefetch buffers for the specified io.
 * The root buffer is not prefetched, it is assumed that it will be cached
 * all the time.
 */
static void verity_prefetch_io(struct work_struct *work)
{
	struct dm_verity_prefetch_work *pw =
		container_of(work, struct dm_verity_prefetch_work, work);
	struct dm_verity *v = pw->v;
	int i;

	for (i = v->levels - 2; i >= 0; i--) {
		sector_t hash_block_start;
		sector_t hash_block_end;
		verity_hash_at_level(v, pw->block, i, &hash_block_start, NULL);
		verity_hash_at_level(v, pw->block + pw->n_blocks - 1, i, &hash_block_end, NULL);
		if (!i) {
			unsigned cluster = READ_ONCE(dm_verity_prefetch_cluster);

			cluster >>= v->data_dev_block_bits;
			if (unlikely(!cluster))
				goto no_prefetch_cluster;

			if (unlikely(cluster & (cluster - 1)))
				cluster = 1 << __fls(cluster);

			hash_block_start &= ~(sector_t)(cluster - 1);
			hash_block_end |= cluster - 1;
			if (unlikely(hash_block_end >= v->hash_blocks))
				hash_block_end = v->hash_blocks - 1;
		}
no_prefetch_cluster:
		dm_bufio_prefetch(v->bufio, hash_block_start,
				  hash_block_end - hash_block_start + 1);
	}

	kfree(pw);
}

static void verity_submit_prefetch(struct dm_verity *v, struct dm_verity_io *io)
{
	sector_t block = io->block;
	unsigned int n_blocks = io->n_blocks;
	struct dm_verity_prefetch_work *pw;

	if (v->validated_blocks) {
		while (n_blocks && test_bit(block, v->validated_blocks)) {
			block++;
			n_blocks--;
		}
		while (n_blocks && test_bit(block + n_blocks - 1,
					    v->validated_blocks))
			n_blocks--;
		if (!n_blocks)
			return;
	}

	pw = kmalloc(sizeof(struct dm_verity_prefetch_work),
		GFP_NOIO | __GFP_NORETRY | __GFP_NOMEMALLOC | __GFP_NOWARN);

	if (!pw)
		return;

	INIT_WORK(&pw->work, verity_prefetch_io);
	pw->v = v;
	pw->block = block;
	pw->n_blocks = n_blocks;
	queue_work(v->verify_wq, &pw->work);
}

/*
 * Bio map function. It allocates dm_verity_io structure and bio vector and
 * fills them. Then it issues prefetches and the I/O.
 */
static int verity_map(struct dm_target *ti, struct bio *bio)
{
	struct dm_verity *v = ti->private;
	struct dm_verity_io *io;

	bio_set_dev(bio, v->data_dev->bdev);
	bio->bi_iter.bi_sector = verity_map_sector(v, bio->bi_iter.bi_sector);

	if (((unsigned)bio->bi_iter.bi_sector | bio_sectors(bio)) &
	    ((1 << (v->data_dev_block_bits - SECTOR_SHIFT)) - 1)) {
		DMERR_LIMIT("unaligned io");
		return DM_MAPIO_KILL;
	}

	if (bio_end_sector(bio) >>
	    (v->data_dev_block_bits - SECTOR_SHIFT) > v->data_blocks) {
		DMERR_LIMIT("io out of range");
		return DM_MAPIO_KILL;
	}

	if (bio_data_dir(bio) == WRITE)
		return DM_MAPIO_KILL;

	io = dm_per_bio_data(bio, ti->per_io_data_size);
	io->v = v;
	io->orig_bi_end_io = bio->bi_end_io;
	io->block = bio->bi_iter.bi_sector >> (v->data_dev_block_bits - SECTOR_SHIFT);
	io->n_blocks = bio->bi_iter.bi_size >> v->data_dev_block_bits;

	bio->bi_end_io = verity_end_io;
	bio->bi_private = io;
	io->iter = bio->bi_iter;

	verity_submit_prefetch(v, io);

	submit_bio_noacct(bio);

	return DM_MAPIO_SUBMITTED;
}

/*
 * Status: V (valid) or C (corruption found)
 */
static void verity_status(struct dm_target *ti, status_type_t type,
			  unsigned status_flags, char *result, unsigned maxlen)
{
	struct dm_verity *v = ti->private;
	unsigned args = 0;
	unsigned sz = 0;
	unsigned x;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%c", v->hash_failed ? 'C' : 'V');
		break;
	case STATUSTYPE_TABLE:
		DMEMIT("%u %s %s %u %u %llu %llu %s ",
			v->version,
			v->data_dev->name,
			v->hash_dev->name,
			1 << v->data_dev_block_bits,
			1 << v->hash_dev_block_bits,
			(unsigned long long)v->data_blocks,
			(unsigned long long)v->hash_start,
			v->alg_name
			);
		for (x = 0; x < v->digest_size; x++)
			DMEMIT("%02x", v->root_digest[x]);
		DMEMIT(" ");
		if (!v->salt_size)
			DMEMIT("-");
		else
			for (x = 0; x < v->salt_size; x++)
				DMEMIT("%02x", v->salt[x]);
		if (v->mode != DM_VERITY_MODE_EIO)
			args++;
		if (verity_fec_is_enabled(v))
			args += DM_VERITY_OPTS_FEC;
		if (v->zero_digest)
			args++;
		if (v->validated_blocks)
			args++;
		if (v->use_tasklet)
			args++;
		if (v->signature_key_desc)
			args += DM_VERITY_ROOT_HASH_VERIFICATION_OPTS;
		if (!args)
			return;
		DMEMIT(" %u", args);
		if (v->mode != DM_VERITY_MODE_EIO) {
			DMEMIT(" ");
			switch (v->mode) {
			case DM_VERITY_MODE_LOGGING:
				DMEMIT(DM_VERITY_OPT_LOGGING);
				break;
			case DM_VERITY_MODE_RESTART:
				DMEMIT(DM_VERITY_OPT_RESTART);
				break;
			case DM_VERITY_MODE_PANIC:
				DMEMIT(DM_VERITY_OPT_PANIC);
				break;
			default:
				BUG();
			}
		}
		if (v->zero_digest)
			DMEMIT(" " DM_VERITY_OPT_IGN_ZEROES);
		if (v->validated_blocks)
			DMEMIT(" " DM_VERITY_OPT_AT_MOST_ONCE);
		if (v->use_tasklet)
			DMEMIT(" " DM_VERITY_OPT_TASKLET_VERIFY);
		sz = verity_fec_status_table(v, sz, result, maxlen);
		if (v->signature_key_desc)
			DMEMIT(" " DM_VERITY_ROOT_HASH_VERIFICATION_OPT_SIG_KEY
				" %s", v->signature_key_desc);
		break;

	case STATUSTYPE_IMA:
		DMEMIT_TARGET_NAME_VERSION(ti->type);
		DMEMIT(",hash_failed=%c", v->hash_failed ? 'C' : 'V');
		DMEMIT(",verity_version=%u", v->version);
		DMEMIT(",data_device_name=%s", v->data_dev->name);
		DMEMIT(",hash_device_name=%s", v->hash_dev->name);
		DMEMIT(",verity_algorithm=%s", v->alg_name);

		DMEMIT(",root_digest=");
		for (x = 0; x < v->digest_size; x++)
			DMEMIT("%02x", v->root_digest[x]);

		DMEMIT(",salt=");
		if (!v->salt_size)
			DMEMIT("-");
		else
			for (x = 0; x < v->salt_size; x++)
				DMEMIT("%02x", v->salt[x]);

		DMEMIT(",ignore_zero_blocks=%c", v->zero_digest ? 'y' : 'n');
		DMEMIT(",check_at_most_once=%c", v->validated_blocks ? 'y' : 'n');
		if (v->signature_key_desc)
			DMEMIT(",root_hash_sig_key_desc=%s", v->signature_key_desc);

		if (v->mode != DM_VERITY_MODE_EIO) {
			DMEMIT(",verity_mode=");
			switch (v->mode) {
			case DM_VERITY_MODE_LOGGING:
				DMEMIT(DM_VERITY_OPT_LOGGING);
				break;
			case DM_VERITY_MODE_RESTART:
				DMEMIT(DM_VERITY_OPT_RESTART);
				break;
			case DM_VERITY_MODE_PANIC:
				DMEMIT(DM_VERITY_OPT_PANIC);
				break;
			default:
				DMEMIT("invalid");
			}
		}
		DMEMIT(";");
		break;
	}
}

static int verity_prepare_ioctl(struct dm_target *ti, struct block_device **bdev)
{
	struct dm_verity *v = ti->private;

	*bdev = v->data_dev->bdev;

	if (v->data_start || ti->len != bdev_nr_sectors(v->data_dev->bdev))
		return 1;
	return 0;
}

static int verity_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct dm_verity *v = ti->private;

	return fn(ti, v->data_dev, v->data_start, ti->len, data);
}

static void verity_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct dm_verity *v = ti->private;

	if (limits->logical_block_size < 1 << v->data_dev_block_bits)
		limits->logical_block_size = 1 << v->data_dev_block_bits;

	if (limits->physical_block_size < 1 << v->data_dev_block_bits)
		limits->physical_block_size = 1 << v->data_dev_block_bits;

	blk_limits_io_min(limits, limits->logical_block_size);
}

static void verity_dtr(struct dm_target *ti)
{
	struct dm_verity *v = ti->private;

	if (v->verify_wq)
		destroy_workqueue(v->verify_wq);

	if (v->bufio)
		dm_bufio_client_destroy(v->bufio);

	kvfree(v->validated_blocks);
	kfree(v->salt);
	kfree(v->root_digest);
	kfree(v->zero_digest);

	if (v->tfm)
		crypto_free_ahash(v->tfm);

	kfree(v->alg_name);

	if (v->hash_dev)
		dm_put_device(ti, v->hash_dev);

	if (v->data_dev)
		dm_put_device(ti, v->data_dev);

	verity_fec_dtr(v);

	kfree(v->signature_key_desc);

	if (v->use_tasklet)
		static_branch_dec(&use_tasklet_enabled);

	kfree(v);
}

static int verity_alloc_most_once(struct dm_verity *v)
{
	struct dm_target *ti = v->ti;

	/* the bitset can only handle INT_MAX blocks */
	if (v->data_blocks > INT_MAX) {
		ti->error = "device too large to use check_at_most_once";
		return -E2BIG;
	}

	v->validated_blocks = kvcalloc(BITS_TO_LONGS(v->data_blocks),
				       sizeof(unsigned long),
				       GFP_KERNEL);
	if (!v->validated_blocks) {
		ti->error = "failed to allocate bitset for check_at_most_once";
		return -ENOMEM;
	}

	return 0;
}

static int verity_alloc_zero_digest(struct dm_verity *v)
{
	int r = -ENOMEM;
	struct ahash_request *req;
	u8 *zero_data;

	v->zero_digest = kmalloc(v->digest_size, GFP_KERNEL);

	if (!v->zero_digest)
		return r;

	req = kmalloc(v->ahash_reqsize, GFP_KERNEL);

	if (!req)
		return r; /* verity_dtr will free zero_digest */

	zero_data = kzalloc(1 << v->data_dev_block_bits, GFP_KERNEL);

	if (!zero_data)
		goto out;

	r = verity_hash(v, req, zero_data, 1 << v->data_dev_block_bits,
			v->zero_digest);

out:
	kfree(req);
	kfree(zero_data);

	return r;
}

static inline bool verity_is_verity_mode(const char *arg_name)
{
	return (!strcasecmp(arg_name, DM_VERITY_OPT_LOGGING) ||
		!strcasecmp(arg_name, DM_VERITY_OPT_RESTART) ||
		!strcasecmp(arg_name, DM_VERITY_OPT_PANIC));
}

static int verity_parse_verity_mode(struct dm_verity *v, const char *arg_name)
{
	if (v->mode)
		return -EINVAL;

	if (!strcasecmp(arg_name, DM_VERITY_OPT_LOGGING))
		v->mode = DM_VERITY_MODE_LOGGING;
	else if (!strcasecmp(arg_name, DM_VERITY_OPT_RESTART))
		v->mode = DM_VERITY_MODE_RESTART;
	else if (!strcasecmp(arg_name, DM_VERITY_OPT_PANIC))
		v->mode = DM_VERITY_MODE_PANIC;

	return 0;
}

static int verity_parse_opt_args(struct dm_arg_set *as, struct dm_verity *v,
				 struct dm_verity_sig_opts *verify_args,
				 bool only_modifier_opts)
{
	int r = 0;
	unsigned argc;
	struct dm_target *ti = v->ti;
	const char *arg_name;

	static const struct dm_arg _args[] = {
		{0, DM_VERITY_OPTS_MAX, "Invalid number of feature args"},
	};

	r = dm_read_arg_group(_args, as, &argc, &ti->error);
	if (r)
		return -EINVAL;

	if (!argc)
		return 0;

	do {
		arg_name = dm_shift_arg(as);
		argc--;

		if (verity_is_verity_mode(arg_name)) {
			if (only_modifier_opts)
				continue;
			r = verity_parse_verity_mode(v, arg_name);
			if (r) {
				ti->error = "Conflicting error handling parameters";
				return r;
			}
			continue;

		} else if (!strcasecmp(arg_name, DM_VERITY_OPT_IGN_ZEROES)) {
			if (only_modifier_opts)
				continue;
			r = verity_alloc_zero_digest(v);
			if (r) {
				ti->error = "Cannot allocate zero digest";
				return r;
			}
			continue;

		} else if (!strcasecmp(arg_name, DM_VERITY_OPT_AT_MOST_ONCE)) {
			if (only_modifier_opts)
				continue;
			r = verity_alloc_most_once(v);
			if (r)
				return r;
			continue;

		} else if (!strcasecmp(arg_name, DM_VERITY_OPT_TASKLET_VERIFY)) {
			v->use_tasklet = true;
			static_branch_inc(&use_tasklet_enabled);
			continue;

		} else if (verity_is_fec_opt_arg(arg_name)) {
			if (only_modifier_opts)
				continue;
			r = verity_fec_parse_opt_args(as, v, &argc, arg_name);
			if (r)
				return r;
			continue;

		} else if (verity_verify_is_sig_opt_arg(arg_name)) {
			if (only_modifier_opts)
				continue;
			r = verity_verify_sig_parse_opt_args(as, v,
							     verify_args,
							     &argc, arg_name);
			if (r)
				return r;
			continue;

		} else if (only_modifier_opts) {
			/*
			 * Ignore unrecognized opt, could easily be an extra
			 * argument to an option whose parsing was skipped.
			 * Normal parsing (@only_modifier_opts=false) will
			 * properly parse all options (and their extra args).
			 */
			continue;
		}

		DMERR("Unrecognized verity feature request: %s", arg_name);
		ti->error = "Unrecognized verity feature request";
		return -EINVAL;
	} while (argc && !r);

	return r;
}

/*
 * Target parameters:
 *	<version>	The current format is version 1.
 *			Vsn 0 is compatible with original Chromium OS releases.
 *	<data device>
 *	<hash device>
 *	<data block size>
 *	<hash block size>
 *	<the number of data blocks>
 *	<hash start block>
 *	<algorithm>
 *	<digest>
 *	<salt>		Hex string or "-" if no salt.
 */
static int verity_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
	struct dm_verity *v;
	struct dm_verity_sig_opts verify_args = {0};
	struct dm_arg_set as;
	unsigned int num;
	unsigned int wq_flags;
	unsigned long long num_ll;
	int r;
	int i;
	sector_t hash_position;
	char dummy;
	char *root_hash_digest_to_validate;

	v = kzalloc(sizeof(struct dm_verity), GFP_KERNEL);
	if (!v) {
		ti->error = "Cannot allocate verity structure";
		return -ENOMEM;
	}
	ti->private = v;
	v->ti = ti;

	r = verity_fec_ctr_alloc(v);
	if (r)
		goto bad;

	if ((dm_table_get_mode(ti->table) & ~FMODE_READ)) {
		ti->error = "Device must be readonly";
		r = -EINVAL;
		goto bad;
	}

	if (argc < 10) {
		ti->error = "Not enough arguments";
		r = -EINVAL;
		goto bad;
	}

	/* Parse optional parameters that modify primary args */
	if (argc > 10) {
		as.argc = argc - 10;
		as.argv = argv + 10;
		r = verity_parse_opt_args(&as, v, &verify_args, true);
		if (r < 0)
			goto bad;
	}

	if (sscanf(argv[0], "%u%c", &num, &dummy) != 1 ||
	    num > 1) {
		ti->error = "Invalid version";
		r = -EINVAL;
		goto bad;
	}
	v->version = num;

	r = dm_get_device(ti, argv[1], FMODE_READ, &v->data_dev);
	if (r) {
		ti->error = "Data device lookup failed";
		goto bad;
	}

	r = dm_get_device(ti, argv[2], FMODE_READ, &v->hash_dev);
	if (r) {
		ti->error = "Hash device lookup failed";
		goto bad;
	}

	if (sscanf(argv[3], "%u%c", &num, &dummy) != 1 ||
	    !num || (num & (num - 1)) ||
	    num < bdev_logical_block_size(v->data_dev->bdev) ||
	    num > PAGE_SIZE) {
		ti->error = "Invalid data device block size";
		r = -EINVAL;
		goto bad;
	}
	v->data_dev_block_bits = __ffs(num);

	if (sscanf(argv[4], "%u%c", &num, &dummy) != 1 ||
	    !num || (num & (num - 1)) ||
	    num < bdev_logical_block_size(v->hash_dev->bdev) ||
	    num > INT_MAX) {
		ti->error = "Invalid hash device block size";
		r = -EINVAL;
		goto bad;
	}
	v->hash_dev_block_bits = __ffs(num);

	if (sscanf(argv[5], "%llu%c", &num_ll, &dummy) != 1 ||
	    (sector_t)(num_ll << (v->data_dev_block_bits - SECTOR_SHIFT))
	    >> (v->data_dev_block_bits - SECTOR_SHIFT) != num_ll) {
		ti->error = "Invalid data blocks";
		r = -EINVAL;
		goto bad;
	}
	v->data_blocks = num_ll;

	if (ti->len > (v->data_blocks << (v->data_dev_block_bits - SECTOR_SHIFT))) {
		ti->error = "Data device is too small";
		r = -EINVAL;
		goto bad;
	}

	if (sscanf(argv[6], "%llu%c", &num_ll, &dummy) != 1 ||
	    (sector_t)(num_ll << (v->hash_dev_block_bits - SECTOR_SHIFT))
	    >> (v->hash_dev_block_bits - SECTOR_SHIFT) != num_ll) {
		ti->error = "Invalid hash start";
		r = -EINVAL;
		goto bad;
	}
	v->hash_start = num_ll;

	v->alg_name = kstrdup(argv[7], GFP_KERNEL);
	if (!v->alg_name) {
		ti->error = "Cannot allocate algorithm name";
		r = -ENOMEM;
		goto bad;
	}

	v->tfm = crypto_alloc_ahash(v->alg_name, 0,
				    v->use_tasklet ? CRYPTO_ALG_ASYNC : 0);
	if (IS_ERR(v->tfm)) {
		ti->error = "Cannot initialize hash function";
		r = PTR_ERR(v->tfm);
		v->tfm = NULL;
		goto bad;
	}

	/*
	 * dm-verity performance can vary greatly depending on which hash
	 * algorithm implementation is used.  Help people debug performance
	 * problems by logging the ->cra_driver_name.
	 */
	DMINFO("%s using implementation \"%s\"", v->alg_name,
	       crypto_hash_alg_common(v->tfm)->base.cra_driver_name);

	v->digest_size = crypto_ahash_digestsize(v->tfm);
	if ((1 << v->hash_dev_block_bits) < v->digest_size * 2) {
		ti->error = "Digest size too big";
		r = -EINVAL;
		goto bad;
	}
	v->ahash_reqsize = sizeof(struct ahash_request) +
		crypto_ahash_reqsize(v->tfm);

	v->root_digest = kmalloc(v->digest_size, GFP_KERNEL);
	if (!v->root_digest) {
		ti->error = "Cannot allocate root digest";
		r = -ENOMEM;
		goto bad;
	}
	if (strlen(argv[8]) != v->digest_size * 2 ||
	    hex2bin(v->root_digest, argv[8], v->digest_size)) {
		ti->error = "Invalid root digest";
		r = -EINVAL;
		goto bad;
	}
	root_hash_digest_to_validate = argv[8];

	if (strcmp(argv[9], "-")) {
		v->salt_size = strlen(argv[9]) / 2;
		v->salt = kmalloc(v->salt_size, GFP_KERNEL);
		if (!v->salt) {
			ti->error = "Cannot allocate salt";
			r = -ENOMEM;
			goto bad;
		}
		if (strlen(argv[9]) != v->salt_size * 2 ||
		    hex2bin(v->salt, argv[9], v->salt_size)) {
			ti->error = "Invalid salt";
			r = -EINVAL;
			goto bad;
		}
	}

	argv += 10;
	argc -= 10;

	/* Optional parameters */
	if (argc) {
		as.argc = argc;
		as.argv = argv;
		r = verity_parse_opt_args(&as, v, &verify_args, false);
		if (r < 0)
			goto bad;
	}

	/* Root hash signature is  a optional parameter*/
	r = verity_verify_root_hash(root_hash_digest_to_validate,
				    strlen(root_hash_digest_to_validate),
				    verify_args.sig,
				    verify_args.sig_size);
	if (r < 0) {
		ti->error = "Root hash verification failed";
		goto bad;
	}
	v->hash_per_block_bits =
		__fls((1 << v->hash_dev_block_bits) / v->digest_size);

	v->levels = 0;
	if (v->data_blocks)
		while (v->hash_per_block_bits * v->levels < 64 &&
		       (unsigned long long)(v->data_blocks - 1) >>
		       (v->hash_per_block_bits * v->levels))
			v->levels++;

	if (v->levels > DM_VERITY_MAX_LEVELS) {
		ti->error = "Too many tree levels";
		r = -E2BIG;
		goto bad;
	}

	hash_position = v->hash_start;
	for (i = v->levels - 1; i >= 0; i--) {
		sector_t s;
		v->hash_level_block[i] = hash_position;
		s = (v->data_blocks + ((sector_t)1 << ((i + 1) * v->hash_per_block_bits)) - 1)
					>> ((i + 1) * v->hash_per_block_bits);
		if (hash_position + s < hash_position) {
			ti->error = "Hash device offset overflow";
			r = -E2BIG;
			goto bad;
		}
		hash_position += s;
	}
	v->hash_blocks = hash_position;

	v->bufio = dm_bufio_client_create(v->hash_dev->bdev,
		1 << v->hash_dev_block_bits, 1, sizeof(struct buffer_aux),
		dm_bufio_alloc_callback, NULL,
		v->use_tasklet ? DM_BUFIO_CLIENT_NO_SLEEP : 0);
	if (IS_ERR(v->bufio)) {
		ti->error = "Cannot initialize dm-bufio";
		r = PTR_ERR(v->bufio);
		v->bufio = NULL;
		goto bad;
	}

	if (dm_bufio_get_device_size(v->bufio) < v->hash_blocks) {
		ti->error = "Hash device is too small";
		r = -E2BIG;
		goto bad;
	}

	/* WQ_UNBOUND greatly improves performance when running on ramdisk */
	wq_flags = WQ_MEM_RECLAIM | WQ_UNBOUND;
	if (v->use_tasklet) {
		/*
		 * Allow verify_wq to preempt softirq since verification in
		 * tasklet will fall-back to using it for error handling
		 * (or if the bufio cache doesn't have required hashes).
		 */
		wq_flags |= WQ_HIGHPRI;
	}
	v->verify_wq = alloc_workqueue("kverityd", wq_flags, num_online_cpus());
	if (!v->verify_wq) {
		ti->error = "Cannot allocate workqueue";
		r = -ENOMEM;
		goto bad;
	}

	ti->per_io_data_size = sizeof(struct dm_verity_io) +
				v->ahash_reqsize + v->digest_size * 2;

	r = verity_fec_ctr(v);
	if (r)
		goto bad;

	ti->per_io_data_size = roundup(ti->per_io_data_size,
				       __alignof__(struct dm_verity_io));

	verity_verify_sig_opts_cleanup(&verify_args);

	return 0;

bad:

	verity_verify_sig_opts_cleanup(&verify_args);
	verity_dtr(ti);

	return r;
}

/*
 * Check whether a DM target is a verity target.
 */
bool dm_is_verity_target(struct dm_target *ti)
{
	return ti->type->module == THIS_MODULE;
}

/*
 * Get the root digest of a verity target.
 *
 * Returns a copy of the root digest, the caller is responsible for
 * freeing the memory of the digest.
 */
int dm_verity_get_root_digest(struct dm_target *ti, u8 **root_digest, unsigned int *digest_size)
{
	struct dm_verity *v = ti->private;

	if (!dm_is_verity_target(ti))
		return -EINVAL;

	*root_digest = kmemdup(v->root_digest, v->digest_size, GFP_KERNEL);
	if (*root_digest == NULL)
		return -ENOMEM;

	*digest_size = v->digest_size;

	return 0;
}

static struct target_type verity_target = {
	.name		= "verity",
	.features	= DM_TARGET_IMMUTABLE,
	.version	= {1, 9, 0},
	.module		= THIS_MODULE,
	.ctr		= verity_ctr,
	.dtr		= verity_dtr,
	.map		= verity_map,
	.status		= verity_status,
	.prepare_ioctl	= verity_prepare_ioctl,
	.iterate_devices = verity_iterate_devices,
	.io_hints	= verity_io_hints,
};

static int __init dm_verity_init(void)
{
	int r;

	r = dm_register_target(&verity_target);
	if (r < 0)
		DMERR("register failed %d", r);

	return r;
}

static void __exit dm_verity_exit(void)
{
	dm_unregister_target(&verity_target);
}

module_init(dm_verity_init);
module_exit(dm_verity_exit);

MODULE_AUTHOR("Mikulas Patocka <mpatocka@redhat.com>");
MODULE_AUTHOR("Mandeep Baines <msb@chromium.org>");
MODULE_AUTHOR("Will Drewry <wad@chromium.org>");
MODULE_DESCRIPTION(DM_NAME " target for transparent disk integrity checking");
MODULE_LICENSE("GPL");
