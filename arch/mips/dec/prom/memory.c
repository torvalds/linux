// SPDX-License-Identifier: GPL-2.0
/*
 * memory.c: memory initialisation code.
 *
 * Copyright (C) 1998 Harald Koerfgen, Frieder Streffer and Paul M. Antoine
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/dec/machtype.h>
#include <asm/dec/prom.h>
#include <asm/page.h>
#include <asm/sections.h>


volatile unsigned long mem_err;		/* So we know an error occurred */

/*
 * Probe memory in 4MB chunks, waiting for an error to tell us we've fallen
 * off the end of real memory.  Only suitable for the 2100/3100's (PMAX).
 */

#define CHUNK_SIZE 0x400000

static inline void pmax_setup_memory_region(void)
{
	volatile unsigned char *memory_page, dummy;
	char old_handler[0x80];
	extern char genexcept_early;

	/* Install exception handler */
	memcpy(&old_handler, (void *)(CKSEG0 + 0x80), 0x80);
	memcpy((void *)(CKSEG0 + 0x80), &genexcept_early, 0x80);

	/* read unmapped and uncached (KSEG1)
	 * DECstations have at least 4MB RAM
	 * Assume less than 480MB of RAM, as this is max for 5000/2xx
	 * FIXME this should be replaced by the first free page!
	 */
	for (memory_page = (unsigned char *)CKSEG1 + CHUNK_SIZE;
	     mem_err == 0 && memory_page < (unsigned char *)CKSEG1 + 0x1e00000;
	     memory_page += CHUNK_SIZE) {
		dummy = *memory_page;
	}
	memcpy((void *)(CKSEG0 + 0x80), &old_handler, 0x80);

	memblock_add(0, (unsigned long)memory_page - CKSEG1 - CHUNK_SIZE);
}

/*
 * Use the REX prom calls to get hold of the memory bitmap, and thence
 * determine memory size.
 */
static inline void rex_setup_memory_region(void)
{
	int i, bitmap_size;
	unsigned long mem_start = 0, mem_size = 0;
	memmap *bm;

	/* some free 64k */
	bm = (memmap *)CKSEG0ADDR(0x28000);

	bitmap_size = rex_getbitmap(bm);

	for (i = 0; i < bitmap_size; i++) {
		/* FIXME: very simplistically only add full sets of pages */
		if (bm->bitmap[i] == 0xff)
			mem_size += (8 * bm->pagesize);
		else if (!mem_size)
			mem_start += (8 * bm->pagesize);
		else {
			memblock_add(mem_start, mem_size);
			mem_start += mem_size + (8 * bm->pagesize);
			mem_size = 0;
		}
	}
	if (mem_size)
		memblock_add(mem_start, mem_size);
}

void __init prom_meminit(u32 magic)
{
	if (!prom_is_rex(magic))
		pmax_setup_memory_region();
	else
		rex_setup_memory_region();
}

void __init prom_free_prom_memory(void)
{
	unsigned long end;

	/*
	 * Free everything below the kernel itself but leave
	 * the first page reserved for the exception handlers.
	 */

#if IS_ENABLED(CONFIG_DECLANCE)
	/*
	 * Leave 128 KB reserved for Lance memory for
	 * IOASIC DECstations.
	 *
	 * XXX: save this address for use in dec_lance.c?
	 */
	if (IOASIC)
		end = __pa(&_text) - 0x00020000;
	else
#endif
		end = __pa(&_text);

	free_init_pages("unused PROM memory", PAGE_SIZE, end);
}
