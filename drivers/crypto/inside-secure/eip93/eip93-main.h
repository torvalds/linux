/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2019 - 2021
 *
 * Richard van Schagen <vschagen@icloud.com>
 * Christian Marangi <ansuelsmth@gmail.com
 */
#ifndef _EIP93_MAIN_H_
#define _EIP93_MAIN_H_

#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <linux/bitfield.h>
#include <linux/interrupt.h>

#define EIP93_RING_BUSY_DELAY		500

#define EIP93_RING_NUM			512
#define EIP93_RING_BUSY			32
#define EIP93_CRA_PRIORITY		1500

#define EIP93_RING_SA_STATE_ADDR(base, idx)	((base) + (idx))
#define EIP93_RING_SA_STATE_DMA(dma_base, idx)	((u32 __force)(dma_base) + \
						 ((idx) * sizeof(struct sa_state)))

/* cipher algorithms */
#define EIP93_ALG_DES			BIT(0)
#define EIP93_ALG_3DES			BIT(1)
#define EIP93_ALG_AES			BIT(2)
#define EIP93_ALG_MASK			GENMASK(2, 0)
/* hash and hmac algorithms */
#define EIP93_HASH_MD5			BIT(3)
#define EIP93_HASH_SHA1			BIT(4)
#define EIP93_HASH_SHA224			BIT(5)
#define EIP93_HASH_SHA256			BIT(6)
#define EIP93_HASH_HMAC			BIT(7)
#define EIP93_HASH_MASK			GENMASK(6, 3)
/* cipher modes */
#define EIP93_MODE_CBC			BIT(8)
#define EIP93_MODE_ECB			BIT(9)
#define EIP93_MODE_CTR			BIT(10)
#define EIP93_MODE_RFC3686		BIT(11)
#define EIP93_MODE_MASK			GENMASK(10, 8)

/* cipher encryption/decryption operations */
#define EIP93_ENCRYPT			BIT(12)
#define EIP93_DECRYPT			BIT(13)

#define EIP93_BUSY			BIT(14)

/* descriptor flags */
#define EIP93_DESC_DMA_IV			BIT(0)
#define EIP93_DESC_IPSEC			BIT(1)
#define EIP93_DESC_FINISH			BIT(2)
#define EIP93_DESC_LAST			BIT(3)
#define EIP93_DESC_FAKE_HMAC		BIT(4)
#define EIP93_DESC_PRNG			BIT(5)
#define EIP93_DESC_HASH			BIT(6)
#define EIP93_DESC_AEAD			BIT(7)
#define EIP93_DESC_SKCIPHER		BIT(8)
#define EIP93_DESC_ASYNC			BIT(9)

#define IS_DMA_IV(desc_flags)		((desc_flags) & EIP93_DESC_DMA_IV)

#define IS_DES(flags)			((flags) & EIP93_ALG_DES)
#define IS_3DES(flags)			((flags) & EIP93_ALG_3DES)
#define IS_AES(flags)			((flags) & EIP93_ALG_AES)

#define IS_HASH_MD5(flags)		((flags) & EIP93_HASH_MD5)
#define IS_HASH_SHA1(flags)		((flags) & EIP93_HASH_SHA1)
#define IS_HASH_SHA224(flags)		((flags) & EIP93_HASH_SHA224)
#define IS_HASH_SHA256(flags)		((flags) & EIP93_HASH_SHA256)
#define IS_HMAC(flags)			((flags) & EIP93_HASH_HMAC)

#define IS_CBC(mode)			((mode) & EIP93_MODE_CBC)
#define IS_ECB(mode)			((mode) & EIP93_MODE_ECB)
#define IS_CTR(mode)			((mode) & EIP93_MODE_CTR)
#define IS_RFC3686(mode)		((mode) & EIP93_MODE_RFC3686)

#define IS_BUSY(flags)			((flags) & EIP93_BUSY)

#define IS_ENCRYPT(dir)			((dir) & EIP93_ENCRYPT)
#define IS_DECRYPT(dir)			((dir) & EIP93_DECRYPT)

#define IS_CIPHER(flags)		((flags) & (EIP93_ALG_DES | \
						    EIP93_ALG_3DES |  \
						    EIP93_ALG_AES))

#define IS_HASH(flags)			((flags) & (EIP93_HASH_MD5 |  \
						    EIP93_HASH_SHA1 |   \
						    EIP93_HASH_SHA224 | \
						    EIP93_HASH_SHA256))

/**
 * struct eip93_device - crypto engine device structure
 */
struct eip93_device {
	void __iomem		*base;
	struct device		*dev;
	struct clk		*clk;
	int			irq;
	struct eip93_ring		*ring;
};

struct eip93_desc_ring {
	void			*base;
	void			*base_end;
	dma_addr_t		base_dma;
	/* write and read pointers */
	void			*read;
	void			*write;
	/* descriptor element offset */
	u32			offset;
};

struct eip93_state_pool {
	void			*base;
	dma_addr_t		base_dma;
};

struct eip93_ring {
	struct tasklet_struct		done_task;
	/* command/result rings */
	struct eip93_desc_ring		cdr;
	struct eip93_desc_ring		rdr;
	spinlock_t			write_lock;
	spinlock_t			read_lock;
	/* aync idr */
	spinlock_t			idr_lock;
	struct idr			crypto_async_idr;
};

enum eip93_alg_type {
	EIP93_ALG_TYPE_AEAD,
	EIP93_ALG_TYPE_SKCIPHER,
	EIP93_ALG_TYPE_HASH,
};

struct eip93_alg_template {
	struct eip93_device	*eip93;
	enum eip93_alg_type	type;
	u32			flags;
	union {
		struct aead_alg		aead;
		struct skcipher_alg	skcipher;
		struct ahash_alg	ahash;
	} alg;
};

#endif /* _EIP93_MAIN_H_ */
