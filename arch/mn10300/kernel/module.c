/* MN10300 Kernel module helper routines
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
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

/*
 * allocate storage for a module
 */
void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc_exec(size);
}

/*
 * free memory returned from module_alloc()
 */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
	 * table entries. */
}

/*
 * allow the arch to fix up the section table
 * - we don't need anything special
 */
int module_frob_arch_sections(Elf_Ehdr *hdr,
			      Elf_Shdr *sechdrs,
			      char *secstrings,
			      struct module *mod)
{
	return 0;
}

static uint32_t reloc_get16(uint8_t *p)
{
	return p[0] | (p[1] << 8);
}

static uint32_t reloc_get24(uint8_t *p)
{
	return reloc_get16(p) | (p[2] << 16);
}

static uint32_t reloc_get32(uint8_t *p)
{
	return reloc_get16(p) | (reloc_get16(p+2) << 16);
}

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
 * apply a REL relocation
 */
int apply_relocate(Elf32_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	printk(KERN_ERR "module %s: RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
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
	unsigned int i;
	Elf32_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	Elf32_Addr relocation;
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

		switch (ELF32_R_TYPE(rel[i].r_info)) {
			/* for the first four relocation types, we add the
			 * adjustment into the value at the location given */
		case R_MN10300_32:
			value = reloc_get32(location);
			value += relocation;
			reloc_put32(location, value);
			break;
		case R_MN10300_24:
			value = reloc_get24(location);
			value += relocation;
			reloc_put24(location, value);
			break;
		case R_MN10300_16:
			value = reloc_get16(location);
			value += relocation;
			reloc_put16(location, value);
			break;
		case R_MN10300_8:
			*location += relocation;
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

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

/*
 * finish loading the module
 */
int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	return module_bug_finalize(hdr, sechdrs, me);
}

/*
 * finish clearing the module
 */
void module_arch_cleanup(struct module *mod)
{
	module_bug_cleanup(mod);
}
