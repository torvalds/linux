/*
 * arch/xtensa/kernel/module.c
 *
 * Module support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 *
 */

#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/cache.h>

LIST_HEAD(module_buf_list);

void *module_alloc(unsigned long size)
{
  panic("module_alloc not implemented");
}

void module_free(struct module *mod, void *module_region)
{
  panic("module_free not implemented");
}

int module_frob_arch_sections(Elf32_Ehdr *hdr,
    			      Elf32_Shdr *sechdrs,
			      char *secstrings,
			      struct module *me)
{
  panic("module_frob_arch_sections not implemented");
}

int apply_relocate(Elf32_Shdr *sechdrs,
    		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *module)
{
  panic ("apply_relocate not implemented");
}

int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *module)
{
  panic("apply_relocate_add not implemented");
}

int module_finalize(const Elf_Ehdr *hdr,
    		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
  panic ("module_finalize not implemented");
}

void module_arch_cleanup(struct module *mod)
{
  panic("module_arch_cleanup not implemented");
}

struct bug_entry *module_find_bug(unsigned long bugaddr)
{
  panic("module_find_bug not implemented");
}
