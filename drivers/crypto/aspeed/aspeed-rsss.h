/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __ASPEED_RSSS_H__
#define __ASPEED_RSSS_H__

#include <crypto/scatterwalk.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/rsa.h>
#include <crypto/engine.h>
#include <crypto/akcipher.h>
#include <crypto/hash.h>
#include <crypto/sha3.h>

#ifdef CONFIG_CRYPTO_DEV_ASPEED_DEBUG
#define RSSS_DBG(d, fmt, ...)	\
	dev_info((d)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#else
#define RSSS_DBG(d, fmt, ...)	\
	dev_dbg((d)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#endif

/*****************************
 *                           *
 * RSSS register definitions *
 *                           *
 * ***************************/
#define ASPEED_RSSS_INT_STS		0xc00	/* RSSS interrupt status */
#define ASPEED_RSSS_INT_EN		0xc04	/* RSSS interrupt enable */
#define ASPEED_RSSS_CTRL		0xc08	/* RSSS generic control */
#define ASPEED_RSA_TRIGGER		0xe00	/* RSA Engine Control: trigger */
#define ASPEED_RSA_KEY_INFO		0xe08	/* RSA Exp/Mod Key Length (Bits) */
#define ASPEED_RSA_ENG_STS		0xe0c	/* RSA Engine Status */

#define ASPEED_SHA3_CMD			0xe80
#define ASPEED_SHA3_SRC_LO		0xe84
#define ASPEED_SHA3_SRC_HI		0xe88
#define ASPEED_SHA3_SRC_LEN		0xe8c
#define ASPEED_SHA3_DST_LO		0xe90
#define ASPEED_SHA3_DST_HI		0xe94
#define ASPEED_SHA3_STATUS		0xe98
#define ASPEED_SHA3_ENG_STS		0xe9c

/* RSSS interrupt status */
#define SM4_INT_DONE			BIT(3)
#define SM3_INT_DONE			BIT(2)
#define SHA3_INT_DONE			BIT(1)
#define RSA_INT_DONE			BIT(0)

/* RSSS interrupt enable */
#define RSA_INT_EN			BIT(3)
#define SHA3_INT_EN			BIT(2)
#define SM3_INT_EN			BIT(1)
#define SM4_INT_EN			BIT(0)

/* RSSS generic control */
#define RSA_OPERATION			(BIT(18) | BIT(19))
#define SRAM_AHB_MODE_CPU		BIT(16)
#define SRAM_AHB_MODE_ENGINE		0x0
#define SRAM_BUFF_PD			(BIT(5) | BIT(4))
#define SM4_DISABLE			BIT(3)
#define SM3_DISABLE			BIT(2)
#define SHA3_DISABLE			BIT(1)

/* RSA trigger */
#define  RSA_TRIGGER			BIT(0)

/* RSA key len */
#define RSA_E_BITS_LEN(x)		((x) << 16)
#define RSA_M_BITS_LEN(x)		(x)

/* RSA SRAM */
#define SRAM_OFFSET_EXP			0x0
#define SRAM_OFFSET_MOD			0x400
#define SRAM_OFFSET_DATA		0x800
#define SRAM_BLOCK_SIZE			0x400

#define ASPEED_RSA_MAX_KEY_LEN		512	/* RSA maximum key length (Bytes) */

#define CRYPTO_FLAGS_BUSY		BIT(1)

/* SHA3 command */
#define SHA3_CMD_TRIG			BIT(31)
#define SHA3_CMD_MODE_224		(0x0 << 28)
#define SHA3_CMD_MODE_256		(0x1 << 28)
#define SHA3_CMD_MODE_384		(0x2 << 28)
#define SHA3_CMD_MODE_512		(0x3 << 28)
#define SHA3_CMD_MODE_S128		(0x4 << 28)
#define SHA3_CMD_MODE_S256		(0x5 << 28)
#define SHA3_CMD_HW_PAD			BIT(27)
#define SHA3_CMD_ACC_FINAL		BIT(26)
#define SHA3_CMD_ACC			BIT(25)
#define SHA3_CMD_SG_MODE		BIT(24)
#define SHA3_CMD_IN_RST			BIT(21)
#define SHA3_CMD_OUT_RST		BIT(20)
#define SHA3_CMD_OUT_LEN(x)		((x) & 0x1ffff)

#define SHA3_FLAGS_SHA224		BIT(0)
#define SHA3_FLAGS_SHA256		BIT(1)
#define SHA3_FLAGS_SHA384		BIT(2)
#define SHA3_FLAGS_SHA512		BIT(3)
#define SHA3_FLAGS_FINUP		BIT(0xa)
#define SHA3_FLAGS_MASK			(0xff)

#define SG_LAST_LIST			BIT(31)

#define SHA_OP_UPDATE			1
#define SHA_OP_FINAL			2

struct aspeed_rsss_dev;

typedef int (*aspeed_rsss_fn_t)(struct aspeed_rsss_dev *);

struct aspeed_sg_list {
	__le32 len;
	__le32 phy_addr;
};

struct aspeed_engine_rsa {
	struct tasklet_struct		done_task;
	unsigned long			flags;
	struct akcipher_request		*req;

	/* RSA input/output SRAM buffer */
	void __iomem			*sram_exp;
	void __iomem			*sram_mod;
	void __iomem			*sram_data;

	/* callback func */
	aspeed_rsss_fn_t		resume;
};

struct aspeed_engine_sha3 {
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
	aspeed_rsss_fn_t		resume;
	aspeed_rsss_fn_t		dma_prepare;
};

struct aspeed_rsss_dev {
	void __iomem			*regs;
	struct device			*dev;
	int				irq;
	struct clk			*clk;

	struct crypto_engine		*crypt_engine_rsa;
	struct crypto_engine		*crypt_engine_sha3;

	struct aspeed_engine_rsa	rsa_engine;
	struct aspeed_engine_sha3	sha3_engine;
};

enum aspeed_algo_type {
	ASPEED_ALGO_TYPE_AKCIPHER,
	ASPEED_ALGO_TYPE_AHASH,
};

struct aspeed_rsss_alg {
	struct aspeed_rsss_dev		*rsss_dev;
	enum aspeed_algo_type		type;
	union {
		struct akcipher_alg	akcipher;
		struct ahash_alg	ahash;
	} alg;
};

/* RSA related */
struct aspeed_rsa_ctx {
	struct crypto_engine_ctx	enginectx;
	struct aspeed_rsss_dev		*rsss_dev;

	struct rsa_key			key;
	int				enc;
	u8				*n;
	u8				*e;
	u8				*d;
	size_t				n_sz;
	size_t				e_sz;
	size_t				d_sz;

	aspeed_rsss_fn_t		trigger;

	struct crypto_akcipher          *fallback_tfm;
};

enum aspeed_rsa_key_mode {
	ASPEED_RSA_EXP_MODE = 0,
	ASPEED_RSA_MOD_MODE,
	ASPEED_RSA_DATA_MODE,
};

/* Hash related */
struct aspeed_sha3_ctx {
	struct crypto_engine_ctx	enginectx;
	struct aspeed_rsss_dev		*rsss_dev;
};

struct aspeed_sha3_reqctx {
	unsigned long			flags;		/* final update flag should no use */
	unsigned long			op;		/* final or update */
	u32				cmd;		/* trigger cmd */

	/* walk state */
	struct scatterlist		*src_sg;
	int				src_nents;
	unsigned int			offset;		/* offset in current sg */
	unsigned int			total;		/* per update length */

	size_t				digsize;
	size_t				blksize;
	size_t				ivsize;

	/* remain data buffer */
	u8				buffer[SHA3_512_BLOCK_SIZE * 2];
	dma_addr_t			buffer_dma_addr;
	size_t				bufcnt;		/* buffer counter */

	/* output buffer */
	u8				digest[SHA3_512_DIGEST_SIZE] __aligned(64);
	dma_addr_t			digest_dma_addr;
	u64				digcnt[2];
};

/******************************************************************************/

#define ast_rsss_write(rsss, val, offset)	\
	writel((val), (rsss)->regs + (offset))

#define ast_rsss_read(rsss, offset)		\
	readl((rsss)->regs + (offset))

int aspeed_rsss_rsa_init(struct aspeed_rsss_dev *rsss_dev);
void aspeed_rsss_rsa_exit(struct aspeed_rsss_dev *rsss_dev);
int aspeed_rsss_sha3_init(struct aspeed_rsss_dev *rsss_dev);
void aspeed_rsss_sha3_exit(struct aspeed_rsss_dev *rsss_dev);

extern struct aspeed_rsss_alg aspeed_rsss_algs_rsa;
extern struct aspeed_rsss_alg aspeed_rsss_algs_sha3_224;
extern struct aspeed_rsss_alg aspeed_rsss_algs_sha3_256;
extern struct aspeed_rsss_alg aspeed_rsss_algs_sha3_384;
extern struct aspeed_rsss_alg aspeed_rsss_algs_sha3_512;

#endif /* __ASPEED_RSSS_H__ */
