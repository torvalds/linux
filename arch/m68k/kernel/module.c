/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

#if 0
#define DEBUGP(fmt, ...) printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define DEBUGP(fmt, ...) no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

#ifdef CONFIG_MODULES

int apply_relocate(Elf32_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	unsigned int i;
	Elf32_Rel *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	uint32_t *location;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_68K_32:
			/* We add the value into the location given */
			*location += sym->st_value;
			break;
		case R_68K_PC32:
			/* Add the value, subtract its position */
			*location += sym->st_value - (uint32_t)location;
			break;
		default:
			pr_err("module %s: Unknown relocation: %u\n", me->name,
			       ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

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

	DEBUGP("Applying relocate_add section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_68K_32:
			/* We add the value into the location given */
			*location = rel[i].r_addend + sym->st_value;
			break;
		case R_68K_PC32:
			/* Add the value, subtract its position */
			*location = rel[i].r_addend + sym->st_value - (uint32_t)location;
			break;
		default:
			pr_err("module %s: Unknown relocation: %u\n", me->name,
			       ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *mod)
{
	module_fixup(mod, mod->arch.fixup_start, mod->arch.fixup_end);
	return 0;
}

#endif /* CONFIG_MODULES */

void module_fixup(struct module *mod, struct m68k_fixup_info *start,
		  struct m68k_fixup_info *end)
{
#ifdef CONFIG_MMU
	struct m68k_fixup_info *fixup;

	for (fixup = start; fixup < end; fixup++) {
		switch (fixup->type) {
		case m68k_fixup_memoffset:
			*(u32 *)fixup->addr = m68k_memoffset;
			break;
		case m68k_fixup_vnode_shift:
			*(u16 *)fixup->addr += m68k_virt_to_node_shift;
			break;
		}
	}
#endif
}
