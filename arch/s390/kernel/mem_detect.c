/*
 * Copyright IBM Corp. 2008, 2009
 *
 * Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/ipl.h>
#include <asm/sclp.h>
#include <asm/setup.h>

#define ADDR2G (1ULL << 31)

static void find_memory_chunks(struct mem_chunk chunk[])
{
	unsigned long long memsize, rnmax, rzm;
	unsigned long addr = 0, size;
	int i = 0, type;

	rzm = sclp_get_rzm();
	rnmax = sclp_get_rnmax();
	memsize = rzm * rnmax;
	if (!rzm)
		rzm = 1ULL << 17;
	if (sizeof(long) == 4) {
		rzm = min(ADDR2G, rzm);
		memsize = memsize ? min(ADDR2G, memsize) : ADDR2G;
	}
	do {
		size = 0;
		type = tprot(addr);
		do {
			size += rzm;
			if (memsize && addr + size >= memsize)
				break;
		} while (type == tprot(addr + size));
		if (type == CHUNK_READ_WRITE || type == CHUNK_READ_ONLY) {
			chunk[i].addr = addr;
			chunk[i].size = size;
			chunk[i].type = type;
			i++;
		}
		addr += size;
	} while (addr < memsize && i < MEMORY_CHUNKS);
}

void detect_memory_layout(struct mem_chunk chunk[])
{
	unsigned long flags, cr0;

	memset(chunk, 0, MEMORY_CHUNKS * sizeof(struct mem_chunk));
	/* Disable IRQs, DAT and low address protection so tprot does the
	 * right thing and we don't get scheduled away with low address
	 * protection disabled.
	 */
	flags = __arch_local_irq_stnsm(0xf8);
	__ctl_store(cr0, 0, 0);
	__ctl_clear_bit(0, 28);
	find_memory_chunks(chunk);
	__ctl_load(cr0, 0, 0);
	arch_local_irq_restore(flags);
}
EXPORT_SYMBOL(detect_memory_layout);

/*
 * Create memory hole with given address, size, and type
 */
void create_mem_hole(struct mem_chunk chunks[], unsigned long addr,
		     unsigned long size, int type)
{
	unsigned long start, end, new_size;
	int i;

	for (i = 0; i < MEMORY_CHUNKS; i++) {
		if (chunks[i].size == 0)
			continue;
		if (addr + size < chunks[i].addr)
			continue;
		if (addr >= chunks[i].addr + chunks[i].size)
			continue;
		start = max(addr, chunks[i].addr);
		end = min(addr + size, chunks[i].addr + chunks[i].size);
		new_size = end - start;
		if (new_size == 0)
			continue;
		if (start == chunks[i].addr &&
		    end == chunks[i].addr + chunks[i].size) {
			/* Remove chunk */
			chunks[i].type = type;
		} else if (start == chunks[i].addr) {
			/* Make chunk smaller at start */
			if (i >= MEMORY_CHUNKS - 1)
				panic("Unable to create memory hole");
			memmove(&chunks[i + 1], &chunks[i],
				sizeof(struct mem_chunk) *
				(MEMORY_CHUNKS - (i + 1)));
			chunks[i + 1].addr = chunks[i].addr + new_size;
			chunks[i + 1].size = chunks[i].size - new_size;
			chunks[i].size = new_size;
			chunks[i].type = type;
			i += 1;
		} else if (end == chunks[i].addr + chunks[i].size) {
			/* Make chunk smaller at end */
			if (i >= MEMORY_CHUNKS - 1)
				panic("Unable to create memory hole");
			memmove(&chunks[i + 1], &chunks[i],
				sizeof(struct mem_chunk) *
				(MEMORY_CHUNKS - (i + 1)));
			chunks[i + 1].addr = start;
			chunks[i + 1].size = new_size;
			chunks[i + 1].type = type;
			chunks[i].size -= new_size;
			i += 1;
		} else {
			/* Create memory hole */
			if (i >= MEMORY_CHUNKS - 2)
				panic("Unable to create memory hole");
			memmove(&chunks[i + 2], &chunks[i],
				sizeof(struct mem_chunk) *
				(MEMORY_CHUNKS - (i + 2)));
			chunks[i + 1].addr = addr;
			chunks[i + 1].size = size;
			chunks[i + 1].type = type;
			chunks[i + 2].addr = addr + size;
			chunks[i + 2].size =
				chunks[i].addr + chunks[i].size - (addr + size);
			chunks[i + 2].type = chunks[i].type;
			chunks[i].size = addr - chunks[i].addr;
			i += 2;
		}
	}
}
