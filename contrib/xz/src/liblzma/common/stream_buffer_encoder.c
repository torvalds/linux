///////////////////////////////////////////////////////////////////////////////
//
/// \file       stream_buffer_encoder.c
/// \brief      Single-call .xz Stream encoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "index.h"


/// Maximum size of Index that has exactly one Record.
/// Index Indicator + Number of Records + Record + CRC32 rounded up to
/// the next multiple of four.
#define INDEX_BOUND ((1 + 1 + 2 * LZMA_VLI_BYTES_MAX + 4 + 3) & ~3)

/// Stream Header, Stream Footer, and Index
#define HEADERS_BOUND (2 * LZMA_STREAM_HEADER_SIZE + INDEX_BOUND)


extern LZMA_API(size_t)
lzma_stream_buffer_bound(size_t uncompressed_size)
{
	// Get the maximum possible size of a Block.
	const size_t block_bound = lzma_block_buffer_bound(uncompressed_size);
	if (block_bound == 0)
		return 0;

	// Catch the possible integer overflow and also prevent the size of
	// the Stream exceeding LZMA_VLI_MAX (theoretically possible on
	// 64-bit systems).
	if (my_min(SIZE_MAX, LZMA_VLI_MAX) - block_bound < HEADERS_BOUND)
		return 0;

	return block_bound + HEADERS_BOUND;
}


extern LZMA_API(lzma_ret)
lzma_stream_buffer_encode(lzma_filter *filters, lzma_check check,
		const lzma_allocator *allocator,
		const uint8_t *in, size_t in_size,
		uint8_t *out, size_t *out_pos_ptr, size_t out_size)
{
	// Sanity checks
	if (filters == NULL || (unsigned int)(check) > LZMA_CHECK_ID_MAX
			|| (in == NULL && in_size != 0) || out == NULL
			|| out_pos_ptr == NULL || *out_pos_ptr > out_size)
		return LZMA_PROG_ERROR;

	if (!lzma_check_is_supported(check))
		return LZMA_UNSUPPORTED_CHECK;

	// Note for the paranoids: Index encoder prevents the Stream from
	// getting too big and still being accepted with LZMA_OK, and Block
	// encoder catches if the input is too big. So we don't need to
	// separately check if the buffers are too big.

	// Use a local copy. We update *out_pos_ptr only if everything
	// succeeds.
	size_t out_pos = *out_pos_ptr;

	// Check that there's enough space for both Stream Header and
	// Stream Footer.
	if (out_size - out_pos <= 2 * LZMA_STREAM_HEADER_SIZE)
		return LZMA_BUF_ERROR;

	// Reserve space for Stream Footer so we don't need to check for
	// available space again before encoding Stream Footer.
	out_size -= LZMA_STREAM_HEADER_SIZE;

	// Encode the Stream Header.
	lzma_stream_flags stream_flags = {
		.version = 0,
		.check = check,
	};

	if (lzma_stream_header_encode(&stream_flags, out + out_pos)
			!= LZMA_OK)
		return LZMA_PROG_ERROR;

	out_pos += LZMA_STREAM_HEADER_SIZE;

	// Encode a Block but only if there is at least one byte of input.
	lzma_block block = {
		.version = 0,
		.check = check,
		.filters = filters,
	};

	if (in_size > 0)
		return_if_error(lzma_block_buffer_encode(&block, allocator,
				in, in_size, out, &out_pos, out_size));

	// Index
	{
		// Create an Index. It will have one Record if there was
		// at least one byte of input to encode. Otherwise the
		// Index will be empty.
		lzma_index *i = lzma_index_init(allocator);
		if (i == NULL)
			return LZMA_MEM_ERROR;

		lzma_ret ret = LZMA_OK;

		if (in_size > 0)
			ret = lzma_index_append(i, allocator,
					lzma_block_unpadded_size(&block),
					block.uncompressed_size);

		// If adding the Record was successful, encode the Index
		// and get its size which will be stored into Stream Footer.
		if (ret == LZMA_OK) {
			ret = lzma_index_buffer_encode(
					i, out, &out_pos, out_size);

			stream_flags.backward_size = lzma_index_size(i);
		}

		lzma_index_end(i, allocator);

		if (ret != LZMA_OK)
			return ret;
	}

	// Stream Footer. We have already reserved space for this.
	if (lzma_stream_footer_encode(&stream_flags, out + out_pos)
			!= LZMA_OK)
		return LZMA_PROG_ERROR;

	out_pos += LZMA_STREAM_HEADER_SIZE;

	// Everything went fine, make the new output position available
	// to the application.
	*out_pos_ptr = out_pos;
	return LZMA_OK;
}
