///////////////////////////////////////////////////////////////////////////////
//
/// \file       simple_decoder.h
/// \brief      Properties decoder for simple filters
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_SIMPLE_DECODER_H
#define LZMA_SIMPLE_DECODER_H

#include "simple_coder.h"

extern lzma_ret lzma_simple_props_decode(
		void **options, const lzma_allocator *allocator,
		const uint8_t *props, size_t props_size);

#endif
