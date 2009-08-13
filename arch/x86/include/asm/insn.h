#ifndef _ASM_X86_INSN_H
#define _ASM_X86_INSN_H
/*
 * x86 instruction analysis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2009
 */

/* insn_attr_t is defined in inat.h */
#include <asm/inat.h>

struct insn_field {
	union {
		insn_value_t value;
		insn_byte_t bytes[4];
	};
	/* !0 if we've run insn_get_xxx() for this field */
	unsigned char got;
	unsigned char nbytes;
};

struct insn {
	struct insn_field prefixes;	/*
					 * Prefixes
					 * prefixes.bytes[3]: last prefix
					 */
	struct insn_field rex_prefix;	/* REX prefix */
	struct insn_field opcode;	/*
					 * opcode.bytes[0]: opcode1
					 * opcode.bytes[1]: opcode2
					 * opcode.bytes[2]: opcode3
					 */
	struct insn_field modrm;
	struct insn_field sib;
	struct insn_field displacement;
	union {
		struct insn_field immediate;
		struct insn_field moffset1;	/* for 64bit MOV */
		struct insn_field immediate1;	/* for 64bit imm or off16/32 */
	};
	union {
		struct insn_field moffset2;	/* for 64bit MOV */
		struct insn_field immediate2;	/* for 64bit imm or seg16 */
	};

	insn_attr_t attr;
	unsigned char opnd_bytes;
	unsigned char addr_bytes;
	unsigned char length;
	unsigned char x86_64;

	const insn_byte_t *kaddr;	/* kernel address of insn to analyze */
	const insn_byte_t *next_byte;
};

#define X86_MODRM_MOD(modrm) (((modrm) & 0xc0) >> 6)
#define X86_MODRM_REG(modrm) (((modrm) & 0x38) >> 3)
#define X86_MODRM_RM(modrm) ((modrm) & 0x07)

#define X86_SIB_SCALE(sib) (((sib) & 0xc0) >> 6)
#define X86_SIB_INDEX(sib) (((sib) & 0x38) >> 3)
#define X86_SIB_BASE(sib) ((sib) & 0x07)

#define X86_REX_W(rex) ((rex) & 8)
#define X86_REX_R(rex) ((rex) & 4)
#define X86_REX_X(rex) ((rex) & 2)
#define X86_REX_B(rex) ((rex) & 1)

/* The last prefix is needed for two-byte and three-byte opcodes */
static inline insn_byte_t insn_last_prefix(struct insn *insn)
{
	return insn->prefixes.bytes[3];
}

extern void insn_init(struct insn *insn, const void *kaddr, int x86_64);
extern void insn_get_prefixes(struct insn *insn);
extern void insn_get_opcode(struct insn *insn);
extern void insn_get_modrm(struct insn *insn);
extern void insn_get_sib(struct insn *insn);
extern void insn_get_displacement(struct insn *insn);
extern void insn_get_immediate(struct insn *insn);
extern void insn_get_length(struct insn *insn);

/* Attribute will be determined after getting ModRM (for opcode groups) */
static inline void insn_get_attribute(struct insn *insn)
{
	insn_get_modrm(insn);
}

/* Instruction uses RIP-relative addressing */
extern int insn_rip_relative(struct insn *insn);

/* Init insn for kernel text */
static inline void kernel_insn_init(struct insn *insn, const void *kaddr)
{
#ifdef CONFIG_X86_64
	insn_init(insn, kaddr, 1);
#else /* CONFIG_X86_32 */
	insn_init(insn, kaddr, 0);
#endif
}

/* Offset of each field from kaddr */
static inline int insn_offset_rex_prefix(struct insn *insn)
{
	return insn->prefixes.nbytes;
}
static inline int insn_offset_opcode(struct insn *insn)
{
	return insn_offset_rex_prefix(insn) + insn->rex_prefix.nbytes;
}
static inline int insn_offset_modrm(struct insn *insn)
{
	return insn_offset_opcode(insn) + insn->opcode.nbytes;
}
static inline int insn_offset_sib(struct insn *insn)
{
	return insn_offset_modrm(insn) + insn->modrm.nbytes;
}
static inline int insn_offset_displacement(struct insn *insn)
{
	return insn_offset_sib(insn) + insn->sib.nbytes;
}
static inline int insn_offset_immediate(struct insn *insn)
{
	return insn_offset_displacement(insn) + insn->displacement.nbytes;
}

#endif /* _ASM_X86_INSN_H */
