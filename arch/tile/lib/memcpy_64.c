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
#define __memcpy memcpy
/* EXPORT_SYMBOL() is in arch/tile/lib/exports.c since this should be asm. */

/* Must be 8 bytes in size. */
#define word_t uint64_t

#if CHIP_L2_LINE_SIZE() != 64 && CHIP_L2_LINE_SIZE() != 128
#error "Assumes 64 or 128 byte line size"
#endif

/* How many cache lines ahead should we prefetch? */
#define PREFETCH_LINES_AHEAD 3

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
int USERCOPY_FUNC(void *__restrict dstv, const void *__restrict srcv, size_t n)
#endif
{
	char *__restrict dst1 = (char *)dstv;
	const char *__restrict src1 = (const char *)srcv;
	const char *__restrict src1_end;
	const char *__restrict prefetch;
	word_t *__restrict dst8;    /* 8-byte pointer to destination memory. */
	word_t final; /* Final bytes to write to trailing word, if any */
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
		prefetch = (prefetch > src1_end) ? prefetch : src1;
	}

	/* Copy bytes until dst is word-aligned. */
	for (; (uintptr_t)dst1 & (sizeof(word_t) - 1); n--)
		ST1(dst1++, LD1(src1++));

	/* 8-byte pointer to destination memory. */
	dst8 = (word_t *)dst1;

	if (__builtin_expect((uintptr_t)src1 & (sizeof(word_t) - 1), 0)) {
		/*
		 * Misaligned copy.  Copy 8 bytes at a time, but don't
		 * bother with other fanciness.
		 *
		 * TODO: Consider prefetching and using wh64 as well.
		 */

		/* Create an aligned src8. */
		const word_t *__restrict src8 =
			(const word_t *)((uintptr_t)src1 & -sizeof(word_t));
		word_t b;

		word_t a = LD8(src8++);
		for (; n >= sizeof(word_t); n -= sizeof(word_t)) {
			b = LD8(src8++);
			a = __insn_dblalign(a, b, src1);
			ST8(dst8++, a);
			a = b;
		}

		if (n == 0)
			return RETVAL;

		b = ((const char *)src8 <= src1_end) ? *src8 : 0;

		/*
		 * Final source bytes to write to trailing partial
		 * word, if any.
		 */
		final = __insn_dblalign(a, b, src1);
	} else {
		/* Aligned copy. */

		const word_t* __restrict src8 = (const word_t *)src1;

		/* src8 and dst8 are both word-aligned. */
		if (n >= CHIP_L2_LINE_SIZE()) {
			/* Copy until 'dst' is cache-line-aligned. */
			for (; (uintptr_t)dst8 & (CHIP_L2_LINE_SIZE() - 1);
			     n -= sizeof(word_t))
				ST8(dst8++, LD8(src8++));

			for (; n >= CHIP_L2_LINE_SIZE(); ) {
				__insn_wh64(dst8);

				/*
				 * Prefetch and advance to next line
				 * to prefetch, but don't go past the end
				 */
				__insn_prefetch(prefetch);
				prefetch += CHIP_L2_LINE_SIZE();
				prefetch = (prefetch > src1_end) ? prefetch :
					(const char *)src8;

				/*
				 * Copy an entire cache line.  Manually
				 * unrolled to avoid idiosyncracies of
				 * compiler unrolling.
				 */
#define COPY_WORD(offset) ({ ST8(dst8+offset, LD8(src8+offset)); n -= 8; })
				COPY_WORD(0);
				COPY_WORD(1);
				COPY_WORD(2);
				COPY_WORD(3);
				COPY_WORD(4);
				COPY_WORD(5);
				COPY_WORD(6);
				COPY_WORD(7);
#if CHIP_L2_LINE_SIZE() == 128
				COPY_WORD(8);
				COPY_WORD(9);
				COPY_WORD(10);
				COPY_WORD(11);
				COPY_WORD(12);
				COPY_WORD(13);
				COPY_WORD(14);
				COPY_WORD(15);
#elif CHIP_L2_LINE_SIZE() != 64
# error Fix code that assumes particular L2 cache line sizes
#endif

				dst8 += CHIP_L2_LINE_SIZE() / sizeof(word_t);
				src8 += CHIP_L2_LINE_SIZE() / sizeof(word_t);
			}
		}

		for (; n >= sizeof(word_t); n -= sizeof(word_t))
			ST8(dst8++, LD8(src8++));

		if (__builtin_expect(n == 0, 1))
			return RETVAL;

		final = LD8(src8);
	}

	/* n != 0 if we get here.  Write out any trailing bytes. */
	dst1 = (char *)dst8;
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
