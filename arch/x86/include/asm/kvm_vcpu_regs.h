/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KVM_VCPU_REGS_H
#define _ASM_X86_KVM_VCPU_REGS_H

#define __VCPU_REGS_RAX  0
#define __VCPU_REGS_RCX  1
#define __VCPU_REGS_RDX  2
#define __VCPU_REGS_RBX  3
#define __VCPU_REGS_RSP  4
#define __VCPU_REGS_RBP  5
#define __VCPU_REGS_RSI  6
#define __VCPU_REGS_RDI  7

#ifdef CONFIG_X86_64
#define __VCPU_REGS_R8   8
#define __VCPU_REGS_R9   9
#define __VCPU_REGS_R10 10
#define __VCPU_REGS_R11 11
#define __VCPU_REGS_R12 12
#define __VCPU_REGS_R13 13
#define __VCPU_REGS_R14 14
#define __VCPU_REGS_R15 15
#endif

#endif /* _ASM_X86_KVM_VCPU_REGS_H */
