/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_TASK_QUEUE_H
#define IOSM_IPC_TASK_QUEUE_H

/* Number of available element for the input message queue of the IPC
 * ipc_task
 */
#define IPC_THREAD_QUEUE_SIZE 256

/**
 * struct ipc_task_queue_args - Struct for Task queue elements
 * @ipc_imem:   Pointer to struct iosm_imem
 * @msg:        Message argument for tasklet function. (optional, can be NULL)
 * @completion: OS object used to wait for the tasklet function to finish for
 *              synchronous calls
 * @func:       Function to be called in tasklet (tl) context
 * @arg:        Generic integer argument for tasklet function (optional)
 * @size:       Message size argument for tasklet function (optional)
 * @response:   Return code of tasklet function for synchronous calls
 * @is_copy:    Is true if msg contains a pointer to a copy of the original msg
 *              for async. calls that needs to be freed once the tasklet returns
 */
struct ipc_task_queue_args {
	struct iosm_imem *ipc_imem;
	void *msg;
	struct completion *completion;
	int (*func)(struct iosm_imem *ipc_imem, int arg, void *msg,
		    size_t size);
	int arg;
	size_t size;
	int response;
	u8 is_copy:1;
};

/**
 * struct ipc_task_queue - Struct for Task queue
 * @q_lock:     Protect the message queue of the ipc ipc_task
 * @args:       Message queue of the IPC ipc_task
 * @q_rpos:     First queue element to process.
 * @q_wpos:     First free element of the input queue.
 */
struct ipc_task_queue {
	spinlock_t q_lock; /* for atomic operation on queue */
	struct ipc_task_queue_args args[IPC_THREAD_QUEUE_SIZE];
	unsigned int q_rpos;
	unsigned int q_wpos;
};

/**
 * struct ipc_task - Struct for Task
 * @dev:	 Pointer to device structure
 * @ipc_tasklet: Tasklet for serialized work offload
 *		 from interrupts and OS callbacks
 * @ipc_queue:	 Task for entry into ipc task queue
 */
struct ipc_task {
	struct device *dev;
	struct tasklet_struct *ipc_tasklet;
	struct ipc_task_queue ipc_queue;
};

/**
 * ipc_task_init - Allocate a tasklet
 * @ipc_task:	Pointer to ipc_task structure
 * Returns: 0 on success and failure value on error.
 */
int ipc_task_init(struct ipc_task *ipc_task);

/**
 * ipc_task_deinit - Free a tasklet, invalidating its pointer.
 * @ipc_task:	Pointer to ipc_task structure
 */
void ipc_task_deinit(struct ipc_task *ipc_task);

/**
 * ipc_task_queue_send_task - Synchronously/Asynchronously call a function in
 *			      tasklet context.
 * @imem:		Pointer to iosm_imem struct
 * @func:		Function to be called in tasklet context
 * @arg:		Integer argument for func
 * @msg:		Message pointer argument for func
 * @size:		Size argument for func
 * @wait:		if true wait for result
 *
 * Returns: Result value returned by func or failure value if func could not
 *	    be called.
 */
int ipc_task_queue_send_task(struct iosm_imem *imem,
			     int (*func)(struct iosm_imem *ipc_imem, int arg,
					 void *msg, size_t size),
			     int arg, void *msg, size_t size, bool wait);

#endif
