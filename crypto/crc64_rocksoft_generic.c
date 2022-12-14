// SPDX-License-Identifier: GPL-2.0-only

#include <linux/crc64.h>
#include <linux/module.h>
#include <crypto/internal/hash.h>
#include <asm/unaligned.h>

static int chksum_init(struct shash_desc *desc)
{
	u64 *crc = shash_desc_ctx(desc);

	*crc = 0;

	return 0;
}

static int chksum_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	u64 *crc = shash_desc_ctx(desc);

	*crc = crc64_rocksoft_generic(*crc, data, length);

	return 0;
}

static int chksum_final(struct shash_desc *desc, u8 *out)
{
	u64 *crc = shash_desc_ctx(desc);

	put_unaligned_le64(*crc, out);
	return 0;
}

static int __chksum_finup(u64 crc, const u8 *data, unsigned int len, u8 *out)
{
	crc = crc64_rocksoft_generic(crc, data, len);
	put_unaligned_le64(crc, out);
	return 0;
}

static int chksum_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	u64 *crc = shash_desc_ctx(desc);

	return __chksum_finup(*crc, data, len, out);
}

static int chksum_digest(struct shash_desc *desc, const u8 *data,
			 unsigned int length, u8 *out)
{
	return __chksum_finup(0, data, length, out);
}

static struct shash_alg alg = {
	.digestsize	= 	sizeof(u64),
	.init		=	chksum_init,
	.update		=	chksum_update,
	.final		=	chksum_final,
	.finup		=	chksum_finup,
	.digest		=	chksum_digest,
	.descsize	=	sizeof(u64),
	.base		=	{
		.cra_name		=	CRC64_ROCKSOFT_STRING,
		.cra_driver_name	=	"crc64-rocksoft-generic",
		.cra_priority		=	200,
		.cra_blocksize		=	1,
		.cra_module		=	THIS_MODULE,
	}
};

static int __init crc64_rocksoft_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit crc64_rocksoft_exit(void)
{
	crypto_unregister_shash(&alg);
}

module_init(crc64_rocksoft_init);
module_exit(crc64_rocksoft_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rocksoft model CRC64 calculation.");
MODULE_ALIAS_CRYPTO("crc64-rocksoft");
MODULE_ALIAS_CRYPTO("crc64-rocksoft-generic");
