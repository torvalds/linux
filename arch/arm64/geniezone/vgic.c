// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/irqchip/arm-gic-v3.h>
#include <linux/gzvm.h>
#include <linux/gzvm_drv.h>
#include "gzvm_arch_common.h"

int gzvm_arch_create_device(u16 vm_id, struct gzvm_create_device *gzvm_dev)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_DEVICE, vm_id,
				    virt_to_phys(gzvm_dev), 0, 0, 0, 0, 0,
				    &res);
}

/**
 * gzvm_arch_inject_irq() - Inject virtual interrupt to a VM
 * @gzvm: Pointer to struct gzvm
 * @vcpu_idx: vcpu index, only valid if PPI
 * @irq: *SPI* irq number (excluding offset value `32`)
 * @level: 1 if true else 0
 *
 * Return:
 * * 0			- Success.
 * * Negative		- Failure.
 */
int gzvm_arch_inject_irq(struct gzvm *gzvm, unsigned int vcpu_idx,
			 u32 irq, bool level)
{
	unsigned long a1 = assemble_vm_vcpu_tuple(gzvm->vm_id, vcpu_idx);
	struct arm_smccc_res res;

	/*
	 * VMM's virtual device irq number starts from 0, but ARM's shared peripheral
	 * interrupt number starts from 32. hypervisor adds offset 32
	 */
	gzvm_hypcall_wrapper(MT_HVC_GZVM_IRQ_LINE, a1, irq, level,
			     0, 0, 0, 0, &res);
	if (res.a0) {
		pr_err("Failed to set IRQ level (%d) to irq#%u on vcpu %d with ret=%d\n",
		       level, irq, vcpu_idx, (int)res.a0);
		return -EFAULT;
	}

	return 0;
}
