///////////////////////////////////////////////////////////////////////////////
//
/// \file       auto_decoder.c
/// \brief      Autodetect between .xz Stream and .lzma (LZMA_Alone) formats
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "stream_decoder.h"
#include "alone_decoder.h"


typedef struct {
	/// Stream decoder or LZMA_Alone decoder
	lzma_next_coder next;

	uint64_t memlimit;
	uint32_t flags;

	enum {
		SEQ_INIT,
		SEQ_CODE,
		SEQ_FINISH,
	} sequence;
} lzma_auto_coder;


static lzma_ret
auto_decode(void *coder_ptr, const lzma_allocator *allocator,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size, lzma_action action)
{
	lzma_auto_coder *coder = coder_ptr;

	switch (coder->sequence) {
	case SEQ_INIT:
		if (*in_pos >= in_size)
			return LZMA_OK;

		// Update the sequence now, because we want to continue from
		// SEQ_CODE even if we return some LZMA_*_CHECK.
		coder->sequence = SEQ_CODE;

		// Detect the file format. For now this is simple, since if
		// it doesn't start with 0xFD (the first magic byte of the
		// new format), it has to be LZMA_Alone, or something that
		// we don't support at all.
		if (in[*in_pos] == 0xFD) {
			return_if_error(lzma_stream_decoder_init(
					&coder->next, allocator,
					coder->memlimit, coder->flags));
		} else {
			return_if_error(lzma_alone_decoder_init(&coder->next,
					allocator, coder->memlimit, true));

			// If the application wants to know about missing
			// integrity check or about the check in general, we
			// need to handle it here, because LZMA_Alone decoder
			// doesn't accept any flags.
			if (coder->flags & LZMA_TELL_NO_CHECK)
				return LZMA_NO_CHECK;

			if (coder->flags & LZMA_TELL_ANY_CHECK)
				return LZMA_GET_CHECK;
		}

	// Fall through

	case SEQ_CODE: {
		const lzma_ret ret = coder->next.code(
				coder->next.coder, allocator,
				in, in_pos, in_size,
				out, out_pos, out_size, action);
		if (ret != LZMA_STREAM_END
				|| (coder->flags & LZMA_CONCATENATED) == 0)
			return ret;

		coder->sequence = SEQ_FINISH;
	}

	// Fall through

	case SEQ_FINISH:
		// When LZMA_DECODE_CONCATENATED was used and we were decoding
		// LZMA_Alone file, we need to check check that there is no
		// trailing garbage and wait for LZMA_FINISH.
		if (*in_pos < in_size)
			return LZMA_DATA_ERROR;

		return action == LZMA_FINISH ? LZMA_STREAM_END : LZMA_OK;

	default:
		assert(0);
		return LZMA_PROG_ERROR;
	}
}


static void
auto_decoder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_auto_coder *coder = coder_ptr;
	lzma_next_end(&coder->next, allocator);
	lzma_free(coder, allocator);
	return;
}


static lzma_check
auto_decoder_get_check(const void *coder_ptr)
{
	const lzma_auto_coder *coder = coder_ptr;

	// It is LZMA_Alone if get_check is NULL.
	return coder->next.get_check == NULL ? LZMA_CHECK_NONE
			: coder->next.get_check(coder->next.coder);
}


static lzma_ret
auto_decoder_memconfig(void *coder_ptr, uint64_t *memusage,
		uint64_t *old_memlimit, uint64_t new_memlimit)
{
	lzma_auto_coder *coder = coder_ptr;

	lzma_ret ret;

	if (coder->next.memconfig != NULL) {
		ret = coder->next.memconfig(coder->next.coder,
				memusage, old_memlimit, new_memlimit);
		assert(*old_memlimit == coder->memlimit);
	} else {
		// No coder is configured yet. Use the base value as
		// the current memory usage.
		*memusage = LZMA_MEMUSAGE_BASE;
		*old_memlimit = coder->memlimit;

		ret = LZMA_OK;
		if (new_memlimit != 0 && new_memlimit < *memusage)
			ret = LZMA_MEMLIMIT_ERROR;
	}

	if (ret == LZMA_OK && new_memlimit != 0)
		coder->memlimit = new_memlimit;

	return ret;
}


static lzma_ret
auto_decoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		uint64_t memlimit, uint32_t flags)
{
	lzma_next_coder_init(&auto_decoder_init, next, allocator);

	if (flags & ~LZMA_SUPPORTED_FLAGS)
		return LZMA_OPTIONS_ERROR;

	lzma_auto_coder *coder = next->coder;
	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_auto_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;
		next->code = &auto_decode;
		next->end = &auto_decoder_end;
		next->get_check = &auto_decoder_get_check;
		next->memconfig = &auto_decoder_memconfig;
		coder->next = LZMA_NEXT_CODER_INIT;
	}

	coder->memlimit = my_max(1, memlimit);
	coder->flags = flags;
	coder->sequence = SEQ_INIT;

	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_auto_decoder(lzma_stream *strm, uint64_t memlimit, uint32_t flags)
{
	lzma_next_strm_init(auto_decoder_init, strm, memlimit, flags);

	strm->internal->supported_actions[LZMA_RUN] = true;
	strm->internal->supported_actions[LZMA_FINISH] = true;

	return LZMA_OK;
}
