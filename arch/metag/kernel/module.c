/*  Kernel module help for Meta.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sort.h>

#include <asm/unaligned.h>

/* Count how many different relocations (different symbol, different
   addend) */
static unsigned int count_relocs(const Elf32_Rela *rela, unsigned int num)
{
	unsigned int i, r_info, r_addend, _count_relocs;

	_count_relocs = 0;
	r_info = 0;
	r_addend = 0;
	for (i = 0; i < num; i++)
		/* Only count relbranch relocs, others don't need stubs */
		if (ELF32_R_TYPE(rela[i].r_info) == R_METAG_RELBRANCH &&
		    (r_info != ELF32_R_SYM(rela[i].r_info) ||
		     r_addend != rela[i].r_addend)) {
			_count_relocs++;
			r_info = ELF32_R_SYM(rela[i].r_info);
			r_addend = rela[i].r_addend;
		}

	return _count_relocs;
}

static int relacmp(const void *_x, const void *_y)
{
	const Elf32_Rela *x, *y;

	y = (Elf32_Rela *)_x;
	x = (Elf32_Rela *)_y;

	/* Compare the entire r_info (as opposed to ELF32_R_SYM(r_info) only) to
	 * make the comparison cheaper/faster. It won't affect the sorting or
	 * the counting algorithms' performance
	 */
	if (x->r_info < y->r_info)
		return -1;
	else if (x->r_info > y->r_info)
		return 1;
	else if (x->r_addend < y->r_addend)
		return -1;
	else if (x->r_addend > y->r_addend)
		return 1;
	else
		return 0;
}

static void relaswap(void *_x, void *_y, int size)
{
	uint32_t *x, *y, tmp;
	int i;

	y = (uint32_t *)_x;
	x = (uint32_t *)_y;

	for (i = 0; i < sizeof(Elf32_Rela) / sizeof(uint32_t); i++) {
		tmp = x[i];
		x[i] = y[i];
		y[i] = tmp;
	}
}

/* Get the potential trampolines size required of the init and
   non-init sections */
static unsigned long get_plt_size(const Elf32_Ehdr *hdr,
				  const Elf32_Shdr *sechdrs,
				  const char *secstrings,
				  int is_init)
{
	unsigned long ret = 0;
	unsigned i;

	/* Everything marked ALLOC (this includes the exported
	   symbols) */
	for (i = 1; i < hdr->e_shnum; i++) {
		/* If it's called *.init*, and we're not init, we're
		   not interested */
		if ((strstr(secstrings + sechdrs[i].sh_name, ".init") != NULL)
		    != is_init)
			continue;

		/* We don't want to look at debug sections. */
		if (strstr(secstrings + sechdrs[i].sh_name, ".debug") != NULL)
			continue;

		if (sechdrs[i].sh_type == SHT_RELA) {
			pr_debug("Found relocations in section %u\n", i);
			pr_debug("Ptr: %p.  Number: %u\n",
				 (void *)hdr + sechdrs[i].sh_offset,
				 sechdrs[i].sh_size / sizeof(Elf32_Rela));

			/* Sort the relocation information based on a symbol and
			 * addend key. This is a stable O(n*log n) complexity
			 * alogrithm but it will reduce the complexity of
			 * count_relocs() to linear complexity O(n)
			 */
			sort((void *)hdr + sechdrs[i].sh_offset,
			     sechdrs[i].sh_size / sizeof(Elf32_Rela),
			     sizeof(Elf32_Rela), relacmp, relaswap);

			ret += count_relocs((void *)hdr
					     + sechdrs[i].sh_offset,
					     sechdrs[i].sh_size
					     / sizeof(Elf32_Rela))
				* sizeof(struct metag_plt_entry);
		}
	}

	return ret;
}

int module_frob_arch_sections(Elf32_Ehdr *hdr,
			      Elf32_Shdr *sechdrs,
			      char *secstrings,
			      struct module *me)
{
	unsigned int i;

	/* Find .plt and .init.plt sections */
	for (i = 0; i < hdr->e_shnum; i++) {
		if (strcmp(secstrings + sechdrs[i].sh_name, ".init.plt") == 0)
			me->arch.init_plt_section = i;
		else if (strcmp(secstrings + sechdrs[i].sh_name, ".plt") == 0)
			me->arch.core_plt_section = i;
	}
	if (!me->arch.core_plt_section || !me->arch.init_plt_section) {
		pr_err("Module doesn't contain .plt or .init.plt sections.\n");
		return -ENOEXEC;
	}

	/* Override their sizes */
	sechdrs[me->arch.core_plt_section].sh_size
		= get_plt_size(hdr, sechdrs, secstrings, 0);
	sechdrs[me->arch.core_plt_section].sh_type = SHT_NOBITS;
	sechdrs[me->arch.init_plt_section].sh_size
		= get_plt_size(hdr, sechdrs, secstrings, 1);
	sechdrs[me->arch.init_plt_section].sh_type = SHT_NOBITS;
	return 0;
}

/* Set up a trampoline in the PLT to bounce us to the distant function */
static uint32_t do_plt_call(void *location, Elf32_Addr val,
			    Elf32_Shdr *sechdrs, struct module *mod)
{
	struct metag_plt_entry *entry;
	/* Instructions used to do the indirect jump.  */
	uint32_t tramp[2];

	/* We have to trash a register, so we assume that any control
	   transfer more than 21-bits away must be a function call
	   (so we can use a call-clobbered register).  */

	/* MOVT D0Re0,#HI(v) */
	tramp[0] = 0x02000005 | (((val & 0xffff0000) >> 16) << 3);
	/* JUMP D0Re0,#LO(v) */
	tramp[1] = 0xac000001 | ((val & 0x0000ffff) << 3);

	/* Init, or core PLT? */
	if (location >= mod->core_layout.base
	    && location < mod->core_layout.base + mod->core_layout.size)
		entry = (void *)sechdrs[mod->arch.core_plt_section].sh_addr;
	else
		entry = (void *)sechdrs[mod->arch.init_plt_section].sh_addr;

	/* Find this entry, or if that fails, the next avail. entry */
	while (entry->tramp[0])
		if (entry->tramp[0] == tramp[0] && entry->tramp[1] == tramp[1])
			return (uint32_t)entry;
		else
			entry++;

	entry->tramp[0] = tramp[0];
	entry->tramp[1] = tramp[1];

	return (uint32_t)entry;
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
	Elf32_Addr relocation;
	uint32_t *location;
	int32_t value;

	pr_debug("Applying relocate section %u to %u\n", relsec,
		 sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);
		relocation = sym->st_value + rel[i].r_addend;

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_METAG_NONE:
			break;
		case R_METAG_HIADDR16:
			relocation >>= 16;
		case R_METAG_LOADDR16:
			*location = (*location & 0xfff80007) |
				((relocation & 0xffff) << 3);
			break;
		case R_METAG_ADDR32:
			/*
			 * Packed data structures may cause a misaligned
			 * R_METAG_ADDR32 to be emitted.
			 */
			put_unaligned(relocation, location);
			break;
		case R_METAG_GETSETOFF:
			*location += ((relocation & 0xfff) << 7);
			break;
		case R_METAG_RELBRANCH:
			if (*location & (0x7ffff << 5)) {
				pr_err("bad relbranch relocation\n");
				break;
			}

			/* This jump is too big for the offset slot. Build
			 * a PLT to jump through to get to where we want to go.
			 * NB: 21bit check - not scaled to 19bit yet
			 */
			if (((int32_t)(relocation -
				       (uint32_t)location) > 0xfffff) ||
			    ((int32_t)(relocation -
				       (uint32_t)location) < -0xfffff)) {
				relocation = do_plt_call(location, relocation,
							 sechdrs, me);
			}

			value = relocation - (uint32_t)location;

			/* branch instruction aligned */
			value /= 4;

			if ((value > 0x7ffff) || (value < -0x7ffff)) {
				/*
				 * this should have been caught by the code
				 * above!
				 */
				pr_err("overflow of relbranch reloc\n");
			}

			*location = (*location & (~(0x7ffff << 5))) |
				((value & 0x7ffff) << 5);
			break;

		default:
			pr_err("module %s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}
