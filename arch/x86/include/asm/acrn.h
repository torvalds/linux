/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ACRN_H
#define _ASM_X86_ACRN_H

/*
 * This CPUID returns feature bitmaps in EAX.
 * Guest VM uses this to detect the appropriate feature bit.
 */
#define	ACRN_CPUID_FEATURES		0x40000001
/* Bit 0 indicates whether guest VM is privileged */
#define	ACRN_FEATURE_PRIVILEGED_VM	BIT(0)

/*
 * Timing Information.
 * This leaf returns the current TSC frequency in kHz.
 *
 * EAX: (Virtual) TSC frequency in kHz.
 * EBX, ECX, EDX: RESERVED (reserved fields are set to zero).
 */
#define ACRN_CPUID_TIMING_INFO		0x40000010

void acrn_setup_intr_handler(void (*handler)(void));
void acrn_remove_intr_handler(void);

static inline u32 acrn_cpuid_base(void)
{
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return cpuid_base_hypervisor("ACRNACRNACRN", 0);

	return 0;
}

static inline unsigned long acrn_get_tsc_khz(void)
{
	return cpuid_eax(ACRN_CPUID_TIMING_INFO);
}

/*
 * Hypercalls for ACRN
 *
 * - VMCALL instruction is used to implement ACRN hypercalls.
 * - ACRN hypercall ABI:
 *   - Hypercall number is passed in R8 register.
 *   - Up to 2 arguments are passed in RDI, RSI.
 *   - Return value will be placed in RAX.
 *
 * Because GCC doesn't support R8 register as direct register constraints, use
 * supported constraint as input with a explicit MOV to R8 in beginning of asm.
 */
static inline long acrn_hypercall0(unsigned long hcall_id)
{
	long result;

	asm volatile("movl %1, %%r8d\n\t"
		     "vmcall\n\t"
		     : "=a" (result)
		     : "g" (hcall_id)
		     : "r8", "memory");

	return result;
}

static inline long acrn_hypercall1(unsigned long hcall_id,
				   unsigned long param1)
{
	long result;

	asm volatile("movl %1, %%r8d\n\t"
		     "vmcall\n\t"
		     : "=a" (result)
		     : "g" (hcall_id), "D" (param1)
		     : "r8", "memory");

	return result;
}

static inline long acrn_hypercall2(unsigned long hcall_id,
				   unsigned long param1,
				   unsigned long param2)
{
	long result;

	asm volatile("movl %1, %%r8d\n\t"
		     "vmcall\n\t"
		     : "=a" (result)
		     : "g" (hcall_id), "D" (param1), "S" (param2)
		     : "r8", "memory");

	return result;
}

#endif /* _ASM_X86_ACRN_H */
