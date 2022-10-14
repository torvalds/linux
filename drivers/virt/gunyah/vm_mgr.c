// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "gh_vm_mgr: " fmt

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/gunyah_rsc_mgr.h>
#include <linux/miscdevice.h>
#include <linux/module.h>

#include <uapi/linux/gunyah.h>

#include "vm_mgr.h"

static void gh_vm_free(struct work_struct *work)
{
	struct gh_vm *ghvm = container_of(work, struct gh_vm, free_work);
	int ret;

	ret = gh_rm_dealloc_vmid(ghvm->rm, ghvm->vmid);
	if (ret)
		pr_warn("Failed to deallocate vmid: %d\n", ret);

	put_gh_rm(ghvm->rm);
	kfree(ghvm);
}

static __must_check struct gh_vm *gh_vm_alloc(struct gh_rm *rm)
{
	struct gh_vm *ghvm;
	int vmid;

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

	INIT_WORK(&ghvm->free_work, gh_vm_free);

	return ghvm;
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
