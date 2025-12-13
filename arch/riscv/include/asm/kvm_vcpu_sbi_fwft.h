/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Rivos Inc.
 *
 * Authors:
 *     Clément Léger <cleger@rivosinc.com>
 */

#ifndef __KVM_VCPU_RISCV_FWFT_H
#define __KVM_VCPU_RISCV_FWFT_H

#include <asm/sbi.h>

struct kvm_sbi_fwft_feature;

struct kvm_sbi_fwft_config {
	const struct kvm_sbi_fwft_feature *feature;
	bool supported;
	bool enabled;
	unsigned long flags;
};

/* FWFT data structure per vcpu */
struct kvm_sbi_fwft {
	struct kvm_sbi_fwft_config *configs;
#ifndef CONFIG_32BIT
	bool have_vs_pmlen_7;
	bool have_vs_pmlen_16;
#endif
};

#define vcpu_to_fwft(vcpu) (&(vcpu)->arch.fwft_context)

#endif /* !__KVM_VCPU_RISCV_FWFT_H */
