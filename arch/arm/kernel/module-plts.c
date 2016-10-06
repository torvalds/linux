/*
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sort.h>

#include <asm/cache.h>
#include <asm/opcodes.h>

#define PLT_ENT_STRIDE		L1_CACHE_BYTES
#define PLT_ENT_COUNT		(PLT_ENT_STRIDE / sizeof(u32))
#define PLT_ENT_SIZE		(sizeof(struct plt_entries) / PLT_ENT_COUNT)

#ifdef CONFIG_THUMB2_KERNEL
#define PLT_ENT_LDR		__opcode_to_mem_thumb32(0xf8dff000 | \
							(PLT_ENT_STRIDE - 4))
#else
#define PLT_ENT_LDR		__opcode_to_mem_arm(0xe59ff000 | \
						    (PLT_ENT_STRIDE - 8))
#endif

struct plt_entries {
	u32	ldr[PLT_ENT_COUNT];
	u32	lit[PLT_ENT_COUNT];
};

u32 get_module_plt(struct module *mod, unsigned long loc, Elf32_Addr val)
{
	struct plt_entries *plt = (struct plt_entries *)mod->arch.plt->sh_addr;
	int idx = 0;

	/*
	 * Look for an existing entry pointing to 'val'. Given that the
	 * relocations are sorted, this will be the last entry we allocated.
	 * (if one exists).
	 */
	if (mod->arch.plt_count > 0) {
		plt += (mod->arch.plt_count - 1) / PLT_ENT_COUNT;
		idx = (mod->arch.plt_count - 1) % PLT_ENT_COUNT;

		if (plt->lit[idx] == val)
			return (u32)&plt->ldr[idx];

		idx = (idx + 1) % PLT_ENT_COUNT;
		if (!idx)
			plt++;
	}

	mod->arch.plt_count++;
	BUG_ON(mod->arch.plt_count * PLT_ENT_SIZE > mod->arch.plt->sh_size);

	if (!idx)
		/* Populate a new set of entries */
		*plt = (struct plt_entries){
			{ [0 ... PLT_ENT_COUNT - 1] = PLT_ENT_LDR, },
			{ val, }
		};
	else
		plt->lit[idx] = val;

	return (u32)&plt->ldr[idx];
}

#define cmp_3way(a,b)	((a) < (b) ? -1 : (a) > (b))

static int cmp_rel(const void *a, const void *b)
{
	const Elf32_Rel *x = a, *y = b;
	int i;

	/* sort by type and symbol index */
	i = cmp_3way(ELF32_R_TYPE(x->r_info), ELF32_R_TYPE(y->r_info));
	if (i == 0)
		i = cmp_3way(ELF32_R_SYM(x->r_info), ELF32_R_SYM(y->r_info));
	return i;
}

static bool is_zero_addend_relocation(Elf32_Addr base, const Elf32_Rel *rel)
{
	u32 *tval = (u32 *)(base + rel->r_offset);

	/*
	 * Do a bitwise compare on the raw addend rather than fully decoding
	 * the offset and doing an arithmetic comparison.
	 * Note that a zero-addend jump/call relocation is encoded taking the
	 * PC bias into account, i.e., -8 for ARM and -4 for Thumb2.
	 */
	switch (ELF32_R_TYPE(rel->r_info)) {
		u16 upper, lower;

	case R_ARM_THM_CALL:
	case R_ARM_THM_JUMP24:
		upper = __mem_to_opcode_thumb16(((u16 *)tval)[0]);
		lower = __mem_to_opcode_thumb16(((u16 *)tval)[1]);

		return (upper & 0x7ff) == 0x7ff && (lower & 0x2fff) == 0x2ffe;

	case R_ARM_CALL:
	case R_ARM_PC24:
	case R_ARM_JUMP24:
		return (__mem_to_opcode_arm(*tval) & 0xffffff) == 0xfffffe;
	}
	BUG();
}

static bool duplicate_rel(Elf32_Addr base, const Elf32_Rel *rel, int num)
{
	const Elf32_Rel *prev;

	/*
	 * Entries are sorted by type and symbol index. That means that,
	 * if a duplicate entry exists, it must be in the preceding
	 * slot.
	 */
	if (!num)
		return false;

	prev = rel + num - 1;
	return cmp_rel(rel + num, prev) == 0 &&
	       is_zero_addend_relocation(base, prev);
}

/* Count how many PLT entries we may need */
static unsigned int count_plts(const Elf32_Sym *syms, Elf32_Addr base,
			       const Elf32_Rel *rel, int num)
{
	unsigned int ret = 0;
	const Elf32_Sym *s;
	int i;

	for (i = 0; i < num; i++) {
		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_ARM_CALL:
		case R_ARM_PC24:
		case R_ARM_JUMP24:
		case R_ARM_THM_CALL:
		case R_ARM_THM_JUMP24:
			/*
			 * We only have to consider branch targets that resolve
			 * to undefined symbols. This is not simply a heuristic,
			 * it is a fundamental limitation, since the PLT itself
			 * is part of the module, and needs to be within range
			 * as well, so modules can never grow beyond that limit.
			 */
			s = syms + ELF32_R_SYM(rel[i].r_info);
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
			 * the addend is zero.
			 */
			if (!is_zero_addend_relocation(base, rel + i) ||
			    !duplicate_rel(base, rel, i))
				ret++;
		}
	}
	return ret;
}

int module_frob_arch_sections(Elf_Ehdr *ehdr, Elf_Shdr *sechdrs,
			      char *secstrings, struct module *mod)
{
	unsigned long plts = 0;
	Elf32_Shdr *s, *sechdrs_end = sechdrs + ehdr->e_shnum;
	Elf32_Sym *syms = NULL;

	/*
	 * To store the PLTs, we expand the .text section for core module code
	 * and for initialization code.
	 */
	for (s = sechdrs; s < sechdrs_end; ++s) {
		if (strcmp(".plt", secstrings + s->sh_name) == 0)
			mod->arch.plt = s;
		else if (s->sh_type == SHT_SYMTAB)
			syms = (Elf32_Sym *)s->sh_addr;
	}

	if (!mod->arch.plt) {
		pr_err("%s: module PLT section missing\n", mod->name);
		return -ENOEXEC;
	}
	if (!syms) {
		pr_err("%s: module symtab section missing\n", mod->name);
		return -ENOEXEC;
	}

	for (s = sechdrs + 1; s < sechdrs_end; ++s) {
		Elf32_Rel *rels = (void *)ehdr + s->sh_offset;
		int numrels = s->sh_size / sizeof(Elf32_Rel);
		Elf32_Shdr *dstsec = sechdrs + s->sh_info;

		if (s->sh_type != SHT_REL)
			continue;

		/* ignore relocations that operate on non-exec sections */
		if (!(dstsec->sh_flags & SHF_EXECINSTR))
			continue;

		/* sort by type and symbol index */
		sort(rels, numrels, sizeof(Elf32_Rel), cmp_rel, NULL);

		plts += count_plts(syms, dstsec->sh_addr, rels, numrels);
	}

	mod->arch.plt->sh_type = SHT_NOBITS;
	mod->arch.plt->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	mod->arch.plt->sh_addralign = L1_CACHE_BYTES;
	mod->arch.plt->sh_size = round_up(plts * PLT_ENT_SIZE,
					  sizeof(struct plt_entries));
	mod->arch.plt_count = 0;

	pr_debug("%s: plt=%x\n", __func__, mod->arch.plt->sh_size);
	return 0;
}
