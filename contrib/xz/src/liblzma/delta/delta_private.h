///////////////////////////////////////////////////////////////////////////////
//
/// \file       delta_private.h
/// \brief      Private common stuff for Delta encoder and decoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_DELTA_PRIVATE_H
#define LZMA_DELTA_PRIVATE_H

#include "delta_common.h"

typedef struct {
	/// Next coder in the chain
	lzma_next_coder next;

	/// Delta distance
	size_t distance;

	/// Position in history[]
	uint8_t pos;

	/// Buffer to hold history of the original data
	uint8_t history[LZMA_DELTA_DIST_MAX];
} lzma_delta_coder;


extern lzma_ret lzma_delta_coder_init(
		lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters);

#endif
