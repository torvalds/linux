/* Kernel module help for sparc32.
 *
 * Copyright (C) 2001 Rusty Russell.
 * Copyright (C) 2002 David S. Miller.
 */

#include <linux/moduleloader.h>
#include <linux/kernel.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>

void *module_alloc(unsigned long size)
{
	void *ret;

	/* We handle the zero case fine, unlike vmalloc */
	if (size == 0)
		return NULL;

	ret = vmalloc(size);
	if (!ret)
		ret = ERR_PTR(-ENOMEM);
	else
		memset(ret, 0, size);

	return ret;
}

/* Free memory returned from module_core_alloc/module_init_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
           table entries. */
}

/* Make generic code ignore STT_REGISTER dummy undefined symbols,
 * and replace references to .func with func as in ppc64's dedotify.
 */
int module_frob_arch_sections(Elf_Ehdr *hdr,
			      Elf_Shdr *sechdrs,
			      char *secstrings,
			      struct module *mod)
{
	unsigned int symidx;
	Elf32_Sym *sym;
	char *strtab;
	int i;

	for (symidx = 0; sechdrs[symidx].sh_type != SHT_SYMTAB; symidx++) {
		if (symidx == hdr->e_shnum-1) {
			printk("%s: no symtab found.\n", mod->name);
			return -ENOEXEC;
		}
	}
	sym = (Elf32_Sym *)sechdrs[symidx].sh_addr;
	strtab = (char *)sechdrs[sechdrs[symidx].sh_link].sh_addr;

	for (i = 1; i < sechdrs[symidx].sh_size / sizeof(Elf_Sym); i++) {
		if (sym[i].st_shndx == SHN_UNDEF) {
			if (ELF32_ST_TYPE(sym[i].st_info) == STT_REGISTER)
				sym[i].st_shndx = SHN_ABS;
			else {
				char *name = strtab + sym[i].st_name;
				if (name[0] == '.')
					memmove(name, name+1, strlen(name));
			}
		}
	}
	return 0;
}

int apply_relocate(Elf32_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	printk(KERN_ERR "module %s: non-ADD RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
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
	u8 *location;
	u32 *loc32;

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		Elf32_Addr v;

		/* This is where to make the change */
		location = (u8 *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		loc32 = (u32 *) location;
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);
		v = sym->st_value + rel[i].r_addend;

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_SPARC_32:
			location[0] = v >> 24;
			location[1] = v >> 16;
			location[2] = v >>  8;
			location[3] = v >>  0;
			break;

		case R_SPARC_WDISP30:
			v -= (Elf32_Addr) location;
			*loc32 = (*loc32 & ~0x3fffffff) |
				((v >> 2) & 0x3fffffff);
			break;

		case R_SPARC_WDISP22:
			v -= (Elf32_Addr) location;
			*loc32 = (*loc32 & ~0x3fffff) |
				((v >> 2) & 0x3fffff);
			break;

		case R_SPARC_LO10:
			*loc32 = (*loc32 & ~0x3ff) | (v & 0x3ff);
			break;

		case R_SPARC_HI22:
			*loc32 = (*loc32 & ~0x3fffff) |
				((v >> 10) & 0x3fffff);
			break;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %x\n",
			       me->name,
			       (int) (ELF32_R_TYPE(rel[i].r_info) & 0xff));
			return -ENOEXEC;
		};
	}
	return 0;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	return 0;
}

void module_arch_cleanup(struct module *mod)
{
}
