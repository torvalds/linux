/*
 * relocate_kernel.S - put the kernel image in place to boot
 * Copyright (C) 2002-2003 Eric Biederman  <ebiederm@xmission.com>
 *
 * GameCube/ppc32 port Copyright (C) 2004 Albert Herranz
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <asm/reg.h>
#include <asm/ppc_asm.h>
#include <asm/processor.h>

#include <asm/kexec.h>

#define PAGE_SIZE      4096 /* must be same value as in <asm/page.h> */

	/*
	 * Must be relocatable PIC code callable as a C function.
	 */
	.globl relocate_new_kernel
relocate_new_kernel:
	/* r3 = page_list   */
	/* r4 = reboot_code_buffer */
	/* r5 = start_address      */

	li	r0, 0

	/*
	 * Set Machine Status Register to a known status,
	 * switch the MMU off and jump to 1: in a single step.
	 */

	mr	r8, r0
	ori     r8, r8, MSR_RI|MSR_ME
	mtspr	SPRN_SRR1, r8
	addi	r8, r4, 1f - relocate_new_kernel
	mtspr	SPRN_SRR0, r8
	sync
	rfi

1:
	/* from this point address translation is turned off */
	/* and interrupts are disabled */

	/* set a new stack at the bottom of our page... */
	/* (not really needed now) */
	addi	r1, r4, KEXEC_CONTROL_CODE_SIZE - 8 /* for LR Save+Back Chain */
	stw	r0, 0(r1)

	/* Do the copies */
	li	r6, 0 /* checksum */
	mr	r0, r3
	b	1f

0:	/* top, read another word for the indirection page */
	lwzu	r0, 4(r3)

1:
	/* is it a destination page? (r8) */
	rlwinm.	r7, r0, 0, 31, 31 /* IND_DESTINATION (1<<0) */
	beq	2f

	rlwinm	r8, r0, 0, 0, 19 /* clear kexec flags, page align */
	b	0b

2:	/* is it an indirection page? (r3) */
	rlwinm.	r7, r0, 0, 30, 30 /* IND_INDIRECTION (1<<1) */
	beq	2f

	rlwinm	r3, r0, 0, 0, 19 /* clear kexec flags, page align */
	subi	r3, r3, 4
	b	0b

2:	/* are we done? */
	rlwinm.	r7, r0, 0, 29, 29 /* IND_DONE (1<<2) */
	beq	2f
	b	3f

2:	/* is it a source page? (r9) */
	rlwinm.	r7, r0, 0, 28, 28 /* IND_SOURCE (1<<3) */
	beq	0b

	rlwinm	r9, r0, 0, 0, 19 /* clear kexec flags, page align */

	li	r7, PAGE_SIZE / 4
	mtctr   r7
	subi    r9, r9, 4
	subi    r8, r8, 4
9:
	lwzu    r0, 4(r9)  /* do the copy */
	xor	r6, r6, r0
	stwu    r0, 4(r8)
	dcbst	0, r8
	sync
	icbi	0, r8
	bdnz    9b

	addi    r9, r9, 4
	addi    r8, r8, 4
	b	0b

3:

	/* To be certain of avoiding problems with self-modifying code
	 * execute a serializing instruction here.
	 */
	isync
	sync

	/* jump to the entry point, usually the setup routine */
	mtlr	r5
	blrl

1:	b	1b

relocate_new_kernel_end:

	.globl relocate_new_kernel_size
relocate_new_kernel_size:
	.long relocate_new_kernel_end - relocate_new_kernel

