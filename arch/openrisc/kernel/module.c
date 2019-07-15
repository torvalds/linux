// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC module.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#include <linux/moduleloader.h>
#include <linux/elf.h>

int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	unsigned int i;
	Elf32_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	uint32_t *location;
	uint32_t value;

	pr_debug("Applying relocate section %u to %u\n", relsec,
		 sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;

		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);
		value = sym->st_value + rel[i].r_addend;

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_OR32_32:
			*location = value;
			break;
		case R_OR32_CONST:
			*((uint16_t *)location + 1) = value;
			break;
		case R_OR32_CONSTH:
			*((uint16_t *)location + 1) = value >> 16;
			break;
		case R_OR32_JUMPTARG:
			value -= (uint32_t)location;
			value >>= 2;
			value &= 0x03ffffff;
			value |= *location & 0xfc000000;
			*location = value;
			break;
		default:
			pr_err("module %s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			break;
		}
	}

	return 0;
}
