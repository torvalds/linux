/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Userland implementation of gettimeofday() for 32 bits processes in a
 * ppc64 kernel for use in the vDSO
 *
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org,
 *                    IBM Corp.
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/vdso.h>
#include <asm/vdso_datapage.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

/* Offset for the low 32-bit part of a field of long type */
#ifdef CONFIG_PPC64
#define LOPART	4
#else
#define LOPART	0
#endif

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

	mr.	r10,r3			/* r10 saves tv */
	mr	r11,r4			/* r11 saves tz */
	get_datapage	r9, r0
	beq	3f
	LOAD_REG_IMMEDIATE(r7, 1000000)	/* load up USEC_PER_SEC */
	bl	__do_get_tspec@local	/* get sec/usec from tb & kernel */
	stw	r3,TVAL32_TV_SEC(r10)
	stw	r4,TVAL32_TV_USEC(r10)

3:	cmplwi	r11,0			/* check if tz is NULL */
	mtlr	r12
	crclr	cr0*4+so
	li	r3,0
	beqlr

	lwz	r4,CFG_TZ_MINUTEWEST(r9)/* fill tz */
	lwz	r5,CFG_TZ_DSTTIME(r9)
	stw	r4,TZONE_TZ_MINWEST(r11)
	stw	r5,TZONE_TZ_DSTTIME(r11)

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
	cmpli	cr0,r3,CLOCK_REALTIME
	cmpli	cr1,r3,CLOCK_MONOTONIC
	cror	cr0*4+eq,cr0*4+eq,cr1*4+eq

	cmpli	cr5,r3,CLOCK_REALTIME_COARSE
	cmpli	cr6,r3,CLOCK_MONOTONIC_COARSE
	cror	cr5*4+eq,cr5*4+eq,cr6*4+eq

	cror	cr0*4+eq,cr0*4+eq,cr5*4+eq
	bne	cr0, .Lgettime_fallback

	mflr	r12			/* r12 saves lr */
  .cfi_register lr,r12
	mr	r11,r4			/* r11 saves tp */
	get_datapage	r9, r0
	LOAD_REG_IMMEDIATE(r7, NSEC_PER_SEC)	/* load up NSEC_PER_SEC */
	beq	cr5, .Lcoarse_clocks
.Lprecise_clocks:
	bl	__do_get_tspec@local	/* get sec/nsec from tb & kernel */
	bne	cr1, .Lfinish		/* not monotonic -> all done */

	/*
	 * CLOCK_MONOTONIC
	 */

	/* now we must fixup using wall to monotonic. We need to snapshot
	 * that value and do the counter trick again. Fortunately, we still
	 * have the counter value in r8 that was returned by __do_get_xsec.
	 * At this point, r3,r4 contain our sec/nsec values, r5 and r6
	 * can be used, r7 contains NSEC_PER_SEC.
	 */

	lwz	r5,(WTOM_CLOCK_SEC+LOPART)(r9)
	lwz	r6,WTOM_CLOCK_NSEC(r9)

	/* We now have our offset in r5,r6. We create a fake dependency
	 * on that value and re-check the counter
	 */
	or	r0,r6,r5
	xor	r0,r0,r0
	add	r9,r9,r0
	lwz	r0,(CFG_TB_UPDATE_COUNT+LOPART)(r9)
        cmpl    cr0,r8,r0		/* check if updated */
	bne-	.Lprecise_clocks
	b	.Lfinish_monotonic

	/*
	 * For coarse clocks we get data directly from the vdso data page, so
	 * we don't need to call __do_get_tspec, but we still need to do the
	 * counter trick.
	 */
.Lcoarse_clocks:
	lwz	r8,(CFG_TB_UPDATE_COUNT+LOPART)(r9)
	andi.	r0,r8,1                 /* pending update ? loop */
	bne-	.Lcoarse_clocks
	add	r9,r9,r0		/* r0 is already 0 */

	/*
	 * CLOCK_REALTIME_COARSE, below values are needed for MONOTONIC_COARSE
	 * too
	 */
	lwz	r3,STAMP_XTIME_SEC+LOPART(r9)
	lwz	r4,STAMP_XTIME_NSEC+LOPART(r9)
	bne	cr6,1f

	/* CLOCK_MONOTONIC_COARSE */
	lwz	r5,(WTOM_CLOCK_SEC+LOPART)(r9)
	lwz	r6,WTOM_CLOCK_NSEC(r9)

	/* check if counter has updated */
	or	r0,r6,r5
1:	or	r0,r0,r3
	or	r0,r0,r4
	xor	r0,r0,r0
	add	r3,r3,r0
	lwz	r0,CFG_TB_UPDATE_COUNT+LOPART(r9)
	cmpl	cr0,r0,r8               /* check if updated */
	bne-	.Lcoarse_clocks

	/* Counter has not updated, so continue calculating proper values for
	 * sec and nsec if monotonic coarse, or just return with the proper
	 * values for realtime.
	 */
	bne	cr6, .Lfinish

	/* Calculate and store result. Note that this mimics the C code,
	 * which may cause funny results if nsec goes negative... is that
	 * possible at all ?
	 */
.Lfinish_monotonic:
	add	r3,r3,r5
	add	r4,r4,r6
	cmpw	cr0,r4,r7
	cmpwi	cr1,r4,0
	blt	1f
	subf	r4,r7,r4
	addi	r3,r3,1
1:	bge	cr1, .Lfinish
	addi	r3,r3,-1
	add	r4,r4,r7

.Lfinish:
	stw	r3,TSPC32_TV_SEC(r11)
	stw	r4,TSPC32_TV_NSEC(r11)

	mtlr	r12
	crclr	cr0*4+so
	li	r3,0
	blr

	/*
	 * syscall fallback
	 */
.Lgettime_fallback:
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
	cmplwi	cr0, r3, CLOCK_MAX
	cmpwi	cr1, r3, CLOCK_REALTIME_COARSE
	cmpwi	cr7, r3, CLOCK_MONOTONIC_COARSE
	bgt	cr0, 99f
	LOAD_REG_IMMEDIATE(r5, KTIME_LOW_RES)
	beq	cr1, 1f
	beq	cr7, 1f

	mflr	r12
  .cfi_register lr,r12
	get_datapage	r3, r0
	lwz	r5, CLOCK_HRTIMER_RES(r3)
	mtlr	r12
1:	li	r3,0
	cmpli	cr0,r4,0
	crclr	cr0*4+so
	beqlr
	stw	r3,TSPC32_TV_SEC(r4)
	stw	r5,TSPC32_TV_NSEC(r4)
	blr

	/*
	 * invalid clock
	 */
99:
	li	r3, EINVAL
	crset	so
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
	get_datapage	r9, r0

	lwz	r3,STAMP_XTIME_SEC+LOPART(r9)

	cmplwi	r11,0			/* check if t is NULL */
	mtlr	r12
	crclr	cr0*4+so
	beqlr
	stw	r3,0(r11)		/* store result at *t */
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_time)

/*
 * This is the core of clock_gettime() and gettimeofday(),
 * it returns the current time in r3 (seconds) and r4.
 * On entry, r7 gives the resolution of r4, either USEC_PER_SEC
 * or NSEC_PER_SEC, giving r4 in microseconds or nanoseconds.
 * It expects the datapage ptr in r9 and doesn't clobber it.
 * It clobbers r0, r5 and r6.
 * On return, r8 contains the counter value that can be reused.
 * This clobbers cr0 but not any other cr field.
 */
__do_get_tspec:
  .cfi_startproc
	/* Check for update count & load values. We use the low
	 * order 32 bits of the update count
	 */
1:	lwz	r8,(CFG_TB_UPDATE_COUNT+LOPART)(r9)
	andi.	r0,r8,1			/* pending update ? loop */
	bne-	1b
	xor	r0,r8,r8		/* create dependency */
	add	r9,r9,r0

	/* Load orig stamp (offset to TB) */
	lwz	r5,CFG_TB_ORIG_STAMP(r9)
	lwz	r6,(CFG_TB_ORIG_STAMP+4)(r9)

	/* Get a stable TB value */
2:	MFTBU(r3)
	MFTBL(r4)
	MFTBU(r0)
	cmplw	cr0,r3,r0
	bne-	2b

	/* Subtract tb orig stamp and shift left 12 bits.
	 */
	subfc	r4,r6,r4
	subfe	r0,r5,r3
	slwi	r0,r0,12
	rlwimi.	r0,r4,12,20,31
	slwi	r4,r4,12

	/*
	 * Load scale factor & do multiplication.
	 * We only use the high 32 bits of the tb_to_xs value.
	 * Even with a 1GHz timebase clock, the high 32 bits of
	 * tb_to_xs will be at least 4 million, so the error from
	 * ignoring the low 32 bits will be no more than 0.25ppm.
	 * The error will just make the clock run very very slightly
	 * slow until the next time the kernel updates the VDSO data,
	 * at which point the clock will catch up to the kernel's value,
	 * so there is no long-term error accumulation.
	 */
	lwz	r5,CFG_TB_TO_XS(r9)	/* load values */
	mulhwu	r4,r4,r5
	li	r3,0

	beq+	4f			/* skip high part computation if 0 */
	mulhwu	r3,r0,r5
	mullw	r5,r0,r5
	addc	r4,r4,r5
	addze	r3,r3
4:
	/* At this point, we have seconds since the xtime stamp
	 * as a 32.32 fixed-point number in r3 and r4.
	 * Load & add the xtime stamp.
	 */
	lwz	r5,STAMP_XTIME_SEC+LOPART(r9)
	lwz	r6,STAMP_SEC_FRAC(r9)
	addc	r4,r4,r6
	adde	r3,r3,r5

	/* We create a fake dependency on the result in r3/r4
	 * and re-check the counter
	 */
	or	r6,r4,r3
	xor	r0,r6,r6
	add	r9,r9,r0
	lwz	r0,(CFG_TB_UPDATE_COUNT+LOPART)(r9)
        cmplw	cr0,r8,r0		/* check if updated */
	bne-	1b

	mulhwu	r4,r4,r7		/* convert to micro or nanoseconds */

	blr
  .cfi_endproc
