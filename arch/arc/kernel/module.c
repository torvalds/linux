/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/kernel.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <asm/unwind.h>

static inline void arc_write_me(unsigned short *addr, unsigned long value)
{
	*addr = (value & 0xffff0000) >> 16;
	*(addr + 1) = (value & 0xffff);
}

/*
 * This gets called before relocation loop in generic loader
 * Make a note of the section index of unwinding section
 */
int module_frob_arch_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
			      char *secstr, struct module *mod)
{
#ifdef CONFIG_ARC_DW2_UNWIND
	mod->arch.unw_sec_idx = 0;
	mod->arch.unw_info = NULL;
	mod->arch.secstr = secstr;
#endif
	return 0;
}

void module_arch_cleanup(struct module *mod)
{
#ifdef CONFIG_ARC_DW2_UNWIND
	if (mod->arch.unw_info)
		unwind_remove_table(mod->arch.unw_info, 0);
#endif
}

int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,	/* sec index for sym tbl */
		       unsigned int relsec,	/* sec index for relo sec */
		       struct module *module)
{
	int i, n, relo_type;
	Elf32_Rela *rel_entry = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym_entry, *sym_sec;
	Elf32_Addr relocation, location, tgt_addr;
	unsigned int tgtsec;

	/*
	 * @relsec has relocations e.g. .rela.init.text
	 * @tgtsec is section to patch e.g. .init.text
	 */
	tgtsec = sechdrs[relsec].sh_info;
	tgt_addr = sechdrs[tgtsec].sh_addr;
	sym_sec = (Elf32_Sym *) sechdrs[symindex].sh_addr;
	n = sechdrs[relsec].sh_size / sizeof(*rel_entry);

	pr_debug("\nSection to fixup %s @%x\n",
		 module->arch.secstr + sechdrs[tgtsec].sh_name, tgt_addr);
	pr_debug("=========================================================\n");
	pr_debug("r_off\tr_add\tst_value ADDRESS  VALUE\n");
	pr_debug("=========================================================\n");

	/* Loop thru entries in relocation section */
	for (i = 0; i < n; i++) {
		const char *s;

		/* This is where to make the change */
		location = tgt_addr + rel_entry[i].r_offset;

		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym_entry = sym_sec + ELF32_R_SYM(rel_entry[i].r_info);

		relocation = sym_entry->st_value + rel_entry[i].r_addend;

		if (sym_entry->st_name == 0 && ELF_ST_TYPE (sym_entry->st_info) == STT_SECTION) {
			s = module->arch.secstr + sechdrs[sym_entry->st_shndx].sh_name;
		} else {
			s = strtab + sym_entry->st_name;
		}

		pr_debug("   %x\t%x\t%x %x %x [%s]\n",
			 rel_entry[i].r_offset, rel_entry[i].r_addend,
			 sym_entry->st_value, location, relocation, s);

		/* This assumes modules are built with -mlong-calls
		 * so any branches/jumps are absolute 32 bit jmps
		 * global data access again is abs 32 bit.
		 * Both of these are handled by same relocation type
		 */
		relo_type = ELF32_R_TYPE(rel_entry[i].r_info);

		if (likely(R_ARC_32_ME == relo_type))	/* ME ( S + A ) */
			arc_write_me((unsigned short *)location, relocation);
		else if (R_ARC_32 == relo_type)		/* ( S + A ) */
			*((Elf32_Addr *) location) = relocation;
		else if (R_ARC_32_PCREL == relo_type)	/* ( S + A ) - PDATA ) */
			*((Elf32_Addr *) location) = relocation - location;
		else
			goto relo_err;

	}

	if (strcmp(module->arch.secstr+sechdrs[tgtsec].sh_name, ".eh_frame") == 0)
		module->arch.unw_sec_idx = tgtsec;

	return 0;

relo_err:
	pr_err("%s: unknown relocation: %u\n",
		module->name, ELF32_R_TYPE(rel_entry[i].r_info));
	return -ENOEXEC;

}

/* Just before lift off: After sections have been relocated, we add the
 * dwarf section to unwinder table pool
 * This couldn't be done in module_frob_arch_sections() because
 * relocations had not been applied by then
 */
int module_finalize(const Elf32_Ehdr *hdr, const Elf_Shdr *sechdrs,
		    struct module *mod)
{
#ifdef CONFIG_ARC_DW2_UNWIND
	void *unw;
	int unwsec = mod->arch.unw_sec_idx;

	if (unwsec) {
		unw = unwind_add_table(mod, (void *)sechdrs[unwsec].sh_addr,
				       sechdrs[unwsec].sh_size);
		mod->arch.unw_info = unw;
	}
#endif
	return 0;
}
