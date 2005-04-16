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

#if 0	/* old code... */

/* Unsigned only, because we don't need signs to multiply and divide. */
typedef unsigned int int128[4];

/* Word order */
enum {
	MSW128,
	NMSW128,
	NLSW128,
	LSW128
};

/* big-endian */
#define LO_WORD(ll) (((unsigned int *) &ll)[1])
#define HI_WORD(ll) (((unsigned int *) &ll)[0])

/* Convenience functions to stuff various integer values into int128s */

static inline void zero128(int128 a)
{
	a[LSW128] = a[NLSW128] = a[NMSW128] = a[MSW128] = 0;
}

/* Human-readable word order in the arguments */
static inline void set128(unsigned int i3, unsigned int i2, unsigned int i1,
			  unsigned int i0, int128 a)
{
	a[LSW128] = i0;
	a[NLSW128] = i1;
	a[NMSW128] = i2;
	a[MSW128] = i3;
}

/* Convenience functions (for testing as well) */
static inline void int64_to_128(unsigned long long src, int128 dest)
{
	dest[LSW128] = (unsigned int) src;
	dest[NLSW128] = src >> 32;
	dest[NMSW128] = dest[MSW128] = 0;
}

static inline void int128_to_64(const int128 src, unsigned long long *dest)
{
	*dest = src[LSW128] | (long long) src[NLSW128] << 32;
}

static inline void put_i128(const int128 a)
{
	printk("%08x %08x %08x %08x\n", a[MSW128], a[NMSW128],
	       a[NLSW128], a[LSW128]);
}

/* Internal shifters:

   Note that these are only good for 0 < count < 32.
 */

static inline void _lsl128(unsigned int count, int128 a)
{
	a[MSW128] = (a[MSW128] << count) | (a[NMSW128] >> (32 - count));
	a[NMSW128] = (a[NMSW128] << count) | (a[NLSW128] >> (32 - count));
	a[NLSW128] = (a[NLSW128] << count) | (a[LSW128] >> (32 - count));
	a[LSW128] <<= count;
}

static inline void _lsr128(unsigned int count, int128 a)
{
	a[LSW128] = (a[LSW128] >> count) | (a[NLSW128] << (32 - count));
	a[NLSW128] = (a[NLSW128] >> count) | (a[NMSW128] << (32 - count));
	a[NMSW128] = (a[NMSW128] >> count) | (a[MSW128] << (32 - count));
	a[MSW128] >>= count;
}

/* Should be faster, one would hope */

static inline void lslone128(int128 a)
{
	asm volatile ("lsl.l #1,%0\n"
		      "roxl.l #1,%1\n"
		      "roxl.l #1,%2\n"
		      "roxl.l #1,%3\n"
		      :
		      "=d" (a[LSW128]),
		      "=d"(a[NLSW128]),
		      "=d"(a[NMSW128]),
		      "=d"(a[MSW128])
		      :
		      "0"(a[LSW128]),
		      "1"(a[NLSW128]),
		      "2"(a[NMSW128]),
		      "3"(a[MSW128]));
}

static inline void lsrone128(int128 a)
{
	asm volatile ("lsr.l #1,%0\n"
		      "roxr.l #1,%1\n"
		      "roxr.l #1,%2\n"
		      "roxr.l #1,%3\n"
		      :
		      "=d" (a[MSW128]),
		      "=d"(a[NMSW128]),
		      "=d"(a[NLSW128]),
		      "=d"(a[LSW128])
		      :
		      "0"(a[MSW128]),
		      "1"(a[NMSW128]),
		      "2"(a[NLSW128]),
		      "3"(a[LSW128]));
}

/* Generalized 128-bit shifters:

   These bit-shift to a multiple of 32, then move whole longwords.  */

static inline void lsl128(unsigned int count, int128 a)
{
	int wordcount, i;

	if (count % 32)
		_lsl128(count % 32, a);

	if (0 == (wordcount = count / 32))
		return;

	/* argh, gak, endian-sensitive */
	for (i = 0; i < 4 - wordcount; i++) {
		a[i] = a[i + wordcount];
	}
	for (i = 3; i >= 4 - wordcount; --i) {
		a[i] = 0;
	}
}

static inline void lsr128(unsigned int count, int128 a)
{
	int wordcount, i;

	if (count % 32)
		_lsr128(count % 32, a);

	if (0 == (wordcount = count / 32))
		return;

	for (i = 3; i >= wordcount; --i) {
		a[i] = a[i - wordcount];
	}
	for (i = 0; i < wordcount; i++) {
		a[i] = 0;
	}
}

static inline int orl128(int a, int128 b)
{
	b[LSW128] |= a;
}

static inline int btsthi128(const int128 a)
{
	return a[MSW128] & 0x80000000;
}

/* test bits (numbered from 0 = LSB) up to and including "top" */
static inline int bftestlo128(int top, const int128 a)
{
	int r = 0;

	if (top > 31)
		r |= a[LSW128];
	if (top > 63)
		r |= a[NLSW128];
	if (top > 95)
		r |= a[NMSW128];

	r |= a[3 - (top / 32)] & ((1 << (top % 32 + 1)) - 1);

	return (r != 0);
}

/* Aargh.  We need these because GCC is broken */
/* FIXME: do them in assembly, for goodness' sake! */
static inline void mask64(int pos, unsigned long long *mask)
{
	*mask = 0;

	if (pos < 32) {
		LO_WORD(*mask) = (1 << pos) - 1;
		return;
	}
	LO_WORD(*mask) = -1;
	HI_WORD(*mask) = (1 << (pos - 32)) - 1;
}

static inline void bset64(int pos, unsigned long long *dest)
{
	/* This conditional will be optimized away.  Thanks, GCC! */
	if (pos < 32)
		asm volatile ("bset %1,%0":"=m"
			      (LO_WORD(*dest)):"id"(pos));
	else
		asm volatile ("bset %1,%0":"=m"
			      (HI_WORD(*dest)):"id"(pos - 32));
}

static inline int btst64(int pos, unsigned long long dest)
{
	if (pos < 32)
		return (0 != (LO_WORD(dest) & (1 << pos)));
	else
		return (0 != (HI_WORD(dest) & (1 << (pos - 32))));
}

static inline void lsl64(int count, unsigned long long *dest)
{
	if (count < 32) {
		HI_WORD(*dest) = (HI_WORD(*dest) << count)
		    | (LO_WORD(*dest) >> count);
		LO_WORD(*dest) <<= count;
		return;
	}
	count -= 32;
	HI_WORD(*dest) = LO_WORD(*dest) << count;
	LO_WORD(*dest) = 0;
}

static inline void lsr64(int count, unsigned long long *dest)
{
	if (count < 32) {
		LO_WORD(*dest) = (LO_WORD(*dest) >> count)
		    | (HI_WORD(*dest) << (32 - count));
		HI_WORD(*dest) >>= count;
		return;
	}
	count -= 32;
	LO_WORD(*dest) = HI_WORD(*dest) >> count;
	HI_WORD(*dest) = 0;
}
#endif

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
		: "g" (src1), "0" (src2));				\
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

#if 0
static inline unsigned int fp_fls128(union fp_mant128 *src)
{
	unsigned long data;
	unsigned int res, off;

	if ((data = src->m32[0]))
		off = 0;
	else if ((data = src->m32[1]))
		off = 32;
	else if ((data = src->m32[2]))
		off = 64;
	else if ((data = src->m32[3]))
		off = 96;
	else
		return 128;

	asm ("bfffo %1{#0,#32},%0" : "=d" (res) : "dm" (data));
	return res + off;
}

static inline void fp_shiftmant128(union fp_mant128 *src, int shift)
{
	unsigned long sticky;

	switch (shift) {
	case 0:
		return;
	case 1:
		asm volatile ("lsl.l #1,%0"
			: "=d" (src->m32[3]) : "0" (src->m32[3]));
		asm volatile ("roxl.l #1,%0"
			: "=d" (src->m32[2]) : "0" (src->m32[2]));
		asm volatile ("roxl.l #1,%0"
			: "=d" (src->m32[1]) : "0" (src->m32[1]));
		asm volatile ("roxl.l #1,%0"
			: "=d" (src->m32[0]) : "0" (src->m32[0]));
		return;
	case 2 ... 31:
		src->m32[0] = (src->m32[0] << shift) | (src->m32[1] >> (32 - shift));
		src->m32[1] = (src->m32[1] << shift) | (src->m32[2] >> (32 - shift));
		src->m32[2] = (src->m32[2] << shift) | (src->m32[3] >> (32 - shift));
		src->m32[3] = (src->m32[3] << shift);
		return;
	case 32 ... 63:
		shift -= 32;
		src->m32[0] = (src->m32[1] << shift) | (src->m32[2] >> (32 - shift));
		src->m32[1] = (src->m32[2] << shift) | (src->m32[3] >> (32 - shift));
		src->m32[2] = (src->m32[3] << shift);
		src->m32[3] = 0;
		return;
	case 64 ... 95:
		shift -= 64;
		src->m32[0] = (src->m32[2] << shift) | (src->m32[3] >> (32 - shift));
		src->m32[1] = (src->m32[3] << shift);
		src->m32[2] = src->m32[3] = 0;
		return;
	case 96 ... 127:
		shift -= 96;
		src->m32[0] = (src->m32[3] << shift);
		src->m32[1] = src->m32[2] = src->m32[3] = 0;
		return;
	case -31 ... -1:
		shift = -shift;
		sticky = 0;
		if (src->m32[3] << (32 - shift))
			sticky = 1;
		src->m32[3] = (src->m32[3] >> shift) | (src->m32[2] << (32 - shift)) | sticky;
		src->m32[2] = (src->m32[2] >> shift) | (src->m32[1] << (32 - shift));
		src->m32[1] = (src->m32[1] >> shift) | (src->m32[0] << (32 - shift));
		src->m32[0] = (src->m32[0] >> shift);
		return;
	case -63 ... -32:
		shift = -shift - 32;
		sticky = 0;
		if ((src->m32[2] << (32 - shift)) || src->m32[3])
			sticky = 1;
		src->m32[3] = (src->m32[2] >> shift) | (src->m32[1] << (32 - shift)) | sticky;
		src->m32[2] = (src->m32[1] >> shift) | (src->m32[0] << (32 - shift));
		src->m32[1] = (src->m32[0] >> shift);
		src->m32[0] = 0;
		return;
	case -95 ... -64:
		shift = -shift - 64;
		sticky = 0;
		if ((src->m32[1] << (32 - shift)) || src->m32[2] || src->m32[3])
			sticky = 1;
		src->m32[3] = (src->m32[1] >> shift) | (src->m32[0] << (32 - shift)) | sticky;
		src->m32[2] = (src->m32[0] >> shift);
		src->m32[1] = src->m32[0] = 0;
		return;
	case -127 ... -96:
		shift = -shift - 96;
		sticky = 0;
		if ((src->m32[0] << (32 - shift)) || src->m32[1] || src->m32[2] || src->m32[3])
			sticky = 1;
		src->m32[3] = (src->m32[0] >> shift) | sticky;
		src->m32[2] = src->m32[1] = src->m32[0] = 0;
		return;
	}

	if (shift < 0 && (src->m32[0] || src->m32[1] || src->m32[2] || src->m32[3]))
		src->m32[3] = 1;
	else
		src->m32[3] = 0;
	src->m32[2] = 0;
	src->m32[1] = 0;
	src->m32[0] = 0;
}
#endif

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

#if 0 /* old code... */
static inline int fls(unsigned int a)
{
	int r;

	asm volatile ("bfffo %1{#0,#32},%0"
		      : "=d" (r) : "md" (a));
	return r;
}

/* fls = "find last set" (cf. ffs(3)) */
static inline int fls128(const int128 a)
{
	if (a[MSW128])
		return fls(a[MSW128]);
	if (a[NMSW128])
		return fls(a[NMSW128]) + 32;
	/* XXX: it probably never gets beyond this point in actual
	   use, but that's indicative of a more general problem in the
	   algorithm (i.e. as per the actual 68881 implementation, we
	   really only need at most 67 bits of precision [plus
	   overflow]) so I'm not going to fix it. */
	if (a[NLSW128])
		return fls(a[NLSW128]) + 64;
	if (a[LSW128])
		return fls(a[LSW128]) + 96;
	else
		return -1;
}

static inline int zerop128(const int128 a)
{
	return !(a[LSW128] | a[NLSW128] | a[NMSW128] | a[MSW128]);
}

static inline int nonzerop128(const int128 a)
{
	return (a[LSW128] | a[NLSW128] | a[NMSW128] | a[MSW128]);
}

/* Addition and subtraction */
/* Do these in "pure" assembly, because "extended" asm is unmanageable
   here */
static inline void add128(const int128 a, int128 b)
{
	/* rotating carry flags */
	unsigned int carry[2];

	carry[0] = a[LSW128] > (0xffffffff - b[LSW128]);
	b[LSW128] += a[LSW128];

	carry[1] = a[NLSW128] > (0xffffffff - b[NLSW128] - carry[0]);
	b[NLSW128] = a[NLSW128] + b[NLSW128] + carry[0];

	carry[0] = a[NMSW128] > (0xffffffff - b[NMSW128] - carry[1]);
	b[NMSW128] = a[NMSW128] + b[NMSW128] + carry[1];

	b[MSW128] = a[MSW128] + b[MSW128] + carry[0];
}

/* Note: assembler semantics: "b -= a" */
static inline void sub128(const int128 a, int128 b)
{
	/* rotating borrow flags */
	unsigned int borrow[2];

	borrow[0] = b[LSW128] < a[LSW128];
	b[LSW128] -= a[LSW128];

	borrow[1] = b[NLSW128] < a[NLSW128] + borrow[0];
	b[NLSW128] = b[NLSW128] - a[NLSW128] - borrow[0];

	borrow[0] = b[NMSW128] < a[NMSW128] + borrow[1];
	b[NMSW128] = b[NMSW128] - a[NMSW128] - borrow[1];

	b[MSW128] = b[MSW128] - a[MSW128] - borrow[0];
}

/* Poor man's 64-bit expanding multiply */
static inline void mul64(unsigned long long a, unsigned long long b, int128 c)
{
	unsigned long long acc;
	int128 acc128;

	zero128(acc128);
	zero128(c);

	/* first the low words */
	if (LO_WORD(a) && LO_WORD(b)) {
		acc = (long long) LO_WORD(a) * LO_WORD(b);
		c[NLSW128] = HI_WORD(acc);
		c[LSW128] = LO_WORD(acc);
	}
	/* Next the high words */
	if (HI_WORD(a) && HI_WORD(b)) {
		acc = (long long) HI_WORD(a) * HI_WORD(b);
		c[MSW128] = HI_WORD(acc);
		c[NMSW128] = LO_WORD(acc);
	}
	/* The middle words */
	if (LO_WORD(a) && HI_WORD(b)) {
		acc = (long long) LO_WORD(a) * HI_WORD(b);
		acc128[NMSW128] = HI_WORD(acc);
		acc128[NLSW128] = LO_WORD(acc);
		add128(acc128, c);
	}
	/* The first and last words */
	if (HI_WORD(a) && LO_WORD(b)) {
		acc = (long long) HI_WORD(a) * LO_WORD(b);
		acc128[NMSW128] = HI_WORD(acc);
		acc128[NLSW128] = LO_WORD(acc);
		add128(acc128, c);
	}
}

/* Note: unsigned */
static inline int cmp128(int128 a, int128 b)
{
	if (a[MSW128] < b[MSW128])
		return -1;
	if (a[MSW128] > b[MSW128])
		return 1;
	if (a[NMSW128] < b[NMSW128])
		return -1;
	if (a[NMSW128] > b[NMSW128])
		return 1;
	if (a[NLSW128] < b[NLSW128])
		return -1;
	if (a[NLSW128] > b[NLSW128])
		return 1;

	return (signed) a[LSW128] - b[LSW128];
}

inline void div128(int128 a, int128 b, int128 c)
{
	int128 mask;

	/* Algorithm:

	   Shift the divisor until it's at least as big as the
	   dividend, keeping track of the position to which we've
	   shifted it, i.e. the power of 2 which we've multiplied it
	   by.

	   Then, for this power of 2 (the mask), and every one smaller
	   than it, subtract the mask from the dividend and add it to
	   the quotient until the dividend is smaller than the raised
	   divisor.  At this point, divide the dividend and the mask
	   by 2 (i.e. shift one place to the right).  Lather, rinse,
	   and repeat, until there are no more powers of 2 left. */

	/* FIXME: needless to say, there's room for improvement here too. */

	/* Shift up */
	/* XXX: since it just has to be "at least as big", we can
	   probably eliminate this horribly wasteful loop.  I will
	   have to prove this first, though */
	set128(0, 0, 0, 1, mask);
	while (cmp128(b, a) < 0 && !btsthi128(b)) {
		lslone128(b);
		lslone128(mask);
	}

	/* Shift down */
	zero128(c);
	do {
		if (cmp128(a, b) >= 0) {
			sub128(b, a);
			add128(mask, c);
		}
		lsrone128(mask);
		lsrone128(b);
	} while (nonzerop128(mask));

	/* The remainder is in a... */
}
#endif

#endif	/* MULTI_ARITH_H */
