///////////////////////////////////////////////////////////////////////////////
//
/// \file       delta_common.c
/// \brief      Common stuff for Delta encoder and decoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "delta_common.h"
#include "delta_private.h"


static void
delta_coder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_delta_coder *coder = coder_ptr;
	lzma_next_end(&coder->next, allocator);
	lzma_free(coder, allocator);
	return;
}


extern lzma_ret
lzma_delta_coder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	// Allocate memory for the decoder if needed.
	lzma_delta_coder *coder = next->coder;
	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_delta_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;

		// End function is the same for encoder and decoder.
		next->end = &delta_coder_end;
		coder->next = LZMA_NEXT_CODER_INIT;
	}

	// Validate the options.
	if (lzma_delta_coder_memusage(filters[0].options) == UINT64_MAX)
		return LZMA_OPTIONS_ERROR;

	// Set the delta distance.
	const lzma_options_delta *opt = filters[0].options;
	coder->distance = opt->dist;

	// Initialize the rest of the variables.
	coder->pos = 0;
	memzero(coder->history, LZMA_DELTA_DIST_MAX);

	// Initialize the next decoder in the chain, if any.
	return lzma_next_filter_init(&coder->next, allocator, filters + 1);
}


extern uint64_t
lzma_delta_coder_memusage(const void *options)
{
	const lzma_options_delta *opt = options;

	if (opt == NULL || opt->type != LZMA_DELTA_TYPE_BYTE
			|| opt->dist < LZMA_DELTA_DIST_MIN
			|| opt->dist > LZMA_DELTA_DIST_MAX)
		return UINT64_MAX;

	return sizeof(lzma_delta_coder);
}
