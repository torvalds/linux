// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel module loader for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <asm/module.h>
#include <linux/elf.h>
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/vmalloc.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

/*
 * module_frob_arch_sections - tweak got/plt sections.
 * @hdr - pointer to elf header
 * @sechdrs - pointer to elf load section headers
 * @secstrings - symbol names
 * @mod - pointer to module
 */
int module_frob_arch_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
				char *secstrings,
				struct module *mod)
{
	unsigned int i;
	int found = 0;

	/* Look for .plt and/or .got.plt and/or .init.plt sections */
	for (i = 0; i < hdr->e_shnum; i++) {
		DEBUGP("Section %d is %s\n", i,
		       secstrings + sechdrs[i].sh_name);
		if (strcmp(secstrings + sechdrs[i].sh_name, ".plt") == 0)
			found = i+1;
		if (strcmp(secstrings + sechdrs[i].sh_name, ".got.plt") == 0)
			found = i+1;
		if (strcmp(secstrings + sechdrs[i].sh_name, ".rela.plt") == 0)
			found = i+1;
	}

	/* At this time, we don't support modules comiled with -shared */
	if (found) {
		printk(KERN_WARNING
			"Module '%s' contains unexpected .plt/.got sections.\n",
			mod->name);
		/*  return -ENOEXEC;  */
	}

	return 0;
}

/*
 * apply_relocate_add - perform rela relocations.
 * @sechdrs - pointer to section headers
 * @strtab - some sort of start address?
 * @symindex - symbol index offset or something?
 * @relsec - address to relocate to?
 * @module - pointer to module
 *
 * Perform rela relocations.
 */
int apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
			unsigned int symindex, unsigned int relsec,
			struct module *module)
{
	unsigned int i;
	Elf32_Sym *sym;
	uint32_t *location;
	uint32_t value;
	unsigned int nrelocs = sechdrs[relsec].sh_size / sizeof(Elf32_Rela);
	Elf32_Rela *rela = (void *)sechdrs[relsec].sh_addr;
	Elf32_Word sym_info = sechdrs[relsec].sh_info;
	Elf32_Sym *sym_base = (Elf32_Sym *) sechdrs[symindex].sh_addr;
	void *loc_base = (void *) sechdrs[sym_info].sh_addr;

	DEBUGP("Applying relocations in section %u to section %u base=%p\n",
	       relsec, sym_info, loc_base);

	for (i = 0; i < nrelocs; i++) {

		/* Symbol to relocate */
		sym = sym_base + ELF32_R_SYM(rela[i].r_info);

		/* Where to make the change */
		location = loc_base + rela[i].r_offset;

		/* `Everything is relative'. */
		value = sym->st_value + rela[i].r_addend;

		DEBUGP("%d: value=%08x loc=%p reloc=%d symbol=%s\n",
		       i, value, location, ELF32_R_TYPE(rela[i].r_info),
		       sym->st_name ?
		       &strtab[sym->st_name] : "(anonymous)");

		switch (ELF32_R_TYPE(rela[i].r_info)) {
		case R_HEXAGON_B22_PCREL: {
			int dist = (int)(value - (uint32_t)location);
			if ((dist < -0x00800000) ||
			    (dist >= 0x00800000)) {
				printk(KERN_ERR
				       "%s: %s: %08x=%08x-%08x %s\n",
				       module->name,
				       "R_HEXAGON_B22_PCREL reloc out of range",
				       dist, value, (uint32_t)location,
				       sym->st_name ?
				       &strtab[sym->st_name] : "(anonymous)");
				return -ENOEXEC;
			}
			DEBUGP("B22_PCREL contents: %08X.\n", *location);
			*location &= ~0x01ff3fff;
			*location |= 0x00003fff & dist;
			*location |= 0x01ff0000 & (dist<<2);
			DEBUGP("Contents after reloc: %08x\n", *location);
			break;
		}
		case R_HEXAGON_HI16:
			value = (value>>16) & 0xffff;
			fallthrough;
		case R_HEXAGON_LO16:
			*location &= ~0x00c03fff;
			*location |= value & 0x3fff;
			*location |= (value & 0xc000) << 8;
			break;
		case R_HEXAGON_32:
			*location = value;
			break;
		case R_HEXAGON_32_PCREL:
			*location = value - (uint32_t)location;
			break;
		case R_HEXAGON_PLT_B22_PCREL:
		case R_HEXAGON_GOTOFF_LO16:
		case R_HEXAGON_GOTOFF_HI16:
			printk(KERN_ERR "%s: GOT/PLT relocations unsupported\n",
			       module->name);
			return -ENOEXEC;
		default:
			printk(KERN_ERR "%s: unknown relocation: %u\n",
			       module->name,
			       ELF32_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}
