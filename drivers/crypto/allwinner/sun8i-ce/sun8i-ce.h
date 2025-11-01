/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sun8i-ce.h - hardware cryptographic offloader for
 * Allwinner H3/A64/H5/H2+/H6 SoC
 *
 * Copyright (C) 2016-2019 Corentin LABBE <clabbe.montjoie@gmail.com>
 */
#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/engine.h>
#include <crypto/skcipher.h>
#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/crypto.h>
#include <linux/hw_random.h>
#include <crypto/internal/hash.h>
#include <crypto/md5.h>
#include <crypto/rng.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>

/* CE Registers */
#define CE_TDQ	0x00
#define CE_CTR	0x04
#define CE_ICR	0x08
#define CE_ISR	0x0C
#define CE_TLR	0x10
#define CE_TSR	0x14
#define CE_ESR	0x18
#define CE_CSSGR	0x1C
#define CE_CDSGR	0x20
#define CE_CSAR	0x24
#define CE_CDAR	0x28
#define CE_TPR	0x2C

/* Used in struct ce_task */
/* ce_task common */
#define CE_ENCRYPTION		0
#define CE_DECRYPTION		BIT(8)

#define CE_COMM_INT		BIT(31)

/* ce_task symmetric */
#define CE_AES_128BITS 0
#define CE_AES_192BITS 1
#define CE_AES_256BITS 2

#define CE_OP_ECB	0
#define CE_OP_CBC	(1 << 8)

#define CE_ALG_AES		0
#define CE_ALG_DES		1
#define CE_ALG_3DES		2
#define CE_ALG_MD5              16
#define CE_ALG_SHA1             17
#define CE_ALG_SHA224           18
#define CE_ALG_SHA256           19
#define CE_ALG_SHA384           20
#define CE_ALG_SHA512           21
#define CE_ALG_TRNG		48
#define CE_ALG_PRNG		49
#define CE_ALG_TRNG_V2		0x1c
#define CE_ALG_PRNG_V2		0x1d

/* Used in ce_variant */
#define CE_ID_NOTSUPP		0xFF

#define CE_ID_CIPHER_AES	0
#define CE_ID_CIPHER_DES	1
#define CE_ID_CIPHER_DES3	2
#define CE_ID_CIPHER_MAX	3

#define CE_ID_HASH_MD5		0
#define CE_ID_HASH_SHA1		1
#define CE_ID_HASH_SHA224	2
#define CE_ID_HASH_SHA256	3
#define CE_ID_HASH_SHA384	4
#define CE_ID_HASH_SHA512	5
#define CE_ID_HASH_MAX		6

#define CE_ID_OP_ECB	0
#define CE_ID_OP_CBC	1
#define CE_ID_OP_MAX	2

/* Used in CE registers */
#define CE_ERR_ALGO_NOTSUP	BIT(0)
#define CE_ERR_DATALEN		BIT(1)
#define CE_ERR_KEYSRAM		BIT(2)
#define CE_ERR_ADDR_INVALID	BIT(5)
#define CE_ERR_KEYLADDER	BIT(6)

#define ESR_H3	0
#define ESR_A64	1
#define ESR_R40	2
#define ESR_H5	3
#define ESR_H6	4
#define ESR_D1	5

#define PRNG_DATA_SIZE (160 / 8)
#define PRNG_SEED_SIZE DIV_ROUND_UP(175, 8)
#define PRNG_LD BIT(17)

#define CE_DIE_ID_SHIFT	16
#define CE_DIE_ID_MASK	0x07

#define MAX_SG 8

#define CE_MAX_CLOCKS 4
#define CE_DMA_TIMEOUT_MS	3000

#define MAXFLOW 4

#define CE_MAX_HASH_DIGEST_SIZE		SHA512_DIGEST_SIZE
#define CE_MAX_HASH_BLOCK_SIZE		SHA512_BLOCK_SIZE

/*
 * struct ce_clock - Describe clocks used by sun8i-ce
 * @name:	Name of clock needed by this variant
 * @freq:	Frequency to set for each clock
 * @max_freq:	Maximum frequency for each clock (generally given by datasheet)
 */
struct ce_clock {
	const char *name;
	unsigned long freq;
	unsigned long max_freq;
};

/*
 * struct ce_variant - Describe CE capability for each variant hardware
 * @alg_cipher:	list of supported ciphers. for each CE_ID_ this will give the
 *              coresponding CE_ALG_XXX value
 * @alg_hash:	list of supported hashes. for each CE_ID_ this will give the
 *              corresponding CE_ALG_XXX value
 * @op_mode:	list of supported block modes
 * @cipher_t_dlen_in_bytes:	Does the request size for cipher is in
 *				bytes or words
 * @hash_t_dlen_in_bytes:	Does the request size for hash is in
 *				bits or words
 * @prng_t_dlen_in_bytes:	Does the request size for PRNG is in
 *				bytes or words
 * @trng_t_dlen_in_bytes:	Does the request size for TRNG is in
 *				bytes or words
 * @ce_clks:	list of clocks needed by this variant
 * @esr:	The type of error register
 * @prng:	The CE_ALG_XXX value for the PRNG
 * @trng:	The CE_ALG_XXX value for the TRNG
 */
struct ce_variant {
	char alg_cipher[CE_ID_CIPHER_MAX];
	char alg_hash[CE_ID_HASH_MAX];
	u32 op_mode[CE_ID_OP_MAX];
	bool cipher_t_dlen_in_bytes;
	bool hash_t_dlen_in_bits;
	bool prng_t_dlen_in_bytes;
	bool trng_t_dlen_in_bytes;
	bool needs_word_addresses;
	struct ce_clock ce_clks[CE_MAX_CLOCKS];
	int esr;
	unsigned char prng;
	unsigned char trng;
};

struct sginfo {
	__le32 addr;
	__le32 len;
} __packed;

/*
 * struct ce_task - CE Task descriptor
 * The structure of this descriptor could be found in the datasheet
 */
struct ce_task {
	__le32 t_id;
	__le32 t_common_ctl;
	__le32 t_sym_ctl;
	__le32 t_asym_ctl;
	__le32 t_key;
	__le32 t_iv;
	__le32 t_ctr;
	__le32 t_dlen;
	struct sginfo t_src[MAX_SG];
	struct sginfo t_dst[MAX_SG];
	__le32 next;
	__le32 reserved[3];
} __packed __aligned(8);

/*
 * struct sun8i_ce_flow - Information used by each flow
 * @engine:	ptr to the crypto_engine for this flow
 * @complete:	completion for the current task on this flow
 * @status:	set to 1 by interrupt if task is done
 * @t_phy:	Physical address of task
 * @tl:		pointer to the current ce_task for this flow
 * @stat_req:	number of request done by this flow
 */
struct sun8i_ce_flow {
	struct crypto_engine *engine;
	struct completion complete;
	int status;
	dma_addr_t t_phy;
	struct ce_task *tl;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	unsigned long stat_req;
#endif
};

/*
 * struct sun8i_ce_dev - main container for all this driver information
 * @base:	base address of CE
 * @ceclks:	clocks used by CE
 * @reset:	pointer to reset controller
 * @dev:	the platform device
 * @mlock:	Control access to device registers
 * @rnglock:	Control access to the RNG (dedicated channel 3)
 * @chanlist:	array of all flow
 * @flow:	flow to use in next request
 * @variant:	pointer to variant specific data
 * @dbgfs_dir:	Debugfs dentry for statistic directory
 * @dbgfs_stats: Debugfs dentry for statistic counters
 */
struct sun8i_ce_dev {
	void __iomem *base;
	struct clk *ceclks[CE_MAX_CLOCKS];
	struct reset_control *reset;
	struct device *dev;
	struct mutex mlock;
	struct mutex rnglock;
	struct sun8i_ce_flow *chanlist;
	atomic_t flow;
	const struct ce_variant *variant;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	struct dentry *dbgfs_dir;
	struct dentry *dbgfs_stats;
#endif
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_TRNG
	struct hwrng trng;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	unsigned long hwrng_stat_req;
	unsigned long hwrng_stat_bytes;
#endif
#endif
};

static inline u32 desc_addr_val(struct sun8i_ce_dev *dev, dma_addr_t addr)
{
	if (dev->variant->needs_word_addresses)
		return addr / 4;

	return addr;
}

static inline __le32 desc_addr_val_le32(struct sun8i_ce_dev *dev,
					dma_addr_t addr)
{
	return cpu_to_le32(desc_addr_val(dev, addr));
}

/*
 * struct sun8i_cipher_req_ctx - context for a skcipher request
 * @op_dir:		direction (encrypt vs decrypt) for this request
 * @flow:		the flow to use for this request
 * @nr_sgs:		The number of source SG (as given by dma_map_sg())
 * @nr_sgd:		The number of destination SG (as given by dma_map_sg())
 * @addr_iv:		The IV addr returned by dma_map_single, need to unmap later
 * @addr_key:		The key addr returned by dma_map_single, need to unmap later
 * @bounce_iv:		Current IV buffer
 * @backup_iv:		Next IV buffer
 * @fallback_req:	request struct for invoking the fallback skcipher TFM
 */
struct sun8i_cipher_req_ctx {
	u32 op_dir;
	int flow;
	int nr_sgs;
	int nr_sgd;
	dma_addr_t addr_iv;
	dma_addr_t addr_key;
	u8 bounce_iv[AES_BLOCK_SIZE] __aligned(sizeof(u32));
	u8 backup_iv[AES_BLOCK_SIZE];
	struct skcipher_request fallback_req;   // keep at the end
};

/*
 * struct sun8i_cipher_tfm_ctx - context for a skcipher TFM
 * @key:		pointer to key data
 * @keylen:		len of the key
 * @ce:			pointer to the private data of driver handling this TFM
 * @fallback_tfm:	pointer to the fallback TFM
 */
struct sun8i_cipher_tfm_ctx {
	u32 *key;
	u32 keylen;
	struct sun8i_ce_dev *ce;
	struct crypto_skcipher *fallback_tfm;
};

/*
 * struct sun8i_ce_hash_tfm_ctx - context for an ahash TFM
 * @ce:			pointer to the private data of driver handling this TFM
 * @fallback_tfm:	pointer to the fallback TFM
 */
struct sun8i_ce_hash_tfm_ctx {
	struct sun8i_ce_dev *ce;
	struct crypto_ahash *fallback_tfm;
};

/*
 * struct sun8i_ce_hash_reqctx - context for an ahash request
 * @fallback_req:	pre-allocated fallback request
 * @flow:	the flow to use for this request
 * @nr_sgs: number of entries in the source scatterlist
 * @result_len: result length in bytes
 * @pad_len: padding length in bytes
 * @addr_res: DMA address of the result buffer, returned by dma_map_single()
 * @addr_pad: DMA address of the padding buffer, returned by dma_map_single()
 * @result: per-request result buffer
 * @pad: per-request padding buffer (up to 2 blocks)
 */
struct sun8i_ce_hash_reqctx {
	int flow;
	int nr_sgs;
	size_t result_len;
	size_t pad_len;
	dma_addr_t addr_res;
	dma_addr_t addr_pad;
	u8 result[CE_MAX_HASH_DIGEST_SIZE] __aligned(CRYPTO_DMA_ALIGN);
	u8 pad[2 * CE_MAX_HASH_BLOCK_SIZE];
	struct ahash_request fallback_req; // keep at the end
};

/*
 * struct sun8i_ce_prng_ctx - context for PRNG TFM
 * @seed:	The seed to use
 * @slen:	The size of the seed
 */
struct sun8i_ce_rng_tfm_ctx {
	void *seed;
	unsigned int slen;
};

/*
 * struct sun8i_ce_alg_template - crypto_alg template
 * @type:		the CRYPTO_ALG_TYPE for this template
 * @ce_algo_id:		the CE_ID for this template
 * @ce_blockmode:	the type of block operation CE_ID
 * @ce:			pointer to the sun8i_ce_dev structure associated with
 *			this template
 * @alg:		one of sub struct must be used
 * @stat_req:		number of request done on this template
 * @stat_fb:		number of request which has fallbacked
 * @stat_bytes:		total data size done by this template
 */
struct sun8i_ce_alg_template {
	u32 type;
	u32 ce_algo_id;
	u32 ce_blockmode;
	struct sun8i_ce_dev *ce;
	union {
		struct skcipher_engine_alg skcipher;
		struct ahash_engine_alg hash;
		struct rng_alg rng;
	} alg;
	unsigned long stat_req;
	unsigned long stat_fb;
	unsigned long stat_bytes;
	unsigned long stat_fb_maxsg;
	unsigned long stat_fb_leniv;
	unsigned long stat_fb_len0;
	unsigned long stat_fb_mod16;
	unsigned long stat_fb_srcali;
	unsigned long stat_fb_srclen;
	unsigned long stat_fb_dstali;
	unsigned long stat_fb_dstlen;
	char fbname[CRYPTO_MAX_ALG_NAME];
};

int sun8i_ce_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int keylen);
int sun8i_ce_des3_setkey(struct crypto_skcipher *tfm, const u8 *key,
			 unsigned int keylen);
int sun8i_ce_cipher_init(struct crypto_tfm *tfm);
void sun8i_ce_cipher_exit(struct crypto_tfm *tfm);
int sun8i_ce_cipher_do_one(struct crypto_engine *engine, void *areq);
int sun8i_ce_skdecrypt(struct skcipher_request *areq);
int sun8i_ce_skencrypt(struct skcipher_request *areq);

int sun8i_ce_get_engine_number(struct sun8i_ce_dev *ce);

int sun8i_ce_run_task(struct sun8i_ce_dev *ce, int flow, const char *name);

int sun8i_ce_hash_init_tfm(struct crypto_ahash *tfm);
void sun8i_ce_hash_exit_tfm(struct crypto_ahash *tfm);
int sun8i_ce_hash_init(struct ahash_request *areq);
int sun8i_ce_hash_export(struct ahash_request *areq, void *out);
int sun8i_ce_hash_import(struct ahash_request *areq, const void *in);
int sun8i_ce_hash_final(struct ahash_request *areq);
int sun8i_ce_hash_update(struct ahash_request *areq);
int sun8i_ce_hash_finup(struct ahash_request *areq);
int sun8i_ce_hash_digest(struct ahash_request *areq);
int sun8i_ce_hash_run(struct crypto_engine *engine, void *breq);

int sun8i_ce_prng_generate(struct crypto_rng *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int dlen);
int sun8i_ce_prng_seed(struct crypto_rng *tfm, const u8 *seed, unsigned int slen);
void sun8i_ce_prng_exit(struct crypto_tfm *tfm);
int sun8i_ce_prng_init(struct crypto_tfm *tfm);

int sun8i_ce_hwrng_register(struct sun8i_ce_dev *ce);
void sun8i_ce_hwrng_unregister(struct sun8i_ce_dev *ce);
