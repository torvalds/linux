/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/kernel/hibernate_asm.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 */

#include <linux/sys.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <generated/asm-offsets.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/assembler.h>

@ restore_image(pgd_t *resume_pg_dir, struct pbe *restore_pblist)
@ r0: resume_pg_dir
@ r1: restore_pblist
@ copy restore_pblist pages
@ restore registers from swsusp_arch_regs_cpu0
@
ENTRY(restore_image)
	sub	r0, r0, #PAGE_OFFSET
	mov	r5, #0
	movc	p0.c6, r5, #6	@invalidate ITLB & DTLB
	movc	p0.c2, r0, #0
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	.p2align 4,,7
101:
	csub.a	r1, #0
	beq	109f

	ldw	r6, [r1+], #PBE_ADDRESS
	ldw	r7, [r1+], #PBE_ORIN_ADDRESS

	movl	ip, #128
102:	ldm.w	(r8 - r15), [r6]+
	stm.w	(r8 - r15), [r7]+
	sub.a	ip, ip, #1
	bne	102b

	ldw	r1, [r1+], #PBE_NEXT
	b	101b

	.p2align 4,,7
109:
	/* go back to the original page tables */
	ldw	r0, =swapper_pg_dir
	sub	r0, r0, #PAGE_OFFSET
	mov	r5, #0
	movc	p0.c6, r5, #6
	movc	p0.c2, r0, #0
	nop
	nop
	nop
	nop
	nop
	nop
	nop

#ifdef	CONFIG_UNICORE_FPU_F64
	ldw	ip, 1f
	add	ip, ip, #SWSUSP_FPSTATE
	lfm.w	(f0  - f7 ), [ip]+
	lfm.w	(f8  - f15), [ip]+
	lfm.w	(f16 - f23), [ip]+
	lfm.w	(f24 - f31), [ip]+
	ldw	r4, [ip]
	ctf	r4, s31
#endif
	mov	r0, #0x0
	ldw	ip, 1f
	add	ip, ip, #SWSUSP_CPU
	ldm.w	(r4 - r15), [ip]+
	ldm	(r16 - r27, sp, pc), [ip]+	@ Load all regs saved previously

	.align	2
1:	.long	swsusp_arch_regs_cpu0


@ swsusp_arch_suspend()
@ - prepare pc for resume, return from function without swsusp_save on resume
@ - save registers in swsusp_arch_regs_cpu0
@ - call swsusp_save write suspend image

ENTRY(swsusp_arch_suspend)
	ldw	ip, 1f
	add	ip, ip, #SWSUSP_CPU
	stm.w	(r4 - r15), [ip]+
	stm.w	(r16 - r27, sp, lr), [ip]+

#ifdef	CONFIG_UNICORE_FPU_F64
	ldw	ip, 1f
	add	ip, ip, #SWSUSP_FPSTATE
	sfm.w	(f0  - f7 ), [ip]+
	sfm.w	(f8  - f15), [ip]+
	sfm.w	(f16 - f23), [ip]+
	sfm.w	(f24 - f31), [ip]+
	cff	r4, s31
	stw	r4, [ip]
#endif
	b	swsusp_save			@ no return

1:	.long	swsusp_arch_regs_cpu0
