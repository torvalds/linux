///////////////////////////////////////////////////////////////////////////////
//
/// \file       simple_private.h
/// \brief      Private definitions for so called simple filters
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_SIMPLE_PRIVATE_H
#define LZMA_SIMPLE_PRIVATE_H

#include "simple_coder.h"


typedef struct {
	/// Next filter in the chain
	lzma_next_coder next;

	/// True if the next coder in the chain has returned LZMA_STREAM_END.
	bool end_was_reached;

	/// True if filter() should encode the data; false to decode.
	/// Currently all simple filters use the same function for encoding
	/// and decoding, because the difference between encoders and decoders
	/// is very small.
	bool is_encoder;

	/// Pointer to filter-specific function, which does
	/// the actual filtering.
	size_t (*filter)(void *simple, uint32_t now_pos,
			bool is_encoder, uint8_t *buffer, size_t size);

	/// Pointer to filter-specific data, or NULL if filter doesn't need
	/// any extra data.
	void *simple;

	/// The lowest 32 bits of the current position in the data. Most
	/// filters need this to do conversions between absolute and relative
	/// addresses.
	uint32_t now_pos;

	/// Size of the memory allocated for the buffer.
	size_t allocated;

	/// Flushing position in the temporary buffer. buffer[pos] is the
	/// next byte to be copied to out[].
	size_t pos;

	/// buffer[filtered] is the first unfiltered byte. When pos is smaller
	/// than filtered, there is unflushed filtered data in the buffer.
	size_t filtered;

	/// Total number of bytes (both filtered and unfiltered) currently
	/// in the temporary buffer.
	size_t size;

	/// Temporary buffer
	uint8_t buffer[];
} lzma_simple_coder;


extern lzma_ret lzma_simple_coder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters,
		size_t (*filter)(void *simple, uint32_t now_pos,
			bool is_encoder, uint8_t *buffer, size_t size),
		size_t simple_size, size_t unfiltered_max,
		uint32_t alignment, bool is_encoder);

#endif
