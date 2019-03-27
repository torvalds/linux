///////////////////////////////////////////////////////////////////////////////
//
/// \file       lzma_decoder.h
/// \brief      LZMA decoder API
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_LZMA_DECODER_H
#define LZMA_LZMA_DECODER_H

#include "common.h"


/// Allocates and initializes LZMA decoder
extern lzma_ret lzma_lzma_decoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters);

extern uint64_t lzma_lzma_decoder_memusage(const void *options);

extern lzma_ret lzma_lzma_props_decode(
		void **options, const lzma_allocator *allocator,
		const uint8_t *props, size_t props_size);


/// \brief      Decodes the LZMA Properties byte (lc/lp/pb)
///
/// \return     true if error occurred, false on success
///
extern bool lzma_lzma_lclppb_decode(
		lzma_options_lzma *options, uint8_t byte);


#ifdef LZMA_LZ_DECODER_H
/// Allocate and setup function pointers only. This is used by LZMA1 and
/// LZMA2 decoders.
extern lzma_ret lzma_lzma_decoder_create(
		lzma_lz_decoder *lz, const lzma_allocator *allocator,
		const void *opt, lzma_lz_options *lz_options);

/// Gets memory usage without validating lc/lp/pb. This is used by LZMA2
/// decoder, because raw LZMA2 decoding doesn't need lc/lp/pb.
extern uint64_t lzma_lzma_decoder_memusage_nocheck(const void *options);

#endif

#endif
