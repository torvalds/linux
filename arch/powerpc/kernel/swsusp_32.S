#include <linux/config.h>
#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>


/*
 * Structure for storing CPU registers on the save area.
 */
#define SL_SP		0
#define SL_PC		4
#define SL_MSR		8
#define SL_SDR1		0xc
#define SL_SPRG0	0x10	/* 4 sprg's */
#define SL_DBAT0	0x20
#define SL_IBAT0	0x28
#define SL_DBAT1	0x30
#define SL_IBAT1	0x38
#define SL_DBAT2	0x40
#define SL_IBAT2	0x48
#define SL_DBAT3	0x50
#define SL_IBAT3	0x58
#define SL_TB		0x60
#define SL_R2		0x68
#define SL_CR		0x6c
#define SL_LR		0x70
#define SL_R12		0x74	/* r12 to r31 */
#define SL_SIZE		(SL_R12 + 80)

	.section .data
	.align	5

_GLOBAL(swsusp_save_area)
	.space	SL_SIZE


	.section .text
	.align	5

_GLOBAL(swsusp_arch_suspend)

	lis	r11,swsusp_save_area@h
	ori	r11,r11,swsusp_save_area@l

	mflr	r0
	stw	r0,SL_LR(r11)
	mfcr	r0
	stw	r0,SL_CR(r11)
	stw	r1,SL_SP(r11)
	stw	r2,SL_R2(r11)
	stmw	r12,SL_R12(r11)

	/* Save MSR & SDR1 */
	mfmsr	r4
	stw	r4,SL_MSR(r11)
	mfsdr1	r4
	stw	r4,SL_SDR1(r11)

	/* Get a stable timebase and save it */
1:	mftbu	r4
	stw	r4,SL_TB(r11)
	mftb	r5
	stw	r5,SL_TB+4(r11)
	mftbu	r3
	cmpw	r3,r4
	bne	1b

	/* Save SPRGs */
	mfsprg	r4,0
	stw	r4,SL_SPRG0(r11)
	mfsprg	r4,1
	stw	r4,SL_SPRG0+4(r11)
	mfsprg	r4,2
	stw	r4,SL_SPRG0+8(r11)
	mfsprg	r4,3
	stw	r4,SL_SPRG0+12(r11)

	/* Save BATs */
	mfdbatu	r4,0
	stw	r4,SL_DBAT0(r11)
	mfdbatl	r4,0
	stw	r4,SL_DBAT0+4(r11)
	mfdbatu	r4,1
	stw	r4,SL_DBAT1(r11)
	mfdbatl	r4,1
	stw	r4,SL_DBAT1+4(r11)
	mfdbatu	r4,2
	stw	r4,SL_DBAT2(r11)
	mfdbatl	r4,2
	stw	r4,SL_DBAT2+4(r11)
	mfdbatu	r4,3
	stw	r4,SL_DBAT3(r11)
	mfdbatl	r4,3
	stw	r4,SL_DBAT3+4(r11)
	mfibatu	r4,0
	stw	r4,SL_IBAT0(r11)
	mfibatl	r4,0
	stw	r4,SL_IBAT0+4(r11)
	mfibatu	r4,1
	stw	r4,SL_IBAT1(r11)
	mfibatl	r4,1
	stw	r4,SL_IBAT1+4(r11)
	mfibatu	r4,2
	stw	r4,SL_IBAT2(r11)
	mfibatl	r4,2
	stw	r4,SL_IBAT2+4(r11)
	mfibatu	r4,3
	stw	r4,SL_IBAT3(r11)
	mfibatl	r4,3
	stw	r4,SL_IBAT3+4(r11)

#if  0
	/* Backup various CPU config stuffs */
	bl	__save_cpu_setup
#endif
	/* Call the low level suspend stuff (we should probably have made
	 * a stackframe...
	 */
	bl	swsusp_save

	/* Restore LR from the save area */
	lis	r11,swsusp_save_area@h
	ori	r11,r11,swsusp_save_area@l
	lwz	r0,SL_LR(r11)
	mtlr	r0

	blr


/* Resume code */
_GLOBAL(swsusp_arch_resume)

	/* Stop pending alitvec streams and memory accesses */
BEGIN_FTR_SECTION
	DSSALL
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
 	sync

	/* Disable MSR:DR to make sure we don't take a TLB or
	 * hash miss during the copy, as our hash table will
	 * for a while be unuseable. For .text, we assume we are
	 * covered by a BAT. This works only for non-G5 at this
	 * point. G5 will need a better approach, possibly using
	 * a small temporary hash table filled with large mappings,
	 * disabling the MMU completely isn't a good option for
	 * performance reasons.
	 * (Note that 750's may have the same performance issue as
	 * the G5 in this case, we should investigate using moving
	 * BATs for these CPUs)
	 */
	mfmsr	r0
	sync
	rlwinm	r0,r0,0,28,26		/* clear MSR_DR */
	mtmsr	r0
	sync
	isync

	/* Load ptr the list of pages to copy in r3 */
	lis	r11,(pagedir_nosave - KERNELBASE)@h
	ori	r11,r11,pagedir_nosave@l
	lwz	r10,0(r11)

	/* Copy the pages. This is a very basic implementation, to
	 * be replaced by something more cache efficient */
1:
	tophys(r3,r10)
	li	r0,256
	mtctr	r0
	lwz	r11,pbe_address(r3)	/* source */
	tophys(r5,r11)
	lwz	r10,pbe_orig_address(r3)	/* destination */
	tophys(r6,r10)
2:
	lwz	r8,0(r5)
	lwz	r9,4(r5)
	lwz	r10,8(r5)
	lwz	r11,12(r5)
	addi	r5,r5,16
	stw	r8,0(r6)
	stw	r9,4(r6)
	stw	r10,8(r6)
	stw	r11,12(r6)
	addi	r6,r6,16
	bdnz	2b
	lwz		r10,pbe_next(r3)
	cmpwi	0,r10,0
	bne	1b

	/* Do a very simple cache flush/inval of the L1 to ensure
	 * coherency of the icache
	 */
	lis	r3,0x0002
	mtctr	r3
	li	r3, 0
1:
	lwz	r0,0(r3)
	addi	r3,r3,0x0020
	bdnz	1b
	isync
	sync

	/* Now flush those cache lines */
	lis	r3,0x0002
	mtctr	r3
	li	r3, 0
1:
	dcbf	0,r3
	addi	r3,r3,0x0020
	bdnz	1b
	sync

	/* Ok, we are now running with the kernel data of the old
	 * kernel fully restored. We can get to the save area
	 * easily now. As for the rest of the code, it assumes the
	 * loader kernel and the booted one are exactly identical
	 */
	lis	r11,swsusp_save_area@h
	ori	r11,r11,swsusp_save_area@l
	tophys(r11,r11)

#if 0
	/* Restore various CPU config stuffs */
	bl	__restore_cpu_setup
#endif
	/* Restore the BATs, and SDR1.  Then we can turn on the MMU.
	 * This is a bit hairy as we are running out of those BATs,
	 * but first, our code is probably in the icache, and we are
	 * writing the same value to the BAT, so that should be fine,
	 * though a better solution will have to be found long-term
	 */
	lwz	r4,SL_SDR1(r11)
	mtsdr1	r4
	lwz	r4,SL_SPRG0(r11)
	mtsprg	0,r4
	lwz	r4,SL_SPRG0+4(r11)
	mtsprg	1,r4
	lwz	r4,SL_SPRG0+8(r11)
	mtsprg	2,r4
	lwz	r4,SL_SPRG0+12(r11)
	mtsprg	3,r4

#if 0
	lwz	r4,SL_DBAT0(r11)
	mtdbatu	0,r4
	lwz	r4,SL_DBAT0+4(r11)
	mtdbatl	0,r4
	lwz	r4,SL_DBAT1(r11)
	mtdbatu	1,r4
	lwz	r4,SL_DBAT1+4(r11)
	mtdbatl	1,r4
	lwz	r4,SL_DBAT2(r11)
	mtdbatu	2,r4
	lwz	r4,SL_DBAT2+4(r11)
	mtdbatl	2,r4
	lwz	r4,SL_DBAT3(r11)
	mtdbatu	3,r4
	lwz	r4,SL_DBAT3+4(r11)
	mtdbatl	3,r4
	lwz	r4,SL_IBAT0(r11)
	mtibatu	0,r4
	lwz	r4,SL_IBAT0+4(r11)
	mtibatl	0,r4
	lwz	r4,SL_IBAT1(r11)
	mtibatu	1,r4
	lwz	r4,SL_IBAT1+4(r11)
	mtibatl	1,r4
	lwz	r4,SL_IBAT2(r11)
	mtibatu	2,r4
	lwz	r4,SL_IBAT2+4(r11)
	mtibatl	2,r4
	lwz	r4,SL_IBAT3(r11)
	mtibatu	3,r4
	lwz	r4,SL_IBAT3+4(r11)
	mtibatl	3,r4
#endif

BEGIN_FTR_SECTION
	li	r4,0
	mtspr	SPRN_DBAT4U,r4
	mtspr	SPRN_DBAT4L,r4
	mtspr	SPRN_DBAT5U,r4
	mtspr	SPRN_DBAT5L,r4
	mtspr	SPRN_DBAT6U,r4
	mtspr	SPRN_DBAT6L,r4
	mtspr	SPRN_DBAT7U,r4
	mtspr	SPRN_DBAT7L,r4
	mtspr	SPRN_IBAT4U,r4
	mtspr	SPRN_IBAT4L,r4
	mtspr	SPRN_IBAT5U,r4
	mtspr	SPRN_IBAT5L,r4
	mtspr	SPRN_IBAT6U,r4
	mtspr	SPRN_IBAT6L,r4
	mtspr	SPRN_IBAT7U,r4
	mtspr	SPRN_IBAT7L,r4
END_FTR_SECTION_IFSET(CPU_FTR_HAS_HIGH_BATS)

	/* Flush all TLBs */
	lis	r4,0x1000
1:	addic.	r4,r4,-0x1000
	tlbie	r4
	blt	1b
	sync

	/* restore the MSR and turn on the MMU */
	lwz	r3,SL_MSR(r11)
	bl	turn_on_mmu
	tovirt(r11,r11)

	/* Restore TB */
	li	r3,0
	mttbl	r3
	lwz	r3,SL_TB(r11)
	lwz	r4,SL_TB+4(r11)
	mttbu	r3
	mttbl	r4

	/* Kick decrementer */
	li	r0,1
	mtdec	r0

	/* Restore the callee-saved registers and return */
	lwz	r0,SL_CR(r11)
	mtcr	r0
	lwz	r2,SL_R2(r11)
	lmw	r12,SL_R12(r11)
	lwz	r1,SL_SP(r11)
	lwz	r0,SL_LR(r11)
	mtlr	r0

	// XXX Note: we don't really need to call swsusp_resume

	li	r3,0
	blr

/* FIXME:This construct is actually not useful since we don't shut
 * down the instruction MMU, we could just flip back MSR-DR on.
 */
turn_on_mmu:
	mflr	r4
	mtsrr0	r4
	mtsrr1	r3
	sync
	isync
	rfi

