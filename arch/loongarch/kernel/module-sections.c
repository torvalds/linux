// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/ftrace.h>
#include <linux/sort.h>

Elf_Addr module_emit_got_entry(struct module *mod, Elf_Shdr *sechdrs, Elf_Addr val)
{
	struct mod_section *got_sec = &mod->arch.got;
	int i = got_sec->num_entries;
	struct got_entry *got = get_got_entry(val, sechdrs, got_sec);

	if (got)
		return (Elf_Addr)got;

	/* There is no GOT entry for val yet, create a new one. */
	got = (struct got_entry *)sechdrs[got_sec->shndx].sh_addr;
	got[i] = emit_got_entry(val);

	got_sec->num_entries++;
	if (got_sec->num_entries > got_sec->max_entries) {
		/*
		 * This may happen when the module contains a GOT_HI20 without
		 * a paired GOT_LO12. Such a module is broken, reject it.
		 */
		pr_err("%s: module contains bad GOT relocation\n", mod->name);
		return 0;
	}

	return (Elf_Addr)&got[i];
}

Elf_Addr module_emit_plt_entry(struct module *mod, Elf_Shdr *sechdrs, Elf_Addr val)
{
	int nr;
	struct mod_section *plt_sec = &mod->arch.plt;
	struct mod_section *plt_idx_sec = &mod->arch.plt_idx;
	struct plt_entry *plt = get_plt_entry(val, sechdrs, plt_sec, plt_idx_sec);
	struct plt_idx_entry *plt_idx;

	if (plt)
		return (Elf_Addr)plt;

	nr = plt_sec->num_entries;

	/* There is no duplicate entry, create a new one */
	plt = (struct plt_entry *)sechdrs[plt_sec->shndx].sh_addr;
	plt[nr] = emit_plt_entry(val);
	plt_idx = (struct plt_idx_entry *)sechdrs[plt_idx_sec->shndx].sh_addr;
	plt_idx[nr] = emit_plt_idx_entry(val);

	plt_sec->num_entries++;
	plt_idx_sec->num_entries++;
	BUG_ON(plt_sec->num_entries > plt_sec->max_entries);

	return (Elf_Addr)&plt[nr];
}

#define cmp_3way(a, b)  ((a) < (b) ? -1 : (a) > (b))

static int compare_rela(const void *x, const void *y)
{
	int ret;
	const Elf_Rela *rela_x = x, *rela_y = y;

	ret = cmp_3way(rela_x->r_info, rela_y->r_info);
	if (ret == 0)
		ret = cmp_3way(rela_x->r_addend, rela_y->r_addend);

	return ret;
}

static void count_max_entries(Elf_Rela *relas, int num,
			      unsigned int *plts, unsigned int *gots)
{
	unsigned int i;

	sort(relas, num, sizeof(Elf_Rela), compare_rela, NULL);

	for (i = 0; i < num; i++) {
		if (i && !compare_rela(&relas[i-1], &relas[i]))
			continue;

		switch (ELF_R_TYPE(relas[i].r_info)) {
		case R_LARCH_SOP_PUSH_PLT_PCREL:
		case R_LARCH_B26:
			(*plts)++;
			break;
		case R_LARCH_GOT_PC_HI20:
			(*gots)++;
			break;
		default:
			break; /* Do nothing. */
		}
	}
}

int module_frob_arch_sections(Elf_Ehdr *ehdr, Elf_Shdr *sechdrs,
			      char *secstrings, struct module *mod)
{
	unsigned int i, num_plts = 0, num_gots = 0;
	Elf_Shdr *got_sec, *plt_sec, *plt_idx_sec, *tramp = NULL;

	/*
	 * Find the empty .plt sections.
	 */
	for (i = 0; i < ehdr->e_shnum; i++) {
		if (!strcmp(secstrings + sechdrs[i].sh_name, ".got"))
			mod->arch.got.shndx = i;
		else if (!strcmp(secstrings + sechdrs[i].sh_name, ".plt"))
			mod->arch.plt.shndx = i;
		else if (!strcmp(secstrings + sechdrs[i].sh_name, ".plt.idx"))
			mod->arch.plt_idx.shndx = i;
		else if (!strcmp(secstrings + sechdrs[i].sh_name, ".ftrace_trampoline"))
			tramp = sechdrs + i;
	}

	if (!mod->arch.got.shndx) {
		pr_err("%s: module GOT section(s) missing\n", mod->name);
		return -ENOEXEC;
	}
	if (!mod->arch.plt.shndx) {
		pr_err("%s: module PLT section(s) missing\n", mod->name);
		return -ENOEXEC;
	}
	if (!mod->arch.plt_idx.shndx) {
		pr_err("%s: module PLT.IDX section(s) missing\n", mod->name);
		return -ENOEXEC;
	}

	/* Calculate the maxinum number of entries */
	for (i = 0; i < ehdr->e_shnum; i++) {
		int num_rela = sechdrs[i].sh_size / sizeof(Elf_Rela);
		Elf_Rela *relas = (void *)ehdr + sechdrs[i].sh_offset;
		Elf_Shdr *dst_sec = sechdrs + sechdrs[i].sh_info;

		if (sechdrs[i].sh_type != SHT_RELA)
			continue;

		/* ignore relocations that operate on non-exec sections */
		if (!(dst_sec->sh_flags & SHF_EXECINSTR))
			continue;

		count_max_entries(relas, num_rela, &num_plts, &num_gots);
	}

	got_sec = sechdrs + mod->arch.got.shndx;
	got_sec->sh_type = SHT_NOBITS;
	got_sec->sh_flags = SHF_ALLOC;
	got_sec->sh_addralign = L1_CACHE_BYTES;
	got_sec->sh_size = (num_gots + 1) * sizeof(struct got_entry);
	mod->arch.got.num_entries = 0;
	mod->arch.got.max_entries = num_gots;

	plt_sec = sechdrs + mod->arch.plt.shndx;
	plt_sec->sh_type = SHT_NOBITS;
	plt_sec->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	plt_sec->sh_addralign = L1_CACHE_BYTES;
	plt_sec->sh_size = (num_plts + 1) * sizeof(struct plt_entry);
	mod->arch.plt.num_entries = 0;
	mod->arch.plt.max_entries = num_plts;

	plt_idx_sec = sechdrs + mod->arch.plt_idx.shndx;
	plt_idx_sec->sh_type = SHT_NOBITS;
	plt_idx_sec->sh_flags = SHF_ALLOC;
	plt_idx_sec->sh_addralign = L1_CACHE_BYTES;
	plt_idx_sec->sh_size = (num_plts + 1) * sizeof(struct plt_idx_entry);
	mod->arch.plt_idx.num_entries = 0;
	mod->arch.plt_idx.max_entries = num_plts;

	if (tramp) {
		tramp->sh_type = SHT_NOBITS;
		tramp->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
		tramp->sh_addralign = __alignof__(struct plt_entry);
		tramp->sh_size = NR_FTRACE_PLTS * sizeof(struct plt_entry);
	}

	return 0;
}
