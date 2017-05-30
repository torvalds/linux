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

/**************************************************************
 * This file defines the driver FIPS Low Level implmentaion functions,
 * that executes the KAT.
 ***************************************************************/
#include <linux/kernel.h>

#include "ssi_driver.h"
#include "ssi_fips_local.h"
#include "ssi_fips_data.h"
#include "cc_crypto_ctx.h"
#include "ssi_hash.h"
#include "ssi_request_mgr.h"


static const u32 digest_len_init[] = {
	0x00000040, 0x00000000, 0x00000000, 0x00000000 };
static const u32 sha1_init[] = {
	SHA1_H4, SHA1_H3, SHA1_H2, SHA1_H1, SHA1_H0 };
static const u32 sha256_init[] = {
	SHA256_H7, SHA256_H6, SHA256_H5, SHA256_H4,
	SHA256_H3, SHA256_H2, SHA256_H1, SHA256_H0 };
#if (CC_SUPPORT_SHA > 256)
static const u32 digest_len_sha512_init[] = {
	0x00000080, 0x00000000, 0x00000000, 0x00000000 };
static const u64 sha512_init[] = {
	SHA512_H7, SHA512_H6, SHA512_H5, SHA512_H4,
	SHA512_H3, SHA512_H2, SHA512_H1, SHA512_H0 };
#endif


#define NIST_CIPHER_AES_MAX_VECTOR_SIZE      32

struct fips_cipher_ctx {
	u8 iv[CC_AES_IV_SIZE];
	u8 key[AES_512_BIT_KEY_SIZE];
	u8 din[NIST_CIPHER_AES_MAX_VECTOR_SIZE];
	u8 dout[NIST_CIPHER_AES_MAX_VECTOR_SIZE];
};

typedef struct _FipsCipherData {
	u8                   isAes;
	u8                   key[AES_512_BIT_KEY_SIZE];
	size_t                    keySize;
	u8                   iv[CC_AES_IV_SIZE];
	enum drv_crypto_direction direction;
	enum drv_cipher_mode      oprMode;
	u8                   dataIn[NIST_CIPHER_AES_MAX_VECTOR_SIZE];
	u8                   dataOut[NIST_CIPHER_AES_MAX_VECTOR_SIZE];
	size_t                    dataInSize;
} FipsCipherData;


struct fips_cmac_ctx {
	u8 key[AES_256_BIT_KEY_SIZE];
	u8 din[NIST_CIPHER_AES_MAX_VECTOR_SIZE];
	u8 mac_res[CC_DIGEST_SIZE_MAX];
};

typedef struct _FipsCmacData {
	enum drv_crypto_direction direction;
	u8                   key[AES_256_BIT_KEY_SIZE];
	size_t                    key_size;
	u8                   data_in[NIST_CIPHER_AES_MAX_VECTOR_SIZE];
	size_t                    data_in_size;
	u8                   mac_res[CC_DIGEST_SIZE_MAX];
	size_t                    mac_res_size;
} FipsCmacData;


struct fips_hash_ctx {
	u8 initial_digest[CC_DIGEST_SIZE_MAX];
	u8 din[NIST_SHA_MSG_SIZE];
	u8 mac_res[CC_DIGEST_SIZE_MAX];
};

typedef struct _FipsHashData {
	enum drv_hash_mode    hash_mode;
	u8               data_in[NIST_SHA_MSG_SIZE];
	size_t                data_in_size;
	u8               mac_res[CC_DIGEST_SIZE_MAX];
} FipsHashData;


/* note that the hmac key length must be equal or less than block size (block size is 64 up to sha256 and 128 for sha384/512) */
struct fips_hmac_ctx {
	u8 initial_digest[CC_DIGEST_SIZE_MAX];
	u8 key[CC_HMAC_BLOCK_SIZE_MAX];
	u8 k0[CC_HMAC_BLOCK_SIZE_MAX];
	u8 digest_bytes_len[HASH_LEN_SIZE];
	u8 tmp_digest[CC_DIGEST_SIZE_MAX];
	u8 din[NIST_HMAC_MSG_SIZE];
	u8 mac_res[CC_DIGEST_SIZE_MAX];
};

typedef struct _FipsHmacData {
	enum drv_hash_mode    hash_mode;
	u8               key[CC_HMAC_BLOCK_SIZE_MAX];
	size_t                key_size;
	u8               data_in[NIST_HMAC_MSG_SIZE];
	size_t                data_in_size;
	u8               mac_res[CC_DIGEST_SIZE_MAX];
} FipsHmacData;


#define FIPS_CCM_B0_A0_ADATA_SIZE   (NIST_AESCCM_IV_SIZE + NIST_AESCCM_IV_SIZE + NIST_AESCCM_ADATA_SIZE)

struct fips_ccm_ctx {
	u8 b0_a0_adata[FIPS_CCM_B0_A0_ADATA_SIZE];
	u8 iv[NIST_AESCCM_IV_SIZE];
	u8 ctr_cnt_0[NIST_AESCCM_IV_SIZE];
	u8 key[CC_AES_KEY_SIZE_MAX];
	u8 din[NIST_AESCCM_TEXT_SIZE];
	u8 dout[NIST_AESCCM_TEXT_SIZE];
	u8 mac_res[NIST_AESCCM_TAG_SIZE];
};

typedef struct _FipsCcmData {
	enum drv_crypto_direction direction;
	u8                   key[CC_AES_KEY_SIZE_MAX];
	size_t                    keySize;
	u8                   nonce[NIST_AESCCM_NONCE_SIZE];
	u8                   adata[NIST_AESCCM_ADATA_SIZE];
	size_t                    adataSize;
	u8                   dataIn[NIST_AESCCM_TEXT_SIZE];
	size_t                    dataInSize;
	u8                   dataOut[NIST_AESCCM_TEXT_SIZE];
	u8                   tagSize;
	u8                   macResOut[NIST_AESCCM_TAG_SIZE];
} FipsCcmData;


struct fips_gcm_ctx {
	u8 adata[NIST_AESGCM_ADATA_SIZE];
	u8 key[CC_AES_KEY_SIZE_MAX];
	u8 hkey[CC_AES_KEY_SIZE_MAX];
	u8 din[NIST_AESGCM_TEXT_SIZE];
	u8 dout[NIST_AESGCM_TEXT_SIZE];
	u8 mac_res[NIST_AESGCM_TAG_SIZE];
	u8 len_block[AES_BLOCK_SIZE];
	u8 iv_inc1[AES_BLOCK_SIZE];
	u8 iv_inc2[AES_BLOCK_SIZE];
};

typedef struct _FipsGcmData {
	enum drv_crypto_direction direction;
	u8                   key[CC_AES_KEY_SIZE_MAX];
	size_t                    keySize;
	u8                   iv[NIST_AESGCM_IV_SIZE];
	u8                   adata[NIST_AESGCM_ADATA_SIZE];
	size_t                    adataSize;
	u8                   dataIn[NIST_AESGCM_TEXT_SIZE];
	size_t                    dataInSize;
	u8                   dataOut[NIST_AESGCM_TEXT_SIZE];
	u8                   tagSize;
	u8                   macResOut[NIST_AESGCM_TAG_SIZE];
} FipsGcmData;


typedef union _fips_ctx {
	struct fips_cipher_ctx cipher;
	struct fips_cmac_ctx cmac;
	struct fips_hash_ctx hash;
	struct fips_hmac_ctx hmac;
	struct fips_ccm_ctx ccm;
	struct fips_gcm_ctx gcm;
} fips_ctx;


/* test data tables */
static const FipsCipherData FipsCipherDataTable[] = {
	/* AES */
	{ 1, NIST_AES_128_KEY, CC_AES_128_BIT_KEY_SIZE, NIST_AES_ECB_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_ECB, NIST_AES_PLAIN_DATA, NIST_AES_128_ECB_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_128_KEY, CC_AES_128_BIT_KEY_SIZE, NIST_AES_ECB_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_ECB, NIST_AES_128_ECB_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_192_KEY, CC_AES_192_BIT_KEY_SIZE, NIST_AES_ECB_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_ECB, NIST_AES_PLAIN_DATA, NIST_AES_192_ECB_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_192_KEY, CC_AES_192_BIT_KEY_SIZE, NIST_AES_ECB_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_ECB, NIST_AES_192_ECB_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_256_KEY, CC_AES_256_BIT_KEY_SIZE, NIST_AES_ECB_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_ECB, NIST_AES_PLAIN_DATA, NIST_AES_256_ECB_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_256_KEY, CC_AES_256_BIT_KEY_SIZE, NIST_AES_ECB_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_ECB, NIST_AES_256_ECB_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_128_KEY, CC_AES_128_BIT_KEY_SIZE, NIST_AES_CBC_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_CBC, NIST_AES_PLAIN_DATA, NIST_AES_128_CBC_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_128_KEY, CC_AES_128_BIT_KEY_SIZE, NIST_AES_CBC_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_CBC, NIST_AES_128_CBC_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_192_KEY, CC_AES_192_BIT_KEY_SIZE, NIST_AES_CBC_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_CBC, NIST_AES_PLAIN_DATA, NIST_AES_192_CBC_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_192_KEY, CC_AES_192_BIT_KEY_SIZE, NIST_AES_CBC_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_CBC, NIST_AES_192_CBC_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_256_KEY, CC_AES_256_BIT_KEY_SIZE, NIST_AES_CBC_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_CBC, NIST_AES_PLAIN_DATA, NIST_AES_256_CBC_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_256_KEY, CC_AES_256_BIT_KEY_SIZE, NIST_AES_CBC_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_CBC, NIST_AES_256_CBC_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_128_KEY, CC_AES_128_BIT_KEY_SIZE, NIST_AES_OFB_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_OFB, NIST_AES_PLAIN_DATA, NIST_AES_128_OFB_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_128_KEY, CC_AES_128_BIT_KEY_SIZE, NIST_AES_OFB_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_OFB, NIST_AES_128_OFB_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_192_KEY, CC_AES_192_BIT_KEY_SIZE, NIST_AES_OFB_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_OFB, NIST_AES_PLAIN_DATA, NIST_AES_192_OFB_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_192_KEY, CC_AES_192_BIT_KEY_SIZE, NIST_AES_OFB_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_OFB, NIST_AES_192_OFB_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_256_KEY, CC_AES_256_BIT_KEY_SIZE, NIST_AES_OFB_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_OFB, NIST_AES_PLAIN_DATA, NIST_AES_256_OFB_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_256_KEY, CC_AES_256_BIT_KEY_SIZE, NIST_AES_OFB_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_OFB, NIST_AES_256_OFB_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_128_KEY, CC_AES_128_BIT_KEY_SIZE, NIST_AES_CTR_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_CTR, NIST_AES_PLAIN_DATA, NIST_AES_128_CTR_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_128_KEY, CC_AES_128_BIT_KEY_SIZE, NIST_AES_CTR_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_CTR, NIST_AES_128_CTR_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_192_KEY, CC_AES_192_BIT_KEY_SIZE, NIST_AES_CTR_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_CTR, NIST_AES_PLAIN_DATA, NIST_AES_192_CTR_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_192_KEY, CC_AES_192_BIT_KEY_SIZE, NIST_AES_CTR_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_CTR, NIST_AES_192_CTR_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_256_KEY, CC_AES_256_BIT_KEY_SIZE, NIST_AES_CTR_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_CTR, NIST_AES_PLAIN_DATA, NIST_AES_256_CTR_CIPHER, NIST_AES_VECTOR_SIZE },
	{ 1, NIST_AES_256_KEY, CC_AES_256_BIT_KEY_SIZE, NIST_AES_CTR_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_CTR, NIST_AES_256_CTR_CIPHER, NIST_AES_PLAIN_DATA, NIST_AES_VECTOR_SIZE },
	{ 1, RFC3962_AES_128_KEY,  CC_AES_128_BIT_KEY_SIZE, RFC3962_AES_CBC_CTS_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_CBC_CTS, RFC3962_AES_PLAIN_DATA, RFC3962_AES_128_CBC_CTS_CIPHER, RFC3962_AES_VECTOR_SIZE },
	{ 1, RFC3962_AES_128_KEY,  CC_AES_128_BIT_KEY_SIZE, RFC3962_AES_CBC_CTS_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_CBC_CTS, RFC3962_AES_128_CBC_CTS_CIPHER, RFC3962_AES_PLAIN_DATA, RFC3962_AES_VECTOR_SIZE },
	{ 1, NIST_AES_256_XTS_KEY, CC_AES_256_BIT_KEY_SIZE,   NIST_AES_256_XTS_IV,  DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_XTS,     NIST_AES_256_XTS_PLAIN, NIST_AES_256_XTS_CIPHER, NIST_AES_256_XTS_VECTOR_SIZE },
	{ 1, NIST_AES_256_XTS_KEY, CC_AES_256_BIT_KEY_SIZE,   NIST_AES_256_XTS_IV,  DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_XTS,     NIST_AES_256_XTS_CIPHER, NIST_AES_256_XTS_PLAIN, NIST_AES_256_XTS_VECTOR_SIZE },
#if (CC_SUPPORT_SHA > 256)
	{ 1, NIST_AES_512_XTS_KEY, 2*CC_AES_256_BIT_KEY_SIZE, NIST_AES_512_XTS_IV,  DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_XTS,     NIST_AES_512_XTS_PLAIN, NIST_AES_512_XTS_CIPHER, NIST_AES_512_XTS_VECTOR_SIZE },
	{ 1, NIST_AES_512_XTS_KEY, 2*CC_AES_256_BIT_KEY_SIZE, NIST_AES_512_XTS_IV,  DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_XTS,     NIST_AES_512_XTS_CIPHER, NIST_AES_512_XTS_PLAIN, NIST_AES_512_XTS_VECTOR_SIZE },
#endif
	/* DES */
	{ 0, NIST_TDES_ECB3_KEY, CC_DRV_DES_TRIPLE_KEY_SIZE, NIST_TDES_ECB_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_ECB, NIST_TDES_ECB3_PLAIN_DATA, NIST_TDES_ECB3_CIPHER, NIST_TDES_VECTOR_SIZE },
	{ 0, NIST_TDES_ECB3_KEY, CC_DRV_DES_TRIPLE_KEY_SIZE, NIST_TDES_ECB_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_ECB, NIST_TDES_ECB3_CIPHER, NIST_TDES_ECB3_PLAIN_DATA, NIST_TDES_VECTOR_SIZE },
	{ 0, NIST_TDES_CBC3_KEY, CC_DRV_DES_TRIPLE_KEY_SIZE, NIST_TDES_CBC3_IV, DRV_CRYPTO_DIRECTION_ENCRYPT, DRV_CIPHER_CBC, NIST_TDES_CBC3_PLAIN_DATA, NIST_TDES_CBC3_CIPHER, NIST_TDES_VECTOR_SIZE },
	{ 0, NIST_TDES_CBC3_KEY, CC_DRV_DES_TRIPLE_KEY_SIZE, NIST_TDES_CBC3_IV, DRV_CRYPTO_DIRECTION_DECRYPT, DRV_CIPHER_CBC, NIST_TDES_CBC3_CIPHER, NIST_TDES_CBC3_PLAIN_DATA, NIST_TDES_VECTOR_SIZE },
};
#define FIPS_CIPHER_NUM_OF_TESTS        (sizeof(FipsCipherDataTable) / sizeof(FipsCipherData))

static const FipsCmacData FipsCmacDataTable[] = {
	{ DRV_CRYPTO_DIRECTION_ENCRYPT, NIST_AES_128_CMAC_KEY, AES_128_BIT_KEY_SIZE, NIST_AES_128_CMAC_PLAIN_DATA, NIST_AES_128_CMAC_VECTOR_SIZE, NIST_AES_128_CMAC_MAC, NIST_AES_128_CMAC_OUTPUT_SIZE },
	{ DRV_CRYPTO_DIRECTION_ENCRYPT, NIST_AES_192_CMAC_KEY, AES_192_BIT_KEY_SIZE, NIST_AES_192_CMAC_PLAIN_DATA, NIST_AES_192_CMAC_VECTOR_SIZE, NIST_AES_192_CMAC_MAC, NIST_AES_192_CMAC_OUTPUT_SIZE },
	{ DRV_CRYPTO_DIRECTION_ENCRYPT, NIST_AES_256_CMAC_KEY, AES_256_BIT_KEY_SIZE, NIST_AES_256_CMAC_PLAIN_DATA, NIST_AES_256_CMAC_VECTOR_SIZE, NIST_AES_256_CMAC_MAC, NIST_AES_256_CMAC_OUTPUT_SIZE },
};
#define FIPS_CMAC_NUM_OF_TESTS        (sizeof(FipsCmacDataTable) / sizeof(FipsCmacData))

static const FipsHashData FipsHashDataTable[] = {
        { DRV_HASH_SHA1,   NIST_SHA_1_MSG,   NIST_SHA_MSG_SIZE, NIST_SHA_1_MD },
        { DRV_HASH_SHA256, NIST_SHA_256_MSG, NIST_SHA_MSG_SIZE, NIST_SHA_256_MD },
#if (CC_SUPPORT_SHA > 256)
//        { DRV_HASH_SHA512, NIST_SHA_512_MSG, NIST_SHA_MSG_SIZE, NIST_SHA_512_MD },
#endif
};
#define FIPS_HASH_NUM_OF_TESTS        (sizeof(FipsHashDataTable) / sizeof(FipsHashData))

static const FipsHmacData FipsHmacDataTable[] = {
        { DRV_HASH_SHA1,   NIST_HMAC_SHA1_KEY,   NIST_HMAC_SHA1_KEY_SIZE,   NIST_HMAC_SHA1_MSG,   NIST_HMAC_MSG_SIZE, NIST_HMAC_SHA1_MD },
        { DRV_HASH_SHA256, NIST_HMAC_SHA256_KEY, NIST_HMAC_SHA256_KEY_SIZE, NIST_HMAC_SHA256_MSG, NIST_HMAC_MSG_SIZE, NIST_HMAC_SHA256_MD },
#if (CC_SUPPORT_SHA > 256)
//        { DRV_HASH_SHA512, NIST_HMAC_SHA512_KEY, NIST_HMAC_SHA512_KEY_SIZE, NIST_HMAC_SHA512_MSG, NIST_HMAC_MSG_SIZE, NIST_HMAC_SHA512_MD },
#endif
};
#define FIPS_HMAC_NUM_OF_TESTS        (sizeof(FipsHmacDataTable) / sizeof(FipsHmacData))

static const FipsCcmData FipsCcmDataTable[] = {
        { DRV_CRYPTO_DIRECTION_ENCRYPT, NIST_AESCCM_128_KEY, NIST_AESCCM_128_BIT_KEY_SIZE, NIST_AESCCM_128_NONCE, NIST_AESCCM_128_ADATA, NIST_AESCCM_ADATA_SIZE, NIST_AESCCM_128_PLAIN_TEXT, NIST_AESCCM_TEXT_SIZE, NIST_AESCCM_128_CIPHER, NIST_AESCCM_TAG_SIZE, NIST_AESCCM_128_MAC },
        { DRV_CRYPTO_DIRECTION_DECRYPT, NIST_AESCCM_128_KEY, NIST_AESCCM_128_BIT_KEY_SIZE, NIST_AESCCM_128_NONCE, NIST_AESCCM_128_ADATA, NIST_AESCCM_ADATA_SIZE, NIST_AESCCM_128_CIPHER, NIST_AESCCM_TEXT_SIZE, NIST_AESCCM_128_PLAIN_TEXT, NIST_AESCCM_TAG_SIZE, NIST_AESCCM_128_MAC },
        { DRV_CRYPTO_DIRECTION_ENCRYPT, NIST_AESCCM_192_KEY, NIST_AESCCM_192_BIT_KEY_SIZE, NIST_AESCCM_192_NONCE, NIST_AESCCM_192_ADATA, NIST_AESCCM_ADATA_SIZE, NIST_AESCCM_192_PLAIN_TEXT, NIST_AESCCM_TEXT_SIZE, NIST_AESCCM_192_CIPHER, NIST_AESCCM_TAG_SIZE, NIST_AESCCM_192_MAC },
        { DRV_CRYPTO_DIRECTION_DECRYPT, NIST_AESCCM_192_KEY, NIST_AESCCM_192_BIT_KEY_SIZE, NIST_AESCCM_192_NONCE, NIST_AESCCM_192_ADATA, NIST_AESCCM_ADATA_SIZE, NIST_AESCCM_192_CIPHER, NIST_AESCCM_TEXT_SIZE, NIST_AESCCM_192_PLAIN_TEXT, NIST_AESCCM_TAG_SIZE, NIST_AESCCM_192_MAC },
        { DRV_CRYPTO_DIRECTION_ENCRYPT, NIST_AESCCM_256_KEY, NIST_AESCCM_256_BIT_KEY_SIZE, NIST_AESCCM_256_NONCE, NIST_AESCCM_256_ADATA, NIST_AESCCM_ADATA_SIZE, NIST_AESCCM_256_PLAIN_TEXT, NIST_AESCCM_TEXT_SIZE, NIST_AESCCM_256_CIPHER, NIST_AESCCM_TAG_SIZE, NIST_AESCCM_256_MAC },
        { DRV_CRYPTO_DIRECTION_DECRYPT, NIST_AESCCM_256_KEY, NIST_AESCCM_256_BIT_KEY_SIZE, NIST_AESCCM_256_NONCE, NIST_AESCCM_256_ADATA, NIST_AESCCM_ADATA_SIZE, NIST_AESCCM_256_CIPHER, NIST_AESCCM_TEXT_SIZE, NIST_AESCCM_256_PLAIN_TEXT, NIST_AESCCM_TAG_SIZE, NIST_AESCCM_256_MAC },
};
#define FIPS_CCM_NUM_OF_TESTS        (sizeof(FipsCcmDataTable) / sizeof(FipsCcmData))

static const FipsGcmData FipsGcmDataTable[] = {
        { DRV_CRYPTO_DIRECTION_ENCRYPT, NIST_AESGCM_128_KEY, NIST_AESGCM_128_BIT_KEY_SIZE, NIST_AESGCM_128_IV, NIST_AESGCM_128_ADATA, NIST_AESGCM_ADATA_SIZE, NIST_AESGCM_128_PLAIN_TEXT, NIST_AESGCM_TEXT_SIZE, NIST_AESGCM_128_CIPHER, NIST_AESGCM_TAG_SIZE, NIST_AESGCM_128_MAC },
        { DRV_CRYPTO_DIRECTION_DECRYPT, NIST_AESGCM_128_KEY, NIST_AESGCM_128_BIT_KEY_SIZE, NIST_AESGCM_128_IV, NIST_AESGCM_128_ADATA, NIST_AESGCM_ADATA_SIZE, NIST_AESGCM_128_CIPHER, NIST_AESGCM_TEXT_SIZE, NIST_AESGCM_128_PLAIN_TEXT, NIST_AESGCM_TAG_SIZE, NIST_AESGCM_128_MAC },
        { DRV_CRYPTO_DIRECTION_ENCRYPT, NIST_AESGCM_192_KEY, NIST_AESGCM_192_BIT_KEY_SIZE, NIST_AESGCM_192_IV, NIST_AESGCM_192_ADATA, NIST_AESGCM_ADATA_SIZE, NIST_AESGCM_192_PLAIN_TEXT, NIST_AESGCM_TEXT_SIZE, NIST_AESGCM_192_CIPHER, NIST_AESGCM_TAG_SIZE, NIST_AESGCM_192_MAC },
        { DRV_CRYPTO_DIRECTION_DECRYPT, NIST_AESGCM_192_KEY, NIST_AESGCM_192_BIT_KEY_SIZE, NIST_AESGCM_192_IV, NIST_AESGCM_192_ADATA, NIST_AESGCM_ADATA_SIZE, NIST_AESGCM_192_CIPHER, NIST_AESGCM_TEXT_SIZE, NIST_AESGCM_192_PLAIN_TEXT, NIST_AESGCM_TAG_SIZE, NIST_AESGCM_192_MAC },
        { DRV_CRYPTO_DIRECTION_ENCRYPT, NIST_AESGCM_256_KEY, NIST_AESGCM_256_BIT_KEY_SIZE, NIST_AESGCM_256_IV, NIST_AESGCM_256_ADATA, NIST_AESGCM_ADATA_SIZE, NIST_AESGCM_256_PLAIN_TEXT, NIST_AESGCM_TEXT_SIZE, NIST_AESGCM_256_CIPHER, NIST_AESGCM_TAG_SIZE, NIST_AESGCM_256_MAC },
        { DRV_CRYPTO_DIRECTION_DECRYPT, NIST_AESGCM_256_KEY, NIST_AESGCM_256_BIT_KEY_SIZE, NIST_AESGCM_256_IV, NIST_AESGCM_256_ADATA, NIST_AESGCM_ADATA_SIZE, NIST_AESGCM_256_CIPHER, NIST_AESGCM_TEXT_SIZE, NIST_AESGCM_256_PLAIN_TEXT, NIST_AESGCM_TAG_SIZE, NIST_AESGCM_256_MAC },
};
#define FIPS_GCM_NUM_OF_TESTS        (sizeof(FipsGcmDataTable) / sizeof(FipsGcmData))


static inline ssi_fips_error_t
FIPS_CipherToFipsError(enum drv_cipher_mode mode, bool is_aes)
{
	switch (mode)
	{
	case DRV_CIPHER_ECB:
		return is_aes ? CC_REE_FIPS_ERROR_AES_ECB_PUT : CC_REE_FIPS_ERROR_DES_ECB_PUT ;
	case DRV_CIPHER_CBC:
		return is_aes ? CC_REE_FIPS_ERROR_AES_CBC_PUT : CC_REE_FIPS_ERROR_DES_CBC_PUT ;
	case DRV_CIPHER_OFB:
		return CC_REE_FIPS_ERROR_AES_OFB_PUT;
	case DRV_CIPHER_CTR:
		return CC_REE_FIPS_ERROR_AES_CTR_PUT;
	case DRV_CIPHER_CBC_CTS:
		return CC_REE_FIPS_ERROR_AES_CBC_CTS_PUT;
	case DRV_CIPHER_XTS:
		return CC_REE_FIPS_ERROR_AES_XTS_PUT;
	default:
		return CC_REE_FIPS_ERROR_GENERAL;
	}

	return CC_REE_FIPS_ERROR_GENERAL;
}


static inline int
ssi_cipher_fips_run_test(struct ssi_drvdata *drvdata,
			 bool is_aes,
			 int cipher_mode,
			 int direction,
			 dma_addr_t key_dma_addr,
			 size_t key_len,
			 dma_addr_t iv_dma_addr,
			 size_t iv_len,
			 dma_addr_t din_dma_addr,
			 dma_addr_t dout_dma_addr,
			 size_t data_size)
{
	/* max number of descriptors used for the flow */
	#define FIPS_CIPHER_MAX_SEQ_LEN 6

	int rc;
	struct ssi_crypto_req ssi_req = {0};
	struct cc_hw_desc desc[FIPS_CIPHER_MAX_SEQ_LEN];
	int idx = 0;
	int s_flow_mode = is_aes ? S_DIN_to_AES : S_DIN_to_DES;

	/* create setup descriptors */
	switch (cipher_mode) {
	case DRV_CIPHER_CBC:
	case DRV_CIPHER_CBC_CTS:
	case DRV_CIPHER_CTR:
	case DRV_CIPHER_OFB:
		/* Load cipher state */
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
				     iv_dma_addr, iv_len, NS_BIT);
		HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], direction);
		HW_DESC_SET_FLOW_MODE(&desc[idx], s_flow_mode);
		HW_DESC_SET_CIPHER_MODE(&desc[idx], cipher_mode);
		if ((cipher_mode == DRV_CIPHER_CTR) ||
		    (cipher_mode == DRV_CIPHER_OFB) ) {
			HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE1);
		} else {
			HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE0);
		}
		idx++;
		/*FALLTHROUGH*/
	case DRV_CIPHER_ECB:
		/* Load key */
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_CIPHER_MODE(&desc[idx], cipher_mode);
		HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], direction);
		if (is_aes) {
			HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
					     key_dma_addr,
					     ((key_len == 24) ? AES_MAX_KEY_SIZE : key_len),
					     NS_BIT);
			HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_len);
		} else {/*des*/
			HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
					     key_dma_addr, key_len,
					     NS_BIT);
			HW_DESC_SET_KEY_SIZE_DES(&desc[idx], key_len);
		}
		HW_DESC_SET_FLOW_MODE(&desc[idx], s_flow_mode);
		HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
		idx++;
		break;
	case DRV_CIPHER_XTS:
		/* Load AES key */
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_CIPHER_MODE(&desc[idx], cipher_mode);
		HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], direction);
		HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
				     key_dma_addr, key_len/2, NS_BIT);
		HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_len/2);
		HW_DESC_SET_FLOW_MODE(&desc[idx], s_flow_mode);
		HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
		idx++;

		/* load XEX key */
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_CIPHER_MODE(&desc[idx], cipher_mode);
		HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], direction);
		HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
				     (key_dma_addr+key_len/2), key_len/2, NS_BIT);
		HW_DESC_SET_XEX_DATA_UNIT_SIZE(&desc[idx], data_size);
		HW_DESC_SET_FLOW_MODE(&desc[idx], s_flow_mode);
		HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_len/2);
		HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_XEX_KEY);
		idx++;

		/* Set state */
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE1);
		HW_DESC_SET_CIPHER_MODE(&desc[idx], cipher_mode);
		HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], direction);
		HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_len/2);
		HW_DESC_SET_FLOW_MODE(&desc[idx], s_flow_mode);
		HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
				     iv_dma_addr, CC_AES_BLOCK_SIZE, NS_BIT);
		idx++;
		break;
	default:
		FIPS_LOG("Unsupported cipher mode (%d)\n", cipher_mode);
		BUG();
	}

	/* create data descriptor */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, din_dma_addr, data_size, NS_BIT);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], dout_dma_addr, data_size, NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], is_aes ? DIN_AES_DOUT : DIN_DES_DOUT);
	idx++;

	/* perform the operation - Lock HW and push sequence */
	BUG_ON(idx > FIPS_CIPHER_MAX_SEQ_LEN);
	rc = send_request(drvdata, &ssi_req, desc, idx, false);

	// send_request returns error just in some corner cases which should not appear in this flow.
	return rc;
}


ssi_fips_error_t
ssi_cipher_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer)
{
	ssi_fips_error_t error = CC_REE_FIPS_ERROR_OK;
	size_t i;
	struct fips_cipher_ctx *virt_ctx = (struct fips_cipher_ctx *)cpu_addr_buffer;

	/* set the phisical pointers for iv, key, din, dout */
	dma_addr_t iv_dma_addr = dma_coherent_buffer + offsetof(struct fips_cipher_ctx, iv);
	dma_addr_t key_dma_addr = dma_coherent_buffer + offsetof(struct fips_cipher_ctx, key);
	dma_addr_t din_dma_addr = dma_coherent_buffer + offsetof(struct fips_cipher_ctx, din);
	dma_addr_t dout_dma_addr = dma_coherent_buffer + offsetof(struct fips_cipher_ctx, dout);

	for (i = 0; i < FIPS_CIPHER_NUM_OF_TESTS; ++i)
	{
		FipsCipherData *cipherData = (FipsCipherData*)&FipsCipherDataTable[i];
		int rc = 0;
		size_t iv_size = cipherData->isAes ? NIST_AES_IV_SIZE : NIST_TDES_IV_SIZE ;

		memset(cpu_addr_buffer, 0, sizeof(struct fips_cipher_ctx));

		/* copy into the allocated buffer */
		memcpy(virt_ctx->iv, cipherData->iv, iv_size);
		memcpy(virt_ctx->key, cipherData->key, cipherData->keySize);
		memcpy(virt_ctx->din, cipherData->dataIn, cipherData->dataInSize);

		FIPS_DBG("ssi_cipher_fips_run_test -  (i = %d) \n", i);
		rc = ssi_cipher_fips_run_test(drvdata,
					      cipherData->isAes,
					      cipherData->oprMode,
					      cipherData->direction,
					      key_dma_addr,
					      cipherData->keySize,
					      iv_dma_addr,
					      iv_size,
					      din_dma_addr,
					      dout_dma_addr,
					      cipherData->dataInSize);
		if (rc != 0)
		{
			FIPS_LOG("ssi_cipher_fips_run_test %d returned error - rc = %d \n", i, rc);
			error = FIPS_CipherToFipsError(cipherData->oprMode, cipherData->isAes);
			break;
		}

		/* compare actual dout to expected */
		if (memcmp(virt_ctx->dout, cipherData->dataOut, cipherData->dataInSize) != 0)
		{
			FIPS_LOG("dout comparison error %d - oprMode=%d, isAes=%d\n", i, cipherData->oprMode, cipherData->isAes);
			FIPS_LOG("  i  expected   received \n");
			FIPS_LOG("  i  0x%08x 0x%08x  (size=%d) \n", (size_t)cipherData->dataOut, (size_t)virt_ctx->dout, cipherData->dataInSize);
			for (i = 0; i < cipherData->dataInSize; ++i)
			{
				FIPS_LOG("  %d    0x%02x     0x%02x \n", i, cipherData->dataOut[i], virt_ctx->dout[i]);
			}

			error = FIPS_CipherToFipsError(cipherData->oprMode, cipherData->isAes);
			break;
		}
	}

	return error;
}


static inline int
ssi_cmac_fips_run_test(struct ssi_drvdata *drvdata,
		       dma_addr_t key_dma_addr,
		       size_t key_len,
		       dma_addr_t din_dma_addr,
		       size_t din_len,
		       dma_addr_t digest_dma_addr,
		       size_t digest_len)
{
	/* max number of descriptors used for the flow */
	#define FIPS_CMAC_MAX_SEQ_LEN 4

	int rc;
	struct ssi_crypto_req ssi_req = {0};
	struct cc_hw_desc desc[FIPS_CMAC_MAX_SEQ_LEN];
	int idx = 0;

	/* Setup CMAC Key */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, key_dma_addr,
			     ((key_len == 24) ? AES_MAX_KEY_SIZE : key_len), NS_BIT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_CMAC);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_len);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Load MAC state */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, digest_dma_addr, CC_AES_BLOCK_SIZE, NS_BIT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE0);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_CMAC);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_len);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_AES);
	idx++;


	//ssi_hash_create_data_desc(state, ctx, DIN_AES_DOUT, desc, false, &idx);
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     din_dma_addr,
			     din_len, NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_AES_DOUT);
	idx++;

	/* Get final MAC result */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], digest_dma_addr, CC_AES_BLOCK_SIZE, NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_AES_to_DOUT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_WRITE_STATE0);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_CMAC);
	idx++;

	/* perform the operation - Lock HW and push sequence */
	BUG_ON(idx > FIPS_CMAC_MAX_SEQ_LEN);
	rc = send_request(drvdata, &ssi_req, desc, idx, false);

	// send_request returns error just in some corner cases which should not appear in this flow.
	return rc;
}

ssi_fips_error_t
ssi_cmac_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer)
{
	ssi_fips_error_t error = CC_REE_FIPS_ERROR_OK;
	size_t i;
	struct fips_cmac_ctx *virt_ctx = (struct fips_cmac_ctx *)cpu_addr_buffer;

	/* set the phisical pointers for key, din, dout */
	dma_addr_t key_dma_addr = dma_coherent_buffer + offsetof(struct fips_cmac_ctx, key);
	dma_addr_t din_dma_addr = dma_coherent_buffer + offsetof(struct fips_cmac_ctx, din);
	dma_addr_t mac_res_dma_addr = dma_coherent_buffer + offsetof(struct fips_cmac_ctx, mac_res);

	for (i = 0; i < FIPS_CMAC_NUM_OF_TESTS; ++i)
	{
		FipsCmacData *cmac_data = (FipsCmacData*)&FipsCmacDataTable[i];
		int rc = 0;

		memset(cpu_addr_buffer, 0, sizeof(struct fips_cmac_ctx));

		/* copy into the allocated buffer */
		memcpy(virt_ctx->key, cmac_data->key, cmac_data->key_size);
		memcpy(virt_ctx->din, cmac_data->data_in, cmac_data->data_in_size);

		BUG_ON(cmac_data->direction != DRV_CRYPTO_DIRECTION_ENCRYPT);

		FIPS_DBG("ssi_cmac_fips_run_test -  (i = %d) \n", i);
		rc = ssi_cmac_fips_run_test(drvdata,
					    key_dma_addr,
					    cmac_data->key_size,
					    din_dma_addr,
					    cmac_data->data_in_size,
					    mac_res_dma_addr,
					    cmac_data->mac_res_size);
		if (rc != 0)
		{
			FIPS_LOG("ssi_cmac_fips_run_test %d returned error - rc = %d \n", i, rc);
			error = CC_REE_FIPS_ERROR_AES_CMAC_PUT;
			break;
		}

		/* compare actual mac result to expected */
		if (memcmp(virt_ctx->mac_res, cmac_data->mac_res, cmac_data->mac_res_size) != 0)
		{
			FIPS_LOG("comparison error %d - digest_size=%d \n", i, cmac_data->mac_res_size);
			FIPS_LOG("  i  expected   received \n");
			FIPS_LOG("  i  0x%08x 0x%08x \n", (size_t)cmac_data->mac_res, (size_t)virt_ctx->mac_res);
			for (i = 0; i < cmac_data->mac_res_size; ++i)
			{
				FIPS_LOG("  %d    0x%02x     0x%02x \n", i, cmac_data->mac_res[i], virt_ctx->mac_res[i]);
			}

			error = CC_REE_FIPS_ERROR_AES_CMAC_PUT;
			break;
		}
	}

	return error;
}


static inline ssi_fips_error_t
FIPS_HashToFipsError(enum drv_hash_mode hash_mode)
{
	switch (hash_mode) {
	case DRV_HASH_SHA1:
		return CC_REE_FIPS_ERROR_SHA1_PUT;
	case DRV_HASH_SHA256:
		return CC_REE_FIPS_ERROR_SHA256_PUT;
#if (CC_SUPPORT_SHA > 256)
	case DRV_HASH_SHA512:
		return CC_REE_FIPS_ERROR_SHA512_PUT;
#endif
	default:
		return CC_REE_FIPS_ERROR_GENERAL;
	}

	return CC_REE_FIPS_ERROR_GENERAL;
}

static inline int
ssi_hash_fips_run_test(struct ssi_drvdata *drvdata,
		       dma_addr_t initial_digest_dma_addr,
		       dma_addr_t din_dma_addr,
		       size_t data_in_size,
		       dma_addr_t mac_res_dma_addr,
		       enum drv_hash_mode hash_mode,
		       enum drv_hash_hw_mode hw_mode,
		       int digest_size,
		       int inter_digestsize)
{
	/* max number of descriptors used for the flow */
	#define FIPS_HASH_MAX_SEQ_LEN 4

	int rc;
	struct ssi_crypto_req ssi_req = {0};
	struct cc_hw_desc desc[FIPS_HASH_MAX_SEQ_LEN];
	int idx = 0;

	/* Load initial digest */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, initial_digest_dma_addr, inter_digestsize, NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Load the hash current length */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
	HW_DESC_SET_DIN_CONST(&desc[idx], 0, HASH_LEN_SIZE);
	HW_DESC_SET_CIPHER_CONFIG1(&desc[idx], HASH_PADDING_ENABLED);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	/* data descriptor */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, din_dma_addr, data_in_size, NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_HASH);
	idx++;

	/* Get final MAC result */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], mac_res_dma_addr, digest_size, NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_HASH_to_DOUT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_WRITE_STATE0);
	HW_DESC_SET_CIPHER_CONFIG1(&desc[idx], HASH_PADDING_DISABLED);
	if (unlikely((hash_mode == DRV_HASH_MD5) ||
		     (hash_mode == DRV_HASH_SHA384) ||
		     (hash_mode == DRV_HASH_SHA512))) {
		HW_DESC_SET_BYTES_SWAP(&desc[idx], 1);
	} else {
		HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], HASH_DIGEST_RESULT_LITTLE_ENDIAN);
	}
	idx++;

	/* perform the operation - Lock HW and push sequence */
	BUG_ON(idx > FIPS_HASH_MAX_SEQ_LEN);
	rc = send_request(drvdata, &ssi_req, desc, idx, false);

	return rc;
}

ssi_fips_error_t
ssi_hash_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer)
{
	ssi_fips_error_t error = CC_REE_FIPS_ERROR_OK;
	size_t i;
	struct fips_hash_ctx *virt_ctx = (struct fips_hash_ctx *)cpu_addr_buffer;

	/* set the phisical pointers for initial_digest, din, mac_res */
	dma_addr_t initial_digest_dma_addr = dma_coherent_buffer + offsetof(struct fips_hash_ctx, initial_digest);
	dma_addr_t din_dma_addr = dma_coherent_buffer + offsetof(struct fips_hash_ctx, din);
	dma_addr_t mac_res_dma_addr = dma_coherent_buffer + offsetof(struct fips_hash_ctx, mac_res);

	for (i = 0; i < FIPS_HASH_NUM_OF_TESTS; ++i)
	{
		FipsHashData *hash_data = (FipsHashData*)&FipsHashDataTable[i];
		int rc = 0;
		enum drv_hash_hw_mode hw_mode = 0;
		int digest_size = 0;
		int inter_digestsize = 0;

		memset(cpu_addr_buffer, 0, sizeof(struct fips_hash_ctx));

		switch (hash_data->hash_mode) {
		case DRV_HASH_SHA1:
			hw_mode = DRV_HASH_HW_SHA1;
			digest_size = CC_SHA1_DIGEST_SIZE;
			inter_digestsize = CC_SHA1_DIGEST_SIZE;
			/* copy the initial digest into the allocated cache coherent buffer */
			memcpy(virt_ctx->initial_digest, (void*)sha1_init, CC_SHA1_DIGEST_SIZE);
			break;
		case DRV_HASH_SHA256:
			hw_mode = DRV_HASH_HW_SHA256;
			digest_size = CC_SHA256_DIGEST_SIZE;
			inter_digestsize = CC_SHA256_DIGEST_SIZE;
			memcpy(virt_ctx->initial_digest, (void*)sha256_init, CC_SHA256_DIGEST_SIZE);
			break;
#if (CC_SUPPORT_SHA > 256)
		case DRV_HASH_SHA512:
			hw_mode = DRV_HASH_HW_SHA512;
			digest_size = CC_SHA512_DIGEST_SIZE;
			inter_digestsize = CC_SHA512_DIGEST_SIZE;
			memcpy(virt_ctx->initial_digest, (void*)sha512_init, CC_SHA512_DIGEST_SIZE);
			break;
#endif
		default:
			error = FIPS_HashToFipsError(hash_data->hash_mode);
			break;
		}

		/* copy the din data into the allocated buffer */
		memcpy(virt_ctx->din, hash_data->data_in, hash_data->data_in_size);

		/* run the test on HW */
		FIPS_DBG("ssi_hash_fips_run_test -  (i = %d) \n", i);
		rc = ssi_hash_fips_run_test(drvdata,
					    initial_digest_dma_addr,
					    din_dma_addr,
					    hash_data->data_in_size,
					    mac_res_dma_addr,
					    hash_data->hash_mode,
					    hw_mode,
					    digest_size,
					    inter_digestsize);
		if (rc != 0)
		{
			FIPS_LOG("ssi_hash_fips_run_test %d returned error - rc = %d \n", i, rc);
			error = FIPS_HashToFipsError(hash_data->hash_mode);
			break;
                }

		/* compare actual mac result to expected */
		if (memcmp(virt_ctx->mac_res, hash_data->mac_res, digest_size) != 0)
		{
			FIPS_LOG("comparison error %d - hash_mode=%d digest_size=%d \n", i, hash_data->hash_mode, digest_size);
			FIPS_LOG("  i  expected   received \n");
			FIPS_LOG("  i  0x%08x 0x%08x \n", (size_t)hash_data->mac_res, (size_t)virt_ctx->mac_res);
			for (i = 0; i < digest_size; ++i)
			{
				FIPS_LOG("  %d    0x%02x     0x%02x \n", i, hash_data->mac_res[i], virt_ctx->mac_res[i]);
			}

			error = FIPS_HashToFipsError(hash_data->hash_mode);
			break;
                }
	}

	return error;
}


static inline ssi_fips_error_t
FIPS_HmacToFipsError(enum drv_hash_mode hash_mode)
{
	switch (hash_mode) {
	case DRV_HASH_SHA1:
		return CC_REE_FIPS_ERROR_HMAC_SHA1_PUT;
	case DRV_HASH_SHA256:
		return CC_REE_FIPS_ERROR_HMAC_SHA256_PUT;
#if (CC_SUPPORT_SHA > 256)
	case DRV_HASH_SHA512:
		return CC_REE_FIPS_ERROR_HMAC_SHA512_PUT;
#endif
	default:
		return CC_REE_FIPS_ERROR_GENERAL;
	}

	return CC_REE_FIPS_ERROR_GENERAL;
}

static inline int
ssi_hmac_fips_run_test(struct ssi_drvdata *drvdata,
		       dma_addr_t initial_digest_dma_addr,
		       dma_addr_t key_dma_addr,
		       size_t key_size,
		       dma_addr_t din_dma_addr,
		       size_t data_in_size,
		       dma_addr_t mac_res_dma_addr,
		       enum drv_hash_mode hash_mode,
		       enum drv_hash_hw_mode hw_mode,
		       size_t digest_size,
		       size_t inter_digestsize,
		       size_t block_size,
		       dma_addr_t k0_dma_addr,
		       dma_addr_t tmp_digest_dma_addr,
		       dma_addr_t digest_bytes_len_dma_addr)
{
	/* The implemented flow is not the same as the one implemented in ssi_hash.c (setkey + digest flows).
	 * In this flow, there is no need to store and reload some of the intermidiate results.
	 */

	/* max number of descriptors used for the flow */
	#define FIPS_HMAC_MAX_SEQ_LEN 12

	int rc;
	struct ssi_crypto_req ssi_req = {0};
	struct cc_hw_desc desc[FIPS_HMAC_MAX_SEQ_LEN];
	int idx = 0;
	int i;
	/* calc the hash opad first and ipad only afterwards (unlike the flow in ssi_hash.c) */
	unsigned int hmacPadConst[2] = { HMAC_OPAD_CONST, HMAC_IPAD_CONST };

	// assume (key_size <= block_size)
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, key_dma_addr, key_size, NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], BYPASS);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], k0_dma_addr, key_size, NS_BIT, 0);
	idx++;

	// if needed, append Key with zeros to create K0
	if ((block_size - key_size) != 0) {
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_DIN_CONST(&desc[idx], 0, (block_size - key_size));
		HW_DESC_SET_FLOW_MODE(&desc[idx], BYPASS);
		HW_DESC_SET_DOUT_DLLI(&desc[idx],
				      (k0_dma_addr + key_size), (block_size - key_size),
				      NS_BIT, 0);
		idx++;
	}

	BUG_ON(idx > FIPS_HMAC_MAX_SEQ_LEN);
	rc = send_request(drvdata, &ssi_req, desc, idx, 0);
	if (unlikely(rc != 0)) {
		SSI_LOG_ERR("send_request() failed (rc=%d)\n", rc);
		return rc;
	}
	idx = 0;

	/* calc derived HMAC key */
	for (i = 0; i < 2; i++) {
		/* Load hash initial state */
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
		HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, initial_digest_dma_addr, inter_digestsize, NS_BIT);
		HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
		HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE0);
		idx++;


		/* Load the hash current length*/
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
		HW_DESC_SET_DIN_CONST(&desc[idx], 0, HASH_LEN_SIZE);
		HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
		HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
		idx++;

		/* Prepare opad/ipad key */
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_XOR_VAL(&desc[idx], hmacPadConst[i]);
		HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
		HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
		HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE1);
		idx++;

		/* Perform HASH update */
		HW_DESC_INIT(&desc[idx]);
		HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
				     k0_dma_addr,
				     block_size, NS_BIT);
		HW_DESC_SET_CIPHER_MODE(&desc[idx],hw_mode);
		HW_DESC_SET_XOR_ACTIVE(&desc[idx]);
		HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_HASH);
		idx++;

		if (i == 0) {
			/* First iteration - calc H(K0^opad) into tmp_digest_dma_addr */
			HW_DESC_INIT(&desc[idx]);
			HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
			HW_DESC_SET_DOUT_DLLI(&desc[idx],
					      tmp_digest_dma_addr,
					      inter_digestsize,
					      NS_BIT, 0);
			HW_DESC_SET_FLOW_MODE(&desc[idx], S_HASH_to_DOUT);
			HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_WRITE_STATE0);
			idx++;

			// is this needed?? or continue with current descriptors??
			BUG_ON(idx > FIPS_HMAC_MAX_SEQ_LEN);
			rc = send_request(drvdata, &ssi_req, desc, idx, 0);
			if (unlikely(rc != 0)) {
				SSI_LOG_ERR("send_request() failed (rc=%d)\n", rc);
				return rc;
			}
			idx = 0;
		}
	}

	/* data descriptor */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     din_dma_addr, data_in_size,
			     NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_HASH);
	idx++;

	/* HW last hash block padding (aka. "DO_PAD") */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], k0_dma_addr, HASH_LEN_SIZE, NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_HASH_to_DOUT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_WRITE_STATE1);
	HW_DESC_SET_CIPHER_DO(&desc[idx], DO_PAD);
	idx++;

	/* store the hash digest result in the context */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], k0_dma_addr, digest_size, NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_HASH_to_DOUT);
	if (unlikely((hash_mode == DRV_HASH_MD5) ||
		     (hash_mode == DRV_HASH_SHA384) ||
		     (hash_mode == DRV_HASH_SHA512))) {
		HW_DESC_SET_BYTES_SWAP(&desc[idx], 1);
	} else {
		HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], HASH_DIGEST_RESULT_LITTLE_ENDIAN);
	}
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_WRITE_STATE0);
	idx++;

	/* at this point:
	 * tmp_digest = H(o_key_pad)
	 * k0 = H(i_key_pad || m)
	 */

	/* Loading hash opad xor key state */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, tmp_digest_dma_addr, inter_digestsize, NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Load the hash current length */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, digest_bytes_len_dma_addr, HASH_LEN_SIZE, NS_BIT);
	HW_DESC_SET_CIPHER_CONFIG1(&desc[idx], HASH_PADDING_ENABLED);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	/* Memory Barrier: wait for IPAD/OPAD axi write to complete */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_NO_DMA(&desc[idx], 0, 0xfffff0);
	HW_DESC_SET_DOUT_NO_DMA(&desc[idx], 0, 0, 1);
	idx++;

	/* Perform HASH update */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, k0_dma_addr, digest_size, NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_HASH);
	idx++;


	/* Get final MAC result */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], hw_mode);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], mac_res_dma_addr, digest_size, NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_HASH_to_DOUT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_WRITE_STATE0);
	HW_DESC_SET_CIPHER_CONFIG1(&desc[idx], HASH_PADDING_DISABLED);
	if (unlikely((hash_mode == DRV_HASH_MD5) ||
		     (hash_mode == DRV_HASH_SHA384) ||
		     (hash_mode == DRV_HASH_SHA512))) {
		HW_DESC_SET_BYTES_SWAP(&desc[idx], 1);
	} else {
		HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], HASH_DIGEST_RESULT_LITTLE_ENDIAN);
	}
	idx++;

	/* perform the operation - Lock HW and push sequence */
	BUG_ON(idx > FIPS_HMAC_MAX_SEQ_LEN);
	rc = send_request(drvdata, &ssi_req, desc, idx, false);

	return rc;
}

ssi_fips_error_t
ssi_hmac_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer)
{
	ssi_fips_error_t error = CC_REE_FIPS_ERROR_OK;
	size_t i;
	struct fips_hmac_ctx *virt_ctx = (struct fips_hmac_ctx *)cpu_addr_buffer;

	/* set the phisical pointers */
	dma_addr_t initial_digest_dma_addr = dma_coherent_buffer + offsetof(struct fips_hmac_ctx, initial_digest);
	dma_addr_t key_dma_addr = dma_coherent_buffer + offsetof(struct fips_hmac_ctx, key);
	dma_addr_t k0_dma_addr = dma_coherent_buffer + offsetof(struct fips_hmac_ctx, k0);
	dma_addr_t tmp_digest_dma_addr = dma_coherent_buffer + offsetof(struct fips_hmac_ctx, tmp_digest);
	dma_addr_t digest_bytes_len_dma_addr = dma_coherent_buffer + offsetof(struct fips_hmac_ctx, digest_bytes_len);
	dma_addr_t din_dma_addr = dma_coherent_buffer + offsetof(struct fips_hmac_ctx, din);
	dma_addr_t mac_res_dma_addr = dma_coherent_buffer + offsetof(struct fips_hmac_ctx, mac_res);

	for (i = 0; i < FIPS_HMAC_NUM_OF_TESTS; ++i)
	{
		FipsHmacData *hmac_data = (FipsHmacData*)&FipsHmacDataTable[i];
		int rc = 0;
		enum drv_hash_hw_mode hw_mode = 0;
		int digest_size = 0;
		int block_size = 0;
		int inter_digestsize = 0;

		memset(cpu_addr_buffer, 0, sizeof(struct fips_hmac_ctx));

		switch (hmac_data->hash_mode) {
		case DRV_HASH_SHA1:
			hw_mode = DRV_HASH_HW_SHA1;
			digest_size = CC_SHA1_DIGEST_SIZE;
			block_size = CC_SHA1_BLOCK_SIZE;
			inter_digestsize = CC_SHA1_DIGEST_SIZE;
			memcpy(virt_ctx->initial_digest, (void*)sha1_init, CC_SHA1_DIGEST_SIZE);
			memcpy(virt_ctx->digest_bytes_len, digest_len_init, HASH_LEN_SIZE);
			break;
		case DRV_HASH_SHA256:
			hw_mode = DRV_HASH_HW_SHA256;
			digest_size = CC_SHA256_DIGEST_SIZE;
			block_size = CC_SHA256_BLOCK_SIZE;
			inter_digestsize = CC_SHA256_DIGEST_SIZE;
			memcpy(virt_ctx->initial_digest, (void*)sha256_init, CC_SHA256_DIGEST_SIZE);
			memcpy(virt_ctx->digest_bytes_len, digest_len_init, HASH_LEN_SIZE);
			break;
#if (CC_SUPPORT_SHA > 256)
		case DRV_HASH_SHA512:
			hw_mode = DRV_HASH_HW_SHA512;
			digest_size = CC_SHA512_DIGEST_SIZE;
			block_size = CC_SHA512_BLOCK_SIZE;
			inter_digestsize = CC_SHA512_DIGEST_SIZE;
			memcpy(virt_ctx->initial_digest, (void*)sha512_init, CC_SHA512_DIGEST_SIZE);
			memcpy(virt_ctx->digest_bytes_len, digest_len_sha512_init, HASH_LEN_SIZE);
			break;
#endif
		default:
			error = FIPS_HmacToFipsError(hmac_data->hash_mode);
			break;
		}

		/* copy into the allocated buffer */
		memcpy(virt_ctx->key, hmac_data->key, hmac_data->key_size);
		memcpy(virt_ctx->din, hmac_data->data_in, hmac_data->data_in_size);

		/* run the test on HW */
		FIPS_DBG("ssi_hmac_fips_run_test -  (i = %d) \n", i);
		rc = ssi_hmac_fips_run_test(drvdata,
					    initial_digest_dma_addr,
					    key_dma_addr,
					    hmac_data->key_size,
					    din_dma_addr,
					    hmac_data->data_in_size,
					    mac_res_dma_addr,
					    hmac_data->hash_mode,
					    hw_mode,
					    digest_size,
					    inter_digestsize,
					    block_size,
					    k0_dma_addr,
					    tmp_digest_dma_addr,
					    digest_bytes_len_dma_addr);
		if (rc != 0)
		{
			FIPS_LOG("ssi_hmac_fips_run_test %d returned error - rc = %d \n", i, rc);
			error = FIPS_HmacToFipsError(hmac_data->hash_mode);
			break;
		}

		/* compare actual mac result to expected */
		if (memcmp(virt_ctx->mac_res, hmac_data->mac_res, digest_size) != 0)
		{
			FIPS_LOG("comparison error %d - hash_mode=%d digest_size=%d \n", i, hmac_data->hash_mode, digest_size);
			FIPS_LOG("  i  expected   received \n");
			FIPS_LOG("  i  0x%08x 0x%08x \n", (size_t)hmac_data->mac_res, (size_t)virt_ctx->mac_res);
			for (i = 0; i < digest_size; ++i)
			{
				FIPS_LOG("  %d    0x%02x     0x%02x \n", i, hmac_data->mac_res[i], virt_ctx->mac_res[i]);
			}

			error = FIPS_HmacToFipsError(hmac_data->hash_mode);
			break;
		}
	}

	return error;
}


static inline int
ssi_ccm_fips_run_test(struct ssi_drvdata *drvdata,
		      enum drv_crypto_direction direction,
		      dma_addr_t key_dma_addr,
		      size_t key_size,
		      dma_addr_t iv_dma_addr,
		      dma_addr_t ctr_cnt_0_dma_addr,
		      dma_addr_t b0_a0_adata_dma_addr,
		      size_t b0_a0_adata_size,
		      dma_addr_t din_dma_addr,
		      size_t din_size,
		      dma_addr_t dout_dma_addr,
		      dma_addr_t mac_res_dma_addr)
{
	/* max number of descriptors used for the flow */
	#define FIPS_CCM_MAX_SEQ_LEN 10

	int rc;
	struct ssi_crypto_req ssi_req = {0};
	struct cc_hw_desc desc[FIPS_CCM_MAX_SEQ_LEN];
	unsigned int idx = 0;
	unsigned int cipher_flow_mode;

	if (direction == DRV_CRYPTO_DIRECTION_DECRYPT) {
		cipher_flow_mode = AES_to_HASH_and_DOUT;
	} else { /* Encrypt */
		cipher_flow_mode = AES_and_HASH;
	}

	/* load key */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_CTR);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, key_dma_addr,
			     ((key_size == NIST_AESCCM_192_BIT_KEY_SIZE) ? CC_AES_KEY_SIZE_MAX : key_size),
			     NS_BIT);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_size);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_AES);
	idx++;

	/* load ctr state */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_CTR);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_size);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     iv_dma_addr, AES_BLOCK_SIZE,
			     NS_BIT);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE1);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_AES);
	idx++;

	/* load MAC key */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_CBC_MAC);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, key_dma_addr,
			     ((key_size == NIST_AESCCM_192_BIT_KEY_SIZE) ? CC_AES_KEY_SIZE_MAX : key_size),
			     NS_BIT);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_size);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
	HW_DESC_SET_AES_NOT_HASH_MODE(&desc[idx]);
	idx++;

	/* load MAC state */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_CBC_MAC);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_size);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, mac_res_dma_addr, NIST_AESCCM_TAG_SIZE, NS_BIT);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
	HW_DESC_SET_AES_NOT_HASH_MODE(&desc[idx]);
	idx++;

	/* prcess assoc data */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, b0_a0_adata_dma_addr, b0_a0_adata_size, NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_HASH);
	idx++;

	/* process the cipher */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, din_dma_addr, din_size, NS_BIT);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], dout_dma_addr, din_size, NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], cipher_flow_mode);
	idx++;

	/* Read temporal MAC */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_CBC_MAC);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], mac_res_dma_addr, NIST_AESCCM_TAG_SIZE, NS_BIT, 0);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_WRITE_STATE0);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], HASH_DIGEST_RESULT_LITTLE_ENDIAN);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_HASH_to_DOUT);
	HW_DESC_SET_AES_NOT_HASH_MODE(&desc[idx]);
	idx++;

	/* load AES-CTR state (for last MAC calculation)*/
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_CTR);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     ctr_cnt_0_dma_addr,
			     AES_BLOCK_SIZE, NS_BIT);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_size);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE1);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Memory Barrier */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_NO_DMA(&desc[idx], 0, 0xfffff0);
	HW_DESC_SET_DOUT_NO_DMA(&desc[idx], 0, 0, 1);
	idx++;

	/* encrypt the "T" value and store MAC inplace */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI, mac_res_dma_addr, NIST_AESCCM_TAG_SIZE, NS_BIT);
	HW_DESC_SET_DOUT_DLLI(&desc[idx], mac_res_dma_addr, NIST_AESCCM_TAG_SIZE, NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_AES_DOUT);
	idx++;

	/* perform the operation - Lock HW and push sequence */
	BUG_ON(idx > FIPS_CCM_MAX_SEQ_LEN);
	rc = send_request(drvdata, &ssi_req, desc, idx, false);

	return rc;
}

ssi_fips_error_t
ssi_ccm_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer)
{
	ssi_fips_error_t error = CC_REE_FIPS_ERROR_OK;
	size_t i;
	struct fips_ccm_ctx *virt_ctx = (struct fips_ccm_ctx *)cpu_addr_buffer;

	/* set the phisical pointers */
	dma_addr_t b0_a0_adata_dma_addr = dma_coherent_buffer + offsetof(struct fips_ccm_ctx, b0_a0_adata);
	dma_addr_t iv_dma_addr = dma_coherent_buffer + offsetof(struct fips_ccm_ctx, iv);
	dma_addr_t ctr_cnt_0_dma_addr = dma_coherent_buffer + offsetof(struct fips_ccm_ctx, ctr_cnt_0);
	dma_addr_t key_dma_addr = dma_coherent_buffer + offsetof(struct fips_ccm_ctx, key);
	dma_addr_t din_dma_addr = dma_coherent_buffer + offsetof(struct fips_ccm_ctx, din);
	dma_addr_t dout_dma_addr = dma_coherent_buffer + offsetof(struct fips_ccm_ctx, dout);
	dma_addr_t mac_res_dma_addr = dma_coherent_buffer + offsetof(struct fips_ccm_ctx, mac_res);

	for (i = 0; i < FIPS_CCM_NUM_OF_TESTS; ++i)
	{
		FipsCcmData *ccmData = (FipsCcmData*)&FipsCcmDataTable[i];
		int rc = 0;

		memset(cpu_addr_buffer, 0, sizeof(struct fips_ccm_ctx));

		/* copy the nonce, key, adata, din data into the allocated buffer */
		memcpy(virt_ctx->key, ccmData->key, ccmData->keySize);
		memcpy(virt_ctx->din, ccmData->dataIn, ccmData->dataInSize);
		{
			/* build B0 -- B0, nonce, l(m) */
			__be16 data = cpu_to_be16(NIST_AESCCM_TEXT_SIZE);
			virt_ctx->b0_a0_adata[0] = NIST_AESCCM_B0_VAL;
			memcpy(virt_ctx->b0_a0_adata + 1, ccmData->nonce, NIST_AESCCM_NONCE_SIZE);
			memcpy(virt_ctx->b0_a0_adata + 14, (u8 *)&data, sizeof(__be16));
			/* build A0+ADATA */
			virt_ctx->b0_a0_adata[NIST_AESCCM_IV_SIZE + 0] = (ccmData->adataSize >> 8) & 0xFF;
			virt_ctx->b0_a0_adata[NIST_AESCCM_IV_SIZE + 1] = ccmData->adataSize & 0xFF;
			memcpy(virt_ctx->b0_a0_adata + NIST_AESCCM_IV_SIZE + 2, ccmData->adata, ccmData->adataSize);
			/* iv */
			virt_ctx->iv[0] = 1; /* L' */
			memcpy(virt_ctx->iv + 1, ccmData->nonce, NIST_AESCCM_NONCE_SIZE);
			virt_ctx->iv[15] = 1;
			/* ctr_count_0 */
			memcpy(virt_ctx->ctr_cnt_0, virt_ctx->iv, NIST_AESCCM_IV_SIZE);
			virt_ctx->ctr_cnt_0[15] = 0;
		}

		FIPS_DBG("ssi_ccm_fips_run_test -  (i = %d) \n", i);
		rc = ssi_ccm_fips_run_test(drvdata,
					   ccmData->direction,
					   key_dma_addr,
					   ccmData->keySize,
					   iv_dma_addr,
					   ctr_cnt_0_dma_addr,
					   b0_a0_adata_dma_addr,
					   FIPS_CCM_B0_A0_ADATA_SIZE,
					   din_dma_addr,
					   ccmData->dataInSize,
					   dout_dma_addr,
					   mac_res_dma_addr);
		if (rc != 0)
		{
			FIPS_LOG("ssi_ccm_fips_run_test %d returned error - rc = %d \n", i, rc);
			error = CC_REE_FIPS_ERROR_AESCCM_PUT;
			break;
		}

		/* compare actual dout to expected */
		if (memcmp(virt_ctx->dout, ccmData->dataOut, ccmData->dataInSize) != 0)
		{
			FIPS_LOG("dout comparison error %d - size=%d \n", i, ccmData->dataInSize);
                        error = CC_REE_FIPS_ERROR_AESCCM_PUT;
			break;
                }

		/* compare actual mac result to expected */
		if (memcmp(virt_ctx->mac_res, ccmData->macResOut, ccmData->tagSize) != 0)
		{
			FIPS_LOG("mac_res comparison error %d - mac_size=%d \n", i, ccmData->tagSize);
			FIPS_LOG("  i  expected   received \n");
			FIPS_LOG("  i  0x%08x 0x%08x \n", (size_t)ccmData->macResOut, (size_t)virt_ctx->mac_res);
			for (i = 0; i < ccmData->tagSize; ++i)
			{
				FIPS_LOG("  %d    0x%02x     0x%02x \n", i, ccmData->macResOut[i], virt_ctx->mac_res[i]);
			}

			error = CC_REE_FIPS_ERROR_AESCCM_PUT;
			break;
		}
	}

	return error;
}


static inline int
ssi_gcm_fips_run_test(struct ssi_drvdata *drvdata,
		      enum drv_crypto_direction direction,
		      dma_addr_t key_dma_addr,
		      size_t key_size,
		      dma_addr_t hkey_dma_addr,
		      dma_addr_t block_len_dma_addr,
		      dma_addr_t iv_inc1_dma_addr,
		      dma_addr_t iv_inc2_dma_addr,
		      dma_addr_t adata_dma_addr,
		      size_t adata_size,
		      dma_addr_t din_dma_addr,
		      size_t din_size,
		      dma_addr_t dout_dma_addr,
		      dma_addr_t mac_res_dma_addr)
{
	/* max number of descriptors used for the flow */
	#define FIPS_GCM_MAX_SEQ_LEN 15

	int rc;
	struct ssi_crypto_req ssi_req = {0};
	struct cc_hw_desc desc[FIPS_GCM_MAX_SEQ_LEN];
	unsigned int idx = 0;
	unsigned int cipher_flow_mode;

	if (direction == DRV_CRYPTO_DIRECTION_DECRYPT) {
		cipher_flow_mode = AES_and_HASH;
	} else { /* Encrypt */
		cipher_flow_mode = AES_to_HASH_and_DOUT;
	}

/////////////////////////////////   1   ////////////////////////////////////
//	ssi_aead_gcm_setup_ghash_desc(req, desc, seq_size);
/////////////////////////////////   1   ////////////////////////////////////

	/* load key to AES*/
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_ECB);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	HW_DESC_SET_DIN_TYPE(&desc[idx],
			     DMA_DLLI, key_dma_addr, key_size,
			     NS_BIT);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_size);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_AES);
	idx++;

	/* process one zero block to generate hkey */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_CONST(&desc[idx], 0x0, AES_BLOCK_SIZE);
	HW_DESC_SET_DOUT_DLLI(&desc[idx],
			      hkey_dma_addr, AES_BLOCK_SIZE,
			      NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_AES_DOUT);
	idx++;

	/* Memory Barrier */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_NO_DMA(&desc[idx], 0, 0xfffff0);
	HW_DESC_SET_DOUT_NO_DMA(&desc[idx], 0, 0, 1);
	idx++;

	/* Load GHASH subkey */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     hkey_dma_addr, AES_BLOCK_SIZE,
			     NS_BIT);
	HW_DESC_SET_DOUT_NO_DMA(&desc[idx], 0, 0, 1);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
	HW_DESC_SET_AES_NOT_HASH_MODE(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_HASH_HW_GHASH);
	HW_DESC_SET_CIPHER_CONFIG1(&desc[idx], HASH_PADDING_ENABLED);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	/* Configure Hash Engine to work with GHASH.
	 * Since it was not possible to extend HASH submodes to add GHASH,
	 * The following command is necessary in order to
	 * select GHASH (according to HW designers)
	 */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_NO_DMA(&desc[idx], 0, 0xfffff0);
	HW_DESC_SET_DOUT_NO_DMA(&desc[idx], 0, 0, 1);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
	HW_DESC_SET_AES_NOT_HASH_MODE(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_HASH_HW_GHASH);
	HW_DESC_SET_CIPHER_DO(&desc[idx], 1); //1=AES_SK RKEK
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	HW_DESC_SET_CIPHER_CONFIG1(&desc[idx], HASH_PADDING_ENABLED);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	/* Load GHASH initial STATE (which is 0). (for any hash there is an initial state) */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_CONST(&desc[idx], 0x0, AES_BLOCK_SIZE);
	HW_DESC_SET_DOUT_NO_DMA(&desc[idx], 0, 0, 1);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_HASH);
	HW_DESC_SET_AES_NOT_HASH_MODE(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_HASH_HW_GHASH);
	HW_DESC_SET_CIPHER_CONFIG1(&desc[idx], HASH_PADDING_ENABLED);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE0);
	idx++;



/////////////////////////////////   2   ////////////////////////////////////
	/* prcess(ghash) assoc data */
//	if (req->assoclen > 0)
//		ssi_aead_create_assoc_desc(req, DIN_HASH, desc, seq_size);
/////////////////////////////////   2   ////////////////////////////////////

	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     adata_dma_addr, adata_size,
			     NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_HASH);
	idx++;


/////////////////////////////////   3   ////////////////////////////////////
//	ssi_aead_gcm_setup_gctr_desc(req, desc, seq_size);
/////////////////////////////////   3   ////////////////////////////////////

	/* load key to AES*/
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_GCTR);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     key_dma_addr, key_size,
			     NS_BIT);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_size);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_KEY0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_AES);
	idx++;

	/* load AES/CTR initial CTR value inc by 2*/
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_GCTR);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_size);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     iv_inc2_dma_addr, AES_BLOCK_SIZE,
			     NS_BIT);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE1);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_AES);
	idx++;


/////////////////////////////////   4   ////////////////////////////////////
	/* process(gctr+ghash) */
//	if (req_ctx->cryptlen != 0)
//		ssi_aead_process_cipher_data_desc(req, cipher_flow_mode, desc, seq_size);
/////////////////////////////////   4   ////////////////////////////////////

	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     din_dma_addr, din_size,
			     NS_BIT);
	HW_DESC_SET_DOUT_DLLI(&desc[idx],
			      dout_dma_addr, din_size,
			      NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], cipher_flow_mode);
	idx++;


/////////////////////////////////   5   ////////////////////////////////////
//	ssi_aead_process_gcm_result_desc(req, desc, seq_size);
/////////////////////////////////   5   ////////////////////////////////////

	/* prcess(ghash) gcm_block_len */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     block_len_dma_addr, AES_BLOCK_SIZE,
			     NS_BIT);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_HASH);
	idx++;

	/* Store GHASH state after GHASH(Associated Data + Cipher +LenBlock) */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_HASH_HW_GHASH);
	HW_DESC_SET_DIN_NO_DMA(&desc[idx], 0, 0xfffff0);
	HW_DESC_SET_DOUT_DLLI(&desc[idx],
			      mac_res_dma_addr, AES_BLOCK_SIZE,
			      NS_BIT, 0);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_WRITE_STATE0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_HASH_to_DOUT);
	HW_DESC_SET_AES_NOT_HASH_MODE(&desc[idx]);
	idx++;

	/* load AES/CTR initial CTR value inc by 1*/
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_GCTR);
	HW_DESC_SET_KEY_SIZE_AES(&desc[idx], key_size);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     iv_inc1_dma_addr, AES_BLOCK_SIZE,
			     NS_BIT);
	HW_DESC_SET_CIPHER_CONFIG0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	HW_DESC_SET_SETUP_MODE(&desc[idx], SETUP_LOAD_STATE1);
	HW_DESC_SET_FLOW_MODE(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Memory Barrier */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_DIN_NO_DMA(&desc[idx], 0, 0xfffff0);
	HW_DESC_SET_DOUT_NO_DMA(&desc[idx], 0, 0, 1);
	idx++;

	/* process GCTR on stored GHASH and store MAC inplace */
	HW_DESC_INIT(&desc[idx]);
	HW_DESC_SET_CIPHER_MODE(&desc[idx], DRV_CIPHER_GCTR);
	HW_DESC_SET_DIN_TYPE(&desc[idx], DMA_DLLI,
			     mac_res_dma_addr, AES_BLOCK_SIZE,
			     NS_BIT);
	HW_DESC_SET_DOUT_DLLI(&desc[idx],
			      mac_res_dma_addr, AES_BLOCK_SIZE,
			      NS_BIT, 0);
	HW_DESC_SET_FLOW_MODE(&desc[idx], DIN_AES_DOUT);
	idx++;

	/* perform the operation - Lock HW and push sequence */
	BUG_ON(idx > FIPS_GCM_MAX_SEQ_LEN);
	rc = send_request(drvdata, &ssi_req, desc, idx, false);

	return rc;
}

ssi_fips_error_t
ssi_gcm_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer)
{
	ssi_fips_error_t error = CC_REE_FIPS_ERROR_OK;
	size_t i;
	struct fips_gcm_ctx *virt_ctx = (struct fips_gcm_ctx *)cpu_addr_buffer;

	/* set the phisical pointers */
	dma_addr_t adata_dma_addr = dma_coherent_buffer + offsetof(struct fips_gcm_ctx, adata);
	dma_addr_t key_dma_addr = dma_coherent_buffer + offsetof(struct fips_gcm_ctx, key);
	dma_addr_t hkey_dma_addr = dma_coherent_buffer + offsetof(struct fips_gcm_ctx, hkey);
	dma_addr_t din_dma_addr = dma_coherent_buffer + offsetof(struct fips_gcm_ctx, din);
	dma_addr_t dout_dma_addr = dma_coherent_buffer + offsetof(struct fips_gcm_ctx, dout);
	dma_addr_t mac_res_dma_addr = dma_coherent_buffer + offsetof(struct fips_gcm_ctx, mac_res);
	dma_addr_t len_block_dma_addr = dma_coherent_buffer + offsetof(struct fips_gcm_ctx, len_block);
	dma_addr_t iv_inc1_dma_addr = dma_coherent_buffer + offsetof(struct fips_gcm_ctx, iv_inc1);
	dma_addr_t iv_inc2_dma_addr = dma_coherent_buffer + offsetof(struct fips_gcm_ctx, iv_inc2);

	for (i = 0; i < FIPS_GCM_NUM_OF_TESTS; ++i)
	{
		FipsGcmData *gcmData = (FipsGcmData*)&FipsGcmDataTable[i];
		int rc = 0;

		memset(cpu_addr_buffer, 0, sizeof(struct fips_gcm_ctx));

		/* copy the key, adata, din data - into the allocated buffer */
		memcpy(virt_ctx->key, gcmData->key, gcmData->keySize);
		memcpy(virt_ctx->adata, gcmData->adata, gcmData->adataSize);
		memcpy(virt_ctx->din, gcmData->dataIn, gcmData->dataInSize);

		/* len_block */
		{
			__be64 len_bits;
			len_bits = cpu_to_be64(gcmData->adataSize * 8);
			memcpy(virt_ctx->len_block, &len_bits, sizeof(len_bits));
			len_bits = cpu_to_be64(gcmData->dataInSize * 8);
			memcpy(virt_ctx->len_block + 8, &len_bits, sizeof(len_bits));
		}
		/* iv_inc1, iv_inc2 */
		{
			__be32 counter = cpu_to_be32(1);
			memcpy(virt_ctx->iv_inc1, gcmData->iv, NIST_AESGCM_IV_SIZE);
			memcpy(virt_ctx->iv_inc1 + NIST_AESGCM_IV_SIZE, &counter, sizeof(counter));
			counter = cpu_to_be32(2);
			memcpy(virt_ctx->iv_inc2, gcmData->iv, NIST_AESGCM_IV_SIZE);
			memcpy(virt_ctx->iv_inc2 + NIST_AESGCM_IV_SIZE, &counter, sizeof(counter));
		}

		FIPS_DBG("ssi_gcm_fips_run_test -  (i = %d) \n", i);
		rc = ssi_gcm_fips_run_test(drvdata,
					   gcmData->direction,
					   key_dma_addr,
					   gcmData->keySize,
					   hkey_dma_addr,
					   len_block_dma_addr,
					   iv_inc1_dma_addr,
					   iv_inc2_dma_addr,
					   adata_dma_addr,
					   gcmData->adataSize,
					   din_dma_addr,
					   gcmData->dataInSize,
					   dout_dma_addr,
					   mac_res_dma_addr);
		if (rc != 0)
		{
			FIPS_LOG("ssi_gcm_fips_run_test %d returned error - rc = %d \n", i, rc);
			error = CC_REE_FIPS_ERROR_AESGCM_PUT;
			break;
		}

		if (gcmData->direction == DRV_CRYPTO_DIRECTION_ENCRYPT) {
			/* compare actual dout to expected */
			if (memcmp(virt_ctx->dout, gcmData->dataOut, gcmData->dataInSize) != 0)
			{
				FIPS_LOG("dout comparison error %d - size=%d \n", i, gcmData->dataInSize);
				FIPS_LOG("  i  expected   received \n");
				FIPS_LOG("  i  0x%08x 0x%08x \n", (size_t)gcmData->dataOut, (size_t)virt_ctx->dout);
				for (i = 0; i < gcmData->dataInSize; ++i)
				{
					FIPS_LOG("  %d    0x%02x     0x%02x \n", i, gcmData->dataOut[i], virt_ctx->dout[i]);
				}

				error = CC_REE_FIPS_ERROR_AESGCM_PUT;
				break;
			}
		}

		/* compare actual mac result to expected */
		if (memcmp(virt_ctx->mac_res, gcmData->macResOut, gcmData->tagSize) != 0)
		{
			FIPS_LOG("mac_res comparison error %d - mac_size=%d \n", i, gcmData->tagSize);
			FIPS_LOG("  i  expected   received \n");
			FIPS_LOG("  i  0x%08x 0x%08x \n", (size_t)gcmData->macResOut, (size_t)virt_ctx->mac_res);
			for (i = 0; i < gcmData->tagSize; ++i)
			{
				FIPS_LOG("  %d    0x%02x     0x%02x \n", i, gcmData->macResOut[i], virt_ctx->mac_res[i]);
			}

			error = CC_REE_FIPS_ERROR_AESGCM_PUT;
			break;
		}
	}
	return error;
}


size_t ssi_fips_max_mem_alloc_size(void)
{
	FIPS_DBG("sizeof(struct fips_cipher_ctx) %d \n", sizeof(struct fips_cipher_ctx));
	FIPS_DBG("sizeof(struct fips_cmac_ctx) %d \n", sizeof(struct fips_cmac_ctx));
	FIPS_DBG("sizeof(struct fips_hash_ctx) %d \n", sizeof(struct fips_hash_ctx));
	FIPS_DBG("sizeof(struct fips_hmac_ctx) %d \n", sizeof(struct fips_hmac_ctx));
	FIPS_DBG("sizeof(struct fips_ccm_ctx) %d \n", sizeof(struct fips_ccm_ctx));
	FIPS_DBG("sizeof(struct fips_gcm_ctx) %d \n", sizeof(struct fips_gcm_ctx));

	return sizeof(fips_ctx);
}

