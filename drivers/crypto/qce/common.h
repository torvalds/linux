/*
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _COMMON_H_
#define _COMMON_H_

#include <linux/crypto.h>
#include <linux/types.h>
#include <crypto/aes.h>
#include <crypto/hash.h>

/* key size in bytes */
#define QCE_SHA_HMAC_KEY_SIZE		64
#define QCE_MAX_CIPHER_KEY_SIZE		AES_KEYSIZE_256

/* IV length in bytes */
#define QCE_AES_IV_LENGTH		AES_BLOCK_SIZE
/* max of AES_BLOCK_SIZE, DES3_EDE_BLOCK_SIZE */
#define QCE_MAX_IV_SIZE			AES_BLOCK_SIZE

/* maximum nonce bytes  */
#define QCE_MAX_NONCE			16
#define QCE_MAX_NONCE_WORDS		(QCE_MAX_NONCE / sizeof(u32))

/* burst size alignment requirement */
#define QCE_MAX_ALIGN_SIZE		64

/* cipher algorithms */
#define QCE_ALG_DES			BIT(0)
#define QCE_ALG_3DES			BIT(1)
#define QCE_ALG_AES			BIT(2)

/* hash and hmac algorithms */
#define QCE_HASH_SHA1			BIT(3)
#define QCE_HASH_SHA256			BIT(4)
#define QCE_HASH_SHA1_HMAC		BIT(5)
#define QCE_HASH_SHA256_HMAC		BIT(6)
#define QCE_HASH_AES_CMAC		BIT(7)

/* cipher modes */
#define QCE_MODE_CBC			BIT(8)
#define QCE_MODE_ECB			BIT(9)
#define QCE_MODE_CTR			BIT(10)
#define QCE_MODE_XTS			BIT(11)
#define QCE_MODE_CCM			BIT(12)
#define QCE_MODE_MASK			GENMASK(12, 8)

/* cipher encryption/decryption operations */
#define QCE_ENCRYPT			BIT(13)
#define QCE_DECRYPT			BIT(14)

#define IS_DES(flags)			(flags & QCE_ALG_DES)
#define IS_3DES(flags)			(flags & QCE_ALG_3DES)
#define IS_AES(flags)			(flags & QCE_ALG_AES)

#define IS_SHA1(flags)			(flags & QCE_HASH_SHA1)
#define IS_SHA256(flags)		(flags & QCE_HASH_SHA256)
#define IS_SHA1_HMAC(flags)		(flags & QCE_HASH_SHA1_HMAC)
#define IS_SHA256_HMAC(flags)		(flags & QCE_HASH_SHA256_HMAC)
#define IS_CMAC(flags)			(flags & QCE_HASH_AES_CMAC)
#define IS_SHA(flags)			(IS_SHA1(flags) || IS_SHA256(flags))
#define IS_SHA_HMAC(flags)		\
		(IS_SHA1_HMAC(flags) || IS_SHA256_HMAC(flags))

#define IS_CBC(mode)			(mode & QCE_MODE_CBC)
#define IS_ECB(mode)			(mode & QCE_MODE_ECB)
#define IS_CTR(mode)			(mode & QCE_MODE_CTR)
#define IS_XTS(mode)			(mode & QCE_MODE_XTS)
#define IS_CCM(mode)			(mode & QCE_MODE_CCM)

#define IS_ENCRYPT(dir)			(dir & QCE_ENCRYPT)
#define IS_DECRYPT(dir)			(dir & QCE_DECRYPT)

struct qce_alg_template {
	struct list_head entry;
	u32 crypto_alg_type;
	unsigned long alg_flags;
	const u32 *std_iv;
	union {
		struct crypto_alg crypto;
		struct ahash_alg ahash;
	} alg;
	struct qce_device *qce;
};

void qce_cpu_to_be32p_array(__be32 *dst, const u8 *src, unsigned int len);
int qce_check_status(struct qce_device *qce, u32 *status);
void qce_get_version(struct qce_device *qce, u32 *major, u32 *minor, u32 *step);
int qce_start(struct crypto_async_request *async_req, u32 type, u32 totallen,
	      u32 offset);

#endif /* _COMMON_H_ */
