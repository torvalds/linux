///////////////////////////////////////////////////////////////////////////////
//
/// \file       filter_buffer_decoder.c
/// \brief      Single-call raw decoding
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "filter_decoder.h"


extern LZMA_API(lzma_ret)
lzma_raw_buffer_decode(
		const lzma_filter *filters, const lzma_allocator *allocator,
		const uint8_t *in, size_t *in_pos, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size)
{
	// Validate what isn't validated later in filter_common.c.
	if (in == NULL || in_pos == NULL || *in_pos > in_size || out == NULL
			|| out_pos == NULL || *out_pos > out_size)
		return LZMA_PROG_ERROR;

	// Initialize the decoer.
	lzma_next_coder next = LZMA_NEXT_CODER_INIT;
	return_if_error(lzma_raw_decoder_init(&next, allocator, filters));

	// Store the positions so that we can restore them if something
	// goes wrong.
	const size_t in_start = *in_pos;
	const size_t out_start = *out_pos;

	// Do the actual decoding and free decoder's memory.
	lzma_ret ret = next.code(next.coder, allocator, in, in_pos, in_size,
			out, out_pos, out_size, LZMA_FINISH);

	if (ret == LZMA_STREAM_END) {
		ret = LZMA_OK;
	} else {
		if (ret == LZMA_OK) {
			// Either the input was truncated or the
			// output buffer was too small.
			assert(*in_pos == in_size || *out_pos == out_size);

			if (*in_pos != in_size) {
				// Since input wasn't consumed completely,
				// the output buffer became full and is
				// too small.
				ret = LZMA_BUF_ERROR;

			} else if (*out_pos != out_size) {
				// Since output didn't became full, the input
				// has to be truncated.
				ret = LZMA_DATA_ERROR;

			} else {
				// All the input was consumed and output
				// buffer is full. Now we don't immediately
				// know the reason for the error. Try
				// decoding one more byte. If it succeeds,
				// then the output buffer was too small. If
				// we cannot get a new output byte, the input
				// is truncated.
				uint8_t tmp[1];
				size_t tmp_pos = 0;
				(void)next.code(next.coder, allocator,
						in, in_pos, in_size,
						tmp, &tmp_pos, 1, LZMA_FINISH);

				if (tmp_pos == 1)
					ret = LZMA_BUF_ERROR;
				else
					ret = LZMA_DATA_ERROR;
			}
		}

		// Restore the positions.
		*in_pos = in_start;
		*out_pos = out_start;
	}

	lzma_next_end(&next, allocator);

	return ret;
}
