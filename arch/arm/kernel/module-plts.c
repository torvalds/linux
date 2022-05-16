// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sort.h>
#include <linux/moduleloader.h>

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

static bool in_init(const struct module *mod, unsigned long loc)
{
	return loc - (u32)mod->init_layout.base < mod->init_layout.size;
}

u32 get_module_plt(struct module *mod, unsigned long loc, Elf32_Addr val)
{
	struct mod_plt_sec *pltsec = !in_init(mod, loc) ? &mod->arch.core :
							  &mod->arch.init;

	struct plt_entries *plt = (struct plt_entries *)pltsec->plt->sh_addr;
	int idx = 0;

	/*
	 * Look for an existing entry pointing to 'val'. Given that the
	 * relocations are sorted, this will be the last entry we allocated.
	 * (if one exists).
	 */
	if (pltsec->plt_count > 0) {
		plt += (pltsec->plt_count - 1) / PLT_ENT_COUNT;
		idx = (pltsec->plt_count - 1) % PLT_ENT_COUNT;

		if (plt->lit[idx] == val)
			return (u32)&plt->ldr[idx];

		idx = (idx + 1) % PLT_ENT_COUNT;
		if (!idx)
			plt++;
	}

	pltsec->plt_count++;
	BUG_ON(pltsec->plt_count * PLT_ENT_SIZE > pltsec->plt->sh_size);

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
			       const Elf32_Rel *rel, int num, Elf32_Word dstidx)
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
			 * to symbols that are defined in a different section.
			 * This is not simply a heuristic, it is a fundamental
			 * limitation, since there is no guaranteed way to emit
			 * PLT entries sufficiently close to the branch if the
			 * section size exceeds the range of a branch
			 * instruction. So ignore relocations against defined
			 * symbols if they live in the same section as the
			 * relocation target.
			 */
			s = syms + ELF32_R_SYM(rel[i].r_info);
			if (s->st_shndx == dstidx)
				break;

			/*
			 * Jump relocations with non-zero addends against
			 * undefined symbols are supported by the ELF spec, but
			 * do not occur in practice (e.g., 'jump n bytes past
			 * the entry point of undefined function symbol f').
			 * So we need to support them, but there is no need to
			 * take them into consideration when trying to optimize
			 * this code. So let's only check for duplicates when
			 * the addend is zero. (Note that calls into the core
			 * module via init PLT entries could involve section
			 * relative symbol references with non-zero addends, for
			 * which we may end up emitting duplicates, but the init
			 * PLT is released along with the rest of the .init
			 * region as soon as module loading completes.)
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
	unsigned long core_plts = 0;
	unsigned long init_plts = 0;
	Elf32_Shdr *s, *sechdrs_end = sechdrs + ehdr->e_shnum;
	Elf32_Sym *syms = NULL;

	/*
	 * To store the PLTs, we expand the .text section for core module code
	 * and for initialization code.
	 */
	for (s = sechdrs; s < sechdrs_end; ++s) {
		if (strcmp(".plt", secstrings + s->sh_name) == 0)
			mod->arch.core.plt = s;
		else if (strcmp(".init.plt", secstrings + s->sh_name) == 0)
			mod->arch.init.plt = s;
		else if (s->sh_type == SHT_SYMTAB)
			syms = (Elf32_Sym *)s->sh_addr;
	}

	if (!mod->arch.core.plt || !mod->arch.init.plt) {
		pr_err("%s: module PLT section(s) missing\n", mod->name);
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

		if (strncmp(secstrings + dstsec->sh_name, ".init", 5) != 0)
			core_plts += count_plts(syms, dstsec->sh_addr, rels,
						numrels, s->sh_info);
		else
			init_plts += count_plts(syms, dstsec->sh_addr, rels,
						numrels, s->sh_info);
	}

	mod->arch.core.plt->sh_type = SHT_NOBITS;
	mod->arch.core.plt->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	mod->arch.core.plt->sh_addralign = L1_CACHE_BYTES;
	mod->arch.core.plt->sh_size = round_up(core_plts * PLT_ENT_SIZE,
					       sizeof(struct plt_entries));
	mod->arch.core.plt_count = 0;

	mod->arch.init.plt->sh_type = SHT_NOBITS;
	mod->arch.init.plt->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	mod->arch.init.plt->sh_addralign = L1_CACHE_BYTES;
	mod->arch.init.plt->sh_size = round_up(init_plts * PLT_ENT_SIZE,
					       sizeof(struct plt_entries));
	mod->arch.init.plt_count = 0;

	pr_debug("%s: plt=%x, init.plt=%x\n", __func__,
		 mod->arch.core.plt->sh_size, mod->arch.init.plt->sh_size);
	return 0;
}
