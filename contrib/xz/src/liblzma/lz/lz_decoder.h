///////////////////////////////////////////////////////////////////////////////
//
/// \file       lz_decoder.h
/// \brief      LZ out window
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_LZ_DECODER_H
#define LZMA_LZ_DECODER_H

#include "common.h"


typedef struct {
	/// Pointer to the dictionary buffer. It can be an allocated buffer
	/// internal to liblzma, or it can a be a buffer given by the
	/// application when in single-call mode (not implemented yet).
	uint8_t *buf;

	/// Write position in dictionary. The next byte will be written to
	/// buf[pos].
	size_t pos;

	/// Indicates how full the dictionary is. This is used by
	/// dict_is_distance_valid() to detect corrupt files that would
	/// read beyond the beginning of the dictionary.
	size_t full;

	/// Write limit
	size_t limit;

	/// Size of the dictionary
	size_t size;

	/// True when dictionary should be reset before decoding more data.
	bool need_reset;

} lzma_dict;


typedef struct {
	size_t dict_size;
	const uint8_t *preset_dict;
	size_t preset_dict_size;
} lzma_lz_options;


typedef struct {
	/// Data specific to the LZ-based decoder
	void *coder;

	/// Function to decode from in[] to *dict
	lzma_ret (*code)(void *coder,
			lzma_dict *restrict dict, const uint8_t *restrict in,
			size_t *restrict in_pos, size_t in_size);

	void (*reset)(void *coder, const void *options);

	/// Set the uncompressed size
	void (*set_uncompressed)(void *coder, lzma_vli uncompressed_size);

	/// Free allocated resources
	void (*end)(void *coder, const lzma_allocator *allocator);

} lzma_lz_decoder;


#define LZMA_LZ_DECODER_INIT \
	(lzma_lz_decoder){ \
		.coder = NULL, \
		.code = NULL, \
		.reset = NULL, \
		.set_uncompressed = NULL, \
		.end = NULL, \
	}


extern lzma_ret lzma_lz_decoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters,
		lzma_ret (*lz_init)(lzma_lz_decoder *lz,
			const lzma_allocator *allocator, const void *options,
			lzma_lz_options *lz_options));

extern uint64_t lzma_lz_decoder_memusage(size_t dictionary_size);

extern void lzma_lz_decoder_uncompressed(
		void *coder, lzma_vli uncompressed_size);


//////////////////////
// Inline functions //
//////////////////////

/// Get a byte from the history buffer.
static inline uint8_t
dict_get(const lzma_dict *const dict, const uint32_t distance)
{
	return dict->buf[dict->pos - distance - 1
			+ (distance < dict->pos ? 0 : dict->size)];
}


/// Test if dictionary is empty.
static inline bool
dict_is_empty(const lzma_dict *const dict)
{
	return dict->full == 0;
}


/// Validate the match distance
static inline bool
dict_is_distance_valid(const lzma_dict *const dict, const size_t distance)
{
	return dict->full > distance;
}


/// Repeat *len bytes at distance.
static inline bool
dict_repeat(lzma_dict *dict, uint32_t distance, uint32_t *len)
{
	// Don't write past the end of the dictionary.
	const size_t dict_avail = dict->limit - dict->pos;
	uint32_t left = my_min(dict_avail, *len);
	*len -= left;

	// Repeat a block of data from the history. Because memcpy() is faster
	// than copying byte by byte in a loop, the copying process gets split
	// into three cases.
	if (distance < left) {
		// Source and target areas overlap, thus we can't use
		// memcpy() nor even memmove() safely.
		do {
			dict->buf[dict->pos] = dict_get(dict, distance);
			++dict->pos;
		} while (--left > 0);

	} else if (distance < dict->pos) {
		// The easiest and fastest case
		memcpy(dict->buf + dict->pos,
				dict->buf + dict->pos - distance - 1,
				left);
		dict->pos += left;

	} else {
		// The bigger the dictionary, the more rare this
		// case occurs. We need to "wrap" the dict, thus
		// we might need two memcpy() to copy all the data.
		assert(dict->full == dict->size);
		const uint32_t copy_pos
				= dict->pos - distance - 1 + dict->size;
		uint32_t copy_size = dict->size - copy_pos;

		if (copy_size < left) {
			memmove(dict->buf + dict->pos, dict->buf + copy_pos,
					copy_size);
			dict->pos += copy_size;
			copy_size = left - copy_size;
			memcpy(dict->buf + dict->pos, dict->buf, copy_size);
			dict->pos += copy_size;
		} else {
			memmove(dict->buf + dict->pos, dict->buf + copy_pos,
					left);
			dict->pos += left;
		}
	}

	// Update how full the dictionary is.
	if (dict->full < dict->pos)
		dict->full = dict->pos;

	return unlikely(*len != 0);
}


/// Puts one byte into the dictionary. Returns true if the dictionary was
/// already full and the byte couldn't be added.
static inline bool
dict_put(lzma_dict *dict, uint8_t byte)
{
	if (unlikely(dict->pos == dict->limit))
		return true;

	dict->buf[dict->pos++] = byte;

	if (dict->pos > dict->full)
		dict->full = dict->pos;

	return false;
}


/// Copies arbitrary amount of data into the dictionary.
static inline void
dict_write(lzma_dict *restrict dict, const uint8_t *restrict in,
		size_t *restrict in_pos, size_t in_size,
		size_t *restrict left)
{
	// NOTE: If we are being given more data than the size of the
	// dictionary, it could be possible to optimize the LZ decoder
	// so that not everything needs to go through the dictionary.
	// This shouldn't be very common thing in practice though, and
	// the slowdown of one extra memcpy() isn't bad compared to how
	// much time it would have taken if the data were compressed.

	if (in_size - *in_pos > *left)
		in_size = *in_pos + *left;

	*left -= lzma_bufcpy(in, in_pos, in_size,
			dict->buf, &dict->pos, dict->limit);

	if (dict->pos > dict->full)
		dict->full = dict->pos;

	return;
}


static inline void
dict_reset(lzma_dict *dict)
{
	dict->need_reset = true;
	return;
}

#endif
