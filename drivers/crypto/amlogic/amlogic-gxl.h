/* SPDX-License-Identifier: GPL-2.0 */
/*
 * amlogic.h - hardware cryptographic offloader for Amlogic SoC
 *
 * Copyright (C) 2018-2019 Corentin LABBE <clabbe@baylibre.com>
 */
#include <crypto/aes.h>
#include <crypto/engine.h>
#include <crypto/skcipher.h>
#include <linux/debugfs.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>

#define MODE_KEY 1
#define MODE_AES_128 0x8
#define MODE_AES_192 0x9
#define MODE_AES_256 0xa

#define MESON_DECRYPT 0
#define MESON_ENCRYPT 1

#define MESON_OPMODE_ECB 0
#define MESON_OPMODE_CBC 1

#define MAXFLOW 2

#define MAXDESC 64

#define DESC_LAST BIT(18)
#define DESC_ENCRYPTION BIT(28)
#define DESC_OWN BIT(31)

/*
 * struct meson_desc - Descriptor for DMA operations
 * Note that without datasheet, some are unknown
 * @t_status:	Descriptor of the cipher operation (see description below)
 * @t_src:	Physical address of data to read
 * @t_dst:	Physical address of data to write
 * t_status is segmented like this:
 * @len:	0-16	length of data to operate
 * @irq:	17	Ignored by hardware
 * @eoc:	18	End means the descriptor is the last
 * @loop:	19	Unknown
 * @mode:	20-23	Type of algorithm (AES, SHA)
 * @begin:	24	Unknown
 * @end:	25	Unknown
 * @op_mode:	26-27	Blockmode (CBC, ECB)
 * @enc:	28	0 means decryption, 1 is for encryption
 * @block:	29	Unknown
 * @error:	30	Unknown
 * @owner:	31	owner of the descriptor, 1 own by HW
 */
struct meson_desc {
	__le32 t_status;
	__le32 t_src;
	__le32 t_dst;
};

/*
 * struct meson_flow - Information used by each flow
 * @engine:	ptr to the crypto_engine for this flow
 * @keylen:	keylen for this flow operation
 * @complete:	completion for the current task on this flow
 * @status:	set to 1 by interrupt if task is done
 * @t_phy:	Physical address of task
 * @tl:		pointer to the current ce_task for this flow
 * @stat_req:	number of request done by this flow
 */
struct meson_flow {
	struct crypto_engine *engine;
	struct completion complete;
	int status;
	unsigned int keylen;
	dma_addr_t t_phy;
	struct meson_desc *tl;
#ifdef CONFIG_CRYPTO_DEV_AMLOGIC_GXL_DEBUG
	unsigned long stat_req;
#endif
};

/*
 * struct meson_dev - main container for all this driver information
 * @base:	base address of amlogic-crypto
 * @busclk:	bus clock for amlogic-crypto
 * @dev:	the platform device
 * @chanlist:	array of all flow
 * @flow:	flow to use in next request
 * @irqs:	IRQ numbers for amlogic-crypto
 * @dbgfs_dir:	Debugfs dentry for statistic directory
 * @dbgfs_stats: Debugfs dentry for statistic counters
 */
struct meson_dev {
	void __iomem *base;
	struct clk *busclk;
	struct device *dev;
	struct meson_flow *chanlist;
	atomic_t flow;
	int *irqs;
#ifdef CONFIG_CRYPTO_DEV_AMLOGIC_GXL_DEBUG
	struct dentry *dbgfs_dir;
#endif
};

/*
 * struct meson_cipher_req_ctx - context for a skcipher request
 * @op_dir:	direction (encrypt vs decrypt) for this request
 * @flow:	the flow to use for this request
 */
struct meson_cipher_req_ctx {
	u32 op_dir;
	int flow;
};

/*
 * struct meson_cipher_tfm_ctx - context for a skcipher TFM
 * @enginectx:		crypto_engine used by this TFM
 * @key:		pointer to key data
 * @keylen:		len of the key
 * @keymode:		The keymode(type and size of key) associated with this TFM
 * @mc:			pointer to the private data of driver handling this TFM
 * @fallback_tfm:	pointer to the fallback TFM
 */
struct meson_cipher_tfm_ctx {
	struct crypto_engine_ctx enginectx;
	u32 *key;
	u32 keylen;
	u32 keymode;
	struct meson_dev *mc;
	struct crypto_sync_skcipher *fallback_tfm;
};

/*
 * struct meson_alg_template - crypto_alg template
 * @type:		the CRYPTO_ALG_TYPE for this template
 * @blockmode:		the type of block operation
 * @mc:			pointer to the meson_dev structure associated with this template
 * @alg:		one of sub struct must be used
 * @stat_req:		number of request done on this template
 * @stat_fb:		total of all data len done on this template
 */
struct meson_alg_template {
	u32 type;
	u32 blockmode;
	union {
		struct skcipher_alg skcipher;
	} alg;
	struct meson_dev *mc;
#ifdef CONFIG_CRYPTO_DEV_AMLOGIC_GXL_DEBUG
	unsigned long stat_req;
	unsigned long stat_fb;
#endif
};

int meson_enqueue(struct crypto_async_request *areq, u32 type);

int meson_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
		     unsigned int keylen);
int meson_cipher_init(struct crypto_tfm *tfm);
void meson_cipher_exit(struct crypto_tfm *tfm);
int meson_skdecrypt(struct skcipher_request *areq);
int meson_skencrypt(struct skcipher_request *areq);
