/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MWAIT_H
#define _ASM_X86_MWAIT_H

#include <linux/sched.h>
#include <linux/sched/idle.h>

#include <asm/cpufeature.h>
#include <asm/nospec-branch.h>

#define MWAIT_SUBSTATE_MASK		0xf
#define MWAIT_CSTATE_MASK		0xf
#define MWAIT_SUBSTATE_SIZE		4
#define MWAIT_HINT2CSTATE(hint)		(((hint) >> MWAIT_SUBSTATE_SIZE) & MWAIT_CSTATE_MASK)
#define MWAIT_HINT2SUBSTATE(hint)	((hint) & MWAIT_CSTATE_MASK)
#define MWAIT_C1_SUBSTATE_MASK  0xf0

#define CPUID5_ECX_EXTENSIONS_SUPPORTED 0x1
#define CPUID5_ECX_INTERRUPT_BREAK	0x2

#define MWAIT_ECX_INTERRUPT_BREAK	0x1
#define MWAITX_ECX_TIMER_ENABLE		BIT(1)
#define MWAITX_MAX_WAIT_CYCLES		UINT_MAX
#define MWAITX_DISABLE_CSTATES		0xf0
#define TPAUSE_C01_STATE		1
#define TPAUSE_C02_STATE		0

static __always_inline void __monitor(const void *eax, u32 ecx, u32 edx)
{
	/*
	 * Use the instruction mnemonic with implicit operands, as the LLVM
	 * assembler fails to assemble the mnemonic with explicit operands:
	 */
	asm volatile("monitor" :: "a" (eax), "c" (ecx), "d" (edx));
}

static __always_inline void __monitorx(const void *eax, u32 ecx, u32 edx)
{
	/* "monitorx %eax, %ecx, %edx" */
	asm volatile(".byte 0x0f, 0x01, 0xfa"
		     :: "a" (eax), "c" (ecx), "d"(edx));
}

static __always_inline void __mwait(u32 eax, u32 ecx)
{
	/*
	 * Use the instruction mnemonic with implicit operands, as the LLVM
	 * assembler fails to assemble the mnemonic with explicit operands:
	 */
	asm volatile("mwait" :: "a" (eax), "c" (ecx));
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
static __always_inline void __mwaitx(u32 eax, u32 ebx, u32 ecx)
{
	/* No need for TSA buffer clearing on AMD */

	/* "mwaitx %eax, %ebx, %ecx" */
	asm volatile(".byte 0x0f, 0x01, 0xfb"
		     :: "a" (eax), "b" (ebx), "c" (ecx));
}

/*
 * Re-enable interrupts right upon calling mwait in such a way that
 * no interrupt can fire _before_ the execution of mwait, ie: no
 * instruction must be placed between "sti" and "mwait".
 *
 * This is necessary because if an interrupt queues a timer before
 * executing mwait, it would otherwise go unnoticed and the next tick
 * would not be reprogrammed accordingly before mwait ever wakes up.
 */
static __always_inline void __sti_mwait(u32 eax, u32 ecx)
{

	asm volatile("sti; mwait" :: "a" (eax), "c" (ecx));
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
static __always_inline void mwait_idle_with_hints(u32 eax, u32 ecx)
{
	if (need_resched())
		return;

	x86_idle_clear_cpu_buffers();

	if (static_cpu_has_bug(X86_BUG_MONITOR) || !current_set_polling_and_test()) {
		const void *addr = &current_thread_info()->flags;

		alternative_input("", "clflush (%[addr])", X86_BUG_CLFLUSH_MONITOR, [addr] "a" (addr));
		__monitor(addr, 0, 0);

		if (need_resched())
			goto out;

		if (ecx & 1) {
			__mwait(eax, ecx);
		} else {
			__sti_mwait(eax, ecx);
			raw_local_irq_disable();
		}
	}

out:
	current_clr_polling();
}

/*
 * Caller can specify whether to enter C0.1 (low latency, less
 * power saving) or C0.2 state (saves more power, but longer wakeup
 * latency). This may be overridden by the IA32_UMWAIT_CONTROL MSR
 * which can force requests for C0.2 to be downgraded to C0.1.
 */
static inline void __tpause(u32 ecx, u32 edx, u32 eax)
{
	/* "tpause %ecx" */
	asm volatile(".byte 0x66, 0x0f, 0xae, 0xf1"
		     :: "c" (ecx), "d" (edx), "a" (eax));
}

#endif /* _ASM_X86_MWAIT_H */
