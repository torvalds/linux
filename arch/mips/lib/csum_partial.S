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
/*
 * As we are sharing code base with the mips32 tree (which use the o32 ABI
 * register definitions). We need to redefine the register definitions from
 * the n64 ABI register naming to the o32 ABI register naming.
 */
#undef t0
#undef t1
#undef t2
#undef t3
#define t0	$8
#define t1	$9
#define t2	$10
#define t3	$11
#define t4	$12
#define t5	$13
#define t6	$14
#define t7	$15

#define USE_DOUBLE
#endif

#ifdef USE_DOUBLE

#define LOAD   ld
#define ADD    daddu
#define NBYTES 8

#else

#define LOAD   lw
#define ADD    addu
#define NBYTES 4

#endif /* USE_DOUBLE */

#define UNIT(unit)  ((unit)*NBYTES)

#define ADDC(sum,reg)						\
	ADD	sum, reg;					\
	sltu	v1, sum, reg;					\
	ADD	sum, v1

#define CSUM_BIGCHUNK1(src, offset, sum, _t0, _t1, _t2, _t3)	\
	LOAD	_t0, (offset + UNIT(0))(src);			\
	LOAD	_t1, (offset + UNIT(1))(src);			\
	LOAD	_t2, (offset + UNIT(2))(src); 			\
	LOAD	_t3, (offset + UNIT(3))(src); 			\
	ADDC(sum, _t0);						\
	ADDC(sum, _t1);						\
	ADDC(sum, _t2);						\
	ADDC(sum, _t3)

#ifdef USE_DOUBLE
#define CSUM_BIGCHUNK(src, offset, sum, _t0, _t1, _t2, _t3)	\
	CSUM_BIGCHUNK1(src, offset, sum, _t0, _t1, _t2, _t3)
#else
#define CSUM_BIGCHUNK(src, offset, sum, _t0, _t1, _t2, _t3)	\
	CSUM_BIGCHUNK1(src, offset, sum, _t0, _t1, _t2, _t3);	\
	CSUM_BIGCHUNK1(src, offset + 0x10, sum, _t0, _t1, _t2, _t3)
#endif

/*
 * a0: source address
 * a1: length of the area to checksum
 * a2: partial checksum
 */

#define src a0
#define sum v0

	.text
	.set	noreorder
	.align	5
LEAF(csum_partial)
	move	sum, zero
	move	t7, zero

	sltiu	t8, a1, 0x8
	bnez	t8, small_csumcpy		/* < 8 bytes to copy */
	 move	t2, a1

	andi	t7, src, 0x1			/* odd buffer? */

hword_align:
	beqz	t7, word_align
	 andi	t8, src, 0x2

	lbu	t0, (src)
	LONG_SUBU	a1, a1, 0x1
#ifdef __MIPSEL__
	sll	t0, t0, 8
#endif
	ADDC(sum, t0)
	PTR_ADDU	src, src, 0x1
	andi	t8, src, 0x2

word_align:
	beqz	t8, dword_align
	 sltiu	t8, a1, 56

	lhu	t0, (src)
	LONG_SUBU	a1, a1, 0x2
	ADDC(sum, t0)
	sltiu	t8, a1, 56
	PTR_ADDU	src, src, 0x2

dword_align:
	bnez	t8, do_end_words
	 move	t8, a1

	andi	t8, src, 0x4
	beqz	t8, qword_align
	 andi	t8, src, 0x8

	lw	t0, 0x00(src)
	LONG_SUBU	a1, a1, 0x4
	ADDC(sum, t0)
	PTR_ADDU	src, src, 0x4
	andi	t8, src, 0x8

qword_align:
	beqz	t8, oword_align
	 andi	t8, src, 0x10

#ifdef USE_DOUBLE
	ld	t0, 0x00(src)
	LONG_SUBU	a1, a1, 0x8
	ADDC(sum, t0)
#else
	lw	t0, 0x00(src)
	lw	t1, 0x04(src)
	LONG_SUBU	a1, a1, 0x8
	ADDC(sum, t0)
	ADDC(sum, t1)
#endif
	PTR_ADDU	src, src, 0x8
	andi	t8, src, 0x10

oword_align:
	beqz	t8, begin_movement
	 LONG_SRL	t8, a1, 0x7

#ifdef USE_DOUBLE
	ld	t0, 0x00(src)
	ld	t1, 0x08(src)
	ADDC(sum, t0)
	ADDC(sum, t1)
#else
	CSUM_BIGCHUNK1(src, 0x00, sum, t0, t1, t3, t4)
#endif
	LONG_SUBU	a1, a1, 0x10
	PTR_ADDU	src, src, 0x10
	LONG_SRL	t8, a1, 0x7

begin_movement:
	beqz	t8, 1f
	 andi	t2, a1, 0x40

move_128bytes:
	CSUM_BIGCHUNK(src, 0x00, sum, t0, t1, t3, t4)
	CSUM_BIGCHUNK(src, 0x20, sum, t0, t1, t3, t4)
	CSUM_BIGCHUNK(src, 0x40, sum, t0, t1, t3, t4)
	CSUM_BIGCHUNK(src, 0x60, sum, t0, t1, t3, t4)
	LONG_SUBU	t8, t8, 0x01
	bnez	t8, move_128bytes
	 PTR_ADDU	src, src, 0x80

1:
	beqz	t2, 1f
	 andi	t2, a1, 0x20

move_64bytes:
	CSUM_BIGCHUNK(src, 0x00, sum, t0, t1, t3, t4)
	CSUM_BIGCHUNK(src, 0x20, sum, t0, t1, t3, t4)
	PTR_ADDU	src, src, 0x40

1:
	beqz	t2, do_end_words
	 andi	t8, a1, 0x1c

move_32bytes:
	CSUM_BIGCHUNK(src, 0x00, sum, t0, t1, t3, t4)
	andi	t8, a1, 0x1c
	PTR_ADDU	src, src, 0x20

do_end_words:
	beqz	t8, small_csumcpy
	 andi	t2, a1, 0x3
	LONG_SRL	t8, t8, 0x2

end_words:
	lw	t0, (src)
	LONG_SUBU	t8, t8, 0x1
	ADDC(sum, t0)
	bnez	t8, end_words
	 PTR_ADDU	src, src, 0x4

/* unknown src alignment and < 8 bytes to go  */
small_csumcpy:
	move	a1, t2

	andi	t0, a1, 4
	beqz	t0, 1f
	 andi	t0, a1, 2

	/* Still a full word to go  */
	ulw	t1, (src)
	PTR_ADDIU	src, 4
	ADDC(sum, t1)

1:	move	t1, zero
	beqz	t0, 1f
	 andi	t0, a1, 1

	/* Still a halfword to go  */
	ulhu	t1, (src)
	PTR_ADDIU	src, 2

1:	beqz	t0, 1f
	 sll	t1, t1, 16

	lbu	t2, (src)
	 nop

#ifdef __MIPSEB__
	sll	t2, t2, 8
#endif
	or	t1, t2

1:	ADDC(sum, t1)

	/* fold checksum */
#ifdef USE_DOUBLE
	dsll32	v1, sum, 0
	daddu	sum, v1
	sltu	v1, sum, v1
	dsra32	sum, sum, 0
	addu	sum, v1
#endif
	sll	v1, sum, 16
	addu	sum, v1
	sltu	v1, sum, v1
	srl	sum, sum, 16
	addu	sum, v1

	/* odd buffer alignment? */
	beqz	t7, 1f
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
	END(csum_partial)
