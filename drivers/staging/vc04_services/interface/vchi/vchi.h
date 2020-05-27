/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHI_H_
#define VCHI_H_

#include "vchi_cfg.h"
#include "vchi_common.h"

/******************************************************************************
 * Global defs
 *****************************************************************************/

#define VCHI_BULK_ROUND_UP(x)     ((((unsigned long)(x)) + VCHI_BULK_ALIGN - 1) & ~(VCHI_BULK_ALIGN - 1))
#define VCHI_BULK_ROUND_DOWN(x)   (((unsigned long)(x)) & ~(VCHI_BULK_ALIGN - 1))
#define VCHI_BULK_ALIGN_NBYTES(x) (VCHI_BULK_ALIGNED(x) ? 0 : (VCHI_BULK_ALIGN - ((unsigned long)(x) & (VCHI_BULK_ALIGN - 1))))

#ifdef USE_VCHIQ_ARM
#define VCHI_BULK_ALIGNED(x)      1
#else
#define VCHI_BULK_ALIGNED(x)      (((unsigned long)(x) & (VCHI_BULK_ALIGN - 1)) == 0)
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
	vchi_callback callback;
	void *callback_param;
};

// Opaque handle for a VCHI instance
struct vchi_instance_handle;

// Opaque handle for a server or client
struct vchi_service_handle;

/******************************************************************************
 * Global funcs - implementation is specific to which side you are on
 * (local / remote)
 *****************************************************************************/

// Routine used to initialise the vchi on both local + remote connections
extern int32_t vchi_initialise(struct vchi_instance_handle **instance_handle);

extern int32_t vchi_connect(struct vchi_instance_handle *instance_handle);

//When this is called, ensure that all services have no data pending.
//Bulk transfers can remain 'queued'
extern int32_t vchi_disconnect(struct vchi_instance_handle *instance_handle);

/******************************************************************************
 * Global service API
 *****************************************************************************/
// Routine to open a named service
extern int32_t vchi_service_open(struct vchi_instance_handle *instance_handle,
				 struct service_creation *setup,
				 struct vchi_service_handle **handle);

extern int32_t vchi_get_peer_version(const struct vchi_service_handle *handle,
				     short *peer_version);

// Routine to close a named service
extern int32_t vchi_service_close(const struct vchi_service_handle *handle);

// Routine to increment ref count on a named service
extern int32_t vchi_service_use(const struct vchi_service_handle *handle);

// Routine to decrement ref count on a named service
extern int32_t vchi_service_release(const struct vchi_service_handle *handle);

/* Routine to send a message from kernel memory across a service */
extern int
vchi_queue_kernel_message(struct vchi_service_handle *handle,
			  void *data,
			  unsigned int size);

// Routine to receive a msg from a service
// Dequeue is equivalent to hold, copy into client buffer, release
extern int32_t vchi_msg_dequeue(struct vchi_service_handle *handle,
				void *data,
				uint32_t max_data_size_to_read,
				uint32_t *actual_msg_size,
				enum vchi_flags flags);

// Routine to look at a message in place.
// The message is not dequeued, so a subsequent call to peek or dequeue
// will return the same message.
extern int32_t vchi_msg_peek(struct vchi_service_handle *handle,
			     void **data,
			     uint32_t *msg_size,
			     enum vchi_flags flags);

// Routine to remove a message after it has been read in place with peek
// The first message on the queue is dequeued.
extern int32_t vchi_msg_remove(struct vchi_service_handle *handle);

// Routine to look at a message in place.
// The message is dequeued, so the caller is left holding it; the descriptor is
// filled in and must be released when the user has finished with the message.
extern int32_t vchi_msg_hold(struct vchi_service_handle *handle,
			     void **data,        // } may be NULL, as info can be
			     uint32_t *msg_size, // } obtained from HELD_MSG_T
			     enum vchi_flags flags,
			     struct vchi_held_msg *message_descriptor);

/*******************************************************************************
 * Global service support API - operations on held messages
 * and message iterators
 ******************************************************************************/

// Routine to release a held message after it has been processed
extern int32_t vchi_held_msg_release(struct vchi_held_msg *message);

/******************************************************************************
 * Global bulk API
 *****************************************************************************/

// Routine to prepare interface for a transfer from the other side
extern int32_t vchi_bulk_queue_receive(struct vchi_service_handle *handle,
				       void *data_dst,
				       uint32_t data_size,
				       enum vchi_flags flags,
				       void *transfer_handle);

// Routine to queue up data ready for transfer to the other (once they have signalled they are ready)
extern int32_t vchi_bulk_queue_transmit(struct vchi_service_handle *handle,
					const void *data_src,
					uint32_t data_size,
					enum vchi_flags flags,
					void *transfer_handle);

/******************************************************************************
 * Configuration plumbing
 *****************************************************************************/

#endif /* VCHI_H_ */

/****************************** End of file **********************************/
