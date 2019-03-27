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

#include "alone_decoder.h"
#include "lzma_decoder.h"
#include "lz_decoder.h"


typedef struct {
	lzma_next_coder next;

	enum {
		SEQ_PROPERTIES,
		SEQ_DICTIONARY_SIZE,
		SEQ_UNCOMPRESSED_SIZE,
		SEQ_CODER_INIT,
		SEQ_CODE,
	} sequence;

	/// If true, reject files that are unlikely to be .lzma files.
	/// If false, more non-.lzma files get accepted and will give
	/// LZMA_DATA_ERROR either immediately or after a few output bytes.
	bool picky;

	/// Position in the header fields
	size_t pos;

	/// Uncompressed size decoded from the header
	lzma_vli uncompressed_size;

	/// Memory usage limit
	uint64_t memlimit;

	/// Amount of memory actually needed (only an estimate)
	uint64_t memusage;

	/// Options decoded from the header needed to initialize
	/// the LZMA decoder
	lzma_options_lzma options;
} lzma_alone_coder;


static lzma_ret
alone_decode(void *coder_ptr,
		const lzma_allocator *allocator lzma_attribute((__unused__)),
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size,
		lzma_action action)
{
	lzma_alone_coder *coder = coder_ptr;

	while (*out_pos < out_size
			&& (coder->sequence == SEQ_CODE || *in_pos < in_size))
	switch (coder->sequence) {
	case SEQ_PROPERTIES:
		if (lzma_lzma_lclppb_decode(&coder->options, in[*in_pos]))
			return LZMA_FORMAT_ERROR;

		coder->sequence = SEQ_DICTIONARY_SIZE;
		++*in_pos;
		break;

	case SEQ_DICTIONARY_SIZE:
		coder->options.dict_size
				|= (size_t)(in[*in_pos]) << (coder->pos * 8);

		if (++coder->pos == 4) {
			if (coder->picky && coder->options.dict_size
					!= UINT32_MAX) {
				// A hack to ditch tons of false positives:
				// We allow only dictionary sizes that are
				// 2^n or 2^n + 2^(n-1). LZMA_Alone created
				// only files with 2^n, but accepts any
				// dictionary size.
				uint32_t d = coder->options.dict_size - 1;
				d |= d >> 2;
				d |= d >> 3;
				d |= d >> 4;
				d |= d >> 8;
				d |= d >> 16;
				++d;

				if (d != coder->options.dict_size)
					return LZMA_FORMAT_ERROR;
			}

			coder->pos = 0;
			coder->sequence = SEQ_UNCOMPRESSED_SIZE;
		}

		++*in_pos;
		break;

	case SEQ_UNCOMPRESSED_SIZE:
		coder->uncompressed_size
				|= (lzma_vli)(in[*in_pos]) << (coder->pos * 8);
		++*in_pos;
		if (++coder->pos < 8)
			break;

		// Another hack to ditch false positives: Assume that
		// if the uncompressed size is known, it must be less
		// than 256 GiB.
		if (coder->picky
				&& coder->uncompressed_size != LZMA_VLI_UNKNOWN
				&& coder->uncompressed_size
					>= (LZMA_VLI_C(1) << 38))
			return LZMA_FORMAT_ERROR;

		// Calculate the memory usage so that it is ready
		// for SEQ_CODER_INIT.
		coder->memusage = lzma_lzma_decoder_memusage(&coder->options)
				+ LZMA_MEMUSAGE_BASE;

		coder->pos = 0;
		coder->sequence = SEQ_CODER_INIT;

	// Fall through

	case SEQ_CODER_INIT: {
		if (coder->memusage > coder->memlimit)
			return LZMA_MEMLIMIT_ERROR;

		lzma_filter_info filters[2] = {
			{
				.init = &lzma_lzma_decoder_init,
				.options = &coder->options,
			}, {
				.init = NULL,
			}
		};

		const lzma_ret ret = lzma_next_filter_init(&coder->next,
				allocator, filters);
		if (ret != LZMA_OK)
			return ret;

		// Use a hack to set the uncompressed size.
		lzma_lz_decoder_uncompressed(coder->next.coder,
				coder->uncompressed_size);

		coder->sequence = SEQ_CODE;
		break;
	}

	case SEQ_CODE: {
		return coder->next.code(coder->next.coder,
				allocator, in, in_pos, in_size,
				out, out_pos, out_size, action);
	}

	default:
		return LZMA_PROG_ERROR;
	}

	return LZMA_OK;
}


static void
alone_decoder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_alone_coder *coder = coder_ptr;
	lzma_next_end(&coder->next, allocator);
	lzma_free(coder, allocator);
	return;
}


static lzma_ret
alone_decoder_memconfig(void *coder_ptr, uint64_t *memusage,
		uint64_t *old_memlimit, uint64_t new_memlimit)
{
	lzma_alone_coder *coder = coder_ptr;

	*memusage = coder->memusage;
	*old_memlimit = coder->memlimit;

	if (new_memlimit != 0) {
		if (new_memlimit < coder->memusage)
			return LZMA_MEMLIMIT_ERROR;

		coder->memlimit = new_memlimit;
	}

	return LZMA_OK;
}


extern lzma_ret
lzma_alone_decoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		uint64_t memlimit, bool picky)
{
	lzma_next_coder_init(&lzma_alone_decoder_init, next, allocator);

	lzma_alone_coder *coder = next->coder;

	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_alone_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;
		next->code = &alone_decode;
		next->end = &alone_decoder_end;
		next->memconfig = &alone_decoder_memconfig;
		coder->next = LZMA_NEXT_CODER_INIT;
	}

	coder->sequence = SEQ_PROPERTIES;
	coder->picky = picky;
	coder->pos = 0;
	coder->options.dict_size = 0;
	coder->options.preset_dict = NULL;
	coder->options.preset_dict_size = 0;
	coder->uncompressed_size = 0;
	coder->memlimit = my_max(1, memlimit);
	coder->memusage = LZMA_MEMUSAGE_BASE;

	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_alone_decoder(lzma_stream *strm, uint64_t memlimit)
{
	lzma_next_strm_init(lzma_alone_decoder_init, strm, memlimit, false);

	strm->internal->supported_actions[LZMA_RUN] = true;
	strm->internal->supported_actions[LZMA_FINISH] = true;

	return LZMA_OK;
}
