/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef VCHI_H_
#define VCHI_H_

#include "interface/vchi/vchi_cfg.h"
#include "interface/vchi/vchi_common.h"

/******************************************************************************
 Global defs
 *****************************************************************************/

#define VCHI_BULK_ROUND_UP(x)     ((((unsigned long)(x))+VCHI_BULK_ALIGN-1) & ~(VCHI_BULK_ALIGN-1))
#define VCHI_BULK_ROUND_DOWN(x)   (((unsigned long)(x)) & ~(VCHI_BULK_ALIGN-1))
#define VCHI_BULK_ALIGN_NBYTES(x) (VCHI_BULK_ALIGNED(x) ? 0 : (VCHI_BULK_ALIGN - ((unsigned long)(x) & (VCHI_BULK_ALIGN-1))))

#ifdef USE_VCHIQ_ARM
#define VCHI_BULK_ALIGNED(x)      1
#else
#define VCHI_BULK_ALIGNED(x)      (((unsigned long)(x) & (VCHI_BULK_ALIGN-1)) == 0)
#endif

struct vchi_version {
	uint32_t version;
	uint32_t version_min;
};
#define VCHI_VERSION(v_) { v_, v_ }
#define VCHI_VERSION_EX(v_, m_) { v_, m_ }

// Macros to manipulate 'FOURCC' values
#define MAKE_FOURCC(x) ((int32_t)((x[0] << 24) | (x[1] << 16) | (x[2] << 8) | x[3]))

// Opaque service information
struct opaque_vchi_service_t;

// Descriptor for a held message. Allocated by client, initialised by vchi_msg_hold,
// vchi_msg_iter_hold or vchi_msg_iter_hold_next. Fields are for internal VCHI use only.
struct vchi_held_msg {
	struct opaque_vchi_service_t *service;
	void *message;
};

// structure used to provide the information needed to open a server or a client
struct service_creation {
	struct vchi_version version;
	int32_t service_id;
	VCHI_CALLBACK_T callback;
	void *callback_param;
};

// Opaque handle for a VCHI instance
typedef struct opaque_vchi_instance_handle_t *VCHI_INSTANCE_T;

// Opaque handle for a server or client
typedef struct opaque_vchi_service_handle_t *VCHI_SERVICE_HANDLE_T;

/******************************************************************************
 Global funcs - implementation is specific to which side you are on (local / remote)
 *****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

// Routine used to initialise the vchi on both local + remote connections
extern int32_t vchi_initialise(VCHI_INSTANCE_T *instance_handle);

extern int32_t vchi_exit(void);

extern int32_t vchi_connect(VCHI_INSTANCE_T instance_handle);

//When this is called, ensure that all services have no data pending.
//Bulk transfers can remain 'queued'
extern int32_t vchi_disconnect(VCHI_INSTANCE_T instance_handle);

// helper functions
extern void *vchi_allocate_buffer(VCHI_SERVICE_HANDLE_T handle, uint32_t *length);
extern void vchi_free_buffer(VCHI_SERVICE_HANDLE_T handle, void *address);
extern uint32_t vchi_current_time(VCHI_INSTANCE_T instance_handle);

/******************************************************************************
 Global service API
 *****************************************************************************/
// Routine to destroy a service
extern int32_t vchi_service_destroy(const VCHI_SERVICE_HANDLE_T handle);

// Routine to open a named service
extern int32_t vchi_service_open(VCHI_INSTANCE_T instance_handle,
				 struct service_creation *setup,
				 VCHI_SERVICE_HANDLE_T *handle);

extern int32_t vchi_get_peer_version(const VCHI_SERVICE_HANDLE_T handle,
				     short *peer_version);

// Routine to close a named service
extern int32_t vchi_service_close(const VCHI_SERVICE_HANDLE_T handle);

// Routine to increment ref count on a named service
extern int32_t vchi_service_use(const VCHI_SERVICE_HANDLE_T handle);

// Routine to decrement ref count on a named service
extern int32_t vchi_service_release(const VCHI_SERVICE_HANDLE_T handle);

// Routine to set a control option for a named service
extern int32_t vchi_service_set_option(const VCHI_SERVICE_HANDLE_T handle,
					VCHI_SERVICE_OPTION_T option,
					int value);

/* Routine to send a message from kernel memory across a service */
extern int
vchi_queue_kernel_message(VCHI_SERVICE_HANDLE_T handle,
			  void *data,
			  unsigned int size);

/* Routine to send a message from user memory across a service */
extern int
vchi_queue_user_message(VCHI_SERVICE_HANDLE_T handle,
			void __user *data,
			unsigned int size);

// Routine to receive a msg from a service
// Dequeue is equivalent to hold, copy into client buffer, release
extern int32_t vchi_msg_dequeue(VCHI_SERVICE_HANDLE_T handle,
				void *data,
				uint32_t max_data_size_to_read,
				uint32_t *actual_msg_size,
				VCHI_FLAGS_T flags);

// Routine to look at a message in place.
// The message is not dequeued, so a subsequent call to peek or dequeue
// will return the same message.
extern int32_t vchi_msg_peek(VCHI_SERVICE_HANDLE_T handle,
			     void **data,
			     uint32_t *msg_size,
			     VCHI_FLAGS_T flags);

// Routine to remove a message after it has been read in place with peek
// The first message on the queue is dequeued.
extern int32_t vchi_msg_remove(VCHI_SERVICE_HANDLE_T handle);

// Routine to look at a message in place.
// The message is dequeued, so the caller is left holding it; the descriptor is
// filled in and must be released when the user has finished with the message.
extern int32_t vchi_msg_hold(VCHI_SERVICE_HANDLE_T handle,
			     void **data,        // } may be NULL, as info can be
			     uint32_t *msg_size, // } obtained from HELD_MSG_T
			     VCHI_FLAGS_T flags,
			     struct vchi_held_msg *message_descriptor);

// Initialise an iterator to look through messages in place
extern int32_t vchi_msg_look_ahead(VCHI_SERVICE_HANDLE_T handle,
				   struct vchi_msg_iter *iter,
				   VCHI_FLAGS_T flags);

/******************************************************************************
 Global service support API - operations on held messages and message iterators
 *****************************************************************************/

// Routine to get the address of a held message
extern void *vchi_held_msg_ptr(const struct vchi_held_msg *message);

// Routine to get the size of a held message
extern int32_t vchi_held_msg_size(const struct vchi_held_msg *message);

// Routine to get the transmit timestamp as written into the header by the peer
extern uint32_t vchi_held_msg_tx_timestamp(const struct vchi_held_msg *message);

// Routine to get the reception timestamp, written as we parsed the header
extern uint32_t vchi_held_msg_rx_timestamp(const struct vchi_held_msg *message);

// Routine to release a held message after it has been processed
extern int32_t vchi_held_msg_release(struct vchi_held_msg *message);

// Indicates whether the iterator has a next message.
extern int32_t vchi_msg_iter_has_next(const struct vchi_msg_iter *iter);

// Return the pointer and length for the next message and advance the iterator.
extern int32_t vchi_msg_iter_next(struct vchi_msg_iter *iter,
				  void **data,
				  uint32_t *msg_size);

// Remove the last message returned by vchi_msg_iter_next.
// Can only be called once after each call to vchi_msg_iter_next.
extern int32_t vchi_msg_iter_remove(struct vchi_msg_iter *iter);

// Hold the last message returned by vchi_msg_iter_next.
// Can only be called once after each call to vchi_msg_iter_next.
extern int32_t vchi_msg_iter_hold(struct vchi_msg_iter *iter,
				  struct vchi_held_msg *message);

// Return information for the next message, and hold it, advancing the iterator.
extern int32_t vchi_msg_iter_hold_next(struct vchi_msg_iter *iter,
				       void **data,        // } may be NULL
				       uint32_t *msg_size, // }
				       struct vchi_held_msg *message);

/******************************************************************************
 Global bulk API
 *****************************************************************************/

// Routine to prepare interface for a transfer from the other side
extern int32_t vchi_bulk_queue_receive(VCHI_SERVICE_HANDLE_T handle,
				       void *data_dst,
				       uint32_t data_size,
				       VCHI_FLAGS_T flags,
				       void *transfer_handle);

// Prepare interface for a transfer from the other side into relocatable memory.
int32_t vchi_bulk_queue_receive_reloc(const VCHI_SERVICE_HANDLE_T handle,
				      uint32_t offset,
				      uint32_t data_size,
				      const VCHI_FLAGS_T flags,
				      void * const bulk_handle);

// Routine to queue up data ready for transfer to the other (once they have signalled they are ready)
extern int32_t vchi_bulk_queue_transmit(VCHI_SERVICE_HANDLE_T handle,
					const void *data_src,
					uint32_t data_size,
					VCHI_FLAGS_T flags,
					void *transfer_handle);

/******************************************************************************
 Configuration plumbing
 *****************************************************************************/

#ifdef __cplusplus
}
#endif

extern int32_t vchi_bulk_queue_transmit_reloc(VCHI_SERVICE_HANDLE_T handle,
					      uint32_t offset,
					      uint32_t data_size,
					      VCHI_FLAGS_T flags,
					      void *transfer_handle);
#endif /* VCHI_H_ */

/****************************** End of file **********************************/
