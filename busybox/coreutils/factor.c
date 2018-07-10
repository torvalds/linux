/*
 * Copyright (C) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FACTOR
//config:	bool "factor (2.6 kb)"
//config:	default y
//config:	help
//config:	factor factorizes integers

//applet:IF_FACTOR(APPLET(factor, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_FACTOR) += factor.o

//usage:#define factor_trivial_usage
//usage:       "[NUMBER]..."
//usage:#define factor_full_usage "\n\n"
//usage:       "Print prime factors"

#include "libbb.h"

#if 0
# define dbg(...) bb_error_msg(__VA_ARGS__)
#else
# define dbg(...) ((void)0)
#endif

typedef unsigned long long wide_t;

#if ULLONG_MAX == (UINT_MAX * UINT_MAX + 2 * UINT_MAX)
/* "unsigned" is half as wide as ullong */
typedef unsigned half_t;
#define HALF_MAX UINT_MAX
#define HALF_FMT ""
#elif ULLONG_MAX == (ULONG_MAX * ULONG_MAX + 2 * ULONG_MAX)
/* long is half as wide as ullong */
typedef unsigned long half_t;
#define HALF_MAX ULONG_MAX
#define HALF_FMT "l"
#else
#error Cant find an integer type which is half as wide as ullong
#endif

static half_t isqrt_odd(wide_t N)
{
	half_t s = isqrt(N);
	/* Subtract 1 from even s, odd s won't change: */
	/* (doesnt work for zero, but we know that s != 0 here) */
	s = (s - 1) | 1;
	return s;
}

static NOINLINE void factorize(wide_t N)
{
	half_t factor;
	half_t max_factor;
	// unsigned count3;
	// unsigned count5;
	// unsigned count7;
	// ^^^^^^^^^^^^^^^ commented-out simple sieving code (easier to grasp).
	// Faster sieving, using one word for potentially up to 6 counters:
	// count upwards in each mask, counter "triggers" when it sets its mask to "100[0]..."
	// 10987654321098765432109876543210 - bits 31-0 in 32-bit word
	//    17777713333311111777775555333 - bit masks for counters for primes 3,5,7,11,13,17
	//         100000100001000010001001 - value for adding 1 to each mask
	//    10000010000010000100001000100 - value for checking that any mask reached msb
	enum {
		SHIFT_3 = 1 << 0,
		SHIFT_5 = 1 << 3,
		SHIFT_7 = 1 << 7,
		INCREMENT_EACH = SHIFT_3 | SHIFT_5 | SHIFT_7,
		MULTIPLE_OF_3 = 1 << 2,
		MULTIPLE_OF_5 = 1 << 6,
		MULTIPLE_OF_7 = 1 << 11,
		MULTIPLE_DETECTED = MULTIPLE_OF_3 | MULTIPLE_OF_5 | MULTIPLE_OF_7,
	};
	unsigned sieve_word;

	if (N < 4)
		goto end;

	while (!(N & 1)) {
		printf(" 2");
		N >>= 1;
	}

	/* The code needs to be optimized for the case where
	 * there are large prime factors. For example,
	 * this is not hard:
	 * 8262075252869367027 = 3 7 17 23 47 101 113 127 131 137 823
	 * (the largest factor to test is only ~sqrt(823) = 28)
	 * but this is:
	 * 18446744073709551601 = 53 348051774975651917
	 * the last factor requires testing up to
	 * 589959129 - about 100 million iterations.
	 * The slowest case (largest prime) for N < 2^64 is
	 * factor 18446744073709551557 (0xffffffffffffffc5).
	 */
	max_factor = isqrt_odd(N);
	// count3 = 3;
	// count5 = 6;
	// count7 = 9;
	sieve_word = 0
		/* initial count for SHIFT_n is (n-1)/2*3: */
		+ (MULTIPLE_OF_3 - 3 * SHIFT_3)
		+ (MULTIPLE_OF_5 - 6 * SHIFT_5)
		+ (MULTIPLE_OF_7 - 9 * SHIFT_7)
		//+ (MULTIPLE_OF_11 - 15 * SHIFT_11)
		//+ (MULTIPLE_OF_13 - 18 * SHIFT_13)
		//+ (MULTIPLE_OF_17 - 24 * SHIFT_17)
	;
	factor = 3;
	for (;;) {
		/* The division is the most costly part of the loop.
		 * On 64bit CPUs, takes at best 12 cycles, often ~20.
		 */
		while ((N % factor) == 0) { /* not likely */
			N = N / factor;
			printf(" %"HALF_FMT"u", factor);
			max_factor = isqrt_odd(N);
		}
 next_factor:
		if (factor >= max_factor)
			break;
		factor += 2;
		/* Rudimentary wheel sieving: skip multiples of 3, 5 and 7:
		 * Every third odd number is divisible by three and thus isn't a prime:
		 * 5 7 9 11 13 15 17 19 21 23 25 27 29 31 33 35 37 39 41 43 45 47...
		 * ^ ^   ^  ^     ^  ^     ^  _     ^  ^     _  ^     ^  ^     ^
		 * (^ = primes, _ = would-be-primes-if-not-divisible-by-5)
		 * The numbers with space under them are excluded by sieve 3.
		 */
		// count7--;
		// count5--;
		// count3--;
		// if (count3 && count5 && count7)
		// 	continue;
		sieve_word += INCREMENT_EACH;
		if (!(sieve_word & MULTIPLE_DETECTED))
			continue;
		/*
		 * "factor" is multiple of 3 33% of the time (count3 reached 0),
		 * else, multiple of 5 13% of the time,
		 * else, multiple of 7 7.6% of the time.
		 * Cumulatively, with 3,5,7 sieving we are here 54.3% of the time.
		 */
		// if (count3 == 0)
		// 	count3 = 3;
		if (sieve_word & MULTIPLE_OF_3)
			sieve_word -= SHIFT_3 * 3;
		// if (count5 == 0)
		// 	count5 = 5;
		if (sieve_word & MULTIPLE_OF_5)
			sieve_word -= SHIFT_5 * 5;
		// if (count7 == 0)
		// 	count7 = 7;
		if (sieve_word & MULTIPLE_OF_7)
			sieve_word -= SHIFT_7 * 7;
		goto next_factor;
	}
 end:
	if (N > 1)
		printf(" %llu", N);
	bb_putchar('\n');
}

static void factorize_numstr(const char *numstr)
{
	wide_t N;

	/* Leading + is ok (coreutils compat) */
	if (*numstr == '+')
		numstr++;
	N = bb_strtoull(numstr, NULL, 10);
	if (errno)
		bb_show_usage();
	printf("%llu:", N);
	factorize(N);
}

int factor_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int factor_main(int argc UNUSED_PARAM, char **argv)
{
	//// coreutils has undocumented option ---debug (three dashes)
	//getopt32(argv, "");
	//argv += optind;
	argv++;

	if (!*argv) {
		/* Read from stdin, several numbers per line are accepted */
		for (;;) {
			char *numstr, *line;
			line = xmalloc_fgetline(stdin);
			if (!line)
				return EXIT_SUCCESS;
			numstr = line;
			for (;;) {
				char *end;
				numstr = skip_whitespace(numstr);
				if (!numstr[0])
					break;
				end = skip_non_whitespace(numstr);
				if (*end != '\0')
					*end++ = '\0';
				factorize_numstr(numstr);
				numstr = end;
			}
			free(line);
		}
	}

	do {
		/* Leading spaces are ok (coreutils compat) */
		factorize_numstr(skip_whitespace(*argv));
	} while (*++argv);

	return EXIT_SUCCESS;
}
