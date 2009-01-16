/*
 * Userland implementation of gettimeofday() for 32 bits processes in a
 * ppc64 kernel for use in the vDSO
 *
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org,
 *                    IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/vdso.h>
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

	mr	r10,r3			/* r10 saves tv */
	mr	r11,r4			/* r11 saves tz */
	bl	__get_datapage@local	/* get data page */
	mr	r9, r3			/* datapage ptr in r9 */
	cmplwi	r10,0			/* check if tv is NULL */
	beq	3f
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

3:	cmplwi	r11,0			/* check if tz is NULL */
	beq	1f
	lwz	r4,CFG_TZ_MINUTEWEST(r9)/* fill tz */
	lwz	r5,CFG_TZ_DSTTIME(r9)
	stw	r4,TZONE_TZ_MINWEST(r11)
	stw	r5,TZONE_TZ_DSTTIME(r11)

1:	mtlr	r12
	crclr	cr0*4+so
	li	r3,0
	blr

2:
	mtlr	r12
	mr	r3,r10
	mr	r4,r11
	li	r0,__NR_gettimeofday
	sc
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
	bne	cr0,99f

	mflr	r12			/* r12 saves lr */
  .cfi_register lr,r12
	mr	r11,r4			/* r11 saves tp */
	bl	__get_datapage@local	/* get data page */
	mr	r9,r3			/* datapage ptr in r9 */

50:	bl	__do_get_tspec@local	/* get sec/nsec from tb & kernel */
	bne	cr1,80f			/* not monotonic -> all done */

	/*
	 * CLOCK_MONOTONIC
	 */

	/* now we must fixup using wall to monotonic. We need to snapshot
	 * that value and do the counter trick again. Fortunately, we still
	 * have the counter value in r8 that was returned by __do_get_xsec.
	 * At this point, r3,r4 contain our sec/nsec values, r5 and r6
	 * can be used, r7 contains NSEC_PER_SEC.
	 */

	lwz	r5,WTOM_CLOCK_SEC(r9)
	lwz	r6,WTOM_CLOCK_NSEC(r9)

	/* We now have our offset in r5,r6. We create a fake dependency
	 * on that value and re-check the counter
	 */
	or	r0,r6,r5
	xor	r0,r0,r0
	add	r9,r9,r0
	lwz	r0,(CFG_TB_UPDATE_COUNT+LOPART)(r9)
        cmpl    cr0,r8,r0		/* check if updated */
	bne-	50b

	/* Calculate and store result. Note that this mimics the C code,
	 * which may cause funny results if nsec goes negative... is that
	 * possible at all ?
	 */
	add	r3,r3,r5
	add	r4,r4,r6
	cmpw	cr0,r4,r7
	cmpwi	cr1,r4,0
	blt	1f
	subf	r4,r7,r4
	addi	r3,r3,1
1:	bge	cr1,80f
	addi	r3,r3,-1
	add	r4,r4,r7

80:	stw	r3,TSPC32_TV_SEC(r11)
	stw	r4,TSPC32_TV_NSEC(r11)

	mtlr	r12
	crclr	cr0*4+so
	li	r3,0
	blr

	/*
	 * syscall fallback
	 */
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
	cror	cr0*4+eq,cr0*4+eq,cr1*4+eq
	bne	cr0,99f

	li	r3,0
	cmpli	cr0,r4,0
	crclr	cr0*4+so
	beqlr
	lis	r5,CLOCK_REALTIME_RES@h
	ori	r5,r5,CLOCK_REALTIME_RES@l
	stw	r3,TSPC32_TV_SEC(r4)
	stw	r5,TSPC32_TV_NSEC(r4)
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
 * This is the core of gettimeofday() & friends, it returns the xsec
 * value in r3 & r4 and expects the datapage ptr (non clobbered)
 * in r9. clobbers r0,r4,r5,r6,r7,r8.
 * When returning, r8 contains the counter value that can be reused
 * by the monotonic clock implementation
 */
__do_get_xsec:
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
2:	mftbu	r3
	mftbl	r4
	mftbu	r0
	cmpl	cr0,r3,r0
	bne-	2b

	/* Substract tb orig stamp. If the high part is non-zero, we jump to
	 * the slow path which call the syscall.
	 * If it's ok, then we have our 32 bits tb_ticks value in r7
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
	or	r6,r4,r3
	xor	r0,r6,r6
	add	r9,r9,r0
	lwz	r0,(CFG_TB_UPDATE_COUNT+LOPART)(r9)
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

/*
 * This is the core of clock_gettime(), it returns the current
 * time in seconds and nanoseconds in r3 and r4.
 * It expects the datapage ptr in r9 and doesn't clobber it.
 * It clobbers r0, r5, r6, r10 and returns NSEC_PER_SEC in r7.
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
2:	mftbu	r3
	mftbl	r4
	mftbu	r0
	cmpl	cr0,r3,r0
	bne-	2b

	/* Subtract tb orig stamp and shift left 12 bits.
	 */
	subfc	r7,r6,r4
	subfe	r0,r5,r3
	slwi	r0,r0,12
	rlwimi.	r0,r7,12,20,31
	slwi	r7,r7,12

	/* Load scale factor & do multiplication */
	lwz	r5,CFG_TB_TO_XS(r9)	/* load values */
	lwz	r6,(CFG_TB_TO_XS+4)(r9)
	mulhwu	r3,r7,r6
	mullw	r10,r7,r5
	mulhwu	r4,r7,r5
	addc	r10,r3,r10
	li	r3,0

	beq+	4f			/* skip high part computation if 0 */
	mulhwu	r3,r0,r5
	mullw	r7,r0,r5
	mulhwu	r5,r0,r6
	mullw	r6,r0,r6
	adde	r4,r4,r7
	addze	r3,r3
	addc	r4,r4,r5
	addze	r3,r3
	addc	r10,r10,r6

4:	addze	r4,r4			/* add in carry */
	lis	r7,NSEC_PER_SEC@h
	ori	r7,r7,NSEC_PER_SEC@l
	mulhwu	r4,r4,r7		/* convert to nanoseconds */

	/* At this point, we have seconds & nanoseconds since the xtime
	 * stamp in r3+CA and r4.  Load & add the xtime stamp.
	 */
#ifdef CONFIG_PPC64
	lwz	r5,STAMP_XTIME+TSPC64_TV_SEC+LOPART(r9)
	lwz	r6,STAMP_XTIME+TSPC64_TV_NSEC+LOPART(r9)
#else
	lwz	r5,STAMP_XTIME+TSPC32_TV_SEC(r9)
	lwz	r6,STAMP_XTIME+TSPC32_TV_NSEC(r9)
#endif
	add	r4,r4,r6
	adde	r3,r3,r5

	/* We now have our result in r3,r4. We create a fake dependency
	 * on that result and re-check the counter
	 */
	or	r6,r4,r3
	xor	r0,r6,r6
	add	r9,r9,r0
	lwz	r0,(CFG_TB_UPDATE_COUNT+LOPART)(r9)
        cmpl    cr0,r8,r0		/* check if updated */
	bne-	1b

	/* check for nanosecond overflow and adjust if necessary */
	cmpw	r4,r7
	bltlr				/* all done if no overflow */
	subf	r4,r7,r4		/* adjust if overflow */
	addi	r3,r3,1

	blr
  .cfi_endproc
