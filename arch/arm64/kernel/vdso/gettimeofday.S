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
seqcnt		.req	w7
w_tmp		.req	w8
x_tmp		.req	x8

/*
 * Conventions for macro arguments:
 * - An argument is write-only if its name starts with "res".
 * - All other arguments are read-only, unless otherwise specified.
 */

	.macro	seqcnt_acquire
9999:	ldr	seqcnt, [vdso_data, #VDSO_TB_SEQ_COUNT]
	tbnz	seqcnt, #0, 9999b
	dmb	ishld
	.endm

	.macro	seqcnt_check fail
	dmb	ishld
	ldr	w_tmp, [vdso_data, #VDSO_TB_SEQ_COUNT]
	cmp	w_tmp, seqcnt
	b.ne	\fail
	.endm

	.macro	syscall_check fail
	ldr	w_tmp, [vdso_data, #VDSO_USE_SYSCALL]
	cbnz	w_tmp, \fail
	.endm

	.macro get_nsec_per_sec res
	mov	\res, #NSEC_PER_SEC_LO16
	movk	\res, #NSEC_PER_SEC_HI16, lsl #16
	.endm

	/*
	 * Returns the clock delta, in nanoseconds left-shifted by the clock
	 * shift.
	 */
	.macro	get_clock_shifted_nsec res, cycle_last, mult
	/* Read the virtual counter. */
	isb
	mrs	x_tmp, cntvct_el0
	/* Calculate cycle delta and convert to ns. */
	sub	\res, x_tmp, \cycle_last
	/* We can only guarantee 56 bits of precision. */
	movn	x_tmp, #0xff00, lsl #48
	and	\res, x_tmp, \res
	mul	\res, \res, \mult
	.endm

	/*
	 * Returns in res_{sec,nsec} the REALTIME timespec, based on the
	 * "wall time" (xtime) and the clock_mono delta.
	 */
	.macro	get_ts_realtime res_sec, res_nsec, \
			clock_nsec, xtime_sec, xtime_nsec, nsec_to_sec
	add	\res_nsec, \clock_nsec, \xtime_nsec
	udiv	x_tmp, \res_nsec, \nsec_to_sec
	add	\res_sec, \xtime_sec, x_tmp
	msub	\res_nsec, x_tmp, \nsec_to_sec, \res_nsec
	.endm

	/*
	 * Returns in res_{sec,nsec} the timespec based on the clock_raw delta,
	 * used for CLOCK_MONOTONIC_RAW.
	 */
	.macro	get_ts_clock_raw res_sec, res_nsec, clock_nsec, nsec_to_sec
	udiv	\res_sec, \clock_nsec, \nsec_to_sec
	msub	\res_nsec, \res_sec, \nsec_to_sec, \clock_nsec
	.endm

	/* sec and nsec are modified in place. */
	.macro add_ts sec, nsec, ts_sec, ts_nsec, nsec_to_sec
	/* Add timespec. */
	add	\sec, \sec, \ts_sec
	add	\nsec, \nsec, \ts_nsec

	/* Normalise the new timespec. */
	cmp	\nsec, \nsec_to_sec
	b.lt	9999f
	sub	\nsec, \nsec, \nsec_to_sec
	add	\sec, \sec, #1
9999:
	cmp	\nsec, #0
	b.ge	9998f
	add	\nsec, \nsec, \nsec_to_sec
	sub	\sec, \sec, #1
9998:
	.endm

	.macro clock_gettime_return, shift=0
	.if \shift == 1
	lsr	x11, x11, x12
	.endif
	stp	x10, x11, [x1, #TSPEC_TV_SEC]
	mov	x0, xzr
	ret
	.endm

	.macro jump_slot jumptable, index, label
	.if (. - \jumptable) != 4 * (\index)
	.error "Jump slot index mismatch"
	.endif
	b	\label
	.endm

	.text

/* int __kernel_gettimeofday(struct timeval *tv, struct timezone *tz); */
ENTRY(__kernel_gettimeofday)
	.cfi_startproc
	adr	vdso_data, _vdso_data
	/* If tv is NULL, skip to the timezone code. */
	cbz	x0, 2f

	/* Compute the time of day. */
1:	seqcnt_acquire
	syscall_check fail=4f
	ldr	x10, [vdso_data, #VDSO_CS_CYCLE_LAST]
	/* w11 = cs_mono_mult, w12 = cs_shift */
	ldp	w11, w12, [vdso_data, #VDSO_CS_MONO_MULT]
	ldp	x13, x14, [vdso_data, #VDSO_XTIME_CLK_SEC]
	seqcnt_check fail=1b

	get_nsec_per_sec res=x9
	lsl	x9, x9, x12

	get_clock_shifted_nsec res=x15, cycle_last=x10, mult=x11
	get_ts_realtime res_sec=x10, res_nsec=x11, \
		clock_nsec=x15, xtime_sec=x13, xtime_nsec=x14, nsec_to_sec=x9

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
	ret
4:
	/* Syscall fallback. */
	mov	x8, #__NR_gettimeofday
	svc	#0
	ret
	.cfi_endproc
ENDPROC(__kernel_gettimeofday)

#define JUMPSLOT_MAX CLOCK_MONOTONIC_COARSE

/* int __kernel_clock_gettime(clockid_t clock_id, struct timespec *tp); */
ENTRY(__kernel_clock_gettime)
	.cfi_startproc
	cmp	w0, #JUMPSLOT_MAX
	b.hi	syscall
	adr	vdso_data, _vdso_data
	adr	x_tmp, jumptable
	add	x_tmp, x_tmp, w0, uxtw #2
	br	x_tmp

	ALIGN
jumptable:
	jump_slot jumptable, CLOCK_REALTIME, realtime
	jump_slot jumptable, CLOCK_MONOTONIC, monotonic
	b	syscall
	b	syscall
	jump_slot jumptable, CLOCK_MONOTONIC_RAW, monotonic_raw
	jump_slot jumptable, CLOCK_REALTIME_COARSE, realtime_coarse
	jump_slot jumptable, CLOCK_MONOTONIC_COARSE, monotonic_coarse

	.if (. - jumptable) != 4 * (JUMPSLOT_MAX + 1)
	.error	"Wrong jumptable size"
	.endif

	ALIGN
realtime:
	seqcnt_acquire
	syscall_check fail=syscall
	ldr	x10, [vdso_data, #VDSO_CS_CYCLE_LAST]
	/* w11 = cs_mono_mult, w12 = cs_shift */
	ldp	w11, w12, [vdso_data, #VDSO_CS_MONO_MULT]
	ldp	x13, x14, [vdso_data, #VDSO_XTIME_CLK_SEC]
	seqcnt_check fail=realtime

	/* All computations are done with left-shifted nsecs. */
	get_nsec_per_sec res=x9
	lsl	x9, x9, x12

	get_clock_shifted_nsec res=x15, cycle_last=x10, mult=x11
	get_ts_realtime res_sec=x10, res_nsec=x11, \
		clock_nsec=x15, xtime_sec=x13, xtime_nsec=x14, nsec_to_sec=x9
	clock_gettime_return, shift=1

	ALIGN
monotonic:
	seqcnt_acquire
	syscall_check fail=syscall
	ldr	x10, [vdso_data, #VDSO_CS_CYCLE_LAST]
	/* w11 = cs_mono_mult, w12 = cs_shift */
	ldp	w11, w12, [vdso_data, #VDSO_CS_MONO_MULT]
	ldp	x13, x14, [vdso_data, #VDSO_XTIME_CLK_SEC]
	ldp	x3, x4, [vdso_data, #VDSO_WTM_CLK_SEC]
	seqcnt_check fail=monotonic

	/* All computations are done with left-shifted nsecs. */
	lsl	x4, x4, x12
	get_nsec_per_sec res=x9
	lsl	x9, x9, x12

	get_clock_shifted_nsec res=x15, cycle_last=x10, mult=x11
	get_ts_realtime res_sec=x10, res_nsec=x11, \
		clock_nsec=x15, xtime_sec=x13, xtime_nsec=x14, nsec_to_sec=x9

	add_ts sec=x10, nsec=x11, ts_sec=x3, ts_nsec=x4, nsec_to_sec=x9
	clock_gettime_return, shift=1

	ALIGN
monotonic_raw:
	seqcnt_acquire
	syscall_check fail=syscall
	ldr	x10, [vdso_data, #VDSO_CS_CYCLE_LAST]
	/* w11 = cs_raw_mult, w12 = cs_shift */
	ldp	w12, w11, [vdso_data, #VDSO_CS_SHIFT]
	ldp	x13, x14, [vdso_data, #VDSO_RAW_TIME_SEC]
	seqcnt_check fail=monotonic_raw

	/* All computations are done with left-shifted nsecs. */
	get_nsec_per_sec res=x9
	lsl	x9, x9, x12

	get_clock_shifted_nsec res=x15, cycle_last=x10, mult=x11
	get_ts_clock_raw res_sec=x10, res_nsec=x11, \
		clock_nsec=x15, nsec_to_sec=x9

	add_ts sec=x10, nsec=x11, ts_sec=x13, ts_nsec=x14, nsec_to_sec=x9
	clock_gettime_return, shift=1

	ALIGN
realtime_coarse:
	seqcnt_acquire
	ldp	x10, x11, [vdso_data, #VDSO_XTIME_CRS_SEC]
	seqcnt_check fail=realtime_coarse
	clock_gettime_return

	ALIGN
monotonic_coarse:
	seqcnt_acquire
	ldp	x10, x11, [vdso_data, #VDSO_XTIME_CRS_SEC]
	ldp	x13, x14, [vdso_data, #VDSO_WTM_CLK_SEC]
	seqcnt_check fail=monotonic_coarse

	/* Computations are done in (non-shifted) nsecs. */
	get_nsec_per_sec res=x9
	add_ts sec=x10, nsec=x11, ts_sec=x13, ts_nsec=x14, nsec_to_sec=x9
	clock_gettime_return

	ALIGN
syscall: /* Syscall fallback. */
	mov	x8, #__NR_clock_gettime
	svc	#0
	ret
	.cfi_endproc
ENDPROC(__kernel_clock_gettime)

/* int __kernel_clock_getres(clockid_t clock_id, struct timespec *res); */
ENTRY(__kernel_clock_getres)
	.cfi_startproc
	cmp	w0, #CLOCK_REALTIME
	ccmp	w0, #CLOCK_MONOTONIC, #0x4, ne
	ccmp	w0, #CLOCK_MONOTONIC_RAW, #0x4, ne
	b.ne	1f

	adr	vdso_data, _vdso_data
	ldr	w2, [vdso_data, #CLOCK_REALTIME_RES]
	b	2f
1:
	cmp	w0, #CLOCK_REALTIME_COARSE
	ccmp	w0, #CLOCK_MONOTONIC_COARSE, #0x4, ne
	b.ne	4f
	ldr	x2, 5f
2:
	cbz	x1, 3f
	stp	xzr, x2, [x1]

3:	/* res == NULL. */
	mov	w0, wzr
	ret

4:	/* Syscall fallback. */
	mov	x8, #__NR_clock_getres
	svc	#0
	ret
5:
	.quad	CLOCK_COARSE_RES
	.cfi_endproc
ENDPROC(__kernel_clock_getres)
