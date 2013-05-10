/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Tom May, <ftom@netcom.com>
 *              Pentium Pro/II routines:
 *              Alexander Kjeldaas <astor@guardian.no>
 *              Finn Arne Gangstad <finnag@guardian.no>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 * Changes:     Ingo Molnar, converted csum_partial_copy() to 2.1 exception
 *			     handling.
 *		Andi Kleen,  add zeroing on error
 *                   converted to pure assembler
 *		Hirokazu Takata,Hiroyuki Kondo rewrite for the m32r architecture.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */

/*
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum)
 */


#ifdef CONFIG_ISA_DUAL_ISSUE

	/*
	 * Experiments with Ethernet and SLIP connections show that buff
	 * is aligned on either a 2-byte or 4-byte boundary.  We get at
	 * least a twofold speedup on 486 and Pentium if it is 4-byte aligned.
	 * Fortunately, it is easy to convert 2-byte alignment to 4-byte
	 * alignment for the unrolled loop.
	 */

	.text
ENTRY(csum_partial)
	; Function args
	;  r0: unsigned char *buff
	;  r1: int len
	;  r2: unsigned int sum

	push	r2		    ||	ldi	r2, #0
	and3	r7, r0, #1		; Check alignment.
	beqz	r7, 1f	 		; Jump if alignment is ok.
	; 1-byte mis aligned
	ldub	r4, @r0		    ||	addi	r0, #1
	; clear c-bit || Alignment uses up bytes.
	cmp	r0, r0		    ||	addi	r1, #-1
	ldi	r3, #0		    ||	addx	r2, r4
	addx	r2, r3
	.fillinsn
1:
	and3	r4, r0, #2		; Check alignment.
	beqz	r4, 2f	 		; Jump if alignment is ok.
	; clear c-bit || Alignment uses up two bytes.
	cmp	r0, r0		    ||	addi	r1, #-2
	bgtz	r1, 1f			; Jump if we had at least two bytes.
	bra	4f		    ||	addi	r1, #2
	.fillinsn			; len(r1) was < 2.  Deal with it.
1:
	; 2-byte aligned
	lduh	r4, @r0		    ||	ldi	r3, #0
	addx	r2, r4		    ||	addi	r0, #2
	addx	r2, r3
	.fillinsn
2:
	; 4-byte aligned
	cmp	r0, r0			; clear c-bit
	srl3	r6, r1, #5
	beqz	r6, 2f
	.fillinsn

1:	ld	r3, @r0+
	ld	r4, @r0+					; +4
	ld	r5, @r0+					; +8
	ld	r3, @r0+	    ||	addx    r2, r3		; +12
	ld	r4, @r0+	    ||	addx    r2, r4		; +16
	ld	r5, @r0+	    ||	addx    r2, r5		; +20
	ld	r3, @r0+	    ||	addx    r2, r3		; +24
	ld	r4, @r0+	    ||	addx    r2, r4		; +28
	addx	r2, r5		    ||	addi	r6, #-1
	addx	r2, r3
	addx	r2, r4
	bnez	r6, 1b

	addx	r2, r6			; r6=0
	cmp	r0, r0			; This clears c-bit
	.fillinsn
2:	and3	r6, r1, #0x1c		; withdraw len
	beqz	r6, 4f
	srli	r6, #2
	.fillinsn

3:	ld	r4, @r0+	    ||	addi	r6, #-1
	addx	r2, r4
	bnez	r6, 3b

	addx	r2, r6			; r6=0
	cmp	r0, r0			; This clears c-bit
	.fillinsn
4:	and3	r1, r1, #3
	beqz	r1, 7f			; if len == 0 goto end
	and3	r6, r1, #2
	beqz	r6, 5f			; if len < 2  goto 5f(1byte)
	lduh	r4, @r0		    ||	addi	r0, #2
	addi	r1, #-2		    ||	slli    r4, #16
	addx	r2, r4
	beqz	r1, 6f
	.fillinsn
5:	ldub	r4, @r0		    ||	ldi	r1, #0
#ifndef __LITTLE_ENDIAN__
	slli    r4, #8
#endif
	addx	r2, r4
	.fillinsn
6:	addx	r2, r1
	.fillinsn
7:
	and3	r0, r2, #0xffff
	srli	r2, #16
	add	r0, r2
	srl3	r2, r0, #16
	beqz	r2, 1f
	addi	r0, #1
	and3	r0, r0, #0xffff
	.fillinsn
1:
	beqz	r7, 1f			; swap the upper byte for the lower
	and3	r2, r0, #0xff
	srl3	r0, r0, #8
	slli	r2, #8
	or	r0, r2
	.fillinsn
1:
	pop	r2		    ||	cmp	r0, r0
	addx	r0, r2		    ||	ldi	r2, #0
	addx	r0, r2
	jmp	r14

#else /* not CONFIG_ISA_DUAL_ISSUE */

	/*
	 * Experiments with Ethernet and SLIP connections show that buff
	 * is aligned on either a 2-byte or 4-byte boundary.  We get at
	 * least a twofold speedup on 486 and Pentium if it is 4-byte aligned.
	 * Fortunately, it is easy to convert 2-byte alignment to 4-byte
	 * alignment for the unrolled loop.
	 */

	.text
ENTRY(csum_partial)
	; Function args
	;  r0: unsigned char *buff
	;  r1: int len
	;  r2: unsigned int sum

	push	r2
	ldi	r2, #0
	and3	r7, r0, #1		; Check alignment.
	beqz	r7, 1f	 		; Jump if alignment is ok.
	; 1-byte mis aligned
	ldub	r4, @r0
	addi	r0, #1
	addi	r1, #-1			; Alignment uses up bytes.
	cmp	r0, r0			; clear c-bit
	ldi	r3, #0
	addx	r2, r4
	addx	r2, r3
	.fillinsn
1:
	and3	r4, r0, #2		; Check alignment.
	beqz	r4, 2f	 		; Jump if alignment is ok.
	addi	r1, #-2			; Alignment uses up two bytes.
	cmp		r0, r0			; clear c-bit
	bgtz	r1, 1f			; Jump if we had at least two bytes.
	addi	r1, #2			; len(r1) was < 2.  Deal with it.
	bra	4f
	.fillinsn
1:
	; 2-byte aligned
	lduh	r4, @r0
	addi	r0, #2
	ldi		r3, #0
	addx	r2, r4
	addx	r2, r3
	.fillinsn
2:
	; 4-byte aligned
	cmp	r0, r0			; clear c-bit
	srl3	r6, r1, #5
	beqz	r6, 2f
	.fillinsn

1:	ld	r3, @r0+
	ld	r4, @r0+		; +4
	ld	r5, @r0+		; +8
	addx	r2, r3
	addx	r2, r4
	addx	r2, r5
	ld	r3, @r0+		; +12
	ld	r4, @r0+		; +16
	ld	r5, @r0+		; +20
	addx	r2, r3
	addx	r2, r4
	addx	r2, r5
	ld	r3, @r0+		; +24
	ld	r4, @r0+		; +28
	addi	r6, #-1
	addx	r2, r3
	addx	r2, r4
	bnez	r6, 1b
	addx	r2, r6			; r6=0
	cmp	r0, r0			; This clears c-bit
	.fillinsn

2:	and3	r6, r1, #0x1c		; withdraw len
	beqz	r6, 4f
	srli	r6, #2
	.fillinsn

3:	ld	r4, @r0+
	addi	r6, #-1
	addx	r2, r4
	bnez	r6, 3b
	addx	r2, r6			; r6=0
	cmp	r0, r0			; This clears c-bit
	.fillinsn

4:	and3	r1, r1, #3
	beqz	r1, 7f			; if len == 0 goto end
	and3	r6, r1, #2
	beqz	r6, 5f			; if len < 2  goto 5f(1byte)

	lduh	r4, @r0
	addi	r0, #2
	addi	r1, #-2
	slli    r4, #16
	addx	r2, r4
	beqz	r1, 6f
	.fillinsn
5:	ldub	r4, @r0
#ifndef __LITTLE_ENDIAN__
	slli    r4, #8
#endif
	addx	r2, r4
	.fillinsn
6:	ldi	r5, #0
	addx	r2, r5
	.fillinsn
7:
	and3	r0, r2, #0xffff
	srli	r2, #16
	add	r0, r2
	srl3	r2, r0, #16
	beqz	r2, 1f
	addi	r0, #1
	and3	r0, r0, #0xffff
	.fillinsn
1:
	beqz	r7, 1f
	mv	r2, r0
	srl3	r0, r2, #8
	and3	r2, r2, #0xff
	slli	r2, #8
	or	r0, r2
	.fillinsn
1:
	pop	r2
	cmp	r0, r0
	addx	r0, r2
	ldi	r2, #0
	addx	r0, r2
	jmp	r14

#endif /* not CONFIG_ISA_DUAL_ISSUE */

/*
unsigned int csum_partial_copy_generic (const char *src, char *dst,
				  int len, int sum, int *src_err_ptr, int *dst_err_ptr)
 */

/*
 * Copy from ds while checksumming, otherwise like csum_partial
 *
 * The macros SRC and DST specify the type of access for the instruction.
 * thus we can call a custom exception handler for all access types.
 *
 * FIXME: could someone double-check whether I haven't mixed up some SRC and
 *	  DST definitions? It's damn hard to trigger all cases.  I hope I got
 *	  them all but there's no guarantee.
 */

ENTRY(csum_partial_copy_generic)
	nop
	nop
	nop
	nop
	jmp r14
	nop
	nop
	nop

	.end
