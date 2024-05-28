/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DEBUGREG_H
#define _ASM_X86_DEBUGREG_H

#include <linux/bug.h>
#include <linux/percpu.h>
#include <uapi/asm/debugreg.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>

DECLARE_PER_CPU(unsigned long, cpu_dr7);

#ifndef CONFIG_PARAVIRT_XXL
/*
 * These special macros can be used to get or set a debugging register
 */
#define get_debugreg(var, register)				\
	(var) = native_get_debugreg(register)
#define set_debugreg(value, register)				\
	native_set_debugreg(register, value)
#endif

static __always_inline unsigned long native_get_debugreg(int regno)
{
	unsigned long val = 0;	/* Damn you, gcc! */

	switch (regno) {
	case 0:
		asm("mov %%db0, %0" :"=r" (val));
		break;
	case 1:
		asm("mov %%db1, %0" :"=r" (val));
		break;
	case 2:
		asm("mov %%db2, %0" :"=r" (val));
		break;
	case 3:
		asm("mov %%db3, %0" :"=r" (val));
		break;
	case 6:
		asm("mov %%db6, %0" :"=r" (val));
		break;
	case 7:
		/*
		 * Apply __FORCE_ORDER to DR7 reads to forbid re-ordering them
		 * with other code.
		 *
		 * This is needed because a DR7 access can cause a #VC exception
		 * when running under SEV-ES. Taking a #VC exception is not a
		 * safe thing to do just anywhere in the entry code and
		 * re-ordering might place the access into an unsafe location.
		 *
		 * This happened in the NMI handler, where the DR7 read was
		 * re-ordered to happen before the call to sev_es_ist_enter(),
		 * causing stack recursion.
		 */
		asm volatile("mov %%db7, %0" : "=r" (val) : __FORCE_ORDER);
		break;
	default:
		BUG();
	}
	return val;
}

static __always_inline void native_set_debugreg(int regno, unsigned long value)
{
	switch (regno) {
	case 0:
		asm("mov %0, %%db0"	::"r" (value));
		break;
	case 1:
		asm("mov %0, %%db1"	::"r" (value));
		break;
	case 2:
		asm("mov %0, %%db2"	::"r" (value));
		break;
	case 3:
		asm("mov %0, %%db3"	::"r" (value));
		break;
	case 6:
		asm("mov %0, %%db6"	::"r" (value));
		break;
	case 7:
		/*
		 * Apply __FORCE_ORDER to DR7 writes to forbid re-ordering them
		 * with other code.
		 *
		 * While is didn't happen with a DR7 write (see the DR7 read
		 * comment above which explains where it happened), add the
		 * __FORCE_ORDER here too to avoid similar problems in the
		 * future.
		 */
		asm volatile("mov %0, %%db7"	::"r" (value), __FORCE_ORDER);
		break;
	default:
		BUG();
	}
}

static inline void hw_breakpoint_disable(void)
{
	/* Zero the control register for HW Breakpoint */
	set_debugreg(0UL, 7);

	/* Zero-out the individual HW breakpoint address registers */
	set_debugreg(0UL, 0);
	set_debugreg(0UL, 1);
	set_debugreg(0UL, 2);
	set_debugreg(0UL, 3);
}

static __always_inline bool hw_breakpoint_active(void)
{
	return __this_cpu_read(cpu_dr7) & DR_GLOBAL_ENABLE_MASK;
}

extern void hw_breakpoint_restore(void);

static __always_inline unsigned long local_db_save(void)
{
	unsigned long dr7;

	if (static_cpu_has(X86_FEATURE_HYPERVISOR) && !hw_breakpoint_active())
		return 0;

	get_debugreg(dr7, 7);
	dr7 &= ~0x400; /* architecturally set bit */
	if (dr7)
		set_debugreg(0, 7);
	/*
	 * Ensure the compiler doesn't lower the above statements into
	 * the critical section; disabling breakpoints late would not
	 * be good.
	 */
	barrier();

	return dr7;
}

static __always_inline void local_db_restore(unsigned long dr7)
{
	/*
	 * Ensure the compiler doesn't raise this statement into
	 * the critical section; enabling breakpoints early would
	 * not be good.
	 */
	barrier();
	if (dr7)
		set_debugreg(dr7, 7);
}

#ifdef CONFIG_CPU_SUP_AMD
extern void amd_set_dr_addr_mask(unsigned long mask, unsigned int dr);
extern unsigned long amd_get_dr_addr_mask(unsigned int dr);
#else
static inline void amd_set_dr_addr_mask(unsigned long mask, unsigned int dr) { }
static inline unsigned long amd_get_dr_addr_mask(unsigned int dr)
{
	return 0;
}
#endif

static inline unsigned long get_debugctlmsr(void)
{
	unsigned long debugctlmsr = 0;

#ifndef CONFIG_X86_DEBUGCTLMSR
	if (boot_cpu_data.x86 < 6)
		return 0;
#endif
	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctlmsr);

	return debugctlmsr;
}

static inline void update_debugctlmsr(unsigned long debugctlmsr)
{
#ifndef CONFIG_X86_DEBUGCTLMSR
	if (boot_cpu_data.x86 < 6)
		return;
#endif
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctlmsr);
}

#endif /* _ASM_X86_DEBUGREG_H */
