/*
 * Cryptographic API.
 *
 * Support for VIA PadLock hardware crypto engine.
 *
 * Copyright (c) 2006  Michal Ludvig <michal@logix.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <crypto/padlock.h>
#include <crypto/sha.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <asm/cpu_device_id.h>
#include <asm/i387.h>

struct padlock_sha_desc {
	struct shash_desc fallback;
};

struct padlock_sha_ctx {
	struct crypto_shash *fallback;
};

static int padlock_sha_init(struct shash_desc *desc)
{
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);
	struct padlock_sha_ctx *ctx = crypto_shash_ctx(desc->tfm);

	dctx->fallback.tfm = ctx->fallback;
	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	return crypto_shash_init(&dctx->fallback);
}

static int padlock_sha_update(struct shash_desc *desc,
			      const u8 *data, unsigned int length)
{
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);

	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	return crypto_shash_update(&dctx->fallback, data, length);
}

static int padlock_sha_export(struct shash_desc *desc, void *out)
{
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);

	return crypto_shash_export(&dctx->fallback, out);
}

static int padlock_sha_import(struct shash_desc *desc, const void *in)
{
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);
	struct padlock_sha_ctx *ctx = crypto_shash_ctx(desc->tfm);

	dctx->fallback.tfm = ctx->fallback;
	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	return crypto_shash_import(&dctx->fallback, in);
}

static inline void padlock_output_block(uint32_t *src,
		 	uint32_t *dst, size_t count)
{
	while (count--)
		*dst++ = swab32(*src++);
}

static int padlock_sha1_finup(struct shash_desc *desc, const u8 *in,
			      unsigned int count, u8 *out)
{
	/* We can't store directly to *out as it may be unaligned. */
	/* BTW Don't reduce the buffer size below 128 Bytes!
	 *     PadLock microcode needs it that big. */
	char buf[128 + PADLOCK_ALIGNMENT - STACK_ALIGN] __attribute__
		((aligned(STACK_ALIGN)));
	char *result = PTR_ALIGN(&buf[0], PADLOCK_ALIGNMENT);
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);
	struct sha1_state state;
	unsigned int space;
	unsigned int leftover;
	int ts_state;
	int err;

	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	err = crypto_shash_export(&dctx->fallback, &state);
	if (err)
		goto out;

	if (state.count + count > ULONG_MAX)
		return crypto_shash_finup(&dctx->fallback, in, count, out);

	leftover = ((state.count - 1) & (SHA1_BLOCK_SIZE - 1)) + 1;
	space =  SHA1_BLOCK_SIZE - leftover;
	if (space) {
		if (count > space) {
			err = crypto_shash_update(&dctx->fallback, in, space) ?:
			      crypto_shash_export(&dctx->fallback, &state);
			if (err)
				goto out;
			count -= space;
			in += space;
		} else {
			memcpy(state.buffer + leftover, in, count);
			in = state.buffer;
			count += leftover;
			state.count &= ~(SHA1_BLOCK_SIZE - 1);
		}
	}

	memcpy(result, &state.state, SHA1_DIGEST_SIZE);

	/* prevent taking the spurious DNA fault with padlock. */
	ts_state = irq_ts_save();
	asm volatile (".byte 0xf3,0x0f,0xa6,0xc8" /* rep xsha1 */
		      : \
		      : "c"((unsigned long)state.count + count), \
			"a"((unsigned long)state.count), \
			"S"(in), "D"(result));
	irq_ts_restore(ts_state);

	padlock_output_block((uint32_t *)result, (uint32_t *)out, 5);

out:
	return err;
}

static int padlock_sha1_final(struct shash_desc *desc, u8 *out)
{
	u8 buf[4];

	return padlock_sha1_finup(desc, buf, 0, out);
}

static int padlock_sha256_finup(struct shash_desc *desc, const u8 *in,
				unsigned int count, u8 *out)
{
	/* We can't store directly to *out as it may be unaligned. */
	/* BTW Don't reduce the buffer size below 128 Bytes!
	 *     PadLock microcode needs it that big. */
	char buf[128 + PADLOCK_ALIGNMENT - STACK_ALIGN] __attribute__
		((aligned(STACK_ALIGN)));
	char *result = PTR_ALIGN(&buf[0], PADLOCK_ALIGNMENT);
	struct padlock_sha_desc *dctx = shash_desc_ctx(desc);
	struct sha256_state state;
	unsigned int space;
	unsigned int leftover;
	int ts_state;
	int err;

	dctx->fallback.flags = desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	err = crypto_shash_export(&dctx->fallback, &state);
	if (err)
		goto out;

	if (state.count + count > ULONG_MAX)
		return crypto_shash_finup(&dctx->fallback, in, count, out);

	leftover = ((state.count - 1) & (SHA256_BLOCK_SIZE - 1)) + 1;
	space =  SHA256_BLOCK_SIZE - leftover;
	if (space) {
		if (count > space) {
			err = crypto_shash_update(&dctx->fallback, in, space) ?:
			      crypto_shash_export(&dctx->fallback, &state);
			if (err)
				goto out;
			count -= space;
			in += space;
		} else {
			memcpy(state.buf + leftover, in, count);
			in = state.buf;
			count += leftover;
			state.count &= ~(SHA1_BLOCK_SIZE - 1);
		}
	}

	memcpy(result, &state.state, SHA256_DIGEST_SIZE);

	/* prevent taking the spurious DNA fault with padlock. */
	ts_state = irq_ts_save();
	asm volatile (".byte 0xf3,0x0f,0xa6,0xd0" /* rep xsha256 */
		      : \
		      : "c"((unsigned long)state.count + count), \
			"a"((unsigned long)state.count), \
			"S"(in), "D"(result));
	irq_ts_restore(ts_state);

	padlock_output_block((uint32_t *)result, (uint32_t *)out, 8);

out:
	return err;
}

static int padlock_sha256_final(struct shash_desc *desc, u8 *out)
{
	u8 buf[4];

	return padlock_sha256_finup(desc, buf, 0, out);
}

static int padlock_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_shash *hash = __crypto_shash_cast(tfm);
	const char *fallback_driver_name = crypto_tfm_alg_name(tfm);
	struct padlock_sha_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_shash *fallback_tfm;
	int err = -ENOMEM;

	/* Allocate a fallback and abort if it failed. */
	fallback_tfm = crypto_alloc_shash(fallback_driver_name, 0,
					  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback_tfm)) {
		printk(KERN_WARNING PFX "Fallback driver '%s' could not be loaded!\n",
		       fallback_driver_name);
		err = PTR_ERR(fallback_tfm);
		goto out;
	}

	ctx->fallback = fallback_tfm;
	hash->descsize += crypto_shash_descsize(fallback_tfm);
	return 0;

out:
	return err;
}

static void padlock_cra_exit(struct crypto_tfm *tfm)
{
	struct padlock_sha_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(ctx->fallback);
}

static struct shash_alg sha1_alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init   	= 	padlock_sha_init,
	.update 	=	padlock_sha_update,
	.finup  	=	padlock_sha1_finup,
	.final  	=	padlock_sha1_final,
	.export		=	padlock_sha_export,
	.import		=	padlock_sha_import,
	.descsize	=	sizeof(struct padlock_sha_desc),
	.statesize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name		=	"sha1",
		.cra_driver_name	=	"sha1-padlock",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_ALG_TYPE_SHASH |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		=	SHA1_BLOCK_SIZE,
		.cra_ctxsize		=	sizeof(struct padlock_sha_ctx),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	padlock_cra_init,
		.cra_exit		=	padlock_cra_exit,
	}
};

static struct shash_alg sha256_alg = {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init   	= 	padlock_sha_init,
	.update 	=	padlock_sha_update,
	.finup  	=	padlock_sha256_finup,
	.final  	=	padlock_sha256_final,
	.export		=	padlock_sha_export,
	.import		=	padlock_sha_import,
	.descsize	=	sizeof(struct padlock_sha_desc),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name		=	"sha256",
		.cra_driver_name	=	"sha256-padlock",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_ALG_TYPE_SHASH |
						CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		=	SHA256_BLOCK_SIZE,
		.cra_ctxsize		=	sizeof(struct padlock_sha_ctx),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	padlock_cra_init,
		.cra_exit		=	padlock_cra_exit,
	}
};

/* Add two shash_alg instance for hardware-implemented *
* multiple-parts hash supported by VIA Nano Processor.*/
static int padlock_sha1_init_nano(struct shash_desc *desc)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	*sctx = (struct sha1_state){
		.state = { SHA1_H0, SHA1_H1, SHA1_H2, SHA1_H3, SHA1_H4 },
	};

	return 0;
}

static int padlock_sha1_update_nano(struct shash_desc *desc,
			const u8 *data,	unsigned int len)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	unsigned int partial, done;
	const u8 *src;
	/*The PHE require the out buffer must 128 bytes and 16-bytes aligned*/
	u8 buf[128 + PADLOCK_ALIGNMENT - STACK_ALIGN] __attribute__
		((aligned(STACK_ALIGN)));
	u8 *dst = PTR_ALIGN(&buf[0], PADLOCK_ALIGNMENT);
	int ts_state;

	partial = sctx->count & 0x3f;
	sctx->count += len;
	done = 0;
	src = data;
	memcpy(dst, (u8 *)(sctx->state), SHA1_DIGEST_SIZE);

	if ((partial + len) >= SHA1_BLOCK_SIZE) {

		/* Append the bytes in state's buffer to a block to handle */
		if (partial) {
			done = -partial;
			memcpy(sctx->buffer + partial, data,
				done + SHA1_BLOCK_SIZE);
			src = sctx->buffer;
			ts_state = irq_ts_save();
			asm volatile (".byte 0xf3,0x0f,0xa6,0xc8"
			: "+S"(src), "+D"(dst) \
			: "a"((long)-1), "c"((unsigned long)1));
			irq_ts_restore(ts_state);
			done += SHA1_BLOCK_SIZE;
			src = data + done;
		}

		/* Process the left bytes from the input data */
		if (len - done >= SHA1_BLOCK_SIZE) {
			ts_state = irq_ts_save();
			asm volatile (".byte 0xf3,0x0f,0xa6,0xc8"
			: "+S"(src), "+D"(dst)
			: "a"((long)-1),
			"c"((unsigned long)((len - done) / SHA1_BLOCK_SIZE)));
			irq_ts_restore(ts_state);
			done += ((len - done) - (len - done) % SHA1_BLOCK_SIZE);
			src = data + done;
		}
		partial = 0;
	}
	memcpy((u8 *)(sctx->state), dst, SHA1_DIGEST_SIZE);
	memcpy(sctx->buffer + partial, src, len - done);

	return 0;
}

static int padlock_sha1_final_nano(struct shash_desc *desc, u8 *out)
{
	struct sha1_state *state = (struct sha1_state *)shash_desc_ctx(desc);
	unsigned int partial, padlen;
	__be64 bits;
	static const u8 padding[64] = { 0x80, };

	bits = cpu_to_be64(state->count << 3);

	/* Pad out to 56 mod 64 */
	partial = state->count & 0x3f;
	padlen = (partial < 56) ? (56 - partial) : ((64+56) - partial);
	padlock_sha1_update_nano(desc, padding, padlen);

	/* Append length field bytes */
	padlock_sha1_update_nano(desc, (const u8 *)&bits, sizeof(bits));

	/* Swap to output */
	padlock_output_block((uint32_t *)(state->state), (uint32_t *)out, 5);

	return 0;
}

static int padlock_sha256_init_nano(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	*sctx = (struct sha256_state){
		.state = { SHA256_H0, SHA256_H1, SHA256_H2, SHA256_H3, \
				SHA256_H4, SHA256_H5, SHA256_H6, SHA256_H7},
	};

	return 0;
}

static int padlock_sha256_update_nano(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int partial, done;
	const u8 *src;
	/*The PHE require the out buffer must 128 bytes and 16-bytes aligned*/
	u8 buf[128 + PADLOCK_ALIGNMENT - STACK_ALIGN] __attribute__
		((aligned(STACK_ALIGN)));
	u8 *dst = PTR_ALIGN(&buf[0], PADLOCK_ALIGNMENT);
	int ts_state;

	partial = sctx->count & 0x3f;
	sctx->count += len;
	done = 0;
	src = data;
	memcpy(dst, (u8 *)(sctx->state), SHA256_DIGEST_SIZE);

	if ((partial + len) >= SHA256_BLOCK_SIZE) {

		/* Append the bytes in state's buffer to a block to handle */
		if (partial) {
			done = -partial;
			memcpy(sctx->buf + partial, data,
				done + SHA256_BLOCK_SIZE);
			src = sctx->buf;
			ts_state = irq_ts_save();
			asm volatile (".byte 0xf3,0x0f,0xa6,0xd0"
			: "+S"(src), "+D"(dst)
			: "a"((long)-1), "c"((unsigned long)1));
			irq_ts_restore(ts_state);
			done += SHA256_BLOCK_SIZE;
			src = data + done;
		}

		/* Process the left bytes from input data*/
		if (len - done >= SHA256_BLOCK_SIZE) {
			ts_state = irq_ts_save();
			asm volatile (".byte 0xf3,0x0f,0xa6,0xd0"
			: "+S"(src), "+D"(dst)
			: "a"((long)-1),
			"c"((unsigned long)((len - done) / 64)));
			irq_ts_restore(ts_state);
			done += ((len - done) - (len - done) % 64);
			src = data + done;
		}
		partial = 0;
	}
	memcpy((u8 *)(sctx->state), dst, SHA256_DIGEST_SIZE);
	memcpy(sctx->buf + partial, src, len - done);

	return 0;
}

static int padlock_sha256_final_nano(struct shash_desc *desc, u8 *out)
{
	struct sha256_state *state =
		(struct sha256_state *)shash_desc_ctx(desc);
	unsigned int partial, padlen;
	__be64 bits;
	static const u8 padding[64] = { 0x80, };

	bits = cpu_to_be64(state->count << 3);

	/* Pad out to 56 mod 64 */
	partial = state->count & 0x3f;
	padlen = (partial < 56) ? (56 - partial) : ((64+56) - partial);
	padlock_sha256_update_nano(desc, padding, padlen);

	/* Append length field bytes */
	padlock_sha256_update_nano(desc, (const u8 *)&bits, sizeof(bits));

	/* Swap to output */
	padlock_output_block((uint32_t *)(state->state), (uint32_t *)out, 8);

	return 0;
}

static int padlock_sha_export_nano(struct shash_desc *desc,
				void *out)
{
	int statesize = crypto_shash_statesize(desc->tfm);
	void *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, statesize);
	return 0;
}

static int padlock_sha_import_nano(struct shash_desc *desc,
				const void *in)
{
	int statesize = crypto_shash_statesize(desc->tfm);
	void *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, statesize);
	return 0;
}

static struct shash_alg sha1_alg_nano = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	padlock_sha1_init_nano,
	.update		=	padlock_sha1_update_nano,
	.final		=	padlock_sha1_final_nano,
	.export		=	padlock_sha_export_nano,
	.import		=	padlock_sha_import_nano,
	.descsize	=	sizeof(struct sha1_state),
	.statesize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name		=	"sha1",
		.cra_driver_name	=	"sha1-padlock-nano",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize		=	SHA1_BLOCK_SIZE,
		.cra_module		=	THIS_MODULE,
	}
};

static struct shash_alg sha256_alg_nano = {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	padlock_sha256_init_nano,
	.update		=	padlock_sha256_update_nano,
	.final		=	padlock_sha256_final_nano,
	.export		=	padlock_sha_export_nano,
	.import		=	padlock_sha_import_nano,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name		=	"sha256",
		.cra_driver_name	=	"sha256-padlock-nano",
		.cra_priority		=	PADLOCK_CRA_PRIORITY,
		.cra_flags		=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize		=	SHA256_BLOCK_SIZE,
		.cra_module		=	THIS_MODULE,
	}
};

static struct x86_cpu_id padlock_sha_ids[] = {
	X86_FEATURE_MATCH(X86_FEATURE_PHE),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, padlock_sha_ids);

static int __init padlock_init(void)
{
	int rc = -ENODEV;
	struct cpuinfo_x86 *c = &cpu_data(0);
	struct shash_alg *sha1;
	struct shash_alg *sha256;

	if (!x86_match_cpu(padlock_sha_ids) || !cpu_has_phe_enabled)
		return -ENODEV;

	/* Register the newly added algorithm module if on *
	* VIA Nano processor, or else just do as before */
	if (c->x86_model < 0x0f) {
		sha1 = &sha1_alg;
		sha256 = &sha256_alg;
	} else {
		sha1 = &sha1_alg_nano;
		sha256 = &sha256_alg_nano;
	}

	rc = crypto_register_shash(sha1);
	if (rc)
		goto out;

	rc = crypto_register_shash(sha256);
	if (rc)
		goto out_unreg1;

	printk(KERN_NOTICE PFX "Using VIA PadLock ACE for SHA1/SHA256 algorithms.\n");

	return 0;

out_unreg1:
	crypto_unregister_shash(sha1);

out:
	printk(KERN_ERR PFX "VIA PadLock SHA1/SHA256 initialization failed.\n");
	return rc;
}

static void __exit padlock_fini(void)
{
	struct cpuinfo_x86 *c = &cpu_data(0);

	if (c->x86_model >= 0x0f) {
		crypto_unregister_shash(&sha1_alg_nano);
		crypto_unregister_shash(&sha256_alg_nano);
	} else {
		crypto_unregister_shash(&sha1_alg);
		crypto_unregister_shash(&sha256_alg);
	}
}

module_init(padlock_init);
module_exit(padlock_fini);

MODULE_DESCRIPTION("VIA PadLock SHA1/SHA256 algorithms support.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Ludvig");

MODULE_ALIAS("sha1-all");
MODULE_ALIAS("sha256-all");
MODULE_ALIAS("sha1-padlock");
MODULE_ALIAS("sha256-padlock");
