/* Kernel cryptographic api.
 * cast6.c - Cast6 cipher algorithm [rfc2612].
 *
 * CAST-256 (*cast6*) is a DES like Substitution-Permutation Network (SPN)
 * cryptosystem built upon the CAST-128 (*cast5*) [rfc2144] encryption
 * algorithm.
 *
 * Copyright (C) 2003 Kartikey Mahendra Bhatt <kartik_me@hotmail.com>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */


#include <asm/byteorder.h>
#include <linux/init.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <crypto/cast6.h>

#define s1 cast_s1
#define s2 cast_s2
#define s3 cast_s3
#define s4 cast_s4

#define F1(D, r, m)  ((I = ((m) + (D))), (I = rol32(I, (r))),   \
	(((s1[I >> 24] ^ s2[(I>>16)&0xff]) - s3[(I>>8)&0xff]) + s4[I&0xff]))
#define F2(D, r, m)  ((I = ((m) ^ (D))), (I = rol32(I, (r))),   \
	(((s1[I >> 24] - s2[(I>>16)&0xff]) + s3[(I>>8)&0xff]) ^ s4[I&0xff]))
#define F3(D, r, m)  ((I = ((m) - (D))), (I = rol32(I, (r))),   \
	(((s1[I >> 24] + s2[(I>>16)&0xff]) ^ s3[(I>>8)&0xff]) - s4[I&0xff]))

static const u32 Tm[24][8] = {
	{ 0x5a827999, 0xc95c653a, 0x383650db, 0xa7103c7c, 0x15ea281d,
		0x84c413be, 0xf39dff5f, 0x6277eb00 } ,
	{ 0xd151d6a1, 0x402bc242, 0xaf05ade3, 0x1ddf9984, 0x8cb98525,
		0xfb9370c6, 0x6a6d5c67, 0xd9474808 } ,
	{ 0x482133a9, 0xb6fb1f4a, 0x25d50aeb, 0x94aef68c, 0x0388e22d,
		0x7262cdce, 0xe13cb96f, 0x5016a510 } ,
	{ 0xbef090b1, 0x2dca7c52, 0x9ca467f3, 0x0b7e5394, 0x7a583f35,
		0xe9322ad6, 0x580c1677, 0xc6e60218 } ,
	{ 0x35bfedb9, 0xa499d95a, 0x1373c4fb, 0x824db09c, 0xf1279c3d,
		0x600187de, 0xcedb737f, 0x3db55f20 } ,
	{ 0xac8f4ac1, 0x1b693662, 0x8a432203, 0xf91d0da4, 0x67f6f945,
		0xd6d0e4e6, 0x45aad087, 0xb484bc28 } ,
	{ 0x235ea7c9, 0x9238936a, 0x01127f0b, 0x6fec6aac, 0xdec6564d,
		0x4da041ee, 0xbc7a2d8f, 0x2b541930 } ,
	{ 0x9a2e04d1, 0x0907f072, 0x77e1dc13, 0xe6bbc7b4, 0x5595b355,
		0xc46f9ef6, 0x33498a97, 0xa2237638 } ,
	{ 0x10fd61d9, 0x7fd74d7a, 0xeeb1391b, 0x5d8b24bc, 0xcc65105d,
		0x3b3efbfe, 0xaa18e79f, 0x18f2d340 } ,
	{ 0x87ccbee1, 0xf6a6aa82, 0x65809623, 0xd45a81c4, 0x43346d65,
		0xb20e5906, 0x20e844a7, 0x8fc23048 } ,
	{ 0xfe9c1be9, 0x6d76078a, 0xdc4ff32b, 0x4b29decc, 0xba03ca6d,
		0x28ddb60e, 0x97b7a1af, 0x06918d50 } ,
	{ 0x756b78f1, 0xe4456492, 0x531f5033, 0xc1f93bd4, 0x30d32775,
		0x9fad1316, 0x0e86feb7, 0x7d60ea58 } ,
	{ 0xec3ad5f9, 0x5b14c19a, 0xc9eead3b, 0x38c898dc, 0xa7a2847d,
		0x167c701e, 0x85565bbf, 0xf4304760 } ,
	{ 0x630a3301, 0xd1e41ea2, 0x40be0a43, 0xaf97f5e4, 0x1e71e185,
		0x8d4bcd26, 0xfc25b8c7, 0x6affa468 } ,
	{ 0xd9d99009, 0x48b37baa, 0xb78d674b, 0x266752ec, 0x95413e8d,
		0x041b2a2e, 0x72f515cf, 0xe1cf0170 } ,
	{ 0x50a8ed11, 0xbf82d8b2, 0x2e5cc453, 0x9d36aff4, 0x0c109b95,
		0x7aea8736, 0xe9c472d7, 0x589e5e78 } ,
	{ 0xc7784a19, 0x365235ba, 0xa52c215b, 0x14060cfc, 0x82dff89d,
		0xf1b9e43e, 0x6093cfdf, 0xcf6dbb80 } ,
	{ 0x3e47a721, 0xad2192c2, 0x1bfb7e63, 0x8ad56a04, 0xf9af55a5,
		0x68894146, 0xd7632ce7, 0x463d1888 } ,
	{ 0xb5170429, 0x23f0efca, 0x92cadb6b, 0x01a4c70c, 0x707eb2ad,
		0xdf589e4e, 0x4e3289ef, 0xbd0c7590 } ,
	{ 0x2be66131, 0x9ac04cd2, 0x099a3873, 0x78742414, 0xe74e0fb5,
		0x5627fb56, 0xc501e6f7, 0x33dbd298 } ,
	{ 0xa2b5be39, 0x118fa9da, 0x8069957b, 0xef43811c, 0x5e1d6cbd,
		0xccf7585e, 0x3bd143ff, 0xaaab2fa0 } ,
	{ 0x19851b41, 0x885f06e2, 0xf738f283, 0x6612de24, 0xd4ecc9c5,
		0x43c6b566, 0xb2a0a107, 0x217a8ca8 } ,
	{ 0x90547849, 0xff2e63ea, 0x6e084f8b, 0xdce23b2c, 0x4bbc26cd,
		0xba96126e, 0x296ffe0f, 0x9849e9b0 } ,
	{ 0x0723d551, 0x75fdc0f2, 0xe4d7ac93, 0x53b19834, 0xc28b83d5,
		0x31656f76, 0xa03f5b17, 0x0f1946b8 }
};

static const u8 Tr[4][8] = {
	{ 0x13, 0x04, 0x15, 0x06, 0x17, 0x08, 0x19, 0x0a } ,
	{ 0x1b, 0x0c, 0x1d, 0x0e, 0x1f, 0x10, 0x01, 0x12 } ,
	{ 0x03, 0x14, 0x05, 0x16, 0x07, 0x18, 0x09, 0x1a } ,
	{ 0x0b, 0x1c, 0x0d, 0x1e, 0x0f, 0x00, 0x11, 0x02 }
};

/* forward octave */
static inline void W(u32 *key, unsigned int i)
{
	u32 I;
	key[6] ^= F1(key[7], Tr[i % 4][0], Tm[i][0]);
	key[5] ^= F2(key[6], Tr[i % 4][1], Tm[i][1]);
	key[4] ^= F3(key[5], Tr[i % 4][2], Tm[i][2]);
	key[3] ^= F1(key[4], Tr[i % 4][3], Tm[i][3]);
	key[2] ^= F2(key[3], Tr[i % 4][4], Tm[i][4]);
	key[1] ^= F3(key[2], Tr[i % 4][5], Tm[i][5]);
	key[0] ^= F1(key[1], Tr[i % 4][6], Tm[i][6]);
	key[7] ^= F2(key[0], Tr[i % 4][7], Tm[i][7]);
}

int __cast6_setkey(struct cast6_ctx *c, const u8 *in_key,
		   unsigned key_len, u32 *flags)
{
	int i;
	u32 key[8];
	__be32 p_key[8]; /* padded key */

	if (key_len % 4 != 0) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	memset(p_key, 0, 32);
	memcpy(p_key, in_key, key_len);

	key[0] = be32_to_cpu(p_key[0]);		/* A */
	key[1] = be32_to_cpu(p_key[1]);		/* B */
	key[2] = be32_to_cpu(p_key[2]);		/* C */
	key[3] = be32_to_cpu(p_key[3]);		/* D */
	key[4] = be32_to_cpu(p_key[4]);		/* E */
	key[5] = be32_to_cpu(p_key[5]);		/* F */
	key[6] = be32_to_cpu(p_key[6]);		/* G */
	key[7] = be32_to_cpu(p_key[7]);		/* H */

	for (i = 0; i < 12; i++) {
		W(key, 2 * i);
		W(key, 2 * i + 1);

		c->Kr[i][0] = key[0] & 0x1f;
		c->Kr[i][1] = key[2] & 0x1f;
		c->Kr[i][2] = key[4] & 0x1f;
		c->Kr[i][3] = key[6] & 0x1f;

		c->Km[i][0] = key[7];
		c->Km[i][1] = key[5];
		c->Km[i][2] = key[3];
		c->Km[i][3] = key[1];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(__cast6_setkey);

int cast6_setkey(struct crypto_tfm *tfm, const u8 *key, unsigned int keylen)
{
	return __cast6_setkey(crypto_tfm_ctx(tfm), key, keylen,
			      &tfm->crt_flags);
}
EXPORT_SYMBOL_GPL(cast6_setkey);

/*forward quad round*/
static inline void Q(u32 *block, u8 *Kr, u32 *Km)
{
	u32 I;
	block[2] ^= F1(block[3], Kr[0], Km[0]);
	block[1] ^= F2(block[2], Kr[1], Km[1]);
	block[0] ^= F3(block[1], Kr[2], Km[2]);
	block[3] ^= F1(block[0], Kr[3], Km[3]);
}

/*reverse quad round*/
static inline void QBAR(u32 *block, u8 *Kr, u32 *Km)
{
	u32 I;
	block[3] ^= F1(block[0], Kr[3], Km[3]);
	block[0] ^= F3(block[1], Kr[2], Km[2]);
	block[1] ^= F2(block[2], Kr[1], Km[1]);
	block[2] ^= F1(block[3], Kr[0], Km[0]);
}

void __cast6_encrypt(struct cast6_ctx *c, u8 *outbuf, const u8 *inbuf)
{
	const __be32 *src = (const __be32 *)inbuf;
	__be32 *dst = (__be32 *)outbuf;
	u32 block[4];
	u32 *Km;
	u8 *Kr;

	block[0] = be32_to_cpu(src[0]);
	block[1] = be32_to_cpu(src[1]);
	block[2] = be32_to_cpu(src[2]);
	block[3] = be32_to_cpu(src[3]);

	Km = c->Km[0]; Kr = c->Kr[0]; Q(block, Kr, Km);
	Km = c->Km[1]; Kr = c->Kr[1]; Q(block, Kr, Km);
	Km = c->Km[2]; Kr = c->Kr[2]; Q(block, Kr, Km);
	Km = c->Km[3]; Kr = c->Kr[3]; Q(block, Kr, Km);
	Km = c->Km[4]; Kr = c->Kr[4]; Q(block, Kr, Km);
	Km = c->Km[5]; Kr = c->Kr[5]; Q(block, Kr, Km);
	Km = c->Km[6]; Kr = c->Kr[6]; QBAR(block, Kr, Km);
	Km = c->Km[7]; Kr = c->Kr[7]; QBAR(block, Kr, Km);
	Km = c->Km[8]; Kr = c->Kr[8]; QBAR(block, Kr, Km);
	Km = c->Km[9]; Kr = c->Kr[9]; QBAR(block, Kr, Km);
	Km = c->Km[10]; Kr = c->Kr[10]; QBAR(block, Kr, Km);
	Km = c->Km[11]; Kr = c->Kr[11]; QBAR(block, Kr, Km);

	dst[0] = cpu_to_be32(block[0]);
	dst[1] = cpu_to_be32(block[1]);
	dst[2] = cpu_to_be32(block[2]);
	dst[3] = cpu_to_be32(block[3]);
}
EXPORT_SYMBOL_GPL(__cast6_encrypt);

static void cast6_encrypt(struct crypto_tfm *tfm, u8 *outbuf, const u8 *inbuf)
{
	__cast6_encrypt(crypto_tfm_ctx(tfm), outbuf, inbuf);
}

void __cast6_decrypt(struct cast6_ctx *c, u8 *outbuf, const u8 *inbuf)
{
	const __be32 *src = (const __be32 *)inbuf;
	__be32 *dst = (__be32 *)outbuf;
	u32 block[4];
	u32 *Km;
	u8 *Kr;

	block[0] = be32_to_cpu(src[0]);
	block[1] = be32_to_cpu(src[1]);
	block[2] = be32_to_cpu(src[2]);
	block[3] = be32_to_cpu(src[3]);

	Km = c->Km[11]; Kr = c->Kr[11]; Q(block, Kr, Km);
	Km = c->Km[10]; Kr = c->Kr[10]; Q(block, Kr, Km);
	Km = c->Km[9]; Kr = c->Kr[9]; Q(block, Kr, Km);
	Km = c->Km[8]; Kr = c->Kr[8]; Q(block, Kr, Km);
	Km = c->Km[7]; Kr = c->Kr[7]; Q(block, Kr, Km);
	Km = c->Km[6]; Kr = c->Kr[6]; Q(block, Kr, Km);
	Km = c->Km[5]; Kr = c->Kr[5]; QBAR(block, Kr, Km);
	Km = c->Km[4]; Kr = c->Kr[4]; QBAR(block, Kr, Km);
	Km = c->Km[3]; Kr = c->Kr[3]; QBAR(block, Kr, Km);
	Km = c->Km[2]; Kr = c->Kr[2]; QBAR(block, Kr, Km);
	Km = c->Km[1]; Kr = c->Kr[1]; QBAR(block, Kr, Km);
	Km = c->Km[0]; Kr = c->Kr[0]; QBAR(block, Kr, Km);

	dst[0] = cpu_to_be32(block[0]);
	dst[1] = cpu_to_be32(block[1]);
	dst[2] = cpu_to_be32(block[2]);
	dst[3] = cpu_to_be32(block[3]);
}
EXPORT_SYMBOL_GPL(__cast6_decrypt);

static void cast6_decrypt(struct crypto_tfm *tfm, u8 *outbuf, const u8 *inbuf)
{
	__cast6_decrypt(crypto_tfm_ctx(tfm), outbuf, inbuf);
}

static struct crypto_alg alg = {
	.cra_name = "cast6",
	.cra_driver_name = "cast6-generic",
	.cra_priority = 100,
	.cra_flags = CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize = CAST6_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct cast6_ctx),
	.cra_alignmask = 3,
	.cra_module = THIS_MODULE,
	.cra_u = {
		  .cipher = {
			     .cia_min_keysize = CAST6_MIN_KEY_SIZE,
			     .cia_max_keysize = CAST6_MAX_KEY_SIZE,
			     .cia_setkey = cast6_setkey,
			     .cia_encrypt = cast6_encrypt,
			     .cia_decrypt = cast6_decrypt}
		  }
};

static int __init cast6_mod_init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit cast6_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(cast6_mod_init);
module_exit(cast6_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cast6 Cipher Algorithm");
MODULE_ALIAS_CRYPTO("cast6");
