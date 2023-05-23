/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __ASPEED_HACE_H__
#define __ASPEED_HACE_H__

#include <crypto/aes.h>
#include <crypto/engine.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <linux/bits.h>
#include <linux/compiler_attributes.h>
#include <linux/interrupt.h>
#include <linux/types.h>

/*****************************
 *                           *
 * HACE register definitions *
 *                           *
 * ***************************/
#define ASPEED_HACE_SRC			0x00	/* Crypto Data Source Base Address Register */
#define ASPEED_HACE_DEST		0x04	/* Crypto Data Destination Base Address Register */
#define ASPEED_HACE_CONTEXT		0x08	/* Crypto Context Buffer Base Address Register */
#define ASPEED_HACE_DATA_LEN		0x0C	/* Crypto Data Length Register */
#define ASPEED_HACE_CMD			0x10	/* Crypto Engine Command Register */

/* G5 */
#define ASPEED_HACE_TAG			0x18	/* HACE Tag Register */
/* G6 */
#define ASPEED_HACE_GCM_ADD_LEN		0x14	/* Crypto AES-GCM Additional Data Length Register */
#define ASPEED_HACE_GCM_TAG_BASE_ADDR	0x18	/* Crypto AES-GCM Tag Write Buff Base Address Reg */

#define ASPEED_HACE_STS			0x1C	/* HACE Status Register */

#define ASPEED_HACE_HASH_SRC		0x20	/* Hash Data Source Base Address Register */
#define ASPEED_HACE_HASH_DIGEST_BUFF	0x24	/* Hash Digest Write Buffer Base Address Register */
#define ASPEED_HACE_HASH_KEY_BUFF	0x28	/* Hash HMAC Key Buffer Base Address Register */
#define ASPEED_HACE_HASH_DATA_LEN	0x2C	/* Hash Data Length Register */
#define ASPEED_HACE_HASH_CMD		0x30	/* Hash Engine Command Register */

/* crypto cmd */
#define  HACE_CMD_SINGLE_DES		0
#define  HACE_CMD_TRIPLE_DES		BIT(17)
#define  HACE_CMD_AES_SELECT		0
#define  HACE_CMD_DES_SELECT		BIT(16)
#define  HACE_CMD_ISR_EN		BIT(12)
#define  HACE_CMD_CONTEXT_SAVE_ENABLE	(0)
#define  HACE_CMD_CONTEXT_SAVE_DISABLE	BIT(9)
#define  HACE_CMD_AES			(0)
#define  HACE_CMD_DES			(0)
#define  HACE_CMD_RC4			BIT(8)
#define  HACE_CMD_DECRYPT		(0)
#define  HACE_CMD_ENCRYPT		BIT(7)

#define  HACE_CMD_ECB			(0x0 << 4)
#define  HACE_CMD_CBC			(0x1 << 4)
#define  HACE_CMD_CFB			(0x2 << 4)
#define  HACE_CMD_OFB			(0x3 << 4)
#define  HACE_CMD_CTR			(0x4 << 4)
#define  HACE_CMD_OP_MODE_MASK		(0x7 << 4)

#define  HACE_CMD_AES128		(0x0 << 2)
#define  HACE_CMD_AES192		(0x1 << 2)
#define  HACE_CMD_AES256		(0x2 << 2)
#define  HACE_CMD_OP_CASCADE		(0x3)
#define  HACE_CMD_OP_INDEPENDENT	(0x1)

/* G5 */
#define  HACE_CMD_RI_WO_DATA_ENABLE	(0)
#define  HACE_CMD_RI_WO_DATA_DISABLE	BIT(11)
#define  HACE_CMD_CONTEXT_LOAD_ENABLE	(0)
#define  HACE_CMD_CONTEXT_LOAD_DISABLE	BIT(10)
/* G6 */
#define  HACE_CMD_AES_KEY_FROM_OTP	BIT(24)
#define  HACE_CMD_GHASH_TAG_XOR_EN	BIT(23)
#define  HACE_CMD_GHASH_PAD_LEN_INV	BIT(22)
#define  HACE_CMD_GCM_TAG_ADDR_SEL	BIT(21)
#define  HACE_CMD_MBUS_REQ_SYNC_EN	BIT(20)
#define  HACE_CMD_DES_SG_CTRL		BIT(19)
#define  HACE_CMD_SRC_SG_CTRL		BIT(18)
#define  HACE_CMD_CTR_IV_AES_96		(0x1 << 14)
#define  HACE_CMD_CTR_IV_DES_32		(0x1 << 14)
#define  HACE_CMD_CTR_IV_AES_64		(0x2 << 14)
#define  HACE_CMD_CTR_IV_AES_32		(0x3 << 14)
#define  HACE_CMD_AES_KEY_HW_EXP	BIT(13)
#define  HACE_CMD_GCM			(0x5 << 4)

/* interrupt status reg */
#define  HACE_CRYPTO_ISR		BIT(12)
#define  HACE_HASH_ISR			BIT(9)
#define  HACE_HASH_BUSY			BIT(0)

/* hash cmd reg */
#define  HASH_CMD_MBUS_REQ_SYNC_EN	BIT(20)
#define  HASH_CMD_HASH_SRC_SG_CTRL	BIT(18)
#define  HASH_CMD_SHA512_224		(0x3 << 10)
#define  HASH_CMD_SHA512_256		(0x2 << 10)
#define  HASH_CMD_SHA384		(0x1 << 10)
#define  HASH_CMD_SHA512		(0)
#define  HASH_CMD_INT_ENABLE		BIT(9)
#define  HASH_CMD_HMAC			(0x1 << 7)
#define  HASH_CMD_ACC_MODE		(0x2 << 7)
#define  HASH_CMD_HMAC_KEY		(0x3 << 7)
#define  HASH_CMD_SHA1			(0x2 << 4)
#define  HASH_CMD_SHA224		(0x4 << 4)
#define  HASH_CMD_SHA256		(0x5 << 4)
#define  HASH_CMD_SHA512_SER		(0x6 << 4)
#define  HASH_CMD_SHA_SWAP		(0x2 << 2)

#define HASH_SG_LAST_LIST		BIT(31)

#define CRYPTO_FLAGS_BUSY		BIT(1)

#define SHA_OP_UPDATE			1
#define SHA_OP_FINAL			2

#define SHA_FLAGS_SHA1			BIT(0)
#define SHA_FLAGS_SHA224		BIT(1)
#define SHA_FLAGS_SHA256		BIT(2)
#define SHA_FLAGS_SHA384		BIT(3)
#define SHA_FLAGS_SHA512		BIT(4)
#define SHA_FLAGS_SHA512_224		BIT(5)
#define SHA_FLAGS_SHA512_256		BIT(6)
#define SHA_FLAGS_HMAC			BIT(8)
#define SHA_FLAGS_FINUP			BIT(9)
#define SHA_FLAGS_MASK			(0xff)

#define ASPEED_CRYPTO_SRC_DMA_BUF_LEN	0xa000
#define ASPEED_CRYPTO_DST_DMA_BUF_LEN	0xa000
#define ASPEED_CRYPTO_GCM_TAG_OFFSET	0x9ff0
#define ASPEED_HASH_SRC_DMA_BUF_LEN	0xa000
#define ASPEED_HASH_QUEUE_LENGTH	50

#define HACE_CMD_IV_REQUIRE		(HACE_CMD_CBC | HACE_CMD_CFB | \
					 HACE_CMD_OFB | HACE_CMD_CTR)

struct aspeed_hace_dev;
struct scatterlist;

typedef int (*aspeed_hace_fn_t)(struct aspeed_hace_dev *);

struct aspeed_sg_list {
	__le32 len;
	__le32 phy_addr;
};

struct aspeed_engine_hash {
	struct tasklet_struct		done_task;
	unsigned long			flags;
	struct ahash_request		*req;

	/* input buffer */
	void				*ahash_src_addr;
	dma_addr_t			ahash_src_dma_addr;

	dma_addr_t			src_dma;
	dma_addr_t			digest_dma;

	size_t				src_length;

	/* callback func */
	aspeed_hace_fn_t		resume;
	aspeed_hace_fn_t		dma_prepare;
};

struct aspeed_sha_hmac_ctx {
	struct crypto_shash *shash;
	u8 ipad[SHA512_BLOCK_SIZE];
	u8 opad[SHA512_BLOCK_SIZE];
};

struct aspeed_sham_ctx {
	struct aspeed_hace_dev		*hace_dev;
	unsigned long			flags;	/* hmac flag */

	struct aspeed_sha_hmac_ctx	base[];
};

struct aspeed_sham_reqctx {
	unsigned long		flags;		/* final update flag should no use*/
	unsigned long		op;		/* final or update */
	u32			cmd;		/* trigger cmd */

	/* walk state */
	struct scatterlist	*src_sg;
	int			src_nents;
	unsigned int		offset;		/* offset in current sg */
	unsigned int		total;		/* per update length */

	size_t			digsize;
	size_t			block_size;
	size_t			ivsize;
	const __be32		*sha_iv;

	/* remain data buffer */
	u8			buffer[SHA512_BLOCK_SIZE * 2];
	dma_addr_t		buffer_dma_addr;
	size_t			bufcnt;		/* buffer counter */

	/* output buffer */
	u8			digest[SHA512_DIGEST_SIZE] __aligned(64);
	dma_addr_t		digest_dma_addr;
	u64			digcnt[2];
};

struct aspeed_engine_crypto {
	struct tasklet_struct		done_task;
	unsigned long			flags;
	struct skcipher_request		*req;

	/* context buffer */
	void				*cipher_ctx;
	dma_addr_t			cipher_ctx_dma;

	/* input buffer, could be single/scatter-gather lists */
	void				*cipher_addr;
	dma_addr_t			cipher_dma_addr;

	/* output buffer, only used in scatter-gather lists */
	void				*dst_sg_addr;
	dma_addr_t			dst_sg_dma_addr;

	/* callback func */
	aspeed_hace_fn_t		resume;
};

struct aspeed_cipher_ctx {
	struct aspeed_hace_dev		*hace_dev;
	int				key_len;
	u8				key[AES_MAX_KEYLENGTH];

	/* callback func */
	aspeed_hace_fn_t		start;

	struct crypto_skcipher          *fallback_tfm;
};

struct aspeed_cipher_reqctx {
	int enc_cmd;
	int src_nents;
	int dst_nents;

	struct skcipher_request         fallback_req;   /* keep at the end */
};

struct aspeed_hace_dev {
	void __iomem			*regs;
	struct device			*dev;
	int				irq;
	struct clk			*clk;
	unsigned long			version;

	struct crypto_engine		*crypt_engine_hash;
	struct crypto_engine		*crypt_engine_crypto;

	struct aspeed_engine_hash	hash_engine;
	struct aspeed_engine_crypto	crypto_engine;
};

struct aspeed_hace_alg {
	struct aspeed_hace_dev		*hace_dev;

	const char			*alg_base;

	union {
		struct skcipher_engine_alg skcipher;
		struct ahash_engine_alg ahash;
	} alg;
};

enum aspeed_version {
	AST2500_VERSION = 5,
	AST2600_VERSION,
	AST2700_VERSION,
};

#define ast_hace_write(hace, val, offset)	\
	writel((val), (hace)->regs + (offset))
#define ast_hace_read(hace, offset)		\
	readl((hace)->regs + (offset))

void aspeed_register_hace_hash_algs(struct aspeed_hace_dev *hace_dev);
void aspeed_unregister_hace_hash_algs(struct aspeed_hace_dev *hace_dev);
void aspeed_register_hace_crypto_algs(struct aspeed_hace_dev *hace_dev);
void aspeed_unregister_hace_crypto_algs(struct aspeed_hace_dev *hace_dev);

#endif
