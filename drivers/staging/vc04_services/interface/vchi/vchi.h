/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHI_H_
#define VCHI_H_

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
extern int vchi_service_open(struct vchiq_instance *instance,
			    struct vchiq_service_params *params,
			    unsigned *handle);

extern int32_t vchi_get_peer_version(unsigned handle, short *peer_version);

// Routine to close a named service
extern int32_t vchi_service_close(unsigned handle);

// Routine to increment ref count on a named service
extern int32_t vchi_service_use(unsigned handle);

// Routine to decrement ref count on a named service
extern int32_t vchi_service_release(unsigned handle);

/* Routine to send a message from kernel memory across a service */
extern int vchi_queue_kernel_message(unsigned handle, void *data,
				     unsigned int size);

// Routine to look at a message in place.
// The message is dequeued, so the caller is left holding it; the descriptor is
// filled in and must be released when the user has finished with the message.
struct vchiq_header *vchi_msg_hold(unsigned handle);

/*******************************************************************************
 * Global service support API - operations on held messages
 * and message iterators
 ******************************************************************************/

// Routine to release a held message after it has been processed
extern int32_t vchi_held_msg_release(unsigned handle, struct vchiq_header *message);

/******************************************************************************
 * Configuration plumbing
 *****************************************************************************/

#endif /* VCHI_H_ */

/****************************** End of file **********************************/
