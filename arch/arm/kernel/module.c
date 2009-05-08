/*
 *  linux/arch/arm/kernel/module.c
 *
 *  Copyright (C) 2002 Russell King.
 *  Modified for nommu by Hyok S. Choi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Module allocation method suggested by Andi Kleen.
 */
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>

#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/unwind.h>

#ifdef CONFIG_XIP_KERNEL
/*
 * The XIP kernel text is mapped in the module area for modules and
 * some other stuff to work without any indirect relocations.
 * MODULES_VADDR is redefined here and not in asm/memory.h to avoid
 * recompiling the whole kernel when CONFIG_XIP_KERNEL is turned on/off.
 */
#undef MODULES_VADDR
#define MODULES_VADDR	(((unsigned long)_etext + ~PGDIR_MASK) & PGDIR_MASK)
#endif

#ifdef CONFIG_MMU
void *module_alloc(unsigned long size)
{
	struct vm_struct *area;

	size = PAGE_ALIGN(size);
	if (!size)
		return NULL;

	area = __get_vm_area(size, VM_ALLOC, MODULES_VADDR, MODULES_END);
	if (!area)
		return NULL;

	return __vmalloc_area(area, GFP_KERNEL, PAGE_KERNEL_EXEC);
}
#else /* CONFIG_MMU */
void *module_alloc(unsigned long size)
{
	return size == 0 ? NULL : vmalloc(size);
}
#endif /* !CONFIG_MMU */

void module_free(struct module *module, void *region)
{
	vfree(region);
}

int module_frob_arch_sections(Elf_Ehdr *hdr,
			      Elf_Shdr *sechdrs,
			      char *secstrings,
			      struct module *mod)
{
#ifdef CONFIG_ARM_UNWIND
	Elf_Shdr *s, *sechdrs_end = sechdrs + hdr->e_shnum;

	for (s = sechdrs; s < sechdrs_end; s++) {
		if (strcmp(".ARM.exidx.init.text", secstrings + s->sh_name) == 0)
			mod->arch.unw_sec_init = s;
		else if (strcmp(".ARM.exidx.devinit.text", secstrings + s->sh_name) == 0)
			mod->arch.unw_sec_devinit = s;
		else if (strcmp(".ARM.exidx", secstrings + s->sh_name) == 0)
			mod->arch.unw_sec_core = s;
		else if (strcmp(".init.text", secstrings + s->sh_name) == 0)
			mod->arch.sec_init_text = s;
		else if (strcmp(".devinit.text", secstrings + s->sh_name) == 0)
			mod->arch.sec_devinit_text = s;
		else if (strcmp(".text", secstrings + s->sh_name) == 0)
			mod->arch.sec_core_text = s;
	}
#endif
	return 0;
}

int
apply_relocate(Elf32_Shdr *sechdrs, const char *strtab, unsigned int symindex,
	       unsigned int relindex, struct module *module)
{
	Elf32_Shdr *symsec = sechdrs + symindex;
	Elf32_Shdr *relsec = sechdrs + relindex;
	Elf32_Shdr *dstsec = sechdrs + relsec->sh_info;
	Elf32_Rel *rel = (void *)relsec->sh_addr;
	unsigned int i;

	for (i = 0; i < relsec->sh_size / sizeof(Elf32_Rel); i++, rel++) {
		unsigned long loc;
		Elf32_Sym *sym;
		s32 offset;

		offset = ELF32_R_SYM(rel->r_info);
		if (offset < 0 || offset > (symsec->sh_size / sizeof(Elf32_Sym))) {
			printk(KERN_ERR "%s: bad relocation, section %d reloc %d\n",
				module->name, relindex, i);
			return -ENOEXEC;
		}

		sym = ((Elf32_Sym *)symsec->sh_addr) + offset;

		if (rel->r_offset < 0 || rel->r_offset > dstsec->sh_size - sizeof(u32)) {
			printk(KERN_ERR "%s: out of bounds relocation, "
				"section %d reloc %d offset %d size %d\n",
				module->name, relindex, i, rel->r_offset,
				dstsec->sh_size);
			return -ENOEXEC;
		}

		loc = dstsec->sh_addr + rel->r_offset;

		switch (ELF32_R_TYPE(rel->r_info)) {
		case R_ARM_NONE:
			/* ignore */
			break;

		case R_ARM_ABS32:
			*(u32 *)loc += sym->st_value;
			break;

		case R_ARM_PC24:
		case R_ARM_CALL:
		case R_ARM_JUMP24:
			offset = (*(u32 *)loc & 0x00ffffff) << 2;
			if (offset & 0x02000000)
				offset -= 0x04000000;

			offset += sym->st_value - loc;
			if (offset & 3 ||
			    offset <= (s32)0xfe000000 ||
			    offset >= (s32)0x02000000) {
				printk(KERN_ERR
				       "%s: relocation out of range, section "
				       "%d reloc %d sym '%s'\n", module->name,
				       relindex, i, strtab + sym->st_name);
				return -ENOEXEC;
			}

			offset >>= 2;

			*(u32 *)loc &= 0xff000000;
			*(u32 *)loc |= offset & 0x00ffffff;
			break;

	       case R_ARM_V4BX:
		       /* Preserve Rm and the condition code. Alter
			* other bits to re-code instruction as
			* MOV PC,Rm.
			*/
		       *(u32 *)loc &= 0xf000000f;
		       *(u32 *)loc |= 0x01a0f000;
		       break;

		case R_ARM_PREL31:
			offset = *(u32 *)loc + sym->st_value - loc;
			*(u32 *)loc = offset & 0x7fffffff;
			break;

		case R_ARM_MOVW_ABS_NC:
		case R_ARM_MOVT_ABS:
			offset = *(u32 *)loc;
			offset = ((offset & 0xf0000) >> 4) | (offset & 0xfff);
			offset = (offset ^ 0x8000) - 0x8000;

			offset += sym->st_value;
			if (ELF32_R_TYPE(rel->r_info) == R_ARM_MOVT_ABS)
				offset >>= 16;

			*(u32 *)loc &= 0xfff0f000;
			*(u32 *)loc |= ((offset & 0xf000) << 4) |
					(offset & 0x0fff);
			break;

		default:
			printk(KERN_ERR "%s: unknown relocation: %u\n",
			       module->name, ELF32_R_TYPE(rel->r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

int
apply_relocate_add(Elf32_Shdr *sechdrs, const char *strtab,
		   unsigned int symindex, unsigned int relsec, struct module *module)
{
	printk(KERN_ERR "module %s: ADD RELOCATION unsupported\n",
	       module->name);
	return -ENOEXEC;
}

#ifdef CONFIG_ARM_UNWIND
static void register_unwind_tables(struct module *mod)
{
	if (mod->arch.unw_sec_init && mod->arch.sec_init_text)
		mod->arch.unwind_init =
			unwind_table_add(mod->arch.unw_sec_init->sh_addr,
					 mod->arch.unw_sec_init->sh_size,
					 mod->arch.sec_init_text->sh_addr,
					 mod->arch.sec_init_text->sh_size);
	if (mod->arch.unw_sec_devinit && mod->arch.sec_devinit_text)
		mod->arch.unwind_devinit =
			unwind_table_add(mod->arch.unw_sec_devinit->sh_addr,
					 mod->arch.unw_sec_devinit->sh_size,
					 mod->arch.sec_devinit_text->sh_addr,
					 mod->arch.sec_devinit_text->sh_size);
	if (mod->arch.unw_sec_core && mod->arch.sec_core_text)
		mod->arch.unwind_core =
			unwind_table_add(mod->arch.unw_sec_core->sh_addr,
					 mod->arch.unw_sec_core->sh_size,
					 mod->arch.sec_core_text->sh_addr,
					 mod->arch.sec_core_text->sh_size);
}

static void unregister_unwind_tables(struct module *mod)
{
	unwind_table_del(mod->arch.unwind_init);
	unwind_table_del(mod->arch.unwind_devinit);
	unwind_table_del(mod->arch.unwind_core);
}
#else
static inline void register_unwind_tables(struct module *mod) { }
static inline void unregister_unwind_tables(struct module *mod) { }
#endif

int
module_finalize(const Elf32_Ehdr *hdr, const Elf_Shdr *sechdrs,
		struct module *module)
{
	register_unwind_tables(module);
	return 0;
}

void
module_arch_cleanup(struct module *mod)
{
	unregister_unwind_tables(mod);
}
