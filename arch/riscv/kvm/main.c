// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>
#include <asm/hwcap.h>
#include <asm/sbi.h>

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}

int kvm_arch_check_processor_compat(void *opaque)
{
	return 0;
}

int kvm_arch_hardware_setup(void *opaque)
{
	return 0;
}

int kvm_arch_hardware_enable(void)
{
	unsigned long hideleg, hedeleg;

	hedeleg = 0;
	hedeleg |= (1UL << EXC_INST_MISALIGNED);
	hedeleg |= (1UL << EXC_BREAKPOINT);
	hedeleg |= (1UL << EXC_SYSCALL);
	hedeleg |= (1UL << EXC_INST_PAGE_FAULT);
	hedeleg |= (1UL << EXC_LOAD_PAGE_FAULT);
	hedeleg |= (1UL << EXC_STORE_PAGE_FAULT);
	csr_write(CSR_HEDELEG, hedeleg);

	hideleg = 0;
	hideleg |= (1UL << IRQ_VS_SOFT);
	hideleg |= (1UL << IRQ_VS_TIMER);
	hideleg |= (1UL << IRQ_VS_EXT);
	csr_write(CSR_HIDELEG, hideleg);

	csr_write(CSR_HCOUNTEREN, -1UL);

	csr_write(CSR_HVIP, 0);

	return 0;
}

void kvm_arch_hardware_disable(void)
{
	csr_write(CSR_HEDELEG, 0);
	csr_write(CSR_HIDELEG, 0);
}

int kvm_arch_init(void *opaque)
{
	const char *str;

	if (!riscv_isa_extension_available(NULL, h)) {
		kvm_info("hypervisor extension not available\n");
		return -ENODEV;
	}

	if (sbi_spec_is_0_1()) {
		kvm_info("require SBI v0.2 or higher\n");
		return -ENODEV;
	}

	if (sbi_probe_extension(SBI_EXT_RFENCE) <= 0) {
		kvm_info("require SBI RFENCE extension\n");
		return -ENODEV;
	}

	kvm_riscv_stage2_mode_detect();

	kvm_riscv_stage2_vmid_detect();

	kvm_info("hypervisor extension available\n");

	switch (kvm_riscv_stage2_mode()) {
	case HGATP_MODE_SV32X4:
		str = "Sv32x4";
		break;
	case HGATP_MODE_SV39X4:
		str = "Sv39x4";
		break;
	case HGATP_MODE_SV48X4:
		str = "Sv48x4";
		break;
	default:
		return -ENODEV;
	}
	kvm_info("using %s G-stage page table format\n", str);

	kvm_info("VMID %ld bits available\n", kvm_riscv_stage2_vmid_bits());

	return 0;
}

void kvm_arch_exit(void)
{
}

static int riscv_kvm_init(void)
{
	return kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);
}
module_init(riscv_kvm_init);
