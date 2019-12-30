// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */
#include <linux/module.h>
#include <linux/types.h>

#include "interface/vchi/vchi.h"
#include "vchiq.h"
#include "vchiq_core.h"

#include "vchiq_util.h"

#define vchiq_status_to_vchi(status) ((int32_t)status)

struct shim_service {
	unsigned int handle;

	struct vchiu_queue queue;

	vchi_callback callback;
	void *callback_param;
};

/***********************************************************
 * Name: vchi_msg_peek
 *
 * Arguments:  struct vchi_service_handle *handle,
 *             void **data,
 *             uint32_t *msg_size,

 *             enum vchi_flags flags
 *
 * Description: Routine to return a pointer to the current message (to allow in
 *              place processing). The message can be removed using
 *              vchi_msg_remove when you're finished
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
int32_t vchi_msg_peek(struct vchi_service_handle *handle,
		      void **data,
		      uint32_t *msg_size,
		      enum vchi_flags flags)
{
	struct shim_service *service = (struct shim_service *)handle;
	struct vchiq_header *header;

	WARN_ON((flags != VCHI_FLAGS_NONE) &&
		(flags != VCHI_FLAGS_BLOCK_UNTIL_OP_COMPLETE));

	if (flags == VCHI_FLAGS_NONE)
		if (vchiu_queue_is_empty(&service->queue))
			return -1;

	header = vchiu_queue_peek(&service->queue);

	*data = header->data;
	*msg_size = header->size;

	return 0;
}
EXPORT_SYMBOL(vchi_msg_peek);

/***********************************************************
 * Name: vchi_msg_remove
 *
 * Arguments:  struct vchi_service_handle *handle,
 *
 * Description: Routine to remove a message (after it has been read with
 *              vchi_msg_peek)
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
int32_t vchi_msg_remove(struct vchi_service_handle *handle)
{
	struct shim_service *service = (struct shim_service *)handle;
	struct vchiq_header *header;

	header = vchiu_queue_pop(&service->queue);

	vchiq_release_message(service->handle, header);

	return 0;
}
EXPORT_SYMBOL(vchi_msg_remove);

/***********************************************************
 * Name: vchi_msg_queue
 *
 * Arguments:  struct vchi_service_handle *handle,
 *             ssize_t (*copy_callback)(void *context, void *dest,
 *				        size_t offset, size_t maxsize),
 *	       void *context,
 *             uint32_t data_size
 *
 * Description: Thin wrapper to queue a message onto a connection
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
static
int32_t vchi_msg_queue(struct vchi_service_handle *handle,
	ssize_t (*copy_callback)(void *context, void *dest,
				 size_t offset, size_t maxsize),
	void *context,
	uint32_t data_size)
{
	struct shim_service *service = (struct shim_service *)handle;
	enum vchiq_status status;

	while (1) {
		status = vchiq_queue_message(service->handle,
					     copy_callback,
					     context,
					     data_size);

		/*
		 * vchiq_queue_message() may return VCHIQ_RETRY, so we need to
		 * implement a retry mechanism since this function is supposed
		 * to block until queued
		 */
		if (status != VCHIQ_RETRY)
			break;

		msleep(1);
	}

	return vchiq_status_to_vchi(status);
}

static ssize_t
vchi_queue_kernel_message_callback(void *context,
				   void *dest,
				   size_t offset,
				   size_t maxsize)
{
	memcpy(dest, context + offset, maxsize);
	return maxsize;
}

int
vchi_queue_kernel_message(struct vchi_service_handle *handle,
			  void *data,
			  unsigned int size)
{
	return vchi_msg_queue(handle,
			      vchi_queue_kernel_message_callback,
			      data,
			      size);
}
EXPORT_SYMBOL(vchi_queue_kernel_message);

struct vchi_queue_user_message_context {
	void __user *data;
};

static ssize_t
vchi_queue_user_message_callback(void *context,
				 void *dest,
				 size_t offset,
				 size_t maxsize)
{
	struct vchi_queue_user_message_context *copycontext = context;

	if (copy_from_user(dest, copycontext->data + offset, maxsize))
		return -EFAULT;

	return maxsize;
}

int
vchi_queue_user_message(struct vchi_service_handle *handle,
			void __user *data,
			unsigned int size)
{
	struct vchi_queue_user_message_context copycontext = {
		.data = data
	};

	return vchi_msg_queue(handle,
			      vchi_queue_user_message_callback,
			      &copycontext,
			      size);
}
EXPORT_SYMBOL(vchi_queue_user_message);

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
int32_t vchi_bulk_queue_receive(struct vchi_service_handle *handle, void *data_dst,
				uint32_t data_size, enum vchi_flags flags,
				void *bulk_handle)
{
	struct shim_service *service = (struct shim_service *)handle;
	enum vchiq_bulk_mode mode;
	enum vchiq_status status;

	switch ((int)flags) {
	case VCHI_FLAGS_CALLBACK_WHEN_OP_COMPLETE
		| VCHI_FLAGS_BLOCK_UNTIL_QUEUED:
		WARN_ON(!service->callback);
		mode = VCHIQ_BULK_MODE_CALLBACK;
		break;
	case VCHI_FLAGS_BLOCK_UNTIL_OP_COMPLETE:
		mode = VCHIQ_BULK_MODE_BLOCKING;
		break;
	case VCHI_FLAGS_BLOCK_UNTIL_QUEUED:
	case VCHI_FLAGS_NONE:
		mode = VCHIQ_BULK_MODE_NOCALLBACK;
		break;
	default:
		WARN(1, "unsupported message\n");
		return vchiq_status_to_vchi(VCHIQ_ERROR);
	}

	while (1) {
		status = vchiq_bulk_receive(service->handle, data_dst,
			data_size, bulk_handle, mode);
		/*
		 * vchiq_bulk_receive() may return VCHIQ_RETRY, so we need to
		 * implement a retry mechanism since this function is supposed
		 * to block until queued
		 */
		if (status != VCHIQ_RETRY)
			break;

		msleep(1);
	}

	return vchiq_status_to_vchi(status);
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
int32_t vchi_bulk_queue_transmit(struct vchi_service_handle *handle,
				 const void *data_src,
				 uint32_t data_size,
				 enum vchi_flags flags,
				 void *bulk_handle)
{
	struct shim_service *service = (struct shim_service *)handle;
	enum vchiq_bulk_mode mode;
	enum vchiq_status status;

	switch ((int)flags) {
	case VCHI_FLAGS_CALLBACK_WHEN_OP_COMPLETE
		| VCHI_FLAGS_BLOCK_UNTIL_QUEUED:
		WARN_ON(!service->callback);
		mode = VCHIQ_BULK_MODE_CALLBACK;
		break;
	case VCHI_FLAGS_BLOCK_UNTIL_DATA_READ:
	case VCHI_FLAGS_BLOCK_UNTIL_OP_COMPLETE:
		mode = VCHIQ_BULK_MODE_BLOCKING;
		break;
	case VCHI_FLAGS_BLOCK_UNTIL_QUEUED:
	case VCHI_FLAGS_NONE:
		mode = VCHIQ_BULK_MODE_NOCALLBACK;
		break;
	default:
		WARN(1, "unsupported message\n");
		return vchiq_status_to_vchi(VCHIQ_ERROR);
	}

	while (1) {
		status = vchiq_bulk_transmit(service->handle, data_src,
			data_size, bulk_handle, mode);

		/*
		 * vchiq_bulk_transmit() may return VCHIQ_RETRY, so we need to
		 * implement a retry mechanism since this function is supposed
		 * to block until queued
		 */
		if (status != VCHIQ_RETRY)
			break;

		msleep(1);
	}

	return vchiq_status_to_vchi(status);
}
EXPORT_SYMBOL(vchi_bulk_queue_transmit);

/***********************************************************
 * Name: vchi_msg_dequeue
 *
 * Arguments:  struct vchi_service_handle *handle,
 *             void *data,
 *             uint32_t max_data_size_to_read,
 *             uint32_t *actual_msg_size
 *             enum vchi_flags flags
 *
 * Description: Routine to dequeue a message into the supplied buffer
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
int32_t vchi_msg_dequeue(struct vchi_service_handle *handle, void *data,
			 uint32_t max_data_size_to_read,
			 uint32_t *actual_msg_size, enum vchi_flags flags)
{
	struct shim_service *service = (struct shim_service *)handle;
	struct vchiq_header *header;

	WARN_ON((flags != VCHI_FLAGS_NONE) &&
		(flags != VCHI_FLAGS_BLOCK_UNTIL_OP_COMPLETE));

	if (flags == VCHI_FLAGS_NONE)
		if (vchiu_queue_is_empty(&service->queue))
			return -1;

	header = vchiu_queue_pop(&service->queue);

	memcpy(data, header->data, header->size < max_data_size_to_read ?
		header->size : max_data_size_to_read);

	*actual_msg_size = header->size;

	vchiq_release_message(service->handle, header);

	return 0;
}
EXPORT_SYMBOL(vchi_msg_dequeue);

/***********************************************************
 * Name: vchi_held_msg_release
 *
 * Arguments:  struct vchi_held_msg *message
 *
 * Description: Routine to release a held message (after it has been read with
 *              vchi_msg_hold)
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
int32_t vchi_held_msg_release(struct vchi_held_msg *message)
{
	/*
	 * Convert the service field pointer back to an
	 * unsigned int which is an int.
	 * This pointer is opaque to everything except
	 * vchi_msg_hold which simply upcasted the int
	 * to a pointer.
	 */

	vchiq_release_message((unsigned int)(long)message->service,
			      (struct vchiq_header *)message->message);

	return 0;
}
EXPORT_SYMBOL(vchi_held_msg_release);

/***********************************************************
 * Name: vchi_msg_hold
 *
 * Arguments:  struct vchi_service_handle *handle,
 *             void **data,
 *             uint32_t *msg_size,
 *             enum vchi_flags flags,
 *             struct vchi_held_msg *message_handle
 *
 * Description: Routine to return a pointer to the current message (to allow
 *              in place processing). The message is dequeued - don't forget
 *              to release the message using vchi_held_msg_release when you're
 *              finished.
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/
int32_t vchi_msg_hold(struct vchi_service_handle *handle, void **data,
		      uint32_t *msg_size, enum vchi_flags flags,
		      struct vchi_held_msg *message_handle)
{
	struct shim_service *service = (struct shim_service *)handle;
	struct vchiq_header *header;

	WARN_ON((flags != VCHI_FLAGS_NONE) &&
		(flags != VCHI_FLAGS_BLOCK_UNTIL_OP_COMPLETE));

	if (flags == VCHI_FLAGS_NONE)
		if (vchiu_queue_is_empty(&service->queue))
			return -1;

	header = vchiu_queue_pop(&service->queue);

	*data = header->data;
	*msg_size = header->size;

	/*
	 * upcast the unsigned int which is an int
	 * to a pointer and stuff it in the held message.
	 * This pointer is opaque to everything except
	 * vchi_held_msg_release which simply downcasts it back
	 * to an int.
	 */

	message_handle->service =
		(struct opaque_vchi_service_t *)(long)service->handle;
	message_handle->message = header;

	return 0;
}
EXPORT_SYMBOL(vchi_msg_hold);

/***********************************************************
 * Name: vchi_initialise
 *
 * Arguments: struct vchi_instance_handle **instance_handle
 *
 * Description: Initialises the hardware but does not transmit anything
 *              When run as a Host App this will be called twice hence the need
 *              to malloc the state information
 *
 * Returns: 0 if successful, failure otherwise
 *
 ***********************************************************/

int32_t vchi_initialise(struct vchi_instance_handle **instance_handle)
{
	struct vchiq_instance *instance;
	enum vchiq_status status;

	status = vchiq_initialise(&instance);

	*instance_handle = (struct vchi_instance_handle *)instance;

	return vchiq_status_to_vchi(status);
}
EXPORT_SYMBOL(vchi_initialise);

/***********************************************************
 * Name: vchi_connect
 *
 * Arguments: struct vchi_instance_handle *instance_handle
 *
 * Description: Starts the command service on each connection,
 *              causing INIT messages to be pinged back and forth
 *
 * Returns: 0 if successful, failure otherwise
 *
 ***********************************************************/
int32_t vchi_connect(struct vchi_instance_handle *instance_handle)
{
	struct vchiq_instance *instance = (struct vchiq_instance *)instance_handle;

	return vchiq_connect(instance);
}
EXPORT_SYMBOL(vchi_connect);

/***********************************************************
 * Name: vchi_disconnect
 *
 * Arguments: struct vchi_instance_handle *instance_handle
 *
 * Description: Stops the command service on each connection,
 *              causing DE-INIT messages to be pinged back and forth
 *
 * Returns: 0 if successful, failure otherwise
 *
 ***********************************************************/
int32_t vchi_disconnect(struct vchi_instance_handle *instance_handle)
{
	struct vchiq_instance *instance = (struct vchiq_instance *)instance_handle;

	return vchiq_status_to_vchi(vchiq_shutdown(instance));
}
EXPORT_SYMBOL(vchi_disconnect);

/***********************************************************
 * Name: vchi_service_open
 * Name: vchi_service_create
 *
 * Arguments: struct vchi_instance_handle *instance_handle
 *            struct service_creation *setup,
 *            struct vchi_service_handle **handle
 *
 * Description: Routine to open a service
 *
 * Returns: int32_t - success == 0
 *
 ***********************************************************/

static enum vchiq_status shim_callback(enum vchiq_reason reason,
				    struct vchiq_header *header,
				    unsigned int handle,
				    void *bulk_user)
{
	struct shim_service *service =
		(struct shim_service *)VCHIQ_GET_SERVICE_USERDATA(handle);

	if (!service->callback)
		goto release;

	switch (reason) {
	case VCHIQ_MESSAGE_AVAILABLE:
		vchiu_queue_push(&service->queue, header);

		service->callback(service->callback_param,
				  VCHI_CALLBACK_MSG_AVAILABLE, NULL);

		goto done;

	case VCHIQ_BULK_TRANSMIT_DONE:
		service->callback(service->callback_param,
				  VCHI_CALLBACK_BULK_SENT, bulk_user);
		break;

	case VCHIQ_BULK_RECEIVE_DONE:
		service->callback(service->callback_param,
				  VCHI_CALLBACK_BULK_RECEIVED, bulk_user);
		break;

	case VCHIQ_SERVICE_CLOSED:
		service->callback(service->callback_param,
				  VCHI_CALLBACK_SERVICE_CLOSED, NULL);
		break;

	case VCHIQ_SERVICE_OPENED:
		/* No equivalent VCHI reason */
		break;

	case VCHIQ_BULK_TRANSMIT_ABORTED:
		service->callback(service->callback_param,
				  VCHI_CALLBACK_BULK_TRANSMIT_ABORTED,
				  bulk_user);
		break;

	case VCHIQ_BULK_RECEIVE_ABORTED:
		service->callback(service->callback_param,
				  VCHI_CALLBACK_BULK_RECEIVE_ABORTED,
				  bulk_user);
		break;

	default:
		WARN(1, "not supported\n");
		break;
	}

release:
	vchiq_release_message(service->handle, header);
done:
	return VCHIQ_SUCCESS;
}

static struct shim_service *service_alloc(struct vchiq_instance *instance,
	struct service_creation *setup)
{
	struct shim_service *service = kzalloc(sizeof(struct shim_service), GFP_KERNEL);

	(void)instance;

	if (service) {
		if (!vchiu_queue_init(&service->queue, 64)) {
			service->callback = setup->callback;
			service->callback_param = setup->callback_param;
		} else {
			kfree(service);
			service = NULL;
		}
	}

	return service;
}

static void service_free(struct shim_service *service)
{
	if (service) {
		vchiu_queue_delete(&service->queue);
		kfree(service);
	}
}

int32_t vchi_service_open(struct vchi_instance_handle *instance_handle,
	struct service_creation *setup,
	struct vchi_service_handle **handle)
{
	struct vchiq_instance *instance = (struct vchiq_instance *)instance_handle;
	struct shim_service *service = service_alloc(instance, setup);

	*handle = (struct vchi_service_handle *)service;

	if (service) {
		struct vchiq_service_params params;
		enum vchiq_status status;

		memset(&params, 0, sizeof(params));
		params.fourcc = setup->service_id;
		params.callback = shim_callback;
		params.userdata = service;
		params.version = setup->version.version;
		params.version_min = setup->version.version_min;

		status = vchiq_open_service(instance, &params,
			&service->handle);
		if (status != VCHIQ_SUCCESS) {
			service_free(service);
			service = NULL;
			*handle = NULL;
		}
	}

	return service ? 0 : -1;
}
EXPORT_SYMBOL(vchi_service_open);

int32_t vchi_service_close(const struct vchi_service_handle *handle)
{
	int32_t ret = -1;
	struct shim_service *service = (struct shim_service *)handle;

	if (service) {
		enum vchiq_status status = vchiq_close_service(service->handle);
		if (status == VCHIQ_SUCCESS)
			service_free(service);

		ret = vchiq_status_to_vchi(status);
	}
	return ret;
}
EXPORT_SYMBOL(vchi_service_close);

int32_t vchi_service_destroy(const struct vchi_service_handle *handle)
{
	int32_t ret = -1;
	struct shim_service *service = (struct shim_service *)handle;

	if (service) {
		enum vchiq_status status = vchiq_remove_service(service->handle);

		if (status == VCHIQ_SUCCESS) {
			service_free(service);
			service = NULL;
		}

		ret = vchiq_status_to_vchi(status);
	}
	return ret;
}
EXPORT_SYMBOL(vchi_service_destroy);

int32_t vchi_service_set_option(const struct vchi_service_handle *handle,
				enum vchi_service_option option,
				int value)
{
	int32_t ret = -1;
	struct shim_service *service = (struct shim_service *)handle;
	enum vchiq_service_option vchiq_option;

	switch (option) {
	case VCHI_SERVICE_OPTION_TRACE:
		vchiq_option = VCHIQ_SERVICE_OPTION_TRACE;
		break;
	case VCHI_SERVICE_OPTION_SYNCHRONOUS:
		vchiq_option = VCHIQ_SERVICE_OPTION_SYNCHRONOUS;
		break;
	default:
		service = NULL;
		break;
	}
	if (service) {
		enum vchiq_status status =
			vchiq_set_service_option(service->handle,
						vchiq_option,
						value);

		ret = vchiq_status_to_vchi(status);
	}
	return ret;
}
EXPORT_SYMBOL(vchi_service_set_option);

int32_t vchi_get_peer_version(const struct vchi_service_handle *handle, short *peer_version)
{
	int32_t ret = -1;
	struct shim_service *service = (struct shim_service *)handle;

	if (service) {
		enum vchiq_status status;

		status = vchiq_get_peer_version(service->handle, peer_version);
		ret = vchiq_status_to_vchi(status);
	}
	return ret;
}
EXPORT_SYMBOL(vchi_get_peer_version);

/***********************************************************
 * Name: vchi_service_use
 *
 * Arguments: const struct vchi_service_handle *handle
 *
 * Description: Routine to increment refcount on a service
 *
 * Returns: void
 *
 ***********************************************************/
int32_t vchi_service_use(const struct vchi_service_handle *handle)
{
	int32_t ret = -1;

	struct shim_service *service = (struct shim_service *)handle;
	if (service)
		ret = vchiq_status_to_vchi(vchiq_use_service(service->handle));
	return ret;
}
EXPORT_SYMBOL(vchi_service_use);

/***********************************************************
 * Name: vchi_service_release
 *
 * Arguments: const struct vchi_service_handle *handle
 *
 * Description: Routine to decrement refcount on a service
 *
 * Returns: void
 *
 ***********************************************************/
int32_t vchi_service_release(const struct vchi_service_handle *handle)
{
	int32_t ret = -1;

	struct shim_service *service = (struct shim_service *)handle;
	if (service)
		ret = vchiq_status_to_vchi(
			vchiq_release_service(service->handle));
	return ret;
}
EXPORT_SYMBOL(vchi_service_release);
