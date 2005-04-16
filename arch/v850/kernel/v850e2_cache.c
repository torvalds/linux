/*
 * arch/v850/kernel/v850e2_cache.c -- Cache control for V850E2 cache
 * 	memories
 *
 *  Copyright (C) 2003  NEC Electronics Corporation
 *  Copyright (C) 2003  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/mm.h>

#include <asm/v850e2_cache.h>

/* Cache operations we can do.  The encoding corresponds directly to the
   value we need to write into the COPR register.  */
enum cache_op {
	OP_SYNC_IF_DIRTY 	   = V850E2_CACHE_COPR_CFC(0), /* 000 */
	OP_SYNC_IF_VALID 	   = V850E2_CACHE_COPR_CFC(1), /* 001 */
	OP_SYNC_IF_VALID_AND_CLEAR = V850E2_CACHE_COPR_CFC(3), /* 011 */
	OP_WAY_CLEAR 		   = V850E2_CACHE_COPR_CFC(4), /* 100 */
	OP_FILL 		   = V850E2_CACHE_COPR_CFC(5), /* 101 */
	OP_CLEAR 		   = V850E2_CACHE_COPR_CFC(6), /* 110 */
	OP_CREATE_DIRTY 	   = V850E2_CACHE_COPR_CFC(7)  /* 111 */
};

/* Which cache to use.  This encoding also corresponds directly to the
   value we need to write into the COPR register. */
enum cache {
	ICACHE = 0,
	DCACHE = V850E2_CACHE_COPR_LBSL
};

/* Returns ADDR rounded down to the beginning of its cache-line.  */
#define CACHE_LINE_ADDR(addr)  \
   ((addr) & ~(V850E2_CACHE_LINE_SIZE - 1))
/* Returns END_ADDR rounded up to the `limit' of its cache-line.  */
#define CACHE_LINE_END_ADDR(end_addr)  \
   CACHE_LINE_ADDR(end_addr + (V850E2_CACHE_LINE_SIZE - 1))


/* Low-level cache ops.  */

/* Apply cache-op OP to all entries in CACHE.  */
static inline void cache_op_all (enum cache_op op, enum cache cache)
{
	int cmd = op | cache | V850E2_CACHE_COPR_WSLE | V850E2_CACHE_COPR_STRT;

	if (op != OP_WAY_CLEAR) {
		/* The WAY_CLEAR operation does the whole way, but other
		   ops take begin-index and count params; we just indicate
		   the entire cache.  */
		V850E2_CACHE_CADL = 0;
		V850E2_CACHE_CADH = 0;
		V850E2_CACHE_CCNT = V850E2_CACHE_WAY_SIZE - 1;
	}

	V850E2_CACHE_COPR = cmd | V850E2_CACHE_COPR_WSL(0); /* way 0 */
	V850E2_CACHE_COPR = cmd | V850E2_CACHE_COPR_WSL(1); /* way 1 */
	V850E2_CACHE_COPR = cmd | V850E2_CACHE_COPR_WSL(2); /* way 2 */
	V850E2_CACHE_COPR = cmd | V850E2_CACHE_COPR_WSL(3); /* way 3 */
}

/* Apply cache-op OP to all entries in CACHE covering addresses ADDR
   through ADDR+LEN.  */
static inline void cache_op_range (enum cache_op op, u32 addr, u32 len,
				   enum cache cache)
{
	u32 start = CACHE_LINE_ADDR (addr);
	u32 end = CACHE_LINE_END_ADDR (addr + len);
	u32 num_lines = (end - start) >> V850E2_CACHE_LINE_SIZE_BITS;

	V850E2_CACHE_CADL = start & 0xFFFF;
	V850E2_CACHE_CADH = start >> 16;
	V850E2_CACHE_CCNT = num_lines - 1;

	V850E2_CACHE_COPR = op | cache | V850E2_CACHE_COPR_STRT;
}


/* High-level ops.  */

static void cache_exec_after_store_all (void)
{
	cache_op_all (OP_SYNC_IF_DIRTY, DCACHE);
	cache_op_all (OP_WAY_CLEAR, ICACHE);
}

static void cache_exec_after_store_range (u32 start, u32 len)
{
	cache_op_range (OP_SYNC_IF_DIRTY, start, len, DCACHE);
	cache_op_range (OP_CLEAR, start, len, ICACHE);
}


/* Exported functions.  */

void flush_icache (void)
{
	cache_exec_after_store_all ();
}

void flush_icache_range (unsigned long start, unsigned long end)
{
	cache_exec_after_store_range (start, end - start);
}

void flush_icache_page (struct vm_area_struct *vma, struct page *page)
{
	cache_exec_after_store_range (page_to_virt (page), PAGE_SIZE);
}

void flush_icache_user_range (struct vm_area_struct *vma, struct page *page,
			      unsigned long addr, int len)
{
	cache_exec_after_store_range (addr, len);
}

void flush_cache_sigtramp (unsigned long addr)
{
	/* For the exact size, see signal.c, but 16 bytes should be enough.  */
	cache_exec_after_store_range (addr, 16);
}
