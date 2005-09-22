/* $Id: head.S,v 1.87 2002/02/09 19:49:31 davem Exp $
 * head.S: Initial boot code for the Sparc64 port of Linux.
 *
 * Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 David Sitsky (David.Sitsky@anu.edu.au)
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <asm/thread_info.h>
#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/errno.h>
#include <asm/signal.h>
#include <asm/processor.h>
#include <asm/lsu.h>
#include <asm/dcr.h>
#include <asm/dcu.h>
#include <asm/head.h>
#include <asm/ttable.h>
#include <asm/mmu.h>
	
/* This section from from _start to sparc64_boot_end should fit into
 * 0x0000.0000.0040.4000 to 0x0000.0000.0040.8000 and will be sharing space
 * with bootup_user_stack, which is from 0x0000.0000.0040.4000 to
 * 0x0000.0000.0040.6000 and empty_bad_page, which is from
 * 0x0000.0000.0040.6000 to 0x0000.0000.0040.8000. 
 */

	.text
	.globl	start, _start, stext, _stext
_start:
start:
_stext:
stext:
bootup_user_stack:
! 0x0000000000404000
	b	sparc64_boot
	 flushw					/* Flush register file.      */

/* This stuff has to be in sync with SILO and other potential boot loaders
 * Fields should be kept upward compatible and whenever any change is made,
 * HdrS version should be incremented.
 */
        .global root_flags, ram_flags, root_dev
        .global sparc_ramdisk_image, sparc_ramdisk_size
	.global sparc_ramdisk_image64

        .ascii  "HdrS"
        .word   LINUX_VERSION_CODE

	/* History:
	 *
	 * 0x0300 : Supports being located at other than 0x4000
	 * 0x0202 : Supports kernel params string
	 * 0x0201 : Supports reboot_command
	 */
	.half   0x0301          /* HdrS version */

root_flags:
        .half   1
root_dev:
        .half   0
ram_flags:
        .half   0
sparc_ramdisk_image:
        .word   0
sparc_ramdisk_size:
        .word   0
        .xword  reboot_command
	.xword	bootstr_info
sparc_ramdisk_image64:
	.xword	0
	.word	_end

	/* We must be careful, 32-bit OpenBOOT will get confused if it
	 * tries to save away a register window to a 64-bit kernel
	 * stack address.  Flush all windows, disable interrupts,
	 * remap if necessary, jump onto kernel trap table, then kernel
	 * stack, or else we die.
	 *
	 * PROM entry point is on %o4
	 */
sparc64_boot:
	BRANCH_IF_CHEETAH_BASE(g1,g7,cheetah_boot)
	BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(g1,g7,cheetah_plus_boot)
	ba,pt	%xcc, spitfire_boot
	 nop

cheetah_plus_boot:
	/* Preserve OBP chosen DCU and DCR register settings.  */
	ba,pt	%xcc, cheetah_generic_boot
	 nop

cheetah_boot:
	mov	DCR_BPE | DCR_RPE | DCR_SI | DCR_IFPOE | DCR_MS, %g1
	wr	%g1, %asr18

	sethi	%uhi(DCU_ME|DCU_RE|DCU_HPE|DCU_SPE|DCU_SL|DCU_WE), %g7
	or	%g7, %ulo(DCU_ME|DCU_RE|DCU_HPE|DCU_SPE|DCU_SL|DCU_WE), %g7
	sllx	%g7, 32, %g7
	or	%g7, DCU_DM | DCU_IM | DCU_DC | DCU_IC, %g7
	stxa	%g7, [%g0] ASI_DCU_CONTROL_REG
	membar	#Sync

cheetah_generic_boot:
	mov	TSB_EXTENSION_P, %g3
	stxa	%g0, [%g3] ASI_DMMU
	stxa	%g0, [%g3] ASI_IMMU
	membar	#Sync

	mov	TSB_EXTENSION_S, %g3
	stxa	%g0, [%g3] ASI_DMMU
	membar	#Sync

	mov	TSB_EXTENSION_N, %g3
	stxa	%g0, [%g3] ASI_DMMU
	stxa	%g0, [%g3] ASI_IMMU
	membar	#Sync

	wrpr    %g0, (PSTATE_PRIV|PSTATE_PEF|PSTATE_IE), %pstate
	wr	%g0, 0, %fprs

	/* Just like for Spitfire, we probe itlb-2 for a mapping which
	 * matches our current %pc.  We take the physical address in
	 * that mapping and use it to make our own.
	 */

	/* %g5 holds the tlb data */
        sethi   %uhi(_PAGE_VALID | _PAGE_SZ4MB), %g5
        sllx    %g5, 32, %g5
        or      %g5, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W | _PAGE_G), %g5

	/* Put PADDR tlb data mask into %g3. */
	sethi	%uhi(_PAGE_PADDR), %g3
	or	%g3, %ulo(_PAGE_PADDR), %g3
	sllx	%g3, 32, %g3
	sethi	%hi(_PAGE_PADDR), %g7
	or	%g7, %lo(_PAGE_PADDR), %g7
	or	%g3, %g7, %g3

	set	2 << 16, %l0		/* TLB entry walker. */
	set	0x1fff, %l2		/* Page mask. */
	rd	%pc, %l3
	andn	%l3, %l2, %g2		/* vaddr comparator */

1:	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	membar	#Sync
	andn	%g1, %l2, %g1
	cmp	%g1, %g2
	be,pn	%xcc, cheetah_got_tlbentry
	 nop
	and	%l0, (127 << 3), %g1
	cmp	%g1, (127 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	/* Search the small TLB.  OBP never maps us like that but
	 * newer SILO can.
	 */
	clr	%l0

1:	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	membar	#Sync
	andn	%g1, %l2, %g1
	cmp	%g1, %g2
	be,pn	%xcc, cheetah_got_tlbentry
	 nop
	cmp	%l0, (15 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	/* BUG() if we get here... */
	ta	0x5

cheetah_got_tlbentry:
	ldxa	[%l0] ASI_ITLB_DATA_ACCESS, %g0
	ldxa	[%l0] ASI_ITLB_DATA_ACCESS, %g1
	membar	#Sync
	and	%g1, %g3, %g1
	set	0x5fff, %l0
	andn	%g1, %l0, %g1
	or	%g5, %g1, %g5

	/* Clear out any KERNBASE area entries. */
	set	2 << 16, %l0
	sethi	%hi(KERNBASE), %g3
	sethi	%hi(KERNBASE<<1), %g7
	mov	TLB_TAG_ACCESS, %l7

	/* First, check ITLB */
1:	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	membar	#Sync
	andn	%g1, %l2, %g1
	cmp	%g1, %g3
	blu,pn	%xcc, 2f
	 cmp	%g1, %g7
	bgeu,pn	%xcc, 2f
	 nop
	stxa	%g0, [%l7] ASI_IMMU
	membar	#Sync
	stxa	%g0, [%l0] ASI_ITLB_DATA_ACCESS
	membar	#Sync

2:	and	%l0, (127 << 3), %g1
	cmp	%g1, (127 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	/* Next, check DTLB */
	set	2 << 16, %l0
1:	ldxa	[%l0] ASI_DTLB_TAG_READ, %g1
	membar	#Sync
	andn	%g1, %l2, %g1
	cmp	%g1, %g3
	blu,pn	%xcc, 2f
	 cmp	%g1, %g7
	bgeu,pn	%xcc, 2f
	 nop
	stxa	%g0, [%l7] ASI_DMMU
	membar	#Sync
	stxa	%g0, [%l0] ASI_DTLB_DATA_ACCESS
	membar	#Sync
	
2:	and	%l0, (511 << 3), %g1
	cmp	%g1, (511 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	/* On Cheetah+, have to check second DTLB.  */
	BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(g1,l0,2f)
	ba,pt	%xcc, 9f
	 nop

2:	set	3 << 16, %l0
1:	ldxa	[%l0] ASI_DTLB_TAG_READ, %g1
	membar	#Sync
	andn	%g1, %l2, %g1
	cmp	%g1, %g3
	blu,pn	%xcc, 2f
	 cmp	%g1, %g7
	bgeu,pn	%xcc, 2f
	 nop
	stxa	%g0, [%l7] ASI_DMMU
	membar	#Sync
	stxa	%g0, [%l0] ASI_DTLB_DATA_ACCESS
	membar	#Sync
	
2:	and	%l0, (511 << 3), %g1
	cmp	%g1, (511 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

9:

	/* Now lock the TTE we created into ITLB-0 and DTLB-0,
	 * entry 15 (and maybe 14 too).
	 */
	sethi	%hi(KERNBASE), %g3
	set	(0 << 16) | (15 << 3), %g7
	stxa	%g3, [%l7] ASI_DMMU
	membar	#Sync
	stxa	%g5, [%g7] ASI_DTLB_DATA_ACCESS
	membar	#Sync
	stxa	%g3, [%l7] ASI_IMMU
	membar	#Sync
	stxa	%g5, [%g7] ASI_ITLB_DATA_ACCESS
	membar	#Sync
	flush	%g3
	membar	#Sync
	sethi	%hi(_end), %g3			/* Check for bigkernel case */
	or	%g3, %lo(_end), %g3
	srl	%g3, 23, %g3			/* Check if _end > 8M */
	brz,pt	%g3, 1f
	 sethi	%hi(KERNBASE), %g3		/* Restore for fixup code below */
	sethi	%hi(0x400000), %g3
	or	%g3, %lo(0x400000), %g3
	add	%g5, %g3, %g5			/* New tte data */
	andn	%g5, (_PAGE_G), %g5
	sethi	%hi(KERNBASE+0x400000), %g3
	or	%g3, %lo(KERNBASE+0x400000), %g3
	set	(0 << 16) | (14 << 3), %g7
	stxa	%g3, [%l7] ASI_DMMU
	membar	#Sync
	stxa	%g5, [%g7] ASI_DTLB_DATA_ACCESS
	membar	#Sync
	stxa	%g3, [%l7] ASI_IMMU
	membar	#Sync
	stxa	%g5, [%g7] ASI_ITLB_DATA_ACCESS
	membar	#Sync
	flush	%g3
	membar	#Sync
	sethi	%hi(KERNBASE), %g3		/* Restore for fixup code below */
	ba,pt	%xcc, 1f
	 nop

1:	set	sun4u_init, %g2
	jmpl    %g2 + %g0, %g0
	 nop

spitfire_boot:
	/* Typically PROM has already enabled both MMU's and both on-chip
	 * caches, but we do it here anyway just to be paranoid.
	 */
	mov	(LSU_CONTROL_IC|LSU_CONTROL_DC|LSU_CONTROL_IM|LSU_CONTROL_DM), %g1
	stxa	%g1, [%g0] ASI_LSU_CONTROL
	membar	#Sync

	/*
	 * Make sure we are in privileged mode, have address masking,
         * using the ordinary globals and have enabled floating
         * point.
	 *
	 * Again, typically PROM has left %pil at 13 or similar, and
	 * (PSTATE_PRIV | PSTATE_PEF | PSTATE_IE) in %pstate.
         */
	wrpr    %g0, (PSTATE_PRIV|PSTATE_PEF|PSTATE_IE), %pstate
	wr	%g0, 0, %fprs

spitfire_create_mappings:
	/* %g5 holds the tlb data */
        sethi   %uhi(_PAGE_VALID | _PAGE_SZ4MB), %g5
        sllx    %g5, 32, %g5
        or      %g5, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W | _PAGE_G), %g5

	/* Base of physical memory cannot reliably be assumed to be
	 * at 0x0!  Figure out where it happens to be. -DaveM
	 */

	/* Put PADDR tlb data mask into %g3. */
	sethi	%uhi(_PAGE_PADDR_SF), %g3
	or	%g3, %ulo(_PAGE_PADDR_SF), %g3
	sllx	%g3, 32, %g3
	sethi	%hi(_PAGE_PADDR_SF), %g7
	or	%g7, %lo(_PAGE_PADDR_SF), %g7
	or	%g3, %g7, %g3

	/* Walk through entire ITLB, looking for entry which maps
	 * our %pc currently, stick PADDR from there into %g5 tlb data.
	 */
	clr	%l0			/* TLB entry walker. */
	set	0x1fff, %l2		/* Page mask. */
	rd	%pc, %l3
	andn	%l3, %l2, %g2		/* vaddr comparator */
1:
	/* Yes, the nops seem to be necessary for now, don't ask me why. -DaveM */
	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1		/* Get vaddr */
	cmp	%g1, %g2
	be,a,pn	%xcc, spitfire_got_tlbentry
	 ldxa	[%l0] ASI_ITLB_DATA_ACCESS, %g1
	cmp	%l0, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	/* BUG() if we get here... */
	ta	0x5

spitfire_got_tlbentry:
	/* Nops here again, perhaps Cheetah/Blackbird are better behaved... */
	nop
	nop
	nop
	and	%g1, %g3, %g1		/* Mask to just get paddr bits.       */
	set	0x5fff, %l3		/* Mask offset to get phys base.      */
	andn	%g1, %l3, %g1

	/* NOTE: We hold on to %g1 paddr base as we need it below to lock
	 * NOTE: the PROM cif code into the TLB.
	 */

	or	%g5, %g1, %g5		/* Or it into TAG being built.        */

	clr	%l0			/* TLB entry walker. */
	sethi	%hi(KERNBASE), %g3	/* 4M lower limit */
	sethi	%hi(KERNBASE<<1), %g7	/* 8M upper limit */
	mov	TLB_TAG_ACCESS, %l7
1:
	/* Yes, the nops seem to be necessary for now, don't ask me why. -DaveM */
	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1		/* Get vaddr */
	cmp	%g1, %g3
	blu,pn	%xcc, 2f
	 cmp	%g1, %g7
	bgeu,pn	%xcc, 2f
	 nop
	stxa	%g0, [%l7] ASI_IMMU
	stxa	%g0, [%l0] ASI_ITLB_DATA_ACCESS
	membar	#Sync
2:
	cmp	%l0, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	nop; nop; nop

	clr	%l0			/* TLB entry walker. */
1:
	/* Yes, the nops seem to be necessary for now, don't ask me why. -DaveM */
	ldxa	[%l0] ASI_DTLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1		/* Get vaddr */
	cmp	%g1, %g3
	blu,pn	%xcc, 2f
	 cmp	%g1, %g7
	bgeu,pn	%xcc, 2f
	 nop
	stxa	%g0, [%l7] ASI_DMMU
	stxa	%g0, [%l0] ASI_DTLB_DATA_ACCESS
	membar	#Sync
2:
	cmp	%l0, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	nop; nop; nop


	/* PROM never puts any TLB entries into the MMU with the lock bit
	 * set.  So we gladly use tlb entry 63 for KERNBASE. And maybe 62 too.
	 */

	sethi	%hi(KERNBASE), %g3
	mov	(63 << 3), %g7
	stxa	%g3, [%l7] ASI_DMMU		/* KERNBASE into TLB TAG	*/
	stxa	%g5, [%g7] ASI_DTLB_DATA_ACCESS	/* TTE into TLB DATA		*/
	membar	#Sync
	stxa	%g3, [%l7] ASI_IMMU		/* KERNBASE into TLB TAG	*/
	stxa	%g5, [%g7] ASI_ITLB_DATA_ACCESS	/* TTE into TLB DATA		*/
	membar	#Sync
	flush	%g3
	membar	#Sync
	sethi	%hi(_end), %g3			/* Check for bigkernel case */
	or	%g3, %lo(_end), %g3
	srl	%g3, 23, %g3			/* Check if _end > 8M */
	brz,pt	%g3, 2f
	 sethi	%hi(KERNBASE), %g3		/* Restore for fixup code below */
	sethi	%hi(0x400000), %g3
	or	%g3, %lo(0x400000), %g3
	add	%g5, %g3, %g5			/* New tte data */
	andn	%g5, (_PAGE_G), %g5
	sethi	%hi(KERNBASE+0x400000), %g3
	or	%g3, %lo(KERNBASE+0x400000), %g3
	mov	(62 << 3), %g7
	stxa	%g3, [%l7] ASI_DMMU
	stxa	%g5, [%g7] ASI_DTLB_DATA_ACCESS
	membar	#Sync
	stxa	%g3, [%l7] ASI_IMMU
	stxa	%g5, [%g7] ASI_ITLB_DATA_ACCESS
	membar	#Sync
	flush	%g3
	membar	#Sync
	sethi	%hi(KERNBASE), %g3		/* Restore for fixup code below */
2:	ba,pt	%xcc, 1f
	 nop
1:
	set	sun4u_init, %g2
	jmpl    %g2 + %g0, %g0
	 nop

sun4u_init:
	/* Set ctx 0 */
	mov	PRIMARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync

	mov	SECONDARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync

	/* We are now safely (we hope) in Nucleus context (0), rewrite
	 * the KERNBASE TTE's so they no longer have the global bit set.
	 * Don't forget to setup TAG_ACCESS first 8-)
	 */
	mov	TLB_TAG_ACCESS, %g2
	stxa	%g3, [%g2] ASI_IMMU
	stxa	%g3, [%g2] ASI_DMMU
	membar	#Sync

	BRANCH_IF_ANY_CHEETAH(g1,g7,cheetah_tlb_fixup)

	ba,pt	%xcc, spitfire_tlb_fixup
	 nop

cheetah_tlb_fixup:
	set	(0 << 16) | (15 << 3), %g7
	ldxa	[%g7] ASI_ITLB_DATA_ACCESS, %g0
	ldxa	[%g7] ASI_ITLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_ITLB_DATA_ACCESS
	membar	#Sync

	ldxa	[%g7] ASI_DTLB_DATA_ACCESS, %g0
	ldxa	[%g7] ASI_DTLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_DTLB_DATA_ACCESS
	membar	#Sync

	/* Kill instruction prefetch queues. */
	flush	%g3
	membar	#Sync

	mov	2, %g2		/* Set TLB type to cheetah+. */
	BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(g1,g7,1f)

	mov	1, %g2		/* Set TLB type to cheetah. */

1:	sethi	%hi(tlb_type), %g1
	stw	%g2, [%g1 + %lo(tlb_type)]

	BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(g1,g7,1f)
	ba,pt	%xcc, 2f
	 nop

1:	/* Patch context register writes to support nucleus page
	 * size correctly.
	 */
	call	cheetah_plus_patch_etrap
	 nop
	call	cheetah_plus_patch_rtrap
	 nop
	call	cheetah_plus_patch_fpdis
	 nop
	call	cheetah_plus_patch_winfixup
	 nop

2:	/* Patch copy/page operations to cheetah optimized versions. */
	call	cheetah_patch_copyops
	 nop
	call	cheetah_patch_copy_page
	 nop
	call	cheetah_patch_cachetlbops
	 nop

	ba,pt	%xcc, tlb_fixup_done
	 nop

spitfire_tlb_fixup:
	mov	(63 << 3), %g7
	ldxa	[%g7] ASI_ITLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_ITLB_DATA_ACCESS
	membar	#Sync

	ldxa	[%g7] ASI_DTLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_DTLB_DATA_ACCESS
	membar	#Sync

	/* Kill instruction prefetch queues. */
	flush	%g3
	membar	#Sync

	/* Set TLB type to spitfire. */
	mov	0, %g2
	sethi	%hi(tlb_type), %g1
	stw	%g2, [%g1 + %lo(tlb_type)]

tlb_fixup_done:
	sethi	%hi(init_thread_union), %g6
	or	%g6, %lo(init_thread_union), %g6
	ldx	[%g6 + TI_TASK], %g4
	mov	%sp, %l6
	mov	%o4, %l7

#if 0	/* We don't do it like this anymore, but for historical hack value
	 * I leave this snippet here to show how crazy we can be sometimes. 8-)
	 */

	/* Setup "Linux Current Register", thanks Sun 8-) */
	wr	%g0, 0x1, %pcr

	/* Blackbird errata workaround.  See commentary in
	 * smp.c:smp_percpu_timer_interrupt() for more
	 * information.
	 */
	ba,pt	%xcc, 99f
	 nop
	.align	64
99:	wr	%g6, %g0, %pic
	rd	%pic, %g0
#endif

	wr	%g0, ASI_P, %asi
	mov	1, %g1
	sllx	%g1, THREAD_SHIFT, %g1
	sub	%g1, (STACKFRAME_SZ + STACK_BIAS), %g1
	add	%g6, %g1, %sp
	mov	0, %fp

	/* Set per-cpu pointer initially to zero, this makes
	 * the boot-cpu use the in-kernel-image per-cpu areas
	 * before setup_per_cpu_area() is invoked.
	 */
	clr	%g5

	wrpr	%g0, 0, %wstate
	wrpr	%g0, 0x0, %tl

	/* Clear the bss */
	sethi	%hi(__bss_start), %o0
	or	%o0, %lo(__bss_start), %o0
	sethi	%hi(_end), %o1
	or	%o1, %lo(_end), %o1
	call	__bzero
	 sub	%o1, %o0, %o1

	mov	%l6, %o1			! OpenPROM stack
	call	prom_init
	 mov	%l7, %o0			! OpenPROM cif handler

	/* Off we go.... */
	call	start_kernel
	 nop
	/* Not reached... */

/* IMPORTANT NOTE: Whenever making changes here, check
 * trampoline.S as well. -jj */
	.globl	setup_tba
setup_tba:	/* i0 = is_starfire */
	save	%sp, -160, %sp

	rdpr	%tba, %g7
	sethi	%hi(prom_tba), %o1
	or	%o1, %lo(prom_tba), %o1
	stx	%g7, [%o1]

	/* Setup "Linux" globals 8-) */
	rdpr	%pstate, %o1
	mov	%g6, %o2
	wrpr	%o1, (PSTATE_AG|PSTATE_IE), %pstate
	sethi	%hi(sparc64_ttable_tl0), %g1
	wrpr	%g1, %tba
	mov	%o2, %g6

	/* Set up MMU globals */
	wrpr	%o1, (PSTATE_MG|PSTATE_IE), %pstate

	/* Set fixed globals used by dTLB miss handler. */
#define KERN_HIGHBITS		((_PAGE_VALID|_PAGE_SZ4MB)^0xfffff80000000000)
#define KERN_LOWBITS		(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)

	mov	TSB_REG, %g1
	stxa	%g0, [%g1] ASI_DMMU
	membar	#Sync
	stxa	%g0, [%g1] ASI_IMMU
	membar	#Sync
	mov	TLB_SFSR, %g1
	sethi	%uhi(KERN_HIGHBITS), %g2
	or	%g2, %ulo(KERN_HIGHBITS), %g2
	sllx	%g2, 32, %g2
	or	%g2, KERN_LOWBITS, %g2

	BRANCH_IF_ANY_CHEETAH(g3,g7,cheetah_vpte_base)
	ba,pt	%xcc, spitfire_vpte_base
	 nop

cheetah_vpte_base:
	sethi		%uhi(VPTE_BASE_CHEETAH), %g3
	or		%g3, %ulo(VPTE_BASE_CHEETAH), %g3
	ba,pt		%xcc, 2f
	 sllx		%g3, 32, %g3

spitfire_vpte_base:
	sethi		%uhi(VPTE_BASE_SPITFIRE), %g3
	or		%g3, %ulo(VPTE_BASE_SPITFIRE), %g3
	sllx		%g3, 32, %g3

2:
	clr	%g7
#undef KERN_HIGHBITS
#undef KERN_LOWBITS

	/* Kill PROM timer */
	sethi	%hi(0x80000000), %o2
	sllx	%o2, 32, %o2
	wr	%o2, 0, %tick_cmpr

	BRANCH_IF_ANY_CHEETAH(o2,o3,1f)

	ba,pt	%xcc, 2f
	 nop

	/* Disable STICK_INT interrupts. */
1:
	sethi	%hi(0x80000000), %o2
	sllx	%o2, 32, %o2
	wr	%o2, %asr25

	/* Ok, we're done setting up all the state our trap mechanims needs,
	 * now get back into normal globals and let the PROM know what is up.
	 */
2:
	wrpr	%g0, %g0, %wstate
	wrpr	%o1, PSTATE_IE, %pstate

	call	init_irqwork_curcpu
	 nop

	call	prom_set_trap_table
	 sethi	%hi(sparc64_ttable_tl0), %o0

	BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(g2,g3,1f)
	ba,pt	%xcc, 2f
	 nop

1:	/* Start using proper page size encodings in ctx register.  */
	sethi	%uhi(CTX_CHEETAH_PLUS_NUC), %g3
	mov	PRIMARY_CONTEXT, %g1
	sllx	%g3, 32, %g3
	sethi	%hi(CTX_CHEETAH_PLUS_CTX0), %g2
	or	%g3, %g2, %g3
	stxa	%g3, [%g1] ASI_DMMU
	membar	#Sync

2:
	rdpr	%pstate, %o1
	or	%o1, PSTATE_IE, %o1
	wrpr	%o1, 0, %pstate

	ret
	 restore

/*
 * The following skips make sure the trap table in ttable.S is aligned
 * on a 32K boundary as required by the v9 specs for TBA register.
 */
sparc64_boot_end:
	.skip	0x2000 + _start - sparc64_boot_end
bootup_user_stack_end:
	.skip	0x2000

#ifdef CONFIG_SBUS
/* This is just a hack to fool make depend config.h discovering
   strategy: As the .S files below need config.h, but
   make depend does not find it for them, we include config.h
   in head.S */
#endif

! 0x0000000000408000

#include "ttable.S"
#include "systbls.S"

	.align	1024
	.globl	swapper_pg_dir
swapper_pg_dir:
	.word	0

#include "ktlb.S"
#include "etrap.S"
#include "rtrap.S"
#include "winfixup.S"
#include "entry.S"

	/* This is just anal retentiveness on my part... */
	.align	16384

	.data
	.align	8
	.globl	prom_tba, tlb_type
prom_tba:	.xword	0
tlb_type:	.word	0	/* Must NOT end up in BSS */
	.section	".fixup",#alloc,#execinstr
	.globl	__ret_efault
__ret_efault:
	ret
	 restore %g0, -EFAULT, %o0

