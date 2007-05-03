/*
 * PowerPC 64-bit swsusp implementation
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPLv2
 */

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
#define SL_r1		0x00	/* stack pointer */
#define SL_PC		0x08
#define SL_MSR		0x10
#define SL_SDR1		0x18
#define SL_XER		0x20
#define SL_TB		0x40
#define SL_r2		0x48
#define SL_CR		0x50
#define SL_LR		0x58
#define SL_r12		0x60
#define SL_r13		0x68
#define SL_r14		0x70
#define SL_r15		0x78
#define SL_r16		0x80
#define SL_r17		0x88
#define SL_r18		0x90
#define SL_r19		0x98
#define SL_r20		0xa0
#define SL_r21		0xa8
#define SL_r22		0xb0
#define SL_r23		0xb8
#define SL_r24		0xc0
#define SL_r25		0xc8
#define SL_r26		0xd0
#define SL_r27		0xd8
#define SL_r28		0xe0
#define SL_r29		0xe8
#define SL_r30		0xf0
#define SL_r31		0xf8
#define SL_SIZE		SL_r31+8

/* these macros rely on the save area being
 * pointed to by r11 */
#define SAVE_SPECIAL(special)		\
	mf##special	r0		;\
	std	r0, SL_##special(r11)
#define RESTORE_SPECIAL(special)	\
	ld	r0, SL_##special(r11)	;\
	mt##special	r0
#define SAVE_REGISTER(reg)		\
	std	reg, SL_##reg(r11)
#define RESTORE_REGISTER(reg)		\
	ld	reg, SL_##reg(r11)

/* space for storing cpu state */
	.section .data
	.align  5
swsusp_save_area:
	.space SL_SIZE

	.section ".toc","aw"
swsusp_save_area_ptr:
	.tc	swsusp_save_area[TC],swsusp_save_area
restore_pblist_ptr:
	.tc	restore_pblist[TC],restore_pblist

	.section .text
	.align  5
_GLOBAL(swsusp_arch_suspend)
	ld	r11,swsusp_save_area_ptr@toc(r2)
	SAVE_SPECIAL(LR)
	SAVE_REGISTER(r1)
	SAVE_SPECIAL(CR)
	SAVE_SPECIAL(TB)
	SAVE_REGISTER(r2)
	SAVE_REGISTER(r12)
	SAVE_REGISTER(r13)
	SAVE_REGISTER(r14)
	SAVE_REGISTER(r15)
	SAVE_REGISTER(r16)
	SAVE_REGISTER(r17)
	SAVE_REGISTER(r18)
	SAVE_REGISTER(r19)
	SAVE_REGISTER(r20)
	SAVE_REGISTER(r21)
	SAVE_REGISTER(r22)
	SAVE_REGISTER(r23)
	SAVE_REGISTER(r24)
	SAVE_REGISTER(r25)
	SAVE_REGISTER(r26)
	SAVE_REGISTER(r27)
	SAVE_REGISTER(r28)
	SAVE_REGISTER(r29)
	SAVE_REGISTER(r30)
	SAVE_REGISTER(r31)
	SAVE_SPECIAL(MSR)
	SAVE_SPECIAL(SDR1)
	SAVE_SPECIAL(XER)

	/* we push the stack up 128 bytes but don't store the
	 * stack pointer on the stack like a real stackframe */
	addi	r1,r1,-128

	bl _iommu_save
	bl swsusp_save

	/* restore LR */
	ld	r11,swsusp_save_area_ptr@toc(r2)
	RESTORE_SPECIAL(LR)
	addi	r1,r1,128

	blr

/* Resume code */
_GLOBAL(swsusp_arch_resume)
	/* Stop pending alitvec streams and memory accesses */
BEGIN_FTR_SECTION
	DSSALL
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
	sync

	ld	r12,restore_pblist_ptr@toc(r2)
	ld	r12,0(r12)

	cmpdi	r12,0
	beq-	nothing_to_copy
	li	r15,512
copyloop:
	ld	r13,pbe_address(r12)
	ld	r14,pbe_orig_address(r12)

	mtctr	r15
	li	r10,0
copy_page_loop:
	ldx	r0,r10,r13
	stdx	r0,r10,r14
	addi	r10,r10,8
	bdnz copy_page_loop

	ld	r12,pbe_next(r12)
	cmpdi	r12,0
	bne+	copyloop
nothing_to_copy:

	/* flush caches */
	lis	r3, 0x10
	mtctr	r3
	li	r3, 0
	ori	r3, r3, CONFIG_KERNEL_START>>48
	li	r0, 48
	sld	r3, r3, r0
	li	r0, 0
1:
	dcbf	r0,r3
	addi	r3,r3,0x20
	bdnz	1b

	sync

	tlbia

	ld	r11,swsusp_save_area_ptr@toc(r2)

	RESTORE_SPECIAL(CR)

	/* restore timebase */
	/* load saved tb */
	ld	r1, SL_TB(r11)
	/* get upper 32 bits of it */
	srdi	r2, r1, 32
	/* clear tb lower to avoid wrap */
	li	r0, 0
	mttbl	r0
	/* set tb upper */
	mttbu	r2
	/* set tb lower */
	mttbl	r1

	/* restore registers */
	RESTORE_REGISTER(r1)
	RESTORE_REGISTER(r2)
	RESTORE_REGISTER(r12)
	RESTORE_REGISTER(r13)
	RESTORE_REGISTER(r14)
	RESTORE_REGISTER(r15)
	RESTORE_REGISTER(r16)
	RESTORE_REGISTER(r17)
	RESTORE_REGISTER(r18)
	RESTORE_REGISTER(r19)
	RESTORE_REGISTER(r20)
	RESTORE_REGISTER(r21)
	RESTORE_REGISTER(r22)
	RESTORE_REGISTER(r23)
	RESTORE_REGISTER(r24)
	RESTORE_REGISTER(r25)
	RESTORE_REGISTER(r26)
	RESTORE_REGISTER(r27)
	RESTORE_REGISTER(r28)
	RESTORE_REGISTER(r29)
	RESTORE_REGISTER(r30)
	RESTORE_REGISTER(r31)
	/* can't use RESTORE_SPECIAL(MSR) */
	ld	r0, SL_MSR(r11)
	mtmsrd	r0, 0
	RESTORE_SPECIAL(SDR1)
	RESTORE_SPECIAL(XER)

	sync

	addi	r1,r1,-128
	bl	slb_flush_and_rebolt
	bl	do_after_copyback
	addi	r1,r1,128

	ld	r11,swsusp_save_area_ptr@toc(r2)
	RESTORE_SPECIAL(LR)

	li	r3, 0
	blr
