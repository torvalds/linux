/*
 * Copyright (C) 2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009 PetaLogix
 * Copyright (C) 2007 LynuxWorks, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/page.h>

/* Loop unrolling for __copy_tofrom_user */
#define COPY(offset)	\
1:	lwi	r4 , r6, 0x0000 + offset;	\
2:	lwi	r19, r6, 0x0004 + offset;	\
3:	lwi	r20, r6, 0x0008 + offset;	\
4:	lwi	r21, r6, 0x000C + offset;	\
5:	lwi	r22, r6, 0x0010 + offset;	\
6:	lwi	r23, r6, 0x0014 + offset;	\
7:	lwi	r24, r6, 0x0018 + offset;	\
8:	lwi	r25, r6, 0x001C + offset;	\
9:	swi	r4 , r5, 0x0000 + offset;	\
10:	swi	r19, r5, 0x0004 + offset;	\
11:	swi	r20, r5, 0x0008 + offset;	\
12:	swi	r21, r5, 0x000C + offset;	\
13:	swi	r22, r5, 0x0010 + offset;	\
14:	swi	r23, r5, 0x0014 + offset;	\
15:	swi	r24, r5, 0x0018 + offset;	\
16:	swi	r25, r5, 0x001C + offset;	\
	.section __ex_table,"a";		\
	.word	1b, 33f;			\
	.word	2b, 33f;			\
	.word	3b, 33f;			\
	.word	4b, 33f;			\
	.word	5b, 33f;			\
	.word	6b, 33f;			\
	.word	7b, 33f;			\
	.word	8b, 33f;			\
	.word	9b, 33f;			\
	.word	10b, 33f;			\
	.word	11b, 33f;			\
	.word	12b, 33f;			\
	.word	13b, 33f;			\
	.word	14b, 33f;			\
	.word	15b, 33f;			\
	.word	16b, 33f;			\
	.text

#define COPY_80(offset)	\
	COPY(0x00 + offset);\
	COPY(0x20 + offset);\
	COPY(0x40 + offset);\
	COPY(0x60 + offset);

/*
 * int __copy_tofrom_user(char *to, char *from, int len)
 * Return:
 *   0 on success
 *   number of not copied bytes on error
 */
	.text
.globl __copy_tofrom_user;
.type  __copy_tofrom_user, @function
.align 4;
__copy_tofrom_user:
	/*
	 * r5 - to
	 * r6 - from
	 * r7, r3 - count
	 * r4 - tempval
	 */
	beqid	r7, 0f /* zero size is not likely */
	or	r3, r5, r6 /* find if is any to/from unaligned */
	or	r3, r3, r7 /* find if count is unaligned */
	andi	r3, r3, 0x3 /* mask last 3 bits */
	bneid	r3, bu1 /* if r3 is not zero then byte copying */
	or	r3, r0, r0

	rsubi	r3, r7, PAGE_SIZE /* detect PAGE_SIZE */
	beqid	r3, page;
	or	r3, r0, r0

w1:	lw	r4, r6, r3 /* at least one 4 byte copy */
w2:	sw	r4, r5, r3
	addik	r7, r7, -4
	bneid	r7, w1
	addik	r3, r3, 4
	addik	r3, r7, 0
	rtsd	r15, 8
	nop

	.section	__ex_table,"a"
	.word	w1, 0f;
	.word	w2, 0f;
	.text

.align 4 /* Alignment is important to keep icache happy */
page:	/* Create room on stack and save registers for storing values */
	addik   r1, r1, -40
	swi	r5, r1, 0
	swi	r6, r1, 4
	swi	r7, r1, 8
	swi	r19, r1, 12
	swi	r20, r1, 16
	swi	r21, r1, 20
	swi	r22, r1, 24
	swi	r23, r1, 28
	swi	r24, r1, 32
	swi	r25, r1, 36
loop:	/* r4, r19, r20, r21, r22, r23, r24, r25 are used for storing values */
	/* Loop unrolling to get performance boost */
	COPY_80(0x000);
	COPY_80(0x080);
	COPY_80(0x100);
	COPY_80(0x180);
	/* copy loop */
	addik   r6, r6, 0x200
	addik   r7, r7, -0x200
	bneid   r7, loop
	addik   r5, r5, 0x200

	/* Restore register content */
	lwi	r5, r1, 0
	lwi	r6, r1, 4
	lwi	r7, r1, 8
	lwi	r19, r1, 12
	lwi	r20, r1, 16
	lwi	r21, r1, 20
	lwi	r22, r1, 24
	lwi	r23, r1, 28
	lwi	r24, r1, 32
	lwi	r25, r1, 36
	addik   r1, r1, 40
	/* return back */
	addik	r3, r0, 0
	rtsd	r15, 8
	nop

/* Fault case - return temp count */
33:
	addik	r3, r7, 0
	/* Restore register content */
	lwi	r5, r1, 0
	lwi	r6, r1, 4
	lwi	r7, r1, 8
	lwi	r19, r1, 12
	lwi	r20, r1, 16
	lwi	r21, r1, 20
	lwi	r22, r1, 24
	lwi	r23, r1, 28
	lwi	r24, r1, 32
	lwi	r25, r1, 36
	addik   r1, r1, 40
	/* return back */
	rtsd	r15, 8
	nop

.align 4 /* Alignment is important to keep icache happy */
bu1:	lbu	r4,r6,r3
bu2:	sb	r4,r5,r3
	addik	r7,r7,-1
	bneid	r7,bu1
	addik	r3,r3,1		/* delay slot */
0:
	addik	r3,r7,0
	rtsd	r15,8
	nop
	.size   __copy_tofrom_user, . - __copy_tofrom_user

	.section	__ex_table,"a"
	.word	bu1, 0b;
	.word	bu2, 0b;
	.text
