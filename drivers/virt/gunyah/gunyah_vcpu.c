// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/gunyah.h>
#include <linux/gunyah_vm_mgr.h>
#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "vm_mgr.h"

#include <uapi/linux/gunyah.h>

#define MAX_VCPU_NAME		20 /* gh-vcpu:u32_max+NUL */

struct gh_vcpu {
	struct gh_vm_function_instance *f;
	struct gh_resource *rsc;
	struct mutex run_lock;
	/* Track why vcpu_run left last time around. */
	enum {
		GH_VCPU_UNKNOWN = 0,
		GH_VCPU_READY,
		GH_VCPU_MMIO_READ,
		GH_VCPU_SYSTEM_DOWN,
	} state;
	u8 mmio_read_len;
	struct gh_vcpu_run *vcpu_run;
	struct completion ready;
	struct gh_vm *ghvm;

	struct notifier_block nb;
	struct gh_vm_resource_ticket ticket;
	struct kref kref;
};

static void vcpu_release(struct kref *kref)
{
	struct gh_vcpu *vcpu = container_of(kref, struct gh_vcpu, kref);

	free_page((unsigned long)vcpu->vcpu_run);
	kfree(vcpu);
}

/*
 * When hypervisor allows us to schedule vCPU again, it gives us an interrupt
 */
static irqreturn_t gh_vcpu_irq_handler(int irq, void *data)
{
	struct gh_vcpu *vcpu = data;

	complete(&vcpu->ready);
	return IRQ_HANDLED;
}

static bool gh_handle_mmio(struct gh_vcpu *vcpu,
				struct gh_hypercall_vcpu_run_resp *vcpu_run_resp)
{
	int ret = 0;
	u64 addr = vcpu_run_resp->state_data[0],
	    len  = vcpu_run_resp->state_data[1],
	    data = vcpu_run_resp->state_data[2];

	if (WARN_ON(len > sizeof(u64)))
		len = sizeof(u64);

	if (vcpu_run_resp->state == GH_VCPU_ADDRSPACE_VMMIO_READ) {
		vcpu->vcpu_run->mmio.is_write = 0;
		/* Record that we need to give vCPU user's supplied value next gh_vcpu_run() */
		vcpu->state = GH_VCPU_MMIO_READ;
		vcpu->mmio_read_len = len;
	} else { /* GH_VCPU_ADDRSPACE_VMMIO_WRITE */
		/* Try internal handlers first */
		ret = gh_vm_mmio_write(vcpu->f->ghvm, addr, len, data);
		if (!ret)
			return true;

		/* Give userspace the info */
		vcpu->vcpu_run->mmio.is_write = 1;
		memcpy(vcpu->vcpu_run->mmio.data, &data, len);
	}

	vcpu->vcpu_run->mmio.phys_addr = addr;
	vcpu->vcpu_run->mmio.len = len;
	vcpu->vcpu_run->exit_reason = GH_VCPU_EXIT_MMIO;

	return false;
}

static int gh_vcpu_rm_notification(struct notifier_block *nb, unsigned long action, void *data)
{
	struct gh_vcpu *vcpu = container_of(nb, struct gh_vcpu, nb);
	struct gh_rm_vm_exited_payload *exit_payload = data;

	if (action == GH_RM_NOTIFICATION_VM_EXITED &&
		le16_to_cpu(exit_payload->vmid) == vcpu->ghvm->vmid)
		complete(&vcpu->ready);

	return NOTIFY_OK;
}

static inline enum gh_vm_status remap_vm_status(enum gh_rm_vm_status rm_status)
{
	switch (rm_status) {
	case GH_RM_VM_STATUS_INIT_FAILED:
		return GH_VM_STATUS_LOAD_FAILED;
	case GH_RM_VM_STATUS_EXITED:
		return GH_VM_STATUS_EXITED;
	default:
		return GH_VM_STATUS_CRASHED;
	}
}

/**
 * gh_vcpu_check_system() - Check whether VM as a whole is running
 * @vcpu: Pointer to gh_vcpu
 *
 * Returns true if the VM is alive.
 * Returns false if the vCPU is the VM is not alive (can only be that VM is shutting down).
 */
static bool gh_vcpu_check_system(struct gh_vcpu *vcpu)
	__must_hold(&vcpu->run_lock)
{
	bool ret = true;

	down_read(&vcpu->ghvm->status_lock);
	if (likely(vcpu->ghvm->vm_status == GH_RM_VM_STATUS_RUNNING))
		goto out;

	vcpu->vcpu_run->status.status = remap_vm_status(vcpu->ghvm->vm_status);
	vcpu->vcpu_run->status.exit_info = vcpu->ghvm->exit_info;
	vcpu->vcpu_run->exit_reason = GH_VCPU_EXIT_STATUS;
	vcpu->state = GH_VCPU_SYSTEM_DOWN;
	ret = false;
out:
	up_read(&vcpu->ghvm->status_lock);
	return ret;
}

/**
 * gh_vcpu_run() - Request Gunyah to begin scheduling this vCPU.
 * @vcpu: The client descriptor that was obtained via gh_vcpu_alloc()
 */
static int gh_vcpu_run(struct gh_vcpu *vcpu)
{
	struct gh_hypercall_vcpu_run_resp vcpu_run_resp;
	u64 state_data[3] = { 0 };
	enum gh_error gh_error;
	int ret = 0;

	if (!vcpu->f)
		return -ENODEV;

	if (mutex_lock_interruptible(&vcpu->run_lock))
		return -ERESTARTSYS;

	if (!vcpu->rsc) {
		ret = -ENODEV;
		goto out;
	}

	switch (vcpu->state) {
	case GH_VCPU_UNKNOWN:
		if (vcpu->ghvm->vm_status != GH_RM_VM_STATUS_RUNNING) {
			/* Check if VM is up. If VM is starting, will block until VM is fully up
			 * since that thread does down_write.
			 */
			if (!gh_vcpu_check_system(vcpu))
				goto out;
		}
		vcpu->state = GH_VCPU_READY;
		break;
	case GH_VCPU_MMIO_READ:
		if (unlikely(vcpu->mmio_read_len > sizeof(state_data[0])))
			vcpu->mmio_read_len = sizeof(state_data[0]);
		memcpy(&state_data[0], vcpu->vcpu_run->mmio.data, vcpu->mmio_read_len);
		vcpu->state = GH_VCPU_READY;
		break;
	case GH_VCPU_SYSTEM_DOWN:
		goto out;
	default:
		break;
	}

	while (!ret && !signal_pending(current)) {
		if (vcpu->vcpu_run->immediate_exit) {
			ret = -EINTR;
			goto out;
		}

		gh_error = gh_hypercall_vcpu_run(vcpu->rsc->capid, state_data, &vcpu_run_resp);
		if (gh_error == GH_ERROR_OK) {
			switch (vcpu_run_resp.state) {
			case GH_VCPU_STATE_READY:
				if (need_resched())
					schedule();
				break;
			case GH_VCPU_STATE_POWERED_OFF:
				/* vcpu might be off because the VM is shut down.
				 * If so, it won't ever run again: exit back to user
				 */
				if (!gh_vcpu_check_system(vcpu))
					goto out;
				/* Otherwise, another vcpu will turn it on (e.g. by PSCI)
				 * and hyp sends an interrupt to wake Linux up.
				 */
				fallthrough;
			case GH_VCPU_STATE_EXPECTS_WAKEUP:
				ret = wait_for_completion_interruptible(&vcpu->ready);
				/* reinitialize completion before next hypercall. If we reinitialize
				 * after the hypercall, interrupt may have already come before
				 * re-initializing the completion and then end up waiting for
				 * event that already happened.
				 */
				reinit_completion(&vcpu->ready);
				/* Check system status again. Completion might've
				 * come from gh_vcpu_rm_notification
				 */
				if (!ret && !gh_vcpu_check_system(vcpu))
					goto out;
				break;
			case GH_VCPU_STATE_BLOCKED:
				schedule();
				break;
			case GH_VCPU_ADDRSPACE_VMMIO_READ:
			case GH_VCPU_ADDRSPACE_VMMIO_WRITE:
				if (!gh_handle_mmio(vcpu, &vcpu_run_resp))
					goto out;
				break;
			default:
				pr_warn_ratelimited("Unknown vCPU state: %llx\n",
							vcpu_run_resp.sized_state);
				schedule();
				break;
			}
		} else if (gh_error == GH_ERROR_RETRY) {
			schedule();
		} else {
			ret = gh_error_remap(gh_error);
		}
	}

out:
	mutex_unlock(&vcpu->run_lock);

	if (signal_pending(current))
		return -ERESTARTSYS;

	return ret;
}

static long gh_vcpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gh_vcpu *vcpu = filp->private_data;
	long ret = -EINVAL;

	switch (cmd) {
	case GH_VCPU_RUN:
		ret = gh_vcpu_run(vcpu);
		break;
	case GH_VCPU_MMAP_SIZE:
		ret = PAGE_SIZE;
		break;
	default:
		break;
	}
	return ret;
}

static int gh_vcpu_release(struct inode *inode, struct file *filp)
{
	struct gh_vcpu *vcpu = filp->private_data;

	gh_vm_put(vcpu->ghvm);
	kref_put(&vcpu->kref, vcpu_release);
	return 0;
}

static vm_fault_t gh_vcpu_fault(struct vm_fault *vmf)
{
	struct gh_vcpu *vcpu = vmf->vma->vm_file->private_data;
	struct page *page = NULL;

	if (vmf->pgoff == 0)
		page = virt_to_page(vcpu->vcpu_run);

	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct gh_vcpu_ops = {
	.fault = gh_vcpu_fault,
};

static int gh_vcpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_ops = &gh_vcpu_ops;
	return 0;
}

static const struct file_operations gh_vcpu_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = gh_vcpu_ioctl,
	.release = gh_vcpu_release,
	.llseek = noop_llseek,
	.mmap = gh_vcpu_mmap,
};

static bool gh_vcpu_populate(struct gh_vm_resource_ticket *ticket, struct gh_resource *ghrsc)
{
	struct gh_vcpu *vcpu = container_of(ticket, struct gh_vcpu, ticket);
	int ret;

	mutex_lock(&vcpu->run_lock);
	if (vcpu->rsc) {
		pr_warn("vcpu%d already got a Gunyah resource. Check if multiple resources with same label were configured.\n",
			vcpu->ticket.label);
		ret = -EEXIST;
		goto out;
	}

	vcpu->rsc = ghrsc;
	init_completion(&vcpu->ready);

	ret = request_irq(vcpu->rsc->irq, gh_vcpu_irq_handler, IRQF_TRIGGER_RISING, "gh_vcpu",
			vcpu);
	if (ret)
		pr_warn("Failed to request vcpu irq %d: %d", vcpu->rsc->irq, ret);

out:
	mutex_unlock(&vcpu->run_lock);
	return !ret;
}

static void gh_vcpu_unpopulate(struct gh_vm_resource_ticket *ticket,
				   struct gh_resource *ghrsc)
{
	struct gh_vcpu *vcpu = container_of(ticket, struct gh_vcpu, ticket);

	vcpu->vcpu_run->immediate_exit = true;
	complete_all(&vcpu->ready);
	mutex_lock(&vcpu->run_lock);
	free_irq(vcpu->rsc->irq, vcpu);
	vcpu->rsc = NULL;
	mutex_unlock(&vcpu->run_lock);
}

static long gh_vcpu_bind(struct gh_vm_function_instance *f)
{
	struct gh_fn_vcpu_arg *arg = f->argp;
	struct gh_vcpu *vcpu;
	char name[MAX_VCPU_NAME];
	struct file *file;
	struct page *page;
	int fd;
	long r;

	if (f->arg_size != sizeof(*arg))
		return -EINVAL;

	vcpu = kzalloc(sizeof(*vcpu), GFP_KERNEL);
	if (!vcpu)
		return -ENOMEM;

	vcpu->f = f;
	f->data = vcpu;
	mutex_init(&vcpu->run_lock);
	kref_init(&vcpu->kref);

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page) {
		r = -ENOMEM;
		goto err_destroy_vcpu;
	}
	vcpu->vcpu_run = page_address(page);

	vcpu->ticket.resource_type = GH_RESOURCE_TYPE_VCPU;
	vcpu->ticket.label = arg->id;
	vcpu->ticket.owner = THIS_MODULE;
	vcpu->ticket.populate = gh_vcpu_populate;
	vcpu->ticket.unpopulate = gh_vcpu_unpopulate;

	r = gh_vm_add_resource_ticket(f->ghvm, &vcpu->ticket);
	if (r)
		goto err_destroy_page;

	if (!gh_vm_get(f->ghvm)) {
		r = -ENODEV;
		goto err_remove_resource_ticket;
	}
	vcpu->ghvm = f->ghvm;

	vcpu->nb.notifier_call = gh_vcpu_rm_notification;
	/* Ensure we run after the vm_mgr handles the notification and does
	 * any necessary state changes. We wake up to check the new state.
	 */
	vcpu->nb.priority = -1;
	r = gh_rm_notifier_register(f->rm, &vcpu->nb);
	if (r)
		goto err_put_gh_vm;

	kref_get(&vcpu->kref);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		r = fd;
		goto err_notifier;
	}

	snprintf(name, sizeof(name), "gh-vcpu:%u", vcpu->ticket.label);
	file = anon_inode_getfile(name, &gh_vcpu_fops, vcpu, O_RDWR);
	if (IS_ERR(file)) {
		r = PTR_ERR(file);
		goto err_put_fd;
	}

	fd_install(fd, file);

	return fd;
err_put_fd:
	put_unused_fd(fd);
err_notifier:
	gh_rm_notifier_unregister(f->rm, &vcpu->nb);
err_put_gh_vm:
	gh_vm_put(vcpu->ghvm);
err_remove_resource_ticket:
	gh_vm_remove_resource_ticket(f->ghvm, &vcpu->ticket);
err_destroy_page:
	free_page((unsigned long)vcpu->vcpu_run);
err_destroy_vcpu:
	kfree(vcpu);
	return r;
}

static void gh_vcpu_unbind(struct gh_vm_function_instance *f)
{
	struct gh_vcpu *vcpu = f->data;

	gh_rm_notifier_unregister(f->rm, &vcpu->nb);
	gh_vm_remove_resource_ticket(vcpu->f->ghvm, &vcpu->ticket);
	vcpu->f = NULL;

	kref_put(&vcpu->kref, vcpu_release);
}

static bool gh_vcpu_compare(const struct gh_vm_function_instance *f,
				const void *arg, size_t size)
{
	const struct gh_fn_vcpu_arg *instance = f->argp,
					 *other = arg;

	if (sizeof(*other) != size)
		return false;

	return instance->id == other->id;
}

DECLARE_GH_VM_FUNCTION_INIT(vcpu, GH_FN_VCPU, 1, gh_vcpu_bind, gh_vcpu_unbind, gh_vcpu_compare);
MODULE_DESCRIPTION("Gunyah vCPU Function");
MODULE_LICENSE("GPL");
