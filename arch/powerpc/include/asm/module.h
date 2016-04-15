#ifndef _ASM_POWERPC_MODULE_H
#define _ASM_POWERPC_MODULE_H
#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/list.h>
#include <asm/bug.h>
#include <asm-generic/module.h>


#ifndef __powerpc64__
/*
 * Thanks to Paul M for explaining this.
 *
 * PPC can only do rel jumps += 32MB, and often the kernel and other
 * modules are furthur away than this.  So, we jump to a table of
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
#ifdef CONFIG_DYNAMIC_FTRACE
	unsigned long toc;
	unsigned long tramp;
#endif

#else /* powerpc64 */
	/* Indices of PLT sections within module. */
	unsigned int core_plt_section;
	unsigned int init_plt_section;
#ifdef CONFIG_DYNAMIC_FTRACE
	unsigned long tramp;
#endif
#endif /* powerpc64 */

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
#endif

int module_trampoline_target(struct module *mod, unsigned long trampoline,
			     unsigned long *target);

#ifdef CONFIG_DYNAMIC_FTRACE
int module_finalize_ftrace(struct module *mod, const Elf_Shdr *sechdrs);
#else
static inline int module_finalize_ftrace(struct module *mod, const Elf_Shdr *sechdrs)
{
	return 0;
}
#endif

struct exception_table_entry;
void sort_ex_table(struct exception_table_entry *start,
		   struct exception_table_entry *finish);

#if defined(CONFIG_MODVERSIONS) && defined(CONFIG_PPC64)
#define ARCH_RELOCATES_KCRCTAB
#define reloc_start PHYSICAL_START
#endif
#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_MODULE_H */
