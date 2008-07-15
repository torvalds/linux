/*
 * PRNG: Pseudo Random Number Generator
 *       Based on NIST Recommended PRNG From ANSI X9.31 Appendix A.2.4 using
 *       AES 128 cipher in RFC3686 ctr mode
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

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/crypto.h>
#include <linux/highmem.h>
#include <linux/moduleparam.h>
#include <linux/jiffies.h>
#include <linux/timex.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include "prng.h"

#define TEST_PRNG_ON_START 0

#define DEFAULT_PRNG_KEY "0123456789abcdef1011"
#define DEFAULT_PRNG_KSZ 20
#define DEFAULT_PRNG_IV "defaultv"
#define DEFAULT_PRNG_IVSZ 8
#define DEFAULT_BLK_SZ 16
#define DEFAULT_V_SEED "zaybxcwdveuftgsh"

/*
 * Flags for the prng_context flags field
 */

#define PRNG_FIXED_SIZE 0x1
#define PRNG_NEED_RESET 0x2

/*
 * Note: DT is our counter value
 * 	 I is our intermediate value
 *	 V is our seed vector
 * See http://csrc.nist.gov/groups/STM/cavp/documents/rng/931rngext.pdf
 * for implementation details
 */


struct prng_context {
	char *prng_key;
	char *prng_iv;
	spinlock_t prng_lock;
	unsigned char rand_data[DEFAULT_BLK_SZ];
	unsigned char last_rand_data[DEFAULT_BLK_SZ];
	unsigned char DT[DEFAULT_BLK_SZ];
	unsigned char I[DEFAULT_BLK_SZ];
	unsigned char V[DEFAULT_BLK_SZ];
	u32 rand_data_valid;
	struct crypto_blkcipher *tfm;
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

#define dbgprint(format, args...) do {if(dbg) printk(format, ##args);} while(0)

static void xor_vectors(unsigned char *in1, unsigned char *in2,
		        unsigned char *out, unsigned int size)
{
	int i;

	for (i=0;i<size;i++)
		out[i] = in1[i] ^ in2[i];

}
/*
 * Returns DEFAULT_BLK_SZ bytes of random data per call
 * returns 0 if generation succeded, <0 if something went wrong
 */
static int _get_more_prng_bytes(struct prng_context *ctx)
{
	int i;
	struct blkcipher_desc desc;
	struct scatterlist sg_in, sg_out;
	int ret;
	unsigned char tmp[DEFAULT_BLK_SZ];

	desc.tfm = ctx->tfm;
	desc.flags = 0;


	dbgprint(KERN_CRIT "Calling _get_more_prng_bytes for context %p\n",ctx);

	hexdump("Input DT: ", ctx->DT, DEFAULT_BLK_SZ);
	hexdump("Input I: ", ctx->I, DEFAULT_BLK_SZ);
	hexdump("Input V: ", ctx->V, DEFAULT_BLK_SZ);

	/*
	 * This algorithm is a 3 stage state machine
	 */
	for (i=0;i<3;i++) {

		desc.tfm = ctx->tfm;
		desc.flags = 0;
		switch (i) {
			case 0:
				/*
				 * Start by encrypting the counter value
				 * This gives us an intermediate value I
				 */
				memcpy(tmp, ctx->DT, DEFAULT_BLK_SZ);
				sg_init_one(&sg_out, &ctx->I[0], DEFAULT_BLK_SZ);
				hexdump("tmp stage 0: ", tmp, DEFAULT_BLK_SZ);
				break;
			case 1:

				/*
				 * Next xor I with our secret vector V
				 * encrypt that result to obtain our
				 * pseudo random data which we output
				 */
				xor_vectors(ctx->I, ctx->V, tmp, DEFAULT_BLK_SZ);
				sg_init_one(&sg_out, &ctx->rand_data[0], DEFAULT_BLK_SZ);
				hexdump("tmp stage 1: ", tmp, DEFAULT_BLK_SZ);
				break;
			case 2:
				/*
				 * First check that we didn't produce the same random data
				 * that we did last time around through this
				 */
				if (!memcmp(ctx->rand_data, ctx->last_rand_data, DEFAULT_BLK_SZ)) {
					printk(KERN_ERR "ctx %p Failed repetition check!\n",
						ctx);
					ctx->flags |= PRNG_NEED_RESET;
					return -1;
				}
				memcpy(ctx->last_rand_data, ctx->rand_data, DEFAULT_BLK_SZ);

				/*
				 * Lastly xor the random data with I
				 * and encrypt that to obtain a new secret vector V
				 */
				xor_vectors(ctx->rand_data, ctx->I, tmp, DEFAULT_BLK_SZ);
				sg_init_one(&sg_out, &ctx->V[0], DEFAULT_BLK_SZ);
				hexdump("tmp stage 2: ", tmp, DEFAULT_BLK_SZ);
				break;
		}

		/* Initialize our input buffer */
		sg_init_one(&sg_in, &tmp[0], DEFAULT_BLK_SZ);

		/* do the encryption */
		ret = crypto_blkcipher_encrypt(&desc, &sg_out, &sg_in, DEFAULT_BLK_SZ);

		/* And check the result */
		if (ret) {
			dbgprint(KERN_CRIT "Encryption of new block failed for context %p\n",ctx);
			ctx->rand_data_valid = DEFAULT_BLK_SZ;
			return -1;
		}

	}

	/*
	 * Now update our DT value
	 */
	for (i=DEFAULT_BLK_SZ-1;i>0;i--) {
		ctx->DT[i] = ctx->DT[i-1];
	}
	ctx->DT[0] += 1;

	dbgprint("Returning new block for context %p\n",ctx);
	ctx->rand_data_valid = 0;

	hexdump("Output DT: ", ctx->DT, DEFAULT_BLK_SZ);
	hexdump("Output I: ", ctx->I, DEFAULT_BLK_SZ);
	hexdump("Output V: ", ctx->V, DEFAULT_BLK_SZ);
	hexdump("New Random Data: ", ctx->rand_data, DEFAULT_BLK_SZ);

	return 0;
}

/* Our exported functions */
int get_prng_bytes(char *buf, int nbytes, struct prng_context *ctx)
{
	unsigned long flags;
	unsigned char *ptr = buf;
	unsigned int byte_count = (unsigned int)nbytes;
	int err;


	if (nbytes < 0)
		return -EINVAL;

	spin_lock_irqsave(&ctx->prng_lock, flags);

	err = -EFAULT;
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

	err = byte_count;

	dbgprint(KERN_CRIT "getting %d random bytes for context %p\n",byte_count, ctx);


remainder:
	if (ctx->rand_data_valid == DEFAULT_BLK_SZ) {
		if (_get_more_prng_bytes(ctx) < 0) {
			memset(buf, 0, nbytes);
			err = -EFAULT;
			goto done;
		}
	}

	/*
	 * Copy up to the next whole block size
	 */
	if (byte_count < DEFAULT_BLK_SZ) {
		for (;ctx->rand_data_valid < DEFAULT_BLK_SZ; ctx->rand_data_valid++) {
			*ptr = ctx->rand_data[ctx->rand_data_valid];
			ptr++;
			byte_count--;
			if (byte_count == 0)
				goto done;
		}
	}

	/*
	 * Now copy whole blocks
	 */
	for(;byte_count >= DEFAULT_BLK_SZ; byte_count -= DEFAULT_BLK_SZ) {
		if (_get_more_prng_bytes(ctx) < 0) {
			memset(buf, 0, nbytes);
			err = -1;
			goto done;
		}
		memcpy(ptr, ctx->rand_data, DEFAULT_BLK_SZ);
		ctx->rand_data_valid += DEFAULT_BLK_SZ;
		ptr += DEFAULT_BLK_SZ;
	}

	/*
	 * Now copy any extra partial data
	 */
	if (byte_count)
		goto remainder;

done:
	spin_unlock_irqrestore(&ctx->prng_lock, flags);
	dbgprint(KERN_CRIT "returning %d from get_prng_bytes in context %p\n",err, ctx);
	return err;
}
EXPORT_SYMBOL_GPL(get_prng_bytes);

struct prng_context *alloc_prng_context(void)
{
	struct prng_context *ctx=kzalloc(sizeof(struct prng_context), GFP_KERNEL);

	spin_lock_init(&ctx->prng_lock);

	if (reset_prng_context(ctx, NULL, NULL, NULL, NULL)) {
		kfree(ctx);
		ctx = NULL;
	}

	dbgprint(KERN_CRIT "returning context %p\n",ctx);
	return ctx;
}

EXPORT_SYMBOL_GPL(alloc_prng_context);

void free_prng_context(struct prng_context *ctx)
{
	crypto_free_blkcipher(ctx->tfm);
	kfree(ctx);
}
EXPORT_SYMBOL_GPL(free_prng_context);

int reset_prng_context(struct prng_context *ctx,
		       unsigned char *key, unsigned char *iv,
		       unsigned char *V, unsigned char *DT)
{
	int ret;
	int iv_len;
	int rc = -EFAULT;

	spin_lock(&ctx->prng_lock);
	ctx->flags |= PRNG_NEED_RESET;

	if (key)
		memcpy(ctx->prng_key,key,strlen(ctx->prng_key));
	else
		ctx->prng_key = DEFAULT_PRNG_KEY;

	if (iv)
		memcpy(ctx->prng_iv,iv, strlen(ctx->prng_iv));
	else
		ctx->prng_iv = DEFAULT_PRNG_IV;

	if (V)
		memcpy(ctx->V,V,DEFAULT_BLK_SZ);
	else
		memcpy(ctx->V,DEFAULT_V_SEED,DEFAULT_BLK_SZ);

	if (DT)
		memcpy(ctx->DT, DT, DEFAULT_BLK_SZ);
	else
		memset(ctx->DT, 0, DEFAULT_BLK_SZ);

	memset(ctx->rand_data,0,DEFAULT_BLK_SZ);
	memset(ctx->last_rand_data,0,DEFAULT_BLK_SZ);

	if (ctx->tfm)
		crypto_free_blkcipher(ctx->tfm);

	ctx->tfm = crypto_alloc_blkcipher("rfc3686(ctr(aes))",0,0);
	if (!ctx->tfm) {
		dbgprint(KERN_CRIT "Failed to alloc crypto tfm for context %p\n",ctx->tfm);
		goto out;
	}

	ctx->rand_data_valid = DEFAULT_BLK_SZ;

	ret = crypto_blkcipher_setkey(ctx->tfm, ctx->prng_key, strlen(ctx->prng_key));
	if (ret) {
		dbgprint(KERN_CRIT "PRNG: setkey() failed flags=%x\n",
			crypto_blkcipher_get_flags(ctx->tfm));
		crypto_free_blkcipher(ctx->tfm);
		goto out;
	}

	iv_len = crypto_blkcipher_ivsize(ctx->tfm);
	if (iv_len) {
		crypto_blkcipher_set_iv(ctx->tfm, ctx->prng_iv, iv_len);
	}
	rc = 0;
	ctx->flags &= ~PRNG_NEED_RESET;
out:
	spin_unlock(&ctx->prng_lock);

	return rc;

}
EXPORT_SYMBOL_GPL(reset_prng_context);

/* Module initalization */
static int __init prng_mod_init(void)
{

#ifdef TEST_PRNG_ON_START
	int i;
	unsigned char tmpbuf[DEFAULT_BLK_SZ];

	struct prng_context *ctx = alloc_prng_context();
	if (ctx == NULL)
		return -EFAULT;
	for (i=0;i<16;i++) {
		if (get_prng_bytes(tmpbuf, DEFAULT_BLK_SZ, ctx) < 0) {
			free_prng_context(ctx);
			return -EFAULT;
		}
	}
	free_prng_context(ctx);
#endif

	return 0;
}

static void __exit prng_mod_fini(void)
{
	return;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Software Pseudo Random Number Generator");
MODULE_AUTHOR("Neil Horman <nhorman@tuxdriver.com>");
module_param(dbg, int, 0);
MODULE_PARM_DESC(dbg, "Boolean to enable debugging (0/1 == off/on)");
module_init(prng_mod_init);
module_exit(prng_mod_fini);
