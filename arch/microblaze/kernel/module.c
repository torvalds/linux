// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 */

#include <linux/export.h>
#include <linux/moduleloader.h>
#include <linux/kernel.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/pgtable.h>

#include <asm/cacheflush.h>

int apply_relocate_add(Elf32_Shdr *sechdrs, const char *strtab,
	unsigned int symindex, unsigned int relsec, struct module *module)
{

	unsigned int i;
	Elf32_Rela *rela = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	unsigned long int *location;
	unsigned long int value;

	pr_debug("Applying add relocation section %u to %u\n",
		relsec, sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rela); i++) {

		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr +
				rela[i].r_offset;
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr +
			ELF32_R_SYM(rela[i].r_info);
		value = sym->st_value + rela[i].r_addend;

		switch (ELF32_R_TYPE(rela[i].r_info)) {

		/*
		 * Be careful! mb-gcc / mb-ld splits the relocs between the
		 * text and the reloc table. In general this means we must
		 * read the current contents of (*location), add any offset
		 * then store the result back in
		 */

		case R_MICROBLAZE_32:
			*location = value;
			break;

		case R_MICROBLAZE_64:
			location[0] = (location[0] & 0xFFFF0000) |
					(value >> 16);
			location[1] = (location[1] & 0xFFFF0000) |
					(value & 0xFFFF);
			break;

		case R_MICROBLAZE_64_PCREL:
			value -= (unsigned long int)(location) + 4;
			location[0] = (location[0] & 0xFFFF0000) |
					(value >> 16);
			location[1] = (location[1] & 0xFFFF0000) |
					(value & 0xFFFF);
			pr_debug("R_MICROBLAZE_64_PCREL (%08lx)\n",
				value);
			break;

		case R_MICROBLAZE_32_PCREL_LO:
			pr_debug("R_MICROBLAZE_32_PCREL_LO\n");
			break;

		case R_MICROBLAZE_64_NONE:
			pr_debug("R_MICROBLAZE_64_NONE\n");
			break;

		case R_MICROBLAZE_NONE:
			pr_debug("R_MICROBLAZE_NONE\n");
			break;

		default:
			pr_err("module %s: Unknown relocation: %u\n",
				module->name,
				ELF32_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

int module_finalize(const Elf32_Ehdr *hdr, const Elf_Shdr *sechdrs,
		struct module *module)
{
	flush_dcache();
	return 0;
}
