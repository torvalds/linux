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
#include <asm/mach-powertv/asic.h>
#include <asm/mach-powertv/ioremap.h>

#include "init.h"

/* Memory constants */
#define KIBIBYTE(n)		((n) * 1024)	/* Number of kibibytes */
#define MEBIBYTE(n)		((n) * KIBIBYTE(1024)) /* Number of mebibytes */
#define DEFAULT_MEMSIZE		MEBIBYTE(128)	/* If no memsize provided */

#define BLDR_SIZE	KIBIBYTE(256)		/* Memory reserved for bldr */
#define RV_SIZE		MEBIBYTE(4)		/* Size of reset vector */

#define LOW_MEM_END	0x20000000		/* Highest low memory address */
#define BLDR_ALIAS	0x10000000		/* Bootloader address */
#define RV_PHYS		0x1fc00000		/* Reset vector address */
#define LOW_RAM_END	RV_PHYS			/* End of real RAM in low mem */

/*
 * Very low-level conversion from processor physical address to device
 * DMA address for the first bank of memory.
 */
#define PHYS_TO_DMA(paddr)	((paddr) + (CONFIG_LOW_RAM_DMA - LOW_RAM_ALIAS))

unsigned long ptv_memsize;

/*
 * struct low_mem_reserved - Items in low memory that are reserved
 * @start:	Physical address of item
 * @size:	Size, in bytes, of this item
 * @is_aliased:	True if this is RAM aliased from another location. If false,
 *		it is something other than aliased RAM and the RAM in the
 *		unaliased address is still visible outside of low memory.
 */
struct low_mem_reserved {
	phys_addr_t	start;
	phys_addr_t	size;
	bool		is_aliased;
};

/*
 * Must be in ascending address order
 */
struct low_mem_reserved low_mem_reserved[] = {
	{BLDR_ALIAS, BLDR_SIZE, true},	/* Bootloader RAM */
	{RV_PHYS, RV_SIZE, false},	/* Reset vector */
};

/*
 * struct mem_layout - layout of a piece of the system RAM
 * @phys:	Physical address of the start of this piece of RAM. This is the
 *		address at which both the processor and I/O devices see the
 *		RAM.
 * @alias:	Alias of this piece of memory in order to make it appear in
 *		the low memory part of the processor's address space. I/O
 *		devices don't see anything here.
 * @size:	Size, in bytes, of this piece of RAM
 */
struct mem_layout {
	phys_addr_t	phys;
	phys_addr_t	alias;
	phys_addr_t	size;
};

/*
 * struct mem_layout_list - list descriptor for layouts of system RAM pieces
 * @family:	Specifies the family being described
 * @n:		Number of &struct mem_layout elements
 * @layout:	Pointer to the list of &mem_layout structures
 */
struct mem_layout_list {
	enum family_type	family;
	size_t			n;
	struct mem_layout	*layout;
};

static struct mem_layout f1500_layout[] = {
	{0x20000000, 0x10000000, MEBIBYTE(256)},
};

static struct mem_layout f4500_layout[] = {
	{0x40000000, 0x10000000, MEBIBYTE(256)},
	{0x20000000, 0x20000000, MEBIBYTE(32)},
};

static struct mem_layout f8500_layout[] = {
	{0x40000000, 0x10000000, MEBIBYTE(256)},
	{0x20000000, 0x20000000, MEBIBYTE(32)},
	{0x30000000, 0x30000000, MEBIBYTE(32)},
};

static struct mem_layout fx600_layout[] = {
	{0x20000000, 0x10000000, MEBIBYTE(256)},
	{0x60000000, 0x60000000, MEBIBYTE(128)},
};

static struct mem_layout_list layout_list[] = {
	{FAMILY_1500, ARRAY_SIZE(f1500_layout), f1500_layout},
	{FAMILY_1500VZE, ARRAY_SIZE(f1500_layout), f1500_layout},
	{FAMILY_1500VZF, ARRAY_SIZE(f1500_layout), f1500_layout},
	{FAMILY_4500, ARRAY_SIZE(f4500_layout), f4500_layout},
	{FAMILY_8500, ARRAY_SIZE(f8500_layout), f8500_layout},
	{FAMILY_8500RNG, ARRAY_SIZE(f8500_layout), f8500_layout},
	{FAMILY_4600, ARRAY_SIZE(fx600_layout), fx600_layout},
	{FAMILY_4600VZA, ARRAY_SIZE(fx600_layout), fx600_layout},
	{FAMILY_8600, ARRAY_SIZE(fx600_layout), fx600_layout},
	{FAMILY_8600VZB, ARRAY_SIZE(fx600_layout), fx600_layout},
};

/* If we can't determine the layout, use this */
static struct mem_layout default_layout[] = {
	{0x20000000, 0x10000000, MEBIBYTE(128)},
};

/**
 * register_non_ram - register low memory not available for RAM usage
 */
static __init void register_non_ram(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(low_mem_reserved); i++)
		add_memory_region(low_mem_reserved[i].start,
			low_mem_reserved[i].size, BOOT_MEM_RESERVED);
}

/**
 * get_memsize - get the size of memory as a single bank
 */
static phys_addr_t get_memsize(void)
{
	static char cmdline[COMMAND_LINE_SIZE] __initdata;
	phys_addr_t memsize = 0;
	char *memsize_str;
	char *ptr;

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
				pr_info("_prom_memsize = 0x%x\n", memsize);
				/* add in memory that the bootloader doesn't
				 * report */
				memsize += BLDR_SIZE;
			} else {
				memsize = DEFAULT_MEMSIZE;
				pr_info("Memsize not passed by bootloader, "
					"defaulting to 0x%x\n", memsize);
			}
		}
	}

	return memsize;
}

/**
 * register_low_ram - register an aliased section of RAM
 * @p:		Alias address of memory
 * @n:		Number of bytes in this section of memory
 *
 * Returns the number of bytes registered
 *
 */
static __init phys_addr_t register_low_ram(phys_addr_t p, phys_addr_t n)
{
	phys_addr_t s;
	int i;
	phys_addr_t orig_n;

	orig_n = n;

	BUG_ON(p + n > RV_PHYS);

	for (i = 0; n != 0 && i < ARRAY_SIZE(low_mem_reserved); i++) {
		phys_addr_t start;
		phys_addr_t size;

		start = low_mem_reserved[i].start;
		size = low_mem_reserved[i].size;

		/* Handle memory before this low memory section */
		if (p < start) {
			phys_addr_t s;
			s = min(n, start - p);
			add_memory_region(p, s, BOOT_MEM_RAM);
			p += s;
			n -= s;
		}

		/* Handle the low memory section itself. If it's aliased,
		 * we reduce the number of byes left, but if not, the RAM
		 * is available elsewhere and we don't reduce the number of
		 * bytes remaining. */
		if (p == start) {
			if (low_mem_reserved[i].is_aliased) {
				s = min(n, size);
				n -= s;
				p += s;
			} else
				p += n;
		}
	}

	return orig_n - n;
}

/*
 * register_ram - register real RAM
 * @p:	Address of memory as seen by devices
 * @alias:	If the memory is seen at an additional address by the processor,
 *		this will be the address, otherwise it is the same as @p.
 * @n:		Number of bytes in this section of memory
 */
static __init void register_ram(phys_addr_t p, phys_addr_t alias,
	phys_addr_t n)
{
	/*
	 * If some or all of this memory has an alias, break it into the
	 * aliased and non-aliased portion.
	 */
	if (p != alias) {
		phys_addr_t alias_size;
		phys_addr_t registered;

		alias_size = min(n, LOW_RAM_END - alias);
		registered = register_low_ram(alias, alias_size);
		ioremap_add_map(alias, p, n);
		n -= registered;
		p += registered;
	}

#ifdef CONFIG_HIGHMEM
	if (n != 0) {
		add_memory_region(p, n, BOOT_MEM_RAM);
		ioremap_add_map(p, p, n);
	}
#endif
}

/**
 * register_address_space - register things in the address space
 * @memsize:	Number of bytes of RAM installed
 *
 * Takes the given number of bytes of RAM and registers as many of the regions,
 * or partial regions, as it can. So, the default configuration might have
 * two regions with 256 MiB each. If the memsize passed in on the command line
 * is 384 MiB, it will register the first region with 256 MiB and the second
 * with 128 MiB.
 */
static __init void register_address_space(phys_addr_t memsize)
{
	int i;
	phys_addr_t size;
	size_t n;
	struct mem_layout *layout;
	enum family_type family;

	/*
	 * Register all of the things that aren't available to the kernel as
	 * memory.
	 */
	register_non_ram();

	/* Find the appropriate memory description */
	family = platform_get_family();

	for (i = 0; i < ARRAY_SIZE(layout_list); i++) {
		if (layout_list[i].family == family)
			break;
	}

	if (i == ARRAY_SIZE(layout_list)) {
		n = ARRAY_SIZE(default_layout);
		layout = default_layout;
	} else {
		n = layout_list[i].n;
		layout = layout_list[i].layout;
	}

	for (i = 0; memsize != 0 && i < n; i++) {
		size = min(memsize, layout[i].size);
		register_ram(layout[i].phys, layout[i].alias, size);
		memsize -= size;
	}
}

void __init prom_meminit(void)
{
	ptv_memsize = get_memsize();
	register_address_space(ptv_memsize);
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
