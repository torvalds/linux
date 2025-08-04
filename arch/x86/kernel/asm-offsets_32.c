// SPDX-License-Identifier: GPL-2.0
#ifndef __LINUX_KBUILD_H
# error "Please do not build this file directly, build asm-offsets.c instead"
#endif

#include <linux/efi.h>

#include <asm/ucontext.h>

/* workaround for a warning with -Wmissing-prototypes */
void foo(void);

void foo(void)
{
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

	/*
	 * Offset from the entry stack to task stack stored in TSS. Kernel entry
	 * happens on the per-cpu entry-stack, and the asm code switches to the
	 * task-stack pointer stored in x86_tss.sp1, which is a copy of
	 * task->thread.sp0 where entry code can find it.
	 */
	DEFINE(TSS_entry2task_stack,
	       offsetof(struct cpu_entry_area, tss.x86_tss.sp1) -
	       offsetofend(struct cpu_entry_area, entry_stack_page.stack));

	BLANK();
	DEFINE(EFI_svam, offsetof(efi_runtime_services_t, set_virtual_address_map));
}
