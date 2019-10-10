// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2005, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Thomas Charleux (thomas.charleux@jaluna.com)
 */
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>

static inline int fixup_pcr(u32 *ip, Elf32_Addr dest, u32 maskbits, int shift)
{
	u32 opcode;
	long ep = (long)ip & ~31;
	long delta = ((long)dest - ep) >> 2;
	long mask = (1 << maskbits) - 1;

	if ((delta >> (maskbits - 1)) == 0 ||
	    (delta >> (maskbits - 1)) == -1) {
		opcode = *ip;
		opcode &= ~(mask << shift);
		opcode |= ((delta & mask) << shift);
		*ip = opcode;

		pr_debug("REL PCR_S%d[%p] dest[%p] opcode[%08x]\n",
			 maskbits, ip, (void *)dest, opcode);

		return 0;
	}
	pr_err("PCR_S%d reloc %p -> %p out of range!\n",
	       maskbits, ip, (void *)dest);

	return -1;
}

/*
 * apply a RELA relocation
 */
int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	Elf32_Rela *rel = (void *) sechdrs[relsec].sh_addr;
	Elf_Sym *sym;
	u32 *location, opcode;
	unsigned int i;
	Elf32_Addr v;
	Elf_Addr offset = 0;

	pr_debug("Applying relocate section %u to %u with offset 0x%x\n",
		 relsec, sechdrs[relsec].sh_info, offset);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset - offset;

		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (Elf_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);

		/* this is the adjustment to be made */
		v = sym->st_value + rel[i].r_addend;

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_C6000_ABS32:
			pr_debug("RELA ABS32: [%p] = 0x%x\n", location, v);
			*location = v;
			break;
		case R_C6000_ABS16:
			pr_debug("RELA ABS16: [%p] = 0x%x\n", location, v);
			*(u16 *)location = v;
			break;
		case R_C6000_ABS8:
			pr_debug("RELA ABS8: [%p] = 0x%x\n", location, v);
			*(u8 *)location = v;
			break;
		case R_C6000_ABS_L16:
			opcode = *location;
			opcode &= ~0x7fff80;
			opcode |= ((v & 0xffff) << 7);
			pr_debug("RELA ABS_L16[%p] v[0x%x] opcode[0x%x]\n",
				 location, v, opcode);
			*location = opcode;
			break;
		case R_C6000_ABS_H16:
			opcode = *location;
			opcode &= ~0x7fff80;
			opcode |= ((v >> 9) & 0x7fff80);
			pr_debug("RELA ABS_H16[%p] v[0x%x] opcode[0x%x]\n",
				 location, v, opcode);
			*location = opcode;
			break;
		case R_C6000_PCR_S21:
			if (fixup_pcr(location, v, 21, 7))
				return -ENOEXEC;
			break;
		case R_C6000_PCR_S12:
			if (fixup_pcr(location, v, 12, 16))
				return -ENOEXEC;
			break;
		case R_C6000_PCR_S10:
			if (fixup_pcr(location, v, 10, 13))
				return -ENOEXEC;
			break;
		default:
			pr_err("module %s: Unknown RELA relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}
