/* SPDX-License-Identifier: GPL-2.0 */
/*
 * relocate_kernel.S - put the kernel image in place to boot
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/kexec.h>

	.align	3	/* not needed for this code, but keeps fncpy() happy */

ENTRY(relocate_new_kernel)

	adr	r7, relocate_new_kernel_end
	ldr	r0, [r7, #KEXEC_INDIR_PAGE]
	ldr	r1, [r7, #KEXEC_START_ADDR]

	/*
	 * If there is no indirection page (we are doing crashdumps)
	 * skip any relocation.
	 */
	cmp	r0, #0
	beq	2f

0:	/* top, read another word for the indirection page */
	ldr	r3, [r0],#4

	/* Is it a destination page. Put destination address to r4 */
	tst	r3,#1
	beq	1f
	bic	r4,r3,#1
	b	0b
1:
	/* Is it an indirection page */
	tst	r3,#2
	beq	1f
	bic	r0,r3,#2
	b	0b
1:

	/* are we done ? */
	tst	r3,#4
	beq	1f
	b	2f

1:
	/* is it source ? */
	tst	r3,#8
	beq	0b
	bic r3,r3,#8
	mov r6,#1024
9:
	ldr r5,[r3],#4
	str r5,[r4],#4
	subs r6,r6,#1
	bne 9b
	b 0b

2:
	/* Jump to relocated kernel */
	mov	lr, r1
	mov	r0, #0
	ldr	r1, [r7, #KEXEC_MACH_TYPE]
	ldr	r2, [r7, #KEXEC_R2]
 ARM(	ret	lr	)
 THUMB(	bx	lr	)

ENDPROC(relocate_new_kernel)

	.align	3
relocate_new_kernel_end:

	.globl relocate_new_kernel_size
relocate_new_kernel_size:
	.long relocate_new_kernel_end - relocate_new_kernel


