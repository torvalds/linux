// SPDX-License-Identifier: GPL-2.0-or-later
/*  Kernel module help for Alpha.
    Copyright (C) 2002 Richard Henderson.

*/
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt...)
#endif

/* Allocate the GOT at the end of the core sections.  */

struct got_entry {
	struct got_entry *next;
	Elf64_Sxword r_addend;
	int got_offset;
};

static inline void
process_reloc_for_got(Elf64_Rela *rela,
		      struct got_entry *chains, Elf64_Xword *poffset)
{
	unsigned long r_sym = ELF64_R_SYM (rela->r_info);
	unsigned long r_type = ELF64_R_TYPE (rela->r_info);
	Elf64_Sxword r_addend = rela->r_addend;
	struct got_entry *g;

	if (r_type != R_ALPHA_LITERAL)
		return;

	for (g = chains + r_sym; g ; g = g->next)
		if (g->r_addend == r_addend) {
			if (g->got_offset == 0) {
				g->got_offset = *poffset;
				*poffset += 8;
			}
			goto found_entry;
		}

	g = kmalloc (sizeof (*g), GFP_KERNEL);
	g->next = chains[r_sym].next;
	g->r_addend = r_addend;
	g->got_offset = *poffset;
	*poffset += 8;
	chains[r_sym].next = g;

 found_entry:
	/* Trick: most of the ELF64_R_TYPE field is unused.  There are
	   42 valid relocation types, and a 32-bit field.  Co-opt the
	   bits above 256 to store the got offset for this reloc.  */
	rela->r_info |= g->got_offset << 8;
}

int
module_frob_arch_sections(Elf64_Ehdr *hdr, Elf64_Shdr *sechdrs,
			  char *secstrings, struct module *me)
{
	struct got_entry *chains;
	Elf64_Rela *rela;
	Elf64_Shdr *esechdrs, *symtab, *s, *got;
	unsigned long nsyms, nrela, i;

	esechdrs = sechdrs + hdr->e_shnum;
	symtab = got = NULL;

	/* Find out how large the symbol table is.  Allocate one got_entry
	   head per symbol.  Normally this will be enough, but not always.
	   We'll chain different offsets for the symbol down each head.  */
	for (s = sechdrs; s < esechdrs; ++s)
		if (s->sh_type == SHT_SYMTAB)
			symtab = s;
		else if (!strcmp(".got", secstrings + s->sh_name)) {
			got = s;
			me->arch.gotsecindex = s - sechdrs;
		}

	if (!symtab) {
		printk(KERN_ERR "module %s: no symbol table\n", me->name);
		return -ENOEXEC;
	}
	if (!got) {
		printk(KERN_ERR "module %s: no got section\n", me->name);
		return -ENOEXEC;
	}

	nsyms = symtab->sh_size / sizeof(Elf64_Sym);
	chains = kcalloc(nsyms, sizeof(struct got_entry), GFP_KERNEL);
	if (!chains) {
		printk(KERN_ERR
		       "module %s: no memory for symbol chain buffer\n",
		       me->name);
		return -ENOMEM;
	}

	got->sh_size = 0;
	got->sh_addralign = 8;
	got->sh_type = SHT_NOBITS;

	/* Examine all LITERAL relocations to find out what GOT entries
	   are required.  This sizes the GOT section as well.  */
	for (s = sechdrs; s < esechdrs; ++s)
		if (s->sh_type == SHT_RELA) {
			nrela = s->sh_size / sizeof(Elf64_Rela);
			rela = (void *)hdr + s->sh_offset;
			for (i = 0; i < nrela; ++i)
				process_reloc_for_got(rela+i, chains,
						      &got->sh_size);
		}

	/* Free the memory we allocated.  */
	for (i = 0; i < nsyms; ++i) {
		struct got_entry *g, *n;
		for (g = chains[i].next; g ; g = n) {
			n = g->next;
			kfree(g);
		}
	}
	kfree(chains);

	return 0;
}

int
apply_relocate_add(Elf64_Shdr *sechdrs, const char *strtab,
		   unsigned int symindex, unsigned int relsec,
		   struct module *me)
{
	Elf64_Rela *rela = (void *)sechdrs[relsec].sh_addr;
	unsigned long i, n = sechdrs[relsec].sh_size / sizeof(*rela);
	Elf64_Sym *symtab, *sym;
	void *base, *location;
	unsigned long got, gp;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);

	base = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr;
	symtab = (Elf64_Sym *)sechdrs[symindex].sh_addr;

	got = sechdrs[me->arch.gotsecindex].sh_addr;
	gp = got + 0x8000;

	for (i = 0; i < n; i++) {
		unsigned long r_sym = ELF64_R_SYM (rela[i].r_info);
		unsigned long r_type = ELF64_R_TYPE (rela[i].r_info);
		unsigned long r_got_offset = r_type >> 8;
		unsigned long value, hi, lo;
		r_type &= 0xff;

		/* This is where to make the change.  */
		location = base + rela[i].r_offset;

		/* This is the symbol it is referring to.  Note that all
		   unresolved symbols have been resolved.  */
		sym = symtab + r_sym;
		value = sym->st_value + rela[i].r_addend;

		switch (r_type) {
		case R_ALPHA_NONE:
			break;
		case R_ALPHA_REFLONG:
			*(u32 *)location = value;
			break;
		case R_ALPHA_REFQUAD:
			/* BUG() can produce misaligned relocations. */
			((u32 *)location)[0] = value;
			((u32 *)location)[1] = value >> 32;
			break;
		case R_ALPHA_GPREL32:
			value -= gp;
			if ((int)value != value)
				goto reloc_overflow;
			*(u32 *)location = value;
			break;
		case R_ALPHA_LITERAL:
			hi = got + r_got_offset;
			lo = hi - gp;
			if ((short)lo != lo)
				goto reloc_overflow;
			*(u16 *)location = lo;
			*(u64 *)hi = value;
			break;
		case R_ALPHA_LITUSE:
			break;
		case R_ALPHA_GPDISP:
			value = gp - (u64)location;
			lo = (short)value;
			hi = (int)(value - lo);
			if (hi + lo != value)
				goto reloc_overflow;
			*(u16 *)location = hi >> 16;
			*(u16 *)(location + rela[i].r_addend) = lo;
			break;
		case R_ALPHA_BRSGP:
			/* BRSGP is only allowed to bind to local symbols.
			   If the section is undef, this means that the
			   value was resolved from somewhere else.  */
			if (sym->st_shndx == SHN_UNDEF)
				goto reloc_overflow;
			if ((sym->st_other & STO_ALPHA_STD_GPLOAD) ==
			    STO_ALPHA_STD_GPLOAD)
				/* Omit the prologue. */
				value += 8;
			fallthrough;
		case R_ALPHA_BRADDR:
			value -= (u64)location + 4;
			if (value & 3)
				goto reloc_overflow;
			value = (long)value >> 2;
			if (value + (1<<21) >= 1<<22)
				goto reloc_overflow;
			value &= 0x1fffff;
			value |= *(u32 *)location & ~0x1fffff;
			*(u32 *)location = value;
			break;
		case R_ALPHA_HINT:
			break;
		case R_ALPHA_SREL32:
			value -= (u64)location;
			if ((int)value != value)
				goto reloc_overflow;
			*(u32 *)location = value;
			break;
		case R_ALPHA_SREL64:
			value -= (u64)location;
			*(u64 *)location = value;
			break;
		case R_ALPHA_GPRELHIGH:
			value = (long)(value - gp + 0x8000) >> 16;
			if ((short) value != value)
				goto reloc_overflow;
			*(u16 *)location = value;
			break;
		case R_ALPHA_GPRELLOW:
			value -= gp;
			*(u16 *)location = value;
			break;
		case R_ALPHA_GPREL16:
			value -= gp;
			if ((short) value != value)
				goto reloc_overflow;
			*(u16 *)location = value;
			break;
		default:
			printk(KERN_ERR "module %s: Unknown relocation: %lu\n",
			       me->name, r_type);
			return -ENOEXEC;
		reloc_overflow:
			if (ELF64_ST_TYPE (sym->st_info) == STT_SECTION)
			  printk(KERN_ERR
			         "module %s: Relocation (type %lu) overflow vs section %d\n",
			         me->name, r_type, sym->st_shndx);
			else
			  printk(KERN_ERR
			         "module %s: Relocation (type %lu) overflow vs %s\n",
			         me->name, r_type, strtab + sym->st_name);
			return -ENOEXEC;
		}
	}

	return 0;
}
