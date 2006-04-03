/*
 * Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/bootmem.h>

#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/sections.h>

#include <asm/mips-boards/prom.h>

/*#define DEBUG*/

enum simmem_memtypes {
	simmem_reserved = 0,
	simmem_free,
};
struct prom_pmemblock mdesc[PROM_MAX_PMEMBLOCKS];

#ifdef DEBUG
static char *mtypes[3] = {
	"SIM reserved memory",
	"SIM free memory",
};
#endif

struct prom_pmemblock * __init prom_getmdesc(void)
{
	unsigned int memsize;

	memsize = 0x02000000;
	prom_printf("Setting default memory size 0x%08x\n", memsize);

	memset(mdesc, 0, sizeof(mdesc));

	mdesc[0].type = simmem_reserved;
	mdesc[0].base = 0x00000000;
	mdesc[0].size = 0x00001000;

	mdesc[1].type = simmem_free;
	mdesc[1].base = 0x00001000;
	mdesc[1].size = 0x000ff000;

	mdesc[2].type = simmem_reserved;
	mdesc[2].base = 0x00100000;
	mdesc[2].size = CPHYSADDR(PAGE_ALIGN(&_end)) - mdesc[2].base;

	mdesc[3].type = simmem_free;
	mdesc[3].base = CPHYSADDR(PAGE_ALIGN(&_end));
	mdesc[3].size = memsize - mdesc[3].base;

	return &mdesc[0];
}

static int __init prom_memtype_classify (unsigned int type)
{
	switch (type) {
	case simmem_free:
		return BOOT_MEM_RAM;
	case simmem_reserved:
	default:
		return BOOT_MEM_RESERVED;
	}
}

void __init prom_meminit(void)
{
	struct prom_pmemblock *p;

	p = prom_getmdesc();

	while (p->size) {
		long type;
		unsigned long base, size;

		type = prom_memtype_classify (p->type);
		base = p->base;
		size = p->size;

		add_memory_region(base, size, type);
                p++;
	}
}

unsigned long __init prom_free_prom_memory(void)
{
	int i;
	unsigned long freed = 0;
	unsigned long addr;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		if (boot_mem_map.map[i].type != BOOT_MEM_ROM_DATA)
			continue;

		addr = boot_mem_map.map[i].addr;
		while (addr < boot_mem_map.map[i].addr
			      + boot_mem_map.map[i].size) {
			ClearPageReserved(virt_to_page(__va(addr)));
			init_page_count(virt_to_page(__va(addr)));
			free_page((unsigned long)__va(addr));
			addr += PAGE_SIZE;
			freed += PAGE_SIZE;
		}
	}
	printk("Freeing prom memory: %ldkb freed\n", freed >> 10);

	return freed;
}
