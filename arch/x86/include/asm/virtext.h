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
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#ifndef _ASM_X86_VIRTEX_H
#define _ASM_X86_VIRTEX_H

#include <asm/processor.h>
#include <asm/system.h>

#include <asm/vmx.h>

/*
 * VMX functions:
 */

static inline int cpu_has_vmx(void)
{
	unsigned long ecx = cpuid_ecx(1);
	return test_bit(5, &ecx); /* CPUID.1:ECX.VMX[bit 5] -> VT */
}


/** Disable VMX on the current CPU
 *
 * vmxoff causes a undefined-opcode exception if vmxon was not run
 * on the CPU previously. Only call this function if you know VMX
 * is enabled.
 */
static inline void cpu_vmxoff(void)
{
	asm volatile (ASM_VMX_VMXOFF : : : "cc");
	write_cr4(read_cr4() & ~X86_CR4_VMXE);
}

static inline int cpu_vmx_enabled(void)
{
	return read_cr4() & X86_CR4_VMXE;
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

#endif /* _ASM_X86_VIRTEX_H */
