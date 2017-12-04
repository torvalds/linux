// SPDX-License-Identifier: GPL-2.0
#ifndef __LINUX_KBUILD_H
# error "Please do not build this file directly, build asm-offsets.c instead"
#endif

#include <asm/ucontext.h>

#define __SYSCALL_I386(nr, sym, qual) [nr] = 1,
static char syscalls[] = {
#include <asm/syscalls_32.h>
};

/* workaround for a warning with -Wmissing-prototypes */
void foo(void);

void foo(void)
{
	OFFSET(CPUINFO_x86, cpuinfo_x86, x86);
	OFFSET(CPUINFO_x86_vendor, cpuinfo_x86, x86_vendor);
	OFFSET(CPUINFO_x86_model, cpuinfo_x86, x86_model);
	OFFSET(CPUINFO_x86_mask, cpuinfo_x86, x86_mask);
	OFFSET(CPUINFO_cpuid_level, cpuinfo_x86, cpuid_level);
	OFFSET(CPUINFO_x86_capability, cpuinfo_x86, x86_capability);
	OFFSET(CPUINFO_x86_vendor_id, cpuinfo_x86, x86_vendor_id);
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

	OFFSET(saved_context_gdt_desc, saved_context, gdt_desc);
	BLANK();

	/* Offset from the sysenter stack to tss.sp0 */
	DEFINE(TSS_sysenter_sp0, offsetof(struct tss_struct, x86_tss.sp0) -
	       offsetofend(struct tss_struct, SYSENTER_stack));

#ifdef CONFIG_CC_STACKPROTECTOR
	BLANK();
	OFFSET(stack_canary_offset, stack_canary, canary);
#endif

	BLANK();
	DEFINE(__NR_syscall_max, sizeof(syscalls) - 1);
	DEFINE(NR_syscalls, sizeof(syscalls));
}
