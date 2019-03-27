///////////////////////////////////////////////////////////////////////////////
//
/// \file       easy_buffer_encoder.c
/// \brief      Easy single-call .xz Stream encoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "easy_preset.h"


extern LZMA_API(lzma_ret)
lzma_easy_buffer_encode(uint32_t preset, lzma_check check,
		const lzma_allocator *allocator, const uint8_t *in,
		size_t in_size, uint8_t *out, size_t *out_pos, size_t out_size)
{
	lzma_options_easy opt_easy;
	if (lzma_easy_preset(&opt_easy, preset))
		return LZMA_OPTIONS_ERROR;

	return lzma_stream_buffer_encode(opt_easy.filters, check,
			allocator, in, in_size, out, out_pos, out_size);
}
