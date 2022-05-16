// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/memblock.h>
#include <linux/mm.h>

#include <asm/bootinfo.h>

#include <loongson.h>
#include <mem.h>
#include <pci.h>


u32 memsize, highmemsize;

void __init prom_init_memory(void)
{
	memblock_add(0x0, (memsize << 20));

#ifdef CONFIG_CPU_SUPPORTS_ADDRWINCFG
	{
		int bit;

		bit = fls(memsize + highmemsize);
		if (bit != ffs(memsize + highmemsize))
			bit += 20;
		else
			bit = bit + 20 - 1;

		/* set cpu window3 to map CPU to DDR: 2G -> 2G */
		LOONGSON_ADDRWIN_CPUTODDR(ADDRWIN_WIN3, 0x80000000ul,
					  0x80000000ul, (1 << bit));
		mmiowb();
	}
#endif /* !CONFIG_CPU_SUPPORTS_ADDRWINCFG */

#ifdef CONFIG_64BIT
	if (highmemsize > 0)
		memblock_add(LOONGSON_HIGHMEM_START, highmemsize << 20);
#endif /* !CONFIG_64BIT */
}

/* override of arch/mips/mm/cache.c: __uncached_access */
int __uncached_access(struct file *file, unsigned long addr)
{
	if (file->f_flags & O_DSYNC)
		return 1;

	return addr >= __pa(high_memory) ||
		((addr >= LOONGSON_MMIO_MEM_START) &&
		 (addr < LOONGSON_MMIO_MEM_END));
}
