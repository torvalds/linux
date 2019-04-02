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

#define SUSPEND_TIMER_TIMEOUT_MS 100
#define SUSPEND_RETRY_TIMER_TIMEOUT_MS 1000

#define VC_SUSPEND_NUM_OFFSET 3 /* number of values before idle which are -ve */
static const char *const suspend_state_names[] = {
	"VC_SUSPEND_FORCE_CANCELED",
	"VC_SUSPEND_REJECTED",
	"VC_SUSPEND_FAILED",
	"VC_SUSPEND_IDLE",
	"VC_SUSPEND_REQUESTED",
	"VC_SUSPEND_IN_PROGRESS",
	"VC_SUSPEND_SUSPENDED"
};
#define VC_RESUME_NUM_OFFSET 1 /* number of values before idle which are -ve */
static const char *const resume_state_names[] = {
	"VC_RESUME_FAILED",
	"VC_RESUME_IDLE",
	"VC_RESUME_REQUESTED",
	"VC_RESUME_IN_PROGRESS",
	"VC_RESUME_RESUMED"
};
/* The number of times we allow force suspend to timeout before actually
** _forcing_ suspend.  This is to cater for SW which fails to release vchiq
** correctly - we don't want to prevent ARM suspend indefinitely in this case.
*/
#define FORCE_SUSPEND_FAIL_MAX 8

/* The time in ms allowed for videocore to go idle when force suspend has been
 * requested */
#define FORCE_SUSPEND_TIMEOUT_MS 200

static void suspend_timer_callback(struct timer_list *t);

struct user_service {
	struct vchiq_service *service;
	void *userdata;
	VCHIQ_INSTANCE_T instance;
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

struct vchiq_instance_struct {
	struct vchiq_state *state;
	struct vchiq_completion_data completions[MAX_COMPLETIONS];
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

vchiq_static_assert(ARRAY_SIZE(ioctl_names) ==
		    (VCHIQ_IOC_MAX + 1));

static VCHIQ_STATUS_T
vchiq_blocking_bulk_transfer(VCHIQ_SERVICE_HANDLE_T handle, void *data,
	unsigned int size, VCHIQ_BULK_DIR_T dir);

#define VCHIQ_INIT_RETRIES 10
VCHIQ_STATUS_T vchiq_initialise(VCHIQ_INSTANCE_T *instance_out)
{
	VCHIQ_STATUS_T status = VCHIQ_ERROR;
	struct vchiq_state *state;
	VCHIQ_INSTANCE_T instance = NULL;
	int i;

	vchiq_log_trace(vchiq_core_log_level, "%s called", __func__);

	/* VideoCore may not be ready due to boot up timing.
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
		goto failed;
	}

	instance->connected = 0;
	instance->state = state;
	mutex_init(&instance->bulk_waiter_list_mutex);
	INIT_LIST_HEAD(&instance->bulk_waiter_list);

	*instance_out = instance;

	status = VCHIQ_SUCCESS;

failed:
	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, status);

	return status;
}
EXPORT_SYMBOL(vchiq_initialise);

VCHIQ_STATUS_T vchiq_shutdown(VCHIQ_INSTANCE_T instance)
{
	VCHIQ_STATUS_T status;
	struct vchiq_state *state = instance->state;

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p) called", __func__, instance);

	if (mutex_lock_killable(&state->mutex) != 0)
		return VCHIQ_RETRY;

	/* Remove all services */
	status = vchiq_shutdown_internal(state, instance);

	mutex_unlock(&state->mutex);

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, status);

	if (status == VCHIQ_SUCCESS) {
		struct bulk_waiter_node *waiter, *next;

		list_for_each_entry_safe(waiter, next,
					 &instance->bulk_waiter_list, list) {
			list_del(&waiter->list);
			vchiq_log_info(vchiq_arm_log_level,
					"bulk_waiter - cleaned up %pK for pid %d",
					waiter, waiter->pid);
			kfree(waiter);
		}
		kfree(instance);
	}

	return status;
}
EXPORT_SYMBOL(vchiq_shutdown);

static int vchiq_is_connected(VCHIQ_INSTANCE_T instance)
{
	return instance->connected;
}

VCHIQ_STATUS_T vchiq_connect(VCHIQ_INSTANCE_T instance)
{
	VCHIQ_STATUS_T status;
	struct vchiq_state *state = instance->state;

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p) called", __func__, instance);

	if (mutex_lock_killable(&state->mutex) != 0) {
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

VCHIQ_STATUS_T vchiq_add_service(
	VCHIQ_INSTANCE_T              instance,
	const struct vchiq_service_params *params,
	VCHIQ_SERVICE_HANDLE_T       *phandle)
{
	VCHIQ_STATUS_T status;
	struct vchiq_state *state = instance->state;
	struct vchiq_service *service = NULL;
	int srvstate;

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p) called", __func__, instance);

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
	} else
		status = VCHIQ_ERROR;

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, status);

	return status;
}
EXPORT_SYMBOL(vchiq_add_service);

VCHIQ_STATUS_T vchiq_open_service(
	VCHIQ_INSTANCE_T              instance,
	const struct vchiq_service_params *params,
	VCHIQ_SERVICE_HANDLE_T       *phandle)
{
	VCHIQ_STATUS_T   status = VCHIQ_ERROR;
	struct vchiq_state   *state = instance->state;
	struct vchiq_service *service = NULL;

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p) called", __func__, instance);

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

VCHIQ_STATUS_T
vchiq_bulk_transmit(VCHIQ_SERVICE_HANDLE_T handle, const void *data,
	unsigned int size, void *userdata, VCHIQ_BULK_MODE_T mode)
{
	VCHIQ_STATUS_T status;

	switch (mode) {
	case VCHIQ_BULK_MODE_NOCALLBACK:
	case VCHIQ_BULK_MODE_CALLBACK:
		status = vchiq_bulk_transfer(handle, (void *)data, size,
					     userdata, mode,
					     VCHIQ_BULK_TRANSMIT);
		break;
	case VCHIQ_BULK_MODE_BLOCKING:
		status = vchiq_blocking_bulk_transfer(handle,
			(void *)data, size, VCHIQ_BULK_TRANSMIT);
		break;
	default:
		return VCHIQ_ERROR;
	}

	return status;
}
EXPORT_SYMBOL(vchiq_bulk_transmit);

VCHIQ_STATUS_T
vchiq_bulk_receive(VCHIQ_SERVICE_HANDLE_T handle, void *data,
	unsigned int size, void *userdata, VCHIQ_BULK_MODE_T mode)
{
	VCHIQ_STATUS_T status;

	switch (mode) {
	case VCHIQ_BULK_MODE_NOCALLBACK:
	case VCHIQ_BULK_MODE_CALLBACK:
		status = vchiq_bulk_transfer(handle, data, size, userdata,
					     mode, VCHIQ_BULK_RECEIVE);
		break;
	case VCHIQ_BULK_MODE_BLOCKING:
		status = vchiq_blocking_bulk_transfer(handle,
			(void *)data, size, VCHIQ_BULK_RECEIVE);
		break;
	default:
		return VCHIQ_ERROR;
	}

	return status;
}
EXPORT_SYMBOL(vchiq_bulk_receive);

static VCHIQ_STATUS_T
vchiq_blocking_bulk_transfer(VCHIQ_SERVICE_HANDLE_T handle, void *data,
	unsigned int size, VCHIQ_BULK_DIR_T dir)
{
	VCHIQ_INSTANCE_T instance;
	struct vchiq_service *service;
	VCHIQ_STATUS_T status;
	struct bulk_waiter_node *waiter = NULL;

	service = find_service_by_handle(handle);
	if (!service)
		return VCHIQ_ERROR;

	instance = service->instance;

	unlock_service(service);

	mutex_lock(&instance->bulk_waiter_list_mutex);
	list_for_each_entry(waiter, &instance->bulk_waiter_list, list) {
		if (waiter->pid == current->pid) {
			list_del(&waiter->list);
			break;
		}
	}
	mutex_unlock(&instance->bulk_waiter_list_mutex);

	if (waiter) {
		struct vchiq_bulk *bulk = waiter->bulk_waiter.bulk;

		if (bulk) {
			/* This thread has an outstanding bulk transfer. */
			if ((bulk->data != data) ||
				(bulk->size != size)) {
				/* This is not a retry of the previous one.
				 * Cancel the signal when the transfer
				 * completes.
				 */
				spin_lock(&bulk_waiter_spinlock);
				bulk->userdata = NULL;
				spin_unlock(&bulk_waiter_spinlock);
			}
		}
	}

	if (!waiter) {
		waiter = kzalloc(sizeof(struct bulk_waiter_node), GFP_KERNEL);
		if (!waiter) {
			vchiq_log_error(vchiq_core_log_level,
				"%s - out of memory", __func__);
			return VCHIQ_ERROR;
		}
	}

	status = vchiq_bulk_transfer(handle, data, size, &waiter->bulk_waiter,
				     VCHIQ_BULK_MODE_BLOCKING, dir);
	if ((status != VCHIQ_RETRY) || fatal_signal_pending(current) ||
		!waiter->bulk_waiter.bulk) {
		struct vchiq_bulk *bulk = waiter->bulk_waiter.bulk;

		if (bulk) {
			/* Cancel the signal when the transfer
			 * completes.
			 */
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
/****************************************************************************
*
*   add_completion
*
***************************************************************************/

static VCHIQ_STATUS_T
add_completion(VCHIQ_INSTANCE_T instance, VCHIQ_REASON_T reason,
	       struct vchiq_header *header, struct user_service *user_service,
	       void *bulk_userdata)
{
	struct vchiq_completion_data *completion;
	int insert;

	DEBUG_INITIALISE(g_state.local)

	insert = instance->completion_insert;
	while ((insert - instance->completion_remove) >= MAX_COMPLETIONS) {
		/* Out of space - wait for the client */
		DEBUG_TRACE(SERVICE_CALLBACK_LINE);
		vchiq_log_trace(vchiq_arm_log_level,
			"%s - completion queue full", __func__);
		DEBUG_COUNT(COMPLETION_QUEUE_FULL_COUNT);
		if (wait_for_completion_killable(&instance->remove_event)) {
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
		/* Take an extra reference, to be held until
		   this CLOSED notification is delivered. */
		lock_service(user_service->service);
		if (instance->use_close_delivered)
			user_service->close_pending = 1;
	}

	/* A write barrier is needed here to ensure that the entire completion
		record is written out before the insert point. */
	wmb();

	if (reason == VCHIQ_MESSAGE_AVAILABLE)
		user_service->message_available_pos = insert;

	insert++;
	instance->completion_insert = insert;

	complete(&instance->insert_event);

	return VCHIQ_SUCCESS;
}

/****************************************************************************
*
*   service_callback
*
***************************************************************************/

static VCHIQ_STATUS_T
service_callback(VCHIQ_REASON_T reason, struct vchiq_header *header,
		 VCHIQ_SERVICE_HANDLE_T handle, void *bulk_userdata)
{
	/* How do we ensure the callback goes to the right client?
	** The service_user data points to a user_service record
	** containing the original callback and the user state structure, which
	** contains a circular buffer for completion records.
	*/
	struct user_service *user_service;
	struct vchiq_service *service;
	VCHIQ_INSTANCE_T instance;
	bool skip_completion = false;

	DEBUG_INITIALISE(g_state.local)

	DEBUG_TRACE(SERVICE_CALLBACK_LINE);

	service = handle_to_service(handle);
	BUG_ON(!service);
	user_service = (struct user_service *)service->base.userdata;
	instance = user_service->instance;

	if (!instance || instance->closing)
		return VCHIQ_SUCCESS;

	vchiq_log_trace(vchiq_arm_log_level,
		"%s - service %lx(%d,%p), reason %d, header %lx, "
		"instance %lx, bulk_userdata %lx",
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
			/* If there is no MESSAGE_AVAILABLE in the completion
			** queue, add one
			*/
			if ((user_service->message_available_pos -
				instance->completion_remove) < 0) {
				VCHIQ_STATUS_T status;

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
			if (wait_for_completion_killable(
						&user_service->remove_event)
				!= 0) {
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

		/* If there is a thread waiting in DEQUEUE_MESSAGE, or if
		** there is a MESSAGE_AVAILABLE in the completion queue then
		** bypass the completion queue.
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

/****************************************************************************
*
*   user_service_free
*
***************************************************************************/
static void
user_service_free(void *userdata)
{
	kfree(userdata);
}

/****************************************************************************
*
*   close_delivered
*
***************************************************************************/
static void close_delivered(struct user_service *user_service)
{
	vchiq_log_info(vchiq_arm_log_level,
		"%s(handle=%x)",
		__func__, user_service->service->handle);

	if (user_service->close_pending) {
		/* Allow the underlying service to be culled */
		unlock_service(user_service->service);

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

/**************************************************************************
 *
 *   vchiq_ioc_queue_message
 *
 **************************************************************************/
static VCHIQ_STATUS_T
vchiq_ioc_queue_message(VCHIQ_SERVICE_HANDLE_T handle,
			struct vchiq_element *elements,
			unsigned long count)
{
	struct vchiq_io_copy_callback_context context;
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

	return vchiq_queue_message(handle, vchiq_ioc_copy_element_data,
				   &context, total_size);
}

/****************************************************************************
*
*   vchiq_ioctl
*
***************************************************************************/
static long
vchiq_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	VCHIQ_INSTANCE_T instance = file->private_data;
	VCHIQ_STATUS_T status = VCHIQ_SUCCESS;
	struct vchiq_service *service = NULL;
	long ret = 0;
	int i, rc;

	DEBUG_INITIALISE(g_state.local)

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
			instance, &i)) != NULL) {
			status = vchiq_remove_service(service->handle);
			unlock_service(service);
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
		if (rc != 0) {
			vchiq_log_error(vchiq_arm_log_level,
				"vchiq: connect: could not lock mutex for "
				"state %d: %d",
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
		struct vchiq_create_service args;
		struct user_service *user_service = NULL;
		void *userdata;
		int srvstate;

		if (copy_from_user
			 (&args, (const void __user *)arg,
			  sizeof(args)) != 0) {
			ret = -EFAULT;
			break;
		}

		user_service = kmalloc(sizeof(*user_service), GFP_KERNEL);
		if (!user_service) {
			ret = -ENOMEM;
			break;
		}

		if (args.is_open) {
			if (!instance->connected) {
				ret = -ENOTCONN;
				kfree(user_service);
				break;
			}
			srvstate = VCHIQ_SRVSTATE_OPENING;
		} else {
			srvstate =
				 instance->connected ?
				 VCHIQ_SRVSTATE_LISTENING :
				 VCHIQ_SRVSTATE_HIDDEN;
		}

		userdata = args.params.userdata;
		args.params.callback = service_callback;
		args.params.userdata = user_service;
		service = vchiq_add_service_internal(
				instance->state,
				&args.params, srvstate,
				instance, user_service_free);

		if (service != NULL) {
			user_service->service = service;
			user_service->userdata = userdata;
			user_service->instance = instance;
			user_service->is_vchi = (args.is_vchi != 0);
			user_service->dequeue_pending = 0;
			user_service->close_pending = 0;
			user_service->message_available_pos =
				instance->completion_remove - 1;
			user_service->msg_insert = 0;
			user_service->msg_remove = 0;
			init_completion(&user_service->insert_event);
			init_completion(&user_service->remove_event);
			init_completion(&user_service->close_event);

			if (args.is_open) {
				status = vchiq_open_service_internal
					(service, instance->pid);
				if (status != VCHIQ_SUCCESS) {
					vchiq_remove_service(service->handle);
					service = NULL;
					ret = (status == VCHIQ_RETRY) ?
						-EINTR : -EIO;
					break;
				}
			}

			if (copy_to_user((void __user *)
				&(((struct vchiq_create_service __user *)
					arg)->handle),
				(const void *)&service->handle,
				sizeof(service->handle)) != 0) {
				ret = -EFAULT;
				vchiq_remove_service(service->handle);
			}

			service = NULL;
		} else {
			ret = -EEXIST;
			kfree(user_service);
		}
	} break;

	case VCHIQ_IOC_CLOSE_SERVICE:
	case VCHIQ_IOC_REMOVE_SERVICE: {
		VCHIQ_SERVICE_HANDLE_T handle = (VCHIQ_SERVICE_HANDLE_T)arg;
		struct user_service *user_service;

		service = find_service_for_instance(instance, handle);
		if (!service) {
			ret = -EINVAL;
			break;
		}

		user_service = service->base.userdata;

		/* close_pending is false on first entry, and when the
		   wait in vchiq_close_service has been interrupted. */
		if (!user_service->close_pending) {
			status = (cmd == VCHIQ_IOC_CLOSE_SERVICE) ?
				 vchiq_close_service(service->handle) :
				 vchiq_remove_service(service->handle);
			if (status != VCHIQ_SUCCESS)
				break;
		}

		/* close_pending is true once the underlying service
		   has been closed until the client library calls the
		   CLOSE_DELIVERED ioctl, signalling close_event. */
		if (user_service->close_pending &&
			wait_for_completion_killable(
				&user_service->close_event))
			status = VCHIQ_RETRY;
		break;
	}

	case VCHIQ_IOC_USE_SERVICE:
	case VCHIQ_IOC_RELEASE_SERVICE:	{
		VCHIQ_SERVICE_HANDLE_T handle = (VCHIQ_SERVICE_HANDLE_T)arg;

		service = find_service_for_instance(instance, handle);
		if (service != NULL) {
			status = (cmd == VCHIQ_IOC_USE_SERVICE)	?
				vchiq_use_service_internal(service) :
				vchiq_release_service_internal(service);
			if (status != VCHIQ_SUCCESS) {
				vchiq_log_error(vchiq_susp_log_level,
					"%s: cmd %s returned error %d for "
					"service %c%c%c%c:%03d",
					__func__,
					(cmd == VCHIQ_IOC_USE_SERVICE) ?
						"VCHIQ_IOC_USE_SERVICE" :
						"VCHIQ_IOC_RELEASE_SERVICE",
					status,
					VCHIQ_FOURCC_AS_4CHARS(
						service->base.fourcc),
					service->client_id);
				ret = -EINVAL;
			}
		} else
			ret = -EINVAL;
	} break;

	case VCHIQ_IOC_QUEUE_MESSAGE: {
		struct vchiq_queue_message args;

		if (copy_from_user
			 (&args, (const void __user *)arg,
			  sizeof(args)) != 0) {
			ret = -EFAULT;
			break;
		}

		service = find_service_for_instance(instance, args.handle);

		if ((service != NULL) && (args.count <= MAX_ELEMENTS)) {
			/* Copy elements into kernel space */
			struct vchiq_element elements[MAX_ELEMENTS];

			if (copy_from_user(elements, args.elements,
				args.count * sizeof(struct vchiq_element)) == 0)
				status = vchiq_ioc_queue_message
					(args.handle,
					elements, args.count);
			else
				ret = -EFAULT;
		} else {
			ret = -EINVAL;
		}
	} break;

	case VCHIQ_IOC_QUEUE_BULK_TRANSMIT:
	case VCHIQ_IOC_QUEUE_BULK_RECEIVE: {
		struct vchiq_queue_bulk_transfer args;
		struct bulk_waiter_node *waiter = NULL;

		VCHIQ_BULK_DIR_T dir =
			(cmd == VCHIQ_IOC_QUEUE_BULK_TRANSMIT) ?
			VCHIQ_BULK_TRANSMIT : VCHIQ_BULK_RECEIVE;

		if (copy_from_user
			(&args, (const void __user *)arg,
			sizeof(args)) != 0) {
			ret = -EFAULT;
			break;
		}

		service = find_service_for_instance(instance, args.handle);
		if (!service) {
			ret = -EINVAL;
			break;
		}

		if (args.mode == VCHIQ_BULK_MODE_BLOCKING) {
			waiter = kzalloc(sizeof(struct bulk_waiter_node),
				GFP_KERNEL);
			if (!waiter) {
				ret = -ENOMEM;
				break;
			}

			args.userdata = &waiter->bulk_waiter;
		} else if (args.mode == VCHIQ_BULK_MODE_WAITING) {
			mutex_lock(&instance->bulk_waiter_list_mutex);
			list_for_each_entry(waiter, &instance->bulk_waiter_list,
					    list) {
				if (waiter->pid == current->pid) {
					list_del(&waiter->list);
					break;
				}
			}
			mutex_unlock(&instance->bulk_waiter_list_mutex);
			if (!waiter) {
				vchiq_log_error(vchiq_arm_log_level,
					"no bulk_waiter found for pid %d",
					current->pid);
				ret = -ESRCH;
				break;
			}
			vchiq_log_info(vchiq_arm_log_level,
				"found bulk_waiter %pK for pid %d", waiter,
				current->pid);
			args.userdata = &waiter->bulk_waiter;
		}

		status = vchiq_bulk_transfer(args.handle, args.data, args.size,
					     args.userdata, args.mode, dir);

		if (!waiter)
			break;

		if ((status != VCHIQ_RETRY) || fatal_signal_pending(current) ||
			!waiter->bulk_waiter.bulk) {
			if (waiter->bulk_waiter.bulk) {
				/* Cancel the signal when the transfer
				** completes. */
				spin_lock(&bulk_waiter_spinlock);
				waiter->bulk_waiter.bulk->userdata = NULL;
				spin_unlock(&bulk_waiter_spinlock);
			}
			kfree(waiter);
		} else {
			const VCHIQ_BULK_MODE_T mode_waiting =
				VCHIQ_BULK_MODE_WAITING;
			waiter->pid = current->pid;
			mutex_lock(&instance->bulk_waiter_list_mutex);
			list_add(&waiter->list, &instance->bulk_waiter_list);
			mutex_unlock(&instance->bulk_waiter_list_mutex);
			vchiq_log_info(vchiq_arm_log_level,
				"saved bulk_waiter %pK for pid %d",
				waiter, current->pid);

			if (copy_to_user((void __user *)
				&(((struct vchiq_queue_bulk_transfer __user *)
					arg)->mode),
				(const void *)&mode_waiting,
				sizeof(mode_waiting)) != 0)
				ret = -EFAULT;
		}
	} break;

	case VCHIQ_IOC_AWAIT_COMPLETION: {
		struct vchiq_await_completion args;

		DEBUG_TRACE(AWAIT_COMPLETION_LINE);
		if (!instance->connected) {
			ret = -ENOTCONN;
			break;
		}

		if (copy_from_user(&args, (const void __user *)arg,
			sizeof(args)) != 0) {
			ret = -EFAULT;
			break;
		}

		mutex_lock(&instance->completion_mutex);

		DEBUG_TRACE(AWAIT_COMPLETION_LINE);
		while ((instance->completion_remove ==
			instance->completion_insert)
			&& !instance->closing) {
			int rc;

			DEBUG_TRACE(AWAIT_COMPLETION_LINE);
			mutex_unlock(&instance->completion_mutex);
			rc = wait_for_completion_killable(
						&instance->insert_event);
			mutex_lock(&instance->completion_mutex);
			if (rc != 0) {
				DEBUG_TRACE(AWAIT_COMPLETION_LINE);
				vchiq_log_info(vchiq_arm_log_level,
					"AWAIT_COMPLETION interrupted");
				ret = -EINTR;
				break;
			}
		}
		DEBUG_TRACE(AWAIT_COMPLETION_LINE);

		if (ret == 0) {
			int msgbufcount = args.msgbufcount;
			int remove = instance->completion_remove;

			for (ret = 0; ret < args.count; ret++) {
				struct vchiq_completion_data *completion;
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
				completion->service_userdata =
					user_service->userdata;

				header = completion->header;
				if (header) {
					void __user *msgbuf;
					int msglen;

					msglen = header->size +
						sizeof(struct vchiq_header);
					/* This must be a VCHIQ-style service */
					if (args.msgbufsize < msglen) {
						vchiq_log_error(
							vchiq_arm_log_level,
							"header %pK: msgbufsize %x < msglen %x",
							header, args.msgbufsize,
							msglen);
						WARN(1, "invalid message "
							"size\n");
						if (ret == 0)
							ret = -EMSGSIZE;
						break;
					}
					if (msgbufcount <= 0)
						/* Stall here for lack of a
						** buffer for the message. */
						break;
					/* Get the pointer from user space */
					msgbufcount--;
					if (copy_from_user(&msgbuf,
						(const void __user *)
						&args.msgbufs[msgbufcount],
						sizeof(msgbuf)) != 0) {
						if (ret == 0)
							ret = -EFAULT;
						break;
					}

					/* Copy the message to user space */
					if (copy_to_user(msgbuf, header,
						msglen) != 0) {
						if (ret == 0)
							ret = -EFAULT;
						break;
					}

					/* Now it has been copied, the message
					** can be released. */
					vchiq_release_message(service->handle,
						header);

					/* The completion must point to the
					** msgbuf. */
					completion->header = msgbuf;
				}

				if ((completion->reason ==
					VCHIQ_SERVICE_CLOSED) &&
					!instance->use_close_delivered)
					unlock_service(service);

				if (copy_to_user((void __user *)(
					(size_t)args.buf + ret *
					sizeof(struct vchiq_completion_data)),
					completion,
					sizeof(struct vchiq_completion_data))
									!= 0) {
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

			if (msgbufcount != args.msgbufcount) {
				if (copy_to_user((void __user *)
					&((struct vchiq_await_completion *)arg)
						->msgbufcount,
					&msgbufcount,
					sizeof(msgbufcount)) != 0) {
					ret = -EFAULT;
				}
			}
		}

		if (ret != 0)
			complete(&instance->remove_event);
		mutex_unlock(&instance->completion_mutex);
		DEBUG_TRACE(AWAIT_COMPLETION_LINE);
	} break;

	case VCHIQ_IOC_DEQUEUE_MESSAGE: {
		struct vchiq_dequeue_message args;
		struct user_service *user_service;
		struct vchiq_header *header;

		DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
		if (copy_from_user
			 (&args, (const void __user *)arg,
			  sizeof(args)) != 0) {
			ret = -EFAULT;
			break;
		}
		service = find_service_for_instance(instance, args.handle);
		if (!service) {
			ret = -EINVAL;
			break;
		}
		user_service = (struct user_service *)service->base.userdata;
		if (user_service->is_vchi == 0) {
			ret = -EINVAL;
			break;
		}

		spin_lock(&msg_queue_spinlock);
		if (user_service->msg_remove == user_service->msg_insert) {
			if (!args.blocking) {
				spin_unlock(&msg_queue_spinlock);
				DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
				ret = -EWOULDBLOCK;
				break;
			}
			user_service->dequeue_pending = 1;
			do {
				spin_unlock(&msg_queue_spinlock);
				DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
				if (wait_for_completion_killable(
					&user_service->insert_event)) {
					vchiq_log_info(vchiq_arm_log_level,
						"DEQUEUE_MESSAGE interrupted");
					ret = -EINTR;
					break;
				}
				spin_lock(&msg_queue_spinlock);
			} while (user_service->msg_remove ==
				user_service->msg_insert);

			if (ret)
				break;
		}

		BUG_ON((int)(user_service->msg_insert -
			user_service->msg_remove) < 0);

		header = user_service->msg_queue[user_service->msg_remove &
			(MSG_QUEUE_SIZE - 1)];
		user_service->msg_remove++;
		spin_unlock(&msg_queue_spinlock);

		complete(&user_service->remove_event);
		if (header == NULL)
			ret = -ENOTCONN;
		else if (header->size <= args.bufsize) {
			/* Copy to user space if msgbuf is not NULL */
			if ((args.buf == NULL) ||
				(copy_to_user((void __user *)args.buf,
				header->data,
				header->size) == 0)) {
				ret = header->size;
				vchiq_release_message(
					service->handle,
					header);
			} else
				ret = -EFAULT;
		} else {
			vchiq_log_error(vchiq_arm_log_level,
				"header %pK: bufsize %x < size %x",
				header, args.bufsize, header->size);
			WARN(1, "invalid size\n");
			ret = -EMSGSIZE;
		}
		DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
	} break;

	case VCHIQ_IOC_GET_CLIENT_ID: {
		VCHIQ_SERVICE_HANDLE_T handle = (VCHIQ_SERVICE_HANDLE_T)arg;

		ret = vchiq_get_client_id(handle);
	} break;

	case VCHIQ_IOC_GET_CONFIG: {
		struct vchiq_get_config args;
		struct vchiq_config config;

		if (copy_from_user(&args, (const void __user *)arg,
			sizeof(args)) != 0) {
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

		if (copy_from_user(
			&args, (const void __user *)arg,
			sizeof(args)) != 0) {
			ret = -EFAULT;
			break;
		}

		service = find_service_for_instance(instance, args.handle);
		if (!service) {
			ret = -EINVAL;
			break;
		}

		status = vchiq_set_service_option(
				args.handle, args.option, args.value);
	} break;

	case VCHIQ_IOC_LIB_VERSION: {
		unsigned int lib_version = (unsigned int)arg;

		if (lib_version < VCHIQ_VERSION_MIN)
			ret = -EINVAL;
		else if (lib_version >= VCHIQ_VERSION_CLOSE_DELIVERED)
			instance->use_close_delivered = 1;
	} break;

	case VCHIQ_IOC_CLOSE_DELIVERED: {
		VCHIQ_SERVICE_HANDLE_T handle = (VCHIQ_SERVICE_HANDLE_T)arg;

		service = find_closed_service_for_instance(instance, handle);
		if (service != NULL) {
			struct user_service *user_service =
				(struct user_service *)service->base.userdata;
			close_delivered(user_service);
		} else
			ret = -EINVAL;
	} break;

	default:
		ret = -ENOTTY;
		break;
	}

	if (service)
		unlock_service(service);

	if (ret == 0) {
		if (status == VCHIQ_ERROR)
			ret = -EIO;
		else if (status == VCHIQ_RETRY)
			ret = -EINTR;
	}

	if ((status == VCHIQ_SUCCESS) && (ret < 0) && (ret != -EINTR) &&
		(ret != -EWOULDBLOCK))
		vchiq_log_info(vchiq_arm_log_level,
			"  ioctl instance %lx, cmd %s -> status %d, %ld",
			(unsigned long)instance,
			(_IOC_NR(cmd) <= VCHIQ_IOC_MAX) ?
				ioctl_names[_IOC_NR(cmd)] :
				"<invalid>",
			status, ret);
	else
		vchiq_log_trace(vchiq_arm_log_level,
			"  ioctl instance %lx, cmd %s -> status %d, %ld",
			(unsigned long)instance,
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
vchiq_compat_ioctl_create_service(
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vchiq_create_service __user *args;
	struct vchiq_create_service32 __user *ptrargs32 =
		(struct vchiq_create_service32 __user *)arg;
	struct vchiq_create_service32 args32;
	long ret;

	args = compat_alloc_user_space(sizeof(*args));
	if (!args)
		return -EFAULT;

	if (copy_from_user(&args32, ptrargs32, sizeof(args32)))
		return -EFAULT;

	if (put_user(args32.params.fourcc, &args->params.fourcc) ||
	    put_user(compat_ptr(args32.params.callback),
		     &args->params.callback) ||
	    put_user(compat_ptr(args32.params.userdata),
		     &args->params.userdata) ||
	    put_user(args32.params.version, &args->params.version) ||
	    put_user(args32.params.version_min,
		     &args->params.version_min) ||
	    put_user(args32.is_open, &args->is_open) ||
	    put_user(args32.is_vchi, &args->is_vchi) ||
	    put_user(args32.handle, &args->handle))
		return -EFAULT;

	ret = vchiq_ioctl(file, VCHIQ_IOC_CREATE_SERVICE, (unsigned long)args);

	if (ret < 0)
		return ret;

	if (get_user(args32.handle, &args->handle))
		return -EFAULT;

	if (copy_to_user(&ptrargs32->handle,
			 &args32.handle,
			 sizeof(args32.handle)))
		return -EFAULT;

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
				 unsigned long arg)
{
	struct vchiq_queue_message __user *args;
	struct vchiq_element __user *elements;
	struct vchiq_queue_message32 args32;
	unsigned int count;

	if (copy_from_user(&args32,
			   (struct vchiq_queue_message32 __user *)arg,
			   sizeof(args32)))
		return -EFAULT;

	args = compat_alloc_user_space(sizeof(*args) +
				       (sizeof(*elements) * MAX_ELEMENTS));

	if (!args)
		return -EFAULT;

	if (put_user(args32.handle, &args->handle) ||
	    put_user(args32.count, &args->count) ||
	    put_user(compat_ptr(args32.elements), &args->elements))
		return -EFAULT;

	if (args32.count > MAX_ELEMENTS)
		return -EINVAL;

	if (args32.elements && args32.count) {
		struct vchiq_element32 tempelement32[MAX_ELEMENTS];

		elements = (struct vchiq_element __user *)(args + 1);

		if (copy_from_user(&tempelement32,
				   compat_ptr(args32.elements),
				   sizeof(tempelement32)))
			return -EFAULT;

		for (count = 0; count < args32.count; count++) {
			if (put_user(compat_ptr(tempelement32[count].data),
				     &elements[count].data) ||
			    put_user(tempelement32[count].size,
				     &elements[count].size))
				return -EFAULT;
		}

		if (put_user(elements, &args->elements))
			return -EFAULT;
	}

	return vchiq_ioctl(file, VCHIQ_IOC_QUEUE_MESSAGE, (unsigned long)args);
}

struct vchiq_queue_bulk_transfer32 {
	unsigned int handle;
	compat_uptr_t data;
	unsigned int size;
	compat_uptr_t userdata;
	VCHIQ_BULK_MODE_T mode;
};

#define VCHIQ_IOC_QUEUE_BULK_TRANSMIT32 \
	_IOWR(VCHIQ_IOC_MAGIC, 5, struct vchiq_queue_bulk_transfer32)
#define VCHIQ_IOC_QUEUE_BULK_RECEIVE32 \
	_IOWR(VCHIQ_IOC_MAGIC, 6, struct vchiq_queue_bulk_transfer32)

static long
vchiq_compat_ioctl_queue_bulk(struct file *file,
			      unsigned int cmd,
			      unsigned long arg)
{
	struct vchiq_queue_bulk_transfer __user *args;
	struct vchiq_queue_bulk_transfer32 args32;
	struct vchiq_queue_bulk_transfer32 __user *ptrargs32 =
		(struct vchiq_queue_bulk_transfer32 __user *)arg;
	long ret;

	args = compat_alloc_user_space(sizeof(*args));
	if (!args)
		return -EFAULT;

	if (copy_from_user(&args32, ptrargs32, sizeof(args32)))
		return -EFAULT;

	if (put_user(args32.handle, &args->handle) ||
	    put_user(compat_ptr(args32.data), &args->data) ||
	    put_user(args32.size, &args->size) ||
	    put_user(compat_ptr(args32.userdata), &args->userdata) ||
	    put_user(args32.mode, &args->mode))
		return -EFAULT;

	if (cmd == VCHIQ_IOC_QUEUE_BULK_TRANSMIT32)
		cmd = VCHIQ_IOC_QUEUE_BULK_TRANSMIT;
	else
		cmd = VCHIQ_IOC_QUEUE_BULK_RECEIVE;

	ret = vchiq_ioctl(file, cmd, (unsigned long)args);

	if (ret < 0)
		return ret;

	if (get_user(args32.mode, &args->mode))
		return -EFAULT;

	if (copy_to_user(&ptrargs32->mode,
			 &args32.mode,
			 sizeof(args32.mode)))
		return -EFAULT;

	return 0;
}

struct vchiq_completion_data32 {
	VCHIQ_REASON_T reason;
	compat_uptr_t header;
	compat_uptr_t service_userdata;
	compat_uptr_t bulk_userdata;
};

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
				    unsigned long arg)
{
	struct vchiq_await_completion __user *args;
	struct vchiq_completion_data __user *completion;
	struct vchiq_completion_data completiontemp;
	struct vchiq_await_completion32 args32;
	struct vchiq_completion_data32 completion32;
	unsigned int __user *msgbufcount32;
	unsigned int msgbufcount_native;
	compat_uptr_t msgbuf32;
	void __user *msgbuf;
	void * __user *msgbufptr;
	long ret;

	args = compat_alloc_user_space(sizeof(*args) +
				       sizeof(*completion) +
				       sizeof(*msgbufptr));
	if (!args)
		return -EFAULT;

	completion = (struct vchiq_completion_data __user *)(args + 1);
	msgbufptr = (void * __user *)(completion + 1);

	if (copy_from_user(&args32,
			   (struct vchiq_completion_data32 __user *)arg,
			   sizeof(args32)))
		return -EFAULT;

	if (put_user(args32.count, &args->count) ||
	    put_user(compat_ptr(args32.buf), &args->buf) ||
	    put_user(args32.msgbufsize, &args->msgbufsize) ||
	    put_user(args32.msgbufcount, &args->msgbufcount) ||
	    put_user(compat_ptr(args32.msgbufs), &args->msgbufs))
		return -EFAULT;

	/* These are simple cases, so just fall into the native handler */
	if (!args32.count || !args32.buf || !args32.msgbufcount)
		return vchiq_ioctl(file,
				   VCHIQ_IOC_AWAIT_COMPLETION,
				   (unsigned long)args);

	/*
	 * These are the more complex cases.  Typical applications of this
	 * ioctl will use a very large count, with a very large msgbufcount.
	 * Since the native ioctl can asynchronously fill in the returned
	 * buffers and the application can in theory begin processing messages
	 * even before the ioctl returns, a bit of a trick is used here.
	 *
	 * By forcing both count and msgbufcount to be 1, it forces the native
	 * ioctl to only claim at most 1 message is available.   This tricks
	 * the calling application into thinking only 1 message was actually
	 * available in the queue so like all good applications it will retry
	 * waiting until all the required messages are received.
	 *
	 * This trick has been tested and proven to work with vchiq_test,
	 * Minecraft_PI, the "hello pi" examples, and various other
	 * applications that are included in Raspbian.
	 */

	if (copy_from_user(&msgbuf32,
			   compat_ptr(args32.msgbufs) +
			   (sizeof(compat_uptr_t) *
			   (args32.msgbufcount - 1)),
			   sizeof(msgbuf32)))
		return -EFAULT;

	msgbuf = compat_ptr(msgbuf32);

	if (copy_to_user(msgbufptr,
			 &msgbuf,
			 sizeof(msgbuf)))
		return -EFAULT;

	if (copy_to_user(&args->msgbufs,
			 &msgbufptr,
			 sizeof(msgbufptr)))
		return -EFAULT;

	if (put_user(1U, &args->count) ||
	    put_user(completion, &args->buf) ||
	    put_user(1U, &args->msgbufcount))
		return -EFAULT;

	ret = vchiq_ioctl(file,
			  VCHIQ_IOC_AWAIT_COMPLETION,
			  (unsigned long)args);

	/*
	 * An return value of 0 here means that no messages where available
	 * in the message queue.  In this case the native ioctl does not
	 * return any data to the application at all.  Not even to update
	 * msgbufcount.  This functionality needs to be kept here for
	 * compatibility.
	 *
	 * Of course, < 0 means that an error occurred and no data is being
	 * returned.
	 *
	 * Since count and msgbufcount was forced to 1, that means
	 * the only other possible return value is 1. Meaning that 1 message
	 * was available, so that multiple message case does not need to be
	 * handled here.
	 */
	if (ret <= 0)
		return ret;

	if (copy_from_user(&completiontemp, completion, sizeof(*completion)))
		return -EFAULT;

	completion32.reason = completiontemp.reason;
	completion32.header = ptr_to_compat(completiontemp.header);
	completion32.service_userdata =
		ptr_to_compat(completiontemp.service_userdata);
	completion32.bulk_userdata =
		ptr_to_compat(completiontemp.bulk_userdata);

	if (copy_to_user(compat_ptr(args32.buf),
			 &completion32,
			 sizeof(completion32)))
		return -EFAULT;

	if (get_user(msgbufcount_native, &args->msgbufcount))
		return -EFAULT;

	if (!msgbufcount_native)
		args32.msgbufcount--;

	msgbufcount32 =
		&((struct vchiq_await_completion32 __user *)arg)->msgbufcount;

	if (copy_to_user(msgbufcount32,
			 &args32.msgbufcount,
			 sizeof(args32.msgbufcount)))
		return -EFAULT;

	return 1;
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
				   unsigned long arg)
{
	struct vchiq_dequeue_message __user *args;
	struct vchiq_dequeue_message32 args32;

	args = compat_alloc_user_space(sizeof(*args));
	if (!args)
		return -EFAULT;

	if (copy_from_user(&args32,
			   (struct vchiq_dequeue_message32 __user *)arg,
			   sizeof(args32)))
		return -EFAULT;

	if (put_user(args32.handle, &args->handle) ||
	    put_user(args32.blocking, &args->blocking) ||
	    put_user(args32.bufsize, &args->bufsize) ||
	    put_user(compat_ptr(args32.buf), &args->buf))
		return -EFAULT;

	return vchiq_ioctl(file, VCHIQ_IOC_DEQUEUE_MESSAGE,
			   (unsigned long)args);
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
			      unsigned long arg)
{
	struct vchiq_get_config __user *args;
	struct vchiq_get_config32 args32;

	args = compat_alloc_user_space(sizeof(*args));
	if (!args)
		return -EFAULT;

	if (copy_from_user(&args32,
			   (struct vchiq_get_config32 __user *)arg,
			   sizeof(args32)))
		return -EFAULT;

	if (put_user(args32.config_size, &args->config_size) ||
	    put_user(compat_ptr(args32.pconfig), &args->pconfig))
		return -EFAULT;

	return vchiq_ioctl(file, VCHIQ_IOC_GET_CONFIG, (unsigned long)args);
}

static long
vchiq_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case VCHIQ_IOC_CREATE_SERVICE32:
		return vchiq_compat_ioctl_create_service(file, cmd, arg);
	case VCHIQ_IOC_QUEUE_MESSAGE32:
		return vchiq_compat_ioctl_queue_message(file, cmd, arg);
	case VCHIQ_IOC_QUEUE_BULK_TRANSMIT32:
	case VCHIQ_IOC_QUEUE_BULK_RECEIVE32:
		return vchiq_compat_ioctl_queue_bulk(file, cmd, arg);
	case VCHIQ_IOC_AWAIT_COMPLETION32:
		return vchiq_compat_ioctl_await_completion(file, cmd, arg);
	case VCHIQ_IOC_DEQUEUE_MESSAGE32:
		return vchiq_compat_ioctl_dequeue_message(file, cmd, arg);
	case VCHIQ_IOC_GET_CONFIG32:
		return vchiq_compat_ioctl_get_config(file, cmd, arg);
	default:
		return vchiq_ioctl(file, cmd, arg);
	}
}

#endif

static int vchiq_open(struct inode *inode, struct file *file)
{
	struct vchiq_state *state = vchiq_get_state();
	VCHIQ_INSTANCE_T instance;

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
	VCHIQ_INSTANCE_T instance = file->private_data;
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
		unlock_service(service);
	}

	/* ...and wait for them to die */
	i = 0;
	while ((service = next_service_by_instance(state, instance, &i))) {
		struct user_service *user_service = service->base.userdata;

		wait_for_completion(&service->remove_event);

		BUG_ON(service->srvstate != VCHIQ_SRVSTATE_FREE);

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

		unlock_service(service);
	}

	/* Release any closed services */
	while (instance->completion_remove !=
		instance->completion_insert) {
		struct vchiq_completion_data *completion;
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
			unlock_service(service);
		}
		instance->completion_remove++;
	}

	/* Release the PEER service count. */
	vchiq_release_internal(instance->state, NULL);

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

	vchiq_debugfs_remove_instance(instance);

	kfree(instance);
	file->private_data = NULL;

out:
	return ret;
}

/****************************************************************************
*
*   vchiq_dump
*
***************************************************************************/

void
vchiq_dump(void *dump_context, const char *str, int len)
{
	struct dump_context *context = (struct dump_context *)dump_context;

	if (context->actual < context->space) {
		int copy_bytes;

		if (context->offset > 0) {
			int skip_bytes = min(len, (int)context->offset);

			str += skip_bytes;
			len -= skip_bytes;
			context->offset -= skip_bytes;
			if (context->offset > 0)
				return;
		}
		copy_bytes = min(len, (int)(context->space - context->actual));
		if (copy_bytes == 0)
			return;
		if (copy_to_user(context->buf + context->actual, str,
			copy_bytes))
			context->actual = -EFAULT;
		context->actual += copy_bytes;
		len -= copy_bytes;

		/* If tne terminating NUL is included in the length, then it
		** marks the end of a line and should be replaced with a
		** carriage return. */
		if ((len == 0) && (str[copy_bytes - 1] == '\0')) {
			char cr = '\n';

			if (copy_to_user(context->buf + context->actual - 1,
				&cr, 1))
				context->actual = -EFAULT;
		}
	}
}

/****************************************************************************
*
*   vchiq_dump_platform_instance_state
*
***************************************************************************/

void
vchiq_dump_platform_instances(void *dump_context)
{
	struct vchiq_state *state = vchiq_get_state();
	char buf[80];
	int len;
	int i;

	/* There is no list of instances, so instead scan all services,
		marking those that have been dumped. */

	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service = state->services[i];
		VCHIQ_INSTANCE_T instance;

		if (service && (service->base.callback == service_callback)) {
			instance = service->instance;
			if (instance)
				instance->mark = 0;
		}
	}

	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service = state->services[i];
		VCHIQ_INSTANCE_T instance;

		if (service && (service->base.callback == service_callback)) {
			instance = service->instance;
			if (instance && !instance->mark) {
				len = snprintf(buf, sizeof(buf),
					"Instance %pK: pid %d,%s completions %d/%d",
					instance, instance->pid,
					instance->connected ? " connected, " :
						"",
					instance->completion_insert -
						instance->completion_remove,
					MAX_COMPLETIONS);

				vchiq_dump(dump_context, buf, len + 1);

				instance->mark = 1;
			}
		}
	}
}

/****************************************************************************
*
*   vchiq_dump_platform_service_state
*
***************************************************************************/

void
vchiq_dump_platform_service_state(void *dump_context,
				  struct vchiq_service *service)
{
	struct user_service *user_service =
			(struct user_service *)service->base.userdata;
	char buf[80];
	int len;

	len = snprintf(buf, sizeof(buf), "  instance %pK", service->instance);

	if ((service->base.callback == service_callback) &&
		user_service->is_vchi) {
		len += snprintf(buf + len, sizeof(buf) - len,
			", %d/%d messages",
			user_service->msg_insert - user_service->msg_remove,
			MSG_QUEUE_SIZE);

		if (user_service->dequeue_pending)
			len += snprintf(buf + len, sizeof(buf) - len,
				" (dequeue pending)");
	}

	vchiq_dump(dump_context, buf, len + 1);
}

/****************************************************************************
*
*   vchiq_read
*
***************************************************************************/

static ssize_t
vchiq_read(struct file *file, char __user *buf,
	size_t count, loff_t *ppos)
{
	struct dump_context context;

	context.buf = buf;
	context.actual = 0;
	context.space = count;
	context.offset = *ppos;

	vchiq_dump_state(&context, &g_state);

	*ppos += context.actual;

	return context.actual;
}

struct vchiq_state *
vchiq_get_state(void)
{

	if (g_state.remote == NULL)
		printk(KERN_ERR "%s: g_state.remote == NULL\n", __func__);
	else if (g_state.remote->initialised != 1)
		printk(KERN_NOTICE "%s: g_state.remote->initialised != 1 (%d)\n",
			__func__, g_state.remote->initialised);

	return ((g_state.remote != NULL) &&
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

int
vchiq_videocore_wanted(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	if (!arm_state)
		/* autosuspend not supported - always return wanted */
		return 1;
	else if (arm_state->blocked_count)
		return 1;
	else if (!arm_state->videocore_use_count)
		/* usage count zero - check for override unless we're forcing */
		if (arm_state->resume_blocked)
			return 0;
		else
			return vchiq_platform_videocore_wanted(state);
	else
		/* non-zero usage count - videocore still required */
		return 1;
}

static VCHIQ_STATUS_T
vchiq_keepalive_vchiq_callback(VCHIQ_REASON_T reason,
	struct vchiq_header *header,
	VCHIQ_SERVICE_HANDLE_T service_user,
	void *bulk_user)
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

	VCHIQ_STATUS_T status;
	VCHIQ_INSTANCE_T instance;
	VCHIQ_SERVICE_HANDLE_T ka_handle;

	struct vchiq_service_params params = {
		.fourcc      = VCHIQ_MAKE_FOURCC('K', 'E', 'E', 'P'),
		.callback    = vchiq_keepalive_vchiq_callback,
		.version     = KEEPALIVE_VER,
		.version_min = KEEPALIVE_VER_MIN
	};

	status = vchiq_initialise(&instance);
	if (status != VCHIQ_SUCCESS) {
		vchiq_log_error(vchiq_susp_log_level,
			"%s vchiq_initialise failed %d", __func__, status);
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

		if (wait_for_completion_killable(&arm_state->ka_evt)
				!= 0) {
			vchiq_log_error(vchiq_susp_log_level,
				"%s interrupted", __func__);
			flush_signals(current);
			continue;
		}

		/* read and clear counters.  Do release_count then use_count to
		 * prevent getting more releases than uses */
		rc = atomic_xchg(&arm_state->ka_release_count, 0);
		uc = atomic_xchg(&arm_state->ka_use_count, 0);

		/* Call use/release service the requisite number of times.
		 * Process use before release so use counts don't go negative */
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

VCHIQ_STATUS_T
vchiq_arm_init_state(struct vchiq_state *state,
		     struct vchiq_arm_state *arm_state)
{
	if (arm_state) {
		rwlock_init(&arm_state->susp_res_lock);

		init_completion(&arm_state->ka_evt);
		atomic_set(&arm_state->ka_use_count, 0);
		atomic_set(&arm_state->ka_use_ack_count, 0);
		atomic_set(&arm_state->ka_release_count, 0);

		init_completion(&arm_state->vc_suspend_complete);

		init_completion(&arm_state->vc_resume_complete);
		/* Initialise to 'done' state.  We only want to block on resume
		 * completion while videocore is suspended. */
		set_resume_state(arm_state, VC_RESUME_RESUMED);

		init_completion(&arm_state->resume_blocker);
		/* Initialise to 'done' state.  We only want to block on this
		 * completion while resume is blocked */
		complete_all(&arm_state->resume_blocker);

		init_completion(&arm_state->blocked_blocker);
		/* Initialise to 'done' state.  We only want to block on this
		 * completion while things are waiting on the resume blocker */
		complete_all(&arm_state->blocked_blocker);

		arm_state->suspend_timer_timeout = SUSPEND_TIMER_TIMEOUT_MS;
		arm_state->suspend_timer_running = 0;
		arm_state->state = state;
		timer_setup(&arm_state->suspend_timer, suspend_timer_callback,
			    0);

		arm_state->first_connect = 0;

	}
	return VCHIQ_SUCCESS;
}

/*
** Functions to modify the state variables;
**	set_suspend_state
**	set_resume_state
**
** There are more state variables than we might like, so ensure they remain in
** step.  Suspend and resume state are maintained separately, since most of
** these state machines can operate independently.  However, there are a few
** states where state transitions in one state machine cause a reset to the
** other state machine.  In addition, there are some completion events which
** need to occur on state machine reset and end-state(s), so these are also
** dealt with in these functions.
**
** In all states we set the state variable according to the input, but in some
** cases we perform additional steps outlined below;
**
** VC_SUSPEND_IDLE - Initialise the suspend completion at the same time.
**			The suspend completion is completed after any suspend
**			attempt.  When we reset the state machine we also reset
**			the completion.  This reset occurs when videocore is
**			resumed, and also if we initiate suspend after a suspend
**			failure.
**
** VC_SUSPEND_IN_PROGRESS - This state is considered the point of no return for
**			suspend - ie from this point on we must try to suspend
**			before resuming can occur.  We therefore also reset the
**			resume state machine to VC_RESUME_IDLE in this state.
**
** VC_SUSPEND_SUSPENDED - Suspend has completed successfully. Also call
**			complete_all on the suspend completion to notify
**			anything waiting for suspend to happen.
**
** VC_SUSPEND_REJECTED - Videocore rejected suspend. Videocore will also
**			initiate resume, so no need to alter resume state.
**			We call complete_all on the suspend completion to notify
**			of suspend rejection.
**
** VC_SUSPEND_FAILED - We failed to initiate videocore suspend.  We notify the
**			suspend completion and reset the resume state machine.
**
** VC_RESUME_IDLE - Initialise the resume completion at the same time.  The
**			resume completion is in it's 'done' state whenever
**			videcore is running.  Therefore, the VC_RESUME_IDLE
**			state implies that videocore is suspended.
**			Hence, any thread which needs to wait until videocore is
**			running can wait on this completion - it will only block
**			if videocore is suspended.
**
** VC_RESUME_RESUMED - Resume has completed successfully.  Videocore is running.
**			Call complete_all on the resume completion to unblock
**			any threads waiting for resume.	 Also reset the suspend
**			state machine to it's idle state.
**
** VC_RESUME_FAILED - Currently unused - no mechanism to fail resume exists.
*/

void
set_suspend_state(struct vchiq_arm_state *arm_state,
		  enum vc_suspend_status new_state)
{
	/* set the state in all cases */
	arm_state->vc_suspend_state = new_state;

	/* state specific additional actions */
	switch (new_state) {
	case VC_SUSPEND_FORCE_CANCELED:
		complete_all(&arm_state->vc_suspend_complete);
		break;
	case VC_SUSPEND_REJECTED:
		complete_all(&arm_state->vc_suspend_complete);
		break;
	case VC_SUSPEND_FAILED:
		complete_all(&arm_state->vc_suspend_complete);
		arm_state->vc_resume_state = VC_RESUME_RESUMED;
		complete_all(&arm_state->vc_resume_complete);
		break;
	case VC_SUSPEND_IDLE:
		reinit_completion(&arm_state->vc_suspend_complete);
		break;
	case VC_SUSPEND_REQUESTED:
		break;
	case VC_SUSPEND_IN_PROGRESS:
		set_resume_state(arm_state, VC_RESUME_IDLE);
		break;
	case VC_SUSPEND_SUSPENDED:
		complete_all(&arm_state->vc_suspend_complete);
		break;
	default:
		BUG();
		break;
	}
}

void
set_resume_state(struct vchiq_arm_state *arm_state,
		 enum vc_resume_status new_state)
{
	/* set the state in all cases */
	arm_state->vc_resume_state = new_state;

	/* state specific additional actions */
	switch (new_state) {
	case VC_RESUME_FAILED:
		break;
	case VC_RESUME_IDLE:
		reinit_completion(&arm_state->vc_resume_complete);
		break;
	case VC_RESUME_REQUESTED:
		break;
	case VC_RESUME_IN_PROGRESS:
		break;
	case VC_RESUME_RESUMED:
		complete_all(&arm_state->vc_resume_complete);
		set_suspend_state(arm_state, VC_SUSPEND_IDLE);
		break;
	default:
		BUG();
		break;
	}
}

/* should be called with the write lock held */
inline void
start_suspend_timer(struct vchiq_arm_state *arm_state)
{
	del_timer(&arm_state->suspend_timer);
	arm_state->suspend_timer.expires = jiffies +
		msecs_to_jiffies(arm_state->suspend_timer_timeout);
	add_timer(&arm_state->suspend_timer);
	arm_state->suspend_timer_running = 1;
}

/* should be called with the write lock held */
static inline void
stop_suspend_timer(struct vchiq_arm_state *arm_state)
{
	if (arm_state->suspend_timer_running) {
		del_timer(&arm_state->suspend_timer);
		arm_state->suspend_timer_running = 0;
	}
}

static inline int
need_resume(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	return (arm_state->vc_suspend_state > VC_SUSPEND_IDLE) &&
			(arm_state->vc_resume_state < VC_RESUME_REQUESTED) &&
			vchiq_videocore_wanted(state);
}

static int
block_resume(struct vchiq_arm_state *arm_state)
{
	int status = VCHIQ_SUCCESS;
	const unsigned long timeout_val =
				msecs_to_jiffies(FORCE_SUSPEND_TIMEOUT_MS);
	int resume_count = 0;

	/* Allow any threads which were blocked by the last force suspend to
	 * complete if they haven't already.  Only give this one shot; if
	 * blocked_count is incremented after blocked_blocker is completed
	 * (which only happens when blocked_count hits 0) then those threads
	 * will have to wait until next time around */
	if (arm_state->blocked_count) {
		reinit_completion(&arm_state->blocked_blocker);
		write_unlock_bh(&arm_state->susp_res_lock);
		vchiq_log_info(vchiq_susp_log_level, "%s wait for previously "
			"blocked clients", __func__);
		if (wait_for_completion_killable_timeout(
				&arm_state->blocked_blocker, timeout_val)
					<= 0) {
			vchiq_log_error(vchiq_susp_log_level, "%s wait for "
				"previously blocked clients failed", __func__);
			status = VCHIQ_ERROR;
			write_lock_bh(&arm_state->susp_res_lock);
			goto out;
		}
		vchiq_log_info(vchiq_susp_log_level, "%s previously blocked "
			"clients resumed", __func__);
		write_lock_bh(&arm_state->susp_res_lock);
	}

	/* We need to wait for resume to complete if it's in process */
	while (arm_state->vc_resume_state != VC_RESUME_RESUMED &&
			arm_state->vc_resume_state > VC_RESUME_IDLE) {
		if (resume_count > 1) {
			status = VCHIQ_ERROR;
			vchiq_log_error(vchiq_susp_log_level, "%s waited too "
				"many times for resume", __func__);
			goto out;
		}
		write_unlock_bh(&arm_state->susp_res_lock);
		vchiq_log_info(vchiq_susp_log_level, "%s wait for resume",
			__func__);
		if (wait_for_completion_killable_timeout(
				&arm_state->vc_resume_complete, timeout_val)
					<= 0) {
			vchiq_log_error(vchiq_susp_log_level, "%s wait for "
				"resume failed (%s)", __func__,
				resume_state_names[arm_state->vc_resume_state +
							VC_RESUME_NUM_OFFSET]);
			status = VCHIQ_ERROR;
			write_lock_bh(&arm_state->susp_res_lock);
			goto out;
		}
		vchiq_log_info(vchiq_susp_log_level, "%s resumed", __func__);
		write_lock_bh(&arm_state->susp_res_lock);
		resume_count++;
	}
	reinit_completion(&arm_state->resume_blocker);
	arm_state->resume_blocked = 1;

out:
	return status;
}

static inline void
unblock_resume(struct vchiq_arm_state *arm_state)
{
	complete_all(&arm_state->resume_blocker);
	arm_state->resume_blocked = 0;
}

/* Initiate suspend via slot handler. Should be called with the write lock
 * held */
VCHIQ_STATUS_T
vchiq_arm_vcsuspend(struct vchiq_state *state)
{
	VCHIQ_STATUS_T status = VCHIQ_ERROR;
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	if (!arm_state)
		goto out;

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);
	status = VCHIQ_SUCCESS;

	switch (arm_state->vc_suspend_state) {
	case VC_SUSPEND_REQUESTED:
		vchiq_log_info(vchiq_susp_log_level, "%s: suspend already "
			"requested", __func__);
		break;
	case VC_SUSPEND_IN_PROGRESS:
		vchiq_log_info(vchiq_susp_log_level, "%s: suspend already in "
			"progress", __func__);
		break;

	default:
		/* We don't expect to be in other states, so log but continue
		 * anyway */
		vchiq_log_error(vchiq_susp_log_level,
			"%s unexpected suspend state %s", __func__,
			suspend_state_names[arm_state->vc_suspend_state +
						VC_SUSPEND_NUM_OFFSET]);
		/* fall through */
	case VC_SUSPEND_REJECTED:
	case VC_SUSPEND_FAILED:
		/* Ensure any idle state actions have been run */
		set_suspend_state(arm_state, VC_SUSPEND_IDLE);
		/* fall through */
	case VC_SUSPEND_IDLE:
		vchiq_log_info(vchiq_susp_log_level,
			"%s: suspending", __func__);
		set_suspend_state(arm_state, VC_SUSPEND_REQUESTED);
		/* kick the slot handler thread to initiate suspend */
		request_poll(state, NULL, 0);
		break;
	}

out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit %d", __func__, status);
	return status;
}

void
vchiq_platform_check_suspend(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	int susp = 0;

	if (!arm_state)
		goto out;

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);

	write_lock_bh(&arm_state->susp_res_lock);
	if (arm_state->vc_suspend_state == VC_SUSPEND_REQUESTED &&
			arm_state->vc_resume_state == VC_RESUME_RESUMED) {
		set_suspend_state(arm_state, VC_SUSPEND_IN_PROGRESS);
		susp = 1;
	}
	write_unlock_bh(&arm_state->susp_res_lock);

	if (susp)
		vchiq_platform_suspend(state);

out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit", __func__);
	return;
}

static void
output_timeout_error(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	char err[50] = "";
	int vc_use_count = arm_state->videocore_use_count;
	int active_services = state->unused_service;
	int i;

	if (!arm_state->videocore_use_count) {
		snprintf(err, sizeof(err), " Videocore usecount is 0");
		goto output_msg;
	}
	for (i = 0; i < active_services; i++) {
		struct vchiq_service *service_ptr = state->services[i];

		if (service_ptr && service_ptr->service_use_count &&
			(service_ptr->srvstate != VCHIQ_SRVSTATE_FREE)) {
			snprintf(err, sizeof(err), " %c%c%c%c(%d) service has "
				"use count %d%s", VCHIQ_FOURCC_AS_4CHARS(
					service_ptr->base.fourcc),
				 service_ptr->client_id,
				 service_ptr->service_use_count,
				 service_ptr->service_use_count ==
					 vc_use_count ? "" : " (+ more)");
			break;
		}
	}

output_msg:
	vchiq_log_error(vchiq_susp_log_level,
		"timed out waiting for vc suspend (%d).%s",
		 arm_state->autosuspend_override, err);

}

/* Try to get videocore into suspended state, regardless of autosuspend state.
** We don't actually force suspend, since videocore may get into a bad state
** if we force suspend at a bad time.  Instead, we wait for autosuspend to
** determine a good point to suspend.  If this doesn't happen within 100ms we
** report failure.
**
** Returns VCHIQ_SUCCESS if videocore suspended successfully, VCHIQ_RETRY if
** videocore failed to suspend in time or VCHIQ_ERROR if interrupted.
*/
VCHIQ_STATUS_T
vchiq_arm_force_suspend(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	VCHIQ_STATUS_T status = VCHIQ_ERROR;
	long rc = 0;
	int repeat = -1;

	if (!arm_state)
		goto out;

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);

	write_lock_bh(&arm_state->susp_res_lock);

	status = block_resume(arm_state);
	if (status != VCHIQ_SUCCESS)
		goto unlock;
	if (arm_state->vc_suspend_state == VC_SUSPEND_SUSPENDED) {
		/* Already suspended - just block resume and exit */
		vchiq_log_info(vchiq_susp_log_level, "%s already suspended",
			__func__);
		status = VCHIQ_SUCCESS;
		goto unlock;
	} else if (arm_state->vc_suspend_state <= VC_SUSPEND_IDLE) {
		/* initiate suspend immediately in the case that we're waiting
		 * for the timeout */
		stop_suspend_timer(arm_state);
		if (!vchiq_videocore_wanted(state)) {
			vchiq_log_info(vchiq_susp_log_level, "%s videocore "
				"idle, initiating suspend", __func__);
			status = vchiq_arm_vcsuspend(state);
		} else if (arm_state->autosuspend_override <
						FORCE_SUSPEND_FAIL_MAX) {
			vchiq_log_info(vchiq_susp_log_level, "%s letting "
				"videocore go idle", __func__);
			status = VCHIQ_SUCCESS;
		} else {
			vchiq_log_warning(vchiq_susp_log_level, "%s failed too "
				"many times - attempting suspend", __func__);
			status = vchiq_arm_vcsuspend(state);
		}
	} else {
		vchiq_log_info(vchiq_susp_log_level, "%s videocore suspend "
			"in progress - wait for completion", __func__);
		status = VCHIQ_SUCCESS;
	}

	/* Wait for suspend to happen due to system idle (not forced..) */
	if (status != VCHIQ_SUCCESS)
		goto unblock_resume;

	do {
		write_unlock_bh(&arm_state->susp_res_lock);

		rc = wait_for_completion_killable_timeout(
				&arm_state->vc_suspend_complete,
				msecs_to_jiffies(FORCE_SUSPEND_TIMEOUT_MS));

		write_lock_bh(&arm_state->susp_res_lock);
		if (rc < 0) {
			vchiq_log_warning(vchiq_susp_log_level, "%s "
				"interrupted waiting for suspend", __func__);
			status = VCHIQ_ERROR;
			goto unblock_resume;
		} else if (rc == 0) {
			if (arm_state->vc_suspend_state > VC_SUSPEND_IDLE) {
				/* Repeat timeout once if in progress */
				if (repeat < 0) {
					repeat = 1;
					continue;
				}
			}
			arm_state->autosuspend_override++;
			output_timeout_error(state);

			status = VCHIQ_RETRY;
			goto unblock_resume;
		}
	} while (0 < (repeat--));

	/* Check and report state in case we need to abort ARM suspend */
	if (arm_state->vc_suspend_state != VC_SUSPEND_SUSPENDED) {
		status = VCHIQ_RETRY;
		vchiq_log_error(vchiq_susp_log_level,
			"%s videocore suspend failed (state %s)", __func__,
			suspend_state_names[arm_state->vc_suspend_state +
						VC_SUSPEND_NUM_OFFSET]);
		/* Reset the state only if it's still in an error state.
		 * Something could have already initiated another suspend. */
		if (arm_state->vc_suspend_state < VC_SUSPEND_IDLE)
			set_suspend_state(arm_state, VC_SUSPEND_IDLE);

		goto unblock_resume;
	}

	/* successfully suspended - unlock and exit */
	goto unlock;

unblock_resume:
	/* all error states need to unblock resume before exit */
	unblock_resume(arm_state);

unlock:
	write_unlock_bh(&arm_state->susp_res_lock);

out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit %d", __func__, status);
	return status;
}

void
vchiq_check_suspend(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	if (!arm_state)
		goto out;

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);

	write_lock_bh(&arm_state->susp_res_lock);
	if (arm_state->vc_suspend_state != VC_SUSPEND_SUSPENDED &&
			arm_state->first_connect &&
			!vchiq_videocore_wanted(state)) {
		vchiq_arm_vcsuspend(state);
	}
	write_unlock_bh(&arm_state->susp_res_lock);

out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit", __func__);
}

int
vchiq_arm_allow_resume(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	int resume = 0;
	int ret = -1;

	if (!arm_state)
		goto out;

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);

	write_lock_bh(&arm_state->susp_res_lock);
	unblock_resume(arm_state);
	resume = vchiq_check_resume(state);
	write_unlock_bh(&arm_state->susp_res_lock);

	if (resume) {
		if (wait_for_completion_killable(
			&arm_state->vc_resume_complete) < 0) {
			vchiq_log_error(vchiq_susp_log_level,
				"%s interrupted", __func__);
			/* failed, cannot accurately derive suspend
			 * state, so exit early. */
			goto out;
		}
	}

	read_lock_bh(&arm_state->susp_res_lock);
	if (arm_state->vc_suspend_state == VC_SUSPEND_SUSPENDED) {
		vchiq_log_info(vchiq_susp_log_level,
				"%s: Videocore remains suspended", __func__);
	} else {
		vchiq_log_info(vchiq_susp_log_level,
				"%s: Videocore resumed", __func__);
		ret = 0;
	}
	read_unlock_bh(&arm_state->susp_res_lock);
out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit %d", __func__, ret);
	return ret;
}

/* This function should be called with the write lock held */
int
vchiq_check_resume(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	int resume = 0;

	if (!arm_state)
		goto out;

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);

	if (need_resume(state)) {
		set_resume_state(arm_state, VC_RESUME_REQUESTED);
		request_poll(state, NULL, 0);
		resume = 1;
	}

out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit", __func__);
	return resume;
}

VCHIQ_STATUS_T
vchiq_use_internal(struct vchiq_state *state, struct vchiq_service *service,
		   enum USE_TYPE_E use_type)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	VCHIQ_STATUS_T ret = VCHIQ_SUCCESS;
	char entity[16];
	int *entity_uc;
	int local_uc, local_entity_uc;

	if (!arm_state)
		goto out;

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);

	if (use_type == USE_TYPE_VCHIQ) {
		sprintf(entity, "VCHIQ:   ");
		entity_uc = &arm_state->peer_use_count;
	} else if (service) {
		sprintf(entity, "%c%c%c%c:%03d",
			VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
			service->client_id);
		entity_uc = &service->service_use_count;
	} else {
		vchiq_log_error(vchiq_susp_log_level, "%s null service "
				"ptr", __func__);
		ret = VCHIQ_ERROR;
		goto out;
	}

	write_lock_bh(&arm_state->susp_res_lock);
	while (arm_state->resume_blocked) {
		/* If we call 'use' while force suspend is waiting for suspend,
		 * then we're about to block the thread which the force is
		 * waiting to complete, so we're bound to just time out. In this
		 * case, set the suspend state such that the wait will be
		 * canceled, so we can complete as quickly as possible. */
		if (arm_state->resume_blocked && arm_state->vc_suspend_state ==
				VC_SUSPEND_IDLE) {
			set_suspend_state(arm_state, VC_SUSPEND_FORCE_CANCELED);
			break;
		}
		/* If suspend is already in progress then we need to block */
		if (!try_wait_for_completion(&arm_state->resume_blocker)) {
			/* Indicate that there are threads waiting on the resume
			 * blocker.  These need to be allowed to complete before
			 * a _second_ call to force suspend can complete,
			 * otherwise low priority threads might never actually
			 * continue */
			arm_state->blocked_count++;
			write_unlock_bh(&arm_state->susp_res_lock);
			vchiq_log_info(vchiq_susp_log_level, "%s %s resume "
				"blocked - waiting...", __func__, entity);
			if (wait_for_completion_killable(
					&arm_state->resume_blocker) != 0) {
				vchiq_log_error(vchiq_susp_log_level, "%s %s "
					"wait for resume blocker interrupted",
					__func__, entity);
				ret = VCHIQ_ERROR;
				write_lock_bh(&arm_state->susp_res_lock);
				arm_state->blocked_count--;
				write_unlock_bh(&arm_state->susp_res_lock);
				goto out;
			}
			vchiq_log_info(vchiq_susp_log_level, "%s %s resume "
				"unblocked", __func__, entity);
			write_lock_bh(&arm_state->susp_res_lock);
			if (--arm_state->blocked_count == 0)
				complete_all(&arm_state->blocked_blocker);
		}
	}

	stop_suspend_timer(arm_state);

	local_uc = ++arm_state->videocore_use_count;
	local_entity_uc = ++(*entity_uc);

	/* If there's a pending request which hasn't yet been serviced then
	 * just clear it.  If we're past VC_SUSPEND_REQUESTED state then
	 * vc_resume_complete will block until we either resume or fail to
	 * suspend */
	if (arm_state->vc_suspend_state <= VC_SUSPEND_REQUESTED)
		set_suspend_state(arm_state, VC_SUSPEND_IDLE);

	if ((use_type != USE_TYPE_SERVICE_NO_RESUME) && need_resume(state)) {
		set_resume_state(arm_state, VC_RESUME_REQUESTED);
		vchiq_log_info(vchiq_susp_log_level,
			"%s %s count %d, state count %d",
			__func__, entity, local_entity_uc, local_uc);
		request_poll(state, NULL, 0);
	} else
		vchiq_log_trace(vchiq_susp_log_level,
			"%s %s count %d, state count %d",
			__func__, entity, *entity_uc, local_uc);

	write_unlock_bh(&arm_state->susp_res_lock);

	/* Completion is in a done state when we're not suspended, so this won't
	 * block for the non-suspended case. */
	if (!try_wait_for_completion(&arm_state->vc_resume_complete)) {
		vchiq_log_info(vchiq_susp_log_level, "%s %s wait for resume",
			__func__, entity);
		if (wait_for_completion_killable(
				&arm_state->vc_resume_complete) != 0) {
			vchiq_log_error(vchiq_susp_log_level, "%s %s wait for "
				"resume interrupted", __func__, entity);
			ret = VCHIQ_ERROR;
			goto out;
		}
		vchiq_log_info(vchiq_susp_log_level, "%s %s resumed", __func__,
			entity);
	}

	if (ret == VCHIQ_SUCCESS) {
		VCHIQ_STATUS_T status = VCHIQ_SUCCESS;
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

VCHIQ_STATUS_T
vchiq_release_internal(struct vchiq_state *state, struct vchiq_service *service)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	VCHIQ_STATUS_T ret = VCHIQ_SUCCESS;
	char entity[16];
	int *entity_uc;
	int local_uc, local_entity_uc;

	if (!arm_state)
		goto out;

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);

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
		ret = VCHIQ_ERROR;
		goto unlock;
	}
	local_uc = --arm_state->videocore_use_count;
	local_entity_uc = --(*entity_uc);

	if (!vchiq_videocore_wanted(state)) {
		if (vchiq_platform_use_suspend_timer() &&
				!arm_state->resume_blocked) {
			/* Only use the timer if we're not trying to force
			 * suspend (=> resume_blocked) */
			start_suspend_timer(arm_state);
		} else {
			vchiq_log_info(vchiq_susp_log_level,
				"%s %s count %d, state count %d - suspending",
				__func__, entity, *entity_uc,
				arm_state->videocore_use_count);
			vchiq_arm_vcsuspend(state);
		}
	} else
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

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);
	atomic_inc(&arm_state->ka_use_count);
	complete(&arm_state->ka_evt);
}

void
vchiq_on_remote_release(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);
	atomic_inc(&arm_state->ka_release_count);
	complete(&arm_state->ka_evt);
}

VCHIQ_STATUS_T
vchiq_use_service_internal(struct vchiq_service *service)
{
	return vchiq_use_internal(service->state, service, USE_TYPE_SERVICE);
}

VCHIQ_STATUS_T
vchiq_release_service_internal(struct vchiq_service *service)
{
	return vchiq_release_internal(service->state, service);
}

struct vchiq_debugfs_node *
vchiq_instance_get_debugfs_node(VCHIQ_INSTANCE_T instance)
{
	return &instance->debugfs_node;
}

int
vchiq_instance_get_use_count(VCHIQ_INSTANCE_T instance)
{
	struct vchiq_service *service;
	int use_count = 0, i;

	i = 0;
	while ((service = next_service_by_instance(instance->state,
		instance, &i)) != NULL) {
		use_count += service->service_use_count;
		unlock_service(service);
	}
	return use_count;
}

int
vchiq_instance_get_pid(VCHIQ_INSTANCE_T instance)
{
	return instance->pid;
}

int
vchiq_instance_get_trace(VCHIQ_INSTANCE_T instance)
{
	return instance->trace;
}

void
vchiq_instance_set_trace(VCHIQ_INSTANCE_T instance, int trace)
{
	struct vchiq_service *service;
	int i;

	i = 0;
	while ((service = next_service_by_instance(instance->state,
		instance, &i)) != NULL) {
		service->trace = trace;
		unlock_service(service);
	}
	instance->trace = (trace != 0);
}

static void suspend_timer_callback(struct timer_list *t)
{
	struct vchiq_arm_state *arm_state =
					from_timer(arm_state, t, suspend_timer);
	struct vchiq_state *state = arm_state->state;

	vchiq_log_info(vchiq_susp_log_level,
		"%s - suspend timer expired - check suspend", __func__);
	vchiq_check_suspend(state);
}

VCHIQ_STATUS_T
vchiq_use_service_no_resume(VCHIQ_SERVICE_HANDLE_T handle)
{
	VCHIQ_STATUS_T ret = VCHIQ_ERROR;
	struct vchiq_service *service = find_service_by_handle(handle);

	if (service) {
		ret = vchiq_use_internal(service->state, service,
				USE_TYPE_SERVICE_NO_RESUME);
		unlock_service(service);
	}
	return ret;
}

VCHIQ_STATUS_T
vchiq_use_service(VCHIQ_SERVICE_HANDLE_T handle)
{
	VCHIQ_STATUS_T ret = VCHIQ_ERROR;
	struct vchiq_service *service = find_service_by_handle(handle);

	if (service) {
		ret = vchiq_use_internal(service->state, service,
				USE_TYPE_SERVICE);
		unlock_service(service);
	}
	return ret;
}

VCHIQ_STATUS_T
vchiq_release_service(VCHIQ_SERVICE_HANDLE_T handle)
{
	VCHIQ_STATUS_T ret = VCHIQ_ERROR;
	struct vchiq_service *service = find_service_by_handle(handle);

	if (service) {
		ret = vchiq_release_internal(service->state, service);
		unlock_service(service);
	}
	return ret;
}

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
	/* If there's more than 64 services, only dump ones with
	 * non-zero counts */
	int only_nonzero = 0;
	static const char *nz = "<-- preventing suspend";

	enum vc_suspend_status vc_suspend_state;
	enum vc_resume_status  vc_resume_state;
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
	vc_suspend_state = arm_state->vc_suspend_state;
	vc_resume_state  = arm_state->vc_resume_state;
	peer_count = arm_state->peer_use_count;
	vc_use_count = arm_state->videocore_use_count;
	active_services = state->unused_service;
	if (active_services > MAX_SERVICES)
		only_nonzero = 1;

	for (i = 0; i < active_services; i++) {
		struct vchiq_service *service_ptr = state->services[i];

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

	read_unlock_bh(&arm_state->susp_res_lock);

	vchiq_log_warning(vchiq_susp_log_level,
		"-- Videcore suspend state: %s --",
		suspend_state_names[vc_suspend_state + VC_SUSPEND_NUM_OFFSET]);
	vchiq_log_warning(vchiq_susp_log_level,
		"-- Videcore resume state: %s --",
		resume_state_names[vc_resume_state + VC_RESUME_NUM_OFFSET]);

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

	vchiq_dump_platform_use_state(state);
}

VCHIQ_STATUS_T
vchiq_check_service(struct vchiq_service *service)
{
	struct vchiq_arm_state *arm_state;
	VCHIQ_STATUS_T ret = VCHIQ_ERROR;

	if (!service || !service->state)
		goto out;

	vchiq_log_trace(vchiq_susp_log_level, "%s", __func__);

	arm_state = vchiq_platform_get_arm_state(service->state);

	read_lock_bh(&arm_state->susp_res_lock);
	if (service->service_use_count)
		ret = VCHIQ_SUCCESS;
	read_unlock_bh(&arm_state->susp_res_lock);

	if (ret == VCHIQ_ERROR) {
		vchiq_log_error(vchiq_susp_log_level,
			"%s ERROR - %c%c%c%c:%d service count %d, "
			"state count %d, videocore suspend state %s", __func__,
			VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
			service->client_id, service->service_use_count,
			arm_state->videocore_use_count,
			suspend_state_names[arm_state->vc_suspend_state +
						VC_SUSPEND_NUM_OFFSET]);
		vchiq_dump_service_use_state(service->state);
	}
out:
	return ret;
}

/* stub functions */
void vchiq_on_remote_use_active(struct vchiq_state *state)
{
	(void)state;
}

void vchiq_platform_conn_state_changed(struct vchiq_state *state,
				       VCHIQ_CONNSTATE_T oldstate,
				       VCHIQ_CONNSTATE_T newstate)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	vchiq_log_info(vchiq_susp_log_level, "%d: %s->%s", state->id,
		get_conn_state_name(oldstate), get_conn_state_name(newstate));
	if (state->conn_state == VCHIQ_CONNSTATE_CONNECTED) {
		write_lock_bh(&arm_state->susp_res_lock);
		if (!arm_state->first_connect) {
			char threadname[16];

			arm_state->first_connect = 1;
			write_unlock_bh(&arm_state->susp_res_lock);
			snprintf(threadname, sizeof(threadname), "vchiq-keep/%d",
				state->id);
			arm_state->ka_thread = kthread_create(
				&vchiq_keepalive_thread_func,
				(void *)state,
				threadname);
			if (IS_ERR(arm_state->ka_thread)) {
				vchiq_log_error(vchiq_susp_log_level,
					"vchiq: FATAL: couldn't create thread %s",
					threadname);
			} else {
				wake_up_process(arm_state->ka_thread);
			}
		} else
			write_unlock_bh(&arm_state->susp_res_lock);
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

	drvdata->fw = rpi_firmware_get(fw_node);
	of_node_put(fw_node);
	if (!drvdata->fw)
		return -EPROBE_DEFER;

	platform_set_drvdata(pdev, drvdata);

	err = vchiq_platform_init(pdev, &g_state);
	if (err != 0)
		goto failed_platform_init;

	cdev_init(&vchiq_cdev, &vchiq_fops);
	vchiq_cdev.owner = THIS_MODULE;
	err = cdev_add(&vchiq_cdev, vchiq_devid, 1);
	if (err != 0) {
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
	platform_driver_unregister(&vchiq_driver);

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
