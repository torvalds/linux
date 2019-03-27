///////////////////////////////////////////////////////////////////////////////
//
/// \file       armthumb.c
/// \brief      Filter for ARM-Thumb binaries
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "simple_private.h"


static size_t
armthumb_code(void *simple lzma_attribute((__unused__)),
		uint32_t now_pos, bool is_encoder,
		uint8_t *buffer, size_t size)
{
	size_t i;
	for (i = 0; i + 4 <= size; i += 2) {
		if ((buffer[i + 1] & 0xF8) == 0xF0
				&& (buffer[i + 3] & 0xF8) == 0xF8) {
			uint32_t src = ((buffer[i + 1] & 0x7) << 19)
					| (buffer[i + 0] << 11)
					| ((buffer[i + 3] & 0x7) << 8)
					| (buffer[i + 2]);

			src <<= 1;

			uint32_t dest;
			if (is_encoder)
				dest = now_pos + (uint32_t)(i) + 4 + src;
			else
				dest = src - (now_pos + (uint32_t)(i) + 4);

			dest >>= 1;
			buffer[i + 1] = 0xF0 | ((dest >> 19) & 0x7);
			buffer[i + 0] = (dest >> 11);
			buffer[i + 3] = 0xF8 | ((dest >> 8) & 0x7);
			buffer[i + 2] = (dest);
			i += 2;
		}
	}

	return i;
}


static lzma_ret
armthumb_coder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters, bool is_encoder)
{
	return lzma_simple_coder_init(next, allocator, filters,
			&armthumb_code, 0, 4, 2, is_encoder);
}


extern lzma_ret
lzma_simple_armthumb_encoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return armthumb_coder_init(next, allocator, filters, true);
}


extern lzma_ret
lzma_simple_armthumb_decoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return armthumb_coder_init(next, allocator, filters, false);
}
