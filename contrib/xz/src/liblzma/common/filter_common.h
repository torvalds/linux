///////////////////////////////////////////////////////////////////////////////
//
/// \file       filter_common.c
/// \brief      Filter-specific stuff common for both encoder and decoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_FILTER_COMMON_H
#define LZMA_FILTER_COMMON_H

#include "common.h"


/// Both lzma_filter_encoder and lzma_filter_decoder begin with these members.
typedef struct {
	/// Filter ID
	lzma_vli id;

	/// Initializes the filter encoder and calls lzma_next_filter_init()
	/// for filters + 1.
	lzma_init_function init;

	/// Calculates memory usage of the encoder. If the options are
	/// invalid, UINT64_MAX is returned.
	uint64_t (*memusage)(const void *options);

} lzma_filter_coder;


typedef const lzma_filter_coder *(*lzma_filter_find)(lzma_vli id);


extern lzma_ret lzma_raw_coder_init(
		lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter *filters,
		lzma_filter_find coder_find, bool is_encoder);


extern uint64_t lzma_raw_coder_memusage(lzma_filter_find coder_find,
		const lzma_filter *filters);


#endif
