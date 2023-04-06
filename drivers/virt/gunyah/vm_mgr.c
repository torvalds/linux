// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "gh_vm_mgr: " fmt

#include <linux/anon_inodes.h>
#include <linux/compat.h>
#include <linux/file.h>
#include <linux/gunyah_rsc_mgr.h>
#include <linux/gunyah_vm_mgr.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/xarray.h>

#include <uapi/linux/gunyah.h>

#include "vm_mgr.h"

static DEFINE_XARRAY(functions);

int gh_vm_function_register(struct gh_vm_function *fn)
{
	if (!fn->bind || !fn->unbind)
		return -EINVAL;

	return xa_err(xa_store(&functions, fn->type, fn, GFP_KERNEL));
}
EXPORT_SYMBOL_GPL(gh_vm_function_register);

static void gh_vm_remove_function_instance(struct gh_vm_function_instance *inst)
	__must_hold(&inst->ghvm->fn_lock)
{
	inst->fn->unbind(inst);
	list_del(&inst->vm_list);
	module_put(inst->fn->mod);
	kfree(inst->argp);
	kfree(inst);
}

void gh_vm_function_unregister(struct gh_vm_function *fn)
{
	/* Expecting unregister to only come when unloading a module */
	WARN_ON(fn->mod && module_refcount(fn->mod));
	xa_erase(&functions, fn->type);
}
EXPORT_SYMBOL_GPL(gh_vm_function_unregister);

static struct gh_vm_function *gh_vm_get_function(u32 type)
{
	struct gh_vm_function *fn;
	int r;

	fn = xa_load(&functions, type);
	if (!fn) {
		r = request_module("ghfunc:%d", type);
		if (r)
			return ERR_PTR(r);

		fn = xa_load(&functions, type);
	}

	if (!fn || !try_module_get(fn->mod))
		fn = ERR_PTR(-ENOENT);

	return fn;
}

static long gh_vm_add_function(struct gh_vm *ghvm, struct gh_fn_desc *f)
{
	struct gh_vm_function_instance *inst;
	void __user *argp;
	long r = 0;

	if (f->arg_size > GH_FN_MAX_ARG_SIZE) {
		dev_err(ghvm->parent, "%s: arg_size > %d\n", __func__, GH_FN_MAX_ARG_SIZE);
		return -EINVAL;
	}

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->arg_size = f->arg_size;
	if (inst->arg_size) {
		inst->argp = kzalloc(inst->arg_size, GFP_KERNEL);
		if (!inst->argp) {
			r = -ENOMEM;
			goto free;
		}

		argp = u64_to_user_ptr(f->arg);
		if (copy_from_user(inst->argp, argp, f->arg_size)) {
			r = -EFAULT;
			goto free_arg;
		}
	}

	inst->fn = gh_vm_get_function(f->type);
	if (IS_ERR(inst->fn)) {
		r = PTR_ERR(inst->fn);
		goto free_arg;
	}

	inst->ghvm = ghvm;
	inst->rm = ghvm->rm;

	mutex_lock(&ghvm->fn_lock);
	r = inst->fn->bind(inst);
	if (r < 0) {
		module_put(inst->fn->mod);
		goto free_arg;
	}

	list_add(&inst->vm_list, &ghvm->functions);
	mutex_unlock(&ghvm->fn_lock);

	return r;
free_arg:
	kfree(inst->argp);
free:
	kfree(inst);
	return r;
}

static long gh_vm_rm_function(struct gh_vm *ghvm, struct gh_fn_desc *f)
{
	struct gh_vm_function_instance *inst, *iter;
	void __user *user_argp;
	void *argp;
	long r = 0;

	r = mutex_lock_interruptible(&ghvm->fn_lock);
	if (r)
		return r;

	if (f->arg_size) {
		argp = kzalloc(f->arg_size, GFP_KERNEL);
		if (!argp) {
			r = -ENOMEM;
			goto out;
		}

		user_argp = u64_to_user_ptr(f->arg);
		if (copy_from_user(argp, user_argp, f->arg_size)) {
			r = -EFAULT;
			kfree(argp);
			goto out;
		}

		list_for_each_entry_safe(inst, iter, &ghvm->functions, vm_list) {
			if (inst->fn->type == f->type &&
			    f->arg_size == inst->arg_size &&
			    !memcmp(argp, inst->argp, f->arg_size))
				gh_vm_remove_function_instance(inst);
		}

		kfree(argp);
	}

out:
	mutex_unlock(&ghvm->fn_lock);
	return r;
}

int gh_vm_add_resource_ticket(struct gh_vm *ghvm, struct gh_vm_resource_ticket *ticket)
{
	struct gh_vm_resource_ticket *iter;
	struct gh_resource *ghrsc;
	int ret = 0;

	mutex_lock(&ghvm->resources_lock);
	list_for_each_entry(iter, &ghvm->resource_tickets, list) {
		if (iter->resource_type == ticket->resource_type && iter->label == ticket->label) {
			ret = -EEXIST;
			goto out;
		}
	}

	if (!try_module_get(ticket->owner)) {
		ret = -ENODEV;
		goto out;
	}

	list_add(&ticket->list, &ghvm->resource_tickets);
	INIT_LIST_HEAD(&ticket->resources);

	list_for_each_entry(ghrsc, &ghvm->resources, list) {
		if (ghrsc->type == ticket->resource_type && ghrsc->rm_label == ticket->label) {
			if (!ticket->populate(ticket, ghrsc))
				list_move(&ghrsc->list, &ticket->resources);
		}
	}
out:
	mutex_unlock(&ghvm->resources_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(gh_vm_add_resource_ticket);

void gh_vm_remove_resource_ticket(struct gh_vm *ghvm, struct gh_vm_resource_ticket *ticket)
{
	struct gh_resource *ghrsc, *iter;

	mutex_lock(&ghvm->resources_lock);
	list_for_each_entry_safe(ghrsc, iter, &ticket->resources, list) {
		ticket->unpopulate(ticket, ghrsc);
		list_move(&ghrsc->list, &ghvm->resources);
	}

	module_put(ticket->owner);
	list_del(&ticket->list);
	mutex_unlock(&ghvm->resources_lock);
}
EXPORT_SYMBOL_GPL(gh_vm_remove_resource_ticket);

static void gh_vm_add_resource(struct gh_vm *ghvm, struct gh_resource *ghrsc)
{
	struct gh_vm_resource_ticket *ticket;

	mutex_lock(&ghvm->resources_lock);
	list_for_each_entry(ticket, &ghvm->resource_tickets, list) {
		if (ghrsc->type == ticket->resource_type && ghrsc->rm_label == ticket->label) {
			if (!ticket->populate(ticket, ghrsc)) {
				list_add(&ghrsc->list, &ticket->resources);
				goto found;
			}
		}
	}
	list_add(&ghrsc->list, &ghvm->resources);
found:
	mutex_unlock(&ghvm->resources_lock);
}

static int _gh_vm_io_handler_compare(const struct rb_node *node, const struct rb_node *parent)
{
	struct gh_vm_io_handler *n = container_of(node, struct gh_vm_io_handler, node);
	struct gh_vm_io_handler *p = container_of(parent, struct gh_vm_io_handler, node);

	if (n->addr < p->addr)
		return -1;
	if (n->addr > p->addr)
		return 1;
	if ((n->len && !p->len) || (!n->len && p->len))
		return 0;
	if (n->len < p->len)
		return -1;
	if (n->len > p->len)
		return 1;
	if (n->datamatch < p->datamatch)
		return -1;
	if (n->datamatch > p->datamatch)
		return 1;
	return 0;
}

static int gh_vm_io_handler_compare(struct rb_node *node, const struct rb_node *parent)
{
	return _gh_vm_io_handler_compare(node, parent);
}

static int gh_vm_io_handler_find(const void *key, const struct rb_node *node)
{
	const struct gh_vm_io_handler *k = key;

	return _gh_vm_io_handler_compare(&k->node, node);
}

static struct gh_vm_io_handler *gh_vm_mgr_find_io_hdlr(struct gh_vm *ghvm, u64 addr,
								u64 len, u64 data)
{
	struct gh_vm_io_handler key = {
		.addr = addr,
		.len = len,
		.datamatch = data,
	};
	struct rb_node *node;

	node = rb_find(&key, &ghvm->mmio_handler_root, gh_vm_io_handler_find);
	if (!node)
		return NULL;

	return container_of(node, struct gh_vm_io_handler, node);
}

int gh_vm_mmio_write(struct gh_vm *ghvm, u64 addr, u32 len, u64 data)
{
	struct gh_vm_io_handler *io_hdlr = NULL;
	int ret;

	down_read(&ghvm->mmio_handler_lock);
	io_hdlr = gh_vm_mgr_find_io_hdlr(ghvm, addr, len, data);
	if (!io_hdlr || !io_hdlr->ops || !io_hdlr->ops->write) {
		ret = -ENODEV;
		goto out;
	}

	ret = io_hdlr->ops->write(io_hdlr, addr, len, data);

out:
	up_read(&ghvm->mmio_handler_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(gh_vm_mmio_write);

int gh_vm_add_io_handler(struct gh_vm *ghvm, struct gh_vm_io_handler *io_hdlr)
{
	struct rb_node *found;

	if (io_hdlr->datamatch && (!io_hdlr->len || io_hdlr->len > sizeof(io_hdlr->data)))
		return -EINVAL;

	down_write(&ghvm->mmio_handler_lock);
	found = rb_find_add(&io_hdlr->node, &ghvm->mmio_handler_root, gh_vm_io_handler_compare);
	up_write(&ghvm->mmio_handler_lock);

	return found ? -EEXIST : 0;
}
EXPORT_SYMBOL_GPL(gh_vm_add_io_handler);

void gh_vm_remove_io_handler(struct gh_vm *ghvm, struct gh_vm_io_handler *io_hdlr)
{
	down_write(&ghvm->mmio_handler_lock);
	rb_erase(&io_hdlr->node, &ghvm->mmio_handler_root);
	up_write(&ghvm->mmio_handler_lock);
}
EXPORT_SYMBOL_GPL(gh_vm_remove_io_handler);

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
	ghvm->exit_info.type = le16_to_cpu(payload->exit_type);
	ghvm->exit_info.reason_size = le32_to_cpu(payload->exit_reason_size);
	memcpy(&ghvm->exit_info.reason, payload->exit_reason,
		min(GH_VM_MAX_EXIT_REASON_SIZE, ghvm->exit_info.reason_size));
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
	struct gh_vm_function_instance *inst, *iiter;
	struct gh_vm_resource_ticket *ticket, *titer;
	struct gh_resource *ghrsc, *riter;
	struct gh_vm_mem *mapping, *tmp;
	int ret;

	switch (ghvm->vm_status) {
	case GH_RM_VM_STATUS_RUNNING:
		gh_vm_stop(ghvm);
		fallthrough;
	case GH_RM_VM_STATUS_INIT_FAILED:
	case GH_RM_VM_STATUS_EXITED:
		mutex_lock(&ghvm->fn_lock);
		list_for_each_entry_safe(inst, iiter, &ghvm->functions, vm_list) {
			gh_vm_remove_function_instance(inst);
		}
		mutex_unlock(&ghvm->fn_lock);

		mutex_lock(&ghvm->resources_lock);
		if (!list_empty(&ghvm->resource_tickets)) {
			dev_warn(ghvm->parent, "Dangling resource tickets:\n");
			list_for_each_entry_safe(ticket, titer, &ghvm->resource_tickets, list) {
				dev_warn(ghvm->parent, "  %pS\n", ticket->populate);
				gh_vm_remove_resource_ticket(ghvm, ticket);
			}
		}

		list_for_each_entry_safe(ghrsc, riter, &ghvm->resources, list) {
			gh_rm_free_resource(ghrsc);
		}
		mutex_unlock(&ghvm->resources_lock);

		/* vm_status == LOAD if user creates VM, but then destroys it
		 * without ever trying to start it. In that case, we have only
		 * allocated VMID. Clean up functions (above), memory (below),
		 * and dealloc vmid (below), but no call gh_rm_vm_reset().
		 */
		if (ghvm->vm_status != GH_RM_VM_STATUS_LOAD) {
			ret = gh_rm_vm_reset(ghvm->rm, ghvm->vmid);
			if (ret)
				dev_err(ghvm->parent, "Failed to reset the vm: %d\n", ret);
			wait_event(ghvm->vm_status_wait, ghvm->vm_status == GH_RM_VM_STATUS_RESET);
		}

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

static void _gh_vm_put(struct kref *kref)
{
	struct gh_vm *ghvm = container_of(kref, struct gh_vm, kref);

	/* VM will be reset and make RM calls which can interruptible sleep.
	 * Defer to a work so this thread can receive signal.
	 */
	schedule_work(&ghvm->free_work);
}

int __must_check gh_vm_get(struct gh_vm *ghvm)
{
	return kref_get_unless_zero(&ghvm->kref);
}
EXPORT_SYMBOL_GPL(gh_vm_get);

void gh_vm_put(struct gh_vm *ghvm)
{
	kref_put(&ghvm->kref, _gh_vm_put);
}
EXPORT_SYMBOL_GPL(gh_vm_put);

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
	kref_init(&ghvm->kref);
	mutex_init(&ghvm->resources_lock);
	INIT_LIST_HEAD(&ghvm->resources);
	INIT_LIST_HEAD(&ghvm->resource_tickets);
	INIT_LIST_HEAD(&ghvm->functions);
	ghvm->vm_status = GH_RM_VM_STATUS_LOAD;

	return ghvm;
}

static int gh_vm_start(struct gh_vm *ghvm)
{
	struct gh_vm_mem *mapping;
	struct gh_rm_hyp_resources *resources;
	struct gh_resource *ghrsc;
	u64 dtb_offset;
	u32 mem_handle;
	int ret, i, n;

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

	if (ghvm->auth == GH_RM_VM_AUTH_QCOM_ANDROID_PVM) {
		mapping = gh_vm_mem_find_by_addr(ghvm, ghvm->fw_config.guest_phys_addr,
						ghvm->fw_config.size);
		if (!mapping) {
			pr_warn("Failed to find the memory handle for pVM firmware\n");
			ret = -EINVAL;
			goto err;
		}
		ret = gh_rm_vm_set_firmware_mem(ghvm->rm, ghvm->vmid, &mapping->parcel,
				ghvm->fw_config.guest_phys_addr - mapping->guest_phys_addr,
				ghvm->fw_config.size);
		if (ret) {
			pr_warn("Failed to configure pVM firmware\n");
			goto err;
		}
	}

	ret = gh_rm_vm_init(ghvm->rm, ghvm->vmid);
	ghvm->vm_status = GH_RM_VM_STATUS_RESET;
	if (ret) {
		dev_warn(ghvm->parent, "Failed to initialize VM: %d\n", ret);
		goto err;
	}

	ret = gh_rm_get_hyp_resources(ghvm->rm, ghvm->vmid, &resources);
	if (ret) {
		dev_warn(ghvm->parent, "Failed to get hypervisor resources for VM: %d\n", ret);
		goto err;
	}

	for (i = 0, n = le32_to_cpu(resources->n_entries); i < n; i++) {
		ghrsc = gh_rm_alloc_resource(ghvm->rm, &resources->entries[i]);
		if (!ghrsc) {
			ret = -ENOMEM;
			goto err;
		}

		gh_vm_add_resource(ghvm, ghrsc);
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
	bool lend = false;

	switch (cmd) {
	case GH_VM_ANDROID_LEND_USER_MEM:
		lend = true;
		fallthrough;
	case GH_VM_SET_USER_MEM_REGION: {
		struct gh_userspace_memory_region region;

		if (copy_from_user(&region, argp, sizeof(region)))
			return -EFAULT;

		/* All other flag bits are reserved for future use */
		if (region.flags & ~(GH_MEM_ALLOW_READ | GH_MEM_ALLOW_WRITE | GH_MEM_ALLOW_EXEC))
			return -EINVAL;

		r = gh_vm_mem_alloc(ghvm, &region, lend);
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
	case GH_VM_ANDROID_SET_FW_CONFIG: {
		r = -EFAULT;
		if (copy_from_user(&ghvm->fw_config, argp, sizeof(ghvm->fw_config)))
			break;

		ghvm->auth = GH_RM_VM_AUTH_QCOM_ANDROID_PVM;
		r = 0;
		break;
	}
	case GH_VM_START: {
		r = gh_vm_ensure_started(ghvm);
		break;
	}
	case GH_VM_ADD_FUNCTION: {
		struct gh_fn_desc f;

		if (copy_from_user(&f, argp, sizeof(f)))
			return -EFAULT;

		r = gh_vm_add_function(ghvm, &f);
		break;
	}
	case GH_VM_REMOVE_FUNCTION: {
		struct gh_fn_desc *f;

		f = kzalloc(sizeof(*f), GFP_KERNEL);
		if (!f)
			return -ENOMEM;

		if (copy_from_user(f, argp, sizeof(*f)))
			return -EFAULT;

		r = gh_vm_rm_function(ghvm, f);
		kfree(f);
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

	gh_vm_put(ghvm);
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
