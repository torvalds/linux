/*
 * arch/m68k/atari/stram.c: Functions for ST-RAM allocations
 *
 * Copyright 1994-97 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/mount.h>
#include <linux/blkdev.h>

#include <asm/setup.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/atarihw.h>
#include <asm/atari_stram.h>
#include <asm/io.h>
#include <asm/semaphore.h>

#undef DEBUG

#ifdef DEBUG
#define	DPRINTK(fmt,args...) printk( fmt, ##args )
#else
#define DPRINTK(fmt,args...)
#endif

#if defined(CONFIG_PROC_FS) && defined(CONFIG_STRAM_PROC)
/* abbrev for the && above... */
#define DO_PROC
#include <linux/proc_fs.h>
#endif

/*
 * ++roman:
 *
 * New version of ST-Ram buffer allocation. Instead of using the
 * 1 MB - 4 KB that remain when the ST-Ram chunk starts at $1000
 * (1 MB granularity!), such buffers are reserved like this:
 *
 *  - If the kernel resides in ST-Ram anyway, we can take the buffer
 *    from behind the current kernel data space the normal way
 *    (incrementing start_mem).
 *
 *  - If the kernel is in TT-Ram, stram_init() initializes start and
 *    end of the available region. Buffers are allocated from there
 *    and mem_init() later marks the such used pages as reserved.
 *    Since each TT-Ram chunk is at least 4 MB in size, I hope there
 *    won't be an overrun of the ST-Ram region by normal kernel data
 *    space.
 *
 * For that, ST-Ram may only be allocated while kernel initialization
 * is going on, or exactly: before mem_init() is called. There is also
 * no provision now for freeing ST-Ram buffers. It seems that isn't
 * really needed.
 *
 */

/* Start and end (virtual) of ST-RAM */
static void *stram_start, *stram_end;

/* set after memory_init() executed and allocations via start_mem aren't
 * possible anymore */
static int mem_init_done;

/* set if kernel is in ST-RAM */
static int kernel_in_stram;

typedef struct stram_block {
	struct stram_block *next;
	void *start;
	unsigned long size;
	unsigned flags;
	const char *owner;
} BLOCK;

/* values for flags field */
#define BLOCK_FREE	0x01	/* free structure in the BLOCKs pool */
#define BLOCK_KMALLOCED	0x02	/* structure allocated by kmalloc() */
#define BLOCK_GFP	0x08	/* block allocated with __get_dma_pages() */

/* list of allocated blocks */
static BLOCK *alloc_list;

/* We can't always use kmalloc() to allocate BLOCK structures, since
 * stram_alloc() can be called rather early. So we need some pool of
 * statically allocated structures. 20 of them is more than enough, so in most
 * cases we never should need to call kmalloc(). */
#define N_STATIC_BLOCKS	20
static BLOCK static_blocks[N_STATIC_BLOCKS];

/***************************** Prototypes *****************************/

static BLOCK *add_region( void *addr, unsigned long size );
static BLOCK *find_region( void *addr );
static int remove_region( BLOCK *block );

/************************* End of Prototypes **************************/


/* ------------------------------------------------------------------------ */
/*							   Public Interface								*/
/* ------------------------------------------------------------------------ */

/*
 * This init function is called very early by atari/config.c
 * It initializes some internal variables needed for stram_alloc()
 */
void __init atari_stram_init(void)
{
	int i;

	/* initialize static blocks */
	for( i = 0; i < N_STATIC_BLOCKS; ++i )
		static_blocks[i].flags = BLOCK_FREE;

	/* determine whether kernel code resides in ST-RAM (then ST-RAM is the
	 * first memory block at virtual 0x0) */
	stram_start = phys_to_virt(0);
	kernel_in_stram = (stram_start == 0);

	for( i = 0; i < m68k_num_memory; ++i ) {
		if (m68k_memory[i].addr == 0) {
			/* skip first 2kB or page (supervisor-only!) */
			stram_end = stram_start + m68k_memory[i].size;
			return;
		}
	}
	/* Should never come here! (There is always ST-Ram!) */
	panic( "atari_stram_init: no ST-RAM found!" );
}


/*
 * This function is called from setup_arch() to reserve the pages needed for
 * ST-RAM management.
 */
void __init atari_stram_reserve_pages(void *start_mem)
{
	/* always reserve first page of ST-RAM, the first 2 kB are
	 * supervisor-only! */
	if (!kernel_in_stram)
		reserve_bootmem (0, PAGE_SIZE);

}

void atari_stram_mem_init_hook (void)
{
	mem_init_done = 1;
}


/*
 * This is main public interface: somehow allocate a ST-RAM block
 *
 *  - If we're before mem_init(), we have to make a static allocation. The
 *    region is taken in the kernel data area (if the kernel is in ST-RAM) or
 *    from the start of ST-RAM (if the kernel is in TT-RAM) and added to the
 *    rsvd_stram_* region. The ST-RAM is somewhere in the middle of kernel
 *    address space in the latter case.
 *
 *  - If mem_init() already has been called, try with __get_dma_pages().
 *    This has the disadvantage that it's very hard to get more than 1 page,
 *    and it is likely to fail :-(
 *
 */
void *atari_stram_alloc(long size, const char *owner)
{
	void *addr = NULL;
	BLOCK *block;
	int flags;

	DPRINTK("atari_stram_alloc(size=%08lx,owner=%s)\n", size, owner);

	if (!mem_init_done)
		return alloc_bootmem_low(size);
	else {
		/* After mem_init(): can only resort to __get_dma_pages() */
		addr = (void *)__get_dma_pages(GFP_KERNEL, get_order(size));
		flags = BLOCK_GFP;
		DPRINTK( "atari_stram_alloc: after mem_init, "
				 "get_pages=%p\n", addr );
	}

	if (addr) {
		if (!(block = add_region( addr, size ))) {
			/* out of memory for BLOCK structure :-( */
			DPRINTK( "atari_stram_alloc: out of mem for BLOCK -- "
					 "freeing again\n" );
			free_pages((unsigned long)addr, get_order(size));
			return( NULL );
		}
		block->owner = owner;
		block->flags |= flags;
	}
	return( addr );
}

void atari_stram_free( void *addr )

{
	BLOCK *block;

	DPRINTK( "atari_stram_free(addr=%p)\n", addr );

	if (!(block = find_region( addr ))) {
		printk( KERN_ERR "Attempt to free non-allocated ST-RAM block at %p "
				"from %p\n", addr, __builtin_return_address(0) );
		return;
	}
	DPRINTK( "atari_stram_free: found block (%p): size=%08lx, owner=%s, "
			 "flags=%02x\n", block, block->size, block->owner, block->flags );

	if (!(block->flags & BLOCK_GFP))
		goto fail;

	DPRINTK("atari_stram_free: is kmalloced, order_size=%d\n",
		get_order(block->size));
	free_pages((unsigned long)addr, get_order(block->size));
	remove_region( block );
	return;

  fail:
	printk( KERN_ERR "atari_stram_free: cannot free block at %p "
			"(called from %p)\n", addr, __builtin_return_address(0) );
}


/* ------------------------------------------------------------------------ */
/*							  Region Management								*/
/* ------------------------------------------------------------------------ */


/* insert a region into the alloced list (sorted) */
static BLOCK *add_region( void *addr, unsigned long size )
{
	BLOCK **p, *n = NULL;
	int i;

	for( i = 0; i < N_STATIC_BLOCKS; ++i ) {
		if (static_blocks[i].flags & BLOCK_FREE) {
			n = &static_blocks[i];
			n->flags = 0;
			break;
		}
	}
	if (!n && mem_init_done) {
		/* if statics block pool exhausted and we can call kmalloc() already
		 * (after mem_init()), try that */
		n = kmalloc( sizeof(BLOCK), GFP_KERNEL );
		if (n)
			n->flags = BLOCK_KMALLOCED;
	}
	if (!n) {
		printk( KERN_ERR "Out of memory for ST-RAM descriptor blocks\n" );
		return( NULL );
	}
	n->start = addr;
	n->size  = size;

	for( p = &alloc_list; *p; p = &((*p)->next) )
		if ((*p)->start > addr) break;
	n->next = *p;
	*p = n;

	return( n );
}


/* find a region (by start addr) in the alloced list */
static BLOCK *find_region( void *addr )
{
	BLOCK *p;

	for( p = alloc_list; p; p = p->next ) {
		if (p->start == addr)
			return( p );
		if (p->start > addr)
			break;
	}
	return( NULL );
}


/* remove a block from the alloced list */
static int remove_region( BLOCK *block )
{
	BLOCK **p;

	for( p = &alloc_list; *p; p = &((*p)->next) )
		if (*p == block) break;
	if (!*p)
		return( 0 );

	*p = block->next;
	if (block->flags & BLOCK_KMALLOCED)
		kfree( block );
	else
		block->flags |= BLOCK_FREE;
	return( 1 );
}



/* ------------------------------------------------------------------------ */
/*						 /proc statistics file stuff						*/
/* ------------------------------------------------------------------------ */

#ifdef DO_PROC

#define	PRINT_PROC(fmt,args...) len += sprintf( buf+len, fmt, ##args )

int get_stram_list( char *buf )
{
	int len = 0;
	BLOCK *p;

	PRINT_PROC("Total ST-RAM:      %8u kB\n",
			   (stram_end - stram_start) >> 10);
	PRINT_PROC( "Allocated regions:\n" );
	for( p = alloc_list; p; p = p->next ) {
		if (len + 50 >= PAGE_SIZE)
			break;
		PRINT_PROC("0x%08lx-0x%08lx: %s (",
			   virt_to_phys(p->start),
			   virt_to_phys(p->start+p->size-1),
			   p->owner);
		if (p->flags & BLOCK_GFP)
			PRINT_PROC( "page-alloced)\n" );
		else
			PRINT_PROC( "??)\n" );
	}

	return( len );
}

#endif


/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
