/*
 * Glue Code for the asm optimized version of the AES Cipher Algorithm
 *
 */

#include <linux/module.h>
#include <crypto/aes.h>
#include <asm/crypto/aes.h>

asmlinkage void aes_enc_blk(struct crypto_aes_ctx *ctx, u8 *out, const u8 *in);
asmlinkage void aes_dec_blk(struct crypto_aes_ctx *ctx, u8 *out, const u8 *in);

void crypto_aes_encrypt_x86(struct crypto_aes_ctx *ctx, u8 *dst, const u8 *src)
{
	aes_enc_blk(ctx, dst, src);
}
EXPORT_SYMBOL_GPL(crypto_aes_encrypt_x86);

void crypto_aes_decrypt_x86(struct crypto_aes_ctx *ctx, u8 *dst, const u8 *src)
{
	aes_dec_blk(ctx, dst, src);
}
EXPORT_SYMBOL_GPL(crypto_aes_decrypt_x86);

static void aes_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	aes_enc_blk(crypto_tfm_ctx(tfm), dst, src);
}

static void aes_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	aes_dec_blk(crypto_tfm_ctx(tfm), dst, src);
}

static struct crypto_alg aes_alg = {
	.cra_name		= "aes",
	.cra_driver_name	= "aes-asm",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
	.cra_module		= THIS_MODULE,
	.cra_u	= {
		.cipher	= {
			.cia_min_keysize	= AES_MIN_KEY_SIZE,
			.cia_max_keysize	= AES_MAX_KEY_SIZE,
			.cia_setkey		= crypto_aes_set_key,
			.cia_encrypt		= aes_encrypt,
			.cia_decrypt		= aes_decrypt
		}
	}
};

static int __init aes_init(void)
{
	return crypto_register_alg(&aes_alg);
}

static void __exit aes_fini(void)
{
	crypto_unregister_alg(&aes_alg);
}

module_init(aes_init);
module_exit(aes_fini);

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm, asm optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS("aes");
MODULE_ALIAS("aes-asm");
