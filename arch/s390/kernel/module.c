/*
 *  arch/s390/kernel/module.c - Kernel module help for s390.
 *
 *  S390 version
 *    Copyright (C) 2002, 2003 IBM Deutschland Entwicklung GmbH,
 *			       IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  based on i386 version
 *    Copyright (C) 2001 Rusty Russell.
 *
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
 */
#include <linux/module.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/moduleloader.h>
#include <linux/bug.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

#ifndef CONFIG_64BIT
#define PLT_ENTRY_SIZE 12
#else /* CONFIG_64BIT */
#define PLT_ENTRY_SIZE 20
#endif /* CONFIG_64BIT */

void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc(size);
}

/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
}

static void
check_rela(Elf_Rela *rela, struct module *me)
{
	struct mod_arch_syminfo *info;

	info = me->arch.syminfo + ELF_R_SYM (rela->r_info);
	switch (ELF_R_TYPE (rela->r_info)) {
	case R_390_GOT12:	/* 12 bit GOT offset.  */
	case R_390_GOT16:	/* 16 bit GOT offset.  */
	case R_390_GOT20:	/* 20 bit GOT offset.  */
	case R_390_GOT32:	/* 32 bit GOT offset.  */
	case R_390_GOT64:	/* 64 bit GOT offset.  */
	case R_390_GOTENT:	/* 32 bit PC rel. to GOT entry shifted by 1. */
	case R_390_GOTPLT12:	/* 12 bit offset to jump slot.	*/
	case R_390_GOTPLT16:	/* 16 bit offset to jump slot.  */
	case R_390_GOTPLT20:	/* 20 bit offset to jump slot.  */
	case R_390_GOTPLT32:	/* 32 bit offset to jump slot.  */
	case R_390_GOTPLT64:	/* 64 bit offset to jump slot.	*/
	case R_390_GOTPLTENT:	/* 32 bit rel. offset to jump slot >> 1. */
		if (info->got_offset == -1UL) {
			info->got_offset = me->arch.got_size;
			me->arch.got_size += sizeof(void*);
		}
		break;
	case R_390_PLT16DBL:	/* 16 bit PC rel. PLT shifted by 1.  */
	case R_390_PLT32DBL:	/* 32 bit PC rel. PLT shifted by 1.  */
	case R_390_PLT32:	/* 32 bit PC relative PLT address.  */
	case R_390_PLT64:	/* 64 bit PC relative PLT address.  */
	case R_390_PLTOFF16:	/* 16 bit offset from GOT to PLT. */
	case R_390_PLTOFF32:	/* 32 bit offset from GOT to PLT. */
	case R_390_PLTOFF64:	/* 16 bit offset from GOT to PLT. */
		if (info->plt_offset == -1UL) {
			info->plt_offset = me->arch.plt_size;
			me->arch.plt_size += PLT_ENTRY_SIZE;
		}
		break;
	case R_390_COPY:
	case R_390_GLOB_DAT:
	case R_390_JMP_SLOT:
	case R_390_RELATIVE:
		/* Only needed if we want to support loading of 
		   modules linked with -shared. */
		break;
	}
}

/*
 * Account for GOT and PLT relocations. We can't add sections for
 * got and plt but we can increase the core module size.
 */
int
module_frob_arch_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
			  char *secstrings, struct module *me)
{
	Elf_Shdr *symtab;
	Elf_Sym *symbols;
	Elf_Rela *rela;
	char *strings;
	int nrela, i, j;

	/* Find symbol table and string table. */
	symtab = NULL;
	for (i = 0; i < hdr->e_shnum; i++)
		switch (sechdrs[i].sh_type) {
		case SHT_SYMTAB:
			symtab = sechdrs + i;
			break;
		}
	if (!symtab) {
		printk(KERN_ERR "module %s: no symbol table\n", me->name);
		return -ENOEXEC;
	}

	/* Allocate one syminfo structure per symbol. */
	me->arch.nsyms = symtab->sh_size / sizeof(Elf_Sym);
	me->arch.syminfo = vmalloc(me->arch.nsyms *
				   sizeof(struct mod_arch_syminfo));
	if (!me->arch.syminfo)
		return -ENOMEM;
	symbols = (void *) hdr + symtab->sh_offset;
	strings = (void *) hdr + sechdrs[symtab->sh_link].sh_offset;
	for (i = 0; i < me->arch.nsyms; i++) {
		if (symbols[i].st_shndx == SHN_UNDEF &&
		    strcmp(strings + symbols[i].st_name,
			   "_GLOBAL_OFFSET_TABLE_") == 0)
			/* "Define" it as absolute. */
			symbols[i].st_shndx = SHN_ABS;
		me->arch.syminfo[i].got_offset = -1UL;
		me->arch.syminfo[i].plt_offset = -1UL;
		me->arch.syminfo[i].got_initialized = 0;
		me->arch.syminfo[i].plt_initialized = 0;
	}

	/* Search for got/plt relocations. */
	me->arch.got_size = me->arch.plt_size = 0;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (sechdrs[i].sh_type != SHT_RELA)
			continue;
		nrela = sechdrs[i].sh_size / sizeof(Elf_Rela);
		rela = (void *) hdr + sechdrs[i].sh_offset;
		for (j = 0; j < nrela; j++)
			check_rela(rela + j, me);
	}

	/* Increase core size by size of got & plt and set start
	   offsets for got and plt. */
	me->core_size = ALIGN(me->core_size, 4);
	me->arch.got_offset = me->core_size;
	me->core_size += me->arch.got_size;
	me->arch.plt_offset = me->core_size;
	me->core_size += me->arch.plt_size;
	return 0;
}

int
apply_relocate(Elf_Shdr *sechdrs, const char *strtab, unsigned int symindex,
	       unsigned int relsec, struct module *me)
{
	printk(KERN_ERR "module %s: RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
}

static int
apply_rela(Elf_Rela *rela, Elf_Addr base, Elf_Sym *symtab, 
	   struct module *me)
{
	struct mod_arch_syminfo *info;
	Elf_Addr loc, val;
	int r_type, r_sym;

	/* This is where to make the change */
	loc = base + rela->r_offset;
	/* This is the symbol it is referring to.  Note that all
	   undefined symbols have been resolved.  */
	r_sym = ELF_R_SYM(rela->r_info);
	r_type = ELF_R_TYPE(rela->r_info);
	info = me->arch.syminfo + r_sym;
	val = symtab[r_sym].st_value;

	switch (r_type) {
	case R_390_8:		/* Direct 8 bit.   */
	case R_390_12:		/* Direct 12 bit.  */
	case R_390_16:		/* Direct 16 bit.  */
	case R_390_20:		/* Direct 20 bit.  */
	case R_390_32:		/* Direct 32 bit.  */
	case R_390_64:		/* Direct 64 bit.  */
		val += rela->r_addend;
		if (r_type == R_390_8)
			*(unsigned char *) loc = val;
		else if (r_type == R_390_12)
			*(unsigned short *) loc = (val & 0xfff) |
				(*(unsigned short *) loc & 0xf000);
		else if (r_type == R_390_16)
			*(unsigned short *) loc = val;
		else if (r_type == R_390_20)
			*(unsigned int *) loc =
				(*(unsigned int *) loc & 0xf00000ff) |
				(val & 0xfff) << 16 | (val & 0xff000) >> 4;
		else if (r_type == R_390_32)
			*(unsigned int *) loc = val;
		else if (r_type == R_390_64)
			*(unsigned long *) loc = val;
		break;
	case R_390_PC16:	/* PC relative 16 bit.  */
	case R_390_PC16DBL:	/* PC relative 16 bit shifted by 1.  */
	case R_390_PC32DBL:	/* PC relative 32 bit shifted by 1.  */
	case R_390_PC32:	/* PC relative 32 bit.  */
	case R_390_PC64:	/* PC relative 64 bit.	*/
		val += rela->r_addend - loc;
		if (r_type == R_390_PC16)
			*(unsigned short *) loc = val;
		else if (r_type == R_390_PC16DBL)
			*(unsigned short *) loc = val >> 1;
		else if (r_type == R_390_PC32DBL)
			*(unsigned int *) loc = val >> 1;
		else if (r_type == R_390_PC32)
			*(unsigned int *) loc = val;
		else if (r_type == R_390_PC64)
			*(unsigned long *) loc = val;
		break;
	case R_390_GOT12:	/* 12 bit GOT offset.  */
	case R_390_GOT16:	/* 16 bit GOT offset.  */
	case R_390_GOT20:	/* 20 bit GOT offset.  */
	case R_390_GOT32:	/* 32 bit GOT offset.  */
	case R_390_GOT64:	/* 64 bit GOT offset.  */
	case R_390_GOTENT:	/* 32 bit PC rel. to GOT entry shifted by 1. */
	case R_390_GOTPLT12:	/* 12 bit offset to jump slot.	*/
	case R_390_GOTPLT20:	/* 20 bit offset to jump slot.  */
	case R_390_GOTPLT16:	/* 16 bit offset to jump slot.  */
	case R_390_GOTPLT32:	/* 32 bit offset to jump slot.  */
	case R_390_GOTPLT64:	/* 64 bit offset to jump slot.	*/
	case R_390_GOTPLTENT:	/* 32 bit rel. offset to jump slot >> 1. */
		if (info->got_initialized == 0) {
			Elf_Addr *gotent;

			gotent = me->module_core + me->arch.got_offset +
				info->got_offset;
			*gotent = val;
			info->got_initialized = 1;
		}
		val = info->got_offset + rela->r_addend;
		if (r_type == R_390_GOT12 ||
		    r_type == R_390_GOTPLT12)
			*(unsigned short *) loc = (val & 0xfff) |
				(*(unsigned short *) loc & 0xf000);
		else if (r_type == R_390_GOT16 ||
			 r_type == R_390_GOTPLT16)
			*(unsigned short *) loc = val;
		else if (r_type == R_390_GOT20 ||
			 r_type == R_390_GOTPLT20)
			*(unsigned int *) loc =
				(*(unsigned int *) loc & 0xf00000ff) |
				(val & 0xfff) << 16 | (val & 0xff000) >> 4;
		else if (r_type == R_390_GOT32 ||
			 r_type == R_390_GOTPLT32)
			*(unsigned int *) loc = val;
		else if (r_type == R_390_GOTENT ||
			 r_type == R_390_GOTPLTENT)
			*(unsigned int *) loc =
				(val + (Elf_Addr) me->module_core - loc) >> 1;
		else if (r_type == R_390_GOT64 ||
			 r_type == R_390_GOTPLT64)
			*(unsigned long *) loc = val;
		break;
	case R_390_PLT16DBL:	/* 16 bit PC rel. PLT shifted by 1.  */
	case R_390_PLT32DBL:	/* 32 bit PC rel. PLT shifted by 1.  */
	case R_390_PLT32:	/* 32 bit PC relative PLT address.  */
	case R_390_PLT64:	/* 64 bit PC relative PLT address.  */
	case R_390_PLTOFF16:	/* 16 bit offset from GOT to PLT. */
	case R_390_PLTOFF32:	/* 32 bit offset from GOT to PLT. */
	case R_390_PLTOFF64:	/* 16 bit offset from GOT to PLT. */
		if (info->plt_initialized == 0) {
			unsigned int *ip;
			ip = me->module_core + me->arch.plt_offset +
				info->plt_offset;
#ifndef CONFIG_64BIT
			ip[0] = 0x0d105810; /* basr 1,0; l 1,6(1); br 1 */
			ip[1] = 0x100607f1;
			ip[2] = val;
#else /* CONFIG_64BIT */
			ip[0] = 0x0d10e310; /* basr 1,0; lg 1,10(1); br 1 */
			ip[1] = 0x100a0004;
			ip[2] = 0x07f10000;
			ip[3] = (unsigned int) (val >> 32);
			ip[4] = (unsigned int) val;
#endif /* CONFIG_64BIT */
			info->plt_initialized = 1;
		}
		if (r_type == R_390_PLTOFF16 ||
		    r_type == R_390_PLTOFF32 ||
		    r_type == R_390_PLTOFF64)
			val = me->arch.plt_offset - me->arch.got_offset +
				info->plt_offset + rela->r_addend;
		else {
			if (!((r_type == R_390_PLT16DBL &&
			       val - loc + 0xffffUL < 0x1ffffeUL) ||
			      (r_type == R_390_PLT32DBL &&
			       val - loc + 0xffffffffULL < 0x1fffffffeULL)))
				val = (Elf_Addr) me->module_core +
					me->arch.plt_offset +
					info->plt_offset;
			val += rela->r_addend - loc;
		}
		if (r_type == R_390_PLT16DBL)
			*(unsigned short *) loc = val >> 1;
		else if (r_type == R_390_PLTOFF16)
			*(unsigned short *) loc = val;
		else if (r_type == R_390_PLT32DBL)
			*(unsigned int *) loc = val >> 1;
		else if (r_type == R_390_PLT32 ||
			 r_type == R_390_PLTOFF32)
			*(unsigned int *) loc = val;
		else if (r_type == R_390_PLT64 ||
			 r_type == R_390_PLTOFF64)
			*(unsigned long *) loc = val;
		break;
	case R_390_GOTOFF16:	/* 16 bit offset to GOT.  */
	case R_390_GOTOFF32:	/* 32 bit offset to GOT.  */
	case R_390_GOTOFF64:	/* 64 bit offset to GOT. */
		val = val + rela->r_addend -
			((Elf_Addr) me->module_core + me->arch.got_offset);
		if (r_type == R_390_GOTOFF16)
			*(unsigned short *) loc = val;
		else if (r_type == R_390_GOTOFF32)
			*(unsigned int *) loc = val;
		else if (r_type == R_390_GOTOFF64)
			*(unsigned long *) loc = val;
		break;
	case R_390_GOTPC:	/* 32 bit PC relative offset to GOT. */
	case R_390_GOTPCDBL:	/* 32 bit PC rel. off. to GOT shifted by 1. */
		val = (Elf_Addr) me->module_core + me->arch.got_offset +
			rela->r_addend - loc;
		if (r_type == R_390_GOTPC)
			*(unsigned int *) loc = val;
		else if (r_type == R_390_GOTPCDBL)
			*(unsigned int *) loc = val >> 1;
		break;
	case R_390_COPY:
	case R_390_GLOB_DAT:	/* Create GOT entry.  */
	case R_390_JMP_SLOT:	/* Create PLT entry.  */
	case R_390_RELATIVE:	/* Adjust by program base.  */
		/* Only needed if we want to support loading of 
		   modules linked with -shared. */
		break;
	default:
		printk(KERN_ERR "module %s: Unknown relocation: %u\n",
		       me->name, r_type);
		return -ENOEXEC;
	}
	return 0;
}

int
apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		   unsigned int symindex, unsigned int relsec,
		   struct module *me)
{
	Elf_Addr base;
	Elf_Sym *symtab;
	Elf_Rela *rela;
	unsigned long i, n;
	int rc;

	DEBUGP("Applying relocate section %u to %u\n",
	       relsec, sechdrs[relsec].sh_info);
	base = sechdrs[sechdrs[relsec].sh_info].sh_addr;
	symtab = (Elf_Sym *) sechdrs[symindex].sh_addr;
	rela = (Elf_Rela *) sechdrs[relsec].sh_addr;
	n = sechdrs[relsec].sh_size / sizeof(Elf_Rela);

	for (i = 0; i < n; i++, rela++) {
		rc = apply_rela(rela, base, symtab, me);
		if (rc)
			return rc;
	}
	return 0;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	vfree(me->arch.syminfo);
	return module_bug_finalize(hdr, sechdrs, me);
}

void module_arch_cleanup(struct module *mod)
{
	module_bug_cleanup(mod);
}
