/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_MODULE_H
#define _ASM_S390_MODULE_H

#include <asm-generic/module.h>

/*
 * This file contains the s390 architecture specific module code.
 */

struct mod_arch_syminfo {
	unsigned long got_offset;
	unsigned long plt_offset;
	int got_initialized;
	int plt_initialized;
};

struct mod_arch_specific {
	/* Starting offset of got in the module core memory. */
	unsigned long got_offset;
	/* Starting offset of plt in the module core memory. */
	unsigned long plt_offset;
	/* Size of the got. */
	unsigned long got_size;
	/* Size of the plt. */
	unsigned long plt_size;
	/* Number of symbols in syminfo. */
	int nsyms;
	/* Additional symbol information (got and plt offsets). */
	struct mod_arch_syminfo *syminfo;
#ifdef CONFIG_FUNCTION_TRACER
	/* Start of memory reserved for ftrace hotpatch trampolines. */
	struct ftrace_hotpatch_trampoline *trampolines_start;
	/* End of memory reserved for ftrace hotpatch trampolines. */
	struct ftrace_hotpatch_trampoline *trampolines_end;
	/* Next unused ftrace hotpatch trampoline slot. */
	struct ftrace_hotpatch_trampoline *next_trampoline;
#endif /* CONFIG_FUNCTION_TRACER */
};

static inline const Elf_Shdr *find_section(const Elf_Ehdr *hdr,
					   const Elf_Shdr *sechdrs,
					   const char *name)
{
	const char *secstrs = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	const Elf_Shdr *s, *se;

	for (s = sechdrs, se = sechdrs + hdr->e_shnum; s < se; s++) {
		if (strcmp(name, secstrs + s->sh_name) == 0)
			return s;
	}
	return NULL;
}

#endif /* _ASM_S390_MODULE_H */
