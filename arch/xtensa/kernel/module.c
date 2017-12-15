/*
 * arch/xtensa/kernel/module.c
 *
 * Module support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2006 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 *
 */

#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/cache.h>

static int
decode_calln_opcode (unsigned char *location)
{
#ifdef __XTENSA_EB__
	return (location[0] & 0xf0) == 0x50;
#endif
#ifdef __XTENSA_EL__
	return (location[0] & 0xf) == 0x5;
#endif
}

static int
decode_l32r_opcode (unsigned char *location)
{
#ifdef __XTENSA_EB__
	return (location[0] & 0xf0) == 0x10;
#endif
#ifdef __XTENSA_EL__
	return (location[0] & 0xf) == 0x1;
#endif
}

int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *mod)
{
	unsigned int i;
	Elf32_Rela *rela = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	unsigned char *location;
	uint32_t value;

	pr_debug("Applying relocate section %u to %u\n", relsec,
		 sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rela); i++) {
		location = (char *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rela[i].r_offset;
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rela[i].r_info);
		value = sym->st_value + rela[i].r_addend;

		switch (ELF32_R_TYPE(rela[i].r_info)) {
		case R_XTENSA_NONE:
		case R_XTENSA_DIFF8:
		case R_XTENSA_DIFF16:
		case R_XTENSA_DIFF32:
		case R_XTENSA_ASM_EXPAND:
			break;

		case R_XTENSA_32:
		case R_XTENSA_PLT:
			*(uint32_t *)location += value;
			break;

		case R_XTENSA_SLOT0_OP:
			if (decode_calln_opcode(location)) {
				value -= ((unsigned long)location & -4) + 4;
				if ((value & 3) != 0 ||
				    ((value + (1 << 19)) >> 20) != 0) {
					pr_err("%s: relocation out of range, "
					       "section %d reloc %d "
					       "sym '%s'\n",
					       mod->name, relsec, i,
					       strtab + sym->st_name);
					return -ENOEXEC;
				}
				value = (signed int)value >> 2;
#ifdef __XTENSA_EB__
				location[0] = ((location[0] & ~0x3) |
					    ((value >> 16) & 0x3));
				location[1] = (value >> 8) & 0xff;
				location[2] = value & 0xff;
#endif
#ifdef __XTENSA_EL__
				location[0] = ((location[0] & ~0xc0) |
					    ((value << 6) & 0xc0));
				location[1] = (value >> 2) & 0xff;
				location[2] = (value >> 10) & 0xff;
#endif
			} else if (decode_l32r_opcode(location)) {
				value -= (((unsigned long)location + 3) & -4);
				if ((value & 3) != 0 ||
				    (signed int)value >> 18 != -1) {
					pr_err("%s: relocation out of range, "
					       "section %d reloc %d "
					       "sym '%s'\n",
					       mod->name, relsec, i,
					       strtab + sym->st_name);
					return -ENOEXEC;
				}
				value = (signed int)value >> 2;

#ifdef __XTENSA_EB__
				location[1] = (value >> 8) & 0xff;
				location[2] = value & 0xff;
#endif
#ifdef __XTENSA_EL__
				location[1] = value & 0xff;
				location[2] = (value >> 8) & 0xff;
#endif
			}
			/* FIXME: Ignore any other opcodes.  The Xtensa
			   assembler currently assumes that the linker will
			   always do relaxation and so all PC-relative
			   operands need relocations.  (The assembler also
			   writes out the tentative PC-relative values,
			   assuming no link-time relaxation, so it is usually
			   safe to ignore the relocations.)  If the
			   assembler's "--no-link-relax" flag can be made to
			   work, and if all kernel modules can be assembled
			   with that flag, then unexpected relocations could
			   be detected here.  */
			break;

		case R_XTENSA_SLOT1_OP:
		case R_XTENSA_SLOT2_OP:
		case R_XTENSA_SLOT3_OP:
		case R_XTENSA_SLOT4_OP:
		case R_XTENSA_SLOT5_OP:
		case R_XTENSA_SLOT6_OP:
		case R_XTENSA_SLOT7_OP:
		case R_XTENSA_SLOT8_OP:
		case R_XTENSA_SLOT9_OP:
		case R_XTENSA_SLOT10_OP:
		case R_XTENSA_SLOT11_OP:
		case R_XTENSA_SLOT12_OP:
		case R_XTENSA_SLOT13_OP:
		case R_XTENSA_SLOT14_OP:
			pr_err("%s: unexpected FLIX relocation: %u\n",
			       mod->name,
			       ELF32_R_TYPE(rela[i].r_info));
			return -ENOEXEC;

		case R_XTENSA_SLOT0_ALT:
		case R_XTENSA_SLOT1_ALT:
		case R_XTENSA_SLOT2_ALT:
		case R_XTENSA_SLOT3_ALT:
		case R_XTENSA_SLOT4_ALT:
		case R_XTENSA_SLOT5_ALT:
		case R_XTENSA_SLOT6_ALT:
		case R_XTENSA_SLOT7_ALT:
		case R_XTENSA_SLOT8_ALT:
		case R_XTENSA_SLOT9_ALT:
		case R_XTENSA_SLOT10_ALT:
		case R_XTENSA_SLOT11_ALT:
		case R_XTENSA_SLOT12_ALT:
		case R_XTENSA_SLOT13_ALT:
		case R_XTENSA_SLOT14_ALT:
			pr_err("%s: unexpected ALT relocation: %u\n",
			       mod->name,
			       ELF32_R_TYPE(rela[i].r_info));
			return -ENOEXEC;

		default:
			pr_err("%s: unexpected relocation: %u\n",
			       mod->name,
			       ELF32_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}
