/*
 * RNG implementation using standard kernel RNG.
 *
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * any later version.
 *
 */

#include <crypto/internal/rng.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/random.h>

static int krng_get_random(struct crypto_rng *tfm, u8 *rdata, unsigned int dlen)
{
	get_random_bytes(rdata, dlen);
	return 0;
}

static int krng_reset(struct crypto_rng *tfm, u8 *seed, unsigned int slen)
{
	return 0;
}

static struct crypto_alg krng_alg = {
	.cra_name		= "stdrng",
	.cra_driver_name	= "krng",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_RNG,
	.cra_ctxsize		= 0,
	.cra_type		= &crypto_rng_type,
	.cra_module		= THIS_MODULE,
	.cra_u			= {
		.rng = {
			.rng_make_random	= krng_get_random,
			.rng_reset		= krng_reset,
			.seedsize		= 0,
		}
	}
};


/* Module initalization */
static int __init krng_mod_init(void)
{
	return crypto_register_alg(&krng_alg);
}

static void __exit krng_mod_fini(void)
{
	crypto_unregister_alg(&krng_alg);
	return;
}

module_init(krng_mod_init);
module_exit(krng_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel Random Number Generator");
MODULE_ALIAS_CRYPTO("stdrng");
MODULE_ALIAS_CRYPTO("krng");
