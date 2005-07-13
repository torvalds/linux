/*
 * arch/xtensa/mm/mmu.c
 *
 * Logic that manipulates the Xtensa MMU.  Derived from MIPS.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2003 Tensilica Inc.
 *
 * Joe Taylor
 * Chris Zankel	<chris@zankel.net>
 * Marc Gauthier
 */

#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/system.h>
#include <asm/cacheflush.h>


static inline void __flush_itlb_all (void)
{
	int way, index;

	for (way = 0; way < XCHAL_ITLB_ARF_WAYS; way++) {
		for (index = 0; index < ITLB_ENTRIES_PER_ARF_WAY; index++) {
			int entry = way + (index << PAGE_SHIFT);
			invalidate_itlb_entry_no_isync (entry);
		}
	}
	asm volatile ("isync\n");
}

static inline void __flush_dtlb_all (void)
{
	int way, index;

	for (way = 0; way < XCHAL_DTLB_ARF_WAYS; way++) {
		for (index = 0; index < DTLB_ENTRIES_PER_ARF_WAY; index++) {
			int entry = way + (index << PAGE_SHIFT);
			invalidate_dtlb_entry_no_isync (entry);
		}
	}
	asm volatile ("isync\n");
}


void flush_tlb_all (void)
{
	__flush_itlb_all();
	__flush_dtlb_all();
}

/* If mm is current, we simply assign the current task a new ASID, thus,
 * invalidating all previous tlb entries. If mm is someone else's user mapping,
 * wie invalidate the context, thus, when that user mapping is swapped in,
 * a new context will be assigned to it.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
#if 0
	printk("[tlbmm<%lx>]\n", (unsigned long)mm->context);
#endif

	if (mm == current->active_mm) {
		int flags;
		local_save_flags(flags);
		get_new_mmu_context(mm, asid_cache);
		set_rasid_register(ASID_INSERT(mm->context));
		local_irq_restore(flags);
	}
	else
		mm->context = 0;
}

void flush_tlb_range (struct vm_area_struct *vma,
    		      unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;

	if (mm->context == NO_CONTEXT)
		return;

#if 0
	printk("[tlbrange<%02lx,%08lx,%08lx>]\n",
			(unsigned long)mm->context, start, end);
#endif
	local_save_flags(flags);

	if (end-start + (PAGE_SIZE-1) <= SMALLEST_NTLB_ENTRIES << PAGE_SHIFT) {
		int oldpid = get_rasid_register();
		set_rasid_register (ASID_INSERT(mm->context));
		start &= PAGE_MASK;
 		if (vma->vm_flags & VM_EXEC)
			while(start < end) {
				invalidate_itlb_mapping(start);
				invalidate_dtlb_mapping(start);
				start += PAGE_SIZE;
			}
		else
			while(start < end) {
				invalidate_dtlb_mapping(start);
				start += PAGE_SIZE;
			}

		set_rasid_register(oldpid);
	} else {
		get_new_mmu_context(mm, asid_cache);
		if (mm == current->active_mm)
			set_rasid_register(ASID_INSERT(mm->context));
	}
	local_irq_restore(flags);
}

void flush_tlb_page (struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct* mm = vma->vm_mm;
	unsigned long flags;
	int oldpid;
#if 0
	printk("[tlbpage<%02lx,%08lx>]\n",
			(unsigned long)mm->context, page);
#endif

	if(mm->context == NO_CONTEXT)
		return;

	local_save_flags(flags);

       	oldpid = get_rasid_register();

	if (vma->vm_flags & VM_EXEC)
		invalidate_itlb_mapping(page);
	invalidate_dtlb_mapping(page);

	set_rasid_register(oldpid);

	local_irq_restore(flags);

#if 0
	flush_tlb_all();
	return;
#endif
}


#ifdef DEBUG_TLB

#define USE_ITLB  0
#define USE_DTLB  1

struct way_config_t {
	int indicies;
	int indicies_log2;
	int pgsz_log2;
	int arf;
};

static struct way_config_t itlb[XCHAL_ITLB_WAYS] =
{
	{ XCHAL_ITLB_SET(XCHAL_ITLB_WAY0_SET, ENTRIES),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY0_SET, ENTRIES_LOG2),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY0_SET, PAGESZ_LOG2_MIN),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY0_SET, ARF)
	},
	{ XCHAL_ITLB_SET(XCHAL_ITLB_WAY1_SET, ENTRIES),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY1_SET, ENTRIES_LOG2),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY1_SET, PAGESZ_LOG2_MIN),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY1_SET, ARF)
	},
	{ XCHAL_ITLB_SET(XCHAL_ITLB_WAY2_SET, ENTRIES),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY2_SET, ENTRIES_LOG2),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY2_SET, PAGESZ_LOG2_MIN),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY2_SET, ARF)
	},
	{ XCHAL_ITLB_SET(XCHAL_ITLB_WAY3_SET, ENTRIES),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY3_SET, ENTRIES_LOG2),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY3_SET, PAGESZ_LOG2_MIN),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY3_SET, ARF)
	},
	{ XCHAL_ITLB_SET(XCHAL_ITLB_WAY4_SET, ENTRIES),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY4_SET, ENTRIES_LOG2),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY4_SET, PAGESZ_LOG2_MIN),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY4_SET, ARF)
	},
	{ XCHAL_ITLB_SET(XCHAL_ITLB_WAY5_SET, ENTRIES),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY5_SET, ENTRIES_LOG2),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY5_SET, PAGESZ_LOG2_MIN),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY5_SET, ARF)
	},
	{ XCHAL_ITLB_SET(XCHAL_ITLB_WAY6_SET, ENTRIES),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY6_SET, ENTRIES_LOG2),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY6_SET, PAGESZ_LOG2_MIN),
	  XCHAL_ITLB_SET(XCHAL_ITLB_WAY6_SET, ARF)
	}
};

static struct way_config_t dtlb[XCHAL_DTLB_WAYS] =
{
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY0_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY0_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY0_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY0_SET, ARF)
	},
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY1_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY1_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY1_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY1_SET, ARF)
	},
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY2_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY2_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY2_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY2_SET, ARF)
	},
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY3_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY3_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY3_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY3_SET, ARF)
	},
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY4_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY4_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY4_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY4_SET, ARF)
	},
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY5_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY5_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY5_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY5_SET, ARF)
	},
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY6_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY6_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY6_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY6_SET, ARF)
	},
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY7_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY7_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY7_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY7_SET, ARF)
	},
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY8_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY8_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY8_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY8_SET, ARF)
	},
	{ XCHAL_DTLB_SET(XCHAL_DTLB_WAY9_SET, ENTRIES),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY9_SET, ENTRIES_LOG2),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY9_SET, PAGESZ_LOG2_MIN),
	  XCHAL_DTLB_SET(XCHAL_DTLB_WAY9_SET, ARF)
	}
};

/*  Total number of entries:  */
#define ITLB_TOTAL_ENTRIES	\
		XCHAL_ITLB_SET(XCHAL_ITLB_WAY0_SET, ENTRIES) + \
		XCHAL_ITLB_SET(XCHAL_ITLB_WAY1_SET, ENTRIES) + \
		XCHAL_ITLB_SET(XCHAL_ITLB_WAY2_SET, ENTRIES) + \
		XCHAL_ITLB_SET(XCHAL_ITLB_WAY3_SET, ENTRIES) + \
		XCHAL_ITLB_SET(XCHAL_ITLB_WAY4_SET, ENTRIES) + \
		XCHAL_ITLB_SET(XCHAL_ITLB_WAY5_SET, ENTRIES) + \
		XCHAL_ITLB_SET(XCHAL_ITLB_WAY6_SET, ENTRIES)
#define DTLB_TOTAL_ENTRIES	\
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY0_SET, ENTRIES) + \
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY1_SET, ENTRIES) + \
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY2_SET, ENTRIES) + \
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY3_SET, ENTRIES) + \
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY4_SET, ENTRIES) + \
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY5_SET, ENTRIES) + \
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY6_SET, ENTRIES) + \
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY7_SET, ENTRIES) + \
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY8_SET, ENTRIES) + \
		XCHAL_DTLB_SET(XCHAL_DTLB_WAY9_SET, ENTRIES)


typedef struct {
    unsigned		va;
    unsigned		pa;
    unsigned char	asid;
    unsigned char	ca;
    unsigned char	way;
    unsigned char	index;
    unsigned char	pgsz_log2;	/* 0 .. 32 */
    unsigned char	type;		/* 0=ITLB 1=DTLB */
} tlb_dump_entry_t;

/*  Return -1 if a precedes b, +1 if a follows b, 0 if same:  */
int cmp_tlb_dump_info( tlb_dump_entry_t *a, tlb_dump_entry_t *b )
{
    if (a->asid < b->asid) return -1;
    if (a->asid > b->asid) return  1;
    if (a->va < b->va) return -1;
    if (a->va > b->va) return  1;
    if (a->pa < b->pa) return -1;
    if (a->pa > b->pa) return  1;
    if (a->ca < b->ca) return -1;
    if (a->ca > b->ca) return  1;
    if (a->way < b->way) return -1;
    if (a->way > b->way) return  1;
    if (a->index < b->index) return -1;
    if (a->index > b->index) return  1;
    return 0;
}

void sort_tlb_dump_info( tlb_dump_entry_t *t, int n )
{
    int i, j;
    /*  Simple O(n*n) sort:  */
    for (i = 0; i < n-1; i++)
	for (j = i+1; j < n; j++)
	    if (cmp_tlb_dump_info(t+i, t+j) > 0) {
		tlb_dump_entry_t tmp = t[i];
		t[i] = t[j];
		t[j] = tmp;
	    }
}


static tlb_dump_entry_t itlb_dump_info[ITLB_TOTAL_ENTRIES];
static tlb_dump_entry_t dtlb_dump_info[DTLB_TOTAL_ENTRIES];


static inline char *way_type (int type)
{
	return type ? "autorefill" : "non-autorefill";
}

void print_entry (struct way_config_t *way_info,
		  unsigned int way,
		  unsigned int index,
		  unsigned int virtual,
		  unsigned int translation)
{
	char valid_chr;
	unsigned int va, pa, asid, ca;

	va = virtual &
	  	~((1 << (way_info->pgsz_log2 + way_info->indicies_log2)) - 1);
	asid = virtual & ((1 << XCHAL_MMU_ASID_BITS) - 1);
	pa = translation & ~((1 << way_info->pgsz_log2) - 1);
	ca = translation & ((1 << XCHAL_MMU_CA_BITS) - 1);
	valid_chr = asid ? 'V' : 'I';

	/* Compute and incorporate the effect of the index bits on the
	 * va.  It's more useful for kernel debugging, since we always
	 * want to know the effective va anyway. */

	va += index << way_info->pgsz_log2;

	printk ("\t[%d,%d] (%c) vpn 0x%.8x  ppn 0x%.8x  asid 0x%.2x  am 0x%x\n",
		way, index, valid_chr, va, pa, asid, ca);
}

void print_itlb_entry (struct way_config_t *way_info, int way, int index)
{
	print_entry (way_info, way, index,
		     read_itlb_virtual (way + (index << way_info->pgsz_log2)),
		     read_itlb_translation (way + (index << way_info->pgsz_log2)));
}

void print_dtlb_entry (struct way_config_t *way_info, int way, int index)
{
	print_entry (way_info, way, index,
		     read_dtlb_virtual (way + (index << way_info->pgsz_log2)),
		     read_dtlb_translation (way + (index << way_info->pgsz_log2)));
}

void dump_itlb (void)
{
	int way, index;

	printk ("\nITLB: ways = %d\n", XCHAL_ITLB_WAYS);

	for (way = 0; way < XCHAL_ITLB_WAYS; way++) {
		printk ("\nWay: %d, Entries: %d, MinPageSize: %d, Type: %s\n",
			way, itlb[way].indicies,
			itlb[way].pgsz_log2, way_type(itlb[way].arf));
		for (index = 0; index < itlb[way].indicies; index++) {
			print_itlb_entry(&itlb[way], way, index);
		}
	}
}

void dump_dtlb (void)
{
	int way, index;

	printk ("\nDTLB: ways = %d\n", XCHAL_DTLB_WAYS);

	for (way = 0; way < XCHAL_DTLB_WAYS; way++) {
		printk ("\nWay: %d, Entries: %d, MinPageSize: %d, Type: %s\n",
			way, dtlb[way].indicies,
			dtlb[way].pgsz_log2, way_type(dtlb[way].arf));
		for (index = 0; index < dtlb[way].indicies; index++) {
			print_dtlb_entry(&dtlb[way], way, index);
		}
	}
}

void dump_tlb (tlb_dump_entry_t *tinfo, struct way_config_t *config,
		int entries, int ways, int type, int show_invalid)
{
    tlb_dump_entry_t *e = tinfo;
    int way, i;

    /*  Gather all info:  */
    for (way = 0; way < ways; way++) {
	struct way_config_t *cfg = config + way;
	for (i = 0; i < cfg->indicies; i++) {
	    unsigned wayindex = way + (i << cfg->pgsz_log2);
	    unsigned vv = (type ? read_dtlb_virtual (wayindex)
		    		: read_itlb_virtual (wayindex));
	    unsigned pp = (type ? read_dtlb_translation (wayindex)
		    		: read_itlb_translation (wayindex));

	    /* Compute and incorporate the effect of the index bits on the
	     * va.  It's more useful for kernel debugging, since we always
	     * want to know the effective va anyway. */

	    e->va = (vv & ~((1 << (cfg->pgsz_log2 + cfg->indicies_log2)) - 1));
	    e->va += (i << cfg->pgsz_log2);
	    e->pa = (pp & ~((1 << cfg->pgsz_log2) - 1));
	    e->asid = (vv & ((1 << XCHAL_MMU_ASID_BITS) - 1));
	    e->ca = (pp & ((1 << XCHAL_MMU_CA_BITS) - 1));
	    e->way = way;
	    e->index = i;
	    e->pgsz_log2 = cfg->pgsz_log2;
	    e->type = type;
	    e++;
	}
    }
#if 1
    /*  Sort by ASID and VADDR:  */
    sort_tlb_dump_info (tinfo, entries);
#endif

    /*  Display all sorted info:  */
    printk ("\n%cTLB dump:\n", (type ? 'D' : 'I'));
    for (e = tinfo, i = 0; i < entries; i++, e++) {
#if 0
	if (e->asid == 0 && !show_invalid)
	    continue;
#endif
	printk ("%c way=%d i=%d  ASID=%02X V=%08X -> P=%08X CA=%X (%d %cB)\n",
		(e->type ? 'D' : 'I'), e->way, e->index,
		e->asid, e->va, e->pa, e->ca,
		(1 << (e->pgsz_log2 % 10)),
		" kMG"[e->pgsz_log2 / 10]
		);
    }
}

void dump_tlbs2 (int showinv)
{
    dump_tlb (itlb_dump_info, itlb, ITLB_TOTAL_ENTRIES, XCHAL_ITLB_WAYS, 0, showinv);
    dump_tlb (dtlb_dump_info, dtlb, DTLB_TOTAL_ENTRIES, XCHAL_DTLB_WAYS, 1, showinv);
}

void dump_all_tlbs (void)
{
    dump_tlbs2 (1);
}

void dump_valid_tlbs (void)
{
    dump_tlbs2 (0);
}


void dump_tlbs (void)
{
	dump_itlb();
	dump_dtlb();
}

void dump_cache_tag(int dcache, int idx)
{
	int w, i, s, e;
	unsigned long tag, index;
	unsigned long num_lines, num_ways, cache_size, line_size;

	num_ways = dcache ? XCHAL_DCACHE_WAYS : XCHAL_ICACHE_WAYS;
	cache_size = dcache ? XCHAL_DCACHE_SIZE : XCHAL_ICACHE_SIZE;
	line_size = dcache ? XCHAL_DCACHE_LINESIZE : XCHAL_ICACHE_LINESIZE;

	num_lines = cache_size / num_ways;

	s = 0; e = num_lines;

	if (idx >= 0)
		e = (s = idx * line_size) + 1;

	for (i = s; i < e; i+= line_size) {
		printk("\nline %#08x:", i);
		for (w = 0; w < num_ways; w++) {
			index = w * num_lines + i;
			if (dcache)
				__asm__ __volatile__("ldct %0, %1\n\t"
						: "=a"(tag) : "a"(index));
			else
				__asm__ __volatile__("lict %0, %1\n\t"
						: "=a"(tag) : "a"(index));

			printk(" %#010lx", tag);
		}
	}
	printk ("\n");
}

void dump_icache(int index)
{
	unsigned long data, addr;
	int w, i;

	const unsigned long num_ways = XCHAL_ICACHE_WAYS;
	const unsigned long cache_size = XCHAL_ICACHE_SIZE;
	const unsigned long line_size = XCHAL_ICACHE_LINESIZE;
	const unsigned long num_lines = cache_size / num_ways / line_size;

	for (w = 0; w < num_ways; w++) {
		printk ("\nWay %d", w);

		for (i = 0; i < line_size; i+= 4) {
			addr = w * num_lines + index * line_size + i;
			__asm__ __volatile__("licw %0, %1\n\t"
					: "=a"(data) : "a"(addr));
			printk(" %#010lx", data);
		}
	}
	printk ("\n");
}

void dump_cache_tags(void)
{
	printk("Instruction cache\n");
	dump_cache_tag(0, -1);
	printk("Data cache\n");
	dump_cache_tag(1, -1);
}

#endif
