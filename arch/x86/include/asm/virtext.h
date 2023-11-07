/* SPDX-License-Identifier: GPL-2.0-only */
/* CPU virtualization extensions handling
 *
 * This should carry the code for handling CPU virtualization extensions
 * that needs to live in the kernel core.
 *
 * Author: Eduardo Habkost <ehabkost@redhat.com>
 *
 * Copyright (C) 2008, Red Hat Inc.
 *
 * Contains code from KVM, Copyright (C) 2006 Qumranet, Inc.
 */
#ifndef _ASM_X86_VIRTEX_H
#define _ASM_X86_VIRTEX_H

#include <asm/processor.h>

#include <asm/vmx.h>
#include <asm/svm.h>
#include <asm/tlbflush.h>

/*
 * VMX functions:
 */

static inline int cpu_has_vmx(void)
{
	unsigned long ecx = cpuid_ecx(1);
	return test_bit(5, &ecx); /* CPUID.1:ECX.VMX[bit 5] -> VT */
}


/**
 * cpu_vmxoff() - Disable VMX on the current CPU
 *
 * Disable VMX and clear CR4.VMXE (even if VMXOFF faults)
 *
 * Note, VMXOFF causes a #UD if the CPU is !post-VMXON, but it's impossible to
 * atomically track post-VMXON state, e.g. this may be called in NMI context.
 * Eat all faults as all other faults on VMXOFF faults are mode related, i.e.
 * faults are guaranteed to be due to the !post-VMXON check unless the CPU is
 * magically in RM, VM86, compat mode, or at CPL>0.
 */
static inline void cpu_vmxoff(void)
{
	asm_volatile_goto("1: vmxoff\n\t"
			  _ASM_EXTABLE(1b, %l[fault]) :::: fault);
fault:
	cr4_clear_bits(X86_CR4_VMXE);
}

static inline int cpu_vmx_enabled(void)
{
	return __read_cr4() & X86_CR4_VMXE;
}

/** Disable VMX if it is enabled on the current CPU
 *
 * You shouldn't call this if cpu_has_vmx() returns 0.
 */
static inline void __cpu_emergency_vmxoff(void)
{
	if (cpu_vmx_enabled())
		cpu_vmxoff();
}

/** Disable VMX if it is supported and enabled on the current CPU
 */
static inline void cpu_emergency_vmxoff(void)
{
	if (cpu_has_vmx())
		__cpu_emergency_vmxoff();
}




/*
 * SVM functions:
 */

/** Check if the CPU has SVM support
 *
 * You can use the 'msg' arg to get a message describing the problem,
 * if the function returns zero. Simply pass NULL if you are not interested
 * on the messages; gcc should take care of not generating code for
 * the messages on this case.
 */
static inline int cpu_has_svm(const char **msg)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
	    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON) {
		if (msg)
			*msg = "not amd or hygon";
		return 0;
	}

	if (!boot_cpu_has(X86_FEATURE_SVM)) {
		if (msg)
			*msg = "svm not available";
		return 0;
	}
	return 1;
}


/** Disable SVM on the current CPU
 *
 * You should call this only if cpu_has_svm() returned true.
 */
static inline void cpu_svm_disable(void)
{
	uint64_t efer;

	wrmsrl(MSR_VM_HSAVE_PA, 0);
	rdmsrl(MSR_EFER, efer);
	if (efer & EFER_SVME) {
		/*
		 * Force GIF=1 prior to disabling SVM to ensure INIT and NMI
		 * aren't blocked, e.g. if a fatal error occurred between CLGI
		 * and STGI.  Note, STGI may #UD if SVM is disabled from NMI
		 * context between reading EFER and executing STGI.  In that
		 * case, GIF must already be set, otherwise the NMI would have
		 * been blocked, so just eat the fault.
		 */
		asm_volatile_goto("1: stgi\n\t"
				  _ASM_EXTABLE(1b, %l[fault])
				  ::: "memory" : fault);
fault:
		wrmsrl(MSR_EFER, efer & ~EFER_SVME);
	}
}

/** Makes sure SVM is disabled, if it is supported on the CPU
 */
static inline void cpu_emergency_svm_disable(void)
{
	if (cpu_has_svm(NULL))
		cpu_svm_disable();
}

#endif /* _ASM_X86_VIRTEX_H */
