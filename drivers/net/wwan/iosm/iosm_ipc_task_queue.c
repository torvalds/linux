// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include "iosm_ipc_imem.h"
#include "iosm_ipc_task_queue.h"

/* Actual tasklet function, will be called whenever tasklet is scheduled.
 * Calls event handler involves callback for each element in the message queue
 */
static void ipc_task_queue_handler(unsigned long data)
{
	struct ipc_task_queue *ipc_task = (struct ipc_task_queue *)data;
	unsigned int q_rpos = ipc_task->q_rpos;

	/* Loop over the input queue contents. */
	while (q_rpos != ipc_task->q_wpos) {
		/* Get the current first queue element. */
		struct ipc_task_queue_args *args = &ipc_task->args[q_rpos];

		/* Process the input message. */
		if (args->func)
			args->response = args->func(args->ipc_imem, args->arg,
						    args->msg, args->size);

		/* Signal completion for synchronous calls */
		if (args->completion)
			complete(args->completion);

		/* Free message if copy was allocated. */
		if (args->is_copy)
			kfree(args->msg);

		/* Set invalid queue element. Technically
		 * spin_lock_irqsave is not required here as
		 * the array element has been processed already
		 * so we can assume that immediately after processing
		 * ipc_task element, queue will not rotate again to
		 * ipc_task same element within such short time.
		 */
		args->completion = NULL;
		args->func = NULL;
		args->msg = NULL;
		args->size = 0;
		args->is_copy = false;

		/* calculate the new read ptr and update the volatile read
		 * ptr
		 */
		q_rpos = (q_rpos + 1) % IPC_THREAD_QUEUE_SIZE;
		ipc_task->q_rpos = q_rpos;
	}
}

/* Free memory alloc and trigger completions left in the queue during dealloc */
static void ipc_task_queue_cleanup(struct ipc_task_queue *ipc_task)
{
	unsigned int q_rpos = ipc_task->q_rpos;

	while (q_rpos != ipc_task->q_wpos) {
		struct ipc_task_queue_args *args = &ipc_task->args[q_rpos];

		if (args->completion)
			complete(args->completion);

		if (args->is_copy)
			kfree(args->msg);

		q_rpos = (q_rpos + 1) % IPC_THREAD_QUEUE_SIZE;
		ipc_task->q_rpos = q_rpos;
	}
}

/* Add a message to the queue and trigger the ipc_task. */
static int
ipc_task_queue_add_task(struct iosm_imem *ipc_imem,
			int arg, void *msg,
			int (*func)(struct iosm_imem *ipc_imem, int arg,
				    void *msg, size_t size),
			size_t size, bool is_copy, bool wait)
{
	struct tasklet_struct *ipc_tasklet = ipc_imem->ipc_task->ipc_tasklet;
	struct ipc_task_queue *ipc_task = &ipc_imem->ipc_task->ipc_queue;
	struct completion completion;
	unsigned int pos, nextpos;
	unsigned long flags;
	int result = -EIO;

	init_completion(&completion);

	/* tasklet send may be called from both interrupt or thread
	 * context, therefore protect queue operation by spinlock
	 */
	spin_lock_irqsave(&ipc_task->q_lock, flags);

	pos = ipc_task->q_wpos;
	nextpos = (pos + 1) % IPC_THREAD_QUEUE_SIZE;

	/* Get next queue position. */
	if (nextpos != ipc_task->q_rpos) {
		/* Get the reference to the queue element and save the passed
		 * values.
		 */
		ipc_task->args[pos].arg = arg;
		ipc_task->args[pos].msg = msg;
		ipc_task->args[pos].func = func;
		ipc_task->args[pos].ipc_imem = ipc_imem;
		ipc_task->args[pos].size = size;
		ipc_task->args[pos].is_copy = is_copy;
		ipc_task->args[pos].completion = wait ? &completion : NULL;
		ipc_task->args[pos].response = -1;

		/* apply write barrier so that ipc_task->q_rpos elements
		 * are updated before ipc_task->q_wpos is being updated.
		 */
		smp_wmb();

		/* Update the status of the free queue space. */
		ipc_task->q_wpos = nextpos;
		result = 0;
	}

	spin_unlock_irqrestore(&ipc_task->q_lock, flags);

	if (result == 0) {
		tasklet_schedule(ipc_tasklet);

		if (wait) {
			wait_for_completion(&completion);
			result = ipc_task->args[pos].response;
		}
	} else {
		dev_err(ipc_imem->ipc_task->dev, "queue is full");
	}

	return result;
}

int ipc_task_queue_send_task(struct iosm_imem *imem,
			     int (*func)(struct iosm_imem *ipc_imem, int arg,
					 void *msg, size_t size),
			     int arg, void *msg, size_t size, bool wait)
{
	bool is_copy = false;
	void *copy = msg;
	int ret = -ENOMEM;

	if (size > 0) {
		copy = kmemdup(msg, size, GFP_ATOMIC);
		if (!copy)
			goto out;

		is_copy = true;
	}

	ret = ipc_task_queue_add_task(imem, arg, copy, func,
				      size, is_copy, wait);
	if (ret < 0) {
		dev_err(imem->ipc_task->dev,
			"add task failed for %ps %d, %p, %zu, %d", func, arg,
			copy, size, is_copy);
		if (is_copy)
			kfree(copy);
		goto out;
	}

	ret = 0;
out:
	return ret;
}

int ipc_task_init(struct ipc_task *ipc_task)
{
	struct ipc_task_queue *ipc_queue = &ipc_task->ipc_queue;

	ipc_task->ipc_tasklet = kzalloc(sizeof(*ipc_task->ipc_tasklet),
					GFP_KERNEL);

	if (!ipc_task->ipc_tasklet)
		return -ENOMEM;

	/* Initialize the spinlock needed to protect the message queue of the
	 * ipc_task
	 */
	spin_lock_init(&ipc_queue->q_lock);

	tasklet_init(ipc_task->ipc_tasklet, ipc_task_queue_handler,
		     (unsigned long)ipc_queue);
	return 0;
}

void ipc_task_deinit(struct ipc_task *ipc_task)
{
	tasklet_kill(ipc_task->ipc_tasklet);

	kfree(ipc_task->ipc_tasklet);
	/* This will free/complete any outstanding messages,
	 * without calling the actual handler
	 */
	ipc_task_queue_cleanup(&ipc_task->ipc_queue);
}
