/*
 * Copyright (C) 2014-2016 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sort.h>

struct plt_entry {
	/*
	 * A program that conforms to the AArch64 Procedure Call Standard
	 * (AAPCS64) must assume that a veneer that alters IP0 (x16) and/or
	 * IP1 (x17) may be inserted at any branch instruction that is
	 * exposed to a relocation that supports long branches. Since that
	 * is exactly what we are dealing with here, we are free to use x16
	 * as a scratch register in the PLT veneers.
	 */
	__le32	mov0;	/* movn	x16, #0x....			*/
	__le32	mov1;	/* movk	x16, #0x...., lsl #16		*/
	__le32	mov2;	/* movk	x16, #0x...., lsl #32		*/
	__le32	br;	/* br	x16				*/
};

u64 module_emit_plt_entry(struct module *mod, const Elf64_Rela *rela,
			  Elf64_Sym *sym)
{
	struct plt_entry *plt = (struct plt_entry *)mod->arch.plt->sh_addr;
	int i = mod->arch.plt_num_entries;
	u64 val = sym->st_value + rela->r_addend;

	/*
	 * We only emit PLT entries against undefined (SHN_UNDEF) symbols,
	 * which are listed in the ELF symtab section, but without a type
	 * or a size.
	 * So, similar to how the module loader uses the Elf64_Sym::st_value
	 * field to store the resolved addresses of undefined symbols, let's
	 * borrow the Elf64_Sym::st_size field (whose value is never used by
	 * the module loader, even for symbols that are defined) to record
	 * the address of a symbol's associated PLT entry as we emit it for a
	 * zero addend relocation (which is the only kind we have to deal with
	 * in practice). This allows us to find duplicates without having to
	 * go through the table every time.
	 */
	if (rela->r_addend == 0 && sym->st_size != 0) {
		BUG_ON(sym->st_size < (u64)plt || sym->st_size >= (u64)&plt[i]);
		return sym->st_size;
	}

	mod->arch.plt_num_entries++;
	BUG_ON(mod->arch.plt_num_entries > mod->arch.plt_max_entries);

	/*
	 * MOVK/MOVN/MOVZ opcode:
	 * +--------+------------+--------+-----------+-------------+---------+
	 * | sf[31] | opc[30:29] | 100101 | hw[22:21] | imm16[20:5] | Rd[4:0] |
	 * +--------+------------+--------+-----------+-------------+---------+
	 *
	 * Rd     := 0x10 (x16)
	 * hw     := 0b00 (no shift), 0b01 (lsl #16), 0b10 (lsl #32)
	 * opc    := 0b11 (MOVK), 0b00 (MOVN), 0b10 (MOVZ)
	 * sf     := 1 (64-bit variant)
	 */
	plt[i] = (struct plt_entry){
		cpu_to_le32(0x92800010 | (((~val      ) & 0xffff)) << 5),
		cpu_to_le32(0xf2a00010 | ((( val >> 16) & 0xffff)) << 5),
		cpu_to_le32(0xf2c00010 | ((( val >> 32) & 0xffff)) << 5),
		cpu_to_le32(0xd61f0200)
	};

	if (rela->r_addend == 0)
		sym->st_size = (u64)&plt[i];

	return (u64)&plt[i];
}

#define cmp_3way(a,b)	((a) < (b) ? -1 : (a) > (b))

static int cmp_rela(const void *a, const void *b)
{
	const Elf64_Rela *x = a, *y = b;
	int i;

	/* sort by type, symbol index and addend */
	i = cmp_3way(ELF64_R_TYPE(x->r_info), ELF64_R_TYPE(y->r_info));
	if (i == 0)
		i = cmp_3way(ELF64_R_SYM(x->r_info), ELF64_R_SYM(y->r_info));
	if (i == 0)
		i = cmp_3way(x->r_addend, y->r_addend);
	return i;
}

static bool duplicate_rel(const Elf64_Rela *rela, int num)
{
	/*
	 * Entries are sorted by type, symbol index and addend. That means
	 * that, if a duplicate entry exists, it must be in the preceding
	 * slot.
	 */
	return num > 0 && cmp_rela(rela + num, rela + num - 1) == 0;
}

static unsigned int count_plts(Elf64_Sym *syms, Elf64_Rela *rela, int num)
{
	unsigned int ret = 0;
	Elf64_Sym *s;
	int i;

	for (i = 0; i < num; i++) {
		switch (ELF64_R_TYPE(rela[i].r_info)) {
		case R_AARCH64_JUMP26:
		case R_AARCH64_CALL26:
			/*
			 * We only have to consider branch targets that resolve
			 * to undefined symbols. This is not simply a heuristic,
			 * it is a fundamental limitation, since the PLT itself
			 * is part of the module, and needs to be within 128 MB
			 * as well, so modules can never grow beyond that limit.
			 */
			s = syms + ELF64_R_SYM(rela[i].r_info);
			if (s->st_shndx != SHN_UNDEF)
				break;

			/*
			 * Jump relocations with non-zero addends against
			 * undefined symbols are supported by the ELF spec, but
			 * do not occur in practice (e.g., 'jump n bytes past
			 * the entry point of undefined function symbol f').
			 * So we need to support them, but there is no need to
			 * take them into consideration when trying to optimize
			 * this code. So let's only check for duplicates when
			 * the addend is zero: this allows us to record the PLT
			 * entry address in the symbol table itself, rather than
			 * having to search the list for duplicates each time we
			 * emit one.
			 */
			if (rela[i].r_addend != 0 || !duplicate_rel(rela, i))
				ret++;
			break;
		}
	}
	return ret;
}

int module_frob_arch_sections(Elf_Ehdr *ehdr, Elf_Shdr *sechdrs,
			      char *secstrings, struct module *mod)
{
	unsigned long plt_max_entries = 0;
	Elf64_Sym *syms = NULL;
	int i;

	/*
	 * Find the empty .plt section so we can expand it to store the PLT
	 * entries. Record the symtab address as well.
	 */
	for (i = 0; i < ehdr->e_shnum; i++) {
		if (strcmp(".plt", secstrings + sechdrs[i].sh_name) == 0)
			mod->arch.plt = sechdrs + i;
		else if (sechdrs[i].sh_type == SHT_SYMTAB)
			syms = (Elf64_Sym *)sechdrs[i].sh_addr;
	}

	if (!mod->arch.plt) {
		pr_err("%s: module PLT section missing\n", mod->name);
		return -ENOEXEC;
	}
	if (!syms) {
		pr_err("%s: module symtab section missing\n", mod->name);
		return -ENOEXEC;
	}

	for (i = 0; i < ehdr->e_shnum; i++) {
		Elf64_Rela *rels = (void *)ehdr + sechdrs[i].sh_offset;
		int numrels = sechdrs[i].sh_size / sizeof(Elf64_Rela);
		Elf64_Shdr *dstsec = sechdrs + sechdrs[i].sh_info;

		if (sechdrs[i].sh_type != SHT_RELA)
			continue;

		/* ignore relocations that operate on non-exec sections */
		if (!(dstsec->sh_flags & SHF_EXECINSTR))
			continue;

		/* sort by type, symbol index and addend */
		sort(rels, numrels, sizeof(Elf64_Rela), cmp_rela, NULL);

		plt_max_entries += count_plts(syms, rels, numrels);
	}

	mod->arch.plt->sh_type = SHT_NOBITS;
	mod->arch.plt->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	mod->arch.plt->sh_addralign = L1_CACHE_BYTES;
	mod->arch.plt->sh_size = plt_max_entries * sizeof(struct plt_entry);
	mod->arch.plt_num_entries = 0;
	mod->arch.plt_max_entries = plt_max_entries;
	return 0;
}
