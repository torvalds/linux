/*
** Tablewalk MMU emulator
**
** by Toshiyasu Morita
**
** Started 1/16/98 @ 2:22 am
*/

#include <linux/init.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/bitops.h>
#include <linux/module.h>

#include <asm/setup.h>
#include <asm/traps.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sun3mmu.h>
#include <asm/segment.h>
#include <asm/oplib.h>
#include <asm/mmu_context.h>
#include <asm/dvma.h>


#undef DEBUG_MMU_EMU
#define DEBUG_PROM_MAPS

/*
** Defines
*/

#define CONTEXTS_NUM		8
#define SEGMAPS_PER_CONTEXT_NUM 2048
#define PAGES_PER_SEGMENT	16
#define PMEGS_NUM		256
#define PMEG_MASK		0xFF

/*
** Globals
*/

unsigned long m68k_vmalloc_end;
EXPORT_SYMBOL(m68k_vmalloc_end);

unsigned long pmeg_vaddr[PMEGS_NUM];
unsigned char pmeg_alloc[PMEGS_NUM];
unsigned char pmeg_ctx[PMEGS_NUM];

/* pointers to the mm structs for each task in each
   context. 0xffffffff is a marker for kernel context */
static struct mm_struct *ctx_alloc[CONTEXTS_NUM] = {
    [0] = (struct mm_struct *)0xffffffff
};

/* has this context been mmdrop'd? */
static unsigned char ctx_avail = CONTEXTS_NUM-1;

/* array of pages to be marked off for the rom when we do mem_init later */
/* 256 pages lets the rom take up to 2mb of physical ram..  I really
   hope it never wants mote than that. */
unsigned long rom_pages[256];

/* Print a PTE value in symbolic form. For debugging. */
void print_pte (pte_t pte)
{
#if 0
	/* Verbose version. */
	unsigned long val = pte_val (pte);
	pr_cont(" pte=%lx [addr=%lx",
		val, (val & SUN3_PAGE_PGNUM_MASK) << PAGE_SHIFT);
	if (val & SUN3_PAGE_VALID)	pr_cont(" valid");
	if (val & SUN3_PAGE_WRITEABLE)	pr_cont(" write");
	if (val & SUN3_PAGE_SYSTEM)	pr_cont(" sys");
	if (val & SUN3_PAGE_NOCACHE)	pr_cont(" nocache");
	if (val & SUN3_PAGE_ACCESSED)	pr_cont(" accessed");
	if (val & SUN3_PAGE_MODIFIED)	pr_cont(" modified");
	switch (val & SUN3_PAGE_TYPE_MASK) {
		case SUN3_PAGE_TYPE_MEMORY: pr_cont(" memory"); break;
		case SUN3_PAGE_TYPE_IO:     pr_cont(" io");     break;
		case SUN3_PAGE_TYPE_VME16:  pr_cont(" vme16");  break;
		case SUN3_PAGE_TYPE_VME32:  pr_cont(" vme32");  break;
	}
	pr_cont("]\n");
#else
	/* Terse version. More likely to fit on a line. */
	unsigned long val = pte_val (pte);
	char flags[7], *type;

	flags[0] = (val & SUN3_PAGE_VALID)     ? 'v' : '-';
	flags[1] = (val & SUN3_PAGE_WRITEABLE) ? 'w' : '-';
	flags[2] = (val & SUN3_PAGE_SYSTEM)    ? 's' : '-';
	flags[3] = (val & SUN3_PAGE_NOCACHE)   ? 'x' : '-';
	flags[4] = (val & SUN3_PAGE_ACCESSED)  ? 'a' : '-';
	flags[5] = (val & SUN3_PAGE_MODIFIED)  ? 'm' : '-';
	flags[6] = '\0';

	switch (val & SUN3_PAGE_TYPE_MASK) {
		case SUN3_PAGE_TYPE_MEMORY: type = "memory"; break;
		case SUN3_PAGE_TYPE_IO:     type = "io"    ; break;
		case SUN3_PAGE_TYPE_VME16:  type = "vme16" ; break;
		case SUN3_PAGE_TYPE_VME32:  type = "vme32" ; break;
		default: type = "unknown?"; break;
	}

	pr_cont(" pte=%08lx [%07lx %s %s]\n",
		val, (val & SUN3_PAGE_PGNUM_MASK) << PAGE_SHIFT, flags, type);
#endif
}

/* Print the PTE value for a given virtual address. For debugging. */
void print_pte_vaddr (unsigned long vaddr)
{
	pr_cont(" vaddr=%lx [%02lx]", vaddr, sun3_get_segmap (vaddr));
	print_pte (__pte (sun3_get_pte (vaddr)));
}

/*
 * Initialise the MMU emulator.
 */
void __init mmu_emu_init(unsigned long bootmem_end)
{
	unsigned long seg, num;
	int i,j;

	memset(rom_pages, 0, sizeof(rom_pages));
	memset(pmeg_vaddr, 0, sizeof(pmeg_vaddr));
	memset(pmeg_alloc, 0, sizeof(pmeg_alloc));
	memset(pmeg_ctx, 0, sizeof(pmeg_ctx));

	/* pmeg align the end of bootmem, adding another pmeg,
	 * later bootmem allocations will likely need it */
	bootmem_end = (bootmem_end + (2 * SUN3_PMEG_SIZE)) & ~SUN3_PMEG_MASK;

	/* mark all of the pmegs used thus far as reserved */
	for (i=0; i < __pa(bootmem_end) / SUN3_PMEG_SIZE ; ++i)
		pmeg_alloc[i] = 2;


	/* I'm thinking that most of the top pmeg's are going to be
	   used for something, and we probably shouldn't risk it */
	for(num = 0xf0; num <= 0xff; num++)
		pmeg_alloc[num] = 2;

	/* liberate all existing mappings in the rest of kernel space */
	for(seg = bootmem_end; seg < 0x0f800000; seg += SUN3_PMEG_SIZE) {
		i = sun3_get_segmap(seg);

		if(!pmeg_alloc[i]) {
#ifdef DEBUG_MMU_EMU
			pr_info("freed:");
			print_pte_vaddr (seg);
#endif
			sun3_put_segmap(seg, SUN3_INVALID_PMEG);
		}
	}

	j = 0;
	for (num=0, seg=0x0F800000; seg<0x10000000; seg+=16*PAGE_SIZE) {
		if (sun3_get_segmap (seg) != SUN3_INVALID_PMEG) {
#ifdef DEBUG_PROM_MAPS
			for(i = 0; i < 16; i++) {
				pr_info("mapped:");
				print_pte_vaddr (seg + (i*PAGE_SIZE));
				break;
			}
#endif
			// the lowest mapping here is the end of our
			// vmalloc region
			if (!m68k_vmalloc_end)
				m68k_vmalloc_end = seg;

			// mark the segmap alloc'd, and reserve any
			// of the first 0xbff pages the hardware is
			// already using...  does any sun3 support > 24mb?
			pmeg_alloc[sun3_get_segmap(seg)] = 2;
		}
	}

	dvma_init();


	/* blank everything below the kernel, and we've got the base
	   mapping to start all the contexts off with... */
	for(seg = 0; seg < PAGE_OFFSET; seg += SUN3_PMEG_SIZE)
		sun3_put_segmap(seg, SUN3_INVALID_PMEG);

	set_fs(MAKE_MM_SEG(3));
	for(seg = 0; seg < 0x10000000; seg += SUN3_PMEG_SIZE) {
		i = sun3_get_segmap(seg);
		for(j = 1; j < CONTEXTS_NUM; j++)
			(*(romvec->pv_setctxt))(j, (void *)seg, i);
	}
	set_fs(KERNEL_DS);

}

/* erase the mappings for a dead context.  Uses the pg_dir for hints
   as the pmeg tables proved somewhat unreliable, and unmapping all of
   TASK_SIZE was much slower and no more stable. */
/* todo: find a better way to keep track of the pmegs used by a
   context for when they're cleared */
void clear_context(unsigned long context)
{
     unsigned char oldctx;
     unsigned long i;

     if(context) {
	     if(!ctx_alloc[context])
		     panic("clear_context: context not allocated\n");

	     ctx_alloc[context]->context = SUN3_INVALID_CONTEXT;
	     ctx_alloc[context] = (struct mm_struct *)0;
	     ctx_avail++;
     }

     oldctx = sun3_get_context();

     sun3_put_context(context);

     for(i = 0; i < SUN3_INVALID_PMEG; i++) {
	     if((pmeg_ctx[i] == context) && (pmeg_alloc[i] == 1)) {
		     sun3_put_segmap(pmeg_vaddr[i], SUN3_INVALID_PMEG);
		     pmeg_ctx[i] = 0;
		     pmeg_alloc[i] = 0;
		     pmeg_vaddr[i] = 0;
	     }
     }

     sun3_put_context(oldctx);
}

/* gets an empty context.  if full, kills the next context listed to
   die first */
/* This context invalidation scheme is, well, totally arbitrary, I'm
   sure it could be much more intelligent...  but it gets the job done
   for now without much overhead in making it's decision. */
/* todo: come up with optimized scheme for flushing contexts */
unsigned long get_free_context(struct mm_struct *mm)
{
	unsigned long new = 1;
	static unsigned char next_to_die = 1;

	if(!ctx_avail) {
		/* kill someone to get our context */
		new = next_to_die;
		clear_context(new);
		next_to_die = (next_to_die + 1) & 0x7;
		if(!next_to_die)
			next_to_die++;
	} else {
		while(new < CONTEXTS_NUM) {
			if(ctx_alloc[new])
				new++;
			else
				break;
		}
		// check to make sure one was really free...
		if(new == CONTEXTS_NUM)
			panic("get_free_context: failed to find free context");
	}

	ctx_alloc[new] = mm;
	ctx_avail--;

	return new;
}

/*
 * Dynamically select a `spare' PMEG and use it to map virtual `vaddr' in
 * `context'. Maintain internal PMEG management structures. This doesn't
 * actually map the physical address, but does clear the old mappings.
 */
//todo: better allocation scheme? but is extra complexity worthwhile?
//todo: only clear old entries if necessary? how to tell?

inline void mmu_emu_map_pmeg (int context, int vaddr)
{
	static unsigned char curr_pmeg = 128;
	int i;

	/* Round address to PMEG boundary. */
	vaddr &= ~SUN3_PMEG_MASK;

	/* Find a spare one. */
	while (pmeg_alloc[curr_pmeg] == 2)
		++curr_pmeg;


#ifdef DEBUG_MMU_EMU
	pr_info("mmu_emu_map_pmeg: pmeg %x to context %d vaddr %x\n",
		curr_pmeg, context, vaddr);
#endif

	/* Invalidate old mapping for the pmeg, if any */
	if (pmeg_alloc[curr_pmeg] == 1) {
		sun3_put_context(pmeg_ctx[curr_pmeg]);
		sun3_put_segmap (pmeg_vaddr[curr_pmeg], SUN3_INVALID_PMEG);
		sun3_put_context(context);
	}

	/* Update PMEG management structures. */
	// don't take pmeg's away from the kernel...
	if(vaddr >= PAGE_OFFSET) {
		/* map kernel pmegs into all contexts */
		unsigned char i;

		for(i = 0; i < CONTEXTS_NUM; i++) {
			sun3_put_context(i);
			sun3_put_segmap (vaddr, curr_pmeg);
		}
		sun3_put_context(context);
		pmeg_alloc[curr_pmeg] = 2;
		pmeg_ctx[curr_pmeg] = 0;

	}
	else {
		pmeg_alloc[curr_pmeg] = 1;
		pmeg_ctx[curr_pmeg] = context;
		sun3_put_segmap (vaddr, curr_pmeg);

	}
	pmeg_vaddr[curr_pmeg] = vaddr;

	/* Set hardware mapping and clear the old PTE entries. */
	for (i=0; i<SUN3_PMEG_SIZE; i+=SUN3_PTE_SIZE)
		sun3_put_pte (vaddr + i, SUN3_PAGE_SYSTEM);

	/* Consider a different one next time. */
	++curr_pmeg;
}

/*
 * Handle a pagefault at virtual address `vaddr'; check if there should be a
 * page there (specifically, whether the software pagetables indicate that
 * there is). This is necessary due to the limited size of the second-level
 * Sun3 hardware pagetables (256 groups of 16 pages). If there should be a
 * mapping present, we select a `spare' PMEG and use it to create a mapping.
 * `read_flag' is nonzero for a read fault; zero for a write. Returns nonzero
 * if we successfully handled the fault.
 */
//todo: should we bump minor pagefault counter? if so, here or in caller?
//todo: possibly inline this into bus_error030 in <asm/buserror.h> ?

// kernel_fault is set when a kernel page couldn't be demand mapped,
// and forces another try using the kernel page table.  basically a
// hack so that vmalloc would work correctly.

int mmu_emu_handle_fault (unsigned long vaddr, int read_flag, int kernel_fault)
{
	unsigned long segment, offset;
	unsigned char context;
	pte_t *pte;
	pgd_t * crp;

	if(current->mm == NULL) {
		crp = swapper_pg_dir;
		context = 0;
	} else {
		context = current->mm->context;
		if(kernel_fault)
			crp = swapper_pg_dir;
		else
			crp = current->mm->pgd;
	}

#ifdef DEBUG_MMU_EMU
	pr_info("mmu_emu_handle_fault: vaddr=%lx type=%s crp=%p\n",
		vaddr, read_flag ? "read" : "write", crp);
#endif

	segment = (vaddr >> SUN3_PMEG_SIZE_BITS) & 0x7FF;
	offset  = (vaddr >> SUN3_PTE_SIZE_BITS) & 0xF;

#ifdef DEBUG_MMU_EMU
	pr_info("mmu_emu_handle_fault: segment=%lx offset=%lx\n", segment,
		offset);
#endif

	pte = (pte_t *) pgd_val (*(crp + segment));

//todo: next line should check for valid pmd properly.
	if (!pte) {
//                pr_info("mmu_emu_handle_fault: invalid pmd\n");
                return 0;
        }

	pte = (pte_t *) __va ((unsigned long)(pte + offset));

	/* Make sure this is a valid page */
	if (!(pte_val (*pte) & SUN3_PAGE_VALID))
		return 0;

	/* Make sure there's a pmeg allocated for the page */
	if (sun3_get_segmap (vaddr&~SUN3_PMEG_MASK) == SUN3_INVALID_PMEG)
		mmu_emu_map_pmeg (context, vaddr);

	/* Write the pte value to hardware MMU */
	sun3_put_pte (vaddr&PAGE_MASK, pte_val (*pte));

	/* Update software copy of the pte value */
// I'm not sure this is necessary. If this is required, we ought to simply
// copy this out when we reuse the PMEG or at some other convenient time.
// Doing it here is fairly meaningless, anyway, as we only know about the
// first access to a given page. --m
	if (!read_flag) {
		if (pte_val (*pte) & SUN3_PAGE_WRITEABLE)
			pte_val (*pte) |= (SUN3_PAGE_ACCESSED
					   | SUN3_PAGE_MODIFIED);
		else
			return 0;	/* Write-protect error. */
	} else
		pte_val (*pte) |= SUN3_PAGE_ACCESSED;

#ifdef DEBUG_MMU_EMU
	pr_info("seg:%ld crp:%p ->", get_fs().seg, crp);
	print_pte_vaddr (vaddr);
	pr_cont("\n");
#endif

	return 1;
}
