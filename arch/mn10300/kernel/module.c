/* MN10300 Kernel module helper routines
 *
 * Copyright (C) 2007, 2008, 2009 Red Hat, Inc. All Rights Reserved.
 * Written by Mark Salter (msalter@redhat.com)
 * - Derived from arch/i386/kernel/module.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public Licence as published by
 * the Free Software Foundation; either version 2 of the Licence, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public Licence
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/bug.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt, ...)
#endif

static void reloc_put16(uint8_t *p, uint32_t val)
{
	p[0] = val & 0xff;
	p[1] = (val >> 8) & 0xff;
}

static void reloc_put24(uint8_t *p, uint32_t val)
{
	reloc_put16(p, val);
	p[2] = (val >> 16) & 0xff;
}

static void reloc_put32(uint8_t *p, uint32_t val)
{
	reloc_put16(p, val);
	reloc_put16(p+2, val >> 16);
}

/*
 * apply a RELA relocation
 */
int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	unsigned int i, sym_diff_seen = 0;
	Elf32_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	Elf32_Addr relocation, sym_diff_val = 0;
	uint8_t *location;
	uint32_t value;

	DEBUGP("Applying relocate section %u to %u\n",
	       relsec, sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* this is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;

		/* this is the symbol the relocation is referring to (note that
		 * all undefined symbols have been resolved by the caller) */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);

		/* this is the adjustment to be made */
		relocation = sym->st_value + rel[i].r_addend;

		if (sym_diff_seen) {
			switch (ELF32_R_TYPE(rel[i].r_info)) {
			case R_MN10300_32:
			case R_MN10300_24:
			case R_MN10300_16:
			case R_MN10300_8:
				relocation -= sym_diff_val;
				sym_diff_seen = 0;
				break;
			default:
				printk(KERN_ERR "module %s: Unexpected SYM_DIFF relocation: %u\n",
				       me->name, ELF32_R_TYPE(rel[i].r_info));
				return -ENOEXEC;
			}
		}

		switch (ELF32_R_TYPE(rel[i].r_info)) {
			/* for the first four relocation types, we simply
			 * store the adjustment at the location given */
		case R_MN10300_32:
			reloc_put32(location, relocation);
			break;
		case R_MN10300_24:
			reloc_put24(location, relocation);
			break;
		case R_MN10300_16:
			reloc_put16(location, relocation);
			break;
		case R_MN10300_8:
			*location = relocation;
			break;

			/* for the next three relocation types, we write the
			 * adjustment with the address subtracted over the
			 * value at the location given */
		case R_MN10300_PCREL32:
			value = relocation - (uint32_t) location;
			reloc_put32(location, value);
			break;
		case R_MN10300_PCREL16:
			value = relocation - (uint32_t) location;
			reloc_put16(location, value);
			break;
		case R_MN10300_PCREL8:
			*location = relocation - (uint32_t) location;
			break;

		case R_MN10300_SYM_DIFF:
			/* This is used to adjust the next reloc as required
			 * by relaxation. */
			sym_diff_seen = 1;
			sym_diff_val = sym->st_value;
			break;

		case R_MN10300_ALIGN:
			/* Just ignore the ALIGN relocs.
			 * Only interesting if kernel performed relaxation. */
			continue;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	if (sym_diff_seen) {
		printk(KERN_ERR "module %s: Nothing follows SYM_DIFF relocation: %u\n",
				       me->name, ELF32_R_TYPE(rel[i].r_info));
		return -ENOEXEC;
	}
	return 0;
}
