/*
 * Userspace implementations of gettimeofday() and friends.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

#define NSEC_PER_SEC_LO16	0xca00
#define NSEC_PER_SEC_HI16	0x3b9a

vdso_data	.req	x6
use_syscall	.req	w7
seqcnt		.req	w8

	.macro	seqcnt_acquire
9999:	ldr	seqcnt, [vdso_data, #VDSO_TB_SEQ_COUNT]
	tbnz	seqcnt, #0, 9999b
	dmb	ishld
	ldr	use_syscall, [vdso_data, #VDSO_USE_SYSCALL]
	.endm

	.macro	seqcnt_read, cnt
	dmb	ishld
	ldr	\cnt, [vdso_data, #VDSO_TB_SEQ_COUNT]
	.endm

	.macro	seqcnt_check, cnt, fail
	cmp	\cnt, seqcnt
	b.ne	\fail
	.endm

	.text

/* int __kernel_gettimeofday(struct timeval *tv, struct timezone *tz); */
ENTRY(__kernel_gettimeofday)
	.cfi_startproc
	mov	x2, x30
	.cfi_register x30, x2

	/* Acquire the sequence counter and get the timespec. */
	adr	vdso_data, _vdso_data
1:	seqcnt_acquire
	cbnz	use_syscall, 4f

	/* If tv is NULL, skip to the timezone code. */
	cbz	x0, 2f
	bl	__do_get_tspec
	seqcnt_check w9, 1b

	/* Convert ns to us. */
	mov	x13, #1000
	lsl	x13, x13, x12
	udiv	x11, x11, x13
	stp	x10, x11, [x0, #TVAL_TV_SEC]
2:
	/* If tz is NULL, return 0. */
	cbz	x1, 3f
	ldp	w4, w5, [vdso_data, #VDSO_TZ_MINWEST]
	stp	w4, w5, [x1, #TZ_MINWEST]
3:
	mov	x0, xzr
	ret	x2
4:
	/* Syscall fallback. */
	mov	x8, #__NR_gettimeofday
	svc	#0
	ret	x2
	.cfi_endproc
ENDPROC(__kernel_gettimeofday)

/* int __kernel_clock_gettime(clockid_t clock_id, struct timespec *tp); */
ENTRY(__kernel_clock_gettime)
	.cfi_startproc
	cmp	w0, #CLOCK_REALTIME
	ccmp	w0, #CLOCK_MONOTONIC, #0x4, ne
	b.ne	2f

	mov	x2, x30
	.cfi_register x30, x2

	/* Get kernel timespec. */
	adr	vdso_data, _vdso_data
1:	seqcnt_acquire
	cbnz	use_syscall, 7f

	bl	__do_get_tspec
	seqcnt_check w9, 1b

	cmp	w0, #CLOCK_MONOTONIC
	b.ne	6f

	/* Get wtm timespec. */
	ldp	x13, x14, [vdso_data, #VDSO_WTM_CLK_SEC]

	/* Check the sequence counter. */
	seqcnt_read w9
	seqcnt_check w9, 1b
	b	4f
2:
	cmp	w0, #CLOCK_REALTIME_COARSE
	ccmp	w0, #CLOCK_MONOTONIC_COARSE, #0x4, ne
	b.ne	8f

	/* Get coarse timespec. */
	adr	vdso_data, _vdso_data
3:	seqcnt_acquire
	ldp	x10, x11, [vdso_data, #VDSO_XTIME_CRS_SEC]

	/* Get wtm timespec. */
	ldp	x13, x14, [vdso_data, #VDSO_WTM_CLK_SEC]

	/* Check the sequence counter. */
	seqcnt_read w9
	seqcnt_check w9, 3b

	cmp	w0, #CLOCK_MONOTONIC_COARSE
	b.ne	6f
4:
	/* Add on wtm timespec. */
	add	x10, x10, x13
	lsl	x14, x14, x12
	add	x11, x11, x14

	/* Normalise the new timespec. */
	mov	x15, #NSEC_PER_SEC_LO16
	movk	x15, #NSEC_PER_SEC_HI16, lsl #16
	lsl	x15, x15, x12
	cmp	x11, x15
	b.lt	5f
	sub	x11, x11, x15
	add	x10, x10, #1
5:
	cmp	x11, #0
	b.ge	6f
	add	x11, x11, x15
	sub	x10, x10, #1

6:	/* Store to the user timespec. */
	lsr	x11, x11, x12
	stp	x10, x11, [x1, #TSPEC_TV_SEC]
	mov	x0, xzr
	ret	x2
7:
	mov	x30, x2
8:	/* Syscall fallback. */
	mov	x8, #__NR_clock_gettime
	svc	#0
	ret
	.cfi_endproc
ENDPROC(__kernel_clock_gettime)

/* int __kernel_clock_getres(clockid_t clock_id, struct timespec *res); */
ENTRY(__kernel_clock_getres)
	.cfi_startproc
	cbz	w1, 3f

	cmp	w0, #CLOCK_REALTIME
	ccmp	w0, #CLOCK_MONOTONIC, #0x4, ne
	b.ne	1f

	ldr	x2, 5f
	b	2f
1:
	cmp	w0, #CLOCK_REALTIME_COARSE
	ccmp	w0, #CLOCK_MONOTONIC_COARSE, #0x4, ne
	b.ne	4f
	ldr	x2, 6f
2:
	stp	xzr, x2, [x1]

3:	/* res == NULL. */
	mov	w0, wzr
	ret

4:	/* Syscall fallback. */
	mov	x8, #__NR_clock_getres
	svc	#0
	ret
5:
	.quad	CLOCK_REALTIME_RES
6:
	.quad	CLOCK_COARSE_RES
	.cfi_endproc
ENDPROC(__kernel_clock_getres)

/*
 * Read the current time from the architected counter.
 * Expects vdso_data to be initialised.
 * Clobbers the temporary registers (x9 - x15).
 * Returns:
 *  - w9		= vDSO sequence counter
 *  - (x10, x11)	= (ts->tv_sec, shifted ts->tv_nsec)
 *  - w12		= cs_shift
 */
ENTRY(__do_get_tspec)
	.cfi_startproc

	/* Read from the vDSO data page. */
	ldr	x10, [vdso_data, #VDSO_CS_CYCLE_LAST]
	ldp	x13, x14, [vdso_data, #VDSO_XTIME_CLK_SEC]
	ldp	w11, w12, [vdso_data, #VDSO_CS_MULT]
	seqcnt_read w9

	/* Read the virtual counter. */
	isb
	mrs	x15, cntvct_el0

	/* Calculate cycle delta and convert to ns. */
	sub	x10, x15, x10
	/* We can only guarantee 56 bits of precision. */
	movn	x15, #0xff00, lsl #48
	and	x10, x15, x10
	mul	x10, x10, x11

	/* Use the kernel time to calculate the new timespec. */
	mov	x11, #NSEC_PER_SEC_LO16
	movk	x11, #NSEC_PER_SEC_HI16, lsl #16
	lsl	x11, x11, x12
	add	x15, x10, x14
	udiv	x14, x15, x11
	add	x10, x13, x14
	mul	x13, x14, x11
	sub	x11, x15, x13

	ret
	.cfi_endproc
ENDPROC(__do_get_tspec)
