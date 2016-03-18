/*
 * AArch64 loadable module support.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/bitops.h>
#include <linux/elf.h>
#include <linux/gfp.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleloader.h>
#include <linux/vmalloc.h>
#include <asm/alternative.h>
#include <asm/insn.h>
#include <asm/sections.h>

void *module_alloc(unsigned long size)
{
	void *p;

	p = __vmalloc_node_range(size, MODULE_ALIGN, module_alloc_base,
				module_alloc_base + MODULES_VSIZE,
				GFP_KERNEL, PAGE_KERNEL_EXEC, 0,
				NUMA_NO_NODE, __builtin_return_address(0));

	if (!p && IS_ENABLED(CONFIG_ARM64_MODULE_PLTS) &&
	    !IS_ENABLED(CONFIG_KASAN))
		/*
		 * KASAN can only deal with module allocations being served
		 * from the reserved module region, since the remainder of
		 * the vmalloc region is already backed by zero shadow pages,
		 * and punching holes into it is non-trivial. Since the module
		 * region is not randomized when KASAN is enabled, it is even
		 * less likely that the module region gets exhausted, so we
		 * can simply omit this fallback in that case.
		 */
		p = __vmalloc_node_range(size, MODULE_ALIGN, VMALLOC_START,
				VMALLOC_END, GFP_KERNEL, PAGE_KERNEL_EXEC, 0,
				NUMA_NO_NODE, __builtin_return_address(0));

	if (p && (kasan_module_alloc(p, size) < 0)) {
		vfree(p);
		return NULL;
	}

	return p;
}

enum aarch64_reloc_op {
	RELOC_OP_NONE,
	RELOC_OP_ABS,
	RELOC_OP_PREL,
	RELOC_OP_PAGE,
};

static u64 do_reloc(enum aarch64_reloc_op reloc_op, void *place, u64 val)
{
	switch (reloc_op) {
	case RELOC_OP_ABS:
		return val;
	case RELOC_OP_PREL:
		return val - (u64)place;
	case RELOC_OP_PAGE:
		return (val & ~0xfff) - ((u64)place & ~0xfff);
	case RELOC_OP_NONE:
		return 0;
	}

	pr_err("do_reloc: unknown relocation operation %d\n", reloc_op);
	return 0;
}

static int reloc_data(enum aarch64_reloc_op op, void *place, u64 val, int len)
{
	s64 sval = do_reloc(op, place, val);

	switch (len) {
	case 16:
		*(s16 *)place = sval;
		if (sval < S16_MIN || sval > U16_MAX)
			return -ERANGE;
		break;
	case 32:
		*(s32 *)place = sval;
		if (sval < S32_MIN || sval > U32_MAX)
			return -ERANGE;
		break;
	case 64:
		*(s64 *)place = sval;
		break;
	default:
		pr_err("Invalid length (%d) for data relocation\n", len);
		return 0;
	}
	return 0;
}

enum aarch64_insn_movw_imm_type {
	AARCH64_INSN_IMM_MOVNZ,
	AARCH64_INSN_IMM_MOVKZ,
};

static int reloc_insn_movw(enum aarch64_reloc_op op, void *place, u64 val,
			   int lsb, enum aarch64_insn_movw_imm_type imm_type)
{
	u64 imm;
	s64 sval;
	u32 insn = le32_to_cpu(*(u32 *)place);

	sval = do_reloc(op, place, val);
	imm = sval >> lsb;

	if (imm_type == AARCH64_INSN_IMM_MOVNZ) {
		/*
		 * For signed MOVW relocations, we have to manipulate the
		 * instruction encoding depending on whether or not the
		 * immediate is less than zero.
		 */
		insn &= ~(3 << 29);
		if (sval >= 0) {
			/* >=0: Set the instruction to MOVZ (opcode 10b). */
			insn |= 2 << 29;
		} else {
			/*
			 * <0: Set the instruction to MOVN (opcode 00b).
			 *     Since we've masked the opcode already, we
			 *     don't need to do anything other than
			 *     inverting the new immediate field.
			 */
			imm = ~imm;
		}
	}

	/* Update the instruction with the new encoding. */
	insn = aarch64_insn_encode_immediate(AARCH64_INSN_IMM_16, insn, imm);
	*(u32 *)place = cpu_to_le32(insn);

	if (imm > U16_MAX)
		return -ERANGE;

	return 0;
}

static int reloc_insn_imm(enum aarch64_reloc_op op, void *place, u64 val,
			  int lsb, int len, enum aarch64_insn_imm_type imm_type)
{
	u64 imm, imm_mask;
	s64 sval;
	u32 insn = le32_to_cpu(*(u32 *)place);

	/* Calculate the relocation value. */
	sval = do_reloc(op, place, val);
	sval >>= lsb;

	/* Extract the value bits and shift them to bit 0. */
	imm_mask = (BIT(lsb + len) - 1) >> lsb;
	imm = sval & imm_mask;

	/* Update the instruction's immediate field. */
	insn = aarch64_insn_encode_immediate(imm_type, insn, imm);
	*(u32 *)place = cpu_to_le32(insn);

	/*
	 * Extract the upper value bits (including the sign bit) and
	 * shift them to bit 0.
	 */
	sval = (s64)(sval & ~(imm_mask >> 1)) >> (len - 1);

	/*
	 * Overflow has occurred if the upper bits are not all equal to
	 * the sign bit of the value.
	 */
	if ((u64)(sval + 1) >= 2)
		return -ERANGE;

	return 0;
}

int apply_relocate_add(Elf64_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	unsigned int i;
	int ovf;
	bool overflow_check;
	Elf64_Sym *sym;
	void *loc;
	u64 val;
	Elf64_Rela *rel = (void *)sechdrs[relsec].sh_addr;

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* loc corresponds to P in the AArch64 ELF document. */
		loc = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;

		/* sym is the ELF symbol we're referring to. */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
			+ ELF64_R_SYM(rel[i].r_info);

		/* val corresponds to (S + A) in the AArch64 ELF document. */
		val = sym->st_value + rel[i].r_addend;

		/* Check for overflow by default. */
		overflow_check = true;

		/* Perform the static relocation. */
		switch (ELF64_R_TYPE(rel[i].r_info)) {
		/* Null relocations. */
		case R_ARM_NONE:
		case R_AARCH64_NONE:
			ovf = 0;
			break;

		/* Data relocations. */
		case R_AARCH64_ABS64:
			overflow_check = false;
			ovf = reloc_data(RELOC_OP_ABS, loc, val, 64);
			break;
		case R_AARCH64_ABS32:
			ovf = reloc_data(RELOC_OP_ABS, loc, val, 32);
			break;
		case R_AARCH64_ABS16:
			ovf = reloc_data(RELOC_OP_ABS, loc, val, 16);
			break;
		case R_AARCH64_PREL64:
			overflow_check = false;
			ovf = reloc_data(RELOC_OP_PREL, loc, val, 64);
			break;
		case R_AARCH64_PREL32:
			ovf = reloc_data(RELOC_OP_PREL, loc, val, 32);
			break;
		case R_AARCH64_PREL16:
			ovf = reloc_data(RELOC_OP_PREL, loc, val, 16);
			break;

		/* MOVW instruction relocations. */
		case R_AARCH64_MOVW_UABS_G0_NC:
			overflow_check = false;
		case R_AARCH64_MOVW_UABS_G0:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 0,
					      AARCH64_INSN_IMM_MOVKZ);
			break;
		case R_AARCH64_MOVW_UABS_G1_NC:
			overflow_check = false;
		case R_AARCH64_MOVW_UABS_G1:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 16,
					      AARCH64_INSN_IMM_MOVKZ);
			break;
		case R_AARCH64_MOVW_UABS_G2_NC:
			overflow_check = false;
		case R_AARCH64_MOVW_UABS_G2:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 32,
					      AARCH64_INSN_IMM_MOVKZ);
			break;
		case R_AARCH64_MOVW_UABS_G3:
			/* We're using the top bits so we can't overflow. */
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 48,
					      AARCH64_INSN_IMM_MOVKZ);
			break;
		case R_AARCH64_MOVW_SABS_G0:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 0,
					      AARCH64_INSN_IMM_MOVNZ);
			break;
		case R_AARCH64_MOVW_SABS_G1:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 16,
					      AARCH64_INSN_IMM_MOVNZ);
			break;
		case R_AARCH64_MOVW_SABS_G2:
			ovf = reloc_insn_movw(RELOC_OP_ABS, loc, val, 32,
					      AARCH64_INSN_IMM_MOVNZ);
			break;
		case R_AARCH64_MOVW_PREL_G0_NC:
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 0,
					      AARCH64_INSN_IMM_MOVKZ);
			break;
		case R_AARCH64_MOVW_PREL_G0:
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 0,
					      AARCH64_INSN_IMM_MOVNZ);
			break;
		case R_AARCH64_MOVW_PREL_G1_NC:
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 16,
					      AARCH64_INSN_IMM_MOVKZ);
			break;
		case R_AARCH64_MOVW_PREL_G1:
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 16,
					      AARCH64_INSN_IMM_MOVNZ);
			break;
		case R_AARCH64_MOVW_PREL_G2_NC:
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 32,
					      AARCH64_INSN_IMM_MOVKZ);
			break;
		case R_AARCH64_MOVW_PREL_G2:
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 32,
					      AARCH64_INSN_IMM_MOVNZ);
			break;
		case R_AARCH64_MOVW_PREL_G3:
			/* We're using the top bits so we can't overflow. */
			overflow_check = false;
			ovf = reloc_insn_movw(RELOC_OP_PREL, loc, val, 48,
					      AARCH64_INSN_IMM_MOVNZ);
			break;

		/* Immediate instruction relocations. */
		case R_AARCH64_LD_PREL_LO19:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2, 19,
					     AARCH64_INSN_IMM_19);
			break;
		case R_AARCH64_ADR_PREL_LO21:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 0, 21,
					     AARCH64_INSN_IMM_ADR);
			break;
#ifndef CONFIG_ARM64_ERRATUM_843419
		case R_AARCH64_ADR_PREL_PG_HI21_NC:
			overflow_check = false;
		case R_AARCH64_ADR_PREL_PG_HI21:
			ovf = reloc_insn_imm(RELOC_OP_PAGE, loc, val, 12, 21,
					     AARCH64_INSN_IMM_ADR);
			break;
#endif
		case R_AARCH64_ADD_ABS_LO12_NC:
		case R_AARCH64_LDST8_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 0, 12,
					     AARCH64_INSN_IMM_12);
			break;
		case R_AARCH64_LDST16_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 1, 11,
					     AARCH64_INSN_IMM_12);
			break;
		case R_AARCH64_LDST32_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 2, 10,
					     AARCH64_INSN_IMM_12);
			break;
		case R_AARCH64_LDST64_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 3, 9,
					     AARCH64_INSN_IMM_12);
			break;
		case R_AARCH64_LDST128_ABS_LO12_NC:
			overflow_check = false;
			ovf = reloc_insn_imm(RELOC_OP_ABS, loc, val, 4, 8,
					     AARCH64_INSN_IMM_12);
			break;
		case R_AARCH64_TSTBR14:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2, 14,
					     AARCH64_INSN_IMM_14);
			break;
		case R_AARCH64_CONDBR19:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2, 19,
					     AARCH64_INSN_IMM_19);
			break;
		case R_AARCH64_JUMP26:
		case R_AARCH64_CALL26:
			ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2, 26,
					     AARCH64_INSN_IMM_26);

			if (IS_ENABLED(CONFIG_ARM64_MODULE_PLTS) &&
			    ovf == -ERANGE) {
				val = module_emit_plt_entry(me, &rel[i], sym);
				ovf = reloc_insn_imm(RELOC_OP_PREL, loc, val, 2,
						     26, AARCH64_INSN_IMM_26);
			}
			break;

		default:
			pr_err("module %s: unsupported RELA relocation: %llu\n",
			       me->name, ELF64_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}

		if (overflow_check && ovf == -ERANGE)
			goto overflow;

	}

	return 0;

overflow:
	pr_err("module %s: overflow in relocation type %d val %Lx\n",
	       me->name, (int)ELF64_R_TYPE(rel[i].r_info), val);
	return -ENOEXEC;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	const Elf_Shdr *s, *se;
	const char *secstrs = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

	for (s = sechdrs, se = sechdrs + hdr->e_shnum; s < se; s++) {
		if (strcmp(".altinstructions", secstrs + s->sh_name) == 0) {
			apply_alternatives((void *)s->sh_addr, s->sh_size);
			return 0;
		}
	}

	return 0;
}
