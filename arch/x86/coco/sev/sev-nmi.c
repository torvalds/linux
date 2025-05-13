// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2019 SUSE
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#define pr_fmt(fmt)	"SEV: " fmt

#include <linux/bug.h>
#include <linux/kernel.h>

#include <asm/cpu_entry_area.h>
#include <asm/msr.h>
#include <asm/ptrace.h>
#include <asm/sev.h>
#include <asm/sev-internal.h>

static __always_inline bool on_vc_stack(struct pt_regs *regs)
{
	unsigned long sp = regs->sp;

	/* User-mode RSP is not trusted */
	if (user_mode(regs))
		return false;

	/* SYSCALL gap still has user-mode RSP */
	if (ip_within_syscall_gap(regs))
		return false;

	return ((sp >= __this_cpu_ist_bottom_va(VC)) && (sp < __this_cpu_ist_top_va(VC)));
}

/*
 * This function handles the case when an NMI is raised in the #VC
 * exception handler entry code, before the #VC handler has switched off
 * its IST stack. In this case, the IST entry for #VC must be adjusted,
 * so that any nested #VC exception will not overwrite the stack
 * contents of the interrupted #VC handler.
 *
 * The IST entry is adjusted unconditionally so that it can be also be
 * unconditionally adjusted back in __sev_es_ist_exit(). Otherwise a
 * nested sev_es_ist_exit() call may adjust back the IST entry too
 * early.
 *
 * The __sev_es_ist_enter() and __sev_es_ist_exit() functions always run
 * on the NMI IST stack, as they are only called from NMI handling code
 * right now.
 */
void noinstr __sev_es_ist_enter(struct pt_regs *regs)
{
	unsigned long old_ist, new_ist;

	/* Read old IST entry */
	new_ist = old_ist = __this_cpu_read(cpu_tss_rw.x86_tss.ist[IST_INDEX_VC]);

	/*
	 * If NMI happened while on the #VC IST stack, set the new IST
	 * value below regs->sp, so that the interrupted stack frame is
	 * not overwritten by subsequent #VC exceptions.
	 */
	if (on_vc_stack(regs))
		new_ist = regs->sp;

	/*
	 * Reserve additional 8 bytes and store old IST value so this
	 * adjustment can be unrolled in __sev_es_ist_exit().
	 */
	new_ist -= sizeof(old_ist);
	*(unsigned long *)new_ist = old_ist;

	/* Set new IST entry */
	this_cpu_write(cpu_tss_rw.x86_tss.ist[IST_INDEX_VC], new_ist);
}

void noinstr __sev_es_ist_exit(void)
{
	unsigned long ist;

	/* Read IST entry */
	ist = __this_cpu_read(cpu_tss_rw.x86_tss.ist[IST_INDEX_VC]);

	if (WARN_ON(ist == __this_cpu_ist_top_va(VC)))
		return;

	/* Read back old IST entry and write it to the TSS */
	this_cpu_write(cpu_tss_rw.x86_tss.ist[IST_INDEX_VC], *(unsigned long *)ist);
}

void noinstr __sev_es_nmi_complete(void)
{
	struct ghcb_state state;
	struct ghcb *ghcb;

	ghcb = __sev_get_ghcb(&state);

	vc_ghcb_invalidate(ghcb);
	ghcb_set_sw_exit_code(ghcb, SVM_VMGEXIT_NMI_COMPLETE);
	ghcb_set_sw_exit_info_1(ghcb, 0);
	ghcb_set_sw_exit_info_2(ghcb, 0);

	sev_es_wr_ghcb_msr(__pa_nodebug(ghcb));
	VMGEXIT();

	__sev_put_ghcb(&state);
}
