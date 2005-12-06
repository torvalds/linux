Memory management for CRIS/MMU
------------------------------
HISTORY:

$Log: README.mm,v $
Revision 1.1  2001/12/17 13:59:27  bjornw
Initial revision

Revision 1.1  2000/07/10 16:25:21  bjornw
Initial revision

Revision 1.4  2000/01/17 02:31:59  bjornw
Added discussion of paging and VM.

Revision 1.3  1999/12/03 16:43:23  hp
Blurb about that the 3.5G-limitation is not a MMU limitation

Revision 1.2  1999/12/03 16:04:21  hp
Picky comment about not mapping the first page

Revision 1.1  1999/12/03 15:41:30  bjornw
First version of CRIS/MMU memory layout specification.





------------------------------

See the ETRAX-NG HSDD for reference.

We use the page-size of 8 kbytes, as opposed to the i386 page-size of 4 kbytes.

The MMU can, apart from the normal mapping of pages, also do a top-level
segmentation of the kernel memory space. We use this feature to avoid having
to use page-tables to map the physical memory into the kernel's address
space. We also use it to keep the user-mode virtual mapping in the same
map during kernel-mode, so that the kernel easily can access the corresponding
user-mode process' data.

As a comparision, the Linux/i386 2.0 puts the kernel and physical RAM at
address 0, overlapping with the user-mode virtual space, so that descriptor
registers are needed for each memory access to specify which MMU space to
map through. That changed in 2.2, putting the kernel/physical RAM at 
0xc0000000, to co-exist with the user-mode mapping. We will do something
quite similar, but with the additional complexity of having to map the
internal chip I/O registers and the flash memory area (including SRAM
and peripherial chip-selets).

The kernel-mode segmentation map:

        ------------------------                ------------------------
FFFFFFFF|                      | => cached      |                      | 
        |    kernel seg_f      |    flash       |                      |
F0000000|______________________|                |                      |
EFFFFFFF|                      | => uncached    |                      | 
        |    kernel seg_e      |    flash       |                      |
E0000000|______________________|                |        DRAM          |
DFFFFFFF|                      |  paged to any  |      Un-cached       | 
        |    kernel seg_d      |    =======>    |                      |
D0000000|______________________|                |                      |
CFFFFFFF|                      |                |                      | 
        |    kernel seg_c      |==\             |                      |
C0000000|______________________|   \            |______________________|
BFFFFFFF|                      |  uncached      |                      |
        |    kernel seg_b      |=====\=========>|       Registers      |
B0000000|______________________|      \c        |______________________|
AFFFFFFF|                      |       \a       |                      |
        |                      |        \c      | FLASH/SRAM/Peripheral|
        |                      |         \h     |______________________|
        |                      |          \e    |                      |
        |                      |           \d   |                      |
        | kernel seg_0 - seg_a |            \==>|         DRAM         | 
        |                      |                |        Cached        |
        |                      |  paged to any  |                      |
        |                      |    =======>    |______________________| 
        |                      |                |                      |
        |                      |                |        Illegal       |
        |                      |                |______________________|
        |                      |                |                      |      
        |                      |                | FLASH/SRAM/Peripheral|
00000000|______________________|                |______________________|

In user-mode it looks the same except that only the space 0-AFFFFFFF is
available. Therefore, in this model, the virtual address space per process
is limited to 0xb0000000 bytes (minus 8192 bytes, since the first page,
0..8191, is never mapped, in order to trap NULL references).

It also means that the total physical RAM that can be mapped is 256 MB
(kseg_c above). More RAM can be mapped by choosing a different segmentation
and shrinking the user-mode memory space.

The MMU can map all 4 GB in user mode, but doing that would mean that a
few extra instructions would be needed for each access to user mode
memory.

The kernel needs access to both cached and uncached flash. Uncached is
necessary because of the special write/erase sequences. Also, the 
peripherial chip-selects are decoded from that region.

The kernel also needs its own virtual memory space. That is kseg_d. It
is used by the vmalloc() kernel function to allocate virtual contiguous
chunks of memory not possible using the normal kmalloc physical RAM 
allocator.

The setting of the actual MMU control registers to use this layout would
be something like this:

R_MMU_KSEG = ( ( seg_f, seg     ) |   // Flash cached
               ( seg_e, seg     ) |   // Flash uncached
               ( seg_d, page    ) |   // kernel vmalloc area    
               ( seg_c, seg     ) |   // kernel linear segment
               ( seg_b, seg     ) |   // kernel linear segment
               ( seg_a, page    ) |
               ( seg_9, page    ) |
               ( seg_8, page    ) |
               ( seg_7, page    ) |
               ( seg_6, page    ) |
               ( seg_5, page    ) |
               ( seg_4, page    ) |
               ( seg_3, page    ) |
               ( seg_2, page    ) |
               ( seg_1, page    ) |
               ( seg_0, page    ) );

R_MMU_KBASE_HI = ( ( base_f, 0x0 ) |   // flash/sram/periph cached
                   ( base_e, 0x8 ) |   // flash/sram/periph uncached
                   ( base_d, 0x0 ) |   // don't care
                   ( base_c, 0x4 ) |   // physical RAM cached area
                   ( base_b, 0xb ) |   // uncached on-chip registers
                   ( base_a, 0x0 ) |   // don't care
                   ( base_9, 0x0 ) |   // don't care
                   ( base_8, 0x0 ) );  // don't care

R_MMU_KBASE_LO = ( ( base_7, 0x0 ) |   // don't care
                   ( base_6, 0x0 ) |   // don't care
                   ( base_5, 0x0 ) |   // don't care
                   ( base_4, 0x0 ) |   // don't care
                   ( base_3, 0x0 ) |   // don't care
                   ( base_2, 0x0 ) |   // don't care
                   ( base_1, 0x0 ) |   // don't care
                   ( base_0, 0x0 ) );  // don't care

NOTE: while setting up the MMU, we run in a non-mapped mode in the DRAM (0x40
segment) and need to setup the seg_4 to a unity mapping, so that we don't get
a fault before we have had time to jump into the real kernel segment (0xc0). This
is done in head.S temporarily, but fixed by the kernel later in paging_init.


Paging - PTE's, PMD's and PGD's
-------------------------------

[ References: asm/pgtable.h, asm/page.h, asm/mmu.h ]

The paging mechanism uses virtual addresses to split a process memory-space into
pages, a page being the smallest unit that can be freely remapped in memory. On
Linux/CRIS, a page is 8192 bytes (for technical reasons not equal to 4096 as in 
most other 32-bit architectures). It would be inefficient to let a virtual memory
mapping be controlled by a long table of page mappings, so it is broken down into
a 2-level structure with a Page Directory containing pointers to Page Tables which
each have maps of up to 2048 pages (8192 / sizeof(void *)). Linux can actually
handle 3-level structures as well, with a Page Middle Directory in between, but
in many cases, this is folded into a two-level structure by excluding the Middle
Directory.

We'll take a look at how an address is translated while we discuss how it's handled
in the Linux kernel.

The example address is 0xd004000c; in binary this is:

31       23       15       7      0
11010000 00000100 00000000 00001100

|______| |__________||____________|
  PGD        PTE       page offset

Given the top-level Page Directory, the offset in that directory is calculated
using the upper 8 bits:

static inline pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> PGDIR_SHIFT);
}

PGDIR_SHIFT is the log2 of the amount of memory an entry in the PGD can map; in our
case it is 24, corresponding to 16 MB. This means that each entry in the PGD 
corresponds to 16 MB of virtual memory.

The pgd_t from our example will therefore be the 208'th (0xd0) entry in mm->pgd.

Since the Middle Directory does not exist, it is a unity mapping:

static inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

The Page Table provides the final lookup by using bits 13 to 23 as index:

static inline pte_t * pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) &
					   (PTRS_PER_PTE - 1));
}

PAGE_SHIFT is the log2 of the size of a page; 13 in our case. PTRS_PER_PTE is
the number of pointers that fit in a Page Table and is used to mask off the 
PGD-part of the address.

The so-far unused bits 0 to 12 are used to index inside a page linearily.

The VM system
-------------

The kernels own page-directory is the swapper_pg_dir, cleared in paging_init, 
and contains the kernels virtual mappings (the kernel itself is not paged - it
is mapped linearily using kseg_c as described above). Architectures without
kernel segments like the i386, need to setup swapper_pg_dir directly in head.S
to map the kernel itself. swapper_pg_dir is pointed to by init_mm.pgd as the
init-task's PGD.

To see what support functions are used to setup a page-table, let's look at the
kernel's internal paged memory system, vmalloc/vfree.

void * vmalloc(unsigned long size)

The vmalloc-system keeps a paged segment in kernel-space at 0xd0000000. What
happens first is that a virtual address chunk is allocated to the request using
get_vm_area(size). After that, physical RAM pages are allocated and put into
the kernel's page-table using alloc_area_pages(addr, size). 

static int alloc_area_pages(unsigned long address, unsigned long size)

First the PGD entry is found using init_mm.pgd. This is passed to
alloc_area_pmd (remember the 3->2 folding). It uses pte_alloc_kernel to
check if the PGD entry points anywhere - if not, a page table page is
allocated and the PGD entry updated. Then the alloc_area_pte function is
used just like alloc_area_pmd to check which page table entry is desired, 
and a physical page is allocated and the table entry updated. All of this
is repeated at the top-level until the entire address range specified has 
been mapped.



