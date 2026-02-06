// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Google, Inc.
 *
 * Author: Sami Tolvanen <samitolvanen@google.com>
 */

#include "dm-verity-fec.h"
#include <linux/math64.h>

#define DM_MSG_PREFIX	"verity-fec"

/*
 * When correcting a block, the FEC implementation performs optimally when it
 * can collect all the associated RS codewords at the same time.  As each byte
 * is part of a different codeword, there are '1 << data_dev_block_bits'
 * codewords.  Each buffer has space for the message bytes for
 * '1 << DM_VERITY_FEC_BUF_RS_BITS' codewords, so that gives
 * '1 << (data_dev_block_bits - DM_VERITY_FEC_BUF_RS_BITS)' buffers.
 */
static inline unsigned int fec_max_nbufs(struct dm_verity *v)
{
	return 1 << (v->data_dev_block_bits - DM_VERITY_FEC_BUF_RS_BITS);
}

/* Loop over each allocated buffer. */
#define fec_for_each_buffer(io, __i) \
	for (__i = 0; __i < (io)->nbufs; __i++)

/* Loop over each RS message in each allocated buffer. */
/* To stop early, use 'goto', not 'break' (since this uses nested loops). */
#define fec_for_each_buffer_rs_message(io, __i, __j) \
	fec_for_each_buffer(io, __i) \
		for (__j = 0; __j < 1 << DM_VERITY_FEC_BUF_RS_BITS; __j++)

/*
 * Return a pointer to the current RS message when called inside
 * fec_for_each_buffer_rs_message.
 */
static inline u8 *fec_buffer_rs_message(struct dm_verity *v,
					struct dm_verity_fec_io *fio,
					unsigned int i, unsigned int j)
{
	return &fio->bufs[i][j * v->fec->rs_k];
}

/*
 * Decode all RS codewords whose message bytes were loaded into fio->bufs.  Copy
 * the corrected bytes into fio->output starting from out_pos.
 */
static int fec_decode_bufs(struct dm_verity *v, struct dm_verity_io *io,
			   struct dm_verity_fec_io *fio, u64 target_block,
			   unsigned int target_region, u64 index_in_region,
			   unsigned int out_pos, int neras)
{
	int r = 0, corrected = 0, res;
	struct dm_buffer *buf;
	unsigned int n, i, j, parity_pos, to_copy;
	uint16_t par_buf[DM_VERITY_FEC_MAX_ROOTS];
	u8 *par, *msg_buf;
	u64 parity_block;
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);

	/*
	 * Compute the index of the first parity block that will be needed and
	 * the starting position in that block.  Then read that block.
	 *
	 * block_size is always a power of 2, but roots might not be.  Note that
	 * when it's not, a codeword's parity bytes can span a block boundary.
	 */
	parity_block = ((index_in_region << v->data_dev_block_bits) + out_pos) *
		       v->fec->roots;
	parity_pos = parity_block & (v->fec->block_size - 1);
	parity_block >>= v->data_dev_block_bits;
	par = dm_bufio_read_with_ioprio(v->fec->bufio, parity_block, &buf,
					bio->bi_ioprio);
	if (IS_ERR(par)) {
		DMERR("%s: FEC %llu: parity read failed (block %llu): %ld",
		      v->data_dev->name, target_block, parity_block,
		      PTR_ERR(par));
		return PTR_ERR(par);
	}

	/*
	 * Decode the RS codewords whose message bytes are in bufs. Each RS
	 * codeword results in one corrected target byte and consumes fec->roots
	 * parity bytes.
	 */
	fec_for_each_buffer_rs_message(fio, n, i) {
		msg_buf = fec_buffer_rs_message(v, fio, n, i);

		/*
		 * Copy the next 'roots' parity bytes to 'par_buf', reading
		 * another parity block if needed.
		 */
		to_copy = min(v->fec->block_size - parity_pos, v->fec->roots);
		for (j = 0; j < to_copy; j++)
			par_buf[j] = par[parity_pos++];
		if (to_copy < v->fec->roots) {
			parity_block++;
			parity_pos = 0;

			dm_bufio_release(buf);
			par = dm_bufio_read_with_ioprio(v->fec->bufio,
							parity_block, &buf,
							bio->bi_ioprio);
			if (IS_ERR(par)) {
				DMERR("%s: FEC %llu: parity read failed (block %llu): %ld",
				      v->data_dev->name, target_block,
				      parity_block, PTR_ERR(par));
				return PTR_ERR(par);
			}
			for (; j < v->fec->roots; j++)
				par_buf[j] = par[parity_pos++];
		}

		/* Decode an RS codeword using the Reed-Solomon library. */
		res = decode_rs8(fio->rs, msg_buf, par_buf, v->fec->rs_k,
				 NULL, neras, fio->erasures, 0, NULL);
		if (res < 0) {
			r = res;
			goto done;
		}
		corrected += res;
		fio->output[out_pos++] = msg_buf[target_region];

		if (out_pos >= v->fec->block_size)
			goto done;
	}
done:
	dm_bufio_release(buf);

	if (r < 0 && neras)
		DMERR_LIMIT("%s: FEC %llu: failed to correct: %d",
			    v->data_dev->name, target_block, r);
	else if (r == 0 && corrected > 0)
		DMWARN_LIMIT("%s: FEC %llu: corrected %d errors",
			     v->data_dev->name, target_block, corrected);

	return r;
}

/*
 * Locate data block erasures using verity hashes.
 */
static int fec_is_erasure(struct dm_verity *v, struct dm_verity_io *io,
			  const u8 *want_digest, const u8 *data)
{
	if (unlikely(verity_hash(v, io, data, v->fec->block_size,
				 io->tmp_digest)))
		return 0;

	return memcmp(io->tmp_digest, want_digest, v->digest_size) != 0;
}

/*
 * Read the message block at index @index_in_region within each of the
 * @v->fec->rs_k regions and deinterleave their contents into @io->fec_io->bufs.
 *
 * @target_block gives the index of specific block within this sequence that is
 * being corrected, relative to the start of all the FEC message blocks.
 *
 * @out_pos gives the current output position, i.e. the position in (each) block
 * from which to start the deinterleaving.  Deinterleaving continues until
 * either end-of-block is reached or there's no more buffer space.
 *
 * If @neras is non-NULL, then also use verity hashes and the presence/absence
 * of I/O errors to determine which of the message blocks in the sequence are
 * likely to be incorrect.  Write the number of such blocks to *@neras and the
 * indices of the corresponding RS message bytes in [0, k - 1] to
 * @io->fec_io->erasures, up to a limit of @v->fec->roots + 1 such blocks.
 */
static int fec_read_bufs(struct dm_verity *v, struct dm_verity_io *io,
			 u64 target_block, u64 index_in_region,
			 unsigned int out_pos, int *neras)
{
	bool is_zero;
	int i, j;
	struct dm_buffer *buf;
	struct dm_bufio_client *bufio;
	struct dm_verity_fec_io *fio = io->fec_io;
	u64 block;
	u8 *bbuf;
	u8 want_digest[HASH_MAX_DIGESTSIZE];
	unsigned int n, src_pos;
	struct bio *bio = dm_bio_from_per_bio_data(io, v->ti->per_io_data_size);

	if (neras)
		*neras = 0;

	if (WARN_ON(v->digest_size > sizeof(want_digest)))
		return -EINVAL;

	for (i = 0; i < v->fec->rs_k; i++) {
		/*
		 * Read the block from region i.  It contains the i'th message
		 * byte of the target block's RS codewords.
		 */
		block = i * v->fec->region_blocks + index_in_region;
		bufio = v->fec->data_bufio;

		if (block >= v->data_blocks) {
			block -= v->data_blocks;

			/*
			 * blocks outside the area were assumed to contain
			 * zeros when encoding data was generated
			 */
			if (unlikely(block >= v->fec->hash_blocks))
				continue;

			block += v->hash_start;
			bufio = v->bufio;
		}

		bbuf = dm_bufio_read_with_ioprio(bufio, block, &buf, bio->bi_ioprio);
		if (IS_ERR(bbuf)) {
			DMWARN_LIMIT("%s: FEC %llu: read failed (%llu): %ld",
				     v->data_dev->name, target_block, block,
				     PTR_ERR(bbuf));

			/* assume the block is corrupted */
			if (neras && *neras <= v->fec->roots)
				fio->erasures[(*neras)++] = i;

			continue;
		}

		/* locate erasures if the block is on the data device */
		if (bufio == v->fec->data_bufio &&
		    verity_hash_for_block(v, io, block, want_digest,
					  &is_zero) == 0) {
			/* skip known zero blocks entirely */
			if (is_zero)
				goto done;

			/*
			 * skip if we have already found the theoretical
			 * maximum number (i.e. fec->roots) of erasures
			 */
			if (neras && *neras <= v->fec->roots &&
			    fec_is_erasure(v, io, want_digest, bbuf))
				fio->erasures[(*neras)++] = i;
		}

		/*
		 * Deinterleave the bytes of the block, starting from 'out_pos',
		 * into the i'th byte of the RS message buffers.  Stop when
		 * end-of-block is reached or there are no more buffers.
		 */
		src_pos = out_pos;
		fec_for_each_buffer_rs_message(fio, n, j) {
			if (src_pos >= v->fec->block_size)
				goto done;
			fec_buffer_rs_message(v, fio, n, j)[i] = bbuf[src_pos++];
		}
done:
		dm_bufio_release(buf);
	}
	return 0;
}

/*
 * Allocate and initialize a struct dm_verity_fec_io to use for FEC for a bio.
 * This runs the first time a block needs to be corrected for a bio.  In the
 * common case where no block needs to be corrected, this code never runs.
 *
 * This always succeeds, as all required allocations are done from mempools.
 * Additional buffers are also allocated opportunistically to improve error
 * correction performance, but these aren't required to succeed.
 */
static struct dm_verity_fec_io *fec_alloc_and_init_io(struct dm_verity *v)
{
	const unsigned int max_nbufs = fec_max_nbufs(v);
	struct dm_verity_fec *f = v->fec;
	struct dm_verity_fec_io *fio;
	unsigned int n;

	fio = mempool_alloc(&f->fio_pool, GFP_NOIO);
	fio->rs = mempool_alloc(&f->rs_pool, GFP_NOIO);

	fio->bufs[0] = mempool_alloc(&f->prealloc_pool, GFP_NOIO);

	/* try to allocate the maximum number of buffers */
	for (n = 1; n < max_nbufs; n++) {
		fio->bufs[n] = kmem_cache_alloc(f->cache, GFP_NOWAIT);
		/* we can manage with even one buffer if necessary */
		if (unlikely(!fio->bufs[n]))
			break;
	}
	fio->nbufs = n;

	fio->output = mempool_alloc(&f->output_pool, GFP_NOIO);
	fio->level = 0;
	return fio;
}

/*
 * Initialize buffers and clear erasures. fec_read_bufs() assumes buffers are
 * zeroed before deinterleaving.
 */
static void fec_init_bufs(struct dm_verity *v, struct dm_verity_fec_io *fio)
{
	unsigned int n;

	fec_for_each_buffer(fio, n)
		memset(fio->bufs[n], 0, v->fec->rs_k << DM_VERITY_FEC_BUF_RS_BITS);

	memset(fio->erasures, 0, sizeof(fio->erasures));
}

/*
 * Try to correct the message (data or hash) block at index @target_block.
 *
 * If @use_erasures is true, use verity hashes to locate erasures.  This makes
 * the error correction slower but up to twice as capable.
 *
 * On success, return 0 and write the corrected block to @fio->output.  0 is
 * returned only if the digest of the corrected block matches @want_digest; this
 * is critical to ensure that FEC can't cause dm-verity to return bad data.
 */
static int fec_decode(struct dm_verity *v, struct dm_verity_io *io,
		      struct dm_verity_fec_io *fio, u64 target_block,
		      const u8 *want_digest, bool use_erasures)
{
	int r, neras = 0;
	unsigned int target_region, out_pos;
	u64 index_in_region;

	/*
	 * Compute 'target_region', the index of the region the target block is
	 * in; and 'index_in_region', the index of the target block within its
	 * region.  The latter value is also the index within its region of each
	 * message block that shares its RS codewords with the target block.
	 */
	target_region = div64_u64_rem(target_block, v->fec->region_blocks,
				      &index_in_region);
	if (WARN_ON_ONCE(target_region >= v->fec->rs_k))
		/* target_block is out-of-bounds.  Should never happen. */
		return -EIO;

	for (out_pos = 0; out_pos < v->fec->block_size;) {
		fec_init_bufs(v, fio);

		r = fec_read_bufs(v, io, target_block, index_in_region, out_pos,
				  use_erasures ? &neras : NULL);
		if (unlikely(r < 0))
			return r;

		r = fec_decode_bufs(v, io, fio, target_block, target_region,
				    index_in_region, out_pos, neras);
		if (r < 0)
			return r;

		out_pos += fio->nbufs << DM_VERITY_FEC_BUF_RS_BITS;
	}

	/* Always re-validate the corrected block against the expected hash */
	r = verity_hash(v, io, fio->output, v->fec->block_size, io->tmp_digest);
	if (unlikely(r < 0))
		return r;

	if (memcmp(io->tmp_digest, want_digest, v->digest_size)) {
		DMERR_LIMIT("%s: FEC %llu: failed to correct (%d erasures)",
			    v->data_dev->name, target_block, neras);
		return -EILSEQ;
	}

	return 0;
}

/* Correct errors in a block. Copies corrected block to dest. */
int verity_fec_decode(struct dm_verity *v, struct dm_verity_io *io,
		      enum verity_block_type type, const u8 *want_digest,
		      sector_t block, u8 *dest)
{
	int r;
	struct dm_verity_fec_io *fio;

	if (!verity_fec_is_enabled(v))
		return -EOPNOTSUPP;

	fio = io->fec_io;
	if (!fio)
		fio = io->fec_io = fec_alloc_and_init_io(v);

	if (fio->level)
		return -EIO;

	fio->level++;

	if (type == DM_VERITY_BLOCK_TYPE_METADATA)
		block = block - v->hash_start + v->data_blocks;

	/*
	 * Locating erasures is slow, so attempt to recover the block without
	 * them first. Do a second attempt with erasures if the corruption is
	 * bad enough.
	 */
	r = fec_decode(v, io, fio, block, want_digest, false);
	if (r < 0) {
		r = fec_decode(v, io, fio, block, want_digest, true);
		if (r < 0)
			goto done;
	}

	memcpy(dest, fio->output, v->fec->block_size);
	atomic64_inc(&v->fec->corrected);

done:
	fio->level--;
	return r;
}

/*
 * Clean up per-bio data.
 */
void __verity_fec_finish_io(struct dm_verity_io *io)
{
	unsigned int n;
	struct dm_verity_fec *f = io->v->fec;
	struct dm_verity_fec_io *fio = io->fec_io;

	mempool_free(fio->rs, &f->rs_pool);

	mempool_free(fio->bufs[0], &f->prealloc_pool);

	for (n = 1; n < fio->nbufs; n++)
		kmem_cache_free(f->cache, fio->bufs[n]);

	mempool_free(fio->output, &f->output_pool);

	mempool_free(fio, &f->fio_pool);
	io->fec_io = NULL;
}

/*
 * Append feature arguments and values to the status table.
 */
unsigned int verity_fec_status_table(struct dm_verity *v, unsigned int sz,
				 char *result, unsigned int maxlen)
{
	if (!verity_fec_is_enabled(v))
		return sz;

	DMEMIT(" " DM_VERITY_OPT_FEC_DEV " %s "
	       DM_VERITY_OPT_FEC_BLOCKS " %llu "
	       DM_VERITY_OPT_FEC_START " %llu "
	       DM_VERITY_OPT_FEC_ROOTS " %d",
	       v->fec->dev->name,
	       (unsigned long long)v->fec->blocks,
	       (unsigned long long)v->fec->start,
	       v->fec->roots);

	return sz;
}

void verity_fec_dtr(struct dm_verity *v)
{
	struct dm_verity_fec *f = v->fec;

	if (!verity_fec_is_enabled(v))
		goto out;

	mempool_exit(&f->fio_pool);
	mempool_exit(&f->rs_pool);
	mempool_exit(&f->prealloc_pool);
	mempool_exit(&f->output_pool);
	kmem_cache_destroy(f->cache);

	if (!IS_ERR_OR_NULL(f->data_bufio))
		dm_bufio_client_destroy(f->data_bufio);
	if (!IS_ERR_OR_NULL(f->bufio))
		dm_bufio_client_destroy(f->bufio);

	if (f->dev)
		dm_put_device(v->ti, f->dev);
out:
	kfree(f);
	v->fec = NULL;
}

static void *fec_rs_alloc(gfp_t gfp_mask, void *pool_data)
{
	struct dm_verity *v = pool_data;

	return init_rs_gfp(8, 0x11d, 0, 1, v->fec->roots, gfp_mask);
}

static void fec_rs_free(void *element, void *pool_data)
{
	struct rs_control *rs = element;

	if (rs)
		free_rs(rs);
}

bool verity_is_fec_opt_arg(const char *arg_name)
{
	return (!strcasecmp(arg_name, DM_VERITY_OPT_FEC_DEV) ||
		!strcasecmp(arg_name, DM_VERITY_OPT_FEC_BLOCKS) ||
		!strcasecmp(arg_name, DM_VERITY_OPT_FEC_START) ||
		!strcasecmp(arg_name, DM_VERITY_OPT_FEC_ROOTS));
}

int verity_fec_parse_opt_args(struct dm_arg_set *as, struct dm_verity *v,
			      unsigned int *argc, const char *arg_name)
{
	int r;
	struct dm_target *ti = v->ti;
	const char *arg_value;
	unsigned long long num_ll;
	unsigned char num_c;
	char dummy;

	if (!*argc) {
		ti->error = "FEC feature arguments require a value";
		return -EINVAL;
	}

	arg_value = dm_shift_arg(as);
	(*argc)--;

	if (!strcasecmp(arg_name, DM_VERITY_OPT_FEC_DEV)) {
		if (v->fec->dev) {
			ti->error = "FEC device already specified";
			return -EINVAL;
		}
		r = dm_get_device(ti, arg_value, BLK_OPEN_READ, &v->fec->dev);
		if (r) {
			ti->error = "FEC device lookup failed";
			return r;
		}

	} else if (!strcasecmp(arg_name, DM_VERITY_OPT_FEC_BLOCKS)) {
		if (sscanf(arg_value, "%llu%c", &num_ll, &dummy) != 1 ||
		    ((sector_t)(num_ll << (v->data_dev_block_bits - SECTOR_SHIFT))
		     >> (v->data_dev_block_bits - SECTOR_SHIFT) != num_ll)) {
			ti->error = "Invalid " DM_VERITY_OPT_FEC_BLOCKS;
			return -EINVAL;
		}
		v->fec->blocks = num_ll;

	} else if (!strcasecmp(arg_name, DM_VERITY_OPT_FEC_START)) {
		if (sscanf(arg_value, "%llu%c", &num_ll, &dummy) != 1 ||
		    ((sector_t)(num_ll << (v->data_dev_block_bits - SECTOR_SHIFT)) >>
		     (v->data_dev_block_bits - SECTOR_SHIFT) != num_ll)) {
			ti->error = "Invalid " DM_VERITY_OPT_FEC_START;
			return -EINVAL;
		}
		v->fec->start = num_ll;

	} else if (!strcasecmp(arg_name, DM_VERITY_OPT_FEC_ROOTS)) {
		if (sscanf(arg_value, "%hhu%c", &num_c, &dummy) != 1 || !num_c ||
		    num_c < DM_VERITY_FEC_MIN_ROOTS ||
		    num_c > DM_VERITY_FEC_MAX_ROOTS) {
			ti->error = "Invalid " DM_VERITY_OPT_FEC_ROOTS;
			return -EINVAL;
		}
		v->fec->roots = num_c;

	} else {
		ti->error = "Unrecognized verity FEC feature request";
		return -EINVAL;
	}

	return 0;
}

/*
 * Allocate dm_verity_fec for v->fec. Must be called before verity_fec_ctr.
 */
int verity_fec_ctr_alloc(struct dm_verity *v)
{
	struct dm_verity_fec *f;

	f = kzalloc_obj(struct dm_verity_fec);
	if (!f) {
		v->ti->error = "Cannot allocate FEC structure";
		return -ENOMEM;
	}
	v->fec = f;

	return 0;
}

/*
 * Validate arguments and preallocate memory. Must be called after arguments
 * have been parsed using verity_fec_parse_opt_args.
 */
int verity_fec_ctr(struct dm_verity *v)
{
	struct dm_verity_fec *f = v->fec;
	struct dm_target *ti = v->ti;
	u64 hash_blocks;
	int ret;

	if (!verity_fec_is_enabled(v)) {
		verity_fec_dtr(v);
		return 0;
	}

	/*
	 * FEC is computed over data blocks, possible metadata, and
	 * hash blocks. In other words, FEC covers total of fec_blocks
	 * blocks consisting of the following:
	 *
	 *  data blocks | hash blocks | metadata (optional)
	 *
	 * We allow metadata after hash blocks to support a use case
	 * where all data is stored on the same device and FEC covers
	 * the entire area.
	 *
	 * If metadata is included, we require it to be available on the
	 * hash device after the hash blocks.
	 */

	hash_blocks = v->hash_end - v->hash_start;

	/*
	 * Require matching block sizes for data and hash devices for
	 * simplicity.
	 */
	if (v->data_dev_block_bits != v->hash_dev_block_bits) {
		ti->error = "Block sizes must match to use FEC";
		return -EINVAL;
	}
	f->block_size = 1 << v->data_dev_block_bits;

	if (!f->roots) {
		ti->error = "Missing " DM_VERITY_OPT_FEC_ROOTS;
		return -EINVAL;
	}
	f->rs_k = DM_VERITY_FEC_RS_N - f->roots;

	if (!f->blocks) {
		ti->error = "Missing " DM_VERITY_OPT_FEC_BLOCKS;
		return -EINVAL;
	}

	f->region_blocks = f->blocks;
	if (sector_div(f->region_blocks, f->rs_k))
		f->region_blocks++;

	/*
	 * Due to optional metadata, f->blocks can be larger than
	 * data_blocks and hash_blocks combined.
	 */
	if (f->blocks < v->data_blocks + hash_blocks || !f->region_blocks) {
		ti->error = "Invalid " DM_VERITY_OPT_FEC_BLOCKS;
		return -EINVAL;
	}

	/*
	 * Metadata is accessed through the hash device, so we require
	 * it to be large enough.
	 */
	f->hash_blocks = f->blocks - v->data_blocks;
	if (dm_bufio_get_device_size(v->bufio) <
	    v->hash_start + f->hash_blocks) {
		ti->error = "Hash device is too small for "
			DM_VERITY_OPT_FEC_BLOCKS;
		return -E2BIG;
	}

	f->bufio = dm_bufio_client_create(f->dev->bdev, f->block_size,
					  1, 0, NULL, NULL, 0);
	if (IS_ERR(f->bufio)) {
		ti->error = "Cannot initialize FEC bufio client";
		return PTR_ERR(f->bufio);
	}

	dm_bufio_set_sector_offset(f->bufio, f->start << (v->data_dev_block_bits - SECTOR_SHIFT));

	if (dm_bufio_get_device_size(f->bufio) < f->region_blocks * f->roots) {
		ti->error = "FEC device is too small";
		return -E2BIG;
	}

	f->data_bufio = dm_bufio_client_create(v->data_dev->bdev, f->block_size,
					       1, 0, NULL, NULL, 0);
	if (IS_ERR(f->data_bufio)) {
		ti->error = "Cannot initialize FEC data bufio client";
		return PTR_ERR(f->data_bufio);
	}

	if (dm_bufio_get_device_size(f->data_bufio) < v->data_blocks) {
		ti->error = "Data device is too small";
		return -E2BIG;
	}

	/* Preallocate some dm_verity_fec_io structures */
	ret = mempool_init_kmalloc_pool(&f->fio_pool, num_online_cpus(),
					struct_size((struct dm_verity_fec_io *)0,
						    bufs, fec_max_nbufs(v)));
	if (ret) {
		ti->error = "Cannot allocate FEC IO pool";
		return ret;
	}

	/* Preallocate an rs_control structure for each worker thread */
	ret = mempool_init(&f->rs_pool, num_online_cpus(), fec_rs_alloc,
			   fec_rs_free, (void *) v);
	if (ret) {
		ti->error = "Cannot allocate RS pool";
		return ret;
	}

	f->cache = kmem_cache_create("dm_verity_fec_buffers",
				     f->rs_k << DM_VERITY_FEC_BUF_RS_BITS,
				     0, 0, NULL);
	if (!f->cache) {
		ti->error = "Cannot create FEC buffer cache";
		return -ENOMEM;
	}

	/* Preallocate one buffer for each thread */
	ret = mempool_init_slab_pool(&f->prealloc_pool, num_online_cpus(),
				     f->cache);
	if (ret) {
		ti->error = "Cannot allocate FEC buffer prealloc pool";
		return ret;
	}

	/* Preallocate an output buffer for each thread */
	ret = mempool_init_kmalloc_pool(&f->output_pool, num_online_cpus(),
					f->block_size);
	if (ret) {
		ti->error = "Cannot allocate FEC output pool";
		return ret;
	}

	return 0;
}
