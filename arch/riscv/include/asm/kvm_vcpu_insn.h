/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#ifndef __KVM_VCPU_RISCV_INSN_H
#define __KVM_VCPU_RISCV_INSN_H

struct kvm_vcpu;
struct kvm_run;
struct kvm_cpu_trap;

struct kvm_mmio_decode {
	unsigned long insn;
	int insn_len;
	int len;
	int shift;
	int return_handled;
};

struct kvm_csr_decode {
	unsigned long insn;
	int return_handled;
};

/* Return values used by function emulating a particular instruction */
enum kvm_insn_return {
	KVM_INSN_EXIT_TO_USER_SPACE = 0,
	KVM_INSN_CONTINUE_NEXT_SEPC,
	KVM_INSN_CONTINUE_SAME_SEPC,
	KVM_INSN_ILLEGAL_TRAP,
	KVM_INSN_VIRTUAL_TRAP
};

void kvm_riscv_vcpu_wfi(struct kvm_vcpu *vcpu);
int kvm_riscv_vcpu_csr_return(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_riscv_vcpu_virtual_insn(struct kvm_vcpu *vcpu, struct kvm_run *run,
				struct kvm_cpu_trap *trap);

int kvm_riscv_vcpu_mmio_load(struct kvm_vcpu *vcpu, struct kvm_run *run,
			     unsigned long fault_addr,
			     unsigned long htinst);
int kvm_riscv_vcpu_mmio_store(struct kvm_vcpu *vcpu, struct kvm_run *run,
			      unsigned long fault_addr,
			      unsigned long htinst);
int kvm_riscv_vcpu_mmio_return(struct kvm_vcpu *vcpu, struct kvm_run *run);

#endif
