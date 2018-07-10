/* vi: set sw=4 ts=4: */
/*
 * $RANDOM support.
 *
 * Copyright (C) 2009 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/* For testing against dieharder, you need only random.{c,h}
 * Howto:
 * gcc -O2 -Wall -DRANDTEST random.c -o random
 * ./random | dieharder -g 200 -a
 */

#if !defined RANDTEST

# include "libbb.h"
# include "random.h"
# define RAND_BASH_MASK 0x7fff

#else
# include <stdint.h>
# include <unistd.h>
# include <stdio.h>
# include <time.h>
# define FAST_FUNC /* nothing */
# define PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN /* nothing */
# define POP_SAVED_FUNCTION_VISIBILITY /* nothing */
# define monotonic_us() time(NULL)
# include "random.h"
# define RAND_BASH_MASK 0xffffffff /* off */
#endif

uint32_t FAST_FUNC
next_random(random_t *rnd)
{
	/* Galois LFSR parameter:
	 * Taps at 32 31 29 1:
	 */
	enum { MASK = 0x8000000b };
	/* Another example - taps at 32 31 30 10: */
	/* enum { MASK = 0x00400007 }; */

	/* Xorshift parameters:
	 * Choices for a,b,c: 10,13,10; 8,9,22; 2,7,3; 23,3,24
	 * (given by algorithm author)
	 */
        enum {
                a = 2,
                b = 7,
                c = 3,
        };

	uint32_t t;

	if (UNINITED_RANDOM_T(rnd)) {
		/* Can use monotonic_ns() for better randomness but for now
		 * it is not used anywhere else in busybox... so avoid bloat
		 */
		INIT_RANDOM_T(rnd, getpid(), monotonic_us());
	}

	/* LCG: period of 2^32, but quite weak:
	 * bit 0 alternates beetween 0 and 1 (pattern of length 2)
	 * bit 1 has a repeating pattern of length 4
	 * bit 2 has a repeating pattern of length 8
	 * etc...
	 */
	rnd->LCG = 1664525 * rnd->LCG + 1013904223;

	/* Galois LFSR:
	 * period of 2^32-1 = 3 * 5 * 17 * 257 * 65537.
	 * Successive values are right-shifted one bit
	 * and possibly xored with a sparse constant.
	 */
	t = (rnd->galois_LFSR << 1);
	if (rnd->galois_LFSR < 0) /* if we just shifted 1 out of msb... */
		t ^= MASK;
	rnd->galois_LFSR = t;

	/* http://en.wikipedia.org/wiki/Xorshift
	 * Moderately good statistical properties:
	 * fails the following "dieharder -g 200 -a" tests:
	 *       diehard_operm5|   0
	 *         diehard_oqso|   0
	 * diehard_count_1s_byt|   0
	 *     diehard_3dsphere|   3
	 *      diehard_squeeze|   0
	 *         diehard_runs|   0
	 *         diehard_runs|   0
	 *        diehard_craps|   0
	 *        diehard_craps|   0
	 * rgb_minimum_distance|   3
	 * rgb_minimum_distance|   4
	 * rgb_minimum_distance|   5
	 *     rgb_permutations|   3
	 *     rgb_permutations|   4
	 *     rgb_permutations|   5
	 *         dab_filltree|  32
	 *         dab_filltree|  32
	 *         dab_monobit2|  12
	 */
 again:
	t = rnd->xs64_x ^ (rnd->xs64_x << a);
	rnd->xs64_x = rnd->xs64_y;
	rnd->xs64_y = rnd->xs64_y ^ (rnd->xs64_y >> c) ^ t ^ (t >> b);
	/*
	 * Period 2^64-1 = 2^32+1 * 2^32-1 has a common divisor with Galois LFSR.
	 * By skipping two possible states (0x1 and 0x2) we reduce period to
	 * 2^64-3 = 13 * 3889 * 364870227143809 which has no common divisors:
	 */
	if (rnd->xs64_y == 0 && rnd->xs64_x <= 2)
		goto again;

	/* Combined LCG + Galois LFSR rng has 2^32 * 2^32-1 period.
	 * Strength:
	 * individually, both are extremely weak cryptographycally;
	 * when combined, they fail the following "dieharder -g 200 -a" tests:
	 *     diehard_rank_6x8|   0
	 *         diehard_oqso|   0
	 *          diehard_dna|   0
	 * diehard_count_1s_byt|   0
	 *          rgb_bitdist|   2
	 *         dab_monobit2|  12
	 *
	 * Combining them with xorshift-64 increases period to
	 * 2^32 * 2^32-1 * 2^64-3
	 * which is about 2^128, or in base 10 ~3.40*10^38.
	 * Strength of the combination:
	 * passes all "dieharder -g 200 -a" tests.
	 *
	 * Combining with subtraction and addition is just for fun.
	 * It does not add meaningful strength, could use xor operation instead.
	 */
	t = rnd->galois_LFSR - rnd->LCG + rnd->xs64_y;

	/* bash compat $RANDOM range: */
	return t & RAND_BASH_MASK;
}

#ifdef RANDTEST
static random_t rnd;

int main(int argc, char **argv)
{
	int i;
	uint32_t buf[4096];

	for (;;) {
		for (i = 0; i < sizeof(buf) / sizeof(buf[0]); i++) {
			buf[i] = next_random(&rnd);
		}
		write(1, buf, sizeof(buf));
	}

        return 0;
}

#endif
