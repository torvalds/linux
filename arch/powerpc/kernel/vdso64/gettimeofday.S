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

	mr	r11,r3			/* r11 holds tv */
	mr	r10,r4			/* r10 holds tz */
	bl	V_LOCAL_FUNC(__get_datapage)	/* get data page */
	bl	V_LOCAL_FUNC(__do_get_xsec)	/* get xsec from tb & kernel */
	lis     r7,15			/* r7 = 1000000 = USEC_PER_SEC */
	ori     r7,r7,16960
	rldicl  r5,r4,44,20		/* r5 = sec = xsec / XSEC_PER_SEC */
	rldicr  r6,r5,20,43		/* r6 = sec * XSEC_PER_SEC */
	std	r5,TVAL64_TV_SEC(r11)	/* store sec in tv */
	subf	r0,r6,r4		/* r0 = xsec = (xsec - r6) */
	mulld   r0,r0,r7		/* usec = (xsec * USEC_PER_SEC) /
					 * XSEC_PER_SEC
					 */
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
 * Exact prototype of clock_gettime()
 *
 * int __kernel_clock_gettime(clockid_t clock_id, struct timespec *tp);
 *
 */
V_FUNCTION_BEGIN(__kernel_clock_gettime)
  .cfi_startproc
	/* Check for supported clock IDs */
	cmpwi	cr0,r3,CLOCK_REALTIME
	cmpwi	cr1,r3,CLOCK_MONOTONIC
	cror	cr0,cr0,cr1
	bne	cr0,99f

	mflr	r12			/* r12 saves lr */
  .cfi_register lr,r12
	mr	r10,r3			/* r10 saves id */
	mr	r11,r4			/* r11 saves tp */
	bl	V_LOCAL_FUNC(__get_datapage)	/* get data page */
	beq	cr1,50f			/* if monotonic -> jump there */

	/*
	 * CLOCK_REALTIME
	 */

	bl	V_LOCAL_FUNC(__do_get_xsec)	/* get xsec from tb & kernel */

	lis     r7,0x3b9a		/* r7 = 1000000000 = NSEC_PER_SEC */
	ori     r7,r7,0xca00
	rldicl  r5,r4,44,20		/* r5 = sec = xsec / XSEC_PER_SEC */
	rldicr  r6,r5,20,43		/* r6 = sec * XSEC_PER_SEC */
	std	r5,TSPC64_TV_SEC(r11)	/* store sec in tv */
	subf	r0,r6,r4		/* r0 = xsec = (xsec - r6) */
	mulld   r0,r0,r7		/* nsec = (xsec * NSEC_PER_SEC) /
					 * XSEC_PER_SEC
					 */
	rldicl  r0,r0,44,20
	std	r0,TSPC64_TV_NSEC(r11)	/* store nsec in tp */

	mtlr	r12
	li	r3,0
	blr

	/*
	 * CLOCK_MONOTONIC
	 */

50:	bl	V_LOCAL_FUNC(__do_get_xsec)	/* get xsec from tb & kernel */

	lis     r7,0x3b9a		/* r7 = 1000000000 = NSEC_PER_SEC */
	ori     r7,r7,0xca00
	rldicl  r5,r4,44,20		/* r5 = sec = xsec / XSEC_PER_SEC */
	rldicr  r6,r5,20,43		/* r6 = sec * XSEC_PER_SEC */
	subf	r0,r6,r4		/* r0 = xsec = (xsec - r6) */
	mulld   r0,r0,r7		/* nsec = (xsec * NSEC_PER_SEC) /
					 * XSEC_PER_SEC
					 */
	rldicl  r6,r0,44,20

	/* now we must fixup using wall to monotonic. We need to snapshot
	 * that value and do the counter trick again. Fortunately, we still
	 * have the counter value in r8 that was returned by __do_get_xsec.
	 * At this point, r5,r6 contain our sec/nsec values.
	 * can be used
	 */

	lwz	r4,WTOM_CLOCK_SEC(r9)
	lwz	r7,WTOM_CLOCK_NSEC(r9)

	/* We now have our result in r4,r7. We create a fake dependency
	 * on that result and re-check the counter
	 */
	or	r9,r4,r7
	xor	r0,r9,r9
	add	r3,r3,r0
	ld	r0,CFG_TB_UPDATE_COUNT(r3)
        cmpld   cr0,r0,r8		/* check if updated */
	bne-	50b

	/* Calculate and store result. Note that this mimmics the C code,
	 * which may cause funny results if nsec goes negative... is that
	 * possible at all ?
	 */
	add	r4,r4,r5
	add	r7,r7,r6
	lis	r9,NSEC_PER_SEC@h
	ori	r9,r9,NSEC_PER_SEC@l
	cmpli	cr0,r7,r9
	blt	1f
	subf	r7,r9,r7
	addi	r4,r4,1
1:	std	r4,TSPC64_TV_SEC(r11)
	std	r7,TSPC64_TV_NSEC(r11)

	mtlr	r12
	li	r3,0
	blr

	/*
	 * syscall fallback
	 */
98:
	mtlr	r12
	mr	r3,r10
	mr	r4,r11
99:
	li	r0,__NR_clock_gettime
	sc
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_clock_gettime)


/*
 * Exact prototype of clock_getres()
 *
 * int __kernel_clock_getres(clockid_t clock_id, struct timespec *res);
 *
 */
V_FUNCTION_BEGIN(__kernel_clock_getres)
  .cfi_startproc
	/* Check for supported clock IDs */
	cmpwi	cr0,r3,CLOCK_REALTIME
	cmpwi	cr1,r3,CLOCK_MONOTONIC
	cror	cr0,cr0,cr1
	bne	cr0,99f

	li	r3,0
	cmpli	cr0,r4,0
	beqlr
	lis	r5,CLOCK_REALTIME_RES@h
	ori	r5,r5,CLOCK_REALTIME_RES@l
	std	r3,TSPC64_TV_SEC(r4)
	std	r5,TSPC64_TV_NSEC(r4)
	blr

	/*
	 * syscall fallback
	 */
99:
	li	r0,__NR_clock_getres
	sc
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_clock_getres)


/*
 * This is the core of gettimeofday(), it returns the xsec
 * value in r4 and expects the datapage ptr (non clobbered)
 * in r3. clobbers r0,r4,r5,r6,r7,r8
 * When returning, r8 contains the counter value that can be reused
 */
V_FUNCTION_BEGIN(__do_get_xsec)
  .cfi_startproc
	/* check for update count & load values */
1:	ld	r8,CFG_TB_UPDATE_COUNT(r3)
	andi.	r0,r4,1			/* pending update ? loop */
	bne-	1b
	xor	r0,r4,r4		/* create dependency */
	add	r3,r3,r0

	/* Get TB & offset it */
	mftb	r7
	ld	r9,CFG_TB_ORIG_STAMP(r3)
	subf	r7,r9,r7

	/* Scale result */
	ld	r5,CFG_TB_TO_XS(r3)
	mulhdu	r7,r7,r5

	/* Add stamp since epoch */
	ld	r6,CFG_STAMP_XSEC(r3)
	add	r4,r6,r7

	xor	r0,r4,r4
	add	r3,r3,r0
	ld	r0,CFG_TB_UPDATE_COUNT(r3)
        cmpld   cr0,r0,r8		/* check if updated */
	bne-	1b
	blr
  .cfi_endproc
V_FUNCTION_END(__do_get_xsec)
