///////////////////////////////////////////////////////////////////////////////
//
/// \file       lzma2_encoder.h
/// \brief      LZMA2 encoder
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_LZMA2_ENCODER_H
#define LZMA_LZMA2_ENCODER_H

#include "common.h"


/// Maximum number of bytes of actual data per chunk (no headers)
#define LZMA2_CHUNK_MAX (UINT32_C(1) << 16)

/// Maximum uncompressed size of LZMA chunk (no headers)
#define LZMA2_UNCOMPRESSED_MAX (UINT32_C(1) << 21)

/// Maximum size of LZMA2 headers
#define LZMA2_HEADER_MAX 6

/// Size of a header for uncompressed chunk
#define LZMA2_HEADER_UNCOMPRESSED 3


extern lzma_ret lzma_lzma2_encoder_init(
		lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters);

extern uint64_t lzma_lzma2_encoder_memusage(const void *options);

extern lzma_ret lzma_lzma2_props_encode(const void *options, uint8_t *out);

extern uint64_t lzma_lzma2_block_size(const void *options);

#endif
