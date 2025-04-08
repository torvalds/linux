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
#include "dm-audit.h"
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/jump_label.h>
#include <linux/security.h>

#define DM_MSG_PREFIX			"verity"

#define DM_VERITY_ENV_LENGTH		42
#define DM_VERITY_ENV_VAR_NAME		"DM_VERITY_ERR_BLOCK_NR"

#define DM_VERITY_DEFAULT_PREFETCH_SIZE	262144
#define DM_VERITY_USE_BH_DEFAULT_BYTES	8192

#define DM_VERITY_MAX_CORRUPTED_ERRS	100

#define DM_VERITY_OPT_LOGGING		"ignore_corruption"
#define DM_VERITY_OPT_RESTART		"restart_on_corruption"
#define DM_VERITY_OPT_PANIC		"panic_on_corruption"
#define DM_VERITY_OPT_ERROR_RESTART	"restart_on_error"
#define DM_VERITY_OPT_ERROR_PANIC	"panic_on_error"
#define DM_VERITY_OPT_IGN_ZEROES	"ignore_zero_blocks"
#define DM_VERITY_OPT_AT_MOST_ONCE	"check_at_most_once"
#define DM_VERITY_OPT_TASKLET_VERIFY	"try_verify_in_tasklet"

#define DM_VERITY_OPTS_MAX		(5 + DM_VERITY_OPTS_FEC + \
					 DM_VERITY_ROOT_HASH_VERIFICATION_OPTS)

static unsigned int dm_verity_prefetch_cluster = DM_VERITY_DEFAULT_PREFETCH_SIZE;

module_param_named(prefetch_cluster, dm_verity_prefetch_cluster, uint, 0644);

static unsigned int dm_verity_use_bh_bytes[4] = {
	DM_VERITY_USE_BH_DEFAULT_BYTES,	// IOPRIO_CLASS_NONE
	DM_VERITY_USE_BH_DEFAULT_BYTES,	// IOPRIO_CLASS_RT
	DM_VERITY_USE_BH_DEFAULT_BYTES,	// IOPRIO_CLASS_BE
	0				// IOPRIO_CLASS_IDLE
};

module_param_array_named(use_bh_bytes, dm_verity_use_bh_bytes, uint, NULL, 0644);

static DEFINE_STATIC_KEY_FALSE(use_bh_wq_enabled);

/* Is at least one dm-verity instance using ahash_tfm instead of shash_tfm? */
static DEFINE_STATIC_KEY_FALSE(ahash_enabled);

struct dm_verity_prefetch_work {
	struct work_struct work;
	struct dm_verity *v;
	unsigned short ioprio;
	sector_t block;
	unsigned int n_blocks;
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
	return dm_target_offset(v->ti, bi_sector);
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

static int verity_ahash_update(struct dm_verity *v, struct ahash_request *req,
				const u8 *data, size_t len,
				struct crypto_wait *wait)
{
	struct scatterlist sg;

	if (likely(!is_vmalloc_addr(data))) {
		sg_init_one(&sg, data, len);
		ahash_request_set_crypt(req, &sg, NULL, len);
		return crypto_wait_req(crypto_ahash_update(req), wait);
	}

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

/*
 * Wrapper for crypto_ahash_init, which handles verity salting.
 */
static int verity_ahash_init(struct dm_verity *v, struct ahash_request *req,
				struct crypto_wait *wait, bool may_sleep)
{
	int r;

	ahash_request_set_tfm(req, v->ahash_tfm);
	ahash_request_set_callback(req,
		may_sleep ? CRYPTO_TFM_REQ_MAY_SLEEP | CRYPTO_TFM_REQ_MAY_BACKLOG : 0,
		crypto_req_done, (void *)wait);
	crypto_init_wait(wait);

	r = crypto_wait_req(crypto_ahash_init(req), wait);

	if (unlikely(r < 0)) {
		if (r != -ENOMEM)
			DMERR("crypto_ahash_init failed: %d", r);
		return r;
	}

	if (likely(v->salt_size && (v->version >= 1)))
		r = verity_ahash_update(v, req, v->salt, v->salt_size, wait);

	return r;
}

static int verity_ahash_final(struct dm_verity *v, struct ahash_request *req,
			      u8 *digest, struct crypto_wait *wait)
{
	int r;

	if (unlikely(v->salt_size && (!v->version))) {
		r = verity_ahash_update(v, req, v->salt, v->salt_size, wait);

		if (r < 0) {
			DMERR("%s failed updating salt: %d", __func__, r);
			goto out;
		}
	}

	ahash_request_set_crypt(req, NULL, digest, 0);
	r = crypto_wait_req(crypto_ahash_final(req), wait);
out:
	return r;
}

int verity_hash(struct dm_verity *v, struct dm_verity_io *io,
		const u8 *data, size_t len, u8 *digest, bool may_sleep)
{
	int r;

	if (static_branch_unlikely(&ahash_enabled) && !v->shash_tfm) {
		struct ahash_request *req = verity_io_hash_req(v, io);
		struct crypto_wait wait;

		r = verity_ahash_init(v, req, &wait, may_sleep) ?:
		    verity_ahash_update(v, req, data, len, &wait) ?:
		    verity_ahash_final(v, req, digest, &wait);
	} else {
		struct shash_desc *desc = verity_io_hash_req(v, io);

		desc->tfm = v->shash_tfm;
		r = crypto_shash_import(desc, v->initial_hashstate) ?:
		    crypto_shash_finup(desc, data, len, digest);
	}
	if (unlikely(r))
		DMERR("Error hashing block: %d", r);
	return r;
}

static void verity_hash_at_level(struct dm_verity *v, sector_t block, int level,
				 sector_t *hash_block, unsigned int *offset)
{
	sector_t position = verity_position_at_level(v, block, level);
	unsigned int idx;

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

	if (v->corrupted_errs == DM_VERITY_MAX_CORRUPTED_ERRS) {
		DMERR("%s: reached maximum errors", v->data_dev->name);
		dm_audit_log_target(DM_MSG_PREFIX, "max-corrupted-errors", v->ti, 0);
	}

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
	unsigned int offset;
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);

	verity_hash_at_level(v, block, level, &hash_block, &offset);

	if (static_branch_unlikely(&use_bh_wq_enabled) && io->in_bh) {
		data = dm_bufio_get(v->bufio, hash_block, &buf);
		if (IS_ERR_OR_NULL(data)) {
			/*
			 * In tasklet and the hash was not in the bufio cache.
			 * Return early and resume execution from a work-queue
			 * to read the hash from disk.
			 */
			return -EAGAIN;
		}
	} else {
		data = dm_bufio_read_with_ioprio(v->bufio, hash_block,
						&buf, bio->bi_ioprio);
	}

	if (IS_ERR(data)) {
		if (skip_unverified)
			return 1;
		r = PTR_ERR(data);
		data = dm_bufio_new(v->bufio, hash_block, &buf);
		if (IS_ERR(data))
			return r;
		if (verity_fec_decode(v, io, DM_VERITY_BLOCK_TYPE_METADATA,
				      hash_block, data) == 0) {
			aux = dm_bufio_get_aux_data(buf);
			aux->hash_verified = 1;
			goto release_ok;
		} else {
			dm_bufio_release(buf);
			dm_bufio_forget(v->bufio, hash_block);
			return r;
		}
	}

	aux = dm_bufio_get_aux_data(buf);

	if (!aux->hash_verified) {
		if (skip_unverified) {
			r = 1;
			goto release_ret_r;
		}

		r = verity_hash(v, io, data, 1 << v->hash_dev_block_bits,
				verity_io_real_digest(v, io), !io->in_bh);
		if (unlikely(r < 0))
			goto release_ret_r;

		if (likely(memcmp(verity_io_real_digest(v, io), want_digest,
				  v->digest_size) == 0))
			aux->hash_verified = 1;
		else if (static_branch_unlikely(&use_bh_wq_enabled) && io->in_bh) {
			/*
			 * Error handling code (FEC included) cannot be run in a
			 * tasklet since it may sleep, so fallback to work-queue.
			 */
			r = -EAGAIN;
			goto release_ret_r;
		} else if (verity_fec_decode(v, io, DM_VERITY_BLOCK_TYPE_METADATA,
					     hash_block, data) == 0)
			aux->hash_verified = 1;
		else if (verity_handle_err(v,
					   DM_VERITY_BLOCK_TYPE_METADATA,
					   hash_block)) {
			struct bio *bio;
			io->had_mismatch = true;
			bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);
			dm_audit_log_bio(DM_MSG_PREFIX, "verify-metadata", bio,
					 block, 0);
			r = -EIO;
			goto release_ret_r;
		}
	}

release_ok:
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

static noinline int verity_recheck(struct dm_verity *v, struct dm_verity_io *io,
				   sector_t cur_block, u8 *dest)
{
	struct page *page;
	void *buffer;
	int r;
	struct dm_io_request io_req;
	struct dm_io_region io_loc;

	page = mempool_alloc(&v->recheck_pool, GFP_NOIO);
	buffer = page_to_virt(page);

	io_req.bi_opf = REQ_OP_READ;
	io_req.mem.type = DM_IO_KMEM;
	io_req.mem.ptr.addr = buffer;
	io_req.notify.fn = NULL;
	io_req.client = v->io;
	io_loc.bdev = v->data_dev->bdev;
	io_loc.sector = cur_block << (v->data_dev_block_bits - SECTOR_SHIFT);
	io_loc.count = 1 << (v->data_dev_block_bits - SECTOR_SHIFT);
	r = dm_io(&io_req, 1, &io_loc, NULL, IOPRIO_DEFAULT);
	if (unlikely(r))
		goto free_ret;

	r = verity_hash(v, io, buffer, 1 << v->data_dev_block_bits,
			verity_io_real_digest(v, io), true);
	if (unlikely(r))
		goto free_ret;

	if (memcmp(verity_io_real_digest(v, io),
		   verity_io_want_digest(v, io), v->digest_size)) {
		r = -EIO;
		goto free_ret;
	}

	memcpy(dest, buffer, 1 << v->data_dev_block_bits);
	r = 0;
free_ret:
	mempool_free(page, &v->recheck_pool);

	return r;
}

static int verity_handle_data_hash_mismatch(struct dm_verity *v,
					    struct dm_verity_io *io,
					    struct bio *bio, sector_t blkno,
					    u8 *data)
{
	if (static_branch_unlikely(&use_bh_wq_enabled) && io->in_bh) {
		/*
		 * Error handling code (FEC included) cannot be run in the
		 * BH workqueue, so fallback to a standard workqueue.
		 */
		return -EAGAIN;
	}
	if (verity_recheck(v, io, blkno, data) == 0) {
		if (v->validated_blocks)
			set_bit(blkno, v->validated_blocks);
		return 0;
	}
#if defined(CONFIG_DM_VERITY_FEC)
	if (verity_fec_decode(v, io, DM_VERITY_BLOCK_TYPE_DATA, blkno,
			      data) == 0)
		return 0;
#endif
	if (bio->bi_status)
		return -EIO; /* Error correction failed; Just return error */

	if (verity_handle_err(v, DM_VERITY_BLOCK_TYPE_DATA, blkno)) {
		io->had_mismatch = true;
		dm_audit_log_bio(DM_MSG_PREFIX, "verify-data", bio, blkno, 0);
		return -EIO;
	}
	return 0;
}

/*
 * Verify one "dm_verity_io" structure.
 */
static int verity_verify_io(struct dm_verity_io *io)
{
	struct dm_verity *v = io->v;
	const unsigned int block_size = 1 << v->data_dev_block_bits;
	struct bvec_iter iter_copy;
	struct bvec_iter *iter;
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);
	unsigned int b;

	if (static_branch_unlikely(&use_bh_wq_enabled) && io->in_bh) {
		/*
		 * Copy the iterator in case we need to restart
		 * verification in a work-queue.
		 */
		iter_copy = io->iter;
		iter = &iter_copy;
	} else
		iter = &io->iter;

	for (b = 0; b < io->n_blocks;
	     b++, bio_advance_iter(bio, iter, block_size)) {
		int r;
		sector_t cur_block = io->block + b;
		bool is_zero;
		struct bio_vec bv;
		void *data;

		if (v->validated_blocks && bio->bi_status == BLK_STS_OK &&
		    likely(test_bit(cur_block, v->validated_blocks)))
			continue;

		r = verity_hash_for_block(v, io, cur_block,
					  verity_io_want_digest(v, io),
					  &is_zero);
		if (unlikely(r < 0))
			return r;

		bv = bio_iter_iovec(bio, *iter);
		if (unlikely(bv.bv_len < block_size)) {
			/*
			 * Data block spans pages.  This should not happen,
			 * since dm-verity sets dma_alignment to the data block
			 * size minus 1, and dm-verity also doesn't allow the
			 * data block size to be greater than PAGE_SIZE.
			 */
			DMERR_LIMIT("unaligned io (data block spans pages)");
			return -EIO;
		}

		data = bvec_kmap_local(&bv);

		if (is_zero) {
			/*
			 * If we expect a zero block, don't validate, just
			 * return zeros.
			 */
			memset(data, 0, block_size);
			kunmap_local(data);
			continue;
		}

		r = verity_hash(v, io, data, block_size,
				verity_io_real_digest(v, io), !io->in_bh);
		if (unlikely(r < 0)) {
			kunmap_local(data);
			return r;
		}

		if (likely(memcmp(verity_io_real_digest(v, io),
				  verity_io_want_digest(v, io), v->digest_size) == 0)) {
			if (v->validated_blocks)
				set_bit(cur_block, v->validated_blocks);
			kunmap_local(data);
			continue;
		}
		r = verity_handle_data_hash_mismatch(v, io, bio, cur_block,
						     data);
		kunmap_local(data);
		if (unlikely(r))
			return r;
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

static void restart_io_error(struct work_struct *w)
{
	kernel_restart("dm-verity device has I/O error");
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

	if (!static_branch_unlikely(&use_bh_wq_enabled) || !io->in_bh)
		verity_fec_finish_io(io);

	if (unlikely(status != BLK_STS_OK) &&
	    unlikely(!(bio->bi_opf & REQ_RAHEAD)) &&
	    !io->had_mismatch &&
	    !verity_is_system_shutting_down()) {
		if (v->error_mode == DM_VERITY_MODE_PANIC) {
			panic("dm-verity device has I/O error");
		}
		if (v->error_mode == DM_VERITY_MODE_RESTART) {
			static DECLARE_WORK(restart_work, restart_io_error);
			queue_work(v->verify_wq, &restart_work);
			/*
			 * We deliberately don't call bio_endio here, because
			 * the machine will be restarted anyway.
			 */
			return;
		}
	}

	bio_endio(bio);
}

static void verity_work(struct work_struct *w)
{
	struct dm_verity_io *io = container_of(w, struct dm_verity_io, work);

	io->in_bh = false;

	verity_finish_io(io, errno_to_blk_status(verity_verify_io(io)));
}

static void verity_bh_work(struct work_struct *w)
{
	struct dm_verity_io *io = container_of(w, struct dm_verity_io, bh_work);
	int err;

	io->in_bh = true;
	err = verity_verify_io(io);
	if (err == -EAGAIN || err == -ENOMEM) {
		/* fallback to retrying with work-queue */
		INIT_WORK(&io->work, verity_work);
		queue_work(io->v->verify_wq, &io->work);
		return;
	}

	verity_finish_io(io, errno_to_blk_status(err));
}

static inline bool verity_use_bh(unsigned int bytes, unsigned short ioprio)
{
	return ioprio <= IOPRIO_CLASS_IDLE &&
		bytes <= READ_ONCE(dm_verity_use_bh_bytes[ioprio]);
}

static void verity_end_io(struct bio *bio)
{
	struct dm_verity_io *io = bio->bi_private;
	unsigned short ioprio = IOPRIO_PRIO_CLASS(bio->bi_ioprio);
	unsigned int bytes = io->n_blocks << io->v->data_dev_block_bits;

	if (bio->bi_status &&
	    (!verity_fec_is_enabled(io->v) ||
	     verity_is_system_shutting_down() ||
	     (bio->bi_opf & REQ_RAHEAD))) {
		verity_finish_io(io, bio->bi_status);
		return;
	}

	if (static_branch_unlikely(&use_bh_wq_enabled) && io->v->use_bh_wq &&
		verity_use_bh(bytes, ioprio)) {
		if (in_hardirq() || irqs_disabled()) {
			INIT_WORK(&io->bh_work, verity_bh_work);
			queue_work(system_bh_wq, &io->bh_work);
		} else {
			verity_bh_work(&io->bh_work);
		}
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
			unsigned int cluster = READ_ONCE(dm_verity_prefetch_cluster);

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
		dm_bufio_prefetch_with_ioprio(v->bufio, hash_block_start,
					hash_block_end - hash_block_start + 1,
					pw->ioprio);
	}

	kfree(pw);
}

static void verity_submit_prefetch(struct dm_verity *v, struct dm_verity_io *io,
				   unsigned short ioprio)
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
	pw->ioprio = ioprio;
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

	if (((unsigned int)bio->bi_iter.bi_sector | bio_sectors(bio)) &
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
	io->had_mismatch = false;

	bio->bi_end_io = verity_end_io;
	bio->bi_private = io;
	io->iter = bio->bi_iter;

	verity_fec_init_io(io);

	verity_submit_prefetch(v, io, bio->bi_ioprio);

	submit_bio_noacct(bio);

	return DM_MAPIO_SUBMITTED;
}

static void verity_postsuspend(struct dm_target *ti)
{
	struct dm_verity *v = ti->private;
	flush_workqueue(v->verify_wq);
	dm_bufio_client_reset(v->bufio);
}

/*
 * Status: V (valid) or C (corruption found)
 */
static void verity_status(struct dm_target *ti, status_type_t type,
			  unsigned int status_flags, char *result, unsigned int maxlen)
{
	struct dm_verity *v = ti->private;
	unsigned int args = 0;
	unsigned int sz = 0;
	unsigned int x;

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
		if (v->error_mode != DM_VERITY_MODE_EIO)
			args++;
		if (verity_fec_is_enabled(v))
			args += DM_VERITY_OPTS_FEC;
		if (v->zero_digest)
			args++;
		if (v->validated_blocks)
			args++;
		if (v->use_bh_wq)
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
		if (v->error_mode != DM_VERITY_MODE_EIO) {
			DMEMIT(" ");
			switch (v->error_mode) {
			case DM_VERITY_MODE_RESTART:
				DMEMIT(DM_VERITY_OPT_ERROR_RESTART);
				break;
			case DM_VERITY_MODE_PANIC:
				DMEMIT(DM_VERITY_OPT_ERROR_PANIC);
				break;
			default:
				BUG();
			}
		}
		if (v->zero_digest)
			DMEMIT(" " DM_VERITY_OPT_IGN_ZEROES);
		if (v->validated_blocks)
			DMEMIT(" " DM_VERITY_OPT_AT_MOST_ONCE);
		if (v->use_bh_wq)
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
		if (v->error_mode != DM_VERITY_MODE_EIO) {
			DMEMIT(",verity_error_mode=");
			switch (v->error_mode) {
			case DM_VERITY_MODE_RESTART:
				DMEMIT(DM_VERITY_OPT_ERROR_RESTART);
				break;
			case DM_VERITY_MODE_PANIC:
				DMEMIT(DM_VERITY_OPT_ERROR_PANIC);
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

	if (ti->len != bdev_nr_sectors(v->data_dev->bdev))
		return 1;
	return 0;
}

static int verity_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct dm_verity *v = ti->private;

	return fn(ti, v->data_dev, 0, ti->len, data);
}

static void verity_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct dm_verity *v = ti->private;

	if (limits->logical_block_size < 1 << v->data_dev_block_bits)
		limits->logical_block_size = 1 << v->data_dev_block_bits;

	if (limits->physical_block_size < 1 << v->data_dev_block_bits)
		limits->physical_block_size = 1 << v->data_dev_block_bits;

	limits->io_min = limits->logical_block_size;

	/*
	 * Similar to what dm-crypt does, opt dm-verity out of support for
	 * direct I/O that is aligned to less than the traditional direct I/O
	 * alignment requirement of logical_block_size.  This prevents dm-verity
	 * data blocks from crossing pages, eliminating various edge cases.
	 */
	limits->dma_alignment = limits->logical_block_size - 1;
}

#ifdef CONFIG_SECURITY

static int verity_init_sig(struct dm_verity *v, const void *sig,
			   size_t sig_size)
{
	v->sig_size = sig_size;

	if (sig) {
		v->root_digest_sig = kmemdup(sig, v->sig_size, GFP_KERNEL);
		if (!v->root_digest_sig)
			return -ENOMEM;
	}

	return 0;
}

static void verity_free_sig(struct dm_verity *v)
{
	kfree(v->root_digest_sig);
}

#else

static inline int verity_init_sig(struct dm_verity *v, const void *sig,
				  size_t sig_size)
{
	return 0;
}

static inline void verity_free_sig(struct dm_verity *v)
{
}

#endif /* CONFIG_SECURITY */

static void verity_dtr(struct dm_target *ti)
{
	struct dm_verity *v = ti->private;

	if (v->verify_wq)
		destroy_workqueue(v->verify_wq);

	mempool_exit(&v->recheck_pool);
	if (v->io)
		dm_io_client_destroy(v->io);

	if (v->bufio)
		dm_bufio_client_destroy(v->bufio);

	kvfree(v->validated_blocks);
	kfree(v->salt);
	kfree(v->initial_hashstate);
	kfree(v->root_digest);
	kfree(v->zero_digest);
	verity_free_sig(v);

	if (v->ahash_tfm) {
		static_branch_dec(&ahash_enabled);
		crypto_free_ahash(v->ahash_tfm);
	} else {
		crypto_free_shash(v->shash_tfm);
	}

	kfree(v->alg_name);

	if (v->hash_dev)
		dm_put_device(ti, v->hash_dev);

	if (v->data_dev)
		dm_put_device(ti, v->data_dev);

	verity_fec_dtr(v);

	kfree(v->signature_key_desc);

	if (v->use_bh_wq)
		static_branch_dec(&use_bh_wq_enabled);

	kfree(v);

	dm_audit_log_dtr(DM_MSG_PREFIX, ti, 1);
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
	struct dm_verity_io *io;
	u8 *zero_data;

	v->zero_digest = kmalloc(v->digest_size, GFP_KERNEL);

	if (!v->zero_digest)
		return r;

	io = kmalloc(sizeof(*io) + v->hash_reqsize, GFP_KERNEL);

	if (!io)
		return r; /* verity_dtr will free zero_digest */

	zero_data = kzalloc(1 << v->data_dev_block_bits, GFP_KERNEL);

	if (!zero_data)
		goto out;

	r = verity_hash(v, io, zero_data, 1 << v->data_dev_block_bits,
			v->zero_digest, true);

out:
	kfree(io);
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

static inline bool verity_is_verity_error_mode(const char *arg_name)
{
	return (!strcasecmp(arg_name, DM_VERITY_OPT_ERROR_RESTART) ||
		!strcasecmp(arg_name, DM_VERITY_OPT_ERROR_PANIC));
}

static int verity_parse_verity_error_mode(struct dm_verity *v, const char *arg_name)
{
	if (v->error_mode)
		return -EINVAL;

	if (!strcasecmp(arg_name, DM_VERITY_OPT_ERROR_RESTART))
		v->error_mode = DM_VERITY_MODE_RESTART;
	else if (!strcasecmp(arg_name, DM_VERITY_OPT_ERROR_PANIC))
		v->error_mode = DM_VERITY_MODE_PANIC;

	return 0;
}

static int verity_parse_opt_args(struct dm_arg_set *as, struct dm_verity *v,
				 struct dm_verity_sig_opts *verify_args,
				 bool only_modifier_opts)
{
	int r = 0;
	unsigned int argc;
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

		} else if (verity_is_verity_error_mode(arg_name)) {
			if (only_modifier_opts)
				continue;
			r = verity_parse_verity_error_mode(v, arg_name);
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
			v->use_bh_wq = true;
			static_branch_inc(&use_bh_wq_enabled);
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

static int verity_setup_hash_alg(struct dm_verity *v, const char *alg_name)
{
	struct dm_target *ti = v->ti;
	struct crypto_ahash *ahash;
	struct crypto_shash *shash = NULL;
	const char *driver_name;

	v->alg_name = kstrdup(alg_name, GFP_KERNEL);
	if (!v->alg_name) {
		ti->error = "Cannot allocate algorithm name";
		return -ENOMEM;
	}

	/*
	 * Allocate the hash transformation object that this dm-verity instance
	 * will use.  The vast majority of dm-verity users use CPU-based
	 * hashing, so when possible use the shash API to minimize the crypto
	 * API overhead.  If the ahash API resolves to a different driver
	 * (likely an off-CPU hardware offload), use ahash instead.  Also use
	 * ahash if the obsolete dm-verity format with the appended salt is
	 * being used, so that quirk only needs to be handled in one place.
	 */
	ahash = crypto_alloc_ahash(alg_name, 0,
				   v->use_bh_wq ? CRYPTO_ALG_ASYNC : 0);
	if (IS_ERR(ahash)) {
		ti->error = "Cannot initialize hash function";
		return PTR_ERR(ahash);
	}
	driver_name = crypto_ahash_driver_name(ahash);
	if (v->version >= 1 /* salt prepended, not appended? */) {
		shash = crypto_alloc_shash(alg_name, 0, 0);
		if (!IS_ERR(shash) &&
		    strcmp(crypto_shash_driver_name(shash), driver_name) != 0) {
			/*
			 * ahash gave a different driver than shash, so probably
			 * this is a case of real hardware offload.  Use ahash.
			 */
			crypto_free_shash(shash);
			shash = NULL;
		}
	}
	if (!IS_ERR_OR_NULL(shash)) {
		crypto_free_ahash(ahash);
		ahash = NULL;
		v->shash_tfm = shash;
		v->digest_size = crypto_shash_digestsize(shash);
		v->hash_reqsize = sizeof(struct shash_desc) +
				  crypto_shash_descsize(shash);
		DMINFO("%s using shash \"%s\"", alg_name, driver_name);
	} else {
		v->ahash_tfm = ahash;
		static_branch_inc(&ahash_enabled);
		v->digest_size = crypto_ahash_digestsize(ahash);
		v->hash_reqsize = sizeof(struct ahash_request) +
				  crypto_ahash_reqsize(ahash);
		DMINFO("%s using ahash \"%s\"", alg_name, driver_name);
	}
	if ((1 << v->hash_dev_block_bits) < v->digest_size * 2) {
		ti->error = "Digest size too big";
		return -EINVAL;
	}
	return 0;
}

static int verity_setup_salt_and_hashstate(struct dm_verity *v, const char *arg)
{
	struct dm_target *ti = v->ti;

	if (strcmp(arg, "-") != 0) {
		v->salt_size = strlen(arg) / 2;
		v->salt = kmalloc(v->salt_size, GFP_KERNEL);
		if (!v->salt) {
			ti->error = "Cannot allocate salt";
			return -ENOMEM;
		}
		if (strlen(arg) != v->salt_size * 2 ||
		    hex2bin(v->salt, arg, v->salt_size)) {
			ti->error = "Invalid salt";
			return -EINVAL;
		}
	}
	if (v->shash_tfm) {
		SHASH_DESC_ON_STACK(desc, v->shash_tfm);
		int r;

		/*
		 * Compute the pre-salted hash state that can be passed to
		 * crypto_shash_import() for each block later.
		 */
		v->initial_hashstate = kmalloc(
			crypto_shash_statesize(v->shash_tfm), GFP_KERNEL);
		if (!v->initial_hashstate) {
			ti->error = "Cannot allocate initial hash state";
			return -ENOMEM;
		}
		desc->tfm = v->shash_tfm;
		r = crypto_shash_init(desc) ?:
		    crypto_shash_update(desc, v->salt, v->salt_size) ?:
		    crypto_shash_export(desc, v->initial_hashstate);
		if (r) {
			ti->error = "Cannot set up initial hash state";
			return r;
		}
	}
	return 0;
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
static int verity_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct dm_verity *v;
	struct dm_verity_sig_opts verify_args = {0};
	struct dm_arg_set as;
	unsigned int num;
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

	if ((dm_table_get_mode(ti->table) & ~BLK_OPEN_READ)) {
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

	r = dm_get_device(ti, argv[1], BLK_OPEN_READ, &v->data_dev);
	if (r) {
		ti->error = "Data device lookup failed";
		goto bad;
	}

	r = dm_get_device(ti, argv[2], BLK_OPEN_READ, &v->hash_dev);
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

	r = verity_setup_hash_alg(v, argv[7]);
	if (r)
		goto bad;

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

	r = verity_setup_salt_and_hashstate(v, argv[9]);
	if (r)
		goto bad;

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

	r = verity_init_sig(v, verify_args.sig, verify_args.sig_size);
	if (r < 0) {
		ti->error = "Cannot allocate root digest signature";
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

	r = mempool_init_page_pool(&v->recheck_pool, 1, 0);
	if (unlikely(r)) {
		ti->error = "Cannot allocate mempool";
		goto bad;
	}

	v->io = dm_io_client_create();
	if (IS_ERR(v->io)) {
		r = PTR_ERR(v->io);
		v->io = NULL;
		ti->error = "Cannot allocate dm io";
		goto bad;
	}

	v->bufio = dm_bufio_client_create(v->hash_dev->bdev,
		1 << v->hash_dev_block_bits, 1, sizeof(struct buffer_aux),
		dm_bufio_alloc_callback, NULL,
		v->use_bh_wq ? DM_BUFIO_CLIENT_NO_SLEEP : 0);
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

	/*
	 * Using WQ_HIGHPRI improves throughput and completion latency by
	 * reducing wait times when reading from a dm-verity device.
	 *
	 * Also as required for the "try_verify_in_tasklet" feature: WQ_HIGHPRI
	 * allows verify_wq to preempt softirq since verification in BH workqueue
	 * will fall-back to using it for error handling (or if the bufio cache
	 * doesn't have required hashes).
	 */
	v->verify_wq = alloc_workqueue("kverityd", WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	if (!v->verify_wq) {
		ti->error = "Cannot allocate workqueue";
		r = -ENOMEM;
		goto bad;
	}

	ti->per_io_data_size = sizeof(struct dm_verity_io) + v->hash_reqsize;

	r = verity_fec_ctr(v);
	if (r)
		goto bad;

	ti->per_io_data_size = roundup(ti->per_io_data_size,
				       __alignof__(struct dm_verity_io));

	verity_verify_sig_opts_cleanup(&verify_args);

	dm_audit_log_ctr(DM_MSG_PREFIX, ti, 1);

	return 0;

bad:

	verity_verify_sig_opts_cleanup(&verify_args);
	dm_audit_log_ctr(DM_MSG_PREFIX, ti, 0);
	verity_dtr(ti);

	return r;
}

/*
 * Get the verity mode (error behavior) of a verity target.
 *
 * Returns the verity mode of the target, or -EINVAL if 'ti' is not a verity
 * target.
 */
int dm_verity_get_mode(struct dm_target *ti)
{
	struct dm_verity *v = ti->private;

	if (!dm_is_verity_target(ti))
		return -EINVAL;

	return v->mode;
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

#ifdef CONFIG_SECURITY

#ifdef CONFIG_DM_VERITY_VERIFY_ROOTHASH_SIG

static int verity_security_set_signature(struct block_device *bdev,
					 struct dm_verity *v)
{
	/*
	 * if the dm-verity target is unsigned, v->root_digest_sig will
	 * be NULL, and the hook call is still required to let LSMs mark
	 * the device as unsigned. This information is crucial for LSMs to
	 * block operations such as execution on unsigned files
	 */
	return security_bdev_setintegrity(bdev,
					  LSM_INT_DMVERITY_SIG_VALID,
					  v->root_digest_sig,
					  v->sig_size);
}

#else

static inline int verity_security_set_signature(struct block_device *bdev,
						struct dm_verity *v)
{
	return 0;
}

#endif /* CONFIG_DM_VERITY_VERIFY_ROOTHASH_SIG */

/*
 * Expose verity target's root hash and signature data to LSMs before resume.
 *
 * Returns 0 on success, or -ENOMEM if the system is out of memory.
 */
static int verity_preresume(struct dm_target *ti)
{
	struct block_device *bdev;
	struct dm_verity_digest root_digest;
	struct dm_verity *v;
	int r;

	v = ti->private;
	bdev = dm_disk(dm_table_get_md(ti->table))->part0;
	root_digest.digest = v->root_digest;
	root_digest.digest_len = v->digest_size;
	if (static_branch_unlikely(&ahash_enabled) && !v->shash_tfm)
		root_digest.alg = crypto_ahash_alg_name(v->ahash_tfm);
	else
		root_digest.alg = crypto_shash_alg_name(v->shash_tfm);

	r = security_bdev_setintegrity(bdev, LSM_INT_DMVERITY_ROOTHASH, &root_digest,
				       sizeof(root_digest));
	if (r)
		return r;

	r =  verity_security_set_signature(bdev, v);
	if (r)
		goto bad;

	return 0;

bad:

	security_bdev_setintegrity(bdev, LSM_INT_DMVERITY_ROOTHASH, NULL, 0);

	return r;
}

#endif /* CONFIG_SECURITY */

static struct target_type verity_target = {
	.name		= "verity",
/* Note: the LSMs depend on the singleton and immutable features */
	.features	= DM_TARGET_SINGLETON | DM_TARGET_IMMUTABLE,
	.version	= {1, 11, 0},
	.module		= THIS_MODULE,
	.ctr		= verity_ctr,
	.dtr		= verity_dtr,
	.map		= verity_map,
	.postsuspend	= verity_postsuspend,
	.status		= verity_status,
	.prepare_ioctl	= verity_prepare_ioctl,
	.iterate_devices = verity_iterate_devices,
	.io_hints	= verity_io_hints,
#ifdef CONFIG_SECURITY
	.preresume	= verity_preresume,
#endif /* CONFIG_SECURITY */
};
module_dm(verity);

/*
 * Check whether a DM target is a verity target.
 */
bool dm_is_verity_target(struct dm_target *ti)
{
	return ti->type == &verity_target;
}

MODULE_AUTHOR("Mikulas Patocka <mpatocka@redhat.com>");
MODULE_AUTHOR("Mandeep Baines <msb@chromium.org>");
MODULE_AUTHOR("Will Drewry <wad@chromium.org>");
MODULE_DESCRIPTION(DM_NAME " target for transparent disk integrity checking");
MODULE_LICENSE("GPL");
