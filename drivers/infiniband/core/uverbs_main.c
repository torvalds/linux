/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/anon_inodes.h>
#include <linux/slab.h>

#include <linux/uaccess.h>

#include <rdma/ib.h>
#include <rdma/uverbs_std_types.h>

#include "uverbs.h"
#include "core_priv.h"
#include "rdma_core.h"

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("InfiniBand userspace verbs access");
MODULE_LICENSE("Dual BSD/GPL");

enum {
	IB_UVERBS_MAJOR       = 231,
	IB_UVERBS_BASE_MINOR  = 192,
	IB_UVERBS_MAX_DEVICES = RDMA_MAX_PORTS,
	IB_UVERBS_NUM_FIXED_MINOR = 32,
	IB_UVERBS_NUM_DYNAMIC_MINOR = IB_UVERBS_MAX_DEVICES - IB_UVERBS_NUM_FIXED_MINOR,
};

#define IB_UVERBS_BASE_DEV	MKDEV(IB_UVERBS_MAJOR, IB_UVERBS_BASE_MINOR)

static dev_t dynamic_uverbs_dev;
static struct class *uverbs_class;

static DECLARE_BITMAP(dev_map, IB_UVERBS_MAX_DEVICES);

static ssize_t (*uverbs_cmd_table[])(struct ib_uverbs_file *file,
				     struct ib_device *ib_dev,
				     const char __user *buf, int in_len,
				     int out_len) = {
	[IB_USER_VERBS_CMD_GET_CONTEXT]		= ib_uverbs_get_context,
	[IB_USER_VERBS_CMD_QUERY_DEVICE]	= ib_uverbs_query_device,
	[IB_USER_VERBS_CMD_QUERY_PORT]		= ib_uverbs_query_port,
	[IB_USER_VERBS_CMD_ALLOC_PD]		= ib_uverbs_alloc_pd,
	[IB_USER_VERBS_CMD_DEALLOC_PD]		= ib_uverbs_dealloc_pd,
	[IB_USER_VERBS_CMD_REG_MR]		= ib_uverbs_reg_mr,
	[IB_USER_VERBS_CMD_REREG_MR]		= ib_uverbs_rereg_mr,
	[IB_USER_VERBS_CMD_DEREG_MR]		= ib_uverbs_dereg_mr,
	[IB_USER_VERBS_CMD_ALLOC_MW]		= ib_uverbs_alloc_mw,
	[IB_USER_VERBS_CMD_DEALLOC_MW]		= ib_uverbs_dealloc_mw,
	[IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL] = ib_uverbs_create_comp_channel,
	[IB_USER_VERBS_CMD_CREATE_CQ]		= ib_uverbs_create_cq,
	[IB_USER_VERBS_CMD_RESIZE_CQ]		= ib_uverbs_resize_cq,
	[IB_USER_VERBS_CMD_POLL_CQ]		= ib_uverbs_poll_cq,
	[IB_USER_VERBS_CMD_REQ_NOTIFY_CQ]	= ib_uverbs_req_notify_cq,
	[IB_USER_VERBS_CMD_DESTROY_CQ]		= ib_uverbs_destroy_cq,
	[IB_USER_VERBS_CMD_CREATE_QP]		= ib_uverbs_create_qp,
	[IB_USER_VERBS_CMD_QUERY_QP]		= ib_uverbs_query_qp,
	[IB_USER_VERBS_CMD_MODIFY_QP]		= ib_uverbs_modify_qp,
	[IB_USER_VERBS_CMD_DESTROY_QP]		= ib_uverbs_destroy_qp,
	[IB_USER_VERBS_CMD_POST_SEND]		= ib_uverbs_post_send,
	[IB_USER_VERBS_CMD_POST_RECV]		= ib_uverbs_post_recv,
	[IB_USER_VERBS_CMD_POST_SRQ_RECV]	= ib_uverbs_post_srq_recv,
	[IB_USER_VERBS_CMD_CREATE_AH]		= ib_uverbs_create_ah,
	[IB_USER_VERBS_CMD_DESTROY_AH]		= ib_uverbs_destroy_ah,
	[IB_USER_VERBS_CMD_ATTACH_MCAST]	= ib_uverbs_attach_mcast,
	[IB_USER_VERBS_CMD_DETACH_MCAST]	= ib_uverbs_detach_mcast,
	[IB_USER_VERBS_CMD_CREATE_SRQ]		= ib_uverbs_create_srq,
	[IB_USER_VERBS_CMD_MODIFY_SRQ]		= ib_uverbs_modify_srq,
	[IB_USER_VERBS_CMD_QUERY_SRQ]		= ib_uverbs_query_srq,
	[IB_USER_VERBS_CMD_DESTROY_SRQ]		= ib_uverbs_destroy_srq,
	[IB_USER_VERBS_CMD_OPEN_XRCD]		= ib_uverbs_open_xrcd,
	[IB_USER_VERBS_CMD_CLOSE_XRCD]		= ib_uverbs_close_xrcd,
	[IB_USER_VERBS_CMD_CREATE_XSRQ]		= ib_uverbs_create_xsrq,
	[IB_USER_VERBS_CMD_OPEN_QP]		= ib_uverbs_open_qp,
};

static int (*uverbs_ex_cmd_table[])(struct ib_uverbs_file *file,
				    struct ib_device *ib_dev,
				    struct ib_udata *ucore,
				    struct ib_udata *uhw) = {
	[IB_USER_VERBS_EX_CMD_CREATE_FLOW]	= ib_uverbs_ex_create_flow,
	[IB_USER_VERBS_EX_CMD_DESTROY_FLOW]	= ib_uverbs_ex_destroy_flow,
	[IB_USER_VERBS_EX_CMD_QUERY_DEVICE]	= ib_uverbs_ex_query_device,
	[IB_USER_VERBS_EX_CMD_CREATE_CQ]	= ib_uverbs_ex_create_cq,
	[IB_USER_VERBS_EX_CMD_CREATE_QP]        = ib_uverbs_ex_create_qp,
	[IB_USER_VERBS_EX_CMD_CREATE_WQ]        = ib_uverbs_ex_create_wq,
	[IB_USER_VERBS_EX_CMD_MODIFY_WQ]        = ib_uverbs_ex_modify_wq,
	[IB_USER_VERBS_EX_CMD_DESTROY_WQ]       = ib_uverbs_ex_destroy_wq,
	[IB_USER_VERBS_EX_CMD_CREATE_RWQ_IND_TBL] = ib_uverbs_ex_create_rwq_ind_table,
	[IB_USER_VERBS_EX_CMD_DESTROY_RWQ_IND_TBL] = ib_uverbs_ex_destroy_rwq_ind_table,
	[IB_USER_VERBS_EX_CMD_MODIFY_QP]        = ib_uverbs_ex_modify_qp,
	[IB_USER_VERBS_EX_CMD_MODIFY_CQ]        = ib_uverbs_ex_modify_cq,
};

static void ib_uverbs_add_one(struct ib_device *device);
static void ib_uverbs_remove_one(struct ib_device *device, void *client_data);

int uverbs_dealloc_mw(struct ib_mw *mw)
{
	struct ib_pd *pd = mw->pd;
	int ret;

	ret = mw->device->dealloc_mw(mw);
	if (!ret)
		atomic_dec(&pd->usecnt);
	return ret;
}

static void ib_uverbs_release_dev(struct kobject *kobj)
{
	struct ib_uverbs_device *dev =
		container_of(kobj, struct ib_uverbs_device, kobj);

	cleanup_srcu_struct(&dev->disassociate_srcu);
	kfree(dev);
}

static struct kobj_type ib_uverbs_dev_ktype = {
	.release = ib_uverbs_release_dev,
};

static void ib_uverbs_release_async_event_file(struct kref *ref)
{
	struct ib_uverbs_async_event_file *file =
		container_of(ref, struct ib_uverbs_async_event_file, ref);

	kfree(file);
}

void ib_uverbs_release_ucq(struct ib_uverbs_file *file,
			  struct ib_uverbs_completion_event_file *ev_file,
			  struct ib_ucq_object *uobj)
{
	struct ib_uverbs_event *evt, *tmp;

	if (ev_file) {
		spin_lock_irq(&ev_file->ev_queue.lock);
		list_for_each_entry_safe(evt, tmp, &uobj->comp_list, obj_list) {
			list_del(&evt->list);
			kfree(evt);
		}
		spin_unlock_irq(&ev_file->ev_queue.lock);

		uverbs_uobject_put(&ev_file->uobj_file.uobj);
	}

	spin_lock_irq(&file->async_file->ev_queue.lock);
	list_for_each_entry_safe(evt, tmp, &uobj->async_list, obj_list) {
		list_del(&evt->list);
		kfree(evt);
	}
	spin_unlock_irq(&file->async_file->ev_queue.lock);
}

void ib_uverbs_release_uevent(struct ib_uverbs_file *file,
			      struct ib_uevent_object *uobj)
{
	struct ib_uverbs_event *evt, *tmp;

	spin_lock_irq(&file->async_file->ev_queue.lock);
	list_for_each_entry_safe(evt, tmp, &uobj->event_list, obj_list) {
		list_del(&evt->list);
		kfree(evt);
	}
	spin_unlock_irq(&file->async_file->ev_queue.lock);
}

void ib_uverbs_detach_umcast(struct ib_qp *qp,
			     struct ib_uqp_object *uobj)
{
	struct ib_uverbs_mcast_entry *mcast, *tmp;

	list_for_each_entry_safe(mcast, tmp, &uobj->mcast_list, list) {
		ib_detach_mcast(qp, &mcast->gid, mcast->lid);
		list_del(&mcast->list);
		kfree(mcast);
	}
}

static int ib_uverbs_cleanup_ucontext(struct ib_uverbs_file *file,
				      struct ib_ucontext *context,
				      bool device_removed)
{
	context->closing = 1;
	uverbs_cleanup_ucontext(context, device_removed);
	put_pid(context->tgid);

	ib_rdmacg_uncharge(&context->cg_obj, context->device,
			   RDMACG_RESOURCE_HCA_HANDLE);

	return context->device->dealloc_ucontext(context);
}

static void ib_uverbs_comp_dev(struct ib_uverbs_device *dev)
{
	complete(&dev->comp);
}

void ib_uverbs_release_file(struct kref *ref)
{
	struct ib_uverbs_file *file =
		container_of(ref, struct ib_uverbs_file, ref);
	struct ib_device *ib_dev;
	int srcu_key;

	srcu_key = srcu_read_lock(&file->device->disassociate_srcu);
	ib_dev = srcu_dereference(file->device->ib_dev,
				  &file->device->disassociate_srcu);
	if (ib_dev && !ib_dev->disassociate_ucontext)
		module_put(ib_dev->owner);
	srcu_read_unlock(&file->device->disassociate_srcu, srcu_key);

	if (atomic_dec_and_test(&file->device->refcount))
		ib_uverbs_comp_dev(file->device);

	kobject_put(&file->device->kobj);
	kfree(file);
}

static ssize_t ib_uverbs_event_read(struct ib_uverbs_event_queue *ev_queue,
				    struct ib_uverbs_file *uverbs_file,
				    struct file *filp, char __user *buf,
				    size_t count, loff_t *pos,
				    size_t eventsz)
{
	struct ib_uverbs_event *event;
	int ret = 0;

	spin_lock_irq(&ev_queue->lock);

	while (list_empty(&ev_queue->event_list)) {
		spin_unlock_irq(&ev_queue->lock);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(ev_queue->poll_wait,
					     (!list_empty(&ev_queue->event_list) ||
			/* The barriers built into wait_event_interruptible()
			 * and wake_up() guarentee this will see the null set
			 * without using RCU
			 */
					     !uverbs_file->device->ib_dev)))
			return -ERESTARTSYS;

		/* If device was disassociated and no event exists set an error */
		if (list_empty(&ev_queue->event_list) &&
		    !uverbs_file->device->ib_dev)
			return -EIO;

		spin_lock_irq(&ev_queue->lock);
	}

	event = list_entry(ev_queue->event_list.next, struct ib_uverbs_event, list);

	if (eventsz > count) {
		ret   = -EINVAL;
		event = NULL;
	} else {
		list_del(ev_queue->event_list.next);
		if (event->counter) {
			++(*event->counter);
			list_del(&event->obj_list);
		}
	}

	spin_unlock_irq(&ev_queue->lock);

	if (event) {
		if (copy_to_user(buf, event, eventsz))
			ret = -EFAULT;
		else
			ret = eventsz;
	}

	kfree(event);

	return ret;
}

static ssize_t ib_uverbs_async_event_read(struct file *filp, char __user *buf,
					  size_t count, loff_t *pos)
{
	struct ib_uverbs_async_event_file *file = filp->private_data;

	return ib_uverbs_event_read(&file->ev_queue, file->uverbs_file, filp,
				    buf, count, pos,
				    sizeof(struct ib_uverbs_async_event_desc));
}

static ssize_t ib_uverbs_comp_event_read(struct file *filp, char __user *buf,
					 size_t count, loff_t *pos)
{
	struct ib_uverbs_completion_event_file *comp_ev_file =
		filp->private_data;

	return ib_uverbs_event_read(&comp_ev_file->ev_queue,
				    comp_ev_file->uobj_file.ufile, filp,
				    buf, count, pos,
				    sizeof(struct ib_uverbs_comp_event_desc));
}

static __poll_t ib_uverbs_event_poll(struct ib_uverbs_event_queue *ev_queue,
					 struct file *filp,
					 struct poll_table_struct *wait)
{
	__poll_t pollflags = 0;

	poll_wait(filp, &ev_queue->poll_wait, wait);

	spin_lock_irq(&ev_queue->lock);
	if (!list_empty(&ev_queue->event_list))
		pollflags = POLLIN | POLLRDNORM;
	spin_unlock_irq(&ev_queue->lock);

	return pollflags;
}

static __poll_t ib_uverbs_async_event_poll(struct file *filp,
					       struct poll_table_struct *wait)
{
	return ib_uverbs_event_poll(filp->private_data, filp, wait);
}

static __poll_t ib_uverbs_comp_event_poll(struct file *filp,
					      struct poll_table_struct *wait)
{
	struct ib_uverbs_completion_event_file *comp_ev_file =
		filp->private_data;

	return ib_uverbs_event_poll(&comp_ev_file->ev_queue, filp, wait);
}

static int ib_uverbs_async_event_fasync(int fd, struct file *filp, int on)
{
	struct ib_uverbs_event_queue *ev_queue = filp->private_data;

	return fasync_helper(fd, filp, on, &ev_queue->async_queue);
}

static int ib_uverbs_comp_event_fasync(int fd, struct file *filp, int on)
{
	struct ib_uverbs_completion_event_file *comp_ev_file =
		filp->private_data;

	return fasync_helper(fd, filp, on, &comp_ev_file->ev_queue.async_queue);
}

static int ib_uverbs_async_event_close(struct inode *inode, struct file *filp)
{
	struct ib_uverbs_async_event_file *file = filp->private_data;
	struct ib_uverbs_file *uverbs_file = file->uverbs_file;
	struct ib_uverbs_event *entry, *tmp;
	int closed_already = 0;

	mutex_lock(&uverbs_file->device->lists_mutex);
	spin_lock_irq(&file->ev_queue.lock);
	closed_already = file->ev_queue.is_closed;
	file->ev_queue.is_closed = 1;
	list_for_each_entry_safe(entry, tmp, &file->ev_queue.event_list, list) {
		if (entry->counter)
			list_del(&entry->obj_list);
		kfree(entry);
	}
	spin_unlock_irq(&file->ev_queue.lock);
	if (!closed_already) {
		list_del(&file->list);
		ib_unregister_event_handler(&uverbs_file->event_handler);
	}
	mutex_unlock(&uverbs_file->device->lists_mutex);

	kref_put(&uverbs_file->ref, ib_uverbs_release_file);
	kref_put(&file->ref, ib_uverbs_release_async_event_file);

	return 0;
}

static int ib_uverbs_comp_event_close(struct inode *inode, struct file *filp)
{
	struct ib_uverbs_completion_event_file *file = filp->private_data;
	struct ib_uverbs_event *entry, *tmp;

	spin_lock_irq(&file->ev_queue.lock);
	list_for_each_entry_safe(entry, tmp, &file->ev_queue.event_list, list) {
		if (entry->counter)
			list_del(&entry->obj_list);
		kfree(entry);
	}
	spin_unlock_irq(&file->ev_queue.lock);

	uverbs_close_fd(filp);

	return 0;
}

const struct file_operations uverbs_event_fops = {
	.owner	 = THIS_MODULE,
	.read	 = ib_uverbs_comp_event_read,
	.poll    = ib_uverbs_comp_event_poll,
	.release = ib_uverbs_comp_event_close,
	.fasync  = ib_uverbs_comp_event_fasync,
	.llseek	 = no_llseek,
};

static const struct file_operations uverbs_async_event_fops = {
	.owner	 = THIS_MODULE,
	.read	 = ib_uverbs_async_event_read,
	.poll    = ib_uverbs_async_event_poll,
	.release = ib_uverbs_async_event_close,
	.fasync  = ib_uverbs_async_event_fasync,
	.llseek	 = no_llseek,
};

void ib_uverbs_comp_handler(struct ib_cq *cq, void *cq_context)
{
	struct ib_uverbs_event_queue   *ev_queue = cq_context;
	struct ib_ucq_object	       *uobj;
	struct ib_uverbs_event	       *entry;
	unsigned long			flags;

	if (!ev_queue)
		return;

	spin_lock_irqsave(&ev_queue->lock, flags);
	if (ev_queue->is_closed) {
		spin_unlock_irqrestore(&ev_queue->lock, flags);
		return;
	}

	entry = kmalloc(sizeof *entry, GFP_ATOMIC);
	if (!entry) {
		spin_unlock_irqrestore(&ev_queue->lock, flags);
		return;
	}

	uobj = container_of(cq->uobject, struct ib_ucq_object, uobject);

	entry->desc.comp.cq_handle = cq->uobject->user_handle;
	entry->counter		   = &uobj->comp_events_reported;

	list_add_tail(&entry->list, &ev_queue->event_list);
	list_add_tail(&entry->obj_list, &uobj->comp_list);
	spin_unlock_irqrestore(&ev_queue->lock, flags);

	wake_up_interruptible(&ev_queue->poll_wait);
	kill_fasync(&ev_queue->async_queue, SIGIO, POLL_IN);
}

static void ib_uverbs_async_handler(struct ib_uverbs_file *file,
				    __u64 element, __u64 event,
				    struct list_head *obj_list,
				    u32 *counter)
{
	struct ib_uverbs_event *entry;
	unsigned long flags;

	spin_lock_irqsave(&file->async_file->ev_queue.lock, flags);
	if (file->async_file->ev_queue.is_closed) {
		spin_unlock_irqrestore(&file->async_file->ev_queue.lock, flags);
		return;
	}

	entry = kmalloc(sizeof *entry, GFP_ATOMIC);
	if (!entry) {
		spin_unlock_irqrestore(&file->async_file->ev_queue.lock, flags);
		return;
	}

	entry->desc.async.element    = element;
	entry->desc.async.event_type = event;
	entry->desc.async.reserved   = 0;
	entry->counter               = counter;

	list_add_tail(&entry->list, &file->async_file->ev_queue.event_list);
	if (obj_list)
		list_add_tail(&entry->obj_list, obj_list);
	spin_unlock_irqrestore(&file->async_file->ev_queue.lock, flags);

	wake_up_interruptible(&file->async_file->ev_queue.poll_wait);
	kill_fasync(&file->async_file->ev_queue.async_queue, SIGIO, POLL_IN);
}

void ib_uverbs_cq_event_handler(struct ib_event *event, void *context_ptr)
{
	struct ib_ucq_object *uobj = container_of(event->element.cq->uobject,
						  struct ib_ucq_object, uobject);

	ib_uverbs_async_handler(uobj->uverbs_file, uobj->uobject.user_handle,
				event->event, &uobj->async_list,
				&uobj->async_events_reported);
}

void ib_uverbs_qp_event_handler(struct ib_event *event, void *context_ptr)
{
	struct ib_uevent_object *uobj;

	/* for XRC target qp's, check that qp is live */
	if (!event->element.qp->uobject)
		return;

	uobj = container_of(event->element.qp->uobject,
			    struct ib_uevent_object, uobject);

	ib_uverbs_async_handler(context_ptr, uobj->uobject.user_handle,
				event->event, &uobj->event_list,
				&uobj->events_reported);
}

void ib_uverbs_wq_event_handler(struct ib_event *event, void *context_ptr)
{
	struct ib_uevent_object *uobj = container_of(event->element.wq->uobject,
						  struct ib_uevent_object, uobject);

	ib_uverbs_async_handler(context_ptr, uobj->uobject.user_handle,
				event->event, &uobj->event_list,
				&uobj->events_reported);
}

void ib_uverbs_srq_event_handler(struct ib_event *event, void *context_ptr)
{
	struct ib_uevent_object *uobj;

	uobj = container_of(event->element.srq->uobject,
			    struct ib_uevent_object, uobject);

	ib_uverbs_async_handler(context_ptr, uobj->uobject.user_handle,
				event->event, &uobj->event_list,
				&uobj->events_reported);
}

void ib_uverbs_event_handler(struct ib_event_handler *handler,
			     struct ib_event *event)
{
	struct ib_uverbs_file *file =
		container_of(handler, struct ib_uverbs_file, event_handler);

	ib_uverbs_async_handler(file, event->element.port_num, event->event,
				NULL, NULL);
}

void ib_uverbs_free_async_event_file(struct ib_uverbs_file *file)
{
	kref_put(&file->async_file->ref, ib_uverbs_release_async_event_file);
	file->async_file = NULL;
}

void ib_uverbs_init_event_queue(struct ib_uverbs_event_queue *ev_queue)
{
	spin_lock_init(&ev_queue->lock);
	INIT_LIST_HEAD(&ev_queue->event_list);
	init_waitqueue_head(&ev_queue->poll_wait);
	ev_queue->is_closed   = 0;
	ev_queue->async_queue = NULL;
}

struct file *ib_uverbs_alloc_async_event_file(struct ib_uverbs_file *uverbs_file,
					      struct ib_device	*ib_dev)
{
	struct ib_uverbs_async_event_file *ev_file;
	struct file *filp;

	ev_file = kzalloc(sizeof(*ev_file), GFP_KERNEL);
	if (!ev_file)
		return ERR_PTR(-ENOMEM);

	ib_uverbs_init_event_queue(&ev_file->ev_queue);
	ev_file->uverbs_file = uverbs_file;
	kref_get(&ev_file->uverbs_file->ref);
	kref_init(&ev_file->ref);
	filp = anon_inode_getfile("[infinibandevent]", &uverbs_async_event_fops,
				  ev_file, O_RDONLY);
	if (IS_ERR(filp))
		goto err_put_refs;

	mutex_lock(&uverbs_file->device->lists_mutex);
	list_add_tail(&ev_file->list,
		      &uverbs_file->device->uverbs_events_file_list);
	mutex_unlock(&uverbs_file->device->lists_mutex);

	WARN_ON(uverbs_file->async_file);
	uverbs_file->async_file = ev_file;
	kref_get(&uverbs_file->async_file->ref);
	INIT_IB_EVENT_HANDLER(&uverbs_file->event_handler,
			      ib_dev,
			      ib_uverbs_event_handler);
	ib_register_event_handler(&uverbs_file->event_handler);
	/* At that point async file stuff was fully set */

	return filp;

err_put_refs:
	kref_put(&ev_file->uverbs_file->ref, ib_uverbs_release_file);
	kref_put(&ev_file->ref, ib_uverbs_release_async_event_file);
	return filp;
}

static int verify_command_mask(struct ib_device *ib_dev, __u32 command)
{
	u64 mask;

	if (command <= IB_USER_VERBS_CMD_OPEN_QP)
		mask = ib_dev->uverbs_cmd_mask;
	else
		mask = ib_dev->uverbs_ex_cmd_mask;

	if (mask & ((u64)1 << command))
		return 0;

	return -1;
}

static ssize_t ib_uverbs_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *pos)
{
	struct ib_uverbs_file *file = filp->private_data;
	struct ib_device *ib_dev;
	struct ib_uverbs_cmd_hdr hdr;
	__u32 command;
	__u32 flags;
	int srcu_key;
	ssize_t ret;

	if (!ib_safe_file_access(filp)) {
		pr_err_once("uverbs_write: process %d (%s) changed security contexts after opening file descriptor, this is not allowed.\n",
			    task_tgid_vnr(current), current->comm);
		return -EACCES;
	}

	if (count < sizeof hdr)
		return -EINVAL;

	if (copy_from_user(&hdr, buf, sizeof hdr))
		return -EFAULT;

	srcu_key = srcu_read_lock(&file->device->disassociate_srcu);
	ib_dev = srcu_dereference(file->device->ib_dev,
				  &file->device->disassociate_srcu);
	if (!ib_dev) {
		ret = -EIO;
		goto out;
	}

	if (hdr.command & ~(__u32)(IB_USER_VERBS_CMD_FLAGS_MASK |
				   IB_USER_VERBS_CMD_COMMAND_MASK)) {
		ret = -EINVAL;
		goto out;
	}

	command = hdr.command & IB_USER_VERBS_CMD_COMMAND_MASK;
	if (verify_command_mask(ib_dev, command)) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (!file->ucontext &&
	    command != IB_USER_VERBS_CMD_GET_CONTEXT) {
		ret = -EINVAL;
		goto out;
	}

	flags = (hdr.command &
		 IB_USER_VERBS_CMD_FLAGS_MASK) >> IB_USER_VERBS_CMD_FLAGS_SHIFT;

	if (!flags) {
		if (command >= ARRAY_SIZE(uverbs_cmd_table) ||
		    !uverbs_cmd_table[command]) {
			ret = -EINVAL;
			goto out;
		}

		if (hdr.in_words * 4 != count) {
			ret = -EINVAL;
			goto out;
		}

		ret = uverbs_cmd_table[command](file, ib_dev,
						 buf + sizeof(hdr),
						 hdr.in_words * 4,
						 hdr.out_words * 4);

	} else if (flags == IB_USER_VERBS_CMD_FLAG_EXTENDED) {
		struct ib_uverbs_ex_cmd_hdr ex_hdr;
		struct ib_udata ucore;
		struct ib_udata uhw;
		size_t written_count = count;

		if (command >= ARRAY_SIZE(uverbs_ex_cmd_table) ||
		    !uverbs_ex_cmd_table[command]) {
			ret = -ENOSYS;
			goto out;
		}

		if (!file->ucontext) {
			ret = -EINVAL;
			goto out;
		}

		if (count < (sizeof(hdr) + sizeof(ex_hdr))) {
			ret = -EINVAL;
			goto out;
		}

		if (copy_from_user(&ex_hdr, buf + sizeof(hdr), sizeof(ex_hdr))) {
			ret = -EFAULT;
			goto out;
		}

		count -= sizeof(hdr) + sizeof(ex_hdr);
		buf += sizeof(hdr) + sizeof(ex_hdr);

		if ((hdr.in_words + ex_hdr.provider_in_words) * 8 != count) {
			ret = -EINVAL;
			goto out;
		}

		if (ex_hdr.cmd_hdr_reserved) {
			ret = -EINVAL;
			goto out;
		}

		if (ex_hdr.response) {
			if (!hdr.out_words && !ex_hdr.provider_out_words) {
				ret = -EINVAL;
				goto out;
			}

			if (!access_ok(VERIFY_WRITE,
				       u64_to_user_ptr(ex_hdr.response),
				       (hdr.out_words + ex_hdr.provider_out_words) * 8)) {
				ret = -EFAULT;
				goto out;
			}
		} else {
			if (hdr.out_words || ex_hdr.provider_out_words) {
				ret = -EINVAL;
				goto out;
			}
		}

		ib_uverbs_init_udata_buf_or_null(&ucore, buf,
					u64_to_user_ptr(ex_hdr.response),
					hdr.in_words * 8, hdr.out_words * 8);

		ib_uverbs_init_udata_buf_or_null(&uhw,
					buf + ucore.inlen,
					u64_to_user_ptr(ex_hdr.response) + ucore.outlen,
					ex_hdr.provider_in_words * 8,
					ex_hdr.provider_out_words * 8);

		ret = uverbs_ex_cmd_table[command](file, ib_dev, &ucore, &uhw);
		if (!ret)
			ret = written_count;
	} else {
		ret = -ENOSYS;
	}

out:
	srcu_read_unlock(&file->device->disassociate_srcu, srcu_key);
	return ret;
}

static int ib_uverbs_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ib_uverbs_file *file = filp->private_data;
	struct ib_device *ib_dev;
	int ret = 0;
	int srcu_key;

	srcu_key = srcu_read_lock(&file->device->disassociate_srcu);
	ib_dev = srcu_dereference(file->device->ib_dev,
				  &file->device->disassociate_srcu);
	if (!ib_dev) {
		ret = -EIO;
		goto out;
	}

	if (!file->ucontext)
		ret = -ENODEV;
	else
		ret = ib_dev->mmap(file->ucontext, vma);
out:
	srcu_read_unlock(&file->device->disassociate_srcu, srcu_key);
	return ret;
}

/*
 * ib_uverbs_open() does not need the BKL:
 *
 *  - the ib_uverbs_device structures are properly reference counted and
 *    everything else is purely local to the file being created, so
 *    races against other open calls are not a problem;
 *  - there is no ioctl method to race against;
 *  - the open method will either immediately run -ENXIO, or all
 *    required initialization will be done.
 */
static int ib_uverbs_open(struct inode *inode, struct file *filp)
{
	struct ib_uverbs_device *dev;
	struct ib_uverbs_file *file;
	struct ib_device *ib_dev;
	int ret;
	int module_dependent;
	int srcu_key;

	dev = container_of(inode->i_cdev, struct ib_uverbs_device, cdev);
	if (!atomic_inc_not_zero(&dev->refcount))
		return -ENXIO;

	srcu_key = srcu_read_lock(&dev->disassociate_srcu);
	mutex_lock(&dev->lists_mutex);
	ib_dev = srcu_dereference(dev->ib_dev,
				  &dev->disassociate_srcu);
	if (!ib_dev) {
		ret = -EIO;
		goto err;
	}

	/* In case IB device supports disassociate ucontext, there is no hard
	 * dependency between uverbs device and its low level device.
	 */
	module_dependent = !(ib_dev->disassociate_ucontext);

	if (module_dependent) {
		if (!try_module_get(ib_dev->owner)) {
			ret = -ENODEV;
			goto err;
		}
	}

	file = kzalloc(sizeof(*file), GFP_KERNEL);
	if (!file) {
		ret = -ENOMEM;
		if (module_dependent)
			goto err_module;

		goto err;
	}

	file->device	 = dev;
	spin_lock_init(&file->idr_lock);
	idr_init(&file->idr);
	file->ucontext	 = NULL;
	file->async_file = NULL;
	kref_init(&file->ref);
	mutex_init(&file->mutex);
	mutex_init(&file->cleanup_mutex);

	filp->private_data = file;
	kobject_get(&dev->kobj);
	list_add_tail(&file->list, &dev->uverbs_file_list);
	mutex_unlock(&dev->lists_mutex);
	srcu_read_unlock(&dev->disassociate_srcu, srcu_key);

	return nonseekable_open(inode, filp);

err_module:
	module_put(ib_dev->owner);

err:
	mutex_unlock(&dev->lists_mutex);
	srcu_read_unlock(&dev->disassociate_srcu, srcu_key);
	if (atomic_dec_and_test(&dev->refcount))
		ib_uverbs_comp_dev(dev);

	return ret;
}

static int ib_uverbs_close(struct inode *inode, struct file *filp)
{
	struct ib_uverbs_file *file = filp->private_data;

	mutex_lock(&file->cleanup_mutex);
	if (file->ucontext) {
		ib_uverbs_cleanup_ucontext(file, file->ucontext, false);
		file->ucontext = NULL;
	}
	mutex_unlock(&file->cleanup_mutex);
	idr_destroy(&file->idr);

	mutex_lock(&file->device->lists_mutex);
	if (!file->is_closed) {
		list_del(&file->list);
		file->is_closed = 1;
	}
	mutex_unlock(&file->device->lists_mutex);

	if (file->async_file)
		kref_put(&file->async_file->ref,
			 ib_uverbs_release_async_event_file);

	kref_put(&file->ref, ib_uverbs_release_file);

	return 0;
}

static const struct file_operations uverbs_fops = {
	.owner	 = THIS_MODULE,
	.write	 = ib_uverbs_write,
	.open	 = ib_uverbs_open,
	.release = ib_uverbs_close,
	.llseek	 = no_llseek,
#if IS_ENABLED(CONFIG_INFINIBAND_EXP_USER_ACCESS)
	.unlocked_ioctl = ib_uverbs_ioctl,
#endif
};

static const struct file_operations uverbs_mmap_fops = {
	.owner	 = THIS_MODULE,
	.write	 = ib_uverbs_write,
	.mmap    = ib_uverbs_mmap,
	.open	 = ib_uverbs_open,
	.release = ib_uverbs_close,
	.llseek	 = no_llseek,
#if IS_ENABLED(CONFIG_INFINIBAND_EXP_USER_ACCESS)
	.unlocked_ioctl = ib_uverbs_ioctl,
#endif
};

static struct ib_client uverbs_client = {
	.name   = "uverbs",
	.add    = ib_uverbs_add_one,
	.remove = ib_uverbs_remove_one
};

static ssize_t show_ibdev(struct device *device, struct device_attribute *attr,
			  char *buf)
{
	int ret = -ENODEV;
	int srcu_key;
	struct ib_uverbs_device *dev = dev_get_drvdata(device);
	struct ib_device *ib_dev;

	if (!dev)
		return -ENODEV;

	srcu_key = srcu_read_lock(&dev->disassociate_srcu);
	ib_dev = srcu_dereference(dev->ib_dev, &dev->disassociate_srcu);
	if (ib_dev)
		ret = sprintf(buf, "%s\n", ib_dev->name);
	srcu_read_unlock(&dev->disassociate_srcu, srcu_key);

	return ret;
}
static DEVICE_ATTR(ibdev, S_IRUGO, show_ibdev, NULL);

static ssize_t show_dev_abi_version(struct device *device,
				    struct device_attribute *attr, char *buf)
{
	struct ib_uverbs_device *dev = dev_get_drvdata(device);
	int ret = -ENODEV;
	int srcu_key;
	struct ib_device *ib_dev;

	if (!dev)
		return -ENODEV;
	srcu_key = srcu_read_lock(&dev->disassociate_srcu);
	ib_dev = srcu_dereference(dev->ib_dev, &dev->disassociate_srcu);
	if (ib_dev)
		ret = sprintf(buf, "%d\n", ib_dev->uverbs_abi_ver);
	srcu_read_unlock(&dev->disassociate_srcu, srcu_key);

	return ret;
}
static DEVICE_ATTR(abi_version, S_IRUGO, show_dev_abi_version, NULL);

static CLASS_ATTR_STRING(abi_version, S_IRUGO,
			 __stringify(IB_USER_VERBS_ABI_VERSION));

static void ib_uverbs_add_one(struct ib_device *device)
{
	int devnum;
	dev_t base;
	struct ib_uverbs_device *uverbs_dev;
	int ret;

	if (!device->alloc_ucontext)
		return;

	uverbs_dev = kzalloc(sizeof *uverbs_dev, GFP_KERNEL);
	if (!uverbs_dev)
		return;

	ret = init_srcu_struct(&uverbs_dev->disassociate_srcu);
	if (ret) {
		kfree(uverbs_dev);
		return;
	}

	atomic_set(&uverbs_dev->refcount, 1);
	init_completion(&uverbs_dev->comp);
	uverbs_dev->xrcd_tree = RB_ROOT;
	mutex_init(&uverbs_dev->xrcd_tree_mutex);
	kobject_init(&uverbs_dev->kobj, &ib_uverbs_dev_ktype);
	mutex_init(&uverbs_dev->lists_mutex);
	INIT_LIST_HEAD(&uverbs_dev->uverbs_file_list);
	INIT_LIST_HEAD(&uverbs_dev->uverbs_events_file_list);

	devnum = find_first_zero_bit(dev_map, IB_UVERBS_MAX_DEVICES);
	if (devnum >= IB_UVERBS_MAX_DEVICES)
		goto err;
	uverbs_dev->devnum = devnum;
	set_bit(devnum, dev_map);
	if (devnum >= IB_UVERBS_NUM_FIXED_MINOR)
		base = dynamic_uverbs_dev + devnum - IB_UVERBS_NUM_FIXED_MINOR;
	else
		base = IB_UVERBS_BASE_DEV + devnum;

	rcu_assign_pointer(uverbs_dev->ib_dev, device);
	uverbs_dev->num_comp_vectors = device->num_comp_vectors;

	cdev_init(&uverbs_dev->cdev, NULL);
	uverbs_dev->cdev.owner = THIS_MODULE;
	uverbs_dev->cdev.ops = device->mmap ? &uverbs_mmap_fops : &uverbs_fops;
	cdev_set_parent(&uverbs_dev->cdev, &uverbs_dev->kobj);
	kobject_set_name(&uverbs_dev->cdev.kobj, "uverbs%d", uverbs_dev->devnum);
	if (cdev_add(&uverbs_dev->cdev, base, 1))
		goto err_cdev;

	uverbs_dev->dev = device_create(uverbs_class, device->dev.parent,
					uverbs_dev->cdev.dev, uverbs_dev,
					"uverbs%d", uverbs_dev->devnum);
	if (IS_ERR(uverbs_dev->dev))
		goto err_cdev;

	if (device_create_file(uverbs_dev->dev, &dev_attr_ibdev))
		goto err_class;
	if (device_create_file(uverbs_dev->dev, &dev_attr_abi_version))
		goto err_class;

	if (!device->specs_root) {
		const struct uverbs_object_tree_def *default_root[] = {
			uverbs_default_get_objects()};

		uverbs_dev->specs_root = uverbs_alloc_spec_tree(1,
								default_root);
		if (IS_ERR(uverbs_dev->specs_root))
			goto err_class;

		device->specs_root = uverbs_dev->specs_root;
	}

	ib_set_client_data(device, &uverbs_client, uverbs_dev);

	return;

err_class:
	device_destroy(uverbs_class, uverbs_dev->cdev.dev);

err_cdev:
	cdev_del(&uverbs_dev->cdev);
	clear_bit(devnum, dev_map);

err:
	if (atomic_dec_and_test(&uverbs_dev->refcount))
		ib_uverbs_comp_dev(uverbs_dev);
	wait_for_completion(&uverbs_dev->comp);
	kobject_put(&uverbs_dev->kobj);
	return;
}

static void ib_uverbs_free_hw_resources(struct ib_uverbs_device *uverbs_dev,
					struct ib_device *ib_dev)
{
	struct ib_uverbs_file *file;
	struct ib_uverbs_async_event_file *event_file;
	struct ib_event event;

	/* Pending running commands to terminate */
	synchronize_srcu(&uverbs_dev->disassociate_srcu);
	event.event = IB_EVENT_DEVICE_FATAL;
	event.element.port_num = 0;
	event.device = ib_dev;

	mutex_lock(&uverbs_dev->lists_mutex);
	while (!list_empty(&uverbs_dev->uverbs_file_list)) {
		struct ib_ucontext *ucontext;
		file = list_first_entry(&uverbs_dev->uverbs_file_list,
					struct ib_uverbs_file, list);
		file->is_closed = 1;
		list_del(&file->list);
		kref_get(&file->ref);
		mutex_unlock(&uverbs_dev->lists_mutex);


		mutex_lock(&file->cleanup_mutex);
		ucontext = file->ucontext;
		file->ucontext = NULL;
		mutex_unlock(&file->cleanup_mutex);

		/* At this point ib_uverbs_close cannot be running
		 * ib_uverbs_cleanup_ucontext
		 */
		if (ucontext) {
			/* We must release the mutex before going ahead and
			 * calling disassociate_ucontext. disassociate_ucontext
			 * might end up indirectly calling uverbs_close,
			 * for example due to freeing the resources
			 * (e.g mmput).
			 */
			ib_uverbs_event_handler(&file->event_handler, &event);
			ib_dev->disassociate_ucontext(ucontext);
			mutex_lock(&file->cleanup_mutex);
			ib_uverbs_cleanup_ucontext(file, ucontext, true);
			mutex_unlock(&file->cleanup_mutex);
		}

		mutex_lock(&uverbs_dev->lists_mutex);
		kref_put(&file->ref, ib_uverbs_release_file);
	}

	while (!list_empty(&uverbs_dev->uverbs_events_file_list)) {
		event_file = list_first_entry(&uverbs_dev->
					      uverbs_events_file_list,
					      struct ib_uverbs_async_event_file,
					      list);
		spin_lock_irq(&event_file->ev_queue.lock);
		event_file->ev_queue.is_closed = 1;
		spin_unlock_irq(&event_file->ev_queue.lock);

		list_del(&event_file->list);
		ib_unregister_event_handler(
			&event_file->uverbs_file->event_handler);
		event_file->uverbs_file->event_handler.device =
			NULL;

		wake_up_interruptible(&event_file->ev_queue.poll_wait);
		kill_fasync(&event_file->ev_queue.async_queue, SIGIO, POLL_IN);
	}
	mutex_unlock(&uverbs_dev->lists_mutex);
}

static void ib_uverbs_remove_one(struct ib_device *device, void *client_data)
{
	struct ib_uverbs_device *uverbs_dev = client_data;
	int wait_clients = 1;

	if (!uverbs_dev)
		return;

	dev_set_drvdata(uverbs_dev->dev, NULL);
	device_destroy(uverbs_class, uverbs_dev->cdev.dev);
	cdev_del(&uverbs_dev->cdev);
	clear_bit(uverbs_dev->devnum, dev_map);

	if (device->disassociate_ucontext) {
		/* We disassociate HW resources and immediately return.
		 * Userspace will see a EIO errno for all future access.
		 * Upon returning, ib_device may be freed internally and is not
		 * valid any more.
		 * uverbs_device is still available until all clients close
		 * their files, then the uverbs device ref count will be zero
		 * and its resources will be freed.
		 * Note: At this point no more files can be opened since the
		 * cdev was deleted, however active clients can still issue
		 * commands and close their open files.
		 */
		rcu_assign_pointer(uverbs_dev->ib_dev, NULL);
		ib_uverbs_free_hw_resources(uverbs_dev, device);
		wait_clients = 0;
	}

	if (atomic_dec_and_test(&uverbs_dev->refcount))
		ib_uverbs_comp_dev(uverbs_dev);
	if (wait_clients)
		wait_for_completion(&uverbs_dev->comp);
	if (uverbs_dev->specs_root) {
		uverbs_free_spec_tree(uverbs_dev->specs_root);
		device->specs_root = NULL;
	}

	kobject_put(&uverbs_dev->kobj);
}

static char *uverbs_devnode(struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return kasprintf(GFP_KERNEL, "infiniband/%s", dev_name(dev));
}

static int __init ib_uverbs_init(void)
{
	int ret;

	ret = register_chrdev_region(IB_UVERBS_BASE_DEV,
				     IB_UVERBS_NUM_FIXED_MINOR,
				     "infiniband_verbs");
	if (ret) {
		pr_err("user_verbs: couldn't register device number\n");
		goto out;
	}

	ret = alloc_chrdev_region(&dynamic_uverbs_dev, 0,
				  IB_UVERBS_NUM_DYNAMIC_MINOR,
				  "infiniband_verbs");
	if (ret) {
		pr_err("couldn't register dynamic device number\n");
		goto out_alloc;
	}

	uverbs_class = class_create(THIS_MODULE, "infiniband_verbs");
	if (IS_ERR(uverbs_class)) {
		ret = PTR_ERR(uverbs_class);
		pr_err("user_verbs: couldn't create class infiniband_verbs\n");
		goto out_chrdev;
	}

	uverbs_class->devnode = uverbs_devnode;

	ret = class_create_file(uverbs_class, &class_attr_abi_version.attr);
	if (ret) {
		pr_err("user_verbs: couldn't create abi_version attribute\n");
		goto out_class;
	}

	ret = ib_register_client(&uverbs_client);
	if (ret) {
		pr_err("user_verbs: couldn't register client\n");
		goto out_class;
	}

	return 0;

out_class:
	class_destroy(uverbs_class);

out_chrdev:
	unregister_chrdev_region(dynamic_uverbs_dev,
				 IB_UVERBS_NUM_DYNAMIC_MINOR);

out_alloc:
	unregister_chrdev_region(IB_UVERBS_BASE_DEV,
				 IB_UVERBS_NUM_FIXED_MINOR);

out:
	return ret;
}

static void __exit ib_uverbs_cleanup(void)
{
	ib_unregister_client(&uverbs_client);
	class_destroy(uverbs_class);
	unregister_chrdev_region(IB_UVERBS_BASE_DEV,
				 IB_UVERBS_NUM_FIXED_MINOR);
	unregister_chrdev_region(dynamic_uverbs_dev,
				 IB_UVERBS_NUM_DYNAMIC_MINOR);
}

module_init(ib_uverbs_init);
module_exit(ib_uverbs_cleanup);
