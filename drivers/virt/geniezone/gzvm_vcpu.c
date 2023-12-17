// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <asm/sysreg.h>
#include <linux/anon_inodes.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gzvm_drv.h>

/* maximum size needed for holding an integer */
#define ITOA_MAX_LEN 12

static long gzvm_vcpu_update_one_reg(struct gzvm_vcpu *vcpu,
				     void __user *argp,
				     bool is_write)
{
	struct gzvm_one_reg reg;
	void __user *reg_addr;
	u64 data = 0;
	u64 reg_size;
	long ret;

	if (copy_from_user(&reg, argp, sizeof(reg)))
		return -EFAULT;

	reg_addr = (void __user *)reg.addr;
	reg_size = (reg.id & GZVM_REG_SIZE_MASK) >> GZVM_REG_SIZE_SHIFT;
	reg_size = BIT(reg_size);

	if (reg_size != 1 && reg_size != 2 && reg_size != 4 && reg_size != 8)
		return -EINVAL;

	if (is_write) {
		/* GZ hypervisor would filter out invalid vcpu register access */
		if (copy_from_user(&data, reg_addr, reg_size))
			return -EFAULT;
	} else {
		return -EOPNOTSUPP;
	}

	ret = gzvm_arch_vcpu_update_one_reg(vcpu, reg.id, is_write, &data);

	if (ret)
		return ret;

	return 0;
}

/**
 * gzvm_vcpu_handle_mmio() - Handle mmio in kernel space.
 * @vcpu: Pointer to vcpu.
 *
 * Return:
 * * true - This mmio exit has been processed.
 * * false - This mmio exit has not been processed, require userspace.
 */
static bool gzvm_vcpu_handle_mmio(struct gzvm_vcpu *vcpu)
{
	__u64 addr;
	__u32 len;
	const void *val_ptr;

	/* So far, we don't have in-kernel mmio read handler */
	if (!vcpu->run->mmio.is_write)
		return false;
	addr = vcpu->run->mmio.phys_addr;
	len = vcpu->run->mmio.size;
	val_ptr = &vcpu->run->mmio.data;

	return gzvm_ioevent_write(vcpu, addr, len, val_ptr);
}

/**
 * gzvm_vcpu_run() - Handle vcpu run ioctl, entry point to guest and exit
 *		     point from guest
 * @vcpu: Pointer to struct gzvm_vcpu
 * @argp: Pointer to struct gzvm_vcpu_run in userspace
 *
 * Return:
 * * 0			- Success.
 * * Negative		- Failure.
 */
static long gzvm_vcpu_run(struct gzvm_vcpu *vcpu, void __user *argp)
{
	bool need_userspace = false;
	u64 exit_reason = 0;

	if (copy_from_user(vcpu->run, argp, sizeof(struct gzvm_vcpu_run)))
		return -EFAULT;

	for (int i = 0; i < ARRAY_SIZE(vcpu->run->padding1); i++) {
		if (vcpu->run->padding1[i])
			return -EINVAL;
	}

	if (vcpu->run->immediate_exit == 1)
		return -EINTR;

	while (!need_userspace && !signal_pending(current)) {
		gzvm_arch_vcpu_run(vcpu, &exit_reason);

		switch (exit_reason) {
		case GZVM_EXIT_MMIO:
			if (!gzvm_vcpu_handle_mmio(vcpu))
				need_userspace = true;
			break;
		/**
		 * it's geniezone's responsibility to fill corresponding data
		 * structure
		 */
		case GZVM_EXIT_HYPERCALL:
			if (!gzvm_handle_guest_hvc(vcpu))
				need_userspace = true;
			break;
		case GZVM_EXIT_EXCEPTION:
			if (!gzvm_handle_guest_exception(vcpu))
				need_userspace = true;
			break;
		case GZVM_EXIT_DEBUG:
			fallthrough;
		case GZVM_EXIT_FAIL_ENTRY:
			fallthrough;
		case GZVM_EXIT_INTERNAL_ERROR:
			fallthrough;
		case GZVM_EXIT_SYSTEM_EVENT:
			fallthrough;
		case GZVM_EXIT_SHUTDOWN:
			need_userspace = true;
			break;
		case GZVM_EXIT_IRQ:
			fallthrough;
		case GZVM_EXIT_GZ:
			break;
		case GZVM_EXIT_UNKNOWN:
			fallthrough;
		default:
			pr_err("vcpu unknown exit\n");
			need_userspace = true;
			goto out;
		}
	}

out:
	if (copy_to_user(argp, vcpu->run, sizeof(struct gzvm_vcpu_run)))
		return -EFAULT;
	if (signal_pending(current)) {
		// invoke hvc to inform gz to map memory
		gzvm_arch_inform_exit(vcpu->gzvm->vm_id);
		return -ERESTARTSYS;
	}
	return 0;
}

static long gzvm_vcpu_ioctl(struct file *filp, unsigned int ioctl,
			    unsigned long arg)
{
	int ret = -ENOTTY;
	void __user *argp = (void __user *)arg;
	struct gzvm_vcpu *vcpu = filp->private_data;

	switch (ioctl) {
	case GZVM_RUN:
		ret = gzvm_vcpu_run(vcpu, argp);
		break;
	case GZVM_GET_ONE_REG:
		/* !is_write */
		ret = -EOPNOTSUPP;
		break;
	case GZVM_SET_ONE_REG:
		/* is_write */
		ret = gzvm_vcpu_update_one_reg(vcpu, argp, true);
		break;
	default:
		break;
	}

	return ret;
}

static const struct file_operations gzvm_vcpu_fops = {
	.unlocked_ioctl = gzvm_vcpu_ioctl,
	.llseek		= noop_llseek,
};

/* caller must hold the vm lock */
static void gzvm_destroy_vcpu(struct gzvm_vcpu *vcpu)
{
	if (!vcpu)
		return;

	gzvm_arch_destroy_vcpu(vcpu->gzvm->vm_id, vcpu->vcpuid);
	/* clean guest's data */
	memset(vcpu->run, 0, GZVM_VCPU_RUN_MAP_SIZE);
	free_pages_exact(vcpu->run, GZVM_VCPU_RUN_MAP_SIZE);
	kfree(vcpu);
}

/**
 * gzvm_destroy_vcpus() - Destroy all vcpus, caller has to hold the vm lock
 *
 * @gzvm: vm struct that owns the vcpus
 */
void gzvm_destroy_vcpus(struct gzvm *gzvm)
{
	int i;

	for (i = 0; i < GZVM_MAX_VCPUS; i++) {
		gzvm_destroy_vcpu(gzvm->vcpus[i]);
		gzvm->vcpus[i] = NULL;
	}
}

/* create_vcpu_fd() - Allocates an inode for the vcpu. */
static int create_vcpu_fd(struct gzvm_vcpu *vcpu)
{
	/* sizeof("gzvm-vcpu:") + max(strlen(itoa(vcpuid))) + null */
	char name[10 + ITOA_MAX_LEN + 1];

	snprintf(name, sizeof(name), "gzvm-vcpu:%d", vcpu->vcpuid);
	return anon_inode_getfd(name, &gzvm_vcpu_fops, vcpu, O_RDWR | O_CLOEXEC);
}

/**
 * gzvm_vm_ioctl_create_vcpu() - for GZVM_CREATE_VCPU
 * @gzvm: Pointer to struct gzvm
 * @cpuid: equals arg
 *
 * Return: Fd of vcpu, negative errno if error occurs
 */
int gzvm_vm_ioctl_create_vcpu(struct gzvm *gzvm, u32 cpuid)
{
	struct gzvm_vcpu *vcpu;
	int ret;

	if (cpuid >= GZVM_MAX_VCPUS)
		return -EINVAL;

	vcpu = kzalloc(sizeof(*vcpu), GFP_KERNEL);
	if (!vcpu)
		return -ENOMEM;

	/**
	 * Allocate 2 pages for data sharing between driver and gz hypervisor
	 *
	 * |- page 0           -|- page 1      -|
	 * |gzvm_vcpu_run|......|hwstate|.......|
	 *
	 */
	vcpu->run = alloc_pages_exact(GZVM_VCPU_RUN_MAP_SIZE,
				      GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!vcpu->run) {
		ret = -ENOMEM;
		goto free_vcpu;
	}
	vcpu->hwstate = (void *)vcpu->run + PAGE_SIZE;
	vcpu->vcpuid = cpuid;
	vcpu->gzvm = gzvm;
	mutex_init(&vcpu->lock);

	ret = gzvm_arch_create_vcpu(gzvm->vm_id, vcpu->vcpuid, vcpu->run);
	if (ret < 0)
		goto free_vcpu_run;

	ret = create_vcpu_fd(vcpu);
	if (ret < 0)
		goto free_vcpu_run;
	gzvm->vcpus[cpuid] = vcpu;

	return ret;

free_vcpu_run:
	free_pages_exact(vcpu->run, GZVM_VCPU_RUN_MAP_SIZE);
free_vcpu:
	kfree(vcpu);
	return ret;
}
