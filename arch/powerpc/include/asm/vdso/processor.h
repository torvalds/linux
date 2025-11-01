/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_POWERPC_VDSO_PROCESSOR_H
#define _ASM_POWERPC_VDSO_PROCESSOR_H

#ifndef __ASSEMBLER__

/* Macros for adjusting thread priority (hardware multi-threading) */
#ifdef CONFIG_PPC64
#define HMT_very_low()		asm volatile("or 31, 31, 31	# very low priority")
#define HMT_low()		asm volatile("or 1, 1, 1	# low priority")
#define HMT_medium_low()	asm volatile("or 6, 6, 6	# medium low priority")
#define HMT_medium()		asm volatile("or 2, 2, 2	# medium priority")
#define HMT_medium_high()	asm volatile("or 5, 5, 5	# medium high priority")
#define HMT_high()		asm volatile("or 3, 3, 3	# high priority")
#else
#define HMT_very_low()
#define HMT_low()
#define HMT_medium_low()
#define HMT_medium()
#define HMT_medium_high()
#define HMT_high()
#endif

#ifdef CONFIG_PPC64
#define cpu_relax()							\
	asm volatile(ASM_FTR_IFCLR(					\
		/* Pre-POWER10 uses low ; medium priority nops */	\
		"or 1,1,1 ; or 2,2,2",					\
		/* POWER10 onward uses pause_short (wait 2,0) */	\
		PPC_WAIT(2, 0),						\
		%0) :: "i" (CPU_FTR_ARCH_31) : "memory")
#else
#define cpu_relax()	barrier()
#endif

#endif /* __ASSEMBLER__ */

#endif /* _ASM_POWERPC_VDSO_PROCESSOR_H */
