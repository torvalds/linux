///////////////////////////////////////////////////////////////////////////////
//
/// \file       lzma_encoder.h
/// \brief      LZMA encoder API
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_LZMA_ENCODER_H
#define LZMA_LZMA_ENCODER_H

#include "common.h"


typedef struct lzma_lzma1_encoder_s lzma_lzma1_encoder;


extern lzma_ret lzma_lzma_encoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters);


extern uint64_t lzma_lzma_encoder_memusage(const void *options);

extern lzma_ret lzma_lzma_props_encode(const void *options, uint8_t *out);


/// Encodes lc/lp/pb into one byte. Returns false on success and true on error.
extern bool lzma_lzma_lclppb_encode(
		const lzma_options_lzma *options, uint8_t *byte);


#ifdef LZMA_LZ_ENCODER_H

/// Initializes raw LZMA encoder; this is used by LZMA2.
extern lzma_ret lzma_lzma_encoder_create(
		void **coder_ptr, const lzma_allocator *allocator,
		const lzma_options_lzma *options, lzma_lz_options *lz_options);


/// Resets an already initialized LZMA encoder; this is used by LZMA2.
extern lzma_ret lzma_lzma_encoder_reset(
		lzma_lzma1_encoder *coder, const lzma_options_lzma *options);


extern lzma_ret lzma_lzma_encode(lzma_lzma1_encoder *restrict coder,
		lzma_mf *restrict mf, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size,
		uint32_t read_limit);

#endif

#endif
