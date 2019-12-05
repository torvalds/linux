/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sun4i-ss.h - hardware cryptographic accelerator for Allwinner A20 SoC
 *
 * Copyright (C) 2013-2015 Corentin LABBE <clabbe.montjoie@gmail.com>
 *
 * Support AES cipher with 128,192,256 bits keysize.
 * Support MD5 and SHA1 hash algorithms.
 * Support DES and 3DES
 *
 * You could find the datasheet in Documentation/arm/sunxi.rst
 */

#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <crypto/scatterwalk.h>
#include <linux/scatterlist.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <crypto/md5.h>
#include <crypto/skcipher.h>
#include <crypto/sha.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/aes.h>
#include <crypto/internal/des.h>
#include <crypto/internal/rng.h>
#include <crypto/rng.h>

#define SS_CTL            0x00
#define SS_KEY0           0x04
#define SS_KEY1           0x08
#define SS_KEY2           0x0C
#define SS_KEY3           0x10
#define SS_KEY4           0x14
#define SS_KEY5           0x18
#define SS_KEY6           0x1C
#define SS_KEY7           0x20

#define SS_IV0            0x24
#define SS_IV1            0x28
#define SS_IV2            0x2C
#define SS_IV3            0x30

#define SS_FCSR           0x44

#define SS_MD0            0x4C
#define SS_MD1            0x50
#define SS_MD2            0x54
#define SS_MD3            0x58
#define SS_MD4            0x5C

#define SS_RXFIFO         0x200
#define SS_TXFIFO         0x204

/* SS_CTL configuration values */

/* PRNG generator mode - bit 15 */
#define SS_PRNG_ONESHOT		(0 << 15)
#define SS_PRNG_CONTINUE	(1 << 15)

/* IV mode for hash */
#define SS_IV_ARBITRARY		(1 << 14)

/* SS operation mode - bits 12-13 */
#define SS_ECB			(0 << 12)
#define SS_CBC			(1 << 12)
#define SS_CTS			(3 << 12)

/* Counter width for CNT mode - bits 10-11 */
#define SS_CNT_16BITS		(0 << 10)
#define SS_CNT_32BITS		(1 << 10)
#define SS_CNT_64BITS		(2 << 10)

/* Key size for AES - bits 8-9 */
#define SS_AES_128BITS		(0 << 8)
#define SS_AES_192BITS		(1 << 8)
#define SS_AES_256BITS		(2 << 8)

/* Operation direction - bit 7 */
#define SS_ENCRYPTION		(0 << 7)
#define SS_DECRYPTION		(1 << 7)

/* SS Method - bits 4-6 */
#define SS_OP_AES		(0 << 4)
#define SS_OP_DES		(1 << 4)
#define SS_OP_3DES		(2 << 4)
#define SS_OP_SHA1		(3 << 4)
#define SS_OP_MD5		(4 << 4)
#define SS_OP_PRNG		(5 << 4)

/* Data end bit - bit 2 */
#define SS_DATA_END		(1 << 2)

/* PRNG start bit - bit 1 */
#define SS_PRNG_START		(1 << 1)

/* SS Enable bit - bit 0 */
#define SS_DISABLED		(0 << 0)
#define SS_ENABLED		(1 << 0)

/* SS_FCSR configuration values */
/* RX FIFO status - bit 30 */
#define SS_RXFIFO_FREE		(1 << 30)

/* RX FIFO empty spaces - bits 24-29 */
#define SS_RXFIFO_SPACES(val)	(((val) >> 24) & 0x3f)

/* TX FIFO status - bit 22 */
#define SS_TXFIFO_AVAILABLE	(1 << 22)

/* TX FIFO available spaces - bits 16-21 */
#define SS_TXFIFO_SPACES(val)	(((val) >> 16) & 0x3f)

#define SS_RX_MAX	32
#define SS_RX_DEFAULT	SS_RX_MAX
#define SS_TX_MAX	33

#define SS_RXFIFO_EMP_INT_PENDING	(1 << 10)
#define SS_TXFIFO_AVA_INT_PENDING	(1 << 8)
#define SS_RXFIFO_EMP_INT_ENABLE	(1 << 2)
#define SS_TXFIFO_AVA_INT_ENABLE	(1 << 0)

#define SS_SEED_LEN 192
#define SS_DATA_LEN 160

struct sun4i_ss_ctx {
	void __iomem *base;
	int irq;
	struct clk *busclk;
	struct clk *ssclk;
	struct reset_control *reset;
	struct device *dev;
	struct resource *res;
	spinlock_t slock; /* control the use of the device */
#ifdef CONFIG_CRYPTO_DEV_SUN4I_SS_PRNG
	u32 seed[SS_SEED_LEN / BITS_PER_LONG];
#endif
};

struct sun4i_ss_alg_template {
	u32 type;
	u32 mode;
	union {
		struct skcipher_alg crypto;
		struct ahash_alg hash;
		struct rng_alg rng;
	} alg;
	struct sun4i_ss_ctx *ss;
};

struct sun4i_tfm_ctx {
	u32 key[AES_MAX_KEY_SIZE / 4];/* divided by sizeof(u32) */
	u32 keylen;
	u32 keymode;
	struct sun4i_ss_ctx *ss;
	struct crypto_sync_skcipher *fallback_tfm;
};

struct sun4i_cipher_req_ctx {
	u32 mode;
};

struct sun4i_req_ctx {
	u32 mode;
	u64 byte_count; /* number of bytes "uploaded" to the device */
	u32 hash[5]; /* for storing SS_IVx register */
	char buf[64];
	unsigned int len;
	int flags;
};

int sun4i_hash_crainit(struct crypto_tfm *tfm);
void sun4i_hash_craexit(struct crypto_tfm *tfm);
int sun4i_hash_init(struct ahash_request *areq);
int sun4i_hash_update(struct ahash_request *areq);
int sun4i_hash_final(struct ahash_request *areq);
int sun4i_hash_finup(struct ahash_request *areq);
int sun4i_hash_digest(struct ahash_request *areq);
int sun4i_hash_export_md5(struct ahash_request *areq, void *out);
int sun4i_hash_import_md5(struct ahash_request *areq, const void *in);
int sun4i_hash_export_sha1(struct ahash_request *areq, void *out);
int sun4i_hash_import_sha1(struct ahash_request *areq, const void *in);

int sun4i_ss_cbc_aes_encrypt(struct skcipher_request *areq);
int sun4i_ss_cbc_aes_decrypt(struct skcipher_request *areq);
int sun4i_ss_ecb_aes_encrypt(struct skcipher_request *areq);
int sun4i_ss_ecb_aes_decrypt(struct skcipher_request *areq);

int sun4i_ss_cbc_des_encrypt(struct skcipher_request *areq);
int sun4i_ss_cbc_des_decrypt(struct skcipher_request *areq);
int sun4i_ss_ecb_des_encrypt(struct skcipher_request *areq);
int sun4i_ss_ecb_des_decrypt(struct skcipher_request *areq);

int sun4i_ss_cbc_des3_encrypt(struct skcipher_request *areq);
int sun4i_ss_cbc_des3_decrypt(struct skcipher_request *areq);
int sun4i_ss_ecb_des3_encrypt(struct skcipher_request *areq);
int sun4i_ss_ecb_des3_decrypt(struct skcipher_request *areq);

int sun4i_ss_cipher_init(struct crypto_tfm *tfm);
void sun4i_ss_cipher_exit(struct crypto_tfm *tfm);
int sun4i_ss_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int keylen);
int sun4i_ss_des_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int keylen);
int sun4i_ss_des3_setkey(struct crypto_skcipher *tfm, const u8 *key,
			 unsigned int keylen);
int sun4i_ss_prng_generate(struct crypto_rng *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int dlen);
int sun4i_ss_prng_seed(struct crypto_rng *tfm, const u8 *seed, unsigned int slen);
