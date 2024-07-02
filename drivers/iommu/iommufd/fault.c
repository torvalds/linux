// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Intel Corporation
 */
#define pr_fmt(fmt) "iommufd: " fmt

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/iommufd.h>
#include <linux/poll.h>
#include <linux/anon_inodes.h>
#include <uapi/linux/iommufd.h>

#include "../iommu-priv.h"
#include "iommufd_private.h"

void iommufd_fault_destroy(struct iommufd_object *obj)
{
	struct iommufd_fault *fault = container_of(obj, struct iommufd_fault, obj);
	struct iopf_group *group, *next;

	/*
	 * The iommufd object's reference count is zero at this point.
	 * We can be confident that no other threads are currently
	 * accessing this pointer. Therefore, acquiring the mutex here
	 * is unnecessary.
	 */
	list_for_each_entry_safe(group, next, &fault->deliver, node) {
		list_del(&group->node);
		iopf_group_response(group, IOMMU_PAGE_RESP_INVALID);
		iopf_free_group(group);
	}
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

static ssize_t iommufd_fault_fops_read(struct file *filep, char __user *buf,
				       size_t count, loff_t *ppos)
{
	size_t fault_size = sizeof(struct iommu_hwpt_pgfault);
	struct iommufd_fault *fault = filep->private_data;
	struct iommu_hwpt_pgfault data;
	struct iommufd_device *idev;
	struct iopf_group *group;
	struct iopf_fault *iopf;
	size_t done = 0;
	int rc = 0;

	if (*ppos || count % fault_size)
		return -ESPIPE;

	mutex_lock(&fault->mutex);
	while (!list_empty(&fault->deliver) && count > done) {
		group = list_first_entry(&fault->deliver,
					 struct iopf_group, node);

		if (group->fault_count * fault_size > count - done)
			break;

		rc = xa_alloc(&fault->response, &group->cookie, group,
			      xa_limit_32b, GFP_KERNEL);
		if (rc)
			break;

		idev = to_iommufd_handle(group->attach_handle)->idev;
		list_for_each_entry(iopf, &group->faults, list) {
			iommufd_compose_fault_message(&iopf->fault,
						      &data, idev,
						      group->cookie);
			if (copy_to_user(buf + done, &data, fault_size)) {
				xa_erase(&fault->response, group->cookie);
				rc = -EFAULT;
				break;
			}
			done += fault_size;
		}

		list_del(&group->node);
	}
	mutex_unlock(&fault->mutex);

	return done == 0 ? rc : done;
}

static ssize_t iommufd_fault_fops_write(struct file *filep, const char __user *buf,
					size_t count, loff_t *ppos)
{
	size_t response_size = sizeof(struct iommu_hwpt_page_response);
	struct iommufd_fault *fault = filep->private_data;
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

static __poll_t iommufd_fault_fops_poll(struct file *filep,
					struct poll_table_struct *wait)
{
	struct iommufd_fault *fault = filep->private_data;
	__poll_t pollflags = EPOLLOUT;

	poll_wait(filep, &fault->wait_queue, wait);
	mutex_lock(&fault->mutex);
	if (!list_empty(&fault->deliver))
		pollflags |= EPOLLIN | EPOLLRDNORM;
	mutex_unlock(&fault->mutex);

	return pollflags;
}

static int iommufd_fault_fops_release(struct inode *inode, struct file *filep)
{
	struct iommufd_fault *fault = filep->private_data;

	refcount_dec(&fault->obj.users);
	iommufd_ctx_put(fault->ictx);
	return 0;
}

static const struct file_operations iommufd_fault_fops = {
	.owner		= THIS_MODULE,
	.open		= nonseekable_open,
	.read		= iommufd_fault_fops_read,
	.write		= iommufd_fault_fops_write,
	.poll		= iommufd_fault_fops_poll,
	.release	= iommufd_fault_fops_release,
	.llseek		= no_llseek,
};

int iommufd_fault_alloc(struct iommufd_ucmd *ucmd)
{
	struct iommu_fault_alloc *cmd = ucmd->cmd;
	struct iommufd_fault *fault;
	struct file *filep;
	int fdno;
	int rc;

	if (cmd->flags)
		return -EOPNOTSUPP;

	fault = iommufd_object_alloc(ucmd->ictx, fault, IOMMUFD_OBJ_FAULT);
	if (IS_ERR(fault))
		return PTR_ERR(fault);

	fault->ictx = ucmd->ictx;
	INIT_LIST_HEAD(&fault->deliver);
	xa_init_flags(&fault->response, XA_FLAGS_ALLOC1);
	mutex_init(&fault->mutex);
	init_waitqueue_head(&fault->wait_queue);

	filep = anon_inode_getfile("[iommufd-pgfault]", &iommufd_fault_fops,
				   fault, O_RDWR);
	if (IS_ERR(filep)) {
		rc = PTR_ERR(filep);
		goto out_abort;
	}

	refcount_inc(&fault->obj.users);
	iommufd_ctx_get(fault->ictx);
	fault->filep = filep;

	fdno = get_unused_fd_flags(O_CLOEXEC);
	if (fdno < 0) {
		rc = fdno;
		goto out_fput;
	}

	cmd->out_fault_id = fault->obj.id;
	cmd->out_fault_fd = fdno;

	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_put_fdno;
	iommufd_object_finalize(ucmd->ictx, &fault->obj);

	fd_install(fdno, fault->filep);

	return 0;
out_put_fdno:
	put_unused_fd(fdno);
out_fput:
	fput(filep);
	refcount_dec(&fault->obj.users);
	iommufd_ctx_put(fault->ictx);
out_abort:
	iommufd_object_abort_and_destroy(ucmd->ictx, &fault->obj);

	return rc;
}
