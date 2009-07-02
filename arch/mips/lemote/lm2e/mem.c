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

extern unsigned long memsize, highmemsize;

void __init prom_init_memory(void)
{
    add_memory_region(0x0, (memsize << 20), BOOT_MEM_RAM);
#ifdef CONFIG_64BIT
    if (highmemsize > 0)
		add_memory_region(0x20000000, highmemsize << 20, BOOT_MEM_RAM);
#endif /* CONFIG_64BIT */
}

/* override of arch/mips/mm/cache.c: __uncached_access */
int __uncached_access(struct file *file, unsigned long addr)
{
	if (file->f_flags & O_SYNC)
		return 1;

	/*
	 * On the Lemote Loongson 2e system, the peripheral registers
	 * reside between 0x1000:0000 and 0x2000:0000.
	 */
	return addr >= __pa(high_memory) ||
		((addr >= 0x10000000) && (addr < 0x20000000));
}
