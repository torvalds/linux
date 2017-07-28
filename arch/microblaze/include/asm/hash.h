#ifndef _ASM_HASH_H
#define _ASM_HASH_H

/*
 * Fortunately, most people who want to run Linux on Microblaze enable
 * both multiplier and barrel shifter, but omitting them is technically
 * a supported configuration.
 *
 * With just a barrel shifter, we can implement an efficient constant
 * multiply using shifts and adds.  GCC can find a 9-step solution, but
 * this 6-step solution was found by Yevgen Voronenko's implementation
 * of the Hcub algorithm at http://spiral.ece.cmu.edu/mcm/gen.html.
 *
 * That software is really not designed for a single multiplier this large,
 * but if you run it enough times with different seeds, it'll find several
 * 6-shift, 6-add sequences for computing x * 0x61C88647.  They are all
 *	c = (x << 19) + x;
 *	a = (x <<  9) + c;
 *	b = (x << 23) + a;
 *	return (a<<11) + (b<<6) + (c<<3) - b;
 * with variations on the order of the final add.
 *
 * Without even a shifter, it's hopless; any hash function will suck.
 */

#if CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL == 0

#define HAVE_ARCH__HASH_32 1

/* Multiply by GOLDEN_RATIO_32 = 0x61C88647 */
static inline u32 __attribute_const__ __hash_32(u32 a)
{
#if CONFIG_XILINX_MICROBLAZE0_USE_BARREL
	unsigned int b, c;

	/* Phase 1: Compute three intermediate values */
	b =  a << 23;
	c = (a << 19) + a;
	a = (a <<  9) + c;
	b += a;

	/* Phase 2: Compute (a << 11) + (b << 6) + (c << 3) - b */
	a <<= 5;
	a += b;		/* (a << 5) + b */
	a <<= 3;
	a += c;		/* (a << 8) + (b << 3) + c */
	a <<= 3;
	return a - b;	/* (a << 11) + (b << 6) + (c << 3) - b */
#else
	/*
	 * "This is really going to hurt."
	 *
	 * Without a barrel shifter, left shifts are implemented as
	 * repeated additions, and the best we can do is an optimal
	 * addition-subtraction chain.  This one is not known to be
	 * optimal, but at 37 steps, it's decent for a 31-bit multiplier.
	 *
	 * Question: given its size (37*4 = 148 bytes per instance),
	 * and slowness, is this worth having inline?
	 */
	unsigned int b, c, d;

	b = a << 4;	/* 4    */
	c = b << 1;	/* 1  5 */
	b += a;		/* 1  6 */
	c += b;		/* 1  7 */
	c <<= 3;	/* 3 10 */
	c -= a;		/* 1 11 */
	d = c << 7;	/* 7 18 */
	d += b;		/* 1 19 */
	d <<= 8;	/* 8 27 */
	d += a;		/* 1 28 */
	d <<= 1;	/* 1 29 */
	d += b;		/* 1 30 */
	d <<= 6;	/* 6 36 */
	return d + c;	/* 1 37 total instructions*/
#endif
}

#endif /* !CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL */
#endif /* _ASM_HASH_H */
