/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sun8i-ss.h - hardware cryptographic offloader for
 * Allwinner A80/A83T SoC
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

#define SS_START	1

#define SS_ENCRYPTION		0
#define SS_DECRYPTION		BIT(6)

#define SS_ALG_AES		0
#define SS_ALG_DES		(1 << 2)
#define SS_ALG_3DES		(2 << 2)

#define SS_CTL_REG		0x00
#define SS_INT_CTL_REG		0x04
#define SS_INT_STA_REG		0x08
#define SS_KEY_ADR_REG		0x10
#define SS_IV_ADR_REG		0x18
#define SS_SRC_ADR_REG		0x20
#define SS_DST_ADR_REG		0x28
#define SS_LEN_ADR_REG		0x30

#define SS_ID_NOTSUPP		0xFF

#define SS_ID_CIPHER_AES	0
#define SS_ID_CIPHER_DES	1
#define SS_ID_CIPHER_DES3	2
#define SS_ID_CIPHER_MAX	3

#define SS_ID_OP_ECB	0
#define SS_ID_OP_CBC	1
#define SS_ID_OP_MAX	2

#define SS_AES_128BITS 0
#define SS_AES_192BITS 1
#define SS_AES_256BITS 2

#define SS_OP_ECB	0
#define SS_OP_CBC	(1 << 13)

#define SS_FLOW0	BIT(30)
#define SS_FLOW1	BIT(31)

#define MAX_SG 8

#define MAXFLOW 2

#define SS_MAX_CLOCKS 2

#define SS_DIE_ID_SHIFT	20
#define SS_DIE_ID_MASK	0x07

/*
 * struct ss_clock - Describe clocks used by sun8i-ss
 * @name:       Name of clock needed by this variant
 * @freq:       Frequency to set for each clock
 * @max_freq:   Maximum frequency for each clock
 */
struct ss_clock {
	const char *name;
	unsigned long freq;
	unsigned long max_freq;
};

/*
 * struct ss_variant - Describe SS capability for each variant hardware
 * @alg_cipher:	list of supported ciphers. for each SS_ID_ this will give the
 *              coresponding SS_ALG_XXX value
 * @op_mode:	list of supported block modes
 * @ss_clks!	list of clock needed by this variant
 */
struct ss_variant {
	char alg_cipher[SS_ID_CIPHER_MAX];
	u32 op_mode[SS_ID_OP_MAX];
	struct ss_clock ss_clks[SS_MAX_CLOCKS];
};

struct sginfo {
	u32 addr;
	u32 len;
};

/*
 * struct sun8i_ss_flow - Information used by each flow
 * @engine:	ptr to the crypto_engine for this flow
 * @complete:	completion for the current task on this flow
 * @status:	set to 1 by interrupt if task is done
 * @stat_req:	number of request done by this flow
 */
struct sun8i_ss_flow {
	struct crypto_engine *engine;
	struct completion complete;
	int status;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	unsigned long stat_req;
#endif
};

/*
 * struct sun8i_ss_dev - main container for all this driver information
 * @base:	base address of SS
 * @ssclks:	clocks used by SS
 * @reset:	pointer to reset controller
 * @dev:	the platform device
 * @mlock:	Control access to device registers
 * @flows:	array of all flow
 * @flow:	flow to use in next request
 * @variant:	pointer to variant specific data
 * @dbgfs_dir:	Debugfs dentry for statistic directory
 * @dbgfs_stats: Debugfs dentry for statistic counters
 */
struct sun8i_ss_dev {
	void __iomem *base;
	struct clk *ssclks[SS_MAX_CLOCKS];
	struct reset_control *reset;
	struct device *dev;
	struct mutex mlock;
	struct sun8i_ss_flow *flows;
	atomic_t flow;
	const struct ss_variant *variant;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	struct dentry *dbgfs_dir;
	struct dentry *dbgfs_stats;
#endif
};

/*
 * struct sun8i_cipher_req_ctx - context for a skcipher request
 * @t_src:		list of mapped SGs with their size
 * @t_dst:		list of mapped SGs with their size
 * @p_key:		DMA address of the key
 * @p_iv:		DMA address of the IV
 * @method:		current algorithm for this request
 * @op_mode:		op_mode for this request
 * @op_dir:		direction (encrypt vs decrypt) for this request
 * @flow:		the flow to use for this request
 * @ivlen:		size of biv
 * @keylen:		keylen for this request
 * @biv:		buffer which contain the IV
 * @fallback_req:	request struct for invoking the fallback skcipher TFM
 */
struct sun8i_cipher_req_ctx {
	struct sginfo t_src[MAX_SG];
	struct sginfo t_dst[MAX_SG];
	u32 p_key;
	u32 p_iv;
	u32 method;
	u32 op_mode;
	u32 op_dir;
	int flow;
	unsigned int ivlen;
	unsigned int keylen;
	void *biv;
	struct skcipher_request fallback_req;   // keep at the end
};

/*
 * struct sun8i_cipher_tfm_ctx - context for a skcipher TFM
 * @enginectx:		crypto_engine used by this TFM
 * @key:		pointer to key data
 * @keylen:		len of the key
 * @ss:			pointer to the private data of driver handling this TFM
 * @fallback_tfm:	pointer to the fallback TFM
 */
struct sun8i_cipher_tfm_ctx {
	struct crypto_engine_ctx enginectx;
	u32 *key;
	u32 keylen;
	struct sun8i_ss_dev *ss;
	struct crypto_skcipher *fallback_tfm;
};

/*
 * struct sun8i_ss_alg_template - crypto_alg template
 * @type:		the CRYPTO_ALG_TYPE for this template
 * @ss_algo_id:		the SS_ID for this template
 * @ss_blockmode:	the type of block operation SS_ID
 * @ss:			pointer to the sun8i_ss_dev structure associated with
 *			this template
 * @alg:		one of sub struct must be used
 * @stat_req:		number of request done on this template
 * @stat_fb:		number of request which has fallbacked
 */
struct sun8i_ss_alg_template {
	u32 type;
	u32 ss_algo_id;
	u32 ss_blockmode;
	struct sun8i_ss_dev *ss;
	union {
		struct skcipher_alg skcipher;
	} alg;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_SS_DEBUG
	unsigned long stat_req;
	unsigned long stat_fb;
#endif
};

int sun8i_ss_enqueue(struct crypto_async_request *areq, u32 type);

int sun8i_ss_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int keylen);
int sun8i_ss_des3_setkey(struct crypto_skcipher *tfm, const u8 *key,
			 unsigned int keylen);
int sun8i_ss_cipher_init(struct crypto_tfm *tfm);
void sun8i_ss_cipher_exit(struct crypto_tfm *tfm);
int sun8i_ss_skdecrypt(struct skcipher_request *areq);
int sun8i_ss_skencrypt(struct skcipher_request *areq);

int sun8i_ss_get_engine_number(struct sun8i_ss_dev *ss);

int sun8i_ss_run_task(struct sun8i_ss_dev *ss, struct sun8i_cipher_req_ctx *rctx, const char *name);
