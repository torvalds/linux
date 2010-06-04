/* $Id: head.S,v 1.7 2003/09/01 17:58:19 lethal Exp $
 *
 *  arch/sh/kernel/head.S
 *
 *  Copyright (C) 1999, 2000  Niibe Yutaka & Kaz Kojima
 *  Copyright (C) 2010  Matt Fleming
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Head.S contains the SH exception handlers and startup code.
 */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/mmu.h>
#include <cpu/mmu_context.h>

#ifdef CONFIG_CPU_SH4A
#define SYNCO()		synco

#define PREFI(label, reg)	\
	mov.l	label, reg;	\
	prefi	@reg
#else
#define SYNCO()
#define PREFI(label, reg)
#endif

	.section	.empty_zero_page, "aw"
ENTRY(empty_zero_page)
	.long	1		/* MOUNT_ROOT_RDONLY */
	.long	0		/* RAMDISK_FLAGS */
	.long	0x0200		/* ORIG_ROOT_DEV */
	.long	1		/* LOADER_TYPE */
	.long	0x00000000	/* INITRD_START */
	.long	0x00000000	/* INITRD_SIZE */
#ifdef CONFIG_32BIT
	.long	0x53453f00 + 32	/* "SE?" = 32 bit */
#else
	.long	0x53453f00 + 29	/* "SE?" = 29 bit */
#endif
1:
	.skip	PAGE_SIZE - empty_zero_page - 1b

	__HEAD

/*
 * Condition at the entry of _stext:
 *
 *   BSC has already been initialized.
 *   INTC may or may not be initialized.
 *   VBR may or may not be initialized.
 *   MMU may or may not be initialized.
 *   Cache may or may not be initialized.
 *   Hardware (including on-chip modules) may or may not be initialized. 
 *
 */
ENTRY(_stext)
	!			Initialize Status Register
	mov.l	1f, r0		! MD=1, RB=0, BL=0, IMASK=0xF
	ldc	r0, sr
	!			Initialize global interrupt mask
#ifdef CONFIG_CPU_HAS_SR_RB
	mov	#0, r0
	ldc	r0, r6_bank
#endif
	
	/*
	 * Prefetch if possible to reduce cache miss penalty.
	 *
	 * We do this early on for SH-4A as a micro-optimization,
	 * as later on we will have speculative execution enabled
	 * and this will become less of an issue.
	 */
	PREFI(5f, r0)
	PREFI(6f, r0)

	!
	mov.l	2f, r0
	mov	r0, r15		! Set initial r15 (stack pointer)
#ifdef CONFIG_CPU_HAS_SR_RB
	mov.l	7f, r0
	ldc	r0, r7_bank	! ... and initial thread_info
#endif

#ifdef CONFIG_PMB
/*
 * Reconfigure the initial PMB mappings setup by the hardware.
 *
 * When we boot in 32-bit MMU mode there are 2 PMB entries already
 * setup for us.
 *
 * Entry       VPN	   PPN	    V	SZ	C	UB	WT
 * ---------------------------------------------------------------
 *   0	    0x80000000 0x00000000   1  512MB	1	0	1
 *   1	    0xA0000000 0x00000000   1  512MB	0	0	0
 *
 * But we reprogram them here because we want complete control over
 * our address space and the initial mappings may not map PAGE_OFFSET
 * to __MEMORY_START (or even map all of our RAM).
 *
 * Once we've setup cached and uncached mappings we clear the rest of the
 * PMB entries. This clearing also deals with the fact that PMB entries
 * can persist across reboots. The PMB could have been left in any state
 * when the reboot occurred, so to be safe we clear all entries and start
 * with with a clean slate.
 *
 * The uncached mapping is constructed using the smallest possible
 * mapping with a single unbufferable page. Only the kernel text needs to
 * be covered via the uncached mapping so that certain functions can be
 * run uncached.
 *
 * Drivers and the like that have previously abused the 1:1 identity
 * mapping are unsupported in 32-bit mode and must specify their caching
 * preference when page tables are constructed.
 *
 * This frees up the P2 space for more nefarious purposes.
 *
 * Register utilization is as follows:
 *
 *	r0 = PMB_DATA data field
 *	r1 = PMB_DATA address field
 *	r2 = PMB_ADDR data field
 *	r3 = PMB_ADDR address field
 *	r4 = PMB_E_SHIFT
 *	r5 = remaining amount of RAM to map
 *	r6 = PMB mapping size we're trying to use
 *	r7 = cached_to_uncached
 *	r8 = scratch register
 *	r9 = scratch register
 *	r10 = number of PMB entries we've setup
 *	r11 = scratch register
 */

	mov.l	.LMMUCR, r1	/* Flush the TLB */
	mov.l	@r1, r0
	or	#MMUCR_TI, r0
	mov.l	r0, @r1

	mov.l	.LMEMORY_SIZE, r5

	mov	#PMB_E_SHIFT, r0
	mov	#0x1, r4
	shld	r0, r4

	mov.l	.LFIRST_DATA_ENTRY, r0
	mov.l	.LPMB_DATA, r1
	mov.l	.LFIRST_ADDR_ENTRY, r2
	mov.l	.LPMB_ADDR, r3

	/*
	 * First we need to walk the PMB and figure out if there are any
	 * existing mappings that match the initial mappings VPN/PPN.
	 * If these have already been established by the bootloader, we
	 * don't bother setting up new entries here, and let the late PMB
	 * initialization take care of things instead.
	 *
	 * Note that we may need to coalesce and merge entries in order
	 * to reclaim more available PMB slots, which is much more than
	 * we want to do at this early stage.
	 */
	mov	#0, r10
	mov	#NR_PMB_ENTRIES, r9

	mov	r1, r7		/* temporary PMB_DATA iter */

.Lvalidate_existing_mappings:

	mov.l	.LPMB_DATA_MASK, r11
	mov.l	@r7, r8
	and	r11, r8
	cmp/eq	r0, r8		/* Check for valid __MEMORY_START mappings */
	bt	.Lpmb_done

	add	#1, r10		/* Increment the loop counter */
	cmp/eq	r9, r10
	bf/s	.Lvalidate_existing_mappings
	 add	r4, r7		/* Increment to the next PMB_DATA entry */

	/*
	 * If we've fallen through, continue with setting up the initial
	 * mappings.
	 */

	mov	r5, r7		/* cached_to_uncached */
	mov	#0, r10

#ifdef CONFIG_UNCACHED_MAPPING
	/*
	 * Uncached mapping
	 */
	mov	#(PMB_SZ_16M >> 2), r9
	shll2	r9

	mov	#(PMB_UB >> 8), r8
	shll8	r8

	or	r0, r8
	or	r9, r8
	mov.l	r8, @r1
	mov	r2, r8
	add	r7, r8
	mov.l	r8, @r3

	add	r4, r1
	add	r4, r3
	add	#1, r10
#endif

/*
 * Iterate over all of the available sizes from largest to
 * smallest for constructing the cached mapping.
 */
#define __PMB_ITER_BY_SIZE(size)			\
.L##size:						\
	mov	#(size >> 4), r6;			\
	shll16	r6;					\
	shll8	r6;					\
							\
	cmp/hi	r5, r6;					\
	bt	9999f;					\
							\
	mov	#(PMB_SZ_##size##M >> 2), r9;		\
	shll2	r9;					\
							\
	/*						\
	 * Cached mapping				\
	 */						\
	mov	#PMB_C, r8;				\
	or	r0, r8;					\
	or	r9, r8;					\
	mov.l	r8, @r1;				\
	mov.l	r2, @r3;				\
							\
	/* Increment to the next PMB_DATA entry */	\
	add	r4, r1;					\
	/* Increment to the next PMB_ADDR entry */	\
	add	r4, r3;					\
	/* Increment number of PMB entries */		\
	add	#1, r10;				\
							\
	sub	r6, r5;					\
	add	r6, r0;					\
	add	r6, r2;					\
							\
	bra	.L##size;				\
9999:

	__PMB_ITER_BY_SIZE(512)
	__PMB_ITER_BY_SIZE(128)
	__PMB_ITER_BY_SIZE(64)
	__PMB_ITER_BY_SIZE(16)

#ifdef CONFIG_UNCACHED_MAPPING
	/*
	 * Now that we can access it, update cached_to_uncached and
	 * uncached_size.
	 */
	mov.l	.Lcached_to_uncached, r0
	mov.l	r7, @r0

	mov.l	.Luncached_size, r0
	mov	#1, r7
	shll16	r7
	shll8	r7
	mov.l	r7, @r0
#endif

	/*
	 * Clear the remaining PMB entries.
	 *
	 * r3 = entry to begin clearing from
	 * r10 = number of entries we've setup so far
	 */
	mov	#0, r1
	mov	#NR_PMB_ENTRIES, r0

.Lagain:
	mov.l	r1, @r3		/* Clear PMB_ADDR entry */
	add	#1, r10		/* Increment the loop counter */
	cmp/eq	r0, r10
	bf/s	.Lagain
	 add	r4, r3		/* Increment to the next PMB_ADDR entry */

	mov.l	6f, r0
	icbi	@r0

.Lpmb_done:
#endif /* CONFIG_PMB */

#ifndef CONFIG_SH_NO_BSS_INIT
	/*
	 * Don't clear BSS if running on slow platforms such as an RTL simulation,
	 * remote memory via SHdebug link, etc.  For these the memory can be guaranteed
	 * to be all zero on boot anyway.
	 */
				! Clear BSS area
#ifdef CONFIG_SMP	
	mov.l	3f, r0
	cmp/eq	#0, r0		! skip clear if set to zero
	bt	10f
#endif
	
	mov.l	3f, r1
	add	#4, r1
	mov.l	4f, r2
	mov	#0, r0
9:	cmp/hs	r2, r1
	bf/s	9b		! while (r1 < r2)
	 mov.l	r0,@-r2

10:		
#endif

	!			Additional CPU initialization
	mov.l	6f, r0
	jsr	@r0
	 nop

	SYNCO()			! Wait for pending instructions..
	
	!			Start kernel
	mov.l	5f, r0
	jmp	@r0
	 nop

	.balign 4
#if defined(CONFIG_CPU_SH2)
1:	.long	0x000000F0		! IMASK=0xF
#else
1:	.long	0x400080F0		! MD=1, RB=0, BL=0, FD=1, IMASK=0xF
#endif
ENTRY(stack_start)
2:	.long	init_thread_union+THREAD_SIZE
3:	.long	__bss_start
4:	.long	_end
5:	.long	start_kernel
6:	.long	cpu_init
7:	.long	init_thread_union

#ifdef CONFIG_PMB
.LPMB_ADDR:		.long	PMB_ADDR
.LPMB_DATA:		.long	PMB_DATA
.LPMB_DATA_MASK:	.long	PMB_PFN_MASK | PMB_V
.LFIRST_ADDR_ENTRY:	.long	PAGE_OFFSET | PMB_V
.LFIRST_DATA_ENTRY:	.long	__MEMORY_START | PMB_V
.LMMUCR:		.long	MMUCR
.LMEMORY_SIZE:		.long	__MEMORY_SIZE
#ifdef CONFIG_UNCACHED_MAPPING
.Lcached_to_uncached:	.long	cached_to_uncached
.Luncached_size:	.long	uncached_size
#endif
#endif
