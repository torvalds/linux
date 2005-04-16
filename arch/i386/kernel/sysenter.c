/*
 * linux/arch/i386/kernel/sysenter.c
 *
 * (C) Copyright 2002 Linus Torvalds
 *
 * This file contains the needed initializations to support sysenter.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/thread_info.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/elf.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>

extern asmlinkage void sysenter_entry(void);

void enable_sep_cpu(void *info)
{
	int cpu = get_cpu();
	struct tss_struct *tss = &per_cpu(init_tss, cpu);

	tss->ss1 = __KERNEL_CS;
	tss->esp1 = sizeof(struct tss_struct) + (unsigned long) tss;
	wrmsr(MSR_IA32_SYSENTER_CS, __KERNEL_CS, 0);
	wrmsr(MSR_IA32_SYSENTER_ESP, tss->esp1, 0);
	wrmsr(MSR_IA32_SYSENTER_EIP, (unsigned long) sysenter_entry, 0);
	put_cpu();	
}

/*
 * These symbols are defined by vsyscall.o to mark the bounds
 * of the ELF DSO images included therein.
 */
extern const char vsyscall_int80_start, vsyscall_int80_end;
extern const char vsyscall_sysenter_start, vsyscall_sysenter_end;

static int __init sysenter_setup(void)
{
	void *page = (void *)get_zeroed_page(GFP_ATOMIC);

	__set_fixmap(FIX_VSYSCALL, __pa(page), PAGE_READONLY_EXEC);

	if (!boot_cpu_has(X86_FEATURE_SEP)) {
		memcpy(page,
		       &vsyscall_int80_start,
		       &vsyscall_int80_end - &vsyscall_int80_start);
		return 0;
	}

	memcpy(page,
	       &vsyscall_sysenter_start,
	       &vsyscall_sysenter_end - &vsyscall_sysenter_start);

	on_each_cpu(enable_sep_cpu, NULL, 1, 1);
	return 0;
}

__initcall(sysenter_setup);
