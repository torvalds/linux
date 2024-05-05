/*
 * Kernel module support for Nios II.
 *
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *   Written by Wentao Xu <xuwentao@microtronix.com>
 * Copyright (C) 2001, 2003 Rusty Russell
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

#include <asm/cacheflush.h>

void *module_alloc(unsigned long size)
{
	return __vmalloc_node_range(size, 1, MODULES_VADDR, MODULES_END,
				    GFP_KERNEL, PAGE_KERNEL_EXEC,
				    VM_FLUSH_RESET_PERMS, NUMA_NO_NODE,
				    __builtin_return_address(0));
}

int apply_relocate_add(Elf32_Shdr *sechdrs, const char *strtab,
			unsigned int symindex, unsigned int relsec,
			struct module *mod)
{
	unsigned int i;
	Elf32_Rela *rela = (void *)sechdrs[relsec].sh_addr;

	pr_debug("Applying relocate section %u to %u\n", relsec,
		 sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rela); i++) {
		/* This is where to make the change */
		uint32_t word;
		uint32_t *loc
			= ((void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			   + rela[i].r_offset);
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		Elf32_Sym *sym
			= ((Elf32_Sym *)sechdrs[symindex].sh_addr
				+ ELF32_R_SYM(rela[i].r_info));
		uint32_t v = sym->st_value + rela[i].r_addend;

		pr_debug("reltype %d 0x%x name:<%s>\n",
			ELF32_R_TYPE(rela[i].r_info),
			rela[i].r_offset, strtab + sym->st_name);

		switch (ELF32_R_TYPE(rela[i].r_info)) {
		case R_NIOS2_NONE:
			break;
		case R_NIOS2_BFD_RELOC_32:
			*loc += v;
			break;
		case R_NIOS2_PCREL16:
			v -= (uint32_t)loc + 4;
			if ((int32_t)v > 0x7fff ||
				(int32_t)v < -(int32_t)0x8000) {
				pr_err("module %s: relocation overflow\n",
					mod->name);
				return -ENOEXEC;
			}
			word = *loc;
			*loc = ((((word >> 22) << 16) | (v & 0xffff)) << 6) |
				(word & 0x3f);
			break;
		case R_NIOS2_CALL26:
			if (v & 3) {
				pr_err("module %s: dangerous relocation\n",
					mod->name);
				return -ENOEXEC;
			}
			if ((v >> 28) != ((uint32_t)loc >> 28)) {
				pr_err("module %s: relocation overflow\n",
					mod->name);
				return -ENOEXEC;
			}
			*loc = (*loc & 0x3f) | ((v >> 2) << 6);
			break;
		case R_NIOS2_HI16:
			word = *loc;
			*loc = ((((word >> 22) << 16) |
				((v >> 16) & 0xffff)) << 6) | (word & 0x3f);
			break;
		case R_NIOS2_LO16:
			word = *loc;
			*loc = ((((word >> 22) << 16) | (v & 0xffff)) << 6) |
					(word & 0x3f);
			break;
		case R_NIOS2_HIADJ16:
			{
				Elf32_Addr word2;

				word = *loc;
				word2 = ((v >> 16) + ((v >> 15) & 1)) & 0xffff;
				*loc = ((((word >> 22) << 16) | word2) << 6) |
						(word & 0x3f);
			}
			break;

		default:
			pr_err("module %s: Unknown reloc: %u\n",
				mod->name, ELF32_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}

int module_finalize(const Elf_Ehdr *hdr, const Elf_Shdr *sechdrs,
			struct module *me)
{
	flush_cache_all();
	return 0;
}
