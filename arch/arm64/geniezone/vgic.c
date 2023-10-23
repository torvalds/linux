// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/irqchip/arm-gic-v3.h>
#include <linux/gzvm.h>
#include <linux/gzvm_drv.h>
#include "gzvm_arch_common.h"

/**
 * is_irq_valid() - Check the irq number and irq_type are matched
 * @irq:		interrupt number
 * @irq_type:		interrupt type
 *
 * Return:
 * true if irq is valid else false.
 */
static bool is_irq_valid(u32 irq, u32 irq_type)
{
	switch (irq_type) {
	case GZVM_IRQ_TYPE_CPU:
		/*  0 ~ 15: SGI */
		if (likely(irq <= GZVM_IRQ_CPU_FIQ))
			return true;
		break;
	case GZVM_IRQ_TYPE_PPI:
		/* 16 ~ 31: PPI */
		if (likely(irq >= GZVM_VGIC_NR_SGIS &&
			   irq < GZVM_VGIC_NR_PRIVATE_IRQS))
			return true;
		break;
	case GZVM_IRQ_TYPE_SPI:
		/* 32 ~ : SPT */
		if (likely(irq >= GZVM_VGIC_NR_PRIVATE_IRQS))
			return true;
		break;
	default:
		return false;
	}
	return false;
}

/**
 * gzvm_vgic_inject_irq() - Inject virtual interrupt to a VM
 * @gzvm: Pointer to struct gzvm
 * @vcpu_idx: vcpu index, only valid if PPI
 * @irq_type: Interrupt type
 * @irq: irq number
 * @level: 1 if true else 0
 *
 * Return:
 * * 0			- Success.
 * * Negative		- Failure.
 */
static int gzvm_vgic_inject_irq(struct gzvm *gzvm, unsigned int vcpu_idx,
				u32 irq_type, u32 irq, bool level)
{
	unsigned long a1 = assemble_vm_vcpu_tuple(gzvm->vm_id, vcpu_idx);
	struct arm_smccc_res res;

	if (!unlikely(is_irq_valid(irq, irq_type)))
		return -EINVAL;

	gzvm_hypcall_wrapper(MT_HVC_GZVM_IRQ_LINE, a1, irq, level,
			     0, 0, 0, 0, &res);
	if (res.a0) {
		pr_err("Failed to set IRQ level (%d) to irq#%u on vcpu %d with ret=%d\n",
		       level, irq, vcpu_idx, (int)res.a0);
		return -EFAULT;
	}

	return 0;
}

/**
 * gzvm_vgic_inject_spi() - Inject virtual spi interrupt
 * @gzvm: Pointer to struct gzvm
 * @vcpu_idx: vcpu index
 * @spi_irq: This is spi interrupt number (starts from 0 instead of 32)
 * @level: 1 if true else 0
 *
 * Return:
 * * 0 if succeed else other negative values indicating each errors
 */
static int gzvm_vgic_inject_spi(struct gzvm *gzvm, unsigned int vcpu_idx,
				u32 spi_irq, bool level)
{
	return gzvm_vgic_inject_irq(gzvm, 0, GZVM_IRQ_TYPE_SPI,
				    spi_irq + GZVM_VGIC_NR_PRIVATE_IRQS,
				    level);
}

int gzvm_arch_create_device(u16 vm_id, struct gzvm_create_device *gzvm_dev)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_DEVICE, vm_id,
				    virt_to_phys(gzvm_dev), 0, 0, 0, 0, 0,
				    &res);
}

int gzvm_arch_inject_irq(struct gzvm *gzvm, unsigned int vcpu_idx,
			 u32 irq_type, u32 irq, bool level)
{
	/* default use spi */
	return gzvm_vgic_inject_spi(gzvm, vcpu_idx, irq, level);
}
