/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/boot/head.S
 *
 * initial bootloader stuff..
 */

#include <asm/pal.h>

	.set noreorder
	.globl	__start
	.ent	__start
__start:
	br	$29,2f
2:	ldgp	$29,0($29)
	jsr	$26,start_kernel
	call_pal PAL_halt
	.end __start

	.align 5
	.globl	wrent
	.ent	wrent
wrent:
	.prologue 0
	call_pal PAL_wrent
	ret ($26)
	.end wrent

	.align 5
	.globl	wrkgp
	.ent	wrkgp
wrkgp:
	.prologue 0
	call_pal PAL_wrkgp
	ret ($26)
	.end wrkgp

	.align 5
	.globl	switch_to_osf_pal
	.ent	switch_to_osf_pal
switch_to_osf_pal:
	subq	$30,128,$30
	.frame	$30,128,$26
	stq	$26,0($30)
	stq	$1,8($30)
	stq	$2,16($30)
	stq	$3,24($30)
	stq	$4,32($30)
	stq	$5,40($30)
	stq	$6,48($30)
	stq	$7,56($30)
	stq	$8,64($30)
	stq	$9,72($30)
	stq	$10,80($30)
	stq	$11,88($30)
	stq	$12,96($30)
	stq	$13,104($30)
	stq	$14,112($30)
	stq	$15,120($30)
	.prologue 0

	stq	$30,0($17)	/* save KSP in PCB */

	bis	$30,$30,$20	/* a4 = KSP */
	br	$17,1f

	ldq	$26,0($30)
	ldq	$1,8($30)
	ldq	$2,16($30)
	ldq	$3,24($30)
	ldq	$4,32($30)
	ldq	$5,40($30)
	ldq	$6,48($30)
	ldq	$7,56($30)
	ldq	$8,64($30)
	ldq	$9,72($30)
	ldq	$10,80($30)
	ldq	$11,88($30)
	ldq	$12,96($30)
	ldq	$13,104($30)
	ldq	$14,112($30)
	ldq	$15,120($30)
	addq	$30,128,$30
	ret ($26)
1:	call_pal PAL_swppal
	.end	switch_to_osf_pal

	.align 3
	.globl	tbi
	.ent	tbi
tbi:
	.prologue 0
	call_pal PAL_tbi
	ret	($26)
	.end tbi

	.align 3
	.globl	halt
	.ent	halt
halt:
	.prologue 0
	call_pal PAL_halt
	.end halt

/* $16 - new stack page */
	.align 3
	.globl	move_stack
	.ent	move_stack
move_stack:
	.prologue 0
	lda	$0, 0x1fff($31)
	and	$0, $30, $1			/* Stack offset */
	or	$1, $16, $16			/* New stack pointer */
	mov	$30, $1
	mov	$16, $2
1:	ldq	$3, 0($1)			/* Move the stack */
	addq	$1, 8, $1
	stq	$3, 0($2)
	and	$0, $1, $4
	addq	$2, 8, $2
	bne	$4, 1b
	mov	$16, $30
	ret	($26)
	.end move_stack
