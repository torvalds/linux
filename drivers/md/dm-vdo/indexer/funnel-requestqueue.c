// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "funnel-requestqueue.h"

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/wait.h>

#include "funnel-queue.h"
#include "logger.h"
#include "memory-alloc.h"
#include "thread-utils.h"

/*
 * This queue will attempt to handle requests in reasonably sized batches instead of reacting
 * immediately to each new request. The wait time between batches is dynamically adjusted up or
 * down to try to balance responsiveness against wasted thread run time.
 *
 * If the wait time becomes long enough, the queue will become dormant and must be explicitly
 * awoken when a new request is enqueued. The enqueue operation updates "newest" in the funnel
 * queue via xchg (which is a memory barrier), and later checks "dormant" to decide whether to do a
 * wakeup of the worker thread.
 *
 * When deciding to go to sleep, the worker thread sets "dormant" and then examines "newest" to
 * decide if the funnel queue is idle. In dormant mode, the last examination of "newest" before
 * going to sleep is done inside the wait_event_interruptible() macro, after a point where one or
 * more memory barriers have been issued. (Preparing to sleep uses spin locks.) Even if the funnel
 * queue's "next" field update isn't visible yet to make the entry accessible, its existence will
 * kick the worker thread out of dormant mode and back into timer-based mode.
 *
 * Unbatched requests are used to communicate between different zone threads and will also cause
 * the queue to awaken immediately.
 */

enum {
	NANOSECOND = 1,
	MICROSECOND = 1000 * NANOSECOND,
	MILLISECOND = 1000 * MICROSECOND,
	DEFAULT_WAIT_TIME = 20 * MICROSECOND,
	MINIMUM_WAIT_TIME = DEFAULT_WAIT_TIME / 2,
	MAXIMUM_WAIT_TIME = MILLISECOND,
	MINIMUM_BATCH = 32,
	MAXIMUM_BATCH = 64,
};

struct uds_request_queue {
	/* Wait queue for synchronizing producers and consumer */
	struct wait_queue_head wait_head;
	/* Function to process a request */
	uds_request_queue_processor_fn processor;
	/* Queue of new incoming requests */
	struct funnel_queue *main_queue;
	/* Queue of old requests to retry */
	struct funnel_queue *retry_queue;
	/* The thread id of the worker thread */
	struct thread *thread;
	/* True if the worker was started */
	bool started;
	/* When true, requests can be enqueued */
	bool running;
	/* A flag set when the worker is waiting without a timeout */
	atomic_t dormant;
};

static inline struct uds_request *poll_queues(struct uds_request_queue *queue)
{
	struct funnel_queue_entry *entry;

	entry = vdo_funnel_queue_poll(queue->retry_queue);
	if (entry != NULL)
		return container_of(entry, struct uds_request, queue_link);

	entry = vdo_funnel_queue_poll(queue->main_queue);
	if (entry != NULL)
		return container_of(entry, struct uds_request, queue_link);

	return NULL;
}

static inline bool are_queues_idle(struct uds_request_queue *queue)
{
	return vdo_is_funnel_queue_idle(queue->retry_queue) &&
	       vdo_is_funnel_queue_idle(queue->main_queue);
}

/*
 * Determine if there is a next request to process, and return it if there is. Also return flags
 * indicating whether the worker thread can sleep (for the use of wait_event() macros) and whether
 * the thread did sleep before returning a new request.
 */
static inline bool dequeue_request(struct uds_request_queue *queue,
				   struct uds_request **request_ptr, bool *waited_ptr)
{
	struct uds_request *request = poll_queues(queue);

	if (request != NULL) {
		*request_ptr = request;
		return true;
	}

	if (!READ_ONCE(queue->running)) {
		/* Wake the worker thread so it can exit. */
		*request_ptr = NULL;
		return true;
	}

	*request_ptr = NULL;
	*waited_ptr = true;
	return false;
}

static void wait_for_request(struct uds_request_queue *queue, bool dormant,
			     unsigned long timeout, struct uds_request **request,
			     bool *waited)
{
	if (dormant) {
		wait_event_interruptible(queue->wait_head,
					 (dequeue_request(queue, request, waited) ||
					  !are_queues_idle(queue)));
		return;
	}

	wait_event_interruptible_hrtimeout(queue->wait_head,
					   dequeue_request(queue, request, waited),
					   ns_to_ktime(timeout));
}

static void request_queue_worker(void *arg)
{
	struct uds_request_queue *queue = arg;
	struct uds_request *request = NULL;
	unsigned long time_batch = DEFAULT_WAIT_TIME;
	bool dormant = atomic_read(&queue->dormant);
	bool waited = false;
	long current_batch = 0;

	for (;;) {
		wait_for_request(queue, dormant, time_batch, &request, &waited);
		if (likely(request != NULL)) {
			current_batch++;
			queue->processor(request);
		} else if (!READ_ONCE(queue->running)) {
			break;
		}

		if (dormant) {
			/*
			 * The queue has been roused from dormancy. Clear the flag so enqueuers can
			 * stop broadcasting. No fence is needed for this transition.
			 */
			atomic_set(&queue->dormant, false);
			dormant = false;
			time_batch = DEFAULT_WAIT_TIME;
		} else if (waited) {
			/*
			 * We waited for this request to show up. Adjust the wait time to smooth
			 * out the batch size.
			 */
			if (current_batch < MINIMUM_BATCH) {
				/*
				 * If the last batch of requests was too small, increase the wait
				 * time.
				 */
				time_batch += time_batch / 4;
				if (time_batch >= MAXIMUM_WAIT_TIME) {
					atomic_set(&queue->dormant, true);
					dormant = true;
				}
			} else if (current_batch > MAXIMUM_BATCH) {
				/*
				 * If the last batch of requests was too large, decrease the wait
				 * time.
				 */
				time_batch -= time_batch / 4;
				if (time_batch < MINIMUM_WAIT_TIME)
					time_batch = MINIMUM_WAIT_TIME;
			}
			current_batch = 0;
		}
	}

	/*
	 * Ensure that we process any remaining requests that were enqueued before trying to shut
	 * down. The corresponding write barrier is in uds_request_queue_finish().
	 */
	smp_rmb();
	while ((request = poll_queues(queue)) != NULL)
		queue->processor(request);
}

int uds_make_request_queue(const char *queue_name,
			   uds_request_queue_processor_fn processor,
			   struct uds_request_queue **queue_ptr)
{
	int result;
	struct uds_request_queue *queue;

	result = vdo_allocate(1, struct uds_request_queue, __func__, &queue);
	if (result != VDO_SUCCESS)
		return result;

	queue->processor = processor;
	queue->running = true;
	atomic_set(&queue->dormant, false);
	init_waitqueue_head(&queue->wait_head);

	result = vdo_make_funnel_queue(&queue->main_queue);
	if (result != VDO_SUCCESS) {
		uds_request_queue_finish(queue);
		return result;
	}

	result = vdo_make_funnel_queue(&queue->retry_queue);
	if (result != VDO_SUCCESS) {
		uds_request_queue_finish(queue);
		return result;
	}

	result = vdo_create_thread(request_queue_worker, queue, queue_name,
				   &queue->thread);
	if (result != VDO_SUCCESS) {
		uds_request_queue_finish(queue);
		return result;
	}

	queue->started = true;
	*queue_ptr = queue;
	return UDS_SUCCESS;
}

static inline void wake_up_worker(struct uds_request_queue *queue)
{
	if (wq_has_sleeper(&queue->wait_head))
		wake_up(&queue->wait_head);
}

void uds_request_queue_enqueue(struct uds_request_queue *queue,
			       struct uds_request *request)
{
	struct funnel_queue *sub_queue;
	bool unbatched = request->unbatched;

	sub_queue = request->requeued ? queue->retry_queue : queue->main_queue;
	vdo_funnel_queue_put(sub_queue, &request->queue_link);

	/*
	 * We must wake the worker thread when it is dormant. A read fence isn't needed here since
	 * we know the queue operation acts as one.
	 */
	if (atomic_read(&queue->dormant) || unbatched)
		wake_up_worker(queue);
}

void uds_request_queue_finish(struct uds_request_queue *queue)
{
	if (queue == NULL)
		return;

	/*
	 * This memory barrier ensures that any requests we queued will be seen. The point is that
	 * when dequeue_request() sees the following update to the running flag, it will also be
	 * able to see any change we made to a next field in the funnel queue entry. The
	 * corresponding read barrier is in request_queue_worker().
	 */
	smp_wmb();
	WRITE_ONCE(queue->running, false);

	if (queue->started) {
		wake_up_worker(queue);
		vdo_join_threads(queue->thread);
	}

	vdo_free_funnel_queue(queue->main_queue);
	vdo_free_funnel_queue(queue->retry_queue);
	vdo_free(queue);
}
