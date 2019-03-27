///////////////////////////////////////////////////////////////////////////////
//
/// \file       stream_encoder.c
/// \brief      Encodes .xz Streams
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "block_encoder.h"
#include "index_encoder.h"


typedef struct {
	enum {
		SEQ_STREAM_HEADER,
		SEQ_BLOCK_INIT,
		SEQ_BLOCK_HEADER,
		SEQ_BLOCK_ENCODE,
		SEQ_INDEX_ENCODE,
		SEQ_STREAM_FOOTER,
	} sequence;

	/// True if Block encoder has been initialized by
	/// stream_encoder_init() or stream_encoder_update()
	/// and thus doesn't need to be initialized in stream_encode().
	bool block_encoder_is_initialized;

	/// Block
	lzma_next_coder block_encoder;

	/// Options for the Block encoder
	lzma_block block_options;

	/// The filter chain currently in use
	lzma_filter filters[LZMA_FILTERS_MAX + 1];

	/// Index encoder. This is separate from Block encoder, because this
	/// doesn't take much memory, and when encoding multiple Streams
	/// with the same encoding options we avoid reallocating memory.
	lzma_next_coder index_encoder;

	/// Index to hold sizes of the Blocks
	lzma_index *index;

	/// Read position in buffer[]
	size_t buffer_pos;

	/// Total number of bytes in buffer[]
	size_t buffer_size;

	/// Buffer to hold Stream Header, Block Header, and Stream Footer.
	/// Block Header has biggest maximum size.
	uint8_t buffer[LZMA_BLOCK_HEADER_SIZE_MAX];
} lzma_stream_coder;


static lzma_ret
block_encoder_init(lzma_stream_coder *coder, const lzma_allocator *allocator)
{
	// Prepare the Block options. Even though Block encoder doesn't need
	// compressed_size, uncompressed_size, and header_size to be
	// initialized, it is a good idea to do it here, because this way
	// we catch if someone gave us Filter ID that cannot be used in
	// Blocks/Streams.
	coder->block_options.compressed_size = LZMA_VLI_UNKNOWN;
	coder->block_options.uncompressed_size = LZMA_VLI_UNKNOWN;

	return_if_error(lzma_block_header_size(&coder->block_options));

	// Initialize the actual Block encoder.
	return lzma_block_encoder_init(&coder->block_encoder, allocator,
			&coder->block_options);
}


static lzma_ret
stream_encode(void *coder_ptr, const lzma_allocator *allocator,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size, lzma_action action)
{
	lzma_stream_coder *coder = coder_ptr;

	// Main loop
	while (*out_pos < out_size)
	switch (coder->sequence) {
	case SEQ_STREAM_HEADER:
	case SEQ_BLOCK_HEADER:
	case SEQ_STREAM_FOOTER:
		lzma_bufcpy(coder->buffer, &coder->buffer_pos,
				coder->buffer_size, out, out_pos, out_size);
		if (coder->buffer_pos < coder->buffer_size)
			return LZMA_OK;

		if (coder->sequence == SEQ_STREAM_FOOTER)
			return LZMA_STREAM_END;

		coder->buffer_pos = 0;
		++coder->sequence;
		break;

	case SEQ_BLOCK_INIT: {
		if (*in_pos == in_size) {
			// If we are requested to flush or finish the current
			// Block, return LZMA_STREAM_END immediately since
			// there's nothing to do.
			if (action != LZMA_FINISH)
				return action == LZMA_RUN
						? LZMA_OK : LZMA_STREAM_END;

			// The application had used LZMA_FULL_FLUSH to finish
			// the previous Block, but now wants to finish without
			// encoding new data, or it is simply creating an
			// empty Stream with no Blocks.
			//
			// Initialize the Index encoder, and continue to
			// actually encoding the Index.
			return_if_error(lzma_index_encoder_init(
					&coder->index_encoder, allocator,
					coder->index));
			coder->sequence = SEQ_INDEX_ENCODE;
			break;
		}

		// Initialize the Block encoder unless it was already
		// initialized by stream_encoder_init() or
		// stream_encoder_update().
		if (!coder->block_encoder_is_initialized)
			return_if_error(block_encoder_init(coder, allocator));

		// Make it false so that we don't skip the initialization
		// with the next Block.
		coder->block_encoder_is_initialized = false;

		// Encode the Block Header. This shouldn't fail since we have
		// already initialized the Block encoder.
		if (lzma_block_header_encode(&coder->block_options,
				coder->buffer) != LZMA_OK)
			return LZMA_PROG_ERROR;

		coder->buffer_size = coder->block_options.header_size;
		coder->sequence = SEQ_BLOCK_HEADER;
		break;
	}

	case SEQ_BLOCK_ENCODE: {
		static const lzma_action convert[LZMA_ACTION_MAX + 1] = {
			LZMA_RUN,
			LZMA_SYNC_FLUSH,
			LZMA_FINISH,
			LZMA_FINISH,
			LZMA_FINISH,
		};

		const lzma_ret ret = coder->block_encoder.code(
				coder->block_encoder.coder, allocator,
				in, in_pos, in_size,
				out, out_pos, out_size, convert[action]);
		if (ret != LZMA_STREAM_END || action == LZMA_SYNC_FLUSH)
			return ret;

		// Add a new Index Record.
		const lzma_vli unpadded_size = lzma_block_unpadded_size(
				&coder->block_options);
		assert(unpadded_size != 0);
		return_if_error(lzma_index_append(coder->index, allocator,
				unpadded_size,
				coder->block_options.uncompressed_size));

		coder->sequence = SEQ_BLOCK_INIT;
		break;
	}

	case SEQ_INDEX_ENCODE: {
		// Call the Index encoder. It doesn't take any input, so
		// those pointers can be NULL.
		const lzma_ret ret = coder->index_encoder.code(
				coder->index_encoder.coder, allocator,
				NULL, NULL, 0,
				out, out_pos, out_size, LZMA_RUN);
		if (ret != LZMA_STREAM_END)
			return ret;

		// Encode the Stream Footer into coder->buffer.
		const lzma_stream_flags stream_flags = {
			.version = 0,
			.backward_size = lzma_index_size(coder->index),
			.check = coder->block_options.check,
		};

		if (lzma_stream_footer_encode(&stream_flags, coder->buffer)
				!= LZMA_OK)
			return LZMA_PROG_ERROR;

		coder->buffer_size = LZMA_STREAM_HEADER_SIZE;
		coder->sequence = SEQ_STREAM_FOOTER;
		break;
	}

	default:
		assert(0);
		return LZMA_PROG_ERROR;
	}

	return LZMA_OK;
}


static void
stream_encoder_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_stream_coder *coder = coder_ptr;

	lzma_next_end(&coder->block_encoder, allocator);
	lzma_next_end(&coder->index_encoder, allocator);
	lzma_index_end(coder->index, allocator);

	for (size_t i = 0; coder->filters[i].id != LZMA_VLI_UNKNOWN; ++i)
		lzma_free(coder->filters[i].options, allocator);

	lzma_free(coder, allocator);
	return;
}


static lzma_ret
stream_encoder_update(void *coder_ptr, const lzma_allocator *allocator,
		const lzma_filter *filters,
		const lzma_filter *reversed_filters)
{
	lzma_stream_coder *coder = coder_ptr;

	if (coder->sequence <= SEQ_BLOCK_INIT) {
		// There is no incomplete Block waiting to be finished,
		// thus we can change the whole filter chain. Start by
		// trying to initialize the Block encoder with the new
		// chain. This way we detect if the chain is valid.
		coder->block_encoder_is_initialized = false;
		coder->block_options.filters = (lzma_filter *)(filters);
		const lzma_ret ret = block_encoder_init(coder, allocator);
		coder->block_options.filters = coder->filters;
		if (ret != LZMA_OK)
			return ret;

		coder->block_encoder_is_initialized = true;

	} else if (coder->sequence <= SEQ_BLOCK_ENCODE) {
		// We are in the middle of a Block. Try to update only
		// the filter-specific options.
		return_if_error(coder->block_encoder.update(
				coder->block_encoder.coder, allocator,
				filters, reversed_filters));
	} else {
		// Trying to update the filter chain when we are already
		// encoding Index or Stream Footer.
		return LZMA_PROG_ERROR;
	}

	// Free the copy of the old chain and make a copy of the new chain.
	for (size_t i = 0; coder->filters[i].id != LZMA_VLI_UNKNOWN; ++i)
		lzma_free(coder->filters[i].options, allocator);

	return lzma_filters_copy(filters, coder->filters, allocator);
}


static lzma_ret
stream_encoder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter *filters, lzma_check check)
{
	lzma_next_coder_init(&stream_encoder_init, next, allocator);

	if (filters == NULL)
		return LZMA_PROG_ERROR;

	lzma_stream_coder *coder = next->coder;

	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_stream_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;
		next->code = &stream_encode;
		next->end = &stream_encoder_end;
		next->update = &stream_encoder_update;

		coder->filters[0].id = LZMA_VLI_UNKNOWN;
		coder->block_encoder = LZMA_NEXT_CODER_INIT;
		coder->index_encoder = LZMA_NEXT_CODER_INIT;
		coder->index = NULL;
	}

	// Basic initializations
	coder->sequence = SEQ_STREAM_HEADER;
	coder->block_options.version = 0;
	coder->block_options.check = check;

	// Initialize the Index
	lzma_index_end(coder->index, allocator);
	coder->index = lzma_index_init(allocator);
	if (coder->index == NULL)
		return LZMA_MEM_ERROR;

	// Encode the Stream Header
	lzma_stream_flags stream_flags = {
		.version = 0,
		.check = check,
	};
	return_if_error(lzma_stream_header_encode(
			&stream_flags, coder->buffer));

	coder->buffer_pos = 0;
	coder->buffer_size = LZMA_STREAM_HEADER_SIZE;

	// Initialize the Block encoder. This way we detect unsupported
	// filter chains when initializing the Stream encoder instead of
	// giving an error after Stream Header has already written out.
	return stream_encoder_update(coder, allocator, filters, NULL);
}


extern LZMA_API(lzma_ret)
lzma_stream_encoder(lzma_stream *strm,
		const lzma_filter *filters, lzma_check check)
{
	lzma_next_strm_init(stream_encoder_init, strm, filters, check);

	strm->internal->supported_actions[LZMA_RUN] = true;
	strm->internal->supported_actions[LZMA_SYNC_FLUSH] = true;
	strm->internal->supported_actions[LZMA_FULL_FLUSH] = true;
	strm->internal->supported_actions[LZMA_FULL_BARRIER] = true;
	strm->internal->supported_actions[LZMA_FINISH] = true;

	return LZMA_OK;
}
