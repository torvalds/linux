/*
 * linux/arch/unicore32/kernel/sleep.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/hardware.h>

		.text

pkunity_cpu_save_cp:

	@ get coprocessor registers

	movc	r3, p0.c7, #0			@ PID
	movc	r4, p0.c2, #0			@ translation table base addr
	movc	r5, p0.c1, #0			@ control reg


	@ store them plus current virtual stack ptr on stack
	mov	r6, sp
	stm.w	(r3 - r6), [sp-]

	mov	pc, lr

pkunity_cpu_save_sp:
	@ preserve phys address of stack
	mov	r0, sp
	stw.w	lr, [sp+], #-4
	b.l	sleep_phys_sp
	ldw	r1, =sleep_save_sp
	stw	r0, [r1]
	ldw.w	pc, [sp]+, #4

/*
 * puv3_cpu_suspend()
 *
 * Forces CPU into sleep state.
 *
 * r0 = value for PWRMODE M field for desired sleep state
 */

ENTRY(puv3_cpu_suspend)
	stm.w	(r16 - r27, lr), [sp-]		@ save registers on stack
	stm.w	(r4 - r15), [sp-]		@ save registers on stack

#ifdef	CONFIG_UNICORE_FPU_F64
	sfm.w	(f0  - f7 ), [sp-]
	sfm.w	(f8  - f15), [sp-]
	sfm.w	(f16 - f23), [sp-]
	sfm.w	(f24 - f31), [sp-]
	cff	r4, s31
	stm.w	(r4), [sp-]
#endif
	b.l	pkunity_cpu_save_cp

	b.l	pkunity_cpu_save_sp

	@ clean data cache
	mov	r1, #0
	movc	p0.c5, r1, #14
	nop
	nop
	nop
	nop



	@ DDR2 BaseAddr
	ldw	r0, =(PKUNITY_DDR2CTRL_BASE)

	@ PM BaseAddr
	ldw	r1, =(PKUNITY_PM_BASE)

	@ set PLL_SYS_CFG reg, 275
	movl	r6, #0x00002401
	stw	r6, [r1+], #0x18
	@ set PLL_DDR_CFG reg, 66MHz
	movl	r6, #0x00100c00
	stw	r6, [r1+], #0x1c

	@ set wake up source
	movl	r8, #0x800001ff		@ epip4d
	stw	r8, [r1+], #0xc

	@ set PGSR
	movl	r5, #0x40000
	stw	r5, [r1+], #0x10

	@ prepare DDR2 refresh settings
	ldw	r5, [r0+], #0x24
	or	r5, r5, #0x00000001

	@ prepare PMCR for PLL changing
	movl	r6, #0xc

	@ prepare for closing PLL
	movl	r7, #0x1

	@ prepare sleep mode
	mov	r8, #0x1

@	movl	r0, 0x11111111
@	put_word_ocd r0
	b	pkunity_cpu_do_suspend

	.ltorg
	.align	5
pkunity_cpu_do_suspend:
	b	101f
	@ put DDR2 into self-refresh
100:	stw	r5, [r0+], #0x24
	@ change PLL
	stw	r6, [r1]
	b	1f

	.ltorg
	.align	5
101:	b	102f
	@ wait for PLL changing complete
1:	ldw	r6, [r1+], #0x44
	csub.a	r6, #0x1
	bne	1b
	b	2f

	.ltorg
	.align	5
102:	b	100b
	@ close PLL
2:	stw	r7, [r1+], #0x4
	@ enter sleep mode
	stw	r8, [r1]
3:	b	3b




/*
 * puv3_cpu_resume()
 *
 * entry point from bootloader into kernel during resume
 *
 * Note: Yes, part of the following code is located into the .data section.
 *       This is to allow sleep_save_sp to be accessed with a relative load
 *       while we can't rely on any MMU translation.  We could have put
 *       sleep_save_sp in the .text section as well, but some setups might
 *       insist on it to be truly read-only.
 */

	.data
	.align 5
ENTRY(puv3_cpu_resume)
@	movl	r0, 0x20202020
@	put_word_ocd r0

	ldw	r0, sleep_save_sp		@ stack phys addr
	ldw	r2, =resume_after_mmu		@ its absolute virtual address
	ldm	(r3 - r6), [r0]+		@ CP regs + virt stack ptr
	mov	sp, r6				@ CP regs + virt stack ptr

	mov	r1, #0
	movc	p0.c6, r1, #6			@ invalidate I & D TLBs
	movc	p0.c5, r1, #28			@ invalidate I & D caches, BTB

	movc	p0.c7, r3, #0			@ PID
	movc	p0.c2, r4, #0			@ translation table base addr
	movc	p0.c1, r5, #0			@ control reg, turn on mmu
	nop
	jump	r2
	nop
	nop
	nop
	nop
	nop

sleep_save_sp:
	.word	0				@ preserve stack phys ptr here

	.text
resume_after_mmu:
@	movl	r0, 0x30303030
@	put_word_ocd r0

#ifdef	CONFIG_UNICORE_FPU_F64
	lfm.w	(f0  - f7 ), [sp]+
	lfm.w	(f8  - f15), [sp]+
	lfm.w	(f16 - f23), [sp]+
	lfm.w	(f24 - f31), [sp]+
	ldm.w	(r4), [sp]+
	ctf	r4, s31
#endif
	ldm.w	(r4 - r15), [sp]+		@ restore registers from stack
	ldm.w	(r16 - r27, pc), [sp]+		@ return to caller
