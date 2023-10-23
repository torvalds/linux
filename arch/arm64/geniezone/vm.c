// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <asm/sysreg.h>
#include <linux/arm-smccc.h>
#include <linux/err.h>
#include <linux/uaccess.h>

#include <linux/gzvm.h>
#include <linux/gzvm_drv.h>
#include "gzvm_arch_common.h"

#define PAR_PA47_MASK ((((1UL << 48) - 1) >> 12) << 12)

int gzvm_arch_inform_exit(u16 vm_id)
{
	struct arm_smccc_res res;

	arm_smccc_hvc(MT_HVC_GZVM_INFORM_EXIT, vm_id, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 == 0)
		return 0;

	return -ENXIO;
}

int gzvm_arch_probe(void)
{
	struct arm_smccc_res res;

	arm_smccc_hvc(MT_HVC_GZVM_PROBE, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 == 0)
		return 0;

	return -ENXIO;
}

int gzvm_arch_set_memregion(u16 vm_id, size_t buf_size,
			    phys_addr_t region)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_SET_MEMREGION, vm_id,
				    buf_size, region, 0, 0, 0, 0, &res);
}

static int gzvm_cap_arm_vm_ipa_size(void __user *argp)
{
	__u64 value = CONFIG_ARM64_PA_BITS;

	if (copy_to_user(argp, &value, sizeof(__u64)))
		return -EFAULT;

	return 0;
}

int gzvm_arch_check_extension(struct gzvm *gzvm, __u64 cap, void __user *argp)
{
	int ret = -EOPNOTSUPP;

	switch (cap) {
	case GZVM_CAP_ARM_PROTECTED_VM: {
		__u64 success = 1;

		if (copy_to_user(argp, &success, sizeof(__u64)))
			return -EFAULT;
		ret = 0;
		break;
	}
	case GZVM_CAP_ARM_VM_IPA_SIZE: {
		ret = gzvm_cap_arm_vm_ipa_size(argp);
		break;
	}
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/**
 * gzvm_arch_create_vm() - create vm
 * @vm_type: VM type. Only supports Linux VM now.
 *
 * Return:
 * * positive value	- VM ID
 * * -ENOMEM		- Memory not enough for storing VM data
 */
int gzvm_arch_create_vm(unsigned long vm_type)
{
	struct arm_smccc_res res;
	int ret;

	ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_VM, vm_type, 0, 0, 0, 0,
				   0, 0, &res);

	if (ret == 0)
		return res.a1;
	else
		return ret;
}

int gzvm_arch_destroy_vm(u16 vm_id)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_DESTROY_VM, vm_id, 0, 0, 0, 0,
				    0, 0, &res);
}

int gzvm_arch_memregion_purpose(struct gzvm *gzvm,
				struct gzvm_userspace_memory_region *mem)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_MEMREGION_PURPOSE, gzvm->vm_id,
				    mem->guest_phys_addr, mem->memory_size,
				    mem->flags, 0, 0, 0, &res);
}

int gzvm_arch_set_dtb_config(struct gzvm *gzvm, struct gzvm_dtb_config *cfg)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_SET_DTB_CONFIG, gzvm->vm_id,
				    cfg->dtb_addr, cfg->dtb_size, 0, 0, 0, 0,
				    &res);
}

static int gzvm_vm_arch_enable_cap(struct gzvm *gzvm,
				   struct gzvm_enable_cap *cap,
				   struct arm_smccc_res *res)
{
	return gzvm_hypcall_wrapper(MT_HVC_GZVM_ENABLE_CAP, gzvm->vm_id,
				    cap->cap, cap->args[0], cap->args[1],
				    cap->args[2], cap->args[3], cap->args[4],
				    res);
}

/**
 * gzvm_vm_ioctl_get_pvmfw_size() - Get pvmfw size from hypervisor, return
 *				    in x1, and return to userspace in args
 * @gzvm: Pointer to struct gzvm.
 * @cap: Pointer to struct gzvm_enable_cap.
 * @argp: Pointer to struct gzvm_enable_cap in user space.
 *
 * Return:
 * * 0			- Succeed
 * * -EINVAL		- Hypervisor return invalid results
 * * -EFAULT		- Fail to copy back to userspace buffer
 */
static int gzvm_vm_ioctl_get_pvmfw_size(struct gzvm *gzvm,
					struct gzvm_enable_cap *cap,
					void __user *argp)
{
	struct arm_smccc_res res = {0};

	if (gzvm_vm_arch_enable_cap(gzvm, cap, &res) != 0)
		return -EINVAL;

	cap->args[1] = res.a1;
	if (copy_to_user(argp, cap, sizeof(*cap)))
		return -EFAULT;

	return 0;
}

/**
 * gzvm_vm_ioctl_cap_pvm() - Proceed GZVM_CAP_ARM_PROTECTED_VM's subcommands
 * @gzvm: Pointer to struct gzvm.
 * @cap: Pointer to struct gzvm_enable_cap.
 * @argp: Pointer to struct gzvm_enable_cap in user space.
 *
 * Return:
 * * 0			- Succeed
 * * -EINVAL		- Invalid subcommand or arguments
 */
static int gzvm_vm_ioctl_cap_pvm(struct gzvm *gzvm,
				 struct gzvm_enable_cap *cap,
				 void __user *argp)
{
	int ret = -EINVAL;
	struct arm_smccc_res res = {0};

	switch (cap->args[0]) {
	case GZVM_CAP_ARM_PVM_SET_PVMFW_IPA:
		fallthrough;
	case GZVM_CAP_ARM_PVM_SET_PROTECTED_VM:
		ret = gzvm_vm_arch_enable_cap(gzvm, cap, &res);
		break;
	case GZVM_CAP_ARM_PVM_GET_PVMFW_SIZE:
		ret = gzvm_vm_ioctl_get_pvmfw_size(gzvm, cap, argp);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int gzvm_vm_ioctl_arch_enable_cap(struct gzvm *gzvm,
				  struct gzvm_enable_cap *cap,
				  void __user *argp)
{
	int ret = -EINVAL;

	switch (cap->cap) {
	case GZVM_CAP_ARM_PROTECTED_VM:
		ret = gzvm_vm_ioctl_cap_pvm(gzvm, cap, argp);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * gzvm_hva_to_pa_arch() - converts hva to pa with arch-specific way
 * @hva: Host virtual address.
 *
 * Return: 0 if translation error
 */
u64 gzvm_hva_to_pa_arch(u64 hva)
{
	u64 par;
	unsigned long flags;

	local_irq_save(flags);
	asm volatile("at s1e1r, %0" :: "r" (hva));
	isb();
	par = read_sysreg_par();
	local_irq_restore(flags);

	if (par & SYS_PAR_EL1_F)
		return 0;

	return par & PAR_PA47_MASK;
}
