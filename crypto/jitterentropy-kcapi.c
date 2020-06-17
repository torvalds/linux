/*
 * Non-physical true random number generator based on timing jitter --
 * Linux Kernel Crypto API specific code
 *
 * Copyright Stephan Mueller <smueller@chronox.de>, 2015
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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fips.h>
#include <linux/time.h>
#include <linux/crypto.h>
#include <crypto/internal/rng.h>

#include "jitterentropy.h"

/***************************************************************************
 * Helper function
 ***************************************************************************/

void *jent_zalloc(unsigned int len)
{
	return kzalloc(len, GFP_KERNEL);
}

void jent_zfree(void *ptr)
{
	kzfree(ptr);
}

int jent_fips_enabled(void)
{
	return fips_enabled;
}

void jent_panic(char *s)
{
	panic("%s", s);
}

void jent_memcpy(void *dest, const void *src, unsigned int n)
{
	memcpy(dest, src, n);
}

/*
 * Obtain a high-resolution time stamp value. The time stamp is used to measure
 * the execution time of a given code path and its variations. Hence, the time
 * stamp must have a sufficiently high resolution.
 *
 * Note, if the function returns zero because a given architecture does not
 * implement a high-resolution time stamp, the RNG code's runtime test
 * will detect it and will not produce output.
 */
void jent_get_nstime(__u64 *out)
{
	__u64 tmp = 0;

	tmp = random_get_entropy();

	/*
	 * If random_get_entropy does not return a value, i.e. it is not
	 * implemented for a given architecture, use a clock source.
	 * hoping that there are timers we can work with.
	 */
	if (tmp == 0)
		tmp = ktime_get_ns();

	*out = tmp;
}

/***************************************************************************
 * Kernel crypto API interface
 ***************************************************************************/

struct jitterentropy {
	spinlock_t jent_lock;
	struct rand_data *entropy_collector;
	unsigned int reset_cnt;
};

static int jent_kcapi_init(struct crypto_tfm *tfm)
{
	struct jitterentropy *rng = crypto_tfm_ctx(tfm);
	int ret = 0;

	rng->entropy_collector = jent_entropy_collector_alloc(1, 0);
	if (!rng->entropy_collector)
		ret = -ENOMEM;

	spin_lock_init(&rng->jent_lock);
	return ret;
}

static void jent_kcapi_cleanup(struct crypto_tfm *tfm)
{
	struct jitterentropy *rng = crypto_tfm_ctx(tfm);

	spin_lock(&rng->jent_lock);
	if (rng->entropy_collector)
		jent_entropy_collector_free(rng->entropy_collector);
	rng->entropy_collector = NULL;
	spin_unlock(&rng->jent_lock);
}

static int jent_kcapi_random(struct crypto_rng *tfm,
			     const u8 *src, unsigned int slen,
			     u8 *rdata, unsigned int dlen)
{
	struct jitterentropy *rng = crypto_rng_ctx(tfm);
	int ret = 0;

	spin_lock(&rng->jent_lock);

	/* Return a permanent error in case we had too many resets in a row. */
	if (rng->reset_cnt > (1<<10)) {
		ret = -EFAULT;
		goto out;
	}

	ret = jent_read_entropy(rng->entropy_collector, rdata, dlen);

	/* Reset RNG in case of health failures */
	if (ret < -1) {
		pr_warn_ratelimited("Reset Jitter RNG due to health test failure: %s failure\n",
				    (ret == -2) ? "Repetition Count Test" :
						  "Adaptive Proportion Test");

		rng->reset_cnt++;

		ret = -EAGAIN;
	} else {
		rng->reset_cnt = 0;

		/* Convert the Jitter RNG error into a usable error code */
		if (ret == -1)
			ret = -EINVAL;
	}

out:
	spin_unlock(&rng->jent_lock);

	return ret;
}

static int jent_kcapi_reset(struct crypto_rng *tfm,
			    const u8 *seed, unsigned int slen)
{
	return 0;
}

static struct rng_alg jent_alg = {
	.generate		= jent_kcapi_random,
	.seed			= jent_kcapi_reset,
	.seedsize		= 0,
	.base			= {
		.cra_name               = "jitterentropy_rng",
		.cra_driver_name        = "jitterentropy_rng",
		.cra_priority           = 100,
		.cra_ctxsize            = sizeof(struct jitterentropy),
		.cra_module             = THIS_MODULE,
		.cra_init               = jent_kcapi_init,
		.cra_exit               = jent_kcapi_cleanup,

	}
};

static int __init jent_mod_init(void)
{
	int ret = 0;

	ret = jent_entropy_init();
	if (ret) {
		pr_info("jitterentropy: Initialization failed with host not compliant with requirements: %d\n", ret);
		return -EFAULT;
	}
	return crypto_register_rng(&jent_alg);
}

static void __exit jent_mod_exit(void)
{
	crypto_unregister_rng(&jent_alg);
}

module_init(jent_mod_init);
module_exit(jent_mod_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Non-physical True Random Number Generator based on CPU Jitter");
MODULE_ALIAS_CRYPTO("jitterentropy_rng");
