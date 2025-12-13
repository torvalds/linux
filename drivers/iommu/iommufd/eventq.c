// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Intel Corporation
 */
#define pr_fmt(fmt) "iommufd: " fmt

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/iommufd.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <uapi/linux/iommufd.h>

#include "../iommu-priv.h"
#include "iommufd_private.h"

/* IOMMUFD_OBJ_FAULT Functions */
void iommufd_auto_response_faults(struct iommufd_hw_pagetable *hwpt,
				  struct iommufd_attach_handle *handle)
{
	struct iommufd_fault *fault = hwpt->fault;
	struct iopf_group *group, *next;
	struct list_head free_list;
	unsigned long index;

	if (!fault || !handle)
		return;
	INIT_LIST_HEAD(&free_list);

	mutex_lock(&fault->mutex);
	spin_lock(&fault->common.lock);
	list_for_each_entry_safe(group, next, &fault->common.deliver, node) {
		if (group->attach_handle != &handle->handle)
			continue;
		list_move(&group->node, &free_list);
	}
	spin_unlock(&fault->common.lock);

	list_for_each_entry_safe(group, next, &free_list, node) {
		list_del(&group->node);
		iopf_group_response(group, IOMMU_PAGE_RESP_INVALID);
		iopf_free_group(group);
	}

	xa_for_each(&fault->response, index, group) {
		if (group->attach_handle != &handle->handle)
			continue;
		xa_erase(&fault->response, index);
		iopf_group_response(group, IOMMU_PAGE_RESP_INVALID);
		iopf_free_group(group);
	}
	mutex_unlock(&fault->mutex);
}

void iommufd_fault_destroy(struct iommufd_object *obj)
{
	struct iommufd_eventq *eventq =
		container_of(obj, struct iommufd_eventq, obj);
	struct iommufd_fault *fault = eventq_to_fault(eventq);
	struct iopf_group *group, *next;
	unsigned long index;

	/*
	 * The iommufd object's reference count is zero at this point.
	 * We can be confident that no other threads are currently
	 * accessing this pointer. Therefore, acquiring the mutex here
	 * is unnecessary.
	 */
	list_for_each_entry_safe(group, next, &fault->common.deliver, node) {
		list_del(&group->node);
		iopf_group_response(group, IOMMU_PAGE_RESP_INVALID);
		iopf_free_group(group);
	}
	xa_for_each(&fault->response, index, group) {
		xa_erase(&fault->response, index);
		iopf_group_response(group, IOMMU_PAGE_RESP_INVALID);
		iopf_free_group(group);
	}
	xa_destroy(&fault->response);
	mutex_destroy(&fault->mutex);
}

static void iommufd_compose_fault_message(struct iommu_fault *fault,
					  struct iommu_hwpt_pgfault *hwpt_fault,
					  struct iommufd_device *idev,
					  u32 cookie)
{
	hwpt_fault->flags = fault->prm.flags;
	hwpt_fault->dev_id = idev->obj.id;
	hwpt_fault->pasid = fault->prm.pasid;
	hwpt_fault->grpid = fault->prm.grpid;
	hwpt_fault->perm = fault->prm.perm;
	hwpt_fault->addr = fault->prm.addr;
	hwpt_fault->length = 0;
	hwpt_fault->cookie = cookie;
}

/* Fetch the first node out of the fault->deliver list */
static struct iopf_group *
iommufd_fault_deliver_fetch(struct iommufd_fault *fault)
{
	struct list_head *list = &fault->common.deliver;
	struct iopf_group *group = NULL;

	spin_lock(&fault->common.lock);
	if (!list_empty(list)) {
		group = list_first_entry(list, struct iopf_group, node);
		list_del(&group->node);
	}
	spin_unlock(&fault->common.lock);
	return group;
}

/* Restore a node back to the head of the fault->deliver list */
static void iommufd_fault_deliver_restore(struct iommufd_fault *fault,
					  struct iopf_group *group)
{
	spin_lock(&fault->common.lock);
	list_add(&group->node, &fault->common.deliver);
	spin_unlock(&fault->common.lock);
}

static ssize_t iommufd_fault_fops_read(struct file *filep, char __user *buf,
				       size_t count, loff_t *ppos)
{
	size_t fault_size = sizeof(struct iommu_hwpt_pgfault);
	struct iommufd_eventq *eventq = filep->private_data;
	struct iommufd_fault *fault = eventq_to_fault(eventq);
	struct iommu_hwpt_pgfault data = {};
	struct iommufd_device *idev;
	struct iopf_group *group;
	struct iopf_fault *iopf;
	size_t done = 0;
	int rc = 0;

	if (*ppos || count % fault_size)
		return -ESPIPE;

	mutex_lock(&fault->mutex);
	while ((group = iommufd_fault_deliver_fetch(fault))) {
		if (done >= count ||
		    group->fault_count * fault_size > count - done) {
			iommufd_fault_deliver_restore(fault, group);
			break;
		}

		rc = xa_alloc(&fault->response, &group->cookie, group,
			      xa_limit_32b, GFP_KERNEL);
		if (rc) {
			iommufd_fault_deliver_restore(fault, group);
			break;
		}

		idev = to_iommufd_handle(group->attach_handle)->idev;
		list_for_each_entry(iopf, &group->faults, list) {
			iommufd_compose_fault_message(&iopf->fault,
						      &data, idev,
						      group->cookie);
			if (copy_to_user(buf + done, &data, fault_size)) {
				xa_erase(&fault->response, group->cookie);
				iommufd_fault_deliver_restore(fault, group);
				rc = -EFAULT;
				break;
			}
			done += fault_size;
		}
	}
	mutex_unlock(&fault->mutex);

	return done == 0 ? rc : done;
}

static ssize_t iommufd_fault_fops_write(struct file *filep, const char __user *buf,
					size_t count, loff_t *ppos)
{
	size_t response_size = sizeof(struct iommu_hwpt_page_response);
	struct iommufd_eventq *eventq = filep->private_data;
	struct iommufd_fault *fault = eventq_to_fault(eventq);
	struct iommu_hwpt_page_response response;
	struct iopf_group *group;
	size_t done = 0;
	int rc = 0;

	if (*ppos || count % response_size)
		return -ESPIPE;

	mutex_lock(&fault->mutex);
	while (count > done) {
		rc = copy_from_user(&response, buf + done, response_size);
		if (rc)
			break;

		static_assert((int)IOMMUFD_PAGE_RESP_SUCCESS ==
			      (int)IOMMU_PAGE_RESP_SUCCESS);
		static_assert((int)IOMMUFD_PAGE_RESP_INVALID ==
			      (int)IOMMU_PAGE_RESP_INVALID);
		if (response.code != IOMMUFD_PAGE_RESP_SUCCESS &&
		    response.code != IOMMUFD_PAGE_RESP_INVALID) {
			rc = -EINVAL;
			break;
		}

		group = xa_erase(&fault->response, response.cookie);
		if (!group) {
			rc = -EINVAL;
			break;
		}

		iopf_group_response(group, response.code);
		iopf_free_group(group);
		done += response_size;
	}
	mutex_unlock(&fault->mutex);

	return done == 0 ? rc : done;
}

/* IOMMUFD_OBJ_VEVENTQ Functions */

void iommufd_veventq_abort(struct iommufd_object *obj)
{
	struct iommufd_eventq *eventq =
		container_of(obj, struct iommufd_eventq, obj);
	struct iommufd_veventq *veventq = eventq_to_veventq(eventq);
	struct iommufd_viommu *viommu = veventq->viommu;
	struct iommufd_vevent *cur, *next;

	lockdep_assert_held_write(&viommu->veventqs_rwsem);

	list_for_each_entry_safe(cur, next, &eventq->deliver, node) {
		list_del(&cur->node);
		if (cur != &veventq->lost_events_header)
			kfree(cur);
	}

	refcount_dec(&viommu->obj.users);
	list_del(&veventq->node);
}

void iommufd_veventq_destroy(struct iommufd_object *obj)
{
	struct iommufd_veventq *veventq = eventq_to_veventq(
		container_of(obj, struct iommufd_eventq, obj));

	down_write(&veventq->viommu->veventqs_rwsem);
	iommufd_veventq_abort(obj);
	up_write(&veventq->viommu->veventqs_rwsem);
}

static struct iommufd_vevent *
iommufd_veventq_deliver_fetch(struct iommufd_veventq *veventq)
{
	struct iommufd_eventq *eventq = &veventq->common;
	struct list_head *list = &eventq->deliver;
	struct iommufd_vevent *vevent = NULL;

	spin_lock(&eventq->lock);
	if (!list_empty(list)) {
		struct iommufd_vevent *next;

		next = list_first_entry(list, struct iommufd_vevent, node);
		/* Make a copy of the lost_events_header for copy_to_user */
		if (next == &veventq->lost_events_header) {
			vevent = kzalloc(sizeof(*vevent), GFP_ATOMIC);
			if (!vevent)
				goto out_unlock;
		}
		list_del(&next->node);
		if (vevent)
			memcpy(vevent, next, sizeof(*vevent));
		else
			vevent = next;
	}
out_unlock:
	spin_unlock(&eventq->lock);
	return vevent;
}

static void iommufd_veventq_deliver_restore(struct iommufd_veventq *veventq,
					    struct iommufd_vevent *vevent)
{
	struct iommufd_eventq *eventq = &veventq->common;
	struct list_head *list = &eventq->deliver;

	spin_lock(&eventq->lock);
	if (vevent_for_lost_events_header(vevent)) {
		/* Remove the copy of the lost_events_header */
		kfree(vevent);
		vevent = NULL;
		/* An empty list needs the lost_events_header back */
		if (list_empty(list))
			vevent = &veventq->lost_events_header;
	}
	if (vevent)
		list_add(&vevent->node, list);
	spin_unlock(&eventq->lock);
}

static ssize_t iommufd_veventq_fops_read(struct file *filep, char __user *buf,
					 size_t count, loff_t *ppos)
{
	struct iommufd_eventq *eventq = filep->private_data;
	struct iommufd_veventq *veventq = eventq_to_veventq(eventq);
	struct iommufd_vevent_header *hdr;
	struct iommufd_vevent *cur;
	size_t done = 0;
	int rc = 0;

	if (*ppos)
		return -ESPIPE;

	while ((cur = iommufd_veventq_deliver_fetch(veventq))) {
		/* Validate the remaining bytes against the header size */
		if (done >= count || sizeof(*hdr) > count - done) {
			iommufd_veventq_deliver_restore(veventq, cur);
			break;
		}
		hdr = &cur->header;

		/* If being a normal vEVENT, validate against the full size */
		if (!vevent_for_lost_events_header(cur) &&
		    sizeof(hdr) + cur->data_len > count - done) {
			iommufd_veventq_deliver_restore(veventq, cur);
			break;
		}

		if (copy_to_user(buf + done, hdr, sizeof(*hdr))) {
			iommufd_veventq_deliver_restore(veventq, cur);
			rc = -EFAULT;
			break;
		}
		done += sizeof(*hdr);

		if (cur->data_len &&
		    copy_to_user(buf + done, cur->event_data, cur->data_len)) {
			iommufd_veventq_deliver_restore(veventq, cur);
			rc = -EFAULT;
			break;
		}
		spin_lock(&eventq->lock);
		if (!vevent_for_lost_events_header(cur))
			veventq->num_events--;
		spin_unlock(&eventq->lock);
		done += cur->data_len;
		kfree(cur);
	}

	return done == 0 ? rc : done;
}

/* Common Event Queue Functions */

static __poll_t iommufd_eventq_fops_poll(struct file *filep,
					 struct poll_table_struct *wait)
{
	struct iommufd_eventq *eventq = filep->private_data;
	__poll_t pollflags = 0;

	if (eventq->obj.type == IOMMUFD_OBJ_FAULT)
		pollflags |= EPOLLOUT;

	poll_wait(filep, &eventq->wait_queue, wait);
	spin_lock(&eventq->lock);
	if (!list_empty(&eventq->deliver))
		pollflags |= EPOLLIN | EPOLLRDNORM;
	spin_unlock(&eventq->lock);

	return pollflags;
}

static int iommufd_eventq_fops_release(struct inode *inode, struct file *filep)
{
	struct iommufd_eventq *eventq = filep->private_data;

	refcount_dec(&eventq->obj.users);
	iommufd_ctx_put(eventq->ictx);
	return 0;
}

#define INIT_EVENTQ_FOPS(read_op, write_op)                                    \
	((const struct file_operations){                                       \
		.owner = THIS_MODULE,                                          \
		.open = nonseekable_open,                                      \
		.read = read_op,                                               \
		.write = write_op,                                             \
		.poll = iommufd_eventq_fops_poll,                              \
		.release = iommufd_eventq_fops_release,                        \
	})

static int iommufd_eventq_init(struct iommufd_eventq *eventq, char *name,
			       struct iommufd_ctx *ictx,
			       const struct file_operations *fops)
{
	struct file *filep;

	spin_lock_init(&eventq->lock);
	INIT_LIST_HEAD(&eventq->deliver);
	init_waitqueue_head(&eventq->wait_queue);

	/* The filep is fput() by the core code during failure */
	filep = anon_inode_getfile(name, fops, eventq, O_RDWR);
	if (IS_ERR(filep))
		return PTR_ERR(filep);

	eventq->ictx = ictx;
	iommufd_ctx_get(eventq->ictx);
	eventq->filep = filep;
	refcount_inc(&eventq->obj.users);

	return get_unused_fd_flags(O_CLOEXEC);
}

static const struct file_operations iommufd_fault_fops =
	INIT_EVENTQ_FOPS(iommufd_fault_fops_read, iommufd_fault_fops_write);

int iommufd_fault_alloc(struct iommufd_ucmd *ucmd)
{
	struct iommu_fault_alloc *cmd = ucmd->cmd;
	struct iommufd_fault *fault;
	int fdno;
	int rc;

	if (cmd->flags)
		return -EOPNOTSUPP;

	fault = __iommufd_object_alloc_ucmd(ucmd, fault, IOMMUFD_OBJ_FAULT,
					    common.obj);
	if (IS_ERR(fault))
		return PTR_ERR(fault);

	xa_init_flags(&fault->response, XA_FLAGS_ALLOC1);
	mutex_init(&fault->mutex);

	fdno = iommufd_eventq_init(&fault->common, "[iommufd-pgfault]",
				   ucmd->ictx, &iommufd_fault_fops);
	if (fdno < 0)
		return fdno;

	cmd->out_fault_id = fault->common.obj.id;
	cmd->out_fault_fd = fdno;

	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_put_fdno;

	fd_install(fdno, fault->common.filep);

	return 0;
out_put_fdno:
	put_unused_fd(fdno);
	return rc;
}

int iommufd_fault_iopf_handler(struct iopf_group *group)
{
	struct iommufd_hw_pagetable *hwpt;
	struct iommufd_fault *fault;

	hwpt = group->attach_handle->domain->iommufd_hwpt;
	fault = hwpt->fault;

	spin_lock(&fault->common.lock);
	list_add_tail(&group->node, &fault->common.deliver);
	spin_unlock(&fault->common.lock);

	wake_up_interruptible(&fault->common.wait_queue);

	return 0;
}

static const struct file_operations iommufd_veventq_fops =
	INIT_EVENTQ_FOPS(iommufd_veventq_fops_read, NULL);

int iommufd_veventq_alloc(struct iommufd_ucmd *ucmd)
{
	struct iommu_veventq_alloc *cmd = ucmd->cmd;
	struct iommufd_veventq *veventq;
	struct iommufd_viommu *viommu;
	int fdno;
	int rc;

	if (cmd->flags || cmd->__reserved ||
	    cmd->type == IOMMU_VEVENTQ_TYPE_DEFAULT)
		return -EOPNOTSUPP;
	if (!cmd->veventq_depth)
		return -EINVAL;

	viommu = iommufd_get_viommu(ucmd, cmd->viommu_id);
	if (IS_ERR(viommu))
		return PTR_ERR(viommu);

	down_write(&viommu->veventqs_rwsem);

	if (iommufd_viommu_find_veventq(viommu, cmd->type)) {
		rc = -EEXIST;
		goto out_unlock_veventqs;
	}

	veventq = __iommufd_object_alloc(ucmd->ictx, veventq,
					 IOMMUFD_OBJ_VEVENTQ, common.obj);
	if (IS_ERR(veventq)) {
		rc = PTR_ERR(veventq);
		goto out_unlock_veventqs;
	}

	veventq->type = cmd->type;
	veventq->viommu = viommu;
	refcount_inc(&viommu->obj.users);
	veventq->depth = cmd->veventq_depth;
	list_add_tail(&veventq->node, &viommu->veventqs);
	veventq->lost_events_header.header.flags =
		IOMMU_VEVENTQ_FLAG_LOST_EVENTS;

	fdno = iommufd_eventq_init(&veventq->common, "[iommufd-viommu-event]",
				   ucmd->ictx, &iommufd_veventq_fops);
	if (fdno < 0) {
		rc = fdno;
		goto out_abort;
	}

	cmd->out_veventq_id = veventq->common.obj.id;
	cmd->out_veventq_fd = fdno;

	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_put_fdno;

	iommufd_object_finalize(ucmd->ictx, &veventq->common.obj);
	fd_install(fdno, veventq->common.filep);
	goto out_unlock_veventqs;

out_put_fdno:
	put_unused_fd(fdno);
out_abort:
	iommufd_object_abort_and_destroy(ucmd->ictx, &veventq->common.obj);
out_unlock_veventqs:
	up_write(&viommu->veventqs_rwsem);
	iommufd_put_object(ucmd->ictx, &viommu->obj);
	return rc;
}
