///////////////////////////////////////////////////////////////////////////////
//
/// \file       outqueue.c
/// \brief      Output queue handling in multithreaded coding
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "outqueue.h"


/// This is to ease integer overflow checking: We may allocate up to
/// 2 * LZMA_THREADS_MAX buffers and we need some extra memory for other
/// data structures (that's the second /2).
#define BUF_SIZE_MAX (UINT64_MAX / LZMA_THREADS_MAX / 2 / 2)


static lzma_ret
get_options(uint64_t *bufs_alloc_size, uint32_t *bufs_count,
		uint64_t buf_size_max, uint32_t threads)
{
	if (threads > LZMA_THREADS_MAX || buf_size_max > BUF_SIZE_MAX)
		return LZMA_OPTIONS_ERROR;

	// The number of buffers is twice the number of threads.
	// This wastes RAM but keeps the threads busy when buffers
	// finish out of order.
	//
	// NOTE: If this is changed, update BUF_SIZE_MAX too.
	*bufs_count = threads * 2;
	*bufs_alloc_size = *bufs_count * buf_size_max;

	return LZMA_OK;
}


extern uint64_t
lzma_outq_memusage(uint64_t buf_size_max, uint32_t threads)
{
	uint64_t bufs_alloc_size;
	uint32_t bufs_count;

	if (get_options(&bufs_alloc_size, &bufs_count, buf_size_max, threads)
			!= LZMA_OK)
		return UINT64_MAX;

	return sizeof(lzma_outq) + bufs_count * sizeof(lzma_outbuf)
			+ bufs_alloc_size;
}


extern lzma_ret
lzma_outq_init(lzma_outq *outq, const lzma_allocator *allocator,
		uint64_t buf_size_max, uint32_t threads)
{
	uint64_t bufs_alloc_size;
	uint32_t bufs_count;

	// Set bufs_count and bufs_alloc_size.
	return_if_error(get_options(&bufs_alloc_size, &bufs_count,
			buf_size_max, threads));

	// Allocate memory if needed.
	if (outq->buf_size_max != buf_size_max
			|| outq->bufs_allocated != bufs_count) {
		lzma_outq_end(outq, allocator);

#if SIZE_MAX < UINT64_MAX
		if (bufs_alloc_size > SIZE_MAX)
			return LZMA_MEM_ERROR;
#endif

		outq->bufs = lzma_alloc(bufs_count * sizeof(lzma_outbuf),
				allocator);
		outq->bufs_mem = lzma_alloc((size_t)(bufs_alloc_size),
				allocator);

		if (outq->bufs == NULL || outq->bufs_mem == NULL) {
			lzma_outq_end(outq, allocator);
			return LZMA_MEM_ERROR;
		}
	}

	// Initialize the rest of the main structure. Initialization of
	// outq->bufs[] is done when they are actually needed.
	outq->buf_size_max = (size_t)(buf_size_max);
	outq->bufs_allocated = bufs_count;
	outq->bufs_pos = 0;
	outq->bufs_used = 0;
	outq->read_pos = 0;

	return LZMA_OK;
}


extern void
lzma_outq_end(lzma_outq *outq, const lzma_allocator *allocator)
{
	lzma_free(outq->bufs, allocator);
	outq->bufs = NULL;

	lzma_free(outq->bufs_mem, allocator);
	outq->bufs_mem = NULL;

	return;
}


extern lzma_outbuf *
lzma_outq_get_buf(lzma_outq *outq)
{
	// Caller must have checked it with lzma_outq_has_buf().
	assert(outq->bufs_used < outq->bufs_allocated);

	// Initialize the new buffer.
	lzma_outbuf *buf = &outq->bufs[outq->bufs_pos];
	buf->buf = outq->bufs_mem + outq->bufs_pos * outq->buf_size_max;
	buf->size = 0;
	buf->finished = false;

	// Update the queue state.
	if (++outq->bufs_pos == outq->bufs_allocated)
		outq->bufs_pos = 0;

	++outq->bufs_used;

	return buf;
}


extern bool
lzma_outq_is_readable(const lzma_outq *outq)
{
	uint32_t i = outq->bufs_pos - outq->bufs_used;
	if (outq->bufs_pos < outq->bufs_used)
		i += outq->bufs_allocated;

	return outq->bufs[i].finished;
}


extern lzma_ret
lzma_outq_read(lzma_outq *restrict outq, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size,
		lzma_vli *restrict unpadded_size,
		lzma_vli *restrict uncompressed_size)
{
	// There must be at least one buffer from which to read.
	if (outq->bufs_used == 0)
		return LZMA_OK;

	// Get the buffer.
	uint32_t i = outq->bufs_pos - outq->bufs_used;
	if (outq->bufs_pos < outq->bufs_used)
		i += outq->bufs_allocated;

	lzma_outbuf *buf = &outq->bufs[i];

	// If it isn't finished yet, we cannot read from it.
	if (!buf->finished)
		return LZMA_OK;

	// Copy from the buffer to output.
	lzma_bufcpy(buf->buf, &outq->read_pos, buf->size,
			out, out_pos, out_size);

	// Return if we didn't get all the data from the buffer.
	if (outq->read_pos < buf->size)
		return LZMA_OK;

	// The buffer was finished. Tell the caller its size information.
	*unpadded_size = buf->unpadded_size;
	*uncompressed_size = buf->uncompressed_size;

	// Free this buffer for further use.
	--outq->bufs_used;
	outq->read_pos = 0;

	return LZMA_STREAM_END;
}
