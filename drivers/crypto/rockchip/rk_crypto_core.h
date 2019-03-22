/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_CORE_H__
#define __RK_CRYPTO_CORE_H__

#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/algapi.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <crypto/internal/hash.h>

#include <linux/clk.h>
#include <crypto/md5.h>
#include <crypto/sha.h>

struct rk_crypto_soc_data {
	struct rk_crypto_tmp **cipher_algs;
	int cipher_num;
	const char * const *clks;
	const char * const *rsts;
	int clks_num;
	int rsts_num;
	unsigned int hw_info_size;
	int (*hw_init)(struct device *dev, void *hw_info);
	void (*hw_deinit)(struct device *dev, void *hw_info);
};

struct rk_crypto_info {
	struct device			*dev;
	struct reset_control		*rst;
	void __iomem			*reg;
	int				irq;
	struct crypto_queue		queue;
	struct tasklet_struct		queue_task;
	struct tasklet_struct		done_task;
	struct crypto_async_request	*async_req;
	int					err;
	void				*hw_info;
	/* device lock */
	spinlock_t			lock;

	/* the public variable */
	struct scatterlist		*sg_src;
	struct scatterlist		*sg_dst;
	struct scatterlist		sg_tmp;
	struct scatterlist		*first;
	struct rk_crypto_soc_data	*soc_data;
	struct clk_bulk_data	*clk_bulks;
	unsigned int			left_bytes;
	void				*addr_vir;
	int				aligned;
	int				align_size;
	size_t				src_nents;
	size_t				dst_nents;
	unsigned int			total;
	unsigned int			count;
	dma_addr_t			addr_in;
	dma_addr_t			addr_out;
	bool				busy;
	int (*start)(struct rk_crypto_info *dev);
	int (*update)(struct rk_crypto_info *dev);
	void (*complete)(struct crypto_async_request *base, int err);
	int (*enable_clk)(struct rk_crypto_info *dev);
	void (*disable_clk)(struct rk_crypto_info *dev);
	int (*irq_handle)(int irq, void *dev_id);
	int (*load_data)(struct rk_crypto_info *dev,
			 struct scatterlist *sg_src,
			 struct scatterlist *sg_dst);
	void (*unload_data)(struct rk_crypto_info *dev);
	int (*enqueue)(struct rk_crypto_info *dev,
		       struct crypto_async_request *async_req);
};

/* the private variable of hash */
struct rk_ahash_ctx {
	struct rk_crypto_info		*dev;
	/* for fallback */
	struct crypto_ahash		*fallback_tfm;
};

/* the privete variable of hash for fallback */
struct rk_ahash_rctx {
	struct ahash_request		fallback_req;
	u32				mode;
};

/* the private variable of cipher */
struct rk_cipher_ctx {
	struct rk_crypto_info		*dev;
	unsigned char			key[AES_MAX_KEY_SIZE * 2];
	unsigned int			keylen;
	u32				mode;
	u8				iv[AES_BLOCK_SIZE];
};

enum alg_type {
	ALG_TYPE_HASH,
	ALG_TYPE_CIPHER,
};

struct rk_crypto_tmp {
	struct rk_crypto_info		*dev;
	union {
		struct crypto_alg	crypto;
		struct ahash_alg	hash;
	} alg;
	enum alg_type			type;
};

#define CRYPTO_READ(dev, offset)		  \
		readl_relaxed(((dev)->reg + (offset)))
#define CRYPTO_WRITE(dev, offset, val)	  \
		writel_relaxed((val), ((dev)->reg + (offset)))

#endif

