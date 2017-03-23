/*
 * kexec trampoline
 *
 * Based on code taken from kexec-tools and kexec-lite.
 *
 * Copyright (C) 2004 - 2005, Milton D Miller II, IBM Corporation
 * Copyright (C) 2006, Mohan Kumar M, IBM Corporation
 * Copyright (C) 2013, Anton Blanchard, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation (version 2 of the License).
 */

#if defined(__LITTLE_ENDIAN__)
#define STWX_BE	stwbrx
#define LWZX_BE	lwbrx
#elif defined(__BIG_ENDIAN__)
#define STWX_BE	stwx
#define LWZX_BE	lwzx
#else
#error no endianness defined!
#endif

	.machine ppc64
	.balign 256
	.globl purgatory_start
purgatory_start:
	b	master

	/* ABI: possible run_at_load flag at 0x5c */
	.org purgatory_start + 0x5c
	.globl run_at_load
run_at_load:
	.long 0
	.size run_at_load, . - run_at_load

	/* ABI: slaves start at 60 with r3=phys */
	.org purgatory_start + 0x60
slave:
	b .
	/* ABI: end of copied region */
	.org purgatory_start + 0x100
	.size purgatory_start, . - purgatory_start

/*
 * The above 0x100 bytes at purgatory_start are replaced with the
 * code from the kernel (or next stage) by setup_purgatory().
 */

master:
	or	%r1,%r1,%r1	/* low priority to let other threads catchup */
	isync
	mr	%r17,%r3	/* save cpu id to r17 */
	mr	%r15,%r4	/* save physical address in reg15 */

	or	%r3,%r3,%r3	/* ok now to high priority, lets boot */
	lis	%r6,0x1
	mtctr	%r6		/* delay a bit for slaves to catch up */
	bdnz	.		/* before we overwrite 0-100 again */

	bl	0f		/* Work out where we're running */
0:	mflr	%r18

	/* load device-tree address */
	ld	%r3, (dt_offset - 0b)(%r18)
	mr	%r16,%r3	/* save dt address in reg16 */
	li	%r4,20
	LWZX_BE	%r6,%r3,%r4	/* fetch __be32 version number at byte 20 */
	cmpwi	%r0,%r6,2	/* v2 or later? */
	blt	1f
	li	%r4,28
	STWX_BE	%r17,%r3,%r4	/* Store my cpu as __be32 at byte 28 */
1:
	/* load the kernel address */
	ld	%r4,(kernel - 0b)(%r18)

	/* load the run_at_load flag */
	/* possibly patched by kexec */
	ld	%r6,(run_at_load - 0b)(%r18)
	/* and patch it into the kernel */
	stw	%r6,(0x5c)(%r4)

	mr	%r3,%r16	/* restore dt address */

	li	%r5,0		/* r5 will be 0 for kernel */

	mfmsr	%r11
	andi.	%r10,%r11,1	/* test MSR_LE */
	bne	.Little_endian

	mtctr	%r4		/* prepare branch to */
	bctr			/* start kernel */

.Little_endian:
	mtsrr0	%r4		/* prepare branch to */

	clrrdi	%r11,%r11,1	/* clear MSR_LE */
	mtsrr1	%r11

	rfid			/* update MSR and start kernel */


	.balign 8
	.globl kernel
kernel:
	.llong  0x0
	.size kernel, . - kernel

	.balign 8
	.globl dt_offset
dt_offset:
	.llong  0x0
	.size dt_offset, . - dt_offset


	.data
	.balign 8
.globl purgatory_sha256_digest
purgatory_sha256_digest:
	.skip	32
	.size purgatory_sha256_digest, . - purgatory_sha256_digest

	.balign 8
.globl purgatory_sha_regions
purgatory_sha_regions:
	.skip	8 * 2 * 16
	.size purgatory_sha_regions, . - purgatory_sha_regions
