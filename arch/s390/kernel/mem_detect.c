/*
 *    Copyright IBM Corp. 2008
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/ipl.h>
#include <asm/sclp.h>
#include <asm/setup.h>

static inline int tprot(unsigned long addr)
{
	int rc = -EFAULT;

	asm volatile(
		"	tprot	0(%1),0\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (rc) : "a" (addr) : "cc");
	return rc;
}

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
	flags = __raw_local_irq_stnsm(0xf8);
	__ctl_store(cr0, 0, 0);
	__ctl_clear_bit(0, 28);
	find_memory_chunks(chunk);
	__ctl_load(cr0, 0, 0);
	__raw_local_irq_ssm(flags);
}
EXPORT_SYMBOL(detect_memory_layout);
