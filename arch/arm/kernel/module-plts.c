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

static bool in_init(const struct module *mod, u32 addr)
{
	return addr - (u32)mod->init_layout.base < mod->init_layout.size;
}

u32 get_module_plt(struct module *mod, unsigned long loc, Elf32_Addr val)
{
	struct plt_entries *plt, *plt_end;
	int c, *count;

	if (in_init(mod, loc)) {
		plt = (void *)mod->arch.init_plt->sh_addr;
		plt_end = (void *)plt + mod->arch.init_plt->sh_size;
		count = &mod->arch.init_plt_count;
	} else {
		plt = (void *)mod->arch.core_plt->sh_addr;
		plt_end = (void *)plt + mod->arch.core_plt->sh_size;
		count = &mod->arch.core_plt_count;
	}

	/* Look for an existing entry pointing to 'val' */
	for (c = *count; plt < plt_end; c -= PLT_ENT_COUNT, plt++) {
		int i;

		if (!c) {
			/* Populate a new set of entries */
			*plt = (struct plt_entries){
				{ [0 ... PLT_ENT_COUNT - 1] = PLT_ENT_LDR, },
				{ val, }
			};
			++*count;
			return (u32)plt->ldr;
		}
		for (i = 0; i < PLT_ENT_COUNT; i++) {
			if (!plt->lit[i]) {
				plt->lit[i] = val;
				++*count;
			}
			if (plt->lit[i] == val)
				return (u32)&plt->ldr[i];
		}
	}
	BUG();
}

static int duplicate_rel(Elf32_Addr base, const Elf32_Rel *rel, int num,
			   u32 mask)
{
	u32 *loc1, *loc2;
	int i;

	for (i = 0; i < num; i++) {
		if (rel[i].r_info != rel[num].r_info)
			continue;

		/*
		 * Identical relocation types against identical symbols can
		 * still result in different PLT entries if the addend in the
		 * place is different. So resolve the target of the relocation
		 * to compare the values.
		 */
		loc1 = (u32 *)(base + rel[i].r_offset);
		loc2 = (u32 *)(base + rel[num].r_offset);
		if (((*loc1 ^ *loc2) & mask) == 0)
			return 1;
	}
	return 0;
}

/* Count how many PLT entries we may need */
static unsigned int count_plts(Elf32_Addr base, const Elf32_Rel *rel, int num)
{
	unsigned int ret = 0;
	int i;

	/*
	 * Sure, this is order(n^2), but it's usually short, and not
	 * time critical
	 */
	for (i = 0; i < num; i++)
		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_ARM_CALL:
		case R_ARM_PC24:
		case R_ARM_JUMP24:
			if (!duplicate_rel(base, rel, i,
					   __opcode_to_mem_arm(0x00ffffff)))
				ret++;
			break;
#ifdef CONFIG_THUMB2_KERNEL
		case R_ARM_THM_CALL:
		case R_ARM_THM_JUMP24:
			if (!duplicate_rel(base, rel, i,
					   __opcode_to_mem_thumb32(0x07ff2fff)))
				ret++;
#endif
		}
	return ret;
}

int module_frob_arch_sections(Elf_Ehdr *ehdr, Elf_Shdr *sechdrs,
			      char *secstrings, struct module *mod)
{
	unsigned long core_plts = 0, init_plts = 0;
	Elf32_Shdr *s, *sechdrs_end = sechdrs + ehdr->e_shnum;

	/*
	 * To store the PLTs, we expand the .text section for core module code
	 * and the .init.text section for initialization code.
	 */
	for (s = sechdrs; s < sechdrs_end; ++s)
		if (strcmp(".core.plt", secstrings + s->sh_name) == 0)
			mod->arch.core_plt = s;
		else if (strcmp(".init.plt", secstrings + s->sh_name) == 0)
			mod->arch.init_plt = s;

	if (!mod->arch.core_plt || !mod->arch.init_plt) {
		pr_err("%s: sections missing\n", mod->name);
		return -ENOEXEC;
	}

	for (s = sechdrs + 1; s < sechdrs_end; ++s) {
		const Elf32_Rel *rels = (void *)ehdr + s->sh_offset;
		int numrels = s->sh_size / sizeof(Elf32_Rel);
		Elf32_Shdr *dstsec = sechdrs + s->sh_info;

		if (s->sh_type != SHT_REL)
			continue;

		if (strstr(secstrings + s->sh_name, ".init"))
			init_plts += count_plts(dstsec->sh_addr, rels, numrels);
		else
			core_plts += count_plts(dstsec->sh_addr, rels, numrels);
	}

	mod->arch.core_plt->sh_type = SHT_NOBITS;
	mod->arch.core_plt->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	mod->arch.core_plt->sh_addralign = L1_CACHE_BYTES;
	mod->arch.core_plt->sh_size = round_up(core_plts * PLT_ENT_SIZE,
					       sizeof(struct plt_entries));
	mod->arch.core_plt_count = 0;

	mod->arch.init_plt->sh_type = SHT_NOBITS;
	mod->arch.init_plt->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	mod->arch.init_plt->sh_addralign = L1_CACHE_BYTES;
	mod->arch.init_plt->sh_size = round_up(init_plts * PLT_ENT_SIZE,
					       sizeof(struct plt_entries));
	mod->arch.init_plt_count = 0;
	pr_debug("%s: core.plt=%x, init.plt=%x\n", __func__,
		 mod->arch.core_plt->sh_size, mod->arch.init_plt->sh_size);
	return 0;
}
