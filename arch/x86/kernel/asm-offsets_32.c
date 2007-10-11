/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#include <linux/crypto.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/suspend.h>
#include <asm/ucontext.h>
#include "sigframe_32.h"
#include <asm/pgtable.h>
#include <asm/fixmap.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/elf.h>

#include <xen/interface/xen.h>

#ifdef CONFIG_LGUEST_GUEST
#include <linux/lguest.h>
#include "../../../drivers/lguest/lg.h"
#endif

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem));

/* workaround for a warning with -Wmissing-prototypes */
void foo(void);

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
	OFFSET(TI_preempt_count, thread_info, preempt_count);
	OFFSET(TI_addr_limit, thread_info, addr_limit);
	OFFSET(TI_restart_block, thread_info, restart_block);
	OFFSET(TI_sysenter_return, thread_info, sysenter_return);
	OFFSET(TI_cpu, thread_info, cpu);
	BLANK();

	OFFSET(GDS_size, Xgt_desc_struct, size);
	OFFSET(GDS_address, Xgt_desc_struct, address);
	OFFSET(GDS_pad, Xgt_desc_struct, pad);
	BLANK();

	OFFSET(PT_EBX, pt_regs, ebx);
	OFFSET(PT_ECX, pt_regs, ecx);
	OFFSET(PT_EDX, pt_regs, edx);
	OFFSET(PT_ESI, pt_regs, esi);
	OFFSET(PT_EDI, pt_regs, edi);
	OFFSET(PT_EBP, pt_regs, ebp);
	OFFSET(PT_EAX, pt_regs, eax);
	OFFSET(PT_DS,  pt_regs, xds);
	OFFSET(PT_ES,  pt_regs, xes);
	OFFSET(PT_FS,  pt_regs, xfs);
	OFFSET(PT_ORIG_EAX, pt_regs, orig_eax);
	OFFSET(PT_EIP, pt_regs, eip);
	OFFSET(PT_CS,  pt_regs, xcs);
	OFFSET(PT_EFLAGS, pt_regs, eflags);
	OFFSET(PT_OLDESP, pt_regs, esp);
	OFFSET(PT_OLDSS,  pt_regs, xss);
	BLANK();

	OFFSET(EXEC_DOMAIN_handler, exec_domain, handler);
	OFFSET(RT_SIGFRAME_sigcontext, rt_sigframe, uc.uc_mcontext);
	BLANK();

	OFFSET(pbe_address, pbe, address);
	OFFSET(pbe_orig_address, pbe, orig_address);
	OFFSET(pbe_next, pbe, next);

	/* Offset from the sysenter stack to tss.esp0 */
	DEFINE(TSS_sysenter_esp0, offsetof(struct tss_struct, x86_tss.esp0) -
		 sizeof(struct tss_struct));

	DEFINE(PAGE_SIZE_asm, PAGE_SIZE);
	DEFINE(PAGE_SHIFT_asm, PAGE_SHIFT);
	DEFINE(PTRS_PER_PTE, PTRS_PER_PTE);
	DEFINE(PTRS_PER_PMD, PTRS_PER_PMD);
	DEFINE(PTRS_PER_PGD, PTRS_PER_PGD);

	DEFINE(VDSO_PRELINK_asm, VDSO_PRELINK);

	OFFSET(crypto_tfm_ctx_offset, crypto_tfm, __crt_ctx);

#ifdef CONFIG_PARAVIRT
	BLANK();
	OFFSET(PARAVIRT_enabled, paravirt_ops, paravirt_enabled);
	OFFSET(PARAVIRT_irq_disable, paravirt_ops, irq_disable);
	OFFSET(PARAVIRT_irq_enable, paravirt_ops, irq_enable);
	OFFSET(PARAVIRT_irq_enable_sysexit, paravirt_ops, irq_enable_sysexit);
	OFFSET(PARAVIRT_iret, paravirt_ops, iret);
	OFFSET(PARAVIRT_read_cr0, paravirt_ops, read_cr0);
#endif

#ifdef CONFIG_XEN
	BLANK();
	OFFSET(XEN_vcpu_info_mask, vcpu_info, evtchn_upcall_mask);
	OFFSET(XEN_vcpu_info_pending, vcpu_info, evtchn_upcall_pending);
#endif

#ifdef CONFIG_LGUEST_GUEST
	BLANK();
	OFFSET(LGUEST_DATA_irq_enabled, lguest_data, irq_enabled);
	OFFSET(LGUEST_PAGES_host_gdt_desc, lguest_pages, state.host_gdt_desc);
	OFFSET(LGUEST_PAGES_host_idt_desc, lguest_pages, state.host_idt_desc);
	OFFSET(LGUEST_PAGES_host_cr3, lguest_pages, state.host_cr3);
	OFFSET(LGUEST_PAGES_host_sp, lguest_pages, state.host_sp);
	OFFSET(LGUEST_PAGES_guest_gdt_desc, lguest_pages,state.guest_gdt_desc);
	OFFSET(LGUEST_PAGES_guest_idt_desc, lguest_pages,state.guest_idt_desc);
	OFFSET(LGUEST_PAGES_guest_gdt, lguest_pages, state.guest_gdt);
	OFFSET(LGUEST_PAGES_regs_trapnum, lguest_pages, regs.trapnum);
	OFFSET(LGUEST_PAGES_regs_errcode, lguest_pages, regs.errcode);
	OFFSET(LGUEST_PAGES_regs, lguest_pages, regs);
#endif
}
