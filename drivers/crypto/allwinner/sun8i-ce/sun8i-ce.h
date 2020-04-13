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

/* Used in ce_variant */
#define CE_ID_NOTSUPP		0xFF

#define CE_ID_CIPHER_AES	0
#define CE_ID_CIPHER_DES	1
#define CE_ID_CIPHER_DES3	2
#define CE_ID_CIPHER_MAX	3

#define CE_ID_OP_ECB	0
#define CE_ID_OP_CBC	1
#define CE_ID_OP_MAX	2

/* Used in CE registers */
#define CE_ERR_ALGO_NOTSUP	BIT(0)
#define CE_ERR_DATALEN		BIT(1)
#define CE_ERR_KEYSRAM		BIT(2)
#define CE_ERR_ADDR_INVALID	BIT(5)
#define CE_ERR_KEYLADDER	BIT(6)

#define CE_DIE_ID_SHIFT	16
#define CE_DIE_ID_MASK	0x07

#define MAX_SG 8

#define CE_MAX_CLOCKS 3

#define MAXFLOW 4

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
 * @op_mode:	list of supported block modes
 * @has_t_dlen_in_bytes:	Does the request size for cipher is in
 *				bytes or words
 * @ce_clks:	list of clocks needed by this variant
 */
struct ce_variant {
	char alg_cipher[CE_ID_CIPHER_MAX];
	u32 op_mode[CE_ID_OP_MAX];
	bool has_t_dlen_in_bytes;
	struct ce_clock ce_clks[CE_MAX_CLOCKS];
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
 * @bounce_iv:	buffer which contain the IV
 * @ivlen:	size of bounce_iv
 * @complete:	completion for the current task on this flow
 * @status:	set to 1 by interrupt if task is done
 * @t_phy:	Physical address of task
 * @tl:		pointer to the current ce_task for this flow
 * @stat_req:	number of request done by this flow
 */
struct sun8i_ce_flow {
	struct crypto_engine *engine;
	void *bounce_iv;
	unsigned int ivlen;
	struct completion complete;
	int status;
	dma_addr_t t_phy;
	int timeout;
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
	struct sun8i_ce_flow *chanlist;
	atomic_t flow;
	const struct ce_variant *variant;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	struct dentry *dbgfs_dir;
	struct dentry *dbgfs_stats;
#endif
};

/*
 * struct sun8i_cipher_req_ctx - context for a skcipher request
 * @op_dir:	direction (encrypt vs decrypt) for this request
 * @flow:	the flow to use for this request
 */
struct sun8i_cipher_req_ctx {
	u32 op_dir;
	int flow;
};

/*
 * struct sun8i_cipher_tfm_ctx - context for a skcipher TFM
 * @enginectx:		crypto_engine used by this TFM
 * @key:		pointer to key data
 * @keylen:		len of the key
 * @ce:			pointer to the private data of driver handling this TFM
 * @fallback_tfm:	pointer to the fallback TFM
 */
struct sun8i_cipher_tfm_ctx {
	struct crypto_engine_ctx enginectx;
	u32 *key;
	u32 keylen;
	struct sun8i_ce_dev *ce;
	struct crypto_sync_skcipher *fallback_tfm;
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
 */
struct sun8i_ce_alg_template {
	u32 type;
	u32 ce_algo_id;
	u32 ce_blockmode;
	struct sun8i_ce_dev *ce;
	union {
		struct skcipher_alg skcipher;
	} alg;
#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	unsigned long stat_req;
	unsigned long stat_fb;
#endif
};

int sun8i_ce_enqueue(struct crypto_async_request *areq, u32 type);

int sun8i_ce_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int keylen);
int sun8i_ce_des3_setkey(struct crypto_skcipher *tfm, const u8 *key,
			 unsigned int keylen);
int sun8i_ce_cipher_init(struct crypto_tfm *tfm);
void sun8i_ce_cipher_exit(struct crypto_tfm *tfm);
int sun8i_ce_skdecrypt(struct skcipher_request *areq);
int sun8i_ce_skencrypt(struct skcipher_request *areq);

int sun8i_ce_get_engine_number(struct sun8i_ce_dev *ce);

int sun8i_ce_run_task(struct sun8i_ce_dev *ce, int flow, const char *name);
