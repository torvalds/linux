/*
 *  linux/arch/cris/arch-v10/mm/init.c
 *
 */
#include <linux/mmzone.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/mmu.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/arch/svinto.h>

extern void tlb_init(void);

/*
 * The kernel is already mapped with a kernel segment at kseg_c so 
 * we don't need to map it with a page table. However head.S also
 * temporarily mapped it at kseg_4 so we should set up the ksegs again,
 * clear the TLB and do some other paging setup stuff.
 */

void __init 
paging_init(void)
{
	int i;
	unsigned long zones_size[MAX_NR_ZONES];

	printk("Setting up paging and the MMU.\n");
	
	/* clear out the init_mm.pgd that will contain the kernel's mappings */

	for(i = 0; i < PTRS_PER_PGD; i++)
		swapper_pg_dir[i] = __pgd(0);
	
	/* make sure the current pgd table points to something sane
	 * (even if it is most probably not used until the next 
	 *  switch_mm)
	 */

	per_cpu(current_pgd, smp_processor_id()) = init_mm.pgd;

	/* initialise the TLB (tlb.c) */

	tlb_init();

	/* see README.mm for details on the KSEG setup */

#ifdef CONFIG_CRIS_LOW_MAP
	/* Etrax-100 LX version 1 has a bug so that we cannot map anything
	 * across the 0x80000000 boundary, so we need to shrink the user-virtual
	 * area to 0x50000000 instead of 0xb0000000 and map things slightly
	 * different. The unused areas are marked as paged so that we can catch
	 * freak kernel accesses there.
	 *
	 * The ARTPEC chip is mapped at 0xa so we pass that segment straight
	 * through. We cannot vremap it because the vmalloc area is below 0x8
	 * and Juliette needs an uncached area above 0x8.
	 *
	 * Same thing with 0xc and 0x9, which is memory-mapped I/O on some boards.
	 * We map them straight over in LOW_MAP, but use vremap in LX version 2.
	 */

#define CACHED_BOOTROM (KSEG_F | 0x08000000UL)

	*R_MMU_KSEG = ( IO_STATE(R_MMU_KSEG, seg_f, seg  ) |  /* bootrom */
			IO_STATE(R_MMU_KSEG, seg_e, page ) |
			IO_STATE(R_MMU_KSEG, seg_d, page ) | 
			IO_STATE(R_MMU_KSEG, seg_c, page ) |   
			IO_STATE(R_MMU_KSEG, seg_b, seg  ) |  /* kernel reg area */
#ifdef CONFIG_JULIETTE
			IO_STATE(R_MMU_KSEG, seg_a, seg  ) |  /* ARTPEC etc. */
#else
			IO_STATE(R_MMU_KSEG, seg_a, page ) |
#endif
			IO_STATE(R_MMU_KSEG, seg_9, seg  ) |  /* LED's on some boards */
			IO_STATE(R_MMU_KSEG, seg_8, seg  ) |  /* CSE0/1, flash and I/O */
			IO_STATE(R_MMU_KSEG, seg_7, page ) |  /* kernel vmalloc area */
			IO_STATE(R_MMU_KSEG, seg_6, seg  ) |  /* kernel DRAM area */
			IO_STATE(R_MMU_KSEG, seg_5, seg  ) |  /* cached flash */
			IO_STATE(R_MMU_KSEG, seg_4, page ) |  /* user area */
			IO_STATE(R_MMU_KSEG, seg_3, page ) |  /* user area */
			IO_STATE(R_MMU_KSEG, seg_2, page ) |  /* user area */
			IO_STATE(R_MMU_KSEG, seg_1, page ) |  /* user area */
			IO_STATE(R_MMU_KSEG, seg_0, page ) ); /* user area */

	*R_MMU_KBASE_HI = ( IO_FIELD(R_MMU_KBASE_HI, base_f, 0x3 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_e, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_d, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_c, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_b, 0xb ) |
#ifdef CONFIG_JULIETTE
			    IO_FIELD(R_MMU_KBASE_HI, base_a, 0xa ) |
#else
			    IO_FIELD(R_MMU_KBASE_HI, base_a, 0x0 ) |
#endif
			    IO_FIELD(R_MMU_KBASE_HI, base_9, 0x9 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_8, 0x8 ) );
	
	*R_MMU_KBASE_LO = ( IO_FIELD(R_MMU_KBASE_LO, base_7, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_6, 0x4 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_5, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_4, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_3, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_2, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_1, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_0, 0x0 ) );
#else
	/* This code is for the corrected Etrax-100 LX version 2... */

#define CACHED_BOOTROM (KSEG_A | 0x08000000UL)

	*R_MMU_KSEG = ( IO_STATE(R_MMU_KSEG, seg_f, seg  ) | /* cached flash */
			IO_STATE(R_MMU_KSEG, seg_e, seg  ) | /* uncached flash */
			IO_STATE(R_MMU_KSEG, seg_d, page ) | /* vmalloc area */
			IO_STATE(R_MMU_KSEG, seg_c, seg  ) | /* kernel area */
			IO_STATE(R_MMU_KSEG, seg_b, seg  ) | /* kernel reg area */
			IO_STATE(R_MMU_KSEG, seg_a, seg  ) | /* bootrom */
			IO_STATE(R_MMU_KSEG, seg_9, page ) | /* user area */
			IO_STATE(R_MMU_KSEG, seg_8, page ) |
			IO_STATE(R_MMU_KSEG, seg_7, page ) |
			IO_STATE(R_MMU_KSEG, seg_6, page ) |
			IO_STATE(R_MMU_KSEG, seg_5, page ) |
			IO_STATE(R_MMU_KSEG, seg_4, page ) |
			IO_STATE(R_MMU_KSEG, seg_3, page ) |
			IO_STATE(R_MMU_KSEG, seg_2, page ) |
			IO_STATE(R_MMU_KSEG, seg_1, page ) |
			IO_STATE(R_MMU_KSEG, seg_0, page ) );

	*R_MMU_KBASE_HI = ( IO_FIELD(R_MMU_KBASE_HI, base_f, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_e, 0x8 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_d, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_c, 0x4 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_b, 0xb ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_a, 0x3 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_9, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_HI, base_8, 0x0 ) );
	
	*R_MMU_KBASE_LO = ( IO_FIELD(R_MMU_KBASE_LO, base_7, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_6, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_5, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_4, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_3, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_2, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_1, 0x0 ) |
			    IO_FIELD(R_MMU_KBASE_LO, base_0, 0x0 ) );
#endif

	*R_MMU_CONTEXT = ( IO_FIELD(R_MMU_CONTEXT, page_id, 0 ) );
	
	/* The MMU has been enabled ever since head.S but just to make
	 * it totally obvious we do it here as well.
	 */

	*R_MMU_CTRL = ( IO_STATE(R_MMU_CTRL, inv_excp, enable ) |
			IO_STATE(R_MMU_CTRL, acc_excp, enable ) |
			IO_STATE(R_MMU_CTRL, we_excp,  enable ) );
	
	*R_MMU_ENABLE = IO_STATE(R_MMU_ENABLE, mmu_enable, enable);

	/*
	 * initialize the bad page table and bad page to point
	 * to a couple of allocated pages
	 */

	empty_zero_page = (unsigned long)alloc_bootmem_pages(PAGE_SIZE);
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	/* All pages are DMA'able in Etrax, so put all in the DMA'able zone */

	zones_size[0] = ((unsigned long)high_memory - PAGE_OFFSET) >> PAGE_SHIFT;

	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

	/* Use free_area_init_node instead of free_area_init, because the former
	 * is designed for systems where the DRAM starts at an address substantially
	 * higher than 0, like us (we start at PAGE_OFFSET). This saves space in the
	 * mem_map page array.
	 */

	free_area_init_node(0, zones_size, PAGE_OFFSET >> PAGE_SHIFT, 0);
}

/* Initialize remaps of some I/O-ports. It is important that this
 * is called before any driver is initialized.
 */

static int
__init init_ioremap(void)
{
  
	/* Give the external I/O-port addresses their values */

#ifdef CONFIG_CRIS_LOW_MAP
	/* Simply a linear map (see the KSEG map above in paging_init) */
	port_cse1_addr = (volatile unsigned long *)(MEM_CSE1_START | 
	                                            MEM_NON_CACHEABLE);
	port_csp0_addr = (volatile unsigned long *)(MEM_CSP0_START |
	                                            MEM_NON_CACHEABLE);
	port_csp4_addr = (volatile unsigned long *)(MEM_CSP4_START |
	                                            MEM_NON_CACHEABLE);
#else
	/* Note that nothing blows up just because we do this remapping 
	 * it's ok even if the ports are not used or connected 
	 * to anything (or connected to a non-I/O thing) */        
	port_cse1_addr = (volatile unsigned long *)
	  ioremap((unsigned long)(MEM_CSE1_START | MEM_NON_CACHEABLE), 16);
	port_csp0_addr = (volatile unsigned long *)
	  ioremap((unsigned long)(MEM_CSP0_START | MEM_NON_CACHEABLE), 16);
	port_csp4_addr = (volatile unsigned long *)
	  ioremap((unsigned long)(MEM_CSP4_START | MEM_NON_CACHEABLE), 16);
#endif	
	return 0;
}

__initcall(init_ioremap);

/* Helper function for the two below */

static inline void
flush_etrax_cacherange(void *startadr, int length)
{
	/* CACHED_BOOTROM is mapped to the boot-rom area (cached) which
	 * we can use to get fast dummy-reads of cachelines
	 */

	volatile short *flushadr = (volatile short *)(((unsigned long)startadr & ~PAGE_MASK) |
						      CACHED_BOOTROM);

	length = length > 8192 ? 8192 : length;  /* No need to flush more than cache size */

	while(length > 0) {
		*flushadr; /* dummy read to flush */
		flushadr += (32/sizeof(short));  /* a cacheline is 32 bytes */
		length -= 32;
	}
}

/* Due to a bug in Etrax100(LX) all versions, receiving DMA buffers
 * will occationally corrupt certain CPU writes if the DMA buffers
 * happen to be hot in the cache.
 * 
 * As a workaround, we have to flush the relevant parts of the cache
 * before (re) inserting any receiving descriptor into the DMA HW.
 */

void
prepare_rx_descriptor(struct etrax_dma_descr *desc)
{
	flush_etrax_cacherange((void *)desc->buf, desc->sw_len ? desc->sw_len : 65536);
}

/* Do the same thing but flush the entire cache */

void
flush_etrax_cache(void)
{
	flush_etrax_cacherange(0, 8192);
}
