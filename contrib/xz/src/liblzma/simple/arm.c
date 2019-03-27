///////////////////////////////////////////////////////////////////////////////
//
/// \file       arm.c
/// \brief      Filter for ARM binaries
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
arm_code(void *simple lzma_attribute((__unused__)),
		uint32_t now_pos, bool is_encoder,
		uint8_t *buffer, size_t size)
{
	size_t i;
	for (i = 0; i + 4 <= size; i += 4) {
		if (buffer[i + 3] == 0xEB) {
			uint32_t src = (buffer[i + 2] << 16)
					| (buffer[i + 1] << 8)
					| (buffer[i + 0]);
			src <<= 2;

			uint32_t dest;
			if (is_encoder)
				dest = now_pos + (uint32_t)(i) + 8 + src;
			else
				dest = src - (now_pos + (uint32_t)(i) + 8);

			dest >>= 2;
			buffer[i + 2] = (dest >> 16);
			buffer[i + 1] = (dest >> 8);
			buffer[i + 0] = dest;
		}
	}

	return i;
}


static lzma_ret
arm_coder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters, bool is_encoder)
{
	return lzma_simple_coder_init(next, allocator, filters,
			&arm_code, 0, 4, 4, is_encoder);
}


extern lzma_ret
lzma_simple_arm_encoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return arm_coder_init(next, allocator, filters, true);
}


extern lzma_ret
lzma_simple_arm_decoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return arm_coder_init(next, allocator, filters, false);
}
