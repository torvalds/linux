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
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/poll.h>
#include <uapi/linux/iommufd.h>

#include "../iommu-priv.h"
#include "iommufd_private.h"

/* IOMMUFD_OBJ_FAULT Functions */

int iommufd_fault_iopf_enable(struct iommufd_device *idev)
{
	struct device *dev = idev->dev;
	int ret;

	/*
	 * Once we turn on PCI/PRI support for VF, the response failure code
	 * should not be forwarded to the hardware due to PRI being a shared
	 * resource between PF and VFs. There is no coordination for this
	 * shared capability. This waits for a vPRI reset to recover.
	 */
	if (dev_is_pci(dev)) {
		struct pci_dev *pdev = to_pci_dev(dev);

		if (pdev->is_virtfn && pci_pri_supported(pdev))
			return -EINVAL;
	}

	mutex_lock(&idev->iopf_lock);
	/* Device iopf has already been on. */
	if (++idev->iopf_enabled > 1) {
		mutex_unlock(&idev->iopf_lock);
		return 0;
	}

	ret = iommu_dev_enable_feature(dev, IOMMU_DEV_FEAT_IOPF);
	if (ret)
		--idev->iopf_enabled;
	mutex_unlock(&idev->iopf_lock);

	return ret;
}

void iommufd_fault_iopf_disable(struct iommufd_device *idev)
{
	mutex_lock(&idev->iopf_lock);
	if (!WARN_ON(idev->iopf_enabled == 0)) {
		if (--idev->iopf_enabled == 0)
			iommu_dev_disable_feature(idev->dev, IOMMU_DEV_FEAT_IOPF);
	}
	mutex_unlock(&idev->iopf_lock);
}

void iommufd_auto_response_faults(struct iommufd_hw_pagetable *hwpt,
				  struct iommufd_attach_handle *handle)
{
	struct iommufd_fault *fault = hwpt->fault;
	struct iopf_group *group, *next;
	struct list_head free_list;
	unsigned long index;

	if (!fault)
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

/* Common Event Queue Functions */

static __poll_t iommufd_eventq_fops_poll(struct file *filep,
					 struct poll_table_struct *wait)
{
	struct iommufd_eventq *eventq = filep->private_data;
	__poll_t pollflags = EPOLLOUT;

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
	int fdno;

	spin_lock_init(&eventq->lock);
	INIT_LIST_HEAD(&eventq->deliver);
	init_waitqueue_head(&eventq->wait_queue);

	filep = anon_inode_getfile(name, fops, eventq, O_RDWR);
	if (IS_ERR(filep))
		return PTR_ERR(filep);

	eventq->ictx = ictx;
	iommufd_ctx_get(eventq->ictx);
	eventq->filep = filep;
	refcount_inc(&eventq->obj.users);

	fdno = get_unused_fd_flags(O_CLOEXEC);
	if (fdno < 0)
		fput(filep);
	return fdno;
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

	fault = __iommufd_object_alloc(ucmd->ictx, fault, IOMMUFD_OBJ_FAULT,
				       common.obj);
	if (IS_ERR(fault))
		return PTR_ERR(fault);

	xa_init_flags(&fault->response, XA_FLAGS_ALLOC1);
	mutex_init(&fault->mutex);

	fdno = iommufd_eventq_init(&fault->common, "[iommufd-pgfault]",
				   ucmd->ictx, &iommufd_fault_fops);
	if (fdno < 0) {
		rc = fdno;
		goto out_abort;
	}

	cmd->out_fault_id = fault->common.obj.id;
	cmd->out_fault_fd = fdno;

	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_put_fdno;
	iommufd_object_finalize(ucmd->ictx, &fault->common.obj);

	fd_install(fdno, fault->common.filep);

	return 0;
out_put_fdno:
	put_unused_fd(fdno);
	fput(fault->common.filep);
out_abort:
	iommufd_object_abort_and_destroy(ucmd->ictx, &fault->common.obj);

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
