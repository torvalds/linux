// SPDX-License-Identifier: GPL-2.0
#ifndef __LINUX_KBUILD_H
# error "Please do not build this file directly, build asm-offsets.c instead"
#endif

#include <asm/ia32.h>

#define __SYSCALL_64(nr, sym, qual) [nr] = 1,
#define __SYSCALL_X32(nr, sym, qual)
static char syscalls_64[] = {
#include <asm/syscalls_64.h>
};
#undef __SYSCALL_64
#undef __SYSCALL_X32

#ifdef CONFIG_X86_X32_ABI
#define __SYSCALL_64(nr, sym, qual)
#define __SYSCALL_X32(nr, sym, qual) [nr] = 1,
static char syscalls_x32[] = {
#include <asm/syscalls_64.h>
};
#undef __SYSCALL_64
#undef __SYSCALL_X32
#endif

#define __SYSCALL_I386(nr, sym, qual) [nr] = 1,
static char syscalls_ia32[] = {
#include <asm/syscalls_32.h>
};
#undef __SYSCALL_I386

#if defined(CONFIG_KVM_GUEST) && defined(CONFIG_PARAVIRT_SPINLOCKS)
#include <asm/kvm_para.h>
#endif

int main(void)
{
#ifdef CONFIG_PARAVIRT
#ifdef CONFIG_PARAVIRT_XXL
	OFFSET(PV_CPU_usergs_sysret64, paravirt_patch_template,
	       cpu.usergs_sysret64);
	OFFSET(PV_CPU_swapgs, paravirt_patch_template, cpu.swapgs);
#ifdef CONFIG_DEBUG_ENTRY
	OFFSET(PV_IRQ_save_fl, paravirt_patch_template, irq.save_fl);
#endif
#endif
	BLANK();
#endif

#if defined(CONFIG_KVM_GUEST) && defined(CONFIG_PARAVIRT_SPINLOCKS)
	OFFSET(KVM_STEAL_TIME_preempted, kvm_steal_time, preempted);
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
	ENTRY(gdt_desc);
	BLANK();
#undef ENTRY

	OFFSET(TSS_ist, tss_struct, x86_tss.ist);
	DEFINE(DB_STACK_OFFSET, offsetof(struct cea_exception_stacks, DB_stack) -
	       offsetof(struct cea_exception_stacks, DB1_stack));
	BLANK();

#ifdef CONFIG_STACKPROTECTOR
	DEFINE(stack_canary_offset, offsetof(struct fixed_percpu_data, stack_canary));
	BLANK();
#endif

	DEFINE(__NR_syscall_max, sizeof(syscalls_64) - 1);
	DEFINE(NR_syscalls, sizeof(syscalls_64));

#ifdef CONFIG_X86_X32_ABI
	DEFINE(__NR_syscall_x32_max, sizeof(syscalls_x32) - 1);
	DEFINE(X32_NR_syscalls, sizeof(syscalls_x32));
#endif

	DEFINE(__NR_syscall_compat_max, sizeof(syscalls_ia32) - 1);
	DEFINE(IA32_NR_syscalls, sizeof(syscalls_ia32));

	return 0;
}
