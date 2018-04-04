/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

/* \file cc_cipher.h
 * ARM CryptoCell Cipher Crypto API
 */

#ifndef __CC_CIPHER_H__
#define __CC_CIPHER_H__

#include <linux/kernel.h>
#include <crypto/algapi.h>
#include "cc_driver.h"
#include "cc_buffer_mgr.h"

/* Crypto cipher flags */
#define CC_CRYPTO_CIPHER_KEY_KFDE0	BIT(0)
#define CC_CRYPTO_CIPHER_KEY_KFDE1	BIT(1)
#define CC_CRYPTO_CIPHER_KEY_KFDE2	BIT(2)
#define CC_CRYPTO_CIPHER_KEY_KFDE3	BIT(3)
#define CC_CRYPTO_CIPHER_DU_SIZE_512B	BIT(4)

#define CC_CRYPTO_CIPHER_KEY_KFDE_MASK (CC_CRYPTO_CIPHER_KEY_KFDE0 | \
					CC_CRYPTO_CIPHER_KEY_KFDE1 | \
					CC_CRYPTO_CIPHER_KEY_KFDE2 | \
					CC_CRYPTO_CIPHER_KEY_KFDE3)

struct blkcipher_req_ctx {
	struct async_gen_req_ctx gen_ctx;
	enum cc_req_dma_buf_type dma_buf_type;
	u32 in_nents;
	u32 in_mlli_nents;
	u32 out_nents;
	u32 out_mlli_nents;
	u8 *backup_info; /*store iv for generated IV flow*/
	u8 *iv;
	bool is_giv;
	struct mlli_params mlli_params;
};

int cc_cipher_alloc(struct cc_drvdata *drvdata);

int cc_cipher_free(struct cc_drvdata *drvdata);

#ifndef CRYPTO_ALG_BULK_MASK

#define CRYPTO_ALG_BULK_DU_512	0x00002000
#define CRYPTO_ALG_BULK_DU_4096	0x00004000
#define CRYPTO_ALG_BULK_MASK	(CRYPTO_ALG_BULK_DU_512 |\
				CRYPTO_ALG_BULK_DU_4096)
#endif /* CRYPTO_ALG_BULK_MASK */

#ifdef CRYPTO_TFM_REQ_HW_KEY

static inline bool cc_is_hw_key(struct crypto_tfm *tfm)
{
	return (crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_HW_KEY);
}

#else

struct arm_hw_key_info {
	int hw_key1;
	int hw_key2;
};

static inline bool cc_is_hw_key(struct crypto_tfm *tfm)
{
	return false;
}

#endif /* CRYPTO_TFM_REQ_HW_KEY */

#endif /*__CC_CIPHER_H__*/
