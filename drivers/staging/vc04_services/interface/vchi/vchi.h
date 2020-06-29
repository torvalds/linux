/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHI_H_
#define VCHI_H_

#include "vchi_cfg.h"
#include "vchi_common.h"

/******************************************************************************
 * Global defs
 *****************************************************************************/

struct vchi_version {
	uint32_t version;
	uint32_t version_min;
};
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

// Opaque handle for a VCHIQ instance
struct vchiq_instance;

// Opaque handle for a server or client
struct vchi_service;

/******************************************************************************
 * Global funcs - implementation is specific to which side you are on
 * (local / remote)
 *****************************************************************************/

// Routine used to initialise the vchi on both local + remote connections
extern int32_t vchi_initialise(struct vchiq_instance **instance);

extern int32_t vchi_connect(struct vchiq_instance *instance);

//When this is called, ensure that all services have no data pending.
//Bulk transfers can remain 'queued'
extern int32_t vchi_disconnect(struct vchiq_instance *instance);

/******************************************************************************
 * Global service API
 *****************************************************************************/
// Routine to open a named service
extern int32_t vchi_service_open(struct vchiq_instance *instance,
				 struct service_creation *setup,
				 struct vchi_service **service);

extern int32_t vchi_get_peer_version(struct vchi_service *service,
				     short *peer_version);

// Routine to close a named service
extern int32_t vchi_service_close(struct vchi_service *service);

// Routine to increment ref count on a named service
extern int32_t vchi_service_use(struct vchi_service *service);

// Routine to decrement ref count on a named service
extern int32_t vchi_service_release(struct vchi_service *service);

/* Routine to send a message from kernel memory across a service */
extern int vchi_queue_kernel_message(struct vchi_service *service, void *data,
				     unsigned int size);

// Routine to look at a message in place.
// The message is dequeued, so the caller is left holding it; the descriptor is
// filled in and must be released when the user has finished with the message.
extern int32_t vchi_msg_hold(struct vchi_service *service,
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
extern int32_t vchi_bulk_queue_receive(struct vchi_service *service,
				       void *data_dst,
				       uint32_t data_size,
				       enum vchi_flags flags,
				       void *transfer_handle);

// Routine to queue up data ready for transfer to the other (once they have signalled they are ready)
extern int32_t vchi_bulk_queue_transmit(struct vchi_service *service,
					const void *data_src,
					uint32_t data_size,
					enum vchi_flags flags,
					void *transfer_handle);

/******************************************************************************
 * Configuration plumbing
 *****************************************************************************/

#endif /* VCHI_H_ */

/****************************** End of file **********************************/
