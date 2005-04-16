/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/suspend.h>
#include <asm/ucontext.h>
#include "sigframe.h"
#include <asm/fixmap.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

void foo(void)
{
	OFFSET(SIGCONTEXT_eax, sigcontext, eax);
	OFFSET(SIGCONTEXT_ebx, sigcontext, ebx);
	OFFSET(SIGCONTEXT_ecx, sigcontext, ecx);
	OFFSET(SIGCONTEXT_edx, sigcontext, edx);
	OFFSET(SIGCONTEXT_esi, sigcontext, esi);
	OFFSET(SIGCONTEXT_edi, sigcontext, edi);
	OFFSET(SIGCONTEXT_ebp, sigcontext, ebp);
	OFFSET(SIGCONTEXT_esp, sigcontext, esp);
	OFFSET(SIGCONTEXT_eip, sigcontext, eip);
	BLANK();

	OFFSET(CPUINFO_x86, cpuinfo_x86, x86);
	OFFSET(CPUINFO_x86_vendor, cpuinfo_x86, x86_vendor);
	OFFSET(CPUINFO_x86_model, cpuinfo_x86, x86_model);
	OFFSET(CPUINFO_x86_mask, cpuinfo_x86, x86_mask);
	OFFSET(CPUINFO_hard_math, cpuinfo_x86, hard_math);
	OFFSET(CPUINFO_cpuid_level, cpuinfo_x86, cpuid_level);
	OFFSET(CPUINFO_x86_capability, cpuinfo_x86, x86_capability);
	OFFSET(CPUINFO_x86_vendor_id, cpuinfo_x86, x86_vendor_id);
	BLANK();

	OFFSET(TI_task, thread_info, task);
	OFFSET(TI_exec_domain, thread_info, exec_domain);
	OFFSET(TI_flags, thread_info, flags);
	OFFSET(TI_status, thread_info, status);
	OFFSET(TI_cpu, thread_info, cpu);
	OFFSET(TI_preempt_count, thread_info, preempt_count);
	OFFSET(TI_addr_limit, thread_info, addr_limit);
	OFFSET(TI_restart_block, thread_info, restart_block);
	BLANK();

	OFFSET(EXEC_DOMAIN_handler, exec_domain, handler);
	OFFSET(RT_SIGFRAME_sigcontext, rt_sigframe, uc.uc_mcontext);
	BLANK();

	OFFSET(pbe_address, pbe, address);
	OFFSET(pbe_orig_address, pbe, orig_address);
	OFFSET(pbe_next, pbe, next);

	/* Offset from the sysenter stack to tss.esp0 */
	DEFINE(TSS_sysenter_esp0, offsetof(struct tss_struct, esp0) -
		 sizeof(struct tss_struct));

	DEFINE(PAGE_SIZE_asm, PAGE_SIZE);
	DEFINE(VSYSCALL_BASE, __fix_to_virt(FIX_VSYSCALL));
}
