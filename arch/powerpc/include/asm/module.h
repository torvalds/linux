/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_MODULE_H
#define _ASM_POWERPC_MODULE_H
#ifdef __KERNEL__

#include <linux/list.h>
#include <asm/bug.h>
#include <asm-generic/module.h>

#ifndef __powerpc64__
/*
 * Thanks to Paul M for explaining this.
 *
 * PPC can only do rel jumps += 32MB, and often the kernel and other
 * modules are further away than this.  So, we jump to a table of
 * trampolines attached to the module (the Procedure Linkage Table)
 * whenever that happens.
 */

struct ppc_plt_entry {
	/* 16 byte jump instruction sequence (4 instructions) */
	unsigned int jump[4];
};
#endif	/* __powerpc64__ */


struct mod_arch_specific {
#ifdef __powerpc64__
	unsigned int stubs_section;	/* Index of stubs section in module */
	unsigned int toc_section;	/* What section is the TOC? */
	bool toc_fixed;			/* Have we fixed up .TOC.? */

	/* For module function descriptor dereference */
	unsigned long start_opd;
	unsigned long end_opd;
#else /* powerpc64 */
	/* Indices of PLT sections within module. */
	unsigned int core_plt_section;
	unsigned int init_plt_section;
#endif /* powerpc64 */

#ifdef CONFIG_DYNAMIC_FTRACE
	unsigned long tramp;
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	unsigned long tramp_regs;
#endif
#endif

	/* List of BUG addresses, source line numbers and filenames */
	struct list_head bug_list;
	struct bug_entry *bug_table;
	unsigned int num_bugs;
};

/*
 * Select ELF headers.
 * Make empty section for module_frob_arch_sections to expand.
 */

#ifdef __powerpc64__
#    ifdef MODULE
	asm(".section .stubs,\"ax\",@nobits; .align 3; .previous");
#    endif
#else
#    ifdef MODULE
	asm(".section .plt,\"ax\",@nobits; .align 3; .previous");
	asm(".section .init.plt,\"ax\",@nobits; .align 3; .previous");
#    endif	/* MODULE */
#endif

#ifdef CONFIG_DYNAMIC_FTRACE
#    ifdef MODULE
	asm(".section .ftrace.tramp,\"ax\",@nobits; .align 3; .previous");
#    endif	/* MODULE */

int module_trampoline_target(struct module *mod, unsigned long trampoline,
			     unsigned long *target);
int module_finalize_ftrace(struct module *mod, const Elf_Shdr *sechdrs);
#else
static inline int module_finalize_ftrace(struct module *mod, const Elf_Shdr *sechdrs)
{
	return 0;
}
#endif

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_MODULE_H */
