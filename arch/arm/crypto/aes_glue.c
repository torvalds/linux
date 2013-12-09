/*
 * Glue Code for the asm optimized version of the AES Cipher Algorithm
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <crypto/aes.h>

#include "aes_glue.h"

EXPORT_SYMBOL(AES_encrypt);
EXPORT_SYMBOL(AES_decrypt);
EXPORT_SYMBOL(private_AES_set_encrypt_key);
EXPORT_SYMBOL(private_AES_set_decrypt_key);

static void aes_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct AES_CTX *ctx = crypto_tfm_ctx(tfm);
	AES_encrypt(src, dst, &ctx->enc_key);
}

static void aes_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct AES_CTX *ctx = crypto_tfm_ctx(tfm);
	AES_decrypt(src, dst, &ctx->dec_key);
}

static int aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		unsigned int key_len)
{
	struct AES_CTX *ctx = crypto_tfm_ctx(tfm);

	switch (key_len) {
	case AES_KEYSIZE_128:
		key_len = 128;
		break;
	case AES_KEYSIZE_192:
		key_len = 192;
		break;
	case AES_KEYSIZE_256:
		key_len = 256;
		break;
	default:
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	if (private_AES_set_encrypt_key(in_key, key_len, &ctx->enc_key) == -1) {
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}
	/* private_AES_set_decrypt_key expects an encryption key as input */
	ctx->dec_key = ctx->enc_key;
	if (private_AES_set_decrypt_key(in_key, key_len, &ctx->dec_key) == -1) {
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}
	return 0;
}

static struct crypto_alg aes_alg = {
	.cra_name		= "aes",
	.cra_driver_name	= "aes-asm",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct AES_CTX),
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(aes_alg.cra_list),
	.cra_u	= {
		.cipher	= {
			.cia_min_keysize	= AES_MIN_KEY_SIZE,
			.cia_max_keysize	= AES_MAX_KEY_SIZE,
			.cia_setkey		= aes_set_key,
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

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm (ASM)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("aes");
MODULE_ALIAS("aes-asm");
MODULE_AUTHOR("David McCullough <ucdevel@gmail.com>");
