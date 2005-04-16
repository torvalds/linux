/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ralf Baechle
 */
#include <asm/asm.h>
#include <asm/regdef.h>

#define ADDC(sum,reg)			\
	addu	sum, reg;		\
	sltu	v1, sum, reg;		\
	addu	sum, v1

#define CSUM_BIGCHUNK(src, offset, sum, t0, t1, t2, t3) \
	lw	t0, (offset + 0x00)(src); \
	lw	t1, (offset + 0x04)(src); \
	lw	t2, (offset + 0x08)(src); \
	lw	t3, (offset + 0x0c)(src); \
	ADDC(sum, t0);                    \
	ADDC(sum, t1);                    \
	ADDC(sum, t2);                    \
	ADDC(sum, t3);                    \
	lw	t0, (offset + 0x10)(src); \
	lw	t1, (offset + 0x14)(src); \
	lw	t2, (offset + 0x18)(src); \
	lw	t3, (offset + 0x1c)(src); \
	ADDC(sum, t0);                    \
	ADDC(sum, t1);                    \
	ADDC(sum, t2);                    \
	ADDC(sum, t3);                    \

/*
 * a0: source address
 * a1: length of the area to checksum
 * a2: partial checksum
 */

#define src a0
#define dest a1
#define sum v0

	.text
	.set	noreorder

/* unknown src alignment and < 8 bytes to go  */
small_csumcpy:
	move	a1, t2

	andi	t0, a1, 4
	beqz	t0, 1f
	 andi	t0, a1, 2

	/* Still a full word to go  */
	ulw	t1, (src)
	addiu	src, 4
	ADDC(sum, t1)

1:	move	t1, zero
	beqz	t0, 1f
	 andi	t0, a1, 1

	/* Still a halfword to go  */
	ulhu	t1, (src)
	addiu	src, 2

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

/* ------------------------------------------------------------------------- */

	.align	5
LEAF(csum_partial)
	move	sum, zero
	move	t7, zero

	sltiu	t8, a1, 0x8
	bnez	t8, small_csumcpy		/* < 8 bytes to copy */
	 move	t2, a1

	beqz	a1, out
	 andi	t7, src, 0x1			/* odd buffer? */

hword_align:
	beqz	t7, word_align
	 andi	t8, src, 0x2

	lbu	t0, (src)
	subu	a1, a1, 0x1
#ifdef __MIPSEL__
	sll	t0, t0, 8
#endif
	ADDC(sum, t0)
	addu	src, src, 0x1
	andi	t8, src, 0x2

word_align:
	beqz	t8, dword_align
	 sltiu	t8, a1, 56

	lhu	t0, (src)
	subu	a1, a1, 0x2
	ADDC(sum, t0)
	sltiu	t8, a1, 56
	addu	src, src, 0x2

dword_align:
	bnez	t8, do_end_words
	 move	t8, a1

	andi	t8, src, 0x4
	beqz	t8, qword_align
	 andi	t8, src, 0x8

	lw	t0, 0x00(src)
	subu	a1, a1, 0x4
	ADDC(sum, t0)
	addu	src, src, 0x4
	andi	t8, src, 0x8

qword_align:
	beqz	t8, oword_align
	 andi	t8, src, 0x10

	lw	t0, 0x00(src)
	lw	t1, 0x04(src)
	subu	a1, a1, 0x8
	ADDC(sum, t0)
	ADDC(sum, t1)
	addu	src, src, 0x8
	andi	t8, src, 0x10

oword_align:
	beqz	t8, begin_movement
	 srl	t8, a1, 0x7

	lw	t3, 0x08(src)
	lw	t4, 0x0c(src)
	lw	t0, 0x00(src)
	lw	t1, 0x04(src)
	ADDC(sum, t3)
	ADDC(sum, t4)
	ADDC(sum, t0)
	ADDC(sum, t1)
	subu	a1, a1, 0x10
	addu	src, src, 0x10
	srl	t8, a1, 0x7

begin_movement:
	beqz	t8, 1f
	 andi	t2, a1, 0x40

move_128bytes:
	CSUM_BIGCHUNK(src, 0x00, sum, t0, t1, t3, t4)
	CSUM_BIGCHUNK(src, 0x20, sum, t0, t1, t3, t4)
	CSUM_BIGCHUNK(src, 0x40, sum, t0, t1, t3, t4)
	CSUM_BIGCHUNK(src, 0x60, sum, t0, t1, t3, t4)
	subu	t8, t8, 0x01
	bnez	t8, move_128bytes
	 addu	src, src, 0x80

1:
	beqz	t2, 1f
	 andi	t2, a1, 0x20

move_64bytes:
	CSUM_BIGCHUNK(src, 0x00, sum, t0, t1, t3, t4)
	CSUM_BIGCHUNK(src, 0x20, sum, t0, t1, t3, t4)
	addu	src, src, 0x40

1:
	beqz	t2, do_end_words
	 andi	t8, a1, 0x1c

move_32bytes:
	CSUM_BIGCHUNK(src, 0x00, sum, t0, t1, t3, t4)
	andi	t8, a1, 0x1c
	addu	src, src, 0x20

do_end_words:
	beqz	t8, maybe_end_cruft
	 srl	t8, t8, 0x2

end_words:
	lw	t0, (src)
	subu	t8, t8, 0x1
	ADDC(sum, t0)
	bnez	t8, end_words
	 addu	src, src, 0x4

maybe_end_cruft:
	andi	t2, a1, 0x3

small_memcpy:
 j small_csumcpy; move a1, t2
	beqz	t2, out
	 move	a1, t2

end_bytes:
	lb	t0, (src)
	subu	a1, a1, 0x1
	bnez	a2, end_bytes
	 addu	src, src, 0x1

out:
	jr	ra
	 move	v0, sum
	END(csum_partial)
