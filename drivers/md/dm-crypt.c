/*
 * Copyright (C) 2003 Christophe Saout <christophe@saout.de>
 * Copyright (C) 2004 Clemens Fruhwirth <clemens@endorphin.org>
 * Copyright (C) 2006-2008 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <linux/workqueue.h>
#include <linux/backing-dev.h>
#include <asm/atomic.h>
#include <linux/scatterlist.h>
#include <asm/page.h>
#include <asm/unaligned.h>

#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "crypt"
#define MESG_STR(x) x, sizeof(x)

/*
 * context holding the current state of a multi-part conversion
 */
struct convert_context {
	struct completion restart;
	struct bio *bio_in;
	struct bio *bio_out;
	unsigned int offset_in;
	unsigned int offset_out;
	unsigned int idx_in;
	unsigned int idx_out;
	sector_t sector;
	atomic_t pending;
};

/*
 * per bio private data
 */
struct dm_crypt_io {
	struct dm_target *target;
	struct bio *base_bio;
	struct work_struct work;

	struct convert_context ctx;

	atomic_t pending;
	int error;
	sector_t sector;
	struct dm_crypt_io *base_io;
};

struct dm_crypt_request {
	struct convert_context *ctx;
	struct scatterlist sg_in;
	struct scatterlist sg_out;
};

struct crypt_config;

struct crypt_iv_operations {
	int (*ctr)(struct crypt_config *cc, struct dm_target *ti,
		   const char *opts);
	void (*dtr)(struct crypt_config *cc);
	const char *(*status)(struct crypt_config *cc);
	int (*generator)(struct crypt_config *cc, u8 *iv, sector_t sector);
};

/*
 * Crypt: maps a linear range of a block device
 * and encrypts / decrypts at the same time.
 */
enum flags { DM_CRYPT_SUSPENDED, DM_CRYPT_KEY_VALID };
struct crypt_config {
	struct dm_dev *dev;
	sector_t start;

	/*
	 * pool for per bio private data, crypto requests and
	 * encryption requeusts/buffer pages
	 */
	mempool_t *io_pool;
	mempool_t *req_pool;
	mempool_t *page_pool;
	struct bio_set *bs;

	struct workqueue_struct *io_queue;
	struct workqueue_struct *crypt_queue;

	/*
	 * crypto related data
	 */
	struct crypt_iv_operations *iv_gen_ops;
	char *iv_mode;
	union {
		struct crypto_cipher *essiv_tfm;
		int benbi_shift;
	} iv_gen_private;
	sector_t iv_offset;
	unsigned int iv_size;

	/*
	 * Layout of each crypto request:
	 *
	 *   struct ablkcipher_request
	 *      context
	 *      padding
	 *   struct dm_crypt_request
	 *      padding
	 *   IV
	 *
	 * The padding is added so that dm_crypt_request and the IV are
	 * correctly aligned.
	 */
	unsigned int dmreq_start;
	struct ablkcipher_request *req;

	char cipher[CRYPTO_MAX_ALG_NAME];
	char chainmode[CRYPTO_MAX_ALG_NAME];
	struct crypto_ablkcipher *tfm;
	unsigned long flags;
	unsigned int key_size;
	u8 key[0];
};

#define MIN_IOS        16
#define MIN_POOL_PAGES 32
#define MIN_BIO_PAGES  8

static struct kmem_cache *_crypt_io_pool;

static void clone_init(struct dm_crypt_io *, struct bio *);
static void kcryptd_queue_crypt(struct dm_crypt_io *io);

/*
 * Different IV generation algorithms:
 *
 * plain: the initial vector is the 32-bit little-endian version of the sector
 *        number, padded with zeros if necessary.
 *
 * essiv: "encrypted sector|salt initial vector", the sector number is
 *        encrypted with the bulk cipher using a salt as key. The salt
 *        should be derived from the bulk cipher's key via hashing.
 *
 * benbi: the 64-bit "big-endian 'narrow block'-count", starting at 1
 *        (needed for LRW-32-AES and possible other narrow block modes)
 *
 * null: the initial vector is always zero.  Provides compatibility with
 *       obsolete loop_fish2 devices.  Do not use for new devices.
 *
 * plumb: unimplemented, see:
 * http://article.gmane.org/gmane.linux.kernel.device-mapper.dm-crypt/454
 */

static int crypt_iv_plain_gen(struct crypt_config *cc, u8 *iv, sector_t sector)
{
	memset(iv, 0, cc->iv_size);
	*(u32 *)iv = cpu_to_le32(sector & 0xffffffff);

	return 0;
}

static int crypt_iv_essiv_ctr(struct crypt_config *cc, struct dm_target *ti,
			      const char *opts)
{
	struct crypto_cipher *essiv_tfm;
	struct crypto_hash *hash_tfm;
	struct hash_desc desc;
	struct scatterlist sg;
	unsigned int saltsize;
	u8 *salt;
	int err;

	if (opts == NULL) {
		ti->error = "Digest algorithm missing for ESSIV mode";
		return -EINVAL;
	}

	/* Hash the cipher key with the given hash algorithm */
	hash_tfm = crypto_alloc_hash(opts, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(hash_tfm)) {
		ti->error = "Error initializing ESSIV hash";
		return PTR_ERR(hash_tfm);
	}

	saltsize = crypto_hash_digestsize(hash_tfm);
	salt = kmalloc(saltsize, GFP_KERNEL);
	if (salt == NULL) {
		ti->error = "Error kmallocing salt storage in ESSIV";
		crypto_free_hash(hash_tfm);
		return -ENOMEM;
	}

	sg_init_one(&sg, cc->key, cc->key_size);
	desc.tfm = hash_tfm;
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;
	err = crypto_hash_digest(&desc, &sg, cc->key_size, salt);
	crypto_free_hash(hash_tfm);

	if (err) {
		ti->error = "Error calculating hash in ESSIV";
		kfree(salt);
		return err;
	}

	/* Setup the essiv_tfm with the given salt */
	essiv_tfm = crypto_alloc_cipher(cc->cipher, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(essiv_tfm)) {
		ti->error = "Error allocating crypto tfm for ESSIV";
		kfree(salt);
		return PTR_ERR(essiv_tfm);
	}
	if (crypto_cipher_blocksize(essiv_tfm) !=
	    crypto_ablkcipher_ivsize(cc->tfm)) {
		ti->error = "Block size of ESSIV cipher does "
			    "not match IV size of block cipher";
		crypto_free_cipher(essiv_tfm);
		kfree(salt);
		return -EINVAL;
	}
	err = crypto_cipher_setkey(essiv_tfm, salt, saltsize);
	if (err) {
		ti->error = "Failed to set key for ESSIV cipher";
		crypto_free_cipher(essiv_tfm);
		kfree(salt);
		return err;
	}
	kfree(salt);

	cc->iv_gen_private.essiv_tfm = essiv_tfm;
	return 0;
}

static void crypt_iv_essiv_dtr(struct crypt_config *cc)
{
	crypto_free_cipher(cc->iv_gen_private.essiv_tfm);
	cc->iv_gen_private.essiv_tfm = NULL;
}

static int crypt_iv_essiv_gen(struct crypt_config *cc, u8 *iv, sector_t sector)
{
	memset(iv, 0, cc->iv_size);
	*(u64 *)iv = cpu_to_le64(sector);
	crypto_cipher_encrypt_one(cc->iv_gen_private.essiv_tfm, iv, iv);
	return 0;
}

static int crypt_iv_benbi_ctr(struct crypt_config *cc, struct dm_target *ti,
			      const char *opts)
{
	unsigned bs = crypto_ablkcipher_blocksize(cc->tfm);
	int log = ilog2(bs);

	/* we need to calculate how far we must shift the sector count
	 * to get the cipher block count, we use this shift in _gen */

	if (1 << log != bs) {
		ti->error = "cypher blocksize is not a power of 2";
		return -EINVAL;
	}

	if (log > 9) {
		ti->error = "cypher blocksize is > 512";
		return -EINVAL;
	}

	cc->iv_gen_private.benbi_shift = 9 - log;

	return 0;
}

static void crypt_iv_benbi_dtr(struct crypt_config *cc)
{
}

static int crypt_iv_benbi_gen(struct crypt_config *cc, u8 *iv, sector_t sector)
{
	__be64 val;

	memset(iv, 0, cc->iv_size - sizeof(u64)); /* rest is cleared below */

	val = cpu_to_be64(((u64)sector << cc->iv_gen_private.benbi_shift) + 1);
	put_unaligned(val, (__be64 *)(iv + cc->iv_size - sizeof(u64)));

	return 0;
}

static int crypt_iv_null_gen(struct crypt_config *cc, u8 *iv, sector_t sector)
{
	memset(iv, 0, cc->iv_size);

	return 0;
}

static struct crypt_iv_operations crypt_iv_plain_ops = {
	.generator = crypt_iv_plain_gen
};

static struct crypt_iv_operations crypt_iv_essiv_ops = {
	.ctr       = crypt_iv_essiv_ctr,
	.dtr       = crypt_iv_essiv_dtr,
	.generator = crypt_iv_essiv_gen
};

static struct crypt_iv_operations crypt_iv_benbi_ops = {
	.ctr	   = crypt_iv_benbi_ctr,
	.dtr	   = crypt_iv_benbi_dtr,
	.generator = crypt_iv_benbi_gen
};

static struct crypt_iv_operations crypt_iv_null_ops = {
	.generator = crypt_iv_null_gen
};

static void crypt_convert_init(struct crypt_config *cc,
			       struct convert_context *ctx,
			       struct bio *bio_out, struct bio *bio_in,
			       sector_t sector)
{
	ctx->bio_in = bio_in;
	ctx->bio_out = bio_out;
	ctx->offset_in = 0;
	ctx->offset_out = 0;
	ctx->idx_in = bio_in ? bio_in->bi_idx : 0;
	ctx->idx_out = bio_out ? bio_out->bi_idx : 0;
	ctx->sector = sector + cc->iv_offset;
	init_completion(&ctx->restart);
}

static struct dm_crypt_request *dmreq_of_req(struct crypt_config *cc,
					     struct ablkcipher_request *req)
{
	return (struct dm_crypt_request *)((char *)req + cc->dmreq_start);
}

static struct ablkcipher_request *req_of_dmreq(struct crypt_config *cc,
					       struct dm_crypt_request *dmreq)
{
	return (struct ablkcipher_request *)((char *)dmreq - cc->dmreq_start);
}

static int crypt_convert_block(struct crypt_config *cc,
			       struct convert_context *ctx,
			       struct ablkcipher_request *req)
{
	struct bio_vec *bv_in = bio_iovec_idx(ctx->bio_in, ctx->idx_in);
	struct bio_vec *bv_out = bio_iovec_idx(ctx->bio_out, ctx->idx_out);
	struct dm_crypt_request *dmreq;
	u8 *iv;
	int r = 0;

	dmreq = dmreq_of_req(cc, req);
	iv = (u8 *)ALIGN((unsigned long)(dmreq + 1),
			 crypto_ablkcipher_alignmask(cc->tfm) + 1);

	dmreq->ctx = ctx;
	sg_init_table(&dmreq->sg_in, 1);
	sg_set_page(&dmreq->sg_in, bv_in->bv_page, 1 << SECTOR_SHIFT,
		    bv_in->bv_offset + ctx->offset_in);

	sg_init_table(&dmreq->sg_out, 1);
	sg_set_page(&dmreq->sg_out, bv_out->bv_page, 1 << SECTOR_SHIFT,
		    bv_out->bv_offset + ctx->offset_out);

	ctx->offset_in += 1 << SECTOR_SHIFT;
	if (ctx->offset_in >= bv_in->bv_len) {
		ctx->offset_in = 0;
		ctx->idx_in++;
	}

	ctx->offset_out += 1 << SECTOR_SHIFT;
	if (ctx->offset_out >= bv_out->bv_len) {
		ctx->offset_out = 0;
		ctx->idx_out++;
	}

	if (cc->iv_gen_ops) {
		r = cc->iv_gen_ops->generator(cc, iv, ctx->sector);
		if (r < 0)
			return r;
	}

	ablkcipher_request_set_crypt(req, &dmreq->sg_in, &dmreq->sg_out,
				     1 << SECTOR_SHIFT, iv);

	if (bio_data_dir(ctx->bio_in) == WRITE)
		r = crypto_ablkcipher_encrypt(req);
	else
		r = crypto_ablkcipher_decrypt(req);

	return r;
}

static void kcryptd_async_done(struct crypto_async_request *async_req,
			       int error);
static void crypt_alloc_req(struct crypt_config *cc,
			    struct convert_context *ctx)
{
	if (!cc->req)
		cc->req = mempool_alloc(cc->req_pool, GFP_NOIO);
	ablkcipher_request_set_tfm(cc->req, cc->tfm);
	ablkcipher_request_set_callback(cc->req, CRYPTO_TFM_REQ_MAY_BACKLOG |
					CRYPTO_TFM_REQ_MAY_SLEEP,
					kcryptd_async_done,
					dmreq_of_req(cc, cc->req));
}

/*
 * Encrypt / decrypt data from one bio to another one (can be the same one)
 */
static int crypt_convert(struct crypt_config *cc,
			 struct convert_context *ctx)
{
	int r;

	atomic_set(&ctx->pending, 1);

	while(ctx->idx_in < ctx->bio_in->bi_vcnt &&
	      ctx->idx_out < ctx->bio_out->bi_vcnt) {

		crypt_alloc_req(cc, ctx);

		atomic_inc(&ctx->pending);

		r = crypt_convert_block(cc, ctx, cc->req);

		switch (r) {
		/* async */
		case -EBUSY:
			wait_for_completion(&ctx->restart);
			INIT_COMPLETION(ctx->restart);
			/* fall through*/
		case -EINPROGRESS:
			cc->req = NULL;
			ctx->sector++;
			continue;

		/* sync */
		case 0:
			atomic_dec(&ctx->pending);
			ctx->sector++;
			cond_resched();
			continue;

		/* error */
		default:
			atomic_dec(&ctx->pending);
			return r;
		}
	}

	return 0;
}

static void dm_crypt_bio_destructor(struct bio *bio)
{
	struct dm_crypt_io *io = bio->bi_private;
	struct crypt_config *cc = io->target->private;

	bio_free(bio, cc->bs);
}

/*
 * Generate a new unfragmented bio with the given size
 * This should never violate the device limitations
 * May return a smaller bio when running out of pages, indicated by
 * *out_of_pages set to 1.
 */
static struct bio *crypt_alloc_buffer(struct dm_crypt_io *io, unsigned size,
				      unsigned *out_of_pages)
{
	struct crypt_config *cc = io->target->private;
	struct bio *clone;
	unsigned int nr_iovecs = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	gfp_t gfp_mask = GFP_NOIO | __GFP_HIGHMEM;
	unsigned i, len;
	struct page *page;

	clone = bio_alloc_bioset(GFP_NOIO, nr_iovecs, cc->bs);
	if (!clone)
		return NULL;

	clone_init(io, clone);
	*out_of_pages = 0;

	for (i = 0; i < nr_iovecs; i++) {
		page = mempool_alloc(cc->page_pool, gfp_mask);
		if (!page) {
			*out_of_pages = 1;
			break;
		}

		/*
		 * if additional pages cannot be allocated without waiting,
		 * return a partially allocated bio, the caller will then try
		 * to allocate additional bios while submitting this partial bio
		 */
		if (i == (MIN_BIO_PAGES - 1))
			gfp_mask = (gfp_mask | __GFP_NOWARN) & ~__GFP_WAIT;

		len = (size > PAGE_SIZE) ? PAGE_SIZE : size;

		if (!bio_add_page(clone, page, len, 0)) {
			mempool_free(page, cc->page_pool);
			break;
		}

		size -= len;
	}

	if (!clone->bi_size) {
		bio_put(clone);
		return NULL;
	}

	return clone;
}

static void crypt_free_buffer_pages(struct crypt_config *cc, struct bio *clone)
{
	unsigned int i;
	struct bio_vec *bv;

	for (i = 0; i < clone->bi_vcnt; i++) {
		bv = bio_iovec_idx(clone, i);
		BUG_ON(!bv->bv_page);
		mempool_free(bv->bv_page, cc->page_pool);
		bv->bv_page = NULL;
	}
}

static struct dm_crypt_io *crypt_io_alloc(struct dm_target *ti,
					  struct bio *bio, sector_t sector)
{
	struct crypt_config *cc = ti->private;
	struct dm_crypt_io *io;

	io = mempool_alloc(cc->io_pool, GFP_NOIO);
	io->target = ti;
	io->base_bio = bio;
	io->sector = sector;
	io->error = 0;
	io->base_io = NULL;
	atomic_set(&io->pending, 0);

	return io;
}

static void crypt_inc_pending(struct dm_crypt_io *io)
{
	atomic_inc(&io->pending);
}

/*
 * One of the bios was finished. Check for completion of
 * the whole request and correctly clean up the buffer.
 * If base_io is set, wait for the last fragment to complete.
 */
static void crypt_dec_pending(struct dm_crypt_io *io)
{
	struct crypt_config *cc = io->target->private;
	struct bio *base_bio = io->base_bio;
	struct dm_crypt_io *base_io = io->base_io;
	int error = io->error;

	if (!atomic_dec_and_test(&io->pending))
		return;

	mempool_free(io, cc->io_pool);

	if (likely(!base_io))
		bio_endio(base_bio, error);
	else {
		if (error && !base_io->error)
			base_io->error = error;
		crypt_dec_pending(base_io);
	}
}

/*
 * kcryptd/kcryptd_io:
 *
 * Needed because it would be very unwise to do decryption in an
 * interrupt context.
 *
 * kcryptd performs the actual encryption or decryption.
 *
 * kcryptd_io performs the IO submission.
 *
 * They must be separated as otherwise the final stages could be
 * starved by new requests which can block in the first stages due
 * to memory allocation.
 */
static void crypt_endio(struct bio *clone, int error)
{
	struct dm_crypt_io *io = clone->bi_private;
	struct crypt_config *cc = io->target->private;
	unsigned rw = bio_data_dir(clone);

	if (unlikely(!bio_flagged(clone, BIO_UPTODATE) && !error))
		error = -EIO;

	/*
	 * free the processed pages
	 */
	if (rw == WRITE)
		crypt_free_buffer_pages(cc, clone);

	bio_put(clone);

	if (rw == READ && !error) {
		kcryptd_queue_crypt(io);
		return;
	}

	if (unlikely(error))
		io->error = error;

	crypt_dec_pending(io);
}

static void clone_init(struct dm_crypt_io *io, struct bio *clone)
{
	struct crypt_config *cc = io->target->private;

	clone->bi_private = io;
	clone->bi_end_io  = crypt_endio;
	clone->bi_bdev    = cc->dev->bdev;
	clone->bi_rw      = io->base_bio->bi_rw;
	clone->bi_destructor = dm_crypt_bio_destructor;
}

static void kcryptd_io_read(struct dm_crypt_io *io)
{
	struct crypt_config *cc = io->target->private;
	struct bio *base_bio = io->base_bio;
	struct bio *clone;

	crypt_inc_pending(io);

	/*
	 * The block layer might modify the bvec array, so always
	 * copy the required bvecs because we need the original
	 * one in order to decrypt the whole bio data *afterwards*.
	 */
	clone = bio_alloc_bioset(GFP_NOIO, bio_segments(base_bio), cc->bs);
	if (unlikely(!clone)) {
		io->error = -ENOMEM;
		crypt_dec_pending(io);
		return;
	}

	clone_init(io, clone);
	clone->bi_idx = 0;
	clone->bi_vcnt = bio_segments(base_bio);
	clone->bi_size = base_bio->bi_size;
	clone->bi_sector = cc->start + io->sector;
	memcpy(clone->bi_io_vec, bio_iovec(base_bio),
	       sizeof(struct bio_vec) * clone->bi_vcnt);

	generic_make_request(clone);
}

static void kcryptd_io_write(struct dm_crypt_io *io)
{
	struct bio *clone = io->ctx.bio_out;
	generic_make_request(clone);
}

static void kcryptd_io(struct work_struct *work)
{
	struct dm_crypt_io *io = container_of(work, struct dm_crypt_io, work);

	if (bio_data_dir(io->base_bio) == READ)
		kcryptd_io_read(io);
	else
		kcryptd_io_write(io);
}

static void kcryptd_queue_io(struct dm_crypt_io *io)
{
	struct crypt_config *cc = io->target->private;

	INIT_WORK(&io->work, kcryptd_io);
	queue_work(cc->io_queue, &io->work);
}

static void kcryptd_crypt_write_io_submit(struct dm_crypt_io *io,
					  int error, int async)
{
	struct bio *clone = io->ctx.bio_out;
	struct crypt_config *cc = io->target->private;

	if (unlikely(error < 0)) {
		crypt_free_buffer_pages(cc, clone);
		bio_put(clone);
		io->error = -EIO;
		crypt_dec_pending(io);
		return;
	}

	/* crypt_convert should have filled the clone bio */
	BUG_ON(io->ctx.idx_out < clone->bi_vcnt);

	clone->bi_sector = cc->start + io->sector;

	if (async)
		kcryptd_queue_io(io);
	else
		generic_make_request(clone);
}

static void kcryptd_crypt_write_convert(struct dm_crypt_io *io)
{
	struct crypt_config *cc = io->target->private;
	struct bio *clone;
	struct dm_crypt_io *new_io;
	int crypt_finished;
	unsigned out_of_pages = 0;
	unsigned remaining = io->base_bio->bi_size;
	sector_t sector = io->sector;
	int r;

	/*
	 * Prevent io from disappearing until this function completes.
	 */
	crypt_inc_pending(io);
	crypt_convert_init(cc, &io->ctx, NULL, io->base_bio, sector);

	/*
	 * The allocated buffers can be smaller than the whole bio,
	 * so repeat the whole process until all the data can be handled.
	 */
	while (remaining) {
		clone = crypt_alloc_buffer(io, remaining, &out_of_pages);
		if (unlikely(!clone)) {
			io->error = -ENOMEM;
			break;
		}

		io->ctx.bio_out = clone;
		io->ctx.idx_out = 0;

		remaining -= clone->bi_size;
		sector += bio_sectors(clone);

		crypt_inc_pending(io);
		r = crypt_convert(cc, &io->ctx);
		crypt_finished = atomic_dec_and_test(&io->ctx.pending);

		/* Encryption was already finished, submit io now */
		if (crypt_finished) {
			kcryptd_crypt_write_io_submit(io, r, 0);

			/*
			 * If there was an error, do not try next fragments.
			 * For async, error is processed in async handler.
			 */
			if (unlikely(r < 0))
				break;

			io->sector = sector;
		}

		/*
		 * Out of memory -> run queues
		 * But don't wait if split was due to the io size restriction
		 */
		if (unlikely(out_of_pages))
			congestion_wait(BLK_RW_ASYNC, HZ/100);

		/*
		 * With async crypto it is unsafe to share the crypto context
		 * between fragments, so switch to a new dm_crypt_io structure.
		 */
		if (unlikely(!crypt_finished && remaining)) {
			new_io = crypt_io_alloc(io->target, io->base_bio,
						sector);
			crypt_inc_pending(new_io);
			crypt_convert_init(cc, &new_io->ctx, NULL,
					   io->base_bio, sector);
			new_io->ctx.idx_in = io->ctx.idx_in;
			new_io->ctx.offset_in = io->ctx.offset_in;

			/*
			 * Fragments after the first use the base_io
			 * pending count.
			 */
			if (!io->base_io)
				new_io->base_io = io;
			else {
				new_io->base_io = io->base_io;
				crypt_inc_pending(io->base_io);
				crypt_dec_pending(io);
			}

			io = new_io;
		}
	}

	crypt_dec_pending(io);
}

static void kcryptd_crypt_read_done(struct dm_crypt_io *io, int error)
{
	if (unlikely(error < 0))
		io->error = -EIO;

	crypt_dec_pending(io);
}

static void kcryptd_crypt_read_convert(struct dm_crypt_io *io)
{
	struct crypt_config *cc = io->target->private;
	int r = 0;

	crypt_inc_pending(io);

	crypt_convert_init(cc, &io->ctx, io->base_bio, io->base_bio,
			   io->sector);

	r = crypt_convert(cc, &io->ctx);

	if (atomic_dec_and_test(&io->ctx.pending))
		kcryptd_crypt_read_done(io, r);

	crypt_dec_pending(io);
}

static void kcryptd_async_done(struct crypto_async_request *async_req,
			       int error)
{
	struct dm_crypt_request *dmreq = async_req->data;
	struct convert_context *ctx = dmreq->ctx;
	struct dm_crypt_io *io = container_of(ctx, struct dm_crypt_io, ctx);
	struct crypt_config *cc = io->target->private;

	if (error == -EINPROGRESS) {
		complete(&ctx->restart);
		return;
	}

	mempool_free(req_of_dmreq(cc, dmreq), cc->req_pool);

	if (!atomic_dec_and_test(&ctx->pending))
		return;

	if (bio_data_dir(io->base_bio) == READ)
		kcryptd_crypt_read_done(io, error);
	else
		kcryptd_crypt_write_io_submit(io, error, 1);
}

static void kcryptd_crypt(struct work_struct *work)
{
	struct dm_crypt_io *io = container_of(work, struct dm_crypt_io, work);

	if (bio_data_dir(io->base_bio) == READ)
		kcryptd_crypt_read_convert(io);
	else
		kcryptd_crypt_write_convert(io);
}

static void kcryptd_queue_crypt(struct dm_crypt_io *io)
{
	struct crypt_config *cc = io->target->private;

	INIT_WORK(&io->work, kcryptd_crypt);
	queue_work(cc->crypt_queue, &io->work);
}

/*
 * Decode key from its hex representation
 */
static int crypt_decode_key(u8 *key, char *hex, unsigned int size)
{
	char buffer[3];
	char *endp;
	unsigned int i;

	buffer[2] = '\0';

	for (i = 0; i < size; i++) {
		buffer[0] = *hex++;
		buffer[1] = *hex++;

		key[i] = (u8)simple_strtoul(buffer, &endp, 16);

		if (endp != &buffer[2])
			return -EINVAL;
	}

	if (*hex != '\0')
		return -EINVAL;

	return 0;
}

/*
 * Encode key into its hex representation
 */
static void crypt_encode_key(char *hex, u8 *key, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++) {
		sprintf(hex, "%02x", *key);
		hex += 2;
		key++;
	}
}

static int crypt_set_key(struct crypt_config *cc, char *key)
{
	unsigned key_size = strlen(key) >> 1;

	if (cc->key_size && cc->key_size != key_size)
		return -EINVAL;

	cc->key_size = key_size; /* initial settings */

	if ((!key_size && strcmp(key, "-")) ||
	   (key_size && crypt_decode_key(cc->key, key, key_size) < 0))
		return -EINVAL;

	set_bit(DM_CRYPT_KEY_VALID, &cc->flags);

	return 0;
}

static int crypt_wipe_key(struct crypt_config *cc)
{
	clear_bit(DM_CRYPT_KEY_VALID, &cc->flags);
	memset(&cc->key, 0, cc->key_size * sizeof(u8));
	return 0;
}

/*
 * Construct an encryption mapping:
 * <cipher> <key> <iv_offset> <dev_path> <start>
 */
static int crypt_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct crypt_config *cc;
	struct crypto_ablkcipher *tfm;
	char *tmp;
	char *cipher;
	char *chainmode;
	char *ivmode;
	char *ivopts;
	unsigned int key_size;
	unsigned long long tmpll;

	if (argc != 5) {
		ti->error = "Not enough arguments";
		return -EINVAL;
	}

	tmp = argv[0];
	cipher = strsep(&tmp, "-");
	chainmode = strsep(&tmp, "-");
	ivopts = strsep(&tmp, "-");
	ivmode = strsep(&ivopts, ":");

	if (tmp)
		DMWARN("Unexpected additional cipher options");

	key_size = strlen(argv[1]) >> 1;

 	cc = kzalloc(sizeof(*cc) + key_size * sizeof(u8), GFP_KERNEL);
	if (cc == NULL) {
		ti->error =
			"Cannot allocate transparent encryption context";
		return -ENOMEM;
	}

 	if (crypt_set_key(cc, argv[1])) {
		ti->error = "Error decoding key";
		goto bad_cipher;
	}

	/* Compatiblity mode for old dm-crypt cipher strings */
	if (!chainmode || (strcmp(chainmode, "plain") == 0 && !ivmode)) {
		chainmode = "cbc";
		ivmode = "plain";
	}

	if (strcmp(chainmode, "ecb") && !ivmode) {
		ti->error = "This chaining mode requires an IV mechanism";
		goto bad_cipher;
	}

	if (snprintf(cc->cipher, CRYPTO_MAX_ALG_NAME, "%s(%s)",
		     chainmode, cipher) >= CRYPTO_MAX_ALG_NAME) {
		ti->error = "Chain mode + cipher name is too long";
		goto bad_cipher;
	}

	tfm = crypto_alloc_ablkcipher(cc->cipher, 0, 0);
	if (IS_ERR(tfm)) {
		ti->error = "Error allocating crypto tfm";
		goto bad_cipher;
	}

	strcpy(cc->cipher, cipher);
	strcpy(cc->chainmode, chainmode);
	cc->tfm = tfm;

	/*
	 * Choose ivmode. Valid modes: "plain", "essiv:<esshash>", "benbi".
	 * See comments at iv code
	 */

	if (ivmode == NULL)
		cc->iv_gen_ops = NULL;
	else if (strcmp(ivmode, "plain") == 0)
		cc->iv_gen_ops = &crypt_iv_plain_ops;
	else if (strcmp(ivmode, "essiv") == 0)
		cc->iv_gen_ops = &crypt_iv_essiv_ops;
	else if (strcmp(ivmode, "benbi") == 0)
		cc->iv_gen_ops = &crypt_iv_benbi_ops;
	else if (strcmp(ivmode, "null") == 0)
		cc->iv_gen_ops = &crypt_iv_null_ops;
	else {
		ti->error = "Invalid IV mode";
		goto bad_ivmode;
	}

	if (cc->iv_gen_ops && cc->iv_gen_ops->ctr &&
	    cc->iv_gen_ops->ctr(cc, ti, ivopts) < 0)
		goto bad_ivmode;

	cc->iv_size = crypto_ablkcipher_ivsize(tfm);
	if (cc->iv_size)
		/* at least a 64 bit sector number should fit in our buffer */
		cc->iv_size = max(cc->iv_size,
				  (unsigned int)(sizeof(u64) / sizeof(u8)));
	else {
		if (cc->iv_gen_ops) {
			DMWARN("Selected cipher does not support IVs");
			if (cc->iv_gen_ops->dtr)
				cc->iv_gen_ops->dtr(cc);
			cc->iv_gen_ops = NULL;
		}
	}

	cc->io_pool = mempool_create_slab_pool(MIN_IOS, _crypt_io_pool);
	if (!cc->io_pool) {
		ti->error = "Cannot allocate crypt io mempool";
		goto bad_slab_pool;
	}

	cc->dmreq_start = sizeof(struct ablkcipher_request);
	cc->dmreq_start += crypto_ablkcipher_reqsize(tfm);
	cc->dmreq_start = ALIGN(cc->dmreq_start, crypto_tfm_ctx_alignment());
	cc->dmreq_start += crypto_ablkcipher_alignmask(tfm) &
			   ~(crypto_tfm_ctx_alignment() - 1);

	cc->req_pool = mempool_create_kmalloc_pool(MIN_IOS, cc->dmreq_start +
			sizeof(struct dm_crypt_request) + cc->iv_size);
	if (!cc->req_pool) {
		ti->error = "Cannot allocate crypt request mempool";
		goto bad_req_pool;
	}
	cc->req = NULL;

	cc->page_pool = mempool_create_page_pool(MIN_POOL_PAGES, 0);
	if (!cc->page_pool) {
		ti->error = "Cannot allocate page mempool";
		goto bad_page_pool;
	}

	cc->bs = bioset_create(MIN_IOS, 0);
	if (!cc->bs) {
		ti->error = "Cannot allocate crypt bioset";
		goto bad_bs;
	}

	if (crypto_ablkcipher_setkey(tfm, cc->key, key_size) < 0) {
		ti->error = "Error setting key";
		goto bad_device;
	}

	if (sscanf(argv[2], "%llu", &tmpll) != 1) {
		ti->error = "Invalid iv_offset sector";
		goto bad_device;
	}
	cc->iv_offset = tmpll;

	if (sscanf(argv[4], "%llu", &tmpll) != 1) {
		ti->error = "Invalid device sector";
		goto bad_device;
	}
	cc->start = tmpll;

	if (dm_get_device(ti, argv[3], cc->start, ti->len,
			  dm_table_get_mode(ti->table), &cc->dev)) {
		ti->error = "Device lookup failed";
		goto bad_device;
	}

	if (ivmode && cc->iv_gen_ops) {
		if (ivopts)
			*(ivopts - 1) = ':';
		cc->iv_mode = kmalloc(strlen(ivmode) + 1, GFP_KERNEL);
		if (!cc->iv_mode) {
			ti->error = "Error kmallocing iv_mode string";
			goto bad_ivmode_string;
		}
		strcpy(cc->iv_mode, ivmode);
	} else
		cc->iv_mode = NULL;

	cc->io_queue = create_singlethread_workqueue("kcryptd_io");
	if (!cc->io_queue) {
		ti->error = "Couldn't create kcryptd io queue";
		goto bad_io_queue;
	}

	cc->crypt_queue = create_singlethread_workqueue("kcryptd");
	if (!cc->crypt_queue) {
		ti->error = "Couldn't create kcryptd queue";
		goto bad_crypt_queue;
	}

	ti->num_flush_requests = 1;
	ti->private = cc;
	return 0;

bad_crypt_queue:
	destroy_workqueue(cc->io_queue);
bad_io_queue:
	kfree(cc->iv_mode);
bad_ivmode_string:
	dm_put_device(ti, cc->dev);
bad_device:
	bioset_free(cc->bs);
bad_bs:
	mempool_destroy(cc->page_pool);
bad_page_pool:
	mempool_destroy(cc->req_pool);
bad_req_pool:
	mempool_destroy(cc->io_pool);
bad_slab_pool:
	if (cc->iv_gen_ops && cc->iv_gen_ops->dtr)
		cc->iv_gen_ops->dtr(cc);
bad_ivmode:
	crypto_free_ablkcipher(tfm);
bad_cipher:
	/* Must zero key material before freeing */
	kzfree(cc);
	return -EINVAL;
}

static void crypt_dtr(struct dm_target *ti)
{
	struct crypt_config *cc = (struct crypt_config *) ti->private;

	destroy_workqueue(cc->io_queue);
	destroy_workqueue(cc->crypt_queue);

	if (cc->req)
		mempool_free(cc->req, cc->req_pool);

	bioset_free(cc->bs);
	mempool_destroy(cc->page_pool);
	mempool_destroy(cc->req_pool);
	mempool_destroy(cc->io_pool);

	kfree(cc->iv_mode);
	if (cc->iv_gen_ops && cc->iv_gen_ops->dtr)
		cc->iv_gen_ops->dtr(cc);
	crypto_free_ablkcipher(cc->tfm);
	dm_put_device(ti, cc->dev);

	/* Must zero key material before freeing */
	kzfree(cc);
}

static int crypt_map(struct dm_target *ti, struct bio *bio,
		     union map_info *map_context)
{
	struct dm_crypt_io *io;
	struct crypt_config *cc;

	if (unlikely(bio_empty_barrier(bio))) {
		cc = ti->private;
		bio->bi_bdev = cc->dev->bdev;
		return DM_MAPIO_REMAPPED;
	}

	io = crypt_io_alloc(ti, bio, bio->bi_sector - ti->begin);

	if (bio_data_dir(io->base_bio) == READ)
		kcryptd_queue_io(io);
	else
		kcryptd_queue_crypt(io);

	return DM_MAPIO_SUBMITTED;
}

static int crypt_status(struct dm_target *ti, status_type_t type,
			char *result, unsigned int maxlen)
{
	struct crypt_config *cc = (struct crypt_config *) ti->private;
	unsigned int sz = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		if (cc->iv_mode)
			DMEMIT("%s-%s-%s ", cc->cipher, cc->chainmode,
			       cc->iv_mode);
		else
			DMEMIT("%s-%s ", cc->cipher, cc->chainmode);

		if (cc->key_size > 0) {
			if ((maxlen - sz) < ((cc->key_size << 1) + 1))
				return -ENOMEM;

			crypt_encode_key(result + sz, cc->key, cc->key_size);
			sz += cc->key_size << 1;
		} else {
			if (sz >= maxlen)
				return -ENOMEM;
			result[sz++] = '-';
		}

		DMEMIT(" %llu %s %llu", (unsigned long long)cc->iv_offset,
				cc->dev->name, (unsigned long long)cc->start);
		break;
	}
	return 0;
}

static void crypt_postsuspend(struct dm_target *ti)
{
	struct crypt_config *cc = ti->private;

	set_bit(DM_CRYPT_SUSPENDED, &cc->flags);
}

static int crypt_preresume(struct dm_target *ti)
{
	struct crypt_config *cc = ti->private;

	if (!test_bit(DM_CRYPT_KEY_VALID, &cc->flags)) {
		DMERR("aborting resume - crypt key is not set.");
		return -EAGAIN;
	}

	return 0;
}

static void crypt_resume(struct dm_target *ti)
{
	struct crypt_config *cc = ti->private;

	clear_bit(DM_CRYPT_SUSPENDED, &cc->flags);
}

/* Message interface
 *	key set <key>
 *	key wipe
 */
static int crypt_message(struct dm_target *ti, unsigned argc, char **argv)
{
	struct crypt_config *cc = ti->private;

	if (argc < 2)
		goto error;

	if (!strnicmp(argv[0], MESG_STR("key"))) {
		if (!test_bit(DM_CRYPT_SUSPENDED, &cc->flags)) {
			DMWARN("not suspended during key manipulation.");
			return -EINVAL;
		}
		if (argc == 3 && !strnicmp(argv[1], MESG_STR("set")))
			return crypt_set_key(cc, argv[2]);
		if (argc == 2 && !strnicmp(argv[1], MESG_STR("wipe")))
			return crypt_wipe_key(cc);
	}

error:
	DMWARN("unrecognised message received.");
	return -EINVAL;
}

static int crypt_merge(struct dm_target *ti, struct bvec_merge_data *bvm,
		       struct bio_vec *biovec, int max_size)
{
	struct crypt_config *cc = ti->private;
	struct request_queue *q = bdev_get_queue(cc->dev->bdev);

	if (!q->merge_bvec_fn)
		return max_size;

	bvm->bi_bdev = cc->dev->bdev;
	bvm->bi_sector = cc->start + bvm->bi_sector - ti->begin;

	return min(max_size, q->merge_bvec_fn(q, bvm, biovec));
}

static int crypt_iterate_devices(struct dm_target *ti,
				 iterate_devices_callout_fn fn, void *data)
{
	struct crypt_config *cc = ti->private;

	return fn(ti, cc->dev, cc->start, ti->len, data);
}

static struct target_type crypt_target = {
	.name   = "crypt",
	.version = {1, 7, 0},
	.module = THIS_MODULE,
	.ctr    = crypt_ctr,
	.dtr    = crypt_dtr,
	.map    = crypt_map,
	.status = crypt_status,
	.postsuspend = crypt_postsuspend,
	.preresume = crypt_preresume,
	.resume = crypt_resume,
	.message = crypt_message,
	.merge  = crypt_merge,
	.iterate_devices = crypt_iterate_devices,
};

static int __init dm_crypt_init(void)
{
	int r;

	_crypt_io_pool = KMEM_CACHE(dm_crypt_io, 0);
	if (!_crypt_io_pool)
		return -ENOMEM;

	r = dm_register_target(&crypt_target);
	if (r < 0) {
		DMERR("register failed %d", r);
		kmem_cache_destroy(_crypt_io_pool);
	}

	return r;
}

static void __exit dm_crypt_exit(void)
{
	dm_unregister_target(&crypt_target);
	kmem_cache_destroy(_crypt_io_pool);
}

module_init(dm_crypt_init);
module_exit(dm_crypt_exit);

MODULE_AUTHOR("Christophe Saout <christophe@saout.de>");
MODULE_DESCRIPTION(DM_NAME " target for transparent encryption / decryption");
MODULE_LICENSE("GPL");
