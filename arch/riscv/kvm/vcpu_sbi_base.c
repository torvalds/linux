// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/version.h>
#include <asm/csr.h>
#include <asm/sbi.h>
#include <asm/kvm_vcpu_timer.h>
#include <asm/kvm_vcpu_sbi.h>

static int kvm_sbi_ext_base_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				    unsigned long *out_val,
				    struct kvm_cpu_trap *trap, bool *exit)
{
	int ret = 0;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	struct sbiret ecall_ret;

	switch (cp->a6) {
	case SBI_EXT_BASE_GET_SPEC_VERSION:
		*out_val = (KVM_SBI_VERSION_MAJOR <<
			    SBI_SPEC_VERSION_MAJOR_SHIFT) |
			    KVM_SBI_VERSION_MINOR;
		break;
	case SBI_EXT_BASE_GET_IMP_ID:
		*out_val = KVM_SBI_IMPID;
		break;
	case SBI_EXT_BASE_GET_IMP_VERSION:
		*out_val = LINUX_VERSION_CODE;
		break;
	case SBI_EXT_BASE_PROBE_EXT:
		if ((cp->a0 >= SBI_EXT_EXPERIMENTAL_START &&
		     cp->a0 <= SBI_EXT_EXPERIMENTAL_END) ||
		    (cp->a0 >= SBI_EXT_VENDOR_START &&
		     cp->a0 <= SBI_EXT_VENDOR_END)) {
			/*
			 * For experimental/vendor extensions
			 * forward it to the userspace
			 */
			kvm_riscv_vcpu_sbi_forward(vcpu, run);
			*exit = true;
		} else
			*out_val = kvm_vcpu_sbi_find_ext(cp->a0) ? 1 : 0;
		break;
	case SBI_EXT_BASE_GET_MVENDORID:
	case SBI_EXT_BASE_GET_MARCHID:
	case SBI_EXT_BASE_GET_MIMPID:
		ecall_ret = sbi_ecall(SBI_EXT_BASE, cp->a6, 0, 0, 0, 0, 0, 0);
		if (!ecall_ret.error)
			*out_val = ecall_ret.value;
		/*TODO: We are unnecessarily converting the error twice */
		ret = sbi_err_map_linux_errno(ecall_ret.error);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_base = {
	.extid_start = SBI_EXT_BASE,
	.extid_end = SBI_EXT_BASE,
	.handler = kvm_sbi_ext_base_handler,
};

static int kvm_sbi_ext_forward_handler(struct kvm_vcpu *vcpu,
					struct kvm_run *run,
					unsigned long *out_val,
					struct kvm_cpu_trap *utrap,
					bool *exit)
{
	/*
	 * Both SBI experimental and vendor extensions are
	 * unconditionally forwarded to userspace.
	 */
	kvm_riscv_vcpu_sbi_forward(vcpu, run);
	*exit = true;
	return 0;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_experimental = {
	.extid_start = SBI_EXT_EXPERIMENTAL_START,
	.extid_end = SBI_EXT_EXPERIMENTAL_END,
	.handler = kvm_sbi_ext_forward_handler,
};

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_vendor = {
	.extid_start = SBI_EXT_VENDOR_START,
	.extid_end = SBI_EXT_VENDOR_END,
	.handler = kvm_sbi_ext_forward_handler,
};
