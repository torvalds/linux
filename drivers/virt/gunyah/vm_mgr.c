// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "gh_vm_mgr: " fmt

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/gunyah_rsc_mgr.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <uapi/linux/gunyah.h>

#include "vm_mgr.h"

static int gh_vm_rm_notification_status(struct gh_vm *ghvm, void *data)
{
	struct gh_rm_vm_status_payload *payload = data;

	if (payload->vmid != ghvm->vmid)
		return NOTIFY_OK;

	/* All other state transitions are synchronous to a corresponding RM call */
	if (payload->vm_status == GH_RM_VM_STATUS_RESET) {
		down_write(&ghvm->status_lock);
		ghvm->vm_status = payload->vm_status;
		up_write(&ghvm->status_lock);
		wake_up(&ghvm->vm_status_wait);
	}

	return NOTIFY_DONE;
}

static int gh_vm_rm_notification_exited(struct gh_vm *ghvm, void *data)
{
	struct gh_rm_vm_exited_payload *payload = data;

	if (payload->vmid != ghvm->vmid)
		return NOTIFY_OK;

	down_write(&ghvm->status_lock);
	ghvm->vm_status = GH_RM_VM_STATUS_EXITED;
	up_write(&ghvm->status_lock);

	return NOTIFY_DONE;
}

static int gh_vm_rm_notification(struct notifier_block *nb, unsigned long action, void *data)
{
	struct gh_vm *ghvm = container_of(nb, struct gh_vm, nb);

	switch (action) {
	case GH_RM_NOTIFICATION_VM_STATUS:
		return gh_vm_rm_notification_status(ghvm, data);
	case GH_RM_NOTIFICATION_VM_EXITED:
		return gh_vm_rm_notification_exited(ghvm, data);
	default:
		return NOTIFY_OK;
	}
}

static void gh_vm_stop(struct gh_vm *ghvm)
{
	int ret;

	down_write(&ghvm->status_lock);
	if (ghvm->vm_status == GH_RM_VM_STATUS_RUNNING) {
		ret = gh_rm_vm_stop(ghvm->rm, ghvm->vmid);
		if (ret)
			dev_warn(ghvm->parent, "Failed to stop VM: %d\n", ret);
	}

	ghvm->vm_status = GH_RM_VM_STATUS_EXITED;
	up_write(&ghvm->status_lock);
}

static void gh_vm_free(struct work_struct *work)
{
	struct gh_vm *ghvm = container_of(work, struct gh_vm, free_work);
	struct gh_vm_mem *mapping, *tmp;
	int ret;

	switch (ghvm->vm_status) {
	case GH_RM_VM_STATUS_RUNNING:
		gh_vm_stop(ghvm);
		fallthrough;
	case GH_RM_VM_STATUS_INIT_FAILED:
	case GH_RM_VM_STATUS_EXITED:
		ret = gh_rm_vm_reset(ghvm->rm, ghvm->vmid);
		if (ret)
			dev_err(ghvm->parent, "Failed to reset the vm: %d\n", ret);
		wait_event(ghvm->vm_status_wait, ghvm->vm_status == GH_RM_VM_STATUS_RESET);

		fallthrough;
	case GH_RM_VM_STATUS_LOAD:
		mutex_lock(&ghvm->mm_lock);
		list_for_each_entry_safe(mapping, tmp, &ghvm->memory_mappings, list) {
			gh_vm_mem_reclaim(ghvm, mapping);
			kfree(mapping);
		}
		mutex_unlock(&ghvm->mm_lock);
		fallthrough;
	case GH_RM_VM_STATUS_NO_STATE:
		ret = gh_rm_dealloc_vmid(ghvm->rm, ghvm->vmid);
		if (ret)
			dev_warn(ghvm->parent, "Failed to deallocate vmid: %d\n", ret);

		gh_rm_notifier_unregister(ghvm->rm, &ghvm->nb);
		gh_rm_put(ghvm->rm);
		kfree(ghvm);
		break;
	default:
		dev_err(ghvm->parent, "VM is unknown state: %d. VM will not be cleaned up.\n",
			ghvm->vm_status);

		gh_rm_notifier_unregister(ghvm->rm, &ghvm->nb);
		gh_rm_put(ghvm->rm);
		kfree(ghvm);
		break;
	}
}

static __must_check struct gh_vm *gh_vm_alloc(struct gh_rm *rm)
{
	struct gh_vm *ghvm;
	int vmid, ret;

	vmid = gh_rm_alloc_vmid(rm, 0);
	if (vmid < 0)
		return ERR_PTR(vmid);

	ghvm = kzalloc(sizeof(*ghvm), GFP_KERNEL);
	if (!ghvm) {
		gh_rm_dealloc_vmid(rm, vmid);
		return ERR_PTR(-ENOMEM);
	}

	ghvm->parent = gh_rm_get(rm);
	ghvm->vmid = vmid;
	ghvm->rm = rm;

	init_waitqueue_head(&ghvm->vm_status_wait);
	ghvm->nb.notifier_call = gh_vm_rm_notification;
	ret = gh_rm_notifier_register(rm, &ghvm->nb);
	if (ret) {
		gh_rm_put(rm);
		gh_rm_dealloc_vmid(rm, vmid);
		kfree(ghvm);
		return ERR_PTR(ret);
	}

	mutex_init(&ghvm->mm_lock);
	INIT_LIST_HEAD(&ghvm->memory_mappings);
	init_rwsem(&ghvm->status_lock);
	INIT_WORK(&ghvm->free_work, gh_vm_free);
	ghvm->vm_status = GH_RM_VM_STATUS_LOAD;

	return ghvm;
}

static int gh_vm_start(struct gh_vm *ghvm)
{
	struct gh_vm_mem *mapping;
	u64 dtb_offset;
	u32 mem_handle;
	int ret;

	down_write(&ghvm->status_lock);
	if (ghvm->vm_status != GH_RM_VM_STATUS_LOAD) {
		up_write(&ghvm->status_lock);
		return 0;
	}

	mutex_lock(&ghvm->mm_lock);
	list_for_each_entry(mapping, &ghvm->memory_mappings, list) {
		switch (mapping->share_type) {
		case VM_MEM_LEND:
			ret = gh_rm_mem_lend(ghvm->rm, &mapping->parcel);
			break;
		case VM_MEM_SHARE:
			ret = gh_rm_mem_share(ghvm->rm, &mapping->parcel);
			break;
		}
		if (ret) {
			dev_warn(ghvm->parent, "Failed to %s parcel %d: %d\n",
				mapping->share_type == VM_MEM_LEND ? "lend" : "share",
				mapping->parcel.label,
				ret);
			goto err;
		}
	}
	mutex_unlock(&ghvm->mm_lock);

	mapping = gh_vm_mem_find_by_addr(ghvm, ghvm->dtb_config.guest_phys_addr,
					ghvm->dtb_config.size);
	if (!mapping) {
		dev_warn(ghvm->parent, "Failed to find the memory_handle for DTB\n");
		ret = -EINVAL;
		goto err;
	}

	mem_handle = mapping->parcel.mem_handle;
	dtb_offset = ghvm->dtb_config.guest_phys_addr - mapping->guest_phys_addr;

	ret = gh_rm_vm_configure(ghvm->rm, ghvm->vmid, ghvm->auth, mem_handle,
				0, 0, dtb_offset, ghvm->dtb_config.size);
	if (ret) {
		dev_warn(ghvm->parent, "Failed to configure VM: %d\n", ret);
		goto err;
	}

	ret = gh_rm_vm_init(ghvm->rm, ghvm->vmid);
	ghvm->vm_status = GH_RM_VM_STATUS_RESET;
	if (ret) {
		dev_warn(ghvm->parent, "Failed to initialize VM: %d\n", ret);
		goto err;
	}

	ret = gh_rm_vm_start(ghvm->rm, ghvm->vmid);
	if (ret) {
		dev_warn(ghvm->parent, "Failed to start VM: %d\n", ret);
		goto err;
	}

	ghvm->vm_status = GH_RM_VM_STATUS_RUNNING;
	up_write(&ghvm->status_lock);
	return ret;
err:
	ghvm->vm_status = GH_RM_VM_STATUS_INIT_FAILED;
	/* gh_vm_free will handle releasing resources and reclaiming memory */
	up_write(&ghvm->status_lock);
	return ret;
}

static int gh_vm_ensure_started(struct gh_vm *ghvm)
{
	int ret;

	ret = down_read_interruptible(&ghvm->status_lock);
	if (ret)
		return ret;

	/* Unlikely because VM is typically started */
	if (unlikely(ghvm->vm_status == GH_RM_VM_STATUS_LOAD)) {
		up_read(&ghvm->status_lock);
		ret = gh_vm_start(ghvm);
		if (ret)
			goto out;
		/** gh_vm_start() is guaranteed to bring status out of
		 * GH_RM_VM_STATUS_LOAD, thus inifitely recursive call is not
		 * possible
		 */
		return gh_vm_ensure_started(ghvm);
	}

	/* Unlikely because VM is typically running */
	if (unlikely(ghvm->vm_status != GH_RM_VM_STATUS_RUNNING))
		ret = -ENODEV;

out:
	up_read(&ghvm->status_lock);
	return ret;
}

static long gh_vm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gh_vm *ghvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	long r;

	switch (cmd) {
	case GH_VM_SET_USER_MEM_REGION: {
		struct gh_userspace_memory_region region;

		if (!gh_api_has_feature(GH_FEATURE_MEMEXTENT))
			return -EOPNOTSUPP;

		if (copy_from_user(&region, argp, sizeof(region)))
			return -EFAULT;

		/* All other flag bits are reserved for future use */
		if (region.flags & ~(GH_MEM_ALLOW_READ | GH_MEM_ALLOW_WRITE | GH_MEM_ALLOW_EXEC))
			return -EINVAL;

		r = gh_vm_mem_alloc(ghvm, &region);
		break;
	}
	case GH_VM_SET_DTB_CONFIG: {
		struct gh_vm_dtb_config dtb_config;

		if (copy_from_user(&dtb_config, argp, sizeof(dtb_config)))
			return -EFAULT;

		dtb_config.size = PAGE_ALIGN(dtb_config.size);
		if (dtb_config.guest_phys_addr + dtb_config.size < dtb_config.guest_phys_addr)
			return -EOVERFLOW;

		ghvm->dtb_config = dtb_config;

		r = 0;
		break;
	}
	case GH_VM_START: {
		r = gh_vm_ensure_started(ghvm);
		break;
	}
	default:
		r = -ENOTTY;
		break;
	}

	return r;
}

static int gh_vm_release(struct inode *inode, struct file *filp)
{
	struct gh_vm *ghvm = filp->private_data;

	/* VM will be reset and make RM calls which can interruptible sleep.
	 * Defer to a work so this thread can receive signal.
	 */
	schedule_work(&ghvm->free_work);
	return 0;
}

static const struct file_operations gh_vm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = gh_vm_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.release = gh_vm_release,
	.llseek = noop_llseek,
};

static long gh_dev_ioctl_create_vm(struct gh_rm *rm, unsigned long arg)
{
	struct gh_vm *ghvm;
	struct file *file;
	int fd, err;

	/* arg reserved for future use. */
	if (arg)
		return -EINVAL;

	ghvm = gh_vm_alloc(rm);
	if (IS_ERR(ghvm))
		return PTR_ERR(ghvm);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		err = fd;
		goto err_destroy_vm;
	}

	file = anon_inode_getfile("gunyah-vm", &gh_vm_fops, ghvm, O_RDWR);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto err_put_fd;
	}

	fd_install(fd, file);

	return fd;

err_put_fd:
	put_unused_fd(fd);
err_destroy_vm:
	gh_vm_free(&ghvm->free_work);
	return err;
}

long gh_dev_vm_mgr_ioctl(struct gh_rm *rm, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case GH_CREATE_VM:
		return gh_dev_ioctl_create_vm(rm, arg);
	default:
		return -ENOTTY;
	}
}
