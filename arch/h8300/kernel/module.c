// SPDX-License-Identifier: GPL-2.0
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	unsigned int i;
	Elf32_Rela *rela = (void *)sechdrs[relsec].sh_addr;

	pr_debug("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rela); i++) {
		/* This is where to make the change */
		uint32_t *loc =
			(uint32_t *)(sechdrs[sechdrs[relsec].sh_info].sh_addr
					     + rela[i].r_offset);
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		Elf32_Sym *sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rela[i].r_info);
		uint32_t v = sym->st_value + rela[i].r_addend;

		switch (ELF32_R_TYPE(rela[i].r_info)) {
		case R_H8_DIR24R8:
			loc = (uint32_t *)((uint32_t)loc - 1);
			*loc = (*loc & 0xff000000) | ((*loc & 0xffffff) + v);
			break;
		case R_H8_DIR24A8:
			if (ELF32_R_SYM(rela[i].r_info))
				*loc += v;
			break;
		case R_H8_DIR32:
		case R_H8_DIR32A16:
			*loc += v;
			break;
		case R_H8_PCREL16:
			v -= (unsigned long)loc + 2;
			if ((Elf32_Sword)v > 0x7fff ||
			    (Elf32_Sword)v < -(Elf32_Sword)0x8000)
				goto overflow;
			else
				*(unsigned short *)loc = v;
			break;
		case R_H8_PCREL8:
			v -= (unsigned long)loc + 1;
			if ((Elf32_Sword)v > 0x7f ||
			    (Elf32_Sword)v < -(Elf32_Sword)0x80)
				goto overflow;
			else
				*(unsigned char *)loc = v;
			break;
		default:
			pr_err("module %s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
 overflow:
	pr_err("module %s: relocation offset overflow: %08x\n",
	       me->name, rela[i].r_offset);
	return -ENOEXEC;
}
