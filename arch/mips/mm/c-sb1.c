/*
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 2001 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 2000, 2001, 2002, 2003 Broadcom Corporation
 * Copyright (C) 2004  Maciej W. Rozycki
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/init.h>

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cacheops.h>
#include <asm/cpu.h>
#include <asm/mipsregs.h>
#include <asm/mmu_context.h>
#include <asm/uaccess.h>

extern void sb1_dma_init(void);

/* These are probed at ld_mmu time */
static unsigned long icache_size;
static unsigned long dcache_size;

static unsigned short icache_line_size;
static unsigned short dcache_line_size;

static unsigned int icache_index_mask;
static unsigned int dcache_index_mask;

static unsigned short icache_assoc;
static unsigned short dcache_assoc;

static unsigned short icache_sets;
static unsigned short dcache_sets;

static unsigned int icache_range_cutoff;
static unsigned int dcache_range_cutoff;

static inline void sb1_on_each_cpu(void (*func) (void *info), void *info,
				   int retry, int wait)
{
	preempt_disable();
	smp_call_function(func, info, retry, wait);
	func(info);
	preempt_enable();
}

/*
 * The dcache is fully coherent to the system, with one
 * big caveat:  the instruction stream.  In other words,
 * if we miss in the icache, and have dirty data in the
 * L1 dcache, then we'll go out to memory (or the L2) and
 * get the not-as-recent data.
 *
 * So the only time we have to flush the dcache is when
 * we're flushing the icache.  Since the L2 is fully
 * coherent to everything, including I/O, we never have
 * to flush it
 */

#define cache_set_op(op, addr)						\
	__asm__ __volatile__(						\
	"	.set	noreorder		\n"			\
	"	.set	mips64\n\t		\n"			\
	"	cache	%0, (0<<13)(%1)		\n"			\
	"	cache	%0, (1<<13)(%1)		\n"			\
	"	cache	%0, (2<<13)(%1)		\n"			\
	"	cache	%0, (3<<13)(%1)		\n"			\
	"	.set	mips0			\n"			\
	"	.set	reorder"					\
	:								\
	: "i" (op), "r" (addr))

#define sync()								\
	__asm__ __volatile(						\
	"	.set	mips64\n\t		\n"			\
	"	sync				\n"			\
	"	.set	mips0")

#define mispredict()							\
	__asm__ __volatile__(						\
	"	bnezl  $0, 1f		\n" /* Force mispredict */	\
	"1:				\n");

/*
 * Writeback and invalidate the entire dcache
 */
static inline void __sb1_writeback_inv_dcache_all(void)
{
	unsigned long addr = 0;

	while (addr < dcache_line_size * dcache_sets) {
		cache_set_op(Index_Writeback_Inv_D, addr);
		addr += dcache_line_size;
	}
}

/*
 * Writeback and invalidate a range of the dcache.  The addresses are
 * virtual, and since we're using index ops and bit 12 is part of both
 * the virtual frame and physical index, we have to clear both sets
 * (bit 12 set and cleared).
 */
static inline void __sb1_writeback_inv_dcache_range(unsigned long start,
	unsigned long end)
{
	unsigned long index;

	start &= ~(dcache_line_size - 1);
	end = (end + dcache_line_size - 1) & ~(dcache_line_size - 1);

	while (start != end) {
		index = start & dcache_index_mask;
		cache_set_op(Index_Writeback_Inv_D, index);
		cache_set_op(Index_Writeback_Inv_D, index ^ (1<<12));
		start += dcache_line_size;
	}
	sync();
}

/*
 * Writeback and invalidate a range of the dcache.  With physical
 * addresseses, we don't have to worry about possible bit 12 aliasing.
 * XXXKW is it worth turning on KX and using hit ops with xkphys?
 */
static inline void __sb1_writeback_inv_dcache_phys_range(unsigned long start,
	unsigned long end)
{
	start &= ~(dcache_line_size - 1);
	end = (end + dcache_line_size - 1) & ~(dcache_line_size - 1);

	while (start != end) {
		cache_set_op(Index_Writeback_Inv_D, start & dcache_index_mask);
		start += dcache_line_size;
	}
	sync();
}


/*
 * Invalidate the entire icache
 */
static inline void __sb1_flush_icache_all(void)
{
	unsigned long addr = 0;

	while (addr < icache_line_size * icache_sets) {
		cache_set_op(Index_Invalidate_I, addr);
		addr += icache_line_size;
	}
}

/*
 * Invalidate a range of the icache.  The addresses are virtual, and
 * the cache is virtually indexed and tagged.  However, we don't
 * necessarily have the right ASID context, so use index ops instead
 * of hit ops.
 */
static inline void __sb1_flush_icache_range(unsigned long start,
	unsigned long end)
{
	start &= ~(icache_line_size - 1);
	end = (end + icache_line_size - 1) & ~(icache_line_size - 1);

	while (start != end) {
		cache_set_op(Index_Invalidate_I, start & icache_index_mask);
		start += icache_line_size;
	}
	mispredict();
	sync();
}

/*
 * Flush the icache for a given physical page.  Need to writeback the
 * dcache first, then invalidate the icache.  If the page isn't
 * executable, nothing is required.
 */
static void local_sb1_flush_cache_page(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn)
{
	int cpu = smp_processor_id();

#ifndef CONFIG_SMP
	if (!(vma->vm_flags & VM_EXEC))
		return;
#endif

	__sb1_writeback_inv_dcache_range(addr, addr + PAGE_SIZE);

	/*
	 * Bumping the ASID is probably cheaper than the flush ...
	 */
	if (vma->vm_mm == current->active_mm) {
		if (cpu_context(cpu, vma->vm_mm) != 0)
			drop_mmu_context(vma->vm_mm, cpu);
	} else
		__sb1_flush_icache_range(addr, addr + PAGE_SIZE);
}

#ifdef CONFIG_SMP
struct flush_cache_page_args {
	struct vm_area_struct *vma;
	unsigned long addr;
	unsigned long pfn;
};

static void sb1_flush_cache_page_ipi(void *info)
{
	struct flush_cache_page_args *args = info;

	local_sb1_flush_cache_page(args->vma, args->addr, args->pfn);
}

/* Dirty dcache could be on another CPU, so do the IPIs */
static void sb1_flush_cache_page(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn)
{
	struct flush_cache_page_args args;

	if (!(vma->vm_flags & VM_EXEC))
		return;

	addr &= PAGE_MASK;
	args.vma = vma;
	args.addr = addr;
	args.pfn = pfn;
	sb1_on_each_cpu(sb1_flush_cache_page_ipi, (void *) &args, 1, 1);
}
#else
void sb1_flush_cache_page(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn)
	__attribute__((alias("local_sb1_flush_cache_page")));
#endif


/*
 * Invalidate all caches on this CPU
 */
static void __attribute_used__ local_sb1___flush_cache_all(void)
{
	__sb1_writeback_inv_dcache_all();
	__sb1_flush_icache_all();
}

#ifdef CONFIG_SMP
void sb1___flush_cache_all_ipi(void *ignored)
	__attribute__((alias("local_sb1___flush_cache_all")));

static void sb1___flush_cache_all(void)
{
	sb1_on_each_cpu(sb1___flush_cache_all_ipi, 0, 1, 1);
}
#else
void sb1___flush_cache_all(void)
	__attribute__((alias("local_sb1___flush_cache_all")));
#endif

/*
 * When flushing a range in the icache, we have to first writeback
 * the dcache for the same range, so new ifetches will see any
 * data that was dirty in the dcache.
 *
 * The start/end arguments are Kseg addresses (possibly mapped Kseg).
 */

static void local_sb1_flush_icache_range(unsigned long start,
	unsigned long end)
{
	/* Just wb-inv the whole dcache if the range is big enough */
	if ((end - start) > dcache_range_cutoff)
		__sb1_writeback_inv_dcache_all();
	else
		__sb1_writeback_inv_dcache_range(start, end);

	/* Just flush the whole icache if the range is big enough */
	if ((end - start) > icache_range_cutoff)
		__sb1_flush_icache_all();
	else
		__sb1_flush_icache_range(start, end);
}

#ifdef CONFIG_SMP
struct flush_icache_range_args {
	unsigned long start;
	unsigned long end;
};

static void sb1_flush_icache_range_ipi(void *info)
{
	struct flush_icache_range_args *args = info;

	local_sb1_flush_icache_range(args->start, args->end);
}

void sb1_flush_icache_range(unsigned long start, unsigned long end)
{
	struct flush_icache_range_args args;

	args.start = start;
	args.end = end;
	sb1_on_each_cpu(sb1_flush_icache_range_ipi, &args, 1, 1);
}
#else
void sb1_flush_icache_range(unsigned long start, unsigned long end)
	__attribute__((alias("local_sb1_flush_icache_range")));
#endif

/*
 * A signal trampoline must fit into a single cacheline.
 */
static void local_sb1_flush_cache_sigtramp(unsigned long addr)
{
	cache_set_op(Index_Writeback_Inv_D, addr & dcache_index_mask);
	cache_set_op(Index_Writeback_Inv_D, (addr ^ (1<<12)) & dcache_index_mask);
	cache_set_op(Index_Invalidate_I, addr & icache_index_mask);
	mispredict();
}

#ifdef CONFIG_SMP
static void sb1_flush_cache_sigtramp_ipi(void *info)
{
	unsigned long iaddr = (unsigned long) info;
	local_sb1_flush_cache_sigtramp(iaddr);
}

static void sb1_flush_cache_sigtramp(unsigned long addr)
{
	sb1_on_each_cpu(sb1_flush_cache_sigtramp_ipi, (void *) addr, 1, 1);
}
#else
void sb1_flush_cache_sigtramp(unsigned long addr)
	__attribute__((alias("local_sb1_flush_cache_sigtramp")));
#endif


/*
 * Anything that just flushes dcache state can be ignored, as we're always
 * coherent in dcache space.  This is just a dummy function that all the
 * nop'ed routines point to
 */
static void sb1_nop(void)
{
}

/*
 *  Cache set values (from the mips64 spec)
 * 0 - 64
 * 1 - 128
 * 2 - 256
 * 3 - 512
 * 4 - 1024
 * 5 - 2048
 * 6 - 4096
 * 7 - Reserved
 */

static unsigned int decode_cache_sets(unsigned int config_field)
{
	if (config_field == 7) {
		/* JDCXXX - Find a graceful way to abort. */
		return 0;
	}
	return (1<<(config_field + 6));
}

/*
 *  Cache line size values (from the mips64 spec)
 * 0 - No cache present.
 * 1 - 4 bytes
 * 2 - 8 bytes
 * 3 - 16 bytes
 * 4 - 32 bytes
 * 5 - 64 bytes
 * 6 - 128 bytes
 * 7 - Reserved
 */

static unsigned int decode_cache_line_size(unsigned int config_field)
{
	if (config_field == 0) {
		return 0;
	} else if (config_field == 7) {
		/* JDCXXX - Find a graceful way to abort. */
		return 0;
	}
	return (1<<(config_field + 1));
}

/*
 * Relevant bits of the config1 register format (from the MIPS32/MIPS64 specs)
 *
 * 24:22 Icache sets per way
 * 21:19 Icache line size
 * 18:16 Icache Associativity
 * 15:13 Dcache sets per way
 * 12:10 Dcache line size
 * 9:7   Dcache Associativity
 */

static char *way_string[] = {
	"direct mapped", "2-way", "3-way", "4-way",
	"5-way", "6-way", "7-way", "8-way",
};

static __init void probe_cache_sizes(void)
{
	u32 config1;

	config1 = read_c0_config1();
	icache_line_size = decode_cache_line_size((config1 >> 19) & 0x7);
	dcache_line_size = decode_cache_line_size((config1 >> 10) & 0x7);
	icache_sets = decode_cache_sets((config1 >> 22) & 0x7);
	dcache_sets = decode_cache_sets((config1 >> 13) & 0x7);
	icache_assoc = ((config1 >> 16) & 0x7) + 1;
	dcache_assoc = ((config1 >> 7) & 0x7) + 1;
	icache_size = icache_line_size * icache_sets * icache_assoc;
	dcache_size = dcache_line_size * dcache_sets * dcache_assoc;
	/* Need to remove non-index bits for index ops */
	icache_index_mask = (icache_sets - 1) * icache_line_size;
	dcache_index_mask = (dcache_sets - 1) * dcache_line_size;
	/*
	 * These are for choosing range (index ops) versus all.
	 * icache flushes all ways for each set, so drop icache_assoc.
	 * dcache flushes all ways and each setting of bit 12 for each
	 * index, so drop dcache_assoc and halve the dcache_sets.
	 */
	icache_range_cutoff = icache_sets * icache_line_size;
	dcache_range_cutoff = (dcache_sets / 2) * icache_line_size;

	printk("Primary instruction cache %ldkB, %s, linesize %d bytes.\n",
	       icache_size >> 10, way_string[icache_assoc - 1],
	       icache_line_size);
	printk("Primary data cache %ldkB, %s, linesize %d bytes.\n",
	       dcache_size >> 10, way_string[dcache_assoc - 1],
	       dcache_line_size);
}

/*
 * This is called from cache.c.  We have to set up all the
 * memory management function pointers, as well as initialize
 * the caches and tlbs
 */
void sb1_cache_init(void)
{
	extern char except_vec2_sb1;

	/* Special cache error handler for SB1 */
	set_uncached_handler (0x100, &except_vec2_sb1, 0x80);

	probe_cache_sizes();

#ifdef CONFIG_SIBYTE_DMA_PAGEOPS
	sb1_dma_init();
#endif

	/*
	 * None of these are needed for the SB1 - the Dcache is
	 * physically indexed and tagged, so no virtual aliasing can
	 * occur
	 */
	flush_cache_range = (void *) sb1_nop;
	flush_cache_mm = (void (*)(struct mm_struct *))sb1_nop;
	flush_cache_all = sb1_nop;

	/* These routines are for Icache coherence with the Dcache */
	flush_icache_range = sb1_flush_icache_range;
	flush_icache_all = __sb1_flush_icache_all; /* local only */

	/* This implies an Icache flush too, so can't be nop'ed */
	flush_cache_page = sb1_flush_cache_page;

	flush_cache_sigtramp = sb1_flush_cache_sigtramp;
	local_flush_data_cache_page = (void *) sb1_nop;
	flush_data_cache_page = (void *) sb1_nop;

	/* Full flush */
	__flush_cache_all = sb1___flush_cache_all;

	change_c0_config(CONF_CM_CMASK, CONF_CM_DEFAULT);

	/*
	 * This is the only way to force the update of K0 to complete
	 * before subsequent instruction fetch.
	 */
	__asm__ __volatile__(
		".set	push			\n"
	"	.set	noat			\n"
	"	.set	noreorder		\n"
	"	.set	mips3			\n"
	"	" STR(PTR_LA) "	$1, 1f		\n"
	"	" STR(MTC0) "	$1, $14		\n"
	"	eret				\n"
	"1:	.set	pop"
	:
	:
	: "memory");

	local_sb1___flush_cache_all();
}
