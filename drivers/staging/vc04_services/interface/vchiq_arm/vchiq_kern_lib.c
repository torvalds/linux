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

/* ---- Include Files ---------------------------------------------------- */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include "vchiq_core.h"
#include "vchiq_arm.h"
#include "vchiq_killable.h"

/* ---- Public Variables ------------------------------------------------- */

/* ---- Private Constants and Types -------------------------------------- */

struct bulk_waiter_node {
	struct bulk_waiter bulk_waiter;
	int pid;
	struct list_head list;
};

struct vchiq_instance_struct {
	VCHIQ_STATE_T *state;

	int connected;

	struct list_head bulk_waiter_list;
	struct mutex bulk_waiter_list_mutex;
};

static VCHIQ_STATUS_T
vchiq_blocking_bulk_transfer(VCHIQ_SERVICE_HANDLE_T handle, void *data,
	unsigned int size, VCHIQ_BULK_DIR_T dir);

/****************************************************************************
*
*   vchiq_initialise
*
***************************************************************************/
#define VCHIQ_INIT_RETRIES 10
VCHIQ_STATUS_T vchiq_initialise(VCHIQ_INSTANCE_T *instanceOut)
{
	VCHIQ_STATUS_T status = VCHIQ_ERROR;
	VCHIQ_STATE_T *state;
	VCHIQ_INSTANCE_T instance = NULL;
        int i;

	vchiq_log_trace(vchiq_core_log_level, "%s called", __func__);

        /* VideoCore may not be ready due to boot up timing.
           It may never be ready if kernel and firmware are mismatched, so don't block forever. */
        for (i=0; i<VCHIQ_INIT_RETRIES; i++) {
		state = vchiq_get_state();
		if (state)
			break;
		udelay(500);
	}
	if (i==VCHIQ_INIT_RETRIES) {
		vchiq_log_error(vchiq_core_log_level,
			"%s: videocore not initialized\n", __func__);
		goto failed;
	} else if (i>0) {
		vchiq_log_warning(vchiq_core_log_level,
			"%s: videocore initialized after %d retries\n", __func__, i);
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

	*instanceOut = instance;

	status = VCHIQ_SUCCESS;

failed:
	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, status);

	return status;
}
EXPORT_SYMBOL(vchiq_initialise);

/****************************************************************************
*
*   vchiq_shutdown
*
***************************************************************************/

VCHIQ_STATUS_T vchiq_shutdown(VCHIQ_INSTANCE_T instance)
{
	VCHIQ_STATUS_T status;
	VCHIQ_STATE_T *state = instance->state;

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p) called", __func__, instance);

	if (mutex_lock_interruptible(&state->mutex) != 0)
		return VCHIQ_RETRY;

	/* Remove all services */
	status = vchiq_shutdown_internal(state, instance);

	mutex_unlock(&state->mutex);

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p): returning %d", __func__, instance, status);

	if (status == VCHIQ_SUCCESS) {
		struct list_head *pos, *next;
		list_for_each_safe(pos, next,
				&instance->bulk_waiter_list) {
			struct bulk_waiter_node *waiter;
			waiter = list_entry(pos,
					struct bulk_waiter_node,
					list);
			list_del(pos);
			vchiq_log_info(vchiq_arm_log_level,
					"bulk_waiter - cleaned up %x "
					"for pid %d",
					(unsigned int)waiter, waiter->pid);
			kfree(waiter);
		}
		kfree(instance);
	}

	return status;
}
EXPORT_SYMBOL(vchiq_shutdown);

/****************************************************************************
*
*   vchiq_is_connected
*
***************************************************************************/

int vchiq_is_connected(VCHIQ_INSTANCE_T instance)
{
	return instance->connected;
}

/****************************************************************************
*
*   vchiq_connect
*
***************************************************************************/

VCHIQ_STATUS_T vchiq_connect(VCHIQ_INSTANCE_T instance)
{
	VCHIQ_STATUS_T status;
	VCHIQ_STATE_T *state = instance->state;

	vchiq_log_trace(vchiq_core_log_level,
		"%s(%p) called", __func__, instance);

	if (mutex_lock_interruptible(&state->mutex) != 0) {
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

/****************************************************************************
*
*   vchiq_add_service
*
***************************************************************************/

VCHIQ_STATUS_T vchiq_add_service(
	VCHIQ_INSTANCE_T              instance,
	const VCHIQ_SERVICE_PARAMS_T *params,
	VCHIQ_SERVICE_HANDLE_T       *phandle)
{
	VCHIQ_STATUS_T status;
	VCHIQ_STATE_T *state = instance->state;
	VCHIQ_SERVICE_T *service = NULL;
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

/****************************************************************************
*
*   vchiq_open_service
*
***************************************************************************/

VCHIQ_STATUS_T vchiq_open_service(
	VCHIQ_INSTANCE_T              instance,
	const VCHIQ_SERVICE_PARAMS_T *params,
	VCHIQ_SERVICE_HANDLE_T       *phandle)
{
	VCHIQ_STATUS_T   status = VCHIQ_ERROR;
	VCHIQ_STATE_T   *state = instance->state;
	VCHIQ_SERVICE_T *service = NULL;

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
vchiq_queue_bulk_transmit(VCHIQ_SERVICE_HANDLE_T handle,
	const void *data, unsigned int size, void *userdata)
{
	return vchiq_bulk_transfer(handle,
		VCHI_MEM_HANDLE_INVALID, (void *)data, size, userdata,
		VCHIQ_BULK_MODE_CALLBACK, VCHIQ_BULK_TRANSMIT);
}
EXPORT_SYMBOL(vchiq_queue_bulk_transmit);

VCHIQ_STATUS_T
vchiq_queue_bulk_receive(VCHIQ_SERVICE_HANDLE_T handle, void *data,
	unsigned int size, void *userdata)
{
	return vchiq_bulk_transfer(handle,
		VCHI_MEM_HANDLE_INVALID, data, size, userdata,
		VCHIQ_BULK_MODE_CALLBACK, VCHIQ_BULK_RECEIVE);
}
EXPORT_SYMBOL(vchiq_queue_bulk_receive);

VCHIQ_STATUS_T
vchiq_bulk_transmit(VCHIQ_SERVICE_HANDLE_T handle, const void *data,
	unsigned int size, void *userdata, VCHIQ_BULK_MODE_T mode)
{
	VCHIQ_STATUS_T status;

	switch (mode) {
	case VCHIQ_BULK_MODE_NOCALLBACK:
	case VCHIQ_BULK_MODE_CALLBACK:
		status = vchiq_bulk_transfer(handle,
			VCHI_MEM_HANDLE_INVALID, (void *)data, size, userdata,
			mode, VCHIQ_BULK_TRANSMIT);
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
		status = vchiq_bulk_transfer(handle,
			VCHI_MEM_HANDLE_INVALID, data, size, userdata,
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
	VCHIQ_SERVICE_T *service;
	VCHIQ_STATUS_T status;
	struct bulk_waiter_node *waiter = NULL;
	struct list_head *pos;

	service = find_service_by_handle(handle);
	if (!service)
		return VCHIQ_ERROR;

	instance = service->instance;

	unlock_service(service);

	mutex_lock(&instance->bulk_waiter_list_mutex);
	list_for_each(pos, &instance->bulk_waiter_list) {
		if (list_entry(pos, struct bulk_waiter_node,
				list)->pid == current->pid) {
			waiter = list_entry(pos,
				struct bulk_waiter_node,
				list);
			list_del(pos);
			break;
		}
	}
	mutex_unlock(&instance->bulk_waiter_list_mutex);

	if (waiter) {
		VCHIQ_BULK_T *bulk = waiter->bulk_waiter.bulk;
		if (bulk) {
			/* This thread has an outstanding bulk transfer. */
			if ((bulk->data != data) ||
				(bulk->size != size)) {
				/* This is not a retry of the previous one.
				** Cancel the signal when the transfer
				** completes. */
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

	status = vchiq_bulk_transfer(handle, VCHI_MEM_HANDLE_INVALID,
		data, size, &waiter->bulk_waiter, VCHIQ_BULK_MODE_BLOCKING,
		dir);
	if ((status != VCHIQ_RETRY) || fatal_signal_pending(current) ||
		!waiter->bulk_waiter.bulk) {
		VCHIQ_BULK_T *bulk = waiter->bulk_waiter.bulk;
		if (bulk) {
			/* Cancel the signal when the transfer
			 ** completes. */
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
				"saved bulk_waiter %x for pid %d",
				(unsigned int)waiter, current->pid);
	}

	return status;
}
