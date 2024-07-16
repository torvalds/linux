// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/mm.h>
#include <asm/current.h>
#include <asm/traps.h>
#include <asm/vdso.h>

struct vdso_exception_table_entry {
	int insn, fixup;
};

bool fixup_vdso_exception(struct pt_regs *regs, int trapnr,
			  unsigned long error_code, unsigned long fault_addr)
{
	const struct vdso_image *image = current->mm->context.vdso_image;
	const struct vdso_exception_table_entry *extable;
	unsigned int nr_entries, i;
	unsigned long base;

	/*
	 * Do not attempt to fixup #DB or #BP.  It's impossible to identify
	 * whether or not a #DB/#BP originated from within an SGX enclave and
	 * SGX enclaves are currently the only use case for vDSO fixup.
	 */
	if (trapnr == X86_TRAP_DB || trapnr == X86_TRAP_BP)
		return false;

	if (!current->mm->context.vdso)
		return false;

	base =  (unsigned long)current->mm->context.vdso + image->extable_base;
	nr_entries = image->extable_len / (sizeof(*extable));
	extable = image->extable;

	for (i = 0; i < nr_entries; i++) {
		if (regs->ip == base + extable[i].insn) {
			regs->ip = base + extable[i].fixup;
			regs->di = trapnr;
			regs->si = error_code;
			regs->dx = fault_addr;
			return true;
		}
	}

	return false;
}
