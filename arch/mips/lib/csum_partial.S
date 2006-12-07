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

	lw	t0, 0x00(src)
	lw	t1, 0x04(src)
	LONG_SUBU	a1, a1, 0x8
	ADDC(sum, t0)
	ADDC(sum, t1)
	PTR_ADDU	src, src, 0x8
	andi	t8, src, 0x10

oword_align:
	beqz	t8, begin_movement
	 LONG_SRL	t8, a1, 0x7

	lw	t3, 0x08(src)
	lw	t4, 0x0c(src)
	lw	t0, 0x00(src)
	lw	t1, 0x04(src)
	ADDC(sum, t3)
	ADDC(sum, t4)
	ADDC(sum, t0)
	ADDC(sum, t1)
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
