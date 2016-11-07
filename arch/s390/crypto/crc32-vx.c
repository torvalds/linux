/*
 * Crypto-API module for CRC-32 algorithms implemented with the
 * z/Architecture Vector Extension Facility.
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#define KMSG_COMPONENT	"crc32-vx"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/cpufeature.h>
#include <linux/crc32.h>
#include <crypto/internal/hash.h>
#include <asm/fpu/api.h>


#define CRC32_BLOCK_SIZE	1
#define CRC32_DIGEST_SIZE	4

#define VX_MIN_LEN		64
#define VX_ALIGNMENT		16L
#define VX_ALIGN_MASK		(VX_ALIGNMENT - 1)

struct crc_ctx {
	u32 key;
};

struct crc_desc_ctx {
	u32 crc;
};

/* Prototypes for functions in assembly files */
u32 crc32_le_vgfm_16(u32 crc, unsigned char const *buf, size_t size);
u32 crc32_be_vgfm_16(u32 crc, unsigned char const *buf, size_t size);
u32 crc32c_le_vgfm_16(u32 crc, unsigned char const *buf, size_t size);

/*
 * DEFINE_CRC32_VX() - Define a CRC-32 function using the vector extension
 *
 * Creates a function to perform a particular CRC-32 computation. Depending
 * on the message buffer, the hardware-accelerated or software implementation
 * is used.   Note that the message buffer is aligned to improve fetch
 * operations of VECTOR LOAD MULTIPLE instructions.
 *
 */
#define DEFINE_CRC32_VX(___fname, ___crc32_vx, ___crc32_sw)		    \
	static u32 __pure ___fname(u32 crc,				    \
				unsigned char const *data, size_t datalen)  \
	{								    \
		struct kernel_fpu vxstate;				    \
		unsigned long prealign, aligned, remaining;		    \
									    \
		if (datalen < VX_MIN_LEN + VX_ALIGN_MASK)		    \
			return ___crc32_sw(crc, data, datalen);		    \
									    \
		if ((unsigned long)data & VX_ALIGN_MASK) {		    \
			prealign = VX_ALIGNMENT -			    \
				  ((unsigned long)data & VX_ALIGN_MASK);    \
			datalen -= prealign;				    \
			crc = ___crc32_sw(crc, data, prealign);		    \
			data = (void *)((unsigned long)data + prealign);    \
		}							    \
									    \
		aligned = datalen & ~VX_ALIGN_MASK;			    \
		remaining = datalen & VX_ALIGN_MASK;			    \
									    \
		kernel_fpu_begin(&vxstate, KERNEL_VXR_LOW);		    \
		crc = ___crc32_vx(crc, data, aligned);			    \
		kernel_fpu_end(&vxstate, KERNEL_VXR_LOW);		    \
									    \
		if (remaining)						    \
			crc = ___crc32_sw(crc, data + aligned, remaining);  \
									    \
		return crc;						    \
	}

DEFINE_CRC32_VX(crc32_le_vx, crc32_le_vgfm_16, crc32_le)
DEFINE_CRC32_VX(crc32_be_vx, crc32_be_vgfm_16, crc32_be)
DEFINE_CRC32_VX(crc32c_le_vx, crc32c_le_vgfm_16, __crc32c_le)


static int crc32_vx_cra_init_zero(struct crypto_tfm *tfm)
{
	struct crc_ctx *mctx = crypto_tfm_ctx(tfm);

	mctx->key = 0;
	return 0;
}

static int crc32_vx_cra_init_invert(struct crypto_tfm *tfm)
{
	struct crc_ctx *mctx = crypto_tfm_ctx(tfm);

	mctx->key = ~0;
	return 0;
}

static int crc32_vx_init(struct shash_desc *desc)
{
	struct crc_ctx *mctx = crypto_shash_ctx(desc->tfm);
	struct crc_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = mctx->key;
	return 0;
}

static int crc32_vx_setkey(struct crypto_shash *tfm, const u8 *newkey,
			   unsigned int newkeylen)
{
	struct crc_ctx *mctx = crypto_shash_ctx(tfm);

	if (newkeylen != sizeof(mctx->key)) {
		crypto_shash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	mctx->key = le32_to_cpu(*(__le32 *)newkey);
	return 0;
}

static int crc32be_vx_setkey(struct crypto_shash *tfm, const u8 *newkey,
			     unsigned int newkeylen)
{
	struct crc_ctx *mctx = crypto_shash_ctx(tfm);

	if (newkeylen != sizeof(mctx->key)) {
		crypto_shash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	mctx->key = be32_to_cpu(*(__be32 *)newkey);
	return 0;
}

static int crc32le_vx_final(struct shash_desc *desc, u8 *out)
{
	struct crc_desc_ctx *ctx = shash_desc_ctx(desc);

	*(__le32 *)out = cpu_to_le32p(&ctx->crc);
	return 0;
}

static int crc32be_vx_final(struct shash_desc *desc, u8 *out)
{
	struct crc_desc_ctx *ctx = shash_desc_ctx(desc);

	*(__be32 *)out = cpu_to_be32p(&ctx->crc);
	return 0;
}

static int crc32c_vx_final(struct shash_desc *desc, u8 *out)
{
	struct crc_desc_ctx *ctx = shash_desc_ctx(desc);

	/*
	 * Perform a final XOR with 0xFFFFFFFF to be in sync
	 * with the generic crc32c shash implementation.
	 */
	*(__le32 *)out = ~cpu_to_le32p(&ctx->crc);
	return 0;
}

static int __crc32le_vx_finup(u32 *crc, const u8 *data, unsigned int len,
			      u8 *out)
{
	*(__le32 *)out = cpu_to_le32(crc32_le_vx(*crc, data, len));
	return 0;
}

static int __crc32be_vx_finup(u32 *crc, const u8 *data, unsigned int len,
			      u8 *out)
{
	*(__be32 *)out = cpu_to_be32(crc32_be_vx(*crc, data, len));
	return 0;
}

static int __crc32c_vx_finup(u32 *crc, const u8 *data, unsigned int len,
			     u8 *out)
{
	/*
	 * Perform a final XOR with 0xFFFFFFFF to be in sync
	 * with the generic crc32c shash implementation.
	 */
	*(__le32 *)out = ~cpu_to_le32(crc32c_le_vx(*crc, data, len));
	return 0;
}


#define CRC32_VX_FINUP(alg, func)					      \
	static int alg ## _vx_finup(struct shash_desc *desc, const u8 *data,  \
				   unsigned int datalen, u8 *out)	      \
	{								      \
		return __ ## alg ## _vx_finup(shash_desc_ctx(desc),	      \
					      data, datalen, out);	      \
	}

CRC32_VX_FINUP(crc32le, crc32_le_vx)
CRC32_VX_FINUP(crc32be, crc32_be_vx)
CRC32_VX_FINUP(crc32c, crc32c_le_vx)

#define CRC32_VX_DIGEST(alg, func)					      \
	static int alg ## _vx_digest(struct shash_desc *desc, const u8 *data, \
				     unsigned int len, u8 *out)		      \
	{								      \
		return __ ## alg ## _vx_finup(crypto_shash_ctx(desc->tfm),    \
					      data, len, out);		      \
	}

CRC32_VX_DIGEST(crc32le, crc32_le_vx)
CRC32_VX_DIGEST(crc32be, crc32_be_vx)
CRC32_VX_DIGEST(crc32c, crc32c_le_vx)

#define CRC32_VX_UPDATE(alg, func)					      \
	static int alg ## _vx_update(struct shash_desc *desc, const u8 *data, \
				     unsigned int datalen)		      \
	{								      \
		struct crc_desc_ctx *ctx = shash_desc_ctx(desc);	      \
		ctx->crc = func(ctx->crc, data, datalen);		      \
		return 0;						      \
	}

CRC32_VX_UPDATE(crc32le, crc32_le_vx)
CRC32_VX_UPDATE(crc32be, crc32_be_vx)
CRC32_VX_UPDATE(crc32c, crc32c_le_vx)


static struct shash_alg crc32_vx_algs[] = {
	/* CRC-32 LE */
	{
		.init		=	crc32_vx_init,
		.setkey		=	crc32_vx_setkey,
		.update		=	crc32le_vx_update,
		.final		=	crc32le_vx_final,
		.finup		=	crc32le_vx_finup,
		.digest		=	crc32le_vx_digest,
		.descsize	=	sizeof(struct crc_desc_ctx),
		.digestsize	=	CRC32_DIGEST_SIZE,
		.base		=	{
			.cra_name	 = "crc32",
			.cra_driver_name = "crc32-vx",
			.cra_priority	 = 200,
			.cra_blocksize	 = CRC32_BLOCK_SIZE,
			.cra_ctxsize	 = sizeof(struct crc_ctx),
			.cra_module	 = THIS_MODULE,
			.cra_init	 = crc32_vx_cra_init_zero,
		},
	},
	/* CRC-32 BE */
	{
		.init		=	crc32_vx_init,
		.setkey		=	crc32be_vx_setkey,
		.update		=	crc32be_vx_update,
		.final		=	crc32be_vx_final,
		.finup		=	crc32be_vx_finup,
		.digest		=	crc32be_vx_digest,
		.descsize	=	sizeof(struct crc_desc_ctx),
		.digestsize	=	CRC32_DIGEST_SIZE,
		.base		=	{
			.cra_name	 = "crc32be",
			.cra_driver_name = "crc32be-vx",
			.cra_priority	 = 200,
			.cra_blocksize	 = CRC32_BLOCK_SIZE,
			.cra_ctxsize	 = sizeof(struct crc_ctx),
			.cra_module	 = THIS_MODULE,
			.cra_init	 = crc32_vx_cra_init_zero,
		},
	},
	/* CRC-32C LE */
	{
		.init		=	crc32_vx_init,
		.setkey		=	crc32_vx_setkey,
		.update		=	crc32c_vx_update,
		.final		=	crc32c_vx_final,
		.finup		=	crc32c_vx_finup,
		.digest		=	crc32c_vx_digest,
		.descsize	=	sizeof(struct crc_desc_ctx),
		.digestsize	=	CRC32_DIGEST_SIZE,
		.base		=	{
			.cra_name	 = "crc32c",
			.cra_driver_name = "crc32c-vx",
			.cra_priority	 = 200,
			.cra_blocksize	 = CRC32_BLOCK_SIZE,
			.cra_ctxsize	 = sizeof(struct crc_ctx),
			.cra_module	 = THIS_MODULE,
			.cra_init	 = crc32_vx_cra_init_invert,
		},
	},
};


static int __init crc_vx_mod_init(void)
{
	return crypto_register_shashes(crc32_vx_algs,
				       ARRAY_SIZE(crc32_vx_algs));
}

static void __exit crc_vx_mod_exit(void)
{
	crypto_unregister_shashes(crc32_vx_algs, ARRAY_SIZE(crc32_vx_algs));
}

module_cpu_feature_match(VXRS, crc_vx_mod_init);
module_exit(crc_vx_mod_exit);

MODULE_AUTHOR("Hendrik Brueckner <brueckner@linux.vnet.ibm.com>");
MODULE_LICENSE("GPL");

MODULE_ALIAS_CRYPTO("crc32");
MODULE_ALIAS_CRYPTO("crc32-vx");
MODULE_ALIAS_CRYPTO("crc32c");
MODULE_ALIAS_CRYPTO("crc32c-vx");
