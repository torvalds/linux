/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_CORE_H__
#define __RK_CRYPTO_CORE_H__

#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/algapi.h>
#include <crypto/md5.h>
#include <crypto/sha.h>
#include <crypto/sm3.h>
#include <crypto/sm4.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/rsa.h>
#include <crypto/internal/skcipher.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include "rk_crypto_bignum.h"

#define RK_CRYPTO_PRIORITY		300

struct rk_crypto_soc_data {
	char				**valid_algs_name;
	int				valid_algs_num;
	struct rk_crypto_algt		**total_algs;
	int				total_algs_num;
	const char * const		*rsts;
	int				rsts_num;
	unsigned int			hw_info_size;
	bool				use_soft_aes192;
	int				default_pka_offset;
	int (*hw_init)(struct device *dev, void *hw_info);
	void (*hw_deinit)(struct device *dev, void *hw_info);
};

struct rk_crypto_dev {
	struct device			*dev;
	struct reset_control		*rst;
	void __iomem			*reg;
	void __iomem			*pka_reg;
	int				irq;
	struct crypto_queue		queue;
	struct tasklet_struct		queue_task;
	struct tasklet_struct		done_task;
	int				err;
	void				*hw_info;
	struct rk_crypto_soc_data	*soc_data;
	int clks_num;
	struct clk_bulk_data		*clk_bulks;

	/* device lock */
	spinlock_t			lock;
	struct mutex			mutex;

	/* the public variable */
	struct crypto_async_request	*async_req;
	void				*addr_vir;

	bool				busy;
	void (*request_crypto)(struct rk_crypto_dev *rk_dev, const char *name);
	void (*release_crypto)(struct rk_crypto_dev *rk_dev, const char *name);
	int (*load_data)(struct rk_crypto_dev *rk_dev,
			 struct scatterlist *sg_src,
			 struct scatterlist *sg_dst);
	int (*unload_data)(struct rk_crypto_dev *rk_dev);
	int (*enqueue)(struct rk_crypto_dev *rk_dev,
		       struct crypto_async_request *async_req);
};

struct rk_alg_ops {
	int (*start)(struct rk_crypto_dev *rk_dev);
	int (*update)(struct rk_crypto_dev *rk_dev);
	void (*complete)(struct crypto_async_request *base, int err);
	int (*irq_handle)(int irq, void *dev_id);
};

struct rk_alg_ctx {
	struct rk_alg_ops		ops;
	struct scatterlist		*sg_src;
	struct scatterlist		*sg_dst;
	struct scatterlist		sg_tmp;
	struct scatterlist		*req_src;
	struct scatterlist		*req_dst;
	size_t				src_nents;
	size_t				dst_nents;

	unsigned int			total;
	unsigned int			count;
	unsigned int			left_bytes;

	dma_addr_t			addr_in;
	dma_addr_t			addr_out;

	int				aligned;
	int				align_size;
};

/* the private variable of hash */
struct rk_ahash_ctx {
	struct rk_alg_ctx		algs_ctx;
	struct rk_crypto_dev		*rk_dev;
	u8				authkey[SHA512_BLOCK_SIZE];

	/* for fallback */
	struct crypto_ahash		*fallback_tfm;
};

/* the privete variable of hash for fallback */
struct rk_ahash_rctx {
	struct rk_alg_ctx		algs_ctx;
	struct ahash_request		fallback_req;
	u32				mode;
};

/* the private variable of cipher */
struct rk_cipher_ctx {
	struct rk_alg_ctx		algs_ctx;
	struct rk_crypto_dev		*rk_dev;
	unsigned char			key[AES_MAX_KEY_SIZE * 2];
	unsigned int			keylen;
	u32				mode;
	u8				iv[AES_BLOCK_SIZE];

	/* for fallback */
	struct crypto_skcipher		*fallback_tfm;
};

struct rk_rsa_ctx {
	struct rk_alg_ctx		algs_ctx;
	struct rk_bignum *n;
	struct rk_bignum *e;
	struct rk_bignum *d;

	struct rk_crypto_dev		*rk_dev;
};

enum alg_type {
	ALG_TYPE_HASH,
	ALG_TYPE_HMAC,
	ALG_TYPE_CIPHER,
	ALG_TYPE_ASYM,
};

struct rk_crypto_algt {
	struct rk_crypto_dev		*rk_dev;
	union {
		struct crypto_alg	crypto;
		struct ahash_alg	hash;
		struct akcipher_alg	asym;
	} alg;
	enum alg_type			type;
	u32				algo;
	u32				mode;
	char				*name;
};

enum rk_hash_algo {
	HASH_ALGO_MD5,
	HASH_ALGO_SHA1,
	HASH_ALGO_SHA256,
	HASH_ALGO_SHA512,
	HASH_ALGO_SM3,
};

enum rk_cipher_algo {
	CIPHER_ALGO_DES,
	CIPHER_ALGO_DES3_EDE,
	CIPHER_ALGO_AES,
	CIPHER_ALGO_SM4,
};

enum rk_cipher_mode {
	CIPHER_MODE_ECB,
	CIPHER_MODE_CBC,
	CIPHER_MODE_CFB,
	CIPHER_MODE_OFB,
	CIPHER_MODE_CTR,
	CIPHER_MODE_XTS,
};

#define DES_MIN_KEY_SIZE	DES_KEY_SIZE
#define DES_MAX_KEY_SIZE	DES_KEY_SIZE
#define DES3_EDE_MIN_KEY_SIZE	DES3_EDE_KEY_SIZE
#define DES3_EDE_MAX_KEY_SIZE	DES3_EDE_KEY_SIZE
#define SM4_MIN_KEY_SIZE	SM4_KEY_SIZE
#define SM4_MAX_KEY_SIZE	SM4_KEY_SIZE

#define MD5_BLOCK_SIZE		SHA1_BLOCK_SIZE

#define  RK_CIPHER_ALGO_INIT(cipher_algo, cipher_mode, algo_name, driver_name) {\
	.name = #algo_name,\
	.type = ALG_TYPE_CIPHER,\
	.algo = CIPHER_ALGO_##cipher_algo,\
	.mode = CIPHER_MODE_##cipher_mode,\
	.alg.crypto = {\
		.cra_name		= #algo_name,\
		.cra_driver_name	= #driver_name,\
		.cra_priority		= RK_CRYPTO_PRIORITY,\
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |\
					  CRYPTO_ALG_ASYNC,\
		.cra_blocksize		= cipher_algo##_BLOCK_SIZE,\
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),\
		.cra_alignmask		= 0x07,\
		.cra_type		= &crypto_ablkcipher_type,\
		.cra_module		= THIS_MODULE,\
		.cra_init		= rk_ablk_cra_init,\
		.cra_exit		= rk_ablk_cra_exit,\
		.cra_u.ablkcipher	= {\
			.min_keysize	=  cipher_algo##_MIN_KEY_SIZE,\
			.max_keysize	=  cipher_algo##_MAX_KEY_SIZE,\
			.ivsize		= cipher_algo##_BLOCK_SIZE,\
			.setkey		= rk_cipher_setkey,\
			.encrypt	= rk_cipher_encrypt,\
			.decrypt	= rk_cipher_decrypt,\
		} \
	} \
}

#define  RK_CIPHER_ALGO_XTS_INIT(cipher_algo, algo_name, driver_name) {\
	.name = #algo_name,\
	.type = ALG_TYPE_CIPHER,\
	.algo = CIPHER_ALGO_##cipher_algo,\
	.mode = CIPHER_MODE_XTS,\
	.alg.crypto = {\
		.cra_name		= #algo_name,\
		.cra_driver_name	= #driver_name,\
		.cra_priority		= RK_CRYPTO_PRIORITY,\
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |\
					  CRYPTO_ALG_ASYNC,\
		.cra_blocksize		= cipher_algo##_BLOCK_SIZE,\
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),\
		.cra_alignmask		= 0x07,\
		.cra_type		= &crypto_ablkcipher_type,\
		.cra_module		= THIS_MODULE,\
		.cra_init		= rk_ablk_cra_init,\
		.cra_exit		= rk_ablk_cra_exit,\
		.cra_u.ablkcipher	= {\
			.min_keysize	=  cipher_algo##_MAX_KEY_SIZE,\
			.max_keysize	=  cipher_algo##_MAX_KEY_SIZE * 2,\
			.ivsize		= cipher_algo##_BLOCK_SIZE,\
			.setkey		= rk_cipher_setkey,\
			.encrypt	= rk_cipher_encrypt,\
			.decrypt	= rk_cipher_decrypt,\
		} \
	} \
}

#define RK_HASH_ALGO_INIT(hash_algo, algo_name) {\
	.name = #algo_name,\
	.type = ALG_TYPE_HASH,\
	.algo = HASH_ALGO_##hash_algo,\
	.alg.hash = {\
		.init = rk_ahash_init,\
		.update = rk_ahash_update,\
		.final = rk_ahash_final,\
		.finup = rk_ahash_finup,\
		.export = rk_ahash_export,\
		.import = rk_ahash_import,\
		.digest = rk_ahash_digest,\
		.halg = {\
			.digestsize = hash_algo##_DIGEST_SIZE,\
			.statesize = sizeof(struct algo_name##_state),\
			.base = {\
				.cra_name = #algo_name,\
				.cra_driver_name = #algo_name"-rk",\
				.cra_priority = RK_CRYPTO_PRIORITY,\
				.cra_flags = CRYPTO_ALG_ASYNC |\
						 CRYPTO_ALG_NEED_FALLBACK,\
				.cra_blocksize = hash_algo##_BLOCK_SIZE,\
				.cra_ctxsize = sizeof(struct rk_ahash_ctx),\
				.cra_alignmask = 3,\
				.cra_init = rk_cra_hash_init,\
				.cra_exit = rk_cra_hash_exit,\
				.cra_module = THIS_MODULE,\
			} \
		} \
	} \
}

#define RK_HMAC_ALGO_INIT(hash_algo, algo_name) {\
	.name = "hmac(" #algo_name ")",\
	.type = ALG_TYPE_HMAC,\
	.algo = HASH_ALGO_##hash_algo,\
	.alg.hash = {\
		.init = rk_ahash_init,\
		.update = rk_ahash_update,\
		.final = rk_ahash_final,\
		.finup = rk_ahash_finup,\
		.export = rk_ahash_export,\
		.import = rk_ahash_import,\
		.digest = rk_ahash_digest,\
		.setkey = rk_ahash_hmac_setkey,\
		.halg = {\
			.digestsize = hash_algo##_DIGEST_SIZE,\
			.statesize = sizeof(struct algo_name##_state),\
			.base = {\
				.cra_name = "hmac(" #algo_name ")",\
				.cra_driver_name = "hmac-" #algo_name "-rk",\
				.cra_priority = RK_CRYPTO_PRIORITY,\
				.cra_flags = CRYPTO_ALG_ASYNC |\
						 CRYPTO_ALG_NEED_FALLBACK,\
				.cra_blocksize = hash_algo##_BLOCK_SIZE,\
				.cra_ctxsize = sizeof(struct rk_ahash_ctx),\
				.cra_alignmask = 3,\
				.cra_init = rk_cra_hash_init,\
				.cra_exit = rk_cra_hash_exit,\
				.cra_module = THIS_MODULE,\
			} \
		} \
	} \
}

#define IS_TYPE_HMAC(type) ((type) == ALG_TYPE_HMAC)

#define CRYPTO_READ(dev, offset)		  \
		readl_relaxed(((dev)->reg + (offset)))
#define CRYPTO_WRITE(dev, offset, val)	  \
		writel_relaxed((val), ((dev)->reg + (offset)))

#ifdef DEBUG
#define CRYPTO_TRACE(format, ...) pr_err("[%s, %05d]-trace: " format "\n", \
					 __func__, __LINE__, ##__VA_ARGS__)
#define CRYPTO_MSG(format, ...) pr_err("[%s, %05d]-msg:" format "\n", \
				       __func__, __LINE__, ##__VA_ARGS__)
#define CRYPTO_DUMPHEX(var_name, data, len) print_hex_dump(KERN_CONT, (var_name), \
							   DUMP_PREFIX_OFFSET, \
							   16, 1, (data), (len), false)
#else
#define CRYPTO_TRACE(format, ...)
#define CRYPTO_MSG(format, ...)
#define CRYPTO_DUMPHEX(var_name, data, len)
#endif

#endif

