/**
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This is the header file for AMCC Crypto offload Linux device driver for
 * use with Linux CryptoAPI.

 */

#ifndef __CRYPTO4XX_CORE_H__
#define __CRYPTO4XX_CORE_H__

#include <crypto/internal/hash.h>
#include "crypto4xx_reg_def.h"
#include "crypto4xx_sa.h"

#define MODULE_NAME "crypto4xx"

#define PPC460SX_SDR0_SRST                      0x201
#define PPC405EX_SDR0_SRST                      0x200
#define PPC460EX_SDR0_SRST                      0x201
#define PPC460EX_CE_RESET                       0x08000000
#define PPC460SX_CE_RESET                       0x20000000
#define PPC405EX_CE_RESET                       0x00000008

#define CRYPTO4XX_CRYPTO_PRIORITY		300
#define PPC4XX_LAST_PD				63
#define PPC4XX_NUM_PD				64
#define PPC4XX_LAST_GD				1023
#define PPC4XX_NUM_GD				1024
#define PPC4XX_LAST_SD				63
#define PPC4XX_NUM_SD				64
#define PPC4XX_SD_BUFFER_SIZE			2048

#define PD_ENTRY_INUSE				1
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
	u32 using_sd;
	u32 first_gd;		/* first gather discriptor
				used by this packet */
	u32 num_gd;             /* number of gather discriptor
				used by this packet */
	u32 first_sd;		/* first scatter discriptor
				used by this packet */
	u32 num_sd;		/* number of scatter discriptors
				used by this packet */
	struct dynamic_sa_ctl *sa_va;	/* shadow sa */
	u32 sa_pa;
	struct sa_state_record *sr_va;	/* state record for shadow sa */
	u32 sr_pa;
	struct scatterlist *dest_va;
	struct crypto_async_request *async_req; 	/* base crypto request
							for this packet */
};

struct crypto4xx_device {
	struct crypto4xx_core_device *core_dev;
	char *name;
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
};

struct crypto4xx_ctx {
	struct crypto4xx_device *dev;
	struct dynamic_sa_ctl *sa_in;
	dma_addr_t sa_in_dma_addr;
	struct dynamic_sa_ctl *sa_out;
	dma_addr_t sa_out_dma_addr;
	struct sa_state_record *state_record;
	dma_addr_t state_record_dma_addr;
	u32 sa_len;
	u32 offset_to_sr_ptr;           /* offset to state ptr, in dynamic sa */
	u32 direction;
	u32 save_iv;
	u32 pd_ctl;
	u32 is_hash;
};

struct crypto4xx_alg_common {
	u32 type;
	union {
		struct crypto_alg cipher;
		struct ahash_alg hash;
	} u;
};

struct crypto4xx_alg {
	struct list_head  entry;
	struct crypto4xx_alg_common alg;
	struct crypto4xx_device *dev;
};

static inline struct crypto4xx_alg *crypto_alg_to_crypto4xx_alg(
	struct crypto_alg *x)
{
	switch (x->cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_AHASH:
		return container_of(__crypto_ahash_alg(x),
				    struct crypto4xx_alg, alg.u.hash);
	}

	return container_of(x, struct crypto4xx_alg, alg.u.cipher);
}

int crypto4xx_alloc_sa(struct crypto4xx_ctx *ctx, u32 size);
void crypto4xx_free_sa(struct crypto4xx_ctx *ctx);
void crypto4xx_free_ctx(struct crypto4xx_ctx *ctx);
u32 crypto4xx_alloc_state_record(struct crypto4xx_ctx *ctx);
void crypto4xx_memcpy_le(unsigned int *dst,
			 const unsigned char *buf, int len);
u32 crypto4xx_build_pd(struct crypto_async_request *req,
		       struct crypto4xx_ctx *ctx,
		       struct scatterlist *src,
		       struct scatterlist *dst,
		       unsigned int datalen,
		       void *iv, u32 iv_len);
int crypto4xx_setkey_aes_cbc(struct crypto_ablkcipher *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_setkey_aes_cfb(struct crypto_ablkcipher *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_setkey_aes_ecb(struct crypto_ablkcipher *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_setkey_aes_ofb(struct crypto_ablkcipher *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_setkey_rfc3686(struct crypto_ablkcipher *cipher,
			     const u8 *key, unsigned int keylen);
int crypto4xx_encrypt(struct ablkcipher_request *req);
int crypto4xx_decrypt(struct ablkcipher_request *req);
int crypto4xx_rfc3686_encrypt(struct ablkcipher_request *req);
int crypto4xx_rfc3686_decrypt(struct ablkcipher_request *req);
int crypto4xx_sha1_alg_init(struct crypto_tfm *tfm);
int crypto4xx_hash_digest(struct ahash_request *req);
int crypto4xx_hash_final(struct ahash_request *req);
int crypto4xx_hash_update(struct ahash_request *req);
int crypto4xx_hash_init(struct ahash_request *req);
#endif
