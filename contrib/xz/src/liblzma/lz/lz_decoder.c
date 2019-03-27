///////////////////////////////////////////////////////////////////////////////
//
/// \file       lz_decoder.c
/// \brief      LZ out window
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

// liblzma supports multiple LZ77-based filters. The LZ part is shared
// between these filters. The LZ code takes care of dictionary handling
// and passing the data between filters in the chain. The filter-specific
// part decodes from the input buffer to the dictionary.


#include "lz_decoder.h"


typedef struct {
	/// Dictionary (history buffer)
	lzma_dict dict;

	/// The actual LZ-based decoder e.g. LZMA
	lzma_lz_decoder lz;

	/// Next filter in the chain, if any. Note that LZMA and LZMA2 are
	/// only allowed as the last filter, but the long-range filter in
	/// future can be in the middle of the chain.
	lzma_next_coder next;

	/// True if the next filter in the chain has returned LZMA_STREAM_END.
	bool next_finished;

	/// True if the LZ decoder (e.g. LZMA) has detected end of payload
	/// marker. This may become true before next_finished becomes true.
	bool this_finished;

	/// Temporary buffer needed when the LZ-based filter is not the last
	/// filter in the chain. The output of the next filter is first
	/// decoded into buffer[], which is then used as input for the actual
	/// LZ-based decoder.
	struct {
		size_t pos;
		size_t size;
		uint8_t buffer[LZMA_BUFFER_SIZE];
	} temp;
} lzma_coder;


static void
lz_decoder_reset(lzma_coder *coder)
{
	coder->dict.pos = 0;
	coder->dict.full = 0;
	coder->dict.buf[coder->dict.size - 1] = '\0';
	coder->dict.need_reset = false;
	return;
}


static lzma_ret
decode_buffer(lzma_coder *coder,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size)
{
	while (true) {
		// Wrap the dictionary if needed.
		if (coder->dict.pos == coder->dict.size)
			coder->dict.pos = 0;

		// Store the current dictionary position. It is needed to know
		// where to start copying to the out[] buffer.
		const size_t dict_start = coder->dict.pos;

		// Calculate how much we allow coder->lz.code() to decode.
		// It must not decode past the end of the dictionary
		// buffer, and we don't want it to decode more than is
		// actually needed to fill the out[] buffer.
		coder->dict.limit = coder->dict.pos
				+ my_min(out_size - *out_pos,
					coder->dict.size - coder->dict.pos);

		// Call the coder->lz.code() to do the actual decoding.
		const lzma_ret ret = coder->lz.code(
				coder->lz.coder, &coder->dict,
				in, in_pos, in_size);

		// Copy the decoded data from the dictionary to the out[]
		// buffer.
		const size_t copy_size = coder->dict.pos - dict_start;
		assert(copy_size <= out_size - *out_pos);
		memcpy(out + *out_pos, coder->dict.buf + dict_start,
				copy_size);
		*out_pos += copy_size;

		// Reset the dictionary if so requested by coder->lz.code().
		if (coder->dict.need_reset) {
			lz_decoder_reset(coder);

			// Since we reset dictionary, we don't check if
			// dictionary became full.
			if (ret != LZMA_OK || *out_pos == out_size)
				return ret;
		} else {
			// Return if everything got decoded or an error
			// occurred, or if there's no more data to decode.
			//
			// Note that detecting if there's something to decode
			// is done by looking if dictionary become full
			// instead of looking if *in_pos == in_size. This
			// is because it is possible that all the input was
			// consumed already but some data is pending to be
			// written to the dictionary.
			if (ret != LZMA_OK || *out_pos == out_size
					|| coder->dict.pos < coder->dict.size)
				return ret;
		}
	}
}


static lzma_ret
lz_decode(void *coder_ptr,
		const lzma_allocator *allocator lzma_attribute((__unused__)),
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size,
		lzma_action action)
{
	lzma_coder *coder = coder_ptr;

	if (coder->next.code == NULL)
		return decode_buffer(coder, in, in_pos, in_size,
				out, out_pos, out_size);

	// We aren't the last coder in the chain, we need to decode
	// our input to a temporary buffer.
	while (*out_pos < out_size) {
		// Fill the temporary buffer if it is empty.
		if (!coder->next_finished
				&& coder->temp.pos == coder->temp.size) {
			coder->temp.pos = 0;
			coder->temp.size = 0;

			const lzma_ret ret = coder->next.code(
					coder->next.coder,
					allocator, in, in_pos, in_size,
					coder->temp.buffer, &coder->temp.size,
					LZMA_BUFFER_SIZE, action);

			if (ret == LZMA_STREAM_END)
				coder->next_finished = true;
			else if (ret != LZMA_OK || coder->temp.size == 0)
				return ret;
		}

		if (coder->this_finished) {
			if (coder->temp.size != 0)
				return LZMA_DATA_ERROR;

			if (coder->next_finished)
				return LZMA_STREAM_END;

			return LZMA_OK;
		}

		const lzma_ret ret = decode_buffer(coder, coder->temp.buffer,
				&coder->temp.pos, coder->temp.size,
				out, out_pos, out_size);

		if (ret == LZMA_STREAM_END)
			coder->this_finished = true;
		else if (ret != LZMA_OK)
			return ret;
		else if (coder->next_finished && *out_pos < out_size)
			return LZMA_DATA_ERROR;
	}

	return LZMA_OK;
}


static void
lz_decoder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_coder *coder = coder_ptr;

	lzma_next_end(&coder->next, allocator);
	lzma_free(coder->dict.buf, allocator);

	if (coder->lz.end != NULL)
		coder->lz.end(coder->lz.coder, allocator);
	else
		lzma_free(coder->lz.coder, allocator);

	lzma_free(coder, allocator);
	return;
}


extern lzma_ret
lzma_lz_decoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters,
		lzma_ret (*lz_init)(lzma_lz_decoder *lz,
			const lzma_allocator *allocator, const void *options,
			lzma_lz_options *lz_options))
{
	// Allocate the base structure if it isn't already allocated.
	lzma_coder *coder = next->coder;
	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;
		next->code = &lz_decode;
		next->end = &lz_decoder_end;

		coder->dict.buf = NULL;
		coder->dict.size = 0;
		coder->lz = LZMA_LZ_DECODER_INIT;
		coder->next = LZMA_NEXT_CODER_INIT;
	}

	// Allocate and initialize the LZ-based decoder. It will also give
	// us the dictionary size.
	lzma_lz_options lz_options;
	return_if_error(lz_init(&coder->lz, allocator,
			filters[0].options, &lz_options));

	// If the dictionary size is very small, increase it to 4096 bytes.
	// This is to prevent constant wrapping of the dictionary, which
	// would slow things down. The downside is that since we don't check
	// separately for the real dictionary size, we may happily accept
	// corrupt files.
	if (lz_options.dict_size < 4096)
		lz_options.dict_size = 4096;

	// Make dictionary size a multipe of 16. Some LZ-based decoders like
	// LZMA use the lowest bits lzma_dict.pos to know the alignment of the
	// data. Aligned buffer is also good when memcpying from the
	// dictionary to the output buffer, since applications are
	// recommended to give aligned buffers to liblzma.
	//
	// Avoid integer overflow.
	if (lz_options.dict_size > SIZE_MAX - 15)
		return LZMA_MEM_ERROR;

	lz_options.dict_size = (lz_options.dict_size + 15) & ~((size_t)(15));

	// Allocate and initialize the dictionary.
	if (coder->dict.size != lz_options.dict_size) {
		lzma_free(coder->dict.buf, allocator);
		coder->dict.buf
				= lzma_alloc(lz_options.dict_size, allocator);
		if (coder->dict.buf == NULL)
			return LZMA_MEM_ERROR;

		coder->dict.size = lz_options.dict_size;
	}

	lz_decoder_reset(next->coder);

	// Use the preset dictionary if it was given to us.
	if (lz_options.preset_dict != NULL
			&& lz_options.preset_dict_size > 0) {
		// If the preset dictionary is bigger than the actual
		// dictionary, copy only the tail.
		const size_t copy_size = my_min(lz_options.preset_dict_size,
				lz_options.dict_size);
		const size_t offset = lz_options.preset_dict_size - copy_size;
		memcpy(coder->dict.buf, lz_options.preset_dict + offset,
				copy_size);
		coder->dict.pos = copy_size;
		coder->dict.full = copy_size;
	}

	// Miscellaneous initializations
	coder->next_finished = false;
	coder->this_finished = false;
	coder->temp.pos = 0;
	coder->temp.size = 0;

	// Initialize the next filter in the chain, if any.
	return lzma_next_filter_init(&coder->next, allocator, filters + 1);
}


extern uint64_t
lzma_lz_decoder_memusage(size_t dictionary_size)
{
	return sizeof(lzma_coder) + (uint64_t)(dictionary_size);
}


extern void
lzma_lz_decoder_uncompressed(void *coder_ptr, lzma_vli uncompressed_size)
{
	lzma_coder *coder = coder_ptr;
	coder->lz.set_uncompressed(coder->lz.coder, uncompressed_size);
}
