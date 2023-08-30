/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __ASPEED_RSSS_H__
#define __ASPEED_RSSS_H__

#include <crypto/scatterwalk.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/rsa.h>
#include <crypto/engine.h>

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
#define ASPEED_RSA_ENG_STATUS		0xe0c	/* RSA Engine Status */

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

struct aspeed_rsss_dev;

typedef int (*aspeed_rsss_fn_t)(struct aspeed_rsss_dev *);

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

	/* callback func */
	aspeed_rsss_fn_t		resume;
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

struct aspeed_rsss_rsa_alg {
	struct aspeed_rsss_dev		*rsss_dev;
	struct akcipher_alg		akcipher;
};

enum aspeed_rsa_key_mode {
	ASPEED_RSA_EXP_MODE = 0,
	ASPEED_RSA_MOD_MODE,
	ASPEED_RSA_DATA_MODE,
};

#define ast_rsss_write(rsss, val, offset)	\
	writel((val), (rsss)->regs + (offset))

#define ast_rsss_read(rsss, offset)		\
	readl((rsss)->regs + (offset))

#endif /* __ASPEED_RSSS_H__ */
