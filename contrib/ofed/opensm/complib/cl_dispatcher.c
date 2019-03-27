/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *    Implementation of Dispatcher abstraction.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <complib/cl_dispatcher.h>
#include <complib/cl_thread.h>
#include <complib/cl_timer.h>

/* give some guidance when we build our cl_pool of messages */
#define CL_DISP_INITIAL_MSG_COUNT   256
#define CL_DISP_MSG_GROW_SIZE       64

/* give some guidance when we build our cl_pool of registration elements */
#define CL_DISP_INITIAL_REG_COUNT   16
#define CL_DISP_REG_GROW_SIZE       16

/********************************************************************
   __cl_disp_worker

   Description:
   This function takes messages off the FIFO and calls Processmsg()
   This function executes as passive level.

   Inputs:
   p_disp - Pointer to Dispatcher object

   Outputs:
   None

   Returns:
   None
********************************************************************/
void __cl_disp_worker(IN void *context)
{
	cl_disp_msg_t *p_msg;
	cl_dispatcher_t *p_disp = (cl_dispatcher_t *) context;

	cl_spinlock_acquire(&p_disp->lock);

	/* Process the FIFO until we drain it dry. */
	while (cl_qlist_count(&p_disp->msg_fifo)) {
		/* Pop the message at the head from the FIFO. */
		p_msg =
		    (cl_disp_msg_t *) cl_qlist_remove_head(&p_disp->msg_fifo);

		/* we track the tim ethe last message spent in the queue */
		p_disp->last_msg_queue_time_us =
		    cl_get_time_stamp() - p_msg->in_time;

		/*
		 * Release the spinlock while the message is processed.
		 * The user's callback may reenter the dispatcher
		 * and cause the lock to be reaquired.
		 */
		cl_spinlock_release(&p_disp->lock);
		p_msg->p_dest_reg->pfn_rcv_callback((void *)p_msg->p_dest_reg->
						    context,
						    (void *)p_msg->p_data);

		cl_atomic_dec(&p_msg->p_dest_reg->ref_cnt);

		/* The client has seen the data.  Notify the sender as appropriate. */
		if (p_msg->pfn_xmt_callback) {
			p_msg->pfn_xmt_callback((void *)p_msg->context,
						(void *)p_msg->p_data);
			cl_atomic_dec(&p_msg->p_src_reg->ref_cnt);
		}

		/* Grab the lock for the next iteration through the list. */
		cl_spinlock_acquire(&p_disp->lock);

		/* Return this message to the pool. */
		cl_qpool_put(&p_disp->msg_pool, (cl_pool_item_t *) p_msg);
	}

	cl_spinlock_release(&p_disp->lock);
}

void cl_disp_construct(IN cl_dispatcher_t * const p_disp)
{
	CL_ASSERT(p_disp);

	cl_qlist_init(&p_disp->reg_list);
	cl_ptr_vector_construct(&p_disp->reg_vec);
	cl_qlist_init(&p_disp->msg_fifo);
	cl_spinlock_construct(&p_disp->lock);
	cl_qpool_construct(&p_disp->msg_pool);
}

void cl_disp_shutdown(IN cl_dispatcher_t * const p_disp)
{
	CL_ASSERT(p_disp);

	/* Stop the thread pool. */
	cl_thread_pool_destroy(&p_disp->worker_threads);

	/* Process all outstanding callbacks. */
	__cl_disp_worker(p_disp);

	/* Free all registration info. */
	while (!cl_is_qlist_empty(&p_disp->reg_list))
		free(cl_qlist_remove_head(&p_disp->reg_list));
}

void cl_disp_destroy(IN cl_dispatcher_t * const p_disp)
{
	CL_ASSERT(p_disp);

	cl_spinlock_destroy(&p_disp->lock);
	/* Destroy the message pool */
	cl_qpool_destroy(&p_disp->msg_pool);
	/* Destroy the pointer vector of registrants. */
	cl_ptr_vector_destroy(&p_disp->reg_vec);
}

cl_status_t cl_disp_init(IN cl_dispatcher_t * const p_disp,
			 IN const uint32_t thread_count,
			 IN const char *const name)
{
	cl_status_t status;

	CL_ASSERT(p_disp);

	cl_disp_construct(p_disp);

	status = cl_spinlock_init(&p_disp->lock);
	if (status != CL_SUCCESS) {
		cl_disp_destroy(p_disp);
		return (status);
	}

	/* Specify no upper limit to the number of messages in the pool */
	status = cl_qpool_init(&p_disp->msg_pool, CL_DISP_INITIAL_MSG_COUNT,
			       0, CL_DISP_MSG_GROW_SIZE, sizeof(cl_disp_msg_t),
			       NULL, NULL, NULL);
	if (status != CL_SUCCESS) {
		cl_disp_destroy(p_disp);
		return (status);
	}

	status = cl_ptr_vector_init(&p_disp->reg_vec, CL_DISP_INITIAL_REG_COUNT,
				    CL_DISP_REG_GROW_SIZE);
	if (status != CL_SUCCESS) {
		cl_disp_destroy(p_disp);
		return (status);
	}

	status = cl_thread_pool_init(&p_disp->worker_threads, thread_count,
				     __cl_disp_worker, p_disp, name);
	if (status != CL_SUCCESS)
		cl_disp_destroy(p_disp);

	return (status);
}

cl_disp_reg_handle_t cl_disp_register(IN cl_dispatcher_t * const p_disp,
				      IN const cl_disp_msgid_t msg_id,
				      IN cl_pfn_msgrcv_cb_t pfn_callback
				      OPTIONAL,
				      IN const void *const context OPTIONAL)
{
	cl_disp_reg_info_t *p_reg;
	cl_status_t status;

	CL_ASSERT(p_disp);

	/* Check that the requested registrant ID is available. */
	cl_spinlock_acquire(&p_disp->lock);
	if ((msg_id != CL_DISP_MSGID_NONE) &&
	    (msg_id < cl_ptr_vector_get_size(&p_disp->reg_vec)) &&
	    (cl_ptr_vector_get(&p_disp->reg_vec, msg_id))) {
		cl_spinlock_release(&p_disp->lock);
		return (NULL);
	}

	/* Get a registration info from the pool. */
	p_reg = (cl_disp_reg_info_t *) malloc(sizeof(cl_disp_reg_info_t));
	if (!p_reg) {
		cl_spinlock_release(&p_disp->lock);
		return (NULL);
	} else {
		memset(p_reg, 0, sizeof(cl_disp_reg_info_t));
	}

	p_reg->p_disp = p_disp;
	p_reg->ref_cnt = 0;
	p_reg->pfn_rcv_callback = pfn_callback;
	p_reg->context = context;
	p_reg->msg_id = msg_id;

	/* Insert the registration in the list. */
	cl_qlist_insert_tail(&p_disp->reg_list, (cl_list_item_t *) p_reg);

	/* Set the array entry to the registrant. */
	/* The ptr_vector grow automatically as necessary. */
	if (msg_id != CL_DISP_MSGID_NONE) {
		status = cl_ptr_vector_set(&p_disp->reg_vec, msg_id, p_reg);
		if (status != CL_SUCCESS) {
			free(p_reg);
			cl_spinlock_release(&p_disp->lock);
			return (NULL);
		}
	}

	cl_spinlock_release(&p_disp->lock);

	return (p_reg);
}

void cl_disp_unregister(IN const cl_disp_reg_handle_t handle)
{
	cl_disp_reg_info_t *p_reg;
	cl_dispatcher_t *p_disp;

	if (handle == CL_DISP_INVALID_HANDLE)
		return;

	p_reg = (cl_disp_reg_info_t *) handle;
	p_disp = p_reg->p_disp;
	CL_ASSERT(p_disp);

	cl_spinlock_acquire(&p_disp->lock);
	/*
	 * Clear the registrant vector entry.  This will cause any further
	 * post calls to fail.
	 */
	if (p_reg->msg_id != CL_DISP_MSGID_NONE) {
		CL_ASSERT(p_reg->msg_id <
			  cl_ptr_vector_get_size(&p_disp->reg_vec));
		cl_ptr_vector_set(&p_disp->reg_vec, p_reg->msg_id, NULL);
	}
	cl_spinlock_release(&p_disp->lock);

	while (p_reg->ref_cnt > 0)
		cl_thread_suspend(1);

	cl_spinlock_acquire(&p_disp->lock);
	/* Remove the registrant from the list. */
	cl_qlist_remove_item(&p_disp->reg_list, (cl_list_item_t *) p_reg);
	/* Return the registration info to the pool */
	free(p_reg);

	cl_spinlock_release(&p_disp->lock);
}

cl_status_t cl_disp_post(IN const cl_disp_reg_handle_t handle,
			 IN const cl_disp_msgid_t msg_id,
			 IN const void *const p_data,
			 IN cl_pfn_msgdone_cb_t pfn_callback OPTIONAL,
			 IN const void *const context OPTIONAL)
{
	cl_disp_reg_info_t *p_src_reg = (cl_disp_reg_info_t *) handle;
	cl_disp_reg_info_t *p_dest_reg;
	cl_dispatcher_t *p_disp;
	cl_disp_msg_t *p_msg;

	p_disp = handle->p_disp;
	CL_ASSERT(p_disp);
	CL_ASSERT(msg_id != CL_DISP_MSGID_NONE);

	cl_spinlock_acquire(&p_disp->lock);
	/* Check that the recipient exists. */
	if (cl_ptr_vector_get_size(&p_disp->reg_vec) <= msg_id) {
		cl_spinlock_release(&p_disp->lock);
		return (CL_NOT_FOUND);
	}

	p_dest_reg = cl_ptr_vector_get(&p_disp->reg_vec, msg_id);
	if (!p_dest_reg) {
		cl_spinlock_release(&p_disp->lock);
		return (CL_NOT_FOUND);
	}

	/* Get a free message from the pool. */
	p_msg = (cl_disp_msg_t *) cl_qpool_get(&p_disp->msg_pool);
	if (!p_msg) {
		cl_spinlock_release(&p_disp->lock);
		return (CL_INSUFFICIENT_MEMORY);
	}

	/* Initialize the message */
	p_msg->p_src_reg = p_src_reg;
	p_msg->p_dest_reg = p_dest_reg;
	p_msg->p_data = p_data;
	p_msg->pfn_xmt_callback = pfn_callback;
	p_msg->context = context;
	p_msg->in_time = cl_get_time_stamp();

	/*
	 * Increment the sender's reference count if they request a completion
	 * notification.
	 */
	if (pfn_callback)
		cl_atomic_inc(&p_src_reg->ref_cnt);

	/* Increment the recipient's reference count. */
	cl_atomic_inc(&p_dest_reg->ref_cnt);

	/* Queue the message in the FIFO. */
	cl_qlist_insert_tail(&p_disp->msg_fifo, (cl_list_item_t *) p_msg);
	cl_spinlock_release(&p_disp->lock);

	/* Signal the thread pool that there is work to be done. */
	cl_thread_pool_signal(&p_disp->worker_threads);
	return (CL_SUCCESS);
}

void cl_disp_get_queue_status(IN const cl_disp_reg_handle_t handle,
			      OUT uint32_t * p_num_queued_msgs,
			      OUT uint64_t * p_last_msg_queue_time_ms)
{
	cl_dispatcher_t *p_disp = ((cl_disp_reg_info_t *) handle)->p_disp;

	cl_spinlock_acquire(&p_disp->lock);

	if (p_last_msg_queue_time_ms)
		*p_last_msg_queue_time_ms =
		    p_disp->last_msg_queue_time_us / 1000;

	if (p_num_queued_msgs)
		*p_num_queued_msgs = cl_qlist_count(&p_disp->msg_fifo);

	cl_spinlock_release(&p_disp->lock);
}
