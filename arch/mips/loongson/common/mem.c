/*
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/mm.h>

#include <asm/bootinfo.h>

#include <loongson.h>
#include <mem.h>

void __init prom_init_memory(void)
{
    add_memory_region(0x0, (memsize << 20), BOOT_MEM_RAM);
#ifdef CONFIG_64BIT
    if (highmemsize > 0)
	add_memory_region(LOONGSON_HIGHMEM_START,
		highmemsize << 20, BOOT_MEM_RAM);
#endif /* CONFIG_64BIT */
}

/* override of arch/mips/mm/cache.c: __uncached_access */
int __uncached_access(struct file *file, unsigned long addr)
{
	if (file->f_flags & O_SYNC)
		return 1;

	return addr >= __pa(high_memory) ||
		((addr >= LOONGSON_MMIO_MEM_START) &&
		 (addr < LOONGSON_MMIO_MEM_END));
}
