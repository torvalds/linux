/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#ifndef __ARM64_KVM_NVHE_SYS_REGS_H__
#define __ARM64_KVM_NVHE_SYS_REGS_H__

#include <asm/kvm_host.h>

u64 pvm_read_id_reg(const struct kvm_vcpu *vcpu, u32 id);
bool kvm_handle_pvm_sysreg(struct kvm_vcpu *vcpu, u64 *exit_code);
int kvm_check_pvm_sysreg_table(void);
void inject_undef64(struct kvm_vcpu *vcpu);

#endif /* __ARM64_KVM_NVHE_SYS_REGS_H__ */
