#ifndef __LINUX_KBUILD_H
# error "Please do not build this file directly, build asm-offsets.c instead"
#endif

#include <asm/ucontext.h>

#include <linux/lguest.h>
#include "../../../drivers/lguest/lg.h"

#define __SYSCALL_I386(nr, sym, compat) [nr] = 1,
static char syscalls[] = {
#include <asm/syscalls_32.h>
};

/* workaround for a warning with -Wmissing-prototypes */
void foo(void);

void foo(void)
{
	OFFSET(IA32_SIGCONTEXT_ax, sigcontext, ax);
	OFFSET(IA32_SIGCONTEXT_bx, sigcontext, bx);
	OFFSET(IA32_SIGCONTEXT_cx, sigcontext, cx);
	OFFSET(IA32_SIGCONTEXT_dx, sigcontext, dx);
	OFFSET(IA32_SIGCONTEXT_si, sigcontext, si);
	OFFSET(IA32_SIGCONTEXT_di, sigcontext, di);
	OFFSET(IA32_SIGCONTEXT_bp, sigcontext, bp);
	OFFSET(IA32_SIGCONTEXT_sp, sigcontext, sp);
	OFFSET(IA32_SIGCONTEXT_ip, sigcontext, ip);
	BLANK();

	OFFSET(CPUINFO_x86, cpuinfo_x86, x86);
	OFFSET(CPUINFO_x86_vendor, cpuinfo_x86, x86_vendor);
	OFFSET(CPUINFO_x86_model, cpuinfo_x86, x86_model);
	OFFSET(CPUINFO_x86_mask, cpuinfo_x86, x86_mask);
	OFFSET(CPUINFO_cpuid_level, cpuinfo_x86, cpuid_level);
	OFFSET(CPUINFO_x86_capability, cpuinfo_x86, x86_capability);
	OFFSET(CPUINFO_x86_vendor_id, cpuinfo_x86, x86_vendor_id);
	BLANK();

	OFFSET(TI_sysenter_return, thread_info, sysenter_return);
	OFFSET(TI_cpu, thread_info, cpu);
	BLANK();

	OFFSET(PT_EBX, pt_regs, bx);
	OFFSET(PT_ECX, pt_regs, cx);
	OFFSET(PT_EDX, pt_regs, dx);
	OFFSET(PT_ESI, pt_regs, si);
	OFFSET(PT_EDI, pt_regs, di);
	OFFSET(PT_EBP, pt_regs, bp);
	OFFSET(PT_EAX, pt_regs, ax);
	OFFSET(PT_DS,  pt_regs, ds);
	OFFSET(PT_ES,  pt_regs, es);
	OFFSET(PT_FS,  pt_regs, fs);
	OFFSET(PT_GS,  pt_regs, gs);
	OFFSET(PT_ORIG_EAX, pt_regs, orig_ax);
	OFFSET(PT_EIP, pt_regs, ip);
	OFFSET(PT_CS,  pt_regs, cs);
	OFFSET(PT_EFLAGS, pt_regs, flags);
	OFFSET(PT_OLDESP, pt_regs, sp);
	OFFSET(PT_OLDSS,  pt_regs, ss);
	BLANK();

	OFFSET(IA32_RT_SIGFRAME_sigcontext, rt_sigframe, uc.uc_mcontext);
	BLANK();

	OFFSET(saved_context_gdt_desc, saved_context, gdt_desc);
	BLANK();

	/* Offset from the sysenter stack to tss.sp0 */
	DEFINE(TSS_sysenter_sp0, offsetof(struct tss_struct, x86_tss.sp0) -
	       offsetofend(struct tss_struct, SYSENTER_stack));

#if defined(CONFIG_LGUEST) || defined(CONFIG_LGUEST_GUEST) || defined(CONFIG_LGUEST_MODULE)
	BLANK();
	OFFSET(LGUEST_DATA_irq_enabled, lguest_data, irq_enabled);
	OFFSET(LGUEST_DATA_irq_pending, lguest_data, irq_pending);

	BLANK();
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
	BLANK();
	DEFINE(__NR_syscall_max, sizeof(syscalls) - 1);
	DEFINE(NR_syscalls, sizeof(syscalls));
}
