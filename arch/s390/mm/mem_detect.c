/*
 * Copyright IBM Corp. 2008, 2009
 *
 * Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <asm/ipl.h>
#include <asm/sclp.h>
#include <asm/setup.h>

#define ADDR2G (1ULL << 31)

#define CHUNK_READ_WRITE 0
#define CHUNK_READ_ONLY  1

static inline void memblock_physmem_add(phys_addr_t start, phys_addr_t size)
{
	memblock_add_range(&memblock.memory, start, size, 0, 0);
	memblock_add_range(&memblock.physmem, start, size, 0, 0);
}

void __init detect_memory_memblock(void)
{
	unsigned long long memsize, rnmax, rzm;
	unsigned long addr, size;
	int type;

	rzm = sclp_get_rzm();
	rnmax = sclp_get_rnmax();
	memsize = rzm * rnmax;
	if (!rzm)
		rzm = 1ULL << 17;
	max_physmem_end = memsize;
	addr = 0;
	/* keep memblock lists close to the kernel */
	memblock_set_bottom_up(true);
	do {
		size = 0;
		type = tprot(addr);
		do {
			size += rzm;
			if (max_physmem_end && addr + size >= max_physmem_end)
				break;
		} while (type == tprot(addr + size));
		if (type == CHUNK_READ_WRITE || type == CHUNK_READ_ONLY) {
			if (max_physmem_end && (addr + size > max_physmem_end))
				size = max_physmem_end - addr;
			memblock_physmem_add(addr, size);
		}
		addr += size;
	} while (addr < max_physmem_end);
	memblock_set_bottom_up(false);
	if (!max_physmem_end)
		max_physmem_end = memblock_end_of_DRAM();
}
