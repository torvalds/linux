// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto acceleration support for Rockchip Crypto V3
 *
 * Copyright (c) 2022, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */

#include "rk_crypto_core.h"
#include "rk_crypto_v3.h"
#include "rk_crypto_v3_reg.h"
#include "rk_crypto_utils.h"

static const u32 cipher_mode2bit_mask[] = {
	[CIPHER_MODE_ECB]      = CRYPTO_ECB_FLAG,
	[CIPHER_MODE_CBC]      = CRYPTO_CBC_FLAG,
	[CIPHER_MODE_CFB]      = CRYPTO_CFB_FLAG,
	[CIPHER_MODE_OFB]      = CRYPTO_OFB_FLAG,
	[CIPHER_MODE_CTR]      = CRYPTO_CTR_FLAG,
	[CIPHER_MODE_XTS]      = CRYPTO_XTS_FLAG,
	[CIPHER_MODE_CTS]      = CRYPTO_CTS_FLAG,
	[CIPHER_MODE_CCM]      = CRYPTO_CCM_FLAG,
	[CIPHER_MODE_GCM]      = CRYPTO_GCM_FLAG,
	[CIPHER_MODE_CMAC]     = CRYPTO_CMAC_FLAG,
	[CIPHER_MODE_CBCMAC]   = CRYPTO_CBCMAC_FLAG,
};

static const u32 hash_algo2bit_mask[] = {
	[HASH_ALGO_SHA1]       = CRYPTO_HASH_SHA1_FLAG,
	[HASH_ALGO_SHA224]     = CRYPTO_HASH_SHA224_FLAG,
	[HASH_ALGO_SHA256]     = CRYPTO_HASH_SHA256_FLAG,
	[HASH_ALGO_SHA384]     = CRYPTO_HASH_SHA384_FLAG,
	[HASH_ALGO_SHA512]     = CRYPTO_HASH_SHA512_FLAG,
	[HASH_ALGO_SHA512_224] = CRYPTO_HASH_SHA512_224_FLAG,
	[HASH_ALGO_SHA512_256] = CRYPTO_HASH_SHA512_256_FLAG,
	[HASH_ALGO_MD5]        = CRYPTO_HASH_MD5_FLAG,
	[HASH_ALGO_SM3]        = CRYPTO_HASH_SM3_FLAG,
};

static const u32 hmac_algo2bit_mask[] = {
	[HASH_ALGO_SHA1]       = CRYPTO_HMAC_SHA1_FLAG,
	[HASH_ALGO_SHA256]     = CRYPTO_HMAC_SHA256_FLAG,
	[HASH_ALGO_SHA512]     = CRYPTO_HMAC_SHA512_FLAG,
	[HASH_ALGO_MD5]        = CRYPTO_HMAC_MD5_FLAG,
	[HASH_ALGO_SM3]        = CRYPTO_HMAC_SM3_FLAG,
};

static const char * const crypto_v3_rsts[] = {
	 "crypto-rst",
};

static struct rk_crypto_algt *crypto_v3_algs[] = {
	&rk_v3_ecb_sm4_alg,		/* ecb(sm4) */
	&rk_v3_cbc_sm4_alg,		/* cbc(sm4) */
	&rk_v3_xts_sm4_alg,		/* xts(sm4) */
	&rk_v3_cfb_sm4_alg,		/* cfb(sm4) */
	&rk_v3_ofb_sm4_alg,		/* ofb(sm4) */
	&rk_v3_ctr_sm4_alg,		/* ctr(sm4) */
	&rk_v3_gcm_sm4_alg,		/* ctr(sm4) */

	&rk_v3_ecb_aes_alg,		/* ecb(aes) */
	&rk_v3_cbc_aes_alg,		/* cbc(aes) */
	&rk_v3_xts_aes_alg,		/* xts(aes) */
	&rk_v3_cfb_aes_alg,		/* cfb(aes) */
	&rk_v3_ofb_aes_alg,		/* ofb(aes) */
	&rk_v3_ctr_aes_alg,		/* ctr(aes) */
	&rk_v3_gcm_aes_alg,		/* gcm(aes) */

	&rk_v3_ecb_des_alg,		/* ecb(des) */
	&rk_v3_cbc_des_alg,		/* cbc(des) */
	&rk_v3_cfb_des_alg,		/* cfb(des) */
	&rk_v3_ofb_des_alg,		/* ofb(des) */

	&rk_v3_ecb_des3_ede_alg,	/* ecb(des3_ede) */
	&rk_v3_cbc_des3_ede_alg,	/* cbc(des3_ede) */
	&rk_v3_cfb_des3_ede_alg,	/* cfb(des3_ede) */
	&rk_v3_ofb_des3_ede_alg,	/* ofb(des3_ede) */

	&rk_v3_ahash_sha1,		/* sha1 */
	&rk_v3_ahash_sha224,		/* sha224 */
	&rk_v3_ahash_sha256,		/* sha256 */
	&rk_v3_ahash_sha384,		/* sha384 */
	&rk_v3_ahash_sha512,		/* sha512 */
	&rk_v3_ahash_md5,		/* md5 */
	&rk_v3_ahash_sm3,		/* sm3 */

	&rk_v3_hmac_sha1,		/* hmac(sha1) */
	&rk_v3_hmac_sha256,		/* hmac(sha256) */
	&rk_v3_hmac_sha512,		/* hmac(sha512) */
	&rk_v3_hmac_md5,		/* hmac(md5) */
	&rk_v3_hmac_sm3,		/* hmac(sm3) */

	/* Shared v2 version implementation */
	&rk_v2_asym_rsa,		/* rsa */
};

static bool rk_is_cipher_support(struct rk_crypto_dev *rk_dev, u32 algo, u32 mode, u32 key_len)
{
	u32 version = 0;
	u32 mask = 0;
	bool key_len_valid = true;

	switch (algo) {
	case CIPHER_ALGO_DES:
	case CIPHER_ALGO_DES3_EDE:
		version = CRYPTO_READ(rk_dev, CRYPTO_DES_VERSION);

		if (key_len == 8)
			key_len_valid = true;
		else if (key_len == 16 || key_len == 24)
			key_len_valid = version & CRYPTO_TDES_FLAG;
		else
			key_len_valid = false;
		break;
	case CIPHER_ALGO_AES:
		version = CRYPTO_READ(rk_dev, CRYPTO_AES_VERSION);

		if (key_len == 16)
			key_len_valid = version & CRYPTO_AES128_FLAG;
		else if (key_len == 24)
			key_len_valid = version & CRYPTO_AES192_FLAG;
		else if (key_len == 32)
			key_len_valid = version & CRYPTO_AES256_FLAG;
		else
			key_len_valid = false;
		break;
	case CIPHER_ALGO_SM4:
		version = CRYPTO_READ(rk_dev, CRYPTO_SM4_VERSION);

		key_len_valid = (key_len == SM4_KEY_SIZE) ? true : false;
		break;
	default:
		return false;
	}

	mask = cipher_mode2bit_mask[mode];

	if (key_len == 0)
		key_len_valid = true;

	return (version & mask) && key_len_valid;
}

static bool rk_is_hash_support(struct rk_crypto_dev *rk_dev, u32 algo, u32 type)
{
	u32 version = 0;
	u32 mask = 0;

	if (type == ALG_TYPE_HMAC) {
		version = CRYPTO_READ(rk_dev, CRYPTO_HMAC_VERSION);
		mask    = hmac_algo2bit_mask[algo];
	} else if (type == ALG_TYPE_HASH) {
		version = CRYPTO_READ(rk_dev, CRYPTO_HASH_VERSION);
		mask    = hash_algo2bit_mask[algo];
	} else {
		return false;
	}

	return version & mask;
}

int rk_hw_crypto_v3_init(struct device *dev, void *hw_info)
{
	struct rk_hw_crypto_v3_info *info =
		(struct rk_hw_crypto_v3_info *)hw_info;

	if (!dev || !hw_info)
		return -EINVAL;

	memset(info, 0x00, sizeof(*info));

	return rk_crypto_hw_desc_alloc(dev, &info->hw_desc);
}

void rk_hw_crypto_v3_deinit(struct device *dev, void *hw_info)
{
	struct rk_hw_crypto_v3_info *info =
		(struct rk_hw_crypto_v3_info *)hw_info;

	if (!dev || !hw_info)
		return;

	rk_crypto_hw_desc_free(&info->hw_desc);
}

const char * const *rk_hw_crypto_v3_get_rsts(uint32_t *num)
{
	*num = ARRAY_SIZE(crypto_v3_rsts);

	return crypto_v3_rsts;
}

struct rk_crypto_algt **rk_hw_crypto_v3_get_algts(uint32_t *num)
{
	*num = ARRAY_SIZE(crypto_v3_algs);

	return crypto_v3_algs;
}

bool rk_hw_crypto_v3_algo_valid(struct rk_crypto_dev *rk_dev, struct rk_crypto_algt *aglt)
{
	if (aglt->type == ALG_TYPE_CIPHER || aglt->type == ALG_TYPE_AEAD) {
		CRYPTO_TRACE("CIPHER");
		return rk_is_cipher_support(rk_dev, aglt->algo, aglt->mode, 0);
	} else if (aglt->type == ALG_TYPE_HASH || aglt->type == ALG_TYPE_HMAC) {
		CRYPTO_TRACE("HASH/HMAC");
		return rk_is_hash_support(rk_dev, aglt->algo, aglt->type);
	} else if (aglt->type == ALG_TYPE_ASYM) {
		CRYPTO_TRACE("RSA");
		return true;
	} else {
		return false;
	}
}

