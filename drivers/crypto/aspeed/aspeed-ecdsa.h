/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __ASPEED_ECDSA_H__
#define __ASPEED_ECDSA_H__

#ifdef CONFIG_CRYPTO_DEV_ASPEED_DEBUG
#define AST_DBG(d, fmt, ...)	\
	dev_info((d)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#else
#define AST_DBG(d, fmt, ...)	\
	dev_dbg((d)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#endif

/*************************
 *                       *
 * ECDSA regs definition *
 *                       *
 *************************/
#define ASPEED_ECC_STS_REG		0xb0
#define ASPEED_ECC_CTRL_REG		0xb4
#define ASPEED_ECC_CMD_REG		0xbc
#define ASPEED_ECC_INT_EN		0xc0
#define ASPEED_ECC_INT_STS		0xc4

#define ASPEED_ECC_DATA_BASE		0x800
#define ASPEED_ECC_PAR_GX_REG		0x800
#define ASPEED_ECC_PAR_GY_REG		0x840
#define ASPEED_ECC_PAR_QX_REG		0x880
#define ASPEED_ECC_PAR_QY_REG		0x8c0
#define ASPEED_ECC_PAR_P_REG		0x900
#define ASPEED_ECC_PAR_A_REG		0x940
#define ASPEED_ECC_PAR_N_REG		0x980
#define ASPEED_ECC_SIGN_R_REG		0x9c0
#define ASPEED_ECC_SIGN_S_REG		0xa00
#define ASPEED_ECC_MESSAGE_REG		0xa40
#define ASPEED_ECC_ECDSA_VERIFY		0xbc0

/* sts */
#define ECC_IDLE			BIT(0)
#define ECC_VERIFY_PASS			BIT(1)

/* ctrl/cmd */
#define ECC_EN				BIT(0)
#define ECDSA_384_EN			0x0
#define ECDSA_256_EN			BIT(1)
#define ADDR_BE				BIT(2)
#define DATA_BE				BIT(3)

#define PAR_LEN_256			32
#define PAR_LEN_384			48

#define ASPEED_ECC_POLLING_TIME		100
#define ASPEED_ECC_TIMEOUT		100000	/* 100 ms */

#define CRYPTO_FLAGS_BUSY		BIT(1)

#define ast_write(ast, val, offset)	\
	writel((val), (ast)->regs + (offset))

#define ast_read(ast, offset)		\
	readl((ast)->regs + (offset))

struct aspeed_ecdsa_dev;

typedef int (*aspeed_ecdsa_fn_t)(struct aspeed_ecdsa_dev *);

struct aspeed_ecc_ctx {
	struct aspeed_ecdsa_dev		*ecdsa_dev;
	unsigned int			curve_id;
	const struct ecc_curve		*curve;

	bool				pub_key_set;
	u64				x[ECC_MAX_DIGITS]; /* pub key x and y coordinates */
	u64				y[ECC_MAX_DIGITS];
	struct ecc_point		pub_key;

	struct crypto_akcipher		*fallback_tfm;

	aspeed_ecdsa_fn_t		trigger;
};

struct ecdsa_signature_ctx {
	const struct ecc_curve *curve;
	u64 r[ECC_MAX_DIGITS];
	u64 s[ECC_MAX_DIGITS];
};

struct aspeed_engine_ecdsa {
	struct tasklet_struct		done_task;
	unsigned long			flags;
	struct akcipher_request		*req;
	int				results;

	/* callback func */
	aspeed_ecdsa_fn_t		resume;
};

struct aspeed_ecdsa_alg {
	struct aspeed_ecdsa_dev		*ecdsa_dev;
	struct akcipher_engine_alg	akcipher;
};

struct aspeed_ecdsa_dev {
	void __iomem			*regs;
	struct device			*dev;
	struct clk			*clk;
	struct reset_control		*rst;
	int				irq;

	struct crypto_engine		*crypt_engine_ecdsa;
	struct aspeed_engine_ecdsa	ecdsa_engine;
};

extern const struct asn1_decoder ecdsasignature_decoder;

#endif
