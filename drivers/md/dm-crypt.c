/*
 * Copyright (C) 2003 Christophe Saout <christophe@saout.de>
 * Copyright (C) 2004 Clemens Fruhwirth <clemens@endorphin.org>
 * Copyright (C) 2006 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

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

#include "dm.h"

#define DM_MSG_PREFIX "crypt"
#define MESG_STR(x) x, sizeof(x)

/*
 * per bio private data
 */
struct crypt_io {
	struct dm_target *target;
	struct bio *base_bio;
	struct bio *first_clone;
	struct work_struct work;
	atomic_t pending;
	int error;
	int post_process;
};

/*
 * context holding the current state of a multi-part conversion
 */
struct convert_context {
	struct bio *bio_in;
	struct bio *bio_out;
	unsigned int offset_in;
	unsigned int offset_out;
	unsigned int idx_in;
	unsigned int idx_out;
	sector_t sector;
	int write;
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
	 * pool for per bio private data and
	 * for encryption buffer pages
	 */
	mempool_t *io_pool;
	mempool_t *page_pool;
	struct bio_set *bs;

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

	char cipher[CRYPTO_MAX_ALG_NAME];
	char chainmode[CRYPTO_MAX_ALG_NAME];
	struct crypto_blkcipher *tfm;
	unsigned long flags;
	unsigned int key_size;
	u8 key[0];
};

#define MIN_IOS        16
#define MIN_POOL_PAGES 32
#define MIN_BIO_PAGES  8

static struct kmem_cache *_crypt_io_pool;

/*
 * Different IV generation algorithms:
 *
 * plain: the initial vector is the 32-bit little-endian version of the sector
 *        number, padded with zeros if neccessary.
 *
 * essiv: "encrypted sector|salt initial vector", the sector number is
 *        encrypted with the bulk cipher using a salt as key. The salt
 *        should be derived from the bulk cipher's key via hashing.
 *
 * benbi: the 64-bit "big-endian 'narrow block'-count", starting at 1
 *        (needed for LRW-32-AES and possible other narrow block modes)
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

	sg_set_buf(&sg, cc->key, cc->key_size);
	desc.tfm = hash_tfm;
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;
	err = crypto_hash_digest(&desc, &sg, cc->key_size, salt);
	crypto_free_hash(hash_tfm);

	if (err) {
		ti->error = "Error calculating hash in ESSIV";
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
	    crypto_blkcipher_ivsize(cc->tfm)) {
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
	unsigned int bs = crypto_blkcipher_blocksize(cc->tfm);
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

static int
crypt_convert_scatterlist(struct crypt_config *cc, struct scatterlist *out,
                          struct scatterlist *in, unsigned int length,
                          int write, sector_t sector)
{
	u8 iv[cc->iv_size] __attribute__ ((aligned(__alignof__(u64))));
	struct blkcipher_desc desc = {
		.tfm = cc->tfm,
		.info = iv,
		.flags = CRYPTO_TFM_REQ_MAY_SLEEP,
	};
	int r;

	if (cc->iv_gen_ops) {
		r = cc->iv_gen_ops->generator(cc, iv, sector);
		if (r < 0)
			return r;

		if (write)
			r = crypto_blkcipher_encrypt_iv(&desc, out, in, length);
		else
			r = crypto_blkcipher_decrypt_iv(&desc, out, in, length);
	} else {
		if (write)
			r = crypto_blkcipher_encrypt(&desc, out, in, length);
		else
			r = crypto_blkcipher_decrypt(&desc, out, in, length);
	}

	return r;
}

static void
crypt_convert_init(struct crypt_config *cc, struct convert_context *ctx,
                   struct bio *bio_out, struct bio *bio_in,
                   sector_t sector, int write)
{
	ctx->bio_in = bio_in;
	ctx->bio_out = bio_out;
	ctx->offset_in = 0;
	ctx->offset_out = 0;
	ctx->idx_in = bio_in ? bio_in->bi_idx : 0;
	ctx->idx_out = bio_out ? bio_out->bi_idx : 0;
	ctx->sector = sector + cc->iv_offset;
	ctx->write = write;
}

/*
 * Encrypt / decrypt data from one bio to another one (can be the same one)
 */
static int crypt_convert(struct crypt_config *cc,
                         struct convert_context *ctx)
{
	int r = 0;

	while(ctx->idx_in < ctx->bio_in->bi_vcnt &&
	      ctx->idx_out < ctx->bio_out->bi_vcnt) {
		struct bio_vec *bv_in = bio_iovec_idx(ctx->bio_in, ctx->idx_in);
		struct bio_vec *bv_out = bio_iovec_idx(ctx->bio_out, ctx->idx_out);
		struct scatterlist sg_in = {
			.page = bv_in->bv_page,
			.offset = bv_in->bv_offset + ctx->offset_in,
			.length = 1 << SECTOR_SHIFT
		};
		struct scatterlist sg_out = {
			.page = bv_out->bv_page,
			.offset = bv_out->bv_offset + ctx->offset_out,
			.length = 1 << SECTOR_SHIFT
		};

		ctx->offset_in += sg_in.length;
		if (ctx->offset_in >= bv_in->bv_len) {
			ctx->offset_in = 0;
			ctx->idx_in++;
		}

		ctx->offset_out += sg_out.length;
		if (ctx->offset_out >= bv_out->bv_len) {
			ctx->offset_out = 0;
			ctx->idx_out++;
		}

		r = crypt_convert_scatterlist(cc, &sg_out, &sg_in, sg_in.length,
		                              ctx->write, ctx->sector);
		if (r < 0)
			break;

		ctx->sector++;
	}

	return r;
}

 static void dm_crypt_bio_destructor(struct bio *bio)
 {
	struct crypt_io *io = bio->bi_private;
	struct crypt_config *cc = io->target->private;

	bio_free(bio, cc->bs);
 }

/*
 * Generate a new unfragmented bio with the given size
 * This should never violate the device limitations
 * May return a smaller bio when running out of pages
 */
static struct bio *
crypt_alloc_buffer(struct crypt_config *cc, unsigned int size,
                   struct bio *base_bio, unsigned int *bio_vec_idx)
{
	struct bio *clone;
	unsigned int nr_iovecs = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	gfp_t gfp_mask = GFP_NOIO | __GFP_HIGHMEM;
	unsigned int i;

	if (base_bio) {
		clone = bio_alloc_bioset(GFP_NOIO, base_bio->bi_max_vecs, cc->bs);
		__bio_clone(clone, base_bio);
	} else
		clone = bio_alloc_bioset(GFP_NOIO, nr_iovecs, cc->bs);

	if (!clone)
		return NULL;

	clone->bi_destructor = dm_crypt_bio_destructor;

	/* if the last bio was not complete, continue where that one ended */
	clone->bi_idx = *bio_vec_idx;
	clone->bi_vcnt = *bio_vec_idx;
	clone->bi_size = 0;
	clone->bi_flags &= ~(1 << BIO_SEG_VALID);

	/* clone->bi_idx pages have already been allocated */
	size -= clone->bi_idx * PAGE_SIZE;

	for (i = clone->bi_idx; i < nr_iovecs; i++) {
		struct bio_vec *bv = bio_iovec_idx(clone, i);

		bv->bv_page = mempool_alloc(cc->page_pool, gfp_mask);
		if (!bv->bv_page)
			break;

		/*
		 * if additional pages cannot be allocated without waiting,
		 * return a partially allocated bio, the caller will then try
		 * to allocate additional bios while submitting this partial bio
		 */
		if ((i - clone->bi_idx) == (MIN_BIO_PAGES - 1))
			gfp_mask = (gfp_mask | __GFP_NOWARN) & ~__GFP_WAIT;

		bv->bv_offset = 0;
		if (size > PAGE_SIZE)
			bv->bv_len = PAGE_SIZE;
		else
			bv->bv_len = size;

		clone->bi_size += bv->bv_len;
		clone->bi_vcnt++;
		size -= bv->bv_len;
	}

	if (!clone->bi_size) {
		bio_put(clone);
		return NULL;
	}

	/*
	 * Remember the last bio_vec allocated to be able
	 * to correctly continue after the splitting.
	 */
	*bio_vec_idx = clone->bi_vcnt;

	return clone;
}

static void crypt_free_buffer_pages(struct crypt_config *cc,
                                    struct bio *clone, unsigned int bytes)
{
	unsigned int i, start, end;
	struct bio_vec *bv;

	/*
	 * This is ugly, but Jens Axboe thinks that using bi_idx in the
	 * endio function is too dangerous at the moment, so I calculate the
	 * correct position using bi_vcnt and bi_size.
	 * The bv_offset and bv_len fields might already be modified but we
	 * know that we always allocated whole pages.
	 * A fix to the bi_idx issue in the kernel is in the works, so
	 * we will hopefully be able to revert to the cleaner solution soon.
	 */
	i = clone->bi_vcnt - 1;
	bv = bio_iovec_idx(clone, i);
	end = (i << PAGE_SHIFT) + (bv->bv_offset + bv->bv_len) - clone->bi_size;
	start = end - bytes;

	start >>= PAGE_SHIFT;
	if (!clone->bi_size)
		end = clone->bi_vcnt;
	else
		end >>= PAGE_SHIFT;

	for (i = start; i < end; i++) {
		bv = bio_iovec_idx(clone, i);
		BUG_ON(!bv->bv_page);
		mempool_free(bv->bv_page, cc->page_pool);
		bv->bv_page = NULL;
	}
}

/*
 * One of the bios was finished. Check for completion of
 * the whole request and correctly clean up the buffer.
 */
static void dec_pending(struct crypt_io *io, int error)
{
	struct crypt_config *cc = (struct crypt_config *) io->target->private;

	if (error < 0)
		io->error = error;

	if (!atomic_dec_and_test(&io->pending))
		return;

	if (io->first_clone)
		bio_put(io->first_clone);

	bio_endio(io->base_bio, io->base_bio->bi_size, io->error);

	mempool_free(io, cc->io_pool);
}

/*
 * kcryptd:
 *
 * Needed because it would be very unwise to do decryption in an
 * interrupt context.
 */
static struct workqueue_struct *_kcryptd_workqueue;
static void kcryptd_do_work(struct work_struct *work);

static void kcryptd_queue_io(struct crypt_io *io)
{
	INIT_WORK(&io->work, kcryptd_do_work);
	queue_work(_kcryptd_workqueue, &io->work);
}

static int crypt_endio(struct bio *clone, unsigned int done, int error)
{
	struct crypt_io *io = clone->bi_private;
	struct crypt_config *cc = io->target->private;
	unsigned read_io = bio_data_dir(clone) == READ;

	/*
	 * free the processed pages, even if
	 * it's only a partially completed write
	 */
	if (!read_io)
		crypt_free_buffer_pages(cc, clone, done);

	/* keep going - not finished yet */
	if (unlikely(clone->bi_size))
		return 1;

	if (!read_io)
		goto out;

	if (unlikely(!bio_flagged(clone, BIO_UPTODATE))) {
		error = -EIO;
		goto out;
	}

	bio_put(clone);
	io->post_process = 1;
	kcryptd_queue_io(io);
	return 0;

out:
	bio_put(clone);
	dec_pending(io, error);
	return error;
}

static void clone_init(struct crypt_io *io, struct bio *clone)
{
	struct crypt_config *cc = io->target->private;

	clone->bi_private = io;
	clone->bi_end_io  = crypt_endio;
	clone->bi_bdev    = cc->dev->bdev;
	clone->bi_rw      = io->base_bio->bi_rw;
}

static void process_read(struct crypt_io *io)
{
	struct crypt_config *cc = io->target->private;
	struct bio *base_bio = io->base_bio;
	struct bio *clone;
	sector_t sector = base_bio->bi_sector - io->target->begin;

	atomic_inc(&io->pending);

	/*
	 * The block layer might modify the bvec array, so always
	 * copy the required bvecs because we need the original
	 * one in order to decrypt the whole bio data *afterwards*.
	 */
	clone = bio_alloc_bioset(GFP_NOIO, bio_segments(base_bio), cc->bs);
	if (unlikely(!clone)) {
		dec_pending(io, -ENOMEM);
		return;
	}

	clone_init(io, clone);
	clone->bi_destructor = dm_crypt_bio_destructor;
	clone->bi_idx = 0;
	clone->bi_vcnt = bio_segments(base_bio);
	clone->bi_size = base_bio->bi_size;
	clone->bi_sector = cc->start + sector;
	memcpy(clone->bi_io_vec, bio_iovec(base_bio),
	       sizeof(struct bio_vec) * clone->bi_vcnt);

	generic_make_request(clone);
}

static void process_write(struct crypt_io *io)
{
	struct crypt_config *cc = io->target->private;
	struct bio *base_bio = io->base_bio;
	struct bio *clone;
	struct convert_context ctx;
	unsigned remaining = base_bio->bi_size;
	sector_t sector = base_bio->bi_sector - io->target->begin;
	unsigned bvec_idx = 0;

	atomic_inc(&io->pending);

	crypt_convert_init(cc, &ctx, NULL, base_bio, sector, 1);

	/*
	 * The allocated buffers can be smaller than the whole bio,
	 * so repeat the whole process until all the data can be handled.
	 */
	while (remaining) {
		clone = crypt_alloc_buffer(cc, base_bio->bi_size,
					   io->first_clone, &bvec_idx);
		if (unlikely(!clone)) {
			dec_pending(io, -ENOMEM);
			return;
		}

		ctx.bio_out = clone;

		if (unlikely(crypt_convert(cc, &ctx) < 0)) {
			crypt_free_buffer_pages(cc, clone, clone->bi_size);
			bio_put(clone);
			dec_pending(io, -EIO);
			return;
		}

		clone_init(io, clone);
		clone->bi_sector = cc->start + sector;

		if (!io->first_clone) {
			/*
			 * hold a reference to the first clone, because it
			 * holds the bio_vec array and that can't be freed
			 * before all other clones are released
			 */
			bio_get(clone);
			io->first_clone = clone;
		}

		remaining -= clone->bi_size;
		sector += bio_sectors(clone);

		/* prevent bio_put of first_clone */
		if (remaining)
			atomic_inc(&io->pending);

		generic_make_request(clone);

		/* out of memory -> run queues */
		if (remaining)
			congestion_wait(bio_data_dir(clone), HZ/100);
	}
}

static void process_read_endio(struct crypt_io *io)
{
	struct crypt_config *cc = io->target->private;
	struct convert_context ctx;

	crypt_convert_init(cc, &ctx, io->base_bio, io->base_bio,
			   io->base_bio->bi_sector - io->target->begin, 0);

	dec_pending(io, crypt_convert(cc, &ctx));
}

static void kcryptd_do_work(struct work_struct *work)
{
	struct crypt_io *io = container_of(work, struct crypt_io, work);

	if (io->post_process)
		process_read_endio(io);
	else if (bio_data_dir(io->base_bio) == READ)
		process_read(io);
	else
		process_write(io);
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
	struct crypto_blkcipher *tfm;
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
		goto bad1;
	}

	/* Compatiblity mode for old dm-crypt cipher strings */
	if (!chainmode || (strcmp(chainmode, "plain") == 0 && !ivmode)) {
		chainmode = "cbc";
		ivmode = "plain";
	}

	if (strcmp(chainmode, "ecb") && !ivmode) {
		ti->error = "This chaining mode requires an IV mechanism";
		goto bad1;
	}

	if (snprintf(cc->cipher, CRYPTO_MAX_ALG_NAME, "%s(%s)", chainmode, 
		     cipher) >= CRYPTO_MAX_ALG_NAME) {
		ti->error = "Chain mode + cipher name is too long";
		goto bad1;
	}

	tfm = crypto_alloc_blkcipher(cc->cipher, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		ti->error = "Error allocating crypto tfm";
		goto bad1;
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
	else {
		ti->error = "Invalid IV mode";
		goto bad2;
	}

	if (cc->iv_gen_ops && cc->iv_gen_ops->ctr &&
	    cc->iv_gen_ops->ctr(cc, ti, ivopts) < 0)
		goto bad2;

	cc->iv_size = crypto_blkcipher_ivsize(tfm);
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
		goto bad3;
	}

	cc->page_pool = mempool_create_page_pool(MIN_POOL_PAGES, 0);
	if (!cc->page_pool) {
		ti->error = "Cannot allocate page mempool";
		goto bad4;
	}

	cc->bs = bioset_create(MIN_IOS, MIN_IOS, 4);
	if (!cc->bs) {
		ti->error = "Cannot allocate crypt bioset";
		goto bad_bs;
	}

	if (crypto_blkcipher_setkey(tfm, cc->key, key_size) < 0) {
		ti->error = "Error setting key";
		goto bad5;
	}

	if (sscanf(argv[2], "%llu", &tmpll) != 1) {
		ti->error = "Invalid iv_offset sector";
		goto bad5;
	}
	cc->iv_offset = tmpll;

	if (sscanf(argv[4], "%llu", &tmpll) != 1) {
		ti->error = "Invalid device sector";
		goto bad5;
	}
	cc->start = tmpll;

	if (dm_get_device(ti, argv[3], cc->start, ti->len,
	                  dm_table_get_mode(ti->table), &cc->dev)) {
		ti->error = "Device lookup failed";
		goto bad5;
	}

	if (ivmode && cc->iv_gen_ops) {
		if (ivopts)
			*(ivopts - 1) = ':';
		cc->iv_mode = kmalloc(strlen(ivmode) + 1, GFP_KERNEL);
		if (!cc->iv_mode) {
			ti->error = "Error kmallocing iv_mode string";
			goto bad5;
		}
		strcpy(cc->iv_mode, ivmode);
	} else
		cc->iv_mode = NULL;

	ti->private = cc;
	return 0;

bad5:
	bioset_free(cc->bs);
bad_bs:
	mempool_destroy(cc->page_pool);
bad4:
	mempool_destroy(cc->io_pool);
bad3:
	if (cc->iv_gen_ops && cc->iv_gen_ops->dtr)
		cc->iv_gen_ops->dtr(cc);
bad2:
	crypto_free_blkcipher(tfm);
bad1:
	/* Must zero key material before freeing */
	memset(cc, 0, sizeof(*cc) + cc->key_size * sizeof(u8));
	kfree(cc);
	return -EINVAL;
}

static void crypt_dtr(struct dm_target *ti)
{
	struct crypt_config *cc = (struct crypt_config *) ti->private;

	bioset_free(cc->bs);
	mempool_destroy(cc->page_pool);
	mempool_destroy(cc->io_pool);

	kfree(cc->iv_mode);
	if (cc->iv_gen_ops && cc->iv_gen_ops->dtr)
		cc->iv_gen_ops->dtr(cc);
	crypto_free_blkcipher(cc->tfm);
	dm_put_device(ti, cc->dev);

	/* Must zero key material before freeing */
	memset(cc, 0, sizeof(*cc) + cc->key_size * sizeof(u8));
	kfree(cc);
}

static int crypt_map(struct dm_target *ti, struct bio *bio,
		     union map_info *map_context)
{
	struct crypt_config *cc = ti->private;
	struct crypt_io *io;

	io = mempool_alloc(cc->io_pool, GFP_NOIO);
	io->target = ti;
	io->base_bio = bio;
	io->first_clone = NULL;
	io->error = io->post_process = 0;
	atomic_set(&io->pending, 0);
	kcryptd_queue_io(io);

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

static struct target_type crypt_target = {
	.name   = "crypt",
	.version= {1, 3, 0},
	.module = THIS_MODULE,
	.ctr    = crypt_ctr,
	.dtr    = crypt_dtr,
	.map    = crypt_map,
	.status = crypt_status,
	.postsuspend = crypt_postsuspend,
	.preresume = crypt_preresume,
	.resume = crypt_resume,
	.message = crypt_message,
};

static int __init dm_crypt_init(void)
{
	int r;

	_crypt_io_pool = kmem_cache_create("dm-crypt_io",
	                                   sizeof(struct crypt_io),
	                                   0, 0, NULL, NULL);
	if (!_crypt_io_pool)
		return -ENOMEM;

	_kcryptd_workqueue = create_workqueue("kcryptd");
	if (!_kcryptd_workqueue) {
		r = -ENOMEM;
		DMERR("couldn't create kcryptd");
		goto bad1;
	}

	r = dm_register_target(&crypt_target);
	if (r < 0) {
		DMERR("register failed %d", r);
		goto bad2;
	}

	return 0;

bad2:
	destroy_workqueue(_kcryptd_workqueue);
bad1:
	kmem_cache_destroy(_crypt_io_pool);
	return r;
}

static void __exit dm_crypt_exit(void)
{
	int r = dm_unregister_target(&crypt_target);

	if (r < 0)
		DMERR("unregister failed %d", r);

	destroy_workqueue(_kcryptd_workqueue);
	kmem_cache_destroy(_crypt_io_pool);
}

module_init(dm_crypt_init);
module_exit(dm_crypt_exit);

MODULE_AUTHOR("Christophe Saout <christophe@saout.de>");
MODULE_DESCRIPTION(DM_NAME " target for transparent encryption / decryption");
MODULE_LICENSE("GPL");
