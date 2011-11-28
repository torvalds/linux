/*
 * arch/score/kernel/module.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/moduleloader.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

int apply_relocate(Elf_Shdr *sechdrs, const char *strtab,
		unsigned int symindex, unsigned int relindex,
		struct module *me)
{
	Elf32_Shdr *symsec = sechdrs + symindex;
	Elf32_Shdr *relsec = sechdrs + relindex;
	Elf32_Shdr *dstsec = sechdrs + relsec->sh_info;
	Elf32_Rel *rel = (void *)relsec->sh_addr;
	unsigned int i;

	for (i = 0; i < relsec->sh_size / sizeof(Elf32_Rel); i++, rel++) {
		unsigned long loc;
		Elf32_Sym *sym;
		s32 r_offset;

		r_offset = ELF32_R_SYM(rel->r_info);
		if ((r_offset < 0) ||
		    (r_offset > (symsec->sh_size / sizeof(Elf32_Sym)))) {
			printk(KERN_ERR "%s: bad relocation, section %d reloc %d\n",
				me->name, relindex, i);
				return -ENOEXEC;
		}

		sym = ((Elf32_Sym *)symsec->sh_addr) + r_offset;

		if ((rel->r_offset < 0) ||
		    (rel->r_offset > dstsec->sh_size - sizeof(u32))) {
			printk(KERN_ERR "%s: out of bounds relocation, "
				"section %d reloc %d offset %d size %d\n",
				me->name, relindex, i, rel->r_offset,
				dstsec->sh_size);
			return -ENOEXEC;
		}

		loc = dstsec->sh_addr + rel->r_offset;
		switch (ELF32_R_TYPE(rel->r_info)) {
		case R_SCORE_NONE:
			break;
		case R_SCORE_ABS32:
			*(unsigned long *)loc += sym->st_value;
			break;
		case R_SCORE_HI16:
			break;
		case R_SCORE_LO16: {
			unsigned long hi16_offset, offset;
			unsigned long uvalue;
			unsigned long temp, temp_hi;
			temp_hi = *((unsigned long *)loc - 1);
			temp = *(unsigned long *)loc;

			hi16_offset = (((((temp_hi) >> 16) & 0x3) << 15) |
					((temp_hi) & 0x7fff)) >> 1;
			offset = ((temp >> 16 & 0x03) << 15) |
					((temp & 0x7fff) >> 1);
			offset = (hi16_offset << 16) | (offset & 0xffff);
			uvalue = sym->st_value + offset;
			hi16_offset = (uvalue >> 16) << 1;

			temp_hi = ((temp_hi) & (~(0x37fff))) |
					(hi16_offset & 0x7fff) |
					((hi16_offset << 1) & 0x30000);
			*((unsigned long *)loc - 1) = temp_hi;

			offset = (uvalue & 0xffff) << 1;
			temp = (temp & (~(0x37fff))) | (offset & 0x7fff) |
				((offset << 1) & 0x30000);
			*(unsigned long *)loc = temp;
			break;
		}
		case R_SCORE_24: {
			unsigned long hi16_offset, offset;
			unsigned long uvalue;
			unsigned long temp;

			temp = *(unsigned long *)loc;
			offset = (temp & 0x03FF7FFE);
			hi16_offset = (offset & 0xFFFF0000);
			offset = (hi16_offset | ((offset & 0xFFFF) << 1)) >> 2;

			uvalue = (sym->st_value + offset) >> 1;
			uvalue = uvalue & 0x00ffffff;

			temp = (temp & 0xfc008001) |
				((uvalue << 2) & 0x3ff0000) |
				((uvalue & 0x3fff) << 1);
			*(unsigned long *)loc = temp;
			break;
		}
		default:
			printk(KERN_ERR "%s: unknown relocation: %u\n",
				me->name, ELF32_R_TYPE(rel->r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}

int apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		unsigned int symindex, unsigned int relsec,
		struct module *me)
{
	/* Non-standard return value... most other arch's return -ENOEXEC
	 * for an unsupported relocation variant
	 */
	return 0;
}

/* Given an address, look for it in the module exception tables. */
const struct exception_table_entry *search_module_dbetables(unsigned long addr)
{
	return NULL;
}
