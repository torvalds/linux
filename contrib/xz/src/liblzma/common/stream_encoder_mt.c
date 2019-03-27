///////////////////////////////////////////////////////////////////////////////
//
/// \file       stream_encoder_mt.c
/// \brief      Multithreaded .xz Stream encoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "filter_encoder.h"
#include "easy_preset.h"
#include "block_encoder.h"
#include "block_buffer_encoder.h"
#include "index_encoder.h"
#include "outqueue.h"


/// Maximum supported block size. This makes it simpler to prevent integer
/// overflows if we are given unusually large block size.
#define BLOCK_SIZE_MAX (UINT64_MAX / LZMA_THREADS_MAX)


typedef enum {
	/// Waiting for work.
	THR_IDLE,

	/// Encoding is in progress.
	THR_RUN,

	/// Encoding is in progress but no more input data will
	/// be read.
	THR_FINISH,

	/// The main thread wants the thread to stop whatever it was doing
	/// but not exit.
	THR_STOP,

	/// The main thread wants the thread to exit. We could use
	/// cancellation but since there's stopped anyway, this is lazier.
	THR_EXIT,

} worker_state;

typedef struct lzma_stream_coder_s lzma_stream_coder;

typedef struct worker_thread_s worker_thread;
struct worker_thread_s {
	worker_state state;

	/// Input buffer of coder->block_size bytes. The main thread will
	/// put new input into this and update in_size accordingly. Once
	/// no more input is coming, state will be set to THR_FINISH.
	uint8_t *in;

	/// Amount of data available in the input buffer. This is modified
	/// only by the main thread.
	size_t in_size;

	/// Output buffer for this thread. This is set by the main
	/// thread every time a new Block is started with this thread
	/// structure.
	lzma_outbuf *outbuf;

	/// Pointer to the main structure is needed when putting this
	/// thread back to the stack of free threads.
	lzma_stream_coder *coder;

	/// The allocator is set by the main thread. Since a copy of the
	/// pointer is kept here, the application must not change the
	/// allocator before calling lzma_end().
	const lzma_allocator *allocator;

	/// Amount of uncompressed data that has already been compressed.
	uint64_t progress_in;

	/// Amount of compressed data that is ready.
	uint64_t progress_out;

	/// Block encoder
	lzma_next_coder block_encoder;

	/// Compression options for this Block
	lzma_block block_options;

	/// Next structure in the stack of free worker threads.
	worker_thread *next;

	mythread_mutex mutex;
	mythread_cond cond;

	/// The ID of this thread is used to join the thread
	/// when it's not needed anymore.
	mythread thread_id;
};


struct lzma_stream_coder_s {
	enum {
		SEQ_STREAM_HEADER,
		SEQ_BLOCK,
		SEQ_INDEX,
		SEQ_STREAM_FOOTER,
	} sequence;

	/// Start a new Block every block_size bytes of input unless
	/// LZMA_FULL_FLUSH or LZMA_FULL_BARRIER is used earlier.
	size_t block_size;

	/// The filter chain currently in use
	lzma_filter filters[LZMA_FILTERS_MAX + 1];


	/// Index to hold sizes of the Blocks
	lzma_index *index;

	/// Index encoder
	lzma_next_coder index_encoder;


	/// Stream Flags for encoding the Stream Header and Stream Footer.
	lzma_stream_flags stream_flags;

	/// Buffer to hold Stream Header and Stream Footer.
	uint8_t header[LZMA_STREAM_HEADER_SIZE];

	/// Read position in header[]
	size_t header_pos;


	/// Output buffer queue for compressed data
	lzma_outq outq;


	/// Maximum wait time if cannot use all the input and cannot
	/// fill the output buffer. This is in milliseconds.
	uint32_t timeout;


	/// Error code from a worker thread
	lzma_ret thread_error;

	/// Array of allocated thread-specific structures
	worker_thread *threads;

	/// Number of structures in "threads" above. This is also the
	/// number of threads that will be created at maximum.
	uint32_t threads_max;

	/// Number of thread structures that have been initialized, and
	/// thus the number of worker threads actually created so far.
	uint32_t threads_initialized;

	/// Stack of free threads. When a thread finishes, it puts itself
	/// back into this stack. This starts as empty because threads
	/// are created only when actually needed.
	worker_thread *threads_free;

	/// The most recent worker thread to which the main thread writes
	/// the new input from the application.
	worker_thread *thr;


	/// Amount of uncompressed data in Blocks that have already
	/// been finished.
	uint64_t progress_in;

	/// Amount of compressed data in Stream Header + Blocks that
	/// have already been finished.
	uint64_t progress_out;


	mythread_mutex mutex;
	mythread_cond cond;
};


/// Tell the main thread that something has gone wrong.
static void
worker_error(worker_thread *thr, lzma_ret ret)
{
	assert(ret != LZMA_OK);
	assert(ret != LZMA_STREAM_END);

	mythread_sync(thr->coder->mutex) {
		if (thr->coder->thread_error == LZMA_OK)
			thr->coder->thread_error = ret;

		mythread_cond_signal(&thr->coder->cond);
	}

	return;
}


static worker_state
worker_encode(worker_thread *thr, worker_state state)
{
	assert(thr->progress_in == 0);
	assert(thr->progress_out == 0);

	// Set the Block options.
	thr->block_options = (lzma_block){
		.version = 0,
		.check = thr->coder->stream_flags.check,
		.compressed_size = thr->coder->outq.buf_size_max,
		.uncompressed_size = thr->coder->block_size,

		// TODO: To allow changing the filter chain, the filters
		// array must be copied to each worker_thread.
		.filters = thr->coder->filters,
	};

	// Calculate maximum size of the Block Header. This amount is
	// reserved in the beginning of the buffer so that Block Header
	// along with Compressed Size and Uncompressed Size can be
	// written there.
	lzma_ret ret = lzma_block_header_size(&thr->block_options);
	if (ret != LZMA_OK) {
		worker_error(thr, ret);
		return THR_STOP;
	}

	// Initialize the Block encoder.
	ret = lzma_block_encoder_init(&thr->block_encoder,
			thr->allocator, &thr->block_options);
	if (ret != LZMA_OK) {
		worker_error(thr, ret);
		return THR_STOP;
	}

	size_t in_pos = 0;
	size_t in_size = 0;

	thr->outbuf->size = thr->block_options.header_size;
	const size_t out_size = thr->coder->outq.buf_size_max;

	do {
		mythread_sync(thr->mutex) {
			// Store in_pos and out_pos into *thr so that
			// an application may read them via
			// lzma_get_progress() to get progress information.
			//
			// NOTE: These aren't updated when the encoding
			// finishes. Instead, the final values are taken
			// later from thr->outbuf.
			thr->progress_in = in_pos;
			thr->progress_out = thr->outbuf->size;

			while (in_size == thr->in_size
					&& thr->state == THR_RUN)
				mythread_cond_wait(&thr->cond, &thr->mutex);

			state = thr->state;
			in_size = thr->in_size;
		}

		// Return if we were asked to stop or exit.
		if (state >= THR_STOP)
			return state;

		lzma_action action = state == THR_FINISH
				? LZMA_FINISH : LZMA_RUN;

		// Limit the amount of input given to the Block encoder
		// at once. This way this thread can react fairly quickly
		// if the main thread wants us to stop or exit.
		static const size_t in_chunk_max = 16384;
		size_t in_limit = in_size;
		if (in_size - in_pos > in_chunk_max) {
			in_limit = in_pos + in_chunk_max;
			action = LZMA_RUN;
		}

		ret = thr->block_encoder.code(
				thr->block_encoder.coder, thr->allocator,
				thr->in, &in_pos, in_limit, thr->outbuf->buf,
				&thr->outbuf->size, out_size, action);
	} while (ret == LZMA_OK && thr->outbuf->size < out_size);

	switch (ret) {
	case LZMA_STREAM_END:
		assert(state == THR_FINISH);

		// Encode the Block Header. By doing it after
		// the compression, we can store the Compressed Size
		// and Uncompressed Size fields.
		ret = lzma_block_header_encode(&thr->block_options,
				thr->outbuf->buf);
		if (ret != LZMA_OK) {
			worker_error(thr, ret);
			return THR_STOP;
		}

		break;

	case LZMA_OK:
		// The data was incompressible. Encode it using uncompressed
		// LZMA2 chunks.
		//
		// First wait that we have gotten all the input.
		mythread_sync(thr->mutex) {
			while (thr->state == THR_RUN)
				mythread_cond_wait(&thr->cond, &thr->mutex);

			state = thr->state;
			in_size = thr->in_size;
		}

		if (state >= THR_STOP)
			return state;

		// Do the encoding. This takes care of the Block Header too.
		thr->outbuf->size = 0;
		ret = lzma_block_uncomp_encode(&thr->block_options,
				thr->in, in_size, thr->outbuf->buf,
				&thr->outbuf->size, out_size);

		// It shouldn't fail.
		if (ret != LZMA_OK) {
			worker_error(thr, LZMA_PROG_ERROR);
			return THR_STOP;
		}

		break;

	default:
		worker_error(thr, ret);
		return THR_STOP;
	}

	// Set the size information that will be read by the main thread
	// to write the Index field.
	thr->outbuf->unpadded_size
			= lzma_block_unpadded_size(&thr->block_options);
	assert(thr->outbuf->unpadded_size != 0);
	thr->outbuf->uncompressed_size = thr->block_options.uncompressed_size;

	return THR_FINISH;
}


static MYTHREAD_RET_TYPE
worker_start(void *thr_ptr)
{
	worker_thread *thr = thr_ptr;
	worker_state state = THR_IDLE; // Init to silence a warning

	while (true) {
		// Wait for work.
		mythread_sync(thr->mutex) {
			while (true) {
				// The thread is already idle so if we are
				// requested to stop, just set the state.
				if (thr->state == THR_STOP) {
					thr->state = THR_IDLE;
					mythread_cond_signal(&thr->cond);
				}

				state = thr->state;
				if (state != THR_IDLE)
					break;

				mythread_cond_wait(&thr->cond, &thr->mutex);
			}
		}

		assert(state != THR_IDLE);
		assert(state != THR_STOP);

		if (state <= THR_FINISH)
			state = worker_encode(thr, state);

		if (state == THR_EXIT)
			break;

		// Mark the thread as idle unless the main thread has
		// told us to exit. Signal is needed for the case
		// where the main thread is waiting for the threads to stop.
		mythread_sync(thr->mutex) {
			if (thr->state != THR_EXIT) {
				thr->state = THR_IDLE;
				mythread_cond_signal(&thr->cond);
			}
		}

		mythread_sync(thr->coder->mutex) {
			// Mark the output buffer as finished if
			// no errors occurred.
			thr->outbuf->finished = state == THR_FINISH;

			// Update the main progress info.
			thr->coder->progress_in
					+= thr->outbuf->uncompressed_size;
			thr->coder->progress_out += thr->outbuf->size;
			thr->progress_in = 0;
			thr->progress_out = 0;

			// Return this thread to the stack of free threads.
			thr->next = thr->coder->threads_free;
			thr->coder->threads_free = thr;

			mythread_cond_signal(&thr->coder->cond);
		}
	}

	// Exiting, free the resources.
	mythread_mutex_destroy(&thr->mutex);
	mythread_cond_destroy(&thr->cond);

	lzma_next_end(&thr->block_encoder, thr->allocator);
	lzma_free(thr->in, thr->allocator);
	return MYTHREAD_RET_VALUE;
}


/// Make the threads stop but not exit. Optionally wait for them to stop.
static void
threads_stop(lzma_stream_coder *coder, bool wait_for_threads)
{
	// Tell the threads to stop.
	for (uint32_t i = 0; i < coder->threads_initialized; ++i) {
		mythread_sync(coder->threads[i].mutex) {
			coder->threads[i].state = THR_STOP;
			mythread_cond_signal(&coder->threads[i].cond);
		}
	}

	if (!wait_for_threads)
		return;

	// Wait for the threads to settle in the idle state.
	for (uint32_t i = 0; i < coder->threads_initialized; ++i) {
		mythread_sync(coder->threads[i].mutex) {
			while (coder->threads[i].state != THR_IDLE)
				mythread_cond_wait(&coder->threads[i].cond,
						&coder->threads[i].mutex);
		}
	}

	return;
}


/// Stop the threads and free the resources associated with them.
/// Wait until the threads have exited.
static void
threads_end(lzma_stream_coder *coder, const lzma_allocator *allocator)
{
	for (uint32_t i = 0; i < coder->threads_initialized; ++i) {
		mythread_sync(coder->threads[i].mutex) {
			coder->threads[i].state = THR_EXIT;
			mythread_cond_signal(&coder->threads[i].cond);
		}
	}

	for (uint32_t i = 0; i < coder->threads_initialized; ++i) {
		int ret = mythread_join(coder->threads[i].thread_id);
		assert(ret == 0);
		(void)ret;
	}

	lzma_free(coder->threads, allocator);
	return;
}


/// Initialize a new worker_thread structure and create a new thread.
static lzma_ret
initialize_new_thread(lzma_stream_coder *coder,
		const lzma_allocator *allocator)
{
	worker_thread *thr = &coder->threads[coder->threads_initialized];

	thr->in = lzma_alloc(coder->block_size, allocator);
	if (thr->in == NULL)
		return LZMA_MEM_ERROR;

	if (mythread_mutex_init(&thr->mutex))
		goto error_mutex;

	if (mythread_cond_init(&thr->cond))
		goto error_cond;

	thr->state = THR_IDLE;
	thr->allocator = allocator;
	thr->coder = coder;
	thr->progress_in = 0;
	thr->progress_out = 0;
	thr->block_encoder = LZMA_NEXT_CODER_INIT;

	if (mythread_create(&thr->thread_id, &worker_start, thr))
		goto error_thread;

	++coder->threads_initialized;
	coder->thr = thr;

	return LZMA_OK;

error_thread:
	mythread_cond_destroy(&thr->cond);

error_cond:
	mythread_mutex_destroy(&thr->mutex);

error_mutex:
	lzma_free(thr->in, allocator);
	return LZMA_MEM_ERROR;
}


static lzma_ret
get_thread(lzma_stream_coder *coder, const lzma_allocator *allocator)
{
	// If there are no free output subqueues, there is no
	// point to try getting a thread.
	if (!lzma_outq_has_buf(&coder->outq))
		return LZMA_OK;

	// If there is a free structure on the stack, use it.
	mythread_sync(coder->mutex) {
		if (coder->threads_free != NULL) {
			coder->thr = coder->threads_free;
			coder->threads_free = coder->threads_free->next;
		}
	}

	if (coder->thr == NULL) {
		// If there are no uninitialized structures left, return.
		if (coder->threads_initialized == coder->threads_max)
			return LZMA_OK;

		// Initialize a new thread.
		return_if_error(initialize_new_thread(coder, allocator));
	}

	// Reset the parts of the thread state that have to be done
	// in the main thread.
	mythread_sync(coder->thr->mutex) {
		coder->thr->state = THR_RUN;
		coder->thr->in_size = 0;
		coder->thr->outbuf = lzma_outq_get_buf(&coder->outq);
		mythread_cond_signal(&coder->thr->cond);
	}

	return LZMA_OK;
}


static lzma_ret
stream_encode_in(lzma_stream_coder *coder, const lzma_allocator *allocator,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, lzma_action action)
{
	while (*in_pos < in_size
			|| (coder->thr != NULL && action != LZMA_RUN)) {
		if (coder->thr == NULL) {
			// Get a new thread.
			const lzma_ret ret = get_thread(coder, allocator);
			if (coder->thr == NULL)
				return ret;
		}

		// Copy the input data to thread's buffer.
		size_t thr_in_size = coder->thr->in_size;
		lzma_bufcpy(in, in_pos, in_size, coder->thr->in,
				&thr_in_size, coder->block_size);

		// Tell the Block encoder to finish if
		//  - it has got block_size bytes of input; or
		//  - all input was used and LZMA_FINISH, LZMA_FULL_FLUSH,
		//    or LZMA_FULL_BARRIER was used.
		//
		// TODO: LZMA_SYNC_FLUSH and LZMA_SYNC_BARRIER.
		const bool finish = thr_in_size == coder->block_size
				|| (*in_pos == in_size && action != LZMA_RUN);

		bool block_error = false;

		mythread_sync(coder->thr->mutex) {
			if (coder->thr->state == THR_IDLE) {
				// Something has gone wrong with the Block
				// encoder. It has set coder->thread_error
				// which we will read a few lines later.
				block_error = true;
			} else {
				// Tell the Block encoder its new amount
				// of input and update the state if needed.
				coder->thr->in_size = thr_in_size;

				if (finish)
					coder->thr->state = THR_FINISH;

				mythread_cond_signal(&coder->thr->cond);
			}
		}

		if (block_error) {
			lzma_ret ret;

			mythread_sync(coder->mutex) {
				ret = coder->thread_error;
			}

			return ret;
		}

		if (finish)
			coder->thr = NULL;
	}

	return LZMA_OK;
}


/// Wait until more input can be consumed, more output can be read, or
/// an optional timeout is reached.
static bool
wait_for_work(lzma_stream_coder *coder, mythread_condtime *wait_abs,
		bool *has_blocked, bool has_input)
{
	if (coder->timeout != 0 && !*has_blocked) {
		// Every time when stream_encode_mt() is called via
		// lzma_code(), *has_blocked starts as false. We set it
		// to true here and calculate the absolute time when
		// we must return if there's nothing to do.
		//
		// The idea of *has_blocked is to avoid unneeded calls
		// to mythread_condtime_set(), which may do a syscall
		// depending on the operating system.
		*has_blocked = true;
		mythread_condtime_set(wait_abs, &coder->cond, coder->timeout);
	}

	bool timed_out = false;

	mythread_sync(coder->mutex) {
		// There are four things that we wait. If one of them
		// becomes possible, we return.
		//  - If there is input left, we need to get a free
		//    worker thread and an output buffer for it.
		//  - Data ready to be read from the output queue.
		//  - A worker thread indicates an error.
		//  - Time out occurs.
		while ((!has_input || coder->threads_free == NULL
					|| !lzma_outq_has_buf(&coder->outq))
				&& !lzma_outq_is_readable(&coder->outq)
				&& coder->thread_error == LZMA_OK
				&& !timed_out) {
			if (coder->timeout != 0)
				timed_out = mythread_cond_timedwait(
						&coder->cond, &coder->mutex,
						wait_abs) != 0;
			else
				mythread_cond_wait(&coder->cond,
						&coder->mutex);
		}
	}

	return timed_out;
}


static lzma_ret
stream_encode_mt(void *coder_ptr, const lzma_allocator *allocator,
		const uint8_t *restrict in, size_t *restrict in_pos,
		size_t in_size, uint8_t *restrict out,
		size_t *restrict out_pos, size_t out_size, lzma_action action)
{
	lzma_stream_coder *coder = coder_ptr;

	switch (coder->sequence) {
	case SEQ_STREAM_HEADER:
		lzma_bufcpy(coder->header, &coder->header_pos,
				sizeof(coder->header),
				out, out_pos, out_size);
		if (coder->header_pos < sizeof(coder->header))
			return LZMA_OK;

		coder->header_pos = 0;
		coder->sequence = SEQ_BLOCK;

	// Fall through

	case SEQ_BLOCK: {
		// Initialized to silence warnings.
		lzma_vli unpadded_size = 0;
		lzma_vli uncompressed_size = 0;
		lzma_ret ret = LZMA_OK;

		// These are for wait_for_work().
		bool has_blocked = false;
		mythread_condtime wait_abs;

		while (true) {
			mythread_sync(coder->mutex) {
				// Check for Block encoder errors.
				ret = coder->thread_error;
				if (ret != LZMA_OK) {
					assert(ret != LZMA_STREAM_END);
					break;
				}

				// Try to read compressed data to out[].
				ret = lzma_outq_read(&coder->outq,
						out, out_pos, out_size,
						&unpadded_size,
						&uncompressed_size);
			}

			if (ret == LZMA_STREAM_END) {
				// End of Block. Add it to the Index.
				ret = lzma_index_append(coder->index,
						allocator, unpadded_size,
						uncompressed_size);

				// If we didn't fill the output buffer yet,
				// try to read more data. Maybe the next
				// outbuf has been finished already too.
				if (*out_pos < out_size)
					continue;
			}

			if (ret != LZMA_OK) {
				// coder->thread_error was set or
				// lzma_index_append() failed.
				threads_stop(coder, false);
				return ret;
			}

			// Try to give uncompressed data to a worker thread.
			ret = stream_encode_in(coder, allocator,
					in, in_pos, in_size, action);
			if (ret != LZMA_OK) {
				threads_stop(coder, false);
				return ret;
			}

			// See if we should wait or return.
			//
			// TODO: LZMA_SYNC_FLUSH and LZMA_SYNC_BARRIER.
			if (*in_pos == in_size) {
				// LZMA_RUN: More data is probably coming
				// so return to let the caller fill the
				// input buffer.
				if (action == LZMA_RUN)
					return LZMA_OK;

				// LZMA_FULL_BARRIER: The same as with
				// LZMA_RUN but tell the caller that the
				// barrier was completed.
				if (action == LZMA_FULL_BARRIER)
					return LZMA_STREAM_END;

				// Finishing or flushing isn't completed until
				// all input data has been encoded and copied
				// to the output buffer.
				if (lzma_outq_is_empty(&coder->outq)) {
					// LZMA_FINISH: Continue to encode
					// the Index field.
					if (action == LZMA_FINISH)
						break;

					// LZMA_FULL_FLUSH: Return to tell
					// the caller that flushing was
					// completed.
					if (action == LZMA_FULL_FLUSH)
						return LZMA_STREAM_END;
				}
			}

			// Return if there is no output space left.
			// This check must be done after testing the input
			// buffer, because we might want to use a different
			// return code.
			if (*out_pos == out_size)
				return LZMA_OK;

			// Neither in nor out has been used completely.
			// Wait until there's something we can do.
			if (wait_for_work(coder, &wait_abs, &has_blocked,
					*in_pos < in_size))
				return LZMA_TIMED_OUT;
		}

		// All Blocks have been encoded and the threads have stopped.
		// Prepare to encode the Index field.
		return_if_error(lzma_index_encoder_init(
				&coder->index_encoder, allocator,
				coder->index));
		coder->sequence = SEQ_INDEX;

		// Update the progress info to take the Index and
		// Stream Footer into account. Those are very fast to encode
		// so in terms of progress information they can be thought
		// to be ready to be copied out.
		coder->progress_out += lzma_index_size(coder->index)
				+ LZMA_STREAM_HEADER_SIZE;
	}

	// Fall through

	case SEQ_INDEX: {
		// Call the Index encoder. It doesn't take any input, so
		// those pointers can be NULL.
		const lzma_ret ret = coder->index_encoder.code(
				coder->index_encoder.coder, allocator,
				NULL, NULL, 0,
				out, out_pos, out_size, LZMA_RUN);
		if (ret != LZMA_STREAM_END)
			return ret;

		// Encode the Stream Footer into coder->buffer.
		coder->stream_flags.backward_size
				= lzma_index_size(coder->index);
		if (lzma_stream_footer_encode(&coder->stream_flags,
				coder->header) != LZMA_OK)
			return LZMA_PROG_ERROR;

		coder->sequence = SEQ_STREAM_FOOTER;
	}

	// Fall through

	case SEQ_STREAM_FOOTER:
		lzma_bufcpy(coder->header, &coder->header_pos,
				sizeof(coder->header),
				out, out_pos, out_size);
		return coder->header_pos < sizeof(coder->header)
				? LZMA_OK : LZMA_STREAM_END;
	}

	assert(0);
	return LZMA_PROG_ERROR;
}


static void
stream_encoder_mt_end(void *coder_ptr, const lzma_allocator *allocator)
{
	lzma_stream_coder *coder = coder_ptr;

	// Threads must be killed before the output queue can be freed.
	threads_end(coder, allocator);
	lzma_outq_end(&coder->outq, allocator);

	for (size_t i = 0; coder->filters[i].id != LZMA_VLI_UNKNOWN; ++i)
		lzma_free(coder->filters[i].options, allocator);

	lzma_next_end(&coder->index_encoder, allocator);
	lzma_index_end(coder->index, allocator);

	mythread_cond_destroy(&coder->cond);
	mythread_mutex_destroy(&coder->mutex);

	lzma_free(coder, allocator);
	return;
}


/// Options handling for lzma_stream_encoder_mt_init() and
/// lzma_stream_encoder_mt_memusage()
static lzma_ret
get_options(const lzma_mt *options, lzma_options_easy *opt_easy,
		const lzma_filter **filters, uint64_t *block_size,
		uint64_t *outbuf_size_max)
{
	// Validate some of the options.
	if (options == NULL)
		return LZMA_PROG_ERROR;

	if (options->flags != 0 || options->threads == 0
			|| options->threads > LZMA_THREADS_MAX)
		return LZMA_OPTIONS_ERROR;

	if (options->filters != NULL) {
		// Filter chain was given, use it as is.
		*filters = options->filters;
	} else {
		// Use a preset.
		if (lzma_easy_preset(opt_easy, options->preset))
			return LZMA_OPTIONS_ERROR;

		*filters = opt_easy->filters;
	}

	// Block size
	if (options->block_size > 0) {
		if (options->block_size > BLOCK_SIZE_MAX)
			return LZMA_OPTIONS_ERROR;

		*block_size = options->block_size;
	} else {
		// Determine the Block size from the filter chain.
		*block_size = lzma_mt_block_size(*filters);
		if (*block_size == 0)
			return LZMA_OPTIONS_ERROR;

		assert(*block_size <= BLOCK_SIZE_MAX);
	}

	// Calculate the maximum amount output that a single output buffer
	// may need to hold. This is the same as the maximum total size of
	// a Block.
	*outbuf_size_max = lzma_block_buffer_bound64(*block_size);
	if (*outbuf_size_max == 0)
		return LZMA_MEM_ERROR;

	return LZMA_OK;
}


static void
get_progress(void *coder_ptr, uint64_t *progress_in, uint64_t *progress_out)
{
	lzma_stream_coder *coder = coder_ptr;

	// Lock coder->mutex to prevent finishing threads from moving their
	// progress info from the worker_thread structure to lzma_stream_coder.
	mythread_sync(coder->mutex) {
		*progress_in = coder->progress_in;
		*progress_out = coder->progress_out;

		for (size_t i = 0; i < coder->threads_initialized; ++i) {
			mythread_sync(coder->threads[i].mutex) {
				*progress_in += coder->threads[i].progress_in;
				*progress_out += coder->threads[i]
						.progress_out;
			}
		}
	}

	return;
}


static lzma_ret
stream_encoder_mt_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_mt *options)
{
	lzma_next_coder_init(&stream_encoder_mt_init, next, allocator);

	// Get the filter chain.
	lzma_options_easy easy;
	const lzma_filter *filters;
	uint64_t block_size;
	uint64_t outbuf_size_max;
	return_if_error(get_options(options, &easy, &filters,
			&block_size, &outbuf_size_max));

#if SIZE_MAX < UINT64_MAX
	if (block_size > SIZE_MAX)
		return LZMA_MEM_ERROR;
#endif

	// Validate the filter chain so that we can give an error in this
	// function instead of delaying it to the first call to lzma_code().
	// The memory usage calculation verifies the filter chain as
	// a side effect so we take advatange of that.
	if (lzma_raw_encoder_memusage(filters) == UINT64_MAX)
		return LZMA_OPTIONS_ERROR;

	// Validate the Check ID.
	if ((unsigned int)(options->check) > LZMA_CHECK_ID_MAX)
		return LZMA_PROG_ERROR;

	if (!lzma_check_is_supported(options->check))
		return LZMA_UNSUPPORTED_CHECK;

	// Allocate and initialize the base structure if needed.
	lzma_stream_coder *coder = next->coder;
	if (coder == NULL) {
		coder = lzma_alloc(sizeof(lzma_stream_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;

		// For the mutex and condition variable initializations
		// the error handling has to be done here because
		// stream_encoder_mt_end() doesn't know if they have
		// already been initialized or not.
		if (mythread_mutex_init(&coder->mutex)) {
			lzma_free(coder, allocator);
			next->coder = NULL;
			return LZMA_MEM_ERROR;
		}

		if (mythread_cond_init(&coder->cond)) {
			mythread_mutex_destroy(&coder->mutex);
			lzma_free(coder, allocator);
			next->coder = NULL;
			return LZMA_MEM_ERROR;
		}

		next->code = &stream_encode_mt;
		next->end = &stream_encoder_mt_end;
		next->get_progress = &get_progress;
// 		next->update = &stream_encoder_mt_update;

		coder->filters[0].id = LZMA_VLI_UNKNOWN;
		coder->index_encoder = LZMA_NEXT_CODER_INIT;
		coder->index = NULL;
		memzero(&coder->outq, sizeof(coder->outq));
		coder->threads = NULL;
		coder->threads_max = 0;
		coder->threads_initialized = 0;
	}

	// Basic initializations
	coder->sequence = SEQ_STREAM_HEADER;
	coder->block_size = (size_t)(block_size);
	coder->thread_error = LZMA_OK;
	coder->thr = NULL;

	// Allocate the thread-specific base structures.
	assert(options->threads > 0);
	if (coder->threads_max != options->threads) {
		threads_end(coder, allocator);

		coder->threads = NULL;
		coder->threads_max = 0;

		coder->threads_initialized = 0;
		coder->threads_free = NULL;

		coder->threads = lzma_alloc(
				options->threads * sizeof(worker_thread),
				allocator);
		if (coder->threads == NULL)
			return LZMA_MEM_ERROR;

		coder->threads_max = options->threads;
	} else {
		// Reuse the old structures and threads. Tell the running
		// threads to stop and wait until they have stopped.
		threads_stop(coder, true);
	}

	// Output queue
	return_if_error(lzma_outq_init(&coder->outq, allocator,
			outbuf_size_max, options->threads));

	// Timeout
	coder->timeout = options->timeout;

	// Free the old filter chain and copy the new one.
	for (size_t i = 0; coder->filters[i].id != LZMA_VLI_UNKNOWN; ++i)
		lzma_free(coder->filters[i].options, allocator);

	return_if_error(lzma_filters_copy(
			filters, coder->filters, allocator));

	// Index
	lzma_index_end(coder->index, allocator);
	coder->index = lzma_index_init(allocator);
	if (coder->index == NULL)
		return LZMA_MEM_ERROR;

	// Stream Header
	coder->stream_flags.version = 0;
	coder->stream_flags.check = options->check;
	return_if_error(lzma_stream_header_encode(
			&coder->stream_flags, coder->header));

	coder->header_pos = 0;

	// Progress info
	coder->progress_in = 0;
	coder->progress_out = LZMA_STREAM_HEADER_SIZE;

	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_stream_encoder_mt(lzma_stream *strm, const lzma_mt *options)
{
	lzma_next_strm_init(stream_encoder_mt_init, strm, options);

	strm->internal->supported_actions[LZMA_RUN] = true;
// 	strm->internal->supported_actions[LZMA_SYNC_FLUSH] = true;
	strm->internal->supported_actions[LZMA_FULL_FLUSH] = true;
	strm->internal->supported_actions[LZMA_FULL_BARRIER] = true;
	strm->internal->supported_actions[LZMA_FINISH] = true;

	return LZMA_OK;
}


// This function name is a monster but it's consistent with the older
// monster names. :-( 31 chars is the max that C99 requires so in that
// sense it's not too long. ;-)
extern LZMA_API(uint64_t)
lzma_stream_encoder_mt_memusage(const lzma_mt *options)
{
	lzma_options_easy easy;
	const lzma_filter *filters;
	uint64_t block_size;
	uint64_t outbuf_size_max;

	if (get_options(options, &easy, &filters, &block_size,
			&outbuf_size_max) != LZMA_OK)
		return UINT64_MAX;

	// Memory usage of the input buffers
	const uint64_t inbuf_memusage = options->threads * block_size;

	// Memory usage of the filter encoders
	uint64_t filters_memusage = lzma_raw_encoder_memusage(filters);
	if (filters_memusage == UINT64_MAX)
		return UINT64_MAX;

	filters_memusage *= options->threads;

	// Memory usage of the output queue
	const uint64_t outq_memusage = lzma_outq_memusage(
			outbuf_size_max, options->threads);
	if (outq_memusage == UINT64_MAX)
		return UINT64_MAX;

	// Sum them with overflow checking.
	uint64_t total_memusage = LZMA_MEMUSAGE_BASE
			+ sizeof(lzma_stream_coder)
			+ options->threads * sizeof(worker_thread);

	if (UINT64_MAX - total_memusage < inbuf_memusage)
		return UINT64_MAX;

	total_memusage += inbuf_memusage;

	if (UINT64_MAX - total_memusage < filters_memusage)
		return UINT64_MAX;

	total_memusage += filters_memusage;

	if (UINT64_MAX - total_memusage < outq_memusage)
		return UINT64_MAX;

	return total_memusage + outq_memusage;
}
