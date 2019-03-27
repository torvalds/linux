///////////////////////////////////////////////////////////////////////////////
//
/// \file       block_buffer_decoder.c
/// \brief      Single-call .xz Block decoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "block_decoder.h"


extern LZMA_API(lzma_ret)
lzma_block_buffer_decode(lzma_block *block, const lzma_allocator *allocator,
		const uint8_t *in, size_t *in_pos, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size)
{
	if (in_pos == NULL || (in == NULL && *in_pos != in_size)
			|| *in_pos > in_size || out_pos == NULL
			|| (out == NULL && *out_pos != out_size)
			|| *out_pos > out_size)
		return LZMA_PROG_ERROR;

	// Initialize the Block decoder.
	lzma_next_coder block_decoder = LZMA_NEXT_CODER_INIT;
	lzma_ret ret = lzma_block_decoder_init(
			&block_decoder, allocator, block);

	if (ret == LZMA_OK) {
		// Save the positions so that we can restore them in case
		// an error occurs.
		const size_t in_start = *in_pos;
		const size_t out_start = *out_pos;

		// Do the actual decoding.
		ret = block_decoder.code(block_decoder.coder, allocator,
				in, in_pos, in_size, out, out_pos, out_size,
				LZMA_FINISH);

		if (ret == LZMA_STREAM_END) {
			ret = LZMA_OK;
		} else {
			if (ret == LZMA_OK) {
				// Either the input was truncated or the
				// output buffer was too small.
				assert(*in_pos == in_size
						|| *out_pos == out_size);

				// If all the input was consumed, then the
				// input is truncated, even if the output
				// buffer is also full. This is because
				// processing the last byte of the Block
				// never produces output.
				//
				// NOTE: This assumption may break when new
				// filters are added, if the end marker of
				// the filter doesn't consume at least one
				// complete byte.
				if (*in_pos == in_size)
					ret = LZMA_DATA_ERROR;
				else
					ret = LZMA_BUF_ERROR;
			}

			// Restore the positions.
			*in_pos = in_start;
			*out_pos = out_start;
		}
	}

	// Free the decoder memory. This needs to be done even if
	// initialization fails, because the internal API doesn't
	// require the initialization function to free its memory on error.
	lzma_next_end(&block_decoder, allocator);

	return ret;
}
