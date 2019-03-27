///////////////////////////////////////////////////////////////////////////////
//
/// \file       block_encoder.c
/// \brief      Encodes .xz Blocks
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "block_encoder.h"
#include "filter_encoder.h"
#include "check.h"


typedef struct {
	/// The filters in the chain; initialized with lzma_raw_decoder_init().
	lzma_next_coder next;

	/// Encoding options; we also write Unpadded Size, Compressed Size,
	/// and Uncompressed Size back to this structure when the encoding
	/// has been finished.
	lzma_block *block;

	enum {
		SEQ_CODE,
		SEQ_PADDING,
		SEQ_CHECK,
	} sequence;

	/// Compressed Size calculated while encoding
	lzma_vli compressed_size;

	/// Uncompressed Size calculated while encoding
	lzma_vli uncompressed_size;

	/// Position in the Check field
	size_t pos;

	/// Check of the uncompressed data
	lzma_check_state check;
} lzma_block_coder;


static lzma_ret
block_encode(void *coder_ptr, const lzma_allocator *allocator,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size, lzma_action action)
{
	lzma_block_coder *coder = coder_ptr;

	// Check that our amount of input stays in proper limits.
	if (LZMA_VLI_MAX - coder->uncompressed_size < in_size - *in_pos)
		return LZMA_DATA_ERROR;

	switch (coder->sequence) {
	case SEQ_CODE: {
		const size_t in_start = *in_pos;
		const size_t out_start = *out_pos;

		const lzma_ret ret = coder->next.code(coder->next.coder,
				allocator, in, in_pos, in_size,
				out, out_pos, out_size, action);

		const size_t in_used = *in_pos - in_start;
		const size_t out_used = *out_pos - out_start;

		if (COMPRESSED_SIZE_MAX - coder->compressed_size < out_used)
			return LZMA_DATA_ERROR;

		coder->compressed_size += out_used;

		// No need to check for overflow because we have already
		// checked it at the beginning of this function.
		coder->uncompressed_size += in_used;

		lzma_check_update(&coder->check, coder->block->check,
				in + in_start, in_used);

		if (ret != LZMA_STREAM_END || action == LZMA_SYNC_FLUSH)
			return ret;

		assert(*in_pos == in_size);
		assert(action == LZMA_FINISH);

		// Copy the values into coder->block. The caller
		// may use this information to construct Index.
		coder->block->compressed_size = coder->compressed_size;
		coder->block->uncompressed_size = coder->uncompressed_size;

		coder->sequence = SEQ_PADDING;
	}

	// Fall through

	case SEQ_PADDING:
		// Pad Compressed Data to a multiple of four bytes. We can
		// use coder->compressed_size for this since we don't need
		// it for anything else anymore.
		while (coder->compressed_size & 3) {
			if (*out_pos >= out_size)
				return LZMA_OK;

			out[*out_pos] = 0x00;
			++*out_pos;
			++coder->compressed_size;
		}

		if (coder->block->check == LZMA_CHECK_NONE)
			return LZMA_STREAM_END;

		lzma_check_finish(&coder->check, coder->block->check);

		coder->sequence = SEQ_CHECK;

	// Fall through

	case SEQ_CHECK: {
		const size_t check_size = lzma_check_size(coder->block->check);
		lzma_bufcpy(coder->check.buffer.u8, &coder->pos, check_size,
				out, out_pos, out_size);
		if (coder->pos < check_size)
			return LZMA_OK;

		memcpy(coder->block->raw_check, coder->check.buffer.u8,
				check_size);
		return LZMA_STREAM_END;
	}
	}

	return LZMA_PROG_ERROR;
}


static void
block_encoder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_block_coder *coder = coder_ptr;
	lzma_next_end(&coder->next, allocator);
	lzma_free(coder, allocator);
	return;
}


static lzma_ret
block_encoder_update(void *coder_ptr, const lzma_allocator *allocator,
		const lzma_filter *filters lzma_attribute((__unused__)),
		const lzma_filter *reversed_filters)
{
	lzma_block_coder *coder = coder_ptr;

	if (coder->sequence != SEQ_CODE)
		return LZMA_PROG_ERROR;

	return lzma_next_filter_update(
			&coder->next, allocator, reversed_filters);
}


extern lzma_ret
lzma_block_encoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		lzma_block *block)
{
	lzma_next_coder_init(&lzma_block_encoder_init, next, allocator);

	if (block == NULL)
		return LZMA_PROG_ERROR;

	// The contents of the structure may depend on the version so
	// check the version first.
	if (block->version > 1)
		return LZMA_OPTIONS_ERROR;

	// If the Check ID is not supported, we cannot calculate the check and
	// thus not create a proper Block.
	if ((unsigned int)(block->check) > LZMA_CHECK_ID_MAX)
		return LZMA_PROG_ERROR;

	if (!lzma_check_is_supported(block->check))
		return LZMA_UNSUPPORTED_CHECK;

	// Allocate and initialize *next->coder if needed.
	lzma_block_coder *coder = next->coder;
	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_block_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;
		next->code = &block_encode;
		next->end = &block_encoder_end;
		next->update = &block_encoder_update;
		coder->next = LZMA_NEXT_CODER_INIT;
	}

	// Basic initializations
	coder->sequence = SEQ_CODE;
	coder->block = block;
	coder->compressed_size = 0;
	coder->uncompressed_size = 0;
	coder->pos = 0;

	// Initialize the check
	lzma_check_init(&coder->check, block->check);

	// Initialize the requested filters.
	return lzma_raw_encoder_init(&coder->next, allocator, block->filters);
}


extern LZMA_API(lzma_ret)
lzma_block_encoder(lzma_stream *strm, lzma_block *block)
{
	lzma_next_strm_init(lzma_block_encoder_init, strm, block);

	strm->internal->supported_actions[LZMA_RUN] = true;
	strm->internal->supported_actions[LZMA_FINISH] = true;

	return LZMA_OK;
}
