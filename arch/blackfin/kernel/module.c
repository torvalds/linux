/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#define pr_fmt(fmt) "module %s: " fmt, mod->name

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/dma.h>
#include <asm/cacheflush.h>
#include <linux/uaccess.h>

/* Transfer the section to the L1 memory */
int
module_frob_arch_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
			  char *secstrings, struct module *mod)
{
	/*
	 * XXX: sechdrs are vmalloced in kernel/module.c
	 * and would be vfreed just after module is loaded,
	 * so we hack to keep the only information we needed
	 * in mod->arch to correctly free L1 I/D sram later.
	 * NOTE: this breaks the semantic of mod->arch structure.
	 */
	Elf_Shdr *s, *sechdrs_end = sechdrs + hdr->e_shnum;
	void *dest;

	for (s = sechdrs; s < sechdrs_end; ++s) {
		const char *shname = secstrings + s->sh_name;

		if (s->sh_size == 0)
			continue;

		if (!strcmp(".l1.text", shname) ||
		    (!strcmp(".text", shname) &&
		     (hdr->e_flags & EF_BFIN_CODE_IN_L1))) {

			dest = l1_inst_sram_alloc(s->sh_size);
			mod->arch.text_l1 = dest;
			if (dest == NULL) {
				pr_err("L1 inst memory allocation failed\n");
				return -1;
			}
			dma_memcpy(dest, (void *)s->sh_addr, s->sh_size);

		} else if (!strcmp(".l1.data", shname) ||
		           (!strcmp(".data", shname) &&
		            (hdr->e_flags & EF_BFIN_DATA_IN_L1))) {

			dest = l1_data_sram_alloc(s->sh_size);
			mod->arch.data_a_l1 = dest;
			if (dest == NULL) {
				pr_err("L1 data memory allocation failed\n");
				return -1;
			}
			memcpy(dest, (void *)s->sh_addr, s->sh_size);

		} else if (!strcmp(".l1.bss", shname) ||
		           (!strcmp(".bss", shname) &&
		            (hdr->e_flags & EF_BFIN_DATA_IN_L1))) {

			dest = l1_data_sram_zalloc(s->sh_size);
			mod->arch.bss_a_l1 = dest;
			if (dest == NULL) {
				pr_err("L1 data memory allocation failed\n");
				return -1;
			}

		} else if (!strcmp(".l1.data.B", shname)) {

			dest = l1_data_B_sram_alloc(s->sh_size);
			mod->arch.data_b_l1 = dest;
			if (dest == NULL) {
				pr_err("L1 data memory allocation failed\n");
				return -1;
			}
			memcpy(dest, (void *)s->sh_addr, s->sh_size);

		} else if (!strcmp(".l1.bss.B", shname)) {

			dest = l1_data_B_sram_alloc(s->sh_size);
			mod->arch.bss_b_l1 = dest;
			if (dest == NULL) {
				pr_err("L1 data memory allocation failed\n");
				return -1;
			}
			memset(dest, 0, s->sh_size);

		} else if (!strcmp(".l2.text", shname) ||
		           (!strcmp(".text", shname) &&
		            (hdr->e_flags & EF_BFIN_CODE_IN_L2))) {

			dest = l2_sram_alloc(s->sh_size);
			mod->arch.text_l2 = dest;
			if (dest == NULL) {
				pr_err("L2 SRAM allocation failed\n");
				return -1;
			}
			memcpy(dest, (void *)s->sh_addr, s->sh_size);

		} else if (!strcmp(".l2.data", shname) ||
		           (!strcmp(".data", shname) &&
		            (hdr->e_flags & EF_BFIN_DATA_IN_L2))) {

			dest = l2_sram_alloc(s->sh_size);
			mod->arch.data_l2 = dest;
			if (dest == NULL) {
				pr_err("L2 SRAM allocation failed\n");
				return -1;
			}
			memcpy(dest, (void *)s->sh_addr, s->sh_size);

		} else if (!strcmp(".l2.bss", shname) ||
		           (!strcmp(".bss", shname) &&
		            (hdr->e_flags & EF_BFIN_DATA_IN_L2))) {

			dest = l2_sram_zalloc(s->sh_size);
			mod->arch.bss_l2 = dest;
			if (dest == NULL) {
				pr_err("L2 SRAM allocation failed\n");
				return -1;
			}

		} else
			continue;

		s->sh_flags &= ~SHF_ALLOC;
		s->sh_addr = (unsigned long)dest;
	}

	return 0;
}

/*************************************************************************/
/* FUNCTION : apply_relocate_add                                         */
/* ABSTRACT : Blackfin specific relocation handling for the loadable     */
/*            modules. Modules are expected to be .o files.              */
/*            Arithmetic relocations are handled.                        */
/*            We do not expect LSETUP to be split and hence is not       */
/*            handled.                                                   */
/*            R_BFIN_BYTE and R_BFIN_BYTE2 are also not handled as the   */
/*            gas does not generate it.                                  */
/*************************************************************************/
int
apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		   unsigned int symindex, unsigned int relsec,
		   struct module *mod)
{
	unsigned int i;
	Elf32_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	unsigned long location, value, size;

	pr_debug("applying relocate section %u to %u\n",
		relsec, sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = sechdrs[sechdrs[relsec].sh_info].sh_addr +
		           rel[i].r_offset;

		/* This is the symbol it is referring to. Note that all
		   undefined symbols have been resolved. */
		sym = (Elf32_Sym *) sechdrs[symindex].sh_addr
		    + ELF32_R_SYM(rel[i].r_info);
		value = sym->st_value;
		value += rel[i].r_addend;

#ifdef CONFIG_SMP
		if (location >= COREB_L1_DATA_A_START) {
			pr_err("cannot relocate in L1: %u (SMP kernel)\n",
				ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
#endif

		pr_debug("location is %lx, value is %lx type is %d\n",
			location, value, ELF32_R_TYPE(rel[i].r_info));

		switch (ELF32_R_TYPE(rel[i].r_info)) {

		case R_BFIN_HUIMM16:
			value >>= 16;
		case R_BFIN_LUIMM16:
		case R_BFIN_RIMM16:
			size = 2;
			break;
		case R_BFIN_BYTE4_DATA:
			size = 4;
			break;

		case R_BFIN_PCREL24:
		case R_BFIN_PCREL24_JUMP_L:
		case R_BFIN_PCREL12_JUMP:
		case R_BFIN_PCREL12_JUMP_S:
		case R_BFIN_PCREL10:
			pr_err("unsupported relocation: %u (no -mlong-calls?)\n",
				ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;

		default:
			pr_err("unknown relocation: %u\n",
				ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}

		switch (bfin_mem_access_type(location, size)) {
		case BFIN_MEM_ACCESS_CORE:
		case BFIN_MEM_ACCESS_CORE_ONLY:
			memcpy((void *)location, &value, size);
			break;
		case BFIN_MEM_ACCESS_DMA:
			dma_memcpy((void *)location, &value, size);
			break;
		case BFIN_MEM_ACCESS_ITEST:
			isram_memcpy((void *)location, &value, size);
			break;
		default:
			pr_err("invalid relocation for %#lx\n", location);
			return -ENOEXEC;
		}
	}

	return 0;
}

int
module_finalize(const Elf_Ehdr * hdr,
		const Elf_Shdr * sechdrs, struct module *mod)
{
	unsigned int i, strindex = 0, symindex = 0;
	char *secstrings;
	long err = 0;

	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

	for (i = 1; i < hdr->e_shnum; i++) {
		/* Internal symbols and strings. */
		if (sechdrs[i].sh_type == SHT_SYMTAB) {
			symindex = i;
			strindex = sechdrs[i].sh_link;
		}
	}

	for (i = 1; i < hdr->e_shnum; i++) {
		const char *strtab = (char *)sechdrs[strindex].sh_addr;
		unsigned int info = sechdrs[i].sh_info;
		const char *shname = secstrings + sechdrs[i].sh_name;

		/* Not a valid relocation section? */
		if (info >= hdr->e_shnum)
			continue;

		/* Only support RELA relocation types */
		if (sechdrs[i].sh_type != SHT_RELA)
			continue;

		if (!strcmp(".rela.l2.text", shname) ||
		    !strcmp(".rela.l1.text", shname) ||
		    (!strcmp(".rela.text", shname) &&
			 (hdr->e_flags & (EF_BFIN_CODE_IN_L1 | EF_BFIN_CODE_IN_L2)))) {

			err = apply_relocate_add((Elf_Shdr *) sechdrs, strtab,
					   symindex, i, mod);
			if (err < 0)
				return -ENOEXEC;
		}
	}

	return 0;
}

void module_arch_cleanup(struct module *mod)
{
	l1_inst_sram_free(mod->arch.text_l1);
	l1_data_A_sram_free(mod->arch.data_a_l1);
	l1_data_A_sram_free(mod->arch.bss_a_l1);
	l1_data_B_sram_free(mod->arch.data_b_l1);
	l1_data_B_sram_free(mod->arch.bss_b_l1);
	l2_sram_free(mod->arch.text_l2);
	l2_sram_free(mod->arch.data_l2);
	l2_sram_free(mod->arch.bss_l2);
}
