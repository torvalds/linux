// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <linux/elf.h>
#include <linux/ftrace.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/sort.h>

static struct plt_entry __get_adrp_add_pair(u64 dst, u64 pc,
					    enum aarch64_insn_register reg)
{
	u32 adrp, add;

	adrp = aarch64_insn_gen_adr(pc, dst, reg, AARCH64_INSN_ADR_TYPE_ADRP);
	add = aarch64_insn_gen_add_sub_imm(reg, reg, dst % SZ_4K,
					   AARCH64_INSN_VARIANT_64BIT,
					   AARCH64_INSN_ADSB_ADD);

	return (struct plt_entry){ cpu_to_le32(adrp), cpu_to_le32(add) };
}

struct plt_entry get_plt_entry(u64 dst, void *pc)
{
	struct plt_entry plt;
	static u32 br;

	if (!br)
		br = aarch64_insn_gen_branch_reg(AARCH64_INSN_REG_16,
						 AARCH64_INSN_BRANCH_NOLINK);

	plt = __get_adrp_add_pair(dst, (u64)pc, AARCH64_INSN_REG_16);
	plt.br = cpu_to_le32(br);

	return plt;
}

static bool plt_entries_equal(const struct plt_entry *a,
			      const struct plt_entry *b)
{
	u64 p, q;

	/*
	 * Check whether both entries refer to the same target:
	 * do the cheapest checks first.
	 * If the 'add' or 'br' opcodes are different, then the target
	 * cannot be the same.
	 */
	if (a->add != b->add || a->br != b->br)
		return false;

	p = ALIGN_DOWN((u64)a, SZ_4K);
	q = ALIGN_DOWN((u64)b, SZ_4K);

	/*
	 * If the 'adrp' opcodes are the same then we just need to check
	 * that they refer to the same 4k region.
	 */
	if (a->adrp == b->adrp && p == q)
		return true;

	return (p + aarch64_insn_adrp_get_offset(le32_to_cpu(a->adrp))) ==
	       (q + aarch64_insn_adrp_get_offset(le32_to_cpu(b->adrp)));
}

u64 module_emit_plt_entry(struct module *mod, Elf64_Shdr *sechdrs,
			  void *loc, const Elf64_Rela *rela,
			  Elf64_Sym *sym)
{
	struct mod_plt_sec *pltsec = !within_module_init((unsigned long)loc, mod) ?
						&mod->arch.core : &mod->arch.init;
	struct plt_entry *plt = (struct plt_entry *)sechdrs[pltsec->plt_shndx].sh_addr;
	int i = pltsec->plt_num_entries;
	int j = i - 1;
	u64 val = sym->st_value + rela->r_addend;

	if (is_forbidden_offset_for_adrp(&plt[i].adrp))
		i++;

	plt[i] = get_plt_entry(val, &plt[i]);

	/*
	 * Check if the entry we just created is a duplicate. Given that the
	 * relocations are sorted, this will be the last entry we allocated.
	 * (if one exists).
	 */
	if (j >= 0 && plt_entries_equal(plt + i, plt + j))
		return (u64)&plt[j];

	pltsec->plt_num_entries += i - j;
	if (WARN_ON(pltsec->plt_num_entries > pltsec->plt_max_entries))
		return 0;

	return (u64)&plt[i];
}

#ifdef CONFIG_ARM64_ERRATUM_843419
u64 module_emit_veneer_for_adrp(struct module *mod, Elf64_Shdr *sechdrs,
				void *loc, u64 val)
{
	struct mod_plt_sec *pltsec = !within_module_init((unsigned long)loc, mod) ?
						&mod->arch.core : &mod->arch.init;
	struct plt_entry *plt = (struct plt_entry *)sechdrs[pltsec->plt_shndx].sh_addr;
	int i = pltsec->plt_num_entries++;
	u32 br;
	int rd;

	if (WARN_ON(pltsec->plt_num_entries > pltsec->plt_max_entries))
		return 0;

	if (is_forbidden_offset_for_adrp(&plt[i].adrp))
		i = pltsec->plt_num_entries++;

	/* get the destination register of the ADRP instruction */
	rd = aarch64_insn_decode_register(AARCH64_INSN_REGTYPE_RD,
					  le32_to_cpup((__le32 *)loc));

	br = aarch64_insn_gen_branch_imm((u64)&plt[i].br, (u64)loc + 4,
					 AARCH64_INSN_BRANCH_NOLINK);

	plt[i] = __get_adrp_add_pair(val, (u64)&plt[i], rd);
	plt[i].br = cpu_to_le32(br);

	return (u64)&plt[i];
}
#endif

#define cmp_3way(a, b)	((a) < (b) ? -1 : (a) > (b))

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

static unsigned int count_plts(Elf64_Sym *syms, Elf64_Rela *rela, int num,
			       Elf64_Word dstidx, Elf_Shdr *dstsec)
{
	unsigned int ret = 0;
	Elf64_Sym *s;
	int i;

	for (i = 0; i < num; i++) {
		u64 min_align;

		switch (ELF64_R_TYPE(rela[i].r_info)) {
		case R_AARCH64_JUMP26:
		case R_AARCH64_CALL26:
			if (!IS_ENABLED(CONFIG_RANDOMIZE_BASE))
				break;

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
			s = syms + ELF64_R_SYM(rela[i].r_info);
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
			 * the addend is zero: this allows us to record the PLT
			 * entry address in the symbol table itself, rather than
			 * having to search the list for duplicates each time we
			 * emit one.
			 */
			if (rela[i].r_addend != 0 || !duplicate_rel(rela, i))
				ret++;
			break;
		case R_AARCH64_ADR_PREL_PG_HI21_NC:
		case R_AARCH64_ADR_PREL_PG_HI21:
			if (!IS_ENABLED(CONFIG_ARM64_ERRATUM_843419) ||
			    !cpus_have_const_cap(ARM64_WORKAROUND_843419))
				break;

			/*
			 * Determine the minimal safe alignment for this ADRP
			 * instruction: the section alignment at which it is
			 * guaranteed not to appear at a vulnerable offset.
			 *
			 * This comes down to finding the least significant zero
			 * bit in bits [11:3] of the section offset, and
			 * increasing the section's alignment so that the
			 * resulting address of this instruction is guaranteed
			 * to equal the offset in that particular bit (as well
			 * as all less significant bits). This ensures that the
			 * address modulo 4 KB != 0xfff8 or 0xfffc (which would
			 * have all ones in bits [11:3])
			 */
			min_align = 2ULL << ffz(rela[i].r_offset | 0x7);

			/*
			 * Allocate veneer space for each ADRP that may appear
			 * at a vulnerable offset nonetheless. At relocation
			 * time, some of these will remain unused since some
			 * ADRP instructions can be patched to ADR instructions
			 * instead.
			 */
			if (min_align > SZ_4K)
				ret++;
			else
				dstsec->sh_addralign = max(dstsec->sh_addralign,
							   min_align);
			break;
		}
	}

	if (IS_ENABLED(CONFIG_ARM64_ERRATUM_843419) &&
	    cpus_have_const_cap(ARM64_WORKAROUND_843419))
		/*
		 * Add some slack so we can skip PLT slots that may trigger
		 * the erratum due to the placement of the ADRP instruction.
		 */
		ret += DIV_ROUND_UP(ret, (SZ_4K / sizeof(struct plt_entry)));

	return ret;
}

static bool branch_rela_needs_plt(Elf64_Sym *syms, Elf64_Rela *rela,
				  Elf64_Word dstidx)
{

	Elf64_Sym *s = syms + ELF64_R_SYM(rela->r_info);

	if (s->st_shndx == dstidx)
		return false;

	return ELF64_R_TYPE(rela->r_info) == R_AARCH64_JUMP26 ||
	       ELF64_R_TYPE(rela->r_info) == R_AARCH64_CALL26;
}

/* Group branch PLT relas at the front end of the array. */
static int partition_branch_plt_relas(Elf64_Sym *syms, Elf64_Rela *rela,
				      int numrels, Elf64_Word dstidx)
{
	int i = 0, j = numrels - 1;

	if (!IS_ENABLED(CONFIG_RANDOMIZE_BASE))
		return 0;

	while (i < j) {
		if (branch_rela_needs_plt(syms, &rela[i], dstidx))
			i++;
		else if (branch_rela_needs_plt(syms, &rela[j], dstidx))
			swap(rela[i], rela[j]);
		else
			j--;
	}

	return i;
}

int module_frob_arch_sections(Elf_Ehdr *ehdr, Elf_Shdr *sechdrs,
			      char *secstrings, struct module *mod)
{
	unsigned long core_plts = 0;
	unsigned long init_plts = 0;
	Elf64_Sym *syms = NULL;
	Elf_Shdr *pltsec, *tramp = NULL;
	int i;

	/*
	 * Find the empty .plt section so we can expand it to store the PLT
	 * entries. Record the symtab address as well.
	 */
	for (i = 0; i < ehdr->e_shnum; i++) {
		if (!strcmp(secstrings + sechdrs[i].sh_name, ".plt"))
			mod->arch.core.plt_shndx = i;
		else if (!strcmp(secstrings + sechdrs[i].sh_name, ".init.plt"))
			mod->arch.init.plt_shndx = i;
		else if (!strcmp(secstrings + sechdrs[i].sh_name,
				 ".text.ftrace_trampoline"))
			tramp = sechdrs + i;
		else if (sechdrs[i].sh_type == SHT_SYMTAB)
			syms = (Elf64_Sym *)sechdrs[i].sh_addr;
	}

	if (!mod->arch.core.plt_shndx || !mod->arch.init.plt_shndx) {
		pr_err("%s: module PLT section(s) missing\n", mod->name);
		return -ENOEXEC;
	}
	if (!syms) {
		pr_err("%s: module symtab section missing\n", mod->name);
		return -ENOEXEC;
	}

	for (i = 0; i < ehdr->e_shnum; i++) {
		Elf64_Rela *rels = (void *)ehdr + sechdrs[i].sh_offset;
		int nents, numrels = sechdrs[i].sh_size / sizeof(Elf64_Rela);
		Elf64_Shdr *dstsec = sechdrs + sechdrs[i].sh_info;

		if (sechdrs[i].sh_type != SHT_RELA)
			continue;

		/* ignore relocations that operate on non-exec sections */
		if (!(dstsec->sh_flags & SHF_EXECINSTR))
			continue;

		/*
		 * sort branch relocations requiring a PLT by type, symbol index
		 * and addend
		 */
		nents = partition_branch_plt_relas(syms, rels, numrels,
						   sechdrs[i].sh_info);
		if (nents)
			sort(rels, nents, sizeof(Elf64_Rela), cmp_rela, NULL);

		if (!str_has_prefix(secstrings + dstsec->sh_name, ".init"))
			core_plts += count_plts(syms, rels, numrels,
						sechdrs[i].sh_info, dstsec);
		else
			init_plts += count_plts(syms, rels, numrels,
						sechdrs[i].sh_info, dstsec);
	}

	pltsec = sechdrs + mod->arch.core.plt_shndx;
	pltsec->sh_type = SHT_NOBITS;
	pltsec->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	pltsec->sh_addralign = L1_CACHE_BYTES;
	pltsec->sh_size = (core_plts  + 1) * sizeof(struct plt_entry);
	mod->arch.core.plt_num_entries = 0;
	mod->arch.core.plt_max_entries = core_plts;

	pltsec = sechdrs + mod->arch.init.plt_shndx;
	pltsec->sh_type = SHT_NOBITS;
	pltsec->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
	pltsec->sh_addralign = L1_CACHE_BYTES;
	pltsec->sh_size = (init_plts + 1) * sizeof(struct plt_entry);
	mod->arch.init.plt_num_entries = 0;
	mod->arch.init.plt_max_entries = init_plts;

	if (tramp) {
		tramp->sh_type = SHT_NOBITS;
		tramp->sh_flags = SHF_EXECINSTR | SHF_ALLOC;
		tramp->sh_addralign = __alignof__(struct plt_entry);
		tramp->sh_size = NR_FTRACE_PLTS * sizeof(struct plt_entry);
	}

	return 0;
}
