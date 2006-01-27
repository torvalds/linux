/*
 * relocate_kernel.S - put the kernel image in place to boot
 * 2005.9.17 kogiidena@eggplant.ddo.jp
 *
 * LANDISK/sh4 is supported. Maybe, SH archtecture works well.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/config.h>
#include <linux/linkage.h>

#define PAGE_SIZE      4096 /* must be same value as in <asm/page.h> */


		.globl relocate_new_kernel
relocate_new_kernel:
	/* r4 = indirection_page   */
	/* r5 = reboot_code_buffer */
	/* r6 = start_address      */
	/* r7 = vbr_reg            */

	mov.l	10f,r8	  /* 4096 */
	mov.l	11f,r9    /* 0xa0000000 */

	/*  stack setting */
	add	r8,r5
	mov	r5,r15

	bra	1f
	mov	r4,r0	  /* cmd = indirection_page */
0:
	mov.l	@r4+,r0	  /* cmd = *ind++ */

1:	/* addr = (cmd | 0xa0000000) & 0xfffffff0 */
	mov	r0,r2
	or	r9,r2
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

3:	/* else if(cmd & IND_DONE) goto 6  */
	tst	#4,r0
	bt	4f
	bra	6f
	nop

4:	/* else if(cmd & IND_SOURCE) memcpy(dst,addr,PAGE_SIZE) */
	tst	#8,r0
	bt	0b

	mov	r8,r3
	shlr2	r3
	shlr2	r3
5:
	dt	r3
	mov.l	@r2+,r1   /*  16n+0 */
	mov.l	r1,@r5
	add	#4,r5
	mov.l	@r2+,r1	  /*  16n+4 */
	mov.l	r1,@r5
	add	#4,r5
	mov.l	@r2+,r1   /*  16n+8 */
	mov.l	r1,@r5
	add	#4,r5
	mov.l	@r2+,r1   /*  16n+12 */
	mov.l	r1,@r5
	add	#4,r5
	bf	5b

	bra	0b
	nop
6:
#ifdef CONFIG_SH_STANDARD_BIOS
	ldc   r7, vbr
#endif
	jmp @r6
	nop

	.align 2
10:
	.long	PAGE_SIZE
11:
	.long	0xa0000000

relocate_new_kernel_end:

	.globl relocate_new_kernel_size
relocate_new_kernel_size:
	.long relocate_new_kernel_end - relocate_new_kernel
