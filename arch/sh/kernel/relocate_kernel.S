/*
 * relocate_kernel.S - put the kernel image in place to boot
 * 2005.9.17 kogiidena@eggplant.ddo.jp
 *
 * LANDISK/sh4 is supported. Maybe, SH archtecture works well.
 *
 * 2009-03-18 Magnus Damm - Added Kexec Jump support
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */
#include <linux/linkage.h>
#include <asm/addrspace.h>
#include <asm/page.h>

		.globl relocate_new_kernel
relocate_new_kernel:
	/* r4 = indirection_page   */
	/* r5 = reboot_code_buffer */
	/* r6 = start_address      */

	mov.l	10f, r0		/* PAGE_SIZE */
	add	r5, r0		/* setup new stack at end of control page */

	/* save r15->r8 to new stack */
	mov.l	r15, @-r0
	mov	r0, r15
	mov.l	r14, @-r15
	mov.l	r13, @-r15
	mov.l	r12, @-r15
	mov.l	r11, @-r15
	mov.l	r10, @-r15
	mov.l	r9, @-r15
	mov.l	r8, @-r15

	/* save other random registers */
	sts.l	macl, @-r15
	sts.l	mach, @-r15
	stc.l	gbr, @-r15
	stc.l	ssr, @-r15
	stc.l	sr, @-r15
	sts.l	pr, @-r15
	stc.l	spc, @-r15

	/* switch to bank1 and save r7->r0 */
	mov.l	12f, r9
	stc	sr, r8
	or	r9, r8
	ldc	r8, sr
	mov.l	r7, @-r15
	mov.l	r6, @-r15
	mov.l	r5, @-r15
	mov.l	r4, @-r15
	mov.l	r3, @-r15
	mov.l	r2, @-r15
	mov.l	r1, @-r15
	mov.l	r0, @-r15

	/* switch to bank0 and save r7->r0 */
	mov.l	12f, r9
	not	r9, r9
	stc	sr, r8
	and	r9, r8
	ldc	r8, sr
	mov.l	r7, @-r15
	mov.l	r6, @-r15
	mov.l	r5, @-r15
	mov.l	r4, @-r15
	mov.l	r3, @-r15
	mov.l	r2, @-r15
	mov.l	r1, @-r15
	mov.l	r0, @-r15

	mov.l	r4, @-r15	/* save indirection page again */

	bsr	swap_pages	/* swap pages before jumping to new kernel */
	 nop

	mova	11f, r0
	mov.l	r15, @r0	/* save pointer to stack */

	jsr	@r6		/* hand over control to new kernel */
	 nop

	mov.l	11f, r15	/* get pointer to stack */
	mov.l	@r15+, r4	/* restore r4 to get indirection page */

	bsr	swap_pages	/* swap pages back to previous state */
	 nop

	/* make sure bank0 is active and restore r0->r7 */
	mov.l	12f, r9
	not	r9, r9
	stc	sr, r8
	and	r9, r8
	ldc	r8, sr
	mov.l	@r15+, r0
	mov.l	@r15+, r1
	mov.l	@r15+, r2
	mov.l	@r15+, r3
	mov.l	@r15+, r4
	mov.l	@r15+, r5
	mov.l	@r15+, r6
	mov.l	@r15+, r7

	/* switch to bank1 and restore r0->r7 */
	mov.l	12f, r9
	stc	sr, r8
	or	r9, r8
	ldc	r8, sr
	mov.l	@r15+, r0
	mov.l	@r15+, r1
	mov.l	@r15+, r2
	mov.l	@r15+, r3
	mov.l	@r15+, r4
	mov.l	@r15+, r5
	mov.l	@r15+, r6
	mov.l	@r15+, r7

	/* switch back to bank0 */
	mov.l	12f, r9
	not	r9, r9
	stc	sr, r8
	and	r9, r8
	ldc	r8, sr

	/* restore other random registers */
	ldc.l	@r15+, spc
	lds.l	@r15+, pr
	ldc.l	@r15+, sr
	ldc.l	@r15+, ssr
	ldc.l	@r15+, gbr
	lds.l	@r15+, mach
	lds.l	@r15+, macl

	/* restore r8->r15 */
	mov.l	@r15+, r8
	mov.l	@r15+, r9
	mov.l	@r15+, r10
	mov.l	@r15+, r11
	mov.l	@r15+, r12
	mov.l	@r15+, r13
	mov.l	@r15+, r14
	mov.l	@r15+, r15
	rts
	 nop

swap_pages:
	bra	1f
	 mov	r4,r0	  /* cmd = indirection_page */
0:
	mov.l	@r4+,r0	  /* cmd = *ind++ */

1:	/* addr = cmd & 0xfffffff0 */
	mov	r0,r2
	mov	#-16,r1
	and	r1,r2

	/* if(cmd & IND_DESTINATION) dst = addr  */
	tst	#1,r0
	bt	2f
	bra	0b
	 mov	r2,r5

2:	/* else if(cmd & IND_INDIRECTION) ind = addr  */
	tst	#2,r0
	bt	3f
	bra	0b
	 mov	r2,r4

3:	/* else if(cmd & IND_DONE) return */
	tst	#4,r0
	bt	4f
	rts
	 nop

4:	/* else if(cmd & IND_SOURCE) memcpy(dst,addr,PAGE_SIZE) */
	tst	#8,r0
	bt	0b

	mov.l	10f,r3	  /* PAGE_SIZE */
	shlr2	r3
	shlr2	r3
5:
	dt	r3

	/* regular kexec just overwrites the destination page
	 * with the contents of the source page.
	 * for the kexec jump case we need to swap the contents
	 * of the pages.
	 * to keep it simple swap the contents for both cases.
	 */
	mov.l	@(0, r2), r8
	mov.l	@(0, r5), r1
	mov.l	r8, @(0, r5)
	mov.l	r1, @(0, r2)

	mov.l	@(4, r2), r8
	mov.l	@(4, r5), r1
	mov.l	r8, @(4, r5)
	mov.l	r1, @(4, r2)

	mov.l	@(8, r2), r8
	mov.l	@(8, r5), r1
	mov.l	r8, @(8, r5)
	mov.l	r1, @(8, r2)

	mov.l	@(12, r2), r8
	mov.l	@(12, r5), r1
	mov.l	r8, @(12, r5)
	mov.l	r1, @(12, r2)

	add	#16,r5
	add	#16,r2
	bf	5b

	bra	0b
	 nop

	.align 2
10:
	.long	PAGE_SIZE
11:
	.long	0
12:
	.long	0x20000000 ! RB=1

relocate_new_kernel_end:

	.globl relocate_new_kernel_size
relocate_new_kernel_size:
	.long relocate_new_kernel_end - relocate_new_kernel
