/*
 * Glue Code for optimized 586 assembler version of AES
 */

#include <crypto/aes.h>
#include <linux/module.h>
#include <linux/crypto.h>

asmlinkage void aes_enc_blk(struct crypto_tfm *tfm, u8 *dst, const u8 *src);
asmlinkage void aes_dec_blk(struct crypto_tfm *tfm, u8 *dst, const u8 *src);

static void aes_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	aes_enc_blk(tfm, dst, src);
}

static void aes_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	aes_dec_blk(tfm, dst, src);
}

static struct crypto_alg aes_alg = {
	.cra_name		=	"aes",
	.cra_driver_name	=	"aes-i586",
	.cra_priority		=	200,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct crypto_aes_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(aes_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	AES_MIN_KEY_SIZE,
			.cia_max_keysize	=	AES_MAX_KEY_SIZE,
			.cia_setkey		=	crypto_aes_set_key,
			.cia_encrypt	 	=	aes_encrypt,
			.cia_decrypt	  	=	aes_decrypt
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

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm, i586 asm optimized");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Fruhwirth Clemens, James Morris, Brian Gladman, Adam Richter");
MODULE_ALIAS("aes");
