/* clear_page.S: UltraSparc optimized copy page.
 *
 * Copyright (C) 1996, 1998, 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997 Jakub Jelinek (jakub@redhat.com)
 */

#include <asm/visasm.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/spitfire.h>
#include <asm/head.h>

	/* What we used to do was lock a TLB entry into a specific
	 * TLB slot, clear the page with interrupts disabled, then
	 * restore the original TLB entry.  This was great for
	 * disturbing the TLB as little as possible, but it meant
	 * we had to keep interrupts disabled for a long time.
	 *
	 * Now, we simply use the normal TLB loading mechanism,
	 * and this makes the cpu choose a slot all by itself.
	 * Then we do a normal TLB flush on exit.  We need only
	 * disable preemption during the clear.
	 */

#define	DCACHE_SIZE	(PAGE_SIZE * 2)

#if (PAGE_SHIFT == 13)
#define PAGE_SIZE_REM	0x80
#elif (PAGE_SHIFT == 16)
#define PAGE_SIZE_REM	0x100
#else
#error Wrong PAGE_SHIFT specified
#endif

#define TOUCH(reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7)	\
	fsrc2	%reg0, %f48; 	fsrc2	%reg1, %f50;		\
	fsrc2	%reg2, %f52; 	fsrc2	%reg3, %f54;		\
	fsrc2	%reg4, %f56; 	fsrc2	%reg5, %f58;		\
	fsrc2	%reg6, %f60; 	fsrc2	%reg7, %f62;

	.text

	.align		32
	.globl		copy_user_page
	.type		copy_user_page,#function
copy_user_page:		/* %o0=dest, %o1=src, %o2=vaddr */
	lduw		[%g6 + TI_PRE_COUNT], %o4
	sethi		%uhi(PAGE_OFFSET), %g2
	sethi		%hi(PAGE_SIZE), %o3

	sllx		%g2, 32, %g2
	sethi		%hi(PAGE_KERNEL_LOCKED), %g3

	ldx		[%g3 + %lo(PAGE_KERNEL_LOCKED)], %g3
	sub		%o0, %g2, %g1		! dest paddr

	sub		%o1, %g2, %g2		! src paddr

	and		%o2, %o3, %o0		! vaddr D-cache alias bit
	or		%g1, %g3, %g1		! dest TTE data

	or		%g2, %g3, %g2		! src TTE data
	sethi		%hi(TLBTEMP_BASE), %o3

	sethi		%hi(DCACHE_SIZE), %o1
	add		%o0, %o3, %o0		! dest TTE vaddr

	add		%o4, 1, %o2
	add		%o0, %o1, %o1		! src TTE vaddr

	/* Disable preemption.  */
	mov		TLB_TAG_ACCESS, %g3
	stw		%o2, [%g6 + TI_PRE_COUNT]

	/* Load TLB entries.  */
	rdpr		%pstate, %o2
	wrpr		%o2, PSTATE_IE, %pstate
	stxa		%o0, [%g3] ASI_DMMU
	stxa		%g1, [%g0] ASI_DTLB_DATA_IN
	membar		#Sync
	stxa		%o1, [%g3] ASI_DMMU
	stxa		%g2, [%g0] ASI_DTLB_DATA_IN
	membar		#Sync
	wrpr		%o2, 0x0, %pstate

cheetah_copy_page_insn:
	ba,pt		%xcc, 9f
	 nop

1:
	VISEntryHalf
	membar		#StoreLoad | #StoreStore | #LoadStore
	sethi		%hi((PAGE_SIZE/64)-2), %o2
	mov		%o0, %g1
	prefetch	[%o1 + 0x000], #one_read
	or		%o2, %lo((PAGE_SIZE/64)-2), %o2
	prefetch	[%o1 + 0x040], #one_read
	prefetch	[%o1 + 0x080], #one_read
	prefetch	[%o1 + 0x0c0], #one_read
	ldd		[%o1 + 0x000], %f0
	prefetch	[%o1 + 0x100], #one_read
	ldd		[%o1 + 0x008], %f2
	prefetch	[%o1 + 0x140], #one_read
	ldd		[%o1 + 0x010], %f4
	prefetch	[%o1 + 0x180], #one_read
	fsrc2		%f0, %f16
	ldd		[%o1 + 0x018], %f6
	fsrc2		%f2, %f18
	ldd		[%o1 + 0x020], %f8
	fsrc2		%f4, %f20
	ldd		[%o1 + 0x028], %f10
	fsrc2		%f6, %f22
	ldd		[%o1 + 0x030], %f12
	fsrc2		%f8, %f24
	ldd		[%o1 + 0x038], %f14
	fsrc2		%f10, %f26
	ldd		[%o1 + 0x040], %f0
1:	ldd		[%o1 + 0x048], %f2
	fsrc2		%f12, %f28
	ldd		[%o1 + 0x050], %f4
	fsrc2		%f14, %f30
	stda		%f16, [%o0] ASI_BLK_P
	ldd		[%o1 + 0x058], %f6
	fsrc2		%f0, %f16
	ldd		[%o1 + 0x060], %f8
	fsrc2		%f2, %f18
	ldd		[%o1 + 0x068], %f10
	fsrc2		%f4, %f20
	ldd		[%o1 + 0x070], %f12
	fsrc2		%f6, %f22
	ldd		[%o1 + 0x078], %f14
	fsrc2		%f8, %f24
	ldd		[%o1 + 0x080], %f0
	prefetch	[%o1 + 0x180], #one_read
	fsrc2		%f10, %f26
	subcc		%o2, 1, %o2
	add		%o0, 0x40, %o0
	bne,pt		%xcc, 1b
	 add		%o1, 0x40, %o1

	ldd		[%o1 + 0x048], %f2
	fsrc2		%f12, %f28
	ldd		[%o1 + 0x050], %f4
	fsrc2		%f14, %f30
	stda		%f16, [%o0] ASI_BLK_P
	ldd		[%o1 + 0x058], %f6
	fsrc2		%f0, %f16
	ldd		[%o1 + 0x060], %f8
	fsrc2		%f2, %f18
	ldd		[%o1 + 0x068], %f10
	fsrc2		%f4, %f20
	ldd		[%o1 + 0x070], %f12
	fsrc2		%f6, %f22
	add		%o0, 0x40, %o0
	ldd		[%o1 + 0x078], %f14
	fsrc2		%f8, %f24
	fsrc2		%f10, %f26
	fsrc2		%f12, %f28
	fsrc2		%f14, %f30
	stda		%f16, [%o0] ASI_BLK_P
	membar		#Sync
	VISExitHalf
	ba,pt		%xcc, 5f
	 nop

9:
	VISEntry
	ldub		[%g6 + TI_FAULT_CODE], %g3
	mov		%o0, %g1
	cmp		%g3, 0
	rd		%asi, %g3
	be,a,pt		%icc, 1f
	 wr		%g0, ASI_BLK_P, %asi
	wr		%g0, ASI_BLK_COMMIT_P, %asi
1:	ldda		[%o1] ASI_BLK_P, %f0
	add		%o1, 0x40, %o1
	ldda		[%o1] ASI_BLK_P, %f16
	add		%o1, 0x40, %o1
	sethi		%hi(PAGE_SIZE), %o2
1:	TOUCH(f0, f2, f4, f6, f8, f10, f12, f14)
	ldda		[%o1] ASI_BLK_P, %f32
	stda		%f48, [%o0] %asi
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	TOUCH(f16, f18, f20, f22, f24, f26, f28, f30)
	ldda		[%o1] ASI_BLK_P, %f0
	stda		%f48, [%o0] %asi
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	TOUCH(f32, f34, f36, f38, f40, f42, f44, f46)
	ldda		[%o1] ASI_BLK_P, %f16
	stda		%f48, [%o0] %asi
	sub		%o2, 0x40, %o2
	add		%o1, 0x40, %o1
	cmp		%o2, PAGE_SIZE_REM
	bne,pt		%xcc, 1b
	 add		%o0, 0x40, %o0
#if (PAGE_SHIFT == 16)
	TOUCH(f0, f2, f4, f6, f8, f10, f12, f14)
	ldda		[%o1] ASI_BLK_P, %f32
	stda		%f48, [%o0] %asi
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	TOUCH(f16, f18, f20, f22, f24, f26, f28, f30)
	ldda		[%o1] ASI_BLK_P, %f0
	stda		%f48, [%o0] %asi
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	membar		#Sync
	stda		%f32, [%o0] %asi
	add		%o0, 0x40, %o0
	stda		%f0, [%o0] %asi
#else
	membar		#Sync
	stda		%f0, [%o0] %asi
	add		%o0, 0x40, %o0
	stda		%f16, [%o0] %asi
#endif
	membar		#Sync
	wr		%g3, 0x0, %asi
	VISExit

5:
	stxa		%g0, [%g1] ASI_DMMU_DEMAP
	membar		#Sync

	sethi		%hi(DCACHE_SIZE), %g2
	stxa		%g0, [%g1 + %g2] ASI_DMMU_DEMAP
	membar		#Sync

	retl
	 stw		%o4, [%g6 + TI_PRE_COUNT]

	.size		copy_user_page, .-copy_user_page

	.globl		cheetah_patch_copy_page
cheetah_patch_copy_page:
	sethi		%hi(0x01000000), %o1	! NOP
	sethi		%hi(cheetah_copy_page_insn), %o0
	or		%o0, %lo(cheetah_copy_page_insn), %o0
	stw		%o1, [%o0]
	membar		#StoreStore
	flush		%o0
	retl
	 nop
