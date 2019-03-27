///////////////////////////////////////////////////////////////////////////////
//
/// \file       simple_coder.c
/// \brief      Wrapper for simple filters
///
/// Simple filters don't change the size of the data i.e. number of bytes
/// in equals the number of bytes out.
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "simple_private.h"


/// Copied or encodes/decodes more data to out[].
static lzma_ret
copy_or_code(lzma_simple_coder *coder, const lzma_allocator *allocator,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size, lzma_action action)
{
	assert(!coder->end_was_reached);

	if (coder->next.code == NULL) {
		lzma_bufcpy(in, in_pos, in_size, out, out_pos, out_size);

		// Check if end of stream was reached.
		if (coder->is_encoder && action == LZMA_FINISH
				&& *in_pos == in_size)
			coder->end_was_reached = true;

	} else {
		// Call the next coder in the chain to provide us some data.
		const lzma_ret ret = coder->next.code(
				coder->next.coder, allocator,
				in, in_pos, in_size,
				out, out_pos, out_size, action);

		if (ret == LZMA_STREAM_END) {
			assert(!coder->is_encoder
					|| action == LZMA_FINISH);
			coder->end_was_reached = true;

		} else if (ret != LZMA_OK) {
			return ret;
		}
	}

	return LZMA_OK;
}


static size_t
call_filter(lzma_simple_coder *coder, uint8_t *buffer, size_t size)
{
	const size_t filtered = coder->filter(coder->simple,
			coder->now_pos, coder->is_encoder,
			buffer, size);
	coder->now_pos += filtered;
	return filtered;
}


static lzma_ret
simple_code(void *coder_ptr, const lzma_allocator *allocator,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size, lzma_action action)
{
	lzma_simple_coder *coder = coder_ptr;

	// TODO: Add partial support for LZMA_SYNC_FLUSH. We can support it
	// in cases when the filter is able to filter everything. With most
	// simple filters it can be done at offset that is a multiple of 2,
	// 4, or 16. With x86 filter, it needs good luck, and thus cannot
	// be made to work predictably.
	if (action == LZMA_SYNC_FLUSH)
		return LZMA_OPTIONS_ERROR;

	// Flush already filtered data from coder->buffer[] to out[].
	if (coder->pos < coder->filtered) {
		lzma_bufcpy(coder->buffer, &coder->pos, coder->filtered,
				out, out_pos, out_size);

		// If we couldn't flush all the filtered data, return to
		// application immediately.
		if (coder->pos < coder->filtered)
			return LZMA_OK;

		if (coder->end_was_reached) {
			assert(coder->filtered == coder->size);
			return LZMA_STREAM_END;
		}
	}

	// If we get here, there is no filtered data left in the buffer.
	coder->filtered = 0;

	assert(!coder->end_was_reached);

	// If there is more output space left than there is unfiltered data
	// in coder->buffer[], flush coder->buffer[] to out[], and copy/code
	// more data to out[] hopefully filling it completely. Then filter
	// the data in out[]. This step is where most of the data gets
	// filtered if the buffer sizes used by the application are reasonable.
	const size_t out_avail = out_size - *out_pos;
	const size_t buf_avail = coder->size - coder->pos;
	if (out_avail > buf_avail || buf_avail == 0) {
		// Store the old position so that we know from which byte
		// to start filtering.
		const size_t out_start = *out_pos;

		// Flush data from coder->buffer[] to out[], but don't reset
		// coder->pos and coder->size yet. This way the coder can be
		// restarted if the next filter in the chain returns e.g.
		// LZMA_MEM_ERROR.
		memcpy(out + *out_pos, coder->buffer + coder->pos, buf_avail);
		*out_pos += buf_avail;

		// Copy/Encode/Decode more data to out[].
		{
			const lzma_ret ret = copy_or_code(coder, allocator,
					in, in_pos, in_size,
					out, out_pos, out_size, action);
			assert(ret != LZMA_STREAM_END);
			if (ret != LZMA_OK)
				return ret;
		}

		// Filter out[].
		const size_t size = *out_pos - out_start;
		const size_t filtered = call_filter(
				coder, out + out_start, size);

		const size_t unfiltered = size - filtered;
		assert(unfiltered <= coder->allocated / 2);

		// Now we can update coder->pos and coder->size, because
		// the next coder in the chain (if any) was successful.
		coder->pos = 0;
		coder->size = unfiltered;

		if (coder->end_was_reached) {
			// The last byte has been copied to out[] already.
			// They are left as is.
			coder->size = 0;

		} else if (unfiltered > 0) {
			// There is unfiltered data left in out[]. Copy it to
			// coder->buffer[] and rewind *out_pos appropriately.
			*out_pos -= unfiltered;
			memcpy(coder->buffer, out + *out_pos, unfiltered);
		}
	} else if (coder->pos > 0) {
		memmove(coder->buffer, coder->buffer + coder->pos, buf_avail);
		coder->size -= coder->pos;
		coder->pos = 0;
	}

	assert(coder->pos == 0);

	// If coder->buffer[] isn't empty, try to fill it by copying/decoding
	// more data. Then filter coder->buffer[] and copy the successfully
	// filtered data to out[]. It is probable, that some filtered and
	// unfiltered data will be left to coder->buffer[].
	if (coder->size > 0) {
		{
			const lzma_ret ret = copy_or_code(coder, allocator,
					in, in_pos, in_size,
					coder->buffer, &coder->size,
					coder->allocated, action);
			assert(ret != LZMA_STREAM_END);
			if (ret != LZMA_OK)
				return ret;
		}

		coder->filtered = call_filter(
				coder, coder->buffer, coder->size);

		// Everything is considered to be filtered if coder->buffer[]
		// contains the last bytes of the data.
		if (coder->end_was_reached)
			coder->filtered = coder->size;

		// Flush as much as possible.
		lzma_bufcpy(coder->buffer, &coder->pos, coder->filtered,
				out, out_pos, out_size);
	}

	// Check if we got everything done.
	if (coder->end_was_reached && coder->pos == coder->size)
		return LZMA_STREAM_END;

	return LZMA_OK;
}


static void
simple_coder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_simple_coder *coder = coder_ptr;
	lzma_next_end(&coder->next, allocator);
	lzma_free(coder->simple, allocator);
	lzma_free(coder, allocator);
	return;
}


static lzma_ret
simple_coder_update(void *coder_ptr, const lzma_allocator *allocator,
		const lzma_filter *filters_null lzma_attribute((__unused__)),
		const lzma_filter *reversed_filters)
{
	lzma_simple_coder *coder = coder_ptr;

	// No update support, just call the next filter in the chain.
	return lzma_next_filter_update(
			&coder->next, allocator, reversed_filters + 1);
}


extern lzma_ret
lzma_simple_coder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters,
		size_t (*filter)(void *simple, uint32_t now_pos,
			bool is_encoder, uint8_t *buffer, size_t size),
		size_t simple_size, size_t unfiltered_max,
		uint32_t alignment, bool is_encoder)
{
	// Allocate memory for the lzma_simple_coder structure if needed.
	lzma_simple_coder *coder = next->coder;
	if (coder == NULL) {
		// Here we allocate space also for the temporary buffer. We
		// need twice the size of unfiltered_max, because then it
		// is always possible to filter at least unfiltered_max bytes
		// more data in coder->buffer[] if it can be filled completely.
		coder = lzma_alloc(sizeof(lzma_simple_coder)
				+ 2 * unfiltered_max, allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;
		next->code = &simple_code;
		next->end = &simple_coder_end;
		next->update = &simple_coder_update;

		coder->next = LZMA_NEXT_CODER_INIT;
		coder->filter = filter;
		coder->allocated = 2 * unfiltered_max;

		// Allocate memory for filter-specific data structure.
		if (simple_size > 0) {
			coder->simple = lzma_alloc(simple_size, allocator);
			if (coder->simple == NULL)
				return LZMA_MEM_ERROR;
		} else {
			coder->simple = NULL;
		}
	}

	if (filters[0].options != NULL) {
		const lzma_options_bcj *simple = filters[0].options;
		coder->now_pos = simple->start_offset;
		if (coder->now_pos & (alignment - 1))
			return LZMA_OPTIONS_ERROR;
	} else {
		coder->now_pos = 0;
	}

	// Reset variables.
	coder->is_encoder = is_encoder;
	coder->end_was_reached = false;
	coder->pos = 0;
	coder->filtered = 0;
	coder->size = 0;

	return lzma_next_filter_init(&coder->next, allocator, filters + 1);
}
