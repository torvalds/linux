///////////////////////////////////////////////////////////////////////////////
//
/// \file       lzma2_decoder.h
/// \brief      LZMA2 decoder
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_LZMA2_DECODER_H
#define LZMA_LZMA2_DECODER_H

#include "common.h"

extern lzma_ret lzma_lzma2_decoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters);

extern uint64_t lzma_lzma2_decoder_memusage(const void *options);

extern lzma_ret lzma_lzma2_props_decode(
		void **options, const lzma_allocator *allocator,
		const uint8_t *props, size_t props_size);

#endif
