///////////////////////////////////////////////////////////////////////////////
//
/// \file       lzma2_encoder.c
/// \brief      LZMA2 encoder
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "lz_encoder.h"
#include "lzma_encoder.h"
#include "fastpos.h"
#include "lzma2_encoder.h"


typedef struct {
	enum {
		SEQ_INIT,
		SEQ_LZMA_ENCODE,
		SEQ_LZMA_COPY,
		SEQ_UNCOMPRESSED_HEADER,
		SEQ_UNCOMPRESSED_COPY,
	} sequence;

	/// LZMA encoder
	void *lzma;

	/// LZMA options currently in use.
	lzma_options_lzma opt_cur;

	bool need_properties;
	bool need_state_reset;
	bool need_dictionary_reset;

	/// Uncompressed size of a chunk
	size_t uncompressed_size;

	/// Compressed size of a chunk (excluding headers); this is also used
	/// to indicate the end of buf[] in SEQ_LZMA_COPY.
	size_t compressed_size;

	/// Read position in buf[]
	size_t buf_pos;

	/// Buffer to hold the chunk header and LZMA compressed data
	uint8_t buf[LZMA2_HEADER_MAX + LZMA2_CHUNK_MAX];
} lzma_lzma2_coder;


static void
lzma2_header_lzma(lzma_lzma2_coder *coder)
{
	assert(coder->uncompressed_size > 0);
	assert(coder->uncompressed_size <= LZMA2_UNCOMPRESSED_MAX);
	assert(coder->compressed_size > 0);
	assert(coder->compressed_size <= LZMA2_CHUNK_MAX);

	size_t pos;

	if (coder->need_properties) {
		pos = 0;

		if (coder->need_dictionary_reset)
			coder->buf[pos] = 0x80 + (3 << 5);
		else
			coder->buf[pos] = 0x80 + (2 << 5);
	} else {
		pos = 1;

		if (coder->need_state_reset)
			coder->buf[pos] = 0x80 + (1 << 5);
		else
			coder->buf[pos] = 0x80;
	}

	// Set the start position for copying.
	coder->buf_pos = pos;

	// Uncompressed size
	size_t size = coder->uncompressed_size - 1;
	coder->buf[pos++] += size >> 16;
	coder->buf[pos++] = (size >> 8) & 0xFF;
	coder->buf[pos++] = size & 0xFF;

	// Compressed size
	size = coder->compressed_size - 1;
	coder->buf[pos++] = size >> 8;
	coder->buf[pos++] = size & 0xFF;

	// Properties, if needed
	if (coder->need_properties)
		lzma_lzma_lclppb_encode(&coder->opt_cur, coder->buf + pos);

	coder->need_properties = false;
	coder->need_state_reset = false;
	coder->need_dictionary_reset = false;

	// The copying code uses coder->compressed_size to indicate the end
	// of coder->buf[], so we need add the maximum size of the header here.
	coder->compressed_size += LZMA2_HEADER_MAX;

	return;
}


static void
lzma2_header_uncompressed(lzma_lzma2_coder *coder)
{
	assert(coder->uncompressed_size > 0);
	assert(coder->uncompressed_size <= LZMA2_CHUNK_MAX);

	// If this is the first chunk, we need to include dictionary
	// reset indicator.
	if (coder->need_dictionary_reset)
		coder->buf[0] = 1;
	else
		coder->buf[0] = 2;

	coder->need_dictionary_reset = false;

	// "Compressed" size
	coder->buf[1] = (coder->uncompressed_size - 1) >> 8;
	coder->buf[2] = (coder->uncompressed_size - 1) & 0xFF;

	// Set the start position for copying.
	coder->buf_pos = 0;
	return;
}


static lzma_ret
lzma2_encode(void *coder_ptr, lzma_mf *restrict mf,
		uint8_t *restrict out, size_t *restrict out_pos,
		size_t out_size)
{
	lzma_lzma2_coder *restrict coder = coder_ptr;

	while (*out_pos < out_size)
	switch (coder->sequence) {
	case SEQ_INIT:
		// If there's no input left and we are flushing or finishing,
		// don't start a new chunk.
		if (mf_unencoded(mf) == 0) {
			// Write end of payload marker if finishing.
			if (mf->action == LZMA_FINISH)
				out[(*out_pos)++] = 0;

			return mf->action == LZMA_RUN
					? LZMA_OK : LZMA_STREAM_END;
		}

		if (coder->need_state_reset)
			return_if_error(lzma_lzma_encoder_reset(
					coder->lzma, &coder->opt_cur));

		coder->uncompressed_size = 0;
		coder->compressed_size = 0;
		coder->sequence = SEQ_LZMA_ENCODE;

	// Fall through

	case SEQ_LZMA_ENCODE: {
		// Calculate how much more uncompressed data this chunk
		// could accept.
		const uint32_t left = LZMA2_UNCOMPRESSED_MAX
				- coder->uncompressed_size;
		uint32_t limit;

		if (left < mf->match_len_max) {
			// Must flush immediately since the next LZMA symbol
			// could make the uncompressed size of the chunk too
			// big.
			limit = 0;
		} else {
			// Calculate maximum read_limit that is OK from point
			// of view of LZMA2 chunk size.
			limit = mf->read_pos - mf->read_ahead
					+ left - mf->match_len_max;
		}

		// Save the start position so that we can update
		// coder->uncompressed_size.
		const uint32_t read_start = mf->read_pos - mf->read_ahead;

		// Call the LZMA encoder until the chunk is finished.
		const lzma_ret ret = lzma_lzma_encode(coder->lzma, mf,
				coder->buf + LZMA2_HEADER_MAX,
				&coder->compressed_size,
				LZMA2_CHUNK_MAX, limit);

		coder->uncompressed_size += mf->read_pos - mf->read_ahead
				- read_start;

		assert(coder->compressed_size <= LZMA2_CHUNK_MAX);
		assert(coder->uncompressed_size <= LZMA2_UNCOMPRESSED_MAX);

		if (ret != LZMA_STREAM_END)
			return LZMA_OK;

		// See if the chunk compressed. If it didn't, we encode it
		// as uncompressed chunk. This saves a few bytes of space
		// and makes decoding faster.
		if (coder->compressed_size >= coder->uncompressed_size) {
			coder->uncompressed_size += mf->read_ahead;
			assert(coder->uncompressed_size
					<= LZMA2_UNCOMPRESSED_MAX);
			mf->read_ahead = 0;
			lzma2_header_uncompressed(coder);
			coder->need_state_reset = true;
			coder->sequence = SEQ_UNCOMPRESSED_HEADER;
			break;
		}

		// The chunk did compress at least by one byte, so we store
		// the chunk as LZMA.
		lzma2_header_lzma(coder);

		coder->sequence = SEQ_LZMA_COPY;
	}

	// Fall through

	case SEQ_LZMA_COPY:
		// Copy the compressed chunk along its headers to the
		// output buffer.
		lzma_bufcpy(coder->buf, &coder->buf_pos,
				coder->compressed_size,
				out, out_pos, out_size);
		if (coder->buf_pos != coder->compressed_size)
			return LZMA_OK;

		coder->sequence = SEQ_INIT;
		break;

	case SEQ_UNCOMPRESSED_HEADER:
		// Copy the three-byte header to indicate uncompressed chunk.
		lzma_bufcpy(coder->buf, &coder->buf_pos,
				LZMA2_HEADER_UNCOMPRESSED,
				out, out_pos, out_size);
		if (coder->buf_pos != LZMA2_HEADER_UNCOMPRESSED)
			return LZMA_OK;

		coder->sequence = SEQ_UNCOMPRESSED_COPY;

	// Fall through

	case SEQ_UNCOMPRESSED_COPY:
		// Copy the uncompressed data as is from the dictionary
		// to the output buffer.
		mf_read(mf, out, out_pos, out_size, &coder->uncompressed_size);
		if (coder->uncompressed_size != 0)
			return LZMA_OK;

		coder->sequence = SEQ_INIT;
		break;
	}

	return LZMA_OK;
}


static void
lzma2_encoder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_lzma2_coder *coder = coder_ptr;
	lzma_free(coder->lzma, allocator);
	lzma_free(coder, allocator);
	return;
}


static lzma_ret
lzma2_encoder_options_update(void *coder_ptr, const lzma_filter *filter)
{
	lzma_lzma2_coder *coder = coder_ptr;

	// New options can be set only when there is no incomplete chunk.
	// This is the case at the beginning of the raw stream and right
	// after LZMA_SYNC_FLUSH.
	if (filter->options == NULL || coder->sequence != SEQ_INIT)
		return LZMA_PROG_ERROR;

	// Look if there are new options. At least for now,
	// only lc/lp/pb can be changed.
	const lzma_options_lzma *opt = filter->options;
	if (coder->opt_cur.lc != opt->lc || coder->opt_cur.lp != opt->lp
			|| coder->opt_cur.pb != opt->pb) {
		// Validate the options.
		if (opt->lc > LZMA_LCLP_MAX || opt->lp > LZMA_LCLP_MAX
				|| opt->lc + opt->lp > LZMA_LCLP_MAX
				|| opt->pb > LZMA_PB_MAX)
			return LZMA_OPTIONS_ERROR;

		// The new options will be used when the encoder starts
		// a new LZMA2 chunk.
		coder->opt_cur.lc = opt->lc;
		coder->opt_cur.lp = opt->lp;
		coder->opt_cur.pb = opt->pb;
		coder->need_properties = true;
		coder->need_state_reset = true;
	}

	return LZMA_OK;
}


static lzma_ret
lzma2_encoder_init(lzma_lz_encoder *lz, const lzma_allocator *allocator,
		const void *options, lzma_lz_options *lz_options)
{
	if (options == NULL)
		return LZMA_PROG_ERROR;

	lzma_lzma2_coder *coder = lz->coder;
	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_lzma2_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		lz->coder = coder;
		lz->code = &lzma2_encode;
		lz->end = &lzma2_encoder_end;
		lz->options_update = &lzma2_encoder_options_update;

		coder->lzma = NULL;
	}

	coder->opt_cur = *(const lzma_options_lzma *)(options);

	coder->sequence = SEQ_INIT;
	coder->need_properties = true;
	coder->need_state_reset = false;
	coder->need_dictionary_reset
			= coder->opt_cur.preset_dict == NULL
			|| coder->opt_cur.preset_dict_size == 0;

	// Initialize LZMA encoder
	return_if_error(lzma_lzma_encoder_create(&coder->lzma, allocator,
			&coder->opt_cur, lz_options));

	// Make sure that we will always have enough history available in
	// case we need to use uncompressed chunks. They are used when the
	// compressed size of a chunk is not smaller than the uncompressed
	// size, so we need to have at least LZMA2_COMPRESSED_MAX bytes
	// history available.
	if (lz_options->before_size + lz_options->dict_size < LZMA2_CHUNK_MAX)
		lz_options->before_size
				= LZMA2_CHUNK_MAX - lz_options->dict_size;

	return LZMA_OK;
}


extern lzma_ret
lzma_lzma2_encoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return lzma_lz_encoder_init(
			next, allocator, filters, &lzma2_encoder_init);
}


extern uint64_t
lzma_lzma2_encoder_memusage(const void *options)
{
	const uint64_t lzma_mem = lzma_lzma_encoder_memusage(options);
	if (lzma_mem == UINT64_MAX)
		return UINT64_MAX;

	return sizeof(lzma_lzma2_coder) + lzma_mem;
}


extern lzma_ret
lzma_lzma2_props_encode(const void *options, uint8_t *out)
{
	const lzma_options_lzma *const opt = options;
	uint32_t d = my_max(opt->dict_size, LZMA_DICT_SIZE_MIN);

	// Round up to the next 2^n - 1 or 2^n + 2^(n - 1) - 1 depending
	// on which one is the next:
	--d;
	d |= d >> 2;
	d |= d >> 3;
	d |= d >> 4;
	d |= d >> 8;
	d |= d >> 16;

	// Get the highest two bits using the proper encoding:
	if (d == UINT32_MAX)
		out[0] = 40;
	else
		out[0] = get_dist_slot(d + 1) - 24;

	return LZMA_OK;
}


extern uint64_t
lzma_lzma2_block_size(const void *options)
{
	const lzma_options_lzma *const opt = options;

	// Use at least 1 MiB to keep compression ratio better.
	return my_max((uint64_t)(opt->dict_size) * 3, UINT64_C(1) << 20);
}
