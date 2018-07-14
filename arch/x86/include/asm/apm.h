/*
 *  Machine specific APM BIOS functions for generic.
 *  Split out from apm.c by Osamu Tomita <tomita@cinet.co.jp>
 */

#ifndef _ASM_X86_MACH_DEFAULT_APM_H
#define _ASM_X86_MACH_DEFAULT_APM_H

#include <asm/nospec-branch.h>

#ifdef APM_ZERO_SEGS
#	define APM_DO_ZERO_SEGS \
		"pushl %%ds\n\t" \
		"pushl %%es\n\t" \
		"xorl %%edx, %%edx\n\t" \
		"mov %%dx, %%ds\n\t" \
		"mov %%dx, %%es\n\t" \
		"mov %%dx, %%fs\n\t" \
		"mov %%dx, %%gs\n\t"
#	define APM_DO_POP_SEGS \
		"popl %%es\n\t" \
		"popl %%ds\n\t"
#else
#	define APM_DO_ZERO_SEGS
#	define APM_DO_POP_SEGS
#endif

static inline void apm_bios_call_asm(u32 func, u32 ebx_in, u32 ecx_in,
					u32 *eax, u32 *ebx, u32 *ecx,
					u32 *edx, u32 *esi)
{
	/*
	 * N.B. We do NOT need a cld after the BIOS call
	 * because we always save and restore the flags.
	 */
	firmware_restrict_branch_speculation_start();
	__asm__ __volatile__(APM_DO_ZERO_SEGS
		"pushl %%edi\n\t"
		"pushl %%ebp\n\t"
		"lcall *%%cs:apm_bios_entry\n\t"
		"setc %%al\n\t"
		"popl %%ebp\n\t"
		"popl %%edi\n\t"
		APM_DO_POP_SEGS
		: "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx),
		  "=S" (*esi)
		: "a" (func), "b" (ebx_in), "c" (ecx_in)
		: "memory", "cc");
	firmware_restrict_branch_speculation_end();
}

static inline u8 apm_bios_call_simple_asm(u32 func, u32 ebx_in,
						u32 ecx_in, u32 *eax)
{
	int	cx, dx, si;
	u8	error;

	/*
	 * N.B. We do NOT need a cld after the BIOS call
	 * because we always save and restore the flags.
	 */
	firmware_restrict_branch_speculation_start();
	__asm__ __volatile__(APM_DO_ZERO_SEGS
		"pushl %%edi\n\t"
		"pushl %%ebp\n\t"
		"lcall *%%cs:apm_bios_entry\n\t"
		"setc %%bl\n\t"
		"popl %%ebp\n\t"
		"popl %%edi\n\t"
		APM_DO_POP_SEGS
		: "=a" (*eax), "=b" (error), "=c" (cx), "=d" (dx),
		  "=S" (si)
		: "a" (func), "b" (ebx_in), "c" (ecx_in)
		: "memory", "cc");
	firmware_restrict_branch_speculation_end();
	return error;
}

#endif /* _ASM_X86_MACH_DEFAULT_APM_H */
