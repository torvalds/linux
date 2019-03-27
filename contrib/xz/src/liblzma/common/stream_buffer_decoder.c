///////////////////////////////////////////////////////////////////////////////
//
/// \file       stream_buffer_decoder.c
/// \brief      Single-call .xz Stream decoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "stream_decoder.h"


extern LZMA_API(lzma_ret)
lzma_stream_buffer_decode(uint64_t *memlimit, uint32_t flags,
		const lzma_allocator *allocator,
		const uint8_t *in, size_t *in_pos, size_t in_size,
		uint8_t *out, size_t *out_pos, size_t out_size)
{
	// Sanity checks
	if (in_pos == NULL || (in == NULL && *in_pos != in_size)
			|| *in_pos > in_size || out_pos == NULL
			|| (out == NULL && *out_pos != out_size)
			|| *out_pos > out_size)
		return LZMA_PROG_ERROR;

	// Catch flags that are not allowed in buffer-to-buffer decoding.
	if (flags & LZMA_TELL_ANY_CHECK)
		return LZMA_PROG_ERROR;

	// Initialize the Stream decoder.
	// TODO: We need something to tell the decoder that it can use the
	// output buffer as workspace, and thus save significant amount of RAM.
	lzma_next_coder stream_decoder = LZMA_NEXT_CODER_INIT;
	lzma_ret ret = lzma_stream_decoder_init(
			&stream_decoder, allocator, *memlimit, flags);

	if (ret == LZMA_OK) {
		// Save the positions so that we can restore them in case
		// an error occurs.
		const size_t in_start = *in_pos;
		const size_t out_start = *out_pos;

		// Do the actual decoding.
		ret = stream_decoder.code(stream_decoder.coder, allocator,
				in, in_pos, in_size, out, out_pos, out_size,
				LZMA_FINISH);

		if (ret == LZMA_STREAM_END) {
			ret = LZMA_OK;
		} else {
			// Something went wrong, restore the positions.
			*in_pos = in_start;
			*out_pos = out_start;

			if (ret == LZMA_OK) {
				// Either the input was truncated or the
				// output buffer was too small.
				assert(*in_pos == in_size
						|| *out_pos == out_size);

				// If all the input was consumed, then the
				// input is truncated, even if the output
				// buffer is also full. This is because
				// processing the last byte of the Stream
				// never produces output.
				if (*in_pos == in_size)
					ret = LZMA_DATA_ERROR;
				else
					ret = LZMA_BUF_ERROR;

			} else if (ret == LZMA_MEMLIMIT_ERROR) {
				// Let the caller know how much memory would
				// have been needed.
				uint64_t memusage;
				(void)stream_decoder.memconfig(
						stream_decoder.coder,
						memlimit, &memusage, 0);
			}
		}
	}

	// Free the decoder memory. This needs to be done even if
	// initialization fails, because the internal API doesn't
	// require the initialization function to free its memory on error.
	lzma_next_end(&stream_decoder, allocator);

	return ret;
}
