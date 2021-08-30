// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2014 Raspberry Pi (Trading) Ltd. All rights reserved.
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/compat.h>
#include <linux/dma-mapping.h>
#include <linux/rcupdate.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <soc/bcm2835/raspberrypi-firmware.h>

#include "vchiq_core.h"
#include "vchiq_ioctl.h"
#include "vchiq_arm.h"
#include "vchiq_debugfs.h"

#define DEVICE_NAME "vchiq"

/* Override the default prefix, which would be vchiq_arm (from the filename) */
#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX DEVICE_NAME "."

/* Some per-instance constants */
#define MAX_COMPLETIONS 128
#define MAX_SERVICES 64
#define MAX_ELEMENTS 8
#define MSG_QUEUE_SIZE 128

#define KEEPALIVE_VER 1
#define KEEPALIVE_VER_MIN KEEPALIVE_VER

/* Run time control of log level, based on KERN_XXX level. */
int vchiq_arm_log_level = VCHIQ_LOG_DEFAULT;
int vchiq_susp_log_level = VCHIQ_LOG_ERROR;

struct user_service {
	struct vchiq_service *service;
	void __user *userdata;
	struct vchiq_instance *instance;
	char is_vchi;
	char dequeue_pending;
	char close_pending;
	int message_available_pos;
	int msg_insert;
	int msg_remove;
	struct completion insert_event;
	struct completion remove_event;
	struct completion close_event;
	struct vchiq_header *msg_queue[MSG_QUEUE_SIZE];
};

struct bulk_waiter_node {
	struct bulk_waiter bulk_waiter;
	int pid;
	struct list_head list;
};

struct vchiq_instance {
	struct vchiq_state *state;
	struct vchiq_completion_data_kernel completions[MAX_COMPLETIONS];
	int completion_insert;
	int completion_remove;
	struct completion insert_event;
	struct completion remove_event;
	struct mutex completion_mutex;

	int connected;
	int closing;
	int pid;
	int mark;
	int use_close_delivered;
	int trace;

	struct list_head bulk_waiter_list;
	struct mutex bulk_waiter_list_mutex;

	struct vchiq_debugfs_node debugfs_node;
};

struct dump_context {
	char __user *buf;
	size_t actual;
	size_t space;
	loff_t offset;
};

static struct cdev    vchiq_cdev;
static dev_t          vchiq_devid;
static struct vchiq_state g_state;
static struct class  *vchiq_class;
static DEFINE_SPINLOCK(msg_queue_spinlock);
static struct platform_device *bcm2835_camera;
static struct platform_device *bcm2835_audio;

static struct vchiq_drvdata bcm2835_drvdata = {
	.cache_line_size = 32,
};

static struct vchiq_drvdata bcm2836_drvdata = {
	.cache_line_size = 64,
};

static const char *const ioctl_names[] = {
	"CONNECT",
	"SHUTDOWN",
	"CREATE_SERVICE",
	"REMOVE_SERVICE",
	"QUEUE_MESSAGE",
	"QUEUE_BULK_TRANSMIT",
	"QUEUE_BULK_RECEIVE",
	"AWAIT_COMPLETION",
	"DEQUEUE_MESSAGE",
	"GET_CLIENT_ID",
	"GET_CONFIG",
	"CLOSE_SERVICE",
	"USE_SERVICE",
	"RELEASE_SERVICE",
	"SET_SERVICE_OPTION",
	"DUMP_PHYS_MEM",
	"LIB_VERSION",
	"CLOSE_DELIVERED"
};

static_assert(ARRAY_SIZE(ioctl_names) == (VCHIQ_IOC_MAX + 1));

static enum vchiq_status
vchiq_blocking_bulk_transfer(unsigned int handle, void *data,
	unsigned int size, enum vchiq_bulk_dir dir);

#define VCHIQ_INIT_RETRIES 10
int vchiq_initialise(struct vchiq_instance **instance_out)
{
	struct vchiq_state *state;
	struct vchiq_instance *instance = NULL;
	int i, ret;

	/*
	 * VideoCore may not be ready due to boot up timing.
	 * It may never be ready if kernel and firmware are mismatched,so don't
	 * block forever.
	 */
	for (i = 0; i < VCHIQ_INIT_RETRIES; i++) {
		state = vchiq_get_state();
		if (state)
			break;
		usleep_range(500, 600);
	}
	if (i == VCHIQ_INIT_RETRIES) {
		vchiq_log_error(vchiq_core_log_level,
			"%s: videocore not initialized\n", __func__);
		ret = -ENOTCONN;
		goto failed;
	} else if (i > 0) {
		vchiq_log_warning(vchiq_core_log_level,
			"%s: videocore initialized after %d retries\n",
			__func__, i);
	}

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance) {
		vchiq_log_error(vchiq_core_log_level,
			"%s: error allocating vchiq instance\n", __func__);
		ret = -ENOMEM;
		goto failed;
	}

	instance->connected = 0;
	instance->state = state;
	mutex_init(&instance->bulk_waiter_list_mutex);
	INIT_LIST_HEAD(&instance->bulk_waiter_list);

	*instance_out = instance;

	ret = 0;

failed:
	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, ret);

	return ret;
}
EXPORT_SYMBOL(vchiq_initialise);

static void free_bulk_waiter(struct vchiq_instance *instance)
{
	struct bulk_waiter_node *waiter, *next;

	list_for_each_entry_safe(waiter, next,
				 &instance->bulk_waiter_list, list) {
		list_del(&waiter->list);
		vchiq_log_info(vchiq_arm_log_level,
				"bulk_waiter - cleaned up %pK for pid %d",
				waiter, waiter->pid);
		kfree(waiter);
	}
}

enum vchiq_status vchiq_shutdown(struct vchiq_instance *instance)
{
	enum vchiq_status status = VCHIQ_SUCCESS;
	struct vchiq_state *state = instance->state;

	if (mutex_lock_killable(&state->mutex))
		return VCHIQ_RETRY;

	/* Remove all services */
	vchiq_shutdown_internal(state, instance);

	mutex_unlock(&state->mutex);

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, status);

	free_bulk_waiter(instance);
	kfree(instance);

	return status;
}
EXPORT_SYMBOL(vchiq_shutdown);

static int vchiq_is_connected(struct vchiq_instance *instance)
{
	return instance->connected;
}

enum vchiq_status vchiq_connect(struct vchiq_instance *instance)
{
	enum vchiq_status status;
	struct vchiq_state *state = instance->state;

	if (mutex_lock_killable(&state->mutex)) {
		vchiq_log_trace(vchiq_core_log_level,
			"%s: call to mutex_lock failed", __func__);
		status = VCHIQ_RETRY;
		goto failed;
	}
	status = vchiq_connect_internal(state, instance);

	if (status == VCHIQ_SUCCESS)
		instance->connected = 1;

	mutex_unlock(&state->mutex);

failed:
	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, status);

	return status;
}
EXPORT_SYMBOL(vchiq_connect);

static enum vchiq_status
vchiq_add_service(struct vchiq_instance *instance,
		  const struct vchiq_service_params_kernel *params,
		  unsigned int *phandle)
{
	enum vchiq_status status;
	struct vchiq_state *state = instance->state;
	struct vchiq_service *service = NULL;
	int srvstate;

	*phandle = VCHIQ_SERVICE_HANDLE_INVALID;

	srvstate = vchiq_is_connected(instance)
		? VCHIQ_SRVSTATE_LISTENING
		: VCHIQ_SRVSTATE_HIDDEN;

	service = vchiq_add_service_internal(
		state,
		params,
		srvstate,
		instance,
		NULL);

	if (service) {
		*phandle = service->handle;
		status = VCHIQ_SUCCESS;
	} else {
		status = VCHIQ_ERROR;
	}

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, status);

	return status;
}

enum vchiq_status
vchiq_open_service(struct vchiq_instance *instance,
		   const struct vchiq_service_params_kernel *params,
		   unsigned int *phandle)
{
	enum vchiq_status   status = VCHIQ_ERROR;
	struct vchiq_state   *state = instance->state;
	struct vchiq_service *service = NULL;

	*phandle = VCHIQ_SERVICE_HANDLE_INVALID;

	if (!vchiq_is_connected(instance))
		goto failed;

	service = vchiq_add_service_internal(state,
		params,
		VCHIQ_SRVSTATE_OPENING,
		instance,
		NULL);

	if (service) {
		*phandle = service->handle;
		status = vchiq_open_service_internal(service, current->pid);
		if (status != VCHIQ_SUCCESS) {
			vchiq_remove_service(service->handle);
			*phandle = VCHIQ_SERVICE_HANDLE_INVALID;
		}
	}

failed:
	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, status);

	return status;
}
EXPORT_SYMBOL(vchiq_open_service);

enum vchiq_status
vchiq_bulk_transmit(unsigned int handle, const void *data, unsigned int size,
		    void *userdata, enum vchiq_bulk_mode mode)
{
	enum vchiq_status status;

	while (1) {
		switch (mode) {
		case VCHIQ_BULK_MODE_NOCALLBACK:
		case VCHIQ_BULK_MODE_CALLBACK:
			status = vchiq_bulk_transfer(handle,
						     (void *)data, NULL,
						     size, userdata, mode,
						     VCHIQ_BULK_TRANSMIT);
			break;
		case VCHIQ_BULK_MODE_BLOCKING:
			status = vchiq_blocking_bulk_transfer(handle,
				(void *)data, size, VCHIQ_BULK_TRANSMIT);
			break;
		default:
			return VCHIQ_ERROR;
		}

		/*
		 * vchiq_*_bulk_transfer() may return VCHIQ_RETRY, so we need
		 * to implement a retry mechanism since this function is
		 * supposed to block until queued
		 */
		if (status != VCHIQ_RETRY)
			break;

		msleep(1);
	}

	return status;
}
EXPORT_SYMBOL(vchiq_bulk_transmit);

enum vchiq_status vchiq_bulk_receive(unsigned int handle, void *data,
				     unsigned int size, void *userdata,
				     enum vchiq_bulk_mode mode)
{
	enum vchiq_status status;

	while (1) {
		switch (mode) {
		case VCHIQ_BULK_MODE_NOCALLBACK:
		case VCHIQ_BULK_MODE_CALLBACK:
			status = vchiq_bulk_transfer(handle, data, NULL,
						     size, userdata,
						     mode, VCHIQ_BULK_RECEIVE);
			break;
		case VCHIQ_BULK_MODE_BLOCKING:
			status = vchiq_blocking_bulk_transfer(handle,
				(void *)data, size, VCHIQ_BULK_RECEIVE);
			break;
		default:
			return VCHIQ_ERROR;
		}

		/*
		 * vchiq_*_bulk_transfer() may return VCHIQ_RETRY, so we need
		 * to implement a retry mechanism since this function is
		 * supposed to block until queued
		 */
		if (status != VCHIQ_RETRY)
			break;

		msleep(1);
	}

	return status;
}
EXPORT_SYMBOL(vchiq_bulk_receive);

static enum vchiq_status
vchiq_blocking_bulk_transfer(unsigned int handle, void *data, unsigned int size,
			     enum vchiq_bulk_dir dir)
{
	struct vchiq_instance *instance;
	struct vchiq_service *service;
	enum vchiq_status status;
	struct bulk_waiter_node *waiter = NULL;
	bool found = false;

	service = find_service_by_handle(handle);
	if (!service)
		return VCHIQ_ERROR;

	instance = service->instance;

	vchiq_service_put(service);

	mutex_lock(&instance->bulk_waiter_list_mutex);
	list_for_each_entry(waiter, &instance->bulk_waiter_list, list) {
		if (waiter->pid == current->pid) {
			list_del(&waiter->list);
			found = true;
			break;
		}
	}
	mutex_unlock(&instance->bulk_waiter_list_mutex);

	if (found) {
		struct vchiq_bulk *bulk = waiter->bulk_waiter.bulk;

		if (bulk) {
			/* This thread has an outstanding bulk transfer. */
			/* FIXME: why compare a dma address to a pointer? */
			if ((bulk->data != (dma_addr_t)(uintptr_t)data) ||
				(bulk->size != size)) {
				/*
				 * This is not a retry of the previous one.
				 * Cancel the signal when the transfer completes.
				 */
				spin_lock(&bulk_waiter_spinlock);
				bulk->userdata = NULL;
				spin_unlock(&bulk_waiter_spinlock);
			}
		}
	} else {
		waiter = kzalloc(sizeof(*waiter), GFP_KERNEL);
		if (!waiter) {
			vchiq_log_error(vchiq_core_log_level,
				"%s - out of memory", __func__);
			return VCHIQ_ERROR;
		}
	}

	status = vchiq_bulk_transfer(handle, data, NULL, size,
				     &waiter->bulk_waiter,
				     VCHIQ_BULK_MODE_BLOCKING, dir);
	if ((status != VCHIQ_RETRY) || fatal_signal_pending(current) ||
		!waiter->bulk_waiter.bulk) {
		struct vchiq_bulk *bulk = waiter->bulk_waiter.bulk;

		if (bulk) {
			/* Cancel the signal when the transfer completes. */
			spin_lock(&bulk_waiter_spinlock);
			bulk->userdata = NULL;
			spin_unlock(&bulk_waiter_spinlock);
		}
		kfree(waiter);
	} else {
		waiter->pid = current->pid;
		mutex_lock(&instance->bulk_waiter_list_mutex);
		list_add(&waiter->list, &instance->bulk_waiter_list);
		mutex_unlock(&instance->bulk_waiter_list_mutex);
		vchiq_log_info(vchiq_arm_log_level,
				"saved bulk_waiter %pK for pid %d",
				waiter, current->pid);
	}

	return status;
}

static enum vchiq_status
add_completion(struct vchiq_instance *instance, enum vchiq_reason reason,
	       struct vchiq_header *header, struct user_service *user_service,
	       void *bulk_userdata)
{
	struct vchiq_completion_data_kernel *completion;
	int insert;

	DEBUG_INITIALISE(g_state.local)

	insert = instance->completion_insert;
	while ((insert - instance->completion_remove) >= MAX_COMPLETIONS) {
		/* Out of space - wait for the client */
		DEBUG_TRACE(SERVICE_CALLBACK_LINE);
		vchiq_log_trace(vchiq_arm_log_level,
			"%s - completion queue full", __func__);
		DEBUG_COUNT(COMPLETION_QUEUE_FULL_COUNT);
		if (wait_for_completion_interruptible(
					&instance->remove_event)) {
			vchiq_log_info(vchiq_arm_log_level,
				"service_callback interrupted");
			return VCHIQ_RETRY;
		} else if (instance->closing) {
			vchiq_log_info(vchiq_arm_log_level,
				"service_callback closing");
			return VCHIQ_SUCCESS;
		}
		DEBUG_TRACE(SERVICE_CALLBACK_LINE);
	}

	completion = &instance->completions[insert & (MAX_COMPLETIONS - 1)];

	completion->header = header;
	completion->reason = reason;
	/* N.B. service_userdata is updated while processing AWAIT_COMPLETION */
	completion->service_userdata = user_service->service;
	completion->bulk_userdata = bulk_userdata;

	if (reason == VCHIQ_SERVICE_CLOSED) {
		/*
		 * Take an extra reference, to be held until
		 * this CLOSED notification is delivered.
		 */
		vchiq_service_get(user_service->service);
		if (instance->use_close_delivered)
			user_service->close_pending = 1;
	}

	/*
	 * A write barrier is needed here to ensure that the entire completion
	 * record is written out before the insert point.
	 */
	wmb();

	if (reason == VCHIQ_MESSAGE_AVAILABLE)
		user_service->message_available_pos = insert;

	insert++;
	instance->completion_insert = insert;

	complete(&instance->insert_event);

	return VCHIQ_SUCCESS;
}

static enum vchiq_status
service_callback(enum vchiq_reason reason, struct vchiq_header *header,
		 unsigned int handle, void *bulk_userdata)
{
	/*
	 * How do we ensure the callback goes to the right client?
	 * The service_user data points to a user_service record
	 * containing the original callback and the user state structure, which
	 * contains a circular buffer for completion records.
	 */
	struct user_service *user_service;
	struct vchiq_service *service;
	struct vchiq_instance *instance;
	bool skip_completion = false;

	DEBUG_INITIALISE(g_state.local)

	DEBUG_TRACE(SERVICE_CALLBACK_LINE);

	service = handle_to_service(handle);
	if (WARN_ON(!service))
		return VCHIQ_SUCCESS;

	user_service = (struct user_service *)service->base.userdata;
	instance = user_service->instance;

	if (!instance || instance->closing)
		return VCHIQ_SUCCESS;

	vchiq_log_trace(vchiq_arm_log_level,
		"%s - service %lx(%d,%p), reason %d, header %lx, instance %lx, bulk_userdata %lx",
		__func__, (unsigned long)user_service,
		service->localport, user_service->userdata,
		reason, (unsigned long)header,
		(unsigned long)instance, (unsigned long)bulk_userdata);

	if (header && user_service->is_vchi) {
		spin_lock(&msg_queue_spinlock);
		while (user_service->msg_insert ==
			(user_service->msg_remove + MSG_QUEUE_SIZE)) {
			spin_unlock(&msg_queue_spinlock);
			DEBUG_TRACE(SERVICE_CALLBACK_LINE);
			DEBUG_COUNT(MSG_QUEUE_FULL_COUNT);
			vchiq_log_trace(vchiq_arm_log_level,
				"service_callback - msg queue full");
			/*
			 * If there is no MESSAGE_AVAILABLE in the completion
			 * queue, add one
			 */
			if ((user_service->message_available_pos -
				instance->completion_remove) < 0) {
				enum vchiq_status status;

				vchiq_log_info(vchiq_arm_log_level,
					"Inserting extra MESSAGE_AVAILABLE");
				DEBUG_TRACE(SERVICE_CALLBACK_LINE);
				status = add_completion(instance, reason,
					NULL, user_service, bulk_userdata);
				if (status != VCHIQ_SUCCESS) {
					DEBUG_TRACE(SERVICE_CALLBACK_LINE);
					return status;
				}
			}

			DEBUG_TRACE(SERVICE_CALLBACK_LINE);
			if (wait_for_completion_interruptible(
						&user_service->remove_event)) {
				vchiq_log_info(vchiq_arm_log_level,
					"%s interrupted", __func__);
				DEBUG_TRACE(SERVICE_CALLBACK_LINE);
				return VCHIQ_RETRY;
			} else if (instance->closing) {
				vchiq_log_info(vchiq_arm_log_level,
					"%s closing", __func__);
				DEBUG_TRACE(SERVICE_CALLBACK_LINE);
				return VCHIQ_ERROR;
			}
			DEBUG_TRACE(SERVICE_CALLBACK_LINE);
			spin_lock(&msg_queue_spinlock);
		}

		user_service->msg_queue[user_service->msg_insert &
			(MSG_QUEUE_SIZE - 1)] = header;
		user_service->msg_insert++;

		/*
		 * If there is a thread waiting in DEQUEUE_MESSAGE, or if
		 * there is a MESSAGE_AVAILABLE in the completion queue then
		 * bypass the completion queue.
		 */
		if (((user_service->message_available_pos -
			instance->completion_remove) >= 0) ||
			user_service->dequeue_pending) {
			user_service->dequeue_pending = 0;
			skip_completion = true;
		}

		spin_unlock(&msg_queue_spinlock);
		complete(&user_service->insert_event);

		header = NULL;
	}
	DEBUG_TRACE(SERVICE_CALLBACK_LINE);

	if (skip_completion)
		return VCHIQ_SUCCESS;

	return add_completion(instance, reason, header, user_service,
		bulk_userdata);
}

static void
user_service_free(void *userdata)
{
	kfree(userdata);
}

static void close_delivered(struct user_service *user_service)
{
	vchiq_log_info(vchiq_arm_log_level,
		"%s(handle=%x)",
		__func__, user_service->service->handle);

	if (user_service->close_pending) {
		/* Allow the underlying service to be culled */
		vchiq_service_put(user_service->service);

		/* Wake the user-thread blocked in close_ or remove_service */
		complete(&user_service->close_event);

		user_service->close_pending = 0;
	}
}

struct vchiq_io_copy_callback_context {
	struct vchiq_element *element;
	size_t element_offset;
	unsigned long elements_to_go;
};

static ssize_t vchiq_ioc_copy_element_data(void *context, void *dest,
					   size_t offset, size_t maxsize)
{
	struct vchiq_io_copy_callback_context *cc = context;
	size_t total_bytes_copied = 0;
	size_t bytes_this_round;

	while (total_bytes_copied < maxsize) {
		if (!cc->elements_to_go)
			return total_bytes_copied;

		if (!cc->element->size) {
			cc->elements_to_go--;
			cc->element++;
			cc->element_offset = 0;
			continue;
		}

		bytes_this_round = min(cc->element->size - cc->element_offset,
				       maxsize - total_bytes_copied);

		if (copy_from_user(dest + total_bytes_copied,
				  cc->element->data + cc->element_offset,
				  bytes_this_round))
			return -EFAULT;

		cc->element_offset += bytes_this_round;
		total_bytes_copied += bytes_this_round;

		if (cc->element_offset == cc->element->size) {
			cc->elements_to_go--;
			cc->element++;
			cc->element_offset = 0;
		}
	}

	return maxsize;
}

static int
vchiq_ioc_queue_message(unsigned int handle, struct vchiq_element *elements,
			unsigned long count)
{
	struct vchiq_io_copy_callback_context context;
	enum vchiq_status status = VCHIQ_SUCCESS;
	unsigned long i;
	size_t total_size = 0;

	context.element = elements;
	context.element_offset = 0;
	context.elements_to_go = count;

	for (i = 0; i < count; i++) {
		if (!elements[i].data && elements[i].size != 0)
			return -EFAULT;

		total_size += elements[i].size;
	}

	status = vchiq_queue_message(handle, vchiq_ioc_copy_element_data,
				     &context, total_size);

	if (status == VCHIQ_ERROR)
		return -EIO;
	else if (status == VCHIQ_RETRY)
		return -EINTR;
	return 0;
}

static int vchiq_ioc_create_service(struct vchiq_instance *instance,
				    struct vchiq_create_service *args)
{
	struct user_service *user_service = NULL;
	struct vchiq_service *service;
	enum vchiq_status status = VCHIQ_SUCCESS;
	struct vchiq_service_params_kernel params;
	int srvstate;

	user_service = kmalloc(sizeof(*user_service), GFP_KERNEL);
	if (!user_service)
		return -ENOMEM;

	if (args->is_open) {
		if (!instance->connected) {
			kfree(user_service);
			return -ENOTCONN;
		}
		srvstate = VCHIQ_SRVSTATE_OPENING;
	} else {
		srvstate = instance->connected ?
			 VCHIQ_SRVSTATE_LISTENING : VCHIQ_SRVSTATE_HIDDEN;
	}

	params = (struct vchiq_service_params_kernel) {
		.fourcc   = args->params.fourcc,
		.callback = service_callback,
		.userdata = user_service,
		.version  = args->params.version,
		.version_min = args->params.version_min,
	};
	service = vchiq_add_service_internal(instance->state, &params,
					     srvstate, instance,
					     user_service_free);
	if (!service) {
		kfree(user_service);
		return -EEXIST;
	}

	user_service->service = service;
	user_service->userdata = args->params.userdata;
	user_service->instance = instance;
	user_service->is_vchi = (args->is_vchi != 0);
	user_service->dequeue_pending = 0;
	user_service->close_pending = 0;
	user_service->message_available_pos = instance->completion_remove - 1;
	user_service->msg_insert = 0;
	user_service->msg_remove = 0;
	init_completion(&user_service->insert_event);
	init_completion(&user_service->remove_event);
	init_completion(&user_service->close_event);

	if (args->is_open) {
		status = vchiq_open_service_internal(service, instance->pid);
		if (status != VCHIQ_SUCCESS) {
			vchiq_remove_service(service->handle);
			return (status == VCHIQ_RETRY) ?
				-EINTR : -EIO;
		}
	}
	args->handle = service->handle;

	return 0;
}

static int vchiq_ioc_dequeue_message(struct vchiq_instance *instance,
				     struct vchiq_dequeue_message *args)
{
	struct user_service *user_service;
	struct vchiq_service *service;
	struct vchiq_header *header;
	int ret;

	DEBUG_INITIALISE(g_state.local)
	DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
	service = find_service_for_instance(instance, args->handle);
	if (!service)
		return -EINVAL;

	user_service = (struct user_service *)service->base.userdata;
	if (user_service->is_vchi == 0) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock(&msg_queue_spinlock);
	if (user_service->msg_remove == user_service->msg_insert) {
		if (!args->blocking) {
			spin_unlock(&msg_queue_spinlock);
			DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
			ret = -EWOULDBLOCK;
			goto out;
		}
		user_service->dequeue_pending = 1;
		ret = 0;
		do {
			spin_unlock(&msg_queue_spinlock);
			DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
			if (wait_for_completion_interruptible(
				&user_service->insert_event)) {
				vchiq_log_info(vchiq_arm_log_level,
					"DEQUEUE_MESSAGE interrupted");
				ret = -EINTR;
				break;
			}
			spin_lock(&msg_queue_spinlock);
		} while (user_service->msg_remove == user_service->msg_insert);

		if (ret)
			goto out;
	}

	if (WARN_ON_ONCE((int)(user_service->msg_insert -
			 user_service->msg_remove) < 0)) {
		spin_unlock(&msg_queue_spinlock);
		ret = -EINVAL;
		goto out;
	}

	header = user_service->msg_queue[user_service->msg_remove &
		(MSG_QUEUE_SIZE - 1)];
	user_service->msg_remove++;
	spin_unlock(&msg_queue_spinlock);

	complete(&user_service->remove_event);
	if (!header) {
		ret = -ENOTCONN;
	} else if (header->size <= args->bufsize) {
		/* Copy to user space if msgbuf is not NULL */
		if (!args->buf || (copy_to_user(args->buf,
					header->data, header->size) == 0)) {
			ret = header->size;
			vchiq_release_message(service->handle, header);
		} else {
			ret = -EFAULT;
		}
	} else {
		vchiq_log_error(vchiq_arm_log_level,
			"header %pK: bufsize %x < size %x",
			header, args->bufsize, header->size);
		WARN(1, "invalid size\n");
		ret = -EMSGSIZE;
	}
	DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
out:
	vchiq_service_put(service);
	return ret;
}

static int vchiq_irq_queue_bulk_tx_rx(struct vchiq_instance *instance,
				      struct vchiq_queue_bulk_transfer *args,
				      enum vchiq_bulk_dir dir,
				      enum vchiq_bulk_mode __user *mode)
{
	struct vchiq_service *service;
	struct bulk_waiter_node *waiter = NULL;
	bool found = false;
	void *userdata;
	int status = 0;
	int ret;

	service = find_service_for_instance(instance, args->handle);
	if (!service)
		return -EINVAL;

	if (args->mode == VCHIQ_BULK_MODE_BLOCKING) {
		waiter = kzalloc(sizeof(*waiter), GFP_KERNEL);
		if (!waiter) {
			ret = -ENOMEM;
			goto out;
		}

		userdata = &waiter->bulk_waiter;
	} else if (args->mode == VCHIQ_BULK_MODE_WAITING) {
		mutex_lock(&instance->bulk_waiter_list_mutex);
		list_for_each_entry(waiter, &instance->bulk_waiter_list,
				    list) {
			if (waiter->pid == current->pid) {
				list_del(&waiter->list);
				found = true;
				break;
			}
		}
		mutex_unlock(&instance->bulk_waiter_list_mutex);
		if (!found) {
			vchiq_log_error(vchiq_arm_log_level,
				"no bulk_waiter found for pid %d",
				current->pid);
			ret = -ESRCH;
			goto out;
		}
		vchiq_log_info(vchiq_arm_log_level,
			"found bulk_waiter %pK for pid %d", waiter,
			current->pid);
		userdata = &waiter->bulk_waiter;
	} else {
		userdata = args->userdata;
	}

	status = vchiq_bulk_transfer(args->handle, NULL, args->data, args->size,
				     userdata, args->mode, dir);

	if (!waiter) {
		ret = 0;
		goto out;
	}

	if ((status != VCHIQ_RETRY) || fatal_signal_pending(current) ||
		!waiter->bulk_waiter.bulk) {
		if (waiter->bulk_waiter.bulk) {
			/* Cancel the signal when the transfer completes. */
			spin_lock(&bulk_waiter_spinlock);
			waiter->bulk_waiter.bulk->userdata = NULL;
			spin_unlock(&bulk_waiter_spinlock);
		}
		kfree(waiter);
		ret = 0;
	} else {
		const enum vchiq_bulk_mode mode_waiting =
			VCHIQ_BULK_MODE_WAITING;
		waiter->pid = current->pid;
		mutex_lock(&instance->bulk_waiter_list_mutex);
		list_add(&waiter->list, &instance->bulk_waiter_list);
		mutex_unlock(&instance->bulk_waiter_list_mutex);
		vchiq_log_info(vchiq_arm_log_level,
			"saved bulk_waiter %pK for pid %d",
			waiter, current->pid);

		ret = put_user(mode_waiting, mode);
	}
out:
	vchiq_service_put(service);
	if (ret)
		return ret;
	else if (status == VCHIQ_ERROR)
		return -EIO;
	else if (status == VCHIQ_RETRY)
		return -EINTR;
	return 0;
}

/* read a user pointer value from an array pointers in user space */
static inline int vchiq_get_user_ptr(void __user **buf, void __user *ubuf, int index)
{
	int ret;

	if (in_compat_syscall()) {
		compat_uptr_t ptr32;
		compat_uptr_t __user *uptr = ubuf;

		ret = get_user(ptr32, uptr + index);
		if (ret)
			return ret;

		*buf = compat_ptr(ptr32);
	} else {
		uintptr_t ptr, __user *uptr = ubuf;

		ret = get_user(ptr, uptr + index);

		if (ret)
			return ret;

		*buf = (void __user *)ptr;
	}

	return 0;
}

struct vchiq_completion_data32 {
	enum vchiq_reason reason;
	compat_uptr_t header;
	compat_uptr_t service_userdata;
	compat_uptr_t bulk_userdata;
};

static int vchiq_put_completion(struct vchiq_completion_data __user *buf,
				struct vchiq_completion_data *completion,
				int index)
{
	struct vchiq_completion_data32 __user *buf32 = (void __user *)buf;

	if (in_compat_syscall()) {
		struct vchiq_completion_data32 tmp = {
			.reason		  = completion->reason,
			.header		  = ptr_to_compat(completion->header),
			.service_userdata = ptr_to_compat(completion->service_userdata),
			.bulk_userdata	  = ptr_to_compat(completion->bulk_userdata),
		};
		if (copy_to_user(&buf32[index], &tmp, sizeof(tmp)))
			return -EFAULT;
	} else {
		if (copy_to_user(&buf[index], completion, sizeof(*completion)))
			return -EFAULT;
	}

	return 0;
}

static int vchiq_ioc_await_completion(struct vchiq_instance *instance,
				      struct vchiq_await_completion *args,
				      int __user *msgbufcountp)
{
	int msgbufcount;
	int remove;
	int ret;

	DEBUG_INITIALISE(g_state.local)

	DEBUG_TRACE(AWAIT_COMPLETION_LINE);
	if (!instance->connected) {
		return -ENOTCONN;
	}

	mutex_lock(&instance->completion_mutex);

	DEBUG_TRACE(AWAIT_COMPLETION_LINE);
	while ((instance->completion_remove == instance->completion_insert)
		&& !instance->closing) {
		int rc;

		DEBUG_TRACE(AWAIT_COMPLETION_LINE);
		mutex_unlock(&instance->completion_mutex);
		rc = wait_for_completion_interruptible(
					&instance->insert_event);
		mutex_lock(&instance->completion_mutex);
		if (rc) {
			DEBUG_TRACE(AWAIT_COMPLETION_LINE);
			vchiq_log_info(vchiq_arm_log_level,
				"AWAIT_COMPLETION interrupted");
			ret = -EINTR;
			goto out;
		}
	}
	DEBUG_TRACE(AWAIT_COMPLETION_LINE);

	msgbufcount = args->msgbufcount;
	remove = instance->completion_remove;

	for (ret = 0; ret < args->count; ret++) {
		struct vchiq_completion_data_kernel *completion;
		struct vchiq_completion_data user_completion;
		struct vchiq_service *service;
		struct user_service *user_service;
		struct vchiq_header *header;

		if (remove == instance->completion_insert)
			break;

		completion = &instance->completions[
			remove & (MAX_COMPLETIONS - 1)];

		/*
		 * A read memory barrier is needed to stop
		 * prefetch of a stale completion record
		 */
		rmb();

		service = completion->service_userdata;
		user_service = service->base.userdata;

		memset(&user_completion, 0, sizeof(user_completion));
		user_completion = (struct vchiq_completion_data) {
			.reason = completion->reason,
			.service_userdata = user_service->userdata,
		};

		header = completion->header;
		if (header) {
			void __user *msgbuf;
			int msglen;

			msglen = header->size + sizeof(struct vchiq_header);
			/* This must be a VCHIQ-style service */
			if (args->msgbufsize < msglen) {
				vchiq_log_error(vchiq_arm_log_level,
					"header %pK: msgbufsize %x < msglen %x",
					header, args->msgbufsize, msglen);
				WARN(1, "invalid message size\n");
				if (ret == 0)
					ret = -EMSGSIZE;
				break;
			}
			if (msgbufcount <= 0)
				/* Stall here for lack of a buffer for the message. */
				break;
			/* Get the pointer from user space */
			msgbufcount--;
			if (vchiq_get_user_ptr(&msgbuf, args->msgbufs,
						msgbufcount)) {
				if (ret == 0)
					ret = -EFAULT;
				break;
			}

			/* Copy the message to user space */
			if (copy_to_user(msgbuf, header, msglen)) {
				if (ret == 0)
					ret = -EFAULT;
				break;
			}

			/* Now it has been copied, the message can be released. */
			vchiq_release_message(service->handle, header);

			/* The completion must point to the msgbuf. */
			user_completion.header = msgbuf;
		}

		if ((completion->reason == VCHIQ_SERVICE_CLOSED) &&
		    !instance->use_close_delivered)
			vchiq_service_put(service);

		/*
		 * FIXME: address space mismatch, does bulk_userdata
		 * actually point to user or kernel memory?
		 */
		user_completion.bulk_userdata = completion->bulk_userdata;

		if (vchiq_put_completion(args->buf, &user_completion, ret)) {
			if (ret == 0)
				ret = -EFAULT;
			break;
		}

		/*
		 * Ensure that the above copy has completed
		 * before advancing the remove pointer.
		 */
		mb();
		remove++;
		instance->completion_remove = remove;
	}

	if (msgbufcount != args->msgbufcount) {
		if (put_user(msgbufcount, msgbufcountp))
			ret = -EFAULT;
	}
out:
	if (ret)
		complete(&instance->remove_event);
	mutex_unlock(&instance->completion_mutex);
	DEBUG_TRACE(AWAIT_COMPLETION_LINE);

	return ret;
}

static long
vchiq_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vchiq_instance *instance = file->private_data;
	enum vchiq_status status = VCHIQ_SUCCESS;
	struct vchiq_service *service = NULL;
	long ret = 0;
	int i, rc;

	vchiq_log_trace(vchiq_arm_log_level,
		"%s - instance %pK, cmd %s, arg %lx",
		__func__, instance,
		((_IOC_TYPE(cmd) == VCHIQ_IOC_MAGIC) &&
		(_IOC_NR(cmd) <= VCHIQ_IOC_MAX)) ?
		ioctl_names[_IOC_NR(cmd)] : "<invalid>", arg);

	switch (cmd) {
	case VCHIQ_IOC_SHUTDOWN:
		if (!instance->connected)
			break;

		/* Remove all services */
		i = 0;
		while ((service = next_service_by_instance(instance->state,
			instance, &i))) {
			status = vchiq_remove_service(service->handle);
			vchiq_service_put(service);
			if (status != VCHIQ_SUCCESS)
				break;
		}
		service = NULL;

		if (status == VCHIQ_SUCCESS) {
			/* Wake the completion thread and ask it to exit */
			instance->closing = 1;
			complete(&instance->insert_event);
		}

		break;

	case VCHIQ_IOC_CONNECT:
		if (instance->connected) {
			ret = -EINVAL;
			break;
		}
		rc = mutex_lock_killable(&instance->state->mutex);
		if (rc) {
			vchiq_log_error(vchiq_arm_log_level,
				"vchiq: connect: could not lock mutex for state %d: %d",
				instance->state->id, rc);
			ret = -EINTR;
			break;
		}
		status = vchiq_connect_internal(instance->state, instance);
		mutex_unlock(&instance->state->mutex);

		if (status == VCHIQ_SUCCESS)
			instance->connected = 1;
		else
			vchiq_log_error(vchiq_arm_log_level,
				"vchiq: could not connect: %d", status);
		break;

	case VCHIQ_IOC_CREATE_SERVICE: {
		struct vchiq_create_service __user *argp;
		struct vchiq_create_service args;

		argp = (void __user *)arg;
		if (copy_from_user(&args, argp, sizeof(args))) {
			ret = -EFAULT;
			break;
		}

		ret = vchiq_ioc_create_service(instance, &args);
		if (ret < 0)
			break;

		if (put_user(args.handle, &argp->handle)) {
			vchiq_remove_service(args.handle);
			ret = -EFAULT;
		}
	} break;

	case VCHIQ_IOC_CLOSE_SERVICE:
	case VCHIQ_IOC_REMOVE_SERVICE: {
		unsigned int handle = (unsigned int)arg;
		struct user_service *user_service;

		service = find_service_for_instance(instance, handle);
		if (!service) {
			ret = -EINVAL;
			break;
		}

		user_service = service->base.userdata;

		/*
		 * close_pending is false on first entry, and when the
		 * wait in vchiq_close_service has been interrupted.
		 */
		if (!user_service->close_pending) {
			status = (cmd == VCHIQ_IOC_CLOSE_SERVICE) ?
				 vchiq_close_service(service->handle) :
				 vchiq_remove_service(service->handle);
			if (status != VCHIQ_SUCCESS)
				break;
		}

		/*
		 * close_pending is true once the underlying service
		 * has been closed until the client library calls the
		 * CLOSE_DELIVERED ioctl, signalling close_event.
		 */
		if (user_service->close_pending &&
			wait_for_completion_interruptible(
				&user_service->close_event))
			status = VCHIQ_RETRY;
		break;
	}

	case VCHIQ_IOC_USE_SERVICE:
	case VCHIQ_IOC_RELEASE_SERVICE:	{
		unsigned int handle = (unsigned int)arg;

		service = find_service_for_instance(instance, handle);
		if (service) {
			ret = (cmd == VCHIQ_IOC_USE_SERVICE) ?
				vchiq_use_service_internal(service) :
				vchiq_release_service_internal(service);
			if (ret) {
				vchiq_log_error(vchiq_susp_log_level,
					"%s: cmd %s returned error %ld for service %c%c%c%c:%03d",
					__func__,
					(cmd == VCHIQ_IOC_USE_SERVICE) ?
						"VCHIQ_IOC_USE_SERVICE" :
						"VCHIQ_IOC_RELEASE_SERVICE",
					ret,
					VCHIQ_FOURCC_AS_4CHARS(
						service->base.fourcc),
					service->client_id);
			}
		} else {
			ret = -EINVAL;
		}
	} break;

	case VCHIQ_IOC_QUEUE_MESSAGE: {
		struct vchiq_queue_message args;

		if (copy_from_user(&args, (const void __user *)arg,
				   sizeof(args))) {
			ret = -EFAULT;
			break;
		}

		service = find_service_for_instance(instance, args.handle);

		if (service && (args.count <= MAX_ELEMENTS)) {
			/* Copy elements into kernel space */
			struct vchiq_element elements[MAX_ELEMENTS];

			if (copy_from_user(elements, args.elements,
				args.count * sizeof(struct vchiq_element)) == 0)
				ret = vchiq_ioc_queue_message(args.handle, elements,
							      args.count);
			else
				ret = -EFAULT;
		} else {
			ret = -EINVAL;
		}
	} break;

	case VCHIQ_IOC_QUEUE_BULK_TRANSMIT:
	case VCHIQ_IOC_QUEUE_BULK_RECEIVE: {
		struct vchiq_queue_bulk_transfer args;
		struct vchiq_queue_bulk_transfer __user *argp;

		enum vchiq_bulk_dir dir =
			(cmd == VCHIQ_IOC_QUEUE_BULK_TRANSMIT) ?
			VCHIQ_BULK_TRANSMIT : VCHIQ_BULK_RECEIVE;

		argp = (void __user *)arg;
		if (copy_from_user(&args, argp, sizeof(args))) {
			ret = -EFAULT;
			break;
		}

		ret = vchiq_irq_queue_bulk_tx_rx(instance, &args,
						 dir, &argp->mode);
	} break;

	case VCHIQ_IOC_AWAIT_COMPLETION: {
		struct vchiq_await_completion args;
		struct vchiq_await_completion __user *argp;

		argp = (void __user *)arg;
		if (copy_from_user(&args, argp, sizeof(args))) {
			ret = -EFAULT;
			break;
		}

		ret = vchiq_ioc_await_completion(instance, &args,
						 &argp->msgbufcount);
	} break;

	case VCHIQ_IOC_DEQUEUE_MESSAGE: {
		struct vchiq_dequeue_message args;

		if (copy_from_user(&args, (const void __user *)arg,
				   sizeof(args))) {
			ret = -EFAULT;
			break;
		}

		ret = vchiq_ioc_dequeue_message(instance, &args);
	} break;

	case VCHIQ_IOC_GET_CLIENT_ID: {
		unsigned int handle = (unsigned int)arg;

		ret = vchiq_get_client_id(handle);
	} break;

	case VCHIQ_IOC_GET_CONFIG: {
		struct vchiq_get_config args;
		struct vchiq_config config;

		if (copy_from_user(&args, (const void __user *)arg,
				   sizeof(args))) {
			ret = -EFAULT;
			break;
		}
		if (args.config_size > sizeof(config)) {
			ret = -EINVAL;
			break;
		}

		vchiq_get_config(&config);
		if (copy_to_user(args.pconfig, &config, args.config_size)) {
			ret = -EFAULT;
			break;
		}
	} break;

	case VCHIQ_IOC_SET_SERVICE_OPTION: {
		struct vchiq_set_service_option args;

		if (copy_from_user(&args, (const void __user *)arg,
				   sizeof(args))) {
			ret = -EFAULT;
			break;
		}

		service = find_service_for_instance(instance, args.handle);
		if (!service) {
			ret = -EINVAL;
			break;
		}

		ret = vchiq_set_service_option(args.handle, args.option,
					       args.value);
	} break;

	case VCHIQ_IOC_LIB_VERSION: {
		unsigned int lib_version = (unsigned int)arg;

		if (lib_version < VCHIQ_VERSION_MIN)
			ret = -EINVAL;
		else if (lib_version >= VCHIQ_VERSION_CLOSE_DELIVERED)
			instance->use_close_delivered = 1;
	} break;

	case VCHIQ_IOC_CLOSE_DELIVERED: {
		unsigned int handle = (unsigned int)arg;

		service = find_closed_service_for_instance(instance, handle);
		if (service) {
			struct user_service *user_service =
				(struct user_service *)service->base.userdata;
			close_delivered(user_service);
		} else {
			ret = -EINVAL;
		}
	} break;

	default:
		ret = -ENOTTY;
		break;
	}

	if (service)
		vchiq_service_put(service);

	if (ret == 0) {
		if (status == VCHIQ_ERROR)
			ret = -EIO;
		else if (status == VCHIQ_RETRY)
			ret = -EINTR;
	}

	if ((status == VCHIQ_SUCCESS) && (ret < 0) && (ret != -EINTR) &&
		(ret != -EWOULDBLOCK))
		vchiq_log_info(vchiq_arm_log_level,
			"  ioctl instance %pK, cmd %s -> status %d, %ld",
			instance,
			(_IOC_NR(cmd) <= VCHIQ_IOC_MAX) ?
				ioctl_names[_IOC_NR(cmd)] :
				"<invalid>",
			status, ret);
	else
		vchiq_log_trace(vchiq_arm_log_level,
			"  ioctl instance %pK, cmd %s -> status %d, %ld",
			instance,
			(_IOC_NR(cmd) <= VCHIQ_IOC_MAX) ?
				ioctl_names[_IOC_NR(cmd)] :
				"<invalid>",
			status, ret);

	return ret;
}

#if defined(CONFIG_COMPAT)

struct vchiq_service_params32 {
	int fourcc;
	compat_uptr_t callback;
	compat_uptr_t userdata;
	short version; /* Increment for non-trivial changes */
	short version_min; /* Update for incompatible changes */
};

struct vchiq_create_service32 {
	struct vchiq_service_params32 params;
	int is_open;
	int is_vchi;
	unsigned int handle; /* OUT */
};

#define VCHIQ_IOC_CREATE_SERVICE32 \
	_IOWR(VCHIQ_IOC_MAGIC, 2, struct vchiq_create_service32)

static long
vchiq_compat_ioctl_create_service(struct file *file, unsigned int cmd,
				  struct vchiq_create_service32 __user *ptrargs32)
{
	struct vchiq_create_service args;
	struct vchiq_create_service32 args32;
	long ret;

	if (copy_from_user(&args32, ptrargs32, sizeof(args32)))
		return -EFAULT;

	args = (struct vchiq_create_service) {
		.params = {
			.fourcc	     = args32.params.fourcc,
			.callback    = compat_ptr(args32.params.callback),
			.userdata    = compat_ptr(args32.params.userdata),
			.version     = args32.params.version,
			.version_min = args32.params.version_min,
		},
		.is_open = args32.is_open,
		.is_vchi = args32.is_vchi,
		.handle  = args32.handle,
	};

	ret = vchiq_ioc_create_service(file->private_data, &args);
	if (ret < 0)
		return ret;

	if (put_user(args.handle, &ptrargs32->handle)) {
		vchiq_remove_service(args.handle);
		return -EFAULT;
	}

	return 0;
}

struct vchiq_element32 {
	compat_uptr_t data;
	unsigned int size;
};

struct vchiq_queue_message32 {
	unsigned int handle;
	unsigned int count;
	compat_uptr_t elements;
};

#define VCHIQ_IOC_QUEUE_MESSAGE32 \
	_IOW(VCHIQ_IOC_MAGIC,  4, struct vchiq_queue_message32)

static long
vchiq_compat_ioctl_queue_message(struct file *file,
				 unsigned int cmd,
				 struct vchiq_queue_message32 __user *arg)
{
	struct vchiq_queue_message args;
	struct vchiq_queue_message32 args32;
	struct vchiq_service *service;
	int ret;

	if (copy_from_user(&args32, arg, sizeof(args32)))
		return -EFAULT;

	args = (struct vchiq_queue_message) {
		.handle   = args32.handle,
		.count    = args32.count,
		.elements = compat_ptr(args32.elements),
	};

	if (args32.count > MAX_ELEMENTS)
		return -EINVAL;

	service = find_service_for_instance(file->private_data, args.handle);
	if (!service)
		return -EINVAL;

	if (args32.elements && args32.count) {
		struct vchiq_element32 element32[MAX_ELEMENTS];
		struct vchiq_element elements[MAX_ELEMENTS];
		unsigned int count;

		if (copy_from_user(&element32, args.elements,
				   sizeof(element32))) {
			vchiq_service_put(service);
			return -EFAULT;
		}

		for (count = 0; count < args32.count; count++) {
			elements[count].data =
				compat_ptr(element32[count].data);
			elements[count].size = element32[count].size;
		}
		ret = vchiq_ioc_queue_message(args.handle, elements,
					      args.count);
	} else {
		ret = -EINVAL;
	}
	vchiq_service_put(service);

	return ret;
}

struct vchiq_queue_bulk_transfer32 {
	unsigned int handle;
	compat_uptr_t data;
	unsigned int size;
	compat_uptr_t userdata;
	enum vchiq_bulk_mode mode;
};

#define VCHIQ_IOC_QUEUE_BULK_TRANSMIT32 \
	_IOWR(VCHIQ_IOC_MAGIC, 5, struct vchiq_queue_bulk_transfer32)
#define VCHIQ_IOC_QUEUE_BULK_RECEIVE32 \
	_IOWR(VCHIQ_IOC_MAGIC, 6, struct vchiq_queue_bulk_transfer32)

static long
vchiq_compat_ioctl_queue_bulk(struct file *file,
			      unsigned int cmd,
			      struct vchiq_queue_bulk_transfer32 __user *argp)
{
	struct vchiq_queue_bulk_transfer32 args32;
	struct vchiq_queue_bulk_transfer args;
	enum vchiq_bulk_dir dir = (cmd == VCHIQ_IOC_QUEUE_BULK_TRANSMIT32) ?
				  VCHIQ_BULK_TRANSMIT : VCHIQ_BULK_RECEIVE;

	if (copy_from_user(&args32, argp, sizeof(args32)))
		return -EFAULT;

	args = (struct vchiq_queue_bulk_transfer) {
		.handle   = args32.handle,
		.data	  = compat_ptr(args32.data),
		.size	  = args32.size,
		.userdata = compat_ptr(args32.userdata),
		.mode	  = args32.mode,
	};

	return vchiq_irq_queue_bulk_tx_rx(file->private_data, &args,
					  dir, &argp->mode);
}

struct vchiq_await_completion32 {
	unsigned int count;
	compat_uptr_t buf;
	unsigned int msgbufsize;
	unsigned int msgbufcount; /* IN/OUT */
	compat_uptr_t msgbufs;
};

#define VCHIQ_IOC_AWAIT_COMPLETION32 \
	_IOWR(VCHIQ_IOC_MAGIC, 7, struct vchiq_await_completion32)

static long
vchiq_compat_ioctl_await_completion(struct file *file,
				    unsigned int cmd,
				    struct vchiq_await_completion32 __user *argp)
{
	struct vchiq_await_completion args;
	struct vchiq_await_completion32 args32;

	if (copy_from_user(&args32, argp, sizeof(args32)))
		return -EFAULT;

	args = (struct vchiq_await_completion) {
		.count		= args32.count,
		.buf		= compat_ptr(args32.buf),
		.msgbufsize	= args32.msgbufsize,
		.msgbufcount	= args32.msgbufcount,
		.msgbufs	= compat_ptr(args32.msgbufs),
	};

	return vchiq_ioc_await_completion(file->private_data, &args,
					  &argp->msgbufcount);
}

struct vchiq_dequeue_message32 {
	unsigned int handle;
	int blocking;
	unsigned int bufsize;
	compat_uptr_t buf;
};

#define VCHIQ_IOC_DEQUEUE_MESSAGE32 \
	_IOWR(VCHIQ_IOC_MAGIC, 8, struct vchiq_dequeue_message32)

static long
vchiq_compat_ioctl_dequeue_message(struct file *file,
				   unsigned int cmd,
				   struct vchiq_dequeue_message32 __user *arg)
{
	struct vchiq_dequeue_message32 args32;
	struct vchiq_dequeue_message args;

	if (copy_from_user(&args32, arg, sizeof(args32)))
		return -EFAULT;

	args = (struct vchiq_dequeue_message) {
		.handle		= args32.handle,
		.blocking	= args32.blocking,
		.bufsize	= args32.bufsize,
		.buf		= compat_ptr(args32.buf),
	};

	return vchiq_ioc_dequeue_message(file->private_data, &args);
}

struct vchiq_get_config32 {
	unsigned int config_size;
	compat_uptr_t pconfig;
};

#define VCHIQ_IOC_GET_CONFIG32 \
	_IOWR(VCHIQ_IOC_MAGIC, 10, struct vchiq_get_config32)

static long
vchiq_compat_ioctl_get_config(struct file *file,
			      unsigned int cmd,
			      struct vchiq_get_config32 __user *arg)
{
	struct vchiq_get_config32 args32;
	struct vchiq_config config;
	void __user *ptr;

	if (copy_from_user(&args32, arg, sizeof(args32)))
		return -EFAULT;
	if (args32.config_size > sizeof(config))
		return -EINVAL;

	vchiq_get_config(&config);
	ptr = compat_ptr(args32.pconfig);
	if (copy_to_user(ptr, &config, args32.config_size))
		return -EFAULT;

	return 0;
}

static long
vchiq_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = compat_ptr(arg);

	switch (cmd) {
	case VCHIQ_IOC_CREATE_SERVICE32:
		return vchiq_compat_ioctl_create_service(file, cmd, argp);
	case VCHIQ_IOC_QUEUE_MESSAGE32:
		return vchiq_compat_ioctl_queue_message(file, cmd, argp);
	case VCHIQ_IOC_QUEUE_BULK_TRANSMIT32:
	case VCHIQ_IOC_QUEUE_BULK_RECEIVE32:
		return vchiq_compat_ioctl_queue_bulk(file, cmd, argp);
	case VCHIQ_IOC_AWAIT_COMPLETION32:
		return vchiq_compat_ioctl_await_completion(file, cmd, argp);
	case VCHIQ_IOC_DEQUEUE_MESSAGE32:
		return vchiq_compat_ioctl_dequeue_message(file, cmd, argp);
	case VCHIQ_IOC_GET_CONFIG32:
		return vchiq_compat_ioctl_get_config(file, cmd, argp);
	default:
		return vchiq_ioctl(file, cmd, (unsigned long)argp);
	}
}

#endif

static int vchiq_open(struct inode *inode, struct file *file)
{
	struct vchiq_state *state = vchiq_get_state();
	struct vchiq_instance *instance;

	vchiq_log_info(vchiq_arm_log_level, "vchiq_open");

	if (!state) {
		vchiq_log_error(vchiq_arm_log_level,
				"vchiq has no connection to VideoCore");
		return -ENOTCONN;
	}

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return -ENOMEM;

	instance->state = state;
	instance->pid = current->tgid;

	vchiq_debugfs_add_instance(instance);

	init_completion(&instance->insert_event);
	init_completion(&instance->remove_event);
	mutex_init(&instance->completion_mutex);
	mutex_init(&instance->bulk_waiter_list_mutex);
	INIT_LIST_HEAD(&instance->bulk_waiter_list);

	file->private_data = instance;

	return 0;
}

static int vchiq_release(struct inode *inode, struct file *file)
{
	struct vchiq_instance *instance = file->private_data;
	struct vchiq_state *state = vchiq_get_state();
	struct vchiq_service *service;
	int ret = 0;
	int i;

	vchiq_log_info(vchiq_arm_log_level, "%s: instance=%lx", __func__,
		       (unsigned long)instance);

	if (!state) {
		ret = -EPERM;
		goto out;
	}

	/* Ensure videocore is awake to allow termination. */
	vchiq_use_internal(instance->state, NULL, USE_TYPE_VCHIQ);

	mutex_lock(&instance->completion_mutex);

	/* Wake the completion thread and ask it to exit */
	instance->closing = 1;
	complete(&instance->insert_event);

	mutex_unlock(&instance->completion_mutex);

	/* Wake the slot handler if the completion queue is full. */
	complete(&instance->remove_event);

	/* Mark all services for termination... */
	i = 0;
	while ((service = next_service_by_instance(state, instance, &i))) {
		struct user_service *user_service = service->base.userdata;

		/* Wake the slot handler if the msg queue is full. */
		complete(&user_service->remove_event);

		vchiq_terminate_service_internal(service);
		vchiq_service_put(service);
	}

	/* ...and wait for them to die */
	i = 0;
	while ((service = next_service_by_instance(state, instance, &i))) {
		struct user_service *user_service = service->base.userdata;

		wait_for_completion(&service->remove_event);

		if (WARN_ON(service->srvstate != VCHIQ_SRVSTATE_FREE)) {
			vchiq_service_put(service);
			break;
		}

		spin_lock(&msg_queue_spinlock);

		while (user_service->msg_remove != user_service->msg_insert) {
			struct vchiq_header *header;
			int m = user_service->msg_remove & (MSG_QUEUE_SIZE - 1);

			header = user_service->msg_queue[m];
			user_service->msg_remove++;
			spin_unlock(&msg_queue_spinlock);

			if (header)
				vchiq_release_message(service->handle, header);
			spin_lock(&msg_queue_spinlock);
		}

		spin_unlock(&msg_queue_spinlock);

		vchiq_service_put(service);
	}

	/* Release any closed services */
	while (instance->completion_remove != instance->completion_insert) {
		struct vchiq_completion_data_kernel *completion;
		struct vchiq_service *service;

		completion = &instance->completions[
			instance->completion_remove & (MAX_COMPLETIONS - 1)];
		service = completion->service_userdata;
		if (completion->reason == VCHIQ_SERVICE_CLOSED) {
			struct user_service *user_service =
							service->base.userdata;

			/* Wake any blocked user-thread */
			if (instance->use_close_delivered)
				complete(&user_service->close_event);
			vchiq_service_put(service);
		}
		instance->completion_remove++;
	}

	/* Release the PEER service count. */
	vchiq_release_internal(instance->state, NULL);

	free_bulk_waiter(instance);

	vchiq_debugfs_remove_instance(instance);

	kfree(instance);
	file->private_data = NULL;

out:
	return ret;
}

int vchiq_dump(void *dump_context, const char *str, int len)
{
	struct dump_context *context = (struct dump_context *)dump_context;
	int copy_bytes;

	if (context->actual >= context->space)
		return 0;

	if (context->offset > 0) {
		int skip_bytes = min_t(int, len, context->offset);

		str += skip_bytes;
		len -= skip_bytes;
		context->offset -= skip_bytes;
		if (context->offset > 0)
			return 0;
	}
	copy_bytes = min_t(int, len, context->space - context->actual);
	if (copy_bytes == 0)
		return 0;
	if (copy_to_user(context->buf + context->actual, str,
			 copy_bytes))
		return -EFAULT;
	context->actual += copy_bytes;
	len -= copy_bytes;

	/*
	 * If the terminating NUL is included in the length, then it
	 * marks the end of a line and should be replaced with a
	 * carriage return.
	 */
	if ((len == 0) && (str[copy_bytes - 1] == '\0')) {
		char cr = '\n';

		if (copy_to_user(context->buf + context->actual - 1,
				 &cr, 1))
			return -EFAULT;
	}
	return 0;
}

int vchiq_dump_platform_instances(void *dump_context)
{
	struct vchiq_state *state = vchiq_get_state();
	char buf[80];
	int len;
	int i;

	/*
	 * There is no list of instances, so instead scan all services,
	 * marking those that have been dumped.
	 */

	rcu_read_lock();
	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service;
		struct vchiq_instance *instance;

		service = rcu_dereference(state->services[i]);
		if (!service || service->base.callback != service_callback)
			continue;

		instance = service->instance;
		if (instance)
			instance->mark = 0;
	}
	rcu_read_unlock();

	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service;
		struct vchiq_instance *instance;
		int err;

		rcu_read_lock();
		service = rcu_dereference(state->services[i]);
		if (!service || service->base.callback != service_callback) {
			rcu_read_unlock();
			continue;
		}

		instance = service->instance;
		if (!instance || instance->mark) {
			rcu_read_unlock();
			continue;
		}
		rcu_read_unlock();

		len = snprintf(buf, sizeof(buf),
			       "Instance %pK: pid %d,%s completions %d/%d",
			       instance, instance->pid,
			       instance->connected ? " connected, " :
			       "",
			       instance->completion_insert -
			       instance->completion_remove,
			       MAX_COMPLETIONS);
		err = vchiq_dump(dump_context, buf, len + 1);
		if (err)
			return err;
		instance->mark = 1;
	}
	return 0;
}

int vchiq_dump_platform_service_state(void *dump_context,
				      struct vchiq_service *service)
{
	struct user_service *user_service =
			(struct user_service *)service->base.userdata;
	char buf[80];
	int len;

	len = scnprintf(buf, sizeof(buf), "  instance %pK", service->instance);

	if ((service->base.callback == service_callback) &&
		user_service->is_vchi) {
		len += scnprintf(buf + len, sizeof(buf) - len,
			", %d/%d messages",
			user_service->msg_insert - user_service->msg_remove,
			MSG_QUEUE_SIZE);

		if (user_service->dequeue_pending)
			len += scnprintf(buf + len, sizeof(buf) - len,
				" (dequeue pending)");
	}

	return vchiq_dump(dump_context, buf, len + 1);
}

static ssize_t
vchiq_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct dump_context context;
	int err;

	context.buf = buf;
	context.actual = 0;
	context.space = count;
	context.offset = *ppos;

	err = vchiq_dump_state(&context, &g_state);
	if (err)
		return err;

	*ppos += context.actual;

	return context.actual;
}

struct vchiq_state *
vchiq_get_state(void)
{

	if (!g_state.remote)
		pr_err("%s: g_state.remote == NULL\n", __func__);
	else if (g_state.remote->initialised != 1)
		pr_notice("%s: g_state.remote->initialised != 1 (%d)\n",
			  __func__, g_state.remote->initialised);

	return (g_state.remote &&
		(g_state.remote->initialised == 1)) ? &g_state : NULL;
}

static const struct file_operations
vchiq_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vchiq_ioctl,
#if defined(CONFIG_COMPAT)
	.compat_ioctl = vchiq_compat_ioctl,
#endif
	.open = vchiq_open,
	.release = vchiq_release,
	.read = vchiq_read
};

/*
 * Autosuspend related functionality
 */

static enum vchiq_status
vchiq_keepalive_vchiq_callback(enum vchiq_reason reason,
			       struct vchiq_header *header,
			       unsigned int service_user, void *bulk_user)
{
	vchiq_log_error(vchiq_susp_log_level,
		"%s callback reason %d", __func__, reason);
	return 0;
}

static int
vchiq_keepalive_thread_func(void *v)
{
	struct vchiq_state *state = (struct vchiq_state *)v;
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	enum vchiq_status status;
	struct vchiq_instance *instance;
	unsigned int ka_handle;
	int ret;

	struct vchiq_service_params_kernel params = {
		.fourcc      = VCHIQ_MAKE_FOURCC('K', 'E', 'E', 'P'),
		.callback    = vchiq_keepalive_vchiq_callback,
		.version     = KEEPALIVE_VER,
		.version_min = KEEPALIVE_VER_MIN
	};

	ret = vchiq_initialise(&instance);
	if (ret) {
		vchiq_log_error(vchiq_susp_log_level,
			"%s vchiq_initialise failed %d", __func__, ret);
		goto exit;
	}

	status = vchiq_connect(instance);
	if (status != VCHIQ_SUCCESS) {
		vchiq_log_error(vchiq_susp_log_level,
			"%s vchiq_connect failed %d", __func__, status);
		goto shutdown;
	}

	status = vchiq_add_service(instance, &params, &ka_handle);
	if (status != VCHIQ_SUCCESS) {
		vchiq_log_error(vchiq_susp_log_level,
			"%s vchiq_open_service failed %d", __func__, status);
		goto shutdown;
	}

	while (1) {
		long rc = 0, uc = 0;

		if (wait_for_completion_interruptible(&arm_state->ka_evt)) {
			vchiq_log_error(vchiq_susp_log_level,
				"%s interrupted", __func__);
			flush_signals(current);
			continue;
		}

		/*
		 * read and clear counters.  Do release_count then use_count to
		 * prevent getting more releases than uses
		 */
		rc = atomic_xchg(&arm_state->ka_release_count, 0);
		uc = atomic_xchg(&arm_state->ka_use_count, 0);

		/*
		 * Call use/release service the requisite number of times.
		 * Process use before release so use counts don't go negative
		 */
		while (uc--) {
			atomic_inc(&arm_state->ka_use_ack_count);
			status = vchiq_use_service(ka_handle);
			if (status != VCHIQ_SUCCESS) {
				vchiq_log_error(vchiq_susp_log_level,
					"%s vchiq_use_service error %d",
					__func__, status);
			}
		}
		while (rc--) {
			status = vchiq_release_service(ka_handle);
			if (status != VCHIQ_SUCCESS) {
				vchiq_log_error(vchiq_susp_log_level,
					"%s vchiq_release_service error %d",
					__func__, status);
			}
		}
	}

shutdown:
	vchiq_shutdown(instance);
exit:
	return 0;
}

void
vchiq_arm_init_state(struct vchiq_state *state,
		     struct vchiq_arm_state *arm_state)
{
	if (arm_state) {
		rwlock_init(&arm_state->susp_res_lock);

		init_completion(&arm_state->ka_evt);
		atomic_set(&arm_state->ka_use_count, 0);
		atomic_set(&arm_state->ka_use_ack_count, 0);
		atomic_set(&arm_state->ka_release_count, 0);

		arm_state->state = state;
		arm_state->first_connect = 0;

	}
}

int
vchiq_use_internal(struct vchiq_state *state, struct vchiq_service *service,
		   enum USE_TYPE_E use_type)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	int ret = 0;
	char entity[16];
	int *entity_uc;
	int local_uc;

	if (!arm_state) {
		ret = -EINVAL;
		goto out;
	}

	if (use_type == USE_TYPE_VCHIQ) {
		sprintf(entity, "VCHIQ:   ");
		entity_uc = &arm_state->peer_use_count;
	} else if (service) {
		sprintf(entity, "%c%c%c%c:%03d",
			VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
			service->client_id);
		entity_uc = &service->service_use_count;
	} else {
		vchiq_log_error(vchiq_susp_log_level, "%s null service ptr", __func__);
		ret = -EINVAL;
		goto out;
	}

	write_lock_bh(&arm_state->susp_res_lock);
	local_uc = ++arm_state->videocore_use_count;
	++(*entity_uc);

	vchiq_log_trace(vchiq_susp_log_level,
		"%s %s count %d, state count %d",
		__func__, entity, *entity_uc, local_uc);

	write_unlock_bh(&arm_state->susp_res_lock);

	if (!ret) {
		enum vchiq_status status = VCHIQ_SUCCESS;
		long ack_cnt = atomic_xchg(&arm_state->ka_use_ack_count, 0);

		while (ack_cnt && (status == VCHIQ_SUCCESS)) {
			/* Send the use notify to videocore */
			status = vchiq_send_remote_use_active(state);
			if (status == VCHIQ_SUCCESS)
				ack_cnt--;
			else
				atomic_add(ack_cnt,
					&arm_state->ka_use_ack_count);
		}
	}

out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit %d", __func__, ret);
	return ret;
}

int
vchiq_release_internal(struct vchiq_state *state, struct vchiq_service *service)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	int ret = 0;
	char entity[16];
	int *entity_uc;

	if (!arm_state) {
		ret = -EINVAL;
		goto out;
	}

	if (service) {
		sprintf(entity, "%c%c%c%c:%03d",
			VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
			service->client_id);
		entity_uc = &service->service_use_count;
	} else {
		sprintf(entity, "PEER:   ");
		entity_uc = &arm_state->peer_use_count;
	}

	write_lock_bh(&arm_state->susp_res_lock);
	if (!arm_state->videocore_use_count || !(*entity_uc)) {
		/* Don't use BUG_ON - don't allow user thread to crash kernel */
		WARN_ON(!arm_state->videocore_use_count);
		WARN_ON(!(*entity_uc));
		ret = -EINVAL;
		goto unlock;
	}
	--arm_state->videocore_use_count;
	--(*entity_uc);

	vchiq_log_trace(vchiq_susp_log_level,
		"%s %s count %d, state count %d",
		__func__, entity, *entity_uc,
		arm_state->videocore_use_count);

unlock:
	write_unlock_bh(&arm_state->susp_res_lock);

out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit %d", __func__, ret);
	return ret;
}

void
vchiq_on_remote_use(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	atomic_inc(&arm_state->ka_use_count);
	complete(&arm_state->ka_evt);
}

void
vchiq_on_remote_release(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	atomic_inc(&arm_state->ka_release_count);
	complete(&arm_state->ka_evt);
}

int
vchiq_use_service_internal(struct vchiq_service *service)
{
	return vchiq_use_internal(service->state, service, USE_TYPE_SERVICE);
}

int
vchiq_release_service_internal(struct vchiq_service *service)
{
	return vchiq_release_internal(service->state, service);
}

struct vchiq_debugfs_node *
vchiq_instance_get_debugfs_node(struct vchiq_instance *instance)
{
	return &instance->debugfs_node;
}

int
vchiq_instance_get_use_count(struct vchiq_instance *instance)
{
	struct vchiq_service *service;
	int use_count = 0, i;

	i = 0;
	rcu_read_lock();
	while ((service = __next_service_by_instance(instance->state,
						     instance, &i)))
		use_count += service->service_use_count;
	rcu_read_unlock();
	return use_count;
}

int
vchiq_instance_get_pid(struct vchiq_instance *instance)
{
	return instance->pid;
}

int
vchiq_instance_get_trace(struct vchiq_instance *instance)
{
	return instance->trace;
}

void
vchiq_instance_set_trace(struct vchiq_instance *instance, int trace)
{
	struct vchiq_service *service;
	int i;

	i = 0;
	rcu_read_lock();
	while ((service = __next_service_by_instance(instance->state,
						     instance, &i)))
		service->trace = trace;
	rcu_read_unlock();
	instance->trace = (trace != 0);
}

enum vchiq_status
vchiq_use_service(unsigned int handle)
{
	enum vchiq_status ret = VCHIQ_ERROR;
	struct vchiq_service *service = find_service_by_handle(handle);

	if (service) {
		ret = vchiq_use_internal(service->state, service,
				USE_TYPE_SERVICE);
		vchiq_service_put(service);
	}
	return ret;
}
EXPORT_SYMBOL(vchiq_use_service);

enum vchiq_status
vchiq_release_service(unsigned int handle)
{
	enum vchiq_status ret = VCHIQ_ERROR;
	struct vchiq_service *service = find_service_by_handle(handle);

	if (service) {
		ret = vchiq_release_internal(service->state, service);
		vchiq_service_put(service);
	}
	return ret;
}
EXPORT_SYMBOL(vchiq_release_service);

struct service_data_struct {
	int fourcc;
	int clientid;
	int use_count;
};

void
vchiq_dump_service_use_state(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	struct service_data_struct *service_data;
	int i, found = 0;
	/*
	 * If there's more than 64 services, only dump ones with
	 * non-zero counts
	 */
	int only_nonzero = 0;
	static const char *nz = "<-- preventing suspend";

	int peer_count;
	int vc_use_count;
	int active_services;

	if (!arm_state)
		return;

	service_data = kmalloc_array(MAX_SERVICES, sizeof(*service_data),
				     GFP_KERNEL);
	if (!service_data)
		return;

	read_lock_bh(&arm_state->susp_res_lock);
	peer_count = arm_state->peer_use_count;
	vc_use_count = arm_state->videocore_use_count;
	active_services = state->unused_service;
	if (active_services > MAX_SERVICES)
		only_nonzero = 1;

	rcu_read_lock();
	for (i = 0; i < active_services; i++) {
		struct vchiq_service *service_ptr =
			rcu_dereference(state->services[i]);

		if (!service_ptr)
			continue;

		if (only_nonzero && !service_ptr->service_use_count)
			continue;

		if (service_ptr->srvstate == VCHIQ_SRVSTATE_FREE)
			continue;

		service_data[found].fourcc = service_ptr->base.fourcc;
		service_data[found].clientid = service_ptr->client_id;
		service_data[found].use_count = service_ptr->service_use_count;
		found++;
		if (found >= MAX_SERVICES)
			break;
	}
	rcu_read_unlock();

	read_unlock_bh(&arm_state->susp_res_lock);

	if (only_nonzero)
		vchiq_log_warning(vchiq_susp_log_level, "Too many active "
			"services (%d).  Only dumping up to first %d services "
			"with non-zero use-count", active_services, found);

	for (i = 0; i < found; i++) {
		vchiq_log_warning(vchiq_susp_log_level,
			"----- %c%c%c%c:%d service count %d %s",
			VCHIQ_FOURCC_AS_4CHARS(service_data[i].fourcc),
			service_data[i].clientid,
			service_data[i].use_count,
			service_data[i].use_count ? nz : "");
	}
	vchiq_log_warning(vchiq_susp_log_level,
		"----- VCHIQ use count count %d", peer_count);
	vchiq_log_warning(vchiq_susp_log_level,
		"--- Overall vchiq instance use count %d", vc_use_count);

	kfree(service_data);
}

enum vchiq_status
vchiq_check_service(struct vchiq_service *service)
{
	struct vchiq_arm_state *arm_state;
	enum vchiq_status ret = VCHIQ_ERROR;

	if (!service || !service->state)
		goto out;

	arm_state = vchiq_platform_get_arm_state(service->state);

	read_lock_bh(&arm_state->susp_res_lock);
	if (service->service_use_count)
		ret = VCHIQ_SUCCESS;
	read_unlock_bh(&arm_state->susp_res_lock);

	if (ret == VCHIQ_ERROR) {
		vchiq_log_error(vchiq_susp_log_level,
			"%s ERROR - %c%c%c%c:%d service count %d, state count %d", __func__,
			VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
			service->client_id, service->service_use_count,
			arm_state->videocore_use_count);
		vchiq_dump_service_use_state(service->state);
	}
out:
	return ret;
}

void vchiq_platform_conn_state_changed(struct vchiq_state *state,
				       enum vchiq_connstate oldstate,
				       enum vchiq_connstate newstate)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	char threadname[16];

	vchiq_log_info(vchiq_susp_log_level, "%d: %s->%s", state->id,
		get_conn_state_name(oldstate), get_conn_state_name(newstate));
	if (state->conn_state != VCHIQ_CONNSTATE_CONNECTED)
		return;

	write_lock_bh(&arm_state->susp_res_lock);
	if (arm_state->first_connect) {
		write_unlock_bh(&arm_state->susp_res_lock);
		return;
	}

	arm_state->first_connect = 1;
	write_unlock_bh(&arm_state->susp_res_lock);
	snprintf(threadname, sizeof(threadname), "vchiq-keep/%d",
		 state->id);
	arm_state->ka_thread = kthread_create(&vchiq_keepalive_thread_func,
					      (void *)state,
					      threadname);
	if (IS_ERR(arm_state->ka_thread)) {
		vchiq_log_error(vchiq_susp_log_level,
				"vchiq: FATAL: couldn't create thread %s",
				threadname);
	} else {
		wake_up_process(arm_state->ka_thread);
	}
}

static const struct of_device_id vchiq_of_match[] = {
	{ .compatible = "brcm,bcm2835-vchiq", .data = &bcm2835_drvdata },
	{ .compatible = "brcm,bcm2836-vchiq", .data = &bcm2836_drvdata },
	{},
};
MODULE_DEVICE_TABLE(of, vchiq_of_match);

static struct platform_device *
vchiq_register_child(struct platform_device *pdev, const char *name)
{
	struct platform_device_info pdevinfo;
	struct platform_device *child;

	memset(&pdevinfo, 0, sizeof(pdevinfo));

	pdevinfo.parent = &pdev->dev;
	pdevinfo.name = name;
	pdevinfo.id = PLATFORM_DEVID_NONE;
	pdevinfo.dma_mask = DMA_BIT_MASK(32);

	child = platform_device_register_full(&pdevinfo);
	if (IS_ERR(child)) {
		dev_warn(&pdev->dev, "%s not registered\n", name);
		child = NULL;
	}

	return child;
}

static int vchiq_probe(struct platform_device *pdev)
{
	struct device_node *fw_node;
	const struct of_device_id *of_id;
	struct vchiq_drvdata *drvdata;
	struct device *vchiq_dev;
	int err;

	of_id = of_match_node(vchiq_of_match, pdev->dev.of_node);
	drvdata = (struct vchiq_drvdata *)of_id->data;
	if (!drvdata)
		return -EINVAL;

	fw_node = of_find_compatible_node(NULL, NULL,
					  "raspberrypi,bcm2835-firmware");
	if (!fw_node) {
		dev_err(&pdev->dev, "Missing firmware node\n");
		return -ENOENT;
	}

	drvdata->fw = devm_rpi_firmware_get(&pdev->dev, fw_node);
	of_node_put(fw_node);
	if (!drvdata->fw)
		return -EPROBE_DEFER;

	platform_set_drvdata(pdev, drvdata);

	err = vchiq_platform_init(pdev, &g_state);
	if (err)
		goto failed_platform_init;

	cdev_init(&vchiq_cdev, &vchiq_fops);
	vchiq_cdev.owner = THIS_MODULE;
	err = cdev_add(&vchiq_cdev, vchiq_devid, 1);
	if (err) {
		vchiq_log_error(vchiq_arm_log_level,
			"Unable to register device");
		goto failed_platform_init;
	}

	vchiq_dev = device_create(vchiq_class, &pdev->dev, vchiq_devid, NULL,
				  "vchiq");
	if (IS_ERR(vchiq_dev)) {
		err = PTR_ERR(vchiq_dev);
		goto failed_device_create;
	}

	vchiq_debugfs_init();

	vchiq_log_info(vchiq_arm_log_level,
		"vchiq: initialised - version %d (min %d), device %d.%d",
		VCHIQ_VERSION, VCHIQ_VERSION_MIN,
		MAJOR(vchiq_devid), MINOR(vchiq_devid));

	bcm2835_camera = vchiq_register_child(pdev, "bcm2835-camera");
	bcm2835_audio = vchiq_register_child(pdev, "bcm2835_audio");

	return 0;

failed_device_create:
	cdev_del(&vchiq_cdev);
failed_platform_init:
	vchiq_log_warning(vchiq_arm_log_level, "could not load vchiq");
	return err;
}

static int vchiq_remove(struct platform_device *pdev)
{
	platform_device_unregister(bcm2835_audio);
	platform_device_unregister(bcm2835_camera);
	vchiq_debugfs_deinit();
	device_destroy(vchiq_class, vchiq_devid);
	cdev_del(&vchiq_cdev);

	return 0;
}

static struct platform_driver vchiq_driver = {
	.driver = {
		.name = "bcm2835_vchiq",
		.of_match_table = vchiq_of_match,
	},
	.probe = vchiq_probe,
	.remove = vchiq_remove,
};

static int __init vchiq_driver_init(void)
{
	int ret;

	vchiq_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(vchiq_class)) {
		pr_err("Failed to create vchiq class\n");
		return PTR_ERR(vchiq_class);
	}

	ret = alloc_chrdev_region(&vchiq_devid, 0, 1, DEVICE_NAME);
	if (ret) {
		pr_err("Failed to allocate vchiq's chrdev region\n");
		goto class_destroy;
	}

	ret = platform_driver_register(&vchiq_driver);
	if (ret) {
		pr_err("Failed to register vchiq driver\n");
		goto region_unregister;
	}

	return 0;

region_unregister:
	unregister_chrdev_region(vchiq_devid, 1);

class_destroy:
	class_destroy(vchiq_class);

	return ret;
}
module_init(vchiq_driver_init);

static void __exit vchiq_driver_exit(void)
{
	platform_driver_unregister(&vchiq_driver);
	unregister_chrdev_region(vchiq_devid, 1);
	class_destroy(vchiq_class);
}
module_exit(vchiq_driver_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Videocore VCHIQ driver");
MODULE_AUTHOR("Broadcom Corporation");
