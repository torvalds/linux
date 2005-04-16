/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Copyright (C) 2001 Rusty Russell.
 *  Copyright (C) 2003, 2004 Ralf Baechle (ralf@linux-mips.org)
 */

#undef DEBUG

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

struct mips_hi16 {
	struct mips_hi16 *next;
	Elf32_Addr *addr;
	Elf32_Addr value;
};

static struct mips_hi16 *mips_hi16_list;

void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc(size);
}


/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
           table entries. */
}

int module_frob_arch_sections(Elf_Ehdr *hdr,
			      Elf_Shdr *sechdrs,
			      char *secstrings,
			      struct module *mod)
{
	return 0;
}

static int apply_r_mips_none(struct module *me, uint32_t *location,
	Elf32_Addr v)
{
	return 0;
}

static int apply_r_mips_32(struct module *me, uint32_t *location,
	Elf32_Addr v)
{
	*location += v;

	return 0;
}

static int apply_r_mips_26(struct module *me, uint32_t *location,
	Elf32_Addr v)
{
	if (v % 4) {
		printk(KERN_ERR "module %s: dangerous relocation\n", me->name);
		return -ENOEXEC;
	}

	if ((v & 0xf0000000) != (((unsigned long)location + 4) & 0xf0000000)) {
		printk(KERN_ERR
		       "module %s: relocation overflow\n",
		       me->name);
		return -ENOEXEC;
	}

	*location = (*location & ~0x03ffffff) |
	            ((*location + (v >> 2)) & 0x03ffffff);

	return 0;
}

static int apply_r_mips_hi16(struct module *me, uint32_t *location,
	Elf32_Addr v)
{
	struct mips_hi16 *n;

	/*
	 * We cannot relocate this one now because we don't know the value of
	 * the carry we need to add.  Save the information, and let LO16 do the
	 * actual relocation.
	 */
	n = kmalloc(sizeof *n, GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	n->addr = location;
	n->value = v;
	n->next = mips_hi16_list;
	mips_hi16_list = n;

	return 0;
}

static int apply_r_mips_lo16(struct module *me, uint32_t *location,
	Elf32_Addr v)
{
	unsigned long insnlo = *location;
	Elf32_Addr val, vallo;

	/* Sign extend the addend we extract from the lo insn.  */
	vallo = ((insnlo & 0xffff) ^ 0x8000) - 0x8000;

	if (mips_hi16_list != NULL) {
		struct mips_hi16 *l;

		l = mips_hi16_list;
		while (l != NULL) {
			struct mips_hi16 *next;
			unsigned long insn;

			/*
			 * The value for the HI16 had best be the same.
			 */
			if (v != l->value)
				goto out_danger;

			/*
			 * Do the HI16 relocation.  Note that we actually don't
			 * need to know anything about the LO16 itself, except
			 * where to find the low 16 bits of the addend needed
			 * by the LO16.
			 */
			insn = *l->addr;
			val = ((insn & 0xffff) << 16) + vallo;
			val += v;

			/*
			 * Account for the sign extension that will happen in
			 * the low bits.
			 */
			val = ((val >> 16) + ((val & 0x8000) != 0)) & 0xffff;

			insn = (insn & ~0xffff) | val;
			*l->addr = insn;

			next = l->next;
			kfree(l);
			l = next;
		}

		mips_hi16_list = NULL;
	}

	/*
	 * Ok, we're done with the HI16 relocs.  Now deal with the LO16.
	 */
	val = v + vallo;
	insnlo = (insnlo & ~0xffff) | (val & 0xffff);
	*location = insnlo;

	return 0;

out_danger:
	printk(KERN_ERR "module %s: dangerous " "relocation\n", me->name);

	return -ENOEXEC;
}

static int (*reloc_handlers[]) (struct module *me, uint32_t *location,
	Elf32_Addr v) = {
	[R_MIPS_NONE]	= apply_r_mips_none,
	[R_MIPS_32]	= apply_r_mips_32,
	[R_MIPS_26]	= apply_r_mips_26,
	[R_MIPS_HI16]	= apply_r_mips_hi16,
	[R_MIPS_LO16]	= apply_r_mips_lo16
};

int apply_relocate(Elf32_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	Elf32_Rel *rel = (void *) sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	uint32_t *location;
	unsigned int i;
	Elf32_Addr v;
	int res;

	pr_debug("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		Elf32_Word r_info = rel[i].r_info;

		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(r_info);
		if (!sym->st_value) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		v = sym->st_value;

		res = reloc_handlers[ELF32_R_TYPE(r_info)](me, location, v);
		if (res)
			return res;
	}

	return 0;
}

int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	/*
	 * Current binutils always generate .rela relocations.  Keep smiling
	 * if it's empty, abort otherwise.
	 */
	if (!sechdrs[relsec].sh_size)
		return 0;

	printk(KERN_ERR "module %s: ADD RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
}
