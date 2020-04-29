/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Userland implementation of gettimeofday() for 64 bits processes in a
 * ppc64 kernel for use in the vDSO
 *
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org),
 *                    IBM Corp.
 */
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
	cmpldi	r11,0			/* check if tv is NULL */
	beq	2f
	lis	r7,1000000@ha		/* load up USEC_PER_SEC */
	addi	r7,r7,1000000@l
	bl	V_LOCAL_FUNC(__do_get_tspec) /* get sec/us from tb & kernel */
	std	r4,TVAL64_TV_SEC(r11)	/* store sec in tv */
	std	r5,TVAL64_TV_USEC(r11)	/* store usec in tv */
2:	cmpldi	r10,0			/* check if tz is NULL */
	beq	1f
	lwz	r4,CFG_TZ_MINUTEWEST(r3)/* fill tz */
	lwz	r5,CFG_TZ_DSTTIME(r3)
	stw	r4,TZONE_TZ_MINWEST(r10)
	stw	r5,TZONE_TZ_DSTTIME(r10)
1:	mtlr	r12
	crclr	cr0*4+so
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
	cror	cr0*4+eq,cr0*4+eq,cr1*4+eq

	cmpwi	cr5,r3,CLOCK_REALTIME_COARSE
	cmpwi	cr6,r3,CLOCK_MONOTONIC_COARSE
	cror	cr5*4+eq,cr5*4+eq,cr6*4+eq

	cror	cr0*4+eq,cr0*4+eq,cr5*4+eq
	bne	cr0,99f

	mflr	r12			/* r12 saves lr */
  .cfi_register lr,r12
	mr	r11,r4			/* r11 saves tp */
	bl	V_LOCAL_FUNC(__get_datapage)	/* get data page */
	lis	r7,NSEC_PER_SEC@h	/* want nanoseconds */
	ori	r7,r7,NSEC_PER_SEC@l
	beq	cr5,70f
50:	bl	V_LOCAL_FUNC(__do_get_tspec)	/* get time from tb & kernel */
	bne	cr1,80f			/* if not monotonic, all done */

	/*
	 * CLOCK_MONOTONIC
	 */

	/* now we must fixup using wall to monotonic. We need to snapshot
	 * that value and do the counter trick again. Fortunately, we still
	 * have the counter value in r8 that was returned by __do_get_tspec.
	 * At this point, r4,r5 contain our sec/nsec values.
	 */

	ld	r6,WTOM_CLOCK_SEC(r3)
	lwa	r9,WTOM_CLOCK_NSEC(r3)

	/* We now have our result in r6,r9. We create a fake dependency
	 * on that result and re-check the counter
	 */
	or	r0,r6,r9
	xor	r0,r0,r0
	add	r3,r3,r0
	ld	r0,CFG_TB_UPDATE_COUNT(r3)
        cmpld   cr0,r0,r8		/* check if updated */
	bne-	50b
	b	78f

	/*
	 * For coarse clocks we get data directly from the vdso data page, so
	 * we don't need to call __do_get_tspec, but we still need to do the
	 * counter trick.
	 */
70:	ld      r8,CFG_TB_UPDATE_COUNT(r3)
	andi.   r0,r8,1                 /* pending update ? loop */
	bne-    70b
	add     r3,r3,r0		/* r0 is already 0 */

	/*
	 * CLOCK_REALTIME_COARSE, below values are needed for MONOTONIC_COARSE
	 * too
	 */
	ld      r4,STAMP_XTIME_SEC(r3)
	ld      r5,STAMP_XTIME_NSEC(r3)
	bne     cr6,75f

	/* CLOCK_MONOTONIC_COARSE */
	ld	r6,WTOM_CLOCK_SEC(r3)
	lwa     r9,WTOM_CLOCK_NSEC(r3)

	/* check if counter has updated */
	or      r0,r6,r9
75:	or	r0,r0,r4
	or	r0,r0,r5
	xor     r0,r0,r0
	add     r3,r3,r0
	ld      r0,CFG_TB_UPDATE_COUNT(r3)
	cmpld   cr0,r0,r8               /* check if updated */
	bne-    70b

	/* Counter has not updated, so continue calculating proper values for
	 * sec and nsec if monotonic coarse, or just return with the proper
	 * values for realtime.
	 */
	bne     cr6,80f

	/* Add wall->monotonic offset and check for overflow or underflow */
78:	add     r4,r4,r6
	add     r5,r5,r9
	cmpd    cr0,r5,r7
	cmpdi   cr1,r5,0
	blt     79f
	subf    r5,r7,r5
	addi    r4,r4,1
79:	bge     cr1,80f
	addi    r4,r4,-1
	add     r5,r5,r7

80:	std	r4,TSPC64_TV_SEC(r11)
	std	r5,TSPC64_TV_NSEC(r11)

	mtlr	r12
	crclr	cr0*4+so
	li	r3,0
	blr

	/*
	 * syscall fallback
	 */
99:
	li	r0,__NR_clock_gettime
  .cfi_restore lr
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
	cror	cr0*4+eq,cr0*4+eq,cr1*4+eq
	bne	cr0,99f

	mflr	r12
  .cfi_register lr,r12
	bl	V_LOCAL_FUNC(__get_datapage)
	lwz	r5, CLOCK_HRTIMER_RES(r3)
	mtlr	r12
	li	r3,0
	cmpldi	cr0,r4,0
	crclr	cr0*4+so
	beqlr
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
 * Exact prototype of time()
 *
 * time_t time(time *t);
 *
 */
V_FUNCTION_BEGIN(__kernel_time)
  .cfi_startproc
	mflr	r12
  .cfi_register lr,r12

	mr	r11,r3			/* r11 holds t */
	bl	V_LOCAL_FUNC(__get_datapage)

	ld	r4,STAMP_XTIME_SEC(r3)

	cmpldi	r11,0			/* check if t is NULL */
	beq	2f
	std	r4,0(r11)		/* store result at *t */
2:	mtlr	r12
	crclr	cr0*4+so
	mr	r3,r4
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_time)


/*
 * This is the core of clock_gettime() and gettimeofday(),
 * it returns the current time in r4 (seconds) and r5.
 * On entry, r7 gives the resolution of r5, either USEC_PER_SEC
 * or NSEC_PER_SEC, giving r5 in microseconds or nanoseconds.
 * It expects the datapage ptr in r3 and doesn't clobber it.
 * It clobbers r0, r6 and r9.
 * On return, r8 contains the counter value that can be reused.
 * This clobbers cr0 but not any other cr field.
 */
V_FUNCTION_BEGIN(__do_get_tspec)
  .cfi_startproc
	/* check for update count & load values */
1:	ld	r8,CFG_TB_UPDATE_COUNT(r3)
	andi.	r0,r8,1			/* pending update ? loop */
	bne-	1b
	xor	r0,r8,r8		/* create dependency */
	add	r3,r3,r0

	/* Get TB & offset it. We use the MFTB macro which will generate
	 * workaround code for Cell.
	 */
	MFTB(r6)
	ld	r9,CFG_TB_ORIG_STAMP(r3)
	subf	r6,r9,r6

	/* Scale result */
	ld	r5,CFG_TB_TO_XS(r3)
	sldi	r6,r6,12		/* compute time since stamp_xtime */
	mulhdu	r6,r6,r5		/* in units of 2^-32 seconds */

	/* Add stamp since epoch */
	ld	r4,STAMP_XTIME_SEC(r3)
	lwz	r5,STAMP_SEC_FRAC(r3)
	or	r0,r4,r5
	or	r0,r0,r6
	xor	r0,r0,r0
	add	r3,r3,r0
	ld	r0,CFG_TB_UPDATE_COUNT(r3)
	cmpld   r0,r8			/* check if updated */
	bne-	1b			/* reload if so */

	/* convert to seconds & nanoseconds and add to stamp */
	add	r6,r6,r5		/* add on fractional seconds of xtime */
	mulhwu	r5,r6,r7		/* compute micro or nanoseconds and */
	srdi	r6,r6,32		/* seconds since stamp_xtime */
	clrldi	r5,r5,32
	add	r4,r4,r6
	blr
  .cfi_endproc
V_FUNCTION_END(__do_get_tspec)
