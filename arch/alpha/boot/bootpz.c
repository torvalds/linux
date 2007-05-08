/*
 * arch/alpha/boot/bootpz.c
 *
 * Copyright (C) 1997 Jay Estabrook
 *
 * This file is used for creating a compressed BOOTP file for the
 * Linux/AXP kernel
 *
 * based significantly on the arch/alpha/boot/main.c of Linus Torvalds
 * and the decompression code from MILO.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/utsrelease.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/console.h>
#include <asm/hwrpb.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#include <stdarg.h>

#include "kzsize.h"

/* FIXME FIXME FIXME */
#define MALLOC_AREA_SIZE 0x200000 /* 2MB for now */
/* FIXME FIXME FIXME */


/*
  WARNING NOTE

  It is very possible that turning on additional messages may cause
  kernel image corruption due to stack usage to do the printing.

*/

#undef DEBUG_CHECK_RANGE
#undef DEBUG_ADDRESSES
#undef DEBUG_LAST_STEPS

extern unsigned long switch_to_osf_pal(unsigned long nr,
	struct pcb_struct * pcb_va, struct pcb_struct * pcb_pa,
	unsigned long *vptb);

extern int decompress_kernel(void* destination, void *source,
			     size_t ksize, size_t kzsize);

extern void move_stack(unsigned long new_stack);

struct hwrpb_struct *hwrpb = INIT_HWRPB;
static struct pcb_struct pcb_va[1];

/*
 * Find a physical address of a virtual object..
 *
 * This is easy using the virtual page table address.
 */
#define VPTB	((unsigned long *) 0x200000000)

static inline unsigned long
find_pa(unsigned long address)
{
	unsigned long result;

	result = VPTB[address >> 13];
	result >>= 32;
	result <<= 13;
	result |= address & 0x1fff;
	return result;
}	

int
check_range(unsigned long vstart, unsigned long vend,
	    unsigned long kstart, unsigned long kend)
{
	unsigned long vaddr, kaddr;

#ifdef DEBUG_CHECK_RANGE
	srm_printk("check_range: V[0x%lx:0x%lx] K[0x%lx:0x%lx]\n",
		   vstart, vend, kstart, kend);
#endif
	/* do some range checking for detecting an overlap... */
	for (vaddr = vstart; vaddr <= vend; vaddr += PAGE_SIZE)
	{
		kaddr = (find_pa(vaddr) | PAGE_OFFSET);
		if (kaddr >= kstart && kaddr <= kend)
		{
#ifdef DEBUG_CHECK_RANGE
			srm_printk("OVERLAP: vaddr 0x%lx kaddr 0x%lx"
				   " [0x%lx:0x%lx]\n",
				   vaddr, kaddr, kstart, kend);
#endif
			return 1;
		}
	}
	return 0;
}

/*
 * This function moves into OSF/1 pal-code, and has a temporary
 * PCB for that. The kernel proper should replace this PCB with
 * the real one as soon as possible.
 *
 * The page table muckery in here depends on the fact that the boot
 * code has the L1 page table identity-map itself in the second PTE
 * in the L1 page table. Thus the L1-page is virtually addressable
 * itself (through three levels) at virtual address 0x200802000.
 */

#define L1	((unsigned long *) 0x200802000)

void
pal_init(void)
{
	unsigned long i, rev;
	struct percpu_struct * percpu;
	struct pcb_struct * pcb_pa;

	/* Create the dummy PCB.  */
	pcb_va->ksp = 0;
	pcb_va->usp = 0;
	pcb_va->ptbr = L1[1] >> 32;
	pcb_va->asn = 0;
	pcb_va->pcc = 0;
	pcb_va->unique = 0;
	pcb_va->flags = 1;
	pcb_va->res1 = 0;
	pcb_va->res2 = 0;
	pcb_pa = (struct pcb_struct *)find_pa((unsigned long)pcb_va);

	/*
	 * a0 = 2 (OSF)
	 * a1 = return address, but we give the asm the vaddr of the PCB
	 * a2 = physical addr of PCB
	 * a3 = new virtual page table pointer
	 * a4 = KSP (but the asm sets it)
	 */
	srm_printk("Switching to OSF PAL-code... ");

	i = switch_to_osf_pal(2, pcb_va, pcb_pa, VPTB);
	if (i) {
		srm_printk("failed, code %ld\n", i);
		__halt();
	}

	percpu = (struct percpu_struct *)
		(INIT_HWRPB->processor_offset + (unsigned long) INIT_HWRPB);
	rev = percpu->pal_revision = percpu->palcode_avail[2];

	srm_printk("OK (rev %lx)\n", rev);

	tbia(); /* do it directly in case we are SMP */
}

/*
 * Start the kernel.
 */
static inline void
runkernel(void)
{
	__asm__ __volatile__(
		"bis %0,%0,$27\n\t"
		"jmp ($27)"
		: /* no outputs: it doesn't even return */
		: "r" (START_ADDR));
}

/* Must record the SP (it is virtual) on entry, so we can make sure
   not to overwrite it during movement or decompression. */
unsigned long SP_on_entry;

/* Calculate the kernel image address based on the end of the BOOTP
   bootstrapper (ie this program).
*/
extern char _end;
#define KERNEL_ORIGIN \
	((((unsigned long)&_end) + 511) & ~511)

/* Round address to next higher page boundary. */
#define NEXT_PAGE(a)	(((a) | (PAGE_SIZE - 1)) + 1)

#ifdef INITRD_IMAGE_SIZE
# define REAL_INITRD_SIZE INITRD_IMAGE_SIZE
#else
# define REAL_INITRD_SIZE 0
#endif

/* Defines from include/asm-alpha/system.h

	BOOT_ADDR	Virtual address at which the consoles loads
			the BOOTP image.

	KERNEL_START    KSEG address at which the kernel is built to run,
			which includes some initial data pages before the
			code.

	START_ADDR	KSEG address of the entry point of kernel code.

	ZERO_PGE	KSEG address of page full of zeroes, but 
			upon entry to kerne cvan be expected
			to hold the parameter list and possible
			INTRD information.

   These are used in the local defines below.
*/
  

/* Virtual addresses for the BOOTP image. Note that this includes the
   bootstrapper code as well as the compressed kernel image, and
   possibly the INITRD image.

   Oh, and do NOT forget the STACK, which appears to be placed virtually
   beyond the end of the loaded image.
*/
#define V_BOOT_IMAGE_START	BOOT_ADDR
#define V_BOOT_IMAGE_END	SP_on_entry

/* Virtual addresses for just the bootstrapper part of the BOOTP image. */
#define V_BOOTSTRAPPER_START	BOOT_ADDR
#define V_BOOTSTRAPPER_END	KERNEL_ORIGIN

/* Virtual addresses for just the data part of the BOOTP
   image. This may also include the INITRD image, but always
   includes the STACK.
*/
#define V_DATA_START		KERNEL_ORIGIN
#define V_INITRD_START		(KERNEL_ORIGIN + KERNEL_Z_SIZE)
#define V_INTRD_END		(V_INITRD_START + REAL_INITRD_SIZE)
#define V_DATA_END	 	V_BOOT_IMAGE_END

/* KSEG addresses for the uncompressed kernel.

   Note that the end address includes workspace for the decompression.
   Note also that the DATA_START address is ZERO_PGE, to which we write
   just before jumping to the kernel image at START_ADDR.
 */
#define K_KERNEL_DATA_START	ZERO_PGE
#define K_KERNEL_IMAGE_START	START_ADDR
#define K_KERNEL_IMAGE_END	(START_ADDR + KERNEL_SIZE)

/* Define to where we may have to decompress the kernel image, before
   we move it to the final position, in case of overlap. This will be
   above the final position of the kernel.

   Regardless of overlap, we move the INITRD image to the end of this
   copy area, because there needs to be a buffer area after the kernel
   for "bootmem" anyway.
*/
#define K_COPY_IMAGE_START	NEXT_PAGE(K_KERNEL_IMAGE_END)
/* Reserve one page below INITRD for the new stack. */
#define K_INITRD_START \
    NEXT_PAGE(K_COPY_IMAGE_START + KERNEL_SIZE + PAGE_SIZE)
#define K_COPY_IMAGE_END \
    (K_INITRD_START + REAL_INITRD_SIZE + MALLOC_AREA_SIZE)
#define K_COPY_IMAGE_SIZE \
    NEXT_PAGE(K_COPY_IMAGE_END - K_COPY_IMAGE_START)

void
start_kernel(void)
{
	int must_move = 0;

	/* Initialize these for the decompression-in-place situation,
	   which is the smallest amount of work and most likely to
	   occur when using the normal START_ADDR of the kernel
	   (currently set to 16MB, to clear all console code.
	*/
	unsigned long uncompressed_image_start = K_KERNEL_IMAGE_START;
	unsigned long uncompressed_image_end = K_KERNEL_IMAGE_END;

	unsigned long initrd_image_start = K_INITRD_START;

	/*
	 * Note that this crufty stuff with static and envval
	 * and envbuf is because:
	 *
	 * 1. Frequently, the stack is short, and we don't want to overrun;
	 * 2. Frequently the stack is where we are going to copy the kernel to;
	 * 3. A certain SRM console required the GET_ENV output to stack.
	 *    ??? A comment in the aboot sources indicates that the GET_ENV
	 *    destination must be quadword aligned.  Might this explain the
	 *    behaviour, rather than requiring output to the stack, which
	 *    seems rather far-fetched.
	 */
	static long nbytes;
	static char envval[256] __attribute__((aligned(8)));
	register unsigned long asm_sp asm("30");

	SP_on_entry = asm_sp;

	srm_printk("Linux/Alpha BOOTPZ Loader for Linux " UTS_RELEASE "\n");

	/* Validity check the HWRPB. */
	if (INIT_HWRPB->pagesize != 8192) {
		srm_printk("Expected 8kB pages, got %ldkB\n",
		           INIT_HWRPB->pagesize >> 10);
		return;
	}
	if (INIT_HWRPB->vptb != (unsigned long) VPTB) {
		srm_printk("Expected vptb at %p, got %p\n",
			   VPTB, (void *)INIT_HWRPB->vptb);
		return;
	}

	/* PALcode (re)initialization. */
	pal_init();

	/* Get the parameter list from the console environment variable. */
	nbytes = callback_getenv(ENV_BOOTED_OSFLAGS, envval, sizeof(envval));
	if (nbytes < 0 || nbytes >= sizeof(envval)) {
		nbytes = 0;
	}
	envval[nbytes] = '\0';

#ifdef DEBUG_ADDRESSES
	srm_printk("START_ADDR 0x%lx\n", START_ADDR);
	srm_printk("KERNEL_ORIGIN 0x%lx\n", KERNEL_ORIGIN);
	srm_printk("KERNEL_SIZE 0x%x\n", KERNEL_SIZE);
	srm_printk("KERNEL_Z_SIZE 0x%x\n", KERNEL_Z_SIZE);
#endif

	/* Since all the SRM consoles load the BOOTP image at virtual
	 * 0x20000000, we have to ensure that the physical memory
	 * pages occupied by that image do NOT overlap the physical
	 * address range where the kernel wants to be run.  This
	 * causes real problems when attempting to cdecompress the
	 * former into the latter... :-(
	 *
	 * So, we may have to decompress/move the kernel/INITRD image
	 * virtual-to-physical someplace else first before moving
	 * kernel /INITRD to their final resting places... ;-}
	 *
	 * Sigh...
	 */

	/* First, check to see if the range of addresses occupied by
	   the bootstrapper part of the BOOTP image include any of the
	   physical pages into which the kernel will be placed for
	   execution.

	   We only need check on the final kernel image range, since we
	   will put the INITRD someplace that we can be sure is not
	   in conflict.
	 */
	if (check_range(V_BOOTSTRAPPER_START, V_BOOTSTRAPPER_END,
			K_KERNEL_DATA_START, K_KERNEL_IMAGE_END))
	{
		srm_printk("FATAL ERROR: overlap of bootstrapper code\n");
		__halt();
	}

	/* Next, check to see if the range of addresses occupied by
	   the compressed kernel/INITRD/stack portion of the BOOTP
	   image include any of the physical pages into which the
	   decompressed kernel or the INITRD will be placed for
	   execution.
	 */
	if (check_range(V_DATA_START, V_DATA_END,
			K_KERNEL_IMAGE_START, K_COPY_IMAGE_END))
	{
#ifdef DEBUG_ADDRESSES
		srm_printk("OVERLAP: cannot decompress in place\n");
#endif
		uncompressed_image_start = K_COPY_IMAGE_START;
		uncompressed_image_end = K_COPY_IMAGE_END;
		must_move = 1;

		/* Finally, check to see if the range of addresses
		   occupied by the compressed kernel/INITRD part of
		   the BOOTP image include any of the physical pages
		   into which that part is to be copied for
		   decompression.
		*/
		while (check_range(V_DATA_START, V_DATA_END,
				   uncompressed_image_start,
				   uncompressed_image_end))
		{
#if 0
			uncompressed_image_start += K_COPY_IMAGE_SIZE;
			uncompressed_image_end += K_COPY_IMAGE_SIZE;
			initrd_image_start += K_COPY_IMAGE_SIZE;
#else
			/* Keep as close as possible to end of BOOTP image. */
			uncompressed_image_start += PAGE_SIZE;
			uncompressed_image_end += PAGE_SIZE;
			initrd_image_start += PAGE_SIZE;
#endif
		}
	}

	srm_printk("Starting to load the kernel with args '%s'\n", envval);

#ifdef DEBUG_ADDRESSES
	srm_printk("Decompressing the kernel...\n"
		   "...from 0x%lx to 0x%lx size 0x%x\n",
		   V_DATA_START,
		   uncompressed_image_start,
		   KERNEL_SIZE);
#endif
        decompress_kernel((void *)uncompressed_image_start,
			  (void *)V_DATA_START,
			  KERNEL_SIZE, KERNEL_Z_SIZE);

	/*
	 * Now, move things to their final positions, if/as required.
	 */

#ifdef INITRD_IMAGE_SIZE

	/* First, we always move the INITRD image, if present. */
#ifdef DEBUG_ADDRESSES
	srm_printk("Moving the INITRD image...\n"
		   " from 0x%lx to 0x%lx size 0x%x\n",
		   V_INITRD_START,
		   initrd_image_start,
		   INITRD_IMAGE_SIZE);
#endif
	memcpy((void *)initrd_image_start, (void *)V_INITRD_START,
	       INITRD_IMAGE_SIZE);

#endif /* INITRD_IMAGE_SIZE */

	/* Next, we may have to move the uncompressed kernel to the
	   final destination.
	 */
	if (must_move) {
#ifdef DEBUG_ADDRESSES
		srm_printk("Moving the uncompressed kernel...\n"
			   "...from 0x%lx to 0x%lx size 0x%x\n",
			   uncompressed_image_start,
			   K_KERNEL_IMAGE_START,
			   (unsigned)KERNEL_SIZE);
#endif
		/*
		 * Move the stack to a safe place to ensure it won't be
		 * overwritten by kernel image.
		 */
		move_stack(initrd_image_start - PAGE_SIZE);

		memcpy((void *)K_KERNEL_IMAGE_START,
		       (void *)uncompressed_image_start, KERNEL_SIZE);
	}
	
	/* Clear the zero page, then move the argument list in. */
#ifdef DEBUG_LAST_STEPS
	srm_printk("Preparing ZERO_PGE...\n");
#endif
	memset((char*)ZERO_PGE, 0, PAGE_SIZE);
	strcpy((char*)ZERO_PGE, envval);

#ifdef INITRD_IMAGE_SIZE

#ifdef DEBUG_LAST_STEPS
	srm_printk("Preparing INITRD info...\n");
#endif
	/* Finally, set the INITRD paramenters for the kernel. */
	((long *)(ZERO_PGE+256))[0] = initrd_image_start;
	((long *)(ZERO_PGE+256))[1] = INITRD_IMAGE_SIZE;

#endif /* INITRD_IMAGE_SIZE */

#ifdef DEBUG_LAST_STEPS
	srm_printk("Doing 'runkernel()'...\n");
#endif
	runkernel();
}

 /* dummy function, should never be called. */
void *__kmalloc(size_t size, gfp_t flags)
{
	return (void *)NULL;
}
