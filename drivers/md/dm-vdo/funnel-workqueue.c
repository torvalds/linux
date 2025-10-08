// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "funnel-workqueue.h"

#include <linux/atomic.h>
#include <linux/cache.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/percpu.h>

#include "funnel-queue.h"
#include "logger.h"
#include "memory-alloc.h"
#include "numeric.h"
#include "permassert.h"
#include "string-utils.h"

#include "completion.h"
#include "status-codes.h"

static DEFINE_PER_CPU(unsigned int, service_queue_rotor);

/**
 * DOC: Work queue definition.
 *
 * There are two types of work queues: simple, with one worker thread, and round-robin, which uses
 * a group of the former to do the work, and assigns work to them in round-robin fashion (roughly).
 * Externally, both are represented via the same common sub-structure, though there's actually not
 * a great deal of overlap between the two types internally.
 */
struct vdo_work_queue {
	/* Name of just the work queue (e.g., "cpuQ12") */
	char *name;
	bool round_robin_mode;
	struct vdo_thread *owner;
	/* Life cycle functions, etc */
	const struct vdo_work_queue_type *type;
};

struct simple_work_queue {
	struct vdo_work_queue common;
	struct funnel_queue *priority_lists[VDO_WORK_Q_MAX_PRIORITY + 1];
	void *private;

	/*
	 * The fields above are unchanged after setup but often read, and are good candidates for
	 * caching -- and if the max priority is 2, just fit in one x86-64 cache line if aligned.
	 * The fields below are often modified as we sleep and wake, so we want a separate cache
	 * line for performance.
	 */

	/* Any (0 or 1) worker threads waiting for new work to do */
	wait_queue_head_t waiting_worker_threads ____cacheline_aligned;
	/* Hack to reduce wakeup calls if the worker thread is running */
	atomic_t idle;

	/* These are infrequently used so in terms of performance we don't care where they land. */
	struct task_struct *thread;
	/* Notify creator once worker has initialized */
	struct completion *started;
};

struct round_robin_work_queue {
	struct vdo_work_queue common;
	struct simple_work_queue **service_queues;
	unsigned int num_service_queues;
};

static inline struct simple_work_queue *as_simple_work_queue(struct vdo_work_queue *queue)
{
	return ((queue == NULL) ?
		NULL : container_of(queue, struct simple_work_queue, common));
}

static inline struct round_robin_work_queue *as_round_robin_work_queue(struct vdo_work_queue *queue)
{
	return ((queue == NULL) ?
		 NULL :
		 container_of(queue, struct round_robin_work_queue, common));
}

/* Processing normal completions. */

/*
 * Dequeue and return the next waiting completion, if any.
 *
 * We scan the funnel queues from highest priority to lowest, once; there is therefore a race
 * condition where a high-priority completion can be enqueued followed by a lower-priority one, and
 * we'll grab the latter (but we'll catch the high-priority item on the next call). If strict
 * enforcement of priorities becomes necessary, this function will need fixing.
 */
static struct vdo_completion *poll_for_completion(struct simple_work_queue *queue)
{
	int i;

	for (i = queue->common.type->max_priority; i >= 0; i--) {
		struct funnel_queue_entry *link = vdo_funnel_queue_poll(queue->priority_lists[i]);

		if (link != NULL)
			return container_of(link, struct vdo_completion, work_queue_entry_link);
	}

	return NULL;
}

static void enqueue_work_queue_completion(struct simple_work_queue *queue,
					  struct vdo_completion *completion)
{
	VDO_ASSERT_LOG_ONLY(completion->my_queue == NULL,
			    "completion %px (fn %px) to enqueue (%px) is not already queued (%px)",
			    completion, completion->callback, queue, completion->my_queue);
	if (completion->priority == VDO_WORK_Q_DEFAULT_PRIORITY)
		completion->priority = queue->common.type->default_priority;

	if (VDO_ASSERT(completion->priority <= queue->common.type->max_priority,
		       "priority is in range for queue") != VDO_SUCCESS)
		completion->priority = 0;

	completion->my_queue = &queue->common;

	/* Funnel queue handles the synchronization for the put. */
	vdo_funnel_queue_put(queue->priority_lists[completion->priority],
			     &completion->work_queue_entry_link);

	/*
	 * Due to how funnel queue synchronization is handled (just atomic operations), the
	 * simplest safe implementation here would be to wake-up any waiting threads after
	 * enqueueing each item. Even if the funnel queue is not empty at the time of adding an
	 * item to the queue, the consumer thread may not see this since it is not guaranteed to
	 * have the same view of the queue as a producer thread.
	 *
	 * However, the above is wasteful so instead we attempt to minimize the number of thread
	 * wakeups. Using an idle flag, and careful ordering using memory barriers, we should be
	 * able to determine when the worker thread might be asleep or going to sleep. We use
	 * cmpxchg to try to take ownership (vs other producer threads) of the responsibility for
	 * waking the worker thread, so multiple wakeups aren't tried at once.
	 *
	 * This was tuned for some x86 boxes that were handy; it's untested whether doing the read
	 * first is any better or worse for other platforms, even other x86 configurations.
	 */
	smp_mb();
	if ((atomic_read(&queue->idle) != 1) || (atomic_cmpxchg(&queue->idle, 1, 0) != 1))
		return;

	/* There's a maximum of one thread in this list. */
	wake_up(&queue->waiting_worker_threads);
}

static void run_start_hook(struct simple_work_queue *queue)
{
	if (queue->common.type->start != NULL)
		queue->common.type->start(queue->private);
}

static void run_finish_hook(struct simple_work_queue *queue)
{
	if (queue->common.type->finish != NULL)
		queue->common.type->finish(queue->private);
}

/*
 * Wait for the next completion to process, or until kthread_should_stop indicates that it's time
 * for us to shut down.
 *
 * If kthread_should_stop says it's time to stop but we have pending completions return a
 * completion.
 *
 * Also update statistics relating to scheduler interactions.
 */
static struct vdo_completion *wait_for_next_completion(struct simple_work_queue *queue)
{
	struct vdo_completion *completion;
	DEFINE_WAIT(wait);

	while (true) {
		prepare_to_wait(&queue->waiting_worker_threads, &wait,
				TASK_INTERRUPTIBLE);
		/*
		 * Don't set the idle flag until a wakeup will not be lost.
		 *
		 * Force synchronization between setting the idle flag and checking the funnel
		 * queue; the producer side will do them in the reverse order. (There's still a
		 * race condition we've chosen to allow, because we've got a timeout below that
		 * unwedges us if we hit it, but this may narrow the window a little.)
		 */
		atomic_set(&queue->idle, 1);
		smp_mb(); /* store-load barrier between "idle" and funnel queue */

		completion = poll_for_completion(queue);
		if (completion != NULL)
			break;

		/*
		 * We need to check for thread-stop after setting TASK_INTERRUPTIBLE state up
		 * above. Otherwise, schedule() will put the thread to sleep and might miss a
		 * wakeup from kthread_stop() call in vdo_finish_work_queue().
		 */
		if (kthread_should_stop())
			break;

		schedule();

		/*
		 * Most of the time when we wake, it should be because there's work to do. If it
		 * was a spurious wakeup, continue looping.
		 */
		completion = poll_for_completion(queue);
		if (completion != NULL)
			break;
	}

	finish_wait(&queue->waiting_worker_threads, &wait);
	atomic_set(&queue->idle, 0);

	return completion;
}

static void process_completion(struct simple_work_queue *queue,
			       struct vdo_completion *completion)
{
	if (VDO_ASSERT(completion->my_queue == &queue->common,
		       "completion %px from queue %px marked as being in this queue (%px)",
		       completion, queue, completion->my_queue) == VDO_SUCCESS)
		completion->my_queue = NULL;

	vdo_run_completion(completion);
}

static void service_work_queue(struct simple_work_queue *queue)
{
	run_start_hook(queue);

	while (true) {
		struct vdo_completion *completion = poll_for_completion(queue);

		if (completion == NULL)
			completion = wait_for_next_completion(queue);

		if (completion == NULL) {
			/* No completions but kthread_should_stop() was triggered. */
			break;
		}

		process_completion(queue, completion);

		/*
		 * Be friendly to a CPU that has other work to do, if the kernel has told us to.
		 * This speeds up some performance tests; that "other work" might include other VDO
		 * threads.
		 */
		cond_resched();
	}

	run_finish_hook(queue);
}

static int work_queue_runner(void *ptr)
{
	struct simple_work_queue *queue = ptr;

	complete(queue->started);
	service_work_queue(queue);
	return 0;
}

/* Creation & teardown */

static void free_simple_work_queue(struct simple_work_queue *queue)
{
	unsigned int i;

	for (i = 0; i <= VDO_WORK_Q_MAX_PRIORITY; i++)
		vdo_free_funnel_queue(queue->priority_lists[i]);
	vdo_free(queue->common.name);
	vdo_free(queue);
}

static void free_round_robin_work_queue(struct round_robin_work_queue *queue)
{
	struct simple_work_queue **queue_table = queue->service_queues;
	unsigned int count = queue->num_service_queues;
	unsigned int i;

	queue->service_queues = NULL;

	for (i = 0; i < count; i++)
		free_simple_work_queue(queue_table[i]);
	vdo_free(queue_table);
	vdo_free(queue->common.name);
	vdo_free(queue);
}

void vdo_free_work_queue(struct vdo_work_queue *queue)
{
	if (queue == NULL)
		return;

	vdo_finish_work_queue(queue);

	if (queue->round_robin_mode)
		free_round_robin_work_queue(as_round_robin_work_queue(queue));
	else
		free_simple_work_queue(as_simple_work_queue(queue));
}

static int make_simple_work_queue(const char *thread_name_prefix, const char *name,
				  struct vdo_thread *owner, void *private,
				  const struct vdo_work_queue_type *type,
				  struct simple_work_queue **queue_ptr)
{
	DECLARE_COMPLETION_ONSTACK(started);
	struct simple_work_queue *queue;
	int i;
	struct task_struct *thread = NULL;
	int result;

	VDO_ASSERT_LOG_ONLY((type->max_priority <= VDO_WORK_Q_MAX_PRIORITY),
			    "queue priority count %u within limit %u", type->max_priority,
			    VDO_WORK_Q_MAX_PRIORITY);

	result = vdo_allocate(1, struct simple_work_queue, "simple work queue", &queue);
	if (result != VDO_SUCCESS)
		return result;

	queue->private = private;
	queue->started = &started;
	queue->common.type = type;
	queue->common.owner = owner;
	init_waitqueue_head(&queue->waiting_worker_threads);

	result = vdo_duplicate_string(name, "queue name", &queue->common.name);
	if (result != VDO_SUCCESS) {
		vdo_free(queue);
		return -ENOMEM;
	}

	for (i = 0; i <= type->max_priority; i++) {
		result = vdo_make_funnel_queue(&queue->priority_lists[i]);
		if (result != VDO_SUCCESS) {
			free_simple_work_queue(queue);
			return result;
		}
	}

	thread = kthread_run(work_queue_runner, queue, "%s:%s", thread_name_prefix,
			     queue->common.name);
	if (IS_ERR(thread)) {
		free_simple_work_queue(queue);
		return (int) PTR_ERR(thread);
	}

	queue->thread = thread;

	/*
	 * If we don't wait to ensure the thread is running VDO code, a quick kthread_stop (due to
	 * errors elsewhere) could cause it to never get as far as running VDO, skipping the
	 * cleanup code.
	 *
	 * Eventually we should just make that path safe too, and then we won't need this
	 * synchronization.
	 */
	wait_for_completion(&started);

	*queue_ptr = queue;
	return VDO_SUCCESS;
}

/**
 * vdo_make_work_queue() - Create a work queue; if multiple threads are requested, completions will
 *                         be distributed to them in round-robin fashion.
 *
 * Each queue is associated with a struct vdo_thread which has a single vdo thread id. Regardless
 * of the actual number of queues and threads allocated here, code outside of the queue
 * implementation will treat this as a single zone.
 */
int vdo_make_work_queue(const char *thread_name_prefix, const char *name,
			struct vdo_thread *owner, const struct vdo_work_queue_type *type,
			unsigned int thread_count, void *thread_privates[],
			struct vdo_work_queue **queue_ptr)
{
	struct round_robin_work_queue *queue;
	int result;
	char thread_name[TASK_COMM_LEN];
	unsigned int i;

	if (thread_count == 1) {
		struct simple_work_queue *simple_queue;
		void *context = ((thread_privates != NULL) ? thread_privates[0] : NULL);

		result = make_simple_work_queue(thread_name_prefix, name, owner, context,
						type, &simple_queue);
		if (result == VDO_SUCCESS)
			*queue_ptr = &simple_queue->common;
		return result;
	}

	result = vdo_allocate(1, struct round_robin_work_queue, "round-robin work queue",
			      &queue);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_allocate(thread_count, struct simple_work_queue *,
			      "subordinate work queues", &queue->service_queues);
	if (result != VDO_SUCCESS) {
		vdo_free(queue);
		return result;
	}

	queue->num_service_queues = thread_count;
	queue->common.round_robin_mode = true;
	queue->common.owner = owner;

	result = vdo_duplicate_string(name, "queue name", &queue->common.name);
	if (result != VDO_SUCCESS) {
		vdo_free(queue->service_queues);
		vdo_free(queue);
		return -ENOMEM;
	}

	*queue_ptr = &queue->common;

	for (i = 0; i < thread_count; i++) {
		void *context = ((thread_privates != NULL) ? thread_privates[i] : NULL);

		snprintf(thread_name, sizeof(thread_name), "%s%u", name, i);
		result = make_simple_work_queue(thread_name_prefix, thread_name, owner,
						context, type, &queue->service_queues[i]);
		if (result != VDO_SUCCESS) {
			queue->num_service_queues = i;
			/* Destroy previously created subordinates. */
			vdo_free_work_queue(vdo_forget(*queue_ptr));
			return result;
		}
	}

	return VDO_SUCCESS;
}

static void finish_simple_work_queue(struct simple_work_queue *queue)
{
	if (queue->thread == NULL)
		return;

	/* Tells the worker thread to shut down and waits for it to exit. */
	kthread_stop(queue->thread);
	queue->thread = NULL;
}

static void finish_round_robin_work_queue(struct round_robin_work_queue *queue)
{
	struct simple_work_queue **queue_table = queue->service_queues;
	unsigned int count = queue->num_service_queues;
	unsigned int i;

	for (i = 0; i < count; i++)
		finish_simple_work_queue(queue_table[i]);
}

/* No enqueueing of completions should be done once this function is called. */
void vdo_finish_work_queue(struct vdo_work_queue *queue)
{
	if (queue == NULL)
		return;

	if (queue->round_robin_mode)
		finish_round_robin_work_queue(as_round_robin_work_queue(queue));
	else
		finish_simple_work_queue(as_simple_work_queue(queue));
}

/* Debugging dumps */

static void dump_simple_work_queue(struct simple_work_queue *queue)
{
	const char *thread_status = "no threads";
	char task_state_report = '-';

	if (queue->thread != NULL) {
		task_state_report = task_state_to_char(queue->thread);
		thread_status = atomic_read(&queue->idle) ? "idle" : "running";
	}

	vdo_log_info("workQ %px (%s) %s (%c)", &queue->common, queue->common.name,
		     thread_status, task_state_report);

	/* ->waiting_worker_threads wait queue status? anyone waiting? */
}

/*
 * Write to the buffer some info about the completion, for logging. Since the common use case is
 * dumping info about a lot of completions to syslog all at once, the format favors brevity over
 * readability.
 */
void vdo_dump_work_queue(struct vdo_work_queue *queue)
{
	if (queue->round_robin_mode) {
		struct round_robin_work_queue *round_robin = as_round_robin_work_queue(queue);
		unsigned int i;

		for (i = 0; i < round_robin->num_service_queues; i++)
			dump_simple_work_queue(round_robin->service_queues[i]);
	} else {
		dump_simple_work_queue(as_simple_work_queue(queue));
	}
}

static void get_function_name(void *pointer, char *buffer, size_t buffer_length)
{
	if (pointer == NULL) {
		/*
		 * Format "%ps" logs a null pointer as "(null)" with a bunch of leading spaces. We
		 * sometimes use this when logging lots of data; don't be so verbose.
		 */
		strscpy(buffer, "-", buffer_length);
	} else {
		/*
		 * Use a pragma to defeat gcc's format checking, which doesn't understand that
		 * "%ps" actually does support a precision spec in Linux kernel code.
		 */
		char *space;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
		snprintf(buffer, buffer_length, "%.*ps", buffer_length - 1, pointer);
#pragma GCC diagnostic pop

		space = strchr(buffer, ' ');
		if (space != NULL)
			*space = '\0';
	}
}

void vdo_dump_completion_to_buffer(struct vdo_completion *completion, char *buffer,
				   size_t length)
{
	size_t current_length =
		scnprintf(buffer, length, "%.*s/", TASK_COMM_LEN,
			  (completion->my_queue == NULL ? "-" : completion->my_queue->name));

	if (current_length < length - 1) {
		get_function_name((void *) completion->callback, buffer + current_length,
				  length - current_length);
	}
}

/* Completion submission */
/*
 * If the completion has a timeout that has already passed, the timeout handler function may be
 * invoked by this function.
 */
void vdo_enqueue_work_queue(struct vdo_work_queue *queue,
			    struct vdo_completion *completion)
{
	/*
	 * Convert the provided generic vdo_work_queue to the simple_work_queue to actually queue
	 * on.
	 */
	struct simple_work_queue *simple_queue = NULL;

	if (!queue->round_robin_mode) {
		simple_queue = as_simple_work_queue(queue);
	} else {
		struct round_robin_work_queue *round_robin = as_round_robin_work_queue(queue);

		/*
		 * It shouldn't be a big deal if the same rotor gets used for multiple work queues.
		 * Any patterns that might develop are likely to be disrupted by random ordering of
		 * multiple completions and migration between cores, unless the load is so light as
		 * to be regular in ordering of tasks and the threads are confined to individual
		 * cores; with a load that light we won't care.
		 */
		unsigned int rotor = this_cpu_inc_return(service_queue_rotor);
		unsigned int index = rotor % round_robin->num_service_queues;

		simple_queue = round_robin->service_queues[index];
	}

	enqueue_work_queue_completion(simple_queue, completion);
}

/* Misc */

/*
 * Return the work queue pointer recorded at initialization time in the work-queue stack handle
 * initialized on the stack of the current thread, if any.
 */
static struct simple_work_queue *get_current_thread_work_queue(void)
{
	/*
	 * In interrupt context, if a vdo thread is what got interrupted, the calls below will find
	 * the queue for the thread which was interrupted. However, the interrupted thread may have
	 * been processing a completion, in which case starting to process another would violate
	 * our concurrency assumptions.
	 */
	if (in_interrupt())
		return NULL;

	if (kthread_func(current) != work_queue_runner)
		/* Not a VDO work queue thread. */
		return NULL;

	return kthread_data(current);
}

struct vdo_work_queue *vdo_get_current_work_queue(void)
{
	struct simple_work_queue *queue = get_current_thread_work_queue();

	return (queue == NULL) ? NULL : &queue->common;
}

struct vdo_thread *vdo_get_work_queue_owner(struct vdo_work_queue *queue)
{
	return queue->owner;
}

/**
 * vdo_get_work_queue_private_data() - Returns the private data for the current thread's work
 *                                     queue, or NULL if none or if the current thread is not a
 *                                     work queue thread.
 */
void *vdo_get_work_queue_private_data(void)
{
	struct simple_work_queue *queue = get_current_thread_work_queue();

	return (queue != NULL) ? queue->private : NULL;
}

bool vdo_work_queue_type_is(struct vdo_work_queue *queue,
			    const struct vdo_work_queue_type *type)
{
	return (queue->type == type);
}
