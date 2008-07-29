/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
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
 * PROM library functions for acquiring/using memory descriptors given to
 * us from the YAMON.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/pfn.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/sections.h>

#include <asm/mips-boards/prom.h>

/*#define DEBUG*/

enum yamon_memtypes {
	yamon_dontuse,
	yamon_prom,
	yamon_free,
};
static struct prom_pmemblock mdesc[PROM_MAX_PMEMBLOCKS];

#ifdef DEBUG
static char *mtypes[3] = {
	"Dont use memory",
	"YAMON PROM memory",
	"Free memmory",
};
#endif

/* determined physical memory size, not overridden by command line args  */
unsigned long physical_memsize = 0L;

static struct prom_pmemblock * __init prom_getmdesc(void)
{
	char *memsize_str;
	unsigned int memsize;
	char cmdline[CL_SIZE], *ptr;

	/* otherwise look in the environment */
	memsize_str = prom_getenv("memsize");
	if (!memsize_str) {
		printk(KERN_WARNING
		       "memsize not set in boot prom, set to default (32Mb)\n");
		physical_memsize = 0x02000000;
	} else {
#ifdef DEBUG
		pr_debug("prom_memsize = %s\n", memsize_str);
#endif
		physical_memsize = simple_strtol(memsize_str, NULL, 0);
	}

#ifdef CONFIG_CPU_BIG_ENDIAN
	/* SOC-it swaps, or perhaps doesn't swap, when DMA'ing the last
	   word of physical memory */
	physical_memsize -= PAGE_SIZE;
#endif

	/* Check the command line for a memsize directive that overrides
	   the physical/default amount */
	strcpy(cmdline, arcs_cmdline);
	ptr = strstr(cmdline, "memsize=");
	if (ptr && (ptr != cmdline) && (*(ptr - 1) != ' '))
		ptr = strstr(ptr, " memsize=");

	if (ptr)
		memsize = memparse(ptr + 8, &ptr);
	else
		memsize = physical_memsize;

	memset(mdesc, 0, sizeof(mdesc));

	mdesc[0].type = yamon_dontuse;
	mdesc[0].base = 0x00000000;
	mdesc[0].size = 0x00001000;

	mdesc[1].type = yamon_prom;
	mdesc[1].base = 0x00001000;
	mdesc[1].size = 0x000ef000;

	/*
	 * The area 0x000f0000-0x000fffff is allocated for BIOS memory by the
	 * south bridge and PCI access always forwarded to the ISA Bus and
	 * BIOSCS# is always generated.
	 * This mean that this area can't be used as DMA memory for PCI
	 * devices.
	 */
	mdesc[2].type = yamon_dontuse;
	mdesc[2].base = 0x000f0000;
	mdesc[2].size = 0x00010000;

	mdesc[3].type = yamon_dontuse;
	mdesc[3].base = 0x00100000;
	mdesc[3].size = CPHYSADDR(PFN_ALIGN((unsigned long)&_end)) - mdesc[3].base;

	mdesc[4].type = yamon_free;
	mdesc[4].base = CPHYSADDR(PFN_ALIGN(&_end));
	mdesc[4].size = memsize - mdesc[4].base;

	return &mdesc[0];
}

static int __init prom_memtype_classify(unsigned int type)
{
	switch (type) {
	case yamon_free:
		return BOOT_MEM_RAM;
	case yamon_prom:
		return BOOT_MEM_ROM_DATA;
	default:
		return BOOT_MEM_RESERVED;
	}
}

void __init prom_meminit(void)
{
	struct prom_pmemblock *p;

#ifdef DEBUG
	pr_debug("YAMON MEMORY DESCRIPTOR dump:\n");
	p = prom_getmdesc();
	while (p->size) {
		int i = 0;
		pr_debug("[%d,%p]: base<%08lx> size<%08lx> type<%s>\n",
			 i, p, p->base, p->size, mtypes[p->type]);
		p++;
		i++;
	}
#endif
	p = prom_getmdesc();

	while (p->size) {
		long type;
		unsigned long base, size;

		type = prom_memtype_classify(p->type);
		base = p->base;
		size = p->size;

		add_memory_region(base, size, type);
                p++;
	}
}

void __init prom_free_prom_memory(void)
{
	unsigned long addr;
	int i;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		if (boot_mem_map.map[i].type != BOOT_MEM_ROM_DATA)
			continue;

		addr = boot_mem_map.map[i].addr;
		free_init_pages("prom memory",
				addr, addr + boot_mem_map.map[i].size);
	}
}
