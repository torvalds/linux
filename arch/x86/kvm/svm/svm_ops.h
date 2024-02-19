/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_SVM_OPS_H
#define __KVM_X86_SVM_OPS_H

#include <linux/compiler_types.h>

#include "x86.h"

#define svm_asm(insn, clobber...)				\
do {								\
	asm goto("1: " __stringify(insn) "\n\t"	\
			  _ASM_EXTABLE(1b, %l[fault])		\
			  ::: clobber : fault);			\
	return;							\
fault:								\
	kvm_spurious_fault();					\
} while (0)

#define svm_asm1(insn, op1, clobber...)				\
do {								\
	asm goto("1: "  __stringify(insn) " %0\n\t"	\
			  _ASM_EXTABLE(1b, %l[fault])		\
			  :: op1 : clobber : fault);		\
	return;							\
fault:								\
	kvm_spurious_fault();					\
} while (0)

#define svm_asm2(insn, op1, op2, clobber...)				\
do {									\
	asm goto("1: "  __stringify(insn) " %1, %0\n\t"	\
			  _ASM_EXTABLE(1b, %l[fault])			\
			  :: op1, op2 : clobber : fault);		\
	return;								\
fault:									\
	kvm_spurious_fault();						\
} while (0)

static inline void clgi(void)
{
	svm_asm(clgi);
}

static inline void stgi(void)
{
	svm_asm(stgi);
}

static inline void invlpga(unsigned long addr, u32 asid)
{
	svm_asm2(invlpga, "c"(asid), "a"(addr));
}

/*
 * Despite being a physical address, the portion of rAX that is consumed by
 * VMSAVE, VMLOAD, etc... is still controlled by the effective address size,
 * hence 'unsigned long' instead of 'hpa_t'.
 */
static __always_inline void vmsave(unsigned long pa)
{
	svm_asm1(vmsave, "a" (pa), "memory");
}

#endif /* __KVM_X86_SVM_OPS_H */
