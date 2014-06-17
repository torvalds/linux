/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/module.h>
/* EXPORT_SYMBOL() is in arch/tile/lib/exports.c since this should be asm. */

/* Must be 8 bytes in size. */
#define op_t uint64_t

/* Threshold value for when to enter the unrolled loops. */
#define	OP_T_THRES	16

#if CHIP_L2_LINE_SIZE() != 64
#error "Assumes 64 byte line size"
#endif

/* How many cache lines ahead should we prefetch? */
#define PREFETCH_LINES_AHEAD 4

/*
 * Provide "base versions" of load and store for the normal code path.
 * The kernel provides other versions for userspace copies.
 */
#define ST(p, v) (*(p) = (v))
#define LD(p) (*(p))

#ifndef USERCOPY_FUNC
#define ST1 ST
#define ST2 ST
#define ST4 ST
#define ST8 ST
#define LD1 LD
#define LD2 LD
#define LD4 LD
#define LD8 LD
#define RETVAL dstv
void *memcpy(void *__restrict dstv, const void *__restrict srcv, size_t n)
#else
/*
 * Special kernel version will provide implementation of the LDn/STn
 * macros to return a count of uncopied bytes due to mm fault.
 */
#define RETVAL 0
int __attribute__((optimize("omit-frame-pointer")))
USERCOPY_FUNC(void *__restrict dstv, const void *__restrict srcv, size_t n)
#endif
{
	char *__restrict dst1 = (char *)dstv;
	const char *__restrict src1 = (const char *)srcv;
	const char *__restrict src1_end;
	const char *__restrict prefetch;
	op_t *__restrict dst8;    /* 8-byte pointer to destination memory. */
	op_t final; /* Final bytes to write to trailing word, if any */
	long i;

	if (n < 16) {
		for (; n; n--)
			ST1(dst1++, LD1(src1++));
		return RETVAL;
	}

	/*
	 * Locate the end of source memory we will copy.  Don't
	 * prefetch past this.
	 */
	src1_end = src1 + n - 1;

	/* Prefetch ahead a few cache lines, but not past the end. */
	prefetch = src1;
	for (i = 0; i < PREFETCH_LINES_AHEAD; i++) {
		__insn_prefetch(prefetch);
		prefetch += CHIP_L2_LINE_SIZE();
		prefetch = (prefetch < src1_end) ? prefetch : src1;
	}

	/* Copy bytes until dst is word-aligned. */
	for (; (uintptr_t)dst1 & (sizeof(op_t) - 1); n--)
		ST1(dst1++, LD1(src1++));

	/* 8-byte pointer to destination memory. */
	dst8 = (op_t *)dst1;

	if (__builtin_expect((uintptr_t)src1 & (sizeof(op_t) - 1), 0)) {
		/* Unaligned copy. */

		op_t  tmp0 = 0, tmp1 = 0, tmp2, tmp3;
		const op_t *src8 = (const op_t *) ((uintptr_t)src1 &
						   -sizeof(op_t));
		const void *srci = (void *)src1;
		int m;

		m = (CHIP_L2_LINE_SIZE() << 2) -
			(((uintptr_t)dst8) & ((CHIP_L2_LINE_SIZE() << 2) - 1));
		m = (n < m) ? n : m;
		m /= sizeof(op_t);

		/* Copy until 'dst' is cache-line-aligned. */
		n -= (sizeof(op_t) * m);

		switch (m % 4) {
		case 0:
			if (__builtin_expect(!m, 0))
				goto _M0;
			tmp1 = LD8(src8++);
			tmp2 = LD8(src8++);
			goto _8B3;
		case 2:
			m += 2;
			tmp3 = LD8(src8++);
			tmp0 = LD8(src8++);
			goto _8B1;
		case 3:
			m += 1;
			tmp2 = LD8(src8++);
			tmp3 = LD8(src8++);
			goto _8B2;
		case 1:
			m--;
			tmp0 = LD8(src8++);
			tmp1 = LD8(src8++);
			if (__builtin_expect(!m, 0))
				goto _8B0;
		}

		do {
			tmp2 = LD8(src8++);
			tmp0 =  __insn_dblalign(tmp0, tmp1, srci);
			ST8(dst8++, tmp0);
_8B3:
			tmp3 = LD8(src8++);
			tmp1 = __insn_dblalign(tmp1, tmp2, srci);
			ST8(dst8++, tmp1);
_8B2:
			tmp0 = LD8(src8++);
			tmp2 = __insn_dblalign(tmp2, tmp3, srci);
			ST8(dst8++, tmp2);
_8B1:
			tmp1 = LD8(src8++);
			tmp3 = __insn_dblalign(tmp3, tmp0, srci);
			ST8(dst8++, tmp3);
			m -= 4;
		} while (m);

_8B0:
		tmp0 = __insn_dblalign(tmp0, tmp1, srci);
		ST8(dst8++, tmp0);
		src8--;

_M0:
		if (__builtin_expect(n >= CHIP_L2_LINE_SIZE(), 0)) {
			op_t tmp4, tmp5, tmp6, tmp7, tmp8;

			prefetch = ((const char *)src8) +
				CHIP_L2_LINE_SIZE() * PREFETCH_LINES_AHEAD;

			for (tmp0 = LD8(src8++); n >= CHIP_L2_LINE_SIZE();
			     n -= CHIP_L2_LINE_SIZE()) {
				/* Prefetch and advance to next line to
				   prefetch, but don't go past the end.  */
				__insn_prefetch(prefetch);

				/* Make sure prefetch got scheduled
				   earlier.  */
				__asm__ ("" : : : "memory");

				prefetch += CHIP_L2_LINE_SIZE();
				prefetch = (prefetch < src1_end) ? prefetch :
					(const char *) src8;

				tmp1 = LD8(src8++);
				tmp2 = LD8(src8++);
				tmp3 = LD8(src8++);
				tmp4 = LD8(src8++);
				tmp5 = LD8(src8++);
				tmp6 = LD8(src8++);
				tmp7 = LD8(src8++);
				tmp8 = LD8(src8++);

				tmp0 = __insn_dblalign(tmp0, tmp1, srci);
				tmp1 = __insn_dblalign(tmp1, tmp2, srci);
				tmp2 = __insn_dblalign(tmp2, tmp3, srci);
				tmp3 = __insn_dblalign(tmp3, tmp4, srci);
				tmp4 = __insn_dblalign(tmp4, tmp5, srci);
				tmp5 = __insn_dblalign(tmp5, tmp6, srci);
				tmp6 = __insn_dblalign(tmp6, tmp7, srci);
				tmp7 = __insn_dblalign(tmp7, tmp8, srci);

				__insn_wh64(dst8);

				ST8(dst8++, tmp0);
				ST8(dst8++, tmp1);
				ST8(dst8++, tmp2);
				ST8(dst8++, tmp3);
				ST8(dst8++, tmp4);
				ST8(dst8++, tmp5);
				ST8(dst8++, tmp6);
				ST8(dst8++, tmp7);

				tmp0 = tmp8;
			}
			src8--;
		}

		/* Copy the rest 8-byte chunks. */
		if (n >= sizeof(op_t)) {
			tmp0 = LD8(src8++);
			for (; n >= sizeof(op_t); n -= sizeof(op_t)) {
				tmp1 = LD8(src8++);
				tmp0 = __insn_dblalign(tmp0, tmp1, srci);
				ST8(dst8++, tmp0);
				tmp0 = tmp1;
			}
			src8--;
		}

		if (n == 0)
			return RETVAL;

		tmp0 = LD8(src8++);
		tmp1 = ((const char *)src8 <= src1_end)
			? LD8((op_t *)src8) : 0;
		final = __insn_dblalign(tmp0, tmp1, srci);

	} else {
		/* Aligned copy. */

		const op_t *__restrict src8 = (const op_t *)src1;

		/* src8 and dst8 are both word-aligned. */
		if (n >= CHIP_L2_LINE_SIZE()) {
			/* Copy until 'dst' is cache-line-aligned. */
			for (; (uintptr_t)dst8 & (CHIP_L2_LINE_SIZE() - 1);
			     n -= sizeof(op_t))
				ST8(dst8++, LD8(src8++));

			for (; n >= CHIP_L2_LINE_SIZE(); ) {
				op_t tmp0, tmp1, tmp2, tmp3;
				op_t tmp4, tmp5, tmp6, tmp7;

				/*
				 * Prefetch and advance to next line
				 * to prefetch, but don't go past the
				 * end.
				 */
				__insn_prefetch(prefetch);

				/* Make sure prefetch got scheduled
				   earlier.  */
				__asm__ ("" : : : "memory");

				prefetch += CHIP_L2_LINE_SIZE();
				prefetch = (prefetch < src1_end) ? prefetch :
					(const char *)src8;

				/*
				 * Do all the loads before wh64.  This
				 * is necessary if [src8, src8+7] and
				 * [dst8, dst8+7] share the same cache
				 * line and dst8 <= src8, as can be
				 * the case when called from memmove,
				 * or with code tested on x86 whose
				 * memcpy always works with forward
				 * copies.
				 */
				tmp0 = LD8(src8++);
				tmp1 = LD8(src8++);
				tmp2 = LD8(src8++);
				tmp3 = LD8(src8++);
				tmp4 = LD8(src8++);
				tmp5 = LD8(src8++);
				tmp6 = LD8(src8++);
				tmp7 = LD8(src8++);

				/* wh64 and wait for tmp7 load completion. */
				__asm__ ("move %0, %0; wh64 %1\n"
					 : : "r"(tmp7), "r"(dst8));

				ST8(dst8++, tmp0);
				ST8(dst8++, tmp1);
				ST8(dst8++, tmp2);
				ST8(dst8++, tmp3);
				ST8(dst8++, tmp4);
				ST8(dst8++, tmp5);
				ST8(dst8++, tmp6);
				ST8(dst8++, tmp7);

				n -= CHIP_L2_LINE_SIZE();
			}
#if CHIP_L2_LINE_SIZE() != 64
# error "Fix code that assumes particular L2 cache line size."
#endif
		}

		for (; n >= sizeof(op_t); n -= sizeof(op_t))
			ST8(dst8++, LD8(src8++));

		if (__builtin_expect(n == 0, 1))
			return RETVAL;

		final = LD8(src8);
	}

	/* n != 0 if we get here.  Write out any trailing bytes. */
	dst1 = (char *)dst8;
#ifndef __BIG_ENDIAN__
	if (n & 4) {
		ST4((uint32_t *)dst1, final);
		dst1 += 4;
		final >>= 32;
		n &= 3;
	}
	if (n & 2) {
		ST2((uint16_t *)dst1, final);
		dst1 += 2;
		final >>= 16;
		n &= 1;
	}
	if (n)
		ST1((uint8_t *)dst1, final);
#else
	if (n & 4) {
		ST4((uint32_t *)dst1, final >> 32);
		dst1 += 4;
        }
        else
        {
		final >>= 32;
        }
	if (n & 2) {
		ST2((uint16_t *)dst1, final >> 16);
		dst1 += 2;
        }
        else
        {
		final >>= 16;
        }
	if (n & 1)
		ST1((uint8_t *)dst1, final >> 8);
#endif

	return RETVAL;
}

#ifdef USERCOPY_FUNC
#undef ST1
#undef ST2
#undef ST4
#undef ST8
#undef LD1
#undef LD2
#undef LD4
#undef LD8
#undef USERCOPY_FUNC
#endif
