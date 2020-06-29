// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "vchiq_if.h"
#include "../vchi/vchi.h"
#include "vchiq.h"
#include "vchiq_core.h"

int vchi_queue_kernel_message(unsigned handle, void *data, unsigned int size)
{
	enum vchiq_status status;

	while (1) {
		status = vchiq_queue_kernel_message(handle, data, size);

		/*
		 * vchiq_queue_message() may return VCHIQ_RETRY, so we need to
		 * implement a retry mechanism since this function is supposed
		 * to block until queued
		 */
		if (status != VCHIQ_RETRY)
			break;

		msleep(1);
	}

	return status;
}
EXPORT_SYMBOL(vchi_queue_kernel_message);

/***********************************************************
 * Name: vchi_bulk_queue_receive
 *
 * Arguments:  VCHI_BULK_HANDLE_T handle,
 *             void *data_dst,
 *             const uint32_t data_size,
 *             enum vchi_flags flags
 *             void *bulk_handle
 *
 * Description: Routine to setup a rcv buffer
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
int32_t vchi_bulk_queue_receive(unsigned handle, void *data_dst,
				uint32_t data_size, enum vchiq_bulk_mode mode,
				void *bulk_handle)
{
	enum vchiq_status status;

	while (1) {
		status = vchiq_bulk_receive(handle, data_dst, data_size,
					    bulk_handle, mode);
		/*
		 * vchiq_bulk_receive() may return VCHIQ_RETRY, so we need to
		 * implement a retry mechanism since this function is supposed
		 * to block until queued
		 */
		if (status != VCHIQ_RETRY)
			break;

		msleep(1);
	}

	return status;
}
EXPORT_SYMBOL(vchi_bulk_queue_receive);

/***********************************************************
 * Name: vchi_bulk_queue_transmit
 *
 * Arguments:  VCHI_BULK_HANDLE_T handle,
 *             const void *data_src,
 *             uint32_t data_size,
 *             enum vchi_flags flags,
 *             void *bulk_handle
 *
 * Description: Routine to transmit some data
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
int32_t vchi_bulk_queue_transmit(unsigned handle, const void *data_src,
				 uint32_t data_size, enum vchiq_bulk_mode mode,
				 void *bulk_handle)
{
	enum vchiq_status status;

	while (1) {
		status = vchiq_bulk_transmit(handle, data_src, data_size,
					     bulk_handle, mode);

		/*
		 * vchiq_bulk_transmit() may return VCHIQ_RETRY, so we need to
		 * implement a retry mechanism since this function is supposed
		 * to block until queued
		 */
		if (status != VCHIQ_RETRY)
			break;

		msleep(1);
	}

	return status;
}
EXPORT_SYMBOL(vchi_bulk_queue_transmit);


/***********************************************************
 * Name: vchi_held_msg_release
 *
 * Arguments:  unsgined handle
 *	       struct vchiq_header *message
 *
 * Description: Routine to release a held message (after it has been read with
 *              vchi_msg_hold)
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
int32_t vchi_held_msg_release(unsigned handle, struct vchiq_header *message)
{
	/*
	 * Convert the service field pointer back to an
	 * unsigned int which is an int.
	 * This pointer is opaque to everything except
	 * vchi_msg_hold which simply upcasted the int
	 * to a pointer.
	 */

	vchiq_release_message(handle, message);

	return 0;
}
EXPORT_SYMBOL(vchi_held_msg_release);

/***********************************************************
 * Name: vchi_msg_hold
 *
 * Arguments:  struct vchi_service *service,
 *             void **data,
 *             uint32_t *msg_size,
 *             struct vchiq_header **message
 *
 * Description: Routine to return a pointer to the current message (to allow
 *              in place processing). The message is dequeued - don't forget
 *              to release the message using vchi_held_msg_release when you're
 *              finished.
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
struct vchiq_header *vchi_msg_hold(unsigned handle)
{
	return vchiq_msg_hold(handle);
}
EXPORT_SYMBOL(vchi_msg_hold);

/***********************************************************
 * Name: vchi_initialise
 *
 * Arguments: struct vchiq_instance **instance
 *
 * Description: Initialises the hardware but does not transmit anything
 *              When run as a Host App this will be called twice hence the need
 *              to malloc the state information
 *
 * Returns: 0 if successful, failure otherwise
 *
 ***********************************************************/

int32_t vchi_initialise(struct vchiq_instance **instance)
{
	return vchiq_initialise(instance);
}
EXPORT_SYMBOL(vchi_initialise);

/***********************************************************
 * Name: vchi_connect
 *
 * Arguments: struct vchiq_instance *instance
 *
 * Description: Starts the command service on each connection,
 *              causing INIT messages to be pinged back and forth
 *
 * Returns: 0 if successful, failure otherwise
 *
 ***********************************************************/
int32_t vchi_connect(struct vchiq_instance *instance)
{
	return vchiq_connect(instance);
}
EXPORT_SYMBOL(vchi_connect);

/***********************************************************
 * Name: vchi_disconnect
 *
 * Arguments: struct vchiq_instance *instance
 *
 * Description: Stops the command service on each connection,
 *              causing DE-INIT messages to be pinged back and forth
 *
 * Returns: 0 if successful, failure otherwise
 *
 ***********************************************************/
int32_t vchi_disconnect(struct vchiq_instance *instance)
{
	return vchiq_shutdown(instance);
}
EXPORT_SYMBOL(vchi_disconnect);

/***********************************************************
 * Name: vchi_service_open
 * Name: vchi_service_create
 *
 * Arguments: struct vchiq_instance *instance
 *            struct service_creation *setup,
 *            unsigned *handle
 *
 * Description: Routine to open a service
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/

int32_t vchi_service_open(struct vchiq_instance *instance,
		      struct vchiq_service_params *params,
		      unsigned *handle)
{
	return vchiq_open_service(instance, params, handle);
}
EXPORT_SYMBOL(vchi_service_open);

int32_t vchi_service_close(unsigned handle)
{
	return vchiq_close_service(handle);
}
EXPORT_SYMBOL(vchi_service_close);

int32_t vchi_get_peer_version(unsigned handle, short *peer_version)
{
	return vchiq_get_peer_version(handle, peer_version);
}
EXPORT_SYMBOL(vchi_get_peer_version);

/***********************************************************
 * Name: vchi_service_use
 *
 * Arguments: unsigned handle
 *
 * Description: Routine to increment refcount on a service
 *
 * Returns: void
 *
 ***********************************************************/
int32_t vchi_service_use(unsigned handle)
{
	return vchiq_use_service(handle);
}
EXPORT_SYMBOL(vchi_service_use);

/***********************************************************
 * Name: vchi_service_release
 *
 * Arguments: unsigned handle
 *
 * Description: Routine to decrement refcount on a service
 *
 * Returns: void
 *
 ***********************************************************/
int32_t vchi_service_release(unsigned handle)
{
	return vchiq_release_service(handle);
}
EXPORT_SYMBOL(vchi_service_release);
