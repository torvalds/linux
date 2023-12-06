// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/arm-smccc.h>
#include <linux/err.h>
#include <linux/uaccess.h>

#include <linux/gzvm.h>
#include <linux/gzvm_drv.h>
#include "gzvm_arch_common.h"

int gzvm_arch_vcpu_update_one_reg(struct gzvm_vcpu *vcpu, __u64 reg_id,
				  bool is_write, __u64 *data)
{
	struct arm_smccc_res res;
	unsigned long a1;
	int ret;

	a1 = assemble_vm_vcpu_tuple(vcpu->gzvm->vm_id, vcpu->vcpuid);
	if (!is_write) {
		ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_GET_ONE_REG,
					   a1, reg_id, 0, 0, 0, 0, 0, &res);
		if (ret == 0)
			*data = res.a1;
	} else {
		ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_SET_ONE_REG,
					   a1, reg_id, *data, 0, 0, 0, 0, &res);
	}

	return ret;
}

int gzvm_arch_vcpu_run(struct gzvm_vcpu *vcpu, __u64 *exit_reason)
{
	struct arm_smccc_res res;
	unsigned long a1;
	int ret;

	a1 = assemble_vm_vcpu_tuple(vcpu->gzvm->vm_id, vcpu->vcpuid);
	ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_RUN, a1, 0, 0, 0, 0, 0,
				   0, &res);
	*exit_reason = res.a1;
	return ret;
}

int gzvm_arch_destroy_vcpu(u16 vm_id, int vcpuid)
{
	struct arm_smccc_res res;
	unsigned long a1;

	a1 = assemble_vm_vcpu_tuple(vm_id, vcpuid);
	gzvm_hypcall_wrapper(MT_HVC_GZVM_DESTROY_VCPU, a1, 0, 0, 0, 0, 0, 0,
			     &res);

	return 0;
}

/**
 * gzvm_arch_create_vcpu() - Call smc to gz hypervisor to create vcpu
 * @vm_id: vm id
 * @vcpuid: vcpu id
 * @run: Virtual address of vcpu->run
 *
 * Return: The wrapper helps caller to convert geniezone errno to Linux errno.
 */
int gzvm_arch_create_vcpu(u16 vm_id, int vcpuid, void *run)
{
	struct arm_smccc_res res;
	unsigned long a1, a2;
	int ret;

	a1 = assemble_vm_vcpu_tuple(vm_id, vcpuid);
	a2 = (__u64)virt_to_phys(run);
	ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_VCPU, a1, a2, 0, 0, 0, 0,
				   0, &res);

	return ret;
}
