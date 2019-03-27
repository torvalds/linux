///////////////////////////////////////////////////////////////////////////////
//
/// \file       simple_encoder.c
/// \brief      Properties encoder for simple filters
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "simple_encoder.h"


extern lzma_ret
lzma_simple_props_size(uint32_t *size, const void *options)
{
	const lzma_options_bcj *const opt = options;
	*size = (opt == NULL || opt->start_offset == 0) ? 0 : 4;
	return LZMA_OK;
}


extern lzma_ret
lzma_simple_props_encode(const void *options, uint8_t *out)
{
	const lzma_options_bcj *const opt = options;

	// The default start offset is zero, so we don't need to store any
	// options unless the start offset is non-zero.
	if (opt == NULL || opt->start_offset == 0)
		return LZMA_OK;

	unaligned_write32le(out, opt->start_offset);

	return LZMA_OK;
}
