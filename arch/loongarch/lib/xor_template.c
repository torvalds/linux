// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 WANG Xuerui <git@xen0n.name>
 *
 * Template for XOR operations, instantiated in xor_simd.c.
 *
 * Expected preprocessor definitions:
 *
 * - LINE_WIDTH
 * - XOR_FUNC_NAME(nr)
 * - LD_INOUT_LINE(buf)
 * - LD_AND_XOR_LINE(buf)
 * - ST_LINE(buf)
 */

void XOR_FUNC_NAME(2)(unsigned long bytes,
		      unsigned long * __restrict v1,
		      const unsigned long * __restrict v2)
{
	unsigned long lines = bytes / LINE_WIDTH;

	do {
		__asm__ __volatile__ (
			LD_INOUT_LINE(v1)
			LD_AND_XOR_LINE(v2)
			ST_LINE(v1)
		: : [v1] "r"(v1), [v2] "r"(v2) : "memory"
		);

		v1 += LINE_WIDTH / sizeof(unsigned long);
		v2 += LINE_WIDTH / sizeof(unsigned long);
	} while (--lines > 0);
}

void XOR_FUNC_NAME(3)(unsigned long bytes,
		      unsigned long * __restrict v1,
		      const unsigned long * __restrict v2,
		      const unsigned long * __restrict v3)
{
	unsigned long lines = bytes / LINE_WIDTH;

	do {
		__asm__ __volatile__ (
			LD_INOUT_LINE(v1)
			LD_AND_XOR_LINE(v2)
			LD_AND_XOR_LINE(v3)
			ST_LINE(v1)
		: : [v1] "r"(v1), [v2] "r"(v2), [v3] "r"(v3) : "memory"
		);

		v1 += LINE_WIDTH / sizeof(unsigned long);
		v2 += LINE_WIDTH / sizeof(unsigned long);
		v3 += LINE_WIDTH / sizeof(unsigned long);
	} while (--lines > 0);
}

void XOR_FUNC_NAME(4)(unsigned long bytes,
		      unsigned long * __restrict v1,
		      const unsigned long * __restrict v2,
		      const unsigned long * __restrict v3,
		      const unsigned long * __restrict v4)
{
	unsigned long lines = bytes / LINE_WIDTH;

	do {
		__asm__ __volatile__ (
			LD_INOUT_LINE(v1)
			LD_AND_XOR_LINE(v2)
			LD_AND_XOR_LINE(v3)
			LD_AND_XOR_LINE(v4)
			ST_LINE(v1)
		: : [v1] "r"(v1), [v2] "r"(v2), [v3] "r"(v3), [v4] "r"(v4)
		: "memory"
		);

		v1 += LINE_WIDTH / sizeof(unsigned long);
		v2 += LINE_WIDTH / sizeof(unsigned long);
		v3 += LINE_WIDTH / sizeof(unsigned long);
		v4 += LINE_WIDTH / sizeof(unsigned long);
	} while (--lines > 0);
}

void XOR_FUNC_NAME(5)(unsigned long bytes,
		      unsigned long * __restrict v1,
		      const unsigned long * __restrict v2,
		      const unsigned long * __restrict v3,
		      const unsigned long * __restrict v4,
		      const unsigned long * __restrict v5)
{
	unsigned long lines = bytes / LINE_WIDTH;

	do {
		__asm__ __volatile__ (
			LD_INOUT_LINE(v1)
			LD_AND_XOR_LINE(v2)
			LD_AND_XOR_LINE(v3)
			LD_AND_XOR_LINE(v4)
			LD_AND_XOR_LINE(v5)
			ST_LINE(v1)
		: : [v1] "r"(v1), [v2] "r"(v2), [v3] "r"(v3), [v4] "r"(v4),
		    [v5] "r"(v5) : "memory"
		);

		v1 += LINE_WIDTH / sizeof(unsigned long);
		v2 += LINE_WIDTH / sizeof(unsigned long);
		v3 += LINE_WIDTH / sizeof(unsigned long);
		v4 += LINE_WIDTH / sizeof(unsigned long);
		v5 += LINE_WIDTH / sizeof(unsigned long);
	} while (--lines > 0);
}
