/* multi_arith.h: multi-precision integer arithmetic functions, needed
   to do extended-precision floating point.

   (c) 1998 David Huggins-Daines.

   Somewhat based on arch/alpha/math-emu/ieee-math.c, which is (c)
   David Mosberger-Tang.

   You may copy, modify, and redistribute this file under the terms of
   the GNU General Public License, version 2, or any later version, at
   your convenience. */

/* Note:

   These are not general multi-precision math routines.  Rather, they
   implement the subset of integer arithmetic that we need in order to
   multiply, divide, and normalize 128-bit unsigned mantissae.  */

#ifndef MULTI_ARITH_H
#define MULTI_ARITH_H

static inline void fp_denormalize(struct fp_ext *reg, unsigned int cnt)
{
	reg->exp += cnt;

	switch (cnt) {
	case 0 ... 8:
		reg->lowmant = reg->mant.m32[1] << (8 - cnt);
		reg->mant.m32[1] = (reg->mant.m32[1] >> cnt) |
				   (reg->mant.m32[0] << (32 - cnt));
		reg->mant.m32[0] = reg->mant.m32[0] >> cnt;
		break;
	case 9 ... 32:
		reg->lowmant = reg->mant.m32[1] >> (cnt - 8);
		if (reg->mant.m32[1] << (40 - cnt))
			reg->lowmant |= 1;
		reg->mant.m32[1] = (reg->mant.m32[1] >> cnt) |
				   (reg->mant.m32[0] << (32 - cnt));
		reg->mant.m32[0] = reg->mant.m32[0] >> cnt;
		break;
	case 33 ... 39:
		asm volatile ("bfextu %1{%2,#8},%0" : "=d" (reg->lowmant)
			: "m" (reg->mant.m32[0]), "d" (64 - cnt));
		if (reg->mant.m32[1] << (40 - cnt))
			reg->lowmant |= 1;
		reg->mant.m32[1] = reg->mant.m32[0] >> (cnt - 32);
		reg->mant.m32[0] = 0;
		break;
	case 40 ... 71:
		reg->lowmant = reg->mant.m32[0] >> (cnt - 40);
		if ((reg->mant.m32[0] << (72 - cnt)) || reg->mant.m32[1])
			reg->lowmant |= 1;
		reg->mant.m32[1] = reg->mant.m32[0] >> (cnt - 32);
		reg->mant.m32[0] = 0;
		break;
	default:
		reg->lowmant = reg->mant.m32[0] || reg->mant.m32[1];
		reg->mant.m32[0] = 0;
		reg->mant.m32[1] = 0;
		break;
	}
}

static inline int fp_overnormalize(struct fp_ext *reg)
{
	int shift;

	if (reg->mant.m32[0]) {
		asm ("bfffo %1{#0,#32},%0" : "=d" (shift) : "dm" (reg->mant.m32[0]));
		reg->mant.m32[0] = (reg->mant.m32[0] << shift) | (reg->mant.m32[1] >> (32 - shift));
		reg->mant.m32[1] = (reg->mant.m32[1] << shift);
	} else {
		asm ("bfffo %1{#0,#32},%0" : "=d" (shift) : "dm" (reg->mant.m32[1]));
		reg->mant.m32[0] = (reg->mant.m32[1] << shift);
		reg->mant.m32[1] = 0;
		shift += 32;
	}

	return shift;
}

static inline int fp_addmant(struct fp_ext *dest, struct fp_ext *src)
{
	int carry;

	/* we assume here, gcc only insert move and a clr instr */
	asm volatile ("add.b %1,%0" : "=d,g" (dest->lowmant)
		: "g,d" (src->lowmant), "0,0" (dest->lowmant));
	asm volatile ("addx.l %1,%0" : "=d" (dest->mant.m32[1])
		: "d" (src->mant.m32[1]), "0" (dest->mant.m32[1]));
	asm volatile ("addx.l %1,%0" : "=d" (dest->mant.m32[0])
		: "d" (src->mant.m32[0]), "0" (dest->mant.m32[0]));
	asm volatile ("addx.l %0,%0" : "=d" (carry) : "0" (0));

	return carry;
}

static inline int fp_addcarry(struct fp_ext *reg)
{
	if (++reg->exp == 0x7fff) {
		if (reg->mant.m64)
			fp_set_sr(FPSR_EXC_INEX2);
		reg->mant.m64 = 0;
		fp_set_sr(FPSR_EXC_OVFL);
		return 0;
	}
	reg->lowmant = (reg->mant.m32[1] << 7) | (reg->lowmant ? 1 : 0);
	reg->mant.m32[1] = (reg->mant.m32[1] >> 1) |
			   (reg->mant.m32[0] << 31);
	reg->mant.m32[0] = (reg->mant.m32[0] >> 1) | 0x80000000;

	return 1;
}

static inline void fp_submant(struct fp_ext *dest, struct fp_ext *src1,
			      struct fp_ext *src2)
{
	/* we assume here, gcc only insert move and a clr instr */
	asm volatile ("sub.b %1,%0" : "=d,g" (dest->lowmant)
		: "g,d" (src2->lowmant), "0,0" (src1->lowmant));
	asm volatile ("subx.l %1,%0" : "=d" (dest->mant.m32[1])
		: "d" (src2->mant.m32[1]), "0" (src1->mant.m32[1]));
	asm volatile ("subx.l %1,%0" : "=d" (dest->mant.m32[0])
		: "d" (src2->mant.m32[0]), "0" (src1->mant.m32[0]));
}

#define fp_mul64(desth, destl, src1, src2) ({				\
	asm ("mulu.l %2,%1:%0" : "=d" (destl), "=d" (desth)		\
		: "dm" (src1), "0" (src2));				\
})
#define fp_div64(quot, rem, srch, srcl, div)				\
	asm ("divu.l %2,%1:%0" : "=d" (quot), "=d" (rem)		\
		: "dm" (div), "1" (srch), "0" (srcl))
#define fp_add64(dest1, dest2, src1, src2) ({				\
	asm ("add.l %1,%0" : "=d,dm" (dest2)				\
		: "dm,d" (src2), "0,0" (dest2));			\
	asm ("addx.l %1,%0" : "=d" (dest1)				\
		: "d" (src1), "0" (dest1));				\
})
#define fp_addx96(dest, src) ({						\
	/* we assume here, gcc only insert move and a clr instr */	\
	asm volatile ("add.l %1,%0" : "=d,g" (dest->m32[2])		\
		: "g,d" (temp.m32[1]), "0,0" (dest->m32[2]));		\
	asm volatile ("addx.l %1,%0" : "=d" (dest->m32[1])		\
		: "d" (temp.m32[0]), "0" (dest->m32[1]));		\
	asm volatile ("addx.l %1,%0" : "=d" (dest->m32[0])		\
		: "d" (0), "0" (dest->m32[0]));				\
})
#define fp_sub64(dest, src) ({						\
	asm ("sub.l %1,%0" : "=d,dm" (dest.m32[1])			\
		: "dm,d" (src.m32[1]), "0,0" (dest.m32[1]));		\
	asm ("subx.l %1,%0" : "=d" (dest.m32[0])			\
		: "d" (src.m32[0]), "0" (dest.m32[0]));			\
})
#define fp_sub96c(dest, srch, srcm, srcl) ({				\
	char carry;							\
	asm ("sub.l %1,%0" : "=d,dm" (dest.m32[2])			\
		: "dm,d" (srcl), "0,0" (dest.m32[2]));			\
	asm ("subx.l %1,%0" : "=d" (dest.m32[1])			\
		: "d" (srcm), "0" (dest.m32[1]));			\
	asm ("subx.l %2,%1; scs %0" : "=d" (carry), "=d" (dest.m32[0])	\
		: "d" (srch), "1" (dest.m32[0]));			\
	carry;								\
})

static inline void fp_multiplymant(union fp_mant128 *dest, struct fp_ext *src1,
				   struct fp_ext *src2)
{
	union fp_mant64 temp;

	fp_mul64(dest->m32[0], dest->m32[1], src1->mant.m32[0], src2->mant.m32[0]);
	fp_mul64(dest->m32[2], dest->m32[3], src1->mant.m32[1], src2->mant.m32[1]);

	fp_mul64(temp.m32[0], temp.m32[1], src1->mant.m32[0], src2->mant.m32[1]);
	fp_addx96(dest, temp);

	fp_mul64(temp.m32[0], temp.m32[1], src1->mant.m32[1], src2->mant.m32[0]);
	fp_addx96(dest, temp);
}

static inline void fp_dividemant(union fp_mant128 *dest, struct fp_ext *src,
				 struct fp_ext *div)
{
	union fp_mant128 tmp;
	union fp_mant64 tmp64;
	unsigned long *mantp = dest->m32;
	unsigned long fix, rem, first, dummy;
	int i;

	/* the algorithm below requires dest to be smaller than div,
	   but both have the high bit set */
	if (src->mant.m64 >= div->mant.m64) {
		fp_sub64(src->mant, div->mant);
		*mantp = 1;
	} else
		*mantp = 0;
	mantp++;

	/* basic idea behind this algorithm: we can't divide two 64bit numbers
	   (AB/CD) directly, but we can calculate AB/C0, but this means this
	   quotient is off by C0/CD, so we have to multiply the first result
	   to fix the result, after that we have nearly the correct result
	   and only a few corrections are needed. */

	/* C0/CD can be precalculated, but it's an 64bit division again, but
	   we can make it a bit easier, by dividing first through C so we get
	   10/1D and now only a single shift and the value fits into 32bit. */
	fix = 0x80000000;
	dummy = div->mant.m32[1] / div->mant.m32[0] + 1;
	dummy = (dummy >> 1) | fix;
	fp_div64(fix, dummy, fix, 0, dummy);
	fix--;

	for (i = 0; i < 3; i++, mantp++) {
		if (src->mant.m32[0] == div->mant.m32[0]) {
			fp_div64(first, rem, 0, src->mant.m32[1], div->mant.m32[0]);

			fp_mul64(*mantp, dummy, first, fix);
			*mantp += fix;
		} else {
			fp_div64(first, rem, src->mant.m32[0], src->mant.m32[1], div->mant.m32[0]);

			fp_mul64(*mantp, dummy, first, fix);
		}

		fp_mul64(tmp.m32[0], tmp.m32[1], div->mant.m32[0], first - *mantp);
		fp_add64(tmp.m32[0], tmp.m32[1], 0, rem);
		tmp.m32[2] = 0;

		fp_mul64(tmp64.m32[0], tmp64.m32[1], *mantp, div->mant.m32[1]);
		fp_sub96c(tmp, 0, tmp64.m32[0], tmp64.m32[1]);

		src->mant.m32[0] = tmp.m32[1];
		src->mant.m32[1] = tmp.m32[2];

		while (!fp_sub96c(tmp, 0, div->mant.m32[0], div->mant.m32[1])) {
			src->mant.m32[0] = tmp.m32[1];
			src->mant.m32[1] = tmp.m32[2];
			*mantp += 1;
		}
	}
}

static inline void fp_putmant128(struct fp_ext *dest, union fp_mant128 *src,
				 int shift)
{
	unsigned long tmp;

	switch (shift) {
	case 0:
		dest->mant.m64 = src->m64[0];
		dest->lowmant = src->m32[2] >> 24;
		if (src->m32[3] || (src->m32[2] << 8))
			dest->lowmant |= 1;
		break;
	case 1:
		asm volatile ("lsl.l #1,%0"
			: "=d" (tmp) : "0" (src->m32[2]));
		asm volatile ("roxl.l #1,%0"
			: "=d" (dest->mant.m32[1]) : "0" (src->m32[1]));
		asm volatile ("roxl.l #1,%0"
			: "=d" (dest->mant.m32[0]) : "0" (src->m32[0]));
		dest->lowmant = tmp >> 24;
		if (src->m32[3] || (tmp << 8))
			dest->lowmant |= 1;
		break;
	case 31:
		asm volatile ("lsr.l #1,%1; roxr.l #1,%0"
			: "=d" (dest->mant.m32[0])
			: "d" (src->m32[0]), "0" (src->m32[1]));
		asm volatile ("roxr.l #1,%0"
			: "=d" (dest->mant.m32[1]) : "0" (src->m32[2]));
		asm volatile ("roxr.l #1,%0"
			: "=d" (tmp) : "0" (src->m32[3]));
		dest->lowmant = tmp >> 24;
		if (src->m32[3] << 7)
			dest->lowmant |= 1;
		break;
	case 32:
		dest->mant.m32[0] = src->m32[1];
		dest->mant.m32[1] = src->m32[2];
		dest->lowmant = src->m32[3] >> 24;
		if (src->m32[3] << 8)
			dest->lowmant |= 1;
		break;
	}
}

#endif	/* MULTI_ARITH_H */
