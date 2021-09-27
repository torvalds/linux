/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#ifndef __LINUX_KVM_RISCV_H
#define __LINUX_KVM_RISCV_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/ptrace.h>

#define __KVM_HAVE_READONLY_MEM

#define KVM_COALESCED_MMIO_PAGE_OFFSET 1

#define KVM_INTERRUPT_SET	-1U
#define KVM_INTERRUPT_UNSET	-2U

/* for KVM_GET_REGS and KVM_SET_REGS */
struct kvm_regs {
};

/* for KVM_GET_FPU and KVM_SET_FPU */
struct kvm_fpu {
};

/* KVM Debug exit structure */
struct kvm_debug_exit_arch {
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
};

/* definition of registers in kvm_run */
struct kvm_sync_regs {
};

/* dummy definition */
struct kvm_sregs {
};

#endif

#endif /* __LINUX_KVM_RISCV_H */
