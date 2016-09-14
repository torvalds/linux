#ifndef _ASM_X86_MWAIT_H
#define _ASM_X86_MWAIT_H

#include <linux/sched.h>

#include <asm/cpufeature.h>

#define MWAIT_SUBSTATE_MASK		0xf
#define MWAIT_CSTATE_MASK		0xf
#define MWAIT_SUBSTATE_SIZE		4
#define MWAIT_HINT2CSTATE(hint)		(((hint) >> MWAIT_SUBSTATE_SIZE) & MWAIT_CSTATE_MASK)
#define MWAIT_HINT2SUBSTATE(hint)	((hint) & MWAIT_CSTATE_MASK)

#define CPUID_MWAIT_LEAF		5
#define CPUID5_ECX_EXTENSIONS_SUPPORTED 0x1
#define CPUID5_ECX_INTERRUPT_BREAK	0x2

#define MWAIT_ECX_INTERRUPT_BREAK	0x1
#define MWAITX_ECX_TIMER_ENABLE		BIT(1)
#define MWAITX_MAX_LOOPS		((u32)-1)
#define MWAITX_DISABLE_CSTATES		0xf

static inline void __monitor(const void *eax, unsigned long ecx,
			     unsigned long edx)
{
	/* "monitor %eax, %ecx, %edx;" */
	asm volatile(".byte 0x0f, 0x01, 0xc8;"
		     :: "a" (eax), "c" (ecx), "d"(edx));
}

static inline void __monitorx(const void *eax, unsigned long ecx,
			      unsigned long edx)
{
	/* "monitorx %eax, %ecx, %edx;" */
	asm volatile(".byte 0x0f, 0x01, 0xfa;"
		     :: "a" (eax), "c" (ecx), "d"(edx));
}

static inline void __mwait(unsigned long eax, unsigned long ecx)
{
	/* "mwait %eax, %ecx;" */
	asm volatile(".byte 0x0f, 0x01, 0xc9;"
		     :: "a" (eax), "c" (ecx));
}

/*
 * MWAITX allows for a timer expiration to get the core out a wait state in
 * addition to the default MWAIT exit condition of a store appearing at a
 * monitored virtual address.
 *
 * Registers:
 *
 * MWAITX ECX[1]: enable timer if set
 * MWAITX EBX[31:0]: max wait time expressed in SW P0 clocks. The software P0
 * frequency is the same as the TSC frequency.
 *
 * Below is a comparison between MWAIT and MWAITX on AMD processors:
 *
 *                 MWAIT                           MWAITX
 * opcode          0f 01 c9           |            0f 01 fb
 * ECX[0]                  value of RFLAGS.IF seen by instruction
 * ECX[1]          unused/#GP if set  |            enable timer if set
 * ECX[31:2]                     unused/#GP if set
 * EAX                           unused (reserve for hint)
 * EBX[31:0]       unused             |            max wait time (P0 clocks)
 *
 *                 MONITOR                         MONITORX
 * opcode          0f 01 c8           |            0f 01 fa
 * EAX                     (logical) address to monitor
 * ECX                     #GP if not zero
 */
static inline void __mwaitx(unsigned long eax, unsigned long ebx,
			    unsigned long ecx)
{
	/* "mwaitx %eax, %ebx, %ecx;" */
	asm volatile(".byte 0x0f, 0x01, 0xfb;"
		     :: "a" (eax), "b" (ebx), "c" (ecx));
}

static inline void __sti_mwait(unsigned long eax, unsigned long ecx)
{
	trace_hardirqs_on();
	/* "mwait %eax, %ecx;" */
	asm volatile("sti; .byte 0x0f, 0x01, 0xc9;"
		     :: "a" (eax), "c" (ecx));
}

/*
 * This uses new MONITOR/MWAIT instructions on P4 processors with PNI,
 * which can obviate IPI to trigger checking of need_resched.
 * We execute MONITOR against need_resched and enter optimized wait state
 * through MWAIT. Whenever someone changes need_resched, we would be woken
 * up from MWAIT (without an IPI).
 *
 * New with Core Duo processors, MWAIT can take some hints based on CPU
 * capability.
 */
static inline void mwait_idle_with_hints(unsigned long eax, unsigned long ecx)
{
	if (static_cpu_has_bug(X86_BUG_MONITOR) || !current_set_polling_and_test()) {
		if (static_cpu_has_bug(X86_BUG_CLFLUSH_MONITOR)) {
			mb();
			clflush((void *)&current_thread_info()->flags);
			mb();
		}

		__monitor((void *)&current_thread_info()->flags, 0, 0);
		if (!need_resched())
			__mwait(eax, ecx);
	}
	current_clr_polling();
}

#endif /* _ASM_X86_MWAIT_H */
