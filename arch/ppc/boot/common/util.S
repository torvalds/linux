/*
 * Useful bootup functions, which are more easily done in asm than C.
 *
 * NOTE:  Be very very careful about the registers you use here.
 *	We don't follow any ABI calling convention among the
 *	assembler functions that call each other, especially early
 *	in the initialization.  Please preserve at least r3 and r4
 *	for these early functions, as they often contain information
 *	passed from boot roms into the C decompress function.
 *
 * Author: Tom Rini
 *	   trini@mvista.com
 * Derived from arch/ppc/boot/prep/head.S (Cort Dougan, many others).
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/ppc_asm.h>


	.text

#ifdef CONFIG_6xx
	.globl	disable_6xx_mmu
disable_6xx_mmu:
	/* Establish default MSR value, exception prefix 0xFFF.
	 * If necessary, this function must fix up the LR if we
	 * return to a different address space once the MMU is
	 * disabled.
	 */
	li	r8,MSR_IP|MSR_FP
	mtmsr	r8
	isync

	/* Test for a 601 */
	mfpvr	r10
	srwi	r10,r10,16
	cmpwi	0,r10,1		/* 601 ? */
	beq	.clearbats_601

	/* Clear BATs */
	li	r8,0
	mtspr	SPRN_DBAT0U,r8
	mtspr	SPRN_DBAT0L,r8
	mtspr	SPRN_DBAT1U,r8
	mtspr	SPRN_DBAT1L,r8
	mtspr	SPRN_DBAT2U,r8
	mtspr	SPRN_DBAT2L,r8
	mtspr	SPRN_DBAT3U,r8
	mtspr	SPRN_DBAT3L,r8
.clearbats_601:
	mtspr	SPRN_IBAT0U,r8
	mtspr	SPRN_IBAT0L,r8
	mtspr	SPRN_IBAT1U,r8
	mtspr	SPRN_IBAT1L,r8
	mtspr	SPRN_IBAT2U,r8
	mtspr	SPRN_IBAT2L,r8
	mtspr	SPRN_IBAT3U,r8
	mtspr	SPRN_IBAT3L,r8
	isync
	sync
	sync

	/* Set segment registers */
	li	r8,16		/* load up segment register values */
	mtctr	r8		/* for context 0 */
	lis	r8,0x2000	/* Ku = 1, VSID = 0 */
	li	r10,0
3:	mtsrin	r8,r10
	addi	r8,r8,0x111	/* increment VSID */
	addis	r10,r10,0x1000	/* address of next segment */
	bdnz	3b
	blr

	.globl	disable_6xx_l1cache
disable_6xx_l1cache:
	/* Enable, invalidate and then disable the L1 icache/dcache. */
	li	r8,0
	ori	r8,r8,(HID0_ICE|HID0_DCE|HID0_ICFI|HID0_DCI)
	mfspr	r11,SPRN_HID0
	or	r11,r11,r8
	andc	r10,r11,r8
	isync
	mtspr	SPRN_HID0,r8
	sync
	isync
	mtspr	SPRN_HID0,r10
	sync
	isync
	blr
#endif

	.globl	_setup_L2CR
_setup_L2CR:
/*
 * We should be skipping this section on CPUs where this results in an
 * illegal instruction.  If not, please send trini@kernel.crashing.org
 * the PVR of your CPU.
 */
	/* Invalidate/disable L2 cache */
	sync
	isync
	mfspr	r8,SPRN_L2CR
	rlwinm	r8,r8,0,1,31
	oris	r8,r8,L2CR_L2I@h
	sync
	isync
	mtspr	SPRN_L2CR,r8
	sync
	isync

	/* Wait for the invalidation to complete */
	mfspr   r8,SPRN_PVR
	srwi    r8,r8,16
	cmplwi	cr0,r8,0x8000			/* 7450 */
	cmplwi	cr1,r8,0x8001			/* 7455 */
	cmplwi	cr2,r8,0x8002			/* 7457 */
	cror	4*cr0+eq,4*cr0+eq,4*cr1+eq	/* Now test if any are true. */
	cror	4*cr0+eq,4*cr0+eq,4*cr2+eq
	bne     2f

1:	mfspr	r8,SPRN_L2CR	/* On 745x, poll L2I bit (bit 10) */
	rlwinm.	r9,r8,0,10,10
	bne	1b
	b	3f

2:      mfspr   r8,SPRN_L2CR	/* On 75x & 74[01]0, poll L2IP bit (bit 31) */
	rlwinm. r9,r8,0,31,31
	bne     2b

3:	rlwinm	r8,r8,0,11,9	/* Turn off L2I bit */
	sync
	isync
	mtspr	SPRN_L2CR,r8
	sync
	isync
	blr

	.globl	_setup_L3CR
_setup_L3CR:
	/* Invalidate/disable L3 cache */
	sync
	isync
	mfspr	r8,SPRN_L3CR
	rlwinm	r8,r8,0,1,31
	ori	r8,r8,L3CR_L3I@l
	sync
	isync
	mtspr	SPRN_L3CR,r8
	sync
	isync

	/* Wait for the invalidation to complete */
1:	mfspr	r8,SPRN_L3CR
	rlwinm.	r9,r8,0,21,21
	bne	1b

	rlwinm	r8,r8,0,22,20		/* Turn off L3I bit */
	sync
	isync
	mtspr	SPRN_L3CR,r8
	sync
	isync
	blr


/* udelay (on non-601 processors) needs to know the period of the
 * timebase in nanoseconds.  This used to be hardcoded to be 60ns
 * (period of 66MHz/4).  Now a variable is used that is initialized to
 * 60 for backward compatibility, but it can be overridden as necessary
 * with code something like this:
 *    extern unsigned long timebase_period_ns;
 *    timebase_period_ns = 1000000000 / bd->bi_tbfreq;
 */
	.data
	.globl timebase_period_ns
timebase_period_ns:
	.long	60

	.text
/*
 * Delay for a number of microseconds
 */
	.globl	udelay
udelay:
	mfspr	r4,SPRN_PVR
	srwi	r4,r4,16
	cmpwi	0,r4,1		/* 601 ? */
	bne	.udelay_not_601
00:	li	r0,86	/* Instructions / microsecond? */
	mtctr	r0
10:	addi	r0,r0,0 /* NOP */
	bdnz	10b
	subic.	r3,r3,1
	bne	00b
	blr

.udelay_not_601:
	mulli	r4,r3,1000	/* nanoseconds */
	/*  Change r4 to be the number of ticks using:	
	 *	(nanoseconds + (timebase_period_ns - 1 )) / timebase_period_ns
	 *  timebase_period_ns defaults to 60 (16.6MHz) */
	lis	r5,timebase_period_ns@ha
	lwz	r5,timebase_period_ns@l(r5)
	add	r4,r4,r5
	addi	r4,r4,-1
	divw	r4,r4,r5	/* BUS ticks */
1:	mftbu	r5
	mftb	r6
	mftbu	r7
	cmpw	0,r5,r7
	bne	1b		/* Get [synced] base time */
	addc	r9,r6,r4	/* Compute end time */
	addze	r8,r5
2:	mftbu	r5
	cmpw	0,r5,r8
	blt	2b
	bgt	3f
	mftb	r6
	cmpw	0,r6,r9
	blt	2b
3:	blr

	.section ".relocate_code","xa"
/*
 * Flush and enable instruction cache
 * First, flush the data cache in case it was enabled and may be
 * holding instructions for copy back.
 */
        .globl flush_instruction_cache
flush_instruction_cache:        
	mflr	r6
	bl	flush_data_cache

#ifdef CONFIG_8xx
	lis	r3, IDC_INVALL@h
	mtspr	SPRN_IC_CST, r3
	lis	r3, IDC_ENABLE@h
	mtspr	SPRN_IC_CST, r3
	lis	r3, IDC_DISABLE@h
	mtspr	SPRN_DC_CST, r3
#elif CONFIG_4xx
	lis	r3,start@h		# r9 = &_start
	lis	r4,_etext@ha
	addi	r4,r4,_etext@l		# r8 = &_etext
1:	dcbf	r0,r3			# Flush the data cache
	icbi	r0,r3			# Invalidate the instruction cache
	addi	r3,r3,0x10		# Increment by one cache line
	cmplw	cr0,r3,r4		# Are we at the end yet?
	blt	1b			# No, keep flushing and invalidating
#else
	/* Enable, invalidate and then disable the L1 icache/dcache. */
	li	r3,0
	ori	r3,r3,(HID0_ICE|HID0_DCE|HID0_ICFI|HID0_DCI)
	mfspr	r4,SPRN_HID0
	or	r5,r4,r3
	isync
	mtspr	SPRN_HID0,r5
	sync
	isync
	ori	r5,r4,HID0_ICE	/* Enable cache */
	mtspr	SPRN_HID0,r5
	sync
	isync
#endif
	mtlr	r6
	blr

#define NUM_CACHE_LINES 128*8
#define cache_flush_buffer 0x1000

/*
 * Flush data cache
 * Do this by just reading lots of stuff into the cache.
 */
        .globl flush_data_cache
flush_data_cache:       
	lis	r3,cache_flush_buffer@h
	ori	r3,r3,cache_flush_buffer@l
	li	r4,NUM_CACHE_LINES
	mtctr	r4
00:	lwz	r4,0(r3)
	addi	r3,r3,L1_CACHE_BYTES	/* Next line, please */
	bdnz	00b
10:	blr

	.previous

