/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* \file ssi_cipher.h
 * ARM CryptoCell Cipher Crypto API
 */

#ifndef __SSI_CIPHER_H__
#define __SSI_CIPHER_H__

#include <linux/kernel.h>
#include <crypto/algapi.h>
#include "ssi_driver.h"
#include "ssi_buffer_mgr.h"

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
	enum ssi_req_dma_buf_type dma_buf_type;
	u32 in_nents;
	u32 in_mlli_nents;
	u32 out_nents;
	u32 out_mlli_nents;
	u8 *backup_info; /*store iv for generated IV flow*/
	u8 *iv;
	bool is_giv;
	struct mlli_params mlli_params;
};

int ssi_ablkcipher_alloc(struct ssi_drvdata *drvdata);

int ssi_ablkcipher_free(struct ssi_drvdata *drvdata);

#ifndef CRYPTO_ALG_BULK_MASK

#define CRYPTO_ALG_BULK_DU_512	0x00002000
#define CRYPTO_ALG_BULK_DU_4096	0x00004000
#define CRYPTO_ALG_BULK_MASK	(CRYPTO_ALG_BULK_DU_512 |\
				CRYPTO_ALG_BULK_DU_4096)
#endif /* CRYPTO_ALG_BULK_MASK */

#ifdef CRYPTO_TFM_REQ_HW_KEY

static inline bool ssi_is_hw_key(struct crypto_tfm *tfm)
{
	return (crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_HW_KEY);
}

#else

struct arm_hw_key_info {
	int hw_key1;
	int hw_key2;
};

static inline bool ssi_is_hw_key(struct crypto_tfm *tfm)
{
	return false;
}

#endif /* CRYPTO_TFM_REQ_HW_KEY */

#endif /*__SSI_CIPHER_H__*/
