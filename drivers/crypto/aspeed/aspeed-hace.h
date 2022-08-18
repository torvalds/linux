/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __ASPEED_HACE_H__
#define __ASPEED_HACE_H__

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fips.h>
#include <linux/dma-mapping.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/kpp.h>
#include <crypto/internal/skcipher.h>
#include <crypto/algapi.h>
#include <crypto/engine.h>
#include <crypto/hmac.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>

/*****************************
 *                           *
 * HACE register definitions *
 *                           *
 * ***************************/

#define ASPEED_HACE_STS			0x1C	/* HACE Status Register */
#define ASPEED_HACE_HASH_SRC		0x20	/* Hash Data Source Base Address Register */
#define ASPEED_HACE_HASH_DIGEST_BUFF	0x24	/* Hash Digest Write Buffer Base Address Register */
#define ASPEED_HACE_HASH_KEY_BUFF	0x28	/* Hash HMAC Key Buffer Base Address Register */
#define ASPEED_HACE_HASH_DATA_LEN	0x2C	/* Hash Data Length Register */
#define ASPEED_HACE_HASH_CMD		0x30	/* Hash Engine Command Register */

/* interrupt status reg */
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

struct aspeed_hace_dev;

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
	struct crypto_engine_ctx	enginectx;

	struct aspeed_hace_dev		*hace_dev;
	unsigned long			flags;	/* hmac flag */

	struct aspeed_sha_hmac_ctx	base[0];
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

struct aspeed_hace_dev {
	void __iomem			*regs;
	struct device			*dev;
	int				irq;
	struct clk			*clk;
	unsigned long			version;

	struct crypto_engine		*crypt_engine_hash;

	struct aspeed_engine_hash	hash_engine;
};

struct aspeed_hace_alg {
	struct aspeed_hace_dev		*hace_dev;

	const char			*alg_base;

	union {
		struct skcipher_alg	skcipher;
		struct ahash_alg	ahash;
	} alg;
};

enum aspeed_version {
	AST2500_VERSION = 5,
	AST2600_VERSION
};

#define ast_hace_write(hace, val, offset)	\
	writel((val), (hace)->regs + (offset))
#define ast_hace_read(hace, offset)		\
	readl((hace)->regs + (offset))

void aspeed_register_hace_hash_algs(struct aspeed_hace_dev *hace_dev);
void aspeed_unregister_hace_hash_algs(struct aspeed_hace_dev *hace_dev);

#endif
