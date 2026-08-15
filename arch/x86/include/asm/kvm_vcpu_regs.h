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

#ifdef __ASSEMBLER__

#define REG_NUM_INVALID		100

	# convert the 32-bit register operand \r32 into a register number;
	# store it in the value whose name is in \opd.
	# only for !CONFIG_X86_64 (does not support r8d-r15d)
	.macro R32_NUM opd r32
	\opd = REG_NUM_INVALID
	.ifc \r32,%eax
	\opd = __VCPU_REGS_RAX
	.endif
	.ifc \r32,%ecx
	\opd = __VCPU_REGS_RCX
	.endif
	.ifc \r32,%edx
	\opd = __VCPU_REGS_RDX
	.endif
	.ifc \r32,%ebx
	\opd = __VCPU_REGS_RBX
	.endif
	.ifc \r32,%esp
	\opd = __VCPU_REGS_RSP
	.endif
	.ifc \r32,%ebp
	\opd = __VCPU_REGS_RBP
	.endif
	.ifc \r32,%esi
	\opd = __VCPU_REGS_RSI
	.endif
	.ifc \r32,%edi
	\opd = __VCPU_REGS_RDI
	.endif
	.endm

	# convert the 64-bit register operand \r64 into a register number;
	# store it in the value whose name is in \opd.
	.macro R64_NUM opd r64
	\opd = REG_NUM_INVALID
#ifdef CONFIG_X86_64
	.ifc \r64,%rax
	\opd = __VCPU_REGS_RAX
	.endif
	.ifc \r64,%rcx
	\opd = __VCPU_REGS_RCX
	.endif
	.ifc \r64,%rdx
	\opd = __VCPU_REGS_RDX
	.endif
	.ifc \r64,%rbx
	\opd = __VCPU_REGS_RBX
	.endif
	.ifc \r64,%rsp
	\opd = __VCPU_REGS_RSP
	.endif
	.ifc \r64,%rbp
	\opd = __VCPU_REGS_RBP
	.endif
	.ifc \r64,%rsi
	\opd = __VCPU_REGS_RSI
	.endif
	.ifc \r64,%rdi
	\opd = __VCPU_REGS_RDI
	.endif
	.ifc \r64,%r8
	\opd = 8
	.endif
	.ifc \r64,%r9
	\opd = 9
	.endif
	.ifc \r64,%r10
	\opd = 10
	.endif
	.ifc \r64,%r11
	\opd = 11
	.endif
	.ifc \r64,%r12
	\opd = 12
	.endif
	.ifc \r64,%r13
	\opd = 13
	.endif
	.ifc \r64,%r14
	\opd = 14
	.endif
	.ifc \r64,%r15
	\opd = 15
	.endif
#endif
	.endm

.macro REG_NUM reg_num reg
#ifdef CONFIG_X86_64
	R64_NUM \reg_num \reg
#else
	R32_NUM \reg_num \reg
#endif
	.if \reg_num == REG_NUM_INVALID
	.error "invalid register"
	.endif
.endm

#endif

#endif /* _ASM_X86_KVM_VCPU_REGS_H */
