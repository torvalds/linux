/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_INSN_H
#define __KVM_X86_VMX_INSN_H

#include <linux/nospec.h>

#include <asm/vmx.h>

#include "vmx_onhyperv.h"
#include "vmcs.h"
#include "../x86.h"

void vmread_error(unsigned long field);
void vmwrite_error(unsigned long field, unsigned long value);
void vmclear_error(struct vmcs *vmcs, u64 phys_addr);
void vmptrld_error(struct vmcs *vmcs, u64 phys_addr);
void invvpid_error(unsigned long ext, u16 vpid, gva_t gva);
void invept_error(unsigned long ext, u64 eptp, gpa_t gpa);

#ifndef CONFIG_CC_HAS_ASM_GOTO_OUTPUT
/*
 * The VMREAD error trampoline _always_ uses the stack to pass parameters, even
 * for 64-bit targets.  Preserving all registers allows the VMREAD inline asm
 * blob to avoid clobbering GPRs, which in turn allows the compiler to better
 * optimize sequences of VMREADs.
 *
 * Declare the trampoline as an opaque label as it's not safe to call from C
 * code; there is no way to tell the compiler to pass params on the stack for
 * 64-bit targets.
 *
 * void vmread_error_trampoline(unsigned long field, bool fault);
 */
extern unsigned long vmread_error_trampoline;

/*
 * The second VMREAD error trampoline, called from the assembly trampoline,
 * exists primarily to enable instrumentation for the VM-Fail path.
 */
void vmread_error_trampoline2(unsigned long field, bool fault);

#endif

static __always_inline void vmcs_check16(unsigned long field)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6001) == 0x2000,
			 "16-bit accessor invalid for 64-bit field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6001) == 0x2001,
			 "16-bit accessor invalid for 64-bit high field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x4000,
			 "16-bit accessor invalid for 32-bit high field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x6000,
			 "16-bit accessor invalid for natural width field");
}

static __always_inline void vmcs_check32(unsigned long field)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0,
			 "32-bit accessor invalid for 16-bit field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6001) == 0x2000,
			 "32-bit accessor invalid for 64-bit field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6001) == 0x2001,
			 "32-bit accessor invalid for 64-bit high field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x6000,
			 "32-bit accessor invalid for natural width field");
}

static __always_inline void vmcs_check64(unsigned long field)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0,
			 "64-bit accessor invalid for 16-bit field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6001) == 0x2001,
			 "64-bit accessor invalid for 64-bit high field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x4000,
			 "64-bit accessor invalid for 32-bit field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x6000,
			 "64-bit accessor invalid for natural width field");
}

static __always_inline void vmcs_checkl(unsigned long field)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0,
			 "Natural width accessor invalid for 16-bit field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6001) == 0x2000,
			 "Natural width accessor invalid for 64-bit field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6001) == 0x2001,
			 "Natural width accessor invalid for 64-bit high field");
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x4000,
			 "Natural width accessor invalid for 32-bit field");
}

static __always_inline unsigned long __vmcs_readl(unsigned long field)
{
	unsigned long value;

#ifdef CONFIG_CC_HAS_ASM_GOTO_OUTPUT

	asm_goto_output("1: vmread %[field], %[output]\n\t"
			  "jna %l[do_fail]\n\t"

			  _ASM_EXTABLE(1b, %l[do_exception])

			  : [output] "=r" (value)
			  : [field] "r" (field)
			  : "cc"
			  : do_fail, do_exception);

	return value;

do_fail:
	instrumentation_begin();
	vmread_error(field);
	instrumentation_end();
	return 0;

do_exception:
	kvm_spurious_fault();
	return 0;

#else /* !CONFIG_CC_HAS_ASM_GOTO_OUTPUT */

	asm volatile("1: vmread %2, %1\n\t"
		     ".byte 0x3e\n\t" /* branch taken hint */
		     "ja 3f\n\t"

		     /*
		      * VMREAD failed.  Push '0' for @fault, push the failing
		      * @field, and bounce through the trampoline to preserve
		      * volatile registers.
		      */
		     "xorl %k1, %k1\n\t"
		     "2:\n\t"
		     "push %1\n\t"
		     "push %2\n\t"
		     "call vmread_error_trampoline\n\t"

		     /*
		      * Unwind the stack.  Note, the trampoline zeros out the
		      * memory for @fault so that the result is '0' on error.
		      */
		     "pop %2\n\t"
		     "pop %1\n\t"
		     "3:\n\t"

		     /* VMREAD faulted.  As above, except push '1' for @fault. */
		     _ASM_EXTABLE_TYPE_REG(1b, 2b, EX_TYPE_ONE_REG, %1)

		     : ASM_CALL_CONSTRAINT, "=&r"(value) : "r"(field) : "cc");
	return value;

#endif /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT */
}

static __always_inline u16 vmcs_read16(unsigned long field)
{
	vmcs_check16(field);
	if (kvm_is_using_evmcs())
		return evmcs_read16(field);
	return __vmcs_readl(field);
}

static __always_inline u32 vmcs_read32(unsigned long field)
{
	vmcs_check32(field);
	if (kvm_is_using_evmcs())
		return evmcs_read32(field);
	return __vmcs_readl(field);
}

static __always_inline u64 vmcs_read64(unsigned long field)
{
	vmcs_check64(field);
	if (kvm_is_using_evmcs())
		return evmcs_read64(field);
#ifdef CONFIG_X86_64
	return __vmcs_readl(field);
#else
	return __vmcs_readl(field) | ((u64)__vmcs_readl(field+1) << 32);
#endif
}

static __always_inline unsigned long vmcs_readl(unsigned long field)
{
	vmcs_checkl(field);
	if (kvm_is_using_evmcs())
		return evmcs_read64(field);
	return __vmcs_readl(field);
}

#define vmx_asm1(insn, op1, error_args...)				\
do {									\
	asm goto("1: " __stringify(insn) " %0\n\t"			\
			  ".byte 0x2e\n\t" /* branch not taken hint */	\
			  "jna %l[error]\n\t"				\
			  _ASM_EXTABLE(1b, %l[fault])			\
			  : : op1 : "cc" : error, fault);		\
	return;								\
error:									\
	instrumentation_begin();					\
	insn##_error(error_args);					\
	instrumentation_end();						\
	return;								\
fault:									\
	kvm_spurious_fault();						\
} while (0)

#define vmx_asm2(insn, op1, op2, error_args...)				\
do {									\
	asm goto("1: "  __stringify(insn) " %1, %0\n\t"			\
			  ".byte 0x2e\n\t" /* branch not taken hint */	\
			  "jna %l[error]\n\t"				\
			  _ASM_EXTABLE(1b, %l[fault])			\
			  : : op1, op2 : "cc" : error, fault);		\
	return;								\
error:									\
	instrumentation_begin();					\
	insn##_error(error_args);					\
	instrumentation_end();						\
	return;								\
fault:									\
	kvm_spurious_fault();						\
} while (0)

static __always_inline void __vmcs_writel(unsigned long field, unsigned long value)
{
	vmx_asm2(vmwrite, "r"(field), "rm"(value), field, value);
}

static __always_inline void vmcs_write16(unsigned long field, u16 value)
{
	vmcs_check16(field);
	if (kvm_is_using_evmcs())
		return evmcs_write16(field, value);

	__vmcs_writel(field, value);
}

static __always_inline void vmcs_write32(unsigned long field, u32 value)
{
	vmcs_check32(field);
	if (kvm_is_using_evmcs())
		return evmcs_write32(field, value);

	__vmcs_writel(field, value);
}

static __always_inline void vmcs_write64(unsigned long field, u64 value)
{
	vmcs_check64(field);
	if (kvm_is_using_evmcs())
		return evmcs_write64(field, value);

	__vmcs_writel(field, value);
#ifndef CONFIG_X86_64
	__vmcs_writel(field+1, value >> 32);
#endif
}

static __always_inline void vmcs_writel(unsigned long field, unsigned long value)
{
	vmcs_checkl(field);
	if (kvm_is_using_evmcs())
		return evmcs_write64(field, value);

	__vmcs_writel(field, value);
}

static __always_inline void vmcs_clear_bits(unsigned long field, u32 mask)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x2000,
			 "vmcs_clear_bits does not support 64-bit fields");
	if (kvm_is_using_evmcs())
		return evmcs_write32(field, evmcs_read32(field) & ~mask);

	__vmcs_writel(field, __vmcs_readl(field) & ~mask);
}

static __always_inline void vmcs_set_bits(unsigned long field, u32 mask)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x2000,
			 "vmcs_set_bits does not support 64-bit fields");
	if (kvm_is_using_evmcs())
		return evmcs_write32(field, evmcs_read32(field) | mask);

	__vmcs_writel(field, __vmcs_readl(field) | mask);
}

static inline void vmcs_clear(struct vmcs *vmcs)
{
	u64 phys_addr = __pa(vmcs);

	vmx_asm1(vmclear, "m"(phys_addr), vmcs, phys_addr);
}

static inline void vmcs_load(struct vmcs *vmcs)
{
	u64 phys_addr = __pa(vmcs);

	if (kvm_is_using_evmcs())
		return evmcs_load(phys_addr);

	vmx_asm1(vmptrld, "m"(phys_addr), vmcs, phys_addr);
}

static inline void __invvpid(unsigned long ext, u16 vpid, gva_t gva)
{
	struct {
		u64 vpid : 16;
		u64 rsvd : 48;
		u64 gva;
	} operand = { vpid, 0, gva };

	vmx_asm2(invvpid, "r"(ext), "m"(operand), ext, vpid, gva);
}

static inline void __invept(unsigned long ext, u64 eptp, gpa_t gpa)
{
	struct {
		u64 eptp, gpa;
	} operand = {eptp, gpa};

	vmx_asm2(invept, "r"(ext), "m"(operand), ext, eptp, gpa);
}

static inline void vpid_sync_vcpu_single(int vpid)
{
	if (vpid == 0)
		return;

	__invvpid(VMX_VPID_EXTENT_SINGLE_CONTEXT, vpid, 0);
}

static inline void vpid_sync_vcpu_global(void)
{
	__invvpid(VMX_VPID_EXTENT_ALL_CONTEXT, 0, 0);
}

static inline void vpid_sync_context(int vpid)
{
	if (cpu_has_vmx_invvpid_single())
		vpid_sync_vcpu_single(vpid);
	else if (vpid != 0)
		vpid_sync_vcpu_global();
}

static inline void vpid_sync_vcpu_addr(int vpid, gva_t addr)
{
	if (vpid == 0)
		return;

	if (cpu_has_vmx_invvpid_individual_addr())
		__invvpid(VMX_VPID_EXTENT_INDIVIDUAL_ADDR, vpid, addr);
	else
		vpid_sync_context(vpid);
}

static inline void ept_sync_global(void)
{
	__invept(VMX_EPT_EXTENT_GLOBAL, 0, 0);
}

static inline void ept_sync_context(u64 eptp)
{
	if (cpu_has_vmx_invept_context())
		__invept(VMX_EPT_EXTENT_CONTEXT, eptp, 0);
	else
		ept_sync_global();
}

#endif /* __KVM_X86_VMX_INSN_H */
