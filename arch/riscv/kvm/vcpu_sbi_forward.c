// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Ventana Micro Systems Inc.
 */

#include <linux/kvm_host.h>
#include <asm/kvm_vcpu_sbi.h>
#include <asm/sbi.h>

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_experimental = {
	.extid_start = SBI_EXT_EXPERIMENTAL_START,
	.extid_end = SBI_EXT_EXPERIMENTAL_END,
	.handler = kvm_riscv_vcpu_sbi_forward_handler,
};

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_vendor = {
	.extid_start = SBI_EXT_VENDOR_START,
	.extid_end = SBI_EXT_VENDOR_END,
	.handler = kvm_riscv_vcpu_sbi_forward_handler,
};

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_dbcn = {
	.extid_start = SBI_EXT_DBCN,
	.extid_end = SBI_EXT_DBCN,
	.default_disabled = true,
	.handler = kvm_riscv_vcpu_sbi_forward_handler,
};

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_mpxy = {
	.extid_start = SBI_EXT_MPXY,
	.extid_end = SBI_EXT_MPXY,
	.default_disabled = true,
	.handler = kvm_riscv_vcpu_sbi_forward_handler,
};
