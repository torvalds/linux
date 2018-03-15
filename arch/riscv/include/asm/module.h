/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */

#ifndef _ASM_RISCV_MODULE_H
#define _ASM_RISCV_MODULE_H

#include <asm-generic/module.h>

#define MODULE_ARCH_VERMAGIC    "riscv"

u64 module_emit_got_entry(struct module *mod, u64 val);
u64 module_emit_plt_entry(struct module *mod, u64 val);

#ifdef CONFIG_MODULE_SECTIONS
struct mod_section {
	struct elf64_shdr *shdr;
	int num_entries;
	int max_entries;
};

struct mod_arch_specific {
	struct mod_section got;
	struct mod_section plt;
};

struct got_entry {
	u64 symbol_addr;	/* the real variable address */
};

static inline struct got_entry emit_got_entry(u64 val)
{
	return (struct got_entry) {val};
}

static inline struct got_entry *get_got_entry(u64 val,
					      const struct mod_section *sec)
{
	struct got_entry *got = (struct got_entry *)sec->shdr->sh_addr;
	int i;
	for (i = 0; i < sec->num_entries; i++) {
		if (got[i].symbol_addr == val)
			return &got[i];
	}
	return NULL;
}

struct plt_entry {
	/*
	 * Trampoline code to real target address. The return address
	 * should be the original (pc+4) before entring plt entry.
	 * For 8 byte alignment of symbol_addr,
	 * don't pack structure to remove the padding.
	 */
	u32 insn_auipc;		/* auipc t0, 0x0                       */
	u32 insn_ld;		/* ld    t1, 0x10(t0)                  */
	u32 insn_jr;		/* jr    t1                            */
	u64 symbol_addr;	/* the real jump target address        */
};

#define OPC_AUIPC  0x0017
#define OPC_LD     0x3003
#define OPC_JALR   0x0067
#define REG_T0     0x5
#define REG_T1     0x6
#define IMM_OFFSET 0x10

static inline struct plt_entry emit_plt_entry(u64 val)
{
	/*
	 * U-Type encoding:
	 * +------------+----------+----------+
	 * | imm[31:12] | rd[11:7] | opc[6:0] |
	 * +------------+----------+----------+
	 *
	 * I-Type encoding:
	 * +------------+------------+--------+----------+----------+
	 * | imm[31:20] | rs1[19:15] | funct3 | rd[11:7] | opc[6:0] |
	 * +------------+------------+--------+----------+----------+
	 *
	 */
	return (struct plt_entry) {
		OPC_AUIPC | (REG_T0 << 7),
		OPC_LD | (IMM_OFFSET << 20) | (REG_T0 << 15) | (REG_T1 << 7),
		OPC_JALR | (REG_T1 << 15),
		val
	};
}

static inline struct plt_entry *get_plt_entry(u64 val,
					      const struct mod_section *sec)
{
	struct plt_entry *plt = (struct plt_entry *)sec->shdr->sh_addr;
	int i;
	for (i = 0; i < sec->num_entries; i++) {
		if (plt[i].symbol_addr == val)
			return &plt[i];
	}
	return NULL;
}

#endif /* CONFIG_MODULE_SECTIONS */

#endif /* _ASM_RISCV_MODULE_H */
