/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 * Portions copyright (C) 2009 Cisco Systems, Inc.
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
 * Apparently originally from arch/mips/malta-memory.c. Modified to work
 * with the PowerTV bootloader.
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

#include "init.h"

/* Memory constants */
#define KIBIBYTE(n)		((n) * 1024)	/* Number of kibibytes */
#define MEBIBYTE(n)		((n) * KIBIBYTE(1024)) /* Number of mebibytes */
#define DEFAULT_MEMSIZE		MEBIBYTE(256)	/* If no memsize provided */
#define LOW_MEM_MAX		MEBIBYTE(252)	/* Max usable low mem */
#define RES_BOOTLDR_MEMSIZE	MEBIBYTE(1)	/* Memory reserved for bldr */
#define BOOT_MEM_SIZE		KIBIBYTE(256)	/* Memory reserved for bldr */
#define PHYS_MEM_START		0x10000000	/* Start of physical memory */

char __initdata cmdline[COMMAND_LINE_SIZE];

void __init prom_meminit(void)
{
	char *memsize_str;
	unsigned long memsize = 0;
	unsigned int physend;
	char *ptr;
	int low_mem;
	int high_mem;

	/* Check the command line first for a memsize directive */
	strcpy(cmdline, arcs_cmdline);
	ptr = strstr(cmdline, "memsize=");
	if (ptr && (ptr != cmdline) && (*(ptr - 1) != ' '))
		ptr = strstr(ptr, " memsize=");

	if (ptr) {
		memsize = memparse(ptr + 8, &ptr);
	} else {
		/* otherwise look in the environment */
		memsize_str = prom_getenv("memsize");

		if (memsize_str != NULL) {
			pr_info("prom memsize = %s\n", memsize_str);
			memsize = simple_strtol(memsize_str, NULL, 0);
		}

		if (memsize == 0) {
			if (_prom_memsize != 0) {
				memsize = _prom_memsize;
				pr_info("_prom_memsize = 0x%lx\n", memsize);
				/* add in memory that the bootloader doesn't
				 * report */
				memsize += BOOT_MEM_SIZE;
			} else {
				memsize = DEFAULT_MEMSIZE;
				pr_info("Memsize not passed by bootloader, "
					"defaulting to 0x%lx\n", memsize);
			}
		}
	}

	physend = PFN_ALIGN(&_end) - 0x80000000;
	if (memsize > LOW_MEM_MAX) {
		low_mem = LOW_MEM_MAX;
		high_mem = memsize - low_mem;
	} else {
		low_mem = memsize;
		high_mem = 0;
	}

/*
 * TODO: We will use the hard code for memory configuration until
 * the bootloader releases their device tree to us.
 */
	/*
	 * Add the memory reserved for use by the bootloader to the
	 * memory map.
	 */
	add_memory_region(PHYS_MEM_START, RES_BOOTLDR_MEMSIZE,
		BOOT_MEM_RESERVED);
#ifdef CONFIG_HIGHMEM_256_128
	/*
	 * Add memory in low for general use by the kernel and its friends
	 * (like drivers, applications, etc).
	 */
	add_memory_region(PHYS_MEM_START + RES_BOOTLDR_MEMSIZE,
		LOW_MEM_MAX - RES_BOOTLDR_MEMSIZE, BOOT_MEM_RAM);
	/*
	 * Add the memory reserved for reset vector.
	 */
	add_memory_region(0x1fc00000, MEBIBYTE(4), BOOT_MEM_RESERVED);
	/*
	 * Add the memory reserved.
	 */
	add_memory_region(0x20000000, MEBIBYTE(1024 + 75), BOOT_MEM_RESERVED);
	/*
	 * Add memory in high for general use by the kernel and its friends
	 * (like drivers, applications, etc).
	 *
	 * 75MB is reserved for devices which are using the memory in high.
	 */
	add_memory_region(0x60000000 + MEBIBYTE(75), MEBIBYTE(128 - 75),
		BOOT_MEM_RAM);
#elif defined CONFIG_HIGHMEM_128_128
	/*
	 * Add memory in low for general use by the kernel and its friends
	 * (like drivers, applications, etc).
	 */
	add_memory_region(PHYS_MEM_START + RES_BOOTLDR_MEMSIZE,
		MEBIBYTE(128) - RES_BOOTLDR_MEMSIZE, BOOT_MEM_RAM);
	/*
	 * Add the memory reserved.
	 */
	add_memory_region(PHYS_MEM_START + MEBIBYTE(128),
		MEBIBYTE(128 + 1024 + 75), BOOT_MEM_RESERVED);
	/*
	 * Add memory in high for general use by the kernel and its friends
	 * (like drivers, applications, etc).
	 *
	 * 75MB is reserved for devices which are using the memory in high.
	 */
	add_memory_region(0x60000000 + MEBIBYTE(75), MEBIBYTE(128 - 75),
		BOOT_MEM_RAM);
#else
	/* Add low memory regions for either:
	 *   - no-highmemory configuration case -OR-
	 *   - highmemory "HIGHMEM_LOWBANK_ONLY" case
	 */
	/*
	 * Add memory for general use by the kernel and its friends
	 * (like drivers, applications, etc).
	 */
	add_memory_region(PHYS_MEM_START + RES_BOOTLDR_MEMSIZE,
		low_mem - RES_BOOTLDR_MEMSIZE, BOOT_MEM_RAM);
	/*
	 * Add the memory reserved for reset vector.
	 */
	add_memory_region(0x1fc00000, MEBIBYTE(4), BOOT_MEM_RESERVED);
#endif
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
