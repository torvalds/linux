/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#ifndef __ARM64_KVM_NVHE_SYS_REGS_H__
#define __ARM64_KVM_NVHE_SYS_REGS_H__

#include <asm/kvm_host.h>

u64 get_pvm_id_aa64pfr0(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64pfr1(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64zfr0(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64dfr0(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64dfr1(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64afr0(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64afr1(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64isar0(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64isar1(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64mmfr0(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64mmfr1(const struct kvm_vcpu *vcpu);
u64 get_pvm_id_aa64mmfr2(const struct kvm_vcpu *vcpu);

bool kvm_handle_pvm_sysreg(struct kvm_vcpu *vcpu, u64 *exit_code);
int kvm_check_pvm_sysreg_table(void);
void inject_undef64(struct kvm_vcpu *vcpu);

#endif /* __ARM64_KVM_NVHE_SYS_REGS_H__ */
