/*
 *  linux/arch/arm/lib/csumpartial.S
 *
 *  Copyright (C) 1995-1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text

/*
 * Function: __u32 csum_partial(const char *src, int len, __u32 sum)
 * Params  : r0 = buffer, r1 = len, r2 = checksum
 * Returns : r0 = new checksum
 */

buf	.req	r0
len	.req	r1
sum	.req	r2
td0	.req	r3
td1	.req	r4	@ save before use
td2	.req	r5	@ save before use
td3	.req	lr

.Lzero:		mov	r0, sum
		add	sp, sp, #4
		ldr	pc, [sp], #4

		/*
		 * Handle 0 to 7 bytes, with any alignment of source and
		 * destination pointers.  Note that when we get here, C = 0
		 */
.Lless8:		teq	len, #0			@ check for zero count
		beq	.Lzero

		/* we must have at least one byte. */
		tst	buf, #1			@ odd address?
		movne	sum, sum, ror #8
		ldrneb	td0, [buf], #1
		subne	len, len, #1
		adcnes	sum, sum, td0, put_byte_1

.Lless4:		tst	len, #6
		beq	.Lless8_byte

		/* we are now half-word aligned */

.Lless8_wordlp:
#if __LINUX_ARM_ARCH__ >= 4
		ldrh	td0, [buf], #2
		sub	len, len, #2
#else
		ldrb	td0, [buf], #1
		ldrb	td3, [buf], #1
		sub	len, len, #2
#ifndef __ARMEB__
		orr	td0, td0, td3, lsl #8
#else
		orr	td0, td3, td0, lsl #8
#endif
#endif
		adcs	sum, sum, td0
		tst	len, #6
		bne	.Lless8_wordlp

.Lless8_byte:	tst	len, #1			@ odd number of bytes
		ldrneb	td0, [buf], #1		@ include last byte
		adcnes	sum, sum, td0, put_byte_0	@ update checksum

.Ldone:		adc	r0, sum, #0		@ collect up the last carry
		ldr	td0, [sp], #4
		tst	td0, #1			@ check buffer alignment
		movne	r0, r0, ror #8		@ rotate checksum by 8 bits
		ldr	pc, [sp], #4		@ return

.Lnot_aligned:	tst	buf, #1			@ odd address
		ldrneb	td0, [buf], #1		@ make even
		subne	len, len, #1
		adcnes	sum, sum, td0, put_byte_1	@ update checksum

		tst	buf, #2			@ 32-bit aligned?
#if __LINUX_ARM_ARCH__ >= 4
		ldrneh	td0, [buf], #2		@ make 32-bit aligned
		subne	len, len, #2
#else
		ldrneb	td0, [buf], #1
		ldrneb	ip, [buf], #1
		subne	len, len, #2
#ifndef __ARMEB__
		orrne	td0, td0, ip, lsl #8
#else
		orrne	td0, ip, td0, lsl #8
#endif
#endif
		adcnes	sum, sum, td0		@ update checksum
		mov	pc, lr

ENTRY(csum_partial)
		stmfd	sp!, {buf, lr}
		cmp	len, #8			@ Ensure that we have at least
		blo	.Lless8			@ 8 bytes to copy.

		tst	buf, #1
		movne	sum, sum, ror #8

		adds	sum, sum, #0		@ C = 0
		tst	buf, #3			@ Test destination alignment
		blne	.Lnot_aligned		@ align destination, return here

1:		bics	ip, len, #31
		beq	3f

		stmfd	sp!, {r4 - r5}
2:		ldmia	buf!, {td0, td1, td2, td3}
		adcs	sum, sum, td0
		adcs	sum, sum, td1
		adcs	sum, sum, td2
		adcs	sum, sum, td3
		ldmia	buf!, {td0, td1, td2, td3}
		adcs	sum, sum, td0
		adcs	sum, sum, td1
		adcs	sum, sum, td2
		adcs	sum, sum, td3
		sub	ip, ip, #32
		teq	ip, #0
		bne	2b
		ldmfd	sp!, {r4 - r5}

3:		tst	len, #0x1c		@ should not change C
		beq	.Lless4

4:		ldr	td0, [buf], #4
		sub	len, len, #4
		adcs	sum, sum, td0
		tst	len, #0x1c
		bne	4b
		b	.Lless4
ENDPROC(csum_partial)
