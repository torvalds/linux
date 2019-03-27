///////////////////////////////////////////////////////////////////////////////
//
/// \file       stream_decoder.c
/// \brief      Decodes .xz Streams
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "stream_decoder.h"
#include "block_decoder.h"


typedef struct {
	enum {
		SEQ_STREAM_HEADER,
		SEQ_BLOCK_HEADER,
		SEQ_BLOCK,
		SEQ_INDEX,
		SEQ_STREAM_FOOTER,
		SEQ_STREAM_PADDING,
	} sequence;

	/// Block or Metadata decoder. This takes little memory and the same
	/// data structure can be used to decode every Block Header, so it's
	/// a good idea to have a separate lzma_next_coder structure for it.
	lzma_next_coder block_decoder;

	/// Block options decoded by the Block Header decoder and used by
	/// the Block decoder.
	lzma_block block_options;

	/// Stream Flags from Stream Header
	lzma_stream_flags stream_flags;

	/// Index is hashed so that it can be compared to the sizes of Blocks
	/// with O(1) memory usage.
	lzma_index_hash *index_hash;

	/// Memory usage limit
	uint64_t memlimit;

	/// Amount of memory actually needed (only an estimate)
	uint64_t memusage;

	/// If true, LZMA_NO_CHECK is returned if the Stream has
	/// no integrity check.
	bool tell_no_check;

	/// If true, LZMA_UNSUPPORTED_CHECK is returned if the Stream has
	/// an integrity check that isn't supported by this liblzma build.
	bool tell_unsupported_check;

	/// If true, LZMA_GET_CHECK is returned after decoding Stream Header.
	bool tell_any_check;

	/// If true, we will tell the Block decoder to skip calculating
	/// and verifying the integrity check.
	bool ignore_check;

	/// If true, we will decode concatenated Streams that possibly have
	/// Stream Padding between or after them. LZMA_STREAM_END is returned
	/// once the application isn't giving us any new input, and we aren't
	/// in the middle of a Stream, and possible Stream Padding is a
	/// multiple of four bytes.
	bool concatenated;

	/// When decoding concatenated Streams, this is true as long as we
	/// are decoding the first Stream. This is needed to avoid misleading
	/// LZMA_FORMAT_ERROR in case the later Streams don't have valid magic
	/// bytes.
	bool first_stream;

	/// Write position in buffer[] and position in Stream Padding
	size_t pos;

	/// Buffer to hold Stream Header, Block Header, and Stream Footer.
	/// Block Header has biggest maximum size.
	uint8_t buffer[LZMA_BLOCK_HEADER_SIZE_MAX];
} lzma_stream_coder;


static lzma_ret
stream_decoder_reset(lzma_stream_coder *coder, const lzma_allocator *allocator)
{
	// Initialize the Index hash used to verify the Index.
	coder->index_hash = lzma_index_hash_init(coder->index_hash, allocator);
	if (coder->index_hash == NULL)
		return LZMA_MEM_ERROR;

	// Reset the rest of the variables.
	coder->sequence = SEQ_STREAM_HEADER;
	coder->pos = 0;

	return LZMA_OK;
}


static lzma_ret
stream_decode(void *coder_ptr, const lzma_allocator *allocator,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size, lzma_action action)
{
	lzma_stream_coder *coder = coder_ptr;

	// When decoding the actual Block, it may be able to produce more
	// output even if we don't give it any new input.
	while (true)
	switch (coder->sequence) {
	case SEQ_STREAM_HEADER: {
		// Copy the Stream Header to the internal buffer.
		lzma_bufcpy(in, in_pos, in_size, coder->buffer, &coder->pos,
				LZMA_STREAM_HEADER_SIZE);

		// Return if we didn't get the whole Stream Header yet.
		if (coder->pos < LZMA_STREAM_HEADER_SIZE)
			return LZMA_OK;

		coder->pos = 0;

		// Decode the Stream Header.
		const lzma_ret ret = lzma_stream_header_decode(
				&coder->stream_flags, coder->buffer);
		if (ret != LZMA_OK)
			return ret == LZMA_FORMAT_ERROR && !coder->first_stream
					? LZMA_DATA_ERROR : ret;

		// If we are decoding concatenated Streams, and the later
		// Streams have invalid Header Magic Bytes, we give
		// LZMA_DATA_ERROR instead of LZMA_FORMAT_ERROR.
		coder->first_stream = false;

		// Copy the type of the Check so that Block Header and Block
		// decoders see it.
		coder->block_options.check = coder->stream_flags.check;

		// Even if we return LZMA_*_CHECK below, we want
		// to continue from Block Header decoding.
		coder->sequence = SEQ_BLOCK_HEADER;

		// Detect if there's no integrity check or if it is
		// unsupported if those were requested by the application.
		if (coder->tell_no_check && coder->stream_flags.check
				== LZMA_CHECK_NONE)
			return LZMA_NO_CHECK;

		if (coder->tell_unsupported_check
				&& !lzma_check_is_supported(
					coder->stream_flags.check))
			return LZMA_UNSUPPORTED_CHECK;

		if (coder->tell_any_check)
			return LZMA_GET_CHECK;
	}

	// Fall through

	case SEQ_BLOCK_HEADER: {
		if (*in_pos >= in_size)
			return LZMA_OK;

		if (coder->pos == 0) {
			// Detect if it's Index.
			if (in[*in_pos] == 0x00) {
				coder->sequence = SEQ_INDEX;
				break;
			}

			// Calculate the size of the Block Header. Note that
			// Block Header decoder wants to see this byte too
			// so don't advance *in_pos.
			coder->block_options.header_size
					= lzma_block_header_size_decode(
						in[*in_pos]);
		}

		// Copy the Block Header to the internal buffer.
		lzma_bufcpy(in, in_pos, in_size, coder->buffer, &coder->pos,
				coder->block_options.header_size);

		// Return if we didn't get the whole Block Header yet.
		if (coder->pos < coder->block_options.header_size)
			return LZMA_OK;

		coder->pos = 0;

		// Version 1 is needed to support the .ignore_check option.
		coder->block_options.version = 1;

		// Set up a buffer to hold the filter chain. Block Header
		// decoder will initialize all members of this array so
		// we don't need to do it here.
		lzma_filter filters[LZMA_FILTERS_MAX + 1];
		coder->block_options.filters = filters;

		// Decode the Block Header.
		return_if_error(lzma_block_header_decode(&coder->block_options,
				allocator, coder->buffer));

		// If LZMA_IGNORE_CHECK was used, this flag needs to be set.
		// It has to be set after lzma_block_header_decode() because
		// it always resets this to false.
		coder->block_options.ignore_check = coder->ignore_check;

		// Check the memory usage limit.
		const uint64_t memusage = lzma_raw_decoder_memusage(filters);
		lzma_ret ret;

		if (memusage == UINT64_MAX) {
			// One or more unknown Filter IDs.
			ret = LZMA_OPTIONS_ERROR;
		} else {
			// Now we can set coder->memusage since we know that
			// the filter chain is valid. We don't want
			// lzma_memusage() to return UINT64_MAX in case of
			// invalid filter chain.
			coder->memusage = memusage;

			if (memusage > coder->memlimit) {
				// The chain would need too much memory.
				ret = LZMA_MEMLIMIT_ERROR;
			} else {
				// Memory usage is OK.
				// Initialize the Block decoder.
				ret = lzma_block_decoder_init(
						&coder->block_decoder,
						allocator,
						&coder->block_options);
			}
		}

		// Free the allocated filter options since they are needed
		// only to initialize the Block decoder.
		for (size_t i = 0; i < LZMA_FILTERS_MAX; ++i)
			lzma_free(filters[i].options, allocator);

		coder->block_options.filters = NULL;

		// Check if memory usage calculation and Block enocoder
		// initialization succeeded.
		if (ret != LZMA_OK)
			return ret;

		coder->sequence = SEQ_BLOCK;
	}

	// Fall through

	case SEQ_BLOCK: {
		const lzma_ret ret = coder->block_decoder.code(
				coder->block_decoder.coder, allocator,
				in, in_pos, in_size, out, out_pos, out_size,
				action);

		if (ret != LZMA_STREAM_END)
			return ret;

		// Block decoded successfully. Add the new size pair to
		// the Index hash.
		return_if_error(lzma_index_hash_append(coder->index_hash,
				lzma_block_unpadded_size(
					&coder->block_options),
				coder->block_options.uncompressed_size));

		coder->sequence = SEQ_BLOCK_HEADER;
		break;
	}

	case SEQ_INDEX: {
		// If we don't have any input, don't call
		// lzma_index_hash_decode() since it would return
		// LZMA_BUF_ERROR, which we must not do here.
		if (*in_pos >= in_size)
			return LZMA_OK;

		// Decode the Index and compare it to the hash calculated
		// from the sizes of the Blocks (if any).
		const lzma_ret ret = lzma_index_hash_decode(coder->index_hash,
				in, in_pos, in_size);
		if (ret != LZMA_STREAM_END)
			return ret;

		coder->sequence = SEQ_STREAM_FOOTER;
	}

	// Fall through

	case SEQ_STREAM_FOOTER: {
		// Copy the Stream Footer to the internal buffer.
		lzma_bufcpy(in, in_pos, in_size, coder->buffer, &coder->pos,
				LZMA_STREAM_HEADER_SIZE);

		// Return if we didn't get the whole Stream Footer yet.
		if (coder->pos < LZMA_STREAM_HEADER_SIZE)
			return LZMA_OK;

		coder->pos = 0;

		// Decode the Stream Footer. The decoder gives
		// LZMA_FORMAT_ERROR if the magic bytes don't match,
		// so convert that return code to LZMA_DATA_ERROR.
		lzma_stream_flags footer_flags;
		const lzma_ret ret = lzma_stream_footer_decode(
				&footer_flags, coder->buffer);
		if (ret != LZMA_OK)
			return ret == LZMA_FORMAT_ERROR
					? LZMA_DATA_ERROR : ret;

		// Check that Index Size stored in the Stream Footer matches
		// the real size of the Index field.
		if (lzma_index_hash_size(coder->index_hash)
				!= footer_flags.backward_size)
			return LZMA_DATA_ERROR;

		// Compare that the Stream Flags fields are identical in
		// both Stream Header and Stream Footer.
		return_if_error(lzma_stream_flags_compare(
				&coder->stream_flags, &footer_flags));

		if (!coder->concatenated)
			return LZMA_STREAM_END;

		coder->sequence = SEQ_STREAM_PADDING;
	}

	// Fall through

	case SEQ_STREAM_PADDING:
		assert(coder->concatenated);

		// Skip over possible Stream Padding.
		while (true) {
			if (*in_pos >= in_size) {
				// Unless LZMA_FINISH was used, we cannot
				// know if there's more input coming later.
				if (action != LZMA_FINISH)
					return LZMA_OK;

				// Stream Padding must be a multiple of
				// four bytes.
				return coder->pos == 0
						? LZMA_STREAM_END
						: LZMA_DATA_ERROR;
			}

			// If the byte is not zero, it probably indicates
			// beginning of a new Stream (or the file is corrupt).
			if (in[*in_pos] != 0x00)
				break;

			++*in_pos;
			coder->pos = (coder->pos + 1) & 3;
		}

		// Stream Padding must be a multiple of four bytes (empty
		// Stream Padding is OK).
		if (coder->pos != 0) {
			++*in_pos;
			return LZMA_DATA_ERROR;
		}

		// Prepare to decode the next Stream.
		return_if_error(stream_decoder_reset(coder, allocator));
		break;

	default:
		assert(0);
		return LZMA_PROG_ERROR;
	}

	// Never reached
}


static void
stream_decoder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_stream_coder *coder = coder_ptr;
	lzma_next_end(&coder->block_decoder, allocator);
	lzma_index_hash_end(coder->index_hash, allocator);
	lzma_free(coder, allocator);
	return;
}


static lzma_check
stream_decoder_get_check(const void *coder_ptr)
{
	const lzma_stream_coder *coder = coder_ptr;
	return coder->stream_flags.check;
}


static lzma_ret
stream_decoder_memconfig(void *coder_ptr, uint64_t *memusage,
		uint64_t *old_memlimit, uint64_t new_memlimit)
{
	lzma_stream_coder *coder = coder_ptr;

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
lzma_stream_decoder_init(
		lzma_next_coder *next, const lzma_allocator *allocator,
		uint64_t memlimit, uint32_t flags)
{
	lzma_next_coder_init(&lzma_stream_decoder_init, next, allocator);

	if (flags & ~LZMA_SUPPORTED_FLAGS)
		return LZMA_OPTIONS_ERROR;

	lzma_stream_coder *coder = next->coder;
	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_stream_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;
		next->code = &stream_decode;
		next->end = &stream_decoder_end;
		next->get_check = &stream_decoder_get_check;
		next->memconfig = &stream_decoder_memconfig;

		coder->block_decoder = LZMA_NEXT_CODER_INIT;
		coder->index_hash = NULL;
	}

	coder->memlimit = my_max(1, memlimit);
	coder->memusage = LZMA_MEMUSAGE_BASE;
	coder->tell_no_check = (flags & LZMA_TELL_NO_CHECK) != 0;
	coder->tell_unsupported_check
			= (flags & LZMA_TELL_UNSUPPORTED_CHECK) != 0;
	coder->tell_any_check = (flags & LZMA_TELL_ANY_CHECK) != 0;
	coder->ignore_check = (flags & LZMA_IGNORE_CHECK) != 0;
	coder->concatenated = (flags & LZMA_CONCATENATED) != 0;
	coder->first_stream = true;

	return stream_decoder_reset(coder, allocator);
}


extern LZMA_API(lzma_ret)
lzma_stream_decoder(lzma_stream *strm, uint64_t memlimit, uint32_t flags)
{
	lzma_next_strm_init(lzma_stream_decoder_init, strm, memlimit, flags);

	strm->internal->supported_actions[LZMA_RUN] = true;
	strm->internal->supported_actions[LZMA_FINISH] = true;

	return LZMA_OK;
}
