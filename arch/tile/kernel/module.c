/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Based on i386 version, copyright (C) 2001 Rusty Russell.
 */

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/opcode-tile.h>
#include <asm/pgtable.h>
#include <asm/homecache.h>

#ifdef __tilegx__
# define Elf_Rela Elf64_Rela
# define ELF_R_SYM ELF64_R_SYM
# define ELF_R_TYPE ELF64_R_TYPE
#else
# define Elf_Rela Elf32_Rela
# define ELF_R_SYM ELF32_R_SYM
# define ELF_R_TYPE ELF32_R_TYPE
#endif

#ifdef MODULE_DEBUG
#define DEBUGP printk
#else
#define DEBUGP(fmt...)
#endif

/*
 * Allocate some address space in the range MEM_MODULE_START to
 * MEM_MODULE_END and populate it with memory.
 */
void *module_alloc(unsigned long size)
{
	struct page **pages;
	pgprot_t prot_rwx = __pgprot(_PAGE_KERNEL | _PAGE_KERNEL_EXEC);
	struct vm_struct *area;
	int i = 0;
	int npages;

	if (size == 0)
		return NULL;
	npages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	pages = kmalloc(npages * sizeof(struct page *), GFP_KERNEL);
	if (pages == NULL)
		return NULL;
	for (; i < npages; ++i) {
		pages[i] = alloc_page(GFP_KERNEL | __GFP_HIGHMEM);
		if (!pages[i])
			goto error;
	}

	area = __get_vm_area(size, VM_ALLOC, MEM_MODULE_START, MEM_MODULE_END);
	if (!area)
		goto error;

	if (map_vm_area(area, prot_rwx, &pages)) {
		vunmap(area->addr);
		goto error;
	}

	return area->addr;

error:
	while (--i >= 0)
		__free_page(pages[i]);
	kfree(pages);
	return NULL;
}


/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);

	/* Globally flush the L1 icache. */
	flush_remote(0, HV_FLUSH_EVICT_L1I, cpu_online_mask,
		     0, 0, 0, NULL, NULL, 0);

	/*
	 * FIXME: If module_region == mod->module_init, trim exception
	 * table entries.
	 */
}

#ifdef __tilegx__
/*
 * Validate that the high 16 bits of "value" is just the sign-extension of
 * the low 48 bits.
 */
static int validate_hw2_last(long value, struct module *me)
{
	if (((value << 16) >> 16) != value) {
		pr_warning("module %s: Out of range HW2_LAST value %#lx\n",
			   me->name, value);
		return 0;
	}
	return 1;
}

/*
 * Validate that "value" isn't too big to hold in a JumpOff relocation.
 */
static int validate_jumpoff(long value)
{
	/* Determine size of jump offset. */
	int shift = __builtin_clzl(get_JumpOff_X1(create_JumpOff_X1(-1)));

	/* Check to see if it fits into the relocation slot. */
	long f = get_JumpOff_X1(create_JumpOff_X1(value));
	f = (f << shift) >> shift;

	return f == value;
}
#endif

int apply_relocate_add(Elf_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	unsigned int i;
	Elf_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf_Sym *sym;
	u64 *location;
	unsigned long value;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/*
		 * This is the symbol it is referring to.
		 * Note that all undefined symbols have been resolved.
		 */
		sym = (Elf_Sym *)sechdrs[symindex].sh_addr
			+ ELF_R_SYM(rel[i].r_info);
		value = sym->st_value + rel[i].r_addend;

		switch (ELF_R_TYPE(rel[i].r_info)) {

#define MUNGE(func) (*location = ((*location & ~func(-1)) | func(value)))

#ifndef __tilegx__
		case R_TILE_32:
			*(uint32_t *)location = value;
			break;
		case R_TILE_IMM16_X0_HA:
			value = (value + 0x8000) >> 16;
			/*FALLTHROUGH*/
		case R_TILE_IMM16_X0_LO:
			MUNGE(create_Imm16_X0);
			break;
		case R_TILE_IMM16_X1_HA:
			value = (value + 0x8000) >> 16;
			/*FALLTHROUGH*/
		case R_TILE_IMM16_X1_LO:
			MUNGE(create_Imm16_X1);
			break;
		case R_TILE_JOFFLONG_X1:
			value -= (unsigned long) location;  /* pc-relative */
			value = (long) value >> 3;     /* count by instrs */
			MUNGE(create_JOffLong_X1);
			break;
#else
		case R_TILEGX_64:
			*location = value;
			break;
		case R_TILEGX_IMM16_X0_HW2_LAST:
			if (!validate_hw2_last(value, me))
				return -ENOEXEC;
			value >>= 16;
			/*FALLTHROUGH*/
		case R_TILEGX_IMM16_X0_HW1:
			value >>= 16;
			/*FALLTHROUGH*/
		case R_TILEGX_IMM16_X0_HW0:
			MUNGE(create_Imm16_X0);
			break;
		case R_TILEGX_IMM16_X1_HW2_LAST:
			if (!validate_hw2_last(value, me))
				return -ENOEXEC;
			value >>= 16;
			/*FALLTHROUGH*/
		case R_TILEGX_IMM16_X1_HW1:
			value >>= 16;
			/*FALLTHROUGH*/
		case R_TILEGX_IMM16_X1_HW0:
			MUNGE(create_Imm16_X1);
			break;
		case R_TILEGX_JUMPOFF_X1:
			value -= (unsigned long) location;  /* pc-relative */
			value = (long) value >> 3;     /* count by instrs */
			if (!validate_jumpoff(value)) {
				pr_warning("module %s: Out of range jump to"
					   " %#llx at %#llx (%p)\n", me->name,
					   sym->st_value + rel[i].r_addend,
					   rel[i].r_offset, location);
				return -ENOEXEC;
			}
			MUNGE(create_JumpOff_X1);
			break;
#endif

#undef MUNGE

		default:
			pr_err("module %s: Unknown relocation: %d\n",
			       me->name, (int) ELF_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}
