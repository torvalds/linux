/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * PROM library functions for acquiring/using memory descriptors given to
 * us from the YAMON.
 *
 * Copyright (C) 1999,2000,2012  MIPS Technologies, Inc.
 * All rights reserved.
 * Authors: Carsten Langgaard <carstenl@mips.com>
 *          Steven J. Hill <sjhill@mips.com>
 */
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/sections.h>
#include <asm/fw/fw.h>

static fw_memblock_t mdesc[FW_MAX_MEMBLOCKS];

/* determined physical memory size, not overridden by command line args	 */
unsigned long physical_memsize = 0L;

fw_memblock_t * __init fw_getmdesc(int eva)
{
	char *memsize_str, *ememsize_str __maybe_unused = NULL, *ptr;
	unsigned long memsize = 0, ememsize __maybe_unused = 0;
	static char cmdline[COMMAND_LINE_SIZE] __initdata;
	int tmp;

	/* otherwise look in the environment */

	memsize_str = fw_getenv("memsize");
	if (memsize_str)
		tmp = kstrtol(memsize_str, 0, &memsize);
	if (eva) {
	/* Look for ememsize for EVA */
		ememsize_str = fw_getenv("ememsize");
		if (ememsize_str)
			tmp = kstrtol(ememsize_str, 0, &ememsize);
	}
	if (!memsize && !ememsize) {
		pr_warn("memsize not set in YAMON, set to default (32Mb)\n");
		physical_memsize = 0x02000000;
	} else {
		/* If ememsize is set, then set physical_memsize to that */
		physical_memsize = ememsize ? : memsize;
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
	/* And now look for ememsize */
	if (eva) {
		ptr = strstr(cmdline, "ememsize=");
		if (ptr && (ptr != cmdline) && (*(ptr - 1) != ' '))
			ptr = strstr(ptr, " ememsize=");
	}

	if (ptr)
		memsize = memparse(ptr + 8 + (eva ? 1 : 0), &ptr);
	else
		memsize = physical_memsize;

	/* Last 64K for HIGHMEM arithmetics */
	if (memsize > 0x7fff0000)
		memsize = 0x7fff0000;

	memset(mdesc, 0, sizeof(mdesc));

	mdesc[0].type = fw_dontuse;
	mdesc[0].base = PHYS_OFFSET;
	mdesc[0].size = 0x00001000;

	mdesc[1].type = fw_code;
	mdesc[1].base = mdesc[0].base + 0x00001000UL;
	mdesc[1].size = 0x000ef000;

	/*
	 * The area 0x000f0000-0x000fffff is allocated for BIOS memory by the
	 * south bridge and PCI access always forwarded to the ISA Bus and
	 * BIOSCS# is always generated.
	 * This mean that this area can't be used as DMA memory for PCI
	 * devices.
	 */
	mdesc[2].type = fw_dontuse;
	mdesc[2].base = mdesc[0].base + 0x000f0000UL;
	mdesc[2].size = 0x00010000;

	mdesc[3].type = fw_dontuse;
	mdesc[3].base = mdesc[0].base + 0x00100000UL;
	mdesc[3].size = CPHYSADDR(PFN_ALIGN((unsigned long)&_end)) -
		0x00100000UL;

	mdesc[4].type = fw_free;
	mdesc[4].base = mdesc[0].base + CPHYSADDR(PFN_ALIGN(&_end));
	mdesc[4].size = memsize - CPHYSADDR(mdesc[4].base);

	return &mdesc[0];
}

static void free_init_pages_eva_malta(void *begin, void *end)
{
	free_init_pages("unused kernel", __pa_symbol((unsigned long *)begin),
			__pa_symbol((unsigned long *)end));
}

static int __init fw_memtype_classify(unsigned int type)
{
	switch (type) {
	case fw_free:
		return BOOT_MEM_RAM;
	case fw_code:
		return BOOT_MEM_ROM_DATA;
	default:
		return BOOT_MEM_RESERVED;
	}
}

void __init fw_meminit(void)
{
	fw_memblock_t *p;

	p = fw_getmdesc(config_enabled(CONFIG_EVA));
	free_init_pages_eva = (config_enabled(CONFIG_EVA) ?
			       free_init_pages_eva_malta : NULL);

	while (p->size) {
		long type;
		unsigned long base, size;

		type = fw_memtype_classify(p->type);
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
		free_init_pages("YAMON memory",
				addr, addr + boot_mem_map.map[i].size);
	}
}
