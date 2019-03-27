/* $OpenBSD: moduli.c,v 1.32 2017/12/08 03:45:52 deraadt Exp $ */
/*
 * Copyright 1994 Phil Karn <karn@qualcomm.com>
 * Copyright 1996-1998, 2003 William Allen Simpson <wsimpson@greendragon.com>
 * Copyright 2000 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Two-step process to generate safe primes for DHGEX
 *
 *  Sieve candidates for "safe" primes,
 *  suitable for use as Diffie-Hellman moduli;
 *  that is, where q = (p-1)/2 is also prime.
 *
 * First step: generate candidate primes (memory intensive)
 * Second step: test primes' safety (processor intensive)
 */

#include "includes.h"

#ifdef WITH_OPENSSL

#include <sys/types.h>

#include <openssl/bn.h>
#include <openssl/dh.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "xmalloc.h"
#include "dh.h"
#include "log.h"
#include "misc.h"

#include "openbsd-compat/openssl-compat.h"

/*
 * File output defines
 */

/* need line long enough for largest moduli plus headers */
#define QLINESIZE		(100+8192)

/*
 * Size: decimal.
 * Specifies the number of the most significant bit (0 to M).
 * WARNING: internally, usually 1 to N.
 */
#define QSIZE_MINIMUM		(511)

/*
 * Prime sieving defines
 */

/* Constant: assuming 8 bit bytes and 32 bit words */
#define SHIFT_BIT	(3)
#define SHIFT_BYTE	(2)
#define SHIFT_WORD	(SHIFT_BIT+SHIFT_BYTE)
#define SHIFT_MEGABYTE	(20)
#define SHIFT_MEGAWORD	(SHIFT_MEGABYTE-SHIFT_BYTE)

/*
 * Using virtual memory can cause thrashing.  This should be the largest
 * number that is supported without a large amount of disk activity --
 * that would increase the run time from hours to days or weeks!
 */
#define LARGE_MINIMUM	(8UL)	/* megabytes */

/*
 * Do not increase this number beyond the unsigned integer bit size.
 * Due to a multiple of 4, it must be LESS than 128 (yielding 2**30 bits).
 */
#define LARGE_MAXIMUM	(127UL)	/* megabytes */

/*
 * Constant: when used with 32-bit integers, the largest sieve prime
 * has to be less than 2**32.
 */
#define SMALL_MAXIMUM	(0xffffffffUL)

/* Constant: can sieve all primes less than 2**32, as 65537**2 > 2**32-1. */
#define TINY_NUMBER	(1UL<<16)

/* Ensure enough bit space for testing 2*q. */
#define TEST_MAXIMUM	(1UL<<16)
#define TEST_MINIMUM	(QSIZE_MINIMUM + 1)
/* real TEST_MINIMUM	(1UL << (SHIFT_WORD - TEST_POWER)) */
#define TEST_POWER	(3)	/* 2**n, n < SHIFT_WORD */

/* bit operations on 32-bit words */
#define BIT_CLEAR(a,n)	((a)[(n)>>SHIFT_WORD] &= ~(1L << ((n) & 31)))
#define BIT_SET(a,n)	((a)[(n)>>SHIFT_WORD] |= (1L << ((n) & 31)))
#define BIT_TEST(a,n)	((a)[(n)>>SHIFT_WORD] & (1L << ((n) & 31)))

/*
 * Prime testing defines
 */

/* Minimum number of primality tests to perform */
#define TRIAL_MINIMUM	(4)

/*
 * Sieving data (XXX - move to struct)
 */

/* sieve 2**16 */
static u_int32_t *TinySieve, tinybits;

/* sieve 2**30 in 2**16 parts */
static u_int32_t *SmallSieve, smallbits, smallbase;

/* sieve relative to the initial value */
static u_int32_t *LargeSieve, largewords, largetries, largenumbers;
static u_int32_t largebits, largememory;	/* megabytes */
static BIGNUM *largebase;

int gen_candidates(FILE *, u_int32_t, u_int32_t, BIGNUM *);
int prime_test(FILE *, FILE *, u_int32_t, u_int32_t, char *, unsigned long,
    unsigned long);

/*
 * print moduli out in consistent form,
 */
static int
qfileout(FILE * ofile, u_int32_t otype, u_int32_t otests, u_int32_t otries,
    u_int32_t osize, u_int32_t ogenerator, BIGNUM * omodulus)
{
	struct tm *gtm;
	time_t time_now;
	int res;

	time(&time_now);
	gtm = gmtime(&time_now);

	res = fprintf(ofile, "%04d%02d%02d%02d%02d%02d %u %u %u %u %x ",
	    gtm->tm_year + 1900, gtm->tm_mon + 1, gtm->tm_mday,
	    gtm->tm_hour, gtm->tm_min, gtm->tm_sec,
	    otype, otests, otries, osize, ogenerator);

	if (res < 0)
		return (-1);

	if (BN_print_fp(ofile, omodulus) < 1)
		return (-1);

	res = fprintf(ofile, "\n");
	fflush(ofile);

	return (res > 0 ? 0 : -1);
}


/*
 ** Sieve p's and q's with small factors
 */
static void
sieve_large(u_int32_t s)
{
	u_int32_t r, u;

	debug3("sieve_large %u", s);
	largetries++;
	/* r = largebase mod s */
	r = BN_mod_word(largebase, s);
	if (r == 0)
		u = 0; /* s divides into largebase exactly */
	else
		u = s - r; /* largebase+u is first entry divisible by s */

	if (u < largebits * 2) {
		/*
		 * The sieve omits p's and q's divisible by 2, so ensure that
		 * largebase+u is odd. Then, step through the sieve in
		 * increments of 2*s
		 */
		if (u & 0x1)
			u += s; /* Make largebase+u odd, and u even */

		/* Mark all multiples of 2*s */
		for (u /= 2; u < largebits; u += s)
			BIT_SET(LargeSieve, u);
	}

	/* r = p mod s */
	r = (2 * r + 1) % s;
	if (r == 0)
		u = 0; /* s divides p exactly */
	else
		u = s - r; /* p+u is first entry divisible by s */

	if (u < largebits * 4) {
		/*
		 * The sieve omits p's divisible by 4, so ensure that
		 * largebase+u is not. Then, step through the sieve in
		 * increments of 4*s
		 */
		while (u & 0x3) {
			if (SMALL_MAXIMUM - u < s)
				return;
			u += s;
		}

		/* Mark all multiples of 4*s */
		for (u /= 4; u < largebits; u += s)
			BIT_SET(LargeSieve, u);
	}
}

/*
 * list candidates for Sophie-Germain primes (where q = (p-1)/2)
 * to standard output.
 * The list is checked against small known primes (less than 2**30).
 */
int
gen_candidates(FILE *out, u_int32_t memory, u_int32_t power, BIGNUM *start)
{
	BIGNUM *q;
	u_int32_t j, r, s, t;
	u_int32_t smallwords = TINY_NUMBER >> 6;
	u_int32_t tinywords = TINY_NUMBER >> 6;
	time_t time_start, time_stop;
	u_int32_t i;
	int ret = 0;

	largememory = memory;

	if (memory != 0 &&
	    (memory < LARGE_MINIMUM || memory > LARGE_MAXIMUM)) {
		error("Invalid memory amount (min %ld, max %ld)",
		    LARGE_MINIMUM, LARGE_MAXIMUM);
		return (-1);
	}

	/*
	 * Set power to the length in bits of the prime to be generated.
	 * This is changed to 1 less than the desired safe prime moduli p.
	 */
	if (power > TEST_MAXIMUM) {
		error("Too many bits: %u > %lu", power, TEST_MAXIMUM);
		return (-1);
	} else if (power < TEST_MINIMUM) {
		error("Too few bits: %u < %u", power, TEST_MINIMUM);
		return (-1);
	}
	power--; /* decrement before squaring */

	/*
	 * The density of ordinary primes is on the order of 1/bits, so the
	 * density of safe primes should be about (1/bits)**2. Set test range
	 * to something well above bits**2 to be reasonably sure (but not
	 * guaranteed) of catching at least one safe prime.
	 */
	largewords = ((power * power) >> (SHIFT_WORD - TEST_POWER));

	/*
	 * Need idea of how much memory is available. We don't have to use all
	 * of it.
	 */
	if (largememory > LARGE_MAXIMUM) {
		logit("Limited memory: %u MB; limit %lu MB",
		    largememory, LARGE_MAXIMUM);
		largememory = LARGE_MAXIMUM;
	}

	if (largewords <= (largememory << SHIFT_MEGAWORD)) {
		logit("Increased memory: %u MB; need %u bytes",
		    largememory, (largewords << SHIFT_BYTE));
		largewords = (largememory << SHIFT_MEGAWORD);
	} else if (largememory > 0) {
		logit("Decreased memory: %u MB; want %u bytes",
		    largememory, (largewords << SHIFT_BYTE));
		largewords = (largememory << SHIFT_MEGAWORD);
	}

	TinySieve = xcalloc(tinywords, sizeof(u_int32_t));
	tinybits = tinywords << SHIFT_WORD;

	SmallSieve = xcalloc(smallwords, sizeof(u_int32_t));
	smallbits = smallwords << SHIFT_WORD;

	/*
	 * dynamically determine available memory
	 */
	while ((LargeSieve = calloc(largewords, sizeof(u_int32_t))) == NULL)
		largewords -= (1L << (SHIFT_MEGAWORD - 2)); /* 1/4 MB chunks */

	largebits = largewords << SHIFT_WORD;
	largenumbers = largebits * 2;	/* even numbers excluded */

	/* validation check: count the number of primes tried */
	largetries = 0;
	if ((q = BN_new()) == NULL)
		fatal("BN_new failed");

	/*
	 * Generate random starting point for subprime search, or use
	 * specified parameter.
	 */
	if ((largebase = BN_new()) == NULL)
		fatal("BN_new failed");
	if (start == NULL) {
		if (BN_rand(largebase, power, 1, 1) == 0)
			fatal("BN_rand failed");
	} else {
		if (BN_copy(largebase, start) == NULL)
			fatal("BN_copy: failed");
	}

	/* ensure odd */
	if (BN_set_bit(largebase, 0) == 0)
		fatal("BN_set_bit: failed");

	time(&time_start);

	logit("%.24s Sieve next %u plus %u-bit", ctime(&time_start),
	    largenumbers, power);
	debug2("start point: 0x%s", BN_bn2hex(largebase));

	/*
	 * TinySieve
	 */
	for (i = 0; i < tinybits; i++) {
		if (BIT_TEST(TinySieve, i))
			continue; /* 2*i+3 is composite */

		/* The next tiny prime */
		t = 2 * i + 3;

		/* Mark all multiples of t */
		for (j = i + t; j < tinybits; j += t)
			BIT_SET(TinySieve, j);

		sieve_large(t);
	}

	/*
	 * Start the small block search at the next possible prime. To avoid
	 * fencepost errors, the last pass is skipped.
	 */
	for (smallbase = TINY_NUMBER + 3;
	    smallbase < (SMALL_MAXIMUM - TINY_NUMBER);
	    smallbase += TINY_NUMBER) {
		for (i = 0; i < tinybits; i++) {
			if (BIT_TEST(TinySieve, i))
				continue; /* 2*i+3 is composite */

			/* The next tiny prime */
			t = 2 * i + 3;
			r = smallbase % t;

			if (r == 0) {
				s = 0; /* t divides into smallbase exactly */
			} else {
				/* smallbase+s is first entry divisible by t */
				s = t - r;
			}

			/*
			 * The sieve omits even numbers, so ensure that
			 * smallbase+s is odd. Then, step through the sieve
			 * in increments of 2*t
			 */
			if (s & 1)
				s += t; /* Make smallbase+s odd, and s even */

			/* Mark all multiples of 2*t */
			for (s /= 2; s < smallbits; s += t)
				BIT_SET(SmallSieve, s);
		}

		/*
		 * SmallSieve
		 */
		for (i = 0; i < smallbits; i++) {
			if (BIT_TEST(SmallSieve, i))
				continue; /* 2*i+smallbase is composite */

			/* The next small prime */
			sieve_large((2 * i) + smallbase);
		}

		memset(SmallSieve, 0, smallwords << SHIFT_BYTE);
	}

	time(&time_stop);

	logit("%.24s Sieved with %u small primes in %lld seconds",
	    ctime(&time_stop), largetries, (long long)(time_stop - time_start));

	for (j = r = 0; j < largebits; j++) {
		if (BIT_TEST(LargeSieve, j))
			continue; /* Definitely composite, skip */

		debug2("test q = largebase+%u", 2 * j);
		if (BN_set_word(q, 2 * j) == 0)
			fatal("BN_set_word failed");
		if (BN_add(q, q, largebase) == 0)
			fatal("BN_add failed");
		if (qfileout(out, MODULI_TYPE_SOPHIE_GERMAIN,
		    MODULI_TESTS_SIEVE, largetries,
		    (power - 1) /* MSB */, (0), q) == -1) {
			ret = -1;
			break;
		}

		r++; /* count q */
	}

	time(&time_stop);

	free(LargeSieve);
	free(SmallSieve);
	free(TinySieve);

	logit("%.24s Found %u candidates", ctime(&time_stop), r);

	return (ret);
}

static void
write_checkpoint(char *cpfile, u_int32_t lineno)
{
	FILE *fp;
	char tmp[PATH_MAX];
	int r;

	r = snprintf(tmp, sizeof(tmp), "%s.XXXXXXXXXX", cpfile);
	if (r == -1 || r >= PATH_MAX) {
		logit("write_checkpoint: temp pathname too long");
		return;
	}
	if ((r = mkstemp(tmp)) == -1) {
		logit("mkstemp(%s): %s", tmp, strerror(errno));
		return;
	}
	if ((fp = fdopen(r, "w")) == NULL) {
		logit("write_checkpoint: fdopen: %s", strerror(errno));
		unlink(tmp);
		close(r);
		return;
	}
	if (fprintf(fp, "%lu\n", (unsigned long)lineno) > 0 && fclose(fp) == 0
	    && rename(tmp, cpfile) == 0)
		debug3("wrote checkpoint line %lu to '%s'",
		    (unsigned long)lineno, cpfile);
	else
		logit("failed to write to checkpoint file '%s': %s", cpfile,
		    strerror(errno));
}

static unsigned long
read_checkpoint(char *cpfile)
{
	FILE *fp;
	unsigned long lineno = 0;

	if ((fp = fopen(cpfile, "r")) == NULL)
		return 0;
	if (fscanf(fp, "%lu\n", &lineno) < 1)
		logit("Failed to load checkpoint from '%s'", cpfile);
	else
		logit("Loaded checkpoint from '%s' line %lu", cpfile, lineno);
	fclose(fp);
	return lineno;
}

static unsigned long
count_lines(FILE *f)
{
	unsigned long count = 0;
	char lp[QLINESIZE + 1];

	if (fseek(f, 0, SEEK_SET) != 0) {
		debug("input file is not seekable");
		return ULONG_MAX;
	}
	while (fgets(lp, QLINESIZE + 1, f) != NULL)
		count++;
	rewind(f);
	debug("input file has %lu lines", count);
	return count;
}

static char *
fmt_time(time_t seconds)
{
	int day, hr, min;
	static char buf[128];

	min = (seconds / 60) % 60;
	hr = (seconds / 60 / 60) % 24;
	day = seconds / 60 / 60 / 24;
	if (day > 0)
		snprintf(buf, sizeof buf, "%dd %d:%02d", day, hr, min);
	else
		snprintf(buf, sizeof buf, "%d:%02d", hr, min);
	return buf;
}

static void
print_progress(unsigned long start_lineno, unsigned long current_lineno,
    unsigned long end_lineno)
{
	static time_t time_start, time_prev;
	time_t time_now, elapsed;
	unsigned long num_to_process, processed, remaining, percent, eta;
	double time_per_line;
	char *eta_str;

	time_now = monotime();
	if (time_start == 0) {
		time_start = time_prev = time_now;
		return;
	}
	/* print progress after 1m then once per 5m */
	if (time_now - time_prev < 5 * 60)
		return;
	time_prev = time_now;
	elapsed = time_now - time_start;
	processed = current_lineno - start_lineno;
	remaining = end_lineno - current_lineno;
	num_to_process = end_lineno - start_lineno;
	time_per_line = (double)elapsed / processed;
	/* if we don't know how many we're processing just report count+time */
	time(&time_now);
	if (end_lineno == ULONG_MAX) {
		logit("%.24s processed %lu in %s", ctime(&time_now),
		    processed, fmt_time(elapsed));
		return;
	}
	percent = 100 * processed / num_to_process;
	eta = time_per_line * remaining;
	eta_str = xstrdup(fmt_time(eta));
	logit("%.24s processed %lu of %lu (%lu%%) in %s, ETA %s",
	    ctime(&time_now), processed, num_to_process, percent,
	    fmt_time(elapsed), eta_str);
	free(eta_str);
}

/*
 * perform a Miller-Rabin primality test
 * on the list of candidates
 * (checking both q and p)
 * The result is a list of so-call "safe" primes
 */
int
prime_test(FILE *in, FILE *out, u_int32_t trials, u_int32_t generator_wanted,
    char *checkpoint_file, unsigned long start_lineno, unsigned long num_lines)
{
	BIGNUM *q, *p, *a;
	BN_CTX *ctx;
	char *cp, *lp;
	u_int32_t count_in = 0, count_out = 0, count_possible = 0;
	u_int32_t generator_known, in_tests, in_tries, in_type, in_size;
	unsigned long last_processed = 0, end_lineno;
	time_t time_start, time_stop;
	int res;

	if (trials < TRIAL_MINIMUM) {
		error("Minimum primality trials is %d", TRIAL_MINIMUM);
		return (-1);
	}

	if (num_lines == 0)
		end_lineno = count_lines(in);
	else
		end_lineno = start_lineno + num_lines;

	time(&time_start);

	if ((p = BN_new()) == NULL)
		fatal("BN_new failed");
	if ((q = BN_new()) == NULL)
		fatal("BN_new failed");
	if ((ctx = BN_CTX_new()) == NULL)
		fatal("BN_CTX_new failed");

	debug2("%.24s Final %u Miller-Rabin trials (%x generator)",
	    ctime(&time_start), trials, generator_wanted);

	if (checkpoint_file != NULL)
		last_processed = read_checkpoint(checkpoint_file);
	last_processed = start_lineno = MAXIMUM(last_processed, start_lineno);
	if (end_lineno == ULONG_MAX)
		debug("process from line %lu from pipe", last_processed);
	else
		debug("process from line %lu to line %lu", last_processed,
		    end_lineno);

	res = 0;
	lp = xmalloc(QLINESIZE + 1);
	while (fgets(lp, QLINESIZE + 1, in) != NULL && count_in < end_lineno) {
		count_in++;
		if (count_in <= last_processed) {
			debug3("skipping line %u, before checkpoint or "
			    "specified start line", count_in);
			continue;
		}
		if (checkpoint_file != NULL)
			write_checkpoint(checkpoint_file, count_in);
		print_progress(start_lineno, count_in, end_lineno);
		if (strlen(lp) < 14 || *lp == '!' || *lp == '#') {
			debug2("%10u: comment or short line", count_in);
			continue;
		}

		/* XXX - fragile parser */
		/* time */
		cp = &lp[14];	/* (skip) */

		/* type */
		in_type = strtoul(cp, &cp, 10);

		/* tests */
		in_tests = strtoul(cp, &cp, 10);

		if (in_tests & MODULI_TESTS_COMPOSITE) {
			debug2("%10u: known composite", count_in);
			continue;
		}

		/* tries */
		in_tries = strtoul(cp, &cp, 10);

		/* size (most significant bit) */
		in_size = strtoul(cp, &cp, 10);

		/* generator (hex) */
		generator_known = strtoul(cp, &cp, 16);

		/* Skip white space */
		cp += strspn(cp, " ");

		/* modulus (hex) */
		switch (in_type) {
		case MODULI_TYPE_SOPHIE_GERMAIN:
			debug2("%10u: (%u) Sophie-Germain", count_in, in_type);
			a = q;
			if (BN_hex2bn(&a, cp) == 0)
				fatal("BN_hex2bn failed");
			/* p = 2*q + 1 */
			if (BN_lshift(p, q, 1) == 0)
				fatal("BN_lshift failed");
			if (BN_add_word(p, 1) == 0)
				fatal("BN_add_word failed");
			in_size += 1;
			generator_known = 0;
			break;
		case MODULI_TYPE_UNSTRUCTURED:
		case MODULI_TYPE_SAFE:
		case MODULI_TYPE_SCHNORR:
		case MODULI_TYPE_STRONG:
		case MODULI_TYPE_UNKNOWN:
			debug2("%10u: (%u)", count_in, in_type);
			a = p;
			if (BN_hex2bn(&a, cp) == 0)
				fatal("BN_hex2bn failed");
			/* q = (p-1) / 2 */
			if (BN_rshift(q, p, 1) == 0)
				fatal("BN_rshift failed");
			break;
		default:
			debug2("Unknown prime type");
			break;
		}

		/*
		 * due to earlier inconsistencies in interpretation, check
		 * the proposed bit size.
		 */
		if ((u_int32_t)BN_num_bits(p) != (in_size + 1)) {
			debug2("%10u: bit size %u mismatch", count_in, in_size);
			continue;
		}
		if (in_size < QSIZE_MINIMUM) {
			debug2("%10u: bit size %u too short", count_in, in_size);
			continue;
		}

		if (in_tests & MODULI_TESTS_MILLER_RABIN)
			in_tries += trials;
		else
			in_tries = trials;

		/*
		 * guess unknown generator
		 */
		if (generator_known == 0) {
			if (BN_mod_word(p, 24) == 11)
				generator_known = 2;
			else if (BN_mod_word(p, 12) == 5)
				generator_known = 3;
			else {
				u_int32_t r = BN_mod_word(p, 10);

				if (r == 3 || r == 7)
					generator_known = 5;
			}
		}
		/*
		 * skip tests when desired generator doesn't match
		 */
		if (generator_wanted > 0 &&
		    generator_wanted != generator_known) {
			debug2("%10u: generator %d != %d",
			    count_in, generator_known, generator_wanted);
			continue;
		}

		/*
		 * Primes with no known generator are useless for DH, so
		 * skip those.
		 */
		if (generator_known == 0) {
			debug2("%10u: no known generator", count_in);
			continue;
		}

		count_possible++;

		/*
		 * The (1/4)^N performance bound on Miller-Rabin is
		 * extremely pessimistic, so don't spend a lot of time
		 * really verifying that q is prime until after we know
		 * that p is also prime. A single pass will weed out the
		 * vast majority of composite q's.
		 */
		if (BN_is_prime_ex(q, 1, ctx, NULL) <= 0) {
			debug("%10u: q failed first possible prime test",
			    count_in);
			continue;
		}

		/*
		 * q is possibly prime, so go ahead and really make sure
		 * that p is prime. If it is, then we can go back and do
		 * the same for q. If p is composite, chances are that
		 * will show up on the first Rabin-Miller iteration so it
		 * doesn't hurt to specify a high iteration count.
		 */
		if (!BN_is_prime_ex(p, trials, ctx, NULL)) {
			debug("%10u: p is not prime", count_in);
			continue;
		}
		debug("%10u: p is almost certainly prime", count_in);

		/* recheck q more rigorously */
		if (!BN_is_prime_ex(q, trials - 1, ctx, NULL)) {
			debug("%10u: q is not prime", count_in);
			continue;
		}
		debug("%10u: q is almost certainly prime", count_in);

		if (qfileout(out, MODULI_TYPE_SAFE,
		    in_tests | MODULI_TESTS_MILLER_RABIN,
		    in_tries, in_size, generator_known, p)) {
			res = -1;
			break;
		}

		count_out++;
	}

	time(&time_stop);
	free(lp);
	BN_free(p);
	BN_free(q);
	BN_CTX_free(ctx);

	if (checkpoint_file != NULL)
		unlink(checkpoint_file);

	logit("%.24s Found %u safe primes of %u candidates in %ld seconds",
	    ctime(&time_stop), count_out, count_possible,
	    (long) (time_stop - time_start));

	return (res);
}

#endif /* WITH_OPENSSL */
