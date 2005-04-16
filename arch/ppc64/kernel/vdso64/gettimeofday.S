/*
 * Userland implementation of gettimeofday() for 64 bits processes in a
 * ppc64 kernel for use in the vDSO
 *
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org),
 *                    IBM Corp.
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
#include <asm/offsets.h>

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

	mr	r11,r3			/* r11 holds tv */
	mr	r10,r4			/* r10 holds tz */
	bl	V_LOCAL_FUNC(__get_datapage)		/* get data page */
	bl	V_LOCAL_FUNC(__do_get_xsec)		/* get xsec from tb & kernel */
	lis     r7,15			/* r7 = 1000000 = USEC_PER_SEC */
	ori     r7,r7,16960
	rldicl  r5,r4,44,20		/* r5 = sec = xsec / XSEC_PER_SEC */
	rldicr  r6,r5,20,43		/* r6 = sec * XSEC_PER_SEC */
	std	r5,TVAL64_TV_SEC(r11)	/* store sec in tv */
	subf	r0,r6,r4		/* r0 = xsec = (xsec - r6) */
	mulld   r0,r0,r7		/* usec = (xsec * USEC_PER_SEC) / XSEC_PER_SEC */
	rldicl  r0,r0,44,20
	cmpldi	cr0,r10,0		/* check if tz is NULL */
	std	r0,TVAL64_TV_USEC(r11)	/* store usec in tv */
	beq	1f
	lwz	r4,CFG_TZ_MINUTEWEST(r3)/* fill tz */
	lwz	r5,CFG_TZ_DSTTIME(r3)
	stw	r4,TZONE_TZ_MINWEST(r10)
	stw	r5,TZONE_TZ_DSTTIME(r10)
1:	mtlr	r12
	li	r3,0			/* always success */
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_gettimeofday)


/*
 * This is the core of gettimeofday(), it returns the xsec
 * value in r4 and expects the datapage ptr (non clobbered)
 * in r3. clobbers r0,r4,r5,r6,r7,r8
*/
V_FUNCTION_BEGIN(__do_get_xsec)
  .cfi_startproc
	/* check for update count & load values */
1:	ld	r7,CFG_TB_UPDATE_COUNT(r3)
	andi.	r0,r4,1			/* pending update ? loop */
	bne-	1b
	xor	r0,r4,r4		/* create dependency */
	add	r3,r3,r0

	/* Get TB & offset it */
	mftb	r8
	ld	r9,CFG_TB_ORIG_STAMP(r3)
	subf	r8,r9,r8

	/* Scale result */
	ld	r5,CFG_TB_TO_XS(r3)
	mulhdu	r8,r8,r5

	/* Add stamp since epoch */
	ld	r6,CFG_STAMP_XSEC(r3)
	add	r4,r6,r8

	xor	r0,r4,r4
	add	r3,r3,r0
	ld	r0,CFG_TB_UPDATE_COUNT(r3)
        cmpld   cr0,r0,r7		/* check if updated */
	bne-	1b
	blr
  .cfi_endproc
V_FUNCTION_END(__do_get_xsec)
