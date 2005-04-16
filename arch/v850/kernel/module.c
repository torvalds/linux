/*
 * arch/v850/kernel/module.c -- Architecture-specific module functions
 *
 *  Copyright (C) 2002,03  NEC Electronics Corporation
 *  Copyright (C) 2002,03  Miles Bader <miles@gnu.org>
 *  Copyright (C) 2001,03  Rusty Russell
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 *
 * Derived in part from arch/ppc/kernel/module.c
 */

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/moduleloader.h>
#include <linux/elf.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

void *module_alloc (unsigned long size)
{
	return size == 0 ? 0 : vmalloc (size);
}

void module_free (struct module *mod, void *module_region)
{
	vfree (module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
           table entries. */
}

int module_finalize (const Elf_Ehdr *hdr, const Elf_Shdr *sechdrs,
		     struct module *mod)
{
	return 0;
}

/* Count how many different relocations (different symbol, different
   addend) */
static unsigned int count_relocs(const Elf32_Rela *rela, unsigned int num)
{
	unsigned int i, j, ret = 0;

	/* Sure, this is order(n^2), but it's usually short, and not
           time critical */
	for (i = 0; i < num; i++) {
		for (j = 0; j < i; j++) {
			/* If this addend appeared before, it's
                           already been counted */
			if (ELF32_R_SYM(rela[i].r_info)
			    == ELF32_R_SYM(rela[j].r_info)
			    && rela[i].r_addend == rela[j].r_addend)
				break;
		}
		if (j == i) ret++;
	}
	return ret;
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
		if ((strstr(secstrings + sechdrs[i].sh_name, ".init") != 0)
		    != is_init)
			continue;

		if (sechdrs[i].sh_type == SHT_RELA) {
			DEBUGP("Found relocations in section %u\n", i);
			DEBUGP("Ptr: %p.  Number: %u\n",
			       (void *)hdr + sechdrs[i].sh_offset,
			       sechdrs[i].sh_size / sizeof(Elf32_Rela));
			ret += count_relocs((void *)hdr
					     + sechdrs[i].sh_offset,
					     sechdrs[i].sh_size
					     / sizeof(Elf32_Rela))
				* sizeof(struct v850_plt_entry);
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

	/* Find .plt and .pltinit sections */
	for (i = 0; i < hdr->e_shnum; i++) {
		if (strcmp(secstrings + sechdrs[i].sh_name, ".init.plt") == 0)
			me->arch.init_plt_section = i;
		else if (strcmp(secstrings + sechdrs[i].sh_name, ".plt") == 0)
			me->arch.core_plt_section = i;
	}
	if (!me->arch.core_plt_section || !me->arch.init_plt_section) {
		printk("Module doesn't contain .plt or .plt.init sections.\n");
		return -ENOEXEC;
	}

	/* Override their sizes */
	sechdrs[me->arch.core_plt_section].sh_size
		= get_plt_size(hdr, sechdrs, secstrings, 0);
	sechdrs[me->arch.init_plt_section].sh_size
		= get_plt_size(hdr, sechdrs, secstrings, 1);
	return 0;
}

int apply_relocate (Elf32_Shdr *sechdrs, const char *strtab,
		    unsigned int symindex, unsigned int relsec,
		    struct module *mod)
{
	printk ("Barf\n");
	return -ENOEXEC;
}

/* Set up a trampoline in the PLT to bounce us to the distant function */
static uint32_t do_plt_call (void *location, Elf32_Addr val,
			     Elf32_Shdr *sechdrs, struct module *mod)
{
	struct v850_plt_entry *entry;
	/* Instructions used to do the indirect jump.  */
	uint32_t tramp[2];

	/* We have to trash a register, so we assume that any control
	   transfer more than 21-bits away must be a function call
	   (so we can use a call-clobbered register).  */
	tramp[0] = 0x0621 + ((val & 0xffff) << 16);   /* mov sym, r1 ... */
	tramp[1] = ((val >> 16) & 0xffff) + 0x610000; /* ...; jmp r1 */

	/* Init, or core PLT? */
	if (location >= mod->module_core
	    && location < mod->module_core + mod->core_size)
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

int apply_relocate_add (Elf32_Shdr *sechdrs, const char *strtab,
			unsigned int symindex, unsigned int relsec,
			struct module *mod)
{
	unsigned int i;
	Elf32_Rela *rela = (void *)sechdrs[relsec].sh_addr;

	DEBUGP ("Applying relocate section %u to %u\n", relsec,
		sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof (*rela); i++) {
		/* This is where to make the change */
		uint32_t *loc
			= ((void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			   + rela[i].r_offset);
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		Elf32_Sym *sym
			= ((Elf32_Sym *)sechdrs[symindex].sh_addr
			   + ELF32_R_SYM (rela[i].r_info));
		uint32_t val = sym->st_value + rela[i].r_addend;

		switch (ELF32_R_TYPE (rela[i].r_info)) {
		case R_V850_32:
			/* We write two shorts instead of a long because even
			   32-bit insns only need half-word alignment, but
			   32-bit data writes need to be long-word aligned.  */
			val += ((uint16_t *)loc)[0];
			val += ((uint16_t *)loc)[1] << 16;
			((uint16_t *)loc)[0] = val & 0xffff;
			((uint16_t *)loc)[1] = (val >> 16) & 0xffff;
			break;

		case R_V850_22_PCREL:
			/* Maybe jump indirectly via a PLT table entry.  */
			if ((int32_t)(val - (uint32_t)loc) > 0x1fffff
			    || (int32_t)(val - (uint32_t)loc) < -0x200000)
				val = do_plt_call (loc, val, sechdrs, mod);

			val -= (uint32_t)loc;

			/* We write two shorts instead of a long because
			   even 32-bit insns only need half-word alignment,
			   but 32-bit data writes need to be long-word
			   aligned.  */
			((uint16_t *)loc)[0] =
				(*(uint16_t *)loc & 0xffc0) /* opcode + reg */
				| ((val >> 16) & 0xffc03f); /* offs high */
			((uint16_t *)loc)[1] =
				(val & 0xffff);		    /* offs low */
			break;

		default:
			printk (KERN_ERR "module %s: Unknown reloc: %u\n",
				mod->name, ELF32_R_TYPE (rela[i].r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}

void
module_arch_cleanup(struct module *mod)
{
}
