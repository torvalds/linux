/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_HASH_H
#define _ASM_HASH_H

/*
 * HP-PA only implements integer multiply in the FPU.  However, for
 * integer multiplies by constant, it has a number of shift-and-add
 * (but no shift-and-subtract, sigh!) instructions that a compiler
 * can synthesize a code sequence with.
 *
 * Unfortunately, GCC isn't very efficient at using them.  For example
 * it uses three instructions for "x *= 21" when only two are needed.
 * But we can find a sequence manually.
 */

#define HAVE_ARCH__HASH_32 1

/*
 * This is a multiply by GOLDEN_RATIO_32 = 0x61C88647 optimized for the
 * PA7100 pairing rules.  This is an in-order 2-way superscalar processor.
 * Only one instruction in a pair may be a shift (by more than 3 bits),
 * but other than that, simple ALU ops (including shift-and-add by up
 * to 3 bits) may be paired arbitrarily.
 *
 * PA8xxx processors also dual-issue ALU instructions, although with
 * fewer constraints, so this schedule is good for them, too.
 *
 * This 6-step sequence was found by Yevgen Voronenko's implementation
 * of the Hcub algorithm at http://spiral.ece.cmu.edu/mcm/gen.html.
 */
static inline u32 __attribute_const__ __hash_32(u32 x)
{
	u32 a, b, c;

	/*
	 * Phase 1: Compute  a = (x << 19) + x,
	 * b = (x << 9) + a, c = (x << 23) + b.
	 */
	a = x << 19;		/* Two shifts can't be paired */
	b = x << 9;	a += x;
	c = x << 23;	b += a;
			c += b;
	/* Phase 2: Return (b<<11) + (c<<6) + (a<<3) - c */
	b <<= 11;
	a += c << 3;	b -= c;
	return (a << 3) + b;
}

#if BITS_PER_LONG == 64

#define HAVE_ARCH_HASH_64 1

/*
 * Finding a good shift-and-add chain for GOLDEN_RATIO_64 is tricky,
 * because available software for the purpose chokes on constants this
 * large.  (It's mostly designed for compiling FIR filter coefficients
 * into FPGAs.)
 *
 * However, Jason Thong pointed out a work-around.  The Hcub software
 * (http://spiral.ece.cmu.edu/mcm/gen.html) is designed for *multiple*
 * constant multiplication, and is good at finding shift-and-add chains
 * which share common terms.
 *
 * Looking at 0x0x61C8864680B583EB in binary:
 * 0110000111001000100001100100011010000000101101011000001111101011
 *  \______________/    \__________/       \_______/     \________/
 *   \____________________________/         \____________________/
 * you can see the non-zero bits are divided into several well-separated
 * blocks.  Hcub can find algorithms for those terms separately, which
 * can then be shifted and added together.
 *
 * Dividing the input into 2, 3 or 4 blocks, Hcub can find solutions
 * with 10, 9 or 8 adds, respectively, making a total of 11 for the
 * whole number.
 *
 * Using just two large blocks, 0xC3910C8D << 31 in the high bits,
 * and 0xB583EB in the low bits, produces as good an algorithm as any,
 * and with one more small shift than alternatives.
 *
 * The high bits are a larger number and more work to compute, as well
 * as needing one extra cycle to shift left 31 bits before the final
 * addition, so they are the critical path for scheduling.  The low bits
 * can fit into the scheduling slots left over.
 */


/*
 * This _ASSIGN(dst, src) macro performs "dst = src", but prevents GCC
 * from inferring anything about the value assigned to "dest".
 *
 * This prevents it from mis-optimizing certain sequences.
 * In particular, gcc is annoyingly eager to combine consecutive shifts.
 * Given "x <<= 19; y += x; z += x << 1;", GCC will turn this into
 * "y += x << 19; z += x << 20;" even though the latter sequence needs
 * an additional instruction and temporary register.
 *
 * Because no actual assembly code is generated, this construct is
 * usefully portable across all GCC platforms, and so can be test-compiled
 * on non-PA systems.
 *
 * In two places, additional unused input dependencies are added.  This
 * forces GCC's scheduling so it does not rearrange instructions too much.
 * Because the PA-8xxx is out of order, I'm not sure how much this matters,
 * but why make it more difficult for the processor than necessary?
 */
#define _ASSIGN(dst, src, ...) asm("" : "=r" (dst) : "0" (src), ##__VA_ARGS__)

/*
 * Multiply by GOLDEN_RATIO_64 = 0x0x61C8864680B583EB using a heavily
 * optimized shift-and-add sequence.
 *
 * Without the final shift, the multiply proper is 19 instructions,
 * 10 cycles and uses only 4 temporaries.  Whew!
 *
 * You are not expected to understand this.
 */
static __always_inline u32 __attribute_const__
hash_64(u64 a, unsigned int bits)
{
	u64 b, c, d;

	/*
	 * Encourage GCC to move a dynamic shift to %sar early,
	 * thereby freeing up an additional temporary register.
	 */
	if (!__builtin_constant_p(bits))
		asm("" : "=q" (bits) : "0" (64 - bits));
	else
		bits = 64 - bits;

	_ASSIGN(b, a*5);	c = a << 13;
	b = (b << 2) + a;	_ASSIGN(d, a << 17);
	a = b + (a << 1);	c += d;
	d = a << 10;		_ASSIGN(a, a << 19);
	d = a - d;		_ASSIGN(a, a << 4, "X" (d));
	c += b;			a += b;
	d -= c;			c += a << 1;
	a += c << 3;		_ASSIGN(b, b << (7+31), "X" (c), "X" (d));
	a <<= 31;		b += d;
	a += b;
	return a >> bits;
}
#undef _ASSIGN	/* We're a widely-used header file, so don't litter! */

#endif /* BITS_PER_LONG == 64 */

#endif /* _ASM_HASH_H */
