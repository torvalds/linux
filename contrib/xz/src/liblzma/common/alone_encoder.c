///////////////////////////////////////////////////////////////////////////////
//
/// \file       alone_decoder.c
/// \brief      Decoder for LZMA_Alone files
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "lzma_encoder.h"


#define ALONE_HEADER_SIZE (1 + 4 + 8)


typedef struct {
	lzma_next_coder next;

	enum {
		SEQ_HEADER,
		SEQ_CODE,
	} sequence;

	size_t header_pos;
	uint8_t header[ALONE_HEADER_SIZE];
} lzma_alone_coder;


static lzma_ret
alone_encode(void *coder_ptr,
		const lzma_allocator *allocator lzma_attribute((__unused__)),
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size,
		lzma_action action)
{
	lzma_alone_coder *coder = coder_ptr;

	while (*out_pos < out_size)
	switch (coder->sequence) {
	case SEQ_HEADER:
		lzma_bufcpy(coder->header, &coder->header_pos,
				ALONE_HEADER_SIZE,
				out, out_pos, out_size);
		if (coder->header_pos < ALONE_HEADER_SIZE)
			return LZMA_OK;

		coder->sequence = SEQ_CODE;
		break;

	case SEQ_CODE:
		return coder->next.code(coder->next.coder,
				allocator, in, in_pos, in_size,
				out, out_pos, out_size, action);

	default:
		assert(0);
		return LZMA_PROG_ERROR;
	}

	return LZMA_OK;
}


static void
alone_encoder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_alone_coder *coder = coder_ptr;
	lzma_next_end(&coder->next, allocator);
	lzma_free(coder, allocator);
	return;
}


// At least for now, this is not used by any internal function.
static lzma_ret
alone_encoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_options_lzma *options)
{
	lzma_next_coder_init(&alone_encoder_init, next, allocator);

	lzma_alone_coder *coder = next->coder;

	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_alone_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;
		next->code = &alone_encode;
		next->end = &alone_encoder_end;
		coder->next = LZMA_NEXT_CODER_INIT;
	}

	// Basic initializations
	coder->sequence = SEQ_HEADER;
	coder->header_pos = 0;

	// Encode the header:
	// - Properties (1 byte)
	if (lzma_lzma_lclppb_encode(options, coder->header))
		return LZMA_OPTIONS_ERROR;

	// - Dictionary size (4 bytes)
	if (options->dict_size < LZMA_DICT_SIZE_MIN)
		return LZMA_OPTIONS_ERROR;

	// Round up to the next 2^n or 2^n + 2^(n - 1) depending on which
	// one is the next unless it is UINT32_MAX. While the header would
	// allow any 32-bit integer, we do this to keep the decoder of liblzma
	// accepting the resulting files.
	uint32_t d = options->dict_size - 1;
	d |= d >> 2;
	d |= d >> 3;
	d |= d >> 4;
	d |= d >> 8;
	d |= d >> 16;
	if (d != UINT32_MAX)
		++d;

	unaligned_write32le(coder->header + 1, d);

	// - Uncompressed size (always unknown and using EOPM)
	memset(coder->header + 1 + 4, 0xFF, 8);

	// Initialize the LZMA encoder.
	const lzma_filter_info filters[2] = {
		{
			.init = &lzma_lzma_encoder_init,
			.options = (void *)(options),
		}, {
			.init = NULL,
		}
	};

	return lzma_next_filter_init(&coder->next, allocator, filters);
}


/*
extern lzma_ret
lzma_alone_encoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_options_alone *options)
{
	lzma_next_coder_init(&alone_encoder_init, next, allocator, options);
}
*/


extern LZMA_API(lzma_ret)
lzma_alone_encoder(lzma_stream *strm, const lzma_options_lzma *options)
{
	lzma_next_strm_init(alone_encoder_init, strm, options);

	strm->internal->supported_actions[LZMA_RUN] = true;
	strm->internal->supported_actions[LZMA_FINISH] = true;

	return LZMA_OK;
}
