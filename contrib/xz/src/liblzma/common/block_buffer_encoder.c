///////////////////////////////////////////////////////////////////////////////
//
/// \file       block_buffer_encoder.c
/// \brief      Single-call .xz Block encoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "block_buffer_encoder.h"
#include "block_encoder.h"
#include "filter_encoder.h"
#include "lzma2_encoder.h"
#include "check.h"


/// Estimate the maximum size of the Block Header and Check fields for
/// a Block that uses LZMA2 uncompressed chunks. We could use
/// lzma_block_header_size() but this is simpler.
///
/// Block Header Size + Block Flags + Compressed Size
/// + Uncompressed Size + Filter Flags for LZMA2 + CRC32 + Check
/// and round up to the next multiple of four to take Header Padding
/// into account.
#define HEADERS_BOUND ((1 + 1 + 2 * LZMA_VLI_BYTES_MAX + 3 + 4 \
		+ LZMA_CHECK_SIZE_MAX + 3) & ~3)


static uint64_t
lzma2_bound(uint64_t uncompressed_size)
{
	// Prevent integer overflow in overhead calculation.
	if (uncompressed_size > COMPRESSED_SIZE_MAX)
		return 0;

	// Calculate the exact overhead of the LZMA2 headers: Round
	// uncompressed_size up to the next multiple of LZMA2_CHUNK_MAX,
	// multiply by the size of per-chunk header, and add one byte for
	// the end marker.
	const uint64_t overhead = ((uncompressed_size + LZMA2_CHUNK_MAX - 1)
				/ LZMA2_CHUNK_MAX)
			* LZMA2_HEADER_UNCOMPRESSED + 1;

	// Catch the possible integer overflow.
	if (COMPRESSED_SIZE_MAX - overhead < uncompressed_size)
		return 0;

	return uncompressed_size + overhead;
}


extern uint64_t
lzma_block_buffer_bound64(uint64_t uncompressed_size)
{
	// If the data doesn't compress, we always use uncompressed
	// LZMA2 chunks.
	uint64_t lzma2_size = lzma2_bound(uncompressed_size);
	if (lzma2_size == 0)
		return 0;

	// Take Block Padding into account.
	lzma2_size = (lzma2_size + 3) & ~UINT64_C(3);

	// No risk of integer overflow because lzma2_bound() already takes
	// into account the size of the headers in the Block.
	return HEADERS_BOUND + lzma2_size;
}


extern LZMA_API(size_t)
lzma_block_buffer_bound(size_t uncompressed_size)
{
	uint64_t ret = lzma_block_buffer_bound64(uncompressed_size);

#if SIZE_MAX < UINT64_MAX
	// Catch the possible integer overflow on 32-bit systems.
	if (ret > SIZE_MAX)
		return 0;
#endif

	return ret;
}


static lzma_ret
block_encode_uncompressed(lzma_block *block, const uint8_t *in, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size)
{
	// Use LZMA2 uncompressed chunks. We wouldn't need a dictionary at
	// all, but LZMA2 always requires a dictionary, so use the minimum
	// value to minimize memory usage of the decoder.
	lzma_options_lzma lzma2 = {
		.dict_size = LZMA_DICT_SIZE_MIN,
	};

	lzma_filter filters[2];
	filters[0].id = LZMA_FILTER_LZMA2;
	filters[0].options = &lzma2;
	filters[1].id = LZMA_VLI_UNKNOWN;

	// Set the above filter options to *block temporarily so that we can
	// encode the Block Header.
	lzma_filter *filters_orig = block->filters;
	block->filters = filters;

	if (lzma_block_header_size(block) != LZMA_OK) {
		block->filters = filters_orig;
		return LZMA_PROG_ERROR;
	}

	// Check that there's enough output space. The caller has already
	// set block->compressed_size to what lzma2_bound() has returned,
	// so we can reuse that value. We know that compressed_size is a
	// known valid VLI and header_size is a small value so their sum
	// will never overflow.
	assert(block->compressed_size == lzma2_bound(in_size));
	if (out_size - *out_pos
			< block->header_size + block->compressed_size) {
		block->filters = filters_orig;
		return LZMA_BUF_ERROR;
	}

	if (lzma_block_header_encode(block, out + *out_pos) != LZMA_OK) {
		block->filters = filters_orig;
		return LZMA_PROG_ERROR;
	}

	block->filters = filters_orig;
	*out_pos += block->header_size;

	// Encode the data using LZMA2 uncompressed chunks.
	size_t in_pos = 0;
	uint8_t control = 0x01; // Dictionary reset

	while (in_pos < in_size) {
		// Control byte: Indicate uncompressed chunk, of which
		// the first resets the dictionary.
		out[(*out_pos)++] = control;
		control = 0x02; // No dictionary reset

		// Size of the uncompressed chunk
		const size_t copy_size
				= my_min(in_size - in_pos, LZMA2_CHUNK_MAX);
		out[(*out_pos)++] = (copy_size - 1) >> 8;
		out[(*out_pos)++] = (copy_size - 1) & 0xFF;

		// The actual data
		assert(*out_pos + copy_size <= out_size);
		memcpy(out + *out_pos, in + in_pos, copy_size);

		in_pos += copy_size;
		*out_pos += copy_size;
	}

	// End marker
	out[(*out_pos)++] = 0x00;
	assert(*out_pos <= out_size);

	return LZMA_OK;
}


static lzma_ret
block_encode_normal(lzma_block *block, const lzma_allocator *allocator,
		const uint8_t *in, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size)
{
	// Find out the size of the Block Header.
	return_if_error(lzma_block_header_size(block));

	// Reserve space for the Block Header and skip it for now.
	if (out_size - *out_pos <= block->header_size)
		return LZMA_BUF_ERROR;

	const size_t out_start = *out_pos;
	*out_pos += block->header_size;

	// Limit out_size so that we stop encoding if the output would grow
	// bigger than what uncompressed Block would be.
	if (out_size - *out_pos > block->compressed_size)
		out_size = *out_pos + block->compressed_size;

	// TODO: In many common cases this could be optimized to use
	// significantly less memory.
	lzma_next_coder raw_encoder = LZMA_NEXT_CODER_INIT;
	lzma_ret ret = lzma_raw_encoder_init(
			&raw_encoder, allocator, block->filters);

	if (ret == LZMA_OK) {
		size_t in_pos = 0;
		ret = raw_encoder.code(raw_encoder.coder, allocator,
				in, &in_pos, in_size, out, out_pos, out_size,
				LZMA_FINISH);
	}

	// NOTE: This needs to be run even if lzma_raw_encoder_init() failed.
	lzma_next_end(&raw_encoder, allocator);

	if (ret == LZMA_STREAM_END) {
		// Compression was successful. Write the Block Header.
		block->compressed_size
				= *out_pos - (out_start + block->header_size);
		ret = lzma_block_header_encode(block, out + out_start);
		if (ret != LZMA_OK)
			ret = LZMA_PROG_ERROR;

	} else if (ret == LZMA_OK) {
		// Output buffer became full.
		ret = LZMA_BUF_ERROR;
	}

	// Reset *out_pos if something went wrong.
	if (ret != LZMA_OK)
		*out_pos = out_start;

	return ret;
}


static lzma_ret
block_buffer_encode(lzma_block *block, const lzma_allocator *allocator,
		const uint8_t *in, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size,
		bool try_to_compress)
{
	// Validate the arguments.
	if (block == NULL || (in == NULL && in_size != 0) || out == NULL
			|| out_pos == NULL || *out_pos > out_size)
		return LZMA_PROG_ERROR;

	// The contents of the structure may depend on the version so
	// check the version before validating the contents of *block.
	if (block->version > 1)
		return LZMA_OPTIONS_ERROR;

	if ((unsigned int)(block->check) > LZMA_CHECK_ID_MAX
			|| (try_to_compress && block->filters == NULL))
		return LZMA_PROG_ERROR;

	if (!lzma_check_is_supported(block->check))
		return LZMA_UNSUPPORTED_CHECK;

	// Size of a Block has to be a multiple of four, so limit the size
	// here already. This way we don't need to check it again when adding
	// Block Padding.
	out_size -= (out_size - *out_pos) & 3;

	// Get the size of the Check field.
	const size_t check_size = lzma_check_size(block->check);
	assert(check_size != UINT32_MAX);

	// Reserve space for the Check field.
	if (out_size - *out_pos <= check_size)
		return LZMA_BUF_ERROR;

	out_size -= check_size;

	// Initialize block->uncompressed_size and calculate the worst-case
	// value for block->compressed_size.
	block->uncompressed_size = in_size;
	block->compressed_size = lzma2_bound(in_size);
	if (block->compressed_size == 0)
		return LZMA_DATA_ERROR;

	// Do the actual compression.
	lzma_ret ret = LZMA_BUF_ERROR;
	if (try_to_compress)
		ret = block_encode_normal(block, allocator,
				in, in_size, out, out_pos, out_size);

	if (ret != LZMA_OK) {
		// If the error was something else than output buffer
		// becoming full, return the error now.
		if (ret != LZMA_BUF_ERROR)
			return ret;

		// The data was uncompressible (at least with the options
		// given to us) or the output buffer was too small. Use the
		// uncompressed chunks of LZMA2 to wrap the data into a valid
		// Block. If we haven't been given enough output space, even
		// this may fail.
		return_if_error(block_encode_uncompressed(block, in, in_size,
				out, out_pos, out_size));
	}

	assert(*out_pos <= out_size);

	// Block Padding. No buffer overflow here, because we already adjusted
	// out_size so that (out_size - out_start) is a multiple of four.
	// Thus, if the buffer is full, the loop body can never run.
	for (size_t i = (size_t)(block->compressed_size); i & 3; ++i) {
		assert(*out_pos < out_size);
		out[(*out_pos)++] = 0x00;
	}

	// If there's no Check field, we are done now.
	if (check_size > 0) {
		// Calculate the integrity check. We reserved space for
		// the Check field earlier so we don't need to check for
		// available output space here.
		lzma_check_state check;
		lzma_check_init(&check, block->check);
		lzma_check_update(&check, block->check, in, in_size);
		lzma_check_finish(&check, block->check);

		memcpy(block->raw_check, check.buffer.u8, check_size);
		memcpy(out + *out_pos, check.buffer.u8, check_size);
		*out_pos += check_size;
	}

	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_block_buffer_encode(lzma_block *block, const lzma_allocator *allocator,
		const uint8_t *in, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size)
{
	return block_buffer_encode(block, allocator,
			in, in_size, out, out_pos, out_size, true);
}


extern LZMA_API(lzma_ret)
lzma_block_uncomp_encode(lzma_block *block,
		const uint8_t *in, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size)
{
	// It won't allocate any memory from heap so no need
	// for lzma_allocator.
	return block_buffer_encode(block, NULL,
			in, in_size, out, out_pos, out_size, false);
}
