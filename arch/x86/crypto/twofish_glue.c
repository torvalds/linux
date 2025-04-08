/*
 * Glue Code for assembler optimized version of TWOFISH
 *
 * Originally Twofish for GPG
 * By Matthew Skala <mskala@ansuz.sooke.bc.ca>, July 26, 1998
 * 256-bit key length added March 20, 1999
 * Some modifications to reduce the text size by Werner Koch, April, 1998
 * Ported to the kerneli patch by Marc Mutz <Marc@Mutz.com>
 * Ported to CryptoAPI by Colin Slater <hoho@tacomeat.net>
 *
 * The original author has disclaimed all copyright interest in this
 * code and thus put it in the public domain. The subsequent authors
 * have put this under the GNU General Public License.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 * This code is a "clean room" implementation, written from the paper
 * _Twofish: A 128-Bit Block Cipher_ by Bruce Schneier, John Kelsey,
 * Doug Whiting, David Wagner, Chris Hall, and Niels Ferguson, available
 * through http://www.counterpane.com/twofish.html
 *
 * For background information on multiplication in finite fields, used for
 * the matrix operations in the key schedule, see the book _Contemporary
 * Abstract Algebra_ by Joseph A. Gallian, especially chapter 22 in the
 * Third Edition.
 */

#include <crypto/algapi.h>
#include <crypto/twofish.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>

asmlinkage void twofish_enc_blk(struct twofish_ctx *ctx, u8 *dst,
				const u8 *src);
EXPORT_SYMBOL_GPL(twofish_enc_blk);
asmlinkage void twofish_dec_blk(struct twofish_ctx *ctx, u8 *dst,
				const u8 *src);
EXPORT_SYMBOL_GPL(twofish_dec_blk);

static void twofish_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	twofish_enc_blk(crypto_tfm_ctx(tfm), dst, src);
}

static void twofish_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	twofish_dec_blk(crypto_tfm_ctx(tfm), dst, src);
}

static struct crypto_alg alg = {
	.cra_name		=	"twofish",
	.cra_driver_name	=	"twofish-asm",
	.cra_priority		=	200,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	TF_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct twofish_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	TF_MIN_KEY_SIZE,
			.cia_max_keysize	=	TF_MAX_KEY_SIZE,
			.cia_setkey		=	twofish_setkey,
			.cia_encrypt		=	twofish_encrypt,
			.cia_decrypt		=	twofish_decrypt
		}
	}
};

static int __init twofish_glue_init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit twofish_glue_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(twofish_glue_init);
module_exit(twofish_glue_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION ("Twofish Cipher Algorithm, asm optimized");
MODULE_ALIAS_CRYPTO("twofish");
MODULE_ALIAS_CRYPTO("twofish-asm");
