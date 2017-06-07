/* 
 * Cryptographic API.
 *
 * Support for VIA PadLock hardware crypto engine.
 *
 * Copyright (c) 2004  Michal Ludvig <michal@logix.cz>
 *
 */

#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/padlock.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <asm/cpu_device_id.h>
#include <asm/byteorder.h>
#include <asm/processor.h>
#include <asm/fpu/api.h>

/*
 * Number of data blocks actually fetched for each xcrypt insn.
 * Processors with prefetch errata will fetch extra blocks.
 */
static unsigned int ecb_fetch_blocks = 2;
#define MAX_ECB_FETCH_BLOCKS (8)
#define ecb_fetch_bytes (ecb_fetch_blocks * AES_BLOCK_SIZE)

static unsigned int cbc_fetch_blocks = 1;
#define MAX_CBC_FETCH_BLOCKS (4)
#define cbc_fetch_bytes (cbc_fetch_blocks * AES_BLOCK_SIZE)

/* Control word. */
struct cword {
	unsigned int __attribute__ ((__packed__))
		rounds:4,
		algo:3,
		keygen:1,
		interm:1,
		encdec:1,
		ksize:2;
} __attribute__ ((__aligned__(PADLOCK_ALIGNMENT)));

/* Whenever making any changes to the following
 * structure *make sure* you keep E, d_data
 * and cword aligned on 16 Bytes boundaries and
 * the Hardware can access 16 * 16 bytes of E and d_data
 * (only the first 15 * 16 bytes matter but the HW reads
 * more).
 */
struct aes_ctx {
	u32 E[AES_MAX_KEYLENGTH_U32]
		__attribute__ ((__aligned__(PADLOCK_ALIGNMENT)));
	u32 d_data[AES_MAX_KEYLENGTH_U32]
		__attribute__ ((__aligned__(PADLOCK_ALIGNMENT)));
	struct {
		struct cword encrypt;
		struct cword decrypt;
	} cword;
	u32 *D;
};

static DEFINE_PER_CPU(struct cword *, paes_last_cword);

/* Tells whether the ACE is capable to generate
   the extended key for a given key_len. */
static inline int
aes_hw_extkey_available(uint8_t key_len)
{
	/* TODO: We should check the actual CPU model/stepping
	         as it's possible that the capability will be
	         added in the next CPU revisions. */
	if (key_len == 16)
		return 1;
	return 0;
}

static inline struct aes_ctx *aes_ctx_common(void *ctx)
{
	unsigned long addr = (unsigned long)ctx;
	unsigned long align = PADLOCK_ALIGNMENT;

	if (align <= crypto_tfm_ctx_alignment())
		align = 1;
	return (struct aes_ctx *)ALIGN(addr, align);
}

static inline struct aes_ctx *aes_ctx(struct crypto_tfm *tfm)
{
	return aes_ctx_common(crypto_tfm_ctx(tfm));
}

static inline struct aes_ctx *blk_aes_ctx(struct crypto_blkcipher *tfm)
{
	return aes_ctx_common(crypto_blkcipher_ctx(tfm));
}

static int aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	struct aes_ctx *ctx = aes_ctx(tfm);
	const __le32 *key = (const __le32 *)in_key;
	u32 *flags = &tfm->crt_flags;
	struct crypto_aes_ctx gen_aes;
	int cpu;

	if (key_len % 8) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	/*
	 * If the hardware is capable of generating the extended key
	 * itself we must supply the plain key for both encryption
	 * and decryption.
	 */
	ctx->D = ctx->E;

	ctx->E[0] = le32_to_cpu(key[0]);
	ctx->E[1] = le32_to_cpu(key[1]);
	ctx->E[2] = le32_to_cpu(key[2]);
	ctx->E[3] = le32_to_cpu(key[3]);

	/* Prepare control words. */
	memset(&ctx->cword, 0, sizeof(ctx->cword));

	ctx->cword.decrypt.encdec = 1;
	ctx->cword.encrypt.rounds = 10 + (key_len - 16) / 4;
	ctx->cword.decrypt.rounds = ctx->cword.encrypt.rounds;
	ctx->cword.encrypt.ksize = (key_len - 16) / 8;
	ctx->cword.decrypt.ksize = ctx->cword.encrypt.ksize;

	/* Don't generate extended keys if the hardware can do it. */
	if (aes_hw_extkey_available(key_len))
		goto ok;

	ctx->D = ctx->d_data;
	ctx->cword.encrypt.keygen = 1;
	ctx->cword.decrypt.keygen = 1;

	if (crypto_aes_expand_key(&gen_aes, in_key, key_len)) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	memcpy(ctx->E, gen_aes.key_enc, AES_MAX_KEYLENGTH);
	memcpy(ctx->D, gen_aes.key_dec, AES_MAX_KEYLENGTH);

ok:
	for_each_online_cpu(cpu)
		if (&ctx->cword.encrypt == per_cpu(paes_last_cword, cpu) ||
		    &ctx->cword.decrypt == per_cpu(paes_last_cword, cpu))
			per_cpu(paes_last_cword, cpu) = NULL;

	return 0;
}

/* ====== Encryption/decryption routines ====== */

/* These are the real call to PadLock. */
static inline void padlock_reset_key(struct cword *cword)
{
	int cpu = raw_smp_processor_id();

	if (cword != per_cpu(paes_last_cword, cpu))
#ifndef CONFIG_X86_64
		asm volatile ("pushfl; popfl");
#else
		asm volatile ("pushfq; popfq");
#endif
}

static inline void padlock_store_cword(struct cword *cword)
{
	per_cpu(paes_last_cword, raw_smp_processor_id()) = cword;
}

/*
 * While the padlock instructions don't use FP/SSE registers, they
 * generate a spurious DNA fault when CR0.TS is '1'.  Fortunately,
 * the kernel doesn't use CR0.TS.
 */

static inline void rep_xcrypt_ecb(const u8 *input, u8 *output, void *key,
				  struct cword *control_word, int count)
{
	asm volatile (".byte 0xf3,0x0f,0xa7,0xc8"	/* rep xcryptecb */
		      : "+S"(input), "+D"(output)
		      : "d"(control_word), "b"(key), "c"(count));
}

static inline u8 *rep_xcrypt_cbc(const u8 *input, u8 *output, void *key,
				 u8 *iv, struct cword *control_word, int count)
{
	asm volatile (".byte 0xf3,0x0f,0xa7,0xd0"	/* rep xcryptcbc */
		      : "+S" (input), "+D" (output), "+a" (iv)
		      : "d" (control_word), "b" (key), "c" (count));
	return iv;
}

static void ecb_crypt_copy(const u8 *in, u8 *out, u32 *key,
			   struct cword *cword, int count)
{
	/*
	 * Padlock prefetches extra data so we must provide mapped input buffers.
	 * Assume there are at least 16 bytes of stack already in use.
	 */
	u8 buf[AES_BLOCK_SIZE * (MAX_ECB_FETCH_BLOCKS - 1) + PADLOCK_ALIGNMENT - 1];
	u8 *tmp = PTR_ALIGN(&buf[0], PADLOCK_ALIGNMENT);

	memcpy(tmp, in, count * AES_BLOCK_SIZE);
	rep_xcrypt_ecb(tmp, out, key, cword, count);
}

static u8 *cbc_crypt_copy(const u8 *in, u8 *out, u32 *key,
			   u8 *iv, struct cword *cword, int count)
{
	/*
	 * Padlock prefetches extra data so we must provide mapped input buffers.
	 * Assume there are at least 16 bytes of stack already in use.
	 */
	u8 buf[AES_BLOCK_SIZE * (MAX_CBC_FETCH_BLOCKS - 1) + PADLOCK_ALIGNMENT - 1];
	u8 *tmp = PTR_ALIGN(&buf[0], PADLOCK_ALIGNMENT);

	memcpy(tmp, in, count * AES_BLOCK_SIZE);
	return rep_xcrypt_cbc(tmp, out, key, iv, cword, count);
}

static inline void ecb_crypt(const u8 *in, u8 *out, u32 *key,
			     struct cword *cword, int count)
{
	/* Padlock in ECB mode fetches at least ecb_fetch_bytes of data.
	 * We could avoid some copying here but it's probably not worth it.
	 */
	if (unlikely(offset_in_page(in) + ecb_fetch_bytes > PAGE_SIZE)) {
		ecb_crypt_copy(in, out, key, cword, count);
		return;
	}

	rep_xcrypt_ecb(in, out, key, cword, count);
}

static inline u8 *cbc_crypt(const u8 *in, u8 *out, u32 *key,
			    u8 *iv, struct cword *cword, int count)
{
	/* Padlock in CBC mode fetches at least cbc_fetch_bytes of data. */
	if (unlikely(offset_in_page(in) + cbc_fetch_bytes > PAGE_SIZE))
		return cbc_crypt_copy(in, out, key, iv, cword, count);

	return rep_xcrypt_cbc(in, out, key, iv, cword, count);
}

static inline void padlock_xcrypt_ecb(const u8 *input, u8 *output, void *key,
				      void *control_word, u32 count)
{
	u32 initial = count & (ecb_fetch_blocks - 1);

	if (count < ecb_fetch_blocks) {
		ecb_crypt(input, output, key, control_word, count);
		return;
	}

	if (initial)
		asm volatile (".byte 0xf3,0x0f,0xa7,0xc8"	/* rep xcryptecb */
			      : "+S"(input), "+D"(output)
			      : "d"(control_word), "b"(key), "c"(initial));

	asm volatile (".byte 0xf3,0x0f,0xa7,0xc8"	/* rep xcryptecb */
		      : "+S"(input), "+D"(output)
		      : "d"(control_word), "b"(key), "c"(count - initial));
}

static inline u8 *padlock_xcrypt_cbc(const u8 *input, u8 *output, void *key,
				     u8 *iv, void *control_word, u32 count)
{
	u32 initial = count & (cbc_fetch_blocks - 1);

	if (count < cbc_fetch_blocks)
		return cbc_crypt(input, output, key, iv, control_word, count);

	if (initial)
		asm volatile (".byte 0xf3,0x0f,0xa7,0xd0"	/* rep xcryptcbc */
			      : "+S" (input), "+D" (output), "+a" (iv)
			      : "d" (control_word), "b" (key), "c" (initial));

	asm volatile (".byte 0xf3,0x0f,0xa7,0xd0"	/* rep xcryptcbc */
		      : "+S" (input), "+D" (output), "+a" (iv)
		      : "d" (control_word), "b" (key), "c" (count-initial));
	return iv;
}

static void aes_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct aes_ctx *ctx = aes_ctx(tfm);

	padlock_reset_key(&ctx->cword.encrypt);
	ecb_crypt(in, out, ctx->E, &ctx->cword.encrypt, 1);
	padlock_store_cword(&ctx->cword.encrypt);
}

static void aes_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct aes_ctx *ctx = aes_ctx(tfm);

	padlock_reset_key(&ctx->cword.encrypt);
	ecb_crypt(in, out, ctx->D, &ctx->cword.decrypt, 1);
	padlock_store_cword(&ctx->cword.encrypt);
}

static struct crypto_alg aes_alg = {
	.cra_name		=	"aes",
	.cra_driver_name	=	"aes-padlock",
	.cra_priority		=	PADLOCK_CRA_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct aes_ctx),
	.cra_alignmask		=	PADLOCK_ALIGNMENT - 1,
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	AES_MIN_KEY_SIZE,
			.cia_max_keysize	=	AES_MAX_KEY_SIZE,
			.cia_setkey	   	= 	aes_set_key,
			.cia_encrypt	 	=	aes_encrypt,
			.cia_decrypt	  	=	aes_decrypt,
		}
	}
};

static int ecb_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct aes_ctx *ctx = blk_aes_ctx(desc->tfm);
	struct blkcipher_walk walk;
	int err;

	padlock_reset_key(&ctx->cword.encrypt);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		padlock_xcrypt_ecb(walk.src.virt.addr, walk.dst.virt.addr,
				   ctx->E, &ctx->cword.encrypt,
				   nbytes / AES_BLOCK_SIZE);
		nbytes &= AES_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	padlock_store_cword(&ctx->cword.encrypt);

	return err;
}

static int ecb_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct aes_ctx *ctx = blk_aes_ctx(desc->tfm);
	struct blkcipher_walk walk;
	int err;

	padlock_reset_key(&ctx->cword.decrypt);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		padlock_xcrypt_ecb(walk.src.virt.addr, walk.dst.virt.addr,
				   ctx->D, &ctx->cword.decrypt,
				   nbytes / AES_BLOCK_SIZE);
		nbytes &= AES_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	padlock_store_cword(&ctx->cword.encrypt);

	return err;
}

static struct crypto_alg ecb_aes_alg = {
	.cra_name		=	"ecb(aes)",
	.cra_driver_name	=	"ecb-aes-padlock",
	.cra_priority		=	PADLOCK_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct aes_ctx),
	.cra_alignmask		=	PADLOCK_ALIGNMENT - 1,
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	AES_MIN_KEY_SIZE,
			.max_keysize		=	AES_MAX_KEY_SIZE,
			.setkey	   		= 	aes_set_key,
			.encrypt		=	ecb_aes_encrypt,
			.decrypt		=	ecb_aes_decrypt,
		}
	}
};

static int cbc_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct aes_ctx *ctx = blk_aes_ctx(desc->tfm);
	struct blkcipher_walk walk;
	int err;

	padlock_reset_key(&ctx->cword.encrypt);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		u8 *iv = padlock_xcrypt_cbc(walk.src.virt.addr,
					    walk.dst.virt.addr, ctx->E,
					    walk.iv, &ctx->cword.encrypt,
					    nbytes / AES_BLOCK_SIZE);
		memcpy(walk.iv, iv, AES_BLOCK_SIZE);
		nbytes &= AES_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	padlock_store_cword(&ctx->cword.decrypt);

	return err;
}

static int cbc_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	struct aes_ctx *ctx = blk_aes_ctx(desc->tfm);
	struct blkcipher_walk walk;
	int err;

	padlock_reset_key(&ctx->cword.encrypt);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		padlock_xcrypt_cbc(walk.src.virt.addr, walk.dst.virt.addr,
				   ctx->D, walk.iv, &ctx->cword.decrypt,
				   nbytes / AES_BLOCK_SIZE);
		nbytes &= AES_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	padlock_store_cword(&ctx->cword.encrypt);

	return err;
}

static struct crypto_alg cbc_aes_alg = {
	.cra_name		=	"cbc(aes)",
	.cra_driver_name	=	"cbc-aes-padlock",
	.cra_priority		=	PADLOCK_COMPOSITE_PRIORITY,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct aes_ctx),
	.cra_alignmask		=	PADLOCK_ALIGNMENT - 1,
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	AES_MIN_KEY_SIZE,
			.max_keysize		=	AES_MAX_KEY_SIZE,
			.ivsize			=	AES_BLOCK_SIZE,
			.setkey	   		= 	aes_set_key,
			.encrypt		=	cbc_aes_encrypt,
			.decrypt		=	cbc_aes_decrypt,
		}
	}
};

static struct x86_cpu_id padlock_cpu_id[] = {
	X86_FEATURE_MATCH(X86_FEATURE_XCRYPT),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, padlock_cpu_id);

static int __init padlock_init(void)
{
	int ret;
	struct cpuinfo_x86 *c = &cpu_data(0);

	if (!x86_match_cpu(padlock_cpu_id))
		return -ENODEV;

	if (!boot_cpu_has(X86_FEATURE_XCRYPT_EN)) {
		printk(KERN_NOTICE PFX "VIA PadLock detected, but not enabled. Hmm, strange...\n");
		return -ENODEV;
	}

	if ((ret = crypto_register_alg(&aes_alg)))
		goto aes_err;

	if ((ret = crypto_register_alg(&ecb_aes_alg)))
		goto ecb_aes_err;

	if ((ret = crypto_register_alg(&cbc_aes_alg)))
		goto cbc_aes_err;

	printk(KERN_NOTICE PFX "Using VIA PadLock ACE for AES algorithm.\n");

	if (c->x86 == 6 && c->x86_model == 15 && c->x86_mask == 2) {
		ecb_fetch_blocks = MAX_ECB_FETCH_BLOCKS;
		cbc_fetch_blocks = MAX_CBC_FETCH_BLOCKS;
		printk(KERN_NOTICE PFX "VIA Nano stepping 2 detected: enabling workaround.\n");
	}

out:
	return ret;

cbc_aes_err:
	crypto_unregister_alg(&ecb_aes_alg);
ecb_aes_err:
	crypto_unregister_alg(&aes_alg);
aes_err:
	printk(KERN_ERR PFX "VIA PadLock AES initialization failed.\n");
	goto out;
}

static void __exit padlock_fini(void)
{
	crypto_unregister_alg(&cbc_aes_alg);
	crypto_unregister_alg(&ecb_aes_alg);
	crypto_unregister_alg(&aes_alg);
}

module_init(padlock_init);
module_exit(padlock_fini);

MODULE_DESCRIPTION("VIA PadLock AES algorithm support");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Ludvig");

MODULE_ALIAS_CRYPTO("aes");
