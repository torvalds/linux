/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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
 * 	Declaration of dispatcher abstraction.
 */

#ifndef _CL_DISPATCHER_H_
#define _CL_DISPATCHER_H_

#include <complib/cl_atomic.h>
#include <complib/cl_threadpool.h>
#include <complib/cl_qlist.h>
#include <complib/cl_qpool.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_ptr_vector.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Dispatcher
* NAME
*	Dispatcher
*
* DESCRIPTION
*	The Dispatcher provides a facility for message routing to
*	asynchronous worker threads.
*
*	The Dispatcher functions operate on a cl_dispatcher_t structure
*	which should be treated as opaque and should be manipulated
*	only through the provided functions.
*
* SEE ALSO
*	Structures:
*		cl_dispatcher_t
*
*	Initialization/Destruction:
*		cl_disp_construct, cl_disp_init, cl_disp_shutdown, cl_disp_destroy
*
*	Manipulation:
*		cl_disp_post, cl_disp_reset, cl_disp_wait_on
*********/
/****s* Component Library: Dispatcher/cl_disp_msgid_t
* NAME
*	cl_disp_msgid_t
*
* DESCRIPTION
*	Defines the type of dispatcher messages.
*
* SYNOPSIS
*/
typedef uint32_t cl_disp_msgid_t;
/**********/

/****s* Component Library: Dispatcher/CL_DISP_MSGID_NONE
* NAME
*	CL_DISP_MSGID_NONE
*
* DESCRIPTION
*	Defines a message value that means "no message".
*	This value is used during registration by Dispatcher clients
*	that do not wish to receive messages.
*
*	No Dispatcher message is allowed to have this value.
*
* SYNOPSIS
*/
#define CL_DISP_MSGID_NONE	0xFFFFFFFF
/**********/

/****s* Component Library: Dispatcher/CL_DISP_INVALID_HANDLE
* NAME
*	CL_DISP_INVALID_HANDLE
*
* DESCRIPTION
*	Defines the value of an invalid Dispatcher registration handle.
*
* SYNOPSIS
*/
#define CL_DISP_INVALID_HANDLE ((cl_disp_reg_handle_t)0)
/*********/

/****f* Component Library: Dispatcher/cl_pfn_msgrcv_cb_t
* NAME
*	cl_pfn_msgrcv_cb_t
*
* DESCRIPTION
*	This typedef defines the prototype for client functions invoked
*	by the Dispatcher.  The Dispatcher calls the corresponding
*	client function when delivering a message to the client.
*
*	The client function must be reentrant if the user creates a
*	Dispatcher with more than one worker thread.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_msgrcv_cb_t) (IN void *context, IN void *p_data);
/*
* PARAMETERS
*	context
*		[in] Client specific context specified in a call to
*		cl_disp_register
*
*	p_data
*		[in] Pointer to the client specific data payload
*		of this message.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This typedef provides a function prototype reference for
*	the function provided by Dispatcher clients as a parameter
*	to the cl_disp_register function.
*
* SEE ALSO
*	Dispatcher, cl_disp_register
*********/

/****f* Component Library: Dispatcher/cl_pfn_msgdone_cb_t
* NAME
*	cl_pfn_msgdone_cb_t
*
* DESCRIPTION
*	This typedef defines the prototype for client functions invoked
*	by the Dispatcher.  The Dispatcher calls the corresponding
*	client function after completing delivery of a message.
*
*	The client function must be reentrant if the user creates a
*	Dispatcher with more than one worker thread.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_msgdone_cb_t) (IN void *context, IN void *p_data);
/*
* PARAMETERS
*	context
*		[in] Client specific context specified in a call to
*		cl_disp_post
*
*	p_data
*		[in] Pointer to the client specific data payload
*		of this message.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This typedef provides a function prototype reference for
*	the function provided by Dispatcher clients as a parameter
*	to the cl_disp_post function.
*
* SEE ALSO
*	Dispatcher, cl_disp_post
*********/

/****s* Component Library: Dispatcher/cl_dispatcher_t
* NAME
*	cl_dispatcher_t
*
* DESCRIPTION
*	Dispatcher structure.
*
*	The Dispatcher is thread safe.
*
*	The cl_dispatcher_t structure should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_dispatcher {
	cl_spinlock_t lock;
	cl_ptr_vector_t reg_vec;
	cl_qlist_t reg_list;
	cl_thread_pool_t worker_threads;
	cl_qlist_t msg_fifo;
	cl_qpool_t msg_pool;
	uint64_t last_msg_queue_time_us;
} cl_dispatcher_t;
/*
* FIELDS
*	reg_vec
*		Vector of registration info objects.  Indexed by message msg_id.
*
*	lock
*		Spinlock to guard internal structures.
*
*	msg_fifo
*		FIFO of messages being processed by the Dispatcher.  New
*		messages are posted to the tail of the FIFO.  Worker threads
*		pull messages from the front.
*
*	worker_threads
*		Thread pool of worker threads to dispose of posted messages.
*
*	msg_pool
*		Pool of message objects to be processed through the FIFO.
*
*	reg_count
*		Count of the number of registrants.
*
*	state
*		Indicates the state of the object.
*
*       last_msg_queue_time_us
*               The time that the last message spent in the Q in usec
*
* SEE ALSO
*	Dispatcher
*********/

/****s* Component Library: Dispatcher/cl_disp_reg_info_t
* NAME
*	cl_disp_reg_info_t
*
* DESCRIPTION
*	Defines the dispatcher registration object structure.
*
*	The cl_disp_reg_info_t structure is for internal use by the
*	Dispatcher only.
*
* SYNOPSIS
*/
typedef struct _cl_disp_reg_info {
	cl_list_item_t list_item;
	cl_pfn_msgrcv_cb_t pfn_rcv_callback;
	const void *context;
	atomic32_t ref_cnt;
	cl_disp_msgid_t msg_id;
	cl_dispatcher_t *p_disp;
} cl_disp_reg_info_t;
/*
* FIELDS
*	pfn_rcv_callback
*		Client's message receive callback.
*
*	context
*		Client's context for message receive callback.
*
*	rcv_thread_count
*		Number of threads currently in the receive callback.
*
*	msg_done_thread_count
*		Number of threads currently in the message done callback.
*
*	state
*		State of this registration object.
*			DISP_REGSTATE_INIT: initialized and inactive
*			DISP_REGSTATE_ACTIVE: in active use
*			DISP_REGSTATE_UNREGPEND: unregistration is pending
*
*	msg_id
*		Dispatcher message msg_id value for this registration object.
*
*	p_disp
*		Pointer to parent Dispatcher.
*
* SEE ALSO
*********/

/****s* Component Library: Dispatcher/cl_disp_msg_t
* NAME
*	cl_disp_msg_t
*
* DESCRIPTION
*	Defines the dispatcher message structure.
*
*	The cl_disp_msg_t structure is for internal use by the
*	Dispatcher only.
*
* SYNOPSIS
*/
typedef struct _cl_disp_msg {
	cl_pool_item_t item;
	const void *p_data;
	cl_disp_reg_info_t *p_src_reg;
	cl_disp_reg_info_t *p_dest_reg;
	cl_pfn_msgdone_cb_t pfn_xmt_callback;
	uint64_t in_time;
	const void *context;
} cl_disp_msg_t;
/*
* FIELDS
*	item
*		List & Pool linkage.  Must be first element in the structure!!
*
*	msg_id
*		The message's numberic ID value.
*
*	p_data
*		Pointer to the data payload for this message.  The payload
*		is opaque to the Dispatcher.
*
*	p_reg_info
*		Pointer to the registration info of the sender.
*
*	pfn_xmt_callback
*		Client's message done callback.
*
*       in_time
*               The absolute time the message was inserted into the queue
*
*	context
*		Client's message done callback context.
*
* SEE ALSO
*********/

/****s* Component Library: Dispatcher/cl_disp_reg_info_t
* NAME
*	cl_disp_reg_info_t
*
* DESCRIPTION
*	Defines the Dispatcher registration handle.  This handle
*	should be treated as opaque by the client.
*
* SYNOPSIS
*/
typedef const struct _cl_disp_reg_info *cl_disp_reg_handle_t;
/**********/

/****f* Component Library: Dispatcher/cl_disp_construct
* NAME
*	cl_disp_construct
*
* DESCRIPTION
*	This function constructs a Dispatcher object.
*
* SYNOPSIS
*/
void cl_disp_construct(IN cl_dispatcher_t * const p_disp);
/*
* PARAMETERS
*	p_disp
*		[in] Pointer to a Dispatcher.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_disp_init and cl_disp_destroy.
*
* SEE ALSO
*	Dispatcher, cl_disp_init, cl_disp_destroy
*********/

/****f* Component Library: Dispatcher/cl_disp_init
* NAME
*	cl_disp_init
*
* DESCRIPTION
*	This function initializes a Dispatcher object.
*
* SYNOPSIS
*/
cl_status_t
cl_disp_init(IN cl_dispatcher_t * const p_disp,
	     IN const uint32_t thread_count, IN const char *const name);
/*
* PARAMETERS
*	p_disp
*		[in] Pointer to a Dispatcher.
*
*	thread_count
*		[in] The number of worker threads to create in this Dispatcher.
*		A value of 0 causes the Dispatcher to create one worker thread
*		per CPU in the system.  When the Dispatcher is created with
*		only one thread, the Dispatcher guarantees to deliver posted
*		messages in order.  When the Dispatcher is created with more
*		than one thread, messages may be delivered out of order.
*
*	name
*		[in] Name to associate with the threads.  The name may be up to 16
*		characters, including a terminating null character.  All threads
*		created in the Dispatcher have the same name.
*
* RETURN VALUE
*	CL_SUCCESS if the operation is successful.
*
* SEE ALSO
*	Dispatcher, cl_disp_destoy, cl_disp_register, cl_disp_unregister,
*	cl_disp_post
*********/

/****f* Component Library: Dispatcher/cl_disp_shutdown
* NAME
*	cl_disp_shutdown
*
* DESCRIPTION
*	This function shutdown a Dispatcher object. So it unreg all messages and
*  clears the fifo and waits for the threads to exit
*
* SYNOPSIS
*/
void cl_disp_shutdown(IN cl_dispatcher_t * const p_disp);
/*
* PARAMETERS
*	p_disp
*		[in] Pointer to a Dispatcher.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function does not returns until all worker threads
*	have exited client callback functions and been successfully
*	shutdowned.
*
* SEE ALSO
*	Dispatcher, cl_disp_construct, cl_disp_init
*********/

/****f* Component Library: Dispatcher/cl_disp_destroy
* NAME
*	cl_disp_destroy
*
* DESCRIPTION
*	This function destroys a Dispatcher object.
*
* SYNOPSIS
*/
void cl_disp_destroy(IN cl_dispatcher_t * const p_disp);
/*
* PARAMETERS
*	p_disp
*		[in] Pointer to a Dispatcher.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	Dispatcher, cl_disp_construct, cl_disp_init
*********/

/****f* Component Library: Dispatcher/cl_disp_register
* NAME
*	cl_disp_register
*
* DESCRIPTION
*	This function registers a client with a Dispatcher object.
*
* SYNOPSIS
*/
cl_disp_reg_handle_t
cl_disp_register(IN cl_dispatcher_t * const p_disp,
		 IN const cl_disp_msgid_t msg_id,
		 IN cl_pfn_msgrcv_cb_t pfn_callback OPTIONAL,
		 IN const void *const context);
/*
* PARAMETERS
*	p_disp
*		[in] Pointer to a Dispatcher.
*
*	msg_id
*		[in] Numberic message ID for which the client is registering.
*		If the client does not wish to receive any messages,
*		(a send-only client) then the caller should set this value
*		to CL_DISP_MSGID_NONE.  For efficiency, numeric message msg_id
*		values should start with 0 and should be contiguous, or nearly so.
*
*	pfn_callback
*		[in] Message receive callback.  The Dispatcher calls this
*		function after receiving a posted message with the
*		appropriate message msg_id value.  Send-only clients may specify
*		NULL for this value.
*
*	context
*		[in] Client context value passed to the cl_pfn_msgrcv_cb_t
*		function.
*
* RETURN VALUE
*	On success a Dispatcher registration handle.
*	CL_CL_DISP_INVALID_HANDLE otherwise.
*
* SEE ALSO
*	Dispatcher, cl_disp_unregister, cl_disp_post
*********/

/****f* Component Library: Dispatcher/cl_disp_unregister
* NAME
*	cl_disp_unregister
*
* DESCRIPTION
*	This function unregisters a client from a Dispatcher.
*
* SYNOPSIS
*/
void cl_disp_unregister(IN const cl_disp_reg_handle_t handle);
/*
* PARAMETERS
*	handle
*		[in] cl_disp_reg_handle_t value return by cl_disp_register.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function will not return until worker threads have exited
*	the callback functions for this client.  Do not invoke this
*	function from a callback.
*
* SEE ALSO
*	Dispatcher, cl_disp_register
*********/

/****f* Component Library: Dispatcher/cl_disp_post
* NAME
*	cl_disp_post
*
* DESCRIPTION
*	This function posts a message to a Dispatcher object.
*
* SYNOPSIS
*/
cl_status_t
cl_disp_post(IN const cl_disp_reg_handle_t handle,
	     IN const cl_disp_msgid_t msg_id,
	     IN const void *const p_data,
	     IN cl_pfn_msgdone_cb_t pfn_callback OPTIONAL,
	     IN const void *const context);
/*
* PARAMETERS
*	handle
*		[in] cl_disp_reg_handle_t value return by cl_disp_register.
*
*	msg_id
*		[in] Numeric message msg_id value associated with this message.
*
*	p_data
*		[in] Data payload for this message.
*
*	pfn_callback
*		[in] Pointer to a cl_pfn_msgdone_cb_t function.
*		The Dispatcher calls this function after the message has been
*		processed by the recipient.
*		The caller may pass NULL for this value, which indicates no
*		message done callback is necessary.
*
*	context
*		[in] Client context value passed to the cl_pfn_msgdone_cb_t
*		function.
*
* RETURN VALUE
*	CL_SUCCESS if the message was successfully queued in the Dispatcher.
*
* NOTES
*	The caller must not modify the memory pointed to by p_data until
*	the Dispatcher call the pfn_callback function.
*
* SEE ALSO
*	Dispatcher
*********/

/****f* Component Library: Dispatcher/cl_disp_get_queue_status
* NAME
*	cl_disp_get_queue_status
*
* DESCRIPTION
*	This function posts a message to a Dispatcher object.
*
* SYNOPSIS
*/
void
cl_disp_get_queue_status(IN const cl_disp_reg_handle_t handle,
			 OUT uint32_t * p_num_queued_msgs,
			 OUT uint64_t * p_last_msg_queue_time_ms);
/*
* PARAMETERS
*   handle
*     [in] cl_disp_reg_handle_t value return by cl_disp_register.
*
*   p_last_msg_queue_time_ms
*     [out] pointer to a variable to hold the time the last popped up message
*           spent in the queue
*
*   p_num_queued_msgs
*     [out] number of messages in the queue
*
* RETURN VALUE
*	Thr time the last popped up message stayed in the queue, in msec
*
* NOTES
*	Extarnel Locking is not required.
*
* SEE ALSO
*	Dispatcher
*********/

END_C_DECLS
#endif				/* !defined(_CL_DISPATCHER_H_) */
