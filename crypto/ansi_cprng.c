/*
 * PRNG: Pseudo Random Number Generator
 *       Based on NIST Recommended PRNG From ANSI X9.31 Appendix A.2.4 using
 *       AES 128 cipher
 *
 *  (C) Neil Horman <nhorman@tuxdriver.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  any later version.
 *
 *
 */

#include <crypto/internal/rng.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>

#define DEFAULT_PRNG_KEY "0123456789abcdef"
#define DEFAULT_PRNG_KSZ 16
#define DEFAULT_BLK_SZ 16
#define DEFAULT_V_SEED "zaybxcwdveuftgsh"

/*
 * Flags for the prng_context flags field
 */

#define PRNG_FIXED_SIZE 0x1
#define PRNG_NEED_RESET 0x2

/*
 * Note: DT is our counter value
 *	 I is our intermediate value
 *	 V is our seed vector
 * See http://csrc.nist.gov/groups/STM/cavp/documents/rng/931rngext.pdf
 * for implementation details
 */


struct prng_context {
	spinlock_t prng_lock;
	unsigned char rand_data[DEFAULT_BLK_SZ];
	unsigned char last_rand_data[DEFAULT_BLK_SZ];
	unsigned char DT[DEFAULT_BLK_SZ];
	unsigned char I[DEFAULT_BLK_SZ];
	unsigned char V[DEFAULT_BLK_SZ];
	u32 rand_data_valid;
	struct crypto_cipher *tfm;
	u32 flags;
};

static int dbg;

static void hexdump(char *note, unsigned char *buf, unsigned int len)
{
	if (dbg) {
		printk(KERN_CRIT "%s", note);
		print_hex_dump(KERN_CONT, "", DUMP_PREFIX_OFFSET,
				16, 1,
				buf, len, false);
	}
}

#define dbgprint(format, args...) do {\
if (dbg)\
	printk(format, ##args);\
} while (0)

static void xor_vectors(unsigned char *in1, unsigned char *in2,
			unsigned char *out, unsigned int size)
{
	int i;

	for (i = 0; i < size; i++)
		out[i] = in1[i] ^ in2[i];

}
/*
 * Returns DEFAULT_BLK_SZ bytes of random data per call
 * returns 0 if generation succeeded, <0 if something went wrong
 */
static int _get_more_prng_bytes(struct prng_context *ctx, int cont_test)
{
	int i;
	unsigned char tmp[DEFAULT_BLK_SZ];
	unsigned char *output = NULL;


	dbgprint(KERN_CRIT "Calling _get_more_prng_bytes for context %p\n",
		ctx);

	hexdump("Input DT: ", ctx->DT, DEFAULT_BLK_SZ);
	hexdump("Input I: ", ctx->I, DEFAULT_BLK_SZ);
	hexdump("Input V: ", ctx->V, DEFAULT_BLK_SZ);

	/*
	 * This algorithm is a 3 stage state machine
	 */
	for (i = 0; i < 3; i++) {

		switch (i) {
		case 0:
			/*
			 * Start by encrypting the counter value
			 * This gives us an intermediate value I
			 */
			memcpy(tmp, ctx->DT, DEFAULT_BLK_SZ);
			output = ctx->I;
			hexdump("tmp stage 0: ", tmp, DEFAULT_BLK_SZ);
			break;
		case 1:

			/*
			 * Next xor I with our secret vector V
			 * encrypt that result to obtain our
			 * pseudo random data which we output
			 */
			xor_vectors(ctx->I, ctx->V, tmp, DEFAULT_BLK_SZ);
			hexdump("tmp stage 1: ", tmp, DEFAULT_BLK_SZ);
			output = ctx->rand_data;
			break;
		case 2:
			/*
			 * First check that we didn't produce the same
			 * random data that we did last time around through this
			 */
			if (!memcmp(ctx->rand_data, ctx->last_rand_data,
					DEFAULT_BLK_SZ)) {
				if (cont_test) {
					panic("cprng %p Failed repetition check!\n",
						ctx);
				}

				printk(KERN_ERR
					"ctx %p Failed repetition check!\n",
					ctx);

				ctx->flags |= PRNG_NEED_RESET;
				return -EINVAL;
			}
			memcpy(ctx->last_rand_data, ctx->rand_data,
				DEFAULT_BLK_SZ);

			/*
			 * Lastly xor the random data with I
			 * and encrypt that to obtain a new secret vector V
			 */
			xor_vectors(ctx->rand_data, ctx->I, tmp,
				DEFAULT_BLK_SZ);
			output = ctx->V;
			hexdump("tmp stage 2: ", tmp, DEFAULT_BLK_SZ);
			break;
		}


		/* do the encryption */
		crypto_cipher_encrypt_one(ctx->tfm, output, tmp);

	}

	/*
	 * Now update our DT value
	 */
	for (i = DEFAULT_BLK_SZ - 1; i >= 0; i--) {
		ctx->DT[i] += 1;
		if (ctx->DT[i] != 0)
			break;
	}

	dbgprint("Returning new block for context %p\n", ctx);
	ctx->rand_data_valid = 0;

	hexdump("Output DT: ", ctx->DT, DEFAULT_BLK_SZ);
	hexdump("Output I: ", ctx->I, DEFAULT_BLK_SZ);
	hexdump("Output V: ", ctx->V, DEFAULT_BLK_SZ);
	hexdump("New Random Data: ", ctx->rand_data, DEFAULT_BLK_SZ);

	return 0;
}

/* Our exported functions */
static int get_prng_bytes(char *buf, size_t nbytes, struct prng_context *ctx,
				int do_cont_test)
{
	unsigned char *ptr = buf;
	unsigned int byte_count = (unsigned int)nbytes;
	int err;


	spin_lock_bh(&ctx->prng_lock);

	err = -EINVAL;
	if (ctx->flags & PRNG_NEED_RESET)
		goto done;

	/*
	 * If the FIXED_SIZE flag is on, only return whole blocks of
	 * pseudo random data
	 */
	err = -EINVAL;
	if (ctx->flags & PRNG_FIXED_SIZE) {
		if (nbytes < DEFAULT_BLK_SZ)
			goto done;
		byte_count = DEFAULT_BLK_SZ;
	}

	/*
	 * Return 0 in case of success as mandated by the kernel
	 * crypto API interface definition.
	 */
	err = 0;

	dbgprint(KERN_CRIT "getting %d random bytes for context %p\n",
		byte_count, ctx);


remainder:
	if (ctx->rand_data_valid == DEFAULT_BLK_SZ) {
		if (_get_more_prng_bytes(ctx, do_cont_test) < 0) {
			memset(buf, 0, nbytes);
			err = -EINVAL;
			goto done;
		}
	}

	/*
	 * Copy any data less than an entire block
	 */
	if (byte_count < DEFAULT_BLK_SZ) {
empty_rbuf:
		while (ctx->rand_data_valid < DEFAULT_BLK_SZ) {
			*ptr = ctx->rand_data[ctx->rand_data_valid];
			ptr++;
			byte_count--;
			ctx->rand_data_valid++;
			if (byte_count == 0)
				goto done;
		}
	}

	/*
	 * Now copy whole blocks
	 */
	for (; byte_count >= DEFAULT_BLK_SZ; byte_count -= DEFAULT_BLK_SZ) {
		if (ctx->rand_data_valid == DEFAULT_BLK_SZ) {
			if (_get_more_prng_bytes(ctx, do_cont_test) < 0) {
				memset(buf, 0, nbytes);
				err = -EINVAL;
				goto done;
			}
		}
		if (ctx->rand_data_valid > 0)
			goto empty_rbuf;
		memcpy(ptr, ctx->rand_data, DEFAULT_BLK_SZ);
		ctx->rand_data_valid += DEFAULT_BLK_SZ;
		ptr += DEFAULT_BLK_SZ;
	}

	/*
	 * Now go back and get any remaining partial block
	 */
	if (byte_count)
		goto remainder;

done:
	spin_unlock_bh(&ctx->prng_lock);
	dbgprint(KERN_CRIT "returning %d from get_prng_bytes in context %p\n",
		err, ctx);
	return err;
}

static void free_prng_context(struct prng_context *ctx)
{
	crypto_free_cipher(ctx->tfm);
}

static int reset_prng_context(struct prng_context *ctx,
			      const unsigned char *key, size_t klen,
			      const unsigned char *V, const unsigned char *DT)
{
	int ret;
	const unsigned char *prng_key;

	spin_lock_bh(&ctx->prng_lock);
	ctx->flags |= PRNG_NEED_RESET;

	prng_key = (key != NULL) ? key : (unsigned char *)DEFAULT_PRNG_KEY;

	if (!key)
		klen = DEFAULT_PRNG_KSZ;

	if (V)
		memcpy(ctx->V, V, DEFAULT_BLK_SZ);
	else
		memcpy(ctx->V, DEFAULT_V_SEED, DEFAULT_BLK_SZ);

	if (DT)
		memcpy(ctx->DT, DT, DEFAULT_BLK_SZ);
	else
		memset(ctx->DT, 0, DEFAULT_BLK_SZ);

	memset(ctx->rand_data, 0, DEFAULT_BLK_SZ);
	memset(ctx->last_rand_data, 0, DEFAULT_BLK_SZ);

	ctx->rand_data_valid = DEFAULT_BLK_SZ;

	ret = crypto_cipher_setkey(ctx->tfm, prng_key, klen);
	if (ret) {
		dbgprint(KERN_CRIT "PRNG: setkey() failed flags=%x\n",
			crypto_cipher_get_flags(ctx->tfm));
		goto out;
	}

	ret = 0;
	ctx->flags &= ~PRNG_NEED_RESET;
out:
	spin_unlock_bh(&ctx->prng_lock);
	return ret;
}

static int cprng_init(struct crypto_tfm *tfm)
{
	struct prng_context *ctx = crypto_tfm_ctx(tfm);

	spin_lock_init(&ctx->prng_lock);
	ctx->tfm = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(ctx->tfm)) {
		dbgprint(KERN_CRIT "Failed to alloc tfm for context %p\n",
				ctx);
		return PTR_ERR(ctx->tfm);
	}

	if (reset_prng_context(ctx, NULL, DEFAULT_PRNG_KSZ, NULL, NULL) < 0)
		return -EINVAL;

	/*
	 * after allocation, we should always force the user to reset
	 * so they don't inadvertently use the insecure default values
	 * without specifying them intentially
	 */
	ctx->flags |= PRNG_NEED_RESET;
	return 0;
}

static void cprng_exit(struct crypto_tfm *tfm)
{
	free_prng_context(crypto_tfm_ctx(tfm));
}

static int cprng_get_random(struct crypto_rng *tfm,
			    const u8 *src, unsigned int slen,
			    u8 *rdata, unsigned int dlen)
{
	struct prng_context *prng = crypto_rng_ctx(tfm);

	return get_prng_bytes(rdata, dlen, prng, 0);
}

/*
 *  This is the cprng_registered reset method the seed value is
 *  interpreted as the tuple { V KEY DT}
 *  V and KEY are required during reset, and DT is optional, detected
 *  as being present by testing the length of the seed
 */
static int cprng_reset(struct crypto_rng *tfm,
		       const u8 *seed, unsigned int slen)
{
	struct prng_context *prng = crypto_rng_ctx(tfm);
	const u8 *key = seed + DEFAULT_BLK_SZ;
	const u8 *dt = NULL;

	if (slen < DEFAULT_PRNG_KSZ + DEFAULT_BLK_SZ)
		return -EINVAL;

	if (slen >= (2 * DEFAULT_BLK_SZ + DEFAULT_PRNG_KSZ))
		dt = key + DEFAULT_PRNG_KSZ;

	reset_prng_context(prng, key, DEFAULT_PRNG_KSZ, seed, dt);

	if (prng->flags & PRNG_NEED_RESET)
		return -EINVAL;
	return 0;
}

#ifdef CONFIG_CRYPTO_FIPS
static int fips_cprng_get_random(struct crypto_rng *tfm,
				 const u8 *src, unsigned int slen,
				 u8 *rdata, unsigned int dlen)
{
	struct prng_context *prng = crypto_rng_ctx(tfm);

	return get_prng_bytes(rdata, dlen, prng, 1);
}

static int fips_cprng_reset(struct crypto_rng *tfm,
			    const u8 *seed, unsigned int slen)
{
	u8 rdata[DEFAULT_BLK_SZ];
	const u8 *key = seed + DEFAULT_BLK_SZ;
	int rc;

	struct prng_context *prng = crypto_rng_ctx(tfm);

	if (slen < DEFAULT_PRNG_KSZ + DEFAULT_BLK_SZ)
		return -EINVAL;

	/* fips strictly requires seed != key */
	if (!memcmp(seed, key, DEFAULT_PRNG_KSZ))
		return -EINVAL;

	rc = cprng_reset(tfm, seed, slen);

	if (!rc)
		goto out;

	/* this primes our continuity test */
	rc = get_prng_bytes(rdata, DEFAULT_BLK_SZ, prng, 0);
	prng->rand_data_valid = DEFAULT_BLK_SZ;

out:
	return rc;
}
#endif

static struct rng_alg rng_algs[] = { {
	.generate		= cprng_get_random,
	.seed			= cprng_reset,
	.seedsize		= DEFAULT_PRNG_KSZ + 2 * DEFAULT_BLK_SZ,
	.base			=	{
		.cra_name		= "stdrng",
		.cra_driver_name	= "ansi_cprng",
		.cra_priority		= 100,
		.cra_ctxsize		= sizeof(struct prng_context),
		.cra_module		= THIS_MODULE,
		.cra_init		= cprng_init,
		.cra_exit		= cprng_exit,
	}
#ifdef CONFIG_CRYPTO_FIPS
}, {
	.generate		= fips_cprng_get_random,
	.seed			= fips_cprng_reset,
	.seedsize		= DEFAULT_PRNG_KSZ + 2 * DEFAULT_BLK_SZ,
	.base			=	{
		.cra_name		= "fips(ansi_cprng)",
		.cra_driver_name	= "fips_ansi_cprng",
		.cra_priority		= 300,
		.cra_ctxsize		= sizeof(struct prng_context),
		.cra_module		= THIS_MODULE,
		.cra_init		= cprng_init,
		.cra_exit		= cprng_exit,
	}
#endif
} };

/* Module initalization */
static int __init prng_mod_init(void)
{
	return crypto_register_rngs(rng_algs, ARRAY_SIZE(rng_algs));
}

static void __exit prng_mod_fini(void)
{
	crypto_unregister_rngs(rng_algs, ARRAY_SIZE(rng_algs));
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Software Pseudo Random Number Generator");
MODULE_AUTHOR("Neil Horman <nhorman@tuxdriver.com>");
module_param(dbg, int, 0);
MODULE_PARM_DESC(dbg, "Boolean to enable debugging (0/1 == off/on)");
subsys_initcall(prng_mod_init);
module_exit(prng_mod_fini);
MODULE_ALIAS_CRYPTO("stdrng");
MODULE_ALIAS_CRYPTO("ansi_cprng");
