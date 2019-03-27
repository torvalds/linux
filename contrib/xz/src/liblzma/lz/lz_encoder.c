///////////////////////////////////////////////////////////////////////////////
//
/// \file       lz_encoder.c
/// \brief      LZ in window
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "lz_encoder.h"
#include "lz_encoder_hash.h"

// See lz_encoder_hash.h. This is a bit hackish but avoids making
// endianness a conditional in makefiles.
#if defined(WORDS_BIGENDIAN) && !defined(HAVE_SMALL)
#	include "lz_encoder_hash_table.h"
#endif

#include "memcmplen.h"


typedef struct {
	/// LZ-based encoder e.g. LZMA
	lzma_lz_encoder lz;

	/// History buffer and match finder
	lzma_mf mf;

	/// Next coder in the chain
	lzma_next_coder next;
} lzma_coder;


/// \brief      Moves the data in the input window to free space for new data
///
/// mf->buffer is a sliding input window, which keeps mf->keep_size_before
/// bytes of input history available all the time. Now and then we need to
/// "slide" the buffer to make space for the new data to the end of the
/// buffer. At the same time, data older than keep_size_before is dropped.
///
static void
move_window(lzma_mf *mf)
{
	// Align the move to a multiple of 16 bytes. Some LZ-based encoders
	// like LZMA use the lowest bits of mf->read_pos to know the
	// alignment of the uncompressed data. We also get better speed
	// for memmove() with aligned buffers.
	assert(mf->read_pos > mf->keep_size_before);
	const uint32_t move_offset
		= (mf->read_pos - mf->keep_size_before) & ~UINT32_C(15);

	assert(mf->write_pos > move_offset);
	const size_t move_size = mf->write_pos - move_offset;

	assert(move_offset + move_size <= mf->size);

	memmove(mf->buffer, mf->buffer + move_offset, move_size);

	mf->offset += move_offset;
	mf->read_pos -= move_offset;
	mf->read_limit -= move_offset;
	mf->write_pos -= move_offset;

	return;
}


/// \brief      Tries to fill the input window (mf->buffer)
///
/// If we are the last encoder in the chain, our input data is in in[].
/// Otherwise we call the next filter in the chain to process in[] and
/// write its output to mf->buffer.
///
/// This function must not be called once it has returned LZMA_STREAM_END.
///
static lzma_ret
fill_window(lzma_coder *coder, const lzma_allocator *allocator,
		const uint8_t *in, size_t *in_pos, size_t in_size,
		lzma_action action)
{
	assert(coder->mf.read_pos <= coder->mf.write_pos);

	// Move the sliding window if needed.
	if (coder->mf.read_pos >= coder->mf.size - coder->mf.keep_size_after)
		move_window(&coder->mf);

	// Maybe this is ugly, but lzma_mf uses uint32_t for most things
	// (which I find cleanest), but we need size_t here when filling
	// the history window.
	size_t write_pos = coder->mf.write_pos;
	lzma_ret ret;
	if (coder->next.code == NULL) {
		// Not using a filter, simply memcpy() as much as possible.
		lzma_bufcpy(in, in_pos, in_size, coder->mf.buffer,
				&write_pos, coder->mf.size);

		ret = action != LZMA_RUN && *in_pos == in_size
				? LZMA_STREAM_END : LZMA_OK;

	} else {
		ret = coder->next.code(coder->next.coder, allocator,
				in, in_pos, in_size,
				coder->mf.buffer, &write_pos,
				coder->mf.size, action);
	}

	coder->mf.write_pos = write_pos;

	// Silence Valgrind. lzma_memcmplen() can read extra bytes
	// and Valgrind will give warnings if those bytes are uninitialized
	// because Valgrind cannot see that the values of the uninitialized
	// bytes are eventually ignored.
	memzero(coder->mf.buffer + write_pos, LZMA_MEMCMPLEN_EXTRA);

	// If end of stream has been reached or flushing completed, we allow
	// the encoder to process all the input (that is, read_pos is allowed
	// to reach write_pos). Otherwise we keep keep_size_after bytes
	// available as prebuffer.
	if (ret == LZMA_STREAM_END) {
		assert(*in_pos == in_size);
		ret = LZMA_OK;
		coder->mf.action = action;
		coder->mf.read_limit = coder->mf.write_pos;

	} else if (coder->mf.write_pos > coder->mf.keep_size_after) {
		// This needs to be done conditionally, because if we got
		// only little new input, there may be too little input
		// to do any encoding yet.
		coder->mf.read_limit = coder->mf.write_pos
				- coder->mf.keep_size_after;
	}

	// Restart the match finder after finished LZMA_SYNC_FLUSH.
	if (coder->mf.pending > 0
			&& coder->mf.read_pos < coder->mf.read_limit) {
		// Match finder may update coder->pending and expects it to
		// start from zero, so use a temporary variable.
		const uint32_t pending = coder->mf.pending;
		coder->mf.pending = 0;

		// Rewind read_pos so that the match finder can hash
		// the pending bytes.
		assert(coder->mf.read_pos >= pending);
		coder->mf.read_pos -= pending;

		// Call the skip function directly instead of using
		// mf_skip(), since we don't want to touch mf->read_ahead.
		coder->mf.skip(&coder->mf, pending);
	}

	return ret;
}


static lzma_ret
lz_encode(void *coder_ptr, const lzma_allocator *allocator,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size,
		uint8_t *restrict out, size_t *restrict out_pos,
		size_t out_size, lzma_action action)
{
	lzma_coder *coder = coder_ptr;

	while (*out_pos < out_size
			&& (*in_pos < in_size || action != LZMA_RUN)) {
		// Read more data to coder->mf.buffer if needed.
		if (coder->mf.action == LZMA_RUN && coder->mf.read_pos
				>= coder->mf.read_limit)
			return_if_error(fill_window(coder, allocator,
					in, in_pos, in_size, action));

		// Encode
		const lzma_ret ret = coder->lz.code(coder->lz.coder,
				&coder->mf, out, out_pos, out_size);
		if (ret != LZMA_OK) {
			// Setting this to LZMA_RUN for cases when we are
			// flushing. It doesn't matter when finishing or if
			// an error occurred.
			coder->mf.action = LZMA_RUN;
			return ret;
		}
	}

	return LZMA_OK;
}


static bool
lz_encoder_prepare(lzma_mf *mf, const lzma_allocator *allocator,
		const lzma_lz_options *lz_options)
{
	// For now, the dictionary size is limited to 1.5 GiB. This may grow
	// in the future if needed, but it needs a little more work than just
	// changing this check.
	if (lz_options->dict_size < LZMA_DICT_SIZE_MIN
			|| lz_options->dict_size
				> (UINT32_C(1) << 30) + (UINT32_C(1) << 29)
			|| lz_options->nice_len > lz_options->match_len_max)
		return true;

	mf->keep_size_before = lz_options->before_size + lz_options->dict_size;

	mf->keep_size_after = lz_options->after_size
			+ lz_options->match_len_max;

	// To avoid constant memmove()s, allocate some extra space. Since
	// memmove()s become more expensive when the size of the buffer
	// increases, we reserve more space when a large dictionary is
	// used to make the memmove() calls rarer.
	//
	// This works with dictionaries up to about 3 GiB. If bigger
	// dictionary is wanted, some extra work is needed:
	//   - Several variables in lzma_mf have to be changed from uint32_t
	//     to size_t.
	//   - Memory usage calculation needs something too, e.g. use uint64_t
	//     for mf->size.
	uint32_t reserve = lz_options->dict_size / 2;
	if (reserve > (UINT32_C(1) << 30))
		reserve /= 2;

	reserve += (lz_options->before_size + lz_options->match_len_max
			+ lz_options->after_size) / 2 + (UINT32_C(1) << 19);

	const uint32_t old_size = mf->size;
	mf->size = mf->keep_size_before + reserve + mf->keep_size_after;

	// Deallocate the old history buffer if it exists but has different
	// size than what is needed now.
	if (mf->buffer != NULL && old_size != mf->size) {
		lzma_free(mf->buffer, allocator);
		mf->buffer = NULL;
	}

	// Match finder options
	mf->match_len_max = lz_options->match_len_max;
	mf->nice_len = lz_options->nice_len;

	// cyclic_size has to stay smaller than 2 Gi. Note that this doesn't
	// mean limiting dictionary size to less than 2 GiB. With a match
	// finder that uses multibyte resolution (hashes start at e.g. every
	// fourth byte), cyclic_size would stay below 2 Gi even when
	// dictionary size is greater than 2 GiB.
	//
	// It would be possible to allow cyclic_size >= 2 Gi, but then we
	// would need to be careful to use 64-bit types in various places
	// (size_t could do since we would need bigger than 32-bit address
	// space anyway). It would also require either zeroing a multigigabyte
	// buffer at initialization (waste of time and RAM) or allow
	// normalization in lz_encoder_mf.c to access uninitialized
	// memory to keep the code simpler. The current way is simple and
	// still allows pretty big dictionaries, so I don't expect these
	// limits to change.
	mf->cyclic_size = lz_options->dict_size + 1;

	// Validate the match finder ID and setup the function pointers.
	switch (lz_options->match_finder) {
#ifdef HAVE_MF_HC3
	case LZMA_MF_HC3:
		mf->find = &lzma_mf_hc3_find;
		mf->skip = &lzma_mf_hc3_skip;
		break;
#endif
#ifdef HAVE_MF_HC4
	case LZMA_MF_HC4:
		mf->find = &lzma_mf_hc4_find;
		mf->skip = &lzma_mf_hc4_skip;
		break;
#endif
#ifdef HAVE_MF_BT2
	case LZMA_MF_BT2:
		mf->find = &lzma_mf_bt2_find;
		mf->skip = &lzma_mf_bt2_skip;
		break;
#endif
#ifdef HAVE_MF_BT3
	case LZMA_MF_BT3:
		mf->find = &lzma_mf_bt3_find;
		mf->skip = &lzma_mf_bt3_skip;
		break;
#endif
#ifdef HAVE_MF_BT4
	case LZMA_MF_BT4:
		mf->find = &lzma_mf_bt4_find;
		mf->skip = &lzma_mf_bt4_skip;
		break;
#endif

	default:
		return true;
	}

	// Calculate the sizes of mf->hash and mf->son and check that
	// nice_len is big enough for the selected match finder.
	const uint32_t hash_bytes = lz_options->match_finder & 0x0F;
	if (hash_bytes > mf->nice_len)
		return true;

	const bool is_bt = (lz_options->match_finder & 0x10) != 0;
	uint32_t hs;

	if (hash_bytes == 2) {
		hs = 0xFFFF;
	} else {
		// Round dictionary size up to the next 2^n - 1 so it can
		// be used as a hash mask.
		hs = lz_options->dict_size - 1;
		hs |= hs >> 1;
		hs |= hs >> 2;
		hs |= hs >> 4;
		hs |= hs >> 8;
		hs >>= 1;
		hs |= 0xFFFF;

		if (hs > (UINT32_C(1) << 24)) {
			if (hash_bytes == 3)
				hs = (UINT32_C(1) << 24) - 1;
			else
				hs >>= 1;
		}
	}

	mf->hash_mask = hs;

	++hs;
	if (hash_bytes > 2)
		hs += HASH_2_SIZE;
	if (hash_bytes > 3)
		hs += HASH_3_SIZE;
/*
	No match finder uses this at the moment.
	if (mf->hash_bytes > 4)
		hs += HASH_4_SIZE;
*/

	const uint32_t old_hash_count = mf->hash_count;
	const uint32_t old_sons_count = mf->sons_count;
	mf->hash_count = hs;
	mf->sons_count = mf->cyclic_size;
	if (is_bt)
		mf->sons_count *= 2;

	// Deallocate the old hash array if it exists and has different size
	// than what is needed now.
	if (old_hash_count != mf->hash_count
			|| old_sons_count != mf->sons_count) {
		lzma_free(mf->hash, allocator);
		mf->hash = NULL;

		lzma_free(mf->son, allocator);
		mf->son = NULL;
	}

	// Maximum number of match finder cycles
	mf->depth = lz_options->depth;
	if (mf->depth == 0) {
		if (is_bt)
			mf->depth = 16 + mf->nice_len / 2;
		else
			mf->depth = 4 + mf->nice_len / 4;
	}

	return false;
}


static bool
lz_encoder_init(lzma_mf *mf, const lzma_allocator *allocator,
		const lzma_lz_options *lz_options)
{
	// Allocate the history buffer.
	if (mf->buffer == NULL) {
		// lzma_memcmplen() is used for the dictionary buffer
		// so we need to allocate a few extra bytes to prevent
		// it from reading past the end of the buffer.
		mf->buffer = lzma_alloc(mf->size + LZMA_MEMCMPLEN_EXTRA,
				allocator);
		if (mf->buffer == NULL)
			return true;

		// Keep Valgrind happy with lzma_memcmplen() and initialize
		// the extra bytes whose value may get read but which will
		// effectively get ignored.
		memzero(mf->buffer + mf->size, LZMA_MEMCMPLEN_EXTRA);
	}

	// Use cyclic_size as initial mf->offset. This allows
	// avoiding a few branches in the match finders. The downside is
	// that match finder needs to be normalized more often, which may
	// hurt performance with huge dictionaries.
	mf->offset = mf->cyclic_size;
	mf->read_pos = 0;
	mf->read_ahead = 0;
	mf->read_limit = 0;
	mf->write_pos = 0;
	mf->pending = 0;

#if UINT32_MAX >= SIZE_MAX / 4
	// Check for integer overflow. (Huge dictionaries are not
	// possible on 32-bit CPU.)
	if (mf->hash_count > SIZE_MAX / sizeof(uint32_t)
			|| mf->sons_count > SIZE_MAX / sizeof(uint32_t))
		return true;
#endif

	// Allocate and initialize the hash table. Since EMPTY_HASH_VALUE
	// is zero, we can use lzma_alloc_zero() or memzero() for mf->hash.
	//
	// We don't need to initialize mf->son, but not doing that may
	// make Valgrind complain in normalization (see normalize() in
	// lz_encoder_mf.c). Skipping the initialization is *very* good
	// when big dictionary is used but only small amount of data gets
	// actually compressed: most of the mf->son won't get actually
	// allocated by the kernel, so we avoid wasting RAM and improve
	// initialization speed a lot.
	if (mf->hash == NULL) {
		mf->hash = lzma_alloc_zero(mf->hash_count * sizeof(uint32_t),
				allocator);
		mf->son = lzma_alloc(mf->sons_count * sizeof(uint32_t),
				allocator);

		if (mf->hash == NULL || mf->son == NULL) {
			lzma_free(mf->hash, allocator);
			mf->hash = NULL;

			lzma_free(mf->son, allocator);
			mf->son = NULL;

			return true;
		}
	} else {
/*
		for (uint32_t i = 0; i < mf->hash_count; ++i)
			mf->hash[i] = EMPTY_HASH_VALUE;
*/
		memzero(mf->hash, mf->hash_count * sizeof(uint32_t));
	}

	mf->cyclic_pos = 0;

	// Handle preset dictionary.
	if (lz_options->preset_dict != NULL
			&& lz_options->preset_dict_size > 0) {
		// If the preset dictionary is bigger than the actual
		// dictionary, use only the tail.
		mf->write_pos = my_min(lz_options->preset_dict_size, mf->size);
		memcpy(mf->buffer, lz_options->preset_dict
				+ lz_options->preset_dict_size - mf->write_pos,
				mf->write_pos);
		mf->action = LZMA_SYNC_FLUSH;
		mf->skip(mf, mf->write_pos);
	}

	mf->action = LZMA_RUN;

	return false;
}


extern uint64_t
lzma_lz_encoder_memusage(const lzma_lz_options *lz_options)
{
	// Old buffers must not exist when calling lz_encoder_prepare().
	lzma_mf mf = {
		.buffer = NULL,
		.hash = NULL,
		.son = NULL,
		.hash_count = 0,
		.sons_count = 0,
	};

	// Setup the size information into mf.
	if (lz_encoder_prepare(&mf, NULL, lz_options))
		return UINT64_MAX;

	// Calculate the memory usage.
	return ((uint64_t)(mf.hash_count) + mf.sons_count) * sizeof(uint32_t)
			+ mf.size + sizeof(lzma_coder);
}


static void
lz_encoder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_coder *coder = coder_ptr;

	lzma_next_end(&coder->next, allocator);

	lzma_free(coder->mf.son, allocator);
	lzma_free(coder->mf.hash, allocator);
	lzma_free(coder->mf.buffer, allocator);

	if (coder->lz.end != NULL)
		coder->lz.end(coder->lz.coder, allocator);
	else
		lzma_free(coder->lz.coder, allocator);

	lzma_free(coder, allocator);
	return;
}


static lzma_ret
lz_encoder_update(void *coder_ptr, const lzma_allocator *allocator,
		const lzma_filter *filters_null lzma_attribute((__unused__)),
		const lzma_filter *reversed_filters)
{
	lzma_coder *coder = coder_ptr;

	if (coder->lz.options_update == NULL)
		return LZMA_PROG_ERROR;

	return_if_error(coder->lz.options_update(
			coder->lz.coder, reversed_filters));

	return lzma_next_filter_update(
			&coder->next, allocator, reversed_filters + 1);
}


extern lzma_ret
lzma_lz_encoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters,
		lzma_ret (*lz_init)(lzma_lz_encoder *lz,
			const lzma_allocator *allocator, const void *options,
			lzma_lz_options *lz_options))
{
#ifdef HAVE_SMALL
	// We need that the CRC32 table has been initialized.
	lzma_crc32_init();
#endif

	// Allocate and initialize the base data structure.
	lzma_coder *coder = next->coder;
	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;
		next->code = &lz_encode;
		next->end = &lz_encoder_end;
		next->update = &lz_encoder_update;

		coder->lz.coder = NULL;
		coder->lz.code = NULL;
		coder->lz.end = NULL;

		// mf.size is initialized to silence Valgrind
		// when used on optimized binaries (GCC may reorder
		// code in a way that Valgrind gets unhappy).
		coder->mf.buffer = NULL;
		coder->mf.size = 0;
		coder->mf.hash = NULL;
		coder->mf.son = NULL;
		coder->mf.hash_count = 0;
		coder->mf.sons_count = 0;

		coder->next = LZMA_NEXT_CODER_INIT;
	}

	// Initialize the LZ-based encoder.
	lzma_lz_options lz_options;
	return_if_error(lz_init(&coder->lz, allocator,
			filters[0].options, &lz_options));

	// Setup the size information into coder->mf and deallocate
	// old buffers if they have wrong size.
	if (lz_encoder_prepare(&coder->mf, allocator, &lz_options))
		return LZMA_OPTIONS_ERROR;

	// Allocate new buffers if needed, and do the rest of
	// the initialization.
	if (lz_encoder_init(&coder->mf, allocator, &lz_options))
		return LZMA_MEM_ERROR;

	// Initialize the next filter in the chain, if any.
	return lzma_next_filter_init(&coder->next, allocator, filters + 1);
}


extern LZMA_API(lzma_bool)
lzma_mf_is_supported(lzma_match_finder mf)
{
	bool ret = false;

#ifdef HAVE_MF_HC3
	if (mf == LZMA_MF_HC3)
		ret = true;
#endif

#ifdef HAVE_MF_HC4
	if (mf == LZMA_MF_HC4)
		ret = true;
#endif

#ifdef HAVE_MF_BT2
	if (mf == LZMA_MF_BT2)
		ret = true;
#endif

#ifdef HAVE_MF_BT3
	if (mf == LZMA_MF_BT3)
		ret = true;
#endif

#ifdef HAVE_MF_BT4
	if (mf == LZMA_MF_BT4)
		ret = true;
#endif

	return ret;
}
