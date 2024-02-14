/*
 * Non-physical true random number generator based on timing jitter --
 * Jitter RNG standalone code.
 *
 * Copyright Stephan Mueller <smueller@chronox.de>, 2015 - 2023
 *
 * Design
 * ======
 *
 * See https://www.chronox.de/jent.html
 *
 * License
 * =======
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL2 are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/*
 * This Jitterentropy RNG is based on the jitterentropy library
 * version 3.4.0 provided at https://www.chronox.de/jent.html
 */

#ifdef __OPTIMIZE__
 #error "The CPU Jitter random number generator must not be compiled with optimizations. See documentation. Use the compiler switch -O0 for compiling jitterentropy.c."
#endif

typedef	unsigned long long	__u64;
typedef	long long		__s64;
typedef	unsigned int		__u32;
typedef unsigned char		u8;
#define NULL    ((void *) 0)

/* The entropy pool */
struct rand_data {
	/* SHA3-256 is used as conditioner */
#define DATA_SIZE_BITS 256
	/* all data values that are vital to maintain the security
	 * of the RNG are marked as SENSITIVE. A user must not
	 * access that information while the RNG executes its loops to
	 * calculate the next random value. */
	void *hash_state;		/* SENSITIVE hash state entropy pool */
	__u64 prev_time;		/* SENSITIVE Previous time stamp */
	__u64 last_delta;		/* SENSITIVE stuck test */
	__s64 last_delta2;		/* SENSITIVE stuck test */

	unsigned int flags;		/* Flags used to initialize */
	unsigned int osr;		/* Oversample rate */
#define JENT_MEMORY_ACCESSLOOPS 128
#define JENT_MEMORY_SIZE						\
	(CONFIG_CRYPTO_JITTERENTROPY_MEMORY_BLOCKS *			\
	 CONFIG_CRYPTO_JITTERENTROPY_MEMORY_BLOCKSIZE)
	unsigned char *mem;	/* Memory access location with size of
				 * memblocks * memblocksize */
	unsigned int memlocation; /* Pointer to byte in *mem */
	unsigned int memblocks;	/* Number of memory blocks in *mem */
	unsigned int memblocksize; /* Size of one memory block in bytes */
	unsigned int memaccessloops; /* Number of memory accesses per random
				      * bit generation */

	/* Repetition Count Test */
	unsigned int rct_count;			/* Number of stuck values */

	/* Adaptive Proportion Test cutoff values */
	unsigned int apt_cutoff; /* Intermittent health test failure */
	unsigned int apt_cutoff_permanent; /* Permanent health test failure */
#define JENT_APT_WINDOW_SIZE	512	/* Data window size */
	/* LSB of time stamp to process */
#define JENT_APT_LSB		16
#define JENT_APT_WORD_MASK	(JENT_APT_LSB - 1)
	unsigned int apt_observations;	/* Number of collected observations */
	unsigned int apt_count;		/* APT counter */
	unsigned int apt_base;		/* APT base reference */
	unsigned int health_failure;	/* Record health failure */

	unsigned int apt_base_set:1;	/* APT base reference set? */
};

/* Flags that can be used to initialize the RNG */
#define JENT_DISABLE_MEMORY_ACCESS (1<<2) /* Disable memory access for more
					   * entropy, saves MEMORY_SIZE RAM for
					   * entropy collector */

/* -- error codes for init function -- */
#define JENT_ENOTIME		1 /* Timer service not available */
#define JENT_ECOARSETIME	2 /* Timer too coarse for RNG */
#define JENT_ENOMONOTONIC	3 /* Timer is not monotonic increasing */
#define JENT_EVARVAR		5 /* Timer does not produce variations of
				   * variations (2nd derivation of time is
				   * zero). */
#define JENT_ESTUCK		8 /* Too many stuck results during init. */
#define JENT_EHEALTH		9 /* Health test failed during initialization */
#define JENT_ERCT	       10 /* RCT failed during initialization */
#define JENT_EHASH	       11 /* Hash self test failed */
#define JENT_EMEM	       12 /* Can't allocate memory for initialization */

#define JENT_RCT_FAILURE	1 /* Failure in RCT health test. */
#define JENT_APT_FAILURE	2 /* Failure in APT health test. */
#define JENT_PERMANENT_FAILURE_SHIFT	16
#define JENT_PERMANENT_FAILURE(x)	(x << JENT_PERMANENT_FAILURE_SHIFT)
#define JENT_RCT_FAILURE_PERMANENT	JENT_PERMANENT_FAILURE(JENT_RCT_FAILURE)
#define JENT_APT_FAILURE_PERMANENT	JENT_PERMANENT_FAILURE(JENT_APT_FAILURE)

/*
 * The output n bits can receive more than n bits of min entropy, of course,
 * but the fixed output of the conditioning function can only asymptotically
 * approach the output size bits of min entropy, not attain that bound. Random
 * maps will tend to have output collisions, which reduces the creditable
 * output entropy (that is what SP 800-90B Section 3.1.5.1.2 attempts to bound).
 *
 * The value "64" is justified in Appendix A.4 of the current 90C draft,
 * and aligns with NIST's in "epsilon" definition in this document, which is
 * that a string can be considered "full entropy" if you can bound the min
 * entropy in each bit of output to at least 1-epsilon, where epsilon is
 * required to be <= 2^(-32).
 */
#define JENT_ENTROPY_SAFETY_FACTOR	64

#include <linux/fips.h>
#include "jitterentropy.h"

/***************************************************************************
 * Adaptive Proportion Test
 *
 * This test complies with SP800-90B section 4.4.2.
 ***************************************************************************/

/*
 * See the SP 800-90B comment #10b for the corrected cutoff for the SP 800-90B
 * APT.
 * http://www.untruth.org/~josh/sp80090b/UL%20SP800-90B-final%20comments%20v1.9%2020191212.pdf
 * In in the syntax of R, this is C = 2 + qbinom(1 − 2^(−30), 511, 2^(-1/osr)).
 * (The original formula wasn't correct because the first symbol must
 * necessarily have been observed, so there is no chance of observing 0 of these
 * symbols.)
 *
 * For the alpha < 2^-53, R cannot be used as it uses a float data type without
 * arbitrary precision. A SageMath script is used to calculate those cutoff
 * values.
 *
 * For any value above 14, this yields the maximal allowable value of 512
 * (by FIPS 140-2 IG 7.19 Resolution # 16, we cannot choose a cutoff value that
 * renders the test unable to fail).
 */
static const unsigned int jent_apt_cutoff_lookup[15] = {
	325, 422, 459, 477, 488, 494, 499, 502,
	505, 507, 508, 509, 510, 511, 512 };
static const unsigned int jent_apt_cutoff_permanent_lookup[15] = {
	355, 447, 479, 494, 502, 507, 510, 512,
	512, 512, 512, 512, 512, 512, 512 };
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static void jent_apt_init(struct rand_data *ec, unsigned int osr)
{
	/*
	 * Establish the apt_cutoff based on the presumed entropy rate of
	 * 1/osr.
	 */
	if (osr >= ARRAY_SIZE(jent_apt_cutoff_lookup)) {
		ec->apt_cutoff = jent_apt_cutoff_lookup[
			ARRAY_SIZE(jent_apt_cutoff_lookup) - 1];
		ec->apt_cutoff_permanent = jent_apt_cutoff_permanent_lookup[
			ARRAY_SIZE(jent_apt_cutoff_permanent_lookup) - 1];
	} else {
		ec->apt_cutoff = jent_apt_cutoff_lookup[osr - 1];
		ec->apt_cutoff_permanent =
				jent_apt_cutoff_permanent_lookup[osr - 1];
	}
}
/*
 * Reset the APT counter
 *
 * @ec [in] Reference to entropy collector
 */
static void jent_apt_reset(struct rand_data *ec, unsigned int delta_masked)
{
	/* Reset APT counter */
	ec->apt_count = 0;
	ec->apt_base = delta_masked;
	ec->apt_observations = 0;
}

/*
 * Insert a new entropy event into APT
 *
 * @ec [in] Reference to entropy collector
 * @delta_masked [in] Masked time delta to process
 */
static void jent_apt_insert(struct rand_data *ec, unsigned int delta_masked)
{
	/* Initialize the base reference */
	if (!ec->apt_base_set) {
		ec->apt_base = delta_masked;
		ec->apt_base_set = 1;
		return;
	}

	if (delta_masked == ec->apt_base) {
		ec->apt_count++;

		/* Note, ec->apt_count starts with one. */
		if (ec->apt_count >= ec->apt_cutoff_permanent)
			ec->health_failure |= JENT_APT_FAILURE_PERMANENT;
		else if (ec->apt_count >= ec->apt_cutoff)
			ec->health_failure |= JENT_APT_FAILURE;
	}

	ec->apt_observations++;

	if (ec->apt_observations >= JENT_APT_WINDOW_SIZE)
		jent_apt_reset(ec, delta_masked);
}

/***************************************************************************
 * Stuck Test and its use as Repetition Count Test
 *
 * The Jitter RNG uses an enhanced version of the Repetition Count Test
 * (RCT) specified in SP800-90B section 4.4.1. Instead of counting identical
 * back-to-back values, the input to the RCT is the counting of the stuck
 * values during the generation of one Jitter RNG output block.
 *
 * The RCT is applied with an alpha of 2^{-30} compliant to FIPS 140-2 IG 9.8.
 *
 * During the counting operation, the Jitter RNG always calculates the RCT
 * cut-off value of C. If that value exceeds the allowed cut-off value,
 * the Jitter RNG output block will be calculated completely but discarded at
 * the end. The caller of the Jitter RNG is informed with an error code.
 ***************************************************************************/

/*
 * Repetition Count Test as defined in SP800-90B section 4.4.1
 *
 * @ec [in] Reference to entropy collector
 * @stuck [in] Indicator whether the value is stuck
 */
static void jent_rct_insert(struct rand_data *ec, int stuck)
{
	if (stuck) {
		ec->rct_count++;

		/*
		 * The cutoff value is based on the following consideration:
		 * alpha = 2^-30 or 2^-60 as recommended in SP800-90B.
		 * In addition, we require an entropy value H of 1/osr as this
		 * is the minimum entropy required to provide full entropy.
		 * Note, we collect (DATA_SIZE_BITS + ENTROPY_SAFETY_FACTOR)*osr
		 * deltas for inserting them into the entropy pool which should
		 * then have (close to) DATA_SIZE_BITS bits of entropy in the
		 * conditioned output.
		 *
		 * Note, ec->rct_count (which equals to value B in the pseudo
		 * code of SP800-90B section 4.4.1) starts with zero. Hence
		 * we need to subtract one from the cutoff value as calculated
		 * following SP800-90B. Thus C = ceil(-log_2(alpha)/H) = 30*osr
		 * or 60*osr.
		 */
		if ((unsigned int)ec->rct_count >= (60 * ec->osr)) {
			ec->rct_count = -1;
			ec->health_failure |= JENT_RCT_FAILURE_PERMANENT;
		} else if ((unsigned int)ec->rct_count >= (30 * ec->osr)) {
			ec->rct_count = -1;
			ec->health_failure |= JENT_RCT_FAILURE;
		}
	} else {
		/* Reset RCT */
		ec->rct_count = 0;
	}
}

static inline __u64 jent_delta(__u64 prev, __u64 next)
{
#define JENT_UINT64_MAX		(__u64)(~((__u64) 0))
	return (prev < next) ? (next - prev) :
			       (JENT_UINT64_MAX - prev + 1 + next);
}

/*
 * Stuck test by checking the:
 * 	1st derivative of the jitter measurement (time delta)
 * 	2nd derivative of the jitter measurement (delta of time deltas)
 * 	3rd derivative of the jitter measurement (delta of delta of time deltas)
 *
 * All values must always be non-zero.
 *
 * @ec [in] Reference to entropy collector
 * @current_delta [in] Jitter time delta
 *
 * @return
 * 	0 jitter measurement not stuck (good bit)
 * 	1 jitter measurement stuck (reject bit)
 */
static int jent_stuck(struct rand_data *ec, __u64 current_delta)
{
	__u64 delta2 = jent_delta(ec->last_delta, current_delta);
	__u64 delta3 = jent_delta(ec->last_delta2, delta2);

	ec->last_delta = current_delta;
	ec->last_delta2 = delta2;

	/*
	 * Insert the result of the comparison of two back-to-back time
	 * deltas.
	 */
	jent_apt_insert(ec, current_delta);

	if (!current_delta || !delta2 || !delta3) {
		/* RCT with a stuck bit */
		jent_rct_insert(ec, 1);
		return 1;
	}

	/* RCT with a non-stuck bit */
	jent_rct_insert(ec, 0);

	return 0;
}

/*
 * Report any health test failures
 *
 * @ec [in] Reference to entropy collector
 *
 * @return a bitmask indicating which tests failed
 *	0 No health test failure
 *	1 RCT failure
 *	2 APT failure
 *	1<<JENT_PERMANENT_FAILURE_SHIFT RCT permanent failure
 *	2<<JENT_PERMANENT_FAILURE_SHIFT APT permanent failure
 */
static unsigned int jent_health_failure(struct rand_data *ec)
{
	/* Test is only enabled in FIPS mode */
	if (!fips_enabled)
		return 0;

	return ec->health_failure;
}

/***************************************************************************
 * Noise sources
 ***************************************************************************/

/*
 * Update of the loop count used for the next round of
 * an entropy collection.
 *
 * Input:
 * @bits is the number of low bits of the timer to consider
 * @min is the number of bits we shift the timer value to the right at
 *	the end to make sure we have a guaranteed minimum value
 *
 * @return Newly calculated loop counter
 */
static __u64 jent_loop_shuffle(unsigned int bits, unsigned int min)
{
	__u64 time = 0;
	__u64 shuffle = 0;
	unsigned int i = 0;
	unsigned int mask = (1<<bits) - 1;

	jent_get_nstime(&time);

	/*
	 * We fold the time value as much as possible to ensure that as many
	 * bits of the time stamp are included as possible.
	 */
	for (i = 0; ((DATA_SIZE_BITS + bits - 1) / bits) > i; i++) {
		shuffle ^= time & mask;
		time = time >> bits;
	}

	/*
	 * We add a lower boundary value to ensure we have a minimum
	 * RNG loop count.
	 */
	return (shuffle + (1<<min));
}

/*
 * CPU Jitter noise source -- this is the noise source based on the CPU
 *			      execution time jitter
 *
 * This function injects the individual bits of the time value into the
 * entropy pool using a hash.
 *
 * ec [in] entropy collector
 * time [in] time stamp to be injected
 * stuck [in] Is the time stamp identified as stuck?
 *
 * Output:
 * updated hash context in the entropy collector or error code
 */
static int jent_condition_data(struct rand_data *ec, __u64 time, int stuck)
{
#define SHA3_HASH_LOOP (1<<3)
	struct {
		int rct_count;
		unsigned int apt_observations;
		unsigned int apt_count;
		unsigned int apt_base;
	} addtl = {
		ec->rct_count,
		ec->apt_observations,
		ec->apt_count,
		ec->apt_base
	};

	return jent_hash_time(ec->hash_state, time, (u8 *)&addtl, sizeof(addtl),
			      SHA3_HASH_LOOP, stuck);
}

/*
 * Memory Access noise source -- this is a noise source based on variations in
 *				 memory access times
 *
 * This function performs memory accesses which will add to the timing
 * variations due to an unknown amount of CPU wait states that need to be
 * added when accessing memory. The memory size should be larger than the L1
 * caches as outlined in the documentation and the associated testing.
 *
 * The L1 cache has a very high bandwidth, albeit its access rate is  usually
 * slower than accessing CPU registers. Therefore, L1 accesses only add minimal
 * variations as the CPU has hardly to wait. Starting with L2, significant
 * variations are added because L2 typically does not belong to the CPU any more
 * and therefore a wider range of CPU wait states is necessary for accesses.
 * L3 and real memory accesses have even a wider range of wait states. However,
 * to reliably access either L3 or memory, the ec->mem memory must be quite
 * large which is usually not desirable.
 *
 * @ec [in] Reference to the entropy collector with the memory access data -- if
 *	    the reference to the memory block to be accessed is NULL, this noise
 *	    source is disabled
 * @loop_cnt [in] if a value not equal to 0 is set, use the given value
 *		  number of loops to perform the LFSR
 */
static void jent_memaccess(struct rand_data *ec, __u64 loop_cnt)
{
	unsigned int wrap = 0;
	__u64 i = 0;
#define MAX_ACC_LOOP_BIT 7
#define MIN_ACC_LOOP_BIT 0
	__u64 acc_loop_cnt =
		jent_loop_shuffle(MAX_ACC_LOOP_BIT, MIN_ACC_LOOP_BIT);

	if (NULL == ec || NULL == ec->mem)
		return;
	wrap = ec->memblocksize * ec->memblocks;

	/*
	 * testing purposes -- allow test app to set the counter, not
	 * needed during runtime
	 */
	if (loop_cnt)
		acc_loop_cnt = loop_cnt;

	for (i = 0; i < (ec->memaccessloops + acc_loop_cnt); i++) {
		unsigned char *tmpval = ec->mem + ec->memlocation;
		/*
		 * memory access: just add 1 to one byte,
		 * wrap at 255 -- memory access implies read
		 * from and write to memory location
		 */
		*tmpval = (*tmpval + 1) & 0xff;
		/*
		 * Addition of memblocksize - 1 to pointer
		 * with wrap around logic to ensure that every
		 * memory location is hit evenly
		 */
		ec->memlocation = ec->memlocation + ec->memblocksize - 1;
		ec->memlocation = ec->memlocation % wrap;
	}
}

/***************************************************************************
 * Start of entropy processing logic
 ***************************************************************************/
/*
 * This is the heart of the entropy generation: calculate time deltas and
 * use the CPU jitter in the time deltas. The jitter is injected into the
 * entropy pool.
 *
 * WARNING: ensure that ->prev_time is primed before using the output
 *	    of this function! This can be done by calling this function
 *	    and not using its result.
 *
 * @ec [in] Reference to entropy collector
 *
 * @return result of stuck test
 */
static int jent_measure_jitter(struct rand_data *ec, __u64 *ret_current_delta)
{
	__u64 time = 0;
	__u64 current_delta = 0;
	int stuck;

	/* Invoke one noise source before time measurement to add variations */
	jent_memaccess(ec, 0);

	/*
	 * Get time stamp and calculate time delta to previous
	 * invocation to measure the timing variations
	 */
	jent_get_nstime(&time);
	current_delta = jent_delta(ec->prev_time, time);
	ec->prev_time = time;

	/* Check whether we have a stuck measurement. */
	stuck = jent_stuck(ec, current_delta);

	/* Now call the next noise sources which also injects the data */
	if (jent_condition_data(ec, current_delta, stuck))
		stuck = 1;

	/* return the raw entropy value */
	if (ret_current_delta)
		*ret_current_delta = current_delta;

	return stuck;
}

/*
 * Generator of one 64 bit random number
 * Function fills rand_data->hash_state
 *
 * @ec [in] Reference to entropy collector
 */
static void jent_gen_entropy(struct rand_data *ec)
{
	unsigned int k = 0, safety_factor = 0;

	if (fips_enabled)
		safety_factor = JENT_ENTROPY_SAFETY_FACTOR;

	/* priming of the ->prev_time value */
	jent_measure_jitter(ec, NULL);

	while (!jent_health_failure(ec)) {
		/* If a stuck measurement is received, repeat measurement */
		if (jent_measure_jitter(ec, NULL))
			continue;

		/*
		 * We multiply the loop value with ->osr to obtain the
		 * oversampling rate requested by the caller
		 */
		if (++k >= ((DATA_SIZE_BITS + safety_factor) * ec->osr))
			break;
	}
}

/*
 * Entry function: Obtain entropy for the caller.
 *
 * This function invokes the entropy gathering logic as often to generate
 * as many bytes as requested by the caller. The entropy gathering logic
 * creates 64 bit per invocation.
 *
 * This function truncates the last 64 bit entropy value output to the exact
 * size specified by the caller.
 *
 * @ec [in] Reference to entropy collector
 * @data [in] pointer to buffer for storing random data -- buffer must already
 *	      exist
 * @len [in] size of the buffer, specifying also the requested number of random
 *	     in bytes
 *
 * @return 0 when request is fulfilled or an error
 *
 * The following error codes can occur:
 *	-1	entropy_collector is NULL or the generation failed
 *	-2	Intermittent health failure
 *	-3	Permanent health failure
 */
int jent_read_entropy(struct rand_data *ec, unsigned char *data,
		      unsigned int len)
{
	unsigned char *p = data;

	if (!ec)
		return -1;

	while (len > 0) {
		unsigned int tocopy, health_test_result;

		jent_gen_entropy(ec);

		health_test_result = jent_health_failure(ec);
		if (health_test_result > JENT_PERMANENT_FAILURE_SHIFT) {
			/*
			 * At this point, the Jitter RNG instance is considered
			 * as a failed instance. There is no rerun of the
			 * startup test any more, because the caller
			 * is assumed to not further use this instance.
			 */
			return -3;
		} else if (health_test_result) {
			/*
			 * Perform startup health tests and return permanent
			 * error if it fails.
			 */
			if (jent_entropy_init(0, 0, NULL, ec)) {
				/* Mark the permanent error */
				ec->health_failure &=
					JENT_RCT_FAILURE_PERMANENT |
					JENT_APT_FAILURE_PERMANENT;
				return -3;
			}

			return -2;
		}

		if ((DATA_SIZE_BITS / 8) < len)
			tocopy = (DATA_SIZE_BITS / 8);
		else
			tocopy = len;
		if (jent_read_random_block(ec->hash_state, p, tocopy))
			return -1;

		len -= tocopy;
		p += tocopy;
	}

	return 0;
}

/***************************************************************************
 * Initialization logic
 ***************************************************************************/

struct rand_data *jent_entropy_collector_alloc(unsigned int osr,
					       unsigned int flags,
					       void *hash_state)
{
	struct rand_data *entropy_collector;

	entropy_collector = jent_zalloc(sizeof(struct rand_data));
	if (!entropy_collector)
		return NULL;

	if (!(flags & JENT_DISABLE_MEMORY_ACCESS)) {
		/* Allocate memory for adding variations based on memory
		 * access
		 */
		entropy_collector->mem = jent_kvzalloc(JENT_MEMORY_SIZE);
		if (!entropy_collector->mem) {
			jent_zfree(entropy_collector);
			return NULL;
		}
		entropy_collector->memblocksize =
			CONFIG_CRYPTO_JITTERENTROPY_MEMORY_BLOCKSIZE;
		entropy_collector->memblocks =
			CONFIG_CRYPTO_JITTERENTROPY_MEMORY_BLOCKS;
		entropy_collector->memaccessloops = JENT_MEMORY_ACCESSLOOPS;
	}

	/* verify and set the oversampling rate */
	if (osr == 0)
		osr = 1; /* H_submitter = 1 / osr */
	entropy_collector->osr = osr;
	entropy_collector->flags = flags;

	entropy_collector->hash_state = hash_state;

	/* Initialize the APT */
	jent_apt_init(entropy_collector, osr);

	/* fill the data pad with non-zero values */
	jent_gen_entropy(entropy_collector);

	return entropy_collector;
}

void jent_entropy_collector_free(struct rand_data *entropy_collector)
{
	jent_kvzfree(entropy_collector->mem, JENT_MEMORY_SIZE);
	entropy_collector->mem = NULL;
	jent_zfree(entropy_collector);
}

int jent_entropy_init(unsigned int osr, unsigned int flags, void *hash_state,
		      struct rand_data *p_ec)
{
	/*
	 * If caller provides an allocated ec, reuse it which implies that the
	 * health test entropy data is used to further still the available
	 * entropy pool.
	 */
	struct rand_data *ec = p_ec;
	int i, time_backwards = 0, ret = 0, ec_free = 0;
	unsigned int health_test_result;

	if (!ec) {
		ec = jent_entropy_collector_alloc(osr, flags, hash_state);
		if (!ec)
			return JENT_EMEM;
		ec_free = 1;
	} else {
		/* Reset the APT */
		jent_apt_reset(ec, 0);
		/* Ensure that a new APT base is obtained */
		ec->apt_base_set = 0;
		/* Reset the RCT */
		ec->rct_count = 0;
		/* Reset intermittent, leave permanent health test result */
		ec->health_failure &= (~JENT_RCT_FAILURE);
		ec->health_failure &= (~JENT_APT_FAILURE);
	}

	/* We could perform statistical tests here, but the problem is
	 * that we only have a few loop counts to do testing. These
	 * loop counts may show some slight skew and we produce
	 * false positives.
	 *
	 * Moreover, only old systems show potentially problematic
	 * jitter entropy that could potentially be caught here. But
	 * the RNG is intended for hardware that is available or widely
	 * used, but not old systems that are long out of favor. Thus,
	 * no statistical tests.
	 */

	/*
	 * We could add a check for system capabilities such as clock_getres or
	 * check for CONFIG_X86_TSC, but it does not make much sense as the
	 * following sanity checks verify that we have a high-resolution
	 * timer.
	 */
	/*
	 * TESTLOOPCOUNT needs some loops to identify edge systems. 100 is
	 * definitely too little.
	 *
	 * SP800-90B requires at least 1024 initial test cycles.
	 */
#define TESTLOOPCOUNT 1024
#define CLEARCACHE 100
	for (i = 0; (TESTLOOPCOUNT + CLEARCACHE) > i; i++) {
		__u64 start_time = 0, end_time = 0, delta = 0;

		/* Invoke core entropy collection logic */
		jent_measure_jitter(ec, &delta);
		end_time = ec->prev_time;
		start_time = ec->prev_time - delta;

		/* test whether timer works */
		if (!start_time || !end_time) {
			ret = JENT_ENOTIME;
			goto out;
		}

		/*
		 * test whether timer is fine grained enough to provide
		 * delta even when called shortly after each other -- this
		 * implies that we also have a high resolution timer
		 */
		if (!delta || (end_time == start_time)) {
			ret = JENT_ECOARSETIME;
			goto out;
		}

		/*
		 * up to here we did not modify any variable that will be
		 * evaluated later, but we already performed some work. Thus we
		 * already have had an impact on the caches, branch prediction,
		 * etc. with the goal to clear it to get the worst case
		 * measurements.
		 */
		if (i < CLEARCACHE)
			continue;

		/* test whether we have an increasing timer */
		if (!(end_time > start_time))
			time_backwards++;
	}

	/*
	 * we allow up to three times the time running backwards.
	 * CLOCK_REALTIME is affected by adjtime and NTP operations. Thus,
	 * if such an operation just happens to interfere with our test, it
	 * should not fail. The value of 3 should cover the NTP case being
	 * performed during our test run.
	 */
	if (time_backwards > 3) {
		ret = JENT_ENOMONOTONIC;
		goto out;
	}

	/* Did we encounter a health test failure? */
	health_test_result = jent_health_failure(ec);
	if (health_test_result) {
		ret = (health_test_result & JENT_RCT_FAILURE) ? JENT_ERCT :
								JENT_EHEALTH;
		goto out;
	}

out:
	if (ec_free)
		jent_entropy_collector_free(ec);

	return ret;
}
