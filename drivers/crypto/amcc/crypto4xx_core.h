/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
 *
 * This is the header file for AMCC Crypto offload Linux device driver for
 * use with Linux CryptoAPI.

 */

#ifndef __CRYPTO4XX_CORE_H__
#define __CRYPTO4XX_CORE_H__

#include <linux/ratelimit.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/skcipher.h>
#include "crypto4xx_reg_def.h"
#include "crypto4xx_sa.h"

#define PPC460SX_SDR0_SRST                      0x201
#define PPC405EX_SDR0_SRST                      0x200
#define PPC460EX_SDR0_SRST                      0x201
#define PPC460EX_CE_RESET                       0x08000000
#define PPC460SX_CE_RESET                       0x20000000
#define PPC405EX_CE_RESET                       0x00000008

#define CRYPTO4XX_CRYPTO_PRIORITY		300
#define PPC4XX_NUM_PD				256
#define PPC4XX_LAST_PD				(PPC4XX_NUM_PD - 1)
#define PPC4XX_NUM_GD				1024
#define PPC4XX_LAST_GD				(PPC4XX_NUM_GD - 1)
#define PPC4XX_NUM_SD				256
#define PPC4XX_LAST_SD				(PPC4XX_NUM_SD - 1)
#define PPC4XX_SD_BUFFER_SIZE			2048

#define PD_ENTRY_BUSY				BIT(1)
#define PD_ENTRY_INUSE				BIT(0)
#define PD_ENTRY_FREE				0
#define ERING_WAS_FULL				0xffffffff

struct crypto4xx_device;

union shadow_sa_buf {
	struct dynamic_sa_ctl sa;

	/* alloc 256 bytes which is enough for any kind of dynamic sa */
	u8 buf[256];
} __packed;

struct pd_uinfo {
	struct crypto4xx_device *dev;
	u32   state;
	u32 first_gd;		/* first gather discriptor
				used by this packet */
	u32 num_gd;             /* number of gather discriptor
				used by this packet */
	u32 first_sd;		/* first scatter discriptor
				used by this packet */
	u32 num_sd;		/* number of scatter discriptors
				used by this packet */
	struct dynamic_sa_ctl *sa_va;	/* shadow sa */
	struct sa_state_record *sr_va;	/* state record for shadow sa */
	u32 sr_pa;
	struct scatterlist *dest_va;
	struct crypto_async_request *async_req; 	/* base crypto request
							for this packet */
};

struct crypto4xx_device {
	struct crypto4xx_core_device *core_dev;
	void __iomem *ce_base;
	void __iomem *trng_base;

	struct ce_pd *pdr;	/* base address of packet descriptor ring */
	dma_addr_t pdr_pa;	/* physical address of pdr_base_register */
	struct ce_gd *gdr;	/* gather descriptor ring */
	dma_addr_t gdr_pa;	/* physical address of gdr_base_register */
	struct ce_sd *sdr;	/* scatter descriptor ring */
	dma_addr_t sdr_pa;	/* physical address of sdr_base_register */
	void *scatter_buffer_va;
	dma_addr_t scatter_buffer_pa;

	union shadow_sa_buf *shadow_sa_pool;
	dma_addr_t shadow_sa_pool_pa;
	struct sa_state_record *shadow_sr_pool;
	dma_addr_t shadow_sr_pool_pa;
	u32 pdr_tail;
	u32 pdr_head;
	u32 gdr_tail;
	u32 gdr_head;
	u32 sdr_tail;
	u32 sdr_head;
	struct pd_uinfo *pdr_uinfo;
	struct list_head alg_list;	/* List of algorithm supported
					by this device */
	struct ratelimit_state aead_ratelimit;
	bool is_revb;
};

struct crypto4xx_core_device {
	struct device *device;
	struct platform_device *ofdev;
	struct crypto4xx_device *dev;
	struct hwrng *trng;
	u32 int_status;
	u32 irq;
	struct tasklet_struct tasklet;
	spinlock_t lock;
	struct mutex rng_lock;
};

struct crypto4xx_ctx {
	struct crypto4xx_device *dev;
	struct dynamic_sa_ctl *sa_in;
	struct dynamic_sa_ctl *sa_out;
	__le32 iv_nonce;
	u32 sa_len;
	union {
		struct crypto_sync_skcipher *cipher;
		struct crypto_aead *aead;
	} sw_cipher;
};

struct crypto4xx_aead_reqctx {
	struct scatterlist dst[2];
};

struct crypto4xx_alg_common {
	u32 type;
	union {
		struct skcipher_alg cipher;
		struct aead_alg aead;
		struct rng_alg rng;
	} u;
};

struct crypto4xx_alg {
	struct list_head  entry;
	struct crypto4xx_alg_common alg;
	struct crypto4xx_device *dev;
};

#if IS_ENABLED(CONFIG_CC_IS_GCC) && CONFIG_GCC_VERSION >= 120000
#define BUILD_PD_ACCESS __attribute__((access(read_only, 6, 7)))
#else
#define BUILD_PD_ACCESS
#endif

int crypto4xx_alloc_sa(struct crypto4xx_ctx *ctx, u32 size);
void crypto4xx_free_sa(struct crypto4xx_ctx *ctx);
int crypto4xx_build_pd(struct crypto_async_request *req,
		       struct crypto4xx_ctx *ctx,
		       struct scatterlist *src,
		       struct scatterlist *dst,
		       const unsigned int datalen,
		       const void *iv, const u32 iv_len,
		       const struct dynamic_sa_ctl *sa,
		       const unsigned int sa_len,
		       const unsigned int assoclen,
		       struct scatterlist *dst_tmp) BUILD_PD_ACCESS;
int crypto4xx_setkey_aes_cbc(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_setkey_aes_ctr(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_setkey_aes_ecb(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_setkey_rfc3686(struct crypto_skcipher *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_encrypt_ctr(struct skcipher_request *req);
int crypto4xx_decrypt_ctr(struct skcipher_request *req);
int crypto4xx_encrypt_iv_stream(struct skcipher_request *req);
int crypto4xx_decrypt_iv_stream(struct skcipher_request *req);
int crypto4xx_encrypt_iv_block(struct skcipher_request *req);
int crypto4xx_decrypt_iv_block(struct skcipher_request *req);
int crypto4xx_encrypt_noiv_block(struct skcipher_request *req);
int crypto4xx_decrypt_noiv_block(struct skcipher_request *req);
int crypto4xx_rfc3686_encrypt(struct skcipher_request *req);
int crypto4xx_rfc3686_decrypt(struct skcipher_request *req);

/*
 * Note: Only use this function to copy items that is word aligned.
 */
static inline void crypto4xx_memcpy_swab32(u32 *dst, const void *buf,
					   size_t len)
{
	for (; len >= 4; buf += 4, len -= 4)
		*dst++ = __swab32p((u32 *) buf);

	if (len) {
		const u8 *tmp = (u8 *)buf;

		switch (len) {
		case 3:
			*dst = (tmp[2] << 16) |
			       (tmp[1] << 8) |
			       tmp[0];
			break;
		case 2:
			*dst = (tmp[1] << 8) |
			       tmp[0];
			break;
		case 1:
			*dst = tmp[0];
			break;
		default:
			break;
		}
	}
}

static inline void crypto4xx_memcpy_from_le32(u32 *dst, const void *buf,
					      size_t len)
{
	crypto4xx_memcpy_swab32(dst, buf, len);
}

static inline void crypto4xx_memcpy_to_le32(__le32 *dst, const void *buf,
					    size_t len)
{
	crypto4xx_memcpy_swab32((u32 *)dst, buf, len);
}

int crypto4xx_setauthsize_aead(struct crypto_aead *ciper,
			       unsigned int authsize);
int crypto4xx_setkey_aes_ccm(struct crypto_aead *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_encrypt_aes_ccm(struct aead_request *req);
int crypto4xx_decrypt_aes_ccm(struct aead_request *req);
int crypto4xx_setkey_aes_gcm(struct crypto_aead *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_encrypt_aes_gcm(struct aead_request *req);
int crypto4xx_decrypt_aes_gcm(struct aead_request *req);

#endif
