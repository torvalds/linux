/*
 * arch/score/lib/csum_partial.S
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/linkage.h>

#define ADDC(sum,reg)			\
	add	sum, sum, reg;		\
	cmp.c	reg, sum;		\
	bleu	9f;			\
	addi	sum, 0x1;		\
9:

#define CSUM_BIGCHUNK(src, offset, sum)		\
	lw	r8, [src, offset + 0x00];	\
	lw	r9, [src, offset + 0x04];	\
	lw	r10, [src, offset + 0x08];	\
	lw	r11, [src, offset + 0x0c];	\
	ADDC(sum, r8);				\
	ADDC(sum, r9);				\
	ADDC(sum, r10);				\
	ADDC(sum, r11);				\
	lw	r8, [src, offset + 0x10];	\
	lw	r9, [src, offset + 0x14];	\
	lw	r10, [src, offset + 0x18]; 	\
	lw	r11, [src, offset + 0x1c]; 	\
	ADDC(sum, r8);				\
	ADDC(sum, r9);				\
	ADDC(sum, r10);				\
	ADDC(sum, r11);				\

#define src r4
#define dest r5
#define sum r27

	.text
/* unknown src alignment and < 8 bytes to go */
small_csumcpy:
	mv	r5, r10
	ldi	r9, 0x0
	cmpi.c	r25, 0x1
	beq pass_small_set_t7	/*already set, jump to pass_small_set_t7*/
	andri.c	r25,r4 , 0x1	/*Is src 2 bytes aligned?*/

pass_small_set_t7:
	beq	aligned
	cmpi.c	r5, 0x0
	beq	fold
	lbu	r9, [src]
	slli	r9,r9, 0x8	/*Little endian*/
	ADDC(sum, r9)
	addi	src, 0x1
	subi.c	r5, 0x1

	/*len still a full word */
aligned:
	andri.c r8, r5, 0x4	/*Len >= 4?*/
	beq	len_less_4bytes

	/* Still a full word (4byte) to go,and the src is word aligned.*/
	andri.c	r8, src, 0x3	/*src is 4bytes aligned, so use LW!!*/
	beq	four_byte_aligned
	lhu 	r9, [src]
	addi	src, 2
	ADDC(sum, r9)
	lhu 	r9, [src]
	addi	src, 2
	ADDC(sum, r9)
	b len_less_4bytes

four_byte_aligned:		/* Len >=4 and four byte aligned */
	lw	r9, [src]
	addi	src, 4
	ADDC(sum, r9)

len_less_4bytes:		/* 2 byte aligned aligned and length<4B */
	andri.c r8, r5, 0x2
	beq	len_less_2bytes
	lhu	r9, [src]
	addi	src, 0x2	/* src+=2 */
	ADDC(sum, r9)

len_less_2bytes:		/* len = 1 */
	andri.c r8, r5, 0x1
	beq 	fold		/* less than 2 and not equal 1--> len=0 -> fold */
	lbu	r9, [src]

fold_ADDC:
	ADDC(sum, r9)
fold:
	/* fold checksum */
	slli	r26, sum, 16
	add	sum, sum, r26
	cmp.c	r26, sum
	srli	sum, sum, 16
	bleu 	1f 		/* if r26<=sum */
	addi	sum, 0x1 	/* r26>sum */
1:
	/* odd buffer alignment? r25 was set in csum_partial */
	cmpi.c	r25, 0x0
	beq	1f
	slli	r26, sum, 8
	srli	sum, sum, 8
	or	sum, sum, r26
	andi	sum, 0xffff
1:
	.set	optimize
	/* Add the passed partial csum. */
	ADDC(sum, r6)
	mv	r4, sum
	br	r3
	.set	volatile

	.align	5
ENTRY(csum_partial)
	ldi sum, 0
	ldi r25, 0
	mv r10, r5
	cmpi.c	r5, 0x8
	blt	small_csumcpy		/* < 8(singed) bytes to copy */
	cmpi.c	r5, 0x0
	beq	out
	andri.c	r25, src, 0x1		/* odd buffer? */

	beq	word_align
hword_align:				/* 1 byte */
	lbu	r8, [src]
	subi	r5, 0x1
	slli	r8, r8, 8
	ADDC(sum, r8)
	addi	src, 0x1

word_align:				/* 2 bytes */
	andri.c r8, src, 0x2		/* 4bytes(dword)_aligned? */
	beq	dword_align		/* not, maybe dword_align */
	lhu	r8, [src]
	subi	r5, 0x2
	ADDC(sum, r8)
	addi	src, 0x2

dword_align:				/* 4bytes */
	mv 	r26, r5			/* maybe useless when len >=56 */
	ldi 	r8, 56
	cmp.c	r8, r5
	bgtu	do_end_words		/* if a1(len)<t0(56) ,unsigned */
	andri.c	r26, src, 0x4
	beq	qword_align
	lw	r8, [src]
	subi	r5, 0x4
	ADDC(sum, r8)
	addi	src, 0x4

qword_align:				/* 8 bytes */
	andri.c r26, src, 0x8
	beq	oword_align
	lw	r8, [src, 0x0]
	lw	r9, [src, 0x4]
	subi	r5, 0x8			/* len-=0x8 */
	ADDC(sum, r8)
	ADDC(sum, r9)
	addi	src, 0x8

oword_align:				/* 16bytes */
	andri.c	r26, src, 0x10
	beq	begin_movement
	lw	r10, [src, 0x08]
	lw	r11, [src, 0x0c]
	lw	r8, [src, 0x00]
	lw	r9, [src, 0x04]
	ADDC(sum, r10)
	ADDC(sum, r11)
	ADDC(sum, r8)
	ADDC(sum, r9)
	subi	r5, 0x10
	addi	src, 0x10

begin_movement:
	srli.c	r26, r5, 0x7		/* len>=128? */
	beq	1f			/* len<128 */

/* r26 is the result that computed in oword_align */
move_128bytes:
	CSUM_BIGCHUNK(src, 0x00, sum)
	CSUM_BIGCHUNK(src, 0x20, sum)
	CSUM_BIGCHUNK(src, 0x40, sum)
	CSUM_BIGCHUNK(src, 0x60, sum)
	subi.c	r26, 0x01		/* r26 equals len/128 */
	addi	src, 0x80
	bne	move_128bytes

1:	/* len<128,we process 64byte here */
	andri.c	r10, r5, 0x40
	beq	1f

move_64bytes:
	CSUM_BIGCHUNK(src, 0x00, sum)
	CSUM_BIGCHUNK(src, 0x20, sum)
	addi	src, 0x40

1:					/* len<64 */
	andri	r26, r5, 0x1c		/* 0x1c=28 */
	andri.c	r10, r5, 0x20
	beq	do_end_words		/* decided by andri */

move_32bytes:
	CSUM_BIGCHUNK(src, 0x00, sum)
	andri	r26, r5, 0x1c
	addri	src, src, 0x20

do_end_words:				/* len<32 */
	/* r26 was set already in dword_align */
	cmpi.c	r26, 0x0
	beq	maybe_end_cruft		/* len<28 or len<56 */
	srli	r26, r26, 0x2

end_words:
	lw	r8, [src]
	subi.c	r26, 0x1		/* unit is 4 byte */
	ADDC(sum, r8)
	addi	src, 0x4
	cmpi.c	r26, 0x0
	bne	end_words		/* r26!=0 */

maybe_end_cruft:			/* len<4 */
	andri	r10, r5, 0x3

small_memcpy:
	mv	r5, r10
	j	small_csumcpy

out:
	mv	r4, sum
	br	r3

END(csum_partial)
