// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/module.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/moduleloader.h>
#include <linux/pgtable.h>

void *module_alloc(unsigned long size)
{
	return __vmalloc_node_range(size, 1, MODULES_VADDR, MODULES_END,
				    GFP_KERNEL, PAGE_KERNEL, 0, NUMA_NO_NODE,
				    __builtin_return_address(0));
}

void module_free(struct module *module, void *region)
{
	vfree(region);
}

int module_frob_arch_sections(Elf_Ehdr * hdr,
			      Elf_Shdr * sechdrs,
			      char *secstrings, struct module *mod)
{
	return 0;
}

void do_reloc16(unsigned int val, unsigned int *loc, unsigned int val_mask,
		unsigned int val_shift, unsigned int loc_mask,
		unsigned int partial_in_place, unsigned int swap)
{
	unsigned int tmp = 0, tmp2 = 0;

	__asm__ __volatile__("\tlhi.bi\t%0, [%2], 0\n"
			     "\tbeqz\t%3, 1f\n"
			     "\twsbh\t%0, %1\n"
			     "1:\n":"=r"(tmp):"0"(tmp), "r"(loc), "r"(swap)
	    );

	tmp2 = tmp & loc_mask;
	if (partial_in_place) {
		tmp &= (~loc_mask);
		tmp =
		    tmp2 | ((tmp + ((val & val_mask) >> val_shift)) & val_mask);
	} else {
		tmp = tmp2 | ((val & val_mask) >> val_shift);
	}

	__asm__ __volatile__("\tbeqz\t%3, 2f\n"
			     "\twsbh\t%0, %1\n"
			     "2:\n"
			     "\tshi.bi\t%0, [%2], 0\n":"=r"(tmp):"0"(tmp),
			     "r"(loc), "r"(swap)
	    );
}

void do_reloc32(unsigned int val, unsigned int *loc, unsigned int val_mask,
		unsigned int val_shift, unsigned int loc_mask,
		unsigned int partial_in_place, unsigned int swap)
{
	unsigned int tmp = 0, tmp2 = 0;

	__asm__ __volatile__("\tlmw.bi\t%0, [%2], %0, 0\n"
			     "\tbeqz\t%3, 1f\n"
			     "\twsbh\t%0, %1\n"
			     "\trotri\t%0, %1, 16\n"
			     "1:\n":"=r"(tmp):"0"(tmp), "r"(loc), "r"(swap)
	    );

	tmp2 = tmp & loc_mask;
	if (partial_in_place) {
		tmp &= (~loc_mask);
		tmp =
		    tmp2 | ((tmp + ((val & val_mask) >> val_shift)) & val_mask);
	} else {
		tmp = tmp2 | ((val & val_mask) >> val_shift);
	}

	__asm__ __volatile__("\tbeqz\t%3, 2f\n"
			     "\twsbh\t%0, %1\n"
			     "\trotri\t%0, %1, 16\n"
			     "2:\n"
			     "\tsmw.bi\t%0, [%2], %0, 0\n":"=r"(tmp):"0"(tmp),
			     "r"(loc), "r"(swap)
	    );
}

static inline int exceed_limit(int offset, unsigned int val_mask,
			       struct module *module, Elf32_Rela * rel,
			       unsigned int relindex, unsigned int reloc_order)
{
	int abs_off = offset < 0 ? ~offset : offset;

	if (abs_off & (~val_mask)) {
		pr_err("\n%s: relocation type %d out of range.\n"
		       "please rebuild the kernel module with gcc option \"-Wa,-mno-small-text\".\n",
		       module->name, ELF32_R_TYPE(rel->r_info));
		pr_err("section %d reloc %d offset 0x%x relative 0x%x.\n",
		       relindex, reloc_order, rel->r_offset, offset);
		return true;
	}
	return false;
}

#ifdef __NDS32_EL__
#define NEED_SWAP 1
#else
#define NEED_SWAP 0
#endif

int
apply_relocate_add(Elf32_Shdr * sechdrs, const char *strtab,
		   unsigned int symindex, unsigned int relindex,
		   struct module *module)
{
	Elf32_Shdr *symsec = sechdrs + symindex;
	Elf32_Shdr *relsec = sechdrs + relindex;
	Elf32_Shdr *dstsec = sechdrs + relsec->sh_info;
	Elf32_Rela *rel = (void *)relsec->sh_addr;
	unsigned int i;

	for (i = 0; i < relsec->sh_size / sizeof(Elf32_Rela); i++, rel++) {
		Elf32_Addr *loc;
		Elf32_Sym *sym;
		Elf32_Addr v;
		s32 offset;

		offset = ELF32_R_SYM(rel->r_info);
		if (offset < 0
		    || offset > (symsec->sh_size / sizeof(Elf32_Sym))) {
			pr_err("%s: bad relocation\n", module->name);
			pr_err("section %d reloc %d\n", relindex, i);
			return -ENOEXEC;
		}

		sym = ((Elf32_Sym *) symsec->sh_addr) + offset;

		if (rel->r_offset < 0
		    || rel->r_offset > dstsec->sh_size - sizeof(u16)) {
			pr_err("%s: out of bounds relocation\n", module->name);
			pr_err("section %d reloc %d offset 0x%0x size %d\n",
			       relindex, i, rel->r_offset, dstsec->sh_size);
			return -ENOEXEC;
		}

		loc = (Elf32_Addr *) (dstsec->sh_addr + rel->r_offset);
		v = sym->st_value + rel->r_addend;

		switch (ELF32_R_TYPE(rel->r_info)) {
		case R_NDS32_NONE:
		case R_NDS32_INSN16:
		case R_NDS32_LABEL:
		case R_NDS32_LONGCALL1:
		case R_NDS32_LONGCALL2:
		case R_NDS32_LONGCALL3:
		case R_NDS32_LONGCALL4:
		case R_NDS32_LONGJUMP1:
		case R_NDS32_LONGJUMP2:
		case R_NDS32_LONGJUMP3:
		case R_NDS32_9_FIXED_RELA:
		case R_NDS32_15_FIXED_RELA:
		case R_NDS32_17_FIXED_RELA:
		case R_NDS32_25_FIXED_RELA:
		case R_NDS32_LOADSTORE:
		case R_NDS32_DWARF2_OP1_RELA:
		case R_NDS32_DWARF2_OP2_RELA:
		case R_NDS32_DWARF2_LEB_RELA:
		case R_NDS32_RELA_NOP_MIX ... R_NDS32_RELA_NOP_MAX:
			break;

		case R_NDS32_32_RELA:
			do_reloc32(v, loc, 0xffffffff, 0, 0, 0, 0);
			break;

		case R_NDS32_HI20_RELA:
			do_reloc32(v, loc, 0xfffff000, 12, 0xfff00000, 0,
				   NEED_SWAP);
			break;

		case R_NDS32_LO12S3_RELA:
			do_reloc32(v, loc, 0x00000fff, 3, 0xfffff000, 0,
				   NEED_SWAP);
			break;

		case R_NDS32_LO12S2_RELA:
			do_reloc32(v, loc, 0x00000fff, 2, 0xfffff000, 0,
				   NEED_SWAP);
			break;

		case R_NDS32_LO12S1_RELA:
			do_reloc32(v, loc, 0x00000fff, 1, 0xfffff000, 0,
				   NEED_SWAP);
			break;

		case R_NDS32_LO12S0_RELA:
		case R_NDS32_LO12S0_ORI_RELA:
			do_reloc32(v, loc, 0x00000fff, 0, 0xfffff000, 0,
				   NEED_SWAP);
			break;

		case R_NDS32_9_PCREL_RELA:
			if (exceed_limit
			    ((v - (Elf32_Addr) loc), 0x000000ff, module, rel,
			     relindex, i))
				return -ENOEXEC;
			do_reloc16(v - (Elf32_Addr) loc, loc, 0x000001ff, 1,
				   0xffffff00, 0, NEED_SWAP);
			break;

		case R_NDS32_15_PCREL_RELA:
			if (exceed_limit
			    ((v - (Elf32_Addr) loc), 0x00003fff, module, rel,
			     relindex, i))
				return -ENOEXEC;
			do_reloc32(v - (Elf32_Addr) loc, loc, 0x00007fff, 1,
				   0xffffc000, 0, NEED_SWAP);
			break;

		case R_NDS32_17_PCREL_RELA:
			if (exceed_limit
			    ((v - (Elf32_Addr) loc), 0x0000ffff, module, rel,
			     relindex, i))
				return -ENOEXEC;
			do_reloc32(v - (Elf32_Addr) loc, loc, 0x0001ffff, 1,
				   0xffff0000, 0, NEED_SWAP);
			break;

		case R_NDS32_25_PCREL_RELA:
			if (exceed_limit
			    ((v - (Elf32_Addr) loc), 0x00ffffff, module, rel,
			     relindex, i))
				return -ENOEXEC;
			do_reloc32(v - (Elf32_Addr) loc, loc, 0x01ffffff, 1,
				   0xff000000, 0, NEED_SWAP);
			break;
		case R_NDS32_WORD_9_PCREL_RELA:
			if (exceed_limit
			    ((v - (Elf32_Addr) loc), 0x000000ff, module, rel,
			     relindex, i))
				return -ENOEXEC;
			do_reloc32(v - (Elf32_Addr) loc, loc, 0x000001ff, 1,
				   0xffffff00, 0, NEED_SWAP);
			break;

		case R_NDS32_SDA15S3_RELA:
		case R_NDS32_SDA15S2_RELA:
		case R_NDS32_SDA15S1_RELA:
		case R_NDS32_SDA15S0_RELA:
			pr_err("%s: unsupported relocation type %d.\n",
			       module->name, ELF32_R_TYPE(rel->r_info));
			pr_err
			    ("Small data section access doesn't work in the kernel space; "
			     "please rebuild the kernel module with gcc option -mcmodel=large.\n");
			pr_err("section %d reloc %d offset 0x%x size %d\n",
			       relindex, i, rel->r_offset, dstsec->sh_size);
			break;

		default:
			pr_err("%s: unsupported relocation type %d.\n",
			       module->name, ELF32_R_TYPE(rel->r_info));
			pr_err("section %d reloc %d offset 0x%x size %d\n",
			       relindex, i, rel->r_offset, dstsec->sh_size);
		}
	}
	return 0;
}

int
module_finalize(const Elf32_Ehdr * hdr, const Elf_Shdr * sechdrs,
		struct module *module)
{
	return 0;
}

void module_arch_cleanup(struct module *mod)
{
}
