// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2014 Raspberry Pi (Trading) Ltd. All rights reserved.
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/miscdevice.h>

#include "vchiq_core.h"
#include "vchiq_ioctl.h"
#include "vchiq_arm.h"
#include "vchiq_debugfs.h"

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

	DEBUG_INITIALISE(g_state.local);
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
			if (wait_for_completion_interruptible(&user_service->insert_event)) {
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
		if (!args->buf || (copy_to_user(args->buf, header->data, header->size) == 0)) {
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
					"no bulk_waiter found for pid %d", current->pid);
			ret = -ESRCH;
			goto out;
		}
		vchiq_log_info(vchiq_arm_log_level,
			       "found bulk_waiter %pK for pid %d", waiter, current->pid);
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
			       "saved bulk_waiter %pK for pid %d", waiter, current->pid);

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

	DEBUG_INITIALISE(g_state.local);

	DEBUG_TRACE(AWAIT_COMPLETION_LINE);
	if (!instance->connected)
		return -ENOTCONN;

	mutex_lock(&instance->completion_mutex);

	DEBUG_TRACE(AWAIT_COMPLETION_LINE);
	while ((instance->completion_remove == instance->completion_insert) && !instance->closing) {
		int rc;

		DEBUG_TRACE(AWAIT_COMPLETION_LINE);
		mutex_unlock(&instance->completion_mutex);
		rc = wait_for_completion_interruptible(&instance->insert_event);
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

		completion = &instance->completions[remove & (MAX_COMPLETIONS - 1)];

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
			"%s - instance %pK, cmd %s, arg %lx", __func__, instance,
			((_IOC_TYPE(cmd) == VCHIQ_IOC_MAGIC) && (_IOC_NR(cmd) <= VCHIQ_IOC_MAX)) ?
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
		    wait_for_completion_interruptible(&user_service->close_event))
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
						__func__, (cmd == VCHIQ_IOC_USE_SERVICE) ?
						"VCHIQ_IOC_USE_SERVICE" :
						"VCHIQ_IOC_RELEASE_SERVICE",
					ret,
					VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
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

	if ((status == VCHIQ_SUCCESS) && (ret < 0) && (ret != -EINTR) && (ret != -EWOULDBLOCK))
		vchiq_log_info(vchiq_arm_log_level,
			       "  ioctl instance %pK, cmd %s -> status %d, %ld",
			       instance, (_IOC_NR(cmd) <= VCHIQ_IOC_MAX) ?
			       ioctl_names[_IOC_NR(cmd)] : "<invalid>", status, ret);
	else
		vchiq_log_trace(vchiq_arm_log_level,
				"  ioctl instance %pK, cmd %s -> status %d, %ld",
				instance, (_IOC_NR(cmd) <= VCHIQ_IOC_MAX) ?
				ioctl_names[_IOC_NR(cmd)] : "<invalid>", status, ret);

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

		completion = &instance->completions[instance->completion_remove
						    & (MAX_COMPLETIONS - 1)];
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

static struct miscdevice vchiq_miscdev = {
	.fops = &vchiq_fops,
	.minor = MISC_DYNAMIC_MINOR,
	.name = "vchiq",

};

/**
 *	vchiq_register_chrdev - Register the char driver for vchiq
 *				and create the necessary class and
 *				device files in userspace.
 *	@parent		The parent of the char device.
 *
 *	Returns 0 on success else returns the error code.
 */
int vchiq_register_chrdev(struct device *parent)
{
	vchiq_miscdev.parent = parent;

	return misc_register(&vchiq_miscdev);
}

/**
 *	vchiq_deregister_chrdev	- Deregister and cleanup the vchiq char
 *				  driver and device files
 */
void vchiq_deregister_chrdev(void)
{
	misc_deregister(&vchiq_miscdev);
}
