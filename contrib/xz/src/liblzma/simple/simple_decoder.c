///////////////////////////////////////////////////////////////////////////////
//
/// \file       simple_decoder.c
/// \brief      Properties decoder for simple filters
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "simple_decoder.h"


extern lzma_ret
lzma_simple_props_decode(void **options, const lzma_allocator *allocator,
		const uint8_t *props, size_t props_size)
{
	if (props_size == 0)
		return LZMA_OK;

	if (props_size != 4)
		return LZMA_OPTIONS_ERROR;

	lzma_options_bcj *opt = lzma_alloc(
			sizeof(lzma_options_bcj), allocator);
	if (opt == NULL)
		return LZMA_MEM_ERROR;

	opt->start_offset = unaligned_read32le(props);

	// Don't leave an options structure allocated if start_offset is zero.
	if (opt->start_offset == 0)
		lzma_free(opt, allocator);
	else
		*options = opt;

	return LZMA_OK;
}
