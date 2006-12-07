/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Quick'n'dirty IP checksum ...
 *
 * Copyright (C) 1998, 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <asm/asm.h>
#include <asm/regdef.h>

#ifdef CONFIG_64BIT
#define T0	ta0
#define T1	ta1
#define T2	ta2
#define T3	ta3
#define T4	t0
#define T7	t3
#else
#define T0	t0
#define T1	t1
#define T2	t2
#define T3	t3
#define T4	t4
#define T7	t7
#endif

#define ADDC(sum,reg)						\
	addu	sum, reg;					\
	sltu	v1, sum, reg;					\
	addu	sum, v1

#define CSUM_BIGCHUNK(src, offset, sum, _t0, _t1, _t2, _t3)	\
	lw	_t0, (offset + 0x00)(src);			\
	lw	_t1, (offset + 0x04)(src);			\
	lw	_t2, (offset + 0x08)(src); 			\
	lw	_t3, (offset + 0x0c)(src); 			\
	ADDC(sum, _t0);						\
	ADDC(sum, _t1);						\
	ADDC(sum, _t2);						\
	ADDC(sum, _t3);						\
	lw	_t0, (offset + 0x10)(src);			\
	lw	_t1, (offset + 0x14)(src);			\
	lw	_t2, (offset + 0x18)(src);			\
	lw	_t3, (offset + 0x1c)(src);			\
	ADDC(sum, _t0);						\
	ADDC(sum, _t1);						\
	ADDC(sum, _t2);						\
	ADDC(sum, _t3);						\

/*
 * a0: source address
 * a1: length of the area to checksum
 * a2: partial checksum
 */

#define src a0
#define sum v0

	.text
	.set	noreorder

/* unknown src alignment and < 8 bytes to go  */
small_csumcpy:
	move	a1, T2

	andi	T0, a1, 4
	beqz	T0, 1f
	 andi	T0, a1, 2

	/* Still a full word to go  */
	ulw	T1, (src)
	PTR_ADDIU	src, 4
	ADDC(sum, T1)

1:	move	T1, zero
	beqz	T0, 1f
	 andi	T0, a1, 1

	/* Still a halfword to go  */
	ulhu	T1, (src)
	PTR_ADDIU	src, 2

1:	beqz	T0, 1f
	 sll	T1, T1, 16

	lbu	T2, (src)
	 nop

#ifdef __MIPSEB__
	sll	T2, T2, 8
#endif
	or	T1, T2

1:	ADDC(sum, T1)

	/* fold checksum */
	sll	v1, sum, 16
	addu	sum, v1
	sltu	v1, sum, v1
	srl	sum, sum, 16
	addu	sum, v1

	/* odd buffer alignment? */
	beqz	T7, 1f
	 nop
	sll	v1, sum, 8
	srl	sum, sum, 8
	or	sum, v1
	andi	sum, 0xffff
1:
	.set	reorder
	/* Add the passed partial csum.  */
	ADDC(sum, a2)
	jr	ra
	.set	noreorder

/* ------------------------------------------------------------------------- */

	.align	5
LEAF(csum_partial)
	move	sum, zero
	move	T7, zero

	sltiu	t8, a1, 0x8
	bnez	t8, small_csumcpy		/* < 8 bytes to copy */
	 move	T2, a1

	beqz	a1, out
	 andi	T7, src, 0x1			/* odd buffer? */

hword_align:
	beqz	T7, word_align
	 andi	t8, src, 0x2

	lbu	T0, (src)
	LONG_SUBU	a1, a1, 0x1
#ifdef __MIPSEL__
	sll	T0, T0, 8
#endif
	ADDC(sum, T0)
	PTR_ADDU	src, src, 0x1
	andi	t8, src, 0x2

word_align:
	beqz	t8, dword_align
	 sltiu	t8, a1, 56

	lhu	T0, (src)
	LONG_SUBU	a1, a1, 0x2
	ADDC(sum, T0)
	sltiu	t8, a1, 56
	PTR_ADDU	src, src, 0x2

dword_align:
	bnez	t8, do_end_words
	 move	t8, a1

	andi	t8, src, 0x4
	beqz	t8, qword_align
	 andi	t8, src, 0x8

	lw	T0, 0x00(src)
	LONG_SUBU	a1, a1, 0x4
	ADDC(sum, T0)
	PTR_ADDU	src, src, 0x4
	andi	t8, src, 0x8

qword_align:
	beqz	t8, oword_align
	 andi	t8, src, 0x10

	lw	T0, 0x00(src)
	lw	T1, 0x04(src)
	LONG_SUBU	a1, a1, 0x8
	ADDC(sum, T0)
	ADDC(sum, T1)
	PTR_ADDU	src, src, 0x8
	andi	t8, src, 0x10

oword_align:
	beqz	t8, begin_movement
	 LONG_SRL	t8, a1, 0x7

	lw	T3, 0x08(src)
	lw	T4, 0x0c(src)
	lw	T0, 0x00(src)
	lw	T1, 0x04(src)
	ADDC(sum, T3)
	ADDC(sum, T4)
	ADDC(sum, T0)
	ADDC(sum, T1)
	LONG_SUBU	a1, a1, 0x10
	PTR_ADDU	src, src, 0x10
	LONG_SRL	t8, a1, 0x7

begin_movement:
	beqz	t8, 1f
	 andi	T2, a1, 0x40

move_128bytes:
	CSUM_BIGCHUNK(src, 0x00, sum, T0, T1, T3, T4)
	CSUM_BIGCHUNK(src, 0x20, sum, T0, T1, T3, T4)
	CSUM_BIGCHUNK(src, 0x40, sum, T0, T1, T3, T4)
	CSUM_BIGCHUNK(src, 0x60, sum, T0, T1, T3, T4)
	LONG_SUBU	t8, t8, 0x01
	bnez	t8, move_128bytes
	 PTR_ADDU	src, src, 0x80

1:
	beqz	T2, 1f
	 andi	T2, a1, 0x20

move_64bytes:
	CSUM_BIGCHUNK(src, 0x00, sum, T0, T1, T3, T4)
	CSUM_BIGCHUNK(src, 0x20, sum, T0, T1, T3, T4)
	PTR_ADDU	src, src, 0x40

1:
	beqz	T2, do_end_words
	 andi	t8, a1, 0x1c

move_32bytes:
	CSUM_BIGCHUNK(src, 0x00, sum, T0, T1, T3, T4)
	andi	t8, a1, 0x1c
	PTR_ADDU	src, src, 0x20

do_end_words:
	beqz	t8, maybe_end_cruft
	 LONG_SRL	t8, t8, 0x2

end_words:
	lw	T0, (src)
	LONG_SUBU	t8, t8, 0x1
	ADDC(sum, T0)
	bnez	t8, end_words
	 PTR_ADDU	src, src, 0x4

maybe_end_cruft:
	andi	T2, a1, 0x3

small_memcpy:
 j small_csumcpy; move a1, T2		/* XXX ??? */
	beqz	t2, out
	 move	a1, T2

end_bytes:
	lb	T0, (src)
	LONG_SUBU	a1, a1, 0x1
	bnez	a2, end_bytes
	 PTR_ADDU	src, src, 0x1

out:
	jr	ra
	 move	v0, sum
	END(csum_partial)
