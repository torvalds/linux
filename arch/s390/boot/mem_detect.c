// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <asm/sclp.h>
#include <asm/sections.h>
#include <asm/mem_detect.h>
#include "compressed/decompressor.h"
#include "boot.h"

#define CHUNK_READ_WRITE 0
#define CHUNK_READ_ONLY  1

unsigned long __bootdata(max_physmem_end);
struct mem_detect_info __bootdata(mem_detect);

/* up to 256 storage elements, 1020 subincrements each */
#define ENTRIES_EXTENDED_MAX						       \
	(256 * (1020 / 2) * sizeof(struct mem_detect_block))

/*
 * To avoid corrupting old kernel memory during dump, find lowest memory
 * chunk possible either right after the kernel end (decompressed kernel) or
 * after initrd (if it is present and there is no hole between the kernel end
 * and initrd)
 */
static void *mem_detect_alloc_extended(void)
{
	unsigned long offset = ALIGN(mem_safe_offset(), sizeof(u64));

	if (IS_ENABLED(BLK_DEV_INITRD) && INITRD_START && INITRD_SIZE &&
	    INITRD_START < offset + ENTRIES_EXTENDED_MAX)
		offset = ALIGN(INITRD_START + INITRD_SIZE, sizeof(u64));

	return (void *)offset;
}

static struct mem_detect_block *__get_mem_detect_block_ptr(u32 n)
{
	if (n < MEM_INLINED_ENTRIES)
		return &mem_detect.entries[n];
	if (unlikely(!mem_detect.entries_extended))
		mem_detect.entries_extended = mem_detect_alloc_extended();
	return &mem_detect.entries_extended[n - MEM_INLINED_ENTRIES];
}

/*
 * sequential calls to add_mem_detect_block with adjacent memory areas
 * are merged together into single memory block.
 */
void add_mem_detect_block(u64 start, u64 end)
{
	struct mem_detect_block *block;

	if (mem_detect.count) {
		block = __get_mem_detect_block_ptr(mem_detect.count - 1);
		if (block->end == start) {
			block->end = end;
			return;
		}
	}

	block = __get_mem_detect_block_ptr(mem_detect.count);
	block->start = start;
	block->end = end;
	mem_detect.count++;
}

static unsigned long get_mem_detect_end(void)
{
	if (mem_detect.count)
		return __get_mem_detect_block_ptr(mem_detect.count - 1)->end;
	return 0;
}

static int tprot(unsigned long addr)
{
	unsigned long pgm_addr;
	int rc = -EFAULT;
	psw_t old = S390_lowcore.program_new_psw;

	S390_lowcore.program_new_psw.mask = __extract_psw();
	asm volatile(
		"	larl	%[pgm_addr],1f\n"
		"	stg	%[pgm_addr],%[psw_pgm_addr]\n"
		"	tprot	0(%[addr]),0\n"
		"	ipm	%[rc]\n"
		"	srl	%[rc],28\n"
		"1:\n"
		: [pgm_addr] "=&d"(pgm_addr),
		  [psw_pgm_addr] "=Q"(S390_lowcore.program_new_psw.addr),
		  [rc] "+&d"(rc)
		: [addr] "a"(addr)
		: "cc", "memory");
	S390_lowcore.program_new_psw = old;
	return rc;
}

static void scan_memory(unsigned long rzm)
{
	unsigned long addr, size;
	int type;

	if (!rzm)
		rzm = 1UL << 20;

	addr = 0;
	do {
		size = 0;
		/* assume lowcore is writable */
		type = addr ? tprot(addr) : CHUNK_READ_WRITE;
		do {
			size += rzm;
			if (max_physmem_end && addr + size >= max_physmem_end)
				break;
		} while (type == tprot(addr + size));
		if (type == CHUNK_READ_WRITE || type == CHUNK_READ_ONLY) {
			if (max_physmem_end && (addr + size > max_physmem_end))
				size = max_physmem_end - addr;
			add_mem_detect_block(addr, addr + size);
		}
		addr += size;
	} while (addr < max_physmem_end);
}

void detect_memory(void)
{
	unsigned long rzm;

	sclp_early_get_meminfo(&max_physmem_end, &rzm);

	if (!sclp_early_read_storage_info()) {
		mem_detect.info_source = MEM_DETECT_SCLP_STOR_INFO;
		return;
	}

	scan_memory(rzm);
	mem_detect.info_source = MEM_DETECT_TPROT_LOOP;
	if (!max_physmem_end)
		max_physmem_end = get_mem_detect_end();
}
