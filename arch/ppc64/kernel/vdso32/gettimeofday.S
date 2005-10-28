/*
 * Userland implementation of gettimeofday() for 32 bits processes in a
 * ppc64 kernel for use in the vDSO
 *
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org), IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/vdso.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

	.text
/*
 * Exact prototype of gettimeofday
 *
 * int __kernel_gettimeofday(struct timeval *tv, struct timezone *tz);
 *
 */
V_FUNCTION_BEGIN(__kernel_gettimeofday)
  .cfi_startproc
	mflr	r12
  .cfi_register lr,r12

	mr	r10,r3			/* r10 saves tv */
	mr	r11,r4			/* r11 saves tz */
	bl	__get_datapage@local	/* get data page */
	mr	r9, r3			/* datapage ptr in r9 */
	bl	__do_get_xsec@local	/* get xsec from tb & kernel */
	bne-	2f			/* out of line -> do syscall */

	/* seconds are xsec >> 20 */
	rlwinm	r5,r4,12,20,31
	rlwimi	r5,r3,12,0,19
	stw	r5,TVAL32_TV_SEC(r10)

	/* get remaining xsec and convert to usec. we scale
	 * up remaining xsec by 12 bits and get the top 32 bits
	 * of the multiplication
	 */
	rlwinm	r5,r4,12,0,19
	lis	r6,1000000@h
	ori	r6,r6,1000000@l
	mulhwu	r5,r5,r6
	stw	r5,TVAL32_TV_USEC(r10)

	cmpli	cr0,r11,0		/* check if tz is NULL */
	beq	1f
	lwz	r4,CFG_TZ_MINUTEWEST(r9)/* fill tz */
	lwz	r5,CFG_TZ_DSTTIME(r9)
	stw	r4,TZONE_TZ_MINWEST(r11)
	stw	r5,TZONE_TZ_DSTTIME(r11)

1:	mtlr	r12
	li	r3,0
	blr

2:	mr	r3,r10
	mr	r4,r11
	li	r0,__NR_gettimeofday
	sc
	b	1b
  .cfi_endproc
V_FUNCTION_END(__kernel_gettimeofday)

/*
 * This is the core of gettimeofday(), it returns the xsec
 * value in r3 & r4 and expects the datapage ptr (non clobbered)
 * in r9. clobbers r0,r4,r5,r6,r7,r8
*/
__do_get_xsec:
  .cfi_startproc
	/* Check for update count & load values. We use the low
	 * order 32 bits of the update count
	 */
1:	lwz	r8,(CFG_TB_UPDATE_COUNT+4)(r9)
	andi.	r0,r8,1			/* pending update ? loop */
	bne-	1b
	xor	r0,r8,r8		/* create dependency */
	add	r9,r9,r0

	/* Load orig stamp (offset to TB) */
	lwz	r5,CFG_TB_ORIG_STAMP(r9)
	lwz	r6,(CFG_TB_ORIG_STAMP+4)(r9)

	/* Get a stable TB value */
2:	mftbu	r3
	mftbl	r4
	mftbu	r0
	cmpl	cr0,r3,r0
	bne-	2b

	/* Substract tb orig stamp. If the high part is non-zero, we jump to the
	 * slow path which call the syscall. If it's ok, then we have our 32 bits
	 * tb_ticks value in r7
	 */
	subfc	r7,r6,r4
	subfe.	r0,r5,r3
	bne-	3f

	/* Load scale factor & do multiplication */
	lwz	r5,CFG_TB_TO_XS(r9)	/* load values */
	lwz	r6,(CFG_TB_TO_XS+4)(r9)
	mulhwu	r4,r7,r5
	mulhwu	r6,r7,r6
	mullw	r0,r7,r5
	addc	r6,r6,r0

	/* At this point, we have the scaled xsec value in r4 + XER:CA
	 * we load & add the stamp since epoch
	 */
	lwz	r5,CFG_STAMP_XSEC(r9)
	lwz	r6,(CFG_STAMP_XSEC+4)(r9)
	adde	r4,r4,r6
	addze	r3,r5

	/* We now have our result in r3,r4. We create a fake dependency
	 * on that result and re-check the counter
	 */
	xor	r0,r4,r4
	add	r9,r9,r0
	lwz	r0,(CFG_TB_UPDATE_COUNT+4)(r9)
        cmpl    cr0,r8,r0		/* check if updated */
	bne-	1b

	/* Warning ! The caller expects CR:EQ to be set to indicate a
	 * successful calculation (so it won't fallback to the syscall
	 * method). We have overriden that CR bit in the counter check,
	 * but fortunately, the loop exit condition _is_ CR:EQ set, so
	 * we can exit safely here. If you change this code, be careful
	 * of that side effect.
	 */
3:	blr
  .cfi_endproc
