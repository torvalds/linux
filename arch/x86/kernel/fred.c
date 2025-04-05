/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>

#include <asm/desc.h>
#include <asm/fred.h>
#include <asm/tlbflush.h>
#include <asm/traps.h>

/* #DB in the kernel would imply the use of a kernel debugger. */
#define FRED_DB_STACK_LEVEL		1UL
#define FRED_NMI_STACK_LEVEL		2UL
#define FRED_MC_STACK_LEVEL		2UL
/*
 * #DF is the highest level because a #DF means "something went wrong
 * *while delivering an exception*." The number of cases for which that
 * can happen with FRED is drastically reduced and basically amounts to
 * "the stack you pointed me to is broken." Thus, always change stacks
 * on #DF, which means it should be at the highest level.
 */
#define FRED_DF_STACK_LEVEL		3UL

#define FRED_STKLVL(vector, lvl)	((lvl) << (2 * (vector)))

DEFINE_PER_CPU(unsigned long, fred_rsp0);
EXPORT_PER_CPU_SYMBOL(fred_rsp0);

void cpu_init_fred_exceptions(void)
{
	/* When FRED is enabled by default, remove this log message */
	pr_info("Initialize FRED on CPU%d\n", smp_processor_id());

	/*
	 * If a kernel event is delivered before a CPU goes to user level for
	 * the first time, its SS is NULL thus NULL is pushed into the SS field
	 * of the FRED stack frame.  But before ERETS is executed, the CPU may
	 * context switch to another task and go to user level.  Then when the
	 * CPU comes back to kernel mode, SS is changed to __KERNEL_DS.  Later
	 * when ERETS is executed to return from the kernel event handler, a #GP
	 * fault is generated because SS doesn't match the SS saved in the FRED
	 * stack frame.
	 *
	 * Initialize SS to __KERNEL_DS when enabling FRED to avoid such #GPs.
	 */
	loadsegment(ss, __KERNEL_DS);

	wrmsrl(MSR_IA32_FRED_CONFIG,
	       /* Reserve for CALL emulation */
	       FRED_CONFIG_REDZONE |
	       FRED_CONFIG_INT_STKLVL(0) |
	       FRED_CONFIG_ENTRYPOINT(asm_fred_entrypoint_user));

	wrmsrl(MSR_IA32_FRED_STKLVLS, 0);

	/*
	 * Ater a CPU offline/online cycle, the FRED RSP0 MSR should be
	 * resynchronized with its per-CPU cache.
	 */
	wrmsrl(MSR_IA32_FRED_RSP0, __this_cpu_read(fred_rsp0));

	wrmsrl(MSR_IA32_FRED_RSP1, 0);
	wrmsrl(MSR_IA32_FRED_RSP2, 0);
	wrmsrl(MSR_IA32_FRED_RSP3, 0);

	/* Enable FRED */
	cr4_set_bits(X86_CR4_FRED);
	/* Any further IDT use is a bug */
	idt_invalidate();

	/* Use int $0x80 for 32-bit system calls in FRED mode */
	setup_clear_cpu_cap(X86_FEATURE_SYSENTER32);
	setup_clear_cpu_cap(X86_FEATURE_SYSCALL32);
}

/* Must be called after setup_cpu_entry_areas() */
void cpu_init_fred_rsps(void)
{
	/*
	 * The purpose of separate stacks for NMI, #DB and #MC *in the kernel*
	 * (remember that user space faults are always taken on stack level 0)
	 * is to avoid overflowing the kernel stack.
	 */
	wrmsrl(MSR_IA32_FRED_STKLVLS,
	       FRED_STKLVL(X86_TRAP_DB,  FRED_DB_STACK_LEVEL) |
	       FRED_STKLVL(X86_TRAP_NMI, FRED_NMI_STACK_LEVEL) |
	       FRED_STKLVL(X86_TRAP_MC,  FRED_MC_STACK_LEVEL) |
	       FRED_STKLVL(X86_TRAP_DF,  FRED_DF_STACK_LEVEL));

	/* The FRED equivalents to IST stacks... */
	wrmsrl(MSR_IA32_FRED_RSP1, __this_cpu_ist_top_va(DB));
	wrmsrl(MSR_IA32_FRED_RSP2, __this_cpu_ist_top_va(NMI));
	wrmsrl(MSR_IA32_FRED_RSP3, __this_cpu_ist_top_va(DF));
}
