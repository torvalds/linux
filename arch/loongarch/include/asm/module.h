/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_MODULE_H
#define _ASM_MODULE_H

#include <asm/inst.h>
#include <asm-generic/module.h>

#define RELA_STACK_DEPTH 16

struct mod_section {
	Elf_Shdr *shdr;
	int num_entries;
	int max_entries;
};

struct mod_arch_specific {
	struct mod_section got;
	struct mod_section plt;
	struct mod_section plt_idx;
};

struct got_entry {
	Elf_Addr symbol_addr;
};

struct plt_entry {
	u32 inst_lu12iw;
	u32 inst_lu32id;
	u32 inst_lu52id;
	u32 inst_jirl;
};

struct plt_idx_entry {
	Elf_Addr symbol_addr;
};

Elf_Addr module_emit_got_entry(struct module *mod, Elf_Addr val);
Elf_Addr module_emit_plt_entry(struct module *mod, Elf_Addr val);

static inline struct got_entry emit_got_entry(Elf_Addr val)
{
	return (struct got_entry) { val };
}

static inline struct plt_entry emit_plt_entry(unsigned long val)
{
	u32 lu12iw, lu32id, lu52id, jirl;

	lu12iw = (lu12iw_op << 25 | (((val >> 12) & 0xfffff) << 5) | LOONGARCH_GPR_T1);
	lu32id = larch_insn_gen_lu32id(LOONGARCH_GPR_T1, ADDR_IMM(val, LU32ID));
	lu52id = larch_insn_gen_lu52id(LOONGARCH_GPR_T1, LOONGARCH_GPR_T1, ADDR_IMM(val, LU52ID));
	jirl = larch_insn_gen_jirl(0, LOONGARCH_GPR_T1, 0, (val & 0xfff));

	return (struct plt_entry) { lu12iw, lu32id, lu52id, jirl };
}

static inline struct plt_idx_entry emit_plt_idx_entry(unsigned long val)
{
	return (struct plt_idx_entry) { val };
}

static inline int get_plt_idx(unsigned long val, const struct mod_section *sec)
{
	int i;
	struct plt_idx_entry *plt_idx = (struct plt_idx_entry *)sec->shdr->sh_addr;

	for (i = 0; i < sec->num_entries; i++) {
		if (plt_idx[i].symbol_addr == val)
			return i;
	}

	return -1;
}

static inline struct plt_entry *get_plt_entry(unsigned long val,
				      const struct mod_section *sec_plt,
				      const struct mod_section *sec_plt_idx)
{
	int plt_idx = get_plt_idx(val, sec_plt_idx);
	struct plt_entry *plt = (struct plt_entry *)sec_plt->shdr->sh_addr;

	if (plt_idx < 0)
		return NULL;

	return plt + plt_idx;
}

static inline struct got_entry *get_got_entry(Elf_Addr val,
					      const struct mod_section *sec)
{
	struct got_entry *got = (struct got_entry *)sec->shdr->sh_addr;
	int i;

	for (i = 0; i < sec->num_entries; i++)
		if (got[i].symbol_addr == val)
			return &got[i];
	return NULL;
}

#endif /* _ASM_MODULE_H */
