/*
 * AVR32-specific kernel module loader
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * GOT initialization parts are based on the s390 version
 *   Copyright (C) 2002, 2003 IBM Deutschland Entwicklung GmbH,
 *                            IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bug.h>
#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/vmalloc.h>

void module_arch_freeing_init(struct module *mod)
{
	vfree(mod->arch.syminfo);
	mod->arch.syminfo = NULL;
}

static inline int check_rela(Elf32_Rela *rela, struct module *module,
			     char *strings, Elf32_Sym *symbols)
{
	struct mod_arch_syminfo *info;

	info = module->arch.syminfo + ELF32_R_SYM(rela->r_info);
	switch (ELF32_R_TYPE(rela->r_info)) {
	case R_AVR32_GOT32:
	case R_AVR32_GOT16:
	case R_AVR32_GOT8:
	case R_AVR32_GOT21S:
	case R_AVR32_GOT18SW:	/* mcall */
	case R_AVR32_GOT16S:	/* ld.w */
		if (rela->r_addend != 0) {
			printk(KERN_ERR
			       "GOT relocation against %s at offset %u with addend\n",
			       strings + symbols[ELF32_R_SYM(rela->r_info)].st_name,
			       rela->r_offset);
			return -ENOEXEC;
		}
		if (info->got_offset == -1UL) {
			info->got_offset = module->arch.got_size;
			module->arch.got_size += sizeof(void *);
		}
		pr_debug("GOT[%3lu] %s\n", info->got_offset,
			 strings + symbols[ELF32_R_SYM(rela->r_info)].st_name);
		break;
	}

	return 0;
}

int module_frob_arch_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
			      char *secstrings, struct module *module)
{
	Elf32_Shdr *symtab;
	Elf32_Sym *symbols;
	Elf32_Rela *rela;
	char *strings;
	int nrela, i, j;
	int ret;

	/* Find the symbol table */
	symtab = NULL;
	for (i = 0; i < hdr->e_shnum; i++)
		switch (sechdrs[i].sh_type) {
		case SHT_SYMTAB:
			symtab = &sechdrs[i];
			break;
		}
	if (!symtab) {
		printk(KERN_ERR "module %s: no symbol table\n", module->name);
		return -ENOEXEC;
	}

	/* Allocate room for one syminfo structure per symbol. */
	module->arch.nsyms = symtab->sh_size / sizeof(Elf_Sym);
	module->arch.syminfo = vmalloc(module->arch.nsyms
				   * sizeof(struct mod_arch_syminfo));
	if (!module->arch.syminfo)
		return -ENOMEM;

	symbols = (void *)hdr + symtab->sh_offset;
	strings = (void *)hdr + sechdrs[symtab->sh_link].sh_offset;
	for (i = 0; i < module->arch.nsyms; i++) {
		if (symbols[i].st_shndx == SHN_UNDEF &&
		    strcmp(strings + symbols[i].st_name,
			   "_GLOBAL_OFFSET_TABLE_") == 0)
			/* "Define" it as absolute. */
			symbols[i].st_shndx = SHN_ABS;
		module->arch.syminfo[i].got_offset = -1UL;
		module->arch.syminfo[i].got_initialized = 0;
	}

	/* Allocate GOT entries for symbols that need it. */
	module->arch.got_size = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (sechdrs[i].sh_type != SHT_RELA)
			continue;
		nrela = sechdrs[i].sh_size / sizeof(Elf32_Rela);
		rela = (void *)hdr + sechdrs[i].sh_offset;
		for (j = 0; j < nrela; j++) {
			ret = check_rela(rela + j, module,
					 strings, symbols);
			if (ret)
				goto out_free_syminfo;
		}
	}

	/*
	 * Increase core size to make room for GOT and set start
	 * offset for GOT.
	 */
	module->core_layout.size = ALIGN(module->core_layout.size, 4);
	module->arch.got_offset = module->core_layout.size;
	module->core_layout.size += module->arch.got_size;

	return 0;

out_free_syminfo:
	vfree(module->arch.syminfo);
	module->arch.syminfo = NULL;

	return ret;
}

static inline int reloc_overflow(struct module *module, const char *reloc_name,
				 Elf32_Addr relocation)
{
	printk(KERN_ERR "module %s: Value %lx does not fit relocation %s\n",
	       module->name, (unsigned long)relocation, reloc_name);
	return -ENOEXEC;
}

#define get_u16(loc)		(*((uint16_t *)loc))
#define put_u16(loc, val)	(*((uint16_t *)loc) = (val))

int apply_relocate_add(Elf32_Shdr *sechdrs, const char *strtab,
		       unsigned int symindex, unsigned int relindex,
		       struct module *module)
{
	Elf32_Shdr *symsec = sechdrs + symindex;
	Elf32_Shdr *relsec = sechdrs + relindex;
	Elf32_Shdr *dstsec = sechdrs + relsec->sh_info;
	Elf32_Rela *rel = (void *)relsec->sh_addr;
	unsigned int i;
	int ret = 0;

	for (i = 0; i < relsec->sh_size / sizeof(Elf32_Rela); i++, rel++) {
		struct mod_arch_syminfo *info;
		Elf32_Sym *sym;
		Elf32_Addr relocation;
		uint32_t *location;
		uint32_t value;

		location = (void *)dstsec->sh_addr + rel->r_offset;
		sym = (Elf32_Sym *)symsec->sh_addr + ELF32_R_SYM(rel->r_info);
		relocation = sym->st_value + rel->r_addend;

		info = module->arch.syminfo + ELF32_R_SYM(rel->r_info);

		/* Initialize GOT entry if necessary */
		switch (ELF32_R_TYPE(rel->r_info)) {
		case R_AVR32_GOT32:
		case R_AVR32_GOT16:
		case R_AVR32_GOT8:
		case R_AVR32_GOT21S:
		case R_AVR32_GOT18SW:
		case R_AVR32_GOT16S:
			if (!info->got_initialized) {
				Elf32_Addr *gotent;

				gotent = (module->core_layout.base
					  + module->arch.got_offset
					  + info->got_offset);
				*gotent = relocation;
				info->got_initialized = 1;
			}

			relocation = info->got_offset;
			break;
		}

		switch (ELF32_R_TYPE(rel->r_info)) {
		case R_AVR32_32:
		case R_AVR32_32_CPENT:
			*location = relocation;
			break;
		case R_AVR32_22H_PCREL:
			relocation -= (Elf32_Addr)location;
			if ((relocation & 0xffe00001) != 0
			    && (relocation & 0xffc00001) != 0xffc00000)
				return reloc_overflow(module,
						      "R_AVR32_22H_PCREL",
						      relocation);
			relocation >>= 1;

			value = *location;
			value = ((value & 0xe1ef0000)
				 | (relocation & 0xffff)
				 | ((relocation & 0x10000) << 4)
				 | ((relocation & 0x1e0000) << 8));
			*location = value;
			break;
		case R_AVR32_11H_PCREL:
			relocation -= (Elf32_Addr)location;
			if ((relocation & 0xfffffc01) != 0
			    && (relocation & 0xfffff801) != 0xfffff800)
				return reloc_overflow(module,
						      "R_AVR32_11H_PCREL",
						      relocation);
			value = get_u16(location);
			value = ((value & 0xf00c)
				 | ((relocation & 0x1fe) << 3)
				 | ((relocation & 0x600) >> 9));
			put_u16(location, value);
			break;
		case R_AVR32_9H_PCREL:
			relocation -= (Elf32_Addr)location;
			if ((relocation & 0xffffff01) != 0
			    && (relocation & 0xfffffe01) != 0xfffffe00)
				return reloc_overflow(module,
						      "R_AVR32_9H_PCREL",
						      relocation);
			value = get_u16(location);
			value = ((value & 0xf00f)
				 | ((relocation & 0x1fe) << 3));
			put_u16(location, value);
			break;
		case R_AVR32_9UW_PCREL:
			relocation -= ((Elf32_Addr)location) & 0xfffffffc;
			if ((relocation & 0xfffffc03) != 0)
				return reloc_overflow(module,
						      "R_AVR32_9UW_PCREL",
						      relocation);
			value = get_u16(location);
			value = ((value & 0xf80f)
				 | ((relocation & 0x1fc) << 2));
			put_u16(location, value);
			break;
		case R_AVR32_GOTPC:
			/*
			 * R6 = PC - (PC - GOT)
			 *
			 * At this point, relocation contains the
			 * value of PC.  Just subtract the value of
			 * GOT, and we're done.
			 */
			pr_debug("GOTPC: PC=0x%x, got_offset=0x%lx, core=0x%p\n",
				 relocation, module->arch.got_offset,
				 module->core_layout.base);
			relocation -= ((unsigned long)module->core_layout.base
				       + module->arch.got_offset);
			*location = relocation;
			break;
		case R_AVR32_GOT18SW:
			if ((relocation & 0xfffe0003) != 0
			    && (relocation & 0xfffc0000) != 0xfffc0000)
				return reloc_overflow(module, "R_AVR32_GOT18SW",
						     relocation);
			relocation >>= 2;
			/* fall through */
		case R_AVR32_GOT16S:
			if ((relocation & 0xffff8000) != 0
			    && (relocation & 0xffff0000) != 0xffff0000)
				return reloc_overflow(module, "R_AVR32_GOT16S",
						      relocation);
			pr_debug("GOT reloc @ 0x%x -> %u\n",
				 rel->r_offset, relocation);
			value = *location;
			value = ((value & 0xffff0000)
				 | (relocation & 0xffff));
			*location = value;
			break;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %u\n",
			       module->name, ELF32_R_TYPE(rel->r_info));
			return -ENOEXEC;
		}
	}

	return ret;
}
