/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Copyright (C) 2001 Rusty Russell.
 *  Copyright (C) 2003, 2004 Ralf Baechle (ralf@linux-mips.org)
 *  Copyright (C) 2005 Thiemo Seufer
 *  Copyright (C) 2015 Imagination Technologies Ltd.
 */

#include <linux/elf.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/moduleloader.h>

extern int apply_r_mips_none(struct module *me, u32 *location, Elf_Addr v);

static int apply_r_mips_32_rela(struct module *me, u32 *location, Elf_Addr v)
{
	*location = v;

	return 0;
}

static int apply_r_mips_26_rela(struct module *me, u32 *location, Elf_Addr v)
{
	if (v % 4) {
		pr_err("module %s: dangerous R_MIPS_26 RELA relocation\n",
		       me->name);
		return -ENOEXEC;
	}

	if ((v & 0xf0000000) != (((unsigned long)location + 4) & 0xf0000000)) {
		pr_err("module %s: relocation overflow\n", me->name);
		return -ENOEXEC;
	}

	*location = (*location & ~0x03ffffff) | ((v >> 2) & 0x03ffffff);

	return 0;
}

static int apply_r_mips_hi16_rela(struct module *me, u32 *location, Elf_Addr v)
{
	*location = (*location & 0xffff0000) |
		    ((((long long) v + 0x8000LL) >> 16) & 0xffff);

	return 0;
}

static int apply_r_mips_lo16_rela(struct module *me, u32 *location, Elf_Addr v)
{
	*location = (*location & 0xffff0000) | (v & 0xffff);

	return 0;
}

static int apply_r_mips_pc_rela(struct module *me, u32 *location, Elf_Addr v,
				unsigned bits)
{
	unsigned long mask = GENMASK(bits - 1, 0);
	unsigned long se_bits;
	long offset;

	if (v % 4) {
		pr_err("module %s: dangerous R_MIPS_PC%u RELA relocation\n",
		       me->name, bits);
		return -ENOEXEC;
	}

	offset = ((long)v - (long)location) >> 2;

	/* check the sign bit onwards are identical - ie. we didn't overflow */
	se_bits = (offset & BIT(bits - 1)) ? ~0ul : 0;
	if ((offset & ~mask) != (se_bits & ~mask)) {
		pr_err("module %s: relocation overflow\n", me->name);
		return -ENOEXEC;
	}

	*location = (*location & ~mask) | (offset & mask);

	return 0;
}

static int apply_r_mips_pc16_rela(struct module *me, u32 *location, Elf_Addr v)
{
	return apply_r_mips_pc_rela(me, location, v, 16);
}

static int apply_r_mips_pc21_rela(struct module *me, u32 *location, Elf_Addr v)
{
	return apply_r_mips_pc_rela(me, location, v, 21);
}

static int apply_r_mips_pc26_rela(struct module *me, u32 *location, Elf_Addr v)
{
	return apply_r_mips_pc_rela(me, location, v, 26);
}

static int apply_r_mips_64_rela(struct module *me, u32 *location, Elf_Addr v)
{
	*(Elf_Addr *)location = v;

	return 0;
}

static int apply_r_mips_higher_rela(struct module *me, u32 *location,
				    Elf_Addr v)
{
	*location = (*location & 0xffff0000) |
		    ((((long long) v + 0x80008000LL) >> 32) & 0xffff);

	return 0;
}

static int apply_r_mips_highest_rela(struct module *me, u32 *location,
				     Elf_Addr v)
{
	*location = (*location & 0xffff0000) |
		    ((((long long) v + 0x800080008000LL) >> 48) & 0xffff);

	return 0;
}

static int (*reloc_handlers_rela[]) (struct module *me, u32 *location,
				Elf_Addr v) = {
	[R_MIPS_NONE]		= apply_r_mips_none,
	[R_MIPS_32]		= apply_r_mips_32_rela,
	[R_MIPS_26]		= apply_r_mips_26_rela,
	[R_MIPS_HI16]		= apply_r_mips_hi16_rela,
	[R_MIPS_LO16]		= apply_r_mips_lo16_rela,
	[R_MIPS_PC16]		= apply_r_mips_pc16_rela,
	[R_MIPS_64]		= apply_r_mips_64_rela,
	[R_MIPS_HIGHER]		= apply_r_mips_higher_rela,
	[R_MIPS_HIGHEST]	= apply_r_mips_highest_rela,
	[R_MIPS_PC21_S2]	= apply_r_mips_pc21_rela,
	[R_MIPS_PC26_S2]	= apply_r_mips_pc26_rela,
};

int apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		       unsigned int symindex, unsigned int relsec,
		       struct module *me)
{
	Elf_Mips_Rela *rel = (void *) sechdrs[relsec].sh_addr;
	int (*handler)(struct module *me, u32 *location, Elf_Addr v);
	Elf_Sym *sym;
	u32 *location;
	unsigned int i, type;
	Elf_Addr v;
	int res;

	pr_debug("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf_Sym *)sechdrs[symindex].sh_addr
			+ ELF_MIPS_R_SYM(rel[i]);
		if (sym->st_value >= -MAX_ERRNO) {
			/* Ignore unresolved weak symbol */
			if (ELF_ST_BIND(sym->st_info) == STB_WEAK)
				continue;
			pr_warn("%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		type = ELF_MIPS_R_TYPE(rel[i]);

		if (type < ARRAY_SIZE(reloc_handlers_rela))
			handler = reloc_handlers_rela[type];
		else
			handler = NULL;

		if (!handler) {
			pr_err("%s: Unknown relocation type %u\n",
			       me->name, type);
			return -EINVAL;
		}

		v = sym->st_value + rel[i].r_addend;
		res = handler(me, location, v);
		if (res)
			return res;
	}

	return 0;
}
