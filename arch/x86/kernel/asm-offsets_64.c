#include <asm/ia32.h>

#define __SYSCALL_64(nr, sym, compat) [nr] = 1,
#define __SYSCALL_COMMON(nr, sym, compat) [nr] = 1,
#ifdef CONFIG_X86_X32_ABI
# define __SYSCALL_X32(nr, sym, compat) [nr] = 1,
#else
# define __SYSCALL_X32(nr, sym, compat) /* nothing */
#endif
static char syscalls_64[] = {
#include <asm/syscalls_64.h>
};
#define __SYSCALL_I386(nr, sym, compat) [nr] = 1,
static char syscalls_ia32[] = {
#include <asm/syscalls_32.h>
};

int main(void)
{
#ifdef CONFIG_PARAVIRT
	OFFSET(PV_IRQ_adjust_exception_frame, pv_irq_ops, adjust_exception_frame);
	OFFSET(PV_CPU_usergs_sysret32, pv_cpu_ops, usergs_sysret32);
	OFFSET(PV_CPU_usergs_sysret64, pv_cpu_ops, usergs_sysret64);
	OFFSET(PV_CPU_swapgs, pv_cpu_ops, swapgs);
	BLANK();
#endif

#ifdef CONFIG_IA32_EMULATION
	OFFSET(TI_sysenter_return, thread_info, sysenter_return);
	BLANK();

#define ENTRY(entry) OFFSET(IA32_SIGCONTEXT_ ## entry, sigcontext_ia32, entry)
	ENTRY(ax);
	ENTRY(bx);
	ENTRY(cx);
	ENTRY(dx);
	ENTRY(si);
	ENTRY(di);
	ENTRY(bp);
	ENTRY(sp);
	ENTRY(ip);
	BLANK();
#undef ENTRY

	OFFSET(IA32_RT_SIGFRAME_sigcontext, rt_sigframe_ia32, uc.uc_mcontext);
	BLANK();
#endif

#define ENTRY(entry) OFFSET(pt_regs_ ## entry, pt_regs, entry)
	ENTRY(bx);
	ENTRY(cx);
	ENTRY(dx);
	ENTRY(sp);
	ENTRY(bp);
	ENTRY(si);
	ENTRY(di);
	ENTRY(r8);
	ENTRY(r9);
	ENTRY(r10);
	ENTRY(r11);
	ENTRY(r12);
	ENTRY(r13);
	ENTRY(r14);
	ENTRY(r15);
	ENTRY(flags);
	BLANK();
#undef ENTRY

#define ENTRY(entry) OFFSET(saved_context_ ## entry, saved_context, entry)
	ENTRY(cr0);
	ENTRY(cr2);
	ENTRY(cr3);
	ENTRY(cr4);
	ENTRY(cr8);
	ENTRY(gdt_desc);
	BLANK();
#undef ENTRY

	OFFSET(TSS_ist, tss_struct, x86_tss.ist);
	BLANK();

	DEFINE(__NR_syscall_max, sizeof(syscalls_64) - 1);
	DEFINE(NR_syscalls, sizeof(syscalls_64));

	DEFINE(__NR_ia32_syscall_max, sizeof(syscalls_ia32) - 1);
	DEFINE(IA32_NR_syscalls, sizeof(syscalls_ia32));

	return 0;
}
