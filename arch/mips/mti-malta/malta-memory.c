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
#include <asm/cdmm.h>
#include <asm/maar.h>
#include <asm/sections.h>
#include <asm/fw/fw.h>

/* determined physical memory size, not overridden by command line args	 */
unsigned long physical_memsize = 0L;

static void free_init_pages_eva_malta(void *begin, void *end)
{
	free_init_pages("unused kernel", __pa_symbol((unsigned long *)begin),
			__pa_symbol((unsigned long *)end));
}

void __init fw_meminit(void)
{
	char *memsize_str, *ememsize_str = NULL, *ptr;
	unsigned long memsize = 0, ememsize = 0;
	unsigned long kernel_start_phys, kernel_end_phys;
	static char cmdline[COMMAND_LINE_SIZE] __initdata;
	bool eva = config_enabled(CONFIG_EVA);
	int tmp;

	free_init_pages_eva = eva ? free_init_pages_eva_malta : NULL;

	memsize_str = fw_getenv("memsize");
	if (memsize_str) {
		tmp = kstrtoul(memsize_str, 0, &memsize);
		if (tmp)
			pr_warn("Failed to read the 'memsize' env variable.\n");
	}
	if (eva) {
	/* Look for ememsize for EVA */
		ememsize_str = fw_getenv("ememsize");
		if (ememsize_str) {
			tmp = kstrtoul(ememsize_str, 0, &ememsize);
			if (tmp)
				pr_warn("Failed to read the 'ememsize' env variable.\n");
		}
	}
	if (!memsize && !ememsize) {
		pr_warn("memsize not set in YAMON, set to default (32Mb)\n");
		physical_memsize = 0x02000000;
	} else {
		if (memsize > (256 << 20)) { /* memsize should be capped to 256M */
			pr_warn("Unsupported memsize value (0x%lx) detected! "
				"Using 0x10000000 (256M) instead\n",
				memsize);
			memsize = 256 << 20;
		}
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

	add_memory_region(PHYS_OFFSET, 0x00001000, BOOT_MEM_RESERVED);

	/*
	 * YAMON may still be using the region of memory from 0x1000 to 0xfffff
	 * if it has started secondary CPUs.
	 */
	add_memory_region(PHYS_OFFSET + 0x00001000, 0x000ef000,
			  BOOT_MEM_ROM_DATA);

	/*
	 * The area 0x000f0000-0x000fffff is allocated for BIOS memory by the
	 * south bridge and PCI access always forwarded to the ISA Bus and
	 * BIOSCS# is always generated.
	 * This mean that this area can't be used as DMA memory for PCI
	 * devices.
	 */
	add_memory_region(PHYS_OFFSET + 0x000f0000, 0x00010000,
			  BOOT_MEM_RESERVED);

	/*
	 * Reserve the memory used by kernel code, and allow the rest of RAM to
	 * be used.
	 */
	kernel_start_phys = PHYS_OFFSET + 0x00100000;
	kernel_end_phys = PHYS_OFFSET + CPHYSADDR(PFN_ALIGN(&_end));
	add_memory_region(kernel_start_phys, kernel_end_phys,
			  BOOT_MEM_RESERVED);
	add_memory_region(kernel_end_phys, memsize - kernel_end_phys,
			  BOOT_MEM_RAM);
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

phys_addr_t mips_cdmm_phys_base(void)
{
	/* This address is "typically unused" */
	return 0x1fc10000;
}
