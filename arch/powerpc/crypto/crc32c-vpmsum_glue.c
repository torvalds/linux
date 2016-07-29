#include <linux/crc32.h>
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <asm/switch_to.h>

#define CHKSUM_BLOCK_SIZE	1
#define CHKSUM_DIGEST_SIZE	4

#define VMX_ALIGN		16
#define VMX_ALIGN_MASK		(VMX_ALIGN-1)

#define VECTOR_BREAKPOINT	512

u32 __crc32c_vpmsum(u32 crc, unsigned char const *p, size_t len);

static u32 crc32c_vpmsum(u32 crc, unsigned char const *p, size_t len)
{
	unsigned int prealign;
	unsigned int tail;

	if (len < (VECTOR_BREAKPOINT + VMX_ALIGN) || in_interrupt())
		return __crc32c_le(crc, p, len);

	if ((unsigned long)p & VMX_ALIGN_MASK) {
		prealign = VMX_ALIGN - ((unsigned long)p & VMX_ALIGN_MASK);
		crc = __crc32c_le(crc, p, prealign);
		len -= prealign;
		p += prealign;
	}

	if (len & ~VMX_ALIGN_MASK) {
		pagefault_disable();
		enable_kernel_altivec();
		crc = __crc32c_vpmsum(crc, p, len & ~VMX_ALIGN_MASK);
		pagefault_enable();
	}

	tail = len & VMX_ALIGN_MASK;
	if (tail) {
		p += len & ~VMX_ALIGN_MASK;
		crc = __crc32c_le(crc, p, tail);
	}

	return crc;
}

static int crc32c_vpmsum_cra_init(struct crypto_tfm *tfm)
{
	u32 *key = crypto_tfm_ctx(tfm);

	*key = 0;

	return 0;
}

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int crc32c_vpmsum_setkey(struct crypto_shash *hash, const u8 *key,
			       unsigned int keylen)
{
	u32 *mctx = crypto_shash_ctx(hash);

	if (keylen != sizeof(u32)) {
		crypto_shash_set_flags(hash, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	*mctx = le32_to_cpup((__le32 *)key);
	return 0;
}

static int crc32c_vpmsum_init(struct shash_desc *desc)
{
	u32 *mctx = crypto_shash_ctx(desc->tfm);
	u32 *crcp = shash_desc_ctx(desc);

	*crcp = *mctx;

	return 0;
}

static int crc32c_vpmsum_update(struct shash_desc *desc, const u8 *data,
			       unsigned int len)
{
	u32 *crcp = shash_desc_ctx(desc);

	*crcp = crc32c_vpmsum(*crcp, data, len);

	return 0;
}

static int __crc32c_vpmsum_finup(u32 *crcp, const u8 *data, unsigned int len,
				u8 *out)
{
	*(__le32 *)out = ~cpu_to_le32(crc32c_vpmsum(*crcp, data, len));

	return 0;
}

static int crc32c_vpmsum_finup(struct shash_desc *desc, const u8 *data,
			      unsigned int len, u8 *out)
{
	return __crc32c_vpmsum_finup(shash_desc_ctx(desc), data, len, out);
}

static int crc32c_vpmsum_final(struct shash_desc *desc, u8 *out)
{
	u32 *crcp = shash_desc_ctx(desc);

	*(__le32 *)out = ~cpu_to_le32p(crcp);

	return 0;
}

static int crc32c_vpmsum_digest(struct shash_desc *desc, const u8 *data,
			       unsigned int len, u8 *out)
{
	return __crc32c_vpmsum_finup(crypto_shash_ctx(desc->tfm), data, len,
				     out);
}

static struct shash_alg alg = {
	.setkey		= crc32c_vpmsum_setkey,
	.init		= crc32c_vpmsum_init,
	.update		= crc32c_vpmsum_update,
	.final		= crc32c_vpmsum_final,
	.finup		= crc32c_vpmsum_finup,
	.digest		= crc32c_vpmsum_digest,
	.descsize	= sizeof(u32),
	.digestsize	= CHKSUM_DIGEST_SIZE,
	.base		= {
		.cra_name		= "crc32c",
		.cra_driver_name	= "crc32c-vpmsum",
		.cra_priority		= 200,
		.cra_blocksize		= CHKSUM_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(u32),
		.cra_module		= THIS_MODULE,
		.cra_init		= crc32c_vpmsum_cra_init,
	}
};

static int __init crc32c_vpmsum_mod_init(void)
{
	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	return crypto_register_shash(&alg);
}

static void __exit crc32c_vpmsum_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(crc32c_vpmsum_mod_init);
module_exit(crc32c_vpmsum_mod_fini);

MODULE_AUTHOR("Anton Blanchard <anton@samba.org>");
MODULE_DESCRIPTION("CRC32C using vector polynomial multiply-sum instructions");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("crc32c");
MODULE_ALIAS_CRYPTO("crc32c-vpmsum");
